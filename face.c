#if 0
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include <cv.h>
#include "capture.h"

static void detect(IplImage *img, CvHaarClassifierCascade *cascade)
{
    CvMemStorage *storage = cvCreateMemStorage(0);
    CvSeq *faces;
    CvSize minsize = {.width = 10, .height = 10};
    CvSize maxsize = {.width = 100, .height = 100};
    int i;
    faces = cvHaarDetectObjects(img, cascade, storage, 1.2, 2, CV_HAAR_DO_CANNY_PRUNING, minsize, maxsize);
    for (i = 0; i < faces->total; i++) {
        CvRect face_r = *(CvRect*)cvGetSeqElem(faces, i);
        cvRectangle(img, cvPoint(face_r.x, face_r.y), cvPoint(face_r.x+face_r.width, face_r.y+face_r.height), CV_RGB(255, 0, 0), 3, 3, 0);
    }
    cvReleaseMemStorage(&storage);
}

#include <sys/time.h>
static inline double get_time()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec * 1e-6;
}

static IplImage *global_img;
static inline void broken_lockfree_capture(capture_t *ctx)
{
    // this doesnt work mostly because the old pointer shouldn't be
    // deallocated on the spot as it'll still be used by another thread
    IplImage *img = capture_frame(ctx);
    CvSize s = {.width = img->width, .height = img->height};
    IplImage *copy = cvCreateImage(s, img->depth, img->nChannels);
    IplImage *old = global_img;
    cvCopy(img, copy, NULL);
    global_img = copy; // for true lock-freedom, use an atomic CAS
    cvReleaseImage(&old); // wrong; breaks. comment out for a memleak
    release_frame(ctx);
    usleep(15);
}

static inline void capture(capture_t *ctx)
{
    // nonatomic
    global_img = capture_frame(ctx);
    usleep(15);
    global_img = NULL;
    release_frame(ctx);
}
static int threadfinished = 0;
static void *capture_buffer(void *args)
{
    capture_t *ctx = args;
    while (!threadfinished) {
        //broken_lockfree_capture(ctx);
        capture(ctx);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_t thread = {0};
    capture_t ctx = {0};
    CvHaarClassifierCascade *cascade = cvLoad("haarfacedata.xml", NULL, NULL, NULL);
    start_capture(&ctx);
    pthread_create(&thread, NULL, capture_buffer, &ctx);
    cvNamedWindow("result", 1);
    while(1) {
        IplImage *img = global_img;
        if (!img) continue;
        detect(img, cascade);
        cvShowImage("result", img);
        if ((cvWaitKey(1)&255)==27) break; //esc
    }
    threadfinished = 1; // kill the thread
    cvDestroyWindow("result");
    stop_capture(&ctx);
    cvReleaseHaarClassifierCascade(&cascade);
    return 0;
}
#endif
