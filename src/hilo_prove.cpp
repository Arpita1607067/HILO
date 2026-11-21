// hilo_prove (parallel): column-sweep dynamic program that proves or improves an upper bound on the cost.
//
// States, moves, goals, and the base heuristic are those of hilo_fast. The column-sweep state graph is
// acyclic: a move either lowers s[k] at the same column k or moves to a higher column. Visiting the states
// in the order (k ascending, s[k] descending) reaches every state after all of its predecessors, so each
// state is expanded once, with its optimal cost g. States with g + h > U are discarded, where U is the
// current upper bound. Once column k is finished, no later state can return to it, so its states are freed.
//
// Result: the minimum cost of any solution with cost <= U, or a proof that no such solution exists.
// With --keep, finished columns stay in memory and the sequence of the best solution is recovered backward.
//
// Lower bounds:
//   h_rho(s)    = ceil(rho * max(0, bits(s) - (2n+1)))                 (always used)
//   h_lambda(s) = ceil(sum_c lambda[c] * (s[c] - 1) + shift)            (--lambda)
//   lambda are integer column prices from lp_potential_relaxed.py (LP dual, checked exactly against every
//   adder placement), so h_lambda is consistent; h = max(h_rho, h_lambda) is consistent as well.
//   shift = 0 for the paper goal. For the relaxed goal, columns 2n-1 and 2n may end empty, so
//   shift = min(0, lambda[2n], lambda[2n-1] + lambda[2n]) keeps the bound valid.
// Adder pruning: a successor through adder a satisfies f' >= g + h_rho(s) + ceil(cost(a) - rho*r(a)) - 1.
// The adders of each (v0, v1) list are sorted by this increment, so the scan of a state stops early.
// Parallelism: the states of one (k, s[k]) bucket are independent. Threads expand batches of them into
// per-thread buffers, then merge the buffers shard by shard, so no two threads write the same map.
//
// Usage: hilo_prove lookup.csv n paper|relaxed --ub U [--threads T] [--lambda file] [--keep] [--start k,s0..s5,g]
//   --start begins the sweep from a column-sweep state reached with cost g (exact completion of a prefix)
#include <bits/stdc++.h>
#include <thread>
#include <atomic>
using namespace std;
typedef uint64_t u64;

struct Adder { int v[5]; int span, W, cost, red, inc; };
static vector<Adder> lib;
static vector<int> L[64][32];
static int n, C;
static bool paperGoal;
static vector<int> init_;
static vector<long long> suffix_;
static long long rhoNum = 1, rhoDen = 0;
static vector<long long> lam, lamSuffix; static long long lamD = 0, lamShift = 0;   // h_lambda data; lamShift = min over allowed finals of sum lam[c]*(1-f[c])

static void load(const string& path) {
    ifstream in(path);
    if (!in) { fprintf(stderr, "cannot open %s\n", path.c_str()); exit(1); }
    string line; getline(in, line);
    while (getline(in, line)) {
        if (line.empty()) continue;
        stringstream ss(line); string tok; vector<double> f;
        while (getline(ss, tok, ',')) f.push_back(stod(tok));
        if (f.size() < 10) continue;
        Adder a{}; int vs = 0;
        for (int j = 0; j < 5; j++) { a.v[j] = (int)f[j]; if (a.v[j]) a.span = j + 1; vs += a.v[j]; }
        for (int j = 5; j < 8; j++) if ((int)f[j]) { fprintf(stderr, "adder wider than 5 columns\n"); exit(1); }
        a.cost = (int)llround(f[8]); a.W = (int)f[9]; a.red = vs - a.W;
        if (a.red < 0) { fprintf(stderr, "adder returns more bits than it consumes\n"); exit(1); }
        lib.push_back(a);
    }
    for (auto& a : lib)
        if (a.red > 0 && (rhoDen == 0 || (long long)a.cost * rhoDen < rhoNum * (long long)a.red)) { rhoNum = a.cost; rhoDen = a.red; }
    for (size_t i = 0; i < lib.size(); i++) {
        Adder& a = lib[i];
        long long num = (long long)a.cost * rhoDen - rhoNum * (long long)a.red;
        a.inc = (int)((num + rhoDen - 1) / rhoDen) - 1;
        L[a.v[0]][a.v[1]].push_back((int)i);
    }
    for (int x = 0; x < 64; x++) for (int y = 0; y < 32; y++)
        sort(L[x][y].begin(), L[x][y].end(), [](int p, int q) { return lib[p].inc < lib[q].inc; });
}

