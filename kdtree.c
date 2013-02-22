#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui_c.h>

#include "wht.h"

// maximum # of candidates per leaf
#define LEAF_CANDS 8

typedef struct kd_node {
    int *value;
    int nb;
    struct kd_node *left;
    struct kd_node *right;
} kd_node;

typedef struct kd_tree {
    int k;
    kd_node *root;
} kd_tree;

static int calc_dist(int *coeffs, kd_node *n, int k)
{
    int i, j, d = 0;
    int *v = n->value, *u;
    for (i = 0; i < n->nb; i++) {
        u = coeffs;
        for (j = 0; j < k; j++) {
            int a = *v++;
            int b = *u++;
            d += (a - b)*(a - b);
        }
    }
    return d/(n->nb*k);
}

inline static int kdt_compar(const void *a, const void *b, void *opaque)
{
    int off = *(int*)opaque;
    return ((int*)a)[off] - ((int*)b)[off];
}

static kd_node *kdt_new_in(kd_tree *t, int *points, int nb_points,
    int depth, int *order)
{
    if (0 == nb_points) return NULL;
    int axis = order[depth % t->k], median;
    kd_node *node = malloc(sizeof(kd_node));

    if (nb_points <= LEAF_CANDS) {
        node->value = points;
        node->left = node->right = NULL;
        node->nb = nb_points;
        return node;
    }
    qsort_r(points, nb_points, t->k*sizeof(int), &kdt_compar, &axis);
    median = nb_points/2;
    node->value = points+median*t->k;
    while ((points+(median+1)*t->k)[axis] == node->value[axis]) {
        // make nodes with the same value as the median at the axis
        // fall on the left side of the tree by bumping up the median
        node->value += t->k;
        median += 1;
    }
    node->left = kdt_new_in(t, points, median, depth + 1, order);
    node->right = kdt_new_in(t, points+(median+1)*t->k, nb_points - median - 1, depth+1, order);
    node->nb = 1;

    return node;
}

static kd_node* kdt_query_in(kd_node *n, int depth, int* qd,
    int *order, int dim)
{
    int k = order[depth%dim];
    if (n->left == NULL && n->right == NULL) return n;
    if (!memcmp(qd, n->value, dim*sizeof(int))) return n;
    if (n->left && qd[k] <= n->value[k])
        return kdt_query_in(n->left, depth+1, qd, order, dim);
    else if (n->right && qd[k] > n->value[k])
        return kdt_query_in(n->right, depth+1, qd, order, dim);
    fprintf(stderr, "This path should never be taken\n");
    return n;
}

kd_node* kdt_query(kd_tree *t, int *points, int *order)
{
    return kdt_query_in(t->root, 0, points, order, t->k);
}

static void kdt_cleanup(kd_node **n)
{
    kd_node *m = *n;
    if (m->left) kdt_cleanup(&m->left);
    if (m->right) kdt_cleanup(&m->right);
    free(m);
    *n = NULL;
}

void kdt_new(kd_tree *t, int *points, int nb_points, int k,
    int *order)
{
    t->k = k; // dimensionality
    t->root = kdt_new_in(t, points, nb_points, 0, order);
}

#include <sys/time.h>
static inline double get_time()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec * 1e-6;
}

void print_kdtree(kd_node *node, int k, int depth, int *order)
{
    int i;
    printf("(%d) ", order[depth%k]);
    for (i = 0; i < depth; i++) {
        printf(" ");
    }
    int *val = node->value;
    for (i = 0; i < k; i++) {
        printf("%d ", val[i]);
    }
    printf("\n");
    if (node->left) print_kdtree(node->left, k, depth+1, order);
    if (node->right) print_kdtree(node->right, k, depth+1, order);
}

typedef struct {
    int min, max, diff, idx;
} dimstats;

static inline int dim_compar(const void *a, const void *b)
{
    return ((dimstats*)a)->diff - ((dimstats*)b)->diff;
}

static int* calc_dimstats(int *points, int nb, int dim)
{
    int i, j, *order = malloc(dim*sizeof(int));
    dimstats *d = malloc(dim*sizeof(dimstats));
    for (j = 0; j < dim; j++) {
        (d+j)->min = INT_MAX;
        (d+j)->max = INT_MIN;
        (d+j)->diff = INT_MAX;
        (d+j)->idx = j;
    }
    for (i = 0; i < nb; i++) {
        for (j = 0; j < dim; j++) {
            int v = *points++;
            dimstats *ds = d+j;
            if (v < ds->min) ds->min = v;
            if (v > ds->max) ds->max = v;
        }
    }

    for (j = 0; j < dim; j++) {
        dimstats *ds = d+j;
        ds->diff = ds->max - ds->min;
    }
    qsort(d, dim, sizeof(dimstats), &dim_compar);
    printf("Ordering: ");
    for (j = 0; j < dim; j++) {
        order[j] = (d+j)->idx;
        printf("%d ", (d+j)->idx);
    }
    printf("\n");
    free(d);
    return order;
}

