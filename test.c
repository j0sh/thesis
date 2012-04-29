#if 1
#include <stdio.h>
#include <opencv2/imgproc/imgproc_c.h>
#include "capture.h"

IplImage *avg, *diff, *i32, *bkg, *final;

static void calc_avgimg(IplImage *img)
{
    if (!avg) {
        CvSize size = { .width = img->width, .height = img->height };
        avg = cvCreateImage(size, IPL_DEPTH_32F, 3);
        i32 = cvCreateImage(size, IPL_DEPTH_32F, 3);
        diff = cvCreateImage(size, IPL_DEPTH_32F, 3);
        bkg = cvCreateImage(size, IPL_DEPTH_32F, 3);
        final = cvCreateImage(size, IPL_DEPTH_32F, 3);
        cvConvertScale(img, i32, 1/255.0, 0);
        return;
    }
    cvConvertScale(img, i32, 1/255.0, 0);

    cvAbsDiff(i32, avg, diff);
    cvRunningAvg(i32, avg, 0.2, NULL);


    int i, j, k = 0;
    char *data = final->imageData;
    char *diffdata = diff->imageData;
    char *imgdata = i32->imageData;
    char *bkgdata = bkg->imageData;
    for (i = 0; i < img->width; i++) {
        float *final_p = (float*)(data + k);
        float *thresh_p = (float*)(diffdata + k);
        float *img_p = (float*)(imgdata + k);
        float *bkg_p = (float*)(bkgdata + k);
        for (j = 0; j < img->height; j++) {
            float p = *thresh_p++;
            *final_p++ = (p * *img_p++) + ((1.0 - p) * *bkg_p++);
            p = *thresh_p++;
            *final_p++ = (p * *img_p++) + ((1.0 - p) * *bkg_p++);
            p = *thresh_p++;
            *final_p++ = (p * *img_p++) + ((1.0 - p) * *bkg_p++);

        }
        k += img->widthStep*img->nChannels;
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
