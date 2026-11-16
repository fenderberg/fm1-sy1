/* SY1: a Synth1-compatible polysynth for the M-VAVE FM-1, on fm1-base. It plays .sy1 patches.
 * This header is the app's half of the contract with the platform (base/device/unity.c, base/host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "SY1"
#define APP_TAGLINE "500 SYNTH1 PRESETS"
#define APP_SPLASH_RGB RGB(255, 184, 70)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TOUCH                        /* velocity from the keys (lib/touch.h) */
#define APP_SYSEX                        /* patches come in over USB SysEx (bank.h) */
#define APP_OVERLOAD                     /* the render ran out of time: fewer voices */
#define APP_LOG                          /* the development readout: the render's load (fm1_remote.py log) */

/* the store: the settings, and the panel calibration (the platform's). The user patches are not store objects:
 * they fill the flash's data area from PLAT_DATA_LO (bank.h), under these */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
int app_sysex(const uint8_t *p, uint32_t n);
void app_overload(void);
void app_log(void (*line)(const char *t));

/* the host simulator's checks (base/host/plat_host.c "expect") */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
