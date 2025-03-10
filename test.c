#include <stdio.h>
#include "hash.h"
#include "testy/test.h"

HashTable *ht = NULL;

char* keys[] = {"The", "quick", "brown", "fox", "jumps ", "over", "the", "lazy", "dog"};
char *akey = "foo";
char *avalue = "bar";

//--------------------------------------
//
//--------------------------------------
void test_create()
{
    SUITE("Create");

    TEST(ht == NULL);

    ht = ht_create();
    TEST(ht != NULL);
    TEST(ht->table != NULL);
    TEST(ht->entries == 0);
    TEST(ht->collisions == 0);
    TEST(ht->recent_collisions == 0);
}

//--------------------------------------
//
//--------------------------------------
void test_size()
{
    SUITE("Size");

    TEST(ht_capacity(ht) == HT_DEFAULT_SIZE);
    TEST(ht_size(ht) == 0);
}

//--------------------------------------
//
//--------------------------------------
void print_table()
{
    key_value_t key;
    value_value_t value;

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

    for (int i = 0; i < 9; i++)
    {
        TEST(HT_OK == ht_insert(ht, (hash_value_t)keys[i], keys[i], keys[i]));
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

    for (int i = 0; i < 9; i++)
    {
        TEST(ht_find(ht, (hash_value_t)keys[i], keys[i]));
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

    TEST(count == 9);
}

//--------------------------------------
// test removing table items
//--------------------------------------
void test_remove()
{
    SUITE("Remove");

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
void test_main(int argc, char *argv[])
{
    MODULE("hashtable");

    test_create();
    test_size();
    test_insert();
    test_find();
    test_iterate();
    test_remove();
    test_destroy();

    ht = NULL;
    test_create();
    test_destroy();

    ht_debug_stats();
}
