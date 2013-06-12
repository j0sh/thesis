#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui_c.h>

#include "gck.h"
#include "kdtree.h"

#define XY_TO_INT(x, y) (((y) << 16) | (x))
#define XY_TO_X(x) ((x)&((1<<16)-1))
#define XY_TO_Y(y) ((y)>>16)

#define PACK_SCOREIDX(s, i) ((uint64_t)((uint64_t)s << 32 | (i)))
#define UNPACK_SCORE(a) ((a) >> 32)
#define UNPACK_IDX(a) ((a)&(0xFFFFFFFF))

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

static int64_t match_score(int *coeffs, kd_node *n, int k)
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
    return PACK_SCOREIDX(best, idx);
}

static int patch_score(int *c1, int *c2, int k)
{
    int i, dist = 0;
    for (i = 0; i < k; i++) {
        int a = *c1++;
        int b = *c2++;
        dist += (a-b)*(a-b);
    }
    return dist;
}

static void swap2(int *scores, int *index)
{
    int t = scores[0];
    scores[0] = scores[1];
    scores[1] = t;
    int u = index[0];
    index[0] = index[1];
    index[1] = u;
}

static inline void check_guide(kd_tree *t, int *coeffs, int off,
    int *scores, int *pos)
{
    if (t->start + off >= t->end) return;
    int *points = t->start + off;
    int attempt = patch_score(coeffs, points, t->k);
    if (attempt < scores[0]) {
        pos[0] = off;
        scores[0] = attempt;
        if (scores[0] < scores[1]) swap2(scores, pos);
    }
}

static unsigned match_enrich(kd_tree *t, int *coeffs, int x, int y,
    int *prev)
{
    int k = t->k, *start = t->start;
    kd_node *n  = kdt_query(t, coeffs);

    // set results of query
    int64_t res = match_score(coeffs, n, k);
    int best[] = {INT_MAX, UNPACK_SCORE(res)};
    int pos[] = {INT_MAX, n->value[UNPACK_IDX(res)] - start};

    pos[0] = pos[1]; // hack for (x,y) == (0,0)

    // now check 2 best matches for the left
    if (x) {
        check_guide(t, coeffs, prev[-1], best, pos);
        check_guide(t, coeffs, prev[-2], best, pos);
    }

    // check 2 best matches for top
    if (y) {
        check_guide(t, coeffs, prev[0], best, pos);
        check_guide(t, coeffs, prev[1], best, pos);
    }

    // set prev to best matches
    prev[0] = pos[0];
    prev[1] = pos[1];
    return pos[1];
}

static IplImage* match(kd_tree *t, int *coeffs, IplImage *src,
    CvSize dst_size)
{
    IplImage *xy = cvCreateImage(dst_size, IPL_DEPTH_32S, 1);
    IplImage *dst = cvCreateImage(dst_size, IPL_DEPTH_8U, 3);
    int w = dst_size.width  - 8 + 1, h = dst_size.height - 8 + 1;
    int k = t->k, sz = w*h, i, sw = src->width - 8 + 1;
    int *prevs = malloc(w*sizeof(int)*2), *prev = prevs;
    int *xydata = (int*)xy->imageData;
    for (i = 0; i < sz; i++) {
        int x = i % w, y = i/w, sx, sy, sxy;
        if (!x) {
            prev = prevs;
            xydata = (int*)xy->imageData + y*(xy->widthStep/sizeof(int));
        }
        sxy = match_enrich(t, coeffs, x, y, prev) / k;
        sx = sxy % sw; sy = sxy / sw;
        *xydata++ = XY_TO_INT(sx, sy);
        if (sx >= src->width || sy >= src->height) {
            printf("grievous error: got %d,%d but dims %d,%d sxy %d\n", sx, sy, src->width, src->height, sxy);
        }
        coeffs += k;
        prev += 2;
    }
    free(prevs);
    xy2img(xy, src, dst);
    cvReleaseImage(&xy);
    return dst;
}

static void interleave_data(int *data, int w, int h, int kern_size,
    int bases, int *a, int aw)
{
    // takes XXXAAAABBBBCCCCXXX -> ABCABCABCABC
    // eg, remove the results that incorporate padding and interleave
    int kw = w + kern_size - 1, i, j, k, kh = h + kern_size - 1;
    int rw = w - kern_size + 1, rh = h - kern_size + 1;
    int *src = data;
    for (k = 0; k < bases; k++) {
        src = data + kw*kh*k;
        src += kw * (kern_size - 1) + kern_size - 1;
        for (i = 0; i < rh; i++) {
            int *s = src;
            for (j = 0; j < rw; j++) {
                int n = i*rw+j;
                a[n*aw + k] = *s++;
            }
            src += kw;
        }
    }
}

static void coeffs_i(IplImage *img, int bases, int total_b, int *data)
{
    CvSize s = cvGetSize(img);
    int w = s.width, h = s.height;
    if (w != img->widthStep) {
        fprintf(stderr, "image not aligned uh oh\n");
        exit(1);
    }
    int *res = gck_calc_2d((uint8_t*)img->imageData, w, h, 8, bases);
    interleave_data(res, w, h, 8, bases, data, total_b);
    free(res);
}

static void coeffs(IplImage *img, int dim, int *pc, int **in) {
    CvSize size = cvGetSize(img);
    IplImage *lab = cvCreateImage(size, IPL_DEPTH_8U, 3);
    IplImage *l = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *a = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *b = cvCreateImage(size, IPL_DEPTH_8U, 1);
    int w = size.width, h = size.height;
    int *interleaved = gck_alloc_buffer(w, h, 8, dim);

    cvCvtColor(img, lab, CV_BGR2YCrCb);
    cvSplit(img, l, a, b, NULL);

    coeffs_i(l, pc[0], dim, interleaved);

    coeffs_i(a, pc[1], dim, interleaved+pc[0]);

    coeffs_i(b, pc[2], dim, interleaved+pc[0]+pc[1]);

    cvReleaseImage(&lab);
    cvReleaseImage(&l);
    cvReleaseImage(&a);
    cvReleaseImage(&b);

    *in = interleaved;
}

IplImage* prop_match(IplImage *src, IplImage *dst)
{
    int *srcdata, *dstdata;
    CvSize src_size = cvGetSize(src), dst_size = cvGetSize(dst);
    int w1 = src_size.width - 8 + 1, h1 = src_size.height - 8 + 1;
    int sz = w1*h1, plane_coeffs[] = {2, 9, 5};
    int dim = plane_coeffs[0] + plane_coeffs[1] + plane_coeffs[2];
    kd_tree kdt;
    IplImage *matched;
    coeffs(src, dim, plane_coeffs, &srcdata);
    coeffs(dst, dim, plane_coeffs, &dstdata);
    memset(&kdt, 0, sizeof(kdt));
    kdt_new(&kdt, srcdata, sz, dim);
    matched  = match(&kdt, dstdata, src, dst_size);
    free(srcdata);
    free(dstdata);
    kdt_free(&kdt);
    return matched;
}

void prop_coeffs(IplImage *src, int *plane_coeffs, int **data)
{
    int dim = plane_coeffs[0] + plane_coeffs[1] + plane_coeffs[2];
    return coeffs(src, dim, plane_coeffs, data);
}

IplImage *prop_match_complete(kd_tree *kdt, int *data, IplImage *src,
    CvSize dst_size)
{
    return match(kdt, data, src, dst_size);
}

unsigned prop_enrich(kd_tree *t, int *coeffs, int x, int y, int *prev)
{
    return match_enrich(t, coeffs, x, y, prev);
}
