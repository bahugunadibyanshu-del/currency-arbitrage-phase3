/*
  Part 2 (C / DSA): arbitrage route analyser
  - Reads the currency graph written by the C++ program (edges.csv).
  - Bellman-Ford on cost = -ln(effective rate) tells us IF a profitable cycle exists.
  - A search over all 3- and 4-step round trips then finds the BEST route.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#define MAX_CURRENCIES 20
#define MAX_EDGES 400
#define MAX_CYCLES 20000
#define MAX_STEPS 4
#define CODE_LEN 4
#define LINE_LEN 512
#define RISK_WEIGHT_PERCENT 0.04

typedef struct {
    char code[CODE_LEN];
} Currency;

typedef struct {
    int from, to;
    double rate, fee, spread, effectiveRate, cost;
    double volatilityScore, liquidityScore;
} Edge;

typedef struct {
    int path[MAX_STEPS];   /* currencies in order; we return to path[0] at the end */
    int length;
    double multiplier;
    double riskAdjustedScore;
    double averageVolatility;
    double averageLiquidity;
    double riskPenaltyPercent;
} Route;

static Currency currencies[MAX_CURRENCIES];
static Edge edges[MAX_EDGES];
static double effective[MAX_CURRENCIES][MAX_CURRENCIES];
static int currencyCount = 0, edgeCount = 0;

static Route routes[MAX_CYCLES];
static int routeCount = 0;

/* ---------- loading the graph ---------- */

static int currencyIndex(const char *code) {
    for (int i = 0; i < currencyCount; i++)
        if (strcmp(currencies[i].code, code) == 0) return i;
    if (currencyCount >= MAX_CURRENCIES) return -1;
    strncpy(currencies[currencyCount].code, code, CODE_LEN - 1);
    currencies[currencyCount].code[CODE_LEN - 1] = '\0';
    return currencyCount++;
}

static int loadGraph(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Cannot open %s. Run ./market_builder first.\n", filename);
        return 0;
    }

    char line[LINE_LEN];
    if (!fgets(line, sizeof(line), file)) { fclose(file); return 0; }  /* skip header */

    while (fgets(line, sizeof(line), file)) {
        char from[CODE_LEN], to[CODE_LEN];
        double rate, fee, spread, eff, cost, volatility, liquidity;
        if (sscanf(line, "%3[^,],%3[^,],%lf,%lf,%lf,%lf,%lf,%lf,%lf",
                   from, to, &rate, &fee, &spread, &eff, &cost,
                   &volatility, &liquidity) != 9)
            continue;
        if (edgeCount >= MAX_EDGES) {
            fprintf(stderr, "Too many edges for this program.\n");
            fclose(file);
            return 0;
        }
        int u = currencyIndex(from), v = currencyIndex(to);
        if (u < 0 || v < 0) { fclose(file); return 0; }
        edges[edgeCount++] = (Edge){u, v, rate, fee, spread, eff, cost, volatility, liquidity};
        effective[u][v] = eff;
    }
    fclose(file);
    return currencyCount > 1 && edgeCount > 0;
}

static int readStartAmount(double *amount) {
    char input[128];
    char *end;

    printf("\nEnter starting amount for each route (same numeric amount in that route's starting currency): ");
    if (!fgets(input, sizeof(input), stdin)) return 0;

    errno = 0;
    double value = strtod(input, &end);
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
    if (end == input || *end != '\0' || errno == ERANGE || !isfinite(value) || value <= 0.0) {
        fprintf(stderr, "Please enter a finite amount greater than zero.\n");
        return 0;
    }
    *amount = value;
    return 1;
}

static const Edge *findEdge(int from, int to) {
    for (int i = 0; i < edgeCount; i++)
        if (edges[i].from == from && edges[i].to == to) return &edges[i];
    return NULL;
}

/* ---------- Bellman-Ford: does ANY profitable cycle exist? ---------- */

