# hash
[![CMake](https://github.com/mseminatore/hash/actions/workflows/cmake-single-platform.yml/badge.svg)](https://github.com/mseminatore/hash/actions/workflows/cmake-single-platform.yml)
![GitHub License](https://img.shields.io/github/license/mseminatore/hash)

hash table in C

Notes
-----
- The hash table API uses opaque key and value pointers: `ht_key_t` and `ht_value_t` are `const void *`.
- Hash and compare function typedefs accept `ht_key_t` (i.e. `const void *`). This makes the API neutral to key representation — keys can be pointer identities or pointers to NUL-terminated C strings depending on the hash/compare functions you set.
	- The built-in `HT_HASH_STRING` string hasher expects the `ht_key_t` to point to a NUL-terminated C string. The default string hasher reads the key as `const unsigned char *`.
	- The default hash function treats the key pointer value as the hash (fast but only useful when keys are pointers intended to be hashed by address).
	- The default compare function is pointer-equality. If you store strings you must set a string comparison function using `ht_set_compare_func` (e.g. one that calls `strcmp`).
- The table stores pointers only and does not take ownership of keys/values (it won't free them). Callers are responsible for allocation and freeing.

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
