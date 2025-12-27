#include "fb_map.h"
#include <string.h>
#include <stdio.h>
#include <linux/input.h>

static int ctrl_pressed = 0;
static int alt_pressed = 0;
static int shift_pressed = 0;
static int escape_state = 0;
static char escape_buf[16];
static int escape_buf_len = 0;
static unsigned char last_char = 0;

void kb_init(void) {
    ctrl_pressed = 0;
    alt_pressed = 0;
    shift_pressed = 0;
    escape_state = 0;
    escape_buf_len = 0;
}

/* Handle key release to reset modifier state */
void kb_handle_release(int keycode) {
    if (keycode == KEY_LEFTCTRL || keycode == KEY_RIGHTCTRL) {
        ctrl_pressed = 0;
    }
    if (keycode == KEY_LEFTSHIFT || keycode == KEY_RIGHTSHIFT) {
        shift_pressed = 0;
    }
    if (keycode == KEY_LEFTALT || keycode == KEY_RIGHTALT) {
        alt_pressed = 0;
    }
}

/* Map Linux key code to action */
KeyAction kb_map_key(int keycode) {
    /* Track modifier keys */
    if (keycode == KEY_LEFTCTRL || keycode == KEY_RIGHTCTRL) {
        return ACTION_NONE;
    }
    if (keycode == KEY_LEFTSHIFT || keycode == KEY_RIGHTSHIFT) {
        return ACTION_NONE;
    }
    if (keycode == KEY_LEFTALT || keycode == KEY_RIGHTALT) {
        return ACTION_NONE;
    }

    /* Ctrl+Q to quit */
    if (keycode == KEY_Q && ctrl_pressed) {
        return ACTION_QUIT;
    }

    return ACTION_NONE;
}

/* Convert Linux key code to character or escape sequence */
const char* kb_get_sequence(int keycode) {
    static char seq[8];
    seq[0] = '\0';

    /* Update modifier state */
    if (keycode == KEY_LEFTCTRL || keycode == KEY_RIGHTCTRL) {
        ctrl_pressed = 1;
        return "";
    }
    if (keycode == KEY_LEFTSHIFT || keycode == KEY_RIGHTSHIFT) {
        shift_pressed = 1;
        return "";
    }
    if (keycode == KEY_LEFTALT || keycode == KEY_RIGHTALT) {
        alt_pressed = 1;
        return "";
    }

    /* Arrow keys - emit standard ANSI escape sequences */
    if (keycode == KEY_UP) return "\x1b[A";
    if (keycode == KEY_DOWN) return "\x1b[B";
    if (keycode == KEY_RIGHT) return "\x1b[C";
    if (keycode == KEY_LEFT) return "\x1b[D";

    /* Special keys */
    if (keycode == KEY_HOME) return "\x1b[H";
    if (keycode == KEY_END) return "\x1b[F";
    if (keycode == KEY_PAGEUP) return "\x1b[5~";
    if (keycode == KEY_PAGEDOWN) return "\x1b[6~";
    if (keycode == KEY_INSERT) return "\x1b[2~";
    if (keycode == KEY_DELETE) return "\x1b[3~";

    /* Enter/Return */
    if (keycode == KEY_ENTER) return "\r";

    /* Tab */
    if (keycode == KEY_TAB) return "\t";

    /* Backspace */
    if (keycode == KEY_BACKSPACE) return "\x7f";

    /* Escape */
    if (keycode == KEY_ESC) return "\x1b";

    /* Space */
    if (keycode == KEY_SPACE) return " ";

    /* Handle Ctrl+key combinations */
    if (ctrl_pressed) {
        if (keycode == KEY_C) return "\x03";  /* Ctrl+C */
        if (keycode == KEY_D) return "\x04";  /* Ctrl+D */
        if (keycode == KEY_Z) return "\x1a";  /* Ctrl+Z */
        if (keycode == KEY_L) return "\x0c";  /* Ctrl+L */
        if (keycode == KEY_A) return "\x01";  /* Ctrl+A */
        if (keycode == KEY_E) return "\x05";  /* Ctrl+E */
        if (keycode == KEY_K) return "\x0b";  /* Ctrl+K */
        if (keycode == KEY_U) return "\x15";  /* Ctrl+U */
        if (keycode == KEY_W) return "\x17";  /* Ctrl+W */
        if (keycode == KEY_R) return "\x12";  /* Ctrl+R */
        /* Add more as needed */
    }

    /* Letter keys */
    if (keycode >= KEY_A && keycode <= KEY_Z) {
        int idx = keycode - KEY_A;
        seq[0] = shift_pressed ? 'A' + idx : 'a' + idx;
        seq[1] = '\0';
        return seq;
    }

    /* Number keys */
    if (keycode >= KEY_1 && keycode <= KEY_9) {
        if (shift_pressed) {
            const char *shifted = "!@#$%^&*("  ;
            seq[0] = shifted[keycode - KEY_1];
        } else {
            seq[0] = '1' + (keycode - KEY_1);
        }
        seq[1] = '\0';
        return seq;
    }
    if (keycode == KEY_0) {
        seq[0] = shift_pressed ? ')' : '0';
        seq[1] = '\0';
        return seq;
    }

    /* Symbol keys */
    if (keycode == KEY_MINUS) { seq[0] = shift_pressed ? '_' : '-'; seq[1] = '\0'; return seq; }
    if (keycode == KEY_EQUAL) { seq[0] = shift_pressed ? '+' : '='; seq[1] = '\0'; return seq; }
    if (keycode == KEY_LEFTBRACE) { seq[0] = shift_pressed ? '{' : '['; seq[1] = '\0'; return seq; }
    if (keycode == KEY_RIGHTBRACE) { seq[0] = shift_pressed ? '}' : ']'; seq[1] = '\0'; return seq; }
    if (keycode == KEY_SEMICOLON) { seq[0] = shift_pressed ? ':' : ';'; seq[1] = '\0'; return seq; }
    if (keycode == KEY_APOSTROPHE) { seq[0] = shift_pressed ? '"' : '\''; seq[1] = '\0'; return seq; }
    if (keycode == KEY_GRAVE) { seq[0] = shift_pressed ? '~' : '`'; seq[1] = '\0'; return seq; }
    if (keycode == KEY_BACKSLASH) { seq[0] = shift_pressed ? '|' : '\\'; seq[1] = '\0'; return seq; }
    if (keycode == KEY_COMMA) { seq[0] = shift_pressed ? '<' : ','; seq[1] = '\0'; return seq; }
    if (keycode == KEY_DOT) { seq[0] = shift_pressed ? '>' : '.'; seq[1] = '\0'; return seq; }
    if (keycode == KEY_SLASH) { seq[0] = shift_pressed ? '?' : '/'; seq[1] = '\0'; return seq; }

    return "";
}

