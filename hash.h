#ifndef __HASH_H
#define __HASH_H

#include <stdint.h>

#define HT_OK   1
#define HT_FAIL 0

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

//--------------------------------------
//
//--------------------------------------
typedef intptr_t hash_value_t;
typedef void* key_value_t;
typedef void* value_value_t;

//--------------------------------------
//
//--------------------------------------
typedef struct HashTable_Entry
{
    hash_value_t hash;
    key_value_t key;
    value_value_t value;
} HashTable_Entry;

//--------------------------------------
//
//--------------------------------------
typedef struct HashTable
{
    HashTable_Entry *table;
    size_t size;
    size_t entries;
    size_t collisions;
    size_t recent_collisions;
} HashTable;

//--------------------------------------
//
//--------------------------------------
HashTable *ht_init();
int ht_free(HashTable *ht);
value_value_t ht_find(HashTable *ht, hash_value_t hash, key_value_t key);
int ht_insert(HashTable *ht, hash_value_t hash, key_value_t key, value_value_t value);
size_t ht_size(HashTable *ht);
size_t ht_capacity(HashTable *ht);
HashTable *ht_grow(HashTable *ht);

#endif // __HASH_H