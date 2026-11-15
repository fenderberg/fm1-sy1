/* Synth1 .sy1 patch text: line 1 = the name, `key=value` header lines (color, ver), then `index,value`
 * lines. Missing lines keep Synth1's init value; patches older than ver 109 are converted as Synth1 does. */
#pragma once
#include <stdint.h>
#include "s1_tables.h"

#define S1_NAME_LEN 32

typedef struct {
    char name[S1_NAME_LEN];
    int version;                    /* 0: no ver line (read as 100) */
    int32_t values[S1_NPARAMS];
} s1_patch;

void s1_patch_init(s1_patch *p);                                  /* Synth1's init values */
int s1_patch_parse(s1_patch *p, const char *text, uint32_t len);  /* the number of parameter lines read */
