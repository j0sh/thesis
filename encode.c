#include <stdio.h>
#include <stdint.h>
#include "encode.h"
#include "capture.h"

#include <sys/time.h>
static inline double get_time()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec * 1e-6;
}

static void free_mbinfo(void *mbinfo)
{
    free(mbinfo);
}

static int in_circle(int x, int y, int w, int h)
{
    // scale; normalize to 0..3, shift to origin by 1.5
    float fx = 3*x/(float)w - 1.5, fy = 3*y/(float)h - 1.5;
    return !(fx*fx + fy*fy > 1.0); // circle being approx half
}

static uint8_t* get_mbinfo(int w, int h)
{
    int mb_x = w/16 + (0 != w % 16), mb_y = h/16 + (0 != h % 16);
    int x, y, k = mb_x, xy;
    uint8_t* mb_info = malloc(mb_x*mb_y);
    if (!mb_info) return NULL;
    for (y = 0; y < mb_y; y++) {
        for (x = 0; x < mb_x; x++) {
            xy = y*k + x;
            mb_info[xy] = X264_MBINFO_CONSTANT * !in_circle(x, y, mb_x, mb_y);
            //if (x > mb_x/4 && x < 3*mb_x/4) mb_info[xy] = 0;
            //else mb_info[xy] = X264_MBINFO_CONSTANT;
        }
    }
    return mb_info;
}

static float* get_offsets(int w, int h)
{
    int mb_x = w/16 + (0 != w % 16), mb_y = h/16 + (0 != h % 16);
    int x, y, k = mb_x, xy;
    float *offsets = malloc(mb_x*mb_y*sizeof(float));
    if (!offsets) return NULL;
    for (y = 0; y < mb_y; y++) {
        for (x = 0; x < mb_x; x++) {
            xy = y*k + x;
            offsets[xy] = 100.0f * !in_circle(x, y, mb_x, mb_y);
        }
    }
    return offsets;
}

static void print_mbinfo(uint8_t* mbinfo, int w, int h)
{
    int mb_x = w/16 + (0 != w % 16), mb_y = h/16 + (0 != h % 16);
    int x, y, k = mb_x, xy, sxy, m = 0;
    char *str = malloc(mb_x*mb_y+mb_y+1);
    for (y = 0; y < mb_y; y++) {
        for (x = 0; x < mb_x; x++) {
            xy = y*k + x, sxy = xy + m;
            if (mbinfo[xy] == X264_MBINFO_CONSTANT) str[sxy] = '1';
            else str[sxy] = '0';
        }
        m += 1;
        str[sxy+1] = '\n';
    }
    str[sxy + 1] = '\0';
    printf("Constant blocks\n%s\n", str);
    free(str);
}

static void print_offsets(float* offsets, int w, int h)
{
    int mb_x = w/16 + (0 != w % 16), mb_y = h/16 + (0 != h % 16);
    int x, y, k = mb_x, xy, sxy, m = 0;
    char *str = malloc(mb_x*mb_y+mb_y+1);
    for (y = 0; y < mb_y; y++) {
        for (x = 0; x < mb_x; x++) {
            xy = y*k + x, sxy = xy + m;
            if (in_circle(x, y, mb_x, mb_y)) str[sxy] = '1';
            else str[sxy] = '0';
        }
        m += 1;
        str[sxy+1] = '\n';
    }
    str[sxy + 1] = '\0';
    printf("Offset Blocks\n%s\n", str);
    free(str);
}

int start_encode(encode_t *enc, int width, int height)
{
    x264_picture_t *pic_in = &enc->pic_in;
    x264_param_t *param = &enc->param;
    x264_t *h;
    if (x264_param_default_preset(param, "superfast", "zerolatency"))
        return -1;
    x264_picture_init(pic_in);
    pic_in->img.i_csp = X264_CSP_NV12;
    pic_in->img.i_plane = 1;
    pic_in->i_pts = 0;
    param->i_csp = X264_CSP_NV12;
    param->i_width = width;
    param->i_height = height;
    param->b_full_recon = 1;
    param->rc.i_bitrate = 100;
    param->rc.i_rc_method = X264_RC_ABR;
    //param->rc.f_aq_strength = 9.0;
    //param->analyse.i_noise_reduction = 1;
    //param->analyse.b_mb_info = 1;
    enc->nbf = enc->ms = 0;
    h = x264_encoder_open(param);
    if (!h) return -1;
    enc->h = h;
    return 0;
}

