// hilo_fast: minimum-cost decomposition of an n-bit product space with an adder library.
//
// Cost model (same as the HILO notebook):
//   state   : number of bits in each column, least significant column first
//   adder v : placed at column k, consumes v[j] bits from column k+j and returns W bits,
//             one to each column k..k+W-1; its cost comes from the lookup table
// Search space (column sweep):
//   The next adder is always placed at the lowest column k that holds >= 2 bits. Every adder
//   needs >= 2 bits in its lowest column, so columns below k never change again, and an adder
//   touches at most 6 columns, so columns >= k+6 still hold their initial bits. A state is
//   therefore (k, s[k..k+5]) and packs into one 64-bit word. Outputs stay in columns 0..2n.
// Goals:
//   paper   : every column 0..2n holds exactly one bit (the notebook goal test)
//   relaxed : every column holds at most one bit
// Heuristic (admissible and consistent, integer valued):
//   h = ceil(rho * max(0, bits in columns >= k - (2n+1-k))), rho = min cost / (bits removed)
// Modes:
//   astar (default) : exact best-first search; --w > 1 gives weighted A* (bounded suboptimal);
//                     --max stops early and reports a certified lower bound (w = 1 only)
//   beam B          : column-sweep beam search, keeps the B best states per (k, s[k]) bucket;
//                     returns an upper bound quickly for large n
//
// Usage: hilo_fast lookup.csv n paper|relaxed [--w W] [--ub U] [--max M] [--beam B] [--init c0,c1,...] [--h0] [--quiet]
//   --init gives an arbitrary starting product space (LSB first) within the 2n+1 output columns
#include <bits/stdc++.h>
using namespace std;
typedef uint64_t u64;

struct Adder { int v[5]; int span, W, cost, red; };

static vector<Adder> lib;
static int n, C;
static bool paperGoal;
static vector<int> init_;         // initial column heights, zero padded
static vector<long long> suffix_; // suffix sums of init_
static long long rhoNum = 1, rhoDen = 0;
static bool noHeuristic = false;   // --h0: plain Dijkstra, for the heuristic ablation
static vector<int> L[64][32];     // adders indexed by (v0, v1)

static void load(const string& path) {
    ifstream in(path);
    if (!in) { fprintf(stderr, "cannot open %s\n", path.c_str()); exit(1); }
    string line; getline(in, line);
    while (getline(in, line)) {
        if (line.empty()) continue;
        stringstream ss(line); string tok; vector<double> f;
        while (getline(ss, tok, ',')) f.push_back(stod(tok));
        if (f.size() < 10) continue;
        Adder a{}; a.span = 0; int vsum = 0;
        for (int j = 0; j < 8; j++) {
            int x = (int)f[j];
            if (j >= 5) { if (x) { fprintf(stderr, "adder wider than 5 columns\n"); exit(1); } continue; }
            a.v[j] = x; if (x) a.span = j + 1; vsum += x;
        }
        a.cost = (int)llround(f[8]); a.W = (int)f[9]; a.red = vsum - a.W;
        if (a.v[0] < 2 || a.v[0] > 63 || a.v[1] > 31 || a.W > 6) { fprintf(stderr, "unexpected adder\n"); exit(1); }
        lib.push_back(a);
    }
    for (size_t i = 0; i < lib.size(); i++) {
        const Adder& a = lib[i];
        if (a.red > 0 && (rhoDen == 0 || (long long)a.cost * rhoDen < rhoNum * (long long)a.red)) { rhoNum = a.cost; rhoDen = a.red; }
        L[a.v[0]][a.v[1]].push_back((int)i);
    }
}

static inline u64 pack(int k, const int* w) {
    u64 key = (u64)k;
    for (int j = 0; j < 6; j++) key |= (u64)w[j] << (8 * (j + 1));
    return key;
}
static inline void unpack(u64 key, int& k, int* w) {
    k = (int)(key & 255);
    for (int j = 0; j < 6; j++) w[j] = (int)((key >> (8 * (j + 1))) & 255);
}
static inline int hval(int k, const int* w) {
    if (k >= C || noHeuristic) return 0;
    long long bits = suffix_[k + 6];
    for (int j = 0; j < 6; j++) bits += w[j];
    long long ex = bits - (C - k);
    if (ex <= 0) return 0;
    return (int)((ex * rhoNum + rhoDen - 1) / rhoDen);
}

