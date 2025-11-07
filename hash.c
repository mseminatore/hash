#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "hash.h"

#define HT_ADD_ONLY 0
#define HT_REPLACE  1

// configuration defines
#define HT_AUTO_GROW    1   // automatically grow table
#define HT_DEBUG_STATS  1   // track alloc/free stats
#define HT_MAX_FREE     16  // size of free list
#define HT_LINEAR       0   // use linear probing
#define HT_PERTURB      0   // randomize probes

// helper macros
#define HASH_MATCH(hte, hash, key)  (!(hte)->tombstone && (hte)->hash == hash && ht->compare_fn((hte)->key, key))
#define HASH_EMPTY(hte)             ((hte)->tombstone || ((hte)->hash == 0 && (hte)->key == 0 && (hte)->value == 0))

#ifdef _DEBUG
#   define CHECK_THAT(cond)            assert(cond); if (!(cond)) return 0;
#else
#   define CHECK_THAT(cond)            if (!(cond)) return 0;
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

#if HT_TRACK_STATS == 1
    #define HT_INSERT_COLLIDE(ht)           (ht)->insert_collisions++;
    #define HT_SEARCH_COLLIDE(ht)           (ht)->search_collisions++;
    #define HT_RECENT_INSERT_COLLIDE(ht)    (ht)->recent_insert_collisions++;
#else
    #define HT_INSERT_COLLIDE(ht)
    #define HT_SEARCH_COLLIDE(ht)
    #define HT_RECENT_INSERT_COLLIDE(ht)
#endif

//--------------------------------------
// default comparison is equality
//--------------------------------------
static int default_compare_fn(ht_key_t a, ht_key_t b)
{
    CHECK_THAT(a && b);

    // check for equality
    return a == b;
}

//--------------------------------------
// default hash is the key
//--------------------------------------
static ht_hash_t default_hash_fn(ht_key_t key)
{
    CHECK_THAT(key);
    // hash is the key pointer value
    // this is a bad hash function for most uses, but it is fast
    return (ht_hash_t)key;
}

//--------------------------------------
// default string hash function
// MurmurOAAT32
// Note: the string hasher expects the key to be a pointer to a NUL-terminated
// C string (ht_key_t which is typedef'd to const void* in the header). It reads
// the bytes as unsigned characters.
//--------------------------------------
static ht_hash_t string_hash_fn(ht_key_t key)
{
    const unsigned char *s = (const unsigned char*)key;
    ht_hash_t h = 3323198485ul;

    for (; *s; ++s)
    {
        h ^= *s;
        h *= 0x5bd1e995;
        h ^= h >> 15;
    }
    return h;
}

//--------------------------------------
// attempt to set hash function
//--------------------------------------
int ht_set_hash_func(HashTable* ht, ht_hash_func hash_fn)
{
	CHECK_THAT(ht);

    if (hash_fn == HT_HASH_NULL)
        ht->hash_fn = default_hash_fn;
    else if (hash_fn == HT_HASH_STRING)
        ht->hash_fn = string_hash_fn;
    else
        ht->hash_fn = hash_fn;

    return HT_OK;
}

//--------------------------------------
// attempt to set compare function
//--------------------------------------
int ht_set_compare_func(HashTable* ht, ht_compare_func compare_fn)
{
	CHECK_THAT(ht);

    if (compare_fn == NULL)
        ht->compare_fn = default_compare_fn;
    else
        ht->compare_fn = compare_fn;
    
    return HT_OK;
}

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

#if HT_TRACK_STATS == 1
    ht->insert_collisions = 0;
    ht->search_collisions = 0;
    ht->recent_insert_collisions = 0;
#endif

    ht->entries = 0;
    ht->size = HT_DEFAULT_TABLE_SIZE;
    ht->mask = ht->size - 1;
    ht->table = ht->small_table;
    ht->compare_fn = default_compare_fn;
    ht->hash_fn = default_hash_fn;

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
#if HT_TRACK_STATS == 1
    ht->insert_collisions = 0;
    ht->search_collisions = 0;
    ht->recent_insert_collisions = 0;
#endif

    ht->entries = 0;
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
ht_value_t ht_find(HashTable *ht, ht_key_t key)
{
    CHECK_THAT(ht && ht->table);
    CHECK_THAT(key);

    // check for empty table
    if (ht->entries == 0)
        return NULL;

    ht_hash_t hash = ht->hash_fn(key);

#if HT_PERTURB == 1
    size_t perturb = hash;
#else
    size_t perturb = 0;
#endif

    int done = 0;

    // look at entry based on hash
    size_t start_bin = (size_t)hash & ht->mask;
    size_t bin = start_bin;
    HashTable_Entry* hte = &ht->table[start_bin];

    do
    {
        if (HASH_MATCH(hte, hash, key))
        {
            return hte->value;
        }

        HT_SEARCH_COLLIDE(ht);

        perturb >>= HT_PERTURB_VALUE;

#if HT_LINEAR == 1
        bin = (bin + perturb + 1) & ht->mask;
#else
        bin = (5 * bin + perturb + 1) & ht->mask;
#endif
        hte = &ht->table[bin];

//#if HT_PERTURB != 1
        done = bin == start_bin;
//#endif
    } while (!done);

    // if not found, fail
    return NULL;
}

