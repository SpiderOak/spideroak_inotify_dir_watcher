
#include <stdlib.h>
#include <string.h>
#include "hash_cache.h"

typedef struct {
    unsigned int count;      // how often was this item added
    unsigned int hash;       // full hash value
    unsigned int hash_pos;   // position in hash_table
    unsigned int datalen;    // length of data in bytes
    void * memory_pos;       // pointer to data
} hash_cache_entry;

struct hash_cache_s {
    unsigned int hash_size;   // number of entries to fit in the hash table
    unsigned int mem_size;    // size of memory in bytes

    hash_cache_entry ** hash; // the hash-table
    void * memory;            // here to store the data

    hash_cache_entry * entries;
                              // the entries [0..hash_pos-1] are used
    void * memory_pos;        // position of first free byte in memory
    unsigned int counter;     // number of elements in hash table
    unsigned int hash_pos;    // number of used rows in hash table

};

// A simple checksum-function
unsigned int default_hash_function(void * data, unsigned int datalen) {
    unsigned int i;
    unsigned int hash = 0;

    for(i=0; i < datalen; i++) {
        hash +=  i ^ ((unsigned char*)data)[i];
    }

    return hash;
}

// Release a hash_cache
void free_hash_cache(hash_cache * hc) {
    if(hc == NULL) return;

    if(hc->entries != NULL) {
        free(hc->entries);
        hc->entries = NULL;
    }

    if(hc->hash != NULL) {
        free(hc->hash);
        hc->hash = NULL;
    }

    if(hc->memory != NULL) {
        free(hc->memory);
        hc->memory = NULL;
    }

    free(hc);
}

// Create a new hash_cache which can store hash_size/2 elements and up to
// mem_size bytes of data
hash_cache * new_hash_cache(unsigned int hash_size, unsigned int mem_size) {
    hash_cache * hc;

    hc = malloc(sizeof(hash_cache));

    if(hc == NULL) {
        return NULL;
    }

    memset(hc, 0, sizeof(hash_cache));

    hc->hash_size = hash_size;
    hc->mem_size = mem_size;

    hc->entries = malloc(sizeof(hash_cache_entry) * hash_size);
    hc->memory = malloc(mem_size);
    hc->hash = malloc(sizeof(hash_cache_entry*) * hash_size);

    if((hc->entries == NULL) || (hc->hash == NULL) || (hc->memory == NULL)) {
        free_hash_cache(hc);
        return NULL;
    }

    hc->memory_pos = hc->memory;
    memset(hc->hash, 0, sizeof(hash_cache_entry*) * hash_size);

    return hc;
}

// Add a new element
int hash_cache_add(hash_cache * hc, void * data, unsigned int datalen) {
    unsigned int hash;
    unsigned int hash_pos;

    if(datalen > (hc->mem_size - ((char*)hc->memory_pos - (char*)hc->memory))) {
        return 0;  // Memory full
    }

    if(hc->hash_pos > (hc->hash_size / 2)) {
        return 0;  // Hash-table more than 50% full
    }

    hash = default_hash_function(data, datalen);
    hash_pos = hash % hc->hash_size;

    // Find the element or the first free row
    while(
        (hc->hash[hash_pos] != NULL) && (
            (hc->hash[hash_pos]->hash != hash) ||
            (hc->hash[hash_pos]->datalen != datalen) ||
            (memcmp(hc->hash[hash_pos]->memory_pos, data, datalen) != 0)
        )
    ) {
        hash_pos = (hash_pos + 1) % hc->hash_size;
    }

    if(hc->hash[hash_pos] == NULL) {
        // The element is new
        hc->hash[hash_pos]=&(hc->entries[hc->hash_pos]);
        hc->hash[hash_pos]->count = 0;
        hc->hash[hash_pos]->hash = hash;
        hc->hash[hash_pos]->hash_pos = hash_pos;
        hc->hash[hash_pos]->datalen = datalen;
        hc->hash[hash_pos]->memory_pos = hc->memory_pos;
        memcpy(hc->memory_pos, data, datalen);
        hc->memory_pos = (char*) hc->memory_pos + datalen;
        hc->hash_pos++;
    }

    hc->counter++;
    hc->hash[hash_pos]->count++;

    return hc->hash[hash_pos]->count;
}

// Clear the hash-table
void hash_cache_clear(hash_cache * hc) {
    unsigned int i;

    for(i=0; i<hc->hash_pos; i++) {
        hc->entries[i].count = 0;
        hc->hash[hc->entries[i].hash_pos] = NULL;
    }
    hc->hash_pos = 0;
    hc->counter = 0;
    hc->memory_pos = hc->memory;
}

// Iterate the elements
// start with pos=0 and use return value as next pos-argument
// repeat until return-value is 0.
// Like this: for(pos = 0; (pos = hash_cache_iter(hc, pos, &count, (void**)&str, &datalen))!=0;)...
unsigned int hash_cache_iter(hash_cache * hc, unsigned int pos, unsigned int * count, void ** data, unsigned int * datalen) {
    if(hc->hash_pos <= pos) return 0;
    count[0] = hc->entries[pos].count;
    data[0] = hc->entries[pos].memory_pos;
    datalen[0] = hc->entries[pos].datalen;
    return pos + 1;
}





