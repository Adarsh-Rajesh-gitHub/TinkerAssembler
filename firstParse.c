#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>


typedef struct {
    int numInputs;
    char** labels;
    int* memory;
} Table;

Table* createTable(char* numInputs)



int main(int argc, char* args[]) {



    char* fileName = args[1];
    //check if file is another type or just a bad input
    size_t len = strlen(fileName);
    if((len > 2 && (fileName[len-1] != 't' || fileName[len-2] != 'k')) || (len <= 2)) {
        printf("you have given an incorrect input");
        return 1;
    }
     
    FILE *fp = fopen(fileName, "r");

    //size of content in fp
    struct stat st;
    stat(fileName, &st);
    char* content = malloc(st.st_size+1);
    
    //read & verify all read
    int bytesRead = fread(content, 1, st.st_size, fp);
    if(bytesRead != st.st_size) { printf("bad read"); return 1;}
    content[bytesRead] = '\0';



    


    
}