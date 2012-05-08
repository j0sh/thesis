#if 0
#include <stdio.h>
#include <sys/time.h>
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

static inline double get_time()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec * 1e-6;
}

int main(int argc, char **argv)
{
    int i = 0;
    capture_t ctx = {0};
    start_capture(&ctx);
    cvNamedWindow("final", 1);
    // capture background
    for (i = 0; i < 10; i++) {
        IplImage *img = capture_frame(&ctx);
        if (!img) goto realerr;
        calc_avgimg(img);
    }
    cvConvert(avg, bkg);
    double ms = 0; int nbf = 0;
    while(1) {
        IplImage *img = capture_frame(&ctx);
        if (!img) goto realerr;
        double time = get_time();
        calc_avgimg(img);
        ms += (get_time() - time);
        cvShowImage("final", final);
        release_frame(&ctx);
        nbf++;
        if ((cvWaitKey(1)&255)==27)break; // esc
    }
    cvDestroyWindow("final");
    stop_capture(&ctx);
    cvReleaseImage(&avg);
    cvReleaseImage(&i32);
    cvReleaseImage(&diff);
    cvReleaseImage(&bkg);
    cvReleaseImage(&final);
    cvReleaseImage(&t1);
    cvReleaseImage(&t2);
    printf("avgtime : %f ms\n", (ms/nbf)*1000);
    return 0;

 realerr:
    fprintf(stderr, "error capturing\n");
    return 1;
}
#endif
