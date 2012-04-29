#if 1
#include <stdio.h>
#include "capture.h"
int main(int argc, char **argv)
{
    int i;
    capture_t ctx = {0};
    start_capture(&ctx);
    cvNamedWindow("result", 1);
    for (i = 0; i < 100; i++) {
        IplImage *img = capture_frame(&ctx);
        if (!img) goto realerr;
        cvShowImage("result", img);
        release_frame(&ctx);
        if ((cvWaitKey(1)&255)==27)break; // esc
    }
    cvDestroyWindow("result");
    stop_capture(&ctx);
    return 0;

 realerr:
    fprintf(stderr, "error capturing\n");
    return 1;
}
#endif
