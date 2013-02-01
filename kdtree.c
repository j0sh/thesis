#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct kd_node {
    void *value;
    struct kd_node *left;
    struct kd_node *right;
} kd_node;

typedef struct kd_tree {
    int k;
    kd_node *root;
} kd_tree;

static void kdt_insert_internal(kd_tree *t, kd_node *n, int depth)
{
}

void kdt_insert(kd_tree *t, kd_node *n)
{
    kdt_insert_internal(t, n, 0);
}

inline static int kdt_compar(const void *a, const void *b, void *opaque)
{
    return (*(int*)a - *(int*)b);
}

typedef int (*msort_compar)(const void *, const void *, void*);

static void msort_merge(void *a, void *b, void *end, int stride, msort_compar compar, void *opaque, void *tmp)
{
    void *aend = b, *c = tmp;
    while (a < aend && b < end) {
        if (kdt_compar(a, b, opaque) <= 0) {
            memcpy(c, a, stride);
            a += stride;
        } else {
            memcpy(c, b, stride);
            b += stride;
        }
        c += stride;
    }
    if (a < aend) memcpy(c, a, aend - a);
    if (b < end) memcpy(c, b, end - b);
}

// from glibc qsort
#define SWAP(a, b, size)                    \
  do                                        \
    {                                       \
      register size_t __size = (size);      \
      register char *__a = (a), *__b = (b); \
      do                                    \
    {                                       \
      char __tmp = *__a;                    \
      *__a++ = *__b;                        \
      *__b++ = __tmp;                       \
    } while (--__size > 0);                 \
    } while (0)

void msort_in(void *arr, int size, int stride, msort_compar compar, void *opaque, void *tmp)
{
    void *a = arr;
    int sz = size*stride;
    if (size <= 32) {
        // use in-place insertion sort for small arrays
        int i;
        for (i = stride; i < sz; i+= stride) {
            int j;
            for (j = i; j >= stride && kdt_compar(a+j-stride, a+j, opaque) > 0; j-= stride) {
                SWAP(a+j-stride, a+j, stride);
            }
        }
        return;
    }
    int hsz = size/2*stride; // the /2 in this order is impt for odds
    msort_in(a, size/2, stride, compar, opaque, tmp);
    msort_in(a+hsz, size - size/2, stride, compar, opaque, tmp+hsz);
    msort_merge(a, a+hsz, a+sz, stride, compar, opaque, tmp);
    memcpy(arr, tmp, sz);
}

#undef SWAP

void msort(void *arr, int size, int stride, msort_compar compar, void *opaque)
{
    void *tmp = malloc(size*stride);
    msort_in(arr, size, stride, compar, opaque, tmp);
    free(tmp);
}

static int qs_compar(const void *a, const void *b)
{
    return (*(int*)a - *(int*)b);
}

static kd_node *kdt_new_internal(kd_tree *t, int *points, int nb_points, int depth)
{
    int axis = depth % t->k, i, j, median;
    kd_node *node = malloc(sizeof(kd_node));

    /*int *sorted = malloc(nb_points*sizeof(int));

    // quicksort test
    for (i = depth, j = 0; i < t->k*nb_points; i+= t->k) {
        sorted[j] = points[i];
        j += 1;
    }
    qsort(sorted, j, sizeof(int), qs_compar);
    free(sorted);*/
    qsort(points, nb_points, depth*sizeof(int), qs_compar);
    //msort(points, nb_points, depth*sizeof(int), kdt_compar, &axis);

    return node;
}

kd_node* kdt_new(kd_tree *t, int *points, int nb_points, int k)
{
    t->k = k;
    return kdt_new_internal(t, points, nb_points, k);
}

#include <sys/time.h>
static inline double get_time()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec * 1e-6;
}

int main(int argc, char **argv)
{
    int sz = 200000000, depth = 200, *a = malloc(sz * sizeof(int));
    //int sz = 10000000, depth = 1, *a = malloc(sz * sizeof(int));
    int i, j;
    for (i = 0; i < sz; i += depth) {
        for (j = 0; j < depth; j++) a[i + j] = rand() % 262144;
    }

    kd_tree kdt;
    double start = get_time(), end;
    kdt_new(&kdt, a, sz/depth, depth);
    end = get_time() - start;

    for (i = depth; i < sz; i += depth) {
        if (a[i - depth] > a[i]) {
            printf("SORT FAILED got %d (%d) > %d (%d)\n", a[i - depth], i - depth, a[i], i);
            exit(0);
        }
        //printf("(%d %d) ", a[i+ 0], a[i + 1]);
    }
    printf("\nelapsed %f ms\n", end*1000);
    free(a);
    return 0;
}
