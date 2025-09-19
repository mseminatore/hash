# hash
hash table in C

Notes
-----
- The hash table API uses opaque key and value pointers: `ht_key_t` and `ht_value_t` are `const void *`.
- Hash and compare function typedefs accept `ht_key_t` (i.e. `const void *`). This makes the API neutral to key representation — keys can be pointer identities or pointers to NUL-terminated C strings depending on the hash/compare functions you set.
	- The built-in `HT_HASH_STRING` string hasher expects the `ht_key_t` to point to a NUL-terminated C string. The default string hasher reads the key as `const unsigned char *`.
	- The default hash function treats the key pointer value as the hash (fast but only useful when keys are pointers intended to be hashed by address).
	- The default compare function is pointer-equality. If you store strings you must set a string comparison function using `ht_set_compare_func` (e.g. one that calls `strcmp`).
- The table stores pointers only and does not take ownership of keys/values (it won't free them). Callers are responsible for allocation and freeing.
