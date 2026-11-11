#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cmath>

using namespace std;

struct Currency {
    string code;
    double unitsPerUSD;
    double volatilityScore;
    double liquidityScore;
};

/* Classroom simulation scores, not measurements supplied by the rate API. */
static double simulatedVolatility(const string &code) {
    if (code == "USD") return 15.0;
    if (code == "EUR") return 18.0;
    if (code == "GBP") return 30.0;
    if (code == "INR") return 45.0;
    if (code == "JPY") return 22.0;
    if (code == "CAD") return 20.0;
    if (code == "AUD") return 28.0;
    if (code == "CHF") return 16.0;
    if (code == "SGD") return 18.0;
    return 35.0;
}

static double simulatedLiquidity(const string &code) {
    if (code == "USD") return 95.0;
    if (code == "EUR") return 90.0;
    if (code == "GBP") return 85.0;
    if (code == "INR") return 65.0;
    if (code == "JPY") return 88.0;
    if (code == "CAD") return 78.0;
    if (code == "AUD") return 75.0;
    if (code == "CHF") return 80.0;
    if (code == "SGD") return 72.0;
    return 60.0;
}


class LiveRateProvider {
public:
    string fetchRates() const {
        FILE *pipe = popen("curl -fsSL --max-time 20 https://open.er-api.com/v6/latest/USD", "r");
        if (!pipe) return "";
        string response;
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) response += buffer;
        int status = pclose(pipe);
        return status == 0 ? response : "";
    }

    string readFile(const string &path) const {
        ifstream in(path);
        stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    
    bool readRate(const string &json, const string &code, double &rate) const {
        size_t ratesPos = json.find("\"rates\"");
        if (ratesPos == string::npos) return false;

        string key = "\"" + code + "\"";
        size_t pos = json.find(key, ratesPos);
        if (pos == string::npos) return false;
        pos = json.find(':', pos + key.size());
        if (pos == string::npos) return false;

        char *end = nullptr;
        rate = strtod(json.c_str() + pos + 1, &end);
        return end != json.c_str() + pos + 1 && rate > 0.0;
    }

    string lastUpdated(const string &json) const {
        size_t pos = json.find("\"time_last_update_utc\"");
        if (pos == string::npos) return "not supplied";
        size_t start = json.find('"', json.find(':', pos));
        size_t end = json.find('"', start + 1);
        if (start == string::npos || end == string::npos) return "not supplied";
        return json.substr(start + 1, end - start - 1);
    }
};


class MarketConditions {
private:
    double feePercent;
    double spreadPercent;

public:
    MarketConditions(double fee = 0.10, double spread = 0.10)
        : feePercent(fee), spreadPercent(spread) {}

    double fee() const { return feePercent; }
    double spread() const { return spreadPercent; }

    double effectiveRate(double referenceRate) const {
        return referenceRate * (1.0 - feePercent / 100.0) * (1.0 - spreadPercent / 100.0);
    }
};


class MarketDataBuilder {
private:
    vector<Currency> currencies;
    vector<string> supported = {"USD", "EUR", "GBP", "INR", "JPY", "CAD", "AUD", "CHF", "SGD"};
    bool demoMode = false;

    
    double demoFactor(const string &from, const string &to) const {
        if (!demoMode) return 1.0;
        if (from == "INR" && to == "GBP") return 1.015;  // +1.5%
        if (from == "EUR" && to == "JPY") return 1.020;  // +2.0%
        return 1.0;
    }

public:
    void setDemoMode(bool on) { demoMode = on; }

    bool load(const string &json, const LiveRateProvider &provider) {
        currencies.clear();
        for (const string &code : supported) {
            double rate = 0.0;
            if (!provider.readRate(json, code, rate)) {
                cerr << "API response did not contain a valid rate for " << code << ".\n";
                return false;
            }
            currencies.push_back({code, rate, simulatedVolatility(code), simulatedLiquidity(code)});
        }
        return true;
    }

    void showRates() const {
        cout << "\nREFERENCE RATES AND SIMULATED MARKET SCORES\n";
        cout << "  Rates are units of currency per 1 USD; scores range from 0 to 100.\n";
        cout << fixed << setprecision(6);
        for (const Currency &c : currencies)
            cout << "  " << left << setw(6) << c.code << right << setw(14) << c.unitsPerUSD
                 << "  volatility " << setw(5) << c.volatilityScore
                 << "  liquidity " << setw(5) << c.liquidityScore << '\n';
    }

    bool writeEdges(const MarketConditions &market, const string &filename) const {
        ofstream out(filename);
        if (!out) {
            cerr << "Could not create " << filename << ". Check folder permissions.\n";
            return false;
        }
        out << "from,to,rate,fee_percent,spread_percent,effective_rate,cost,volatility_score,liquidity_score\n";
        out << setprecision(12);
        for (size_t i = 0; i < currencies.size(); i++) {
            for (size_t j = 0; j < currencies.size(); j++) {
                if (i == j) continue;
                double cross = currencies[j].unitsPerUSD / currencies[i].unitsPerUSD;
                cross *= demoFactor(currencies[i].code, currencies[j].code);
                double effective = market.effectiveRate(cross);
                double volatility = (currencies[i].volatilityScore + currencies[j].volatilityScore) / 2.0;
                double liquidity = min(currencies[i].liquidityScore, currencies[j].liquidityScore);
                out << currencies[i].code << ',' << currencies[j].code << ',' << cross << ','
                    << market.fee() << ',' << market.spread() << ',' << effective << ','
                    << -log(effective) << ',' << volatility << ',' << liquidity << '\n';
            }
        }
        return true;
    }

    size_t size() const { return currencies.size(); }
};

int main(int argc, char **argv) {
    bool demo = false;
    string file;
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--demo") demo = true;
        else if (arg == "--file" && i + 1 < argc) file = argv[++i];
    }

    cout << "CURRENCY ARBITRAGE - MARKET DATA BUILDER (C++ / OOP)\n";

    LiveRateProvider provider;
    string json;
    if (file.empty()) {
        cout << "Downloading the latest reference-rate snapshot...\n";
        json = provider.fetchRates();
    } else {
        cout << "Reading saved snapshot from " << file << "...\n";
        json = provider.readFile(file);
    }
    if (json.empty()) {
        cerr << "\nERROR: Could not get rates. Check your internet connection and try again.\n";
        cerr << "The program stops rather than using made-up sample data.\n";
        return 1;
    }

    cout << "Source: https://open.er-api.com/v6/latest/USD\n";
    cout << "Provider update: " << provider.lastUpdated(json) << '\n';
    cout << "Note: these are reference rates, not executable bid/ask quotes.\n";

    MarketDataBuilder builder;
    builder.setDemoMode(demo);
    if (!builder.load(json, provider)) return 1;

    MarketConditions market(0.10, 0.10);  
    builder.showRates();

    cout << "\nCOST ASSUMPTIONS (ours, not from the API)\n";
    cout << "  Fee per conversion:    " << market.fee() << "%\n";
    cout << "  Spread per conversion: " << market.spread() << "%\n";

    if (demo) {
        cout << "\n*** DEMO MODE: INR->GBP boosted by 1.5% and EUR->JPY by 2.0% ***\n";
        cout << "*** to imitate a venue with generous quotes. NOT real market data. ***\n";
    }

    if (!builder.writeEdges(market, "edges.csv")) return 1;
    cout << "\nSaved edges.csv with " << builder.size() * (builder.size() - 1)
         << " directed routes. Next: run the C program.\n";
    return 0;
}
