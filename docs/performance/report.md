# Protocol and AOI module performance report

## 1. Scope

This report measures the independently implemented frame codec and grid AOI
module. It is an in-process module benchmark, not an end-to-end MMO server
benchmark.

The following are not included:

- TCP connection establishment or socket I/O;
- epoll event scheduling;
- Protobuf serialization or parsing;
- client-to-server response latency;
- timer wheel performance;
- MySQL or Redis access.

The results therefore must not be described as server QPS, network throughput
or online-player capacity.

## 2. Environment

| Item | Value |
| --- | --- |
| Date | 2026-09-02 |
| Operating system | Microsoft Windows 10.0.19045, x64 |
| Processor | 12th Gen Intel Core i5-12600KF |
| Logical processors | 16 |
| Compiler | MinGW-w64 GCC 8.1.0 |
| C++ mode | C++17 |
| Optimization | `-O2` |
| Random seed | `20260902` |

The project targets Linux, but this module report was produced on the available
Windows development host. It remains a reproducible algorithm baseline; Linux
and end-to-end server results must be reported as separate experiments.

## 3. Method

### 3.1 Protocol frame workload

- Frame format: 4-byte payload length + 4-byte message ID + payload;
- byte order: network byte order;
- payload size: 32 bytes;
- frames per case: 60,000;
- decoder input chunk: pseudo-random 1–256 bytes;
- measured work: frame encoding and incremental frame extraction;
- excluded work: Protobuf parsing and network I/O.

Six cases execute the same protocol workload. The reported protocol summary
uses the median across the six runs to reduce the effect of a single short run.

### 3.2 AOI workload

- map: 1000 × 2000 units;
- grid: 10 columns × 20 rows;
- interest area: current grid and eight neighboring grids;
- measured moves per case: 60,000;
- repetitions per case: 3;
- maximum movement on each axis: 75 units;
- distributions: uniform and 80% central hotspot.

For every movement, the tool measures `AoiWorld::MovePlayer` and records the
new-view recipients as `entered + stayed`. Full broadcast recipients are
`player_count - 1`.

Recipient reduction is calculated as:

```text
1 - average AOI recipients / full broadcast recipients
```

P99 is the 99th percentile of an in-process `MovePlayer` call, measured in
microseconds. It is not network response latency.

## 4. Results

### 4.1 Protocol frame codec

| Metric | Median of six runs |
| --- | ---: |
| Encoding throughput | approximately 16.5 million frames/s |
| Incremental decoding throughput | approximately 67.0 million frames/s |

These values measure small in-memory frames. They are useful as a regression
baseline for the codec only and cannot be used as server throughput.

### 4.2 AOI movement

| Distribution | Players | AOI operations/s | P99 (µs) | Average AOI recipients | Full broadcast recipients | Recipient reduction |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Uniform | 100 | 653,618 | 3.2 | 3.84 | 99 | 96.12% |
| Uniform | 500 | 142,041 | 16.2 | 19.73 | 499 | 96.05% |
| Uniform | 1,000 | 77,608 | 29.9 | 39.83 | 999 | 96.01% |
| Hotspot | 100 | 88,755 | 28.3 | 37.69 | 99 | 61.93% |
| Hotspot | 500 | 15,967 | 138.8 | 199.98 | 499 | 59.92% |
| Hotspot | 1,000 | 7,480 | 302.2 | 404.92 | 999 | 59.47% |

Peak resident memory reported by these cases was approximately 7.3 MB. It
includes the benchmark process and its protocol byte buffer, not a complete
server with client connections.

Raw results:

- [uniform, 100 players](../../benchmarks/results/uniform-100.json)
- [uniform, 500 players](../../benchmarks/results/uniform-500.json)
- [uniform, 1,000 players](../../benchmarks/results/uniform-1000.json)
- [hotspot, 100 players](../../benchmarks/results/hotspot-100.json)
- [hotspot, 500 players](../../benchmarks/results/hotspot-500.json)
- [hotspot, 1,000 players](../../benchmarks/results/hotspot-1000.json)

## 5. Findings

1. Under uniform placement, the 10 × 20 grid reduces average movement
   recipients by approximately 96% for all three player counts.
2. Under hotspot placement, recipient reduction falls to approximately
   59%–62%. AOI still removes unrelated recipients, but its benefit is much
   smaller when many players occupy nearby grids.
3. AOI P99 increases with neighborhood density. At 1,000 players it rises from
   29.9 µs under uniform placement to 302.2 µs under the hotspot workload.
4. The current implementation materializes ordered sets for visible players
   and set differences. The density-sensitive result is evidence for a future
   profiling target, not proof that a particular container replacement will be
   faster.
5. The earlier 95.95% synthetic recipient result is consistent with this
   uniform workload, but it remains a broadcast-recipient reduction rather
   than an end-to-end performance improvement percentage.

## 6. Reproduction and interpretation rules

- Use Release optimization and the same seed when comparing code changes.
- Keep the map, grid, movement radius and distribution unchanged for a valid
  before/after comparison.
- Re-run on an otherwise idle machine when collecting resume data.
- Do not compare Windows and Linux measurements as if the environments were
  identical.
- Do not call module throughput server QPS.
- Do not call `MovePlayer` P99 client response latency.

## 7. Optional next benchmark stage

The independent epoll server is now implemented and covered by loopback
integration tests. A separate TCP load generator can be added when end-to-end
capacity numbers are needed. That experiment should measure:

- connection success rate;
- end-to-end movement response P50/P95/P99;
- server CPU and resident memory;
- socket bytes and error rate;
- slow-client behavior and send-buffer backlog;
- AOI behavior under the same uniform and hotspot workloads.
