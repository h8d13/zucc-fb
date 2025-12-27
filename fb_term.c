/*
 * Framebuffer Terminal Emulator - Full PTY-based terminal with ANSI support
 * Compile: gcc -o fb_term fb_term.c fb_map.c -lm -lutil
 * Run: sudo ./fb_term /path/to/font.ttf
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>
#include <termios.h>
#include <pty.h>
#include <utmp.h>
#include <signal.h>
#include <dirent.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "fb_map.h"

#define MAX_FONTS 4
#define MAX_ESCAPE_PARAMS 16
#define MAX_TERM_COLS 500
#define MAX_TERM_ROWS 200

/* Terminal dimensions - will be calculated dynamically */
static int TERM_COLS = 80;
static int TERM_ROWS = 24;

struct framebuffer {
    int fd;
    uint8_t *mem;
    size_t mem_size;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    int width;
    int height;
    int bpp;
    int line_length;
};

struct font_entry {
    stbtt_fontinfo info;
    unsigned char *buffer;
    size_t buffer_size;
    const char *name;
};

struct cell {
    uint32_t codepoint;  /* Full Unicode codepoint */
    uint32_t fg_color;
    uint32_t bg_color;
    int bold;
};

struct terminal {
    struct cell cells[MAX_TERM_ROWS][MAX_TERM_COLS];
    int cursor_x;
    int cursor_y;
    int cursor_visible;
    uint32_t fg_color;
    uint32_t bg_color;
    int bold;
    int scroll_top;
    int scroll_bottom;
    int master_fd;  /* PTY master fd for sending responses */

    /* ANSI escape sequence parser state */
    enum {
        STATE_NORMAL,
        STATE_ESC,
        STATE_CSI,
        STATE_OSC
    } state;
    int escape_params[MAX_ESCAPE_PARAMS];
    int num_escape_params;
    int private_mode;  /* For CSI ? sequences */
    char escape_buf[256];
    int escape_buf_len;

    /* UTF-8 decoder state */
    unsigned char utf8_buf[4];
    int utf8_buf_len;
};

/* Color palette (xterm-256 compatible) */
uint32_t color_palette[256];

void init_color_palette() {
    /* Basic 16 colors */
    color_palette[0] = 0x00000000;  /* Black */
    color_palette[1] = 0x00CD0000;  /* Red */
    color_palette[2] = 0x0000CD00;  /* Green */
    color_palette[3] = 0x00CDCD00;  /* Yellow */
    color_palette[4] = 0x000000EE;  /* Blue */
    color_palette[5] = 0x00CD00CD;  /* Magenta */
    color_palette[6] = 0x0000CDCD;  /* Cyan */
    color_palette[7] = 0x00E5E5E5;  /* White */
    color_palette[8] = 0x007F7F7F;  /* Bright Black */
    color_palette[9] = 0x00FF0000;  /* Bright Red */
    color_palette[10] = 0x0000FF00; /* Bright Green */
    color_palette[11] = 0x00FFFF00; /* Bright Yellow */
    color_palette[12] = 0x005C5CFF; /* Bright Blue */
    color_palette[13] = 0x00FF00FF; /* Bright Magenta */
    color_palette[14] = 0x0000FFFF; /* Bright Cyan */
    color_palette[15] = 0x00FFFFFF; /* Bright White */

    /* 216 color cube (16-231) */
    for (int i = 0; i < 216; i++) {
        int r = (i / 36) * 51;
        int g = ((i / 6) % 6) * 51;
        int b = (i % 6) * 51;
        color_palette[16 + i] = (r << 16) | (g << 8) | b;
    }

    /* Grayscale (232-255) */
    for (int i = 0; i < 24; i++) {
        int gray = 8 + i * 10;
        color_palette[232 + i] = (gray << 16) | (gray << 8) | gray;
    }
}

int fb_open(struct framebuffer *fb, const char *device) {
    fb->fd = open(device, O_RDWR);
    if (fb->fd < 0) {
        perror("Failed to open framebuffer");
        return -1;
    }

    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->vinfo) < 0) {
        perror("Failed to get variable screen info");
        close(fb->fd);
        return -1;
    }

    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->finfo) < 0) {
        perror("Failed to get fixed screen info");
        close(fb->fd);
        return -1;
    }

    fb->width = fb->vinfo.xres;
    fb->height = fb->vinfo.yres;
    fb->bpp = fb->vinfo.bits_per_pixel;
    fb->line_length = fb->finfo.line_length;
    fb->mem_size = fb->finfo.smem_len;

    fb->mem = mmap(NULL, fb->mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
    if (fb->mem == MAP_FAILED) {
        perror("Failed to mmap framebuffer");
        close(fb->fd);
        return -1;
    }

    return 0;
}

void fb_close(struct framebuffer *fb) {
    if (fb->mem) {
        munmap(fb->mem, fb->mem_size);
    }
    if (fb->fd >= 0) {
        close(fb->fd);
    }
}

