"""Column prices from the LP dual, written as an exact integer potential for the search.

The LP relaxation chooses fractional adder placements x[a,b] >= 0 such that the net number of bits
removed from every column turns the initial product space into the final one (one bit per column).
Its dual assigns a price lam[c] to each column. Dual feasibility says that no placement removes bits
worth more than its cost:  sum_j lam[b+j] * (v_a[j] - out_a[j]) <= cost(a)  for every adder a and column b.
For any state s, h(s) = sum_c lam[c] * (s[c] - 1) is then a consistent lower bound on the remaining cost
(paper goal), and it equals the LP bound at the initial state.

The prices are scaled by D, rounded down to integers, and checked exactly against every placement.
If rounding or solver tolerance breaks a constraint, all prices are scaled down until none is broken.

Usage: python3 -I lp_potential.py lookup.csv n out.txt
"""
import csv, sys
from fractions import Fraction
import numpy as np
from scipy.optimize import linprog
from scipy.sparse import coo_matrix

D = 1_000_000


def main():
    path, n, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    V, W, cost = [], [], []
    with open(path, newline='') as f:
        for r in csv.DictReader(f):
            v = [int(r[f'col{i}']) for i in range(1, 9)][:5]
            V.append(v); W.append(int(r['weight'])); cost.append(int(round(float(r['Cost']))))
    C = 2 * n + 1
    s0 = np.array([min(c + 1, 2 * n - 1 - c) if c <= 2 * n - 2 else 0 for c in range(C)])
    rows, cols, vals, obj, place = [], [], [], [], []
    for a in range(len(W)):
        for b in range(1, C - W[a] + 1):
            coef = {}
            for j in range(W[a]):
                d = (V[a][j] if j < 5 else 0) - 1
                if d:
                    coef[b + j] = d
            for c, d in coef.items():
                rows.append(c); cols.append(len(obj)); vals.append(d)
            obj.append(cost[a]); place.append((a, b, coef))
    nx = len(obj)
    rows += [C - 2, C - 1]; cols += [nx, nx + 1]; vals += [1, 1]   # slack f[2n-1], f[2n]: final bits of the top columns
    A = coo_matrix((vals, (rows, cols)), shape=(C, nx + 2)).tocsr()
    b = (s0 - 1).astype(float); b[C - 2] += 1; b[C - 1] += 1
    # net bits removed per column = s0 - 1 (paper goal: one bit in every column)
    res = linprog(np.array(obj + [0.0, 0.0], float), A_eq=A, b_eq=b, bounds=[(0, None)] * nx + [(0, 1), (0, 1)], method='highs')
    lam = np.array(res.eqlin.marginals)          # d(objective)/d(b_eq) = column price
    print(f'n={n} LP bound={res.fun:.3f}  prices: min {lam.min():.3f} max {lam.max():.3f}')
    L = [int(np.floor(x * D)) for x in lam]
    # exact check of every dual constraint, scaling the prices down if needed
    for it in range(50):
        worst = Fraction(1)
        for a, b, coef in place:
            lhs = sum(L[c] * d for c, d in coef.items())
            cap = cost[a] * D
            if lhs > cap:
                worst = min(worst, Fraction(cap, lhs))
        if worst == 1:
            break
        L = [int(np.floor(x * float(worst) * (1 - 1e-9))) for x in L]   # uniform scaling keeps every satisfied constraint satisfied
    else:
        sys.exit('could not make the prices feasible')
    bound = Fraction(sum(L[c] * int(s0[c] - 1) for c in range(C)), D)
    print(f'integer prices feasible after {it} scaling steps; potential at the initial state = {float(bound):.3f}')
    with open(out, 'w') as f:
        f.write(f'{n} {D}\n' + ' '.join(map(str, L)) + '\n')


if __name__ == '__main__':
    main()
