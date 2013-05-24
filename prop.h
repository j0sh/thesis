#ifndef JOSH_PROP_H_
#define JOSH_PROP_H_

IplImage *prop_match(IplImage *src, IplImage *dst);

// utility stuff
struct kd_tree;
void prop_coeffs(IplImage *sr, int dim, int **data);
IplImage *prop_match_complete(struct kd_tree *kdt, int *data,
    IplImage *src, CvSize dst_size);
unsigned prop_enrich(struct kd_tree *dt, int *coeffs, int x, int y,
    int *prev);
#endif /* JOSH_PROP_H_ */
