#if 1
#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include "capture.h"

static AVFrame *picture;

static AVFrame *alloc_picture(AVCodecContext *c)
{
    AVFrame *pic;
    int size;
    uint8_t *pbuf;
    pic = avcodec_alloc_frame();
    if (!pic) return NULL;
    size = avpicture_get_size(c->pix_fmt, c->width, c->height);
    pbuf = av_malloc(size);
    if (!pbuf) return NULL;
    avpicture_fill((AVPicture*)pic, pbuf, c->pix_fmt, c->width, c->height);
    return pic;
}

static struct SwsContext *sws = NULL;
static uint8_t *video_outbuf = NULL;
static int video_outbuf_size = 0;

static AVStream *add_video_stream(AVFormatContext *fmt, enum CodecID codec_id)
{
    AVCodecContext *c;
    AVStream *st;
    AVCodec *codec;
    codec = avcodec_find_encoder(codec_id);
    if (!codec) {
        fprintf(stderr, "Codec not found.\n");
        return NULL;
    }
    st = avformat_new_stream(fmt, codec);
    if (!st) return NULL;
    c = st->codec;
    c->bit_rate = 400000;
    c->width = 352;
    c->height = 288;
    c->time_base.den = 25;
    c->time_base.num = 1;
    c->gop_size = 12;
    c->pix_fmt = PIX_FMT_YUV420P;
    if (fmt->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;

    if (codec_id == CODEC_ID_H264) {
        av_opt_set(c->priv_data, "preset", "superfast", 0);
        av_opt_set(c->priv_data, "tune", "zerolatency", 0);
    }
    if (avcodec_open2(c, codec, NULL) < 0){
        fprintf(stderr, "Could not open codec.\n");
        return NULL;
    }

    picture = alloc_picture(c);
    if (!picture) return NULL;

    video_outbuf_size = 20000;
    video_outbuf = av_malloc(video_outbuf_size);
    return st;
}

static int frame_number = 0;
static void write_video_frame(IplImage *img, capture_t *cap, AVFormatContext *fmt, AVStream *st)
{
    AVPacket pkt, *in_pkt = &cap->pkt;
    uint8_t *cap_data[4];
    int gotpic = 0;
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
    AVCodecContext *capc = cap->stream->codec, *c = st->codec;
    // convert colorspace
    av_image_fill_pointers(cap_data, capc->pix_fmt, capc->height, in_pkt->data, cap->s_stride);
    if (!sws) sws = sws_getContext(capc->width, capc->height, capc->pix_fmt, c->width, c->height, c->pix_fmt, SWS_BICUBIC, NULL, NULL, 0);
    sws_scale(sws, (const uint8_t* const*)cap_data, cap->s_stride, 0, capc->width, picture->data, picture->linesize);
    picture->pts = frame_number++;
    if (avcodec_encode_video2(c, &pkt, picture, &gotpic) < 0) exit(1);
    if (!gotpic) goto write_finished;
    if (pkt.pts != AV_NOPTS_VALUE)
        pkt.pts = av_rescale_q(pkt.pts, c->time_base, st->time_base);
    if (pkt.dts != AV_NOPTS_VALUE)
        pkt.dts = av_rescale_q(pkt.dts, c->time_base, st->time_base);
    pkt.stream_index = st->index;
    av_write_frame(fmt, &pkt);
write_finished:
    av_free_packet(&pkt);
}

int main(int argc, char **argv)
{
    char *filename = "rtmp://localhost/flvplayback/beakybird";
    // boilerplate
    capture_t ctx = {0};
    if (start_capture(&ctx) > 0) goto realerr;
    cvNamedWindow("cap", 1);
    
    #if 1
    AVOutputFormat *of = NULL;
    AVFormatContext *fmt = NULL;
    AVStream *st = NULL;
    avcodec_register_all();
    avformat_network_init();
    of = av_guess_format("flv", NULL, NULL);
    if (!of) goto realerr;
    of->video_codec = CODEC_ID_H264;
    fmt = avformat_alloc_context();
    if (!fmt) goto realerr;
    fmt->oformat = of;
    st = add_video_stream(fmt, of->video_codec);
    if (!st) goto realerr;
    if (avio_open(&fmt->pb, filename, AVIO_FLAG_WRITE) < 0) {
        fprintf(stderr, "Unable to open %s", filename);
        goto realerr;
    }
    avformat_write_header(fmt, NULL);

    av_dump_format(fmt, 0, filename, 1);
    #endif

#if 1
    while (1) {
        IplImage *img = capture_frame(&ctx);
        if (!img) goto realerr;
        write_video_frame(img, &ctx, fmt, st);
        cvShowImage("cap", img);
        release_frame(&ctx);
        if ((cvWaitKey(40)&255)==27) break; // esc
    }
#endif
    cvDestroyWindow("cap");
    av_freep(&video_outbuf);
    sws_freeContext(sws);
    stop_capture(&ctx);
    return 0;
realerr:
    fprintf(stderr, "Error somewhere\n");
    return 70;
}
#endif
