# Zucc AKA Tux2-Internarchinstall 🐧🌎

```shell
    # gcc -o out/fb_term fb_term.c -lm -lutil
    # ./out/fb_term /path/to/font.ttf [font_size]
```
> This sets a base-font but fallsback to see bellow. It opens a terminal using a PTY. 

Example: You can already try/test in a TTY `sudo ./out/fb_term /path/to/font.ttf [font_size]`
Then cat `chars.txt`

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
