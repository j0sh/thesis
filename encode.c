#include <stdio.h>
#include <stdint.h>
#include <x264.h>
#include "capture.h"

int main(int argc, char **argv)
{
    capture_t ctx = {0};
    x264_param_t param;
    x264_picture_t pic_in, pic_out;
    x264_nal_t *nal;
    x264_t *h = NULL;
    struct SwsContext *sws;

    start_capture(&ctx);
    CvSize size = {.width = ctx.img->width, .height = ctx.img->height};

    uint8_t *pbuf = malloc(avpicture_get_size(PIX_FMT_NV12, size.width, size.height));
    if (!pbuf) goto fail;
    AVFrame pic;
    avpicture_fill((AVPicture*)&pic, pbuf, PIX_FMT_NV12, size.width, size.height);
    struct SwsContext *rgb2yuv = sws_getContext(size.width, size.height, PIX_FMT_BGR24, size.width, size.height, PIX_FMT_NV12, SWS_BICUBIC, NULL, NULL, 0);
    uint8_t *yuv_data[4];
    av_image_fill_pointers(yuv_data, PIX_FMT_NV12, size.height, pbuf, pic.linesize);

    if (x264_param_default_preset(&param, "superfast", "zerolatency")) goto fail;
    x264_picture_init(&pic_in);
    pic_in.img.i_csp = X264_CSP_NV12;
    pic_in.img.i_plane = 1;
    av_image_fill_linesizes(pic_in.img.i_stride, PIX_FMT_NV12, size.width);
    av_image_fill_pointers(pic_in.img.plane, PIX_FMT_NV12, size.height, *yuv_data, pic_in.img.i_stride);
    param.i_csp = X264_CSP_NV12;
    param.i_width = size.width;
    param.i_height = size.height;
    //param.b_full_recon = 1;
    h = x264_encoder_open(&param);
    if (!h) goto fail;
    pic_in.i_pts = 0;
    sws = sws_getContext(size.width, size.height, PIX_FMT_NV12, size.width, size.height, PIX_FMT_BGR24, SWS_BICUBIC, NULL, NULL, 0);
    if (!sws) goto fail;
    IplImage *out = cvCreateImage(size, IPL_DEPTH_8U, 3);
    uint8_t *img_data[4];
    av_image_fill_pointers(img_data, PIX_FMT_BGR24, size.height, (uint8_t*)out->imageData, ctx.d_stride);
    cvNamedWindow("cap", 1);

    while (1) {
        int pi_nal, s;
        IplImage *img = capture_frame(&ctx);
        if (!img) break;
        sws_scale(rgb2yuv, (const uint8_t* const*)ctx.img_data, ctx.d_stride, 0, size.height, yuv_data, pic.linesize);
        s = x264_encoder_encode(h, &nal, &pi_nal, &pic_in, &pic_out);
        if (!s) goto endloop;
        if (s < 0) break;
        sws_scale(sws, (const uint8_t* const*)pic_out.img.plane, pic_out.img.i_stride, 0, size.height, img_data, ctx.d_stride);
endloop:
        cvShowImage("cap", out);
        release_frame(&ctx);
        if ((cvWaitKey(1)&255)==27) break; //esc
    }

    free(pbuf);
    cvReleaseImage(&out);
    sws_freeContext(sws);
    if (h) x264_encoder_close(h);
    stop_capture(&ctx);
    return 0;
fail:
    if (h) x264_encoder_close(h);
    stop_capture(&ctx);
    printf("FAIL\n");
    return -1;
}
