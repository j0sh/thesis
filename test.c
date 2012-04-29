#if 1
#include <stdio.h>
#include <opencv2/imgproc/imgproc_c.h>
#include "capture.h"

IplImage *avg, *diff, *i32, *bkg, *gray, *final;

static void calc_avgimg(IplImage *img)
{
    if (!avg) {
        CvSize size = { .width = img->width, .height = img->height };
        avg = cvCreateImage(size, IPL_DEPTH_32F, 1);
        gray = cvCreateImage(size, IPL_DEPTH_32F, 1);
        i32 = cvCreateImage(size, IPL_DEPTH_32F, 3);
        diff = cvCreateImage(size, IPL_DEPTH_32F, 1);
        bkg = cvCreateImage(size, IPL_DEPTH_32F, 1);
        final = cvCreateImage(size, IPL_DEPTH_32F, 1);
        cvConvertScale(img, i32, 1/255.0, 0);
        cvCvtColor(i32, gray, CV_RGB2GRAY);
        cvConvert(gray, avg);
        return;
    }
    cvConvertScale(img, i32, 1/255.0, 0);
    cvCvtColor(i32, gray, CV_RGB2GRAY);

    cvAbsDiff(gray, avg, diff);
    cvRunningAvg(gray, avg, 0.10, NULL);


    int i, j, k = 0;
    char *data = final->imageData;
    char *diffdata = diff->imageData;
    char *imgdata = gray->imageData;
    char *bkgdata = bkg->imageData;
    for (i = 0; i < img->width; i++) {
        float *final_p = (float*)(data + k);
        float *thresh_p = (float*)(diffdata + k);
        float *img_p = (float*)(imgdata + k);
        float *bkg_p = (float*)(bkgdata + k);
        for (j = 0; j < img->height; j++) {
            float x = *thresh_p;
            float mu = 5.5, sigma = sqrt(0.50);
            float a = 1/(sigma * 2.5066); // 2.5066 == sqrt(2*pi)
            float b = (x - mu)/sigma;
            float c = -0.5*b*b;
            float scaled_p = a*exp(c);
            scaled_p = *thresh_p * 1.2;
            scaled_p = scaled_p >= 1.0 ? 1.0 : scaled_p;
            scaled_p = scaled_p <= 0.0 ? 0.0 : scaled_p;
            *final_p = (scaled_p * *img_p) + ((1.0 - scaled_p) * *bkg_p);
            final_p++;
            thresh_p++;
            img_p++;
            bkg_p++;
        }
        k += img->widthStep;
    }
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
    return 0;

 realerr:
    fprintf(stderr, "error capturing\n");
    return 1;
}
#endif
