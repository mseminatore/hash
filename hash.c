#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "hash.h"

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
    // if not equal, start linear search for match
    // if not found, fail
    // otherwise return found value
    return NULL;
}

//--------------------------------------
// insert an entry
//--------------------------------------
int ht_insert(HashTable *ht, hash_value_t hash, key_value_t key, value_value_t value)
{
    assert(ht && ht->table);

    // check for load factor and grow table if necessary
    float load_factor = (float)ht_size(ht) / ht_capacity(ht);
    if (load_factor > HT_LOAD_FACTOR)
    {
        if (!ht_grow(ht))
        {
            return HT_FAIL;
        }
    }

    // check for free entry based on hash
    size_t bin = hash % (ht->size - 1);

    // if entry unused, fill it and return success
    if (ht->table[bin].hash == 0 && ht->table[bin].key == 0 && ht->table[bin].value == 0)
    {
        ht->table[bin].hash = hash;
        ht->table[bin].key = key;
        ht->table[bin].value = value;
        return HT_OK;
    }

    // otherwise mark collisions and start linear search for open bin
    ht->collisions++;
    ht->recent_collisions++;
    
    // if no free slot found, then fail
    return HT_FAIL;
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
    // alloc new table
    // re-insert existing items

    return NULL;
}
