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
#define MAGENTA(r, g, b) (r == 255 && g == 0 && b == 255)
#define BLACK(r, g, b) (!r && !g && !b)

static int max(int a, int b) { return a >= b ? a : b; }
static int min(int a, int b) { return a <= b ? a : b; }

static inline float maskd(IplImage *mask, int x, int y)
{
    return *((float*)(mask->imageData + y * mask->widthStep) + x);
}

static inline int masked(IplImage *mask, int x, int y)
{
    if (!mask) return 0;
    return maskd(mask, x, y) > 0.0f;
}

static int is_magenta(IplImage *img, int x, int y)
{
    uint8_t *data = (uint8_t*)img->imageData;
    data += y * img->widthStep + (x*img->nChannels);
    uint8_t r = data[0], g = data[1], b = data[2];
    if (MAGENTA(r, g, b)) return 1;
    return 0;
}
static int dist(IplImage *a, IplImage *b, int ax, int ay, int bx, int by, int cutoff, IplImage *mask)
{
    int ans = 0, dx, dy, w = PATCH_W, h = PATCH_W;
    uint8_t *adata = (uint8_t*)a->imageData;
    uint8_t *bdata = (uint8_t*)b->imageData;
    int astride = a->widthStep, bstride = b->widthStep;
    float weight = 0.0f;
    if (ax+w >= a->width) w -= ax+w - a->width;
    if (bx+w >= b->width) w -= bx+w - b->width;
    if (ay+h >= a->height) h -= ay+h - a->height;
    if (by+h >= b->height) h -= by+h - b->height;
    if (cutoff <= 0) cutoff = INT_MAX;
    for (dy = 0; dy < h; dy++) {
        for (dx = 0; dx < w; dx++) {
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
            float w1 = 1.0 - maskd(mask, dx+bx, dy+by);
            ans +=  w1*(dr*dr + dg*dg + db*db);
            weight += w1;
        }
        if (weight <= 0.0 || ans/weight > cutoff) return cutoff;
    }
    return ans/weight;
}

static void improve_guess(IplImage *a, IplImage *b, int ax, int ay, int *xbest, int *ybest, int *dbest, int bx, int by, IplImage *mask)
{
    if (masked(mask, bx, by)) return;
    int db = *dbest;
    int d = dist(a, b, ax, ay, bx, by, db, mask);
    if (d < db) {
        *dbest = d;
        *xbest = bx;
        *ybest = by;
    }
}

static void patchmatch(IplImage *a, IplImage *b, IplImage *ann, IplImage *annd, IplImage *mask)
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
            uint16_t bx = ax;
            uint16_t by = ay;
            while (masked(mask, bx, by)) {
                bx = rand() % bew;
                by = rand() % beh;
            }
            *(data + xy) = XY_TO_INT(bx, by);
            *(ddata + xy) = dist(a, b, ax, ay, bx, by, 0, mask);
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
                if (!masked(mask, ax, ay)) continue;
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
                        improve_guess(a, b, ax, ay, &xbest, &ybest, &dbest, xp, yp, mask);
                    }
                }

                if ((unsigned)(ay - ychange) < (unsigned)aeh) {
                    int vp = *(data + ay * stride + (ax - xchange));
                    int xp = XY_TO_X(vp), yp = XY_TO_Y(vp) + ychange;
                    if ((unsigned)yp < (unsigned)beh) {
                        improve_guess(a, b, ax, ay, &xbest, &ybest, &dbest, xp, yp, mask);
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
                    improve_guess(a, b, ax, ay, &xbest, &ybest, &dbest, xp, yp, mask);
                }

                *(data +  xy) = XY_TO_INT(xbest, ybest);
                *(ddata + xy) = dbest;
            }
        }
    }
}

static void update_accum(IplImage *a, IplImage *acc,
    int ax, int ay, int bx, int by, float weight)
{
    int i;
    uint8_t *adata = (uint8_t*)a->imageData;
    float *bdata = (float*)(acc->imageData + by*acc->widthStep);
    adata += ay*a->widthStep + ax*a->nChannels;
    bdata += bx*acc->nChannels;
    for (i = 0; i < a->nChannels; i++) bdata[i] += weight * adata[i];
    bdata[a->nChannels] += weight;
}

