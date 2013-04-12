#include <stdio.h>

#include <opencv2/highgui/highgui_c.h>
#include <opencv2/imgproc/imgproc_c.h>

#include "wht.h"

// number of nxn Walsh-Hadamard kernels
#define KERNS 8

typedef struct GCKPoint {
    int x, y;
} GCKPoint;


#include <sys/time.h>
static inline double get_time()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec * 1e-6;
}

static IplImage* make_dc(IplImage *img)
{
    CvSize igsz = {img->width+1, img->height+1};
    IplImage *ig = cvCreateImage(igsz, IPL_DEPTH_32S, img->nChannels);
    IplImage *dc = cvCreateImage(cvGetSize(img), IPL_DEPTH_32S, img->nChannels);
    int i, j;

    double start = get_time(), end;
    cvIntegral(img, ig, NULL, NULL);

    uint8_t *d = (uint8_t*)ig->imageData;
    int stride = ig->widthStep/sizeof(int);
    int istride = dc->widthStep/sizeof(int);
    int *data = (int*)d, *line;
    int *data_k = data + KERNS, *line_k;
    int *data_b = data + stride*KERNS, *line_b;
    int *data_bk = data_k + stride*KERNS, *line_bk;
    int *idata = (int*)dc->imageData, *iline;

    for (i = 0; i < img->height - KERNS; i++) {
        line = data;
        line_k = data_k;
        line_b = data_b;
        line_bk = data_bk;
        iline = idata;
        for (j = 0; j < img->width - KERNS; j++) {
#define LINE *iline++ = *line_bk++ + *line++ - *line_k++ - *line_b++
            switch(img->nChannels) {
            case 3: LINE;
            case 2: LINE;
            case 1: LINE;
            }
#undef LINE
        }
        data += stride;
        data_k += stride;
        data_b += stride;
        data_bk += stride;
        idata += istride;
    }

    end = get_time() - start;
    printf("elapsed %f ms\n", end*1000);
    cvReleaseImage(&ig);
    return dc;
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

static void splat_dc(IplImage *dense, IplImage *dc)
{
    int i, j, w = dense->width, h = dense->height;
    int16_t *data = (int16_t*)dc->imageData, *line = data;
    int *dense_data = (int*)dense->imageData, *dline = dense_data;
    memset(data, 0, dc->imageSize);
    for (i = 0; i < h; i+=KERNS) {
        line = data;
        dline = dense_data;
        for (j = 0; j < w; j+=KERNS) {
            *line = *dline;
            line += KERNS;
            dline += KERNS;
        }
        data += (dc->widthStep/sizeof(int16_t))*KERNS;
        dense_data += (dense->widthStep/sizeof(int))*KERNS;
    }
}

static void test_dc(IplImage *img)
{
    CvSize size = cvGetSize(img);
    IplImage *lab = cvCreateImage(size, IPL_DEPTH_8U, 3);
    IplImage *l = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *a = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *b = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *wht = cvCreateImage(size, IPL_DEPTH_16S, 1);
    IplImage *wht_l = cvCreateImage(size, IPL_DEPTH_16S, 1);
    IplImage *wht_a = cvCreateImage(size, IPL_DEPTH_16S, 1);
    IplImage *wht_b = cvCreateImage(size, IPL_DEPTH_16S, 1);
    IplImage *wht_lab = cvCreateImage(size, IPL_DEPTH_16S, 3);
    IplImage *dc;

    cvShowImage("img", img);
    cvCvtColor(img, lab, CV_BGR2Lab);
    cvSplit(lab, l, a, b, NULL);

    dc = make_dc(l);
    splat_dc(dc, wht);
    iwht2d(wht, wht_l);
    cvReleaseImage(&dc);

    dc = make_dc(a);
    splat_dc(dc, wht);
    iwht2d(wht, wht_a);
    cvReleaseImage(&dc);

    dc = make_dc(b);
    splat_dc(dc, wht);
    iwht2d(wht, wht_b);
    cvReleaseImage(&dc);

    cvMerge(wht_l, wht_a, wht_b, NULL, wht_lab);
    cvConvertScale(wht_lab, lab, 1, 0);
    cvCvtColor(lab, img, CV_Lab2BGR);
    cvShowImage("reconstructed", img);

    cvReleaseImage(&l);
    cvReleaseImage(&a);
    cvReleaseImage(&b);
    cvReleaseImage(&lab);
    cvReleaseImage(&wht_lab);
    cvReleaseImage(&wht);
    cvReleaseImage(&wht_l);
    cvReleaseImage(&wht_a);
    cvReleaseImage(&wht_b);

}

static void gck_zigzag(GCKPoint *p, int kern_size, int bases)
{
    GCKPoint *r = p, *w = r + 1;

    // initialize first element
    r->x = 0; r->y = 0;
    bases -= 1;
    kern_size -= 1; // check from 0...(n - 1), not 1...n

    while (bases) {
        if (r->x == 0 && r->y < kern_size) {
            w->x = 0;
            w->y = r->y + 1;
            w++;
            bases--;
            if (!bases) break;
        }
        if (r->x >= kern_size){
            r++;
            continue;
        }
        w->y = r->y;
        w->x = r->x + 1;
        w++;
        r++;
        bases--;
    }
}

static int gck_gc(int a)
{
    // return the gray code representation of a
    return (a >> 1) ^ a;
}

static int gck_prefix(int a, int b, int bits)
{
    // find the common bitwise prefix between a and b.
    int c = 0, n = 1 << bits;
    while (n && (a & n) == (b & n)) {
        c += 1;
        n >>= 1;
    }
    return c;
}

static void print_data(int *data, int w, int h)
{
    int i, j;
    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            printf("%3d ", data[j + i * w]);
        }
        printf("\n");
    }
}

