# HILO: minimum-cost adder trees for n-bit multipliers

HILO finds the cheapest way to reduce the product space of an n-bit multiplication to one bit per column, using the adder library in `lookuptable_with_weights.csv`. It proves optimality and checks every result. It runs for **any bit width from 2 bits up**. Optimal results for 2 to 12, 16, and 32 bits are included.

---

## Install

| Requirement | Used for |
|---|---|
| **g++** with C++17 (Linux, macOS, MinGW), or **Visual C++** on Windows | the three solvers in `src/*.cpp` |
| **Python 3** | replay check, results table |
| **NumPy + SciPy** (`pip install numpy scipy`) | LP prices for the prover and LP lower bounds. Without SciPy those steps are skipped with a message. |
| **sh** (Linux, macOS, or Git Bash on Windows) | `run_hilo.sh` |

The script builds the solvers itself with g++. On Windows without g++, build them once with Visual C++:

```bash
tools\msvc\build.bat          # builds bin\hilo_fast.exe, bin\hilo_any.exe, bin\hilo_prove.exe
```

Then add `NOBUILD=1` to every `run_hilo.sh` command below. If Visual Studio is not in the default place, set `VCVARS` to the path of `vcvars64.bat` first.

---

## Pipeline

```
lookup table → exact A* → beam search → LP prices → prover → replay check → LP bound → results.csv
```

| Stage | Program | What it does |
|---|---|---|
| Exact A* | `hilo_fast` | Optimal search for small widths (up to 12 bits; above that it runs out of memory). |
| Beam search | `hilo_fast --beam 100` | Fast upper bound U for any width, with no optimality guarantee. |
| LP prices | `lp_potential_relaxed.py`, `check_prices.py` | Column prices that make the prover's lower bound strong. They are checked exactly against every adder placement. Prices are stored in `lp_prices/` and computed when missing. |
| Prover | `hilo_prove` | Searches for any solution with cost ≤ U - 1. **No solution** means the beam result is optimal. If it finds one, that cost is the optimum. |
| Replay check | `verify_sequence.py` | Replays every adder sequence and checks its cost and final state. |
| LP bound | `ilp_bound.py --lp-only` | Lower bound that holds for any order of adders. |
| No-order check | `hilo_any --nopor` | Exact search without the column-sweep order (2 to 5 bits), which confirms the order loses nothing. |
| Collect | `collect_results.py` | Writes one row per sequence to `results.csv`. |

---

## Quick start

```bash
BITS=8 sh run_hilo.sh                    # every step for 8 bits (about 10 s), output in runs/n8/
BITS="2 3 4 5 6" sh run_hilo.sh          # several widths in one run, output in runs/n2_3_4_5_6/
BITS=14 sh run_hilo.sh                   # widths above 12 use beam search + prover instead of A*
BITS=16 sh run_hilo.sh lookuptable_with_weights.csv 8   # 16 bits with 8 threads
NOBUILD=1 BITS=8 sh run_hilo.sh          # Windows: use the executables from tools\msvc\build.bat
```

Look at the outcome:

```bash
cat runs/n8/results.csv          # cost, adders, states, time and LP bound per method
cat runs/n8/proof_summary.txt    # whether the prover proved the result optimal
cat runs/n8/verification.txt     # replay check, one OK line per sequence
```

Sample terminal output for `BITS=8`:

```
2026-11-20 21:39:12  exact A*, 8 bits
2026-11-20 21:39:15  beam search B = 100, 8 bits
2026-11-20 21:39:17  prover, 8 bits, bound 441, 8 threads (progress in runs/n8/proofs/prove8_hilo.txt)
2026-11-20 21:39:17  replaying every sequence
2026-11-20 21:39:19  LP bound, 8 bits
2026-11-20 21:39:21  done; see runs/n8/results.csv
```

---

## Single programs

The solvers can also be run by hand. Each prints a header line with the cost, states, time and root lower bound, then one line per adder.

