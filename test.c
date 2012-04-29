#if 1
#include <stdio.h>
#include <opencv2/imgproc/imgproc_c.h>
#include "capture.h"

IplImage *avg, *diff, *i32, *bkg, *final, *t1, *t2;

static void calc_avgimg(IplImage *img)
{
    CvScalar s = cvScalar(1.0, 1.0, 1.0, 0);
    if (!avg) {
        CvSize size = { .width = img->width, .height = img->height };
        avg = cvCreateImage(size, IPL_DEPTH_32F, 3);
        i32 = cvCreateImage(size, IPL_DEPTH_32F, 3);
        diff = cvCreateImage(size, IPL_DEPTH_32F, 3);
        bkg = cvCreateImage(size, IPL_DEPTH_32F, 3);
        final = cvCreateImage(size, IPL_DEPTH_32F, 3);
        t1= cvCreateImage(size, IPL_DEPTH_32F, 3);
        t2= cvCreateImage(size, IPL_DEPTH_32F, 3);
        cvConvertScale(img, i32, 1/255.0, 0);
        return;
    }
    cvConvertScale(img, i32, 1/255.0, 0);

    cvAbsDiff(i32, avg, diff);
    cvRunningAvg(i32, avg, 0.2, NULL);
    
    cvMul(i32, diff, t1, 1.0);
    cvConvertScale(diff, diff, -1.0, 0);
    cvAddS(diff, s, diff, NULL);
    cvMul(bkg, diff, t2, 1.0);
    cvAdd(t1, t2, final, NULL);
}

int main(int argc, char **argv)
{
    int i = 0;
    capture_t ctx = {0};
    start_capture(&ctx);
    cvNamedWindow("avg", 1);
    cvNamedWindow("diff", 1);
    cvNamedWindow("bkg", 1);
    cvNamedWindow("final", 1);
    // capture background
    for (i = 0; i < 10; i++) {
        IplImage *img = capture_frame(&ctx);
        if (!img) goto realerr;
        calc_avgimg(img);
    }
    cvConvert(avg, bkg);
    while(1) {
        IplImage *img = capture_frame(&ctx);
        if (!img) goto realerr;
        calc_avgimg(img);
        cvShowImage("avg", avg);
        cvShowImage("diff", diff);
        cvShowImage("bkg", bkg);
        cvShowImage("final", final);
        release_frame(&ctx);
        if ((cvWaitKey(1)&255)==27)break; // esc
    }
    cvDestroyWindow("diff");
    cvDestroyWindow("avg");
    cvDestroyWindow("bkg");
    cvDestroyWindow("final");
    stop_capture(&ctx);
    cvReleaseImage(&avg);
    cvReleaseImage(&i32);
    cvReleaseImage(&diff);
    cvReleaseImage(&bkg);
    cvReleaseImage(&final);
    cvReleaseImage(&t1);
    cvReleaseImage(&t2);
    return 0;

 realerr:
    fprintf(stderr, "error capturing\n");
    return 1;
}
#endif
