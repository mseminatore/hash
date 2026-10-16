//--------------------------------------
// Load factor benchmark
//
// Build one executable per threshold (see CMakeLists.txt bench targets)
// and compare time, probe counts and capacity.
//
// usage: bench [words_file]
//--------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hash.h"

#define STR_(x) #x
#define STR(x)  STR_(x)

#define PTR_KEYS    200000
#define CHURN_LIVE  1000
#define CHURN_OPS   2000000

static int str_compare(ht_key_t a, ht_key_t b)
{
    return strcmp((const char*)a, (const char*)b) == 0;
}

static double elapsed_ms(clock_t start)
{
    return 1000.0 * (double)(clock() - start) / CLOCKS_PER_SEC;
}

static void report(const char* scenario, const char* phase, size_t n, double ms, size_t probes, HashTable* ht)
{
    printf("%-6s %-8s %-6s %8zu keys %9.2f ms %7.1f ns/op %6.3f extra probes/op  cap %zu\n",
        STR(HT_LOAD_FACTOR_NUM) "/" STR(HT_LOAD_FACTOR_DEN), scenario, phase,
        n, ms, 1e6 * ms / (double)n, (double)probes / (double)n, ht_capacity(ht));
}

//--------------------------------------
// load the word list as NUL-terminated strings
//--------------------------------------
static char** load_words(const char* path, size_t* count)
{
    FILE* fp = fopen(path, "r");
    if (!fp)
        return NULL;

    size_t cap = 1024, n = 0;
    char** words = malloc(cap * sizeof(char*));
    char line[256];

    while (fgets(line, sizeof(line), fp))
    {
        size_t len = strcspn(line, "\r\n");
        line[len] = 0;
        if (n == cap)
        {
            cap <<= 1;
            words = realloc(words, cap * sizeof(char*));
        }
        words[n] = malloc(len + 1);
        memcpy(words[n++], line, len + 1);
    }

    fclose(fp);
    *count = n;
    return words;
}

//--------------------------------------
// string keys: insert all, find all, find misses
//--------------------------------------
static void bench_words(char** words, size_t n)
{
    // misses are the same words with a suffix, so they hash differently
    char** misses = malloc(n * sizeof(char*));
    for (size_t i = 0; i < n; i++)
    {
        size_t len = strlen(words[i]);
        misses[i] = malloc(len + 2);
        memcpy(misses[i], words[i], len);
        misses[i][len] = '#';
        misses[i][len + 1] = 0;
    }

    HashTable* ht = ht_create();
    ht_set_hash_func(ht, HT_HASH_STRING);
    ht_set_compare_func(ht, str_compare);

    clock_t t = clock();
    for (size_t i = 0; i < n; i++)
        ht_insert(ht, words[i], words[i]);
    report("words", "insert", n, elapsed_ms(t), ht->insert_collisions, ht);

    ht->search_collisions = 0;
    t = clock();
    for (size_t i = 0; i < n; i++)
        ht_find(ht, words[i]);
    report("words", "hit", n, elapsed_ms(t), ht->search_collisions, ht);

    ht->search_collisions = 0;
    t = clock();
    for (size_t i = 0; i < n; i++)
        ht_find(ht, misses[i]);
    report("words", "miss", n, elapsed_ms(t), ht->search_collisions, ht);

    ht_free(ht);
    for (size_t i = 0; i < n; i++)
        free(misses[i]);
    free(misses);
}

//--------------------------------------
// pointer keys with the default (address) hash
//--------------------------------------
static void bench_pointers(void)
{
    void** keys = malloc(2 * PTR_KEYS * sizeof(void*));
    for (size_t i = 0; i < 2 * PTR_KEYS; i++)
        keys[i] = malloc(16);

    HashTable* ht = ht_create();

    clock_t t = clock();
    for (size_t i = 0; i < PTR_KEYS; i++)
        ht_insert(ht, keys[i], keys[i]);
    report("ptrs", "insert", PTR_KEYS, elapsed_ms(t), ht->insert_collisions, ht);

    ht->search_collisions = 0;
    t = clock();
    for (size_t i = 0; i < PTR_KEYS; i++)
        ht_find(ht, keys[i]);
    report("ptrs", "hit", PTR_KEYS, elapsed_ms(t), ht->search_collisions, ht);

    ht->search_collisions = 0;
    t = clock();
    for (size_t i = PTR_KEYS; i < 2 * PTR_KEYS; i++)
        ht_find(ht, keys[i]);
    report("ptrs", "miss", PTR_KEYS, elapsed_ms(t), ht->search_collisions, ht);

    ht_free(ht);
    for (size_t i = 0; i < 2 * PTR_KEYS; i++)
        free(keys[i]);
    free(keys);
}

//--------------------------------------
// remove/insert churn over a small live set
//--------------------------------------
static void bench_churn(void)
{
    static int pool[CHURN_LIVE * 2];
    const size_t pool_size = sizeof(pool) / sizeof(pool[0]);

    HashTable* ht = ht_create();
    for (size_t i = 0; i < CHURN_LIVE; i++)
        ht_insert(ht, &pool[i], &pool[i]);

    ht->insert_collisions = 0;
    ht->search_collisions = 0;
    clock_t t = clock();
    for (size_t i = 0; i < CHURN_OPS; i++)
    {
        ht_remove(ht, &pool[i % pool_size]);
        ht_insert(ht, &pool[(i + CHURN_LIVE) % pool_size], &pool[0]);
    }
    report("churn", "rm+ins", CHURN_OPS, elapsed_ms(t), ht->insert_collisions + ht->search_collisions, ht);

    ht_free(ht);
}

int main(int argc, char* argv[])
{
    const char* paths[] = { argc > 1 ? argv[1] : "words_alpha.txt", "../words_alpha.txt", "../../words_alpha.txt" };
    char** words = NULL;
    size_t n = 0;

    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]) && !words; i++)
        words = load_words(paths[i], &n);

    if (words)
        bench_words(words, n);
    else
        fprintf(stderr, "words_alpha.txt not found, skipping string benchmark\n");

    bench_pointers();
    bench_churn();

    for (size_t i = 0; i < n; i++)
        free(words[i]);
    free(words);

    ht_finished();
    return 0;
}
