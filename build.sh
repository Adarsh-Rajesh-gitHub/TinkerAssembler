set -e
gcc -std=c11 -O2 -Wall -Wextra firstParse.c hashmap.c list.c -o hw3 -lm
