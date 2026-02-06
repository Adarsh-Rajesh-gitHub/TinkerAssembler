#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "tinker_h"


// List* createList() {
//     List* inst = malloc(sizeof(List));
//     //arbitrary start size
//     inst->size = 500;
//     //set it to 500 lines of 256 characters
//     inst->entries = malloc(500*256*sizeof(char));
//     for(int i = 0; i < 500; i++) {
//         inst->entries[i] = "\0";
//     }
//     inst->numElements = 0;
//     return inst;
// }

// void insert(List* l, char* line) {
//     if(l->numElements = l->size) {
//         grow(l);
//     }
//     l->entries[l->numElements++] = line;
// }

// void grow(List* l) {
//     int oldSize = l->size;
//     char* oldEntries = l->entries;

//     l->size = oldSize * 2;
//     l->entries = malloc(256*sizeof(char) * l->size);

//     //reinsert
//     for (int i = 0; i < oldSize; i++) {
//         l->entries[i] = oldEntries[i];
//     }

//     for (int i = oldSize; i < l->size; i++) {
//         l->entries[i] = "\0";
//     }
//     free(oldEntries);
// }

List* createList(){
  List* l=malloc(sizeof(List));
  l->size=500; l->numElements=0;
  l->entries=malloc(l->size * sizeof(*l->entries)); // size * 256 bytes
  return l;
}

void add(List* l, const char* line){
  if(l->numElements >= l->size){
    l->size*=2;
    l->entries=realloc(l->entries, l->size * sizeof(*l->entries));
  }
  strncpy(l->entries[l->numElements], line, 255);
  l->entries[l->numElements][255]='\0';
  l->numElements++;
}