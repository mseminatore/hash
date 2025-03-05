#include "hash.h"

//--------------------------------------
// initialize hash table
//--------------------------------------
HashTable *ht_init()
{
    HashTable *ht = HT_MALLOC(sizeof(HashTable));
    if (!ht)
    {
        return NULL;
    }

    ht->collisions = 0;
    ht->entries = 0;
    ht->recent_collisions = 0;
    ht->size = 8;
    
    ht->table = HT_MALLOC(sizeof(HashTable_Entry) * ht->size);
    if (!ht->table)
    {
        // release table entry and fail
        HT_FREE(ht);
        return NULL;
    }

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
value_value_t ht_find(HashTable *ht, key_value_t key)
{
    return NULL;
}

//--------------------------------------
// insert an entry
//--------------------------------------
int ht_insert(HashTable *ht, key_value_t key, value_value_t value);
{

    return HT_FAIL;
}

//--------------------------------------
// return current size
//--------------------------------------
size_t ht_size(HashTable *ht)
{
    assert(ht->table);

    return (ht && ht->table) ? ht->entries : 0;
}

//--------------------------------------
// return table capacity
//--------------------------------------
size_t ht_capacity(HashTable *ht)
{
    assert(ht->table);

    return (ht && ht->table) ? ht->size : 0;
}
