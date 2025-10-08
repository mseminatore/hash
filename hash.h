#ifndef __HASH_H
#define __HASH_H

#ifdef __cplusplus
    extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>

#define HT_FAIL 0
#define HT_OK   1

#define HT_HASH_NULL    (ht_hash_func)0
#define HT_HASH_STRING  (ht_hash_func)1

// configuration
#define HT_TRACK_STATS 1

#ifndef HT_ALLOC
    #define HT_ALLOC malloc
#endif

#ifndef HT_FREE
    #define HT_FREE free
#endif

#ifndef HT_DEFAULT_TABLE_SIZE
    #define HT_DEFAULT_TABLE_SIZE 8
#endif

#ifndef HT_PERTURB_VALUE
    #define HT_PERTURB_VALUE 5
#endif

#ifndef HT_INV_LOAD_FACTOR
    #define HT_INV_LOAD_FACTOR 2
#endif

//--------------------------------------
// define hash, key and value types
//--------------------------------------
// Note: keys and values are stored as opaque pointers. Keys/values are
// declared as `const void *` to indicate the table does not modify them.
// When using the built-in string hasher (`HT_HASH_STRING`) the key must be
// a pointer to a NULL-terminated C string (i.e. `const char *`).
typedef intptr_t ht_hash_t;
typedef const void* ht_key_t;
typedef const void* ht_value_t;

// hash and comparison functions
typedef ht_hash_t (*ht_hash_func)(ht_key_t key);
typedef int (*ht_compare_func)(ht_key_t a, ht_key_t b);

//--------------------------------------
// table entry structure
//--------------------------------------
typedef struct HashTable_Entry
{
    ht_hash_t hash;
    int tombstone;
    ht_key_t key;
    ht_value_t value;
} HashTable_Entry;

//--------------------------------------
//
//--------------------------------------
typedef struct HashTable
{
    HashTable_Entry *table;
    size_t mask;
    size_t size;
    size_t entries;
    ht_hash_func hash_fn;
    ht_compare_func compare_fn;

#if HT_TRACK_STATS == 1
    size_t insert_collisions;
    size_t search_collisions;
    size_t recent_insert_collisions;
#endif
    HashTable_Entry small_table[HT_DEFAULT_TABLE_SIZE];
} HashTable;

//--------------------------------------
//
//--------------------------------------
HashTable *ht_create();
int ht_free(HashTable *ht);
ht_value_t ht_find(HashTable *ht, ht_key_t key);
int ht_insert(HashTable *ht, ht_key_t key, ht_value_t value);
int ht_add(HashTable* ht, ht_key_t key, ht_value_t value);
size_t ht_size(HashTable *ht);
size_t ht_capacity(HashTable *ht);
HashTable *ht_grow(HashTable *ht);
HashTable *ht_shrink(HashTable *ht);
int ht_next(HashTable* ht, size_t *ipos, ht_key_t*pkey, ht_value_t *pvalue);
void ht_finished();
int ht_remove(HashTable* ht, ht_key_t key);
int ht_set_hash_func(HashTable* ht, ht_hash_func hash_fn);
int ht_set_compare_func(HashTable* ht, ht_compare_func compare_fn);

void ht_stats(HashTable* ht);
void ht_debug_stats();

#ifdef __cplusplus
    }
#endif

#endif // __HASH_H