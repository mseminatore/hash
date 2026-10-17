#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hash.h"
#include "testy/test.h"

#ifdef _WIN32
    #define DIR_PREFIX "..\\"
#else
    #define DIR_PREFIX ""
#endif

HashTable *ht = NULL;

char* keys[] = {"The", "quick", "brown", "fox", "jumps ", "over", "the", "lazy", "dog"};
char *akey = "foo";
char *avalue = "bar";

// forward declarations for test helpers
static int compare(const void *a, const void *b);

//--------------------------------------
// Special hash function for testing collisions
//--------------------------------------
static ht_hash_t colliding_hash(const void* key)
{
    return 1;  // Force all keys to same hash bucket
}

static size_t count_empty(HashTable* ht)
{
    size_t n = 0;
    for (size_t i = 0; i < ht->size; i++)
        n += ht->table[i].key == NULL;
    return n;
}

//--------------------------------------
// Test that the default pointer hash spreads aligned keys
// (measured with probe stats, so only when they are tracked)
//--------------------------------------
#if HT_TRACK_STATS == 1
static double avg_probes_for_stride(char* base, size_t stride, size_t n)
{
    HashTable* ht = ht_create();
    for (size_t i = 0; i < n; i++)
        ht_insert(ht, base + i * stride, base);

    ht->search_collisions = 0;
    for (size_t i = 0; i < n; i++)
        ht_find(ht, base + i * stride);

    double avg = (double)ht->search_collisions / (double)n;
    ht_clear(ht);   // keys are static or freed elsewhere
    ht_free(ht);
    return avg;
}

static void test_pointer_hash_spread(void)
{
    SUITE("Pointer Hash Spread");

    enum { N = 4096, MAX_STRIDE = 64 };
    char* base = malloc(N * MAX_STRIDE);
    TEST(base != NULL);

    // keys at common alignments should average well under one extra probe
    TEST(avg_probes_for_stride(base, 4, N) < 0.5);
    TEST(avg_probes_for_stride(base, 16, N) < 0.5);
    TEST(avg_probes_for_stride(base, 64, N) < 0.5);

    free(base);
}
#endif

//--------------------------------------
// Test that updating an existing key at the load limit doesn't grow
//--------------------------------------
static void test_no_grow_on_update(void)
{
    SUITE("No Grow On Update");

    static int pool[64];
    HashTable* ht = ht_create();
    TEST(ht != NULL);

    // fill to exactly the load limit
    size_t n = 0;
    while (ht->entries * HT_LOAD_FACTOR_DEN < ht->size * HT_LOAD_FACTOR_NUM)
        ht_insert(ht, &pool[n], &pool[n]), n++;

    size_t cap = ht_capacity(ht);

    // replacing or re-adding an existing key needs no new slot
    TEST(HT_OK == ht_add(ht, &pool[0], &pool[1]));
    TEST(ht_find(ht, &pool[0]) == &pool[1]);
    TEST(HT_FAIL == ht_insert(ht, &pool[0], &pool[0]));
    TEST(ht_capacity(ht) == cap);

    // a new key still triggers growth
    TEST(HT_OK == ht_insert(ht, &pool[n], &pool[n]));
    TEST(ht_capacity(ht) == 2 * cap);
    TEST(ht_size(ht) == n + 1);

    ht_clear(ht);   // keys are static or freed elsewhere
    ht_free(ht);
}

//--------------------------------------
// Test that inserts into a small table never fail while slots are free.
// The probe sequence can revisit slots before perturb reaches zero, so a
// loop bounded at 'size' used to fail roughly 1 in 2,450 fills.
//--------------------------------------
static uint64_t probe_seed;

static uint64_t splitmix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

static ht_hash_t seeded_hash(ht_key_t key)
{
    return (ht_hash_t)splitmix64((uintptr_t)key ^ probe_seed);
}

