# Currency Arbitrage: Finding the Best Profitable Route
**Part 1:** C++ (OOP) builds the market graph. **Part 2:** C (DSA) finds the best route. **Phase 3 additions:** enter a starting amount, inspect fee and spread deductions, and rank routes using volatility and liquidity scores.

## How to run in VS Code

1. Open this folder in VS Code (**File → Open Folder**).
2. Open the terminal (**Terminal → New Terminal**) and run:

```bash
./run.sh          # real exchange rates (needs internet)
./run.sh demo     # real rates + two labelled fake mispricings, so a profitable route appears
```

Or use the menu: **Terminal → Run Build Task** (live data), or **Terminal → Run Task…** and pick the demo.

Troubleshooting:
- `permission denied` → `chmod +x run.sh`
- `No compiler found` (macOS) → `xcode-select --install`

Manual version of what `run.sh` does:

```bash
clang++ -std=c++17 market_builder.cpp -o market_builder
clang arbitrage.c -o arbitrage -lm
./market_builder      # add --demo for demo mode
./arbitrage
```

## What the programs do

| File | Job |
|---|---|
| `market_builder.cpp` | Classes `LiveRateProvider`, `MarketConditions`, `MarketDataBuilder`. Downloads USD-based rates, derives all 72 cross-rates, applies 0.10% fee + 0.10% spread, adds clearly labelled simulated volatility and liquidity scores, and writes `edges.csv`. |
| `arbitrage.c` | Loads `edges.csv`. **Step 1:** Bellman-Ford checks for a profitable cycle. **Step 2:** depth-first search checks short round trips. Routes are ranked by net return adjusted for simulated volatility and liquidity. The walkthrough shows gross amount, fee, spread, net amount, and route risk scores. |
| `run.sh` | Compiles and runs both. |

Why two steps? Bellman-Ford only tells you *whether* a profitable cycle exists, not which one is best. The route search compares the cycles and ranks them.

When the C analyzer starts, enter a positive starting amount. The same numeric amount is evaluated for each route in that route's starting currency. The route walkthrough shows the gross converted amount, fee deduction, spread deduction, and net amount for every leg.

The route's average volatility risk and average liquidity are scored from 0 to 100. The builder assigns fixed classroom simulation values by currency; these scores do not come from the rate API. Higher volatility and lower liquidity increase a route penalty. The analyzer uses `risk-adjusted score = net multiplier * (1 - penalty percent / 100)`, where `penalty percent = 0.04 * (average volatility + 100 - average liquidity)`. Routes are ranked by this score, while a profitable route still means its net multiplier is above 1.0.

## Expected output

**Live data (`./run.sh`):** no profitable route exists, because every cross-rate comes from one consistent snapshot, so any round trip only loses the fees. The program shows the least-bad round trip for comparison (about -0.6%).

```
STEP 1 - BELLMAN-FORD (is there any profitable cycle?)
  No: after fees and spread, every cycle loses money.
NO PROFITABLE ROUTE EXISTS in this data.
```

**Demo (`./run.sh demo`):** INR→GBP and EUR→JPY are inflated (clearly labelled as fake) to imitate a venue with generous quotes:

```
BEST PROFITABLE ROUTE
  EUR -> JPY -> INR -> GBP -> EUR
  Start with 1000.00 EUR
  EUR -> JPY at ...: gross ..., fee ..., spread ..., net ... JPY
  Average volatility risk: .../100
  Average liquidity: .../100
  Risk penalty: ...%
  Risk-adjusted score: ...%
  ...
  Net change: +27.0465 EUR (+2.7047%)
```
(Exact numbers depend on the day's rates.)

## Limitations
- Data: ExchangeRate-API `https://open.er-api.com/v6/latest/USD`, reference rates updated about daily, not executable quotes.
- Fee and spread are assumptions you can edit in `main()` of `market_builder.cpp`.
- Real arbitrage needs simultaneous bid/ask quotes from several venues, real fees and transfer limits. Not trading advice.
- If the download fails, the program stops instead of inventing rates.
