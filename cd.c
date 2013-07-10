// processing changedetection.net dataset
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui_c.h>

#include "kdtree.h"
#include "prop.h"
#include "wht.h"
#include "sal.h"
#include "gck.h"

#define XY_TO_INT(x, y) (((y) << 16) | (x))
#define XY_TO_X(x) ((x)&((1<<16)-1))
#define XY_TO_Y(y) ((y)>>16)

#define KERNS 8

#include <sys/time.h>
static inline double get_time()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec * 1e-6;
}

static void print_usage(char **argv)
{
    printf("Usage: %s <path> <start> <end> <bkg_name> <diff_name>\n",
        argv[0]);
    exit(1);
}

static char fname[1024];
static char* mkname(char *path, int i)
{
    snprintf(fname, sizeof(fname), "%s/in%06d.jpg", path, i);
    return fname;
}

static int bitrev(unsigned int n, unsigned int bits)
{
    unsigned int i, nrev;// nrev will store the bit-reversed pattern
    int N = 1<<bits;     // find N: shift left 1 by the number of bits
    nrev = n;
    for(i=1; i<bits; i++)
    {
        n >>= 1;
        nrev <<= 1;
        nrev |= n & 1;   // give LSB of n to nrev
    }
    nrev &= N-1;         // clear all bits more significant than N-1
    return nrev;
}

static int gc(int a)
{
    return (a >> 1) ^ a;
}

static int nat2seq(int n, int bits)
{
    // go from natural ordering to sequency ordering:
    // first take gray code encoding, then reverse those bits
    return bitrev(gc(n), bits);
}

unsigned* build_path(int n, int kern)
{
    int i = 0, k = 0, bases = 0, b = log2(kern);
    unsigned *order = malloc(n*sizeof(unsigned));
    if (!order) {
        fprintf(stderr, "quantize: unable to alloc!\n");
        return NULL;
    }
    while (bases < n) {
        int m, y, x;
        if (i < kern) {
            y = k;
            x = 0;
        } else {
            y = kern - 1;
            x = kern - k - 1;
        }
        for (m = 0; m < k+1; m++) {
            order[bases] = XY_TO_INT(nat2seq(x, b), nat2seq(y, b));
            bases++;
            x++;
            y--;
            if (n == bases) break;
        }
        i++;
        k += i < kern ? 1 : -1;
    }
    return order;
}

void quantize(IplImage *img, int n, int kern, unsigned *order, int *buf, int dim)
{
    int i = 0, j = 0, k = 0;
    int stride = img->widthStep/sizeof(int16_t), *qd = buf;
    int16_t *data = (int16_t*)img->imageData;
    if (!n) memset(data, 0, img->imageSize);
    if (n > dim) {
        fprintf(stderr, "quantize: n must be < elems\n");
        return;
    }

    for (i = 0; i < img->height; i+= kern) {
        for (j = 0; j < img->width; j+= kern) {
            int16_t *block = data+(i*stride+j);
            int *ql = qd;
            for (k = 0; k < n; k++) {
                int z = order[k];
                int x = XY_TO_X(z);
                int y = XY_TO_Y(z);
                ql[k] = *(block+(y*stride+x));
            }
            qd += dim;
        }
    }
}

static void dequantize(IplImage *img, int n, unsigned *order,
    int kern, int *buf, int dim)
{
    int i, j, k, *qd = buf;
    int stride = img->widthStep/sizeof(int16_t);
    int16_t *data = (int16_t*)img->imageData;
    for (i = 0; i < img->height; i+= kern) {
        for (j = 0; j < img->width; j+= kern) {
            int16_t *block = data+i*stride+j;
            int *ql = qd;
            for (k = 0; k < n; k++) {
                int z = order[k];
                int x = XY_TO_X(z);
                int y = XY_TO_Y(z);
                *(block+y*stride+x) = *ql++;
            }
            qd += dim;
        }
    }
}

