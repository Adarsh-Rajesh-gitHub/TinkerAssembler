// test_firstParse.c
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "tinker_h"


//tested just helper methods and made invalid and valid test files for input and ran in terminal

int main(void){
    uint64_t v=0;

    // strictParse
    assert(parse_u64_strict("0",&v) && v==0);
    assert(parse_u64_strict("18446744073709551615",&v) && v==UINT64_MAX);
    assert(!parse_u64_strict("-1",&v));
    assert(!parse_u64_strict("12x",&v));


    // commaSpace
    assert(commaSpace("add r1, r2, r3"));
    assert(!commaSpace("add r1,r2, r3"));
    assert(!commaSpace("add r1,  r2, r3"));
    assert(!commaSpace("add r1 , r2, r3"));
    assert(!commaSpace("add r1,\tr2, r3"));

    // validLabel
    assert(validLabel("L1"));
    assert(validLabel("_x9"));
    assert(!validLabel("1bad"));
    assert(!validLabel("a-b"));

    // op_is
    const char *after=0;
    assert(op_is("add r1, r2, r3","add",&after) && strcmp(after,"r1, r2, r3")==0);
    assert(op_is("add, r1, r2, r3","add",&after) && strcmp(after,"r1, r2, r3")==0);
    assert(!op_is("addiX r1, 5","addi",&after));

    puts("passed");
    return 0;
}