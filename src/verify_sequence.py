"""Replays an execution sequence printed by hilo_fast or hilo_any and checks it.

Checks: every adder exists in the adder library with the printed cost and output width, every
placement has enough bits in its columns when it is placed, outputs stay in columns 0..2n,
the final product space meets the goal, and the costs add up to the reported total.

Usage: python3 -I verify_sequence.py lookup.csv n paper|relaxed solution.txt [--init c0,c1,...]
"""
import argparse, csv, re, sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('lookup'); ap.add_argument('n', type=int); ap.add_argument('goal', choices=['paper', 'relaxed'])
    ap.add_argument('solution'); ap.add_argument('--init', default=None)
    a = ap.parse_args()
    lib = {}
    with open(a.lookup, newline='') as f:
        for r in csv.DictReader(f):
            v = [int(r[f'col{i}']) for i in range(1, 9)]
            while v and v[-1] == 0:
                v.pop()
            lib[tuple(v)] = (int(r['weight']), int(round(float(r['Cost']))))
    C = 2 * a.n + 1
    if a.init:
        s = [int(x) for x in a.init.split(',')] + [0] * C
        s = s[:C]
    else:
        s = [min(c + 1, 2 * a.n - 1 - c) if c <= 2 * a.n - 2 else 0 for c in range(C)]
    text = open(a.solution).read()
    head = re.search(r'cost=(\d+)', text)
    reported = int(head.group(1)) if head else None
    total, count = 0, 0
    for m in re.finditer(r'adder \(([\d,]+)\) W=(\d+) cost=(\d+) at column (\d+)', text):
        v = tuple(int(x) for x in m.group(1).split(','))
        W, cost, k = int(m.group(2)), int(m.group(3)), int(m.group(4))
        if lib.get(v) != (W, cost):
            sys.exit(f'FAIL: adder {v} with W={W}, cost={cost} is not in the adder library (library: {lib.get(v)})')
        if k + W - 1 >= C:
            sys.exit(f'FAIL: adder {v} at column {k} writes beyond column {C - 1}')
        for j, x in enumerate(v):
            if s[k + j] < x:
                sys.exit(f'FAIL: adder {v} at column {k} needs {x} bits in column {k + j}, only {s[k + j]} left')
            s[k + j] -= x
        for j in range(W):
            s[k + j] += 1
        total += cost; count += 1
    ok = all(x == 1 for x in s) if a.goal == 'paper' else all(x <= 1 for x in s)
    if not ok:
        sys.exit(f'FAIL: final product space {s} does not meet the {a.goal} goal')
    if reported is not None and reported != total:
        sys.exit(f'FAIL: costs add up to {total}, reported {reported}')
    print(f'OK: n={a.n} goal={a.goal} adders={count} cost={total} final={s}')


if __name__ == '__main__':
    main()
