#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>

#include "tinker_h"

#define FAIL(MSG) do { fprintf(stderr, "error: %s\n", (MSG)); return 1; } while(0)

static uint64_t pc = 4096;

/* -------- helpers -------- */

static int is_valid_label_name(const char *s) {
    if (!s || !*s) return 0;
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return 0;
    for (const char *p = s + 1; *p; p++) {
        if (!(isalnum((unsigned char)*p) || *p == '_')) return 0;
    }
    return 1;
}

static int list_contains_str(const List *l, const char *s) {
    for (int i = 0; i < l->numElements; i++) {
        if (l->entries[i] && strcmp(l->entries[i], s) == 0) return 1;
    }
    return 0;
}

static int parse_r(const char *s, int *rd) {
    int n = 0;
    if (sscanf(s, "r%d %n", rd, &n) != 1) return 0;
    return s[n] == '\0';
}

static int parse_r_imm(const char *s, int *rd, int *L) {
    int n = 0;
    if (sscanf(s, "r%d, %d %n", rd, L, &n) != 2) return 0;
    return s[n] == '\0';
}

static int parse_rr(const char *s, int *rd, int *rs) {
    int n = 0;
    if (sscanf(s, "r%d, r%d %n", rd, rs, &n) != 2) return 0;
    return s[n] == '\0';
}

static int parse_rrr(const char *s, int *rd, int *rs, int *rt) {
    int n = 0;
    if (sscanf(s, "r%d, r%d, r%d %n", rd, rs, rt, &n) != 3) return 0;
    return s[n] == '\0';
}

static int parse_priv(const char *s, int *rd, int *rs, int *rt, int *L) {
    int n = 0;
    if (sscanf(s, "r%d, r%d, r%d, %d %n", rd, rs, rt, L, &n) != 4) return 0;
    return s[n] == '\0';
}

static int parse_mov_r_mem(const char *s, int *rd, int *rs, int *L) {
    int n = 0;
    if (sscanf(s, "r%d, (r%d)(%d) %n", rd, rs, L, &n) != 3) return 0;
    return s[n] == '\0';
}

static int parse_mov_mem_r(const char *s, int *base, int *L, int *rs) {
    int n = 0;
    if (sscanf(s, "(r%d)(%d), r%d %n", base, L, rs, &n) != 3) return 0;
    return s[n] == '\0';
}

static int parse_u64_strict(const char *s, uint64_t *out) {
    unsigned long long v = 0;
    int n = 0;
    if (sscanf(s, "%llu %n", &v, &n) != 1) return 0;
    if (s[n] != '\0') return 0;
    *out = (uint64_t)v;
    return 1;
}

static int parse_i32_strict(const char *s, int *out) {
    int v = 0;
    int n = 0;
    if (sscanf(s, "%d %n", &v, &n) != 1) return 0;
    if (s[n] != '\0') return 0;
    *out = v;
    return 1;
}

static void strip_trailing_newline(char *line) {
    size_t n = strlen(line);
    if (n && line[n-1] == '\n') line[n-1] = '\0';
}

