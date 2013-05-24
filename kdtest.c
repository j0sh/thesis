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

#include <sys/time.h>
static inline double get_time()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec * 1e-6;
}

static void print_tuple(int *a, int nb, int tsz)
{
    int i, j;
    for (i = 0; i < nb; i++) {
        fprintf(stderr, "{");
        for (j = 0; j < tsz; j++) {
            fprintf(stderr, "%d,", a[i*tsz + j]);
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

static IplImage* splat(int *coeffs, CvSize size, int dim)
{
    if (dim != 27) return NULL; // temporary; nothing else supported

    IplImage *l = cvCreateImage(size, IPL_DEPTH_16S, 1);
    IplImage *a = cvCreateImage(size, IPL_DEPTH_16S, 1);
    IplImage *b = cvCreateImage(size, IPL_DEPTH_16S, 1);
    IplImage *lab = cvCreateImage(size, IPL_DEPTH_16S, 3);
    IplImage *lab8= cvCreateImage(size, IPL_DEPTH_8U, 3);
    IplImage *img = cvCreateImage(size, IPL_DEPTH_8U, 3);
    IplImage *trans = cvCreateImage(size, IPL_DEPTH_16S, 1);
    unsigned *order = build_path(25, 8);

    memset(trans->imageData, 0, trans->imageSize);
    dequantize(trans, 25, order, 8, coeffs, dim);
    iwht2d(trans, l);
    memset(trans->imageData, 0, trans->imageSize);
    dequantize(trans, 1, order, 8, coeffs+25, dim);
    iwht2d(trans, a);
    memset(trans->imageData, 0, trans->imageSize);
    dequantize(trans, 1, order, 8, coeffs+26, dim);
    iwht2d(trans, b);

    cvMerge(l, a, b, NULL, lab);
    cvConvertScale(lab, lab8, 1, 0);
    cvCvtColor(lab8, img, CV_Lab2BGR);

    cvReleaseImage(&l);
    cvReleaseImage(&a);
    cvReleaseImage(&b);
    cvReleaseImage(&lab);
    cvReleaseImage(&lab8);
    cvReleaseImage(&trans);
    free(order);
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

static IplImage* match(kd_tree *t, int *coeffs, CvSize s)
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
    IplImage *img = splat(c, s, k);
    free(nodes);
    free(c);
    return img;
}

// old stuff without refinement; remove soonish
static IplImage* match2(kd_tree *t, int *coeffs, CvSize s)
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
    IplImage *img = splat(c, s, k);
    free(c);
    return img;
}

static IplImage* match3(kd_tree *t, int *coeffs, CvSize s)
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
    IplImage *img = splat(c, s, k);
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
            print_tuple(coeffs, 1, t->k);
            errors++;
        }
        coeffs += t->k;
    }
    return errors;
}

static int* block_coeffs(IplImage *img, int dim) {
    if (dim != 27) return NULL; // temporary; nothing else supported

    CvSize size = cvGetSize(img);
    IplImage *lab = cvCreateImage(size, IPL_DEPTH_8U, 3);
    IplImage *l = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *a = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *b = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *trans = cvCreateImage(size, IPL_DEPTH_16S, 1);
    int sz = size.width*size.height/64*dim;
    int *buf = malloc(sizeof(int)*sz);
    unsigned *order = build_path(25, 8);

    cvCvtColor(img, lab, CV_BGR2Lab);
    cvSplit(lab, l, a, b, NULL);

    wht2d(l, trans);
    quantize(trans, 25, 8, order, buf, dim);

    wht2d(a, trans);
    quantize(trans, 1, 8, order, buf+25, dim);

    wht2d(b, trans);
    quantize(trans, 1, 8, order, buf+26, dim);

    cvReleaseImage(&trans);
    cvReleaseImage(&lab);
    cvReleaseImage(&l);
    cvReleaseImage(&a);
    cvReleaseImage(&b);
    free(order);

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
    int dim = 27, sz = size.width * size.height / 64;
    int *c_dst, *c_src;
    c_src = block_coeffs(src, dim);
    c_dst = block_coeffs(dst, dim);
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
    int dim = 27, sz = w1*h1;
    int *i, *c_src;
    prop_coeffs(src, dim, &i);
    c_src = block_coeffs(src, dim);
    kd_tree kdt;
    memset(&kdt, 0, sizeof(kdt));
    cvShowImage("img", src);

    kdt_new(&kdt, i, sz, dim);

    int errors = test_positions(&kdt, i, sz);
    printf("got positions with %d errors\n", errors);

    IplImage *matched = match(&kdt, c_src, size);
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
    int dim = 27, sz = w1*h1, *c_dst;
    int *i;
    kd_tree kdt;

    prop_coeffs(src, dim, &i);
    c_dst = block_coeffs(dst, dim);
    memset(&kdt, 0, sizeof(kdt));

    kdt_new(&kdt, i, sz, dim);
    IplImage *matched = match(&kdt, c_dst, cvGetSize(dst));
    IplImage *matched2 = match2(&kdt, c_dst, cvGetSize(dst));
    IplImage *matched3 = match3(&kdt, c_dst, cvGetSize(dst));
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
    int w1 = src_size.width - 8 + 1, h1 = src_size.height - 8 + 1;
    int dim = 27, sz = w1*h1;
    int *i, *di;
    double t1, t2, t3, t4, t5;
    kd_tree kdt;

    memset(&kdt, 0, sizeof(kdt));
    t1 = get_time();
    prop_coeffs(src, dim, &i);
    t2 = get_time();
    prop_coeffs(dst, dim, &di);

    t3 = get_time();
    kdt_new(&kdt, i, sz, dim);
    t4 = get_time();

    IplImage *matched = prop_match_complete(&kdt, di, src, dst_size);
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
    cvReleaseImage(&matched);
    //cvReleaseImage(&matched2);
    //cvReleaseImage(&matched3);
    cvReleaseImage(&diff);
    cvReleaseImage(&diff2);
    cvReleaseImage(&diff3);
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