static void gck_2d_dc_in(uint8_t *data, int *dc, int data_len, int kern_size)
{
    int i, out_w = data_len + kern_size - 1;
    dc[0] = data[0];
    for (i = 1; i < kern_size; i++) {
        dc[i] = data[i] + dc[i-1];
    }
    for (; i < data_len; i++) {
        dc[i] = data[i] + dc[i-1] - data[i - kern_size];
    }
    for (; i < out_w; i++) {
        dc[i] = dc[i - 1] - data[i - kern_size];
    }
}

static void gck_2d_dc_1(uint8_t *data, int *dc, int data_len, int kern_size, int *out)
{
    int i, out_w = data_len + kern_size - 1;
    dc[0] = data[0];
    for (i = 1; i < kern_size; i++) {
        dc[i] = data[i] + dc[i-1];
    }
    for (; i < data_len; i++) {
        dc[i] = data[i] + dc[i-1] - data[i - kern_size];
    }
    for (; i < out_w; i++) {
        dc[i] = dc[i - 1] - data[i - kern_size];
    }
    memcpy(out, dc, out_w*sizeof(int));
}

static void gck_2d_dc_2(uint8_t *data, int *dc, int data_len, int kern_size, int *out, int *ppr)
{
    int i, out_w = data_len + kern_size - 1;
    dc[0] = data[0];
    out[0] = dc[0] + ppr[0];
    for (i = 1; i < kern_size; i++) {
        dc[i] = data[i] + dc[i-1];
        out[i] = dc[i] + ppr[i];
    }
    for (; i < data_len; i++) {
        dc[i] = data[i] + dc[i-1] - data[i - kern_size];
        out[i] = dc[i] + ppr[i];
    }
    for (; i < out_w; i++) {
        dc[i] = dc[i - 1] - data[i - kern_size];
        out[i] = dc[i] + ppr[i];
    }
}

static void gck_2d_dc_3(uint8_t *data, int *dc, int data_len, int kern_size, int *out, int *ppr, int *pr)
{
    int i, out_w = data_len + kern_size - 1;
    dc[0] = data[0];
    out[0] = dc[0] + ppr[0] - pr[0];
    for (i = 1; i < kern_size; i++) {
        dc[i] = data[i] + dc[i-1];
        out[i] = dc[i] + ppr[i] - pr[i];
    }
    for (; i < data_len; i++) {
        dc[i] = data[i] + dc[i-1] - data[i - kern_size];
        out[i] = dc[i] + ppr[i] - pr[i];
    }
    for (; i < out_w; i++) {
        dc[i] = dc[i - 1] - data[i - kern_size];
        out[i] = dc[i] + ppr[i] - pr[i];
    }
}

static void gck_2d_dc_4(int data_len, int kern_size, int *out, int *ppr, int *pr)
{
    int i, out_w = data_len + kern_size - 1;
    for (i = 0; i < out_w; i++) {
        out[i] = ppr[i] - pr[i];
    }
}