static void test_small_table_insert(void)
{
    SUITE("Small Table Insert");

    static char pool[64];
    long fails = 0;

    for (long trial = 0; trial < 100000; trial++)
    {
        probe_seed = splitmix64((uint64_t)trial);

        HashTable* ht = ht_create();
        ht_set_hash_func(ht, seeded_hash);

        // fill the default-size table up to its grow limit
        for (int i = 0; ht->size == HT_DEFAULT_TABLE_SIZE
            && ht->entries * HT_LOAD_FACTOR_DEN < ht->size * HT_LOAD_FACTOR_NUM; i++)
        {
            if (HT_OK != ht_insert(ht, &pool[i], &pool[i]))
            {
                fails++;
                break;
            }
        }

        ht_clear(ht);   // keys are static
        ht_free(ht);
    }

    TEST(fails == 0);
}

//--------------------------------------
// Test that resizes that fit reuse the embedded small_table
//--------------------------------------
static void test_small_table_reuse(void)
{
    SUITE("Small Table Reuse");

    static int pool[64];
    HashTable* ht = ht_create();
    TEST(ht != NULL);
    TEST(ht->table == ht->small_table);

    // fill to the load limit, then remove everything, leaving only tombstones
    size_t n = 0;
    while (ht->entries * HT_LOAD_FACTOR_DEN < ht->size * HT_LOAD_FACTOR_NUM)
        ht_insert(ht, &pool[n], &pool[n]), n++;
    for (size_t i = 0; i < n; i++)
        ht_remove(ht, &pool[i]);
    TEST(ht->deleted == n);

    // the next insert rehashes in place and should stay in small_table
    TEST(HT_OK == ht_insert(ht, &pool[n], &pool[n]));
    TEST(ht->table == ht->small_table);
    TEST(ht_capacity(ht) == HT_DEFAULT_TABLE_SIZE);
    TEST(ht->deleted == 0);
    TEST(ht_find(ht, &pool[n]) == &pool[n]);

    // grow onto the heap, then shrink back into small_table
    TEST(ht_grow(ht) != NULL);
    TEST(ht->table != ht->small_table);
    TEST(ht_shrink(ht) != NULL);
    TEST(ht->table == ht->small_table);
    TEST(ht_capacity(ht) == HT_DEFAULT_TABLE_SIZE);
    TEST(ht_find(ht, &pool[n]) == &pool[n]);
    TEST(ht_size(ht) == 1);

    ht_clear(ht);   // keys are static or freed elsewhere
    ht_free(ht);
}

//--------------------------------------
// Test that remove/insert churn doesn't fill the table with tombstones
//--------------------------------------
static void test_tombstone_churn(void)
{
    SUITE("Tombstone Churn");

    enum { POOL = 4096, LIVE = 10 };
    static int pool[POOL];

    HashTable* ht = ht_create();
    TEST(ht != NULL);

    int ok = 1;
    for (int i = 0; i < POOL; i++)
    {
        ok &= HT_OK == ht_insert(ht, &pool[i], &pool[i]);
        if (i >= LIVE)
            ok &= HT_OK == ht_remove(ht, &pool[i - LIVE]);

        // there must always be empty slots to terminate probe chains
        ok &= count_empty(ht) > 0;
    }
    TEST(ok);

    // the table should stay small since the live set is small
    TEST(ht_size(ht) == LIVE);
    TEST(ht_capacity(ht) <= 64);

    // live keys are found, removed keys are not
    for (int i = POOL - LIVE; i < POOL; i++)
        ok &= ht_find(ht, &pool[i]) == &pool[i];
    for (int i = 0; i < POOL - LIVE; i++)
        ok &= !ht_contains(ht, &pool[i]);
    TEST(ok);

    // duplicate detection still works after churn
    TEST(HT_FAIL == ht_insert(ht, &pool[POOL - 1], &pool[0]));

    ht_clear(ht);   // keys are static or freed elsewhere
    ht_free(ht);
}

