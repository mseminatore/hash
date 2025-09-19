## Hash Table — Developer Specification

This document describes the hash table implementation in this repository (files: `hash.h`, `hash.c`), the public API, design choices, known limitations, and recommended improvements.

### Overview

This project implements a compact open-addressing hash table in C. Keys and values are stored as opaque pointers (`ht_key_t`, `ht_value_t`) which are defined as `const void *` in the public header. The table uses a small, embedded `small_table` on the `HashTable` struct as the initial storage and will allocate a separate backing array if the table grows.

Key points:
- Open addressing probing with a perturb variable used to influence subsequent probe indices.
- Default hash function treats the key pointer value as the hash; a string hash function (MurmurOAAT32-like) is provided for string keys. The built-in string hasher expects the key to be a pointer to a NUL-terminated C string and reads it as `const unsigned char *`.
- User-provided hash and compare functions are supported.
- Automatic growth occurs when load factor reaches ~0.5 (2 * entries >= size). Shrink is supported but conservative.
- The library does not free user-provided keys/values; ownership remains with the caller.

### Public API (from `hash.h`)

- `HashTable *ht_create();`
  - Allocates and returns a new `HashTable`. Uses the internal `small_table` initially. Returns `NULL` on allocation failure.

- `int ht_free(HashTable *ht);`
  - Releases internal resources. Does not free keys/values. Adds the `HashTable` struct to an internal free-list for reuse.

- `ht_value_t ht_find(HashTable *ht, ht_key_t key);`
  - Returns the value associated with `key` or `NULL` if not found.

- `int ht_insert(HashTable *ht, ht_key_t key, ht_value_t value);`
  - Inserts a key/value pair. Fails (returns `HT_FAIL`) if an equivalent key already exists.

- `int ht_add(HashTable *ht, ht_key_t key, ht_value_t value);`
  - Inserts or replaces the value if the key already exists.

- `size_t ht_size(HashTable *ht);`
  - Returns the number of stored entries.

- `size_t ht_capacity(HashTable *ht);`
  - Returns current table capacity (number of slots).

- `HashTable *ht_grow(HashTable *ht);`
  - Doubles the table size and rehashes entries. Returns the updated `HashTable` or `NULL` on failure.

- `HashTable *ht_shrink(HashTable *ht);`
  - Attempts to halve the table size and rehash. Returns `NULL` if new size would be too small.

- `int ht_next(HashTable* ht, size_t *ipos, ht_key_t*pkey, ht_value_t *pvalue);`
  - Iteration helper. Caller sets `*ipos = 0` to start. On success returns `HT_OK` and advances `*ipos`; on end returns `HT_FAIL`.

- `int ht_remove(HashTable* ht, ht_key_t key);`
  - Removes an entry by clearing the slot (sets hash, key, value to zero). Returns `HT_OK` or `HT_FAIL`.

- `int ht_set_hash_func(HashTable* ht, ht_hash_func hash_fn);`
  - Set hash function. Special sentinel values: `HT_HASH_NULL` -> default pointer hash, `HT_HASH_STRING` -> built-in string hash.

- `int ht_set_compare_func(HashTable* ht, ht_compare_func compare_fn);`
  - Set compare function. `NULL` sets the default pointer-equality compare.

- `void ht_stats(HashTable* ht);` and `void ht_debug_stats();`
  - Debug/stat dumps.

### Data shapes and ownership

- `HashTable_Entry` holds an integer `hash`, and pointer `key` and `value` (both `ht_key_t`/`ht_value_t` are `const void *`).
- The library stores pointers only. It does not allocate or free keys/values — it only stores the pointers you provide. Callers must manage the lifetime (allocation/freeing) of objects referenced by keys and values.

### Configuration and compile-time options

- `HT_DEFAULT_TABLE_SIZE` — initial embedded table size (default 8).
- `HT_PERTURB_VALUE` — number of bits to shift during probe perturbation.
- `HT_TRACK_STATS` — collect per-table collision stats.
- `HT_DEBUG_STATS` — collect allocator statistics.
- `HT_ALLOC` / `HT_FREE` — macros to replace allocation/free functions.
- `HT_AUTO_GROW` / `HT_LINEAR` / `HT_PERTURB` — tuning options in `hash.c`.

### Probing and resizing behavior

- Probing uses a perturb value derived from the hash. If `HT_LINEAR` is enabled, probe increments are linear-based; otherwise the probe uses a multiplicative+perturb formula:

  bin = (5 * bin + perturb + 1) & mask

  and perturb is right-shifted by `HT_PERTURB_VALUE` on each step.

- The table doubles when `2 * entries >= size` (load factor ~0.5).

### Complexity

