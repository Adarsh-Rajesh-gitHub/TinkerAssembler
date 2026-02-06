#define _POSIX_C_SOURCE 200809L
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
            printf("insert returned\n"); fflush(stdout);
            printf("lal\n"); label(line, hM);
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
   
    //write to the intermediate file
    FILE *inter;
    inter = fopen(args[2], "w");
    for(int i = 0; i < intermediate->numElements; i++) {
        fprintf(inter, intermediate->entries[i]);
        fprintf(inter, "\n");
    }
    fclose(inter);   


//---------------Starting the conversion to binary 
    FILE *out = fopen(args[3], "wb");
    if(!out){ fprintf(stderr,"can't open out\n"); return 1; }
    List* bin = createList();
    for(int i = 0; i < intermediate->numElements; i++) {
        if(strncmp(intermediate->entries[i], ".code", 5) == 0 && intermediate->entries[i][5] == '\0'){
            mode = 1; 
            continue;
        } 
        if(strncmp(intermediate->entries[i], ".data", 5) == 0 && intermediate->entries[i][5] == '\0'){
            mode= -1; 
            continue;
        } 
        if(mode == 0) {
            continue;
        }
        if(mode == 1) {
            uint32_t num = 0;
            int rd = 0, rs = 0, rt = 0, L = 0, unique = 0;
            //int rd, rs, rt, L;
            //Integer Arithmetic
            if(strstr(intermediate->entries[i], "addi") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "addi");
                ptr += 5;
                int assign = sscanf(ptr, "r%d, %d", &rd, &L);
                num = (L << 20) | (rd << 5) | 25;
                unique = 25;
            }
            else if(strstr(intermediate->entries[i], "add") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "add");
                ptr += 4;
                int assign = sscanf(ptr, "r%d, r%d, r%d", &rd, &rs, &rt);
                unique = 24;
                //if(assign != 3) fprintf(stderr, "problem w. intermediate"); 
                // num = 24;
                // num <<= 5;
                // num+=rd; 
                // num <<= 5;
                // num+=rs;
                // num <<= 5;
                // num+=rt;
                // num <<= 12;
                // num = (rt << 15) | (rs << 10) | (rd << 5) | 24;
            }
            else if(strstr(intermediate->entries[i], "subi") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "subi");
                ptr += 5;
                int assign = sscanf(ptr, "r%d, %d", &rd, &L);
                num = (L << 20) | (rd << 5) | 27;
                unique = 27;
            }
            else if(strstr(intermediate->entries[i], "sub") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "sub");
                ptr += 4;
                int assign = sscanf(ptr, "r%d, r%d, r%d", &rd, &rs, &rt);
                num = (rt << 15) | (rs << 10) | (rd << 5) | 26;
                unique = 26;
            }
            else if(strstr(intermediate->entries[i], "mul") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "mul");
                ptr += 4;
                int assign = sscanf(ptr, "r%d, r%d, r%d", &rd, &rs, &rt);
                num = (rt << 15) | (rs << 10) | (rd << 5) | 28;
                unique = 28;
            }
            else if(strstr(intermediate->entries[i], "div") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "div");
                ptr += 4;
                int assign = sscanf(ptr, "r%d, r%d, r%d", &rd, &rs, &rt);
                num = (rt << 15) | (rs << 10) | (rd << 5) | 29;
                unique = 29;
            }
            //Logic 
            else if(strstr(intermediate->entries[i], "and") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "and");
                ptr += 4;
                int assign = sscanf(ptr, "r%d, r%d, r%d", &rd, &rs, &rt);
                num = (rt << 15) | (rs << 10) | (rd << 5) | 0;
                unique = 0;
            }
            else if(strstr(intermediate->entries[i], "or") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "or");
                ptr += 3;
                int assign = sscanf(ptr, "r%d, r%d, r%d", &rd, &rs, &rt);
                num = (rt << 15) | (rs << 10) | (rd << 5) | 1;
                unique = 1;
            }
            else if(strstr(intermediate->entries[i], "xor") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "xor");
                ptr += 4;
                int assign = sscanf(ptr, "r%d, r%d, r%d", &rd, &rs, &rt);
                num = (rt << 15) | (rs << 10) | (rd << 5) | 2;
                unique = 2;
            }
            else if(strstr(intermediate->entries[i], "not") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "not");
                ptr += 4;
                int assign = sscanf(ptr, "r%d, r%d", &rd, &rs);
                num = (rs << 10) | (rd << 5) | 3;
                unique = 3;
            }
            else if(strstr(intermediate->entries[i], "shftr") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "shftr");
                ptr += 6;
                int assign = sscanf(ptr, "r%d, r%d, r%d", &rd, &rs, &rt);
                num = (rt << 15) | (rs << 10) | (rd << 5) | 4;
                unique = 4;
            }
            else if(strstr(intermediate->entries[i], "shftri") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "shftri");
                ptr += 7;
                int assign = sscanf(ptr, "r%d, %d", &rd, &L);
                num = (rd << 5) | 5;
                unique = 5;
            }
            else if(strstr(intermediate->entries[i], "shftl") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "shftl");
                ptr += 6;
                int assign = sscanf(ptr, "r%d, r%d, r%d", &rd, &rs, &rt);
                num = (rt << 15) | (rs << 10) | (rd << 5) | 6;
                unique = 6;
            }
            else if(strstr(intermediate->entries[i], "shftli") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "shftli");
                ptr += 7;
                int assign = sscanf(ptr, "r%d, %d", &rd, &L);
                num = (rd << 5) | 7;
                unique = 7;
            }

            /* reordered control block: brr/brnz/brgt/call/return BEFORE br */

            else if(strstr(intermediate->entries[i], "brr r") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "br");
                ptr += 4;
                int assign = sscanf(ptr, "r%d", &rd);
                num = (rd << 5) | 9;
                unique = 9;
            }
            else if(strstr(intermediate->entries[i], "brr") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "br");
                ptr += 4;
                int assign = sscanf(ptr, "%d", &L);
                num = (L << 20) | 10;
                unique = 10;
            }
            else if(strstr(intermediate->entries[i], "brnz") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "brnz");
                ptr += 5;
                int assign = sscanf(ptr, "r%d, r%d", &rd, &rs);
                num = (rt << 15) | (rs << 10) | (rd << 5) | 11;
                unique = 11;
            }
            else if(strstr(intermediate->entries[i], "brgt") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "brgt");
                ptr += 5;
                int assign = sscanf(ptr, "r%d, r%d, r%d", &rd, &rs, &rt);
                num = (rt << 15) | (rs << 10) | (rd << 5) | 14;
                unique = 14;
            }
            else if(strstr(intermediate->entries[i], "call") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "call");
                ptr += 5;
                int assign = sscanf(ptr, "r%d", &rd);
                num = (rd << 5) | 12;
                unique = 12;
            }
            else if(strstr(intermediate->entries[i], "return") != NULL) {
                num = 13;
                unique = 13;
            }
            else if(strstr(intermediate->entries[i], "br") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "br");
                ptr += 3;
                int assign = sscanf(ptr, "r%d", &rd);
                num = (rd << 5) | 8;
                unique = 8;
            }

            //Privileged
            else if(strstr(intermediate->entries[i], "priv") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "priv");
                ptr += 5;
                int assign = sscanf(ptr, "r%d, r%d, r%d, %d", &rd, &rs, &rt, &L);
                num = (L << 20) | (rt << 15) | (rs << 10) | (rd << 5) | 15;
                unique = 15;
            }
            //Data Movement
            else if(strstr(intermediate->entries[i], "mov") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "mov");
                ptr += 5;
                int assign = sscanf(ptr, "r%d, (r%d)(%d)", &rd, &rs, &L);
                if(assign == 3) { num = (L << 20) | (rs << 10) | (rd << 5) | 16; unique = 16;}
                else {
                    assign = sscanf(ptr, "(r%d)(%d, %d)", &rd, &L, &rs);
                    if(assign == 3) {num = (L << 20) | (rs << 10) | (rd << 5) | 19; unique = 19;}
                    else {
                        assign = sscanf(ptr, "r%d, r%d", &rd, &rs);
                        if(assign == 2) {num = (rs << 10) | (rd << 5) | 17; unique = 17;}
                        else {
                            assign = sscanf(ptr, "r%d, %d", &rd, &L);
                            num = (L << 20) | (rd << 5) | 18;
                            unique = 18;
                        }
                    }
                }
            }
            //Floating Point
            else if(strstr(intermediate->entries[i], "addf") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "addf");
                ptr += 5;
                int assign = sscanf(ptr, "r%d, r%d, r%d", &rd, &rs, &rt);
                num = (rt << 15) | (rs << 10) | (rd << 5) | 20;   
                unique = 20;     
            }
            else if(strstr(intermediate->entries[i], "subf") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "subf");
                ptr += 5;
                int assign = sscanf(ptr, "r%d, r%d, r%d", &rd, &rs, &rt);
                num = (rt << 15) | (rs << 10) | (rd << 5) | 21;
                unique = 21;
            }
            else if(strstr(intermediate->entries[i], "mulf") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "mulf");
                ptr += 5;
                int assign = sscanf(ptr, "r%d, r%d, r%d", &rd, &rs, &rt);
                num = (rt << 15) | (rs << 10) | (rd << 5) | 22;
                unique = 22;
            }
            else if(strstr(intermediate->entries[i], "divf") != NULL) {
                char* ptr = strstr(intermediate->entries[i], "divf");
                ptr += 5;
                int assign = sscanf(ptr, "r%d, r%d, r%d", &rd, &rs, &rt);
                num = (rt << 15) | (rs << 10) | (rd << 5) | 23;
                unique = 23;
            }
            else {
                fprintf(stderr, "undefined opp from intermediate");
                return 1;
            }
            num = 0;
            num = (unique << 27) | (rd << 22) | (rs << 17) | (rt << 12) | (L & 0xFFF);


            fwrite(&num, 4, 1, out);

            printf("%x\n", num);
        }
        if (mode == -1) {
            uint64_t val;
            sscanf(intermediate->entries[i] + 1, "%llu", (unsigned long long*)&val);
            // uint64_t be64 =
            //     ((val & 0x00000000000000FFull) << 56) |
            //     ((val & 0x000000000000FF00ull) << 40) |
            //     ((val & 0x0000000000FF0000ull) << 24) |
            //     ((val & 0x00000000FF000000ull) << 8)  |
            //     ((val & 0x000000FF00000000ull) >> 8)  |
            //     ((val & 0x0000FF0000000000ull) >> 24) |
            //     ((val & 0x00FF000000000000ull) >> 40) |
            //     ((val & 0xFF00000000000000ull) >> 56);
            fwrite(&val, 8, 1, out);
            continue;
        }
    }
        
    fclose(out);
    return 0;
}