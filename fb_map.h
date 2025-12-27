#ifndef FB_MAP_H
#define FB_MAP_H

#include <stdint.h>

/* Key modifiers */
#define MOD_CTRL  (1 << 0)
#define MOD_ALT   (1 << 1)
#define MOD_SHIFT (1 << 2)

/* Keyboard shortcut action types */
typedef enum {
    ACTION_NONE,
    ACTION_COPY,
    ACTION_PASTE,
    ACTION_SCROLL_UP,
    ACTION_SCROLL_DOWN,
    ACTION_CLEAR_SCREEN,
    ACTION_QUIT,
    ACTION_INCREASE_FONT,
    ACTION_DECREASE_FONT,
} KeyAction;

/* Initialize keyboard mapping */
void kb_init(void);

/* Map Linux key code to action */
KeyAction kb_map_key(int keycode);

/* Get character or escape sequence for a Linux key code */
const char* kb_get_sequence(int keycode);

/* Handle key release event */
void kb_handle_release(int keycode);

/* Process keyboard input and return action (legacy - for stdin-based input) */
KeyAction kb_process_input(unsigned char ch, int *modifiers, char *output, int *output_len);

#endif /* FB_MAP_H */
