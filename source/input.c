/*
 * Copyright (C) 2026 Prince of Persia Classic Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "input.h"
#include "utils/logger.h"

#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define CONTROLS_PATH    DATA_PATH "controls.txt"
#define CONTROLS_VERSION 2

typedef struct {
    const char *name;   // controls.txt key
    const char *label;  // port menu
    uint32_t default_buttons;
    uint32_t buttons;
} Action;

static Action actions[ACT_COUNT] = {
    [ACT_JUMP]        = { "JUMP",        "Jump / Attack / Confirm", SCE_CTRL_CROSS,                      0 },
    [ACT_CROUCH]      = { "CROUCH",      "Crouch / Defend",         SCE_CTRL_SQUARE,                     0 },
    [ACT_INTERACT]    = { "INTERACT",    "Interact / Sheathe",      SCE_CTRL_TRIANGLE,                   0 },
    [ACT_ROLL]        = { "ROLL",        "Roll / Crouch Down",      SCE_CTRL_CIRCLE | SCE_CTRL_DOWN,     0 },
    [ACT_UP]          = { "UP",          "Climb / Up",              SCE_CTRL_UP,                         0 },
    [ACT_LEFT]        = { "LEFT",        "Walk / Run Left",         SCE_CTRL_LEFT,                       0 },
    [ACT_RIGHT]       = { "RIGHT",       "Walk / Run Right",        SCE_CTRL_RIGHT,                      0 },
    [ACT_TOGGLE_HUD]  = { "TOGGLE_HUD",  "Toggle on-screen HUD",    SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER, 0 },
};

typedef struct {
    const char *name;   // controls.txt spelling
    const char *label;  // port menu
    uint32_t mask;
} ButtonName;

static const ButtonName button_names[] = {
    { "CROSS",       "Cross",    SCE_CTRL_CROSS },
    { "CIRCLE",      "Circle",   SCE_CTRL_CIRCLE },
    { "SQUARE",      "Square",   SCE_CTRL_SQUARE },
    { "TRIANGLE",    "Triangle", SCE_CTRL_TRIANGLE },
    { "L1",          "L",        SCE_CTRL_LTRIGGER },
    { "R1",          "R",        SCE_CTRL_RTRIGGER },
    { "UP",          "Up",       SCE_CTRL_UP },
    { "DOWN",        "Down",     SCE_CTRL_DOWN },
    { "LEFT",        "Left",     SCE_CTRL_LEFT },
    { "RIGHT",       "Right",    SCE_CTRL_RIGHT },
    { "SELECT",      "Select",   SCE_CTRL_SELECT },
    { "START",       "Start",    SCE_CTRL_START },
    { "L2",          "Rear TL",  CUSTOM_BTN_L2 },
    { "R2",          "Rear TR",  CUSTOM_BTN_R2 },
    { "L3",          "Rear BL",  CUSTOM_BTN_L3 },
    { "R3",          "Rear BR",  CUSTOM_BTN_R3 },
    // Aliases
    { "X",           NULL,       SCE_CTRL_CROSS },
    { "O",           NULL,       SCE_CTRL_CIRCLE },
    { "LTRIGGER",    NULL,       SCE_CTRL_LTRIGGER },
    { "L",           NULL,       SCE_CTRL_LTRIGGER },
    { "RTRIGGER",    NULL,       SCE_CTRL_RTRIGGER },
    { "R",           NULL,       SCE_CTRL_RTRIGGER },
    { "DPAD_UP",     NULL,       SCE_CTRL_UP },
    { "DPAD_DOWN",   NULL,       SCE_CTRL_DOWN },
    { "DPAD_LEFT",   NULL,       SCE_CTRL_LEFT },
    { "DPAD_RIGHT",  NULL,       SCE_CTRL_RIGHT },
    { "REAR_UP_L",   NULL,       CUSTOM_BTN_L2 },
    { "REAR_UP_R",   NULL,       CUSTOM_BTN_R2 },
    { "REAR_DOWN_L", NULL,       CUSTOM_BTN_L3 },
    { "REAR_DOWN_R", NULL,       CUSTOM_BTN_R3 },
};
#define BUTTON_NAMES_COUNT (sizeof(button_names) / sizeof(button_names[0]))

int input_action_count(void) {
    return ACT_COUNT;
}

const char *input_action_label(int action) {
    return (action >= 0 && action < ACT_COUNT) ? actions[action].label : "";
}

uint32_t input_action_buttons(int action) {
    return (action >= 0 && action < ACT_COUNT) ? actions[action].buttons : 0;
}

void input_action_bind(int action, uint32_t button, int add) {
    if (action < 0 || action >= ACT_COUNT)
        return;
    for (int i = 0; i < ACT_COUNT; i++)
        actions[i].buttons &= ~button;
    if (add)
        actions[action].buttons |= button;
    else
        actions[action].buttons = button;
}

void input_action_clear(int action) {
    if (action >= 0 && action < ACT_COUNT)
        actions[action].buttons = 0;
}

void input_controls_defaults(void) {
    for (int i = 0; i < ACT_COUNT; i++)
        actions[i].buttons = actions[i].default_buttons;
}

uint32_t input_bindable_buttons(void) {
    uint32_t mask = 0;
    for (unsigned i = 0; i < BUTTON_NAMES_COUNT; i++)
        if (button_names[i].label)
            mask |= button_names[i].mask;
    return mask & ~SCE_CTRL_START;
}

static void buttons_join(uint32_t mask, char *out, size_t size, int labels) {
    size_t len = 0;
    out[0] = '\0';
    for (unsigned i = 0; i < BUTTON_NAMES_COUNT; i++) {
        const ButtonName *b = &button_names[i];
        if (!b->label || !(mask & b->mask))
            continue;
        int n = snprintf(out + len, size - len, "%s%s", len ? ", " : "", labels ? b->label : b->name);
        if (n < 0 || (size_t) n >= size - len)
            break;
        len += n;
    }
    if (!len)
        snprintf(out, size, "%s", labels ? "-" : "NONE");
}

void input_buttons_text(uint32_t mask, char *out, size_t size) {
    buttons_join(mask, out, size, 1);
}

static void word(const char *s, char *out, size_t size) {
    while (*s == ' ' || *s == '\t')
        s++;
    size_t len = 0;
    while (*s && !strchr(" \t,=:#;\r\n", *s) && len < size - 1)
        out[len++] = (char) toupper((unsigned char) *s++);
    out[len] = '\0';
}

static uint32_t parse_button_token(const char *tok) {
    char clean[32];
    word(tok, clean, sizeof(clean));
    if (!clean[0] || strcmp(clean, "NONE") == 0)
        return 0;
    for (unsigned i = 0; i < BUTTON_NAMES_COUNT; i++) {
        if (strcmp(clean, button_names[i].name) == 0)
            return button_names[i].mask;
    }
    l_warn("input: unknown button '%s'", clean);
    return 0;
}

static uint32_t parse_button_list(const char *p) {
    uint32_t mask = 0;
    while (*p && *p != '#' && *p != ';' && *p != '\r' && *p != '\n') {
        mask |= parse_button_token(p);
        while (*p && *p != ',' && *p != '#' && *p != ';' && *p != '\r' && *p != '\n')
            p++;
        if (*p == ',')
            p++;
    }
    return mask;
}

static int find_action_index(const char *name) {
    char clean[32];
    word(name, clean, sizeof(clean));
    for (int i = 0; i < ACT_COUNT; i++) {
        if (strcmp(clean, actions[i].name) == 0)
            return i;
    }
    return -1;
}

void input_controls_save(void) {
    FILE *f = fopen(CONTROLS_PATH, "w");
    if (!f) {
        l_error("input: cannot write %s", CONTROLS_PATH);
        return;
    }
    fprintf(f,
        "# Prince of Persia Classic - PS Vita controls\n"
        "# Also editable in game: START + SELECT (or SELECT in menus).\n"
        "#\n"
        "# Buttons: CROSS, CIRCLE, SQUARE, TRIANGLE, L1, R1, UP, DOWN, LEFT, RIGHT,\n"
        "#   SELECT, L2 / R2 (rear touch top-left / top-right),\n"
        "#   L3 / R3 (rear touch bottom-left / bottom-right), NONE.\n"
        "#   START is always pause / back.\n"
        "#\n"
        "# Actions:\n"
        "#   JUMP        Jump / Attack with sword / Menu confirm\n"
        "#   CROUCH      Crouch / Defend with sword\n"
        "#   INTERACT    Interact with objects / Sheathe sword\n"
        "#   ROLL        Roll forward / Crouch down\n"
        "#   UP          Climb ledges / Look up\n"
        "#   LEFT        Walk / Run left (tap: walk, hold: run)\n"
        "#   RIGHT       Walk / Run right (tap: walk, hold: run)\n"
        "#   TOGGLE_HUD  Show / hide on-screen virtual touch controls\n"
        "#\n"
        "# ACTION = BUTTON, BUTTON ...   (or BUTTON = ACTION)\n"
        "\n"
        "VERSION = %d\n", CONTROLS_VERSION);
    for (int i = 0; i < ACT_COUNT; i++) {
        char list[128];
        buttons_join(actions[i].buttons, list, sizeof(list), 0);
        fprintf(f, "%s = %s\n", actions[i].name, list);
    }
    fclose(f);
}

void input_reload_controls(void) {
    input_controls_defaults();

    FILE *f = fopen(CONTROLS_PATH, "r");
    if (!f) {
        input_controls_save();
        l_info("input: generated default %s", CONTROLS_PATH);
        return;
    }

    uint32_t parsed[ACT_COUNT] = {0};
    int seen[ACT_COUNT] = {0};
    int version = 1;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == ';' || *p == '\r' || *p == '\n' || *p == '\0')
            continue;
        char *eq = strpbrk(p, "=:");
        if (!eq)
            continue;
        *eq = '\0';
        char *right = eq + 1;

        char key[32];
        word(p, key, sizeof(key));
        if (strcmp(key, "VERSION") == 0) {
            sscanf(right, "%d", &version);
            continue;
        }

        int act = find_action_index(p);
        if (act >= 0) {
            seen[act] = 1;
            parsed[act] |= parse_button_list(right);
        } else {
            act = find_action_index(right);
            uint32_t btn = parse_button_token(p);
            if (act >= 0 && btn) {
                seen[act] = 1;
                parsed[act] |= btn;
            }
        }
    }
    fclose(f);

    if (version < CONTROLS_VERSION) {
        input_controls_save();
        l_info("input: %s was version %d, replaced with defaults", CONTROLS_PATH, version);
    } else {
        for (int i = 0; i < ACT_COUNT; i++) {
            if (seen[i])
                actions[i].buttons = parsed[i];
        }
    }

    for (int i = 0; i < ACT_COUNT; i++)
        l_info("input: %-11s -> 0x%08X", actions[i].name, actions[i].buttons);
}

void input_init(void) {
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK,  SCE_TOUCH_SAMPLING_STATE_START);

    input_reload_controls();
    l_info("input: controls initialized");
}
