#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui_c.h>

#include "wht.h"
#include "kdtree.h"
#include "prop.h"

#define XY_TO_INT(x, y) (((y) << 16) | (x))
#define XY_TO_X(x) ((x)&((1<<16)-1))
#define XY_TO_Y(y) ((y)>>16)

#define PACK_SCOREIDX(s, i) ((uint64_t)((uint64_t)s << 32 | (i)))
#define UNPACK_SCORE(a) ((a) >> 32)
#define UNPACK_IDX(a) ((a)&(0xFFFFFFFF))

#include <sys/time.h>
static inline double get_time()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec * 1e-6;
}

static void print_tuple(int **a, int nb, int tsz)
{
    int i, j;
    for (i = 0; i < nb; i++) {
        int *p = a[i];
        fprintf(stderr, "{");
        for (j = 0; j < tsz; j++) {
            fprintf(stderr, "%d,", p[j]);
        }
        fprintf(stderr, "}\n");
    }
}

void print_kdtree(kd_node *node, int k, int depth, int *order)
{
    int i;
    printf("(%d) ", order[depth%k]);
    for (i = 0; i < depth; i++) {
        printf(" ");
    }
    int *val = node->value[0];
    for (i = 0; i < k; i++) {
        printf("%d ", val[i]);
    }
    printf("\n");
    if (node->left) print_kdtree(node->left, k, depth+1, order);
    if (node->right) print_kdtree(node->right, k, depth+1, order);
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

static IplImage* splat(int *coeffs, CvSize size, int *plane_coeffs)
{
    IplImage *l = cvCreateImage(size, IPL_DEPTH_16S, 1);
    IplImage *a = cvCreateImage(size, IPL_DEPTH_16S, 1);
    IplImage *b = cvCreateImage(size, IPL_DEPTH_16S, 1);
    IplImage *lab = cvCreateImage(size, IPL_DEPTH_16S, 3);
    IplImage *lab8= cvCreateImage(size, IPL_DEPTH_8U, 3);
    IplImage *img = cvCreateImage(size, IPL_DEPTH_8U, 3);
    IplImage *trans = cvCreateImage(size, IPL_DEPTH_16S, 1);
    int dim = plane_coeffs[0] + plane_coeffs[1] + plane_coeffs[2];
    unsigned *order_luma = build_path(plane_coeffs[0], 8);
    unsigned *order_chroma = build_path(plane_coeffs[1], 8);
    unsigned *order_p2 = build_path(plane_coeffs[2], 8);

    memset(trans->imageData, 0, trans->imageSize);
    dequantize(trans, plane_coeffs[0], order_luma, 8, coeffs, dim);
    iwht2d(trans, l);
    memset(trans->imageData, 0, trans->imageSize);
    dequantize(trans, plane_coeffs[1], order_chroma, 8,
        coeffs+plane_coeffs[0], dim);
    iwht2d(trans, a);
    memset(trans->imageData, 0, trans->imageSize);
    dequantize(trans, plane_coeffs[2], order_p2, 8,
        coeffs+plane_coeffs[0]+plane_coeffs[1], dim);
    iwht2d(trans, b);

    cvMerge(l, a, b, NULL, lab);
    cvConvertScale(lab, img, 1, 0);
    //cvCvtColor(lab8, img, CV_YCrCb2BGR);

    cvReleaseImage(&l);
    cvReleaseImage(&a);
    cvReleaseImage(&b);
    cvReleaseImage(&lab);
    cvReleaseImage(&lab8);
    cvReleaseImage(&trans);
    free(order_luma);
    free(order_chroma);
    free(order_p2);
    return img;
}

static int find_best_match(int *coeffs, int *newcoeffs, kd_node *n,
    int k, int best)
{
    int i, j, *u, *v, **p = n->value;
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
            memcpy(newcoeffs, n->value[i], k*sizeof(int));
            best = dist;
        }
    }
    return best;
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

static int find_match_idx(int *coeffs, kd_node *n, int k)
{
    int i, *v , **p = n->value;
    for (i = 0; i < n->nb; i++) {
        v = *p++;
        if (!memcmp(coeffs, v, k*sizeof(int))) return i;
    }
    return -1; // no match found
}

static void refine(int *coeffs, int *newcoeffs, kd_node **nodes,
    int x, int y, int w, int k, int best)
{
    kd_node *top = nodes[y*(w-1)+x];
    kd_node *left = nodes[y*w+x-1];
    int newbest = find_best_match(coeffs, newcoeffs, top, k, best);
    find_best_match(coeffs, newcoeffs, left, k, newbest);
}

