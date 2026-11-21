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

## Running any bit width

`BITS` chooses the widths. The script picks the steps for each width:

| Width | Exact A* | Beam + prover | LP bound | No-order check | Rough time |
|---|:-:|:-:|:-:|:-:|---|
| 2 – 5 | ✓ | ✓ | ✓ | ✓ | seconds |
| 6 – 12 | ✓ | ✓ | ✓ | | 1 s (6 bits) to 7 min (12 bits), one core |
| 13 – 16 | | ✓ | ✓ | | 13 bits: 17 s in all. 16 bits: 13 s beam + 65 s proof, 8 threads |
| 32 | | ✓ | ✓ | | 2.5 min beam + **2.7 h proof on 24 threads, 1.7 GB** |

For each width the prover runs with U = beam cost - 1:

- `no solution with cost <= U` → the beam result is **optimal**. `proof_summary.txt` says `the beam result X is optimal`.
- `optimum = X` → a cheaper solution exists. X is the optimum, and its adder list goes to `sequences/nNN_hilo_prover.txt`.

At 12 bits and below, the exact A* and the prover must agree. This doubles as a self-test.

Widths without stored prices (only 8, 10, 12, 16, and 32 are stored) need SciPy. The script computes the prices once and saves them to `lp_prices/lambda_hilo_nN.txt` for the next run.

**Reproduce the stored results.** Without `BITS`, the script works on `results/`. It skips every result that already exists, so it only reruns the cheap checks:

```bash
sh run_hilo.sh                                      # check results/ (a few seconds)
OUT=results_new sh run_hilo.sh                      # recompute 2–12, 16, 32 bits (about 20 min + 1 min proof)
OUT=results_new PROVE32=1 sh run_hilo.sh            # also the 32-bit proof (hours, many cores, several GB)
```

**Key options** (environment variables; the two optional arguments are the lookup table and the thread count):

| Variable | Default | Description |
|---|---|---|
| `BITS` | – | Widths to run, for example `8` or `"13 14 15"`. Sets the lists below. |
| `OUT` | `runs/nN` with `BITS`, else `results` | Output folder. Existing results in it are skipped. |
| `NOBUILD` | – | `1` = use the executables in `bin/` and do not compile |
| `PY` | `python3`, else `python` | Python interpreter |
| `CXX` | `g++` | C++ compiler |
| `EXACT_NS` | `2 … 12` | Widths for exact A* |
| `BEAM_NS` | `16 32` | Widths for beam search |
| `PROVE_NS` | `16` | Widths for the prover. The beam result is made first if it is missing. |
| `PROVE32` | – | `1` = add 32 to `PROVE_NS` |
| `LP_NS` | `8 12 16 32` | Widths for the LP lower bound |
| `ANY_NS` | `2 3 4 5` | Widths for the no-order check (very slow above 5 bits) |

The lists can be combined freely, for example `OUT=runs/beam20 EXACT_NS= BEAM_NS=20 PROVE_NS= LP_NS=20 ANY_NS= sh run_hilo.sh` runs only beam search and the LP bound for 20 bits.

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

Stored in `results/`:

| Bits | AUTO | HILO optimum | Adders | Method | States | Time | LP bound |
|---:|---:|---:|---:|---|---:|---:|---:|
| 2 | | 9 | 1 | A* | 3 | <0.1 s | |
| 3 | 44 | 39 | 1 | A* | 49 | <0.1 s | |
| 4 | 97 | 87 | 2 | A* | 815 | <0.1 s | |
| 5 | 164 | 150 | 3 | A* | 8,625 | <0.1 s | |
| 6 | 264 | 230 | 4 | A* | 44,056 | 0.3 s | |
| 7 | | 328 | 5 | A* | 151,484 | 2.4 s | |
| 8 | 478 | 442 | 5 | A* | 397,654 | 10 s | 395.9 |
| 9 | | 572 | 7 | A* | 878,770 | 36 s | |
| 10 | | 720 | 9 | A* | 1,853,443 | 1.2 min | |
| 11 | | 884 | 10 | A* | 3,537,471 | 3.3 min | |
| 12 | | 1,063 | 11 | A* | 6,023,031 | 7.0 min | 987.0 |
| 16 | 2,024 | 1,944 | 18 | beam + prover | 10,522,110 | 65 s (8 threads) | 1,840.9 |
| 32 | 8,462 | 8,081 | 52 | prover | 1,086,793,162 | 2.7 h (24 threads) | 7,883.8 |

- All costs are optimal under the column-sweep order. 13 to 15 bits are not stored; run them with `BITS="13 14 15" sh run_hilo.sh`.
- AUTO is the greedy synthesis: Table 4 of the draft for 3 to 6 bits, Table IV of the AUTO paper for 8 to 32 bits. It is a reference value.
- For A*, the states column counts the stored states. For the prover, it counts the expanded states.
- The A* times are on one core of a 2-core cloud machine. The 16-bit proof ran on an Intel Core Ultra 7 256V, the 32-bit proof on a 24-thread machine.

**16 bits.** Beam search gives 1,944 operations with 18 adders in 13 s. The prover with U = 1,943 finds no solution, so 1,944 is optimal (`results/proofs/prove16_hilo.txt`). It expanded 10.5 million states and kept at most 1.4 million (0.03 GB) in memory.

**32 bits.** Beam search gives 8,082 operations with 51 adders in 2.5 min. The prover with U = 8,082 finds a solution with 8,081 operations, which is the optimum (`results/proofs/prove32_hilo_ub8082.txt`). It expanded 1.09 billion states and kept at most 60 million (1.67 GB) in memory. A second run with U = 8,081 and `--keep` recovered the 52 adders of this solution (`results/proofs/prove32_hilo_ub8081_keep.txt`, `results/sequences/n32_hilo_prover.txt`).

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
