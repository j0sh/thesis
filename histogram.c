#include <opencv2/imgproc/imgproc_c.h>
#include "capture.h"

IplImage *chromau, *chromav;
#define BINS 64
#define PIXFMT PIX_FMT_NV12

static float min(float a, float b)
{
    return a > b ? b : a;
}
static float max(float a, float b)
{
    return a > b ? a : b;
}

static int history_size(int w, int h)
{
    int mb_x = w/16 + (0 != w % 16), mb_y = h/16 + (0 != h % 16);
    int s_luma = mb_x * mb_y * BINS * sizeof(int);
    int s_chroma = mb_x * mb_y * BINS/4 * sizeof(int);
    return s_luma + (s_chroma * 2);
}

static void calc_hist(IplImage *img, int *histogram)
{
    int w = img->width, h = img->height;
    int mx = w/16 + (0 != w % 16), my = h/16 + (0 != h % 16);
    int j, x, y;
    char *data = img->imageData;
    float *total = calloc(BINS, sizeof(float));
    int strides[4];
    av_image_fill_linesizes(strides, PIXFMT, w);
    memset(histogram, 0, history_size(w, h));
    // luma
    for (y = 0; y < h; y++) {
        int mby = y/16, mbx = 0;
        uint8_t *line = (uint8_t*)data;
        for (x = 0; x < w; x += 16) {
            int mbxy = mby * mx*BINS + mbx*BINS;
            for (j = 0; j < 16; j++) {
                uint8_t val = *line++;
                histogram[mbxy + (val/4)] += 1;
                total[val/4] += 1;
            }
            mbx += 1;
        }
        data += strides[0];
    }

    // calculate mean, variance, etc
    int xy = w*h;
    char *cstr = malloc(mx*my+my+1), *str = cstr;
    for (j = 0; j < BINS; j++) total[j] /= xy;
    for (y = 0; y < my; y++) {
        for (x = 0; x < mx; x++) {
            int mxy = y*mx*BINS+ x*BINS;
            float sum = 0;
            for (j = 0; j < BINS; j++) {
                float v1 = ((float)histogram[mxy + j])/256.0;
                float v2 = total[j];
                v1 += (v1 == 0);
                v2 += (v2 == 0);
                sum += min(v1, v2)/max(v1, v2);
            }
            sum /= BINS;
            *str++ = (sum > 0.40) * 10 + ' ';
        }
        *str++ = '\n';
    }
    *str = '\0';
    //printf("start\n%s\nend\n", cstr);
    free(cstr);
    cstr = NULL;

    char *d2 = data;
    int off = mx*my*BINS;
    int chromaoff = mx*my*(BINS/4);
    float *utotal = calloc(BINS/4, sizeof(float));
    float *vtotal = calloc(BINS/4, sizeof(float));
    for (y = 0; y < (h/2); y++) {
        int mby = y/8, mbx = 0;
        uint8_t *line = (uint8_t*)data;
        for (x = 0; x < (w/2); x+= 8) {
            int mbxy = mby * mx * (BINS/4) + mbx*(BINS/4);
            int histu = off + mbxy;
            int histv = off + mbxy + chromaoff;
            for (j = 0; j < 8; j++) {
                uint8_t u = *line++;
                uint8_t v = *line++;
                histogram[histu + (u/16)] += 1;
                histogram[histv + (v/16)] += 1;
                utotal[u/16] += 1;
                vtotal[v/16] += 1;
            }
            mbx += 1;
        }
        data += strides[0];
    }

    // calculate mean, variance, etc for chroma
    char *ustr = malloc(mx*my+my+1), *str1 = ustr;
    char *vstr = malloc(mx*my+my+1);
    for (j = 0; j < BINS/4; j++) {
        utotal[j] /= (w*h/4);
        vtotal[j] /= (w*h/4);
    }
    for (y = 0; y < my; y++) {
        for (x = 0; x < mx; x++) {
            int mbxy = y*mx*(BINS/4) + x*(BINS/4);
            int histu = off + mbxy;
            int histv = off + mbxy + chromaoff;
            float usum = 0, vsum = 0;
            for (j = 0; j < BINS/4; j++) {
                float u = ((float)histogram[histu + j])/256.0;
                float v = ((float)histogram[histv + j])/256.0;
                float ut = utotal[j];
                float vt = vtotal[j];
                u += (u == 0);
                v += (v == 0);
                usum += min(u, ut)/max(u, ut);
                vsum += min(v, vt)/max(v, vt);
            }
            usum /= (BINS/4);
            vsum /= (BINS/4);
            *str1++ = (vsum > 0.10) * 10 + ' ';
        }
        *str1++ = '\n';
    }
    //printf("chroma start\n%send\n", ustr);
    free(ustr);
    free(vstr);
    ustr = NULL;
    vstr = NULL;

    // chroma display
    char *udata = chromau->imageData;
    char *vdata = chromav->imageData;
    int cstride = chromau->widthStep;
    for (y = 0; y < (h/2); y++) {
        uint8_t *line = (uint8_t*)d2;
        uint8_t *uline = (uint8_t*)udata;
        uint8_t *vline = (uint8_t*)vdata;
        for (x = 0; x < w; x += 2) {
            *uline++ = *line++;
            *vline++ = *line++;
        }
        d2 += strides[1];
        udata += cstride;
        vdata += cstride;
    }
    cvShowImage("chromau", chromau);
    cvShowImage("chromav", chromav);
    free(total);
    free(utotal);
    free(vtotal);
}

