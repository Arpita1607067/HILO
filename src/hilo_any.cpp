// hilo_any: exact search without the column-sweep restriction (verification tool for small n).
//
// Adders may be placed at any column that holds >= 2 bits. To avoid enumerating every order of
// independent placements, the search uses a partial-order reduction that keeps optimality:
//   if adder B (lowest column b) directly follows adder A (lowest column a > b) and B was
//   already feasible before A, the two can be swapped without changing feasibility, cost, or
//   the final state. Repeated swaps sort any solution into a normal form in which a lower
//   placement follows a higher one only if it consumes a carry bit that the higher one produced.
//   The search generates only normal-form sequences (flag --por, default on).
// The cost model, goals, output limit (columns 0..2n), and heuristic are those of hilo_fast.
//
// Usage: hilo_any lookup.csv n paper|relaxed [--nopor] [--max M]
#include <bits/stdc++.h>
using namespace std;

struct Adder { int v[5]; int span, W, cost, red; };
static vector<Adder> lib;
static vector<int> L[64][32];
static long long rhoNum = 1, rhoDen = 0;
static int n, C; static bool paperGoal;

static void load(const string& path) {
    ifstream in(path); string line; getline(in, line);
    while (getline(in, line)) {
        if (line.empty()) continue;
        stringstream ss(line); string tok; vector<double> f;
        while (getline(ss, tok, ',')) f.push_back(stod(tok));
        if (f.size() < 10) continue;
        Adder a{}; int vs = 0;
        for (int j = 0; j < 5; j++) { a.v[j] = (int)f[j]; if (a.v[j]) a.span = j + 1; vs += a.v[j]; }
        a.cost = (int)llround(f[8]); a.W = (int)f[9]; a.red = vs - a.W;
        lib.push_back(a);
    }
    for (size_t i = 0; i < lib.size(); i++) {
        const Adder& a = lib[i];
        if (a.red > 0 && (rhoDen == 0 || (long long)a.cost * rhoDen < rhoNum * (long long)a.red)) { rhoNum = a.cost; rhoDen = a.red; }
        L[a.v[0]][a.v[1]].push_back((int)i);
    }
}