void quantize(IplImage *img, int n, int *buf, int width)
{
    int i, j, k = n*n;
    int stride = img->widthStep/sizeof(int16_t), *qd = buf;
    int16_t *data = (int16_t*)img->imageData;
    if (!n) memset(data, 0, img->imageSize);
    if (k > width) {
        fprintf(stderr, "quantize: n must be < sqrt(width)\n");
        return;
    }
    for (i = 0; i < img->height; i+= 1) {
        if (i%8>=n) continue;
        for (j = 0; j < img->width; j+= 1) {
            if ((i%8 < n) && (j%8 < n)) {
                *qd++ = *(data+i*stride+j);
                k -= 1;
                if (!k) {
                    k = n*n;
                    buf += width;
                    qd = buf;
                }
            }
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

static void query_img(kd_tree *t, int *coeffs, int *order, CvSize s)
{
    int i, j, k = t->k;
    CvSize size = {s.width/8, s.height/8};
    IplImage *img = cvCreateImage(size, IPL_DEPTH_16U, 1);
    uint16_t *data = (uint16_t*)img->imageData, *line;
    cvNamedWindow("queried", 0);
    cvResizeWindow("queried", s.width, s.height);
    for (i = 0; i < img->height; i++) {
        line = data;
        for (j = 0; j < img->width; j++) {
            kd_node *n = kdt_query(t, coeffs, order);
            int dist = calc_dist(coeffs, n, k);
            *line++ = dist;
            coeffs += k;
        }
        data += img->widthStep/sizeof(uint16_t);
    }
    cvShowImage("queried", img);
    cvReleaseImage(&img);
}

static void dequantize(IplImage *img, int n, int *buf, int width)
{
    int i, j, k = n*n, *qd = buf;
    int stride = img->widthStep/sizeof(int16_t);
    int16_t *data = (int16_t*)img->imageData;
    for (i = 0; i < img->height; i++) {
        if (i%8 >= n) continue;
        for (j = 0; j < img->width; j++) {
            if ((i%8 < n) && (j%8 < n)) {
                *(data+i*stride+j) = *qd++;
                k -= 1;
                if (!k) {
                    k = n*n;
                    buf += width;
                    qd = buf;
                }
            }
        }
    }
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

    memset(trans->imageData, 0, trans->imageSize);
    dequantize(trans, 5, coeffs, dim);
    iwht2d(trans, l);
    memset(trans->imageData, 0, trans->imageSize);
    dequantize(trans, 1, coeffs+25, dim);
    iwht2d(trans, a);
    memset(trans->imageData, 0, trans->imageSize);
    dequantize(trans, 1, coeffs+26, dim);
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
    return img;
}


static void find_best_match(int *coeffs, int *newcoeffs, kd_node *n, int k)
{
    int i, j, best = INT_MAX, *u, *v;
    for (i = 0; i < n->nb; i++) {
        int dist = 0;
        u = coeffs;
        v = n->value+i*k;
        for (j = 0; j < k; j++) {
            int a = *u++;
            int b = *v++;
            dist += (a - b)*(a - b);
        }
        if (dist < best) {
            memcpy(newcoeffs, n->value+i*k, k*sizeof(int));
            best = dist;
        }
    }
}

static IplImage* match(kd_tree *t, int *coeffs, int *order, CvSize s)
{
    int i, j, k = t->k;
    CvSize size = {s.width/8, s.height/8};
    int *newcoeffs = malloc(sizeof(int)*size.width*size.height*k);
    int *c = newcoeffs;
    for (i = 0; i < size.height; i++) {
        for (j = 0; j < size.width; j++) {
            kd_node *n = kdt_query(t, coeffs, order);
            find_best_match(coeffs, newcoeffs, n, k);
            coeffs += k;
            newcoeffs += k;
        }
    }
    IplImage *img = splat(c, s, k);
    free(c);
    return img;
}

static int* get_coeffs(IplImage *img, int dim)
{
    if (dim != 27) return NULL; // temporary; nothing else supported

    CvSize size = cvGetSize(img);
    IplImage *lab = cvCreateImage(size, IPL_DEPTH_8U, 3);
    IplImage *l = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *a = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *b = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *trans = cvCreateImage(size, IPL_DEPTH_16S, 1);
    int sz = size.width*size.height/64*dim;
    int *buf = malloc(sizeof(int)*sz);

    cvCvtColor(img, lab, CV_BGR2Lab);
    cvSplit(lab, l, a, b, NULL);

    wht2d(l, trans);
    quantize(trans, 5, buf, dim);

    wht2d(a, trans);
    quantize(trans, 1, buf+25, dim);

    wht2d(b, trans);
    quantize(trans, 1, buf+26, dim);

    cvReleaseImage(&trans);
    cvReleaseImage(&lab);
    cvReleaseImage(&l);
    cvReleaseImage(&a);
    cvReleaseImage(&b);

    return buf;
}

int main()
{
    IplImage *src = alignedImageFrom("lena.png", 8);
    IplImage *dst = alignedImageFrom("eva.jpg", 8);
    CvSize size = cvGetSize(src);

    int dim = 27, sz = (size.width*size.height/64)*dim;
    int *c1 = get_coeffs(src, dim);
    int *c2 = get_coeffs(dst, dim);
    int *c3 = malloc(sz*sizeof(int));
    int *c4 = malloc(dst->width*dst->height/64*dim*sizeof(int));

    kd_tree kdt;

    cvShowImage("img", src);

    double start = get_time(), end;
    int *order = calc_dimstats(c1, sz/dim, dim);
    memcpy(c3, c1, sz*sizeof(int));
    memcpy(c4, c2, dst->width*dst->height/64*dim*sizeof(int));
    kdt_new(&kdt, c1, sz/dim, dim, order);
    end = get_time() - start;

    //IplImage *matched = match(&kdt, c4, order, cvGetSize(dst));
    IplImage *matched = match(&kdt, c3, order, size);

    cvShowImage("matched", matched);
    printf("\nelapsed %f ms\n", end*1000);
    cvWaitKey(0);
    kdt_cleanup(&kdt.root);
    free(c1);
    free(c2);
    free(c3);
    free(c4);
    free(order);
    cvReleaseImage(&src);
    cvReleaseImage(&dst);
    cvReleaseImage(&matched);
    return 0;
}
