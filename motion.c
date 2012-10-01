#include <stdio.h>
#include <stdint.h>
#include <x264.h>
#include <opencv2/imgproc/imgproc_c.h>
#include "encode.h"
#include "capture.h"

#include <sys/time.h>
static inline double get_time()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec * 1e-6;
}

static int* vec_i(int w, int h)
{
    int mb_x = w/16 + (0 != w % 16), mb_y = h/16 + (0 != h % 16);
    int s = mb_x*mb_y*sizeof(int);
    int *vec= malloc(s);
    if (!vec) return NULL;
    memset(vec, 0, s);
    return vec;
}


static float* vec_f(int w, int h)
{
    int mb_x = w/16 + (0 != w % 16), mb_y = h/16 + (0 != h % 16);
    int s = mb_x*mb_y*sizeof(float);
    float *vec = malloc(s);
    if (!vec) return NULL;
    memset(vec, 0, s);
    return vec;
}

#define MADTHRESH 1
#define NBAVG 10
static int **history_i(int w, int h)
{
    int i;
    int **vec = malloc(NBAVG*sizeof(int));
    if (!vec) return NULL;
    for (i = 0; i < NBAVG; i++)
        if (!(vec[i] = vec_i(w, h))) return NULL;
    return vec;
}

static float **get_history(int w, int h)
{
    int i;
    float **avgs = malloc(NBAVG*sizeof(float));
    if (!avgs) return NULL;
    for (i = 0; i < NBAVG; i++)
        if (!(avgs[i] = vec_f(w, h))) return NULL;
    return avgs;
}

static void calc_mbdiffs(float **history, float *avg, int *fg,
    int **fg_hist, IplImage* img, int i, float *quant_offsets)
{
    int w = img->width, h = img->height;
    int mx = w/16 + (0 != w % 16), my = h/16 + (0 != h % 16);
    int x, y, j;
    char *data = img->imageData;
    int stride = img->widthStep;
    int k = i%NBAVG;
    int *cur_fg = fg_hist[k]; // oldest entry in the foreground list
    float *oldmbdiffs = history[k];
    float *mbdiffs = vec_f(w, h);

    // re-calculate new difference values
    memset(mbdiffs, 0, mx*my*sizeof(float));
    for (y = 0; y < h; y++) {
        int mby = y/16, mbx = 0;
        float *line = (float*)data;
        for (x = 0; x < w; x+= 16) {
            int mbxy = mby*mx + mbx;
            float diffs = 0.0f;
            for (j = 0; j < 16; j++) diffs += *line++;
            mbdiffs[mbxy] += diffs;
            mbx += 1;
        }
        data += stride;
    }

    // update with new differences
    char *cstr = malloc(mx*my+my+1), *str = cstr, c;
    char sig[] = {' ',' ',' ','-','-','-','+','+','+','*','*'};
    for (y = 0; y < my; y++) {
        for (x = 0; x < mx; x++) {
            int mbxy = y*mx + x;
            float f = mbdiffs[mbxy]/NBAVG;
            int g = abs(f - avg[mbxy]);
            fg[mbxy] -= cur_fg[mbxy]; // subtract old value
            cur_fg[mbxy] = g > MADTHRESH; // update
            fg[mbxy] += cur_fg[mbxy]; // update foreground
            /*if (fg[mbxy] > (1*NBAVG/2)) {
                c = '+';
                quant_offsets[mbxy] = 0.0;
            } else {
                c = ' ';
                quant_offsets[mbxy] = 10.0;
            }*/
            c = sig[fg[mbxy]];
            quant_offsets[mbxy] = 10.0*(1.0f - fg[mbxy]/NBAVG);

            // update averages
            avg[mbxy] += f;
            avg[mbxy] -= oldmbdiffs[mbxy];
            oldmbdiffs[mbxy] = f;
            *str = c;
            str++;
        }
        *str = '\n';
        str++;
    }
    *str = '\0';
    printf("start\n%send\n", cstr);
    free(cstr);
    free(mbdiffs);
}

IplImage *gray, *avg, *i32, *diff;

static void calc_avgimg(IplImage *img)
{
    // normalize to 0..1
    cvConvertScale(img, i32, 1/255.0, 0);
    // take difference
    cvAbsDiff(i32, avg, diff);
    // 5-frame running average
    cvRunningAvg(i32, avg, 1.0f/NBAVG, NULL);
}