void fb_put_pixel(struct framebuffer *fb, int x, int y, uint32_t color) {
    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) {
        return;
    }
    size_t offset = y * fb->line_length + x * (fb->bpp / 8);
    if (offset + (fb->bpp / 8) > fb->mem_size) {
        return;
    }
    uint32_t *pixel = (uint32_t *)(fb->mem + offset);
    *pixel = color;
}

void fb_clear(struct framebuffer *fb, uint32_t color) {
    for (int y = 0; y < fb->height; y++) {
        for (int x = 0; x < fb->width; x++) {
            fb_put_pixel(fb, x, y, color);
        }
    }
}

void fb_draw_bitmap(struct framebuffer *fb, int x, int y,
                    unsigned char *bitmap, int width, int height,
                    uint32_t fg_color, uint32_t bg_color) {
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            unsigned char alpha = bitmap[j * width + i];

            /* Skip fully transparent pixels - background already drawn */
            if (alpha == 0) {
                continue;
            }

            if (alpha == 255) {
                /* Fully opaque - use foreground */
                fb_put_pixel(fb, x + i, y + j, fg_color);
            } else {
                /* Alpha blend foreground and background */
                uint8_t fg_r = (fg_color >> 16) & 0xFF;
                uint8_t fg_g = (fg_color >> 8) & 0xFF;
                uint8_t fg_b = fg_color & 0xFF;

                uint8_t bg_r = (bg_color >> 16) & 0xFF;
                uint8_t bg_g = (bg_color >> 8) & 0xFF;
                uint8_t bg_b = bg_color & 0xFF;

                /* Blend using alpha */
                uint8_t r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
                uint8_t g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
                uint8_t b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;

                uint32_t blended = (r << 16) | (g << 8) | b;
                fb_put_pixel(fb, x + i, y + j, blended);
            }
        }
    }
}

int load_font(struct font_entry *font, const char *path, const char *name) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return -1;
    }

    fseek(file, 0, SEEK_END);
    font->buffer_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    font->buffer = malloc(font->buffer_size);
    if (!font->buffer) {
        fclose(file);
        return -1;
    }

    fread(font->buffer, 1, font->buffer_size, file);
    fclose(file);

    int font_offset = stbtt_GetFontOffsetForIndex(font->buffer, 0);
    if (font_offset < 0) {
        free(font->buffer);
        return -1;
    }

    if (!stbtt_InitFont(&font->info, font->buffer, font_offset)) {
        free(font->buffer);
        return -1;
    }

    font->name = name;
    return 0;
}

uint32_t utf8_decode(const unsigned char **p) {
    uint32_t codepoint = 0;
    unsigned char c = **p;

    if (c == 0) {
        return 0;
    }

    if ((c & 0x80) == 0) {
        codepoint = c;
        (*p)++;
    } else if ((c & 0xE0) == 0xC0) {
        codepoint = (c & 0x1F) << 6;
        (*p)++;
        if (**p && (**p & 0xC0) == 0x80) {
            codepoint |= (**p & 0x3F);
            (*p)++;
        }
    } else if ((c & 0xF0) == 0xE0) {
        codepoint = (c & 0x0F) << 12;
        (*p)++;
        if (**p && (**p & 0xC0) == 0x80) {
            codepoint |= ((**p & 0x3F) << 6);
            (*p)++;
            if (**p && (**p & 0xC0) == 0x80) {
                codepoint |= (**p & 0x3F);
                (*p)++;
            }
        }
    } else if ((c & 0xF8) == 0xF0) {
        codepoint = (c & 0x07) << 18;
        (*p)++;
        if (**p && (**p & 0xC0) == 0x80) {
            codepoint |= ((**p & 0x3F) << 12);
            (*p)++;
            if (**p && (**p & 0xC0) == 0x80) {
                codepoint |= ((**p & 0x3F) << 6);
                (*p)++;
                if (**p && (**p & 0xC0) == 0x80) {
                    codepoint |= (**p & 0x3F);
                    (*p)++;
                }
            }
        }
    } else {
        (*p)++;
        return 0xFFFD;
    }

    return codepoint;
}

stbtt_fontinfo* find_font_for_codepoint(struct font_entry *fonts, int num_fonts, uint32_t codepoint) {
    for (int i = 0; i < num_fonts; i++) {
        int glyph_index = stbtt_FindGlyphIndex(&fonts[i].info, codepoint);
        if (glyph_index != 0) {
            return &fonts[i].info;
        }
    }
    return &fonts[0].info;
}

