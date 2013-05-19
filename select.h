#ifndef JOSH_SELECT_H
#define JOSH_SELECT_H

int quick_select(int *data, int size, int dimensions, int axis);
int set_swap_buf(int dimensions);
void free_swap_buf();
void pivot_nd(int *data, int size, int dimensions, int axis, int p);

int quick_select2(int **data, int size, int axis);
void pivot2(int **data, int size, int axis, int p);
#endif
