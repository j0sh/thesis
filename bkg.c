#include <opencv2/imgproc/imgproc_c.h>
#include "capture.h"

static void calc_mbdiffs(IplImage *img)
{
    int w = img->width, h = img->height;
    int mx = w/16 + (0 != w % 16), my = h/16 + (0 != h % 16);
    int x, y, j;
    char *data = img->imageData;
    int stride = img->widthStep;
    int size = mx*my*sizeof(uint8_t);
    uint8_t *mbdiffs = malloc(size);
    memset(mbdiffs, 0, size);
    for (y = 0; y < h; y++) {
        int mby = y/16, mbx = 0;
        uint8_t*line = (uint8_t*)data;
        for (x = 0; x < w; x+= 16) {
            int mbxy = mby*mx + mbx;
            int diffs = 0;
            for (j = 0; j < 16; j++) { diffs += *line > 0; line++; }
            mbdiffs[mbxy] += diffs;
            mbx += 1;
        }
        data += stride;
    }
    data = img->imageData;
    for (y = 0; y < h; y++) {
        int mby = y/16, mbx = 0;
        uint8_t *line = (uint8_t*)data;
        for (x = 0; x < w; x += 16) {
            int mbxy = mby*mx + mbx;
            int val = (mbdiffs[mbxy] > 10) * 255;
            for (j = 0; j < 16; j++) *line++ = val;
            mbx += 1;
        }
        data += stride;
    }
    free(mbdiffs);
}

int main(int argc, char **argv)
{
    IplImage *avg, *i32, *diff, *dxdy, *dx, *dy, *lap, *prev = NULL;
    IplImage *sub, *mask, *nmask, *gray, *gray8, *masked;
    char *fname = "bbb.mkv";
    //char *fname = "/home/josh/Desktop/irene.mkv";

    capture_t ctx = {0};
    ctx.filename = fname;
    start_capture(&ctx);
    cvNamedWindow("diffavg", 1);
    cvNamedWindow("diff", 1);
    cvNamedWindow("smoothed", 1);
    cvNamedWindow("actual", 1);
    cvNamedWindow("1st derivative", 1);
    cvNamedWindow("2nd derivative (Laplacian)", 1);
    cvNamedWindow("masked", 1);
    CvSize size = { .width = ctx.img->width,
                    .height = ctx.img->height };
    avg = cvCreateImage(size, IPL_DEPTH_32F, 3);
    i32 = cvCreateImage(size, IPL_DEPTH_32F, 3);
    diff = cvCreateImage(size, IPL_DEPTH_32F, 3);
    dx = cvCreateImage(size, IPL_DEPTH_32F, 3);
    dy = cvCreateImage(size, IPL_DEPTH_32F, 3);
    lap = cvCreateImage(size, IPL_DEPTH_32F, 3);
    dxdy = cvCreateImage(size, IPL_DEPTH_32F, 3);
    sub = cvCreateImage(size, IPL_DEPTH_32F, 3);
    gray = cvCreateImage(size, IPL_DEPTH_32F, 1);
    gray8 = cvCreateImage(size, IPL_DEPTH_8U, 1);
    mask = cvCreateImage(size, IPL_DEPTH_8U, 1);
    nmask = cvCreateImage(size, IPL_DEPTH_8U, 1);
    masked = cvCreateImage(size, IPL_DEPTH_8U, 3);
    cvMoveWindow("diffavg", 0, 0);
    cvMoveWindow("diff", size.width, 0);
    cvMoveWindow("smoothed", 2*size.width, 0);
    cvMoveWindow("actual", 3*size.width, 0);
    cvMoveWindow("1st derivative", 0, size.height+50);
    cvMoveWindow("2nd derivative (Laplacian)", size.width, size.height+50);
    cvMoveWindow("masked", 2*size.width, size.height+50);
    while (1) {
        IplImage *img = capture_frame(&ctx);
        if (!img) goto realerr;

        // do processing
        if (!prev) {
            prev = cvCreateImage(size, IPL_DEPTH_32F, 3);
            continue;
        }

        cvConvertScale(img, i32, 1/255.0, 0);
        cvSmooth(i32, i32, CV_GAUSSIAN, 9, 9, 0, 0);
        cvAbsDiff(i32, prev, diff);
        cvSmooth(diff, diff, CV_GAUSSIAN, 9, 9, 0, 0);
        cvSobel(diff, dx, 1, 0, 3);
        cvSobel(diff, dy, 0, 1, 3);
        cvLaplace(diff, lap, 3);
        cvAdd(dx, dy, dxdy, NULL);
        cvAdd(diff, dxdy, sub, NULL);
        cvCvtColor(diff, gray, CV_RGB2GRAY);
        cvConvertScale(gray, gray8, 256.0, 0);
        cvAdaptiveThreshold(gray8, mask, 255, CV_ADAPTIVE_THRESH_GAUSSIAN_C, CV_THRESH_BINARY, 3, 0);
        calc_mbdiffs(mask);
        cvSmooth(mask, mask, CV_GAUSSIAN, 9, 9, 0, 0);
        cvNot(mask, nmask);
        cvXor(masked, masked, masked, NULL);
        cvCopy(img, masked, mask);
        cvRunningAvg(diff, avg, 0.05, NULL);

        cvShowImage("diffavg", avg);
        cvShowImage("diff", diff);
        cvShowImage("smoothed", i32);
        cvShowImage("actual", img);
        cvShowImage("1st derivative", dxdy);
        cvShowImage("2nd derivative (Laplacian)", lap);
        cvShowImage("masked", masked);

        release_frame(&ctx);
        cvCopy(i32, prev, NULL);
        if ((cvWaitKey(1)&255)==27) break; // esc
    }
    cvDestroyWindow("diff");
    cvDestroyWindow("diffavg");
    cvDestroyWindow("smoothed");
    cvDestroyWindow("actual");
    cvDestroyWindow("1st derivative");
    cvDestroyWindow("2nd derivative (Laplacian)");
    cvDestroyWindow("masked");
    stop_capture(&ctx);
    cvReleaseImage(&avg);
    cvReleaseImage(&i32);
    cvReleaseImage(&diff);
    cvReleaseImage(&dxdy);
    cvReleaseImage(&dx);
    cvReleaseImage(&dy);
    cvReleaseImage(&lap);
    cvReleaseImage(&sub);
    cvReleaseImage(&mask);
    cvReleaseImage(&nmask);
    cvReleaseImage(&masked);
    cvReleaseImage(&gray);
    cvReleaseImage(&gray8);
    if (prev) cvReleaseImage(&prev);
    return 0;
realerr:
    fprintf(stderr, "error capturing\n");
    return 1;
}
