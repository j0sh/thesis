#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "select.h"

static int *swap_buf = NULL, swap_n = 0;   // quite hackish
static void swap(int *a, int *b)
{
    if (!swap_buf) {
        fprintf(stderr, "select: swapbuf null\n");
        exit(1);
    }
    if (a == b) return; // happens sometimes?
    memcpy(swap_buf, a, swap_n);
    memcpy(a, b, swap_n);
    memcpy(b, swap_buf, swap_n);
}

/*
 *  This Quickselect routine is based on the algorithm described in
 *  "Numerical recipes in C", Second Edition,
 *  Cambridge University Press, 1992, Section 8.5, ISBN 0-521-43108-5
 *  This code by Nicolas Devillard - 1998. Public domain.
 */

int quick_select(int arr[], int n, int dim, int axis)
{
    int low, high;
    int median;
    int middle, ll, hh;

    low = 0 ; high = n-1 ; median = (low + high) / 2;
    for (;;) {
        if (high <= low) /* One element only */
            return median;

        if (high == low + 1) {  /* Two elements only */
            if (arr[low*dim + axis] > arr[high*dim + axis])
                swap(arr+low*dim, arr+high*dim);
            return median;
        }

    /* Find median of low, middle and high items; swap into position low */
    middle = (low + high) / 2;
    if (arr[middle*dim + axis] > arr[high*dim + axis]) {
        swap(arr+middle*dim, arr+high*dim);
    }
    if (arr[low*dim + axis] > arr[high*dim + axis]) {
        swap(arr+low*dim, arr+high*dim);
    }
    if (arr[middle*dim + axis] > arr[low*dim + axis]) {
        swap(arr+middle*dim, arr+low*dim);
    }

    /* Swap low item (now in position middle) into position (low+1) */
    swap(arr+middle*dim, arr+(low+1)*dim);

    /* Nibble from each end towards middle, swapping items when stuck */
    ll = low + 1;
    hh = high;
    for (;;) {
        do ll++; while (arr[low*dim+axis] > arr[ll*dim+axis]);
        do hh--; while (arr[hh*dim+axis]  > arr[low*dim+axis]);

        if (hh < ll)
        break;

        swap(arr+ll*dim, arr+hh*dim);
    }

    /* Swap middle item (in position low) back into correct position */
    swap(arr+low*dim, arr+hh*dim);

    /* Re-set active partition */
    if (hh <= median)
        low = ll;
        if (hh >= median)
        high = hh - 1;
    }
}

int set_swap_buf(int dim)
{
    int sz = dim*sizeof(int);
    if (swap_buf) free(swap_buf);
    swap_buf = malloc(sz);
    if (!swap_buf) return -1;
    swap_n = sz;
    return 1;
}

void free_swap_buf()
{
    if (swap_buf) free(swap_buf);
    swap_buf = NULL;
    swap_n = 0;
}

void pivot_nd(int *a, int sz, int dim, int axis, int p)
{
    int i = 0, j = sz - 1;
    while (1) {
        while (i < sz && a[i*dim+axis] <= p) i++;
        while (j >= 0 && a[j*dim+axis] > p) j--;
        if (j <= i) break;
        swap(a+i*dim, a+j*dim);
    }
}

static void swap2(int **a, int **b)
{
    int *c = *a;
    *a = *b;
    *b = c;
}

int quick_select2(int **arr, int n, int axis)
{
    int low, high;
    int median;
    int middle, ll, hh;

    low = 0 ; high = n-1 ; median = (low + high) / 2;
    for (;;) {
        if (high <= low) /* One element only */
            return median;

        if (high == low + 1) {  /* Two elements only */
            if (arr[low][axis] > arr[high][axis])
                swap2(arr+low, arr+high);
            return median;
        }

    /* Find median of low, middle and high items; swap into position low */
    middle = (low + high) / 2;
    if (arr[middle][axis] > arr[high][axis]) {
        swap2(arr+middle, arr+high);
    }
    if (arr[low][axis] > arr[high][axis]) {
        swap2(arr+low, arr+high);
    }
    if (arr[middle][axis] > arr[low][axis]) {
        swap2(arr+middle, arr+low);
    }

    /* Swap low item (now in position middle) into position (low+1) */
    swap2(arr+middle, arr+low+1);

    /* Nibble from each end towards middle, swapping items when stuck */
    ll = low + 1;
    hh = high;
    for (;;) {
        do ll++; while (arr[low][axis] > arr[ll][axis]);
        do hh--; while (arr[hh][axis]  > arr[low][axis]);

        if (hh < ll)
        break;

        swap2(arr+ll, arr+hh);
    }

    /* Swap middle item (in position low) back into correct position */
    swap2(arr+low, arr+hh);

    /* Re-set active partition */
    if (hh <= median)
        low = ll;
        if (hh >= median)
        high = hh - 1;
    }
}

void pivot2(int **a, int sz, int axis, int p)
{
    int i = 0, j = sz - 1;
    while (1) {
        while (i < sz && a[i][axis] <= p) i++;
        while (j >= 0 && a[j][axis] > p) j--;
        if (j <= i) break;
        swap2(a+i, a+j);
    }
}

#if 0
static int compare(const void *a, const void *b)
{
    return (*(int*)a - *(int*)b);
}

int main()
{
    int i, j,*median, data[100], sz = sizeof(data)/sizeof(int), r = 0;
    srand(time(NULL));
    for (j = 0; j < 10000; j++) {
    for (i = 0; i < sz; i++) data[i] = rand() % 100;
    median = quick_select(data, sz);
    qsort(data, sz, sizeof(int), compare);
    if (*median == data[sz/2 - !(sz & 1)]) r++;
    }
    printf("correct: %d, pct %f\n", r, (float)r/10000 * 100);
    return 0;
}
#endif
