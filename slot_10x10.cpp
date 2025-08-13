#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <unordered_map>
#include <random>
#include <thread>
#include <chrono>
#include <algorithm>
#include <utility>
#include <cmath>
using namespace std;

struct GameConfig {
    static constexpr int ROWS = 10;
    static constexpr int COLS = 10;

    // Symbols used
    // Common symbols: A..H (increasing value), W (wild), S (scatter)
    const vector<char> symbols = { 'A','B','C','D','E','F','G','H','W','S' };

    // Weights (same order as symbols). Adjust for volatility.
    // Higher weight => more common.
    const vector<double> weights = {
        18, // A
        16, // B
        14, // C
        12, // D
        10, // E
        7,  // F
        5,  // G
        3,  // H
        1.7,// W (Wild)
        2.3 // S (Scatter)
    };

    // Base pays per symbol for a 3-of-a-kind (multiplier on bet).
    // Longer runs increase linearly (see payForRun).
    const unordered_map<char, double> basePay = {
        {'A', 0.5}, {'B', 0.8}, {'C', 1.0}, {'D', 1.3},
        {'E', 1.7}, {'F', 2.5}, {'G', 4.0}, {'H', 6.0}
        // 'W' has no own pay; 'S' handled separately as scatter
    };

    // Scatter pays anywhere for 5+ occurrences. Payout = bet * scatterStep * (count - 4)
    const double scatterStep = 0.8; // tweak to taste

    // Bet levels (đồng). Change to your currency.
    const vector<long long> betLevels = { 1000,2000,4000,10000,20000,50000,100000,200000,500000 };

    // Starting balance
    long long startBalance = 1'000'000;
};

struct RNG {
    mt19937_64 eng;
    RNG() {
        random_device rd;
        uint64_t seed = ((uint64_t)rd() << 32) ^ chrono::high_resolution_clock::now().time_since_epoch().count();
        eng.seed(seed);
    }
    size_t pick(const vector<double>& w) {
        discrete_distribution<size_t> dist(w.begin(), w.end());
        return dist(eng);
    }
    int randint(int a, int b) { // inclusive
        uniform_int_distribution<int> dist(a, b);
        return dist(eng);
    }
};

struct Game {
    GameConfig cfg;
    RNG rng;
    vector<vector<char>> grid;
    long long balance;
    int betIndex = 3; // default 10,000
    bool useColor = true;
    bool showWins = true;
    bool autoSpin = false;

    Game() {
        balance = cfg.startBalance;
        grid.assign(cfg.ROWS, vector<char>(cfg.COLS, ' '));
    }

    char randomSymbol() {
        size_t idx = rng.pick(cfg.weights);
        return cfg.symbols[idx];
    }

    void spin() {
        for (int r = 0; r < cfg.ROWS; ++r)
            for (int c = 0; c < cfg.COLS; ++c)
                grid[r][c] = randomSymbol();
    }

    static string colorFor(char ch) {
        // Basic ANSI colors (no specific style commitments)
        switch (ch) {
        case 'A': return "\x1b[37m"; // white
        case 'B': return "\x1b[36m"; // cyan
        case 'C': return "\x1b[35m"; // magenta
        case 'D': return "\x1b[34m"; // blue
        case 'E': return "\x1b[32m"; // green
        case 'F': return "\x1b[33m"; // yellow
        case 'G': return "\x1b[31m"; // red
        case 'H': return "\x1b[95m"; // bright magenta
        case 'W': return "\x1b[93m"; // bright yellow
        case 'S': return "\x1b[90m"; // gray
        default: return "\x1b[0m";
        }
    }

    void printGrid() {
        cout << "\n   ";
        for (int c = 0; c < cfg.COLS; ++c) { cout << setw(2) << c << ' '; }
        cout << "\n";
        for (int r = 0; r < cfg.ROWS; ++r) {
            cout << setw(2) << r << " ";
            for (int c = 0; c < cfg.COLS; ++c) {
                char ch = grid[r][c];
                if (useColor) cout << colorFor(ch);
                cout << ' ' << ch << ' ';
                if (useColor) cout << "\x1b[0m";
            }
            cout << "\n";
        }
        cout << "\n";
    }

    // Determine longest initial run from start of a line using wilds.
    // Returns (runLen, symbolUsed). Anchor target only within the prefix before any Scatter.
    // If the prefix is wild-only (3+), treat as highest symbol 'H'.
    pair<int, char> initialRun(const vector<char>& line) {
        char target = 0;
        int len = 0;
        for (char ch : line) {
            if (ch == 'S') break; // scatter breaks line matches
            if (ch == 'W') {
                ++len;
                continue;
            }
            if (target == 0) {
                target = ch;
                ++len;
            } else if (ch == target) {
                ++len;
            } else {
                break;
            }
        }
        if (len > 0 && target == 0) {
            target = 'H';
        }
        return { len, target };
    }