static inline u64 pack(int k, const int* w) { u64 key = (u64)k; for (int j = 0; j < 6; j++) key |= (u64)w[j] << (8 * (j + 1)); return key; }
static inline void unpack(u64 key, int& k, int* w) { k = (int)(key & 255); for (int j = 0; j < 6; j++) w[j] = (int)((key >> (8 * (j + 1))) & 255); }
static inline int hrho(int k, const int* w) {
    if (k >= C) return 0;
    long long bits = suffix_[k + 6];
    for (int j = 0; j < 6; j++) bits += w[j];
    long long ex = bits - (C - k);
    return ex <= 0 ? 0 : (int)((ex * rhoNum + rhoDen - 1) / rhoDen);
}
static inline int hval(int k, const int* w) {
    int h = hrho(k, w);
    if (lamD && k < C) {
        long long v = lamSuffix[k + 6] + lamShift;
        for (int j = 0; j < 6 && k + j < C; j++) v += lam[k + j] * (w[j] - 1);
        if (v > 0) h = max(h, (int)((v + lamD - 1) / lamD));
    }
    return h;
}

static inline u64 mix(u64 x) { x += 0x9e3779b97f4a7c15ULL; x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL; x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL; return x ^ (x >> 31); }
static const int SH = 64;                                // shards per column
static inline int shardOf(u64 key) { return (int)((mix(key) >> 40) & (SH - 1)); }

