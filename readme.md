# Zucc

```shell
    # gcc -o out/fb_term fb_term.c -lm -lutil
    # ./fb_term /path/to/font.ttf [font_size]
```
> This sets a base-font but fallsback to see bellow. It opens a terminal using a PTY. 

Example `cat chars.example`

---

`noto-fonts` For Latin, Greek, Cyrillic
`noto-fonts-cjk` For Chinese, Japanese, Korean
`noto-fonts-extra` For Arabic, Hebrew, Thai, and other scripts

1. Primary font - the one you specify on the command line (Usually also a Noto-something)
2. Arabic fallback - /usr/share/fonts/noto/NotoSansArabic-Regular.ttf
3. Hebrew fallback - /usr/share/fonts/noto/NotoSansHebrew-Regular.ttf
4. Thai fallback - /usr/share/fonts/noto/NotoSansThai-Regular.ttf
5. CJK fallback - /usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc

Attempt was to cover 99% of cases.

To check your available categories: `ls -la /usr/share/fonts/` 