static int hasNegativeCycle(void) {
    double distance[MAX_CURRENCIES];
    for (int i = 0; i < currencyCount; i++) distance[i] = 0.0;  /* start everywhere at once */

    for (int pass = 0; pass < currencyCount; pass++) {
        int changed = 0;
        for (int i = 0; i < edgeCount; i++) {
            Edge *e = &edges[i];
            if (distance[e->from] + e->cost < distance[e->to] - 1e-12) {
                distance[e->to] = distance[e->from] + e->cost;
                changed = 1;
            }
        }
        if (!changed) return 0;
    }
    return 1;
}

/* ---------- finding the best route ---------- */

static void saveRoute(const int path[], int length) {
    if (routeCount >= MAX_CYCLES) return;
    double m = 1.0, volatilityTotal = 0.0, liquidityTotal = 0.0;
    for (int i = 0; i < length; i++) {
        int from = path[i], to = path[(i + 1) % length];
        const Edge *edge = NULL;
        for (int e = 0; e < edgeCount; e++) {
            if (edges[e].from == from && edges[e].to == to) {
                edge = &edges[e];
                break;
            }
        }
        if (!edge) return;
        m *= effective[from][to];
        volatilityTotal += edge->volatilityScore;
        liquidityTotal += edge->liquidityScore;
    }
    double averageVolatility = volatilityTotal / length;
    double averageLiquidity = liquidityTotal / length;
    double penaltyPercent = RISK_WEIGHT_PERCENT * (averageVolatility + (100.0 - averageLiquidity));
    memcpy(routes[routeCount].path, path, length * sizeof(int));
    routes[routeCount].length = length;
    routes[routeCount].multiplier = m;
    routes[routeCount].averageVolatility = averageVolatility;
    routes[routeCount].averageLiquidity = averageLiquidity;
    routes[routeCount].riskPenaltyPercent = penaltyPercent;
    routes[routeCount].riskAdjustedScore = m * (1.0 - penaltyPercent / 100.0);
    routeCount++;
}

/* Depth-first search. The start currency is always the smallest index in the
   cycle, so each cycle is stored once instead of once per rotation. */
static void explore(int path[], int depth, int visited[]) {
    int current = path[depth - 1];
    for (int next = 0; next < currencyCount; next++) {
        if (effective[current][next] <= 0.0) continue;
        if (next == path[0] && depth >= 3) saveRoute(path, depth);
        if (next > path[0] && !visited[next] && depth < MAX_STEPS) {
            visited[next] = 1;
            path[depth] = next;
            explore(path, depth + 1, visited);
            visited[next] = 0;
        }
    }
}

static int compareRoutes(const void *a, const void *b) {
    double x = ((const Route *)a)->riskAdjustedScore, y = ((const Route *)b)->riskAdjustedScore;
    return (y > x) - (y < x);   /* highest risk-adjusted score first */
}

static void printRoute(const Route *r) {
    for (int i = 0; i < r->length; i++) printf("%s -> ", currencies[r->path[i]].code);
    printf("%s\n", currencies[r->path[0]].code);
}

static void printWalkthrough(const Route *r, double startAmount) {
    double amount = startAmount;
    printf("  Start with %.2f %s\n", amount, currencies[r->path[0]].code);
    for (int i = 0; i < r->length; i++) {
        int from = r->path[i], to = r->path[(i + 1) % r->length];
        const Edge *edge = findEdge(from, to);
        if (!edge) return;

        double grossAmount = amount * edge->rate;
        double feeCost = grossAmount * edge->fee / 100.0;
        double afterFee = grossAmount - feeCost;
        double spreadCost = afterFee * edge->spread / 100.0;
        amount = afterFee - spreadCost;
        printf("  %s -> %s at %.6f: gross %.4f, fee %.4f, spread %.4f, net %.4f %s\n",
               currencies[from].code, currencies[to].code, edge->rate,
               grossAmount, feeCost, spreadCost, amount, currencies[to].code);
    }
    printf("  Net change: %+.4f %s (%+.4f%%)\n", amount - startAmount,
           currencies[r->path[0]].code, (amount / startAmount - 1.0) * 100.0);
    printf("  Average volatility risk: %.1f/100\n", r->averageVolatility);
    printf("  Average liquidity: %.1f/100\n", r->averageLiquidity);
    printf("  Risk penalty: %.2f%%\n", r->riskPenaltyPercent);
    printf("  Risk-adjusted score: %+.4f%%\n", (r->riskAdjustedScore - 1.0) * 100.0);
}

