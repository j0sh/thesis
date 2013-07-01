#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui_c.h>

static void print_usage(char **argv)
{
    printf("Usage: %s <path> [outfile]\n", argv[0]);
    exit(1);
}

static IplImage* resize(IplImage *img, CvSize sz)
{
    IplImage *ret = cvCreateImage(sz, img->depth, img->nChannels);
    cvResize(img, ret, CV_INTER_CUBIC);
    return ret;
}

static IplImage *pyrstep(IplImage *img)
{
    CvSize sz = cvGetSize(img);
    sz.width = sz.width / 2 + 1;
    sz.height = sz.height / 2 + 1;
    IplImage *pyrd = cvCreateImage(sz, img->depth, img->nChannels);
    cvPyrDown(img, pyrd, CV_GAUSSIAN_5x5);
    return pyrd;
}

static IplImage* window(IplImage *img)
{
    CvSize sz = cvGetSize(img);
    IplImage *out = cvCreateImage(sz, img->depth, img->nChannels);
    int w = sz.width, h = sz.height, x, y, i;
    int stride = out->widthStep/sizeof(float);
    for (y = 0; y < h; y++) {
        float *d = (float*)out->imageData + (y*stride);
        for (x = 0; x < w; x++) {
            CvRect roi = {x - 5, y - 5, 9, 9};
            cvSetImageROI(img, roi);
            for (i = 0; i < out->nChannels; i++) {
                CvScalar sum = cvSum(img);
                *d++ = sum.val[i]/81.0;
            }
        }
    }
    cvResetImageROI(img);
    return out;
}

// multi-scale contrast using a gaussian pyramid, described in [1]
// [1]: Tie Lu, et al. Learning to Detect a Salient Object:
// http://research.microsoft.com/en-us/um/people/jiansun/papers/SalientDetection_CVPR07.pdf
static IplImage* dopyr(IplImage *img)
{
    int i;
    CvSize sz = cvGetSize(img);
    IplImage *gray = cvCreateImage(sz, img->depth, 1);
    IplImage *img0 = cvCreateImage(sz, IPL_DEPTH_32F, 1);
    cvCvtColor(img, gray, CV_BGR2GRAY);
    cvConvertScale(gray, img0, 1/255.0, 0);
    IplImage *prev = img0, *prev_r = img0;
    IplImage *out = cvCreateImage(sz, IPL_DEPTH_32F, img0->nChannels);
    IplImage *sum = cvCreateImage(sz, out->depth, out->nChannels);
    cvXor(sum, sum, sum, NULL);
    for (i = 1; i < 6; i++) {
        IplImage *level = pyrstep(prev);
        IplImage *resized = resize(level, sz);
        cvAbsDiff(prev_r, resized, out);
        double min, max;
        cvMinMaxLoc(out, &min, &max, NULL, NULL, NULL);
        cvConvertScale(out, out, 1.0/(max - min), -min);
        //cvMul(out, out, out, 1);
        cvAcc(out, sum, NULL);
        if (prev != img0) cvReleaseImage(&prev);
        if (prev_r != img0) cvReleaseImage(&prev_r);
        prev = level;
        prev_r = resized;
    }
    cvConvertScale(sum, out, 1.0/i, 0);
    return out;
}

static IplImage *sum_channels(IplImage *img)
{
    /*CvMat hdr;
    CvSize sz = {img->width, img->height};
    IplImage out_hdr, *out = cvCreateImage(sz, img->depth, 3), *foo;
    foo = cvReshape(img, &hdr, 3, img->width*img->height);
    out = cvGetImage(&hdr, &out_hdr);
    cvReduce(&out_hdr, out, 1, CV_REDUCE_SUM);
    cvReshape(out, &hdr, 3, img->height);
    return cvGetImage(&hdr, &out_hdr);*/
    CvSize sz = cvGetSize(img);
    IplImage *out = cvCreateImage(sz, img->depth, 1);
    IplImage *a = cvCreateImage(sz, img->depth, 1);
    IplImage *b = cvCreateImage(sz, img->depth, 1);
    IplImage *c = cvCreateImage(sz, img->depth, 1);
    cvXor(out, out, out, NULL);
    cvSplit(img, a, b, c, NULL);
    cvAcc(a, out, NULL);
    cvAcc(b, out, NULL);
    cvAcc(c, out, NULL);
    cvReleaseImage(&a);
    cvReleaseImage(&b);
    cvReleaseImage(&c);
    return out;
}

