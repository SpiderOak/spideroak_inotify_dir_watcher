
typedef struct hash_cache_s hash_cache;

// Release a hash_cache
void free_hash_cache(hash_cache * hc);

// Create a new hash_cache which can store hash_size/2 elements and up to
// mem_size bytes of data
hash_cache * new_hash_cache(unsigned int hash_size, unsigned int mem_size);

// Add a new element
int hash_cache_add(hash_cache * hc, void * data, unsigned int datalen);

// Clear the hash-table
void hash_cache_clear(hash_cache * hc);

// Iterate over the elements
// start with pos=0 and use return value as next pos-argument
// repeat until return-value is 0.
// Like this: for(pos = 0; (pos = hash_cache_iter(hc, pos, &count, (void**)&str, &datalen))!=0;)...
unsigned int hash_cache_iter(hash_cache * hc, unsigned int pos, unsigned int * count, void ** data, unsigned int * datalen);