static int* block_coeffs(IplImage *img, int* plane_coeffs) {
    CvSize size = cvGetSize(img);
    IplImage *b = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *g = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *r = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *trans = cvCreateImage(size, IPL_DEPTH_16S, 1);
    int dim = plane_coeffs[0] + plane_coeffs[1] + plane_coeffs[2];
    int sz = size.width*size.height/64*dim;
    int *buf = malloc(sizeof(int)*sz);
    unsigned *order_p0 = build_path(plane_coeffs[0], KERNS);
    unsigned *order_p1 = build_path(plane_coeffs[1], KERNS);
    unsigned *order_p2 = build_path(plane_coeffs[2], KERNS);

    cvSplit(img, b, g, r, NULL);

    wht2d(b, trans);
    quantize(trans, plane_coeffs[0], KERNS, order_p0, buf, dim);

    wht2d(g, trans);
    quantize(trans, plane_coeffs[1], KERNS, order_p1,
        buf+plane_coeffs[0], dim);

    wht2d(r, trans);
    quantize(trans, plane_coeffs[2], KERNS, order_p2,
        buf+plane_coeffs[0]+plane_coeffs[1], dim);

    cvReleaseImage(&trans);
    cvReleaseImage(&b);
    cvReleaseImage(&g);
    cvReleaseImage(&r);
    free(order_p0);
    free(order_p1);
    free(order_p2);

    return buf;
}

static IplImage* splat(int *coeffs, CvSize size, int *plane_coeffs)
{
    IplImage *g = cvCreateImage(size, IPL_DEPTH_16S, 1);
    IplImage *b = cvCreateImage(size, IPL_DEPTH_16S, 1);
    IplImage *r = cvCreateImage(size, IPL_DEPTH_16S, 1);
    IplImage *rgb = cvCreateImage(size, IPL_DEPTH_16S, 3);
    IplImage *img = cvCreateImage(size, IPL_DEPTH_8U, 3);
    IplImage *trans = cvCreateImage(size, IPL_DEPTH_16S, 1);
    int dim = plane_coeffs[0] + plane_coeffs[1] + plane_coeffs[2];
    unsigned *order_p0 = build_path(plane_coeffs[0], KERNS);
    unsigned *order_p1 = build_path(plane_coeffs[1], KERNS);
    unsigned *order_p2 = build_path(plane_coeffs[2], KERNS);

    memset(trans->imageData, 0, trans->imageSize);
    dequantize(trans, plane_coeffs[0], order_p0, KERNS, coeffs, dim);
    iwht2d(trans, g);
    memset(trans->imageData, 0, trans->imageSize);
    dequantize(trans, plane_coeffs[1], order_p1, KERNS,
        coeffs+plane_coeffs[0], dim);
    iwht2d(trans, b);
    memset(trans->imageData, 0, trans->imageSize);
    dequantize(trans, plane_coeffs[2], order_p2, KERNS,
        coeffs+plane_coeffs[0]+plane_coeffs[1], dim);
    iwht2d(trans, r);

    cvMerge(g, b, r, NULL, rgb);
    cvConvertScale(rgb, img, 1, 0);

    cvReleaseImage(&g);
    cvReleaseImage(&b);
    cvReleaseImage(&r);
    cvReleaseImage(&rgb);
    cvReleaseImage(&trans);
    free(order_p0);
    free(order_p1);
    free(order_p2);
    return img;
}

static void xy2blks_special(IplImage *xy, IplImage *src, IplImage *recon, int kernsz)
{
    int w = xy->width - 7, h = xy->height - 7, i, j;
    int xystride = xy->widthStep/sizeof(int32_t);
    int rstride = recon->widthStep;
    int stride = src->widthStep;
    int32_t *xydata = (int32_t*)xy->imageData;
    uint8_t *rdata = (uint8_t*)recon->imageData;
    uint8_t *data = (uint8_t*)src->imageData;
    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            int v = xydata[i*xystride + j], k;
            int x = XY_TO_X(v), y = XY_TO_Y(v);
            for (k = 0; k < kernsz*kernsz; k++) {
                int xoff = k % kernsz, yoff = k / kernsz;
                int rd = (i*kernsz + yoff)*rstride + (j*kernsz + xoff)*3;
                int dd = (y + yoff)*stride + (x + xoff)*3;
                *(rdata + rd + 0) = *(data + dd + 0);
                *(rdata + rd + 1) = *(data + dd + 1);
                *(rdata + rd + 2) = *(data + dd + 2);
            }
        }
    }
}

