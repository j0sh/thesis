#include "capture.h"

int start_capture(capture_t *ctx)
{
    AVFormatContext *ic;
    AVInputFormat *fmt;
    AVStream *st;
    struct SwsContext *sws;
    IplImage *img;
    CvSize size;
    int ret;
    av_register_all();
    avdevice_register_all();
    fmt = av_find_input_format("v4l2");
    if (!fmt) goto main_error;
    ic = avformat_alloc_context();
    if (!ic) goto main_error;
    ret = avformat_open_input(&ic, "/dev/video0", fmt, NULL);
    if (ret < 0) { av_freep(&ic); goto main_error; }
    avformat_find_stream_info(ic, NULL);
    if (ic->nb_streams <= 0) goto main_error;
    st = ic->streams[0];
    if (st->codec->codec_type != AVMEDIA_TYPE_VIDEO) goto main_error;
    sws = sws_getContext(st->codec->width, st->codec->height,
        st->codec->pix_fmt, st->codec->width, st->codec->height,
        PIX_FMT_BGR24, SWS_BICUBIC, NULL, NULL, 0);
    if (!sws) goto main_error;
    size.width = st->codec->width; size.height = st->codec->height;
    ret = av_image_fill_linesizes(ctx->s_stride, st->codec->pix_fmt, st->codec->width);
    if (ret < 0) goto main_error;
    ret = av_image_fill_linesizes(ctx->d_stride, PIX_FMT_RGB24, st->codec->width);
    if (ret < 0) goto main_error;
    img = cvCreateImage(size, 8, 3);
    if (!img) goto main_error;
    av_image_fill_pointers(ctx->img_data, PIX_FMT_RGB24, st->codec->height, (uint8_t*)img->imageData, ctx->d_stride);

    ctx->fmt_ctx = ic;
    ctx->stream = st;
    ctx->img = img;
    ctx->sws = sws;
    return 0;

main_error:
    fprintf(stderr, "An Erro has Happened\n");
    return 1;
}

void stop_capture(capture_t *ctx)
{
    cvReleaseImage(&ctx->img);
    avformat_close_input(&ctx->fmt_ctx);
    av_freep(&ctx->fmt_ctx);
    sws_freeContext(ctx->sws);
}

IplImage* capture_frame(capture_t *ctx)
{
    AVPacket *pkt = &ctx->pkt;
    AVStream *st = ctx->stream;
    int w = st->codec->width, h = st->codec->height;
    uint8_t *cap_data[4];
    int ret = av_read_frame(ctx->fmt_ctx, pkt);
    if (ret < 0) return NULL;
    av_image_fill_pointers(cap_data, st->codec->pix_fmt, h, pkt->data, ctx->s_stride);
    sws_scale(ctx->sws, (const uint8_t* const*)cap_data, ctx->s_stride, 0, w, ctx->img_data, ctx->d_stride);
    return  ctx->img;
}

void release_frame(capture_t *ctx)
{
    av_free_packet(&ctx->pkt);
}

#if 0
int main(int argc, char **argv)
{
    int i;
    capture_t ctx = {0};
    start_capture(&ctx);
    cvNamedWindow("result", 1);
    for (i = 0; i < 100; i++) {
        IplImage *img = capture_frame(&ctx);
        if (!img) goto realerr;
        cvShowImage("result", img);
        release_frame(&ctx);
        if ((cvWaitKey(1)&255)==27)break; // esc
    }
    cvDestroyWindow("result");
    stop_capture(&ctx);
    return 0;

 realerr:
    fprintf(stderr, "error capturing\n");
    return 1;
}
#endif
