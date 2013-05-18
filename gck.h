#ifndef JOSH_GCK_H
#define JOSH_GCK_H

int* gck_calc_2d(uint8_t *data, int w, int h, int kern_size, int bases);
int *gck_alloc_buffer(int w, int h, int kern_size, int bases);

#endif