static void xy2img(IplImage *xy, IplImage *img, IplImage *recon)
{
    int w = xy->width, h = xy->height, i, j;
    int xystride = xy->widthStep/sizeof(int32_t);
    int rstride = recon->widthStep;
    int stride = img->widthStep;
    int32_t *xydata = (int32_t*)xy->imageData;
    uint8_t *rdata = (uint8_t*)recon->imageData;
    uint8_t *data = (uint8_t*)img->imageData;

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            int v = xydata[i*xystride + j];
            int x = XY_TO_X(v), y = XY_TO_Y(v);
            int rd = i*rstride + j*3, dd = y*stride + x*3;
            *(rdata + rd + 0) = *(data + dd + 0);
            *(rdata + rd + 1) = *(data + dd + 1);
            *(rdata + rd + 2) = *(data + dd + 2);
        }
    }
}

static IplImage *alignedImage(CvSize dim, int depth, int chan, int align)
{
    int w = dim.width, h = dim.height;
    int dx = align - (w % align), dy =  align - (h % align);
    w += (dx != align) * dx;
    h += (dy != align) * dy; // really unnecessary
    CvSize s = {w, h};
    return cvCreateImage(s, depth, chan);
}

int orig_w = -1, orig_h = -1;
static IplImage *alignedImageFrom(char *file, int align)
{
    IplImage *pre = cvLoadImage(file, CV_LOAD_IMAGE_COLOR);
    IplImage *img = alignedImage(cvGetSize(pre), pre->depth, pre->nChannels, align);
    char *pre_data = pre->imageData;
    char *img_data = img->imageData;
    int i;
    orig_w = pre->width;
    orig_h = pre->height;
    for (i = 0; i < pre->height; i++) {
        memcpy(img_data, pre_data, pre->widthStep);
        img_data += img->widthStep;
        pre_data += pre->widthStep;
    }
    cvReleaseImage(&pre);
    return img;
}

static int plane_coeffs[] = {2, 9, 5};
IplImage *recon_g, *diff_g, *lap_g, *gray_g, *mask_g, *bkg_g;
static void init_g(IplImage *bkg)
{
    CvSize bsz = cvGetSize(bkg);
    recon_g = cvCreateImage(bsz, bkg->depth, bkg->nChannels);
    diff_g  = cvCreateImage(bsz, bkg->depth, bkg->nChannels);
    bkg_g   = cvCreateImage(bsz, bkg->depth, bkg->nChannels);
    gray_g  = cvCreateImage(bsz, bkg->depth, 1);
    mask_g  = cvCreateImage(bsz, bkg->depth, 1);
    lap_g   = cvCreateImage(bsz, IPL_DEPTH_32F, bkg->nChannels);
}

static void free_g()
{
    cvReleaseImage(&recon_g);
    cvReleaseImage(&diff_g);
    cvReleaseImage(&gray_g);
    cvReleaseImage(&mask_g);
    cvReleaseImage(&lap_g);
    cvReleaseImage(&bkg_g);
}