void render_char(struct framebuffer *fb, struct font_entry *fonts, int num_fonts,
                 uint32_t codepoint, int x, int y, float scale, int baseline,
                 uint32_t fg_color, uint32_t bg_color, int char_width, int char_height) {

    /* Clear cell background */
    for (int yy = 0; yy < char_height; yy++) {
        for (int xx = 0; xx < char_width; xx++) {
            fb_put_pixel(fb, x + xx, y + yy, bg_color);
        }
    }

    if (codepoint == 0 || codepoint == ' ') {
        return;
    }

    stbtt_fontinfo *current_font = find_font_for_codepoint(fonts, num_fonts, codepoint);

    int advance, lsb;
    stbtt_GetCodepointHMetrics(current_font, codepoint, &advance, &lsb);

    int c_x1, c_y1, c_x2, c_y2;
    stbtt_GetCodepointBitmapBox(current_font, codepoint, scale, scale, &c_x1, &c_y1, &c_x2, &c_y2);

    int bm_width = c_x2 - c_x1;
    int bm_height = c_y2 - c_y1;

    if (bm_width > 0 && bm_height > 0) {
        unsigned char *bitmap = malloc(bm_width * bm_height);
        if (bitmap) {
            stbtt_MakeCodepointBitmap(current_font, bitmap, bm_width, bm_height,
                                     bm_width, scale, scale, codepoint);

            fb_draw_bitmap(fb, x + c_x1, y + baseline + c_y1,
                          bitmap, bm_width, bm_height, fg_color, bg_color);

            free(bitmap);
        }
    }
}

void term_init(struct terminal *term) {
    memset(term, 0, sizeof(*term));
    term->fg_color = 0x00FFFFFF;
    term->bg_color = 0x00000000;
    term->scroll_bottom = TERM_ROWS - 1;
    term->cursor_visible = 1;
    term->utf8_buf_len = 0;
    term->master_fd = -1;

    for (int y = 0; y < TERM_ROWS; y++) {
        for (int x = 0; x < TERM_COLS; x++) {
            term->cells[y][x].codepoint = ' ';
            term->cells[y][x].fg_color = term->fg_color;
            term->cells[y][x].bg_color = term->bg_color;
        }
    }
}

void term_scroll_up(struct terminal *term) {
    for (int y = term->scroll_top; y < term->scroll_bottom; y++) {
        memcpy(term->cells[y], term->cells[y + 1], sizeof(struct cell) * TERM_COLS);
    }

    /* Clear last line */
    for (int x = 0; x < TERM_COLS; x++) {
        term->cells[term->scroll_bottom][x].codepoint = ' ';
        term->cells[term->scroll_bottom][x].fg_color = term->fg_color;
        term->cells[term->scroll_bottom][x].bg_color = term->bg_color;
    }
}

void term_scroll_down(struct terminal *term) {
    for (int y = term->scroll_bottom; y > term->scroll_top; y--) {
        memcpy(term->cells[y], term->cells[y - 1], sizeof(struct cell) * TERM_COLS);
    }

    /* Clear first line */
    for (int x = 0; x < TERM_COLS; x++) {
        term->cells[term->scroll_top][x].codepoint = ' ';
        term->cells[term->scroll_top][x].fg_color = term->fg_color;
        term->cells[term->scroll_top][x].bg_color = term->bg_color;
    }
}

void term_newline(struct terminal *term) {
    term->cursor_y++;
    if (term->cursor_y > term->scroll_bottom) {
        term->cursor_y = term->scroll_bottom;
        term_scroll_up(term);
    }
}

void term_carriage_return(struct terminal *term) {
    term->cursor_x = 0;
}

void term_putchar(struct terminal *term, uint32_t codepoint) {
    if (term->cursor_x >= TERM_COLS) {
        term_carriage_return(term);
        term_newline(term);
    }

    if (term->cursor_y >= TERM_ROWS) {
        term->cursor_y = TERM_ROWS - 1;
    }

    term->cells[term->cursor_y][term->cursor_x].codepoint = codepoint;
    term->cells[term->cursor_y][term->cursor_x].fg_color = term->fg_color;
    term->cells[term->cursor_y][term->cursor_x].bg_color = term->bg_color;
    term->cells[term->cursor_y][term->cursor_x].bold = term->bold;

    term->cursor_x++;
}

