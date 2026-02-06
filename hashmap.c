#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "tinker_h"


//hashmap w. linear probing and no deletion implemented
void grow(hashMap* hM);

Pair* createPair(char* label, int memory) {
    Pair* inst = malloc(sizeof(Pair));
    inst->label = label;
    inst->memory = memory;
    return inst;
}

hashMap* createHashMap() {
    hashMap* inst = malloc(sizeof(hashMap));
    //arbitrary initial size
    inst->size = 500;
    inst->entries = malloc(sizeof(Pair)*500);
    for(int i = 0; i < 500; i++) {
        inst->entries[i] = *createPair("-1",-1);
    }
    return inst;
}

//linear probing implemented
void insert(hashMap* hM, char* label, int memory) {
    // hashing of label done by multiplying all asci val of all chars in that string % size
    int len = strlen(label);
    int strHash = 1;
    for (int i = 0; i < len; i++) {
        strHash *= (int)label[i];
        strHash %= hM->size;
    }
    int start = strHash;
    int index = start;
    // wrap round
    while (hM->entries[index].memory != -1) {
        // dupe key
        if (strcmp(hM->entries[index].label, label) == 0) {
            hM->entries[index].memory = memory;
            return;
        }
        index++;
        if (index == hM->size) index = 0;
        // restart probe
        if (index == start) {
            grow(hM);
            len = strlen(label);
            strHash = 1;
            for (int i = 0; i < len; i++) {
                strHash *= (int)label[i];
                strHash %= hM->size;
            }
            start = strHash;
            index = start;
        }
    }
    hM->entries[index] = *createPair(label, memory);
}


int find(hashMap* hM, char* label) {
    //hashing copied from insert to find start point to probe
    int len = strlen(label);
    int strHash = 1;
    for(int i = 0; i < len; i++) {
        strHash *= (int)label[i];
        strHash %= hM->size;
    }

    int start = strHash;
    int index = start;

    while(hM->entries[index].memory != -1 && (strcmp(hM->entries[index].label, label) != 0)) {
        index++;
        if(index == hM->size) index = 0;
        //wrapped all the way around and didn't find it
        if(index == start) break;
    }
    //somehow did not find label
    if(hM->entries[index].memory == -1 || strcmp(hM->entries[index].label, label) != 0) {
        //return *createPair("-1", -1);
        fprintf(stderr, "error: tried finding label that doesn't exist: %s\n", label);
        exit(1);
    }

    return hM->entries[index].memory;
}



void grow(hashMap* hM) {
    int oldSize = hM->size;
    Pair* oldEntries = hM->entries;

    hM->size = oldSize * 2;
    hM->entries = malloc(sizeof(Pair) * hM->size);

    for (int i = 0; i < hM->size; i++) {
        hM->entries[i] = *createPair("-1", -1);
    }

    //reinsert
    for (int i = 0; i < oldSize; i++) {
        if (oldEntries[i].memory != -1) {
            insert(hM, oldEntries[i].label, oldEntries[i].memory);
        }
    }

    free(oldEntries);
}