#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "hash.h"

// configuration defines
#define HT_AUTO_GROW 1
#define HT_DEBUG_STATS 1
#define HT_MAX_FREE 16
#define HT_LINEAR 0

// helper macros
#define HASH_MATCH(hte, hash, key)  ((hte)->hash == hash && (hte)->key == key)
#define HASH_EMPTY(hte)             ((hte)->hash == 0 && (hte)->key == 0 && (hte)->value == 0)

#ifdef _DEBUG
#define CHECK_THAT(cond)            assert(cond); if (!(cond)) return 0;
#else
#define CHECK_THAT(cond)            if (!(cond)) return 0;
#endif

// maintain a free list of already alloc'd tables
static HashTable *ht_free_list[HT_MAX_FREE];
static int ht_free_count = 0;

#if HT_DEBUG_STATS == 1
    static size_t allocs = 0;
    static size_t frees = 0;
    static size_t resuse = 0;
    #define HT_ALLOC_INC allocs++
    #define HT_FREE_INC frees++
    #define HT_RESUSE resuse++
#else
    #define HT_ALLOC_INC
    #define HT_FREE_INC
    #define HT_RESUSE
#endif

//--------------------------------------
// initialize hash table
//--------------------------------------
HashTable *ht_create()
{
    HashTable* ht;
    
    // grab table from freelist if available
    if (ht_free_count > 0)
	{
		ht = ht_free_list[--ht_free_count];
        HT_RESUSE;
	}
	else
	{
		ht = HT_ALLOC(sizeof(HashTable));
        HT_ALLOC_INC;
    }
    
    if (!ht)
    {
        return NULL;
    }

    ht->insert_collisions = 0;
    ht->search_collisions = 0;
    ht->entries = 0;
    ht->recent_insert_collisions = 0;
    ht->size = HT_DEFAULT_SIZE;
    ht->mask = ht->size - 1;
    ht->table = ht->small_table;

    // zero table mem
    size_t table_size = sizeof(HashTable_Entry) * ht->size;
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
    CHECK_THAT(ht && ht->table);

    // TODO - warn if table is not empty?

    // set table to default table
    // free table
    if (ht->table != ht->small_table)
	{
		HT_FREE(ht->table);
		HT_FREE_INC;
        ht->table = ht->small_table;
	}

    // clear struct contents
    ht->insert_collisions = 0;
    ht->search_collisions = 0;
    ht->entries = 0;
    ht->recent_insert_collisions = 0;
    ht->size = 0;

    // add to free list
    if (ht_free_count < HT_MAX_FREE)
	{
		ht_free_list[ht_free_count++] = ht;
	}
    else
    {
        HT_FREE(ht);
        HT_FREE_INC;
    }

    return HT_OK;
}

//--------------------------------------
// cleanup the free list
//--------------------------------------
void ht_finished()
{
    while(ht_free_count > 0)
    {
        HT_FREE(ht_free_list[--ht_free_count]);
        HT_FREE_INC;
    }
}

//--------------------------------------
// iterate over a table
//--------------------------------------
int ht_next(HashTable* ht, size_t *ipos, ht_key_t*pkey, ht_value_t *pvalue)
{
    ht_key_t key = NULL;
    ht_value_t value = NULL;

    CHECK_THAT(ht && ht->table);

    // get index and check bounds
    size_t index = *ipos;
    if (index < 0 || index > ht->size)
        return HT_FAIL;

    // find next table entry
    while (index < ht->size && HASH_EMPTY(&ht->table[index]))
    {
        index++;
    }

    // see if we hit the end of the table
    if (index >= ht->size)
        return HT_FAIL;

    key = ht->table[index].key;
    value = ht->table[index].value;

    // point to next entry
    *ipos = index + 1;

    // return key/value if 
    if (pkey)
        *pkey = key;

    if (pvalue)
        *pvalue = value;

    return HT_OK;
}

//--------------------------------------
// try to find an entry in the table
//--------------------------------------
ht_value_t ht_find(HashTable *ht, ht_hash_t hash, ht_key_t key)
{
    size_t perturb = hash;

    CHECK_THAT(ht && ht->table);

    // look at entry based on hash
    size_t start_bin = hash & ht->mask;
    HashTable_Entry* hte = &ht->table[start_bin];

    if (HASH_MATCH(hte, hash, key))
    {
        return hte->value;
    }

    // if not equal, start linear search for match
#if HT_LINEAR == 1
    size_t bin = (start_bin + 1) & ht->mask;
#else
    size_t bin = (5 * start_bin + 1) & ht->mask;
#endif

    while (bin != start_bin)
    {
        ht->search_collisions++;
        hte = &ht->table[bin];

        if (HASH_MATCH(hte, hash, key))
        {
            return hte->value;
        }

#if HT_LINEAR == 1
        bin = (bin + 1) & ht->mask;
#else
        bin = (5 * bin + 1) & ht->mask;
#endif

    }

    // if not found, fail
    return NULL;
}