- Average case: O(1) for insert/find/remove.
- Resize is O(n) during rehash. Insert may trigger resize, which is the expensive operation.

### Tests and examples

- `test.c` contains unit-style tests using `testy`:
  - `test_create`, `test_set_funcs`, `test_insert`, `test_find`, `test_iterate`, `test_remove`, `test_big_words`.
- A word-list file `words_alpha.txt` is used in `test_big_words` to stress capacity and collisions.

Build and run (on the project root, using CMake):

```powershell
# create build directory and configure
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
# build tests (Visual Studio generator will produce solution in build/)
cmake --build build --config Debug
# run the test executable
.
# or run the compiled test directly from build/Debug/ht_test.exe
build\Debug\ht_test.exe
```

Replace generator/paths as appropriate for your environment.

### Known issues and limitations

1. Removal semantics
   - `ht_remove` marks a slot as completely empty (zeros hash, key and value). In many open-addressing implementations deleting an entry requires placing a tombstone marker rather than reverting to an empty slot to preserve probe chains; otherwise some items that hashed to earlier bins might become unreachable depending on probing strategy. The current implementation performs full-cycle probe scans for lookups (it does not stop on empty slots), which mitigates some problems, but this is fragile and non-standard. Consider switching to an explicit tombstone marker or a rehash-on-delete strategy.

2. Key / hash function contract
  - The `ht_hash_func` and `ht_compare_func` signatures accept `ht_key_t` (i.e. `const void *`) and the public typedefs were updated to use `ht_key_t`. This unifies the API so hash/compare functions take the same key type stored in the table.
  - The default hash function uses the key pointer value as the hash (address-based). The default compare function tests pointer equality.
  - Use `HT_HASH_STRING` or supply a hash function that treats `ht_key_t` as a pointer to a NUL-terminated C string when string hashing is desired. When storing string keys you should also set an appropriate compare function (for example, one that calls `strcmp`).

3. `ht_remove` uses a full table scan checking `hte->key && ht->compare_fn(hte->key, key)` which ignores the stored `hash`. This is correct but linear-time O(capacity) instead of being proportional to probe length — it's slower for large empty tables.

4. Error handling and CHECK_THAT
   - The `CHECK_THAT` macro returns `0` (which maps to `HT_FAIL` for int returns or `NULL` for pointer returns) on invalid inputs in non-debug builds. This can mask errors. Consider returning explicit error codes or asserting in debug only.

5. Thread-safety
   - The hash table is not thread-safe. Concurrent access requires external synchronization.

6. Iteration stability
   - `ht_next` iterates over the underlying table array; concurrent inserts/removals or rehashing will invalidate iteration state.

### Suggested improvements (prioritized)

1. Fix deletion semantics
   - Implement tombstone markers or shift-rehashing for deletion to ensure correctness and expected performance.

2. Clarify and unify hash/compare types
  - The project has updated the typedefs so `ht_hash_func` and `ht_compare_func` accept `ht_key_t` (`const void *`). Keep documentation and examples up-to-date to show both pointer-hash and string-hash usage (and remember to set a string compare when using `HT_HASH_STRING`).

3. Stronger unit tests
   - Add unit tests for edge cases: remove in the middle of probe chains, re-insert after delete, concurrent rehash behavior, and large-load performance.

4. API ergonomics
   - Add callbacks for freeing keys/values (`key_free`, `value_free`) to ease ownership management. Add `ht_clear` to remove all entries without freeing the table.

5. CI and cross-platform builds
   - Add a GitHub Actions workflow to run builds and tests on Windows, macOS and Linux.

6. Documentation
   - Expand `README.md` with usage examples, memory ownership rules, and configuration options.

### Edge cases to watch during development

- Inserting `NULL` keys/values: current implementation checks `key && value` in insertion; insertion with `NULL` is rejected.
- Hash collisions: collision counters are tracked when `HT_TRACK_STATS` is enabled; tune `HT_PERTURB_VALUE` if needed.
- Rehash failure: `ht_grow`/`ht_resize` return `NULL` on allocation failure; callers should handle `NULL` and leave table intact.
- Iteration and rehash: do not rehash while iterating with `ht_next`.

### Verification steps (quick)

1. Build and run tests (see Build and run section above).
2. Run `test_big_words` and confirm `count == ht_size(ht)` in that test.
3. Add a unit test that inserts keys A, B, C that collide into a probe chain, remove the middle element, and ensure remaining keys are still findable. This will expose deletion issues.

### Next steps

- Implement a robust delete (tombstones or shift-rehash).
- Update the hash/compare typedefs for clarity and add documentation.
- Add explicit ownership/free callbacks and unit tests.

---

Generated by automated repository analysis. Use this document as the authoritative dev spec prior to refactor work.