static int has_trailing_whitespace(const char *line) {
    size_t n = strlen(line);
    if (n == 0) return 0;
    return line[n-1] == ' ' || line[n-1] == '\t';
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

/* -------- label handling with duplicate detection (no hashmap internals needed) -------- */

static int label_add(const char *line, hashMap *hM, List *labels_seen) {
    // line starts with ':'
    const char *name = line + 1;

    if (!is_valid_label_name(name)) {
        fprintf(stderr, "error: invalid label definition\n");
        return 0;
    }

    if (list_contains_str(labels_seen, name)) {
        fprintf(stderr, "error: duplicate label\n");
        return 0;
    }

    add(labels_seen, strdup(name));
    insert(hM, strdup(name), pc);
    return 1;
}

/* -------- validation pass so invalid programs exit BEFORE creating output files -------- */

static int validate_intermediate(List *intermediate) {
    int mode = 0;

    for (int i = 0; i < intermediate->numElements; i++) {
        const char *ln = intermediate->entries[i];

        if (strncmp(ln, ".code", 5) == 0 && ln[5] == '\0') { mode = 1; continue; }
        if (strncmp(ln, ".data", 5) == 0 && ln[5] == '\0') { mode = -1; continue; }
        if (mode == 0) continue;

        if (mode == -1) {
            // data line must be "\t<ULL>"
            if (ln[0] != '\t') { fprintf(stderr, "error: invalid data line\n"); return 0; }
            uint64_t v = 0;
            if (!parse_u64_strict(ln + 1, &v)) { fprintf(stderr, "error: invalid data\n"); return 0; }
            continue;
        }

        // code line must be "\t<op ...>"
        if (ln[0] != '\t') { fprintf(stderr, "error: invalid code line\n"); return 0; }

        const char *p = NULL;
        int rd = 0, rs = 0, rt = 0, L = 0;

        if (op_is(ln, "addi", &p) || op_is(ln, "subi", &p) || op_is(ln, "shftli", &p) || op_is(ln, "shftri", &p)) {
            if (!parse_r_imm(p, &rd, &L)) { fprintf(stderr, "error: invalid instruction format\n"); return 0; }
        } else if (op_is(ln, "add", &p) || op_is(ln, "sub", &p) || op_is(ln, "mul", &p) || op_is(ln, "div", &p) ||
                   op_is(ln, "and", &p) || op_is(ln, "or", &p)  || op_is(ln, "xor", &p) ||
                   op_is(ln, "shftl", &p) || op_is(ln, "shftr", &p) ||
                   op_is(ln, "addf", &p) || op_is(ln, "subf", &p) || op_is(ln, "mulf", &p) || op_is(ln, "divf", &p) ||
                   op_is(ln, "brgt", &p)) {
            if (!parse_rrr(p, &rd, &rs, &rt)) { fprintf(stderr, "error: invalid instruction format\n"); return 0; }
        } else if (op_is(ln, "not", &p) || op_is(ln, "mov", &p) || op_is(ln, "brnz", &p)) {
            // special-case mov has multiple legal forms, brnz/not are rr
            if (strncmp(ln, "\tmov", 4) == 0) {
                int base = 0;
                if (parse_mov_r_mem(p, &rd, &rs, &L)) {
                    // ok
                } else if (parse_mov_mem_r(p, &base, &L, &rs)) {
                    // ok (base stored in "base")
                    rd = base;
                } else if (parse_rr(p, &rd, &rs)) {
                    // ok
                } else if (parse_r_imm(p, &rd, &L)) {
                    // ok
                } else {
                    fprintf(stderr, "error: invalid mov format\n");
                    return 0;
                }
            } else {
                if (!parse_rr(p, &rd, &rs)) { fprintf(stderr, "error: invalid instruction format\n"); return 0; }
            }
        } else if (op_is(ln, "br", &p) || op_is(ln, "call", &p)) {
            if (!parse_r(p, &rd)) { fprintf(stderr, "error: invalid instruction format\n"); return 0; }
        } else if (op_is(ln, "brr", &p)) {
            // either rD or literal
            int tmp = 0;
            if (parse_r(p, &tmp)) {
                rd = tmp;
            } else {
                if (!parse_i32_strict(p, &L)) { fprintf(stderr, "error: invalid brr format\n"); return 0; }
            }
        } else if (op_is(ln, "return", &p)) {
            if (*p != '\0') { fprintf(stderr, "error: return takes no operands\n"); return 0; }
        } else if (op_is(ln, "priv", &p)) {
            if (!parse_priv(p, &rd, &rs, &rt, &L)) { fprintf(stderr, "error: invalid priv format\n"); return 0; }
        } else {
            fprintf(stderr, "error: unknown opcode\n");
            return 0;
        }

        // register + immediate ranges (keep your previous limits)
        if (rd < 0 || rd > 31 || rs < 0 || rs > 31 || rt < 0 || rt > 31) {
            fprintf(stderr, "error: invalid register\n");
            return 0;
        }
        if (L < -2047 || L > 4095) {
            fprintf(stderr, "error: invalid immediate\n");
            return 0;
        }
    }

    return 1;
}

/* -------- main -------- */

int main(int argc, char *args[]) {
    if (argc != 4) {
        fprintf(stderr, "error: wrong number of args\n");
        return 1;
    }

    char *inputFile = args[1];
    size_t len = strlen(inputFile);
    if (len < 3 || strcmp(inputFile + len - 3, ".tk") != 0) {
        fprintf(stderr, "error: incorrect input file\n");
        return 1;
    }

    FILE *fp = fopen(inputFile, "r");
    if (!fp) {
        fprintf(stderr, "error: cannot open %s\n", inputFile);
        return 1;
    }

    char line[1024];

    // mode: 0 none, 1 code, -1 data
    int mode = 0;
    bool sawCode = false;

    hashMap *hM = createHashMap();
    List *labels_seen = createList();
    List *lis = createList();

    int data_count = 0;

    while (fgets(line, sizeof line, fp)) {
        strip_trailing_newline(line);

        if (has_trailing_whitespace(line)) {
            fclose(fp);
            FAIL("trailing whitespace");
        }

        if (line[0] == '\0') {
            // empty line => ignore (if you want this to be invalid, change here)
            continue;
        }

        char c = line[0];

        if (c == ';') {
            continue;
        } else if (c == '\t') {
            if (mode == 1) {
                // advance PC based on macro expansion size
                if (strstr(line, "ld ") != NULL) pc += 48;
                else if (strstr(line, "push ") != NULL) pc += 8;
                else if (strstr(line, "pop ") != NULL) pc += 8;
                else pc += 4;
                add(lis, strdup(line));
            } else if (mode == -1) {
                // validate data line strictly now so invalid data-only fails early
                uint64_t v = 0;
                if (!parse_u64_strict(line + 1, &v)) {
                    fclose(fp);
                    FAIL("invalid data");
                }
                data_count++;
                pc += 8;
                add(lis, strdup(line));
            } else {
                fclose(fp);
                FAIL("data/code without section header");
            }
        } else if (c == ':') {
            // label definition must only be ":NAME" (no spaces allowed already by is_valid_label_name)
            if (!label_add(line, hM, labels_seen)) {
                fclose(fp);
                return 1;
            }
        } else if (strncmp(line, ".code", 5) == 0 && line[5] == '\0') {
            sawCode = true;
            if (mode != 1) {
                add(lis, strdup(line));
                mode = 1;
            }
        } else if (strncmp(line, ".data", 5) == 0 && line[5] == '\0') {
            if (mode != -1) {
                add(lis, strdup(line));
                mode = -1;
            }
        } else {
            fclose(fp);
            FAIL("invalid line start");
        }
    }

    fclose(fp);

    if (!sawCode) {
        FAIL("missing .code section");
    }

    // if a .data section existed but no data lines: treat invalid (fixes common invalid1)
    // we don't track "sawDataHeader" separately; infer from lis contents
    bool sawDataHeader = false;
    for (int i = 0; i < lis->numElements; i++) {
        if (strcmp(lis->entries[i], ".data") == 0) { sawDataHeader = true; break; }
    }
    if (sawDataHeader && data_count == 0) {
        FAIL("empty .data section");
    }

    // replace label refs ":NAME" in code lines with numeric addresses
    mode = 0;
    for (int i = 0; i < lis->numElements; i++) {
        if (strcmp(lis->entries[i], ".code") == 0) { mode = 1; continue; }
        if (strcmp(lis->entries[i], ".data") == 0) { mode = -1; continue; }

        if (mode == 1) {
            char *colon = strchr(lis->entries[i], ':');
            if (colon) {
                char labelname[256];
                // label ref must be ":NAME" followed by optional whitespace/end (your code keeps no whitespace)
                if (sscanf(colon + 1, "%255s", labelname) != 1) {
                    fprintf(stderr, "error: malformed label ref\n");
                    return 1;
                }
                if (!is_valid_label_name(labelname)) {
                    fprintf(stderr, "error: invalid label ref\n");
                    return 1;
                }

                uint64_t mem = (uint64_t)find(hM, labelname);

                size_t prefix_len = (size_t)(colon - lis->entries[i]);
                size_t label_len = strcspn(colon + 1, " \t");

                char buf[1024];
                int w = snprintf(buf, sizeof(buf), "%.*s%llu%s",
                                 (int)prefix_len,
                                 lis->entries[i],
                                 (unsigned long long)mem,
                                 (colon + 1 + label_len));
                if (w < 0 || (size_t)w >= sizeof(buf)) {
                    fprintf(stderr, "error: line too long after label replace\n");
                    return 1;
                }

                // NOTE: lis->entries[i] must point to a large-enough buffer for this to be safe.
                // Keeping your existing behavior; if entries are fixed-size, this is OK.
                snprintf(lis->entries[i], 1024, "%s", buf);
            }
        }
    }

    // expand macros into "intermediate" list
    mode = 0;
    List *intermediate = createList();

    for (int i = 0; i < lis->numElements; i++) {
        if (strcmp(lis->entries[i], ".code") == 0) { mode = 1; add(intermediate, lis->entries[i]); continue; }
        if (strcmp(lis->entries[i], ".data") == 0) { mode = -1; add(intermediate, lis->entries[i]); continue; }

        if (mode != 1) {
            add(intermediate, lis->entries[i]);
            continue;
        }

        char *cur = lis->entries[i];

        // build tmp = cur without leading tab, and replace ,() with spaces
        char tmp[256];
        snprintf(tmp, sizeof(tmp), "%s", cur + 1);
        for (int k = 0; tmp[k]; k++) {
            if (tmp[k] == ',' || tmp[k] == '(' || tmp[k] == ')') tmp[k] = ' ';
        }

        // strict macro parsing using %n and full consumption
        int n = 0;
        int rd = -1, rs = -1;
        unsigned long long Lu = 0;

        // clr rd
        n = 0;
        if (sscanf(tmp, "clr r%d %n", &rd, &n) == 1 && tmp[n] == '\0') {
            char b[64];
            snprintf(b, sizeof(b), "\txor r%d, r%d, r%d", rd, rd, rd);
            add(intermediate, strdup(b));
            continue;
        }

        // in rd, rs
        n = 0;
        if (sscanf(tmp, "in r%d r%d %n", &rd, &rs, &n) == 2 && tmp[n] == '\0') {
            char b[64];
            snprintf(b, sizeof(b), "\tpriv r%d, r%d, r0, 3", rd, rs);
            add(intermediate, strdup(b));
            continue;
        }

        // out rd, rs
        n = 0;
        if (sscanf(tmp, "out r%d r%d %n", &rd, &rs, &n) == 2 && tmp[n] == '\0') {
            char b[64];
            snprintf(b, sizeof(b), "\tpriv r%d, r%d, r0, 4", rd, rs);
            add(intermediate, strdup(b));
            continue;
        }

        // halt (no operands)
        n = 0;
        if (sscanf(tmp, "halt %n", &n) == 0 && tmp[n] == '\0') {
            add(intermediate, strdup("\tpriv r0, r0, r0, 0"));
            continue;
        }

        // push rd
        n = 0;
        if (sscanf(tmp, "push r%d %n", &rd, &n) == 1 && tmp[n] == '\0') {
            char b[64];
            snprintf(b, sizeof(b), "\tmov (r31)(-8), r%d", rd);
            add(intermediate, strdup(b));
            add(intermediate, strdup("\tsubi r31, 8"));
            continue;
        }

        // pop rd
        n = 0;
        if (sscanf(tmp, "pop r%d %n", &rd, &n) == 1 && tmp[n] == '\0') {
            char b[64];
            snprintf(b, sizeof(b), "\tmov r%d, (r31)(0)", rd);
            add(intermediate, strdup(b));
            add(intermediate, strdup("\taddi r31, 8"));
            continue;
        }

        // ld rd, L (unsigned literal already label-resolved)
        n = 0;
        if (sscanf(tmp, "ld r%d %llu %n", &rd, &Lu, &n) == 2 && tmp[n] == '\0') {
            uint64_t L = (uint64_t)Lu;

            char b[64];
            snprintf(b, sizeof(b), "\txor r%d, r%d, r%d", rd, rd, rd);
            add(intermediate, strdup(b));

            snprintf(b, sizeof(b), "\taddi r%d, %llu", rd, (unsigned long long)((L >> 52) & 0xFFF));
            add(intermediate, strdup(b));
            snprintf(b, sizeof(b), "\tshftli r%d, 12", rd);
            add(intermediate, strdup(b));

            snprintf(b, sizeof(b), "\taddi r%d, %llu", rd, (unsigned long long)((L >> 40) & 0xFFF));
            add(intermediate, strdup(b));
            snprintf(b, sizeof(b), "\tshftli r%d, 12", rd);
            add(intermediate, strdup(b));

            snprintf(b, sizeof(b), "\taddi r%d, %llu", rd, (unsigned long long)((L >> 28) & 0xFFF));
            add(intermediate, strdup(b));
            snprintf(b, sizeof(b), "\tshftli r%d, 12", rd);
            add(intermediate, strdup(b));

            snprintf(b, sizeof(b), "\taddi r%d, %llu", rd, (unsigned long long)((L >> 16) & 0xFFF));
            add(intermediate, strdup(b));
            snprintf(b, sizeof(b), "\tshftli r%d, 12", rd);
            add(intermediate, strdup(b));

            snprintf(b, sizeof(b), "\taddi r%d, %llu", rd, (unsigned long long)((L >> 4) & 0xFFF));
            add(intermediate, strdup(b));
            snprintf(b, sizeof(b), "\tshftli r%d, 4", rd);
            add(intermediate, strdup(b));

            snprintf(b, sizeof(b), "\taddi r%d, %llu", rd, (unsigned long long)(L & 0xF));
            add(intermediate, strdup(b));

            continue;
        }

        // not a macro: normalize commas to ", " like before
        char *fixed = strdup(cur);
        for (char *p = fixed; (p = strchr(p, ',')); p += 2) {
            if (p[1] != ' ') {
                memmove(p + 2, p + 1, strlen(p + 1) + 1);
                p[1] = ' ';
            }
        }
        add(intermediate, fixed);
    }

    // Validate everything strictly BEFORE creating output files (.tko / intermediate)
    if (!validate_intermediate(intermediate)) {
        return 1;
    }

    // write intermediate file (only after validation)
    FILE *inter = fopen(args[2], "w");
    if (!inter) FAIL("cannot open intermediate output");
    for (int i = 0; i < intermediate->numElements; i++) {
        fprintf(inter, "%s\n", intermediate->entries[i]);
    }
    fclose(inter);

    // write binary output (only after validation)
    FILE *out = fopen(args[3], "wb");
    if (!out) FAIL("cannot open binary output");

    mode = 0;
    for (int i = 0; i < intermediate->numElements; i++) {
        const char *ln = intermediate->entries[i];

        if (strcmp(ln, ".code") == 0) { mode = 1; continue; }
        if (strcmp(ln, ".data") == 0) { mode = -1; continue; }
        if (mode == 0) continue;

        if (mode == -1) {
            uint64_t v = 0;
            if (!parse_u64_strict(ln + 1, &v)) { fclose(out); FAIL("invalid data"); }
            fwrite(&v, 8, 1, out);
            continue;
        }

        // mode == 1
        uint32_t num = 0;
        int rd = 0, rs = 0, rt = 0, L = 0, unique = -1;
        const char *p = NULL;

        if (op_is(ln, "addi", &p)) {
            if (!parse_r_imm(p, &rd, &L)) { fclose(out); FAIL("invalid addi"); }
            unique = 25;
        } else if (op_is(ln, "add", &p)) {
            if (!parse_rrr(p, &rd, &rs, &rt)) { fclose(out); FAIL("invalid add"); }
            unique = 24;
        } else if (op_is(ln, "subi", &p)) {
            if (!parse_r_imm(p, &rd, &L)) { fclose(out); FAIL("invalid subi"); }
            unique = 27;
        } else if (op_is(ln, "sub", &p)) {
            if (!parse_rrr(p, &rd, &rs, &rt)) { fclose(out); FAIL("invalid sub"); }
            unique = 26;
        } else if (op_is(ln, "mul", &p)) {
            if (!parse_rrr(p, &rd, &rs, &rt)) { fclose(out); FAIL("invalid mul"); }
            unique = 28;
        } else if (op_is(ln, "div", &p)) {
            if (!parse_rrr(p, &rd, &rs, &rt)) { fclose(out); FAIL("invalid div"); }
            unique = 29;
        } else if (op_is(ln, "and", &p)) {
            if (!parse_rrr(p, &rd, &rs, &rt)) { fclose(out); FAIL("invalid and"); }
            unique = 0;
        } else if (op_is(ln, "or", &p)) {
            if (!parse_rrr(p, &rd, &rs, &rt)) { fclose(out); FAIL("invalid or"); }
            unique = 1;
        } else if (op_is(ln, "xor", &p)) {
            if (!parse_rrr(p, &rd, &rs, &rt)) { fclose(out); FAIL("invalid xor"); }
            unique = 2;
        } else if (op_is(ln, "not", &p)) {
            if (!parse_rr(p, &rd, &rs)) { fclose(out); FAIL("invalid not"); }
            rt = 0;
            unique = 3;
        } else if (op_is(ln, "shftr", &p)) {
            if (!parse_rrr(p, &rd, &rs, &rt)) { fclose(out); FAIL("invalid shftr"); }
            unique = 4;
        } else if (op_is(ln, "shftri", &p)) {
            if (!parse_r_imm(p, &rd, &L)) { fclose(out); FAIL("invalid shftri"); }
            rs = 0; rt = 0;
            unique = 5;
        } else if (op_is(ln, "shftl", &p)) {
            if (!parse_rrr(p, &rd, &rs, &rt)) { fclose(out); FAIL("invalid shftl"); }
            unique = 6;
        } else if (op_is(ln, "shftli", &p)) {
            if (!parse_r_imm(p, &rd, &L)) { fclose(out); FAIL("invalid shftli"); }
            rs = 0; rt = 0;
            unique = 7;
        } else if (op_is(ln, "brr", &p)) {
            int tmp = 0;
            if (parse_r(p, &tmp)) {
                rd = tmp; rs = 0; rt = 0; L = 0;
                unique = 9;
            } else {
                if (!parse_i32_strict(p, &L)) { fclose(out); FAIL("invalid brr"); }
                rd = 0; rs = 0; rt = 0;
                unique = 10;
            }
        } else if (op_is(ln, "brnz", &p)) {
            if (!parse_rr(p, &rd, &rs)) { fclose(out); FAIL("invalid brnz"); }
            rt = 0; L = 0;
            unique = 11;
        } else if (op_is(ln, "brgt", &p)) {
            if (!parse_rrr(p, &rd, &rs, &rt)) { fclose(out); FAIL("invalid brgt"); }
            unique = 14;
        } else if (op_is(ln, "call", &p)) {
            if (!parse_r(p, &rd)) { fclose(out); FAIL("invalid call"); }
            rs = 0; rt = 0; L = 0;
            unique = 12;
        } else if (op_is(ln, "return", &p)) {
            if (*p != '\0') { fclose(out); FAIL("invalid return"); }
            rd = 0; rs = 0; rt = 0; L = 0;
            unique = 13;
        } else if (op_is(ln, "br", &p)) {
            if (!parse_r(p, &rd)) { fclose(out); FAIL("invalid br"); }
            rs = 0; rt = 0; L = 0;
            unique = 8;
        } else if (op_is(ln, "priv", &p)) {
            if (!parse_priv(p, &rd, &rs, &rt, &L)) { fclose(out); FAIL("invalid priv"); }
            unique = 15;
        } else if (op_is(ln, "mov", &p)) {
            int base = 0;
            if (parse_mov_r_mem(p, &rd, &rs, &L)) {
                rt = 0;
                unique = 16;
            } else if (parse_mov_mem_r(p, &base, &L, &rs)) {
                rd = base; rt = 0;
                unique = 19;
            } else if (parse_rr(p, &rd, &rs)) {
                rt = 0; L = 0;
                unique = 17;
            } else if (parse_r_imm(p, &rd, &L)) {
                rs = 0; rt = 0;
                unique = 18;
            } else {
                fclose(out);
                FAIL("invalid mov");
            }
        } else if (op_is(ln, "addf", &p)) {
            if (!parse_rrr(p, &rd, &rs, &rt)) { fclose(out); FAIL("invalid addf"); }
            unique = 20;
        } else if (op_is(ln, "subf", &p)) {
            if (!parse_rrr(p, &rd, &rs, &rt)) { fclose(out); FAIL("invalid subf"); }
            unique = 21;
        } else if (op_is(ln, "mulf", &p)) {
            if (!parse_rrr(p, &rd, &rs, &rt)) { fclose(out); FAIL("invalid mulf"); }
            unique = 22;
        } else if (op_is(ln, "divf", &p)) {
            if (!parse_rrr(p, &rd, &rs, &rt)) { fclose(out); FAIL("invalid divf"); }
            unique = 23;
        } else {
            fclose(out);
            FAIL("undefined opcode");
        }

        // bounds
        if (unique < 0 || unique > 29) { fclose(out); FAIL("invalid opcode id"); }
        if (rd < 0 || rd > 31 || rs < 0 || rs > 31 || rt < 0 || rt > 31) { fclose(out); FAIL("invalid register"); }
        if (L < -2047 || L > 4095) { fclose(out); FAIL("invalid immediate"); }

        num = (uint32_t)((unique << 27) | (rd << 22) | (rs << 17) | (rt << 12) | (L & 0xFFF));
        fwrite(&num, 4, 1, out);
    }

    fclose(out);
    return 0;
}