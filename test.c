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

    for (i = 0; i < nkeys; i++)
        TEST(HT_OK == ht_insert(ht, keys[i], keys[i]));

    TEST(ht_size(ht) == (size_t)nkeys);

    // Delete every other key to create tombstones
    for (i = 0; i < nkeys; i += 2)
        TEST(HT_OK == ht_remove(ht, keys[i]));

    // Remaining keys must still be findable
    for (i = 1; i < nkeys; i += 2)
        TEST(ht_find(ht, keys[i]) == keys[i]);

    // Re-insert deleted keys (should reuse tombstone slots)
    for (i = 0; i < nkeys; i += 2)
        TEST(HT_OK == ht_insert(ht, keys[i], keys[i]));

    // All keys present
    for (i = 0; i < nkeys; i++)
        TEST(ht_find(ht, keys[i]) == keys[i]);

    TEST(ht_size(ht) == (size_t)nkeys);

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
