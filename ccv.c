#include <ccv.h>
#include "capture.h"

#include <sys/time.h>
static inline double get_time()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec * 1e-6;
}

#if 1
int main(int argc, char **argv)
{
    capture_t ctx = {0};
    ccv_rect_t start_r = ccv_rect(125, 100, 100, 100);

    start_capture(&ctx);
    ccv_enable_default_cache();
    ccv_tld_info_t info;
    ccv_tld_param_t parms = ccv_tld_default_params;
    ccv_dense_matrix_t *x = NULL, *y = NULL;
    ccv_tld_t *tld = NULL;

    // prime the tracker
    IplImage *img = capture_frame(&ctx);
    if (!img) return 1;
    parms.rotation = 10;
    ccv_read(img->imageData, &x, CCV_IO_RGB_RAW | CCV_IO_GRAY, img->height, img->width, ctx.d_stride[0]);
    tld = ccv_tld_new(x, start_r, parms);
    release_frame(&ctx);

    int nbf = 0;
    double start_time, t;
    while (++nbf) {
        IplImage *img = capture_frame(&ctx);
        if (!img) break;
        ccv_read(img->imageData, &y, CCV_IO_RGB_RAW | CCV_IO_GRAY, img->height, img->width, ctx.d_stride[0]);
        start_time = get_time();
        ccv_comp_t box = ccv_tld_track_object(tld, x, y, &info);
        t += (get_time() - start_time);
        int x1 = box.rect.x, y1 = box.rect.y;
        int x2 = x1+box.rect.width, y2 = y1+box.rect.height;
        CvPoint p1 = {x1, y1}, p2 = {x2, y2};
        cvRectangle(img, p1, p2, CV_RGB(0, 255, 0), 2, 8, 0);
        cvShowImage("cap", img);
        if (!(nbf % 10))
            printf("%f s (%f ms)\n", t/nbf, (t/nbf)*1000);
        x = y;
        y = NULL;
        release_frame(&ctx);
        if ((cvWaitKey(1)&255)==27) break; //esc
    }

    stop_capture(&ctx);
    cvDestroyWindow("cap");
    ccv_matrix_free(x);
    ccv_tld_free(tld);
    ccv_disable_cache();
    return 0;
}
#endif