void term_handle_csi(struct terminal *term, char final) {
    int *p = term->escape_params;
    int n = term->num_escape_params;

    switch (final) {
        case 'H': case 'f': /* Cursor Position */
            term->cursor_y = (n > 0 && p[0] > 0) ? p[0] - 1 : 0;
            term->cursor_x = (n > 1 && p[1] > 0) ? p[1] - 1 : 0;
            if (term->cursor_y >= TERM_ROWS) term->cursor_y = TERM_ROWS - 1;
            if (term->cursor_x >= TERM_COLS) term->cursor_x = TERM_COLS - 1;
            break;

        case 'A': /* Cursor Up */
            term->cursor_y -= (n > 0 && p[0] > 0) ? p[0] : 1;
            if (term->cursor_y < 0) term->cursor_y = 0;
            break;

        case 'B': /* Cursor Down */
            term->cursor_y += (n > 0 && p[0] > 0) ? p[0] : 1;
            if (term->cursor_y >= TERM_ROWS) term->cursor_y = TERM_ROWS - 1;
            break;

        case 'C': /* Cursor Forward */
            term->cursor_x += (n > 0 && p[0] > 0) ? p[0] : 1;
            if (term->cursor_x >= TERM_COLS) term->cursor_x = TERM_COLS - 1;
            break;

        case 'D': /* Cursor Backward */
            term->cursor_x -= (n > 0 && p[0] > 0) ? p[0] : 1;
            if (term->cursor_x < 0) term->cursor_x = 0;
            break;

        case 'J': /* Erase Display */
            if (n == 0 || p[0] == 0) {
                /* Clear from cursor to end */
                for (int x = term->cursor_x; x < TERM_COLS; x++) {
                    term->cells[term->cursor_y][x].codepoint = ' ';
                    term->cells[term->cursor_y][x].fg_color = term->fg_color;
                    term->cells[term->cursor_y][x].bg_color = term->bg_color;
                }
                for (int y = term->cursor_y + 1; y < TERM_ROWS; y++) {
                    for (int x = 0; x < TERM_COLS; x++) {
                        term->cells[y][x].codepoint = ' ';
                        term->cells[y][x].fg_color = term->fg_color;
                        term->cells[y][x].bg_color = term->bg_color;
                    }
                }
            } else if (p[0] == 1) {
                /* Clear from beginning to cursor */
                for (int y = 0; y < term->cursor_y; y++) {
                    for (int x = 0; x < TERM_COLS; x++) {
                        term->cells[y][x].codepoint = ' ';
                        term->cells[y][x].fg_color = term->fg_color;
                        term->cells[y][x].bg_color = term->bg_color;
                    }
                }
                for (int x = 0; x <= term->cursor_x; x++) {
                    term->cells[term->cursor_y][x].codepoint = ' ';
                    term->cells[term->cursor_y][x].fg_color = term->fg_color;
                    term->cells[term->cursor_y][x].bg_color = term->bg_color;
                }
            } else if (p[0] == 2 || p[0] == 3) {
                /* Clear entire screen (3 also clears scrollback) */
                for (int y = 0; y < TERM_ROWS; y++) {
                    for (int x = 0; x < TERM_COLS; x++) {
                        term->cells[y][x].codepoint = ' ';
                        term->cells[y][x].fg_color = term->fg_color;
                        term->cells[y][x].bg_color = term->bg_color;
                    }
                }
            }
            break;

        case 'K': /* Erase Line */
            if (n == 0 || p[0] == 0) {
                /* Clear from cursor to end of line */
                for (int x = term->cursor_x; x < TERM_COLS; x++) {
                    term->cells[term->cursor_y][x].codepoint = ' ';
                    term->cells[term->cursor_y][x].fg_color = term->fg_color;
                    term->cells[term->cursor_y][x].bg_color = term->bg_color;
                }
            } else if (p[0] == 1) {
                /* Clear from beginning to cursor */
                for (int x = 0; x <= term->cursor_x; x++) {
                    term->cells[term->cursor_y][x].codepoint = ' ';
                    term->cells[term->cursor_y][x].fg_color = term->fg_color;
                    term->cells[term->cursor_y][x].bg_color = term->bg_color;
                }
            } else if (p[0] == 2) {
                /* Clear entire line */
                for (int x = 0; x < TERM_COLS; x++) {
                    term->cells[term->cursor_y][x].codepoint = ' ';
                    term->cells[term->cursor_y][x].fg_color = term->fg_color;
                    term->cells[term->cursor_y][x].bg_color = term->bg_color;
                }
            }
            break;

        case 'm': /* SGR - Select Graphic Rendition */
            if (n == 0) {
                /* No parameters = reset */
                term->fg_color = 0x00FFFFFF;
                term->bg_color = 0x00000000;
                term->bold = 0;
            }
            for (int i = 0; i < n; i++) {
                if (p[i] == 0) {
                    /* Reset */
                    term->fg_color = 0x00FFFFFF;
                    term->bg_color = 0x00000000;
                    term->bold = 0;
                } else if (p[i] == 1) {
                    term->bold = 1;
                } else if (p[i] == 22) {
                    term->bold = 0;
                } else if (p[i] >= 30 && p[i] <= 37) {
                    /* Foreground color */
                    term->fg_color = color_palette[p[i] - 30];
                } else if (p[i] == 39) {
                    /* Default foreground */
                    term->fg_color = 0x00FFFFFF;
                } else if (p[i] >= 40 && p[i] <= 47) {
                    /* Background color */
                    term->bg_color = color_palette[p[i] - 40];
                } else if (p[i] == 49) {
                    /* Default background */
                    term->bg_color = 0x00000000;
                } else if (p[i] >= 90 && p[i] <= 97) {
                    /* Bright foreground color */
                    term->fg_color = color_palette[p[i] - 90 + 8];
                } else if (p[i] >= 100 && p[i] <= 107) {
                    /* Bright background color */
                    term->bg_color = color_palette[p[i] - 100 + 8];
                }
            }
            break;

        case 'h': /* Set Mode */
            if (term->private_mode) {
                /* DEC Private Mode Set */
                for (int i = 0; i < n; i++) {
                    if (p[i] == 25) {
                        /* Show cursor */
                        term->cursor_visible = 1;
                    } else if (p[i] == 1049 || p[i] == 47 || p[i] == 1047) {
                        /* Alternate screen buffer - we don't implement this, just ignore */
                    }
                    /* Ignore other modes */
                }
            }
            break;

        case 'l': /* Reset Mode */
            if (term->private_mode) {
                /* DEC Private Mode Reset */
                for (int i = 0; i < n; i++) {
                    if (p[i] == 25) {
                        /* Hide cursor */
                        term->cursor_visible = 0;
                    } else if (p[i] == 1049 || p[i] == 47 || p[i] == 1047) {
                        /* Exit alternate screen buffer - we don't implement this, just ignore */
                    }
                    /* Ignore other modes */
                }
            }
            break;

        case 'r': /* Set scrolling region */
            term->scroll_top = (n > 0 && p[0] > 0) ? p[0] - 1 : 0;
            term->scroll_bottom = (n > 1 && p[1] > 0) ? p[1] - 1 : TERM_ROWS - 1;
            if (term->scroll_top >= TERM_ROWS) term->scroll_top = 0;
            if (term->scroll_bottom >= TERM_ROWS) term->scroll_bottom = TERM_ROWS - 1;
            break;

        case 'd': /* Line Position Absolute */
            term->cursor_y = (n > 0 && p[0] > 0) ? p[0] - 1 : 0;
            if (term->cursor_y >= TERM_ROWS) term->cursor_y = TERM_ROWS - 1;
            break;

        case 'G': /* Cursor Character Absolute */
            term->cursor_x = (n > 0 && p[0] > 0) ? p[0] - 1 : 0;
            if (term->cursor_x >= TERM_COLS) term->cursor_x = TERM_COLS - 1;
            break;

        case 'S': /* Scroll Up */
            for (int i = 0; i < ((n > 0 && p[0] > 0) ? p[0] : 1); i++) {
                term_scroll_up(term);
            }
            break;

        case 'T': /* Scroll Down */
            for (int i = 0; i < ((n > 0 && p[0] > 0) ? p[0] : 1); i++) {
                term_scroll_down(term);
            }
            break;

        case 'L': /* Insert Line */
            /* Insert blank line at cursor, shift down */
            for (int i = 0; i < ((n > 0 && p[0] > 0) ? p[0] : 1); i++) {
                for (int y = term->scroll_bottom; y > term->cursor_y; y--) {
                    memcpy(term->cells[y], term->cells[y - 1], sizeof(struct cell) * TERM_COLS);
                }
                for (int x = 0; x < TERM_COLS; x++) {
                    term->cells[term->cursor_y][x].codepoint = ' ';
                    term->cells[term->cursor_y][x].fg_color = term->fg_color;
                    term->cells[term->cursor_y][x].bg_color = term->bg_color;
                }
            }
            break;

        case 'M': /* Delete Line */
            /* Delete line at cursor, shift up */
            for (int i = 0; i < ((n > 0 && p[0] > 0) ? p[0] : 1); i++) {
                for (int y = term->cursor_y; y < term->scroll_bottom; y++) {
                    memcpy(term->cells[y], term->cells[y + 1], sizeof(struct cell) * TERM_COLS);
                }
                for (int x = 0; x < TERM_COLS; x++) {
                    term->cells[term->scroll_bottom][x].codepoint = ' ';
                    term->cells[term->scroll_bottom][x].fg_color = term->fg_color;
                    term->cells[term->scroll_bottom][x].bg_color = term->bg_color;
                }
            }
            break;

        case 'X': /* Erase Characters */
            {
                int count = (n > 0 && p[0] > 0) ? p[0] : 1;
                for (int i = 0; i < count && term->cursor_x + i < TERM_COLS; i++) {
                    term->cells[term->cursor_y][term->cursor_x + i].codepoint = ' ';
                    term->cells[term->cursor_y][term->cursor_x + i].fg_color = term->fg_color;
                    term->cells[term->cursor_y][term->cursor_x + i].bg_color = term->bg_color;
                }
            }
            break;

        case 'P': /* Delete Characters */
            {
                int count = (n > 0 && p[0] > 0) ? p[0] : 1;
                for (int x = term->cursor_x; x < TERM_COLS - count; x++) {
                    term->cells[term->cursor_y][x] = term->cells[term->cursor_y][x + count];
                }
                for (int x = TERM_COLS - count; x < TERM_COLS; x++) {
                    term->cells[term->cursor_y][x].codepoint = ' ';
                    term->cells[term->cursor_y][x].fg_color = term->fg_color;
                    term->cells[term->cursor_y][x].bg_color = term->bg_color;
                }
            }
            break;

        case '@': /* Insert Characters */
            {
                int count = (n > 0 && p[0] > 0) ? p[0] : 1;
                for (int x = TERM_COLS - 1; x >= term->cursor_x + count; x--) {
                    term->cells[term->cursor_y][x] = term->cells[term->cursor_y][x - count];
                }
                for (int x = term->cursor_x; x < term->cursor_x + count && x < TERM_COLS; x++) {
                    term->cells[term->cursor_y][x].codepoint = ' ';
                    term->cells[term->cursor_y][x].fg_color = term->fg_color;
                    term->cells[term->cursor_y][x].bg_color = term->bg_color;
                }
            }
            break;

        case 'n': /* Device Status Report */
            if (n > 0 && p[0] == 6) {
                /* CPR - Cursor Position Report */
                /* Terminal responds with ESC [ row ; col R */
                char response[32];
                int len = snprintf(response, sizeof(response), "\x1b[%d;%dR",
                                 term->cursor_y + 1, term->cursor_x + 1);
                if (term->master_fd >= 0) {
                    write(term->master_fd, response, len);
                }
            } else if (n > 0 && p[0] == 5) {
                /* Status Report - respond that we're OK */
                const char *response = "\x1b[0n";
                if (term->master_fd >= 0) {
                    write(term->master_fd, response, 4);
                }
            }
            break;

        case 'c': /* Device Attributes (DA) */
            /* Respond as VT100 */
            if (term->master_fd >= 0) {
                const char *response = "\x1b[?1;2c";
                write(term->master_fd, response, 7);
            }
            break;

        default:
            /* Unhandled CSI sequence - ignore */
            break;
    }
}