KeyAction kb_process_input(unsigned char ch, int *modifiers, char *output, int *output_len) {
    *output_len = 0;
    *modifiers = 0;

    if (ctrl_pressed) *modifiers |= MOD_CTRL;
    if (alt_pressed) *modifiers |= MOD_ALT;
    if (shift_pressed) *modifiers |= MOD_SHIFT;

    /* Handle escape sequences for special keys */
    if (escape_state > 0) {
        escape_buf[escape_buf_len++] = ch;

        /* Arrow keys: ESC[A, ESC[B, ESC[C, ESC[D */
        if (escape_state == 2 && ch >= 'A' && ch <= 'D') {
            escape_state = 0;
            escape_buf_len = 0;

            switch(ch) {
                case 'A': /* Up arrow */
                    strcpy(output, "\033[A");
                    *output_len = 3;
                    return ACTION_NONE;
                case 'B': /* Down arrow */
                    strcpy(output, "\033[B");
                    *output_len = 3;
                    return ACTION_NONE;
                case 'C': /* Right arrow */
                    strcpy(output, "\033[C");
                    *output_len = 3;
                    return ACTION_NONE;
                case 'D': /* Left arrow */
                    strcpy(output, "\033[D");
                    *output_len = 3;
                    return ACTION_NONE;
            }
        }

        /* Function keys and special keys: ESC[1~, ESC[2~, etc. */
        if (escape_state == 2 && ch == '~') {
            escape_state = 0;
            int code = 0;
            if (escape_buf_len >= 2) {
                code = escape_buf[1] - '0';
                if (escape_buf_len == 3) {
                    code = code * 10 + (escape_buf[2] - '0');
                }
            }
            escape_buf_len = 0;

            switch(code) {
                case 1: /* Home */
                    strcpy(output, "\033[H");
                    *output_len = 3;
                    break;
                case 2: /* Insert */
                    strcpy(output, "\033[2~");
                    *output_len = 4;
                    break;
                case 3: /* Delete */
                    strcpy(output, "\033[3~");
                    *output_len = 4;
                    break;
                case 4: /* End */
                    strcpy(output, "\033[F");
                    *output_len = 3;
                    break;
                case 5: /* Page Up */
                    if (ctrl_pressed) {
                        return ACTION_SCROLL_UP;
                    }
                    strcpy(output, "\033[5~");
                    *output_len = 4;
                    break;
                case 6: /* Page Down */
                    if (ctrl_pressed) {
                        return ACTION_SCROLL_DOWN;
                    }
                    strcpy(output, "\033[6~");
                    *output_len = 4;
                    break;
            }
            return ACTION_NONE;
        }

        /* Continue building escape sequence */
        if (escape_buf_len < 15) {
            return ACTION_NONE;
        }

        /* Timeout - reset */
        escape_state = 0;
        escape_buf_len = 0;
        return ACTION_NONE;
    }

    /* Start of escape sequence */
    if (ch == 0x1B) {
        escape_state = 1;
        escape_buf[0] = ch;
        escape_buf_len = 1;
        return ACTION_NONE;
    }

    /* Continue escape sequence */
    if (escape_state == 1 && ch == '[') {
        escape_state = 2;
        return ACTION_NONE;
    }

    /* Standard keyboard shortcuts with Ctrl */
    if (ch < 32) {
        ctrl_pressed = 1;
        last_char = ch;

        switch(ch) {
            case 0x03: /* Ctrl+C */
                output[0] = ch;
                *output_len = 1;
                return ACTION_NONE;

            case 0x04: /* Ctrl+D */
                output[0] = ch;
                *output_len = 1;
                return ACTION_NONE;

            case 0x0C: /* Ctrl+L - Clear screen */
                output[0] = ch;
                *output_len = 1;
                return ACTION_CLEAR_SCREEN;

            case 0x1A: /* Ctrl+Z */
                output[0] = ch;
                *output_len = 1;
                return ACTION_NONE;

            case 0x11: /* Ctrl+Q - Quit */
                return ACTION_QUIT;

            case 0x0B: /* Ctrl+K - Cut to end of line */
                output[0] = ch;
                *output_len = 1;
                return ACTION_NONE;

            case 0x15: /* Ctrl+U - Cut to beginning of line */
                output[0] = ch;
                *output_len = 1;
                return ACTION_NONE;

            case 0x01: /* Ctrl+A - Beginning of line */
                output[0] = ch;
                *output_len = 1;
                return ACTION_NONE;

            case 0x05: /* Ctrl+E - End of line */
                output[0] = ch;
                *output_len = 1;
                return ACTION_NONE;

            case 0x02: /* Ctrl+B - Back one char */
                output[0] = ch;
                *output_len = 1;
                return ACTION_NONE;

            case 0x06: /* Ctrl+F - Forward one char */
                output[0] = ch;
                *output_len = 1;
                return ACTION_NONE;

            case 0x10: /* Ctrl+P - Previous history */
                output[0] = ch;
                *output_len = 1;
                return ACTION_NONE;

            case 0x0E: /* Ctrl+N - Next history */
                output[0] = ch;
                *output_len = 1;
                return ACTION_NONE;

            case 0x12: /* Ctrl+R - Reverse search */
                output[0] = ch;
                *output_len = 1;
                return ACTION_NONE;

            case 0x17: /* Ctrl+W - Delete word backward */
                output[0] = ch;
                *output_len = 1;
                return ACTION_NONE;

            case 0x19: /* Ctrl+Y - Yank (paste) */
                output[0] = ch;
                *output_len = 1;
                return ACTION_PASTE;

            case 0x1F: /* Ctrl+Shift+- (Ctrl+_) - Decrease font */
                return ACTION_DECREASE_FONT;

            default:
                output[0] = ch;
                *output_len = 1;
                return ACTION_NONE;
        }

        ctrl_pressed = 0;
    }

    /* Ctrl+= for increase font (detect '=' after any control char) */
    if (ch == '=') {
        if (last_char < 32 && last_char != 0) {
            last_char = 0;
            return ACTION_INCREASE_FONT;
        }
    }

    /* Ctrl+- for decrease font (detect '-' after any control char) */
    if (ch == '-') {
        if (last_char < 32 && last_char != 0) {
            last_char = 0;
            return ACTION_DECREASE_FONT;
        }
    }

    /* Reset ctrl flag for regular characters */
    if (ctrl_pressed && ch >= 32) {
        ctrl_pressed = 0;
    }
    last_char = ch;

    /* Regular character */
    output[0] = ch;
    *output_len = 1;

    return ACTION_NONE;
}
