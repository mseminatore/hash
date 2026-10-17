//--------------------------------------
// Load factor benchmark
//
// Build one executable per threshold (see CMakeLists.txt bench targets)
// and compare time, probe counts and capacity. Each scenario runs 'reps'
// times on a fresh table; the best (least noisy) and median times are shown.
//
// usage: bench [words_file] [reps]
//--------------------------------------
#ifndef _WIN32
    #define _POSIX_C_SOURCE 200809L    // clock_gettime
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hash.h"

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

#define STR_(x) #x
#define STR(x)  STR_(x)

// probe counts read as 0 when stats are compiled out
#if HT_TRACK_STATS == 1
    #define STAT(ht, f)         ((ht)->f)
    #define STAT_RESET(ht, f)   ((ht)->f = 0)
#else
    #define STAT(ht, f)         ((size_t)0)
    #define STAT_RESET(ht, f)   ((void)0)
#endif

#define PTR_KEYS    200000
#define CHURN_LIVE  1000
#define CHURN_OPS   2000000

#define DEFAULT_REPS    5
#define MAX_REPS        100

static int reps = DEFAULT_REPS;

// lookup results are folded in here so the calls can't be optimized away
static volatile uintptr_t sink;

//--------------------------------------
// monotonic high-resolution clock, in nanoseconds
//--------------------------------------
static double now_ns(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;

    if (!freq.QuadPart)
        QueryPerformanceFrequency(&freq);

    QueryPerformanceCounter(&t);
    return 1e9 * (double)t.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return 1e9 * (double)ts.tv_sec + (double)ts.tv_nsec;
#endif
}

//--------------------------------------
// timings for one phase across all reps
//--------------------------------------
typedef struct
{
    const char* scenario;
    const char* phase;
    size_t ops;
    double ns[MAX_REPS];    // elapsed time per rep
    size_t probes;          // extra probes (same every rep)
    size_t capacity;
} Phase;

static int compare_double(const void* a, const void* b)
{
    double x = *(const double*)a, y = *(const double*)b;
    return (x > y) - (x < y);
}

static void report(Phase* p)
{
    qsort(p->ns, reps, sizeof(double), compare_double);
    double best = p->ns[0];
    double median = p->ns[reps / 2];

    printf("%-6s %-6s %-6s %8zu ops  best %8.2f ms %7.1f ns/op  median %7.1f ns/op  %6.3f extra probes/op  cap %zu\n",
        STR(HT_LOAD_FACTOR_NUM) "/" STR(HT_LOAD_FACTOR_DEN), p->scenario, p->phase,
        p->ops, best / 1e6, best / (double)p->ops, median / (double)p->ops,
        (double)p->probes / (double)p->ops, p->capacity);
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
// time insert, hit and miss phases over a fresh table each rep
//--------------------------------------
static void bench_lookup(const char* scenario, ht_hash_func hash_fn, ht_compare_func compare_fn,
    void** keys, void** misses, size_t n)
{
    Phase insert = { scenario, "insert", n };
    Phase hit = { scenario, "hit", n };
    Phase miss = { scenario, "miss", n };

    for (int r = 0; r < reps; r++)
    {
        HashTable* ht = ht_create();
        ht_set_hash_func(ht, hash_fn);
        ht_set_compare_func(ht, compare_fn);

        double t = now_ns();
        for (size_t i = 0; i < n; i++)
            ht_insert(ht, keys[i], keys[i]);
        insert.ns[r] = now_ns() - t;
        insert.probes = STAT(ht, insert_collisions);

        STAT_RESET(ht, search_collisions);
        t = now_ns();
        for (size_t i = 0; i < n; i++)
            sink += (uintptr_t)ht_find(ht, keys[i]);
        hit.ns[r] = now_ns() - t;
        hit.probes = STAT(ht, search_collisions);

        STAT_RESET(ht, search_collisions);
        t = now_ns();
        for (size_t i = 0; i < n; i++)
            sink += (uintptr_t)ht_find(ht, misses[i]);
        miss.ns[r] = now_ns() - t;
        miss.probes = STAT(ht, search_collisions);

        insert.capacity = hit.capacity = miss.capacity = ht_capacity(ht);

        ht_clear(ht);   // keys are owned by the caller
        ht_free(ht);
    }

    report(&insert);
    report(&hit);
    report(&miss);
}

//--------------------------------------
// string keys: misses are the same words with a suffix
//--------------------------------------
static int str_compare(ht_key_t a, ht_key_t b)
{
    return strcmp((const char*)a, (const char*)b) == 0;
}

static void bench_words(char** words, size_t n)
{
    char** misses = malloc(n * sizeof(char*));
    for (size_t i = 0; i < n; i++)
    {
        size_t len = strlen(words[i]);
        misses[i] = malloc(len + 2);
        memcpy(misses[i], words[i], len);
        misses[i][len] = '#';
        misses[i][len + 1] = 0;
    }

    bench_lookup("words", HT_HASH_STRING, str_compare, (void**)words, (void**)misses, n);

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

    bench_lookup("ptrs", HT_HASH_NULL, NULL, keys, keys + PTR_KEYS, PTR_KEYS);

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
    Phase churn = { "churn", "rm+ins", CHURN_OPS };

    for (int r = 0; r < reps; r++)
    {
        HashTable* ht = ht_create();
        for (size_t i = 0; i < CHURN_LIVE; i++)
            ht_insert(ht, &pool[i], &pool[i]);

        STAT_RESET(ht, insert_collisions);
        STAT_RESET(ht, search_collisions);
        double t = now_ns();
        for (size_t i = 0; i < CHURN_OPS; i++)
        {
            ht_remove(ht, &pool[i % pool_size]);
            ht_insert(ht, &pool[(i + CHURN_LIVE) % pool_size], &pool[0]);
        }
        churn.ns[r] = now_ns() - t;
        churn.probes = STAT(ht, insert_collisions) + STAT(ht, search_collisions);
        churn.capacity = ht_capacity(ht);

        ht_clear(ht);   // keys are static
        ht_free(ht);
    }

    report(&churn);
}

int main(int argc, char* argv[])
{
    if (argc > 2)
    {
        reps = atoi(argv[2]);
        if (reps < 1 || reps > MAX_REPS)
        {
            fprintf(stderr, "reps must be 1..%d\n", MAX_REPS);
            return 1;
        }
    }

    const char* paths[] = { argc > 1 ? argv[1] : "words_alpha.txt", "../words_alpha.txt", "../../words_alpha.txt" };
    char** words = NULL;
    size_t n = 0;

    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]) && !words; i++)
        words = load_words(paths[i], &n);

    printf("%d reps per scenario\n", reps);

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