void term_process_char(struct terminal *term, unsigned char ch) {
    switch (term->state) {
        case STATE_NORMAL:
            if (ch == '\033') {
                term->state = STATE_ESC;
                term->escape_buf_len = 0;
                term->utf8_buf_len = 0;  /* Reset UTF-8 state */
            } else if (ch == '\n') {
                term_newline(term);
                term->utf8_buf_len = 0;
            } else if (ch == '\r') {
                term_carriage_return(term);
                term->utf8_buf_len = 0;
            } else if (ch == '\b') {
                if (term->cursor_x > 0) term->cursor_x--;
                term->utf8_buf_len = 0;
            } else if (ch == '\t') {
                term->cursor_x = (term->cursor_x + 8) & ~7;
                if (term->cursor_x >= TERM_COLS) {
                    term->cursor_x = 0;
                    term_newline(term);
                }
                term->utf8_buf_len = 0;
            } else if (ch >= 32) {
                /* UTF-8 sequence decoding */
                term->utf8_buf[term->utf8_buf_len++] = ch;

                /* Check if we have a complete UTF-8 sequence */
                int expected_len = 1;
                if ((term->utf8_buf[0] & 0x80) == 0) {
                    expected_len = 1;
                } else if ((term->utf8_buf[0] & 0xE0) == 0xC0) {
                    expected_len = 2;
                } else if ((term->utf8_buf[0] & 0xF0) == 0xE0) {
                    expected_len = 3;
                } else if ((term->utf8_buf[0] & 0xF8) == 0xF0) {
                    expected_len = 4;
                }

                if (term->utf8_buf_len >= expected_len) {
                    /* Decode the complete UTF-8 sequence */
                    const unsigned char *p = term->utf8_buf;
                    uint32_t codepoint = utf8_decode(&p);
                    term_putchar(term, codepoint);
                    term->utf8_buf_len = 0;
                }
            }
            /* Ignore other control characters (0-31) */
            break;

        case STATE_ESC:
            if (ch == '[') {
                term->state = STATE_CSI;
                term->num_escape_params = 0;
                term->private_mode = 0;
                memset(term->escape_params, 0, sizeof(term->escape_params));
                term->escape_buf_len = 0;
            } else if (ch == ']') {
                term->state = STATE_OSC;
                term->escape_buf_len = 0;
            } else if (ch == '(') {
                /* Character set selection - ignore next char */
                term->state = STATE_NORMAL;
            } else {
                /* Unknown escape - return to normal */
                term->state = STATE_NORMAL;
            }
            break;

        case STATE_CSI:
            if (ch >= '0' && ch <= '9') {
                if (term->num_escape_params == 0) {
                    term->num_escape_params = 1;
                }
                term->escape_params[term->num_escape_params - 1] =
                    term->escape_params[term->num_escape_params - 1] * 10 + (ch - '0');
            } else if (ch == ';') {
                if (term->num_escape_params < MAX_ESCAPE_PARAMS) {
                    term->num_escape_params++;
                }
            } else if (ch == '?') {
                /* Mark as private mode sequence */
                term->private_mode = 1;
            } else if (ch >= '@' && ch <= '~') {
                term_handle_csi(term, ch);
                term->state = STATE_NORMAL;
                term->private_mode = 0;
            } else if (ch >= 0x20 && ch <= 0x2F) {
                /* Intermediate characters - ignore for now */
            } else {
                /* Invalid sequence - reset */
                term->state = STATE_NORMAL;
                term->private_mode = 0;
            }
            break;

        case STATE_OSC:
            if (ch == '\007' || ch == '\033') {
                term->state = STATE_NORMAL;
            }
            break;
    }
}

