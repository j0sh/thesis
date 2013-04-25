#ifndef JOSH_GCK_H
#define JOSH_GCK_H

int* gck_calc_2d(uint8_t *data, int w, int h, int kern_size, int bases);
void gck_truncate_data(int *data, int w, int h, int kern_size,
    int bases, int *a);
void gck_interleave_data(int *data, int w, int h, int bases, int *a);
int *gck_alloc_buffer(int w, int h, int kern_size, int bases);

#endif
