#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "hash.h"

#define HT_AUTO_GROW 1

#define HASH_MATCH(hte, hash, key)  ((hte)->hash == hash && (hte)->key == key)
#define HASH_EMPTY(hte)             ((hte)->hash == 0 && (hte)->key == 0 && (hte)->value == 0)

// maintain a free list of already alloc'd tables
#define HT_MAX_FREE 16
static HashTable ht_free_list[HT_MAX_FREE];
static int ht_free_count = 0;

//--------------------------------------
// initialize hash table
//--------------------------------------
HashTable *ht_init()
{
    HashTable *ht = HT_ALLOC(sizeof(HashTable));
    if (!ht)
    {
        return NULL;
    }

    ht->collisions = 0;
    ht->entries = 0;
    ht->recent_collisions = 0;
    ht->size = HT_DEFAULT_SIZE;
    
    size_t table_size = sizeof(HashTable_Entry) * ht->size;
    ht->table = HT_ALLOC(table_size);
    if (!ht->table)
    {
        // release table entry and fail
        HT_FREE(ht);
        return NULL;
    }

    memset(ht->table, 0, table_size);
    return ht;
}

//--------------------------------------
// free hash table
//
// NB: assumes entries have been free'd
//--------------------------------------
int ht_free(HashTable *ht)
{
    if (!ht)
    {
        return HT_FAIL;
    }

    // TODO - warn if table is not empty?

    HT_FREE(ht->table);

    // clear struct contents
    ht->collisions = 0;
    ht->entries = 0;
    ht->recent_collisions = 0;
    ht->size = 0;
    ht->table = 0;

    HT_FREE(ht);
    return HT_OK;
}

//--------------------------------------
// find an entry
//--------------------------------------
value_value_t ht_find(HashTable *ht, hash_value_t hash, key_value_t key)
{
    assert(ht && ht->table);

    // look at entry based on hash
    size_t start_bin = hash % (ht->size - 1);
    HashTable_Entry* hte = &ht->table[start_bin];

    if (HASH_MATCH(hte, hash, key))
    {
        return hte->value;
    }

    // if not equal, start linear search for match
    size_t bin = (start_bin + 1) & (ht->size - 1);

    while (bin != start_bin)
    {
        hte = &ht->table[bin];

        if (HASH_MATCH(hte, hash, key))
        {
            return hte->value;
        }

        bin = (bin + 1) & (ht->size - 1);
    }

    // if not found, fail
    return NULL;
}

//--------------------------------------
//
//--------------------------------------
static int ht_insert_nocheck(HashTable *ht, HashTable_Entry* table, hash_value_t hash, key_value_t key, value_value_t value, size_t size)
{
    // check for free entry based on hash
    size_t start_bin = hash & (size - 1);

    HashTable_Entry* hte = &table[start_bin];

    // if entry unused, fill it and return success
    if (HASH_EMPTY(hte))
    {
        hte->hash = hash;
        hte->key = key;
        hte->value = value;
        return HT_OK;
    }

    // otherwise start linear search for open bin
    size_t bin = (start_bin + 1) & (size - 1);

    while (bin != start_bin)
    {
        // mark collisions
        ht->collisions++;
        ht->recent_collisions++;

        hte = &table[bin];

        if (HASH_EMPTY(hte))
        {
            hte->hash = hash;
            hte->key = key;
            hte->value = value;
            return HT_OK;
        }

        bin = (bin + 1) & (size - 1);
    }

    // if no free slot found, then fail
    return HT_FAIL;
}

//--------------------------------------
// insert an entry
//--------------------------------------
int ht_insert(HashTable *ht, hash_value_t hash, key_value_t key, value_value_t value)
{
    assert(ht && ht->table);

    // check for load factor and grow table if necessary
#if HT_AUTO_GROW
    float load_factor = (float)ht_size(ht) / ht_capacity(ht);
    if (load_factor > HT_LOAD_FACTOR)
    {
        if (!ht_grow(ht))
        {
            return HT_FAIL;
        }
    }
#endif

    int result = ht_insert_nocheck(ht, ht->table, hash, key, value, ht->size);
    if (result == HT_OK)
        ht->entries++;

    return result;
}

//--------------------------------------
// return current size
//--------------------------------------
size_t ht_size(HashTable *ht)
{
    assert(ht && ht->table);

    return (ht && ht->table) ? ht->entries : 0;
}

//--------------------------------------
// return table capacity
//--------------------------------------
size_t ht_capacity(HashTable *ht)
{
    assert(ht && ht->table);

    return (ht && ht->table) ? ht->size : 0;
}

//--------------------------------------
// return table capacity
//--------------------------------------
HashTable *ht_grow(HashTable *ht)
{
    assert(ht && ht->table);

    // increase (double) table size
    size_t new_size = ht->size << 1;

    // alloc new table
    size_t new_table_size = sizeof(HashTable_Entry) * new_size;
    HashTable_Entry* new_table = HT_ALLOC(new_table_size);
    if (!new_table)
    {
        return NULL;
    }

    memset(new_table, 0, new_table_size);

    // re-insert existing items into new table
    HashTable_Entry* hte;
    for (size_t i = 0; i < ht->size; i++)
    {
        hte = &ht->table[i];
        if (!HASH_EMPTY(hte))
        {
            if (HT_FAIL == ht_insert_nocheck(ht, new_table, hte->hash, hte->key, hte->value, new_size))
            {
                HT_FREE(new_table);
                return NULL;
            }
        }
    }

    // free old table
    HT_FREE(ht->table);

    // update hash table state
    ht->table = new_table;
    ht->size = new_size;

    // clear recent collisions
    ht->recent_collisions = 0;

    return ht;
}

//--------------------------------------
//
//--------------------------------------
void ht_stats(HashTable* ht)
{
    assert(ht && ht->table);

    printf("entries: %zu, size: %zu, total collides: %zu, recent collides: %zu\n", ht->entries, ht->size, ht->collisions, ht->recent_collisions);
}
