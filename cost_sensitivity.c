/*
  Cost Sensitivity & Break-Even Analyser (C / DSA)

  Question answered: "How cheap would trading have to be before arbitrage works?"

  - Reads the raw cross-rates from edges.csv (same file arbitrage.c uses).
  - Sweeps the total cost per conversion (fee + spread) from 0% upward.
  - At each cost level it runs Bellman-Ford (cost = -ln(effective rate)) and a
    search over all 3- and 4-step round trips, then records how many routes are
    profitable and the best net return.
  - For every route it also computes the exact break-even cost per conversion:
        gross product P of raw rates, L legs:  P * (1 - c/100)^L = 1
        =>  c* = (1 - P^(-1/L)) * 100
    The biggest c* over all routes is the "cost ceiling": above it, NO route
    in this data can make money.
  - Writes the sweep to sensitivity.csv for charts.

  Usage:  ./cost_sensitivity [edges.csv] [--test]
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_CURRENCIES 20
#define MAX_EDGES 400
#define MAX_STEPS 4
#define CODE_LEN 4
#define LINE_LEN 512
#define SWEEP_STEP 0.05
#define SWEEP_MAX 1.00
#define CURRENT_COST_PERCENT 0.20   /* 0.10% fee + 0.10% spread, as in market_builder.cpp */
#define MAX_ROUTES 20000
#define EPS 1e-9                    /* ignore floating-point noise around zero */

typedef struct { int from, to; double rate; } Edge;

typedef struct {
    int path[MAX_STEPS];
    int length;
    double grossProduct;      /* product of raw rates around the cycle */
    double breakEvenPercent;  /* per-conversion cost at which profit hits zero */
} Route;

static char codes[MAX_CURRENCIES][CODE_LEN];
static Edge edges[MAX_EDGES];
static double raw[MAX_CURRENCIES][MAX_CURRENCIES];
static int nCur = 0, nEdge = 0;

static Route routes[MAX_ROUTES];
static int nRoutes = 0;

/* ---------- graph loading ---------- */

static int currencyIndex(const char *code) {
    for (int i = 0; i < nCur; i++)
        if (strcmp(codes[i], code) == 0) return i;
    if (nCur >= MAX_CURRENCIES) return -1;
    strncpy(codes[nCur], code, CODE_LEN - 1);
    codes[nCur][CODE_LEN - 1] = '\0';
    return nCur++;
}

static void addEdge(int u, int v, double rate) {
    edges[nEdge++] = (Edge){u, v, rate};
    raw[u][v] = rate;
}

static int loadGraph(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) { fprintf(stderr, "Cannot open %s. Run ./market_builder first.\n", filename); return 0; }
    char line[LINE_LEN];
    if (!fgets(line, sizeof line, f)) { fclose(f); return 0; }
    while (fgets(line, sizeof line, f)) {
        char a[CODE_LEN], b[CODE_LEN];
        double rate, d1, d2, d3, d4, d5, d6;
        if (sscanf(line, "%3[^,],%3[^,],%lf,%lf,%lf,%lf,%lf,%lf,%lf",
                   a, b, &rate, &d1, &d2, &d3, &d4, &d5, &d6) != 9) continue;
        if (nEdge >= MAX_EDGES || rate <= 0.0) continue;
        int u = currencyIndex(a), v = currencyIndex(b);
        if (u < 0 || v < 0) continue;
        addEdge(u, v, rate);
    }
    fclose(f);
    return nCur > 1 && nEdge > 0;
}

/* ---------- Bellman-Ford at a given per-conversion cost ---------- */

static int hasProfitableCycle(double costPercent) {
    double keep = 1.0 - costPercent / 100.0, dist[MAX_CURRENCIES];
    for (int i = 0; i < nCur; i++) dist[i] = 0.0;
    for (int pass = 0; pass < nCur; pass++) {
        int changed = 0;
        for (int i = 0; i < nEdge; i++) {
            double w = -log(edges[i].rate * keep);
            if (dist[edges[i].from] + w < dist[edges[i].to] - EPS) {
                dist[edges[i].to] = dist[edges[i].from] + w;
                changed = 1;
            }
        }
        if (!changed) return 0;
    }
    return 1;
}

