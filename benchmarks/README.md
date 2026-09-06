# Module benchmark tool

`mmo_bench` measures the independently implemented protocol frame codec and
grid AOI module. It does not open sockets and is not an end-to-end epoll server
benchmark.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --config Release
```

## Run

```bash
./build/mmo_bench \
  --players 1000 \
  --moves-per-player 20 \
  --repetitions 3 \
  --payload-bytes 32 \
  --distribution uniform \
  --seed 20260902 \
  --output benchmarks/results/uniform-1000.json
```

Options:

- `--players`: number of players stored in the AOI world;
- `--moves-per-player`: measured moves per player in each repetition;
- `--repetitions`: number of deterministic repeated runs;
- `--payload-bytes`: opaque serialized body size for protocol frames;
- `--distribution`: `uniform` or `hotspot`;
- `--seed`: deterministic random seed;
- `--output`: JSON result path.

The hotspot workload places 80% of players in the central 20% × 20% map
region and keeps those players moving inside that region.

## Metrics

- protocol encoding and incremental decoding throughput;
- AOI movement throughput and P50/P95/P99 call latency;
- average recipients after an AOI move;
- recipient reduction relative to broadcasting to `N - 1` players;
- process peak resident memory.

The protocol benchmark feeds encoded frames to the incremental decoder in
pseudo-random chunks of 1–256 bytes, so both fragmented and combined frame
boundaries occur during the measured run. Protobuf parsing, socket I/O, epoll
scheduling and end-to-end response latency are outside the current scope.

See [the performance report](../docs/performance/report.md) for the measured
environment, results and limitations.
