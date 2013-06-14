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

static int best_match_idx(int *coeffs, kd_node *n, int k)
{
    int i, j, *u, *v, **p = n->value, best = INT_MAX, idx = -1;
    for (i = 0; i < n->nb; i++) {
        int dist = 0;
        u = coeffs;
        v = *p++;
        for (j = 0; j < k; j++) {
            int a = *u++;
            int b = *v++;
            dist += (a - b)*(a - b);
        }
        if (dist < best) {
            best = dist;
            idx = i;
        }
    }
    return idx;
}

static double l2_color(int *a, int *b, int k)
{
    int i, dist = 0;
    for (i = 0; i < k; i++) {
        int diff = a[i] - b[i];
        dist += diff*diff;
    }
    return sqrt(dist);
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

static float compute_dist(kd_tree *t, kd_node *n, int idx, int w)
{
    int *v = n->value[idx];
    int i; double dist = 0;
    for (i = 0; i < n->nb; i++) {
        int *u = n->value[i];
        if (i == idx) continue;
        int dcolor = l2_color(u, v, t->k);
        int dpos = l2_pos(t, u, v, w);
        //dist += dcolor/360.0;
        dist += dcolor / (1 + 3*dpos);
    }
    return 1 - exp(-dist/n->nb);
}

int main(int argc, char **argv)
{

    if (argc < 2) print_usage(argv);
    //int plane_coeffs[] = {16, 4, 4}, i, *imgc, *c;
    int plane_coeffs[] = {2, 9, 5}, i, *imgc, *c;
    int dim = plane_coeffs[0] + plane_coeffs[1] + plane_coeffs[2];
    IplImage *img = alignedImageFrom(argv[1], 8);
    CvSize isz = cvGetSize(img);
    int w = isz.width - 8 + 1, h = isz.height - 8 + 1, sz = w*h;
    CvSize salsz = {w, h};
    IplImage *sal = cvCreateImage(salsz, IPL_DEPTH_32F, 1);
    int salstride = sal->widthStep/sizeof(float);
    kd_tree kdt;
    char labels[1024], labeli[1024];

    printf("sal: %s\n", argv[1]);
    snprintf(labels, sizeof(labels), "saliency: %s", name(argv[1]));
    snprintf(labeli, sizeof(labeli), "img: %s", name(argv[1]));
    memset(&kdt, 0, sizeof(kd_tree));
    prop_coeffs(img, plane_coeffs, &imgc);
    kdt_new_overlap(&kdt, imgc, sz, dim, 0.5, 8, w);
    c = imgc;

    for (i = 0; i < sz; i++) {
        int x = i % w, y = i / w;
        float *data = (float*)sal->imageData + (y * salstride + x);
        kd_node *n = kdt_query(&kdt, imgc);
        int idx = best_match_idx(imgc, n, kdt.k);
        *data = compute_dist(&kdt, n, idx, w);
        imgc += kdt.k;
    }

    cvNamedWindow(labels, 1);
    cvMoveWindow(labels, img->width, 0);
    cvShowImage(labeli, img);
    cvShowImage(labels, sal);
    cvWaitKey(0);
    cvReleaseImage(&img);
    cvReleaseImage(&sal);
    kdt_free(&kdt);
    free(c);
    return 0;
}
