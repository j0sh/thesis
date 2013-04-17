#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void swap(int *a, int *b)
{
    int c = *a;
    *a = *b;
    *b = c;
}

static void partition(int *start, int* med, int *end)
{
    int median = *med;
    swap(med, end);
    int i, j = -1, size = end - start;
    for (i = 0; i < size; i++) {
        if (start[i] <= median) {
            j += 1;
            swap(start+i, start+j);
        }
    }
    swap(end, start+j);
}

static int get_median(int *a, int size)
{
    // use in-place insertion sort for small arrays
    int i;
    for (i = 1; i < size; i++) {
        int v = a[i];
        int hole = i;
        while (hole > 0 && v < a[hole - 1]) {
            a[hole] = a[hole - 1];
            hole -= 1;
        }
        a[hole] = v;
    }
    return a[size/2];
}

static int mom_in(int *data, int size, int *medians)
{
    int i, nb = size/5, last = size % 5;
    nb += last ? 1 : 0; // add one more for the leftover
    if (!last) last = 5;
    if (1 == nb) return get_median(data, last);
    for (i = 0; i < nb; i++) {
        medians[i] = get_median(data + i * 5, i == nb ? last : 5);
    }
    return mom_in(medians, nb, medians);
}

int median_of_medians(int *data, int size)
{
    int nb = size/5, median, *medians;
    nb += nb % 5 ? 1 : 0;
    medians = malloc(nb * sizeof(int));
    median = mom_in(data, size, medians);
    free(medians);
    return median;
}

static int compare(const void *a, const void *b)
{
    return (*(int*)a - *(int*)b);
}

int main()
{
    int i, median, data[100], sz = sizeof(data)/sizeof(int);
    srand(time(NULL));
    for (i = 0; i < sz; i++) data[i] = rand() % 100;
    median = median_of_medians(data, sz);
    qsort(data, sz, sizeof(int), compare);
    printf("\nmedian of medians %d sorted %d\n", median, data[sz/2]);
    return 0;
}
