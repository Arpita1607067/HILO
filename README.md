# Column-sweep search for the HILO adder library

This package finds minimum-cost decompositions of the product space of an n-bit multiplication with the adder library in `lookuptable_with_weights.csv`. It replaces the search in `bidirectional_a_star_with_backup_sqllite.ipynb`. It reproduces the optimal costs in Table 4 of the draft (42, 88, 152, and 233 operations for 3- to 6-bit operands) and extends the exact results to 16-bit operands.

## Cost model

The cost model is the same as in the notebook. A state is the number of bits in each column of the product space, with the least significant column first. An adder v placed at column k consumes v[j] bits from column k+j. It returns W bits, one to each of the columns k to k+W-1, where W is the `weight` column of the lookup table. The cost of the adder is the `Cost` column. All output bits stay within columns 0 to 2n.

Two final states are supported.

- `paper`: every column 0 to 2n holds exactly one bit. This matches the goal test of the notebook and the costs in Table 4 of the draft.
- `relaxed`: every column holds at most one bit. This is the usual definition of a fully reduced product space. Column 2n of an n-bit product is always zero, so it may stay empty. The `paper` goal has no solution for 2-bit operands; the `relaxed` goal does.

## Why the search is small

Every adder in the library needs at least two bits in its lowest column. The search therefore always places the next adder at the lowest column k that holds two or more bits. This column-sweep order has two consequences.

1. The columns below k never change again, since no adder can be placed there.
2. An adder covers at most six columns (five input columns and up to six output bits). Hence, the columns above k+5 still hold their initial bits.

A state is therefore fully described by k and the six column heights s[k], ..., s[k+5]. It is packed into one 64-bit word. The search uses A* with the heuristic h = ceil(rho * max(0, B - (2n+1-k))), where B is the number of bits in columns k and above. Here, rho = 312/38 = 8.21 is the lowest cost per removed bit in the library, which belongs to adder (33,7,4). Each adder removes its input count minus W bits, so h is admissible and consistent. The first final state that A* expands is optimal.

The column-sweep order is a restriction of the search space. `hilo_any` places adders at any column and finds the same optimal costs for 2- to 6-bit operands.

## Programs

| File | Purpose |
|---|---|
| `src/hilo_fast.cpp` | Column-sweep search. Exact A* (default), weighted A* (`--w`), and beam search (`--beam B`). |
| `src/hilo_any.cpp` | Exact search without the column-sweep order. Used only to check the restriction on small operands. |
| `src/hilo_prove.cpp` | Column-sweep prover: proves that a bound is optimal or finds the optimum below it. Runs on several threads and can use LP column prices. |
| `src/lp_potential.py` | Computes the LP column prices for `hilo_prove --lambda` and checks them exactly against every adder placement. |
| `src/suffix_states.py` | Lists the intermediate states of a sequence, for exact re-optimization of its last part with `hilo_prove --start`. |
| `src/hilo_bidir.cpp` | Forward, backward, and bidirectional search (bidirectional A* and MM) on the column-sweep states. |
| `src/hilo_sweep.py` | Readable Python version of the exact column-sweep search (same results, about 300x slower). |
| `src/verify_sequence.py` | Replays a printed execution sequence against the adder library and checks the final state and the cost. |
| `src/ilp_bound.py` | LP lower bound on the cost, valid for any execution order (`--lp-only`). The ILP option is slow beyond 4 bits. |
| `src/collect_results.py` | Collects the outputs into `results/results.csv`. |
| `run_all.sh` | Builds the programs and reproduces every result in `results/` (about 45 minutes on one core). |
| `run_proof.sh` | Runs the 32-bit optimality proof in stages on a many-core machine and resumes after an interruption. |

Build and run:

```
g++ -O3 -march=native -std=c++17 -o hilo_fast src/hilo_fast.cpp
./hilo_fast lookuptable_with_weights.csv 8 paper            # exact, 8 s
./hilo_fast lookuptable_with_weights.csv 16 paper --beam 100   # beam search, 22 s
./hilo_fast lookuptable_with_weights.csv 8 paper --h0        # no heuristic (ablation)
./hilo_fast lookuptable_with_weights.csv 8 paper --init 1,1,1,1,3,3,3,3,3,3,3,3,1,1,1,1,0   # second stage after four 4-bit macro adders
python3 src/verify_sequence.py lookuptable_with_weights.csv 16 paper results/sequences/n16_paper_beam100.txt
```