#if 1
int main(int argc, char **argv)
{
    capture_t ctx = {0};
    encode_t enc = {0}, ref = {0};
    x264_picture_t *pic_in, *pic_out;
    int w, h;
    struct SwsContext *sws;

    start_capture(&ctx);
    w = ctx.img->width;
    h = ctx.img->height;
    CvSize size = {.width = w, .height = h};
    uint8_t *pbuf = malloc(avpicture_get_size(PIX_FMT_NV12, w, h));
    float *quant_offsets = vec_f(w, h);
    float *avgs = vec_f(w, h);
    float **history = get_history(w, h);
    int *bkg = vec_i(w, h);
    int **bkg_hist = history_i(w, h);
    if (!pbuf || !quant_offsets || !avgs || !history || !bkg) goto fail;
    start_encode(&enc, w, h, pbuf);
    start_encode(&ref, w, h, pbuf);

    // reset capture swscale to nv12 to feed encoder
    AVCodecContext *codec = ctx.stream->codec;
    sws_freeContext(ctx.sws);
    struct SwsContext *rgb2nv12 = sws_getContext(codec->width, codec->height, codec->pix_fmt, w, h, PIX_FMT_NV12, SWS_BICUBIC, NULL, NULL, 0);
    if (!rgb2nv12) goto fail;
    ctx.sws = rgb2nv12;
    av_image_fill_linesizes(ctx.d_stride, PIX_FMT_NV12, w);
    av_image_fill_pointers(ctx.img_data, PIX_FMT_NV12, h, pbuf, ctx.d_stride);

    // output stuff
    sws = sws_getContext(w, h, PIX_FMT_NV12, w, h, PIX_FMT_BGR24, SWS_BICUBIC, NULL, NULL, 0);
    if (!sws) goto fail;
    IplImage *out = cvCreateImage(size, IPL_DEPTH_8U, 3);
    IplImage *refout = cvCreateImage(size, IPL_DEPTH_8U, 3);
    avg = cvCreateImage(size, IPL_DEPTH_32F, 1);
    i32 = cvCreateImage(size, IPL_DEPTH_32F, 1);
    diff = cvCreateImage(size, IPL_DEPTH_32F, 1);
    gray = cvCreateImageHeader(size, IPL_DEPTH_8U, 1);
    gray->imageData = (char*)pbuf;
    uint8_t *img_data[4], *ref_data[4];
    int rgb_stride[4];
    av_image_fill_linesizes(rgb_stride, PIX_FMT_BGR24, w);
    av_image_fill_pointers(img_data, PIX_FMT_BGR24, h, (uint8_t*)out->imageData, rgb_stride);
    av_image_fill_pointers(ref_data, PIX_FMT_BGR24, h, (uint8_t*)refout->imageData, rgb_stride);
    cvNamedWindow("out", 1);
    cvNamedWindow("ref", 1);

    pic_in = &enc.pic_in;
    pic_out = &enc.pic_out;
    pic_in->prop.quant_offsets = quant_offsets;

    int nbf = 0;
    while (1) {
        int s, t;
        IplImage *img = capture_frame(&ctx);
        if (!img) break;
        calc_avgimg(gray);
        calc_mbdiffs(history, avgs, bkg, bkg_hist, diff, nbf, quant_offsets);
        s = encode_frame(&enc);
        t = encode_frame(&ref);
        if (!s || !t) goto endloop;
        if (s < 0 || t < 0) break;
        sws_scale(sws, (const uint8_t* const*)pic_out->img.plane, pic_out->img.i_stride, 0, h, img_data, rgb_stride);
        sws_scale(sws, (const uint8_t* const*)&ref.pic_out.img.plane, ref.pic_out.img.i_stride, 0, h, ref_data, rgb_stride);
        nbf += 1;
endloop:
        cvShowImage("out", out);
        cvShowImage("ref", refout);
        release_frame(&ctx);
        if ((cvWaitKey(1)&255)==27) break; //esc
    }

    free(avgs);
    free(history);
    free(pbuf);
    cvReleaseImage(&out);
    cvReleaseImage(&i32);
    cvReleaseImage(&avg);
    cvReleaseImage(&diff);
    sws_freeContext(sws);
    free(quant_offsets);
    stop_encode(&enc);
    stop_capture(&ctx);
    cvDestroyWindow("out");
    cvDestroyWindow("ref");
    return 0;
fail:
    stop_encode(&enc);
    stop_capture(&ctx);
    printf("FAIL\n");
    return -1;
}
#endif
