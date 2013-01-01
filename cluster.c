#include <stdio.h>
#include <string.h>
#include <math.h>

#define SZ 360

typedef struct {
    int members[SZ];
    int sum;
    int sqsum;
    int count;

    double mean;
    double var;
} cluster;

static void print_cluster(cluster *c)
{
    int i;
    for (i = 0; i < SZ; i++) {
        if (-1 != c->members[i]) printf("%d ", c->members[i]);
    }
    printf("mu %.2f std %.2f\n", c->mean, sqrt(c->var));
}

static void calc_stats(cluster *c)
{
    double d, x, y;
    d = 1.0/c->count;
    c->mean = c->sum * d;
    x = c->sqsum * d;
    y = c->mean*c->mean;
    c->var = x - y;
}

static void add_to_cluster(cluster *c, int s)
{
    int i;
    double x, y;
    for (i = 0; i < SZ; i++) {
        if (c->members[i] == -1) {
            c->members[i] = s;
            break;
        }
    }
    if (i == SZ) return; // not found

    c->sqsum += s*s;
    c->sum += s;
    c->count += 1;
    calc_stats(c);
}

static void remove_from_cluster(cluster *c, int s)
{
    double x, y;
    int i;
    for (i = 0; i < SZ; i++) {
        if (c->members[i] == s) {
            c->members[i] = -1;
            break;
        }
    }
    if (i == SZ) return; // not found

    c->sqsum -= s*s;
    c->sum -= s;
    c->count -= 1;
    calc_stats(c);
}

static void merge_clusters(cluster *a, cluster *b)
{
    int i, m;
    for (i = 0; i < SZ; i++) {
        int m = b->members[i];
        if (-1 == m) continue;
        add_to_cluster(a, m);
        remove_from_cluster(b, m);
    }
}

static double max(double x, double y)
{
    return x < y ? y : x;
}

static inline double adj_std(double mean, double var)
{
    // return adjusted stddev in case of low variance, big mean
    double l = ceil(log(mean));
    double s = sqrt(var);
    return s + (s < l)*l;
}

static double cdf(int x, double mean, double std)
{
    return 0.5*(1 + erf((x - mean)/sqrt(2*std*std)));
}

static double inter_(cluster *a, cluster *b)
{
    double m = a->mean, v = a->var;
    double s1 = adj_std(a->mean, a->var);
    double s2 = adj_std(b->mean, b->var);
    double i = b->mean + 2*s2, j = b->mean - 2*s2;
    return cdf(i, m, s1) - cdf(j, m, s1);
}

static double inter(cluster *a, cluster *b)
{
    return max(inter_(a, b), inter_(b, a));
}

void do_cluster(int samples[30])
{
    cluster clusters[SZ] = {0};
    int i, j, modified = 1;

    // initialize stuff
    for (i = 0; i < SZ; i++) {
        for (j = 0; j < SZ; j++) clusters[i].members[j] = -1;
        add_to_cluster(&clusters[i], samples[i]);
    }

    while (modified) {
        modified = 0;
        cluster *c1 = NULL, *c2 = NULL;
        double max_inter = 0.025;
        for (i = 0; i < SZ; i++) {
            cluster *a = &clusters[i];
            if (!a->count) continue;
            for (j = i+1; j < SZ; j++) {
                cluster *b = &clusters[j];
                if (!b->count) continue;
                double dist = inter(a, b);
                if (dist > max_inter) {
                    max_inter = dist;
                    c1 = a;
                    c2 = b;
                    modified = 1;
                }
            }
        }
        if (modified) merge_clusters(c1, c2);
    }

    // print clusters
    for (i = j = 0; i < SZ; i++) {
        if (!clusters[i].count) continue;
        printf("cluster %d : ", j++);
        print_cluster(&clusters[i]);
    }
}

int main(int argc, char **argv)
{
    int s[SZ], i;
    srand(time(NULL));
    for (i = 0; i < SZ; i++) {
        int off = (rand() % 10) * 100;
        s[i] = (rand() % 50) + off;
    }
    do_cluster(s);
    return 0;
}