Each run prints the cost, the number of expanded and stored states, the runtime, the root lower bound, and the execution sequence (adder, output width, cost, and column).

## Results

All `paper` results up to 16 bits are optimal under the column-sweep order. A* (`hilo_fast`) solves up to 12 bits. The column-sweep prover (`hilo_prove`, next section) solves 13 to 16 bits. Rows marked beam use B = 100 and are not proven optimal. AUTO is the greedy synthesis (Table 4 of the draft for 3 to 6 bits, Table IV of the AUTO paper for 8 to 32 bits).

| Bits | AUTO | Draft HILO | `paper` goal | `relaxed` goal | Method (`paper`) | States (`paper`) | Time (`paper`) | LP bound (`paper`) |
|---:|---:|---:|---:|---:|---|---:|---:|---:|
| 2 | | 9 | none | 9 | A* | 2 | <0.1 s | |
| 3 | 44 | 42 | 42 | 39 | A* | 49 | <0.1 s | |
| 4 | 97 | 88 | 88 | 87 | A* | 817 | <0.1 s | |
| 5 | 164 | 152 | 152 | 150 | A* | 9,276 | <0.1 s | |
| 6 | 264 | 233 | 233 | 230 | A* | 49,346 | 0.2 s | |
| 7 | | | 331 | 328 | A* | 168,833 | 1.5 s | |
| 8 | 478 | 452 | 445 | 442 | A* | 446,713 | 8 s | 399.0 |
| 9 | | | 575 | 572 | A* | 977,560 | 26 s | |
| 10 | | | 722 | 720 | A* | 1,978,586 | 1.7 min | |
| 11 | | | 886 | 884 | A* | 3,766,707 | 5.7 min | |
| 12 | | | 1,065 | 1,063 | A* | 6,406,126 | 10.3 min | 990.0 |
| 13 | | | 1,260 | | prover | 5,235,977 | 3.0 min | |
| 14 | | | 1,473 | | prover | 8,381,274 | 5.8 min | |
| 15 | | | 1,701 | | prover | 13,388,150 | 12.6 min | |
| 16 | 2,024 | 1,940 | 1,946 | 1,944 (beam) | prover | 19,479,263 | 21.9 min | 1,843.9 |
| 32 | 8,462 | 8,195 | 8,083 (beam) | 8,082 (beam) | beam, B = 300 | 18,005,980 | 5.3 min | 7,886.8 |

For A*, the states column counts the stored states. For the prover, it counts the expanded states; each state is expanded once. The prover also confirms 1,065 at 12 bits with 3.1 million expanded states in 91 s, against 10.3 minutes for A*.

Beam search with B = 100 returns the optimum for 8 to 12, 14, and 16 bits. It returns one operation more for 13 and 15 bits (1,261 and 1,702). For 32 bits, the prover shows that no solution costs 8,014 or less, so the beam result of 8,083 is at most 0.85% above the optimum (next sections).

Heuristic ablation (`--h0`, `paper` goal): without the heuristic, the search stores 0.54 million states and takes 11.8 s for 6 bits, and 3.6 million states and 303 s for 7 bits. With the heuristic, it stores 0.05 and 0.17 million states and takes 0.2 s and 1.5 s.

Search without the column-sweep order (`hilo_any`): the same optimal costs for 2 to 5 bits under both goals, and for 6 bits under the `paper` goal. It stores 0.21 million states for 5 bits (6.5 s) and 14.9 million states for 6 bits (60 min), instead of 9,276 and 49,346 states.

Macro-adder tiling as in Section 6 of the draft (`--init` with the bits returned by 4-bit macro adders, 88 operations each, 8 output bits): the second stage costs 151 (8 bits, exact), 863 (16 bits, beam), and 3,862 (32 bits, beam). The totals are 503, 2,271, and 9,494 operations. A simple bound, rho times the number of bits that the second stage must remove, already gives totals of at least 476, 2,188, and 9,303.

All sequences in `results/sequences` pass `verify_sequence.py`. Runtimes were measured on one core of a 2-core cloud machine.