void term_render(struct framebuffer *fb, struct terminal *term,
                 struct font_entry *fonts, int num_fonts,
                 float scale, int baseline, int char_width, int char_height) {

    for (int y = 0; y < TERM_ROWS; y++) {
        for (int x = 0; x < TERM_COLS; x++) {
            struct cell *cell = &term->cells[y][x];

            int px = x * char_width;
            int py = y * char_height;

            render_char(fb, fonts, num_fonts, cell->codepoint, px, py,
                       scale, baseline, cell->fg_color, cell->bg_color,
                       char_width, char_height);
        }
    }
}

/* Find and open keyboard device */
int open_keyboard(void) {
    DIR *dir = opendir("/dev/input");
    if (!dir) {
        perror("opendir /dev/input");
        return -1;
    }

    struct dirent *entry;
    int kbd_fd = -1;

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;

        char path[512];
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        /* Check if this is a keyboard device */
        unsigned long evbit = 0;
        ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), &evbit);

        if (evbit & (1 << EV_KEY)) {
            /* Check for common keyboard keys */
            unsigned long keybit[KEY_MAX/8 + 1] = {0};
            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit);

            /* Check if it has letter keys (likely a keyboard) */
            int has_letters = 0;
            for (int i = KEY_Q; i <= KEY_P; i++) {
                if (keybit[i/8] & (1 << (i % 8))) {
                    has_letters = 1;
                    break;
                }
            }

            if (has_letters) {
                kbd_fd = fd;
                fprintf(stderr, "Using keyboard: %s\n", path);
                break;
            }
        }

        close(fd);
    }

    closedir(dir);
    return kbd_fd;
}

