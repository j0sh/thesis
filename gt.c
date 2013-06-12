#include <stdio.h>

#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui_c.h>

#include "prop.h"

#define XY_TO_X(x) ((x)&((1<<16)-1))
#define XY_TO_Y(y) ((y)>>16)
static void xy2img(IplImage *xy, IplImage *img, IplImage *recon)
{
    int w = xy->width, h = xy->height, i, j;
    int xystride = xy->widthStep/sizeof(int32_t);
    int rstride = recon->widthStep;
    int stride = img->widthStep;
    int32_t *xydata = (int32_t*)xy->imageData;
    uint8_t *rdata = (uint8_t*)recon->imageData;
    uint8_t *data = (uint8_t*)img->imageData;

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            int v = xydata[i*xystride + j];
            int x = XY_TO_X(v), y = XY_TO_Y(v);
            int rd = i*rstride + j*3, dd = y*stride + x*3;
            *(rdata + rd + 0) = *(data + dd + 0);
            *(rdata + rd + 1) = *(data + dd + 1);
            *(rdata + rd + 2) = *(data + dd + 2);
        }
    }
}
#undef XY_TO_X
#undef XY_TO_Y

static IplImage *alignedImage(CvSize dim, int depth, int chan, int align)
{
    int w = dim.width, h = dim.height;
    int dx = align - (w % align);
    w += (dx != align) * dx;
    CvSize s = {w, h};
    return cvCreateImage(s, depth, chan);
}

static IplImage *alignedImageFrom(char *file, int align)
{
    IplImage *pre = cvLoadImage(file, CV_LOAD_IMAGE_COLOR);
    IplImage *img = alignedImage(cvGetSize(pre), pre->depth, pre->nChannels, align);
    char *pre_data = pre->imageData;
    char *img_data = img->imageData;
    int i;
    for (i = 0; i < pre->height; i++) {
        memcpy(img_data, pre_data, pre->widthStep);
        img_data += img->widthStep;
        pre_data += pre->widthStep;
    }
    cvReleaseImage(&pre);
    return img;
}

static int64_t sumimg(IplImage *img, int kern)
{
    int64_t sum = 0;
    int i, j;
    uint8_t *data;
    for (i = 0; i < img->height; i++) {
        data =  (uint8_t*)img->imageData + i*img->widthStep;
        for (j = 0; j < img->width - kern + 1; j++) {
            sum += *data++;
            sum += *data++;
            sum += *data++;
        }
    }
    return sum;
}

#include <sys/time.h>
static inline double get_time()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec * 1e-6;
}

int main(int argc, char **argv)
{
    double start, end;
#define SAMEAS(q) cvCreateImage(cvGetSize((q)), (q)->depth, (q)->nChannels)
    char *s, *d, *g;
    if (argc < 4) {
        s = "lena.png";
        d = "eva.jpg";
        g = "eva-gt.png";
    } else {
        s = argv[1];
        d = argv[2];
        g = argv[3];
    }
    printf("gt: \"%s\" \"%s\" \"%s\"\n", s, d, g);
    IplImage *src = alignedImageFrom(s, 8);
    IplImage *dst = alignedImageFrom(d, 8);
    IplImage *gt = alignedImageFrom(g, 8);
    IplImage *diff = SAMEAS(dst);
    IplImage *gtdiff = SAMEAS(dst);
    IplImage *diff2 = SAMEAS(dst);
    IplImage *diff3 = SAMEAS(dst);
    IplImage *match = SAMEAS(dst);
    start = get_time();
    IplImage *xy = prop_match(src, dst);
    xy2img(xy, src, match);
    end = get_time();
    cvAbsDiff(match, gt, diff);
    cvAbsDiff(gt, dst, gtdiff);
    cvAbsDiff(match, dst, diff2);
    cvAbsDiff(gtdiff, diff2, diff3);
    /*cvNamedWindow("match-gt", CV_WINDOW_NORMAL);
    cvNamedWindow("gt", CV_WINDOW_NORMAL);
    cvNamedWindow("match", CV_WINDOW_NORMAL);
    cvResizeWindow("match-gt", 960, 400);
    cvResizeWindow("gt", 960, 400);
    cvResizeWindow("match", 960, 400);*/
    //cvShowImage("gt", gt);
    //cvShowImage("match-dst", diff2);
    //cvShowImage("match-gt", diff);
    //cvShowImage("gt-dst" , gtdiff);
    //cvShowImage("match", match);
    //cvShowImage("gtdiff - matchdiff", diff3);
    printf("elapsed %f\n", (end-start)*1000);
    printf("match-gt %lld\n", sumimg(diff, 8));
    //cvWaitKey(0);
    cvReleaseImage(&src);
    cvReleaseImage(&dst);
    cvReleaseImage(&diff);
    cvReleaseImage(&diff2);
    cvReleaseImage(&gtdiff);
    cvReleaseImage(&match);
    cvReleaseImage(&gt);
    cvReleaseImage(&xy);
    return 0;
    return 0;
    return 0;
}