## Proving optimality up to 16 bits

`src/hilo_prove.cpp` takes an upper bound U, for example the beam result minus one. It either finds the optimum at or below U, or proves that no solution with cost U or less exists. In the second case, the beam result is optimal.

- The column-sweep state graph has no cycles. A move either lowers s[k] at the same column or moves to a higher column. The prover visits the states in the order (k ascending, s[k] descending), so every state is expanded once, with its optimal cost.
- Every state with g + h > U is discarded. A finished column is never visited again, so its states can be freed. The live states fit in 0.06 GB at 16 bits. With `--keep`, finished columns stay in memory (below 1 GB at 16 bits), and the sequence of the best solution is recovered backward.
- A successor through adder a satisfies f' >= f + ceil(cost(a) - rho * r(a)) - 1, where r(a) is the number of bits that a removes. The adders of each list are sorted by this increment, so the scan of a state stops once the increment exceeds the slack U - f.

- `--threads T` expands the states of one (k, s[k]) bucket in parallel. Each thread writes its successors to its own buffers, and the buffers are then merged shard by shard, so no two threads write the same table. Two threads give a 2.0x speedup at 12 bits with the same expanded states.
- `--lambda file` adds a second lower bound, h = sum_c lambda[c] * (s[c] - 1), with column prices lambda from the dual of the LP relaxation (`src/lp_potential.py`). The prices are scaled to integers and checked exactly against every adder placement, so the bound is consistent. It equals the LP bound at the initial state. With it, the prover expands 2.1 to 2.3 times fewer states at 12 to 14 bits and reproduces the same optima. Only the `paper` goal is supported.
- `--start k,s0,...,s5,g` starts the sweep from an intermediate state of a known sequence that was reached with cost g. The run then finds the exact best completion of that prefix.

```
./hilo_fast lookuptable_with_weights.csv 16 paper --beam 100 --quiet     # 1,946 in 22 s
./hilo_prove lookuptable_with_weights.csv 16 paper --ub 1945 --keep      # no solution <= 1,945, 22 min
python3 src/lp_potential.py lookuptable_with_weights.csv 16 lambda_n16.txt
./hilo_prove lookuptable_with_weights.csv 16 paper --ub 1945 --threads 2 --lambda lambda_n16.txt
```

The logs of the 13- to 16-bit runs are in `results/proofs`.

## 32 bits

The best 32-bit solution has 8,083 operations (beam search with B = 300, `results/sequences/n32_paper_beam300.txt`). The prover with the LP prices raises the lower bound step by step. Each run that ends without a solution proves that the optimum is higher than its bound.

| Bound U | Expanded states | Time (2 threads) | Result |
|---:|---:|---:|---|
| 7,974 | 3,979,644 | 78 s | no solution, optimum >= 7,975 |
| 7,994 | 15,078,704 | 7.3 min | no solution, optimum >= 7,995 |
| 8,014 | 46,948,105 | 30 min | no solution, optimum >= 8,015 |

The 32-bit optimum therefore lies between 8,015 and 8,083, and the beam result is at most 0.85% above it. The exact re-optimization of the last 11 adders of the 8,083 solution (`--start` after adder 40) finds no better completion.

Each step of 20 operations costs about 3 to 4 times more states and 4 to 6 times more time. The growth slows from step to step. A full proof needs a bound of 8,082, which is 3.4 more steps. Extrapolated, that is roughly 0.7 to 2 billion states and 13 to 60 hours on two cores. The run therefore suits a many-core server.

`run_proof.sh` runs the whole proof:

```
nohup sh run_proof.sh /path/to/lookuptable_with_weights.csv 64 > proof32.log 2>&1 &
```

- It builds `hilo_prove`, checks the SHA-256 of the lookup table, and uses the prices in `results/lp_prices/lambda_n32.txt` (no SciPy needed).
- It runs the prover with the bounds 8,034, 8,054, 8,074, and 8,082. Each run that ends without a solution raises the certified lower bound, so partial progress is never lost. The logs go to `results/proof_n32/`, one line per finished column.
- If a run finds a solution, its cost is the exact optimum. The script then reruns that bound with `--keep`, recovers the sequence, and checks it with `verify_sequence.py`.
- If no run finds a solution, the 8,083-operation solution is optimal.
- When restarted, the script skips the bounds that already finished.
- The second argument is the number of threads. The parallel prover was checked for identical results with 1 to 16 threads, but its speedup was measured only on two cores (2.0x).
- Memory stays small without `--keep` (0.12 GB at 8,014; a few GB expected at 8,082). The `--keep` rerun stores every state and may need tens of GB.

