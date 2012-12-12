#include "capture.h"

#define W 320
#define H 240

#define ERR(msg) { fprintf(stderr, "%s\n", msg); return -1; }
static int add_decoder(capture_t *ctx)
{
    AVCodecContext *c;
    AVCodec *codec = NULL;
    if (!ctx->stream || !ctx->stream->codec) ERR("ctx props invalid");
    c = ctx->stream->codec;
    codec = avcodec_find_decoder(ctx->stream->codec->codec_id);
    if (!codec) ERR("avcodec_find_decoder");
    if (avcodec_open2(c, codec, NULL)) ERR("avcodec_open2");
    ctx->picture = avcodec_alloc_frame();
    if (codec->capabilities & CODEC_CAP_DR1) {
        c->get_buffer = avcodec_default_get_buffer;
        c->release_buffer = avcodec_default_release_buffer;
    }
    return 0;
}
#undef ERR

static int decode(capture_t *ctx, AVPacket *pkt)
{
    int complete = 0, len;
    AVCodecContext *c = ctx->stream->codec;
    AVFrame *pic = ctx->picture;
    len = avcodec_decode_video2(c, pic, &complete, pkt);
    if (len < 0) { fprintf(stderr, "decode error\n"); return -1; }
    if (!complete) { return 1; }
    return 0;
}

#define ERR(msg) { fprintf(stderr, "%s\n", msg); goto main_error; }
int start_capture(capture_t *ctx)
{
    AVFormatContext *ic;
    AVInputFormat *fmt = NULL;
    AVStream *st;
    struct SwsContext *sws;
    IplImage *img;
    int ret;
    CvSize size;
    char *fname = "/dev/video0";
    av_register_all();
    avdevice_register_all();
    if (!ctx->filename) {
        fmt = av_find_input_format("video4linux2");
        if (!fmt) ERR("av_find_input_format");
    } else fname = ctx->filename;
    ic = avformat_alloc_context();
    if (!ic) ERR("avformat_alloc_context");
    ret = avformat_open_input(&ic, fname, fmt, NULL);
    if (ret < 0) { av_freep(&ic); ERR("avformat_open_input"); }
    avformat_find_stream_info(ic, NULL);
    if (ic->nb_streams <= 0) ERR("find_stream_info");
    st = ic->streams[0];
    if (st->codec->codec_type != AVMEDIA_TYPE_VIDEO) ERR("codec_type");
    sws = sws_getContext(st->codec->width, st->codec->height,
        st->codec->pix_fmt, W, H,
        PIX_FMT_BGR24, SWS_BICUBIC, NULL, NULL, 0);
    if (!sws) ERR("sws_getContext");
    size.width = W; size.height= H;
    ret = av_image_fill_linesizes(ctx->s_stride, st->codec->pix_fmt, st->codec->width);
    if (ret < 0) ERR("av_image_fill_linesizes");
    ret = av_image_fill_linesizes(ctx->d_stride, PIX_FMT_RGB24, W);
    if (ret < 0) ERR("av_image_fill_linesizes 2");
    img = cvCreateImage(size, IPL_DEPTH_8U, 3);
    if (!img) ERR("cvCreateImage");
    av_image_fill_pointers(ctx->img_data, PIX_FMT_RGB24, st->codec->height, (uint8_t*)img->imageData, ctx->d_stride);

    ctx->fmt_ctx = ic;
    ctx->stream = st;
    ctx->img = img;
    ctx->sws = sws;
    if (ctx->filename) if (add_decoder(ctx)) ERR("add_decoder");
    return 0;

main_error:
    fprintf(stderr, "An Erro has Happened\n");
    return 1;
}

void stop_capture(capture_t *ctx)
{
    cvReleaseImage(&ctx->img);
    if (ctx->filename) {
        avcodec_close(ctx->stream->codec);
        av_freep(&ctx->picture);
    }
    avformat_close_input(&ctx->fmt_ctx);
    av_freep(&ctx->fmt_ctx);
    sws_freeContext(ctx->sws);
    ctx->sws = NULL;
}

IplImage* capture_frame(capture_t *ctx)
{
    AVPacket *pkt = &ctx->pkt;
    AVStream *st = ctx->stream;
    int w = st->codec->width, h = st->codec->height;
    uint8_t *cap_data[4];
    int ret = av_read_frame(ctx->fmt_ctx, pkt);
    if (ret < 0) return NULL;
    if (ctx->filename) {
        // from file, so decode
        int ret;
        readloop:
        ret = decode(ctx, pkt);
        if (ret > 0) { release_frame(ctx); goto readloop; }
        if (ret < 0) return NULL;
        sws_scale(ctx->sws, (const uint8_t* const*)ctx->picture->data,
            ctx->picture->linesize, 0, h, ctx->img_data, ctx->d_stride);
        return ctx->img;
    }
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
