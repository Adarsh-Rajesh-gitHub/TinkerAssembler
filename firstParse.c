#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include "tinker_h"
#define failed(MSG) do { fprintf(stderr, "error: %s\n", (MSG)); return 1; } while(0)

static uint64_t pc = 4096; 



static int is_valid_label_name(const char *s){
    if(!s||!*s) return 0;
    if(!(isalpha((unsigned char)s[0])||s[0]=='_')) return 0;
    for(const char *p=s+1;*p;p++){
        if(!(isalnum((unsigned char)*p)||*p=='_')) return 0;
    }
    return 1;
}

static int list_contains_str(const List *l,const char *s){
    for(int i=0;i<l->numElements;i++){
        if(l->entries[i]&&strcmp(l->entries[i],s)==0) return 1;
    }
    return 0;
}

static int label_add(const char *line, hashMap *hM, List *labels_seen, uint64_t pc){
    const char *p=line+1;
    char name[256];
    int n=0;

    if(sscanf(p,"%255s %n",name,&n)!=1) return 0;

    p+=n;
    if(*p!='\0'&&*p!=';') return 0;

    if(!is_valid_label_name(name)) return 0;
    if(list_contains_str(labels_seen,name)) return 0;

    add(labels_seen,strdup(name));
    insert(hM,strdup(name),pc);
    return 1;
}



