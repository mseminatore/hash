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
#define HT_PERTURB      1   // randomize probes

// warn on ht_free of a non-empty table (debug builds by default)
#ifndef HT_WARN_FREE_NONEMPTY
    #ifdef _DEBUG
        #define HT_WARN_FREE_NONEMPTY 1
    #else
        #define HT_WARN_FREE_NONEMPTY 0
    #endif
#endif

// tombstone marker: a unique address that can never be a valid user key
static const char ht_tombstone_sentinel = 0;
#define HT_TOMBSTONE ((ht_key_t)&ht_tombstone_sentinel)

// helper macros
#define HASH_MATCH(hte, hash, key)  ((hte)->key && (hte)->key != HT_TOMBSTONE && (hte)->hash == hash && ht->compare_fn((hte)->key, key))
#define HASH_EMPTY(hte)             ((hte)->key == NULL)
#define HASH_DELETED(hte)           ((hte)->key == HT_TOMBSTONE)
#define HASH_USABLE(hte)            (HASH_EMPTY(hte) || HASH_DELETED(hte))

// true when 'used' slots reach the load factor threshold of 'size'
#define HT_AT_LOAD_LIMIT(used, size)    ((used) * HT_LOAD_FACTOR_DEN >= (size) * HT_LOAD_FACTOR_NUM)

// probe loop bound is size + HT_PROBE_EXTRA. While perturb is nonzero the
// sequence can revisit slots; it reaches 0 after this many shifts, and from
// then on the next 'size' steps visit every slot. Bounding at just 'size'
// let inserts into small tables fail with empty slots still available.
#define HT_PROBE_EXTRA  ((sizeof(size_t) * 8 + HT_PERTURB_VALUE - 1) / HT_PERTURB_VALUE)

static HashTable* ht_resize(HashTable* ht, size_t new_size);

#ifdef _DEBUG
#   define CHECK_THAT(cond)            assert(cond); if (!(cond)) return 0;
#else
#   define CHECK_THAT(cond)            if (!(cond)) return 0;
#endif

// maintain a free list of already alloc'd tables
// When HT_THREAD_SAFE, use per-thread free lists to avoid races
#ifdef HT_THREAD_SAFE
  #ifdef _MSC_VER
    #define HT_THREADLOCAL __declspec(thread)
  #else
    #define HT_THREADLOCAL __thread
  #endif
#else
  #define HT_THREADLOCAL
#endif

static HT_THREADLOCAL HashTable *ht_free_list[HT_MAX_FREE];
static HT_THREADLOCAL int ht_free_count = 0;

#if HT_DEBUG_STATS == 1
    static HT_THREADLOCAL size_t allocs = 0;
    static HT_THREADLOCAL size_t frees = 0;
    static HT_THREADLOCAL size_t resuse = 0;
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
// default hash is the key pointer, bit-mixed
//--------------------------------------
static ht_hash_t default_hash_fn(ht_key_t key)
{
    CHECK_THAT(key);
    // pointers are aligned and often evenly spaced, so their low bits
    // cluster under the mask-based bin index. Mix with a murmur3-style
    // finalizer so every address bit affects the low bits.
    // (cheaper xor-shift and Fibonacci mixes were tried; both left
    // collisions for some key strides.)
    uintptr_t h = (uintptr_t)key;
#if UINTPTR_MAX > 0xFFFFFFFFu
    h ^= h >> 33;
    h *= (uintptr_t)0xff51afd7ed558ccdull;
    h ^= h >> 33;
#else
    h ^= h >> 16;
    h *= (uintptr_t)0x85ebca6bu;
    h ^= h >> 16;
#endif
    return (ht_hash_t)h;
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

    // unsigned 32-bit math: wraparound is well-defined and matches the algorithm
    uint32_t h = 3323198485u;

    for (; *s; ++s)
    {
        h ^= *s;
        h *= 0x5bd1e995u;
        h ^= h >> 15;
    }
    return (ht_hash_t)h;
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
HashTable *ht_create(void)
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
    ht->deleted = 0;
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

#if HT_WARN_FREE_NONEMPTY
    // the table never owns keys/values, so freeing a non-empty table
    // may leak them if the caller still needed to free them
    if (ht->entries > 0)
        fprintf(stderr, "ht_free: table %p freed with %zu entries\n", (void*)ht, ht->entries);
#endif

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
    ht->deleted = 0;
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
// clear all entries (caller must free keys/values)
//--------------------------------------
void ht_clear(HashTable *ht)
{
    if (!ht || !ht->table) return;

    /* If table grew beyond small_table, free it and reset to small_table */
    if (ht->table != ht->small_table)
    {
        HT_FREE(ht->table);
        HT_FREE_INC;
        ht->table = ht->small_table;
        ht->size = HT_DEFAULT_TABLE_SIZE;
        ht->mask = ht->size - 1;
    }

    memset(ht->table, 0, sizeof(HashTable_Entry) * ht->size);
    ht->entries = 0;
    ht->deleted = 0;

#if HT_TRACK_STATS == 1
    ht->insert_collisions = 0;
    ht->search_collisions = 0;
    ht->recent_insert_collisions = 0;
#endif
}

//--------------------------------------
// cleanup the free list
//--------------------------------------
void ht_finished(void)
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
    CHECK_THAT(ipos);

    // get index and check bounds
    size_t index = *ipos;
    if (index >= ht->size)
        return HT_FAIL;

    // find next live table entry (skip empty slots and tombstones)
    while (index < ht->size && (HASH_EMPTY(&ht->table[index]) || HASH_DELETED(&ht->table[index])))
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

    if (ht->entries == 0)
        return NULL;

    ht_hash_t hash = ht->hash_fn(key);

#if HT_PERTURB == 1
    size_t perturb = hash;
#else
    size_t perturb = 0;
#endif

    size_t bin = (size_t)hash & ht->mask;
    HashTable_Entry* hte = &ht->table[bin];

    for (size_t i = 0; i < ht->size + HT_PROBE_EXTRA; i++)
    {
        if (HASH_EMPTY(hte))
            return NULL;

        if (HASH_MATCH(hte, hash, key))
            return hte->value;

        // skip tombstones — they don't terminate the probe chain
        HT_SEARCH_COLLIDE(ht);
        perturb >>= HT_PERTURB_VALUE;

#if HT_LINEAR == 1
        bin = (bin + perturb + 1) & ht->mask;
#else
        bin = (5 * bin + perturb + 1) & ht->mask;
#endif
        hte = &ht->table[bin];
    }

    return NULL;
}

