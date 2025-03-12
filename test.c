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
// MurmurOAAT32
//--------------------------------------
static uint32_t hash(const char* key)
{
    uint32_t h = 3323198485ul;
    for (; *key; ++key) 
    {
        h ^= *key;
        h *= 0x5bd1e995;
        h ^= h >> 15;
    }
    return h;
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
    TEST(ht->insert_collisions == 0);
    TEST(ht->search_collisions == 0);
    TEST(ht->recent_insert_collisions == 0);
}

//--------------------------------------
// test table size queries
//--------------------------------------
void test_size()
{
    SUITE("Size");

    TEST(ht_capacity(ht) == HT_DEFAULT_SIZE);
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
        TEST(HT_OK == ht_insert(ht, (ht_hash_t)hash(keys[i]), keys[i], keys[i]));
    }

    print_table();

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
        TEST(ht_find(ht, (ht_hash_t)hash(keys[i]), keys[i]));
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
}

//--------------------------------------
//
//--------------------------------------
void test_big_words()
{
    SUITE("Big Words");

    FILE *fp = fopen(DIR_PREFIX"words_alpha.txt", "r");
    if (fp == NULL)
	{
		perror("fopen");
		return;
	}

    ht = ht_create();
    char word[256];
    while(fgets(word, sizeof(word), fp))
	{
		char *p = strchr(word, '\n');
		if (p) *p = 0;
        char *pword = strdup(word);
		assert(HT_OK == ht_insert(ht, (ht_hash_t)hash(word), pword, pword));
	}

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
    test_insert();
    test_find();
    test_iterate();
    test_remove();
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
