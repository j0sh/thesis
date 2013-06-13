// processing changedetection.net dataset
#include <stdio.h>
#include <stdlib.h>

#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui_c.h>

#include "kdtree.h"
#include "prop.h"
#include "wht.h"

#define XY_TO_INT(x, y) (((y) << 16) | (x))
#define XY_TO_X(x) ((x)&((1<<16)-1))
#define XY_TO_Y(y) ((y)>>16)

static void print_usage(char **argv)
{
    printf("Usage: %s <path> <start> <end>\n", argv[0]);
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
    unsigned *order_p0 = build_path(plane_coeffs[0], 8);
    unsigned *order_p1 = build_path(plane_coeffs[1], 8);
    unsigned *order_p2 = build_path(plane_coeffs[2], 8);

    cvSplit(img, b, g, r, NULL);

    wht2d(b, trans);
    quantize(trans, plane_coeffs[0], 8, order_p0, buf, dim);

    wht2d(g, trans);
    quantize(trans, plane_coeffs[1], 8, order_p1,
        buf+plane_coeffs[0], dim);

    wht2d(r, trans);
    quantize(trans, plane_coeffs[2], 8, order_p2,
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
    unsigned *order_p0 = build_path(plane_coeffs[0], 8);
    unsigned *order_p1 = build_path(plane_coeffs[1], 8);
    unsigned *order_p2 = build_path(plane_coeffs[2], 8);

    memset(trans->imageData, 0, trans->imageSize);
    dequantize(trans, plane_coeffs[0], order_p0, 8, coeffs, dim);
    iwht2d(trans, g);
    memset(trans->imageData, 0, trans->imageSize);
    dequantize(trans, plane_coeffs[1], order_p1, 8,
        coeffs+plane_coeffs[0], dim);
    iwht2d(trans, b);
    memset(trans->imageData, 0, trans->imageSize);
    dequantize(trans, plane_coeffs[2], order_p2, 8,
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

static IplImage *alignedImageFrom(char *file, int align)
{
    IplImage *pre = cvLoadImage(file, CV_LOAD_IMAGE_COLOR);
    IplImage *img = alignedImage(cvGetSize(pre), pre->depth, pre->nChannels, align);
    char *pre_data = pre->imageData;
    char *img_data = img->imageData;
    int i;
    for (i = 0; i < pre->height; i++) {
        memcpy(img_data, pre_data, pre->widthStep);
        img_data += img->widthStep;
        pre_data += pre->widthStep;
    }
    cvReleaseImage(&pre);
    return img;
}

static int plane_coeffs[] = {2, 9, 5};
IplImage *recon_g, *diff_g;
static void process(kd_tree *kdt, IplImage *bkg, IplImage *img)
{
    int *imgc = block_coeffs(img, plane_coeffs);
    CvSize img_blks = {(img->width/8)+7, (img->height/8)+7};
    IplImage *xy = prop_match_complete(kdt, imgc, bkg, img_blks);
    IplImage *rev = splat(imgc, cvGetSize(img), plane_coeffs);
    xy2blks_special(xy, bkg, recon_g, 8);
    cvAbsDiff(recon_g, rev, diff_g);
    //IplImage *xy = prop_match(bkg, img);
    //xy2img(xy, bkg, recon_g);
    //cvAbsDiff(recon_g, img, diff_g);
    cvShowImage("img", recon_g);
    cvShowImage("diff", diff_g);
    free(imgc);
    cvReleaseImage(&xy);
}

int main(int argc, char **argv)
{
    if (argc < 4) print_usage(argv);
    char *path = argv[1];
    int start = atoi(argv[2]), end = atoi(argv[3]), i, *bkgc;
    IplImage *bkg = alignedImageFrom(mkname(path, start-1), 8);
    int dim = plane_coeffs[0] + plane_coeffs[1] + plane_coeffs[2];
    CvSize bsz = cvGetSize(bkg);
    int w = bsz.width - 8 + 1, h = bsz.height - 8 + 1, sz = w*h;
    kd_tree kdt;

    printf("cd: %s %d %d\n", path, start, end);
    memset(&kdt, 0, sizeof(kd_tree));
    prop_coeffs(bkg, plane_coeffs, &bkgc);
    kdt_new(&kdt, bkgc, sz, dim);
    recon_g = cvCreateImage(bsz, bkg->depth, bkg->nChannels);
    diff_g  = cvCreateImage(bsz, bkg->depth, bkg->nChannels);

    for (i = start; i < end; i++) {
        IplImage *img = alignedImageFrom(mkname(path, i), 8);
        process(&kdt, bkg, img);
        if ((cvWaitKey(1)&255)==27)break; // esc
        cvReleaseImage(&img);
    }
    return 0;
}
