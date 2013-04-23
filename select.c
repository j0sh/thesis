#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 *  This Quickselect routine is based on the algorithm described in
 *  "Numerical recipes in C", Second Edition,
 *  Cambridge University Press, 1992, Section 8.5, ISBN 0-521-43108-5
 *  This code by Nicolas Devillard - 1998. Public domain.
 */

#define ELEM_SWAP(a,b) { register int t=(a);(a)=(b);(b)=t; }

int quick_select(int arr[], int n)
{
    int low, high ;
    int median;
    int middle, ll, hh;

    low = 0 ; high = n-1 ; median = (low + high) / 2;
    for (;;) {
        if (high <= low) /* One element only */
            return arr[median] ;

        if (high == low + 1) {  /* Two elements only */
            if (arr[low] > arr[high])
                ELEM_SWAP(arr[low], arr[high]) ;
            return arr[median] ;
        }

    /* Find median of low, middle and high items; swap into position low */
    middle = (low + high) / 2;
    if (arr[middle] > arr[high])    ELEM_SWAP(arr[middle], arr[high]) ;
    if (arr[low] > arr[high])       ELEM_SWAP(arr[low], arr[high]) ;
    if (arr[middle] > arr[low])     ELEM_SWAP(arr[middle], arr[low]) ;

    /* Swap low item (now in position middle) into position (low+1) */
    ELEM_SWAP(arr[middle], arr[low+1]) ;

    /* Nibble from each end towards middle, swapping items when stuck */
    ll = low + 1;
    hh = high;
    for (;;) {
        do ll++; while (arr[low] > arr[ll]) ;
        do hh--; while (arr[hh]  > arr[low]) ;

        if (hh < ll)
        break;

        ELEM_SWAP(arr[ll], arr[hh]) ;
    }

    /* Swap middle item (in position low) back into correct position */
    ELEM_SWAP(arr[low], arr[hh]) ;

    /* Re-set active partition */
    if (hh <= median)
        low = ll;
        if (hh >= median)
        high = hh - 1;
    }
}

#undef ELEM_SWAP

static int *swap_buf = NULL, swap_n = 0;   // quite hackish
static void swap_nd(int *a, int *b)
{
    memcpy(swap_buf, a, swap_n);
    memcpy(a, b, swap_n);
    memcpy(b, swap_buf, swap_n);
}

int pivot_nd(int *a, int sz, int dim, int axis, int p)
{
    // In-place pivot around p along a given axis of n-dimensions.
    // The resulting array will have two parts: elements from
    // 0...p and elements greater than p. Returns the index of
    // the first element greater than p.
    int i = 0, j = sz - 1;
    while (1) {
        while (i < sz && a[i*dim+axis] <= p) i++;
        while (j >= 0 && a[j*dim+axis] > p) j--;
        if (j <= i) return i*dim;
        swap_nd(a+i*dim, a+j*dim);
    }
}

#if 1
static int compare(const void *a, const void *b)
{
    return (*(int*)a - *(int*)b);
}

int main()
{
    int i, j, median, data[100], sz = sizeof(data)/sizeof(int), r = 0;
    srand(time(NULL));
    for (j = 0; j < 10000; j++) {
    for (i = 0; i < sz; i++) data[i] = rand() % 100;
    median = quick_select(data, sz);
    qsort(data, sz, sizeof(int), compare);
    if (median == data[sz/2 - !(sz & 1)]) r++;
    }
    printf("correct: %d, pct %f\n", r, (float)r/10000 * 100);
    return 0;
}
#endif