//--------------------------------------
// check if a key exists in the table
//--------------------------------------
int ht_contains(HashTable *ht, ht_key_t key)
{
    CHECK_THAT(ht && ht->table);
    CHECK_THAT(key);

    if (ht->entries == 0)
        return HT_FAIL;

    ht_hash_t hash = ht->hash_fn(key);

#if HT_PERTURB == 1
    size_t perturb = hash;
#else
    size_t perturb = 0;
#endif

    size_t bin = (size_t)hash & ht->mask;
    HashTable_Entry* hte = &ht->table[bin];

    for (size_t i = 0; i < ht->size + HT_PROBE_EXTRA; i++)
    {
        if (HASH_EMPTY(hte))
            return HT_FAIL;

        if (HASH_MATCH(hte, hash, key))
            return HT_OK;

        HT_SEARCH_COLLIDE(ht);
        perturb >>= HT_PERTURB_VALUE;

#if HT_LINEAR == 1
        bin = (bin + perturb + 1) & ht->mask;
#else
        bin = (5 * bin + perturb + 1) & ht->mask;
#endif
        hte = &ht->table[bin];
    }

    return HT_FAIL;
}

//--------------------------------------
// lookup a key, distinguishing not-found from NULL values
//--------------------------------------
int ht_lookup(HashTable *ht, ht_key_t key, ht_value_t *out_value)
{
    CHECK_THAT(ht && ht->table);
    CHECK_THAT(key);

    if (ht->entries == 0)
        return HT_FAIL;

    ht_hash_t hash = ht->hash_fn(key);

#if HT_PERTURB == 1
    size_t perturb = hash;
#else
    size_t perturb = 0;
#endif

    size_t bin = (size_t)hash & ht->mask;
    HashTable_Entry* hte = &ht->table[bin];

    for (size_t i = 0; i < ht->size + HT_PROBE_EXTRA; i++)
    {
        if (HASH_EMPTY(hte))
            return HT_FAIL;

        if (HASH_MATCH(hte, hash, key))
        {
            if (out_value)
                *out_value = hte->value;
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
    }

    return HT_FAIL;
}

//--------------------------------------
// internal insert
//--------------------------------------
static int ht_insert_nocheck(HashTable *ht, HashTable_Entry* table, ht_hash_t hash, ht_key_t key, ht_value_t value, size_t size, int replace)
{
    CHECK_THAT(ht && table);
    CHECK_THAT(key);
    CHECK_THAT(size && !(size & (size - 1)));

#if HT_PERTURB == 1
    size_t perturb = hash;
#else
    size_t perturb = 0;
#endif

    size_t mask = size - 1;
    size_t bin = (size_t)hash & mask;
    HashTable_Entry* hte = &table[bin];

    // track first tombstone for reuse
    HashTable_Entry* first_tombstone = NULL;

    for (size_t i = 0; i < size + HT_PROBE_EXTRA; i++)
    {
        if (HASH_EMPTY(hte))
        {
            // prefer reusing a tombstone slot if we found one
            if (first_tombstone)
                hte = first_tombstone;

            hte->hash = hash;
            hte->key = key;
            hte->value = value;

            if(ht->table == table)
            {
                ht->entries++;
                if (first_tombstone)
                    ht->deleted--;
            }

            return HT_OK;
        }

        if (HASH_DELETED(hte))
        {
            // remember first tombstone for reuse, but keep probing
            // to check for duplicates
            if (!first_tombstone)
                first_tombstone = hte;
        }
        else if (HASH_MATCH(hte, hash, key))
        {
            if (!replace)
                return HT_FAIL;

            hte->value = value;
            return HT_OK;
        }

        HT_INSERT_COLLIDE(ht);
        HT_RECENT_INSERT_COLLIDE(ht);

        perturb >>= HT_PERTURB_VALUE;

#if HT_LINEAR == 1
        bin = (bin + perturb + 1) & mask;
#else
        bin = (5 * bin + perturb + 1) & mask;
#endif
        hte = &table[bin];
    }

    // probed all slots — use tombstone if available
    if (first_tombstone)
    {
        first_tombstone->hash = hash;
        first_tombstone->key = key;
        first_tombstone->value = value;

        if(ht->table == table)
        {
            ht->entries++;
            ht->deleted--;
        }

        return HT_OK;
    }

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
    // tombstones occupy slots and lengthen probe chains, so count them
    // toward load; if most of the fill is tombstones, rehash in place.
    // An existing key needs no new slot (replaced, or rejected for add-only),
    // so don't resize for it; the extra lookup only happens at the limit.
    if (HT_AT_LOAD_LIMIT(ht->entries + ht->deleted, ht->size) && !ht_contains(ht, key))
    {
        // only rehash in place when at least half the fill is tombstones,
        // so the freed space pays for the O(n) rehash
        size_t new_size = HT_AT_LOAD_LIMIT(2 * (ht->entries + 1), ht->size) ? ht->size << 1 : ht->size;

        if (!ht_resize(ht, new_size))
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
// Marks the slot with a tombstone sentinel to preserve probe chains.
// Tombstones are cleaned up naturally during resize/rehash.
//--------------------------------------
int ht_remove(HashTable* ht, ht_key_t key)
{
    CHECK_THAT(ht && ht->table);

    if (ht->entries == 0)
        return HT_FAIL;

    ht_hash_t hash = ht->hash_fn(key);

#if HT_PERTURB == 1
    size_t perturb = hash;
#else
    size_t perturb = 0;
#endif

    size_t bin = (size_t)hash & ht->mask;
    HashTable_Entry* hte = &ht->table[bin];

    for (size_t i = 0; i < ht->size + HT_PROBE_EXTRA; i++)
    {
        if (HASH_EMPTY(hte))
            return HT_FAIL;

        if (HASH_MATCH(hte, hash, key))
        {
            hte->hash = 0;
            hte->key = HT_TOMBSTONE;
            hte->value = 0;
            ht->entries--;
            ht->deleted++;
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
    }

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

    // tables that fit go back in the embedded small_table instead of the heap.
    // The source may be small_table itself (same-size rehash), so rehash into
    // a stack buffer first and copy it in afterwards.
    int to_small = new_size <= HT_DEFAULT_TABLE_SIZE;
    HashTable_Entry small_buf[HT_DEFAULT_TABLE_SIZE];

    size_t new_table_size = sizeof(HashTable_Entry) * new_size;
    HashTable_Entry* new_table;

    if (to_small)
    {
        new_table = small_buf;
    }
    else
    {
        new_table = HT_ALLOC(new_table_size);
        if (!new_table)
        {
            return NULL;
        }

        HT_ALLOC_INC;
    }

    memset(new_table, 0, new_table_size);

    // re-insert existing items into new table
    HashTable_Entry* hte;
    for (size_t i = 0; i < ht->size; i++)
    {
        hte = &ht->table[i];

        // skip empty slots and tombstones — only re-insert live entries
        if (!HASH_EMPTY(hte) && !HASH_DELETED(hte))
        {
            if (HT_FAIL == ht_insert_nocheck(ht, new_table, hte->hash, hte->key, hte->value, new_size, HT_ADD_ONLY))
            {
                if (!to_small)
                {
                    HT_FREE(new_table);
                    HT_FREE_INC;
                }
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

    if (to_small)
    {
        memcpy(ht->small_table, small_buf, new_table_size);
        new_table = ht->small_table;
    }

    // update hash table state
    ht->table = new_table;
    ht->size = new_size;
    ht->mask = new_size - 1;
    ht->deleted = 0;

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
    if (HT_AT_LOAD_LIMIT(ht->entries, new_size))
        return NULL;

    return ht_resize(ht, new_size);
}

//--------------------------------------
// print some useful debug stats
//--------------------------------------
void ht_debug_stats(void)
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