static int op_is(const char *line, const char *op, const char **after_op) {
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;

    size_t k = strlen(op);
    if (strncmp(p, op, k) != 0) return 0;
    char b = p[k];
    if (b != '\0' && b != ' ' && b != '\t' && b != ',') return 0;
    p += k;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == ',') { p++; while (*p == ' ' || *p == '\t') p++; } 

    *after_op = p;
    return 1;
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
    bool sawCode = false;
    hashMap* hM = createHashMap();
    int numLines = 0;

    List* lis = createList();
    List* labels_seen=createList();

    while(fgets(line, sizeof line, fp)) {
        //printf("%c---------%s", line[0], line);
        numLines++;
        size_t n = strlen(line);

        // strip trailing \n
        if(n && line[n-1] == '\n') line[--n] = '\0';

        size_t t = n;
        while (t && (line[t-1] == ' ' || line[t-1] == '\t')) t--;
        if (t != n) {
            fprintf(stderr, "error: trailing whitespace\n");
            fclose(fp);
            return 1;
        }

        if(n == 0) {
            //printf("nothing on line\n");
            continue; 
        }

        char c = line[0];
        if(c == ';') {
            //printf("comment\n");
            continue; 
        }
        else if(c == '\t') {
            if(mode == 1) {
                //printf("code line\n");
                if(strstr(line, "ld ")) pc+=48;
                else if(strstr(line, "push ")) pc+=8;
                else if(strstr(line, "pop ")) pc+=8;
                else pc+=4;
                add(lis, strdup(line));;
            }
            else if(mode==-1){
                unsigned long long v=0;
                int nn=0;
                if(sscanf(line+1,"%llu %n",&v,&nn)!=1||line[1+nn]!='\0'){
                    fprintf(stderr,"error: invalid data\n");
                    fclose(fp);
                    return 1;
                }
                pc+=8;
                add(lis,strdup(line));
            }
            else {
                fprintf(stderr, "wrong input\n");
                return 1;
            }
        }
        else if(c==':'){
            if(!label_add(line,hM,labels_seen,pc)){
                fprintf(stderr,"error: invalid or duplicate label\n");
                fclose(fp);
                return 1;
            }
        }
        else if(strncmp(line, ".code", 5) == 0 && line[5] == '\0') {
            //printf(".code\n"); 
            sawCode = true;
            if(mode != 1) {
                add(lis, strdup(line));;
                mode = 1;
            }
        }
        else if(strncmp(line, ".data", 5) == 0 && line[5] == '\0') {
            //printf(".data\n"); 
            if(mode != -1) {
                add(lis, strdup(line));;
                mode = -1;
            }
        }
        else {
            fprintf(stderr, "error: invalid line start\n");
            fclose(fp);
            return 1;
        }
    }
    //printf("the input had %d lines\n", numLines);
    fclose(fp);

    //checking initial parsing which combines .code and .data that are together
    for(int i = 0; i < lis->numElements; i++) {
        //printf("%s\n", lis->entries[i]);
    }
    if (!sawCode) {
        fprintf(stderr, "error: missing .code section\n");
        fclose(fp);
        return 1;
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
                if (mode == 1) {
            char *colon = strchr(lis->entries[i], ':');
            if (colon) {
                char labelname[256];
                if(sscanf(colon+1,"%255s",labelname)!=1){
                    fprintf(stderr,"error: malformed label ref\n");
                    return 1;
                }
                if(!is_valid_label_name(labelname)){
                    fprintf(stderr,"error: invalid label ref\n");
                    return 1;
                }
                uint64_t mem=(uint64_t)find(hM,labelname);

                size_t prefix_len = (size_t)(colon - lis->entries[i]);
                size_t label_len = strcspn(colon + 1, " \t"); 

                char buf[1024];
                int w = snprintf(buf, sizeof(buf), "%.*s%llu%s", (int)prefix_len,  lis->entries[i], (unsigned long long)mem, (colon + 1 + label_len));
                if (w < 0 || (size_t)w >= sizeof(buf)) {
                    fprintf(stderr, "error: line too long after label replace\n");
                    exit(1);
                }
                snprintf(lis->entries[i], sizeof lis->entries[i], "%s", buf);
                // char *newln = strdup(buf);
                // free(lis->entries[i]);          
                // lis->entries[i] = newln;
            }
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

int n=0;

        //clr
        n=0;
        if(sscanf(tmp,"clr r%d %n",&rd,&n)==1&&tmp[n]=='\0'){
            char b[64];snprintf(b,sizeof(b),"\txor r%d, r%d, r%d",rd,rd,rd);
            add(intermediate,strdup(b));
            continue;
        }

        //in
        n=0;
        if(sscanf(tmp,"in r%d r%d %n",&rd,&rs,&n)==2&&tmp[n]=='\0'){
            char b[64];snprintf(b,sizeof(b),"\tpriv r%d, r%d, r0, 3",rd,rs);
            add(intermediate,strdup(b));
            continue;
        }

        //out
        n=0;
        if(sscanf(tmp,"out r%d r%d %n",&rd,&rs,&n)==2&&tmp[n]=='\0'){
            char b[64];snprintf(b,sizeof(b),"\tpriv r%d, r%d, r0, 4",rd,rs);
            add(intermediate,strdup(b));
            continue;
        }

        //halt
        n=0;
        if(sscanf(tmp,"halt %n",&n)==0&&tmp[n]=='\0'){
            add(intermediate,strdup("\tpriv r0, r0, r0, 0"));
            continue;
        }

        //push
        n=0;
        if(sscanf(tmp,"push r%d %n",&rd,&n)==1&&tmp[n]=='\0'){
            char b[64];snprintf(b,sizeof(b),"\tmov (r31)(-8), r%d",rd);
            add(intermediate,strdup(b));
            add(intermediate,strdup("\tsubi r31, 8"));
            continue;
        }

        //pop
        n=0;
        if(sscanf(tmp,"pop r%d %n",&rd,&n)==1&&tmp[n]=='\0'){
            char b[64];snprintf(b,sizeof(b),"\tmov r%d, (r31)(0)",rd);
            add(intermediate,strdup(b));
            add(intermediate,strdup("\taddi r31, 8"));
            continue;
        }
        n=0;
        unsigned long long Lu=0;
        if(sscanf(tmp,"ld r%d %llu %n",&rd,&Lu,&n)==2&&tmp[n]=='\0'){
            uint64_t L=(uint64_t)Lu;
            if(sscanf(tmp, "%*s r%d %llu", &rd, &Lu) != 2) {
                fprintf(stderr, "error: malformed ld\n"); exit(1);
            }
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
            continue;
        }
        char opword[16];
        int nn=0;
        if(sscanf(tmp,"%15s %n",opword,&nn)!=1){
            fprintf(stderr,"error: invalid instruction\n");
            return 1;
        }
        if(!strcmp(opword,"clr")||!strcmp(opword,"in")||!strcmp(opword,"out")||
        !strcmp(opword,"halt")||!strcmp(opword,"push")||!strcmp(opword,"pop")||
        !strcmp(opword,"ld")){
            fprintf(stderr,"error: invalid macro format\n");
            return 1;
        }

        // not a macro -> pass through (with comma spacing normalization)
        char *fixed=strdup(cur);
        for(char *p=fixed;(p=strchr(p,','));p+=2){
            if(p[1]!=' '){memmove(p+2,p+1,strlen(p+1)+1);p[1]=' ';}
        }
        add(intermediate,fixed);
        continue;
    }
    for(int i = 0; i < intermediate->numElements; i++) {
        //printf("%s\n", intermediate->entries[i]);
    }
   
    //write to the intermediate file
    FILE *inter;
    inter = fopen(args[2], "w");
    for(int i = 0; i < intermediate->numElements; i++) {
        fprintf(inter, "%s\n", intermediate->entries[i]);
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
        const char *ptrc = NULL;
        char *ptr = NULL;
        //int rd, rs, rt, L;
        //Integer Arithmetic
        if (op_is(intermediate->entries[i], "addi", &ptrc)) {
            ptr = (char*)ptrc;
            int n = 0;
            int assign = sscanf(ptr, "r%d, %d %n", &rd, &L, &n);
            if (assign != 2 || ptr[n] != '\0') {
                fprintf(stderr, "invalid intermediate\n");
                return 1;
            }
            unique = 25;
        }
        else if(op_is(intermediate->entries[i], "add", &ptrc)) {
            ptr = (char*)ptrc;
            int n = 0;
            int assign = sscanf(ptr, "r%d, r%d, r%d %n", &rd, &rs, &rt, &n);
            if (assign != 3 || ptr[n] != '\0') {
                fprintf(stderr, "invalid intermediate\n");
                return 1;
            }
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
        else if(op_is(intermediate->entries[i],"subi",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, %d %n",&rd,&L,&n);
            if(assign!=2||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=27;
        }
        else if(op_is(intermediate->entries[i],"sub",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, r%d, r%d %n",&rd,&rs,&rt,&n);
            if(assign!=3||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=26;
        }
        else if(op_is(intermediate->entries[i],"mul",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, r%d, r%d %n",&rd,&rs,&rt,&n);
            if(assign!=3||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=28;
        }
        else if(op_is(intermediate->entries[i],"div",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, r%d, r%d %n",&rd,&rs,&rt,&n);
            if(assign!=3||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=29;
        }
        //Logic 
        else if(op_is(intermediate->entries[i],"and",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, r%d, r%d %n",&rd,&rs,&rt,&n);
            if(assign!=3||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=0;
        }
        else if(op_is(intermediate->entries[i],"or",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, r%d, r%d %n",&rd,&rs,&rt,&n);
            if(assign!=3||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=1;
        }
        else if(op_is(intermediate->entries[i],"xor",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, r%d, r%d %n",&rd,&rs,&rt,&n);
            if(assign!=3||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=2;
        }
        else if(op_is(intermediate->entries[i],"not",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, r%d %n",&rd,&rs,&n);
            if(assign!=2||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=3;
        }
        else if(op_is(intermediate->entries[i],"shftr",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, r%d, r%d %n",&rd,&rs,&rt,&n);
            if(assign!=3||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=4;
        }
        else if(op_is(intermediate->entries[i],"shftri",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, %d %n",&rd,&L,&n);
            if(assign!=2||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=5;
        }
        else if(op_is(intermediate->entries[i],"shftl",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, r%d, r%d %n",&rd,&rs,&rt,&n);
            if(assign!=3||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=6;
        }
        else if(op_is(intermediate->entries[i],"shftli",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, %d %n",&rd,&L,&n);
            if(assign!=2||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=7;
        }
        else if(op_is(intermediate->entries[i],"brr",&ptrc)){
            ptr=(char*)ptrc;

            int n=0;
            int assign=sscanf(ptr,"r%d %n",&rd,&n);
            if(assign==1&&ptr[n]=='\0'){
                unique=9;
            }
            else{
                n=0;
                assign=sscanf(ptr,"%d %n",&L,&n);
                if(assign!=1||ptr[n]!='\0'){
                    fprintf(stderr,"invalid intermediate\n");
                    return 1;
                }
                unique=10;
            }
        }
        else if(op_is(intermediate->entries[i],"brnz",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, r%d %n",&rd,&rs,&n);
            if(assign!=2||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=11;
        }
        else if(op_is(intermediate->entries[i],"brgt",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, r%d, r%d %n",&rd,&rs,&rt,&n);
            if(assign!=3||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=14;
        }
        else if(op_is(intermediate->entries[i],"call",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d %n",&rd,&n);
            if(assign!=1||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=12;
        }
        else if(op_is(intermediate->entries[i],"return",&ptrc)){
            ptr=(char*)ptrc;
            if(*ptr!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=13;
        }
        else if(op_is(intermediate->entries[i],"br",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d %n",&rd,&n);
            if(assign!=1||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=8;
        }

        //Privileged
        else if(op_is(intermediate->entries[i],"priv",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, r%d, r%d, %d %n",&rd,&rs,&rt,&L,&n);
            if(assign!=4||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=15;
        }
        //Data Movement
        else if(op_is(intermediate->entries[i],"mov",&ptrc)){
            ptr=(char*)ptrc;

            int n=0;
            int assign=sscanf(ptr,"r%d, (r%d)(%d) %n",&rd,&rs,&L,&n);
            if(assign==3&&ptr[n]=='\0'){
                unique=16;
            }else{
                n=0;
                assign=sscanf(ptr,"(r%d)(%d), r%d %n",&rd,&L,&rs,&n);
                if(assign==3&&ptr[n]=='\0'){
                    unique=19;
                }else{
                    n=0;
                    assign=sscanf(ptr,"r%d, r%d %n",&rd,&rs,&n);
                    if(assign==2&&ptr[n]=='\0'){
                        unique=17;
                    }else{
                        n=0;
                        assign=sscanf(ptr,"r%d, %d %n",&rd,&L,&n);
                        if(assign!=2||ptr[n]!='\0'){
                            fprintf(stderr,"invalid intermediate\n");
                            return 1;
                        }
                        unique=18;
                    }
                }
            }
        }
        //Floating Point
        else if(op_is(intermediate->entries[i],"addf",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, r%d, r%d %n",&rd,&rs,&rt,&n);
            if(assign!=3||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=20;
        }
        else if(op_is(intermediate->entries[i],"subf",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, r%d, r%d %n",&rd,&rs,&rt,&n);
            if(assign!=3||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=21;
        }
        else if(op_is(intermediate->entries[i],"mulf",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, r%d, r%d %n",&rd,&rs,&rt,&n);
            if(assign!=3||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=22;
        }
        else if(op_is(intermediate->entries[i],"divf",&ptrc)){
            ptr=(char*)ptrc;
            int n=0;
            int assign=sscanf(ptr,"r%d, r%d, r%d %n",&rd,&rs,&rt,&n);
            if(assign!=3||ptr[n]!='\0'){
                fprintf(stderr,"invalid intermediate\n");
                return 1;
            }
            unique=23;
        }
        else {
            fprintf(stderr, "undefined opp from intermediate");
            return 1;
        }
        if(unique > 29 || rd > 31 || rs > 31 || rt > 31 || L > 4095) {
            fprintf(stderr, "undefined values intermediate");
            return 1;
        }
        if(unique < 0 || rd < 0 || rs < 0 || rt < 0 || L < -2047) {
            fprintf(stderr, "undefined values intermediate");
            return 1;
        } 
        num = 0;
        num = (unique << 27) | (rd << 22) | (rs << 17) | (rt << 12) | (L & 0xFFF);


            fwrite(&num, 4, 1, out);

            //printf("%x\n", num);
        }
        if (mode == -1) {
            unsigned long long tmpv=0; int n=0;
            const char *ds = intermediate->entries[i] + 1;
            if (sscanf(ds, "%llu %n", &tmpv, &n) != 1 || ds[n] != '\0') failed("invalid data");
            uint64_t val = (uint64_t)tmpv;
            fwrite(&val, 8, 1, out);
            continue;
        }
    }
        
    fclose(out);
    return 0;
}