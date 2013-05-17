#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui_c.h>

#include "gck.h"
#include "wht.h"
#include "select.h"

// maximum # of candidates per leaf
#define LEAF_CANDS 8

#define XY_TO_INT(x, y) (((y) << 16) | (x))
#define XY_TO_X(x) ((x)&((1<<16)-1))
#define XY_TO_Y(y) ((y)>>16)

#define PACK_SCOREIDX(s, i) ((uint64_t)((uint64_t)s << 32 | (i)))
#define UNPACK_SCORE(a) ((a) >> 32)
#define UNPACK_IDX(a) ((a)&(0xFFFFFFFF))

typedef struct kd_node {
    int val;
    int nb;
    int axis;
    struct kd_node *left;
    struct kd_node *right;
    int *value;
    int xy[LEAF_CANDS];
} kd_node;
typedef struct kd_tree {
    int k;
    int *order;
    int *points;
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

static kd_node *kdt_new_in(kd_tree *t, int *points,
    int nb_points, int depth)
{
    if (0 >= nb_points) return NULL;
    int axis = t->order[depth % t->k], median, loops = 1;
    kd_node *node = malloc(sizeof(kd_node));

    if (nb_points <= LEAF_CANDS) {
        node->value = points;
        node->left = node->right = NULL;
        node->nb = nb_points;
        return node;
    }
kdt_in:

    median = quick_select(points, nb_points, t->k, axis);
    node->value = points+median*t->k;
    node->val = node->value[axis];
    node->axis = axis;

    pivot_nd(points, nb_points, t->k, axis, node->val);

    while ((nb_points - (median+1)) &&
           (points+(median+1)*t->k)[axis] <= node->val) {
        // make nodes with the same value as the median at the axis
        // fall on the left side of the tree by bumping up the median
        node->value += t->k;
        median += 1;
    }
    if (!(nb_points - (median+1))) {
        depth += 1;
        axis = t->order[depth % t->k];
        loops++;
        if (loops == t->k) {
            // we have actually gone through every single element here
            // and each dimension is ALMOST the same as its neighbor
            // so search for uniques
            int *p = points, i = 0, r = 0, w = 0;
            for (r = 0; r < nb_points; r++) {
                int *q = points;
                for (i = 0; i < w; i++) {
                    if (!memcmp(p, q, t->k*sizeof(int))) break;
                    q += t->k;
                }
                if (i == w) {
                    int *u = points+w*t->k;
                    if (u != p) memcpy(u, p, t->k*sizeof(int));
                    w++;
                }
                p += t->k;
            }
            if (w > LEAF_CANDS) {
                nb_points = w;
                goto kdt_in;
            }
            node->left = node->right = NULL;
            node->nb = w;
            node->value = points;
            return node;
        }
        goto kdt_in;
    }

    node->left = kdt_new_in(t, points, median, depth + 1);
    node->right = kdt_new_in(t, points+(median+1)*t->k, nb_points - median - 1, depth+1);
    node->nb = 1;

    return node;
}

static kd_node* kdt_query_in(kd_node *n, int depth, int* qd, int dim)
{
    int k = n->axis;
    if (n->left == NULL && n->right == NULL) return n;
    if (!memcmp(qd, n->value, dim*sizeof(int))) return n;
    if (n->left && qd[k] <= n->val) {
        return kdt_query_in(n->left, depth+1, qd, dim);
    } else if (n->right && qd[k] > n->val) {
        return kdt_query_in(n->right, depth+1, qd, dim);
    }
    fprintf(stderr, "This path should never be taken\n");
    return n;
}

kd_node* kdt_query(kd_tree *t, int *points)
{
    return kdt_query_in(t->root, 0, points, t->k);
}

static void kdt_free_in(kd_node **n)
{
    kd_node *m = *n;
    if (m->left) kdt_free_in(&m->left);
    if (m->right) kdt_free_in(&m->right);
    free(m);
    *n = NULL;
}

static void kdt_free(kd_tree *t)
{
    if (t->order) free(t->order);
    kdt_free_in(&t->root);
    if (t->points) free(t->points);
}

typedef struct {
    int min, max, diff, idx;
} dimstats;

static inline int dim_compar(const void *a, const void *b)
{
    return ((dimstats*)b)->diff - ((dimstats*)a)->diff;
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
    //printf("Ordering: ");
    for (j = 0; j < dim; j++) {
        order[j] = (d+j)->idx;
        //printf("%d ", (d+j)->idx);
    }
    //printf("\n");
    free(d);
    return order;
}

void kdt_new(kd_tree *t, int *points, int nb_points, int k)
{
    t->points = malloc(nb_points*k*sizeof(int));
    memcpy(t->points, points, nb_points*k*sizeof(int));
    t->k = k; // dimensionality
    t->order = calc_dimstats(t->points, nb_points, k);
    t->root = kdt_new_in(t, t->points, nb_points, 0);
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

static void query_img(kd_tree *t, int *coeffs, CvSize s)
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
            kd_node *n = kdt_query(t, coeffs);
            int dist = calc_dist(coeffs, n, k);
            *line++ = dist;
            coeffs += k;
        }
        data += img->widthStep/sizeof(uint16_t);
    }
    cvShowImage("queried", img);
    cvReleaseImage(&img);
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
    int i, j, *u, *v;
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
    return best;
}

