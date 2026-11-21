"""Exact checks behind the relaxed-goal bound: which columns can end empty, and how far the paper prices must shift."""
import csv, sys
lut, lamfile = sys.argv[1], sys.argv[2]
A = []
with open(lut, newline='') as f:
    for r in csv.DictReader(f):
        A.append(([int(r[f'col{i}']) for i in range(1, 9)], int(r['weight']), int(round(float(r['Cost'])))))
toks = open(lamfile).read().split()
n, D = int(toks[0]), int(toks[1]); lam = list(map(int, toks[2:]))
C = 2 * n + 1; assert len(lam) == C
span = lambda v: max((j + 1 for j, x in enumerate(v) if x), default=0)
bad = [a for a in A if span(a[0]) > a[1]]
print('adders', len(A), ' adders with input span > W:', len(bad), ' max span', max(span(a[0]) for a in A), ' max W', max(a[1] for a in A))
init = [min(c + 1, 2 * n - 1 - c) if c <= 2 * n - 2 else 0 for c in range(C)]
print('initially empty columns:', [c for c in range(C) if init[c] == 0])
viol = 0; tight = 0
for v, W, cost in A:
    for b in range(1, C - W + 1):
        lhs = sum(lam[b + j] * ((v[j] if j < 8 else 0) - (1 if j < W else 0)) for j in range(max(W, span(v))) if b + j < C)
        if lhs > cost * D: viol += 1
        if lhs == cost * D: tight += 1
print('dual constraint violations:', viol, ' tight:', tight)
pot = sum(lam[c] * (init[c] - 1) for c in range(C)) / D
print('lam[%d]=%.6f lam[%d]=%.6f' % (C - 2, lam[C - 2] / D, C - 1, lam[C - 1] / D))
shift = min(0, lam[C - 1], lam[C - 2] + lam[C - 1])
print('shift = %.6f ; paper potential %.3f ; relaxed root bound %.3f' % (shift / D, pot, pot + shift / D))
