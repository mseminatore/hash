# hash (libht)
[![CMake](https://github.com/mseminatore/hash/actions/workflows/cmake-single-platform.yml/badge.svg)](https://github.com/mseminatore/hash/actions/workflows/cmake-single-platform.yml)
![GitHub License](https://img.shields.io/github/license/mseminatore/hash)

This library offers an open-addressing hash table in C. My prior experiences in implementing hash tables (pre-C++ and STL) often used closed-addressing. 
I wanted to bring my hands-on experience up to date with common practice. I looked for inspiration and guidance at many articles examples including CPython's excellent PyDict.

The newer [elastic and funnel hashing](https://arxiv.org/pdf/2501.02305v1.pdf) methods pay off for fixed-size, insert-only tables running close to full. This library targets general-purpose use (deletes, automatic growth, moderate load factors), so exploring those methods is better suited to a separate library.

Notes
-----
- The hash table API uses opaque key and value pointers: `ht_key_t` and `ht_value_t` are `const void *`.
- Hash and compare function typedefs accept `ht_key_t` (i.e. `const void *`). This makes the API neutral to key representation — keys can be pointer identities or pointers to NUL-terminated C strings depending on the hash/compare functions you set.
	- The built-in `HT_HASH_STRING` string hasher expects the `ht_key_t` to point to a NUL-terminated C string. The default string hasher reads the key as `const unsigned char *`.
	- The default hash function hashes the key pointer's address, mixing its bits so aligned or evenly spaced pointers spread well (only useful when keys are pointers intended to be hashed by address).
	- The default compare function is pointer-equality. If you store strings you must set a string comparison function using `ht_set_compare_func` (e.g. one that calls `strcmp`).
- The table stores pointers only and does not take ownership of keys/values (it won't free them). Callers are responsible for allocation and freeing.

Building
--------
CMake builds the static library `ht` and the test runner `ht_test`:

```sh
git submodule update --init      # fetch the testy test framework
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Add `-DHT_SANITIZE=ON` (GCC/Clang) to build with AddressSanitizer and UndefinedBehaviorSanitizer.

Iteration, removal and resizing
-------------------------------
- `ht_next` walks the table's slots. Removing the entry it just returned, with `ht_remove`, is safe during iteration: removal never resizes the table, so the walk continues correctly.
- Anything that can resize the table is **not** safe during iteration: `ht_insert`, `ht_add`, `ht_grow` and `ht_shrink`. A resize moves entries, so the walk could skip or repeat them. Finish iterating first, or collect keys and apply the changes afterwards.
- The table grows automatically. It never shrinks on removal, which keeps `ht_remove` O(1). When later inserts force a rebuild to clear deleted slots, that rebuild is sized to the live entries, so a table emptied by mass removals shrinks then.
- `ht_shrink` shrinks to fit the live entries in one step. It returns the table, even when already as small as possible, and returns NULL only if allocation fails. Older versions returned NULL when no shrink was possible, so callers that relied on that need updating.

Example (string keys)
---------------------
Below is a minimal example showing how to use the built-in string hasher and a string compare function. The table stores pointers only, so we strdup keys and free them on removal.

```c
#include "hash.h"
#include <string.h>
#include <stdlib.h>

static int str_compare(ht_key_t a, ht_key_t b)
{
	return strcmp((const char*)a, (const char*)b) == 0;
}

int main(void)
{
	HashTable *ht = ht_create();
	ht_set_hash_func(ht, HT_HASH_STRING);
	ht_set_compare_func(ht, str_compare);

	char *k = strdup("hello");
	char *v = strdup("world");
	ht_insert(ht, k, v);

	char *found = (char*)ht_find(ht, k);
	// use found

	// cleanup -- remove and free key/value
	ht_remove(ht, k);
	free(k);
	free(v);
	ht_free(ht);
	return 0;
}
```

Notes:
- Always set both a hash function and a compatible compare function when keys are strings.
- The table does not free keys/values for you; free them after removal or when the table is destroyed.