static int *history_i(int w, int h)
{
    int s = history_size(w, h);
    int *hist = malloc(s);
    memset(hist, 0, s);
    return hist;
}

static int setup_pixfmt(capture_t *ctx, uint8_t *pbuf)
{
    // reset colorspace conversion from default
    int w = ctx->img->width, h = ctx->img->height;
    AVCodecContext *codec = ctx->stream->codec;
    struct SwsContext *rgb2nv12 = sws_getContext(
        codec->width, codec->height, codec->pix_fmt, w, h,
        PIXFMT, SWS_BICUBIC, NULL, NULL, 0);
    if (!rgb2nv12) {
        fprintf(stderr, "Unable to set up nv12 scaler!\n");
        return -1;
    }
    sws_freeContext(ctx->sws);
    cvReleaseImage(&ctx->img);
    ctx->sws = rgb2nv12;
    CvSize s = {w, h};
    ctx->img = cvCreateImageHeader(s, IPL_DEPTH_8U, 1);
    av_image_fill_linesizes(ctx->d_stride, PIXFMT, w);
    av_image_fill_pointers(ctx->img_data, PIXFMT, h, pbuf, ctx->d_stride);
    ctx->img->imageData = (char*)pbuf;
    return 0;
}

int main(int argc, char **argv)
{
    int w, h, *hist;
    char *fname = "bbb.mkv";
    capture_t ctx = {0};
    ctx.filename = fname;
    start_capture(&ctx);
    w = ctx.img->width; h = ctx.img->height;

    // colorspace and histogram
    uint8_t *pbuf = malloc(avpicture_get_size(PIXFMT, w, h));
    if (!pbuf) goto realerr;
    if (setup_pixfmt(&ctx, pbuf)) goto realerr;
    hist = history_i(w, h);
    if (!hist) goto realerr;

    // display stuff
    CvSize chroma_size = {w/2, h/2};
    chromau = cvCreateImage(chroma_size, IPL_DEPTH_8U, 1);
    chromav = cvCreateImage(chroma_size, IPL_DEPTH_8U, 1);
    if (!chromau || !chromav) goto realerr;
    cvNamedWindow("luma", 1);
    cvNamedWindow("chromau", 1);
    cvNamedWindow("chromav", 1);
    cvMoveWindow("luma", 0, 0);
    cvMoveWindow("chromau", ctx.img->width, 0);
    cvMoveWindow("chromav", ctx.img->width, ctx.img->height/2 + 55);

    while (1) {
        IplImage *img = capture_frame(&ctx);
        if (!img) goto realerr;
        calc_hist(img, hist);
        cvShowImage("luma", img);
        release_frame(&ctx);
        if ((cvWaitKey(1)&255)==27)break; // esc
    }

    cvDestroyWindow("luma");
    cvDestroyWindow("chromau");
    cvDestroyWindow("chromav");
    stop_capture(&ctx);
    cvReleaseImage(&chromau);
    cvReleaseImage(&chromav);
    free(hist);
    free(pbuf);
    return 0;

 realerr:
    fprintf(stderr, "error capturing\n");
    return 1;
}