static IplImage* match(kd_tree *t, int *coeffs, CvSize s, int *pc)
{
    int i, j, k = t->k, best;
    CvSize size = {s.width/8, s.height/8};
    int *newcoeffs = malloc(sizeof(int)*size.width*size.height*k);
    kd_node **nodes = malloc(sizeof(kd_node*)*size.width*size.height);
    int *c = newcoeffs;
    for (i = 0; i < size.height; i++) {
        for (j = 0; j < size.width; j++) {
            kd_node *n = kdt_query(t, coeffs);
            best = find_best_match(coeffs, newcoeffs, n, k, INT_MAX);
            // check because there's nothing on the top and left
            if (i > 0 && j > 0) {
                refine(coeffs, newcoeffs, nodes, j, i, size.width, k,
                    best);
            }
            nodes[i*size.width+j] = n;
            coeffs += k;
            newcoeffs += k;
        }
    }
    IplImage *img = splat(c, s, pc);
    free(nodes);
    free(c);
    return img;
}

// old stuff without refinement; remove soonish
static IplImage* match2(kd_tree *t, int *coeffs, CvSize s, int *pc)
{
    int i, j, k = t->k;
    CvSize size = {s.width/8, s.height/8};
    int *newcoeffs = malloc(sizeof(int)*size.width*size.height*k);
    int *c = newcoeffs;
    for (i = 0; i < size.height; i++) {
        for (j = 0; j < size.width; j++) {
            kd_node *n = kdt_query(t, coeffs);
            find_best_match(coeffs, newcoeffs, n, k, INT_MAX);
            coeffs += k;
            newcoeffs += k;
        }
    }
    IplImage *img = splat(c, s, pc);
    free(c);
    return img;
}

static IplImage* match3(kd_tree *t, int *coeffs, CvSize s, int *pc)
{
    int x, y, k = t->k;
    CvSize size = {s.width/8, s.height/8};
    int *prevs = malloc(s.width*sizeof(int)*2), *prev = prevs;
    int *newcoeffs = malloc(sizeof(int)*size.width*size.height*k);
    int *c = newcoeffs;
    unsigned xy;
    for (y = 0; y < size.height; y++) {
        for (x = 0; x < size.width; x++) {
            if (!x) prev = prevs;
            xy = prop_enrich(t, coeffs, x, y, prev);
            memcpy(newcoeffs, t->start+xy, k*sizeof(int));
            coeffs += k;
            newcoeffs += k;
            prev += 2;
        }
    }
    IplImage *img = splat(c, s, pc);
    free(c);
    return img;
}

static int test_positions(kd_tree *t, int *coeffs, int nb)
{
    int i, errors = 0;
    for (i = 0; i < nb; i++) {
        kd_node *kdn = kdt_query(t, coeffs);
        if (-1 == find_match_idx(coeffs, kdn, t->k)) {
            fprintf(stderr, "Unable to find elem at %d", i);
            print_tuple(&coeffs, 1, t->k);
            errors++;
        }
        coeffs += t->k;
    }
    return errors;
}

static int* block_coeffs(IplImage *img, int* plane_coeffs) {
    CvSize size = cvGetSize(img);
    IplImage *lab = cvCreateImage(size, IPL_DEPTH_8U, 3);
    IplImage *l = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *a = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *b = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *trans = cvCreateImage(size, IPL_DEPTH_16S, 1);
    int dim = plane_coeffs[0] + plane_coeffs[1] + plane_coeffs[2];
    int sz = size.width*size.height/64*dim;
    int *buf = malloc(sizeof(int)*sz);
    unsigned *order_luma = build_path(plane_coeffs[0], 8);
    unsigned *order_chroma = build_path(plane_coeffs[1], 8);
    unsigned *order_p2 = build_path(plane_coeffs[2], 8);

    cvCvtColor(img, lab, CV_BGR2YCrCb);
    cvSplit(img, l, a, b, NULL);

    wht2d(l, trans);
    quantize(trans, plane_coeffs[0], 8, order_luma, buf, dim);

    wht2d(a, trans);
    quantize(trans, plane_coeffs[1], 8, order_chroma,
        buf+plane_coeffs[0], dim);

    wht2d(b, trans);
    quantize(trans, plane_coeffs[2], 8, order_p2,
        buf+plane_coeffs[0]+plane_coeffs[1], dim);

    cvReleaseImage(&trans);
    cvReleaseImage(&lab);
    cvReleaseImage(&l);
    cvReleaseImage(&a);
    cvReleaseImage(&b);
    free(order_luma);
    free(order_chroma);
    free(order_p2);

    return buf;
}