static int best_match_idx(int *coeffs, kd_node *n, int k)
{
    int i, j, *u, *v, best = INT_MAX, idx = -1;
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
            best = dist;
            idx = i;
        }
    }
    return idx;
}

static int find_match_idx(int *coeffs, kd_node *n, int k)
{
    int i, *v = n->value;
    for (i = 0; i < n->nb; i++) {
        if (!memcmp(coeffs, v, k*sizeof(int))) return i;
        v += k;
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

static kd_node** get_block_positions(kd_tree *t, int *coeffs, CvSize s)
{
    int i, j;
    CvSize size = {s.width/8, s.height/8};
    kd_node **nodes = malloc(size.width*size.height*sizeof(kd_node*));
    kd_node **n = nodes;
    for (i = 0; i < size.height; i++) {
        for (j = 0; j < size.width; j++) {
            kd_node *kdn = kdt_query(t, coeffs);
            int idx = find_match_idx(coeffs, kdn, t->k);
            if (-1 == idx) {
                fprintf(stderr, "Bad offset index (%d,%d)", j, i);
                print_tuple(coeffs, 1, t->k);
            }
            else kdn->xy[idx] = XY_TO_INT(j, i);
            *n++ = kdn;
            coeffs += t->k;
        }
    }
    return nodes;
}

static kd_node** get_positions(kd_tree *t, int *coeffs, CvSize s,
    int kern_size)
{
    int i, j;
    int w = s.width - kern_size + 1, h = s.height - kern_size + 1;
    kd_node **nodes = malloc(s.width*s.height*sizeof(kd_node*));
    kd_node **n = nodes;
    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            kd_node *kdn = kdt_query(t, coeffs);
            int idx = find_match_idx(coeffs, kdn, t->k);
            if (-1 == idx) {
                fprintf(stderr, "Bad offset index (%d,%d)", j, i);
                print_tuple(coeffs, 1, t->k);
            }
            else kdn->xy[idx] = XY_TO_INT(j, i);
            *n++ = kdn;
            coeffs += t->k;
        }
    }
    return nodes;
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

static void coeffs(IplImage *img, int bases, int total_b,
    int *planar, int *interleaved)
{
    CvSize s = cvGetSize(img);
    int w = s.width, h = s.height, rw = w - 8 + 1, rh = h - 8 + 1;
    if (w != img->widthStep) {
        fprintf(stderr, "image not aligned uh oh\n");
        exit(1);
    }
    int *res = gck_calc_2d((uint8_t*)img->imageData, w, h, 8, bases);
    gck_truncate_data(res, w, h, 8, bases, planar);
    gck_interleave_data(planar, rw, rh, bases, interleaved, total_b);
    free(res);
}

static void img_coeffs(IplImage *img, int dim, int **pl, int **in) {
    if (dim != 27) return; // temporary; nothing else supported

    CvSize size = cvGetSize(img);
    IplImage *lab = cvCreateImage(size, IPL_DEPTH_8U, 3);
    IplImage *l = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *a = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *b = cvCreateImage(size, IPL_DEPTH_8U, 1);
    int w = size.width, h = size.height;
    int rw = w - 8 + 1, rh = h - 8 + 1;
    int *planar = gck_alloc_buffer(w, h, 8, dim);
    int *interleaved = gck_alloc_buffer(w, h, 8, dim);

    cvCvtColor(img, lab, CV_BGR2Lab);
    cvSplit(lab, l, a, b, NULL);

    coeffs(l, 25, 27, planar, interleaved);

    coeffs(a, 1, 27, planar+(rw*rh*25), interleaved+25);

    coeffs(b, 1, 27, planar+(rw*rh*26), interleaved+26);

    cvReleaseImage(&lab);
    cvReleaseImage(&l);
    cvReleaseImage(&a);
    cvReleaseImage(&b);

    *pl = planar;
    *in = interleaved;
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
    if (!set_swap_buf(2)) exit(1);
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
    set_swap_buf(dim);
    c_src = block_coeffs(src, dim);
    c_dst = block_coeffs(dst, dim);
    kd_tree kdt;
    cvShowImage("img", src);
    printf("building tree\n");
    kdt_new(&kdt, c_src, sz, dim);
    printf("finished building tree\n");

    kd_node **pos = get_block_positions(&kdt, c_src, size);
    printf("got positions\n");
    free(pos);

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
    int *p, *i, *c_src;
    set_swap_buf(dim);
    img_coeffs(src, dim, &p, &i);
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
    free(p);
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
    int *p, *i;
    kd_tree kdt;

    set_swap_buf(dim);
    img_coeffs(src, dim, &p, &i);
    c_dst = block_coeffs(dst, dim);
    memset(&kdt, 0, sizeof(kdt));

    kdt_new(&kdt, i, sz, dim);
    IplImage *matched = match(&kdt, c_dst, cvGetSize(dst));
    IplImage *matched2 = match2(&kdt, c_dst, cvGetSize(dst));
    cvShowImage("src", src);
    cvShowImage("matched", matched);
    cvShowImage("matched2", matched2);
    cvWaitKey(0);

    kdt_free(&kdt);
    free(p);
    free(i);
    free(c_dst);
    cvReleaseImage(&matched);
    cvReleaseImage(&matched2);
    cvReleaseImage(&src);
    cvReleaseImage(&dst);
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

static int64_t match_score(int *coeffs, kd_node *n, int k)
{
    int i, j, *u, *v, best = INT_MAX, idx = -1;
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
            best = dist;
            idx = i;
        }
    }
    return PACK_SCOREIDX(best, idx);
}