int encode_frame(encode_t *enc)
{
    int s, pi_nal;
    double start = get_time();
    x264_nal_t *nal;
    x264_picture_t *pic_in = &enc->pic_in, *pic_out = &enc->pic_out;
    s = x264_encoder_encode(enc->h, &nal, &pi_nal, pic_in, pic_out);
    enc->ms += (get_time() - start);
    enc->nbf += 1;
    return s;
}

void stop_encode(encode_t *enc)
{
    if (enc->h) x264_encoder_close(enc->h);
}

#if 1
int main(int argc, char **argv)
{
    encode_t enc = {0};
    capture_t ctx = {0};
    struct SwsContext *sws;

    start_capture(&ctx);
    CvSize size = {.width = ctx.img->width, .height = ctx.img->height};
    start_encode(&enc, size.width, size.height);

    uint8_t *pbuf = malloc(avpicture_get_size(PIX_FMT_NV12, size.width, size.height));
    uint8_t *mb_info = get_mbinfo(size.width, size.height);
    float *quant_offsets = get_offsets(size.width, size.height);
    if (!pbuf || !mb_info || !quant_offsets) goto fail;
    AVFrame pic;
    avpicture_fill((AVPicture*)&pic, pbuf, PIX_FMT_NV12, size.width, size.height);
    struct SwsContext *rgb2yuv = sws_getContext(size.width, size.height, PIX_FMT_BGR24, size.width, size.height, PIX_FMT_NV12, SWS_BICUBIC, NULL, NULL, 0);
    uint8_t *yuv_data[4];
    av_image_fill_pointers(yuv_data, PIX_FMT_NV12, size.height, pbuf, pic.linesize);
    x264_picture_t *pic_in = &enc.pic_in;
    av_image_fill_linesizes(pic_in->img.i_stride, PIX_FMT_NV12, size.width);
    av_image_fill_pointers(pic_in->img.plane, PIX_FMT_NV12, size.height, *yuv_data, pic_in->img.i_stride);
    pic_in->prop.quant_offsets = quant_offsets;
    pic_in->prop.mb_info = mb_info;
    print_mbinfo(mb_info, size.width, size.height);
    print_offsets(quant_offsets, size.width, size.height);
    sws = sws_getContext(size.width, size.height, PIX_FMT_NV12, size.width, size.height, PIX_FMT_BGR24, SWS_BICUBIC, NULL, NULL, 0);
    if (!sws) goto fail;
    x264_picture_t *pic_out = &enc.pic_out;
    IplImage *out = cvCreateImage(size, IPL_DEPTH_8U, 3);
    uint8_t *img_data[4];
    av_image_fill_pointers(img_data, PIX_FMT_BGR24, size.height, (uint8_t*)out->imageData, ctx.d_stride);
    cvNamedWindow("cap", 1);

    while (1) {
        int s;
        IplImage *img = capture_frame(&ctx);
        if (!img) break;
        sws_scale(rgb2yuv, (const uint8_t* const*)ctx.img_data, ctx.d_stride, 0, size.height, yuv_data, pic.linesize);
        s = encode_frame(&enc);
        if (!s) goto endloop;
        if (s < 0) break;
        sws_scale(sws, (const uint8_t* const*)pic_out->img.plane, pic_out->img.i_stride, 0, size.height, img_data, ctx.d_stride);
endloop:
        cvShowImage("cap", out);
        release_frame(&ctx);
        if ((cvWaitKey(1)&255)==27) break; //esc
    }

    free(pbuf);
    cvReleaseImage(&out);
    sws_freeContext(sws);
    sws_freeContext(rgb2yuv);
    stop_encode(&enc);
    free(mb_info);
    free(quant_offsets);
    stop_capture(&ctx);
    return 0;
fail:
    stop_encode(&enc);
    stop_capture(&ctx);
    printf("FAIL\n");
    return -1;
}
#endif
