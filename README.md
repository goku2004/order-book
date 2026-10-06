# Low-Latency Matching Engine

A C++20 electronic order matching engine developed as a
systems-engineering study project.

The project progressively explores:

- data-oriented design
- CPU cache behavior
- memory allocation
- custom memory pools
- lock-free data structures
- C++ memory ordering
- cache coherence
- false sharing
- CPU affinity
- NUMA
- OS scheduling
- low-latency benchmarking
- hardware performance counters

The initial implementation is a single-threaded reference
matching engine using price-time priority.

Performance optimizations are introduced incrementally and
validated through reproducible benchmarks.