int main(void) {
    printf("CURRENCY ARBITRAGE ROUTE ANALYSER (C / DSA)\n");
    if (!loadGraph("edges.csv")) {
        fprintf(stderr, "\nNo graph data loaded. Run ./market_builder first.\n");
        return 1;
    }
    printf("Loaded %d currencies and %d directed routes from edges.csv.\n",
           currencyCount, edgeCount);

    double startAmount;
    if (!readStartAmount(&startAmount)) return 1;
    printf("Using %.2f units as the starting amount for each route.\n", startAmount);

    /* Step 1: Bellman-Ford */
    puts("\nSTEP 1 - BELLMAN-FORD (is there any profitable cycle?)");
    int found = hasNegativeCycle();
    puts(found ? "  Yes: a negative cycle exists, so at least one route beats the costs."
               : "  No: after fees and spread, every cycle loses money.");

    /* Step 2: search every 3- and 4-step round trip */
    int path[MAX_STEPS], visited[MAX_CURRENCIES];
    for (int s = 0; s < currencyCount; s++) {
        memset(visited, 0, sizeof(visited));
        visited[s] = 1;
        path[0] = s;
        explore(path, 1, visited);
    }
    qsort(routes, routeCount, sizeof(Route), compareRoutes);

    int profitable = 0;
    for (int i = 0; i < routeCount; i++)
        if (routes[i].multiplier > 1.0) profitable++;

    printf("\nSTEP 2 - ROUTE SEARCH (%d round trips of 3-%d steps checked)\n", routeCount, MAX_STEPS);
    printf("  Profitable routes found: %d\n", profitable);

    if (routeCount == 0) { puts("  No routes to rank."); return 0; }

    if (profitable > 0) {
        int bestProfitable = 0;
        while (bestProfitable < routeCount && routes[bestProfitable].multiplier <= 1.0)
            bestProfitable++;
        puts("\nBEST PROFITABLE ROUTE");
        printf("  ");
        printRoute(&routes[bestProfitable]);
        printWalkthrough(&routes[bestProfitable], startAmount);

        puts("\nTOP PROFITABLE ROUTES (RISK-ADJUSTED SCORE)");
        int shown = 0;
        for (int i = 0; i < routeCount && shown < 5; i++) {
            if (routes[i].multiplier <= 1.0) continue;
            printf("  #%d  net %+.4f%%, adjusted %+.4f%%, volatility %.1f, liquidity %.1f   ",
                   shown + 1, (routes[i].multiplier - 1.0) * 100.0,
                   (routes[i].riskAdjustedScore - 1.0) * 100.0,
                   routes[i].averageVolatility, routes[i].averageLiquidity);
            printRoute(&routes[i]);
            shown++;
        }
    } else {
        puts("\nNO PROFITABLE ROUTE EXISTS in this data.");
        puts("Highest risk-adjusted route, shown for comparison:");
        printf("  ");
        printRoute(&routes[0]);
        printWalkthrough(&routes[0], startAmount);
        puts("\n  This is expected: all rates come from one consistent snapshot, so the");
        puts("  only thing a round trip can do is lose the fees and spread.");
    }

    puts("\nMethod: Bellman-Ford with cost = -ln(effective rate), then exhaustive search of short cycles.");
    puts("Route ranking uses net return with simulated volatility and low-liquidity penalties.");
    puts("Volatility and liquidity scores are classroom assumptions, not live market measurements.");
    puts("Reminder: reference rates are not executable quotes. This is not trading advice.");
    return 0;
}
