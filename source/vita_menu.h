/*
 * Copyright (C) 2026 Prince of Persia Classic Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef SOLOADER_VITA_MENU_H
#define SOLOADER_VITA_MENU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int  vita_menu_active(void);
void vita_menu_open(void);
void vita_menu_update(uint32_t held, uint32_t pressed);
void vita_menu_render(void);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_VITA_MENU_H
