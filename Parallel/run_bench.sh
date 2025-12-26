#!/bin/bash
SIZES="1024 2048 4096"
THREADS="1 2 4 8"
OUT=timings.csv
echo "mode,N,threads,time_ms" > $OUT

for N in $SIZES; do
  echo "Running size N=$N..."
  ./edge_sobel seq $N >> $OUT
  for t in $THREADS; do
    ./edge_sobel omp $N $t >> $OUT
  done
done

echo "All done! Results saved to timings.csv"