/* ---------- enumerate every simple 3- and 4-step cycle once ---------- */

static void saveRoute(const int path[], int len) {
    if (nRoutes >= MAX_ROUTES) return;
    double product = 1.0;
    for (int i = 0; i < len; i++) product *= raw[path[i]][path[(i + 1) % len]];
    Route *r = &routes[nRoutes++];
    memcpy(r->path, path, len * sizeof(int));
    r->length = len;
    r->grossProduct = product;
    /* P*(1-c)^L = 1  ->  c = 1 - P^(-1/L); negative means profitable even with negative costs */
    r->breakEvenPercent = (1.0 - pow(product, -1.0 / len)) * 100.0;
}

static void explore(int path[], int depth, int visited[]) {
    int cur = path[depth - 1];
    for (int next = 0; next < nCur; next++) {
        if (raw[cur][next] <= 0.0) continue;
        if (next == path[0] && depth >= 3) saveRoute(path, depth);
        if (next > path[0] && !visited[next] && depth < MAX_STEPS) {
            visited[next] = 1;
            path[depth] = next;
            explore(path, depth + 1, visited);
            visited[next] = 0;
        }
    }
}

static void enumerateRoutes(void) {
    int path[MAX_STEPS], visited[MAX_CURRENCIES];
    nRoutes = 0;
    for (int s = 0; s < nCur; s++) {
        memset(visited, 0, sizeof visited);
        visited[s] = 1;
        path[0] = s;
        explore(path, 1, visited);
    }
}

static int byBreakEvenDesc(const void *a, const void *b) {
    double x = ((const Route *)a)->breakEvenPercent, y = ((const Route *)b)->breakEvenPercent;
    return (y > x) - (y < x);
}

/* ---------- helpers for the sweep ---------- */

static double netReturnPercent(const Route *r, double costPercent) {
    return (r->grossProduct * pow(1.0 - costPercent / 100.0, r->length) - 1.0) * 100.0;
}

static void routeToString(const Route *r, char *out, size_t n) {
    out[0] = '\0';
    for (int i = 0; i < r->length; i++) {
        strncat(out, codes[r->path[i]], n - strlen(out) - 1);
        strncat(out, " -> ", n - strlen(out) - 1);
    }
    strncat(out, codes[r->path[0]], n - strlen(out) - 1);
}

/* ---------- self test on a tiny hand-made graph ---------- */