// saliency map from Frequency-Tuned Salient Region Detection
// http://infoscience.epfl.ch/record/135217/files/1708.pdf
static IplImage* avgdiff(IplImage *img)
{
    int i;
    CvSize sz = cvGetSize(img);
    IplImage *sm = cvCreateImage(sz, IPL_DEPTH_32F, img->nChannels);
    IplImage *out = cvCreateImage(sz, IPL_DEPTH_32F, img->nChannels);
    IplImage *lab = cvCreateImage(sz, IPL_DEPTH_32F, img->nChannels);
    cvConvertScale(img, out, 1/255.0, 0);
    cvCvtColor(out, lab, CV_BGR2Lab);
    CvScalar avg = cvAvg(lab, NULL);
    for (i = 0; i < lab->nChannels; i++) avg.val[i] = -avg.val[i];
    cvSmooth(lab, sm, CV_GAUSSIAN, 5, 5, 0, 0);
    cvAddS(sm, avg, out, NULL);
    cvMul(out, out, out, 1);
    IplImage *o = sum_channels(out);
    cvReleaseImage(&out);
    cvReleaseImage(&lab);
    cvReleaseImage(&sm);
    return o;
}

static void normalize(IplImage *img)
{
    double min, max;
    cvMinMaxLoc(img, &min, &max, NULL, NULL, NULL);
    CvScalar scalar = cvRealScalar(-min);
    cvAddS(img, scalar, img, NULL);
    cvConvertScale(img, img, 1.0/(max - min), 0);
}

static IplImage *binarize(IplImage *img)
{
    CvSize sz = cvGetSize(img);
    //CvScalar avg = cvAvg(img, NULL);
    //double thresh = avg.val[0] * 2;
    IplImage *i8 = cvCreateImage(sz, IPL_DEPTH_8U, img->nChannels);
    IplImage *out = cvCreateImage(sz, IPL_DEPTH_32F, img->nChannels);
    IplImage *seg = cvCreateImage(sz, IPL_DEPTH_8U, img->nChannels);
    cvConvertScale(img, i8, 255, 0);

/*
    CvSeq *comp;
    CvMemStorage *storage = cvCreateMemStorage(1000);
    int threshold1=255, threshold2=30, level=4;
    img->width &= -(1 << level);
    img->height &= -(1 << level);
    IplImage *i0 = cvCloneImage(img);
    IplImage *i1 = cvCloneImage(img);
    cvPyrSegmentation(i0, i1, storage, &comp, level, threshold1+1, threshold2+1);
    return i1;*/


    CvTermCriteria cvt = cvTermCriteria(CV_TERMCRIT_ITER+CV_TERMCRIT_EPS, 5, 1);
    cvPyrMeanShiftFiltering(img, seg, 40, 40, 4, cvt);
    cvConvertScale(seg, out, 1/255.0, 0);
    //cvReleaseImage(&i8);
    //cvReleaseImage(&seg);
    return seg;
}

static void write2file(char *fname, IplImage *img)
{
    IplImage *out = cvCreateImage(cvGetSize(img), IPL_DEPTH_8U, img->nChannels);
    cvConvertScale(img, out, 255, 0);
    cvSaveImage(fname, out, 0);
    cvReleaseImage(&out);
}

static void print_args(int argc, char **argv)
{
    int i;
    printf("pyr: ");
    for (i = 1; i < argc; i++) printf("%s ", argv[i]);
    printf("\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) print_usage(argv);
    print_args(argc, argv);
    IplImage *img = cvLoadImage(argv[1], CV_LOAD_IMAGE_COLOR);
    IplImage *out = avgdiff(img);
    normalize(out);
    if (argc < 3) {
        //IplImage *bin = binarize(img);
        //normalize(bin);
        //cvShowImage("segmented", bin);
        cvShowImage("in", img);
        cvShowImage("out", out);
        cvWaitKey(0);
        //cvReleaseImage(&bin);
    } else write2file(argv[2], out);
    cvReleaseImage(&out);
    cvReleaseImage(&img);
    return 0;
}
