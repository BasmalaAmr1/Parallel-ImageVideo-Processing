#!/usr/bin/env bash
# run_bench_mpi.sh
set -e
BIN=./sobel_mpi
OUT=timings_mpi.csv
echo "mode,N,P,time_ms" > $OUT

# Strong scaling: fixed size
N=4096
for P in 1 2 4 8; do
  echo "Running strong: N=$N P=$P ..."
  out=$(mpirun -np $P $BIN $N | grep MODE)
  # parse time_ms=
  time_ms=$(echo $out | sed -n 's/.*time_ms=\([0-9.]*\).*/\1/p')
  echo "strong,$N,$P,$time_ms" >> $OUT
done

# Weak scaling: keep work per rank approx constant; base N0=1024 for P=1
N0=1024
for P in 1 2 4 8; do
  N=$(python3 - <<PY
P=$P
N0=1024
# scale N so N^2 / P = N0^2
import math
N = int(round(math.sqrt(P*(N0**2))))
print(N)
PY
)
  echo "Running weak: N=$N P=$P ..."
  out=$(mpirun -np $P $BIN $N | grep MODE)
  time_ms=$(echo $out | sed -n 's/.*time_ms=\([0-9.]*\).*/\1/p')
  echo "weak,$N,$P,$time_ms" >> $OUT
done
echo "Done. Results in $OUT"