static int64_t check_match(int *coeffs, kd_node *n, int k, int best)
{
    int64_t res = match_score(coeffs, n, k);
    int attempt = UNPACK_SCORE(res);
    if (attempt < best) return res;
    return -1;
}

static uint64_t best_match(kd_tree *t, int *coeffs, int x, int y,
    kd_node **prev, kd_node **map, int w, int h, int dir)
{
#define QSZ 10
#define CHKNSET(q) if ((res=check_match(coeffs,(q),k,best))!=-1) { \
    best = UNPACK_SCORE(res); \
    idx = UNPACK_IDX(res); \
    n = (q); \
    xy = n->xy[idx], x1 = XY_TO_X(xy), y1 = XY_TO_Y(xy); \
    if ((dir>0 && y1 < h-1) || (dir < 0 && y1)) ADDQ(map[(y1+dir)*w+x1]); \
    if ((dir>0 && x1 < w-1) || (dir < 0 && x1)) ADDQ(map[y1*w+x1+dir]); }

#define ADDQ(q) {if (qsize < QSZ) queue[qsize++] = (q);}

    int k = t->k, best = INT_MAX, idx = -1, qsize = 0, i, xy, x1, y1;
    int64_t res;
    kd_node *queue[QSZ];
    kd_node *n = kdt_query(t, coeffs);
    CHKNSET(n);

    // for better *perceptual* uniformity: check, but don't include
    // adjacent pixels. For *numerical* uniformity, include adjacent
    if (y) {
        res = check_match(coeffs, prev[0], k, best);
        if (res != -1) {
        xy = prev[0]->xy[UNPACK_IDX(res)];
        x1 = XY_TO_X(xy), y1 = XY_TO_Y(xy);
        if ((dir>0 && y1 < h-1) || (dir < 0 && y1)) ADDQ(map[(y1+dir)*w+x1]);
        if ((dir>0 && x1 < w-1) || (dir < 0 && x1)) ADDQ(map[y1*w+x1+dir]);
        }
    }

    if (x) {
        res = check_match(coeffs, prev[dir], k, best);
        if (res != -1) {
        xy = prev[dir]->xy[UNPACK_IDX(res)];
        x1 = XY_TO_X(xy), y1 = XY_TO_Y(xy);
        if ((dir>0 && y1 < h-1) || (dir < 0 && y1)) ADDQ(map[(y1+dir)*w+x1]);
        if ((dir>0 && x1 < w-1) || (dir < 0 && x1)) ADDQ(map[y1*w+x1+dir]);
        }
    }

    //if (y) ADDQ(prev[0]); if (x) ADDQ(prev[dir]);
    //if (y) CHKNSET(prev[0]); if (x) CHKNSET(prev[dir]);
    for (i = 0; i < qsize; i++) CHKNSET(queue[i]);
    *prev = n;
    return PACK_SCOREIDX(best, n->xy[idx]);

