#include <stdio.h>
#include <stdint.h>

#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui_c.h>

#include "wht.h"

// 8-basis walsh-hadamard transform.
static void wht8(int16_t *in, int16_t *out)
{
    // step 1
    int16_t g[8];
    int16_t h[8];

    g[0] = in[0] + in[4];
    g[1] = in[1] + in[5];
    g[2] = in[2] + in[6];
    g[3] = in[3] + in[7];
    g[4] = in[0] - in[4];
    g[5] = in[1] - in[5];
    g[6] = in[2] - in[6];
    g[7] = in[3] - in[7];

    // step 2
    h[0] = g[0] + g[2];
    h[1] = g[1] + g[3];
    h[2] = g[0] - g[2];
    h[3] = g[1] - g[3];

    h[4] = g[4] + g[6];
    h[5] = g[5] + g[7];
    h[6] = g[4] - g[6];
    h[7] = g[5] - g[7];

    // step 3
    out[0] = h[0] + h[1];
    out[1] = h[0] - h[1];

    out[2] = h[2] + h[3];
    out[3] = h[2] - h[3];

    out[4] = h[4] + h[5];
    out[5] = h[4] - h[5];

    out[6] = h[6] + h[7];
    out[7] = h[6] - h[7];
}

// 8-basis inverse walsh-hadamard transform.
static void iwht8(int16_t *in, int16_t *out)
{
    wht8(in, out);
    out[0] /= 8;
    out[1] /= 8;
    out[2] /= 8;
    out[3] /= 8;
    out[4] /= 8;
    out[5] /= 8;
    out[6] /= 8;
    out[7] /= 8;
}

static void transpose_8to16(uint8_t *in, int16_t *out, int w, int h, int istride, int ostride)
{
    int i, j;
    for (i = 0; i < h; i++)  {
        for (j = 0; j < w; j++) {
            *(out + j * ostride + i) = *(in + i*istride + j);
        }
    }
}

static void transpose16_inplace(int16_t *in, int w, int stride)
{
    int i, j;
    for (i = 0; i < w; i++)  {
        for (j = w-1; j >= i; j--) {
            // swap
            int m = i*stride + j, n = j*stride + i;
            int16_t tmp = *(in + m);
            *(in + m) = *(in + n);
            *(in + n) = tmp;
        }
    }
}

static void transpose16(int16_t *in, int16_t *out, int w, int h, int istride, int ostride)
{
    int i, j;
    for (i = 0; i < h; i++)  {
        for (j = 0; j < w; j++) {
            *(out + j*ostride + i) = *(in + i*istride + j);
        }
    }
}

static void wht2d_i(IplImage *img, IplImage *out, int inv)
{
    int w = img->width, h = img->height, i, j;
    CvSize size = { h, w };
    IplImage *t1 = cvCreateImage(size, IPL_DEPTH_16S, 1);
    IplImage *t2 = cvCreateImage(cvGetSize(out), IPL_DEPTH_16S, 1);
    IplImage *t3 = cvCreateImage(size, IPL_DEPTH_16S, 1);
    int stride  = img->widthStep;
    int ostride = out->widthStep/sizeof(int16_t);
    int tstride = t1->widthStep/sizeof(int16_t);
    int16_t *odata = (int16_t*)out->imageData, *o = odata;
    int16_t *tdata = (int16_t*)t1->imageData, *t = tdata;
    int16_t *udata  = (int16_t*)t2->imageData, *u = udata;
    int16_t *vdata  = (int16_t*)t3->imageData, *v = vdata;

    // 1. transpose image, doing 8-16 conversion if necessary
    // 2. transform every 8 pixels (vertically)
    // 3. transpose image again
    // 4. transform every 8 pixels (horizontally)

    assert((unsigned)img->depth == IPL_DEPTH_8U || IPL_DEPTH_16S == (unsigned)img->depth);
    if (IPL_DEPTH_8U == img->depth) {
        uint8_t *data = (uint8_t*)img->imageData;
        transpose_8to16(data, tdata, w, h, stride, tstride);
    } else if (IPL_DEPTH_16S == (unsigned)img->depth) {
        stride /= sizeof(int16_t);
        int16_t *data = (int16_t*)img->imageData;
        transpose16(data, tdata, w, h, stride, tstride);
    } else assert(0);

    for (j = 0; j < t1->height; j++) {
        for (i = 0; i < tstride; i += 8) {
            if (inv) iwht8(t+i, v+i); else wht8(t+i, v+i);
        }
        v += tstride;
        t += tstride;
    }
    transpose16(vdata, udata, t3->width, t3->height, tstride, ostride);
    for (j = 0; j < out->height; j++) {
        for (i = 0; i < ostride; i += 8) {
            if (inv) iwht8(u+i, o+i); else wht8(u+i, o+i);
        }
        o += ostride;
        u += ostride;
    }

    cvReleaseImage(&t1);
    cvReleaseImage(&t2);
    cvReleaseImage(&t3);
}

void wht2d(IplImage *img, IplImage *out)
{
    wht2d_i(img, out, 0);
}

void iwht2d(IplImage *img, IplImage *out)
{
    wht2d_i(img, out, 1);
}

#if 0
int main(int argc, char **argv)
{
    //IplImage *img = cvLoadImage("/home/josh/Pictures/eva-green-2.jpg", CV_LOAD_IMAGE_COLOR);
    IplImage *img = cvLoadImage("lena.png", CV_LOAD_IMAGE_COLOR);
    CvSize size = cvGetSize(img);
    IplImage *trans = cvCreateImage(size, IPL_DEPTH_16S, 1);
    IplImage *recon = cvCreateImage(size, IPL_DEPTH_16S, 1);
    IplImage *disp = cvCreateImage(size, IPL_DEPTH_16U, 1);
    IplImage *r = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *g = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *b = cvCreateImage(size, IPL_DEPTH_8U, 1);

    cvSplit(img, r, g, b, NULL);
    cvShowImage("orig", r);

    wht2d(r, trans);
    cvConvertScale(trans, disp, 256, 0); // opencv scales 16-bit imgs
    cvShowImage("transformed", disp);
    iwht2d(trans, recon);
    cvConvertScale(recon, disp, 256, 0);
    cvShowImage("reconstructed", disp);

    cvWaitKey(0);

    cvReleaseImage(&img);
    cvReleaseImage(&trans);
    cvReleaseImage(&r);
    cvReleaseImage(&g);
    cvReleaseImage(&b);
    return 0;
}
#endif
