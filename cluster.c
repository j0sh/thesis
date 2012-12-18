#include <stdio.h>
#include <string.h>
#include <math.h>

#define SZ 360

typedef struct {
    int members[SZ];
    int sum;
    int sqsum;
    int count;

    int min;
    int max;
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

static double cdf(int x, double mean, double var)
{
    return 0.5*(1 + erf((x - mean)/sqrt(2*var)));
}

static void calc_stats(cluster *c)
{
    double d, x, y;
    d = 1.0/c->count;
    c->mean = c->sum * d;
    x = c->sqsum * d;
    y = c->mean*c->mean;
    c->var = x - y;
    c->var += !c->var*c->mean/2;    // special case for zero variance
    c->var += c->var < 1.0;        // special case for 0, 1 variance
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
    if (s > c->max) c->max = s;
    if (s < c->min) c->min = s;
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
    if (s == c->min) {
        int m = 0xFFFFFFFF;
        for (i = 0; i < SZ; i++) {
            if (-1 == c->members[i]) continue;
            if (c->members[i] < m) m = c->members[i];
        }
        c->min = m;
    }
    if (s == c->max) {
        int m = -1;
        for (i = 0; i < SZ; i++) {
            if (-1 == c->members[i]) continue;
            if (c->members[i] > m) m = c->members[i];
        }
        c->max = m;
    }
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

static int within(cluster *a, cluster *b)
{
    double m = a->mean, v = a->var;
    double i = b->mean + 2*sqrt(b->var), j = b->mean - 2*sqrt(b->var);
    return cdf(i, m, v) - cdf(j, m, v) > 0.025;
}

void do_cluster(int samples[30])
{
    cluster clusters[SZ] = {0};
    int i, j, modified = 1;

    // initialize stuff
    for (i = 0; i < SZ; i++) {
        for (j = 0; j < SZ; j++) {
            clusters[i].members[j] = -1;
            clusters[i].min = 0X7FFFFFFF;
            clusters[i].max = -1;
        }
        add_to_cluster(&clusters[i], samples[i]);
    }

    while (modified) {
        modified = 0;
        for (i = 0; i < SZ; i++) {
            cluster *a = &clusters[i];
            if (!a->count) continue;
            for (j = i+1; j < SZ; j++) {
                cluster *b = &clusters[j];
                if (b->count && within(a, b)) {
                    merge_clusters(a, b);
                    modified = 1;
                }
            }
        }
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
