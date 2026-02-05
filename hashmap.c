#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>


//hashmap w. linear probing and no deletion implemented

typedef struct {
    char* label;
    int memory;
} Pair;

Pair* createPair(char* label, int memory) {
    Pair* inst = malloc(sizeof(Pair));
    inst->label = label;
    inst->memory = memory;
    return inst;
}


typedef struct {
    int size;
    Pair* entries;
} hashMap;

hashMap* createHashMap() {
    hashMap* inst = malloc(sizeof(hashMap));
    //arbitrary initial size
    inst->size = 500;
    inst->entries = malloc(sizeof(Pair)*500);
    for(int i = 0; i < 500; i++) {
        inst->entries[i] = *createPair('-1',-1);
    }
    return inst;
}

//linear probing implemented
void insert(hashMap* hM, char* label, int memory) {
    //hashing of label done my multiplying all asci val of all chars in that string % size 
    int len = strlen(label);
    int strHash = 1;
    for(int i = 0; i < len; i++) {
        strHash *= (int)label[i];
        strHash %= hM->size;
    }

    int index = strHash;
    //probing when the index calculated is not empty
    while(index < hM->size && hM->entries[index].memory != -1) {
        index++;
    }
    //need to expand array
    if(index == hM->size) {
        grow(hM); 
    }
    hM->entries[index] = *createPair(label, memory);
 }   

Pair find(hashMap* hM, char* label) {
    //hashing copied from insert to find start point to probe
    int len = strlen(label);
    int strHash = 1;
    for(int i = 0; i < len; i++) {
        strHash *= (int)label[i];
        strHash %= hM->size;
    }
    int index = strHash;
    while(index < hM->size && (strcmp(hM->entries[index].label, label) != 0)) {
        index++;
    }
    //somehow did not find label
    if(index == hM->size) {
        printf("tried to find label that doesn't exist");
        return *createPair("-1", 1);
    }
    return hM->entries[index];
}

void grow(hashMap* hM) {
    Pair* entries = malloc(sizeof(Pair)*hM->size*2);
    for(int i = 0; i < hM->size; i++) {
        entries[i] = hM->entries[i];
    }
    for(int i = hM->size; i < 2*hM->size; i++) {
        entries[i] = *createPair("-1", -1);
    }
    hM->entries = entries;
    hM->size*=2;
}