#undef QSZ
#undef CHKNSET
#undef ADDQ
}

IplImage* match_complete(kd_tree *t, int *coeffs, IplImage *src,
    kd_node **xy_map, CvSize dst_size)
{
    IplImage *xy = cvCreateImage(dst_size, IPL_DEPTH_32S, 1);
    IplImage *dst = cvCreateImage(dst_size, IPL_DEPTH_8U, 3);
    int w = dst_size.width  - 8 + 1, h = dst_size.height - 8 + 1;
    int k = t->k, sz = w*h, i, sw = src->width - 8 + 1;
    int sh = src->height - 8 + 1;
    kd_node **nodes = malloc(w*sizeof(kd_node*)), **node;
    int *scores = malloc(sz*sizeof(int)), *s = scores;
    int *coeffsb = coeffs+(sz-1)*k;
    int *xydata = (int*)xy->imageData;
    for (i = 0; i < sz; i++) {
        int x = i % w, y = i/w, sx, sy, sxy;
        uint64_t res;
        if (!x) {
            node = nodes;
            xydata = (int*)xy->imageData + y*(xy->widthStep/sizeof(int));
        }
        res = best_match(t, coeffs, x, y, node, xy_map, sw, sh, -1);
        sxy = UNPACK_IDX(res); sx = XY_TO_X(sxy); sy = XY_TO_Y(sxy);
        *scores++ = UNPACK_SCORE(res);
        *xydata++ = sxy;
        if (sx >= src->width || sy >= src->height) {
            printf("grievous error: got %d,%d but dims %d,%d sxy %d\n", sx, sy, src->width, src->height, sxy);
        }
        coeffs += k;
        node++;
    }
/*
    // reversing contributes very little; eliminate?
    for (i = sz; i >= 0; i--) {
        int x = i % w, y = i/w, sx, sy, sxy, score;
        int rx = (sz - i) % w, ry = (sz - i)/w;
        uint64_t res;
        if (!rx) {
            node = nodes+w-1;
            xydata = (int*)xy->imageData + y*(xy->widthStep/sizeof(int));
        }
        res = best_match(t, coeffsb, rx, ry, node, xy_map, sw, sh, 1);
        sxy = UNPACK_IDX(res); sx = XY_TO_X(sxy); sy = XY_TO_Y(sxy);
        score = UNPACK_SCORE(res);
        if (sx >= src->width || sy >= src->height) {
            printf("grievous error: got %d,%d but dims %d,%d sxy %d\n", sx, sy, src->width, src->height, sxy);
        }
        if (s[i] > score) xydata[x] = sxy;
        coeffsb -= k;
        node--;
    }
*/
    free(s);
    free(nodes);
    xy2img(xy, src, dst);
    cvReleaseImage(&xy);
    return dst;
}

