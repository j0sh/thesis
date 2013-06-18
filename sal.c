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
#include "sal.h"

#define XY_TO_INT(x, y) (((y) << 16) | (x))
#define XY_TO_X(x) ((x)&((1<<16)-1))
#define XY_TO_Y(y) ((y)>>16)

static void print_usage(char **argv)
{
    printf("Usage: %s <path> [outfile]\n", argv[0]);
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
    return sqrt(dist);///sqrt(255*255*k);
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

static double clamp(double d)
{
    if (d > 1) return 1.0;
    if (d < 0) return 0;
    return d;
}

static float compute_dist(kd_tree *t, kd_node *n, int *v, int w)
{
    int i; double dist = 0;
    for (i = 0; i < n->nb; i++) {
        int *u = n->value[i];
        double dcolor = l2_color(u, v, t->k);
        double dpos = l2_pos(t, u, v, w);
        dist += dcolor / (1 + t->k*dpos);
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
    double min, max;

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
    cvMinMaxLoc(sal, &min, &max, NULL, NULL, NULL);
    CvScalar scalar = cvRealScalar(-min);
    cvAddS(sal, scalar, sal, NULL);
    cvConvertScale(sal, sal, 1.0/(max - min), 0);
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

static void write2file(char *fname, IplImage *img)
{
    IplImage *out = cvCreateImage(cvGetSize(img), IPL_DEPTH_8U, img->nChannels);
    cvConvertScale(img, out, 255, 0);
    cvSaveImage(fname, out, 0);
    cvReleaseImage(&out);
}

static float attended_scale(IplImage *thresh, int x, int y)
{
    int d = 32;
    double r = d/2;
    if (x+r >= thresh->width) return 0.5; // lazy; fix later
    if (y+r >= thresh->height) return 0.5;

    int s = thresh->widthStep/sizeof(float), a, b;
    int startx = abs((x - r)), starty = abs((y - r));
    float *f = (float*)thresh->imageData + (starty*s + startx);
    double dist = 0;
    int count = 0;
    for (a = 0; a < d; a++) {
        float *g = f + s*a;
        for (b = 0; b < d; b++) {
            double ed = (a - r)*(a - r) + (b - r)*(b - r);
            if (ed < r*r) {
                if (*g > 0) {
                    dist += sqrt(ed);
                    count++;
                }
            }
            g++;
        }
    }
    if (count) return (count*dist)/((r*r)*sqrt(2*r*r));
    return 0.0;
}

static IplImage* calc_distmap(IplImage *thresh)
{
    IplImage *distmap = cvCreateImage(cvGetSize(thresh), thresh->depth, 1);
    int x, y;
    float *data; int stride = thresh->widthStep/sizeof(float);
    for (y = 0; y < thresh->height; y++) {
        data = (float*)distmap->imageData + y * stride;
        for (x = 0; x < thresh->width; x++) {
            data[x] = attended_scale(thresh, x, y);
        }
    }
    return distmap;
}

static IplImage* combine(IplImage** maps, int nb)
{
    int i;
    double min, max;
    IplImage *img = maps[0];
    CvSize sz = cvGetSize(img);
    IplImage *sum = alignedImage(sz, img->depth, img->nChannels, 8);
    IplImage *mean = alignedImage(sz, img->depth, img->nChannels, 8);
    IplImage *thr = alignedImage(sz, img->depth, 1, 8);
    cvXor(sum, sum, sum, NULL);
    for (i = 0; i < nb; i++) {
        IplImage *s1 = maps[i];
        IplImage *r = resize2(s1, sz);
        cvAcc(r, sum, NULL);
        cvReleaseImage(&r);
    }
    cvConvertScale(sum, mean, 1.0/nb, 0);
    cvXor(sum, sum, sum, NULL);
    for (i = 0; i < 10; i++) {
        cvThreshold(mean, thr, 0.5 + 0.1*i, 0.0, CV_THRESH_TOZERO);
        IplImage *dist = calc_distmap(thr);
        cvAcc(dist, sum, NULL);
        cvReleaseImage(&dist);
    }
    cvConvertScale(sum, sum, 1.0/10, 0);
    cvMinMaxLoc(sum, &min, &max, NULL, NULL, NULL);
    CvScalar scalar = cvRealScalar(-min);
    cvAddS(sum, scalar, sum, NULL);
    cvConvertScale(sum, sum, 1.0/(max - min), 0);
    cvMul(sum, mean, mean, 1);
    cvReleaseImage(&sum);
    cvReleaseImage(&thr);
    return mean;
}

IplImage* saliency(IplImage *img)
{
    IplImage *s1 = salmap(img, 0);
    IplImage *s2 = salmap(resize(img, 0.8), 1);
    IplImage *s3 = salmap(resize(img, 0.5), 1);
    IplImage *s4 = salmap(resize(img, 0.25), 1);
    IplImage *scales[] = { s1, s2, s3, s4 };
    IplImage *sal = combine(scales, sizeof(scales)/sizeof(IplImage*));
    IplImage *res = resize2(sal, cvGetSize(img));
    cvReleaseImage(&s1);
    cvReleaseImage(&s2);
    cvReleaseImage(&s3);
    cvReleaseImage(&s4);
    cvReleaseImage(&sal);
    return res;
}

#if 0
int main(int argc, char **argv)
{

    if (argc < 2) print_usage(argv);
    IplImage *img = alignedImageFrom(argv[1], 8);
    char labels[1024], labeli[1024];

    printf("sal: %s\n", argv[1]);
    snprintf(labels, sizeof(labels), "saliency: %s", name(argv[1]));
    snprintf(labeli, sizeof(labeli), "img: %s", name(argv[1]));
    IplImage *res = saliency(img);


    if (argc < 3) {
    cvNamedWindow(labels, 1);
    cvMoveWindow(labels, img->width, 0);
    cvShowImage(labeli, img);
    cvShowImage(labels, res);
    cvWaitKey(0);
    } else write2file(argv[2], res);
    cvReleaseImage(&img);
    cvReleaseImage(&res);
    return 0;
}
#endif
