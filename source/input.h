/*
 * Copyright (C) 2026 Prince of Persia Classic Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef SOLOADER_INPUT_H
#define SOLOADER_INPUT_H

#include <stddef.h>
#include <stdint.h>
#include <psp2/ctrl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Rear touch quadrants reported as extra buttons
enum {
    CUSTOM_BTN_L2 = (1 << 24), // Rear touch top-left
    CUSTOM_BTN_R2 = (1 << 25), // Rear touch top-right
    CUSTOM_BTN_L3 = (1 << 26), // Rear touch bottom-left
    CUSTOM_BTN_R3 = (1 << 27), // Rear touch bottom-right
};

enum {
    ACT_JUMP = 0,
    ACT_CROUCH,
    ACT_INTERACT,
    ACT_ROLL,
    ACT_UP,
    ACT_LEFT,
    ACT_RIGHT,
    ACT_TOGGLE_HUD,
    ACT_COUNT
};

void input_init(void);
void input_reload_controls(void);
void input_controls_defaults(void);
void input_controls_save(void);

int         input_action_count(void);
const char *input_action_label(int action);
uint32_t    input_action_buttons(int action);
void        input_action_bind(int action, uint32_t button, int add);
void        input_action_clear(int action);

uint32_t    input_bindable_buttons(void);
void        input_buttons_text(uint32_t mask, char *out, size_t size);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_INPUT_H