Test the script on 13 bits first, which takes about a minute:

```
N=13 BOUNDS="1240 1255 1260" BEST=results/sequences/n13_paper_exact.txt sh run_proof.sh /path/to/lookuptable_with_weights.csv 2
```

## Bidirectional search

`src/hilo_bidir.cpp` adds a backward search on the same states. A backward move undoes the last adder of a column-sweep sequence at a column b <= k. The predecessor must again be a column-sweep state, so the columns above b+5 must hold their initial bits. Both directions therefore use the same 64-bit state words. The backward heuristic is the larger of two bounds: rho times the number of bits removed since the start, and 5 operations for each carry bit that must have arrived in a column. The program runs forward A* (`fwd`), backward A* (`bwd`), bidirectional A* that expands the side with the smaller open list (`ba`), and MM (`mm`, Holte et al., AAAI 2016).

| Bits | Forward A* | Bidirectional A* | MM |
|---:|---:|---:|---:|
| 5 | 4,024 exp., 0.02 s | 4,024 + 1 exp., 0.01 s | 1,808 + 5,219 exp., 1.6 s |
| 6 | 20,051 exp., 0.16 s | 20,051 + 8 exp., 0.14 s | 10,229 + 52,803 exp., 25 s |
| 7 | 69,708 exp., 0.97 s | 69,708 + 15 exp., 1.00 s | 38,654 + 354,806 exp., 272 s |
| 8 | 187,180 exp., 4.5 s | 187,180 + 33 exp., 4.5 s | stopped after 25 min, lower bound 418 of 445 |

Expansions are listed as forward + backward. All completed runs return the optimal cost, and the joined sequences pass `verify_sequence.py`. The times of forward A* and bidirectional A* are medians of five runs. Single runs on this machine vary by about 15%, so the two are equal within the measurement noise.

MM works as intended on the forward side. It expands only the forward states with g below half of the optimal cost, which are about half of the states that forward A* expands. The backward side, however, needs 2.4 to 11 times more expansions than the forward expansions it replaces. Each backward expansion also costs about 60 times more than a forward one, since any of the 12,754 adders can be undone at up to six columns as long as its output bits are present. The backward heuristic cannot tell whether a predecessor is reachable from the start. Bidirectional A* sees a large backward open list after the first expansion, so it almost always expands the forward side and reduces to forward A*. It performs the same forward expansions as forward A*. At 8 bits it stores 21% fewer forward states, because its backward side finds a solution with the optimal cost early and the forward side then drops states that cannot beat it. Forward A* with the beam-search result as an upper bound (`--ub 445`) stores almost as few (197,156 instead of 191,645).

## Differences from the notebook

- The notebook tries every column for every adder. The column-sweep order places each adder at a single column, which removes most duplicate states. The saved notebook log reports 6.2 million duplicates for 164,000 expansions.
- The forward heuristic of the notebook is the bit deficit below 2n+1. It is zero for every state with at least 2n+1 bits, so the forward side is a Dijkstra search. `hilo_fast` uses the excess-bit heuristic above.
- The stopping test of the notebook uses only the forward frontier. Hence, the backward side cannot shorten the search. `hilo_fast` is a unidirectional A* search.
- The notebook keeps its open list in SQLite. `hilo_fast` keeps each state as one 64-bit word in memory and uses a bucket queue indexed by the integer f-value.

## Notes

- The costs count the adders only. The generation of the partial products (AND gates) is not included.
- Outputs are limited to columns 0 to 2n. Allowing outputs in up to three more columns, which only hold structurally zero bits, does not change the costs at 8 and 16 bits.
- Beam search keeps the best B states of each (k, s[k]) bucket in the column-sweep order. It gives an upper bound without an optimality guarantee. The LP bound gives a lower bound.
