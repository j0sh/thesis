#ifndef JOSH_GCK_H
#define JOSH_GCK_H

int* gck_calc_2d(uint8_t *data, int w, int h, int kern_size, int bases);
int* gck_valid_data(int *data, int w, int h, int kern_size, int bases);
int* gck_interleave_data(int *data, int w, int h, int bases);

#endif