```bash
bin/hilo_fast lookuptable_with_weights.csv 8 relaxed                    # exact A*: 442
bin/hilo_fast lookuptable_with_weights.csv 32 relaxed --beam 100        # beam: 8,082
bin/hilo_prove lookuptable_with_weights.csv 16 relaxed --ub 1943 --threads 8 --lambda lp_prices/lambda_hilo_n16.txt
bin/hilo_any lookuptable_with_weights.csv 4 relaxed --nopor             # no column-sweep order: 87
python3 src/lp_potential_relaxed.py lookuptable_with_weights.csv 16 lp_prices/lambda_hilo_n16.txt
python3 src/check_prices.py lookuptable_with_weights.csv lp_prices/lambda_hilo_n16.txt
python3 src/ilp_bound.py lookuptable_with_weights.csv 16 relaxed --lp-only
python3 src/verify_sequence.py lookuptable_with_weights.csv 32 relaxed results/sequences/n32_hilo_prover.txt
```

`relaxed` is the goal: every column ends with at most one bit. `paper` (exactly one bit in every column) is also accepted.

| Program | Key flags |
|---|---|
| `hilo_fast` | `--beam B` beam search · `--w W` weighted A* · `--ub U` cost cap · `--max M` stop after M states and report a lower bound · `--init c0,c1,…` custom start state · `--quiet` |
| `hilo_prove` | `--ub U` (required) · `--threads T` · `--lambda file` LP prices · `--keep` recover the adder list · `--start k,s0..s5,g` start from a given state |
| `hilo_any` | `--nopor` turn off the partial-order reduction · `--max M` |
| `ilp_bound.py` | `--lp-only` LP relaxation only · `--time T` ILP time limit · `--init` |

The executables are built with `-march=native`, so build them again on each machine.

---

## Output

Every run writes the same layout to its output folder (`runs/nN/` or `results/`):

```
runs/n8/
├── results.csv               — one row per sequence: n, goal, method, cost, adders, states, time, bounds
├── proof_summary.txt         — prover verdict per width (beam optimal, or the new optimum)
├── verification.txt          — replay check, one OK line per sequence
├── lp_bounds.txt             — LP lower bound per width
├── unrestricted_check.txt    — hilo_any results (2 to 5 bits only)
├── sequences/
│   ├── n08_hilo_exact.txt    — exact A* solution
│   ├── n08_hilo_beam100.txt  — beam search solution
│   └── n08_hilo_prover.txt   — prover solution (only when it beats the beam)
└── proofs/
    ├── prove8_hilo.txt       — full prover log
    └── check_prices_n8.txt   — exact check of the LP prices
```

A sequence file starts with a header line, followed by one line per adder:

```
n=8 goal=relaxed w=1.000 cost=442 pops=156465 states=397654 time=2.81s root_bound=386
   adder (2,3,4) W=5 cost=48 at column 1
   adder (6,7,7) W=6 cost=131 at column 4
   ...
```

---

## The problem

A state is the number of bits in each column of the product space, with the least significant column first. An n-bit multiplication has 2n+1 columns (0 to 2n). Column c starts with min(c+1, 2n-1-c) bits.

An adder v placed at column k consumes v[j] bits from column k+j. It returns W bits, one to each of the columns k to k+W-1, where W is the `weight` column of the lookup table. The cost of the adder is the `Cost` column. All output bits stay within columns 0 to 2n.

The goal is reached when **every column holds at most one bit**. This is the usual definition of a fully reduced product space. Column 2n of an n-bit product always holds zero, so it may stay empty.

### How the search works

Every adder needs at least two bits in its lowest column. The search therefore always places the next adder at the lowest column k that holds two or more bits (column-sweep order). The columns below k never change again, and the columns above k+5 still hold their initial bits, so a state is k plus six column heights, packed into one 64-bit word.

- **Exact A\*** uses the heuristic h = ceil(rho * max(0, B - (2n+1-k))). B is the number of bits in columns k and above. rho = 312/38 = 8.21 is the lowest cost per removed bit in the library. h never overestimates, so the first final state that A* expands is optimal.
- **Beam search** keeps the best B states of each (k, s[k]) bucket. It gives an upper bound without an optimality guarantee.
- **The prover** takes an upper bound U. It visits each state once in column-sweep order, discards every state with g + h > U, and either returns the optimum at or below U or proves that no solution costs U or less. With `--lambda`, h also uses LP column prices: h = sum_c lambda[c] * (s[c] - 1) + shift. The shift, min(0, lambda[2n], lambda[2n-1] + lambda[2n]), allows the top columns to end empty. The prices are scaled to integers and checked exactly against every adder placement, so the bound stays valid.

