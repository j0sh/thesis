#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

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

    if (nb_points <= 8) {
        node->value = points;
        node->left = node->right = NULL;
        node->nb = nb_points;
        return node;
    }
    qsort_r(points, nb_points, t->k*sizeof(int), &kdt_compar, &axis);
    median = nb_points/2;
    node->value = points+median*t->k;
    node->left = kdt_new_in(t, points, median, depth + 1, order);
    node->right = kdt_new_in(t, points+(median+1)*t->k, nb_points - median - 1, depth+1, order);
    node->nb = 1;

    return node;
}

void kdt_new(kd_tree *t, int *points, int nb_points, int k,
    int *order)
{
    t->k = k; // dimensionality
    t->root = kdt_new_in(t, points, nb_points, k, order);
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
    int i, j;
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
    int *order = malloc(dim*sizeof(int));
    printf("Ordering: ");
    for (j = 0; j < dim; j++) {
        order[j] = (d+j)->idx;
        printf("%d ", (d+j)->idx);
    }
    printf("\n");
    return order;
}

int main()
{
    //int sz = 240000000, dim = 24, *a = malloc(sz * sizeof(int));
    int sz = 50, dim = 10, *a = malloc(sz*sizeof(int));
    int i, j;
    for (i = 0; i < sz; i += dim) {
        for (j = 0; j < dim; j++) a[i + j] = rand() % 262144;
    }

    int *order = calc_dimstats(a, sz/dim, dim);
    kd_tree kdt;
    double start = get_time(), end;
    kdt_new(&kdt, a, sz/dim, dim, order);
    end = get_time() - start;

    for (i = 0; i < sz; i+= dim) {
        printf("(");
        for (j = 0; j < dim; j++) {
            printf("%d ", a[i+j]);
        }
        printf(")\n ");
    }

    printf("\n\n");
    print_kdtree(kdt.root, kdt.k, 0, order);

    printf("\nelapsed %f ms\n", end*1000);
    free(a);
    free(order);
    return 0;
}