int spawn_shell(int *master_fd, int cols, int rows) {
    struct winsize ws = {
        .ws_row = rows,
        .ws_col = cols,
        .ws_xpixel = 0,
        .ws_ypixel = 0
    };

    /*
     * IMPORTANT: Pass NULL for termios parameter!
     * The PTY slave should use kernel defaults (ICANON | ECHO enabled).
     * Applications (bash, ncurses) will set their own termios as needed.
     * If we force termios here, we prevent TUI apps from switching to raw mode.
     */
    pid_t pid = forkpty(master_fd, NULL, NULL, &ws);

    if (pid < 0) {
        perror("forkpty");
        return -1;
    }

    if (pid == 0) {
        /* Child process - exec shell */
        /* Set TERM environment variable so applications know what escape sequences to use */
        setenv("TERM", "xterm-256color", 1);

        char *shell = getenv("SHELL");
        if (!shell) shell = "/bin/bash";

        char *args[] = {shell, NULL};
        execvp(shell, args);
        perror("execvp");
        exit(1);
    }

    /* Set master_fd to non-blocking for efficient reading */
    int flags = fcntl(*master_fd, F_GETFL, 0);
    fcntl(*master_fd, F_SETFL, flags | O_NONBLOCK);

    return pid;
}

volatile sig_atomic_t running = 1;