// apply adder a at column k of window w; returns false if the successor is dead
static inline bool apply(int k, const int* w, const Adder& a, int& nk, int* nw) {
    int t[6];
    for (int j = 0; j < 6; j++) t[j] = w[j] - (j < 5 ? a.v[j] : 0) + (j < a.W ? 1 : 0);
    nk = k;
    while (nk < C) {
        int c = nk - k < 6 ? t[nk - k] : init_[nk];
        if (c >= 2) break;
        if (paperGoal && c == 0) return false;
        nk++;
    }
    for (int j = 0; j < 6; j++) {
        int c = nk + j;
        nw[j] = c >= C ? 0 : (c - k < 6 ? t[c - k] : init_[c]);
        if (nw[j] > 250) return false;
    }
    return true;
}

// open-addressing hash map u64 -> uint32
struct FlatMap {
    vector<u64> keys; vector<uint32_t> vals; size_t mask = 0, sz = 0;
    void init(size_t cap) { size_t c = 1; while (c < cap) c <<= 1; keys.assign(c, 0); vals.assign(c, 0); mask = c - 1; sz = 0; }
    static inline u64 mix(u64 x) { x += 0x9e3779b97f4a7c15ULL; x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL; x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL; return x ^ (x >> 31); }
    void grow() {
        vector<u64> ok; vector<uint32_t> ov; ok.swap(keys); ov.swap(vals);
        init(ok.size() * 2);
        for (size_t i = 0; i < ok.size(); i++) if (ok[i]) insert(ok[i], ov[i]);
    }
    inline uint32_t* find(u64 key) {
        size_t i = mix(key) & mask;
        while (keys[i]) { if (keys[i] == key) return &vals[i]; i = (i + 1) & mask; }
        return nullptr;
    }
    inline void insert(u64 key, uint32_t v) {
        if ((sz + 1) * 2 > keys.size()) grow();
        size_t i = mix(key) & mask;
        while (keys[i]) { if (keys[i] == key) { vals[i] = v; return; } i = (i + 1) & mask; }
        keys[i] = key; vals[i] = v; sz++;
    }
};

struct Node { u64 key; int g; uint32_t parent; uint16_t adder; uint8_t base; };

static void print_path(const vector<Node>& nodes, uint32_t id) {
    vector<uint32_t> path;
    for (uint32_t x = id; x != UINT32_MAX; x = nodes[x].parent) path.push_back(x);
    reverse(path.begin(), path.end());
    for (size_t p = 1; p < path.size(); p++) {
        const Adder& a = lib[nodes[path[p]].adder];
        printf("   adder (");
        for (int j = 0; j < a.span; j++) printf("%d%s", a.v[j], j + 1 < a.span ? "," : "");
        printf(") W=%d cost=%d at column %d\n", a.W, a.cost, nodes[path[p]].base);
    }
}

