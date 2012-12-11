#ifndef JOSH_CAPTURE_H
#define JOSH_CAPTURE_H

#include <opencv2/core/core_c.h>
#include <opencv2/core/types_c.h>
#include <opencv2/highgui/highgui_c.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

typedef struct {
    AVFormatContext *fmt_ctx;
    AVStream *stream;
    struct SwsContext *sws;
    int s_stride[4], d_stride[4];
    uint8_t *img_data[4];
    IplImage *img;
    AVPacket pkt;
    char *filename;
    AVFrame *picture;
} capture_t;

int start_capture(capture_t *ctx);
void stop_capture(capture_t *ctx);
IplImage* capture_frame(capture_t *ctx);
void release_frame(capture_t *ctx);

#endif
