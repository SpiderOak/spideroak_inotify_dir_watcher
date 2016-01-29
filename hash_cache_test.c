#include <stdio.h>
#include "hash_cache.h"

int main() {
    hash_cache * hc;
    unsigned int a;
    int i;
    unsigned int pos;
    unsigned int count;
    char * str;
    unsigned int datalen;

    char teststring1[] = "Hallo test";
    char teststring2[] = "Hallo test3";
    char teststring3[] = "Hallo te";

    hc = new_hash_cache(20,80);

    //printf("%d\n", default_hash_function(teststring1, sizeof(teststring1)));
    //printf("%d\n", default_hash_function(teststring2, sizeof(teststring2)));
    //printf("%d\n", default_hash_function(teststring3, sizeof(teststring3)));

    for(i=0;i<10;i++) {

        a = hash_cache_add(hc, teststring1, sizeof(teststring1));
        printf("hc=%d\n",a);
        a = hash_cache_add(hc, teststring2, sizeof(teststring2));
        printf("hc=%d\n",a);
        a = hash_cache_add(hc, teststring3, sizeof(teststring3));
        printf("hc=%d\n",a);


        a = hash_cache_add(hc, teststring1, sizeof(teststring1));
        printf("hc=%d\n",a);

        for(pos = 0; (pos = hash_cache_iter(hc, pos, &count, (void**)&str, &datalen))!=0;) {
            printf("%d %s:%d\n",pos, str, count);
        }
        

        hash_cache_clear(hc);
    }

    free_hash_cache(hc);

    return 0;
}