template <class F>
static inline void for_each_fit(int k, const int* w, F&& fn) {
    int m0 = min(w[0], 63), m1 = min(w[1], 31);
    for (int v0 = 2; v0 <= m0; v0++)
        for (int v1 = 0; v1 <= m1; v1++) {
            const vector<int>& lst = L[v0][v1];
            for (int ai : lst) {
                const Adder& a = lib[ai];
                if (a.v[2] > w[2] || a.v[3] > w[3] || a.v[4] > w[4]) continue;
                if (k + a.W - 1 >= C) continue;
                fn(ai, a);
            }
        }
}

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: hilo_fast lookup.csv n paper|relaxed [--w W] [--ub U] [--max M] [--beam B] [--quiet]\n"); return 1; }
    load(argv[1]);
    n = atoi(argv[2]); C = 2 * n + 1;
    paperGoal = string(argv[3]) == "paper";
    double WGT = 1.0; long long UB = LLONG_MAX / 4; size_t MAXS = (size_t)6e7; long long BEAM = 0; bool quiet = false;
    string initArg;
    for (int i = 4; i < argc; i++) {
        string o = argv[i];
        if (o == "--init") initArg = argv[++i];
        else if (o == "--w") WGT = atof(argv[++i]);
        else if (o == "--ub") UB = atoll(argv[++i]);
        else if (o == "--max") MAXS = (size_t)atoll(argv[++i]);
        else if (o == "--beam") BEAM = atoll(argv[++i]);
        else if (o == "--quiet") quiet = true;
        else if (o == "--h0") noHeuristic = true;
    }
    if (n < 2 || n > 32) { fprintf(stderr, "n must be in 2..32\n"); return 1; }
    init_.assign(C + 16, 0); suffix_.assign(C + 17, 0);
    if (initArg.empty()) {
        for (int c = 0; c <= 2 * n - 2; c++) init_[c] = min(c + 1, 2 * n - 1 - c);
    } else {
        // arbitrary starting product space, e.g. the layout after macro adders (comma separated, LSB first)
        stringstream ss(initArg); string tok; int c = 0;
        while (getline(ss, tok, ',')) { if (c >= C) { fprintf(stderr, "--init longer than 2n+1 columns\n"); return 1; } init_[c++] = atoi(tok.c_str()); }
    }
    for (int c = C + 15; c >= 0; c--) suffix_[c] = suffix_[c + 1] + init_[c];

    int k0 = 0; while (k0 < C && init_[k0] < 2) k0++;
    int w0[6]; for (int j = 0; j < 6; j++) w0[j] = k0 + j < C ? init_[k0 + j] : 0;
    const int root = hval(k0, w0);
    const char* gname = paperGoal ? "paper" : "relaxed";
    auto t0 = chrono::steady_clock::now();
    auto secs = [&]() { return chrono::duration<double>(chrono::steady_clock::now() - t0).count(); };

    vector<Node> nodes; nodes.reserve(1 << 20);
    FlatMap index; index.init(1 << 21);
    nodes.push_back({pack(k0, w0), 0, UINT32_MAX, 0, 0});
    index.insert(nodes[0].key, 0);

    if (BEAM > 0) {
        // column sweep in topological order (k ascending, s[k] descending), beam per bucket
        // buckets[k][h] holds node ids whose state has lowest column k with s[k] = h
        vector<vector<vector<uint32_t>>> buckets(C + 1, vector<vector<uint32_t>>(256));
        buckets[k0][w0[0]].push_back(0);
        long long best = LLONG_MAX; uint32_t bestId = UINT32_MAX; long long expanded = 0;
        for (int k = k0; k < C; k++) {
            for (int h = 255; h >= 2; h--) {
                vector<uint32_t>& B = buckets[k][h];
                if (B.empty()) continue;
                // keep the BEAM best states of this bucket by g + h
                sort(B.begin(), B.end()); B.erase(unique(B.begin(), B.end()), B.end());
                vector<pair<long long, uint32_t>> sc; sc.reserve(B.size());
                for (uint32_t id : B) { int kk, ww[6]; unpack(nodes[id].key, kk, ww); sc.push_back({nodes[id].g + hval(kk, ww), id}); }
                if ((long long)sc.size() > BEAM) { nth_element(sc.begin(), sc.begin() + BEAM, sc.end()); sc.resize(BEAM); }
                for (auto& pr : sc) {
                    uint32_t id = pr.second; expanded++;
                    int kk, w[6]; unpack(nodes[id].key, kk, w);
                    const int g = nodes[id].g;
                    for_each_fit(kk, w, [&](int ai, const Adder& a) {
                        long long ng = g + a.cost;
                        if (ng > UB || ng >= best) return;
                        int nk, nw[6];
                        if (!apply(kk, w, a, nk, nw)) return;
                        if (ng + hval(nk, nw) > min(UB, best - 1)) return;
                        u64 nkey = pack(nk, nw);
                        uint32_t* p = index.find(nkey);
                        if (!p) {
                            uint32_t nid = (uint32_t)nodes.size();
                            nodes.push_back({nkey, (int)ng, id, (uint16_t)ai, (uint8_t)kk});
                            index.insert(nkey, nid);
                            if (nk >= C) { if (ng < best) { best = ng; bestId = nid; } }
                            else buckets[nk][nw[0]].push_back(nid);
                        } else if (ng < nodes[*p].g) {
                            Node& nd = nodes[*p]; nd.g = (int)ng; nd.parent = id; nd.adder = (uint16_t)ai; nd.base = (uint8_t)kk;
                            if (nk >= C) { if (ng < best) { best = ng; bestId = *p; } }
                            else buckets[nk][nw[0]].push_back(*p);
                        }
                    });
                }
                vector<uint32_t>().swap(B);
            }
            if (!quiet) fprintf(stderr, "  beam n=%d column %d done, states=%zu best=%lld t=%.0fs\n", n, k, nodes.size(), best == LLONG_MAX ? -1 : best, secs());
        }
        if (bestId == UINT32_MAX) { printf("n=%d goal=%s beam=%lld no solution found\n", n, gname, BEAM); return 0; }
        printf("n=%d goal=%s beam=%lld cost=%lld expanded=%lld states=%zu time=%.2fs root_bound=%d\n",
               n, gname, BEAM, best, expanded, nodes.size(), secs(), root);
        print_path(nodes, bestId);
        return 0;
    }

    // A* with an integer bucket queue
    vector<vector<uint32_t>> open(1024);
    auto fval = [&](uint32_t id) { int kk, ww[6]; unpack(nodes[id].key, kk, ww); return nodes[id].g + (long long)floor(WGT * hval(kk, ww) + 1e-9); };
    auto push = [&](uint32_t id) { long long f = fval(id); if ((size_t)f >= open.size()) open.resize(f * 2 + 16); open[f].push_back(id); };
    push(0);
    long long pops = 0; size_t fcur = 0;
    while (true) {
        while (fcur < open.size() && open[fcur].empty()) fcur++;
        if (fcur >= open.size()) break;
        uint32_t id = open[fcur].back(); open[fcur].pop_back();
        if ((size_t)fval(id) != fcur) continue;      // stale entry
        pops++;
        int k, w[6]; unpack(nodes[id].key, k, w);
        const int g = nodes[id].g;
        if (k >= C) {
            printf("n=%d goal=%s w=%.3f cost=%d pops=%lld states=%zu time=%.2fs root_bound=%d\n",
                   n, gname, WGT, g, pops, nodes.size(), secs(), root);
            print_path(nodes, id);
            return 0;
        }
        for_each_fit(k, w, [&](int ai, const Adder& a) {
            long long ng = g + a.cost;
            if (ng > UB) return;
            int nk, nw[6];
            if (!apply(k, w, a, nk, nw)) return;
            if (ng + hval(nk, nw) > UB) return;
            u64 nkey = pack(nk, nw);
            uint32_t* p = index.find(nkey);
            if (!p) {
                uint32_t nid = (uint32_t)nodes.size();
                nodes.push_back({nkey, (int)ng, id, (uint16_t)ai, (uint8_t)k});
                index.insert(nkey, nid);
                push(nid);
            } else if (ng < nodes[*p].g) {
                Node& nd = nodes[*p]; nd.g = (int)ng; nd.parent = id; nd.adder = (uint16_t)ai; nd.base = (uint8_t)k;
                push(*p);
            }
        });
        if (nodes.size() > MAXS) {
            printf("n=%d goal=%s w=%.3f stopped at %zu states after %.1fs; certified lower bound %s%zu (root bound %d)\n",
                   n, gname, WGT, nodes.size(), secs(), WGT == 1.0 ? "" : "n/a, last f=", fcur, root);
            return 0;
        }
        if (!quiet && pops % 500000 == 0)
            fprintf(stderr, "  n=%d pops=%lld states=%zu f=%zu t=%.0fs\n", n, pops, nodes.size(), fcur, secs());
    }
    printf("n=%d goal=%s no solution within the bound (pops=%lld states=%zu time=%.2fs)\n", n, gname, pops, nodes.size(), secs());
    return 0;
}
