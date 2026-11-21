#!/bin/sh
# Reproduces the results for the relaxed goal (every column ends with at most one bit).
# Run it from this folder:
#
#   sh run_hilo.sh [lookup.csv] [threads]
#
# Defaults: lookup table lookuptable_with_weights.csv, all cores. Output goes to results/ (set OUT to change
# it). Results that already exist are skipped, so on a fresh clone the script only reruns the cheap checks.
# To recompute everything, write to a new folder:  OUT=results_new sh run_hilo.sh
#
#   1. builds hilo_fast, hilo_any, and hilo_prove into bin/
#   2. exact A* for EXACT_NS                                (default 2 to 12 bits: about 12 minutes on one core)
#   3. beam search (B = 100) for BEAM_NS                    (default 16 and 32 bits: about 3 minutes)
#   4. proofs for PROVE_NS: the prover with bound = beam cost - 1
#                                                           (default 16 bits: about 1 minute on 8 threads)
#   5. 32-bit proof, only with PROVE32=1                    (hours on a many-core machine, several GB)
#   6. replay check of every sequence, LP bounds for LP_NS, check without the column-sweep order for ANY_NS,
#      results.csv
#
# Any bit width n >= 2 works. The simplest way to run chosen widths:
#   BITS=8 sh run_hilo.sh            every step for 8 bits, output in runs/n8
#   BITS="13 14 15" sh run_hilo.sh   every step for 13 to 15 bits, output in runs/n13_14_15
#
# Options (environment variables):
#   BITS="8"        run these widths only (sets the lists below; default output runs/nN)
#   OUT=dir         output folder (default results)
#   PY=python       Python interpreter (default python3, else python). The LP steps need NumPy and SciPy and
#                   are skipped with a message if SciPy is missing; the other steps need plain Python only.
#   CXX=g++         C++ compiler (default g++)
#   NOBUILD=1       use the executables already in bin/ (for example from tools/msvc/build.bat)
#   EXACT_NS="2 3"  bit widths for exact A*            (default 2 to 12; A* above 12 bits needs a lot of memory)
#   BEAM_NS="16"    bit widths for beam search         (default 16 32)
#   PROVE_NS="16"   bit widths for the prover          (default 16; each needs a beam result for the same width)
#   PROVE32=1       add 32 to PROVE_NS
#   LP_NS="8 16"    bit widths for the LP bounds       (default 8 12 16 32)
#   ANY_NS="2 3"    bit widths for hilo_any            (default 2 to 5; above 5 bits it is very slow)

cd "$(dirname "$0")" || exit 1
LUT=${1:-lookuptable_with_weights.csv}
T=${2:-$(nproc 2>/dev/null || echo 1)}
BIN=${BIN:-bin}
CXX=${CXX:-g++}
# BITS="8" or BITS="13 14": run every step for these widths only, into runs/nN (unless OUT is set).
# Exact A* runs up to 12 bits, beam + prover + LP bound for every width, hilo_any up to 5 bits.
if [ -n "$BITS" ]; then
  OUT=${OUT:-runs/n$(echo $BITS | tr ' ' '_')}
  EXACT_NS=""; ANY_NS=""
  for n in $BITS; do
    case $n in ''|*[!0-9]*) echo "BITS must be bit widths, for example BITS=\"8\" or BITS=\"13 14\""; exit 1 ;; esac
    [ "$n" -ge 2 ] || { echo "bit widths start at 2"; exit 1; }
    [ "$n" -le 12 ] && EXACT_NS="$EXACT_NS $n"
    [ "$n" -le 5 ] && ANY_NS="$ANY_NS $n"
  done
  BEAM_NS=$BITS; PROVE_NS=$BITS; LP_NS=$BITS
fi
OUT=${OUT:-results}
EXACT_NS=${EXACT_NS-"2 3 4 5 6 7 8 9 10 11 12"}
BEAM_NS=${BEAM_NS-"16 32"}
PROVE_NS=${PROVE_NS-"16"}
[ "$PROVE32" = 1 ] && PROVE_NS="$PROVE_NS 32"
LP_NS=${LP_NS-"8 12 16 32"}
ANY_NS=${ANY_NS-"2 3 4 5"}
seq_name() { printf "%s/sequences/n%02d_hilo_%s.txt" "$OUT" "$1" "$2"; }   # seq_name n method
PRICES=lp_prices
EXPECTED_SHA=c523cb859b8a03553ebceab3a90fd4bf087a7684673f8c86504a102140e7ad51

[ -f "$LUT" ] || { echo "lookup table not found: $LUT"; exit 1; }
if [ -z "$PY" ]; then
  if python3 -c "" 2>/dev/null; then PY=python3; else PY=python; fi
fi
sha=$(sha256sum "$LUT" 2>/dev/null | cut -d' ' -f1)
[ "$sha" = "$EXPECTED_SHA" ] || echo "warning: the lookup table differs from the one used for the reported results"

mkdir -p "$BIN" "$OUT/sequences" "$OUT/proofs"
SUMMARY=$OUT/proof_summary.txt
log() { echo "$(date '+%F %T')  $*"; }
has_scipy() { "$PY" -c "import scipy" 2>/dev/null; }
cost_of() { head -1 "$1" | sed -n 's/.*cost=\([0-9]*\).*/\1/p'; }