//--------------------------------------
// Test probe chain integrity after deletion
//--------------------------------------
static void test_delete_probe_chain(void)
{
    SUITE("Delete Probe Chain");

    HashTable* ht = ht_create();
    TEST(ht != NULL);

    // Force hash collisions for this test
    ht_set_hash_func(ht, colliding_hash);

    const char* keyA = "A";
    const char* keyB = "B";
    const char* keyC = "C";
    const char* keyD = "D";

    TEST(HT_OK == ht_insert(ht, keyA, keyA));
    TEST(HT_OK == ht_insert(ht, keyB, keyB));
    TEST(HT_OK == ht_insert(ht, keyC, keyC));

    TEST(ht_find(ht, keyA) == keyA);
    TEST(ht_find(ht, keyB) == keyB);
    TEST(ht_find(ht, keyC) == keyC);

    // Remove middle of probe chain
    TEST(HT_OK == ht_remove(ht, keyB));

    // Backward-shift should keep A and C findable
    TEST(ht_find(ht, keyA) == keyA);
    TEST(ht_find(ht, keyB) == NULL);
    TEST(ht_find(ht, keyC) == keyC);

    // Insert D into the freed slot
    TEST(HT_OK == ht_insert(ht, keyD, keyD));

    TEST(ht_find(ht, keyA) == keyA);
    TEST(ht_find(ht, keyC) == keyC);
    TEST(ht_find(ht, keyD) == keyD);

    // Remove first element and verify chain integrity
    TEST(HT_OK == ht_remove(ht, keyA));
    TEST(ht_find(ht, keyA) == NULL);
    TEST(ht_find(ht, keyC) == keyC);
    TEST(ht_find(ht, keyD) == keyD);
    TEST(ht_size(ht) == 2);

    ht_clear(ht);   // keys are static or freed elsewhere
    ht_free(ht);
}

//--------------------------------------
// Test ht_contains and ht_lookup
//--------------------------------------
static void test_contains_lookup(void)
{
    SUITE("Contains/Lookup");

    HashTable* ht = ht_create();
    ht_set_hash_func(ht, HT_HASH_STRING);
    ht_set_compare_func(ht, compare);

    const char* key = "hello";
    const char* val = "world";

    // not found
    TEST(HT_FAIL == ht_contains(ht, key));
    ht_value_t out = (ht_value_t)0xDEAD;
    TEST(HT_FAIL == ht_lookup(ht, key, &out));

    // insert and find
    TEST(HT_OK == ht_insert(ht, key, val));
    TEST(HT_OK == ht_contains(ht, key));
    TEST(HT_OK == ht_lookup(ht, key, &out));
    TEST(out == val);

    // lookup with NULL out_value is safe
    TEST(HT_OK == ht_lookup(ht, key, NULL));

    ht_clear(ht);   // keys are static or freed elsewhere
    ht_free(ht);
}

//--------------------------------------
// Test NULL value support
//--------------------------------------
static void test_null_values(void)
{
    SUITE("NULL Values");

    HashTable* ht = ht_create();
    ht_set_hash_func(ht, HT_HASH_STRING);
    ht_set_compare_func(ht, compare);

    const char* key = "nullval";

    // insert NULL value
    TEST(HT_OK == ht_insert(ht, key, NULL));
    TEST(ht_size(ht) == 1);

    // ht_find returns NULL — ambiguous
    TEST(ht_find(ht, key) == NULL);

    // ht_contains and ht_lookup distinguish it
    TEST(HT_OK == ht_contains(ht, key));
    ht_value_t out = (ht_value_t)0xDEAD;
    TEST(HT_OK == ht_lookup(ht, key, &out));
    TEST(out == NULL);

    // remove and verify gone
    TEST(HT_OK == ht_remove(ht, key));
    TEST(HT_FAIL == ht_contains(ht, key));
    TEST(ht_size(ht) == 0);

    ht_clear(ht);   // keys are static or freed elsewhere
    ht_free(ht);
}