struct GMap {
    vector<u64> keys; vector<int> g; size_t mask = 0, sz = 0;
    GMap() { init(256); }
    void init(size_t cap) { size_t c = 1; while (c < cap) c <<= 1; keys.assign(c, 0); g.assign(c, 0); mask = c - 1; sz = 0; }
    void grow() {
        vector<u64> ok; vector<int> og; ok.swap(keys); og.swap(g);
        size_t c = ok.size() * 2; keys.assign(c, 0); g.assign(c, 0); mask = c - 1; sz = 0;
        for (size_t i = 0; i < ok.size(); i++) if (ok[i]) { size_t j = mix(ok[i]) & mask; while (keys[j]) j = (j + 1) & mask; keys[j] = ok[i]; g[j] = og[i]; sz++; }
    }
    inline int relax(u64 key, int val) {                 // 0: not improved, 1: new, 2: improved
        if ((sz + 1) * 10 > keys.size() * 7) grow();
        size_t i = mix(key) & mask;
        while (keys[i]) { if (keys[i] == key) { if (val < g[i]) { g[i] = val; return 2; } return 0; } i = (i + 1) & mask; }
        keys[i] = key; g[i] = val; sz++; return 1;
    }
    inline int get(u64 key) const { size_t i = mix(key) & mask; while (keys[i]) { if (keys[i] == key) return g[i]; i = (i + 1) & mask; } return INT_MAX; }
    size_t bytes() const { return keys.size() * (sizeof(u64) + sizeof(int)); }
};
struct ColMap {
    GMap sh[SH];
    inline int get(u64 key) const { return sh[shardOf(key)].get(key); }
    size_t size() const { size_t s = 0; for (auto& m : sh) s += m.sz; return s; }
    size_t bytes() const { size_t s = 0; for (auto& m : sh) s += m.bytes(); return s; }
};

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: hilo_prove lookup.csv n paper|relaxed --ub U [--threads T] [--lambda file] [--keep]\n"); return 1; }
    load(argv[1]); n = atoi(argv[2]); C = 2 * n + 1; paperGoal = string(argv[3]) == "paper";
    long long UB0 = -1; int T = 1; bool keep = false; string lamFile, startArg;
    for (int i = 4; i < argc; i++) {
        string o = argv[i];
        if (o == "--ub") UB0 = atoll(argv[++i]);
        else if (o == "--threads") T = max(1, atoi(argv[++i]));
        else if (o == "--lambda") lamFile = argv[++i];
        else if (o == "--keep") keep = true;
        else if (o == "--start") startArg = argv[++i];
    }
    if (UB0 < 0) { fprintf(stderr, "--ub is required\n"); return 1; }
    init_.assign(C + 16, 0); suffix_.assign(C + 17, 0);
    for (int c = 0; c <= 2 * n - 2; c++) init_[c] = min(c + 1, 2 * n - 1 - c);
    for (int c = C + 15; c >= 0; c--) suffix_[c] = suffix_[c + 1] + init_[c];
    if (!lamFile.empty()) {
        ifstream in(lamFile); int nn; in >> nn >> lamD;
        if (nn != n) { fprintf(stderr, "lambda file is for n=%d\n", nn); return 1; }
        lam.assign(C + 16, 0); for (int c = 0; c < C; c++) in >> lam[c];
        if (!paperGoal) lamShift = min({0LL, lam[C - 1], lam[C - 2] + lam[C - 1]});   // relaxed: only columns 2n-1 and 2n can end empty, and 2n only if 2n-1 is
        lamSuffix.assign(C + 17, 0);
        for (int c = C + 15; c >= 0; c--) lamSuffix[c] = lamSuffix[c + 1] + (c < C ? lam[c] * (init_[c] - 1) : 0);
    }
    int k0 = 0; while (init_[k0] < 2) k0++;
    int w0[6]; for (int j = 0; j < 6; j++) w0[j] = init_[k0 + j];
    long long g0 = 0;
    if (!startArg.empty()) {
        // --start k,s[k],...,s[k+5],g : a column-sweep state reached with cost g
        vector<long long> v; stringstream ss(startArg); string tok; while (getline(ss, tok, ',')) v.push_back(atoll(tok.c_str()));
        if (v.size() != 8) { fprintf(stderr, "--start needs k, six column heights, and the cost so far\n"); return 1; }
        k0 = (int)v[0]; for (int j = 0; j < 6; j++) w0[j] = (int)v[1 + j]; g0 = v[7];
    }

    atomic<long long> UB(UB0), best(LLONG_MAX);
    vector<unique_ptr<ColMap>> col(C + 1);
    for (int c = k0; c < C; c++) col[c] = make_unique<ColMap>();
    { u64 key = pack(k0, w0); col[k0]->sh[shardOf(key)].relax(key, (int)g0); }
    const int root = hval(k0, w0);
    long long expanded = 0; size_t peakBytes = 0, peakStates = 0;
    auto t0 = chrono::steady_clock::now();
    auto secs = [&]() { return chrono::duration<double>(chrono::steady_clock::now() - t0).count(); };
    printf("n=%d goal=%s bound=%lld root_bound=%d threads=%d lambda=%s\n", n, paperGoal ? "paper" : "relaxed", UB0, root, T, lamD ? "yes" : "no"); fflush(stdout);

    const int DMAX = 9;                                  // successors land in columns k..k+8
    struct Item { u64 key; int g; };
    vector<vector<vector<vector<Item>>>> buf(T, vector<vector<vector<Item>>>(DMAX, vector<vector<Item>>(SH)));
    vector<vector<u64>> newSame(T);
    vector<vector<u64>> bucket(256);

    auto expand = [&](int tid, u64 key, int g) {
        int k, w[6]; unpack(key, k, w);
        long long ub = UB.load(memory_order_relaxed);
        int hr = hrho(k, w);
        if (g + (long long)hval(k, w) > ub) return false;
        long long slack = ub - (g + hr);                 // the increment bound is stated for h_rho
        int m0 = min(w[0], 63), m1 = min(w[1], 31);
        for (int v0 = 2; v0 <= m0; v0++) for (int v1 = 0; v1 <= m1; v1++) {
            for (int ai : L[v0][v1]) {
                const Adder& a = lib[ai];
                if (a.inc > slack) break;
                if (a.v[2] > w[2] || a.v[3] > w[3] || a.v[4] > w[4] || k + a.W - 1 >= C) continue;
                int t[6]; for (int j = 0; j < 6; j++) t[j] = w[j] - (j < 5 ? a.v[j] : 0) + (j < a.W ? 1 : 0);
                int nk = k; bool dead = false;
                while (nk < C) { int c = nk - k < 6 ? t[nk - k] : init_[nk]; if (c >= 2) break; if (paperGoal && c == 0) { dead = true; break; } nk++; }
                if (dead) continue;
                long long ng = g + a.cost;
                if (nk >= C) {
                    if (ng <= ub) {
                        long long b = best.load(); while (ng < b && !best.compare_exchange_weak(b, ng)) {}
                        long long u = UB.load(); while (ng - 1 < u && !UB.compare_exchange_weak(u, ng - 1)) {}
                        ub = UB.load(); slack = ub - (g + hr);
                    }
                    continue;
                }
                int nw[6]; for (int j = 0; j < 6; j++) { int c = nk + j; nw[j] = c >= C ? 0 : (c - k < 6 ? t[c - k] : init_[c]); if (nw[j] > 250) dead = true; }
                if (dead || nk - k >= DMAX) { if (!dead) { fprintf(stderr, "successor too far\n"); exit(1); } continue; }
                if (ng + hval(nk, nw) > ub) continue;
                u64 nkey = pack(nk, nw);
                buf[tid][nk - k][shardOf(nkey)].push_back({nkey, (int)ng});
            }
        }
        return true;
    };

    const size_t BATCH = max<size_t>(16384, 4096 * (size_t)T);   // larger batches keep thread start-up cheap on many cores
    for (int k = k0; k < C; k++) {
        if (!col[k] || col[k]->size() == 0) { if (!keep) col[k].reset(); continue; }
        for (auto& b : bucket) b.clear();
        ColMap& cm = *col[k];
        for (auto& m : cm.sh) for (size_t i = 0; i < m.keys.size(); i++) if (m.keys[i]) bucket[(m.keys[i] >> 8) & 255].push_back(m.keys[i]);
        long long colExp = 0;
        for (int h = 255; h >= 2; h--) {
            vector<u64>& B = bucket[h];
            for (size_t start = 0; start < B.size(); start += BATCH) {
                size_t stop = min(B.size(), start + BATCH);
                // phase 1: expand in parallel into per-thread buffers
                atomic<size_t> next(start); atomic<long long> cnt(0);
                auto work1 = [&](int tid) {
                    long long c = 0;
                    while (true) {
                        size_t i = next.fetch_add(256);
                        if (i >= stop) break;
                        size_t e = min(stop, i + 256);
                        for (; i < e; i++) { u64 key = B[i]; if (expand(tid, key, cm.get(key))) c++; }
                    }
                    cnt += c;
                };
                if (T == 1) work1(0); else { vector<thread> pool; for (int t = 0; t < T; t++) pool.emplace_back(work1, t); for (auto& th : pool) th.join(); }
                colExp += cnt;
                // phase 2: merge the buffers, one thread per (column offset, shard)
                atomic<int> nextJob(0);
                auto work2 = [&](int tid) {
                    while (true) {
                        int job = nextJob.fetch_add(1);
                        if (job >= DMAX * SH) break;
                        int d = job / SH, s = job % SH;
                        if (k + d >= C) continue;
                        GMap& m = col[k + d]->sh[s];
                        for (int t = 0; t < T; t++) {
                            for (const Item& it : buf[t][d][s]) { int r = m.relax(it.key, it.g); if (r == 1 && d == 0) newSame[tid].push_back(it.key); }
                            buf[t][d][s].clear();
                        }
                    }
                };
                if (T == 1) work2(0); else { vector<thread> pool; for (int t = 0; t < T; t++) pool.emplace_back(work2, t); for (auto& th : pool) th.join(); }
                for (int t = 0; t < T; t++) { for (u64 x : newSame[t]) bucket[(x >> 8) & 255].push_back(x); newSame[t].clear(); }
            }
            vector<u64>().swap(B);
        }
        expanded += colExp;
        size_t liveB = 0, liveS = 0; for (int c = k; c <= C; c++) if (col[c]) { liveB += col[c]->bytes(); liveS += col[c]->size(); }
        peakBytes = max(peakBytes, liveB); peakStates = max(peakStates, liveS);
        size_t thisCol = cm.size();
        if (!keep) col[k].reset();
        long long b = best.load();
        printf("  column %2d: %10zu states, %10lld expanded, live %9zu states (%.2f GB), best %s, t=%.0fs\n",
               k, thisCol, colExp, liveS, liveB / 1e9, b == LLONG_MAX ? "none" : to_string(b).c_str(), secs());
        fflush(stdout);
    }
    long long b = best.load();
    if (b == LLONG_MAX)
        printf("n=%d goal=%s RESULT: no solution with cost <= %lld (optimum >= %lld). expanded=%lld peak_live=%zu states (%.2f GB) time=%.1fs\n",
               n, paperGoal ? "paper" : "relaxed", UB0, UB0 + 1, expanded, peakStates, peakBytes / 1e9, secs());
    else
        printf("n=%d goal=%s RESULT: optimum = %lld (bound was %lld). expanded=%lld peak_live=%zu states (%.2f GB) time=%.1fs\n",
               n, paperGoal ? "paper" : "relaxed", b, UB0, expanded, peakStates, peakBytes / 1e9, secs());
    fflush(stdout);

    if (keep && b != LLONG_MAX) {
        const u64 startKey = pack(k0, w0);
        vector<int> finals = paperGoal ? vector<int>{1} : vector<int>{1, 0};
        for (int x2n : finals) {
            vector<pair<int, int>> seq;
            int tk = C; int tw[6] = {x2n, 0, 0, 0, 0, 0}; long long tg = b; bool ok = true;
            while (true) {
                if (tk < C && pack(tk, tw) == startKey) break;
                auto colv = [&](int c) -> int {
                    if (c >= C) return 0;
                    if (tk >= C) return c < 2 * n ? 1 : tw[0];
                    if (c < tk) return 1;
                    if (c <= tk + 5) return tw[c - tk];
                    return init_[c];
                };
                bool found = false;
                for (int bb = min(tk, C - 1); bb >= 1 && !found; bb--) {
                    bool pristine = true;
                    for (int c = bb + 6; c <= min(tk + 5, C - 1); c++) if (colv(c) != init_[c]) { pristine = false; break; }
                    if (!pristine) break;
                    if (!col[bb]) continue;
                    int cw[6]; for (int j = 0; j < 6; j++) cw[j] = colv(bb + j);
                    for (size_t ai = 0; ai < lib.size() && !found; ai++) {
                        const Adder& a = lib[ai];
                        if (bb + a.W - 1 >= C || tg - a.cost < 0) continue;
                        bool outs = true; for (int j = 0; j < a.W; j++) if (cw[j] < 1) { outs = false; break; }
                        if (!outs) continue;
                        int u[6]; for (int j = 0; j < 6; j++) u[j] = cw[j] - (j < a.W ? 1 : 0) + (j < 5 ? a.v[j] : 0);
                        if (u[0] < 2 || u[0] > 250) continue;
                        u64 key = pack(bb, u);
                        if (col[bb]->get(key) == tg - a.cost) { seq.push_back({(int)ai, bb}); tk = bb; memcpy(tw, u, sizeof(u)); tg -= a.cost; found = true; }
                    }
                }
                if (!found) { ok = false; break; }
            }
            if (!ok || tg != g0) continue;
            printf("execution sequence (cost %lld):\n", b);
            for (auto it = seq.rbegin(); it != seq.rend(); ++it) {
                const Adder& a = lib[it->first];
                printf("   adder ("); for (int j = 0; j < a.span; j++) printf("%d%s", a.v[j], j + 1 < a.span ? "," : "");
                printf(") W=%d cost=%d at column %d\n", a.W, a.cost, it->second);
            }
            break;
        }
    }
    return 0;
}
