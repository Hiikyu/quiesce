# quiesce

`quiesce` is a C++ library that provides lock-free concurrent primitives and algorithms. The library is intended to support high-performance linking code by exposing safe lock-free data structures such as:

- lock-free skip lists
- lock-free hash tables
- other lock-free algorithms and containers

The library also includes safe memory reclamation strategies for concurrent data structures, such as:

- epoch-based reclamation (EBR)
- hazard pointers (Hazptr)

`quiesce` is designed to expose both:

- high-level lock-free primitives for application code
- low-level reclamation interfaces for direct use in custom linking code

## Build

This repository uses CMake and supports a vcpkg-based workflow.

### Configure and build

```bash
cmake --preset vcpkg
cmake --build out/build/vcpkg
```

### Run tests

```bash
cd out/build/vcpkg
ctest --output-on-failure
```

## Dependencies

Dependencies are managed through `vcpkg` and declared in `vcpkg.json`.

- `gtest` for unit tests
- `jemalloc` for memory management benchmarks and experiments

## Notes

- The current implementation includes a minimal example of the library and test harness.
- Future work will expand the lock-free primitive set and build a reusable reclamation layer.