static void test_coeffs()
{
    int t[] = {2, 3, 5, 4, 9, 6, 4, 7, 8, 1, 7, 2};
    printf("t is %p sizeof(t) %d, %p, diff %d\n", t, sizeof(t), t+1, (t+1)-t);

    kd_tree kdt;
    kdt_new(&kdt, t, 6, 2);
    print_kdtree(kdt.root, 2, 0, kdt.order);
}

static void test_wht()
{
    IplImage *src = alignedImageFrom("lena.png", 8);
    IplImage *dst = alignedImageFrom("eva.jpg", 8);
    CvSize size = cvGetSize(src);
    int sz = size.width * size.height / 64;
    int *c_dst, *c_src, plane_coeffs[] = {16, 4, 4};
    int dim = plane_coeffs[0] + plane_coeffs[1] + plane_coeffs[2];
    c_src = block_coeffs(src, plane_coeffs);
    c_dst = block_coeffs(dst, plane_coeffs);
    kd_tree kdt;
    cvShowImage("img", src);
    printf("building tree\n");
    kdt_new(&kdt, c_src, sz, dim);
    printf("finished building tree\n");

    //IplImage *matched = match(&kdt, c_src, cvGetSize(src));
    //IplImage *matched = match(&kdt, c_dst, cvGetSize(dst));
    //printf("got matches\n");
    //cvShowImage("matched", matched);
    cvWaitKey(0);
    kdt_free(&kdt);
    free(c_dst);
    free(c_src);
    //cvReleaseImage(&matched);
    cvReleaseImage(&src);
    cvReleaseImage(&dst);

}

static void test_gck()
{
    IplImage *src = alignedImageFrom("lena.png", 8);
    CvSize size = cvGetSize(src);
    int w1 = size.width - 8 + 1, h1 = size.height - 8 + 1;
    int sz = w1*h1, plane_coeffs[] = {16, 4, 4};
    int *i, *c_src;
    int dim = plane_coeffs[0] + plane_coeffs[1] + plane_coeffs[2];
    prop_coeffs(src, plane_coeffs, &i);
    c_src = block_coeffs(src, plane_coeffs);
    kd_tree kdt;
    memset(&kdt, 0, sizeof(kdt));
    cvShowImage("img", src);

    kdt_new(&kdt, i, sz, dim);

    int errors = test_positions(&kdt, i, sz);
    printf("got positions with %d errors\n", errors);

    IplImage *matched = match(&kdt, c_src, size, plane_coeffs);
    cvShowImage("matched", matched);
    cvWaitKey(0);
    kdt_free(&kdt);
    free(i);
    free(c_src);
    cvReleaseImage(&matched);
    cvReleaseImage(&src);
}

static void test_gck2()
{
    IplImage *src = alignedImageFrom("lena.png", 8);
    IplImage *dst = alignedImageFrom("eva.jpg", 8);
    //IplImage *dst = alignedImageFrom("frames/bbb22.png", 8);
    //IplImage *src = alignedImageFrom("frames/bbb19.png", 8);
    CvSize size = cvGetSize(src);
    int w1 = size.width - 8 + 1, h1 = size.height - 8 + 1;
    int sz = w1*h1, *c_dst;
    int *i, plane_coeffs[] = {2, 9, 5};
    int dim = plane_coeffs[0] + plane_coeffs[1] + plane_coeffs[2];
    kd_tree kdt;

    prop_coeffs(src, plane_coeffs, &i);
    c_dst = block_coeffs(dst, plane_coeffs);
    memset(&kdt, 0, sizeof(kdt));

    kdt_new(&kdt, i, sz, dim);
    IplImage *matched = match(&kdt, c_dst, cvGetSize(dst),
        plane_coeffs);
    IplImage *matched2 = match2(&kdt, c_dst, cvGetSize(dst),
        plane_coeffs);
    IplImage *matched3 = match3(&kdt, c_dst, cvGetSize(dst),
        plane_coeffs);
    cvShowImage("src", src);
    cvShowImage("matched", matched);
    cvShowImage("matched2", matched2);
    cvShowImage("matched3", matched3);
    cvWaitKey(0);

    kdt_free(&kdt);
    free(i);
    free(c_dst);
    cvReleaseImage(&matched);
    cvReleaseImage(&matched2);
    cvReleaseImage(&matched3);
    cvReleaseImage(&src);
    cvReleaseImage(&dst);
}