static void accum(IplImage *a, IplImage *acc, int ax, int ay, int accx, int accy, int dist, IplImage *mask)
{
    float weight = 1.0f/(dist+1);
    int dx, dy, dir = 1, i, w = PATCH_W, h = PATCH_W;
    int stride = a->widthStep, astride = acc->widthStep;
    uint8_t *data = (uint8_t*)a->imageData + ay*stride + ax*a->nChannels;
    uint8_t *adata = (uint8_t*)acc->imageData + accy*astride + accx*acc->nChannels*sizeof(float);
    if (!masked(mask, accx, accy)) return;
    if (!masked(mask, accx, accy)) {
        update_accum(a, acc, ax, ay, accx, accy, weight);
        return;
    }
    if (ax+w >= a->width) w -= ax+w - a->width;
    if (accx+w >= acc->width) w -= accx+w - acc->width;
    if (ay+h >= a->height) h -= ay+h - a->height;
    if (accy+h >= acc->height) h -= accy+h - acc->height;
    for (dy = 0; dy < h; dy++) {
        uint8_t *line = data;
        float *aline = (float*)adata;
        for (dx = 0; dx < w; dx++) {
            //if (!masked(mask, rx, ry)) continue;
            for (i = 0; i < a->nChannels; i++) aline[i] += weight*line[i];
            aline[a->nChannels] += weight;
            line += dir*a->nChannels;
            aline += dir*acc->nChannels;
        }
        data += a->widthStep;
        adata += acc->widthStep;
    }
}

static void normalize(IplImage *a, IplImage *acc, IplImage *mask)
{
    int w = a->width, h  = a->height, c = a->nChannels, i, j, k;
    for (i = 0; i < h; i++) {
        uint8_t* adata = (uint8_t*)a->imageData + i*a->widthStep;
        float* bdata = (float*)(acc->imageData + i*acc->widthStep);
        for (j = 0; j < w; j++) {
            float mw = maskd(mask, j, i); // mask weight
            float tw = bdata[c]; // total weight
            float weight = 1.0f/tw;
            for (k = 0; k < c; k++) {
                adata[k] *= 1 - mw;
                adata[k] += mw*weight*bdata[k];
            }
            adata += c;
            bdata += acc->nChannels;
        }
    }
}