static int selfTest(void) {
    nCur = nEdge = 0;
    memset(raw, 0, sizeof raw);
    strcpy(codes[0], "AAA"); strcpy(codes[1], "BBB"); strcpy(codes[2], "CCC");
    nCur = 3;
    /* AAA->BBB->CCC->AAA = 2*2*0.3 = 1.2 gross; reverse loses */
    addEdge(0, 1, 2.0); addEdge(1, 2, 2.0); addEdge(2, 0, 0.3);
    addEdge(1, 0, 0.5); addEdge(2, 1, 0.5); addEdge(0, 2, 3.3);
    enumerateRoutes();

    int ok = 1;
    if (nRoutes != 2) { printf("FAIL: expected 2 cycles, got %d\n", nRoutes); ok = 0; }
    qsort(routes, nRoutes, sizeof(Route), byBreakEvenDesc);
    double expected = (1.0 - pow(1.2, -1.0 / 3.0)) * 100.0;      /* ~6.28% */
    if (fabs(routes[0].breakEvenPercent - expected) > 1e-9) { printf("FAIL: break-even value\n"); ok = 0; }
    if (!hasProfitableCycle(0.0))  { printf("FAIL: should be profitable at 0%% cost\n"); ok = 0; }
    if (!hasProfitableCycle(5.0))  { printf("FAIL: should be profitable at 5%% cost\n"); ok = 0; }
    if (hasProfitableCycle(7.0))   { printf("FAIL: should NOT be profitable at 7%% cost\n"); ok = 0; }
    if (fabs(netReturnPercent(&routes[0], routes[0].breakEvenPercent)) > 1e-9) {
        printf("FAIL: net return at break-even should be zero\n"); ok = 0;
    }
    printf(ok ? "Self-test: PASS (5/5 checks)\n" : "Self-test: FAILED\n");
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    const char *file = "edges.csv";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test") == 0) return selfTest();
        file = argv[i];
    }

    printf("COST SENSITIVITY & BREAK-EVEN ANALYSER (C / DSA)\n");
    if (!loadGraph(file)) return 1;
    printf("Loaded %d currencies and %d directed routes from %s.\n", nCur, nEdge, file);

    enumerateRoutes();
    if (nRoutes == 0) { puts("No 3-4 step round trips found."); return 0; }
    qsort(routes, nRoutes, sizeof(Route), byBreakEvenDesc);

    FILE *csv = fopen("sensitivity.csv", "w");
    if (csv) fprintf(csv, "cost_per_conversion_percent,profitable_routes,bellman_ford_cycle,best_net_return_percent\n");

    puts("\nSWEEP - total cost per conversion (fee + spread) vs. opportunities");
    puts("  cost %   profitable routes   Bellman-Ford   best net return %");
    for (double c = 0.0; c <= SWEEP_MAX + 1e-9; c += SWEEP_STEP) {
        int profitable = 0;
        double best = -1e9;
        for (int i = 0; i < nRoutes; i++) {
            double r = netReturnPercent(&routes[i], c);
            if (r > EPS) profitable++;
            if (r > best) best = r;
        }
        int bf = hasProfitableCycle(c);
        printf("  %5.2f   %17d   %-12s   %+10.4f%s\n", c, profitable, bf ? "cycle found" : "none", best,
               fabs(c - CURRENT_COST_PERCENT) < 1e-9 ? "   <-- current assumption" : "");
        if (csv) fprintf(csv, "%.2f,%d,%d,%.6f\n", c, profitable, bf, best);
    }
    if (csv) { fclose(csv); puts("\nSaved sweep to sensitivity.csv"); }

    puts("\nBREAK-EVEN COSTS (highest first)");
    puts("  A route makes money only while cost per conversion stays BELOW its break-even.");
    char text[128];
    int shown = nRoutes < 5 ? nRoutes : 5;
    for (int i = 0; i < shown; i++) {
        routeToString(&routes[i], text, sizeof text);
        printf("  #%d  break-even %+8.4f%%   gross multiplier %.6f   %s\n",
               i + 1, routes[i].breakEvenPercent, routes[i].grossProduct, text);
    }

    double ceiling = routes[0].breakEvenPercent;
    puts("\nVERDICT");
    printf("  Cost ceiling for this data: %.4f%% per conversion.\n", ceiling);
    if (ceiling <= EPS)
        puts("  Even at ZERO cost nothing is profitable: the rates are internally consistent.");
    else if (ceiling > CURRENT_COST_PERCENT + EPS)
        printf("  Current assumption (%.2f%%) is BELOW the ceiling: profitable, with %.4f%% of headroom.\n",
               CURRENT_COST_PERCENT, ceiling - CURRENT_COST_PERCENT);
    else
        printf("  Current assumption (%.2f%%) is ABOVE the ceiling by %.4f%%: costs eat every opportunity.\n",
               CURRENT_COST_PERCENT, CURRENT_COST_PERCENT - ceiling);

    puts("\nNote: Bellman-Ford considers cycles of ANY length, while the route search and break-even\n      figures cover 3-4 step round trips, so \"cycle found\" can persist a little longer.");
    puts("Note: rates are reference rates, not executable quotes. Educational only, not trading advice.");
    return 0;
}
