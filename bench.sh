#!/usr/bin/env bash
set -e

PORT_FC=6379
PORT_REDIS=6380
ITERS=3

# Workload matrix: payload size × concurrency × pipeline
WORKLOADS=(
  "small_c50_p1   -t set,get -n 1000000 -c 50  -P 1  -d 8"
  "small_c500_p1  -t set,get -n 1000000 -c 500 -P 1  -d 8"
  "small_c50_p16  -t set,get -n 1000000 -c 50  -P 16 -d 8"
  "med_c50_p1     -t set,get -n 200000  -c 50  -P 1  -d 1024"
  "large_c50_p1   -t set     -n 50000   -c 50  -P 1  -d 65536"
)

bench_one() {
  local label=$1; local port=$2; shift 2
  local args="$@"
  taskset -c 4 redis-benchmark -h 127.0.0.1 -p $port $args --csv 2>/dev/null \
    | awk -F',' -v l=$label '{ gsub(/"/,""); print l "," $1 "," $2 }'
}

echo "label,workload,op,rps"
for w in "${WORKLOADS[@]}"; do
  name=$(echo $w | awk '{print $1}')
  args=$(echo $w | cut -d' ' -f2-)
  for i in $(seq 1 $ITERS); do
    bench_one "fc_${name}_run${i}"    $PORT_FC    $args
    bench_one "redis_${name}_run${i}" $PORT_REDIS $args
  done
done
