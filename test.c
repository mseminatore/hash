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

    ht = ht_init();
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
void test_insert()
{
    SUITE("Insert");

    for (int i = 0; i < 9; i++)
    {
        TEST(HT_OK == ht_insert(ht, (hash_value_t)keys[i], keys[i], avalue));
    }

    //TEST(HT_OK == ht_insert(ht, (hash_value_t)akey, akey, avalue));
    //TEST(HT_OK == ht_insert(ht, (hash_value_t)akey, akey, avalue));
    //TEST(HT_OK == ht_insert(ht, (hash_value_t)akey, akey, avalue));
    //TEST(HT_OK == ht_insert(ht, (hash_value_t)akey, akey, avalue));
    //TEST(HT_OK == ht_insert(ht, (hash_value_t)akey, akey, avalue));
    //TEST(HT_OK == ht_insert(ht, (hash_value_t)akey, akey, avalue));
    //TEST(HT_OK == ht_insert(ht, (hash_value_t)akey, akey, avalue));
    //TEST(HT_OK == ht_insert(ht, (hash_value_t)akey, akey, avalue));
    ht_stats(ht);
}

//--------------------------------------
//
//--------------------------------------
void test_find()
{
    SUITE("Find");

    TEST(ht_find(ht, (hash_value_t)keys[8], keys[8]));
}

//--------------------------------------
//
//--------------------------------------
void test_remove()
{
    SUITE("Remove");

}

//--------------------------------------
//
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
    test_remove();
    test_destroy();
}