static int match_score3(int *coeffs, kd_node *n, int k)
{
    int i, j, *u, *v, best = INT_MAX;
    for (i = 0; i < n->nb; i++) {
        int dist = 0;
        u = coeffs;
        v = n->value+i*k;
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
        sxy = n->xy[idx]; sx = XY_TO_X(sxy); sy = XY_TO_Y(sxy);
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
        sxy = n->xy[idx]; sx = XY_TO_X(sxy); sy = XY_TO_Y(sxy);
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
    //IplImage *src = alignedImageFrom("frames/bbb22.png", 8);
    //IplImage *dst = alignedImageFrom("frames/bbb19.png", 8);
    IplImage *src = alignedImageFrom("lena.png", 8);
    IplImage *dst = alignedImageFrom("eva.jpg", 8);
    CvSize src_size = cvGetSize(src), dst_size = cvGetSize(dst);
    IplImage *diff = cvCreateImage(dst_size, IPL_DEPTH_8U, 3);
    IplImage *diff3 = cvCreateImage(dst_size, IPL_DEPTH_8U, 3);
    IplImage *diff2 = cvCreateImage(dst_size, IPL_DEPTH_8U, 3);
    int w1 = src_size.width - 8 + 1, h1 = src_size.height - 8 + 1;
    int dim = 27, sz = w1*h1;
    int *p, *i, *dp, *di;
    kd_tree kdt;

    set_swap_buf(dim);
    img_coeffs(src, dim, &p, &i);
    img_coeffs(dst, dim, &dp, &di);
    memset(&kdt, 0, sizeof(kdt));

    kdt_new(&kdt, i, sz, dim);
    kd_node** map = get_positions(&kdt, i, src_size, 8);
    IplImage *matched = match_complete(&kdt, di, src, map, dst_size);
    IplImage *matched3 = match_complete3(&kdt, di, src, dst_size);
    IplImage *matched2 = match_complete2(&kdt, di, src, dst_size);
    cvShowImage("original", dst);
    cvShowImage("complete match3", matched3);
    cvShowImage("complete match2", matched2);
    cvShowImage("complete match", matched);
    cvAbsDiff(dst, matched, diff);
    cvAbsDiff(dst, matched3, diff3);
    cvAbsDiff(dst, matched2, diff2);
    //cvShowImage("diff2", diff2);
    //cvShowImage("diff", diff);
    //cvShowImage("diff3", diff3);
    cvWaitKey(0);

    printf("diffsum : %lld\n", sumimg(diff, 8));
    printf("diff2sum: %lld\n", sumimg(diff2, 8));
    printf("diff3sum: %lld\n", sumimg(diff3, 8));

    kdt_free(&kdt);
    free(p);
    free(i);
    free(map);
    cvReleaseImage(&src);
    cvReleaseImage(&dst);
    cvReleaseImage(&matched);
    cvReleaseImage(&matched2);
    cvReleaseImage(&matched3);
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
