#ifndef __HASH_H
#define __HASH_H

#include <stdint.h>

#define HT_OK   1
#define HT_FAIL 0

// configuration
#define HT_TRACK_STATS 1

#ifndef HT_ALLOC
    #define HT_ALLOC malloc
#endif

#ifndef HT_FREE
    #define HT_FREE free
#endif

#ifndef HT_DEFAULT_SIZE
    #define HT_DEFAULT_SIZE 8
#endif

#ifndef HT_LOAD_FACTOR
    #define HT_LOAD_FACTOR 0.67f
#endif

#ifndef HT_PERTURB
    #define HT_PERTURB 5
#endif

//--------------------------------------
// 
//--------------------------------------
typedef intptr_t ht_hash_t;
typedef void* ht_key_t;
typedef void* ht_value_t;

//--------------------------------------
// table entry structure
//--------------------------------------
typedef struct HashTable_Entry
{
    ht_hash_t hash;
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
#if HT_TRACK_STATS == 1
    size_t insert_collisions;
    size_t search_collisions;
    size_t recent_insert_collisions;
#endif
    HashTable_Entry small_table[HT_DEFAULT_SIZE];
} HashTable;

//--------------------------------------
//
//--------------------------------------
HashTable *ht_create();
int ht_free(HashTable *ht);
ht_value_t ht_find(HashTable *ht, ht_hash_t hash, ht_key_t key);
int ht_insert(HashTable *ht, ht_hash_t hash, ht_key_t key, ht_value_t value);
size_t ht_size(HashTable *ht);
size_t ht_capacity(HashTable *ht);
HashTable *ht_grow(HashTable *ht);
HashTable *ht_shrink(HashTable *ht);
void ht_stats(HashTable* ht);
void ht_debug_stats();
int ht_next(HashTable* ht, size_t *ipos, ht_key_t*pkey, ht_value_t *pvalue);
void ht_finished();
int ht_remove(HashTable* ht, ht_key_t key);

#endif // __HASH_H