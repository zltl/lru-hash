#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"
#include "lru_hash.h"
#include "slice.h"

static void lru_hash_table_init(struct lru_hash_table* h) {
    memset(h, 0, sizeof(struct lru_hash_table));
    h->length = LRU_HASH_FIRST_BUCKET_SIZE;
    h->elems = 0;

    h->buckets = h->bucket_init;
}

static void lru_hash_table_clean(struct lru_hash_table* h) {
    h->length = 0;
    h->elems = 0;
    if (h->buckets != h->bucket_init) {
        free(h->buckets);
        h->buckets = h->bucket_init;
    }
    memset(h->bucket_init, 0, sizeof(h->bucket_init));
}

/* resize hash table h, return -1 in failed, 0 success. */
static int lru_hash_table_resize(struct lru_hash_table* h) {
    uint32_t new_length = 4;
    while (new_length < h->elems) {
        new_length *= 2;
    }
    struct lru_entry** new_buckets;
    if (new_length <= LRU_HASH_FIRST_BUCKET_SIZE) {
        if (h->buckets == h->bucket_init) {
            return 0;
        }
        new_buckets = h->bucket_init;
        new_length = LRU_HASH_FIRST_BUCKET_SIZE;
    } else {
        new_buckets =
            (struct lru_entry**)malloc(new_length * sizeof(struct lru_entry*));
        if (new_buckets == NULL) {
            return -1;
        }
    }
    memset(new_buckets, 0, sizeof(new_buckets[0]) * new_length);

    uint32_t count = 0;
    uint32_t i;
    for (i = 0; i < h->length; i++) {
        struct lru_entry* e = h->buckets[i];
        while (e != NULL) {
            struct lru_entry* next = e->next_hash;
            uint32_t hash = e->hash;
            struct lru_entry** ptr = &new_buckets[hash & (new_length - 1)];
            e->next_hash = *ptr;
            *ptr = e;
            e = next;
            count++;
        }
    }

    assert(h->elems == count);
    if (h->buckets != h->bucket_init) {
        free(h->buckets);
    }
    h->buckets = new_buckets;
    h->length = new_length;

    return 0;
}

/* Return a pointer to slot that points to a lru_entry that matches key/hash. If
 * there is no such cache entry, return a pointer to the trailing slot in the
 * corresponding linked list. */
static struct lru_entry** lru_hash_table_find_pointer(struct lru_hash_table* h,
                                                      const slice* key,
                                                      uint32_t hash) {
    struct lru_entry** ptr = &h->buckets[hash & (h->length - 1)];
    while (*ptr != NULL &&
           ((*ptr)->hash != hash || !slice_equal(key, &(*ptr)->key))) {
        ptr = &(*ptr)->next_hash;
    }
    return ptr;
}

static struct lru_entry* lru_hash_table_lookup(struct lru_hash_table* h,
                                               const slice* key,
                                               uint32_t hash) {
    return *lru_hash_table_find_pointer(h, key, hash);
}

static struct lru_entry* lru_hash_table_insert(struct lru_hash_table* h,
                                               struct lru_entry* n) {
    struct lru_entry** ptr = lru_hash_table_find_pointer(h, &n->key, n->hash);
    struct lru_entry* old = *ptr;
    n->next_hash = (old == NULL ? NULL : old->next_hash);
    *ptr = n;
    if (old == NULL) {
        ++h->elems;
        if (h->elems > h->length || (h->length > LRU_HASH_FIRST_BUCKET_SIZE &&
                                     h->elems < h->length / 4)) {
            // Since each cache entry is fairly large, we aim for a small
            // average linked list length (<= 1).
            lru_hash_table_resize(h);
        }
    }
    return old;
}

static struct lru_entry* lru_hash_table_remove(struct lru_hash_table* h,
                                               const slice* key,
                                               uint32_t hash) {
    struct lru_entry** ptr = lru_hash_table_find_pointer(h, key, hash);
    struct lru_entry* result = *ptr;
    if (result != NULL) {
        *ptr = result->next_hash;
        --h->elems;
    }
    return result;
}

void lru_init(lru_hash_t* l, size_t capacity) {
    l->capacity = capacity;
    l->usage = 0;
    INIT_LIST_HEAD(&l->lru);
    INIT_LIST_HEAD(&l->in_use);
    lru_hash_table_init(&l->table);
}

