# Cost Sensitivity & Break-Even Analyser

A new C module for the currency-arbitrage project. The existing programs answer
"is there a profitable route at 0.10% fee + 0.10% spread?". This one answers the
follow-up: **how cheap would trading have to be before arbitrage works?**

## What it does
1. Reads the raw cross-rates from `edges.csv` (the file `market_builder` writes).
2. Sweeps the total cost per conversion from 0.00% to 1.00% in 0.05% steps.
3. At each step runs Bellman-Ford (`cost = -ln(rate * (1 - c))`) and counts profitable 3-4 step round trips.
4. Computes an exact **break-even cost** for every route:

   `P * (1 - c/100)^L = 1  =>  c* = (1 - P^(-1/L)) * 100`

   where `P` is the product of raw rates around the cycle and `L` the number of legs.
5. Reports the **cost ceiling** (largest break-even) and compares it with the current 0.20% assumption.
6. Saves the sweep to `sensitivity.csv` so it can be charted.

## Build and run
```bash
gcc -std=c11 -Wall -Wextra -pedantic cost_sensitivity.c -o cost_sensitivity -lm
./cost_sensitivity --test          # built-in self-test (5 checks)
./cost_sensitivity edges.csv       # analyse your latest market snapshot
```

## How to read the result
- Live data: ceiling is 0.0000%, so even free trading gives nothing. The rates come from one consistent snapshot.
- Demo data (`./market_builder --demo`): ceiling is about 0.86%. The 0.20% assumption has about 0.66% of headroom.

## Notes
- Bellman-Ford checks cycles of any length; the sweep and break-even figures cover 3-4 steps, so "cycle found" can last slightly longer.
- Rates are reference rates, not executable quotes. Educational only, not trading advice.
- Do not commit the compiled executable or `sensitivity.csv`.

## Suggested separate commits
```bash
git add cost_sensitivity.c
git commit -m "feat: add cost sensitivity sweep and break-even calculation"

git add SAMPLE_OUTPUT_live.txt SAMPLE_OUTPUT_demo.txt
git commit -m "docs: add sample runs for live and demo data"

git add README_COST_SENSITIVITY.md
git commit -m "docs: explain break-even cost analysis"
```