static void gck_2d_dc(uint8_t *udata, int *dc, int data_w, int data_h, int kern_size)
{
    int i, j, out_w = kern_size + data_w - 1, size;
    int *pr = NULL; // previous row
    int *ppr = NULL;
    int *t, *u, *v, *w, *data, *odc = dc; // preserve original
    double start, end;
    if (data_w < kern_size || data_h < kern_size) {
        fprintf(stderr, "Kernel larger than data. Exiting\n");
        exit(1);
    }
    size = (data_w + kern_size - 1) * (data_h + kern_size - 1);
    t = u = v = w = calloc(size, sizeof(int));

    gck_2d_dc_1(udata, u, data_w, kern_size, dc);
    for (i = 1; i < kern_size; i++) {
        v = dc;
        u += out_w;
        udata += data_w;
        dc += out_w;
        gck_2d_dc_2(udata, u, data_w, kern_size, dc, v);
    }
    for (; i < data_h; i++) {
        v = dc;
        u += out_w;
        udata += data_w;
        dc += out_w;
        gck_2d_dc_3(udata, u, data_w, kern_size, dc, v, w);
        w += out_w;
    }
    for (; i < data_h + kern_size - 1; i++) {
        v = dc;
        dc += out_w;
        gck_2d_dc_4(data_w, kern_size, dc, v, w);
        w += out_w;
    }

/*
    // horizontal
    for (i = 0; i < data_h; i++) {
        gck_2d_dc_in(udata, u, data_w, kern_size);
        udata += data_w;
        u += out_w;
    }

    ppr = t;
    data = t + out_w;
    pr = odc;
    dc = odc + out_w;
    memcpy(odc, t, size*sizeof(int));

    // vertical
    for (i = 1; i < kern_size; i++) {
        for (j = 0; j < out_w; j++) dc[j] = data[j] + pr[j];
        dc += out_w;
        pr += out_w;
        data += out_w;
    }

    for (; i < data_h; i++) {
        for (j = 0; j < out_w; j++) dc[j] = data[j] + pr[j] - ppr[j];
        dc += out_w;
        pr += out_w;
        ppr += out_w;
        data += out_w;
    }
    for (; i < data_h + kern_size - 1; i++) {
        for (j = 0; j < out_w; j++) dc[j] = pr[j] - ppr[j];
        dc += out_w;
        pr += out_w;
        ppr += out_w;
    }
*/

    free(t);
}

static void gck_2d_horiz(int *cur, int a, int *prev, int b,
    int w, int h, int klen)
{
    int i, j, len = w + klen - 1, hlen = h + klen - 1;
    int bits = log2(klen) - 1; // kernel length, in bits
    int c = gck_gc(a), d = gck_gc(b);
    int prefix = gck_prefix(d, c, bits);
    int delta = 1 << prefix;
    int sign = (c >> (bits - prefix)) & 1;
    for (j = 0; j < hlen; j++) {
    for (i = 0; i < delta; i++) cur[i] = -prev[i];
    for (; i < len; i++) {
        if (sign) cur[i] = prev[i - delta] - cur[i - delta] - prev[i];
        else cur[i] = cur[i - delta] - prev[i - delta] - prev[i];
    }
    cur += len;
    prev += len;
    }
}

static void gck_2d_vert(int *cur, int a, int *prev, int b,
    int w, int h, int klen)
{
    int i, j, hlen = h + klen - 1, wlen = w + klen - 1;
    int bits = log2(klen) - 1; // kernel length, in bits
    int c = gck_gc(a), d = gck_gc(b);
    int prefix = gck_prefix(c, d, bits);
    int delta = 1 << prefix;
    int sign = (c >> d) & 1;
    int *curd = cur, *prevd = prev;
    for (i = 0; i < delta; i++) {
        for (j = 0; j < wlen; j++) cur[j] = -prev[j];
        cur += wlen;
        prev += wlen;
    }
    for (; i < hlen; i++) {
        for (j = 0; j < wlen; j++) {
            if (sign) cur[j] = prevd[j] - curd[j] - prev[j];
            else cur[j] = curd[j] - prevd[j] - prev[j];
        }
        cur += wlen;
        curd += wlen;
        prev += wlen;
        prevd += wlen;
    }
}