//--------------------------------------
// internal insert
//--------------------------------------
static int ht_insert_nocheck(HashTable *ht, HashTable_Entry* table, ht_hash_t hash, ht_key_t key, ht_value_t value, size_t size, int replace)
{
    CHECK_THAT(ht && table);
    CHECK_THAT(key);

    // check that size is power of 2
    CHECK_THAT(size && !(size & (size - 1)));

#if HT_PERTURB == 1
    size_t perturb = hash;
#else
    size_t perturb = 0;
#endif

    size_t mask = size - 1;
    int done = 0;

    // check for free entry based on hash
    size_t start_bin = (size_t)hash & mask;
    size_t bin = start_bin;
    HashTable_Entry* hte = &table[start_bin];

    do
    {
        // if entry unused, fill it and return success
        if (HASH_EMPTY(hte))
        {
            hte->hash = hash;
            hte->key = key;
            hte->value = value;
			hte->tombstone = 0;

            // if we are not re-hashing increment entries
            if(ht->table == table)
                ht->entries++;

            return HT_OK;
        }

        // if entry is a match, update the value
        if (HASH_MATCH(hte, hash, key))
        {
			// if replace is not set, then fail
            if (!replace)
            {
                // puts("ht_insert_nocheck: key already exists");
                return HT_FAIL;
            }

            hte->value = value;
            return HT_OK;
        }

        // mark collisions
        HT_INSERT_COLLIDE(ht);
        HT_RECENT_INSERT_COLLIDE(ht);

        perturb >>= HT_PERTURB_VALUE;

#if HT_LINEAR == 1
        bin = (bin + perturb + 1) & mask;
#else
        bin = (5 * bin + perturb + 1) & mask;
#endif

        hte = &table[bin];
//#if HT_PERTURB != 1
        done = bin == start_bin;
//#endif
    } while (!done);

    // if no free slot found, then fail
    puts("ht_insert_nocheck: no free slot found");
    return HT_FAIL;
}

//--------------------------------------
// attempt to add or update an entry
//--------------------------------------
static int ht_add_or_update(HashTable* ht, ht_key_t key, ht_value_t value, int replace)
{
    CHECK_THAT(ht && ht->table);
    CHECK_THAT(key);

    // check for load factor and grow table if necessary
#if HT_AUTO_GROW
    // load factor of 0.5 to 0.67 is good time to grow
    if (HT_INV_LOAD_FACTOR * ht->entries >= ht->size)
    {
        if (!ht_grow(ht))
        {
            return HT_FAIL;
        }
    }
#endif

    ht_hash_t hash = ht->hash_fn(key);

    int result = ht_insert_nocheck(ht, ht->table, hash, key, value, ht->size, replace);
    return result;
}

//----------------------------------------
// insert an entry, fail if already exists
//----------------------------------------
int ht_insert(HashTable *ht, ht_key_t key, ht_value_t value)
{
	return ht_add_or_update(ht, key, value, HT_ADD_ONLY);
}

//--------------------------------------------------
// attempt to add an entry, update if already exists
//--------------------------------------------------
int ht_add(HashTable* ht, ht_key_t key, ht_value_t value)
{
	return ht_add_or_update(ht, key, value, HT_REPLACE);
}

//--------------------------------------
// attempt to remove entry from table
//--------------------------------------
int ht_remove(HashTable* ht, ht_key_t key)
{
    CHECK_THAT(ht && ht->table);

    // check for empty table
    if (ht->entries == 0)
        return HT_FAIL;

#if 1
    ht_hash_t hash = ht->hash_fn(key);

#if HT_PERTURB == 1
    size_t perturb = hash;
#else
    size_t perturb = 0;
#endif

    int done = 0;

    size_t start_bin = (size_t)hash & ht->mask;
    size_t bin = start_bin;
    HashTable_Entry* hte = &ht->table[start_bin];

    do
    {
        if (HASH_MATCH(hte, hash, key))
        {
            hte->hash = 0;
            hte->tombstone = 1;
            hte->key = 0;
            hte->value = 0;
            ht->entries--;
            return HT_OK;
        }
        HT_SEARCH_COLLIDE(ht);

        perturb >>= HT_PERTURB_VALUE;

#if HT_LINEAR == 1
        bin = (bin + perturb + 1) & ht->mask;
#else
        bin = (5 * bin + perturb + 1) & ht->mask;
#endif
        hte = &ht->table[bin];

//#if HT_PERTURB != 1
        done = bin == start_bin;
//#endif

    } while (!done);

#else
    // look for key in table and if found, remove
    HashTable_Entry *hte = ht->table;
    for (size_t i = 0; i < ht->size; i++)
    {
        // if found, mark entry as empty
        if (hte->key && ht->compare_fn(hte->key, key))
        {
			assert(hte->tombstone == 0);

            hte->hash = 0;
			hte->tombstone = 1;
            hte->key = 0;
            hte->value = 0;
            ht->entries--;
            return HT_OK;
        }
        hte++;
    }
#endif

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

        // if entry is not empty, re-hash into new table
        if (!HASH_EMPTY(hte))
        {
            if (HT_FAIL == ht_insert_nocheck(ht, new_table, hte->hash, hte->key, hte->value, new_size, HT_ADD_ONLY))
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
#if HT_TRACK_STATS == 1
    ht->recent_insert_collisions = 0;
#endif

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
    if (!ht || !ht->table)
        return;

#if HT_TRACK_STATS == 1
    printf("This table -> entries: %zu, size: %zu, insert collides: %zu, recent insert collides: %zu, search collides: %zu\n", ht->entries, ht->size, ht->insert_collisions, ht->recent_insert_collisions, ht->search_collisions);
#endif
}