//--------------------------------------
// Test perturbed probing under heavy collision
// Verifies that insert/find work when the probe
// sequence doesn't visit all slots linearly.
//--------------------------------------
static void test_perturb_collision(void)
{
    SUITE("Perturb Collision");

    HashTable* ht = ht_create();
    TEST(ht != NULL);

    // Force all keys to collide on the same bucket
    ht_set_hash_func(ht, colliding_hash);

    const char* keys[] = {"a","b","c","d","e","f","g"};
    int nkeys = sizeof(keys) / sizeof(keys[0]);
    int i;

    for (i = 0; i < nkeys; i++) {
        TEST(HT_OK == ht_insert(ht, keys[i], keys[i]));
    }

    TEST(ht_size(ht) == (size_t)nkeys);

    // Delete every other key to create tombstones
    for (i = 0; i < nkeys; i += 2) {
        TEST(HT_OK == ht_remove(ht, keys[i]));
    }

    // Remaining keys must still be findable
    for (i = 1; i < nkeys; i += 2) {
        TEST(ht_find(ht, keys[i]) == keys[i]);
    }

    // Re-insert deleted keys (should reuse tombstone slots)
    for (i = 0; i < nkeys; i += 2) {
        TEST(HT_OK == ht_insert(ht, keys[i], keys[i]));
    }

    // All keys present
    for (i = 0; i < nkeys; i++) {
        TEST(ht_find(ht, keys[i]) == keys[i]);
    }

    TEST(ht_size(ht) == (size_t)nkeys);

    ht_clear(ht);   // keys are static or freed elsewhere
    ht_free(ht);
}

//--------------------------------------
// hash used for testing
//--------------------------------------
static ht_hash_t hash(const void *key)
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
// case sensistive string compare
//--------------------------------------
static int compare(const void *a, const void *b)
{
    return strcmp((const char*)a, (const char*)b) == 0;
}

//--------------------------------------
// test table creation
//--------------------------------------
void test_create()
{
    SUITE("Create");

    TEST(ht == NULL);

    ht = ht_create();
    TEST(ht != NULL);
    TEST(ht->table != NULL);
    TEST(ht->entries == 0);
    TEST(ht->size == HT_DEFAULT_TABLE_SIZE);
    TEST(ht->compare_fn != NULL);
    TEST(ht->hash_fn != NULL);

#if HT_TRACK_STATS == 1
    TEST(ht->insert_collisions == 0);
    TEST(ht->search_collisions == 0);
    TEST(ht->recent_insert_collisions == 0);
#endif
}

//--------------------------------------
// test hash and compare function setting
//--------------------------------------
void test_set_funcs()
{
	SUITE("Set Funcs");

	TEST(ht_set_hash_func(ht, hash) == HT_OK);
	TEST(ht->hash_fn != NULL);
	TEST(ht->hash_fn == hash);
	TEST(ht_set_compare_func(ht, compare) == HT_OK);
	TEST(ht->compare_fn != NULL);
	TEST(ht->compare_fn == compare);
}

//--------------------------------------
// test table size queries
//--------------------------------------
void test_size()
{
    SUITE("Size");

    TEST(ht_capacity(ht) == HT_DEFAULT_TABLE_SIZE);
    TESTEX("empty table size is 0", ht_size(ht) == 0);
}

//--------------------------------------
//
//--------------------------------------
void print_table()
{
    ht_key_t key;
    ht_value_t value;

    puts("{");
    size_t index = 0;
    while(ht_next(ht, &index, &key, &value))
    {
        printf("'%s' : '%s',\n", (char*)key, (char*)value);
    }
    puts("}\n");
}

//--------------------------------------
// test inserting items
//--------------------------------------
void test_insert()
{
    SUITE("Insert");

    for (int i = 0; i < ARRAY_SIZE(keys); i++)
    {
        TEST(HT_OK == ht_insert(ht, keys[i], keys[i]));
    }

    // inserting duplicate should fail
    TEST(HT_FAIL == ht_insert(ht, keys[0], keys[0]));

	// add or update existing item should succeed
    TEST(HT_OK == ht_add(ht, keys[0], keys[0]));

    //print_table();

    ht_stats(ht);

    TEST(NULL == ht_shrink(ht));
    TEST(ht_grow(ht) != NULL);
    TEST(ht_grow(ht) != NULL);
    TEST(NULL != ht_shrink(ht));
    ht_stats(ht);
}

