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

//--------------------------------------
// Special hash function for testing tombstones
//--------------------------------------
static ht_hash_t colliding_hash(const void* key)
{
    return 1;  // Force all keys to same hash bucket
}

//--------------------------------------
// Test probe chain integrity with tombstones
//--------------------------------------
static void test_tombstone_reuse(void)
{
    SUITE("Tombstone Reuse");

    HashTable* ht = ht_create();
    TEST(ht != NULL);

    // Force hash collisions for this test
    ht_set_hash_func(ht, colliding_hash);

    // Insert three items that will collide
    const char* keyA = "A";
    const char* keyB = "B";
    const char* keyC = "C";
    const char* keyD = "D";

    TEST(HT_OK == ht_insert(ht, keyA, keyA));
    TEST(HT_OK == ht_insert(ht, keyB, keyB));
    TEST(HT_OK == ht_insert(ht, keyC, keyC));

    // Verify all items are findable
    TEST(ht_find(ht, keyA) == keyA);
    TEST(ht_find(ht, keyB) == keyB);
    TEST(ht_find(ht, keyC) == keyC);

    // Remove B, leaving a tombstone
    TEST(HT_OK == ht_remove(ht, keyB));

    // Verify A and C are still findable
    TEST(ht_find(ht, keyA) == keyA);
    TEST(ht_find(ht, keyB) == NULL);
    TEST(ht_find(ht, keyC) == keyC);

    // Insert D which should reuse B's tombstone
    TEST(HT_OK == ht_insert(ht, keyD, keyD));

    // Verify all items are still findable
    TEST(ht_find(ht, keyA) == keyA);
    TEST(ht_find(ht, keyC) == keyC);
    TEST(ht_find(ht, keyD) == keyD);

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
    int count = 0;

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
    ht_set_hash_func(ht, HT_HASH_STRING);
	ht_set_compare_func(ht, compare);

    char word[256];
    while(fgets(word, sizeof(word), fp))
	{
		char *p = strchr(word, '\n');
		if (p) *p = 0;
        char *pword = strdup(word);
		assert(HT_OK == ht_insert(ht, pword, pword));
        count++;
	}

    TEST(count == ht_size(ht));
    
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
    test_tombstone_reuse();
    ht_stats(ht);
    test_destroy();

    ht = NULL;
    test_big_words();
    ht_stats(ht);
    test_destroy();

    //test_create();
    //test_destroy();

    ht_debug_stats();
    ht_finished();
}