static void process(kd_tree *kdt, IplImage *bkg, IplImage *diff,
    IplImage *img, char *outname)
{
    //int *imgc = block_coeffs(img, plane_coeffs);
    cvAbsDiff(img, bkg, diff_g);
    int *imgc = block_coeffs(diff_g, plane_coeffs);
    CvSize blksz = {(img->width/8)+7, (img->height/8)+7};
    IplImage *xy = prop_match_complete(kdt, imgc, bkg, blksz);
    IplImage *rev = splat(imgc, cvGetSize(img), plane_coeffs);
    xy2blks_special(xy, diff, recon_g, 8);
    cvAbsDiff(rev, recon_g, diff_g);
    cvReleaseImage(&rev);
    /*int *imgc;
    prop_coeffs(diff_g, plane_coeffs, &imgc);
    CvSize blksz = cvGetSize(bkg);
    IplImage *xy = prop_match_complete(kdt, imgc, bkg, blksz);
    xy2img(xy, diff, recon_g);
    cvAbsDiff(diff_g, recon_g, diff_g);*/
    //cvShowImage("diff_g before mul", diff_g);
    //cvAbsDiff(diff_g, diff, diff_g);
    //cvMul(diff_g, idiff, diff_g, 1);
    cvCvtColor(diff_g, gray_g, CV_BGR2GRAY);

    // full pixel dc
    //IplImage *dc = make_dc(gray_g);
    IplImage *mask = cvCreateImage(cvGetSize(img), IPL_DEPTH_8U, 1);
    IplImage *dc_f = cvCreateImage(cvGetSize(img), IPL_DEPTH_32F, 1);
    int *graydc = gck_calc_2d((uint8_t*)gray_g->imageData, img->width, img->height, KERNS, 1);
    IplImage *dc = cvCreateImageHeader(cvGetSize(img), IPL_DEPTH_32S, 1);
    int step = img->width + KERNS - 1;
    cvSetData(dc, graydc, step*sizeof(int));
    cvConvertScale(dc, dc_f, 1/255.0, 0);
    cvThreshold(gray_g, mask, 25, 255.0, CV_THRESH_BINARY);

    mask->width = orig_w;
    mask->height = orig_h;
    if (*outname) cvSaveImage(outname, mask, 0);

    /*double min = 0, max = 0;
    cvMinMaxLoc(dc, &min, &max, NULL, NULL, NULL);
    printf("min: %3f max: %3f\n", min, max);
    CvScalar scalar = cvRealScalar(-min);
    cvAddS(dc, scalar, dc_f, NULL);
    cvConvertScale(dc_f, dc_f, 1.0/(max - min), 0);*/

    // macroblock based counts
    //cvAdaptiveThreshold(gray_g, mask_g, 255, CV_ADAPTIVE_THRESH_GAUSSIAN_C, CV_THRESH_BINARY, 3, 0);

    //cvSmooth(mask_g, mask_g, CV_GAUSSIAN, 9, 9, 0, 0);
    //cvSmooth(diff_g, diff_g, CV_GAUSSIAN, 5, 5, 0, 0);
    //cvLaplace(diff_g, lap_g, 3);
    //IplImage *xy = prop_match(bkg, img);
    //xy2img(xy, bkg, recon_g);
    //cvAbsDiff(recon_g, img, diff_g);
    /*cvShowImage("recon", recon_g);
    //cvShowImage("diff", rev);
    cvShowImage("diff_g", diff_g);
    cvShowImage("img", img);
    cvShowImage("gray", gray_g);
    cvShowImage("dc float", dc_f);
    cvShowImage("mask", mask);
    cvShowImage("dc", dc);*/
    free(imgc);
    cvReleaseImage(&xy);
    cvReleaseImageHeader(&dc);
    free(graydc);
    cvReleaseImage(&mask);
    cvReleaseImage(&dc_f);
}

CvHistogram *make_hist(IplImage *img)
{
    int numBins = 256;
    float range[] = {0, 255};
    float *ranges[] = { range };
    CvHistogram *hist = cvCreateHist(1, &numBins, CV_HIST_ARRAY, ranges, 1);
    cvCalcHist(&img, hist, 0, 0);
    return hist;
}

static void test(IplImage *b)
{
    CvSize s = cvGetSize(b);
    //s.width /= 2;
    //s.height /= 2;
    IplImage *rsz = alignedImage(s, b->depth, b->nChannels, 8);
    cvResize(b, rsz, CV_INTER_CUBIC);
    IplImage *sal = saliency(rsz);
    cvShowImage("saliency", sal);
    cvReleaseImage(&rsz);
    cvReleaseImage(&sal);
    return;
}

typedef struct thread_ctx {
    int start;
    int end;
    int nb;
    char *path;
    char *outfile;
    IplImage *bkg;
    IplImage *diff;
    kd_tree *kdt;
} thread_ctx;