//--------------------------------------
// test finding table items
//--------------------------------------
void test_find()
{
    SUITE("Find");

    for (int i = 0; i < ARRAY_SIZE(keys); i++)
    {
        TEST(ht_find(ht, keys[i]));
    }
}

//--------------------------------------
// test iterating over table items
//--------------------------------------
void test_iterate()
{
    SUITE("Iterate");

    size_t count = 0, index = 0;
    while(ht_next(ht, &index, NULL, NULL))
    {
        count++;
    }

    TEST(count == ht_size(ht));

    // iteration past the end must fail cleanly
    index = ht_capacity(ht);
    TEST(HT_FAIL == ht_next(ht, &index, NULL, NULL));
    index = (size_t)-1;
    TEST(HT_FAIL == ht_next(ht, &index, NULL, NULL));
}

//--------------------------------------
// test removing table items
//--------------------------------------
void test_remove()
{
    SUITE("Remove");

    for (int i = 0; i < ARRAY_SIZE(keys); i++)
    {
        TEST(HT_OK == ht_remove(ht, keys[i]));
    }

	TEST(ht_size(ht) == 0);
}

//--------------------------------------
// test cleanup
//--------------------------------------
void test_destroy()
{
    SUITE("Destroy");

    TEST(ht != NULL);
    TEST(ht->table != NULL);

    ht_clear(ht);   // keys are static or freed elsewhere
    TEST(HT_OK == ht_free(ht));
    ht = NULL; // Ensure ht is reset
}

//--------------------------------------
//
//--------------------------------------
void test_big_words()
{
    SUITE("Big Words");
    int word_count = 0;

    // try several locations for the wordlist to be robust to working directory
    const char *candidates[] = {
        DIR_PREFIX"words_alpha.txt",
        "words_alpha.txt",
        "..\\words_alpha.txt",
        "../words_alpha.txt",
        NULL
    };

    FILE *fp = NULL;
    for (int i = 0; candidates[i] != NULL; ++i)
    {
        fp = fopen(candidates[i], "r");
        if (fp != NULL)
            break;
    }

    if (fp == NULL)
    {
        perror("fopen");
        return;
    }

    ht = ht_create();
	TEST(ht != NULL);
	
    ht_set_hash_func(ht, HT_HASH_STRING);
	ht_set_compare_func(ht, compare);

    char word[256];
    while(fgets(word, sizeof(word), fp))
	{
		char *p = strchr(word, '\n');
		if (p) *p = 0;
        char *pword = strdup(word);
		
		ht_insert(ht, pword, pword);
        assert(ht_find(ht, pword));

        word_count++;
	}

    TEST(word_count == ht_size(ht));

	// debug test failure
    if (word_count != ht_size(ht))
		printf("word count = %d, table count = %zd\n", word_count, ht_size(ht));

    fclose(fp);

    // the table doesn't own the strdup'd keys, so free them here
    size_t index = 0;
    ht_key_t key;
    while (ht_next(ht, &index, &key, NULL))
        free((void*)key);
    ht_clear(ht);
}

//--------------------------------------
//
//--------------------------------------
void test_main(int argc, char *argv[])
{
    MODULE("hashtable");

    test_create();
    test_size();
    test_set_funcs();
    test_insert();
    test_find();
    test_iterate();
    test_remove();
    test_delete_probe_chain();
    test_perturb_collision();
    test_tombstone_churn();
    test_no_grow_on_update();
    test_small_table_reuse();
    test_small_table_insert();
#if HT_TRACK_STATS == 1
    test_pointer_hash_spread();
#endif
    ht_stats(ht);
    test_destroy();

    ht = NULL;
    test_contains_lookup();
    test_null_values();
    test_big_words();
    ht_stats(ht);
    test_destroy();

    //test_create();
    //test_destroy();

    ht_debug_stats();
    ht_finished();
}