void xy2img(IplImage *xy, IplImage *img, IplImage *recon);
static IplImage* fill(IplImage *a, IplImage *target, IplImage *mask)
{

    int i, ax, ay, w = a->width, h = a->height;
    int aew = w - PATCH_W, aeh = h - PATCH_W;
    int tw = target->width, th = target->height;

    CvSize size = cvGetSize(a);
    IplImage *ann = cvCreateImage(size, IPL_DEPTH_32S, 1);
    IplImage *annd = cvCreateImage(size, IPL_DEPTH_32S, 1);
    IplImage *d = cvCreateImage(size, a->depth, a->nChannels);
    IplImage *acc = cvCreateImage(size, IPL_DEPTH_32F, a->nChannels+1);
    IplImage *imask = cvCreateImage(size, mask->depth, mask->nChannels);
    int32_t *data = (int32_t*)ann->imageData;
    int32_t *ddata = (int32_t*)annd->imageData;
    int stride = ann->widthStep/sizeof(int32_t);
    memset(acc->imageData, 0, acc->imageSize);
    IplImage *mask8u = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *imask8u = cvCreateImage(size, IPL_DEPTH_8U, 1);
    cvConvertScale(mask, imask, -1, 1);
    cvConvertScale(mask, mask8u, 256, 0);
    cvConvertScale(mask8u, imask8u, -2, 256);

    printf("recursing %dx%d\n", w, h);
    if (w > 32 && h > 32 && tw > 32 && th > 32) {
        CvSize ssmall = {w/2, h/2}, tsmall = {tw/2, th/2};
        IplImage *ssrc, *stgt, *smsk, *newtgt;

        ssrc = cvCreateImage(ssmall, a->depth, a->nChannels);
        stgt = cvCreateImage(tsmall, target->depth, target->nChannels);
        smsk = cvCreateImage(ssmall, mask->depth, mask->nChannels);

        cvResize(a, ssrc, CV_INTER_LINEAR);
        cvResize(target, stgt, CV_INTER_LINEAR);
        cvResize(mask, smsk, CV_INTER_LINEAR);
        newtgt = fill(ssrc, stgt, smsk);
        cvResize(newtgt, target, CV_INTER_LINEAR);

        cvReleaseImage(&ssrc);
        cvReleaseImage(&stgt);
        cvReleaseImage(&smsk);
    }

    for (i= 0; i< PM_ITERS; i++) { 
        // completeness term
        patchmatch(a, target, ann, annd, imask);
        xy2img(ann, target, d);
        cvShowImage("completeness", d);
        for (ay = 0; ay < aeh; ay++) {
            for (ax = 0; ax < aew; ax++) {
                int xy = ay * stride + ax;
                int v = *(data + xy);
                int xbest = XY_TO_X(v), ybest = XY_TO_Y(v);
                int dbest = *(ddata + xy);
                accum(target, acc, xbest, ybest, ax, ay, dbest, imask);
            }
        }

        // coherence term
        patchmatch(target, a, ann, annd, mask);
        xy2img(ann, a, d);
        cvShowImage("coherence", d);
        for (ay = 0; ay < aeh; ay++) {
            for (ax = 0; ax < aew; ax++) {
                int xy = ay * stride + ax;
                int v = *(data + xy);
                int xbest = XY_TO_X(v), ybest = XY_TO_Y(v);
                int dbest = *(ddata + xy);
                accum(a, acc, xbest, ybest, ax, ay, dbest, mask);
            }
        }

        normalize(target, acc, mask);
        cvCopy(a, target, imask8u);
    }

    printf("finished %dx%d\n", w, h);
    //cvWaitKey(0);
    cvReleaseImage(&d);
    cvReleaseImage(&mask8u);
    cvReleaseImage(&imask);
    cvReleaseImage(&ann);
    cvReleaseImage(&annd);
    cvReleaseImage(&acc);
    return target;
}

void xy2img(IplImage *xy, IplImage *img, IplImage *recon)
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

static IplImage* make_mask(char *fname)
{
    IplImage *img = cvLoadImage(fname, CV_LOAD_IMAGE_COLOR);
    IplImage *mask = cvCreateImage(cvGetSize(img), IPL_DEPTH_32F, 1);
    int w = img->width, h = img->height, j , i;
    float *data;
    for (i = 0; i < h; i++) {
        data = (float*)(mask->imageData + i*mask->widthStep);
        for (j = 0; j < w; j++) {
            *data++ = 1.0*is_magenta(img, j, i);
        }
    }
    cvReleaseImage(&img);
    return mask;
}

#undef XY_TO_INT
#undef XY_TO_X
#undef XY_TO_Y
#undef PM_ITERS
#undef PATCH_W
#undef RS_MAX
#undef MAGENTA
#undef BLACK

int main(int argc, char **argv)
{
#define NEWIMG(s) cvLoadImage(s, CV_LOAD_IMAGE_COLOR)
    // 19 - 22 are 'similar'
    IplImage *mask = make_mask("bunny.png");
    IplImage *a = NEWIMG("frames/bbb20.png");
    IplImage *b = cvCreateImage(cvGetSize(a), a->depth, a->nChannels);
    cvCopy(a, b, NULL);
#undef NEWIMG
    IplImage *out = fill(a, b, mask);

    cvShowImage("a", a);
    cvShowImage("mask", mask);
    cvShowImage("out", out);
    cvMoveWindow("a", 0, 0);
    cvMoveWindow("mask", 480, 0);
    cvMoveWindow("out", 0, 400);
    cvWaitKey(0);
    cvReleaseImage(&a);
    cvReleaseImage(&b);
    cvReleaseImage(&mask);
    return 0;
}