static void* run_thr(void *arg)
{
    thread_ctx *ctx = (thread_ctx*)arg;
    double t = 0;
    int i, start = ctx->start, end = ctx->end, nb = ctx->nb;
    char outname[1024], *path = ctx->path, *outfile = ctx->outfile;
    memset(outname, '\0', sizeof(outname));
    printf("starting thread %d\n", start);
    for (i = start; i <= end; i+= nb) {
        IplImage *img = alignedImageFrom(mkname(path, i), 8);
        double start = get_time();
        if (outfile) snprintf(outname, sizeof(outname), "%s/bin%06d.png", outfile, i);
        process(ctx->kdt, ctx->bkg, ctx->diff, img, outname);
        t += (get_time() - start);
        if ((cvWaitKey(1)&255)==27)break; // esc
        cvReleaseImage(&img);
    }
    printf("avg: %fms\n", t/(i-start)*1000);
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 6) print_usage(argv);
    char *path = argv[1];
    int start = atoi(argv[2]), end = atoi(argv[3]), i, *bkgc;
    //IplImage *bkg = alignedImageFrom(mkname(path, 1), 8);
    IplImage *bkg = alignedImageFrom(argv[4], 8);
    int dim = plane_coeffs[0] + plane_coeffs[1] + plane_coeffs[2];
    CvSize bsz = cvGetSize(bkg);
    IplImage *d8 = alignedImageFrom(argv[5], 8);
    //IplImage *d8 = alignedImageFrom(mkname(path, 1), 8);
    int w = bsz.width - 8 + 1, h = bsz.height - 8 + 1, sz = w*h;
    kd_tree kdt;

    printf("cd: %s %d %d %s %s\n", path, start, end, argv[4], argv[5]);
    init_g(bkg);
    memset(&kdt, 0, sizeof(kd_tree));

    /*
    IplImage *b32 = cvCreateImage(bsz, IPL_DEPTH_32F, bkg->nChannels);
    IplImage *i32 = cvCreateImage(bsz, IPL_DEPTH_32F, bkg->nChannels);
    IplImage *d32 = cvCreateImage(bsz, IPL_DEPTH_32F, bkg->nChannels);
    IplImage *diff= cvCreateImage(bsz, IPL_DEPTH_32F, bkg->nChannels);
    cvXor(b32, b32, b32, NULL);
    cvXor(d32, d32, d32, NULL);
    cvConvertScale(bkg, b32, 1/255.0, 0);
    for (i = 1; i < start; i++) {
        IplImage *img = alignedImageFrom(mkname(path, i), 8);
        cvConvertScale(img, i32, 1/256.0, 0);
        cvAbsDiff(i32, b32, diff);
        cvRunningAvg(diff, d32, 1.0/start, NULL);
        cvRunningAvg(i32, b32, 1.0/start, NULL);
        cvReleaseImage(&img);
        cvShowImage("avg diff", d32);
        cvWaitKey(1);
        printf("i: %d\r", i);
    }
    cvConvertScale(b32, bkg, 255, 0);
    cvReleaseImage(&b32);
    cvReleaseImage(&i32);
    if (argc >= 6) {
        cvSaveImage(argv[4], bkg, 0);
        cvConvertScale(d32, d8, 255, 0);
        cvSaveImage(argv[5], d8, 0); // difference image
        return 0;
    }
    */

    int *imgc = block_coeffs(d8, plane_coeffs);
    IplImage *rev = splat(imgc, bsz, plane_coeffs);
    free(imgc);
    cvReleaseImage(&rev);
    prop_coeffs(bkg, plane_coeffs, &bkgc);
    kdt_new(&kdt, bkgc, sz, dim);

    /*thread_ctx ctxs[3];
    pthread_t thrs[sizeof(ctxs)/sizeof(thread_ctx)];
    for (i = 0; i < (int)(sizeof(ctxs)/sizeof(thread_ctx)); i++) {
        thread_ctx *ctx = &ctxs[i];
        ctx->start = i+1;
        ctx->end = end;
        ctx->nb = sizeof(ctxs)/sizeof(thread_ctx);
        ctx->path = path;
        ctx->outfile = argc >= 7 ? argv[6] : NULL;
        ctx->bkg = bkg;
        ctx->diff = d8;
        ctx->kdt = &kdt;
        pthread_create(&thrs[i], NULL, run_thr, ctx);
    }
    for (i = 0; i < (int)(sizeof(ctxs)/sizeof(thread_ctx)); i++) {
        pthread_join(thrs[i], NULL);
    }
    printf("all done!\n");*/

    double t;
    char outname[1024];
    memset(outname, '\0', sizeof(outname));
    for (i = start; i <= end; i++) {
        IplImage *img = alignedImageFrom(mkname(path, i), 8);
        double start = get_time();
        if (argc >= 7) snprintf(outname, sizeof(outname), "%s/bin%06d.png", argv[6], i);
        //process(&kdt, bkg, d8, img, outname);
        //test(img);
        cvShowImage("image", img);
        t += (get_time() - start);
        if ((cvWaitKey(1)&255)==27)break; // esc
        cvReleaseImage(&img);
    }
    //free_g();
    kdt_free(&kdt);
    free(bkgc);
    cvReleaseImage(&bkg);
    cvReleaseImage(&d8);
    return 0;
}
