#include <stdio.h>
#include <limits.h>

#include <opencv2/highgui/highgui_c.h>
#include <opencv2/imgproc/imgproc_c.h>

#define PATCH_W 7
#define PM_ITERS 5
#define RS_MAX INT_MAX
#define XY_TO_INT(x, y) (((y) << 16) | (x))
#define XY_TO_X(x) ((x)&((1<<16)-1))
#define XY_TO_Y(y) ((y)>>16)

static int max(int a, int b) { return a >= b ? a : b; }
static int min(int a, int b) { return a <= b ? a : b; }

static int dist(IplImage *a, IplImage *b, int ax, int ay, int bx, int by, int cutoff)
{
    int ans = 0, dx, dy;
    uint8_t *adata = (uint8_t*)a->imageData;
    uint8_t *bdata = (uint8_t*)b->imageData;
    int astride = a->widthStep, bstride = b->widthStep;
    if (cutoff <= 0) cutoff = INT_MAX;
    for (dy = 0; dy < PATCH_W; dy++) {
        for (dx = 0; dx < PATCH_W; dx++) {
            int al = (dy+ay) * astride + (dx+ax) * 3;
            int bl = (dy+by) * bstride + (dx+bx) * 3;
            uint8_t *ad = adata + al;
            int ar = ad[0];
            int ag = ad[1];
            int ab = ad[2];
            uint8_t *bd = bdata + bl;
            int br = bd[0];
            int bg = bd[1];
            int bb = bd[2];
            int dr = ar - br, dg = ag - bg, db = ab - bb;
            ans +=  dr*dr + dg*dg + db*db;
        }
        if (ans > cutoff) return cutoff;
    }
    return ans;
}

static void improve_guess(IplImage *a, IplImage *b, int ax, int ay, int *xbest, int *ybest, int *dbest, int bx, int by)
{
    int db = *dbest;
    int d = dist(a, b, ax, ay, bx, by, db);
    if (d < db) {
        *dbest = d;
        *xbest = bx;
        *ybest = by;
    }
}

static void patchmatch(IplImage *a, IplImage *b, IplImage *ann, IplImage *annd)
{
    int iter, rs_start, mag;
    int w = ann->width, h = ann->height;
    int32_t *data = (int32_t*)ann->imageData;
    int32_t *ddata = (int32_t*)annd->imageData;
    int stride = ann->widthStep/sizeof(int32_t);
    int aew = a->width - PATCH_W + 1;
    int aeh = a->height - PATCH_W + 1;
    int bew = b->width - PATCH_W + 1;
    int beh = b->height - PATCH_W + 1;

    int ax, ay;

    // initialize with random values
    for (ay = 0; ay < h; ay++) {
        int xy = ay * stride;
        for (ax = 0; ax < w; ax++) {
            uint16_t bx = rand() % bew;
            uint16_t by = rand() % beh;
            *(data + xy) = XY_TO_INT(bx, by);
            *(ddata + xy) = dist(a, b, ax, ay, bx, by, 0);
            xy += 1;
        }
    }

    for (iter = 0; iter < PM_ITERS; iter++) {
        int ystart = 0, yend = aeh, ychange = 1;
        int xstart = 0, xend = aew, xchange = 1;
        if (iter % 2) {
            ystart = yend - 1; yend = -1; ychange = -1;
            xstart = xend - 1; xend = -1; xchange = -1;
        }
        for (ay = ystart; ay != yend; ay += ychange) {
            for (ax = xstart; ax != xend; ax += xchange) {
                // current best guess
                int xy = ay * stride + ax;
                int v = *(data + xy);
                int xbest = XY_TO_X(v), ybest = XY_TO_Y(v);
                int dbest = *(ddata + xy);

                // Propagation: Improve current guess by trying
                // correspondences from left and above,
                // or below and right on odd iterations
                if ((unsigned)(ax - xchange) < (unsigned)aew) {
                    int vp = *(data + ay * stride + (ax - xchange));
                    int xp = XY_TO_X(vp) + xchange, yp = XY_TO_Y(vp);
                    if ((unsigned)xp < (unsigned)bew) {
                        improve_guess(a, b, ax, ay, &xbest, &ybest, &dbest, xp, yp);
                    }
                }

                if ((unsigned)(ay - ychange) < (unsigned)aeh) {
                    int vp = *(data + ay * stride + (ax - xchange));
                    int xp = XY_TO_X(vp), yp = XY_TO_Y(vp) + ychange;
                    if ((unsigned)yp < (unsigned)beh) {
                        improve_guess(a, b, ax, ay, &xbest, &ybest, &dbest, xp, yp);
                    }
                }

                rs_start = RS_MAX;
                if (rs_start > max(b->width, b->height)) rs_start = max(b->width, b->height);
                for (mag = rs_start; mag >= 1; mag /= 2) {
                    // sampling window
                    int xmin = max(xbest - mag, 0);
                    int xmax = min(xbest + mag + 1, bew);
                    int ymin = max(ybest - mag, 0);
                    int ymax = min(ybest + mag + 1, beh);
                    int xp = xmin + rand() % (xmax - xmin);
                    int yp = ymin + rand() % (ymax - ymin);
                    improve_guess(a, b, ax, ay, &xbest, &ybest, &dbest, xp, yp);
                }

                *(data +  xy) = XY_TO_INT(xbest, ybest);
                *(ddata + xy) = dbest;
            }
        }
    }
}

static void xy2img(IplImage *xy, IplImage *img, IplImage *recon)
{
    int w = xy->width, h = xy->height, i, j;
    int xystride = xy->widthStep/sizeof(int32_t);
    int rstride = recon->widthStep;
    int stride = img->widthStep;
    int32_t *xydata = (int32_t*)xy->imageData;
    uint8_t *rdata = (uint8_t*)recon->imageData;
    uint8_t *data = (uint8_t*)img->imageData;

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            int v = xydata[i*xystride + j];
            int x = XY_TO_X(v), y = XY_TO_Y(v);
            int rd = i*rstride + j*3, dd = y*stride + x*3;
            *(rdata + rd + 0) = *(data + dd + 0);
            *(rdata + rd + 1) = *(data + dd + 1);
            *(rdata + rd + 2) = *(data + dd + 2);
        }
    }
}

#undef XY_TO_INT
#undef XY_TO_X
#undef XY_TO_Y
#undef PM_ITERS
#undef PATCH_W
#undef RS_MAX

int main(int argc, char **argv)
{
#define NEWIMG(s) cvLoadImage(s, CV_LOAD_IMAGE_COLOR)
    // 19 - 22 are 'similar'
    IplImage *a = NEWIMG("frames/bbb22.png");
    IplImage *b = NEWIMG("frames/bbb19.png");
#undef NEWIMG
    CvSize size = cvGetSize(a);
    IplImage *ann = cvCreateImage(size, IPL_DEPTH_32S, 1);
    IplImage *annd = cvCreateImage(size, IPL_DEPTH_32S, 1);
    IplImage *recon = cvCreateImage(size, a->depth, a->nChannels);

    patchmatch(a, b, ann, annd);
    xy2img(ann, b, recon);

    cvShowImage("a", a);
    cvShowImage("b", b);
    cvShowImage("recon", recon);
    cvMoveWindow("a", 0, 0);
    cvMoveWindow("b", 480, 0);
    cvMoveWindow("recon", 0, 400);
    cvWaitKey(0);
    cvReleaseImage(&a);
    cvReleaseImage(&b);
    cvReleaseImage(&ann);
    return 0;
}
