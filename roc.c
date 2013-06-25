#include <stdio.h>

#include <opencv2/highgui/highgui_c.h>
#include <opencv2/imgproc/imgproc_c.h>

static void print_usage(char **argv)
{
    printf("Usage: %s <file1> <file2> <file3>\n", argv[0]);
    exit(1);
}

static int tp, fp, fn, tn;
static void cmp_thresh(IplImage *a, IplImage *gt, double thresh)
{
    int w = gt->width, h = gt->height, x, y;
    int stride = gt->widthStep;
    uint8_t *tdata, *gdata;
    IplImage *thr = cvCloneImage(a);
    cvThreshold(a, thr, thresh, 0.0, CV_THRESH_TOZERO);
    tp = 0, fp = 0, fn = 0, tn = 0;
    for (y = 0; y < h; y++) {
        tdata = (uint8_t*)thr->imageData + y*stride;
        gdata = (uint8_t*)gt->imageData + y*stride;
        for (x = 0; x < w; x++) {
            if (*gdata && *tdata) tp++;
            else if (*gdata) fn++;
            else if (*tdata) fp++;
            else tn++;
            gdata++;
            tdata++;
        }
    }
}

static IplImage* resize(IplImage *img, CvSize sz)
{
    IplImage *ret = cvCreateImage(sz, img->depth, img->nChannels);
    cvResize(img, ret, CV_INTER_CUBIC);
    return ret;
}

static void find_roc(IplImage *a, IplImage *gt)
{
    int i, r = 0;
    if (gt->width != a->width || gt->height != a->height) {
        gt = resize(gt, cvGetSize(a));
        r = 1;
    }
    for (i = 1; i < 255; i++) {
        cmp_thresh(a, gt, i);
        printf("%d: tp %d fp %d fn %d tn %d precision %f recall %f fpr %f\n",
            i, tp, fp, fn, tn,
            tp/(double)(tp+fp), tp/(double)(tp+fn),
            fp/(double)(fp+tn));
    }
    if (r) cvReleaseImage(&gt);
}

static void print_args(int argc, char **argv)
{
    int i;
    printf("roc: ");
    for (i = 1; i < argc; i++) printf("%s ", argv[i]);
    printf("\n");
}

int main(int argc, char **argv)
{
    if (argc < 3) print_usage(argv);
    print_args(argc, argv);
    IplImage *in = cvLoadImage(argv[2], CV_LOAD_IMAGE_GRAYSCALE);
    IplImage *gt = cvLoadImage(argv[1], CV_LOAD_IMAGE_GRAYSCALE);
    find_roc(in, gt);
    cvReleaseImage(&in);
    cvReleaseImage(&gt);
    return 0;
}