static int match_score3(int *coeffs, kd_node *n, int k)
{
    int i, j, *u, *v, **p = n->value, best = INT_MAX;
    for (i = 0; i < n->nb; i++) {
        int dist = 0;
        u = coeffs;
        v = *p++;
        for (j = 0; j < k; j++) {
            int a = *u++;
            int b = *v++;
            dist += (a - b)*(a - b);
        }
        if (dist < best) best = dist;
    }
    return best;
}

static kd_node* best_match3(kd_tree *t, int *coeffs, int x, int y,
    kd_node **nodes)
{
    int k = t->k;
    kd_node *n = kdt_query(t, coeffs);
    int best = match_score3(coeffs, n, k);
    if (!x || !y) return n;
    kd_node *top = nodes[0];
    kd_node *left = nodes[-1];
    int attempt = match_score3(coeffs, top, k);
    if (attempt < best) {
        best = attempt;
        n = top;
    }
    attempt = match_score3(coeffs, left, k);
    if (attempt < best) { best = attempt; n = left; }
    return n;
}

IplImage* match_complete3(kd_tree *t, int *coeffs, IplImage *src,
    CvSize dst_size)
{
    IplImage *dst = cvCreateImage(dst_size, IPL_DEPTH_8U, 3);
    int w = dst_size.width  - 8 + 1, h = dst_size.height - 8 + 1;
    int sw = src->width - 8 + 1;
    int k = t->k, sz = w*h, i;
    uint8_t *dstdata = (uint8_t*)dst->imageData;
    uint8_t *srcdata = (uint8_t*)src->imageData;
    int dststride = dst->widthStep, srcstride = src->widthStep;
    kd_node **nodes = malloc(w*sizeof(kd_node*)), **curnode;
    for (i = 0; i < sz; i++) {
        int x = i % w, y = i/w, idx, sx, sy, sxy;
        if (!x) curnode = nodes;
        kd_node *n = best_match3(t, coeffs, x, y, curnode);
        idx = best_match_idx(coeffs, n, k);
        if (idx < 0) fprintf(stderr, "uhoh negative index\n");
        int *points = n->value[idx];
        sxy = (points - t->start)/t->k;
        sx = sxy % sw, sy = sxy/sw;
        if (sx >= src->width || sy >= src->height) {
            printf("grievous error: got %d,%d but dims %d,%d\n", sx, sy, src->width, src->height);
        }
        dstdata[y*dststride+x*3+0] = srcdata[sy*srcstride+sx*3+0];
        dstdata[y*dststride+x*3+1] = srcdata[sy*srcstride+sx*3+1];
        dstdata[y*dststride+x*3+2] = srcdata[sy*srcstride+sx*3+2];
        coeffs += k;
        *curnode++ = n;
    }
    free(nodes);
    return dst;
}

IplImage* match_complete2(kd_tree *t, int *coeffs, IplImage *src,
    CvSize dst_size)
{
    IplImage *dst = cvCreateImage(dst_size, IPL_DEPTH_8U, 3);
    int w = dst_size.width  - 8 + 1, h = dst_size.height - 8 + 1;
    int sw = src->width - 8 + 1;
    int k = t->k, sz = w*h, i;
    uint8_t *dstdata = (uint8_t*)dst->imageData;
    uint8_t *srcdata = (uint8_t*)src->imageData;
    int dststride = dst->widthStep, srcstride = src->widthStep;
    kd_node **nodes = malloc(w*sizeof(kd_node*)), **curnode;
    for (i = 0; i < sz; i++) {
        int x = i % w, y = i/w, idx, sx, sy, sxy;
        if (!x) curnode = nodes;
        kd_node *n = kdt_query(t, coeffs);
        idx = best_match_idx(coeffs, n, k);
        if (idx < 0) fprintf(stderr, "uhoh negative index\n");
        int *points = n->value[idx];
        sxy = (points - t->start)/t->k;
        sx = sxy % sw, sy = sxy/sw;
        if (sx >= src->width || sy >= src->height) {
            printf("grievous error: got %d,%d but dims %d,%d\n", sx, sy, src->width, src->height);
        }
        dstdata[y*dststride+x*3+0] = srcdata[sy*srcstride+sx*3+0];
        dstdata[y*dststride+x*3+1] = srcdata[sy*srcstride+sx*3+1];
        dstdata[y*dststride+x*3+2] = srcdata[sy*srcstride+sx*3+2];
        coeffs += k;
        *curnode++ = n;
    }
    free(nodes);
    return dst;
}

