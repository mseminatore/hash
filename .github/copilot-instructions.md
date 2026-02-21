# Copilot Instructions — libht (hash table)

## Build & Test

This is a C project using CMake. The `testy` test framework is a git submodule.

```sh
# Configure and build (Unix)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug

# Configure and build (Windows / Visual Studio)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug

# Run all tests
ctest --test-dir build -C Debug --rerun-failed --output-on-failure

# Run the test executable directly
build/Debug/ht_test          # Windows
./build/ht_test              # Unix
```

There is also a Unix `Makefile` (`make && make test`), but CMake is the primary build system and what CI uses.

Individual tests cannot be run in isolation — `test_main()` in `test.c` runs all suites sequentially and some depend on shared state.

## Architecture

- **`hash.h` / `hash.c`** — The library. A single-header, single-source open-addressing hash table with CPython-style probing and opaque `const void *` keys and values.
- **`test.c`** — Test suite using the `testy` framework (git submodule in `testy/`). Uses `MODULE`, `SUITE`, `TEST`, and `TESTEX` macros.
- **`words_alpha.txt`** — Word list used by the `test_big_words` stress test.
- **`devspec.md`** — Detailed developer specification with known issues, API docs, and improvement roadmap. Read this before making design changes.

### How the hash table works

- Open-addressing with CPython-style probe sequence: `bin = (5 * bin + perturb + 1) & mask`, where perturb is derived from the hash and right-shifted each step.
- Deleted entries are marked with a tombstone sentinel pointer (no extra struct field). Tombstones keep probe chains intact and are reused by subsequent inserts. They are cleaned up automatically during resize/rehash.
- Empty slot sentinel: `key == NULL`. Tombstone sentinel: `key == HT_TOMBSTONE` (a unique internal address).
- Starts with a small embedded array (`small_table`) on the struct — no heap allocation until growth.
- Auto-grows at ~50% load factor (`2 * entries >= size`).
- A free-list of `HashTable` structs (`ht_free_list`) avoids repeated malloc/free.
- NULL values are supported. Use `ht_contains` or `ht_lookup` to distinguish "not found" from "found with NULL value" (since `ht_find` returns NULL in both cases).

### Key/value ownership

The table stores pointers only and **never frees keys or values**. Callers own the memory. This is the most important API contract.

## Conventions

- **Return values**: `HT_OK` (1) for success, `HT_FAIL` (0) for failure. Pointer-returning functions return `NULL` on failure.
- **`CHECK_THAT` macro**: Guards function preconditions. Returns 0/NULL on invalid input (asserts in `_DEBUG` builds).
- **Hash/compare functions**: Default is pointer-identity hashing and pointer-equality comparison. For string keys, set both `ht_set_hash_func(ht, HT_HASH_STRING)` and `ht_set_compare_func(ht, compare)` — always set both together.
- **Compile-time config macros** in `hash.h`: `HT_DEFAULT_TABLE_SIZE`, `HT_PERTURB_VALUE`, `HT_INV_LOAD_FACTOR`, `HT_ALLOC`/`HT_FREE`, `HT_TRACK_STATS`.
- **Internal config** in `hash.c`: `HT_AUTO_GROW`, `HT_LINEAR`, `HT_PERTURB`, `HT_DEBUG_STATS`.
- Table sizes must always be powers of 2 (the mask-based indexing depends on this).
- Call `ht_finished()` at program exit to drain the free-list.
