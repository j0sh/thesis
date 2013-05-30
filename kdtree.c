#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "kdtree.h"
#include "select.h"

// maximum # of candidates per leaf
#define LEAF_CANDS 8

static void print_tuple(int **a, int nb, int tsz)
{
    int i, j;
    for (i = 0; i < nb; i++) {
        int *p = a[i];
        fprintf(stderr, "{");
        for (j = 0; j < tsz; j++) {
            fprintf(stderr, "%4d,", p[j]);
        }
        fprintf(stderr, "}\n");
    }
}

static kd_node *kdt_new_in(kd_tree *t, int **points,
    int nb_points, int depth)
{
    if (0 >= nb_points) return NULL;
    int axis = t->order[depth % t->k], median, loops = 1, pos;
    int completelybroken = 0;
    kd_node *node = &t->nodes[t->nb_nodes++];

    if (nb_points <= LEAF_CANDS) {
        int i, **p = points;
        for (i = 0; i < nb_points; i++) {
            pos = (*p++ - t->start)/t->k;
            t->map[pos] = node;
        }
        node->value = points;
        node->left = node->right = NULL;
        node->nb = nb_points;
        return node;
    }
kdt_in:

    median = quick_select(points, nb_points, axis);
    node->value = points+median;
    node->val = node->value[0][axis];
    node->axis = axis;

    pivot(points, nb_points, axis, node->val);

    while (!completelybroken && (nb_points - (median+1)) &&
           points[median+1][axis] <= node->val) {
        // make nodes with the same value as the median at the axis
        // fall on the left side of the tree by bumping up the median
        node->value += 1;
        median += 1;
    }
    if (!(nb_points - (median+1))) {
        depth += 1;
        axis = t->order[depth % t->k];
        loops++;
        if (loops >= t->k) {
            // we have actually gone through every single element here
            // and each dimension is ALMOST the same as its neighbor
            // so search for uniques
            int **p = points, i = 0, r = 0, w = 0;
            for (r = 0; r < nb_points; r++) {
                int **q = points;
                for (i = 0; i < w; i++) {
                    if (!memcmp(*p, *q, t->k*sizeof(int))) break;
                    q += 1;
                }
                if (i == w) points[w++] = *p;
                pos = (*p - t->start)/t->k;
                t->map[pos] = node;
                p += 1;
            }
            if (w == r) completelybroken = 1;
            if (w > LEAF_CANDS) {
                nb_points = w;
                loops = 1;
                goto kdt_in;
            }
            node->left = node->right = NULL;
            node->nb = w;
            node->value = points;
            return node;
        }
        goto kdt_in;
    }

    pos = (node->value[0] - t->start)/t->k;
    t->map[pos] = node;

    node->left = kdt_new_in(t, points, median, depth + 1);
    node->right = kdt_new_in(t, points+median+1, nb_points - median - 1, depth+1);
    node->nb = 1;

    return node;
}

static kd_node* kdt_query_in(kd_node *n, int depth, int* qd, int dim)
{
    int k = n->axis;
    if (n->left == NULL && n->right == NULL) return n;
    if (!memcmp(qd, n->value[0], dim*sizeof(int))) return n;
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

void kdt_free(kd_tree *t)
{
    if (t->order) free(t->order);
    if (t->points) free(t->points);
    if (t->nodes) free(t->nodes);
    if (t->map) free(t->map);
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
    int i;
    t->points = malloc(nb_points*sizeof(int*));
    t->map = malloc(nb_points*sizeof(kd_node*));
    t->nodes = malloc(nb_points*sizeof(kd_node));
    for (i = 0; i < nb_points; i++) t->points[i] = points+i*k;
    t->nb_nodes = 0;
    t->start = points;
    t->k = k; // dimensionality
    t->order = calc_dimstats(points, nb_points, k);
    t->root = kdt_new_in(t, t->points, nb_points, 0);
}