static void gck_get_adj(GCKPoint *p1, GCKPoint *p2)
{
    // get adjacent/ancestor points by undoing the zigzag construction
    if (!p1->y) {
        if (!p1->x) {
            p2->x = p2->y = 0;
            return;
        }
        p2->x = p1->x - 1;
        p2->y = 0;
        return;
    }
    p2->y = p1->y - 1;
    p2->x = p1->x;
}

static int gck_adj_idx(GCKPoint *path, GCKPoint *p, int idx)
{
    // linear search for index of adjacency. Given a point, the
    // adjacency will always be before it, so search backwards.
    while (idx >= 0) {
        if (path->x == p->x && path->y == p->y) return idx;
        path--;
        idx--;
    }
    fprintf(stderr, "Point does not exist in path\n");
    return -1;
}

static void print_path(GCKPoint *path, int nb)
{
    int i, j;
    char *m = malloc(nb*nb);
    memset(m, ' ', nb*nb);
    for (i = 0; i < nb; i++) {
        GCKPoint p;
        gck_get_adj(&path[i], &p);
        j = gck_adj_idx(&path[i], &p, i);
        printf("%2d : (%d %d) adj %d - (%d %d)\n", i+1, path[i].x, path[i].y, j, p.x, p.y);
        m[path[i].y * nb + path[i].x] += '+';
    }
    free(m);
}

static int gck_direction(GCKPoint *p1, GCKPoint *p2)
{
    // return 0 for vertical shifts, 1 for horizontal 
    if (p1->x == p2->x) return 0;
    return 1;
}

static int* gck_calc_2d(uint8_t *data, int w, int h, int kern_size, int bases)
{
    int i, wh = (w+kern_size - 1) * (h + kern_size - 1);
    int size = wh * bases;
    int *res = calloc(size, sizeof(int)); // TODO cacheline padding?
    GCKPoint adj, *path = malloc(bases * sizeof(GCKPoint));
    gck_zigzag(path, kern_size, bases);
    //print_path(path, bases);

    gck_2d_dc(data, res, w, h, kern_size);
//printf("  1:\n");
//print_data(res, w+kern_size - 1, h+kern_size - 1);
//printf("\n");
    for (i = 1; i < bases; i++) {
        gck_get_adj(&path[i], &adj);
        int j = gck_adj_idx(&path[i], &adj, i);
        if (-1 == j) return NULL;
        int horiz = gck_direction(&path[i], &adj);
        int *prev = res + j * wh;
        int *cur = res + i * wh;
        if (horiz) gck_2d_horiz(cur, path[i].x, prev, adj.x, w, h, kern_size);
        else gck_2d_vert(cur, path[i].y, prev, adj.y, w, h, kern_size);
//printf("%3d: (%d %d) adj %d - (%d %d)\n", i+1, path[i].x, path[i].y, j+1, adj.x, adj.y);
//print_data(cur, w+kern_size - 1, h+kern_size - 1);
//printf("\n");
    }

    return res;
}

static void prep_data(uint8_t *data, int w, int h)
{
    int i, j;
    for (i = 0; i < w; i++) {
        for (j = 0; j < h; j++) {
            int k = (i & 1) ? 8 - j : j + 1;
            data[j + i * w] = k;
            printf("%4d", k);
        }
        printf("\n");
    }
}

#define BASES 16
#define KERN 16
int main()
{
    /*IplImage *img = alignedImageFrom("eva.jpg", KERNS);
    IplImage *r = cvCreateImage(cvGetSize(img), img->depth, 1);
    int *imageData = (uint8_t*)r->imageData;
    double t, e;*/
    int i, *res, *res2;
    uint8_t data[64];

    prep_data(data, 8, 8);
    res = res2 = gck_calc_2d(data, 8, 8, 4, 16);
    for (i = 0; i < 16; i++) {
        printf("%4d:\n", i);
        print_data(res2, 8+4-1, 8+4-1);
        printf("\n");
        res2 += (8+4-1)*(8+4-1);
    }

    /*test_dc(img);
    cvSplit(img, r, NULL, NULL, NULL);
    t = get_time();
    res = gck_calc_2d(imageData, r->widthStep, r->height, 16, 16);
    e = get_time() - t;
    cvReleaseImage(&img);
    cvReleaseImage(&r);
    printf("%f\n", e*1000);*/

    free(res);
    return 0;
}
#undef KERNS