    long long evaluate(long long bet, vector<string>& winNotes) {
        long long total = 0;
        auto payForRun = [&](char sym, int run) {
            if (run < 3) return 0.0; // need 3+
            auto it = cfg.basePay.find(sym);
            if (it == cfg.basePay.end()) return 0.0;
            double base = it->second; // 3 of a kind
            // Linear growth: each extra symbol beyond 3 adds +base
            double mult = base * (run - 2);
            return mult;
            };

        // 10 row lines (left->right)
        for (int r = 0; r < cfg.ROWS; ++r) {
            vector<char> line;
            line.reserve(cfg.COLS);
            for (int c = 0; c < cfg.COLS; ++c) line.push_back(grid[r][c]);
            auto [len, sym ] = initialRun(line);
            double m = payForRun(sym, len);
            if (m > 0) {
                long long win = llround(bet * m);
                total += win;
                if (showWins) {
                    winNotes.push_back("Row " + to_string(r) + ": " + string(1, sym) + " x" + to_string(len) + " => +" + to_string(win));
                }
            }
        }
        // 10 column lines (top->bottom)
        for (int c = 0; c < cfg.COLS; ++c) {
            vector<char> line;
            line.reserve(cfg.ROWS);
            for (int r = 0; r < cfg.ROWS; ++r) line.push_back(grid[r][c]);
            auto [len, sym] = initialRun(line);
            double m = payForRun(sym, len);
            if (m > 0) {
                long long win = llround(bet * m);
                total += win;
                if (showWins) {
                    winNotes.push_back("Col " + to_string(c) + ": " + string(1, sym) + " x" + to_string(len) + " => +" + to_string(win));
                }
            }
        }

        // Scatter anywhere (5+)
        int scat = 0;
        for (int r = 0; r < cfg.ROWS; ++r)
            for (int c = 0; c < cfg.COLS; ++c)
                if (grid[r][c] == 'S') ++scat;
        if (scat >= 5) {
            double mult = cfg.scatterStep * (scat - 4);
            long long win = llround(bet * mult);
            total += win;
            if (showWins) {
                winNotes.push_back("Scatter S x" + to_string(scat) + " => +" + to_string(win));
            }
        }

        return total;
    }

    void showPaytable() {
        cout << "\n=== Paytable (3+ in a row/column from start) ===\n";
        vector<pair<char, double>> items(cfg.basePay.begin(), cfg.basePay.end());
        sort(items.begin(), items.end(), [](auto& a, auto& b) {return a.second < b.second; });
        for (auto& p : items) {
            cout << "  " << p.first << ": x" << fixed << setprecision(2) << p.second
                << " for 3; +x" << p.second << " each extra symbol\n";
        }
        cout << "  W: Wild (substitutes any symbol except S)\n";
        cout << "  S: Scatter pays anywhere: x" << cfg.scatterStep << " per symbol above 4 (e.g. 5S => x" << cfg.scatterStep << ")\n";
        cout.unsetf(std::ios::floatfield);
    }

    void showStatus() {
        cout << "\nBalance: " << balance
            << " | Bet: " << cfg.betLevels[betIndex]
            << " | Colors: " << (useColor ? "ON" : "OFF")
            << " | Show wins: " << (showWins ? "ON" : "OFF")
            << " | Auto-spin: " << (autoSpin ? "ON" : "OFF")
            << "\n";
    }

    void menu() {
        while (true) {
            if (!autoSpin) {
                cout << "\n==== NỔ HŨ 10x10 (C++) ====\n";
                showStatus();
                cout << "1) Spin\n2) Change bet\n3) Toggle colors\n4) Toggle show-wins\n5) Paytable\n6) Toggle auto-spin\n7) Add funds (+100k)\n0) Quit\n> ";
            }

            int choice;
            if (autoSpin) {
                choice = 1; // force spin
            }
            else if (!(cin >> choice)) {
                return; // EOF
            }

            if (choice == 0) return;
            else if (choice == 1) {
                long long bet = cfg.betLevels[betIndex];
                if (balance < bet) {
                    cout << "Not enough balance. Add funds or lower bet.\n";
                    autoSpin = false;
                    continue;
                }
                balance -= bet;
                spin();
                printGrid();
                vector<string> notes;
                long long win = evaluate(bet, notes);
                balance += win;
                if (showWins) {
                    if (notes.empty()) cout << "No line wins." << "\n";
                    else {
                        for (auto& s : notes) cout << s << "\n";
                    }
                }
                cout << "Result: -" << bet << " +" << win << " => Balance = " << balance << "\n";
                if (autoSpin) {
                    // lightweight delay to make it readable
                    this_thread::sleep_for(chrono::milliseconds(250));
                    if (balance < bet) {
                        cout << "Auto-spin stopped (insufficient balance).\n";
                        autoSpin = false;
                    }
                }
            }
            else if (choice == 2) {
                cout << "Select bet index:" << "\n";
                for (size_t i = 0; i < cfg.betLevels.size(); ++i) {
                    cout << "  [" << i << "] " << cfg.betLevels[i] << (i == (size_t)betIndex ? "  <- current" : "") << "\n";
                }
                cout << "> ";
                int idx; if (cin >> idx) { if (0 <= idx && idx < (int)cfg.betLevels.size()) betIndex = idx; }
            }
            else if (choice == 3) { useColor = !useColor; cout << "Colors: " << (useColor ? "ON" : "OFF") << "\n"; }
            else if (choice == 4) { showWins = !showWins; cout << "Show wins: " << (showWins ? "ON" : "OFF") << "\n"; }
            else if (choice == 5) { showPaytable(); }
            else if (choice == 6) { autoSpin = !autoSpin; cout << "Auto-spin: " << (autoSpin ? "ON" : "OFF") << "\n"; }
            else if (choice == 7) { balance += 100'000; cout << "+100,000 added. Balance = " << balance << "\n"; }
            else { cout << "Invalid choice." << "\n"; }
        }
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Game game;
    cout << "Welcome! This is a simple 10x10 slot (nổ hũ) demo.\n";
    cout << "Tip: If colors look weird, toggle Colors OFF in the menu.\n";
    game.menu();
    cout << "Goodbye!\n";
    return 0;
}