void sigchld_handler(int sig) {
    (void)sig;
    running = 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <font.ttf> [font_size]\n", argv[0]);
        fprintf(stderr, "  font.ttf  - Path to TrueType font file\n");
        fprintf(stderr, "  font_size - Optional font size in pixels (default: auto-calculated)\n");
        return 1;
    }

    init_color_palette();
    kb_init();

    const char *font_path = argv[1];
    float user_font_size = 0.0f;  /* 0 means auto-calculate */

    /* Check if font size was specified */
    if (argc >= 3) {
        user_font_size = atof(argv[2]);
        if (user_font_size < 6.0f || user_font_size > 72.0f) {
            fprintf(stderr, "Font size must be between 6 and 72\n");
            return 1;
        }
    }

    struct framebuffer fb = {0};
    if (fb_open(&fb, "/dev/fb0") < 0) {
        return 1;
    }

    /* Load fonts */
    struct font_entry fonts[MAX_FONTS];
    int num_fonts = 0;

    if (load_font(&fonts[num_fonts], font_path, "Primary") == 0) {
        num_fonts++;
    } else {
        fprintf(stderr, "Failed to load primary font\n");
        fb_close(&fb);
        return 1;
    }

    if (num_fonts < MAX_FONTS) {
        if (load_font(&fonts[num_fonts], "/usr/share/fonts/noto/NotoSansArabic-Regular.ttf", "Arabic") == 0) {
            num_fonts++;
        }
    }
    if (num_fonts < MAX_FONTS) {
        if (load_font(&fonts[num_fonts], "/usr/share/fonts/noto/NotoSansHebrew-Regular.ttf", "Hebrew") == 0) {
            num_fonts++;
        }
    }
    if (num_fonts < MAX_FONTS) {
        if (load_font(&fonts[num_fonts], "/usr/share/fonts/noto/NotoSansThai-Regular.ttf", "Thai") == 0) {
            num_fonts++;
        }
    }

    /* Determine font size */
    float font_size_px = (user_font_size > 0.0f) ? user_font_size : 16.0f;

    /* Calculate initial font metrics */
    float scale = stbtt_ScaleForPixelHeight(&fonts[0].info, font_size_px);

    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&fonts[0].info, &ascent, &descent, &line_gap);
    int baseline = (int)(ascent * scale);

    /* Calculate character cell dimensions */
    int char_height = (int)((ascent - descent) * scale) + 2;

    /* Find maximum advance width across all printable ASCII characters */
    int max_advance = 0;
    for (int c = 32; c <= 126; c++) {  /* Space to tilde */
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&fonts[0].info, c, &adv, &lsb);
        if (adv > max_advance) max_advance = adv;
    }

    int char_width = (int)(max_advance * scale) + 1;  /* Max width + 1px spacing */

    /* Calculate terminal dimensions based on screen and character size */
    TERM_COLS = (fb.width - 4) / char_width;   /* Leave 4px margin */
    TERM_ROWS = (fb.height - 4) / char_height;

    /* Clamp to reasonable limits */
    if (TERM_COLS < 40) TERM_COLS = 40;
    if (TERM_ROWS < 10) TERM_ROWS = 10;
    if (TERM_COLS > MAX_TERM_COLS) TERM_COLS = MAX_TERM_COLS;
    if (TERM_ROWS > MAX_TERM_ROWS) TERM_ROWS = MAX_TERM_ROWS;

    fprintf(stderr, "Terminal size: %dx%d (char %dx%d, screen %dx%d)\n",
            TERM_COLS, TERM_ROWS, char_width, char_height, fb.width, fb.height);

    fb_clear(&fb, 0x00000000);

    /* Initialize terminal */
    struct terminal term;
    term_init(&term);

    /* Set up signal handler */
    signal(SIGCHLD, sigchld_handler);

    /* Spawn shell */
    int master_fd;
    pid_t shell_pid = spawn_shell(&master_fd, TERM_COLS, TERM_ROWS);
    if (shell_pid < 0) {
        fb_close(&fb);
        return 1;
    }

    /* Set terminal's master_fd so it can respond to queries */
    term.master_fd = master_fd;

    /* Hide the VT console cursor */
    printf("\033[?25l");  /* Hide cursor */
    fflush(stdout);

    /* Set stdin to raw mode to capture all keyboard input */
    struct termios old_term, raw_term;
    tcgetattr(STDIN_FILENO, &old_term);
    raw_term = old_term;
    cfmakeraw(&raw_term);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw_term);
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

    unsigned char buf[4096];
    int needs_render = 1;  /* Flag to track if screen needs redrawing */

    while (running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(master_fd, &fds);

        struct timeval tv = {0, 16666}; /* ~60fps */

        int max_fd = (master_fd > STDIN_FILENO) ? master_fd : STDIN_FILENO;
        int ret = select(max_fd + 1, &fds, NULL, NULL, &tv);

        if (ret > 0) {
            /* Input from stdin */
            if (FD_ISSET(STDIN_FILENO, &fds)) {
                ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
                if (n > 0) {
                    /* Check for Ctrl+Q to quit */
                    for (ssize_t i = 0; i < n; i++) {
                        if (buf[i] == 0x11) {  /* Ctrl+Q */
                            running = 0;
                            break;
                        }
                    }

                    if (running) {
                        /* Pass everything directly to PTY */
                        write(master_fd, buf, n);
                    }
                }
            }

            /* Output from shell */
            if (FD_ISSET(master_fd, &fds)) {
                /* Read all available data in a loop for better batching */
                while (1) {
                    ssize_t n = read(master_fd, buf, sizeof(buf));
                    if (n > 0) {
                        for (ssize_t i = 0; i < n; i++) {
                            term_process_char(&term, buf[i]);
                        }
                        needs_render = 1;  /* Mark that we need to redraw */

                        /* Continue reading if buffer was full (more data likely available) */
                        if (n < (ssize_t)sizeof(buf)) {
                            break;  /* No more data available */
                        }
                    } else if (n == 0) {
                        running = 0;  /* Shell closed */
                        break;
                    } else {
                        /* EAGAIN or EWOULDBLOCK - no more data */
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        /* Other error */
                        break;
                    }
                }
            }
        }

        /* Render terminal only when something changed */
        if (needs_render) {
            term_render(&fb, &term, fonts, num_fonts, scale, baseline, char_width, char_height);
            needs_render = 0;
        }
    }

    /* Restore terminal settings */
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);

    /* Show the VT console cursor again */
    printf("\033[?25h");  /* Show cursor */
    fflush(stdout);

    fb_clear(&fb, 0x00000000);

    for (int i = 0; i < num_fonts; i++) {
        if (fonts[i].buffer) {
            free(fonts[i].buffer);
        }
    }
    fb_close(&fb);

    close(master_fd);

    return 0;
}