//--------------------------------------
// internal insert
//--------------------------------------
static int ht_insert_nocheck(HashTable *ht, HashTable_Entry* table, ht_hash_t hash, ht_key_t key, ht_value_t value, size_t size)
{
    CHECK_THAT(ht && table);

    size_t mask = size - 1;

    // check for free entry based on hash
    size_t start_bin = hash & mask;

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
#if HT_LINEAR == 1
    size_t bin = (start_bin + 1) & mask;
#else
    size_t bin = (5 * start_bin + 1) & mask;
#endif

    while (bin != start_bin)
    {
        // mark collisions
        ht->insert_collisions++;
        ht->recent_insert_collisions++;

        hte = &table[bin];

        // if entry is a match, update the value
        if (HASH_MATCH(hte, hash, key))
        {
            hte->value = value;
            return HT_OK;
        }

        // if entry unused, fill it and return success
        if (HASH_EMPTY(hte))
        {
            hte->hash = hash;
            hte->key = key;
            hte->value = value;
            return HT_OK;
        }

#if HT_LINEAR == 1
        bin = (bin + 1) & mask;
#else
        bin = (5 * bin + 1) & mask;
#endif
    }

    // if no free slot found, then fail
    return HT_FAIL;
}

//--------------------------------------
// insert an entry
//--------------------------------------
int ht_insert(HashTable *ht, ht_hash_t hash, ht_key_t key, ht_value_t value)
{
    CHECK_THAT(ht && ht->table);

    // check for load factor and grow table if necessary
#if HT_AUTO_GROW
    // load factor of 0.5 to 0.67 is good time to grow
    if (2 * ht->entries >= ht->size)
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
// attempt to remove entry from table
//--------------------------------------
int ht_remove(HashTable* ht, ht_key_t key)
{
    CHECK_THAT(ht && ht->table);

    // look for key in table and if found, remove
    HashTable_Entry *hte = ht->table;
    for (size_t i = 0; i < ht->size; i++)
    {
        // if found, mark entry as empty
        if (hte->key == key)
        {
            hte->hash = 0;
            hte->key = 0;
            hte->value = 0;
            ht->entries--;
            return HT_OK;
        }
        hte++;
    }

    // if not found, return failure
    return HT_FAIL;
}

//--------------------------------------
// return current size
//--------------------------------------
size_t ht_size(HashTable *ht)
{
    CHECK_THAT(ht && ht->table);
    return (ht && ht->table) ? ht->entries : 0;
}

//--------------------------------------
// return table capacity
//--------------------------------------
size_t ht_capacity(HashTable *ht)
{
    CHECK_THAT(ht && ht->table);
    return (ht && ht->table) ? ht->size : 0;
}

//--------------------------------------
// attempt to resize the table
//--------------------------------------
static HashTable* ht_resize(HashTable* ht, size_t new_size)
{
    CHECK_THAT(ht && ht->table);

    // alloc new table
    size_t new_table_size = sizeof(HashTable_Entry) * new_size;
    HashTable_Entry* new_table = HT_ALLOC(new_table_size);
    if (!new_table)
    {
        return NULL;
    }

    HT_ALLOC_INC;
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
                HT_FREE_INC;
                return NULL;
            }
        }
    }

    // free old table
    if (ht->table != ht->small_table)
    {
        HT_FREE(ht->table);
        HT_FREE_INC;
    }

    // update hash table state
    ht->table = new_table;
    ht->size = new_size;
    ht->mask = new_size - 1;

    // clear recent collisions
    ht->recent_insert_collisions = 0;

    return ht;
}

//--------------------------------------
// attempt to grow the table
//--------------------------------------
HashTable *ht_grow(HashTable *ht)
{
    CHECK_THAT(ht && ht->table);

    // increase (double) table size
    size_t new_size = ht->size << 1;

    return ht_resize(ht, new_size);
}

//--------------------------------------
// try to shrink the table
//--------------------------------------
HashTable* ht_shrink(HashTable* ht)
{
    CHECK_THAT(ht && ht->table);

	// decrease (half) table size
	size_t new_size = ht->size >> 1;

    // make sure smaller size is large enough
    if ((ht->entries << 1) > new_size)
        return NULL;

    return ht_resize(ht, new_size);
}

//--------------------------------------
// print some useful debug stats
//--------------------------------------
void ht_debug_stats()
{
#if HT_DEBUG_STATS == 1
    printf("All tables -> allocs: %zu, frees: %zu, resuse: %zu, freelist: %d\n", allocs, frees, resuse, ht_free_count);
#endif
}

//--------------------------------------
// print some useful table stats
//--------------------------------------
void ht_stats(HashTable* ht)
{
    CHECK_THAT(ht && ht->table);

    printf("This table -> entries: %zu, size: %zu, insert collides: %zu, recent insert collides: %zu, search collides: %zu\n", ht->entries, ht->size, ht->insert_collisions, ht->recent_insert_collisions, ht->search_collisions);
}
