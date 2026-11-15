/* A patch as the SysEx frame SY1 takes (app/bank.h): F0 7D 53 59 31 01 <slot lo7> <slot hi7> <packed record> F7.
 * Shared by the packer (sy1pack.c) and the app's test (app_test.c). */
#pragma once
#include <stdint.h>
#include "s1_patch.h"

#define SY1_PAYLOAD 226u
#define SY1_FRAME_MAX (8u + (SY1_PAYLOAD + 6u) / 7u * 8u + 1u)

static uint32_t sy1_frame(const s1_patch *p, uint32_t slot, uint8_t *out)
{
    uint8_t rec[SY1_PAYLOAD + 7u];
    uint32_t i, j, n = 0;
    for (i = 0; i < sizeof rec; i++)
        rec[i] = 0;
    rec[0] = 0xA5;                       /* used */
    rec[1] = 1;                          /* format */
    rec[2] = rec[3] = 0xFF;
    for (i = 0; i < 23u && p->name[i]; i++)   /* the name: printable ASCII, the rest as '?' */
        rec[4u + i] = (uint8_t)p->name[i] >= 32u && (uint8_t)p->name[i] < 127u ? (uint8_t)p->name[i] : '?';
    for (i = 0; i < S1_NPARAMS; i++) {
        rec[28u + 2u * i] = (uint8_t)(p->values[i] & 0xFF);
        rec[29u + 2u * i] = (uint8_t)(p->values[i] >> 8 & 0xFF);
    }
    out[n++] = 0xF0; out[n++] = 0x7D; out[n++] = 0x53; out[n++] = 0x59; out[n++] = 0x31; out[n++] = 0x01;
    out[n++] = (uint8_t)(slot & 0x7Fu);
    out[n++] = (uint8_t)(slot >> 7 & 0x7Fu);
    for (i = 0; i < SY1_PAYLOAD; i += 7u) {   /* each 7 bytes as 8: their top bits, then their low 7 */
        uint8_t top = 0;
        for (j = 0; j < 7u; j++)
            top |= (uint8_t)((rec[i + j] >> 7) << j);
        out[n++] = top;
        for (j = 0; j < 7u; j++)
            out[n++] = rec[i + j] & 0x7Fu;
    }
    out[n++] = 0xF7;
    return n;
}