The results are optimal under the column-sweep order. `hilo_any`, which places adders at any column, finds the same optima for 2 to 5 bits.

---

## Results

Optimal decompositions and search effort:

| n | Optimum | LP bound | Adders | A\* time (s) | Algorithm 1 expanded | Algorithm 1 peak | Algorithm 1 time (s) |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 2 | 9 | 9 | 1 | <0.1 | 0 | 1 | <0.1 |
| 3 | 39 | 30 | 1 | <0.1 | 12 | 11 | <0.1 |
| 4 | 87 | 69 | 2 | <0.1 | 208 | 192 | <0.1 |
| 5 | 150 | 126 | 3 | <0.1 | 1.3k | 749 | <0.1 |
| 6 | 230 | 200 | 4 | 0.1 | 5.0k | 2.6k | <0.1 |
| 7 | 328 | 290 | 5 | 0.8 | 20.2k | 9.0k | 0.1 |
| 8 | 442 | 396 | 5 | 4.0 | 60.9k | 20.9k | 0.3 |
| 9 | 572 | 520 | 7 | 13.8 | 148k | 43.0k | 0.8 |
| 10 | 720 | 659 | 9 | 51.0 | 372k | 95.2k | 3.1 |
| 11 | 884 | 815 | 10 | 150 | 804k | 182k | 7.9 |
| 12 | 1,063 | 987 | 11 | 354 | 1.44M | 290k | 17.3 |
| 13 | 1,258 | 1,176 | 14 | – | 2.58M | 461k | 38.9 |
| 14 | 1,471 | 1,382 | 15 | – | 4.27M | 707k | 71.4 |
| 15 | 1,699 | 1,603 | 17 | – | 7.10M | 1.06M | 136 |
| 16 | 1,944 | 1,841 | 18 | – | 10.5M | 1.44M | 227 |
| 32 | 8,081 | 7,884 | 50 | – | 1.09B | 60.5M | 10,214‡ |

Algorithm 1 is the prover (`hilo_prove`). Expanded and peak count the states it expands and the most states it holds at once. ‡ Multi-threaded run.

### Checks

- **Replay.** Every sequence in `results/sequences` passes `verify_sequence.py`. Each ends with one bit in columns 0 to 2n-1 and an empty column 2n (`results/verification.txt`).
- **Prover.** With the bound set to the A* optimum, the prover returns 442, 720, and 1,063 at 8, 10, and 12 bits (`results/proofs/tests`).
- **Prices.** `check_prices.py` finds no adder placement that the prices in `lp_prices/` overestimate.
- **Column-sweep order.** `hilo_any --nopor` places adders at any column and finds the same optima for 2 to 5 bits: 9, 39, 87, and 150 (`results/unrestricted_check.txt`).

---

## Project layout

```
hilo/
├── README.md
├── run_hilo.sh                     — builds and runs everything (BITS=N for any width)
├── lookuptable_with_weights.csv    — adder library (input)
├── src/
│   ├── hilo_fast.cpp               — exact A* and beam search
│   ├── hilo_any.cpp                — exact search without the column-sweep order (2 to 5 bits)
│   ├── hilo_prove.cpp              — prover with LP prices
│   ├── lp_potential_relaxed.py     — computes the LP column prices (needs SciPy)
│   ├── check_prices.py             — checks the prices exactly against every adder placement
│   ├── ilp_bound.py                — LP lower bound, valid for any execution order (needs SciPy)
│   ├── verify_sequence.py          — replays a sequence and checks its final state and cost
│   └── collect_results.py          — collects the outputs into results.csv
├── lp_prices/                      — lambda_hilo_nN.txt (stored for 8, 10, 12, 16, 32; others made on demand)
├── row_mapping/                    — in-row mapping: latency / energy / area of y = A·x (own README)
├── tools/msvc/                     — Windows build without g++ (build.bat, bits/stdc++.h stand-in)
├── bin/                            — built executables
├── runs/                           — output of BITS=… runs (one folder per run)
└── results/                        — stored results for 2–12, 16, 32 bits (same layout as Output above)
```

---

## Notes

- The costs count the adders only. The generation of the partial products (AND gates) is not included.
- The optima hold under the column-sweep order. The LP bounds hold for any order.
- `run_hilo.sh` checks the SHA-256 of the lookup table and warns if it differs from the one used for these results.
