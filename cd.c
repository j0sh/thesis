// processing changedetection.net dataset
#include <stdio.h>
#include <stdlib.h>

#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui_c.h>

static void print_usage(char **argv)
{
    printf("Usage: %s <path> <start> <end>\n", argv[0]);
    exit(1);
}

static char fname[1024];
static char* mkname(char *path, int i)
{
    snprintf(fname, sizeof(fname), "%s/in%06d.jpg", path, i);
    return fname;
}

int main(int argc, char **argv)
{
    if (argc < 4) print_usage(argv);
    char *path = argv[1];
    int start = atoi(argv[2]);
    int end = atoi(argv[3]);
    int i;
    IplImage *img;
    for (i = 0; i < end; i++) {
        img = cvLoadImage(mkname(path, i), CV_LOAD_IMAGE_COLOR);
        cvShowImage("img", img);
        if ((cvWaitKey(1)&255)==27)break; // esc
        cvReleaseImage(&img);
    }
    return 0;
}
