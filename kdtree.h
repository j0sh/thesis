#ifndef JOSH_KDTREE_H_
#define JOSH_KDTREE_H_

typedef struct kd_node {
    int val;
    int nb;
    int axis;
    struct kd_node *left;
    struct kd_node *right;
    int **value;
} kd_node;

typedef struct kd_tree {
    int k, nb_nodes;
    int *order;
    int **points;
    int *start;
    int *end;
    kd_node *root;
    kd_node **map;
    kd_node *nodes;
} kd_tree;

void kdt_new(kd_tree* t, int *points, int nb_points, int k);
void kdt_new_overlap(kd_tree *t, int *points, int nb_points, int k,
    float overlap, int kernsz, int stride);
kd_node* kdt_query(kd_tree *t, int *query);
void kdt_free(kd_tree* t);

#endif  /* JOSH_KDTREE_H_ */
