// Josh Allmann, 11 April 2013.
// Based on "The Gray Code Filter Kernels" by G. Ben-Artzi, et al.
// http://cs.haifa.ac.il/~hagit/papers/PAMI07-GrayCodeKernels.pdf
// to compile: gcc -o gck2d gck2d.c -lm

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

typedef struct GCKPoint {
    int x, y;
} GCKPoint;

static void gck_path(GCKPoint *p, int kern_size, int bases)
{
    // Calculate the route for traversal between NxN kernels.
    // Roughly follows a diagonal pattern starting from the top-left.

    GCKPoint *r = p, *w = r + 1;

    // initialize first element
    r->x = 0; r->y = 0;
    bases -= 1;
    kern_size -= 1; // check from 0...(N - 1), not 1...N

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

static void gck_2d_dc(uint8_t *udata, int *dc, int data_w, int data_h, int kern_size)
{
    int i, j, out_w = kern_size + data_w - 1, size;
    int *pr = NULL; // previous row
    int *ppr = NULL;
    int *t, *u, *data, *odc = dc; // preserve original
    if (data_w < kern_size || data_h < kern_size) {
        fprintf(stderr, "Kernel larger than data. Exiting\n");
        exit(1);
    }
    size = (data_w + kern_size - 1) * (data_h + kern_size - 1);
    t = u = calloc(size, sizeof(int));
    ppr = t;
    data = t + out_w;
    pr = odc;
    dc = odc + out_w;

    // horizontal
    for (i = 0; i < data_h; i++) {
        gck_2d_dc_in(udata, u, data_w, kern_size);
        udata += data_w;
        u += out_w;
    }

    // vertical
    memcpy(odc, t, size*sizeof(int));
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
    // get adjacent points by undoing the diagonal construction
    if (!p1->x) {
        if (!p1->y) {
            p2->x = p2->y = 0;
            return;
        }
        p2->y = p1->y - 1;
        p2->x = 0;
        return;
    }
    p2->x = p1->x - 1;
    p2->y = p1->y;
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
    printf("Traversal Path for Kernels\n");
    for (i = 0; i < nb; i++) {
        GCKPoint p;
        gck_get_adj(&path[i], &p);
        j = gck_adj_idx(&path[i], &p, i);
        printf("%2d : (%d %d) adjacency %d - (%d %d)\n", i, path[i].x, path[i].y, j, p.x, p.y);
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

int* gck_calc_2d(uint8_t *data, int w, int h, int kern_size, int bases)
{
    int i, wh = (w+kern_size - 1) * (h + kern_size - 1);
    int size = wh * bases;
    int *res = calloc(size, sizeof(int)); // TODO cacheline padding?
    GCKPoint adj, *path = malloc(bases * sizeof(GCKPoint));
    gck_path(path, kern_size, bases);
    print_path(path, bases);

    gck_2d_dc(data, res, w, h, kern_size);
    for (i = 1; i < bases; i++) {
        gck_get_adj(&path[i], &adj);
        int j = gck_adj_idx(&path[i], &adj, i);
        if (-1 == j) return NULL;
        int horiz = gck_direction(&path[i], &adj);
        int *prev = res + j * wh;
        int *cur = res + i * wh;
        if (horiz) gck_2d_horiz(cur, path[i].x, prev, adj.x, w, h, kern_size);
        else gck_2d_vert(cur, path[i].y, prev, adj.y, w, h, kern_size);
    }

    free(path);
    return res;
}

int* gck_valid_data(int *data, int w, int h,
    int kern_size, int bases)
{
    // returns data from non-padded areas
    int kw = w + kern_size - 1, i, j, kh = h + kern_size - 1;
    int rw = w - kern_size + 1, rh = h - kern_size + 1;
    int *a = malloc(bases*rw*rh*sizeof(int)), *dst = a, *src = data;
    if (!a) {
        fprintf(stderr, "Unable to malloc in valid_data\n");
        return NULL;
    }
    for  (j = 0; j < bases; j++) {
        src = data + kw*kh*j;
        src += kw * (kern_size - 1) + kern_size - 1;
        for (i = 0; i < rh; i++) {
            memcpy(dst, src, rw*sizeof(int));
            dst += rw;
            src += kw;
        }
    }
    return a;
}

int* gck_interleave_data(int *data, int w, int h, int bases)
{
    // takes AAAABBBBCCCC -> ABCABCABCABC
    int i, j, k, *a = malloc(w*h*bases*sizeof(int));
    if (!a) {
        fprintf(stderr, "Unable to malloc in interleave_data\n");
        return NULL;
    }
    for (k = 0; k < bases; k++) {
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                int n = i*w+j;
                a[n*bases + k] = data[n];
            }
        }
        data += w*h;
    }
    return a;
}

static void prep_data(uint8_t *data, int w, int h)
{
    int i, j;
    printf("Original Data:\n");
    for (i = 0; i < w; i++) {
        for (j = 0; j < h; j++) {
            int k = (i & 1) ? 8 - j : j + 1;
            data[j + i * w] = k;
            printf("%4d", k);
        }
        printf("\n");
    }
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

static void print_bases(int *data, int w, int h,
    int kern_size, int bases)
{
    int i, kw = w+kern_size-1, kh = h+kern_size-1;
    printf("Filtered Results, Per Base:\n");
    for (i = 0; i < bases; i++) {
        printf("%4d:\n", i);
        print_data(data, kw, kh);
        printf("\n");
        data += kw*kh;
    }
}

#define W 8
#define H 8
#define KERN_LEN 4
#define BASES 16

#if 0
int main()
{
    int *res, *valid_res, *interleaved;
    uint8_t data[W*H];

    prep_data(data, W, H);
    res = gck_calc_2d(data, W, H, KERN_LEN, BASES);
    valid_res = gck_valid_data(res, W, H, KERN_LEN, BASES);
    interleaved = gck_interleave_data(valid_res,
                    W-KERN_LEN+1, H-KERN_LEN+1, BASES);
    print_bases(res, W, H, KERN_LEN, BASES);
    print_bases(valid_res, W-KERN_LEN+1, H-KERN_LEN+1, 1, BASES);
    print_bases(interleaved, KERN_LEN, KERN_LEN, 1, BASES);
    free(res);
    free(valid_res);
    free(interleaved);

    return 0;
}
#endif

#undef W
#undef H
#undef KERN_LEN
#undef BASES
