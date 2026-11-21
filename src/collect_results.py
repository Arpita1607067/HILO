"""Collects the solver outputs in results/sequences and results/lp_bounds.txt into results/results.csv.

Usage: python3 -I src/collect_results.py results
"""
import csv, glob, os, re, sys

# Reference values of the greedy synthesis (AUTO). 8, 16, and 32 bits are from Table IV of the AUTO paper.
AUTO = {3: 44, 4: 97, 5: 164, 6: 264, 8: 478, 16: 2024, 32: 8462}


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'results'
    lp = {}
    path = os.path.join(root, 'lp_bounds.txt')
    if os.path.exists(path):
        for line in open(path):
            m = re.match(r'n=(\d+) goal=(\w+) .*LP bound=([\d.]+)', line)
            if m:
                lp[(int(m.group(1)), m.group(2))] = float(m.group(3))
    rows = []
    for f in sorted(glob.glob(os.path.join(root, 'sequences', '*.txt'))):
        name = os.path.basename(f)[:-4]
        n, _, method = name.split('_')   # file name: nNN_hilo_<method>
        n = int(n[1:])
        head = open(f).readline()
        g = re.search(r'goal=(\w+)', head)
        goal = g.group(1) if g else 'relaxed'
        cost = re.search(r'cost=(\d+)', head)
        states = re.search(r'states=(\d+)', head)
        secs = re.search(r'time=([\d.]+)s', head)
        root_b = re.search(r'root_bound=(\d+)', head)
        adders = sum(1 for line in open(f) if line.strip().startswith('adder'))
        rows.append({
            'n': n, 'goal': goal, 'method': method,
            'cost': cost.group(1) if cost else 'none',
            'adders': adders if cost else '',
            'states': states.group(1) if states else '',
            'time_s': secs.group(1) if secs else '',
            'root_bound': root_b.group(1) if root_b else '',
            'lp_bound': f"{lp[(n, goal)]:.1f}" if (n, goal) in lp else '',
            'auto_ops': AUTO.get(n, ''),
        })
    rows.sort(key=lambda r: (r['goal'], r['n']))
    out = os.path.join(root, 'results.csv')
    with open(out, 'w', newline='') as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0]))
        w.writeheader(); w.writerows(rows)
    for r in rows:
        print(r)


if __name__ == '__main__':
    main()
