"""LP and ILP lower bounds for the decomposition problem, plus a realizability check.

The integer program chooses how many times each adder is placed at each column, so that the
net number of bits removed from every column matches the initial and final product spaces:
    s0[c] - sum_{a,b} x[a,b] * (v_a[c-b] - out_a[c-b]) = f[c],   x >= 0 integer
Any valid execution sequence gives such a multiset, so the ILP optimum is a lower bound on the
true optimum. The ILP ignores the order of the placements. If its multiset can be executed in
some order with enough bits in every column at every step, the ILP optimum is the true optimum.

Usage: python3 -I ilp_bound.py lookup.csv n paper|relaxed [--time T] [--init c0,c1,...]
"""
import argparse, csv, sys, time
import numpy as np
from scipy.optimize import milp, linprog, LinearConstraint, Bounds
from scipy.sparse import coo_matrix


def load(path):
    V, W, C = [], [], []
    with open(path, newline='') as f:
        for r in csv.DictReader(f):
            v = [int(r[f'col{i}']) for i in range(1, 9)]
            assert all(x == 0 for x in v[5:])
            V.append(v[:5]); W.append(int(r['weight'])); C.append(int(round(float(r['Cost']))))
    return np.array(V), np.array(W), np.array(C)


def build(n, V, W, cost, init=None):
    Ccols = 2 * n + 1
    s0 = np.zeros(Ccols, dtype=int)
    if init is None:
        for c in range(2 * n - 1):
            s0[c] = min(c + 1, 2 * n - 1 - c)
    else:
        s0[:len(init)] = init
    rows, cols, vals, obj, meta = [], [], [], [], []
    var = 0
    for a in range(len(W)):
        span = max(j + 1 for j in range(5) if V[a][j])
        for b in range(1, Ccols - W[a] + 1):   # outputs stay in columns 0..2n
            for j in range(max(span, W[a])):
                d = (V[a][j] if j < 5 else 0) - (1 if j < W[a] else 0)
                if d:
                    rows.append(b + j); cols.append(var); vals.append(d)
            obj.append(cost[a]); meta.append((a, b)); var += 1
    A = coo_matrix((vals, (rows, cols)), shape=(Ccols, var)).tocsr()
    return s0, A, np.array(obj, dtype=float), meta


def realizable(n, s0, placements, V, W):
    """Depth-first search for an execution order; tries the lowest column first."""
    Ccols = 2 * n + 1
    items = sorted(placements, key=lambda p: (p[1], -V[p[0]][0]))
    counts = {}
    for p in items:
        counts[p] = counts.get(p, 0) + 1
    keys = list(counts)
    seen = set()
    order = []

    def feasible(s, a, b):
        return all(V[a][j] <= s[b + j] for j in range(5) if V[a][j])

    def dfs(s, remaining):
        if sum(remaining) == 0:
            return True
        key = tuple(remaining)
        if key in seen:
            return False
        seen.add(key)
        for i, (a, b) in enumerate(keys):
            if remaining[i] and feasible(s, a, b):
                ns = s.copy()
                for j in range(5):
                    if V[a][j]:
                        ns[b + j] -= V[a][j]
                for j in range(W[a]):
                    ns[b + j] += 1
                remaining[i] -= 1; order.append((a, b))
                if dfs(ns, remaining):
                    return True
                remaining[i] += 1; order.pop()
        return False

    ok = dfs(np.array(list(s0) + [0] * 8), [counts[k] for k in keys])
    return ok, order


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('lookup'); ap.add_argument('n', type=int); ap.add_argument('goal', choices=['paper', 'relaxed'])
    ap.add_argument('--time', type=float, default=600); ap.add_argument('--init', default=None)
    ap.add_argument('--lp-only', action='store_true')
    a = ap.parse_args()
    V, W, cost = load(a.lookup)
    init = [int(x) for x in a.init.split(',')] if a.init else None
    s0, A, obj, meta = build(a.n, V, W, cost, init)
    Ccols = 2 * a.n + 1
    nvar = A.shape[1]
    # equality: A x + f = s0 with f the final column counts
    if a.goal == 'paper':
        lb_f, ub_f = np.ones(Ccols), np.ones(Ccols)
    else:
        lb_f, ub_f = np.zeros(Ccols), np.ones(Ccols)
    from scipy.sparse import hstack, identity
    Af = hstack([A, identity(Ccols, format='csr')]).tocsr()
    c_full = np.concatenate([obj, np.zeros(Ccols)])
    bounds = Bounds(np.concatenate([np.zeros(nvar), lb_f]), np.concatenate([np.full(nvar, np.inf), ub_f]))
    cons = LinearConstraint(Af, s0, s0)
    t0 = time.time()
    lp = milp(c_full, constraints=cons, bounds=bounds, integrality=np.zeros(nvar + Ccols))
    t_lp = time.time() - t0
    print(f'n={a.n} goal={a.goal} variables={nvar:,} LP bound={lp.fun:.2f} ({t_lp:.1f}s)', flush=True)
    if a.lp_only:
        return
    t0 = time.time()
    res = milp(c_full, constraints=cons, bounds=bounds, integrality=np.ones(nvar + Ccols),
               options={'time_limit': a.time, 'disp': False, 'mip_rel_gap': 0})
    t_ilp = time.time() - t0
    if res.x is None:
        print(f'  ILP: no solution ({res.message})'); return
    x = np.round(res.x[:nvar]).astype(int)
    dual_bound = getattr(res, 'mip_dual_bound', None)
    print(f'  ILP: cost={res.fun:.0f} status="{res.message}" dual_bound={dual_bound} time={t_ilp:.1f}s', flush=True)
    placements = []
    for i in np.nonzero(x)[0]:
        placements += [meta[i]] * x[i]
    ok, order = realizable(a.n, s0, placements, V, W)
    print(f'  realizable as an execution sequence: {ok}')
    seq = order if ok else sorted(placements, key=lambda p: p[1])
    for (ad, b) in seq:
        span = max(j + 1 for j in range(5) if V[ad][j])
        print(f'     adder ({",".join(str(V[ad][j]) for j in range(span))}) W={W[ad]} cost={cost[ad]} at column {b}')


if __name__ == '__main__':
    main()
