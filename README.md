# FlashCache

A small in-memory key-value server in C++. Speaks the Redis RESP protocol
over TCP. Single-threaded, epoll-based. Supports `PING`, `SET`, `GET`.

Built as a learning project, then benchmarked against a real Redis-compatible
server to see how close a single-developer implementation can get.

## Performance

Tested against `redis-server` (actually Valkey 8.0.7, the open-source fork
of Redis after the license change — same protocol, same `redis-benchmark`
client) on the same machine. Both servers pinned to a CPU core. Persistence
on the comparison server was disabled (`--save "" --appendonly no`) so the
comparison is on the same code path: parse a RESP command, look up a key,
write the reply.

### Throughput (requests/sec, median of 3 runs)

| Workload                      |  FlashCache |   Valkey |
|-------------------------------|------------:|---------:|
| SET 8 B,  50 clients          |      79,662 |   82,474 |
| GET 8 B,  50 clients          |      80,887 |   79,536 |
| SET 8 B, 500 clients          |      69,901 |   76,225 |
| GET 8 B, 500 clients          |      75,222 |   70,781 |
| SET 8 B,  pipeline 16         |   1,222,494 | 1,104,972|
| GET 8 B,  pipeline 16         |   1,228,501 | 1,215,067|
| SET 1 KB, 50 clients          |      81,566 |   71,633 |
| GET 1 KB, 50 clients          |      81,136 |   80,873 |
| SET 64 KB, 50 clients         |      37,037 |   37,908 |

FlashCache matches Valkey within ±5% on most workloads, and is about 11%
faster on pipelined writes. The 1 KB SET row is partly real and partly
Valkey-side variance (Valkey's three SET runs at 1 KB were 57k / 72k / 82k;
FlashCache's were stable at 80–82k). Run-to-run variance on this hardware
is around 3–5%.

### Latency (ms, GET 8 B, 50 clients, 1 M requests)

|         | FlashCache | Valkey |
|---------|-----------:|-------:|
| p50     |       0.31 |   0.31 |
| p99.2   |       0.36 |   0.42 |
| p99.9   |       0.46 |   0.52 |
| max     |       0.86 |   2.68 |

p50 is identical. FlashCache has a smaller tail — p99.9 is about 12%
lower, worst-case is roughly 3× lower. The likely reason is that Valkey
pauses briefly for background work (key-expiration sampling, slowlog,
dirty-page tracking) that FlashCache simply doesn't have to do.

## What's not implemented

This is a focused subset. The fair comparison above is fair only because
both servers are doing the same work on `SET` / `GET`. Valkey/Redis has
all of these and FlashCache does not:

- Persistence (RDB or AOF)
- Replication, clustering
- TTL / expiration / eviction
- Other data types (hashes, lists, sets, sorted sets)
- Scripting, pub/sub, streams

Some of the latency wins above are because FlashCache isn't carrying the
overhead of those features. That's the honest read.

## How it works

- Single thread, single epoll loop, edge-triggered
- One read buffer per client, grows on demand from 8 KB up to a 64 MB cap
- One write buffer per client; `EPOLLOUT` is armed only when the kernel
  send buffer fills, so partial writes don't drop bytes
- RESP parser with bounds checking and explicit protocol-error close
- Heterogeneous-aware lookups: `SET` / `GET` use `string_view` against the
  underlying `unordered_map<std::string, std::string>` to avoid extra
  string copies on the hot path
- Graceful shutdown via `signalfd` on `SIGINT` / `SIGTERM`

## Build and run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/flash_cache       # listens on 6379
```

Then in another shell:

```bash
redis-cli -p 6379 SET foo bar
redis-cli -p 6379 GET foo
```

Requires: Linux, gcc/clang with C++20, CMake.

## Testing

```bash
./build/flash_test        # gtest suite (parser, etc.)
```

## Reproduce the benchmarks

```bash
sudo cpupower frequency-set -g performance
pkill -9 flash_cache redis-server 2>/dev/null

taskset -c 2 ./build/flash_cache > /tmp/fc.log 2>&1 &
redis-server --port 6380 --daemonize yes --save "" --appendonly no
sleep 0.5

./bench.sh > results.csv          # 5 workloads x 2 servers x 3 runs
cat results.csv

pkill flash_cache
redis-cli -p 6380 shutdown nosave
```

`bench.sh` runs `redis-benchmark` pinned to core 4 against both servers
across the workload matrix. The server is pinned to core 2.

## Test environment

- CPU: AMD Ryzen 5 5600H (12 logical cores, performance governor)
- Server pinned to core 2, client pinned to core 4 (`taskset`)
- OS: Fedora Linux 42 (kernel 6.19.11)
- Compiler: gcc 15.2.1, `-O3 -march=native`
- CMake: 3.31.11
- Comparison server: Valkey 8.0.7 (`redis-server --version` output)

## Roadmap

- `DEL` and TTL / expiration
- Eviction policy when memory bound is hit
- Slab allocator (driven by profiling, not speculation)
- Multi-threaded I/O via `SO_REUSEPORT`

## License

MIT — see [LICENSE](LICENSE).
