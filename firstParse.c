#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdint.h>

#include "tinker_h"

static uint64_t pc = 4096; 




void label(char* line, hashMap* hM) {
    char* name = line+1;
    char* copy = strdup(name);
    insert(hM, copy, pc);
}








int main(int argc, char* args[]) {

    //check for all files if another type or just a bad input
    if(argc != 4) {
        fprintf(stderr, "you have given wrong number of args\n");
        return 1;
    }
    char* inputFile = args[1];
    size_t len = strlen(inputFile);
    if (len < 3 || strcmp(inputFile + len - 3, ".tk") != 0) {
        fprintf(stderr, "you have given an incorrect input file\n");
        return 1;
    }
     
    FILE *fp = fopen(inputFile, "r");
    if (!fp) { fprintf(stderr,"error: cannot open %s\n", inputFile); return 1; }






    char line[1024];
    //let 0 be none, 1 be code, -1 be
    int mode = 0;
    hashMap* hM = createHashMap();
    int numLines = 0;

    List* lis = createList();

    while(fgets(line, sizeof line, fp)) {
        //printf("%c---------%s", line[0], line);
        numLines++;
        size_t n = strlen(line);

        // strip trailing \n
        if(n && line[n-1] == '\n') line[--n] = '\0';

        if(n && (line[n-1] == ' ' || line[n-1] == '\t')) {
            fprintf(stderr, "error: trailing whitespace\n");
            fclose(fp);
            return 1;
        }

        if(n == 0) {
            printf("nothing on line\n");
            continue; 
        }

        char c = line[0];
        if(c == ';') {
            printf("comment\n");
            continue; 
        }
        else if(c == '\t') {
            if(mode == 1) {
                printf("code line\n");
                //add advancement depending on if and what macro
                //if ld takes 13 instrucitons all others take 2
                if(strstr(line, "ld ")) pc+=48;
                else if(strstr(line, "push ")) pc+=8;
                else if(strstr(line, "pop ")) pc+=8;
                else pc+=4;
                add(lis, line);
            }
            else if(mode == -1) {
                printf("code data\n");
                pc+=8;
                add(lis, line);
            }
            else {
                fprintf(stderr, "wrong input\n");
                return 1;
            }
        }
        else if(c == ':')  { 
            printf("label\n"); label(line, hM);
        }
        else if(strncmp(line, ".code", 5) == 0 && line[5] == '\0') {
            printf(".code\n"); 
            if(mode != 1) {
                add(lis, line);
                mode = 1;
            }
        }
        else if(strncmp(line, ".data", 5) == 0 && line[5] == '\0') {
            printf(".data\n"); 
            if(mode != -1) {
                add(lis, line);
                mode = -1;
            }
        }
        else {
            fprintf(stderr, "error: invalid line start\n");
            fclose(fp);
            return 1;
        }
    }
    printf("the input had %d lines\n", numLines);
    fclose(fp);

    //checking initial parsing which combines .code and .data that are together
    for(int i = 0; i < lis->numElements; i++) {
        printf("%s\n", lis->entries[i]);
    }

    //replace labels with the locations in memory they point to
    mode = 0;
    for(int i = 0; i < lis->numElements; i++) {
        if(strncmp(lis->entries[i], ".code", 5) == 0 && lis->entries[i][5] == '\0'){
            mode = 1; continue;
        } 
        else if(strncmp(lis->entries[i], ".data", 5) == 0 && lis->entries[i][5] == '\0'){
            mode = -1; continue;
        } 
        //label in code
        if(mode == 1 && strstr(lis->entries[i], ":") != NULL) {
            char* ptr = strstr(lis->entries[i], ":");
            ptr++;
            int mem = find(hM, ptr);
            if(mem == -1) {
                printf("evil things");
                exit(1);
            }
            // char* newEntry = malloc
            ptr--;
            int n = snprintf(ptr, sizeof(ptr), "%d", mem);
            // ptr = '\0';
            // ptr--;
            // //*ptr = mem;
        }
    }

    mode = 0;
    List* intermediate = createList();
    for(int i = 0; i < lis->numElements; i++) {
        if(strncmp(lis->entries[i], ".code", 5) == 0 && lis->entries[i][5] == '\0'){
            mode = 1; 
            add(intermediate, lis->entries[i]);
            continue;
        } 
        if(strncmp(lis->entries[i], ".data", 5) == 0 && lis->entries[i][5] == '\0'){
            mode= -1; 
            add(intermediate, lis->entries[i]);
            continue;
        } 
        if(mode != 1) {
            add(intermediate, lis->entries[i]);
            continue;
        }

        //expanding the macros
        char *cur = lis->entries[i];
        char tmp[256];
        snprintf(tmp, sizeof(tmp), "%s", cur + 1);     
        for (int k = 0; tmp[k]; k++) {
            if (tmp[k] == ',' || tmp[k] == '(' || tmp[k] == ')') tmp[k] = ' ';
        }

        char op[16]; int rd=-1, rs=-1;
        uint64_t L=0;
        int matched = sscanf(tmp, "%15s r%d r%d %llu", op, &rd, &rs, (unsigned long long*)&L);

        if(matched >= 2 && strcmp(op, "clr") == 0) {
            char b[64]; snprintf(b, sizeof(b), "\txor r%d, r%d, r%d", rd, rd, rd);
            add(intermediate, strdup(b));
        }
        else if(matched >= 3 && strcmp(op, "in") == 0) {
            char b[64]; snprintf(b, sizeof(b), "\tpriv r%d, r%d, r0, 3", rd, rs);
            add(intermediate, strdup(b));
        }
        else if(matched >= 3 && strcmp(op, "out") == 0) {
            char b[64]; snprintf(b, sizeof(b), "\tpriv r%d, r%d, r0, 4", rd, rs);
            add(intermediate, strdup(b));
        }
        else if(strcmp(op, "halt") == 0) {
            add(intermediate, strdup("\tpriv r0, r0, r0, 0"));
            continue;
        }
        else if (matched >= 2 && strcmp(op, "push") == 0) {
            char b[64]; snprintf(b, sizeof(b), "\tmov (r31)(-8), r%d", rd);  
            add(intermediate, strdup(b));
            add(intermediate, strdup("\tsubi r31, 8"));
        }
        else if(matched >= 2 && strcmp(op, "pop") == 0) {
            char b[64]; snprintf(b, sizeof(b), "\tmov r%d, (r31)(0)", rd);
            add(intermediate, strdup(b));
            add(intermediate, strdup("\taddi r31, 8"));
        }
        else if(strcmp(op, "ld") == 0) {
            unsigned long long Lu = 0;
            if(sscanf(tmp, "%*s r%d %llu", &rd, &Lu) != 2) {
                fprintf(stderr, "error: malformed ld\n"); exit(1);
            }
            uint64_t L = (uint64_t)Lu;
            char b[64];
            snprintf(b,sizeof(b),"\txor r%d, r%d, r%d", rd, rd, rd); 
            add(intermediate,strdup(b));
            snprintf(b,sizeof(b),"\taddi r%d, %llu", rd, (unsigned long long)((L>>52)&0xFFF)); 
            add(intermediate,strdup(b));
            snprintf(b,sizeof(b),"\tshftli r%d, 12", rd); 
            add(intermediate,strdup(b));
            snprintf(b,sizeof(b),"\taddi r%d, %llu", rd, (unsigned long long)((L>>40)&0xFFF)); 
            add(intermediate,strdup(b));
            snprintf(b,sizeof(b),"\tshftli r%d, 12", rd); 
            add(intermediate,strdup(b));
            snprintf(b,sizeof(b),"\taddi r%d, %llu", rd, (unsigned long long)((L>>28)&0xFFF)); 
            add(intermediate,strdup(b));
            snprintf(b,sizeof(b),"\tshftli r%d, 12", rd); 
            add(intermediate,strdup(b));
            snprintf(b,sizeof(b),"\taddi r%d, %llu", rd, (unsigned long long)((L>>16)&0xFFF)); 
            add(intermediate,strdup(b));
            snprintf(b,sizeof(b),"\tshftli r%d, 12", rd); 
            add(intermediate,strdup(b));
            snprintf(b,sizeof(b),"\taddi r%d, %llu", rd, (unsigned long long)((L>>4)&0xFFF));  
            add(intermediate,strdup(b));
            snprintf(b,sizeof(b),"\tshftli r%d, 4",  rd); 
            add(intermediate,strdup(b));
            snprintf(b,sizeof(b),"\taddi r%d, %llu", rd, (unsigned long long)(L&0xF));        
            add(intermediate,strdup(b));
        }
        else {
            //not a macro
            char *fixed = strdup(cur);
            for (char *p = fixed; (p = strchr(p, ',')); p += 2)
                if (p[1] != ' ') { memmove(p+2, p+1, strlen(p+1)+1); p[1] = ' '; }
            add(intermediate, fixed);
        }
    }
    for(int i = 0; i < intermediate->numElements; i++) {
        printf("%s\n", intermediate->entries[i]);
    }
    return 0;

}