static int64_t sumimg(IplImage *img, int kern)
{
    int64_t sum = 0;
    int i, j;
    uint8_t *data;
    for (i = 0; i < img->height; i++) {
        data =  (uint8_t*)img->imageData + i*img->widthStep;
        for (j = 0; j < img->width - kern + 1; j++) {
            sum += *data++;
            sum += *data++;
            sum += *data++;
        }
    }
    return sum;
}

#define XY_TO_X(x) ((x)&((1<<16)-1))
#define XY_TO_Y(y) ((y)>>16)
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

static void test_complete()
{
    //IplImage *dst = alignedImageFrom("frames/bbb22.png", 8);
    //IplImage *src = alignedImageFrom("frames/bbb19.png", 8);
    IplImage *src = alignedImageFrom("lena.png", 8);
    IplImage *dst = alignedImageFrom("eva.jpg", 8);
    CvSize src_size = cvGetSize(src), dst_size = cvGetSize(dst);
    IplImage *diff = cvCreateImage(dst_size, IPL_DEPTH_8U, 3);
    IplImage *diff3 = cvCreateImage(dst_size, IPL_DEPTH_8U, 3);
    IplImage *diff2 = cvCreateImage(dst_size, IPL_DEPTH_8U, 3);
    IplImage *matched = cvCreateImage(dst_size, IPL_DEPTH_8U, 3);
    int w1 = src_size.width - 8 + 1, h1 = src_size.height - 8 + 1;
    int plane_coeffs[] = {2, 9, 5}, sz = w1*h1;
    int dim = plane_coeffs[0] + plane_coeffs[1] + plane_coeffs[2];
    int *i, *di;
    double t1, t2, t3, t4, t5;
    kd_tree kdt;

    memset(&kdt, 0, sizeof(kdt));
    t1 = get_time();
    prop_coeffs(src, plane_coeffs, &i);
    t2 = get_time();
    prop_coeffs(dst, plane_coeffs, &di);

    t3 = get_time();
    kdt_new(&kdt, i, sz, dim);
    t4 = get_time();

    IplImage *xy = prop_match_complete(&kdt, di, src, dst_size);
    xy2img(xy, src, matched);
    t5 = get_time();
    IplImage *matched3 = match_complete3(&kdt, di, src, dst_size);
    IplImage *matched2 = match_complete2(&kdt, di, src, dst_size);
    cvShowImage("original", dst);
    cvShowImage("complete match3", matched3);
    cvShowImage("complete match2", matched2);
    cvShowImage("complete match", matched);
    cvAbsDiff(dst, matched, diff);
    cvAbsDiff(dst, matched3, diff3);
    cvAbsDiff(dst, matched2, diff2);
    cvShowImage("diff2", diff2);
    cvShowImage("diff", diff);
    cvShowImage("diff3", diff3);
    cvWaitKey(0);

    printf("diffsum : %lld\n", sumimg(diff, 8));
    printf("diff2sum: %lld\n", sumimg(diff2, 8));
    printf("diff3sum: %lld\n", sumimg(diff3, 8));
    printf("total time: %fms\ncoeffs %f\nbuilding %f\n"
        "matching: %f\ninit: %f\n",
        (t5 - t1)*1000, (t2 - t1)*1000, (t4 - t3)*1000,
        (t5 - t4)*1000, (t4 - t1)*1000);

    kdt_free(&kdt);
    free(i);
    free(di);
    cvReleaseImage(&src);
    cvReleaseImage(&dst);
    cvReleaseImage(&xy);
    cvReleaseImage(&matched);
    cvReleaseImage(&matched2);
    cvReleaseImage(&matched3);
    cvReleaseImage(&diff);
    cvReleaseImage(&diff2);
    cvReleaseImage(&diff3);
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
static void xy2blks(IplImage *xy, IplImage *src, IplImage *recon, int kernsz)
{
    int w = xy->width, h = xy->height, i, j;
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
#undef XY_TO_X
#undef XY_TO_Y

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

static int query4(kd_tree *t, int *coeffs, kd_node **nodes,
    int x, int y, int w)
{
    kd_node *n = kdt_query(t, coeffs), *top, *left;
    int64_t res = match_score(coeffs, n, t->k);
    int score = UNPACK_SCORE(res);
    int *pos = n->value[UNPACK_IDX(res)];
    if (!y) goto try_left;
    top = nodes[y*(w-1)+x];
    res = match_score(coeffs, top, t->k);
    if (UNPACK_SCORE(res) < score) {
        score = UNPACK_SCORE(res);
        pos = top->value[UNPACK_IDX(res)];
        n = top;
    }
try_left:
    if (!x) goto query_finish;
    left = nodes[y*w+x-1];
    res = match_score(coeffs, left, t->k);
    if (UNPACK_SCORE(res) < score) {
        score = UNPACK_SCORE(res);
        pos = left->value[UNPACK_IDX(res)];
        n = left;
    }
query_finish:
    nodes[y*w+x] = n;
    return (pos - t->start)/t->k;
}

static IplImage *match4(kd_tree *t, int *coeffs, CvSize src_size, CvSize dst_size)
{
    int i, j, k = t->k, sw = src_size.width - 8 + 1;
    CvSize size = {dst_size.width/8, dst_size.height/8};
    kd_node **nodes = malloc(sizeof(kd_node*)*size.width*size.height);
    IplImage *xy = cvCreateImage(size, IPL_DEPTH_32S, 1);
    int *xydata = (int*)xy->imageData;
    int xystride = xy->widthStep/sizeof(int);
    for (i = 0; i < size.height; i++) {
        for (j = 0; j < size.width; j++) {
            if (!j) xydata = (int*)xy->imageData + i * xystride;
            int sxy = query4(t, coeffs, nodes, j, i, size.width);
            int sx = sxy % sw, sy = sxy / sw;
            *xydata++ = XY_TO_INT(sx, sy);
            coeffs += k;
        }
    }
    free(nodes);
    return xy;
}

static void test_gck3()
{
    IplImage *dst = alignedImageFrom("frames/bbb22.png", 8);
    IplImage *src = alignedImageFrom("frames/bbb19.png", 8);
    CvSize ssz = cvGetSize(src), dsz = cvGetSize(dst);
    CvSize dst_blks = {(dsz.width/8)+7, (dsz.height/8)+7};
    int w1 = ssz.width - 8 + 1, h1 = ssz.height - 8 + 1;
    int plane_coeffs[] = {2, 9, 5}, *srci, *dsti, sz = w1*h1;
    int dim = plane_coeffs[0] + plane_coeffs[1] + plane_coeffs[2];
    kd_tree kdt;
    IplImage *recon = cvCreateImage(dsz, dst->depth, dst->nChannels);
    IplImage *recon2 = cvCreateImage(dsz, dst->depth, dst->nChannels);
    IplImage *diff = cvCreateImage(dsz, dst->depth, dst->nChannels);
    IplImage *diff2 = cvCreateImage(dsz, dst->depth, dst->nChannels);

    prop_coeffs(src, plane_coeffs, &srci);
    dsti = block_coeffs(dst, plane_coeffs);
    memset(&kdt, 0, sizeof(kdt));
    kdt_new(&kdt, srci, sz, dim);
    IplImage *xy = prop_match_complete(&kdt, dsti, src, dst_blks);
    xy2blks_special(xy, src, recon, 8);
    IplImage *xy2 = match4(&kdt, dsti, ssz, dsz);
    xy2blks(xy2, src, recon2, 8);
    cvAbsDiff(recon, dst, diff);
    cvAbsDiff(recon2, dst, diff2);
    cvShowImage("prop", recon);
    cvShowImage("match4", recon2);
    printf("prop: %lld\nmatch4: %lld\n",
        sumimg(diff, 8), sumimg(diff2, 8));
    cvWaitKey(0);
    cvReleaseImage(&xy);
    cvReleaseImage(&xy2);
    free(srci);
    free(dsti);
    kdt_free(&kdt);
    cvReleaseImage(&recon);
    cvReleaseImage(&recon2);
    cvReleaseImage(&diff);
    cvReleaseImage(&diff2);
}

int main()
{
    //test_wht();
    //test_gck();
    //test_coeffs();
    //test_gck2();
    test_complete();
    return 0;
}
