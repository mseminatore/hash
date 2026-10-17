# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test

C project; CMake is the primary build system (CI runs Linux + Windows × Debug + Release, plus a Linux build with `HT_TRACK_STATS=0`, and also builds the `bench` target). The `testy` test framework is a git submodule — run `git submodule update --init` if `testy/` is empty.

```sh
# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Debug                      # Unix
cmake -S . -B build -G "Visual Studio 17 2022" -A x64        # Windows
cmake --build build --config Debug

# Run tests
ctest --test-dir build -C Debug --output-on-failure
build/Debug/ht_test      # Windows, direct
./build/ht_test          # Unix, direct
```

Load-factor benchmarks (not built by default, not run by ctest): `cmake --build build --config Release --target bench`, then run `build/Release/bench_1_2`, `bench_2_3`, `bench_3_4` from the repo root.

A Unix `Makefile` also exists (`make && make test`) and builds `libht.a` + `ht_test`.

Individual tests cannot be run in isolation: `test_main()` in `test.c` runs every suite sequentially (some share global state) and calls `ht_finished()` at the end. To focus on one suite, temporarily comment out the others in `test_main()`. Tests use testy's `MODULE`, `SUITE`, `TEST`, `TESTEX` macros. `test_big_words` loads `words_alpha.txt`, probing several relative paths so it works from the repo root or a build subdirectory.

## Architecture

The whole library is `hash.h` + `hash.c` (built as static lib `ht`). Plans and known issues are tracked in GitHub issues.

- **Open addressing, CPython-style probing**: `bin = (5 * bin + perturb + 1) & mask`, with `perturb` seeded from the hash and right-shifted each step. Table sizes must always be powers of 2 (mask-based indexing).
- **Slot sentinels, no extra struct fields**: empty slot is `key == NULL`; deleted slot is `key == HT_TOMBSTONE` (address of an internal static). Tombstones keep probe chains intact, are reused by inserts, and are dropped on resize/rehash. The `HASH_MATCH`/`HASH_EMPTY`/`HASH_DELETED` macros in `hash.c` encode this — any probe-loop change must respect both sentinels.
- **Small-table optimization**: each `HashTable` embeds `small_table[HT_DEFAULT_TABLE_SIZE]`; `table` points at it until the first grow. Code that frees or resets storage must check `ht->table != ht->small_table` (see `ht_clear`).
- **Growth**: the load check counts live entries *plus* tombstones (`entries + deleted`) against `HT_LOAD_FACTOR_NUM/DEN` (default 1/2). At the limit, `ht_add_or_update` doubles the table, or rehashes at the same size when at least half the fill is tombstones. `ht->deleted` must stay in sync wherever a tombstone is written or reused.
- **Struct free list**: freed `HashTable` structs are cached in `ht_free_list` (up to `HT_MAX_FREE`) and reused by `ht_create`. `ht_finished()` drains it — call at program exit. When compiled with `HT_THREAD_SAFE`, the free list and debug alloc stats are thread-local (`__declspec(thread)` / `__thread`); the tables themselves are still not thread-safe.
- **NULL values are legal**: `ht_find` returns NULL for both "missing" and "stored NULL"; use `ht_contains` / `ht_lookup` to distinguish.

### Key/value ownership — most important contract

Keys and values are opaque `const void *`. The table stores pointers only and **never frees keys or values**; callers own that memory (including before `ht_clear`/`ht_free`).

Default hash mixes the key pointer's address bits (murmur3-style finalizer) and default compare is pointer equality. For string keys, set **both** `ht_set_hash_func(ht, HT_HASH_STRING)` and a `strcmp`-based `ht_set_compare_func`. Compare functions return nonzero for equal.

## Conventions

- Return `HT_OK` (1) / `HT_FAIL` (0); pointer-returning functions return NULL on failure.
- `CHECK_THAT(cond)` guards preconditions: `return 0` on failure (plus `assert` under `_DEBUG`). It cannot be used in `void` functions — use an explicit `if (...) return;` guard there.
- Public compile-time config in `hash.h` (overridable via `-D`): `HT_DEFAULT_TABLE_SIZE`, `HT_PERTURB_VALUE`, `HT_LOAD_FACTOR_NUM`/`HT_LOAD_FACTOR_DEN` (legacy `HT_INV_LOAD_FACTOR=N` still maps to 1/N), `HT_ALLOC`/`HT_FREE`, `HT_TRACK_STATS`. `HT_TRACK_STATS` and `HT_DEFAULT_TABLE_SIZE` change the `HashTable` layout, so the library and its users must agree on them; code reading stats fields (tests, `bench.c`) must be guarded by `#if HT_TRACK_STATS == 1`. Internal toggles in `hash.c`: `HT_AUTO_GROW`, `HT_DEBUG_STATS`, `HT_MAX_FREE`, `HT_LINEAR`, `HT_PERTURB`, `HT_WARN_FREE_NONEMPTY` (default on under `_DEBUG`; tests call `ht_clear` before `ht_free` to stay quiet).
- Use `HT_ALLOC`/`HT_FREE` (not raw malloc/free) in library code, paired with the `HT_ALLOC_INC`/`HT_FREE_INC` stat macros.