static void lru_ref(lru_hash_t* l, struct lru_entry* e) {
    if (e->refs == 1 && e->in_cache) {
        list_del(&e->list);
        list_add_tail(&e->list, &l->in_use);
    }
    e->refs++;
}

void lru_unref(lru_hash_t* l, struct lru_entry* e) {
    assert(e->refs > 0);
    e->refs--;
    if (e->refs == 0) {
        assert(!e->in_cache);
        (*e->deleter)(&e->key, e->value);
        free(e);
    } else if (e->in_cache && e->refs == 1) {
        list_del(&e->list);
        list_add_tail(&e->list, &l->lru);
    }
}

void lru_clean(lru_hash_t* l) {
    assert(list_empty(&l->in_use));  // Error if caller has an unreleased handle
    struct lru_entry* e;
    while (!list_empty(&l->lru)) {  // for each head, delete
        struct list_head* first = l->lru.next;
        e = list_entry(first, struct lru_entry, list);
        assert(e->in_cache);
        e->in_cache = 0;
        assert(e->refs == 1);
        // delete
        list_del(&e->list);
        lru_unref(l, e);
    }
    lru_hash_table_clean(&l->table);
}

struct lru_entry* lru_lookup(lru_hash_t* l, const slice* key, uint32_t hash) {
    struct lru_entry* e = lru_hash_table_lookup(&l->table, key, hash);
    if (e != NULL) {
        lru_ref(l, e);
    }
    return e;
}

// If e != NULL, finish removing *e from the cache; it has already been
// removed from the hash table.  Return whether e != NULL.
static int lru_finish_erase(lru_hash_t* l, struct lru_entry* e) {
    if (e != NULL) {
        assert(e->in_cache);
        list_del(&e->list);
        /* TODO: remove in_cache variable to improve readability. */
        e->in_cache = 0;
        l->usage -= e->charge;
        lru_unref(l, e);
    }
    return e != NULL;
}

void lru_add_charge(lru_hash_t* l, struct lru_entry* e, int charge) {
    assert((int64_t)(e->charge) + charge >= 0);
    e->charge += charge;
    l->usage += charge;
}

void lru_keep_small(lru_hash_t* l) {
    while (l->usage > l->capacity && !list_empty(&l->lru)) {
        struct list_head* old = l->lru.next;
        struct lru_entry* eold = list_entry(old, struct lru_entry, list);
        assert(eold->refs == 1);
        int erased = lru_finish_erase(
            l, lru_hash_table_remove(&l->table, &eold->key, eold->hash));
        if (!erased) {  // avoid unused variable compile warning
            assert(erased);
        }
    }
}

void lru_change_charge(lru_hash_t* l, struct lru_entry* e, size_t charge) {
    int64_t add = (int64_t)charge - (int64_t)e->charge;
    assert((int64_t)(e->charge) + add >= 0);

    e->charge += add;
    l->usage += add;
}

struct lru_entry* lru_insert(lru_hash_t* l, const slice* key, uint32_t hash,
                             void* value, size_t charge,
                             void (*deleter)(slice* key, void* value)) {
    struct lru_entry* e = (struct lru_entry*)malloc(sizeof(struct lru_entry));
    if (e == NULL) {
        return NULL;
    }
    INIT_LIST_HEAD(&e->list);
    e->value = value;
    e->charge = charge;
    e->deleter = deleter;
    e->key = *key;
    e->in_cache = 0;
    e->hash = hash;
    e->refs = 1;
    if (l->capacity > 0) {
        e->refs++;
        e->in_cache = 1;
        list_add_tail(&e->list, &l->in_use);
        l->usage += charge;
        lru_finish_erase(l, lru_hash_table_insert(&l->table, e));
    } else {
        // capacity == 0 is cache turnoff
        e->next_hash = NULL;
    }

    lru_keep_small(l);
    return e;
}

void lru_release(lru_hash_t* l, struct lru_entry* e) { lru_unref(l, e); }

void lru_erase(lru_hash_t* l, const slice* key, uint32_t hash) {
    lru_finish_erase(l, lru_hash_table_remove(&l->table, key, hash));
}

void lru_prune(lru_hash_t* l) {
    while (!list_empty(&l->lru)) {
        struct list_head* old = l->lru.next;
        struct lru_entry* eold = list_entry(old, struct lru_entry, list);
        assert(eold->refs == 1);
        int erased = lru_finish_erase(
            l, lru_hash_table_remove(&l->table, &eold->key, eold->hash));
        if (!erased) {  // avoid unused variable
            assert(erased);
        }
    }
}
