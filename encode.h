#ifndef JOSH_ENCODE_H
#define JOSH_ENCODE_H

#include <x264.h>

typedef struct {
    x264_t *h;
    x264_param_t param;
    x264_picture_t pic_in, pic_out;

    int nbf;
    double ms;
} encode_t;

int start_encode(encode_t*, int w, int h);
void stop_encode(encode_t*);

#endif