# ---- 1. build -----------------------------------------------------------------------------------
if [ "$NOBUILD" != 1 ]; then
  command -v "$CXX" >/dev/null 2>&1 || { echo "compiler $CXX not found. Install g++, or on Windows run tools/msvc/build.bat and then NOBUILD=1 sh run_hilo.sh"; exit 1; }
  log "building with $CXX"
  "$CXX" -O3 -march=native -std=c++17 -o "$BIN/hilo_fast" src/hilo_fast.cpp || exit 1
  "$CXX" -O3 -march=native -std=c++17 -o "$BIN/hilo_any" src/hilo_any.cpp || exit 1
  "$CXX" -O3 -march=native -std=c++17 -pthread -o "$BIN/hilo_prove" src/hilo_prove.cpp || exit 1
fi
FAST=$BIN/hilo_fast; ANY=$BIN/hilo_any; PROVE=$BIN/hilo_prove

# ---- 2. exact A* --------------------------------------------------------------------------------
for n in $EXACT_NS; do
  f=$(seq_name $n exact)
  [ -s "$f" ] && continue
  log "exact A*, $n bits"
  "$FAST" "$LUT" $n relaxed --quiet > "$f"
done

# ---- 3. beam search -----------------------------------------------------------------------------
beam() {   # beam N: beam search with B = 100, skipped if the result exists
  f=$(seq_name $1 beam100)
  [ -s "$f" ] && return
  log "beam search B = 100, $1 bits"
  "$FAST" "$LUT" $1 relaxed --beam 100 --quiet > "$f"
}
for n in $BEAM_NS; do beam $n; done

# ---- 4 and 5. proofs ----------------------------------------------------------------------------
# prove N: runs the prover with bound U = beam cost - 1 and --keep.
#   "no solution"  -> no solution costs U or less, so the beam result is optimal.
#   "optimum = X"  -> X is the optimum; its adder list goes to sequences/nNN_hilo_prover.txt.
prove() {
  n=$1
  beam=$(seq_name $n beam100)
  prover_seq=$(seq_name $n prover)
  lam=$PRICES/lambda_hilo_n$n.txt
  out=$OUT/proofs/prove${n}_hilo.txt
  grep -q RESULT "$out" 2>/dev/null && return
  [ -s "$prover_seq" ] && return
  beam $n                                   # the bound comes from the beam result
  grep -q "cost=[0-9]" "$beam" || { echo "skipping the $n-bit proof: no beam result in $beam"; return; }
  if [ ! -s "$lam" ]; then
    if has_scipy; then
      log "LP prices, $n bits"
      "$PY" src/lp_potential_relaxed.py "$LUT" $n "$lam" || return
    else
      echo "skipping the $n-bit proof: $lam is missing and SciPy is not installed"; return
    fi
  fi
  "$PY" -I src/check_prices.py "$LUT" "$lam" > "$OUT/proofs/check_prices_n$n.txt"
  grep -q "dual constraint violations: 0 " "$OUT/proofs/check_prices_n$n.txt" || { echo "the prices in $lam fail the exact check"; return; }
  ub=$(( $(cost_of "$beam") - 1 ))
  log "prover, $n bits, bound $ub, $T threads (progress in $out)"
  "$PROVE" "$LUT" $n relaxed --ub $ub --threads $T --lambda "$lam" --keep > "$out"
  res=$(grep RESULT "$out")
  echo "$res" >> "$SUMMARY"
  case "$res" in
    *"no solution"*)
      echo "n=$n: the beam result $(cost_of "$beam") is optimal" >> "$SUMMARY" ;;
    *"optimum ="*)
      opt=$(echo "$res" | sed 's/.*optimum = \([0-9]*\).*/\1/')
      exp=$(echo "$res" | sed -n 's/.*expanded=\([0-9]*\).*/\1/p')
      sec=$(echo "$res" | sed -n 's/.*time=\([0-9.]*\)s.*/\1/p')
      { echo "n=$n goal=relaxed method=prover cost=$opt states=$exp time=${sec}s"; grep '^   adder' "$out"; } > "$prover_seq"
      echo "n=$n: optimum $opt, below the beam result $(cost_of "$beam")" >> "$SUMMARY" ;;
    *)
      echo "n=$n: the prover did not finish; see $out" >> "$SUMMARY" ;;
  esac
}
for n in $PROVE_NS; do prove $n; done

# ---- 6. checks ----------------------------------------------------------------------------------
log "replaying every sequence"
for f in "$OUT"/sequences/*.txt; do
  grep -q "cost=" "$f" || continue
  n=$(basename "$f" | sed 's/^n0*\([0-9]*\)_.*/\1/')
  "$PY" -I src/verify_sequence.py "$LUT" $n relaxed "$f"
done > "$OUT/verification.txt" 2>&1
grep -v "^OK" "$OUT/verification.txt" && echo "warning: some sequences failed the replay check"

# LP bounds and the hilo_any check: each width is added once to its file, then skipped.
for n in $LP_NS; do
  grep -q "^n=$n goal" "$OUT/lp_bounds.txt" 2>/dev/null && continue
  if has_scipy; then
    log "LP bound, $n bits"
    "$PY" src/ilp_bound.py "$LUT" $n relaxed --lp-only >> "$OUT/lp_bounds.txt"
  else
    echo "skipping the $n-bit LP bound: SciPy is not installed"
  fi
done

for n in $ANY_NS; do
  grep -q "^n=$n goal" "$OUT/unrestricted_check.txt" 2>/dev/null && continue
  log "check without the column-sweep order, $n bits"
  "$ANY" "$LUT" $n relaxed --nopor 2>/dev/null | head -1 >> "$OUT/unrestricted_check.txt"
done

"$PY" -I src/collect_results.py "$OUT" > /dev/null
log "done; see $OUT/results.csv"
