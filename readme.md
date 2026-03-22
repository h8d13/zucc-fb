# Zucc AKA Tux2-Internarchinstall 🐧🌎

```shell
    # gcc -o out/fb_term fb_term.c -lm -lutil
    # ./out/fb_term /path/to/font.ttf [font_size]
```
> This sets a base-font but fallsback to see bellow. It opens a terminal using a PTY.

Example: You can already try/test in a TTY `sudo ./out/fb_term /path/to/font.ttf [font_size]`
Then cat `chars.txt`

---

## Running inside a regular terminal (Konsole, Ghostty, etc.)

No framebuffer needed. If `/dev/fb0` is not accessible, the emulator automatically falls back to ANSI output mode and runs inside your existing terminal.

```shell
    # Auto-detect (uses ANSI mode when no framebuffer is available)
    ./out/fb_term

    # Force ANSI terminal mode explicitly (no font required)
    ./out/fb_term --term

    # With a font (font is only used in framebuffer mode)
    ./out/fb_term --term /path/to/font.ttf
```

In ANSI mode:
- **No font files needed** — the parent terminal (Konsole, Ghostty, etc.) handles all font rendering using its own configured font. We just send UTF-8 characters and ANSI color codes.
- Dimensions are read from the parent terminal via `TIOCGWINSZ` and update on resize (SIGWINCH)
- Truecolor (24-bit RGB) is used for all colors
- The cursor is shown as a reverse-video block (always high-contrast regardless of theme)
- The alternate screen buffer is used so your terminal is fully restored on exit

---

Use generic then fallback:

`noto-fonts` For Latin, Greek, Cyrillic

`noto-fonts-cjk` For Chinese, Japanese, Korean

`noto-fonts-extra` For Arabic, Hebrew, Thai, and other scripts

Attempt was to cover 99% of cases 0 dependencies and 20mb of fonts added.

To check your available categories: `ls -la /usr/share/fonts/` 

Building: `xtraktor.sh` just bundles the fonts.
Then `./iso_mod` creates the ISO. Will prompt for `sudo` at `archiso`.

---

Testing: Mostly tested in Ghostty and an actual TTY.