// key layout: [0]=k, [1..C-k]=s[k..C-1], then a, lo, hi (last placement: base and carry-out range)
typedef array<uint8_t, 40> Key;
struct KeyHash { size_t operator()(const Key& k) const { uint64_t h = 1469598103934665603ULL; for (int i = 0; i < 40; i += 8) { uint64_t x; memcpy(&x, &k[i], 8); h = (h ^ x) * 1099511628211ULL; h ^= h >> 29; } return h; } };
struct Node { Key key; int g; int parent; int adder; int base; };

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: hilo_any lookup.csv n paper|relaxed [--nopor] [--max M]\n"); return 1; }
    load(argv[1]); n = atoi(argv[2]); C = 2 * n + 1; paperGoal = string(argv[3]) == "paper";
    bool por = true; size_t MAXS = 30000000;
    for (int i = 4; i < argc; i++) { string o = argv[i]; if (o == "--nopor") por = false; else if (o == "--max") MAXS = atoll(argv[++i]); }
    if (C + 4 > 40) { fprintf(stderr, "n too large for this tool\n"); return 1; }
    vector<int> s0(C, 0); for (int c = 0; c <= 2 * n - 2; c++) s0[c] = min(c + 1, 2 * n - 1 - c);

    auto encode = [&](const vector<int>& s, int a, int lo, int hi, Key& key, int& kout) -> bool {
        key.fill(0); int k = 0;
        while (k < C && s[k] < 2) { if (paperGoal && s[k] == 0) return false; k++; }
        key[0] = (uint8_t)k;
        for (int c = k; c < C; c++) key[1 + c - k] = (uint8_t)s[c];
        if (por) { key[37] = (uint8_t)a; key[38] = (uint8_t)lo; key[39] = (uint8_t)hi; }
        kout = k; return true;
    };
    auto hfun = [&](const vector<int>& s, int k) -> int {
        if (k >= C) return 0;
        long long bits = 0; for (int c = k; c < C; c++) bits += s[c];
        long long ex = bits - (C - k); if (ex <= 0) return 0;
        return (int)((ex * rhoNum + rhoDen - 1) / rhoDen);
    };

    vector<Node> nodes; unordered_map<Key, int, KeyHash> idx; idx.reserve(1 << 20);
    vector<vector<int>> open(4096);
    Key k0; int kk0; encode(s0, 0, 1, 0, k0, kk0);
    nodes.push_back({k0, 0, -1, -1, -1}); idx[k0] = 0; int root = hfun(s0, kk0); open[root].push_back(0);
    auto t0 = chrono::steady_clock::now();
    long long pops = 0; size_t fcur = 0;
    vector<int> s(C), ns(C);
    while (true) {
        while (fcur < open.size() && open[fcur].empty()) fcur++;
        if (fcur >= open.size()) break;
        int id = open[fcur].back(); open[fcur].pop_back();
        const Key key = nodes[id].key; const int g = nodes[id].g;
        int k = key[0];
        fill(s.begin(), s.end(), 1);
        for (int c = k; c < C; c++) s[c] = key[1 + c - k];
        if ((size_t)(g + hfun(s, k)) != fcur) continue;
        pops++;
        if (k >= C) {
            double t = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
            printf("n=%d goal=%s placement=any por=%d cost=%d pops=%lld states=%zu time=%.2fs\n", n, paperGoal ? "paper" : "relaxed", (int)por, g, pops, nodes.size(), t);
            vector<int> path; for (int x = id; x != -1; x = nodes[x].parent) path.push_back(x);
            reverse(path.begin(), path.end());
            for (size_t p = 1; p < path.size(); p++) {
                const Adder& a = lib[nodes[path[p]].adder];
                printf("   adder ("); for (int j = 0; j < a.span; j++) printf("%d%s", a.v[j], j + 1 < a.span ? "," : "");
                printf(") W=%d cost=%d at column %d\n", a.W, a.cost, nodes[path[p]].base);
            }
            return 0;
        }
        const int la = por ? key[37] : 0, llo = por ? key[38] : 1, lhi = por ? key[39] : 0;
        for (int b = k; b < C; b++) {
            if (s[b] < 2) continue;
            int w[5]; for (int j = 0; j < 5; j++) w[j] = b + j < C ? s[b + j] : 0;
            int m0 = min(w[0], 63), m1 = min(w[1], 31);
            for (int v0 = 2; v0 <= m0; v0++) for (int v1 = 0; v1 <= m1; v1++) for (int ai : L[v0][v1]) {
                const Adder& a = lib[ai];
                if (a.v[2] > w[2] || a.v[3] > w[3] || a.v[4] > w[4]) continue;
                if (b + a.W - 1 >= C) continue;
                if (por && b < la) {
                    // allowed only if this placement consumes a carry bit produced by the previous one
                    bool dep = false;
                    for (int c = llo; c <= lhi && !dep; c++) { int j = c - b; if (j >= 0 && j < a.span && a.v[j] == s[c]) dep = true; }
                    if (!dep) continue;
                }
                ns = s;
                for (int j = 0; j < a.span; j++) ns[b + j] -= a.v[j];
                for (int j = 0; j < a.W; j++) ns[b + j] += 1;
                Key nkey; int nk;
                if (!encode(ns, b, b + a.span, b + a.W - 1, nkey, nk)) continue;
                int ng = g + a.cost; int f = ng + hfun(ns, nk);
                auto it = idx.find(nkey);
                if (it == idx.end()) {
                    int nid = (int)nodes.size(); nodes.push_back({nkey, ng, id, ai, b}); idx.emplace(nkey, nid);
                    if ((size_t)f >= open.size()) open.resize(f * 2); open[f].push_back(nid);
                } else if (ng < nodes[it->second].g) {
                    Node& nd = nodes[it->second]; nd.g = ng; nd.parent = id; nd.adder = ai; nd.base = b;
                    if ((size_t)f >= open.size()) open.resize(f * 2); open[f].push_back(it->second);
                }
            }
        }
        if (nodes.size() > MAXS) { printf("n=%d stopped at %zu states; certified lower bound %zu\n", n, nodes.size(), fcur); return 0; }
        if (pops % 200000 == 0) { double t = chrono::duration<double>(chrono::steady_clock::now() - t0).count(); fprintf(stderr, "  n=%d pops=%lld states=%zu f=%zu t=%.0fs\n", n, pops, nodes.size(), fcur, t); }
    }
    printf("n=%d no solution\n", n);
    return 0;
}
