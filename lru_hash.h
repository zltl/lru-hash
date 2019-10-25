#ifndef UTILS_LRU_HASH_H_
#define UTILS_LRU_HASH_H_

#include "list.h"
#include "slice.h"

/* LRU hash cache implementtation */
/* Cache entries have an "in_cache" boolean indicating whether the cache hash a
 * reference on the entry. The only ways that this can become false without the
 * entry being passed to its "deleter" are via erase(), via insert() when an
 * element with a duplicate key is inserted, or on destruction of the cache.
 *
 * The cache keeps two linked lists of items in the cache. All items in the
 * cache are in one list or the other, and never both. Items still referenced by
 * clients but erased from the cache are in neither lists. The lists are:
 * - in-use: contains the items currently referenced by clients, in no
 *   particular order.
 * - LRU: contains the items not currently referenced by clients, in LRU order.
 *   Elements are moved between these lists by the ref() and unref() methods,
 *   when they detect an element in the cache acquiring or losing its only its
 *   only external reference. */

/* lru_hash entry */
struct lru_entry {
    slice key;                                 // key of entry
    void* value;                               // pointer to value
    void (*deleter)(slice* key, void* value);  // deleter of value
    size_t charge;
    struct lru_entry* next_hash;  // pointer to next entry hash bucket
    struct list_head list;
    uint32_t hash;  // hash of key
    uint32_t refs;
    int in_cache;  // whether entry is in the cache.
                   // TODO: remove this variable to simplenize codes.
};

#define LRU_HASH_FIRST_BUCKET_SIZE 4
/* hash table of lru_entry */
struct lru_hash_table {
    /* The table consists of an array of buckets where each bucket is a linked
     * list of lru_entry that hash into the bucket. */
    uint32_t length;
    uint32_t elems;
    struct lru_entry** buckets;
    /* prealloc few buckets when initialize. */
    struct lru_entry* bucket_init[LRU_HASH_FIRST_BUCKET_SIZE];
};

/* lru_hash, auto remove old entry.

   ref: move entry from list lru to in_use tail, add refcount
   unref: --ref, move from in_use to lru if refs==1
   insert: if full, delete list lru

   need lock for multithraed.
*/
struct lru_hash_s {
    size_t capacity;
    size_t usage;
    /* head is oldest entry, tail is newest entry */
    struct list_head lru;
    /* entries are in use by clients, and have refs >= 2 and in_cache=true. */
    struct list_head in_use;
    /* hash table */
    struct lru_hash_table table;
};

typedef struct lru_hash_s lru_hash_t;

void lru_init(lru_hash_t* l, size_t capacity);

struct lru_entry* lru_lookup(lru_hash_t* l, const slice* key, uint32_t hash);

struct lru_entry* lru_insert(lru_hash_t* l, const slice* key, uint32_t hash,
                             void* value, size_t charge,
                             void (*deleter)(slice* key, void* value));

/* lru_release same as lru_unref, lru_release deprecated */
void lru_release(lru_hash_t* l, struct lru_entry* e);
void lru_unref(lru_hash_t* l, struct lru_entry* e);

void lru_erase(lru_hash_t* l, const slice* key, uint32_t hash);

void lru_prune(lru_hash_t* l);

void lru_clean(lru_hash_t* l);

void lru_add_charge(lru_hash_t* l, struct lru_entry* e, int charge);

void lru_change_charge(lru_hash_t* l, struct lru_entry* e, size_t charge);

void lru_keep_small(lru_hash_t* l);

#endif /* UTILS_LRU_HASH_H_ */
