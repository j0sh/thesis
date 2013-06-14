// saliency detection.
// loosely based on Context-Aware Saliency Detection by goferman et al
// with kd-trees as the AK-NN algorithm

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui_c.h>

#include "kdtree.h"
#include "prop.h"
 
#define XY_TO_INT(x, y) (((y) << 16) | (x))
#define XY_TO_X(x) ((x)&((1<<16)-1))
#define XY_TO_Y(y) ((y)>>16)

static void print_usage(char **argv)
{
    printf("Usage: %s <path>\n", argv[0]);
    exit(1);
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

static char *name(char *fname)
{
    int i, len = strlen(fname);
    for (i = len-1; i >= 0; i--) {
        if (fname[i] == '/') return fname+i+1;
    }
    return fname;
}

static double l2_color(int *a, int *b, int k)
{
    int i, dist = 0;
    for (i = 0; i < k; i++) {
        int diff = a[i] - b[i];
        dist += diff*diff;
    }
    return sqrt(dist)/sqrt(255*255*k);
}

static double l2_pos(kd_tree *t, int *a, int *b, int w)
{
    int ap = (a - t->start)/t->k;
    int bp = (b - t->start)/t->k;
    int ax = ap % w, ay = ap / w;
    int bx = bp % w, by = bp / w;
    int dx = ax - bx, dy = ay - by;
    return sqrt(dx*dx + dy*dy);
}

static float compute_dist(kd_tree *t, kd_node *n, int *v, int w)
{
    int i; double dist = 0;
    for (i = 0; i < n->nb; i++) {
        int *u = n->value[i];
        double dcolor = l2_color(u, v, t->k);
        double dpos = l2_pos(t, u, v, w);
        dist += dcolor / (1 + dpos);
    }
    return dist;
}

static void swap2(double *best, kd_node **bestn)
{
    float d = best[0];
    best[0] = best[1];
    best[1] = d;

    kd_node *n = bestn[0];
    bestn[0] = bestn[1];
    bestn[1] = n;
}

static void compute_node(kd_tree *t, double *best, kd_node **bestn,
    kd_node *n, int *imgc, int *nb, double *dist, int w)
{
    double d = compute_dist(t, n, imgc, w);
    *nb += n->nb;
    *dist += d;
    if (d < best[0]) {
        bestn[0] = n;
        best[0] = d;
        if (best[0] < best[1]) swap2(best, bestn);
    }
}

static float compute(kd_tree *t, kd_node **nodes, int *imgc,
    int i, int w)
{
    kd_node *n = kdt_query(t, imgc), *bestn[] = {n, n};
    double dist = compute_dist(t, n, imgc, w), best[] = {dist, dist};
    int nb = n->nb, x = i % w, y = i / w;

    if (!x) goto try_top;
    compute_node(t, best, bestn, nodes[-1], imgc, &nb, &dist, w);
    compute_node(t, best, bestn, nodes[-2], imgc, &nb, &dist, w);

try_top:
    if (!y) goto compute_finish;
    compute_node(t, best, bestn, nodes[0], imgc, &nb, &dist, w);
    compute_node(t, best, bestn, nodes[1], imgc, &nb, &dist, w);

compute_finish:
    nodes[0] = bestn[1];  // max-heap; this is the better match
    nodes[1] = bestn[0];
    return 1 - exp(-dist/nb);
}

static IplImage *salmap(IplImage *img, int free_img)
{
    int plane_coeffs[] = {2, 9, 5}, i, *imgc, *c;
    int dim = plane_coeffs[0] + plane_coeffs[1] + plane_coeffs[2];
    CvSize isz = cvGetSize(img);
    int w = isz.width - 8 + 1, h = isz.height - 8 + 1, sz = w*h;
    CvSize salsz = {w, h};
    IplImage *sal = cvCreateImage(salsz, IPL_DEPTH_32F, 1);
    int salstride = sal->widthStep/sizeof(float);
    kd_tree kdt;
    kd_node **nodes = malloc(w*2*sizeof(kd_node*));

    memset(&kdt, 0, sizeof(kd_tree));
    prop_coeffs(img, plane_coeffs, &imgc);
    kdt_new_overlap(&kdt, imgc, sz, dim, 0.5, 8, w);
    c = imgc;

    for (i = 0; i < sz; i++) {
        int x = i % w, y = i / w;
        float *data = (float*)sal->imageData + (y * salstride + x);
        *data = compute(&kdt, nodes+x*2, imgc, i, w);
        imgc += kdt.k;
    }

    kdt_free(&kdt);
    free(nodes);
    free(c);
    if (free_img) cvReleaseImage(&img);
    return sal;
}

static IplImage* resize(IplImage *img, float scale)
{
    CvSize sz = {img->width * scale, img->height * scale};
    IplImage *ret = alignedImage(sz, img->depth, img->nChannels, 8);
    cvResize(img, ret, CV_INTER_CUBIC);
    return ret;
}

static IplImage *resize2(IplImage *img, CvSize sz)
{
    IplImage *ret = alignedImage(sz, img->depth, img->nChannels, 8);
    cvResize(img, ret, CV_INTER_CUBIC);
    return ret;
}

static void write2file(char *fname, IplImage *img, CvSize sz)
{
    IplImage *res = resize2(img, sz);
    IplImage *out = alignedImage(sz, IPL_DEPTH_8U, img->nChannels, 8);
    cvConvertScale(res, out, 255, 0);
    cvSaveImage(fname, out, 0);
}

static IplImage* combine(IplImage** maps, int nb)
{
    int i;
    IplImage *img = maps[0];
    CvSize sz = cvGetSize(img);
    IplImage *sum = alignedImage(sz, img->depth, img->nChannels, 8);
    IplImage *mean = alignedImage(sz, img->depth, img->nChannels, 8);
    IplImage *thr = alignedImage(sz, img->depth, img->nChannels, 8);
    cvXor(sum, sum, sum, NULL);
    for (i = 0; i < nb; i++) {
        IplImage *s1 = maps[i];
        IplImage *r = resize2(s1, sz);
        cvAcc(r, sum, NULL);
        cvReleaseImage(&r);
    }
    cvConvertScale(sum, mean, 1.0/nb, 0);
    cvThreshold(mean, thr, 0.08, 1.0, CV_THRESH_BINARY);
    cvSmooth(thr, thr, CV_GAUSSIAN, 9, 9, 0, 0);
    cvXor(sum, sum, sum, NULL);
    cvAcc(mean, sum, NULL);
    cvAcc(thr, sum, NULL);
    //cvConvertScale(sum, mean, 0.5, 0);
    cvReleaseImage(&sum);
    cvReleaseImage(&thr);
    return mean;
}

int main(int argc, char **argv)
{

    if (argc < 2) print_usage(argv);
    IplImage *img = alignedImageFrom(argv[1], 8);
    char labels[1024], labeli[1024];

    printf("sal: %s\n", argv[1]);
    snprintf(labels, sizeof(labels), "saliency: %s", name(argv[1]));
    snprintf(labeli, sizeof(labeli), "img: %s", name(argv[1]));

    IplImage *s1 = salmap(img, 0);
    IplImage *s2 = salmap(resize(img, 0.8), 1);
    IplImage *s3 = salmap(resize(img, 0.5), 1);
    IplImage *s4 = salmap(resize(img, 0.25), 1);
    IplImage *scales[] = { s1, s2, s3, s4 };
    IplImage *res = combine(scales, sizeof(scales)/sizeof(IplImage*));

    if (argc < 3) {
    cvNamedWindow(labels, 1);
    cvMoveWindow(labels, img->width, 0);
    cvShowImage(labeli, img);
    cvShowImage(labels, res);
    //cvShowImage("1st level saliency", s1);
    //cvShowImage("2nd level saliency", s2);
    //cvShowImage("3rd level saliency", s3);
    //cvShowImage("4th level saliency", s4);
    cvWaitKey(0);
    } else write2file(argv[2], res, cvGetSize(img));
    cvReleaseImage(&img);
    cvReleaseImage(&s1);
    cvReleaseImage(&s2);
    cvReleaseImage(&s3);
    cvReleaseImage(&s4);
    cvReleaseImage(&res);
    return 0;
}
