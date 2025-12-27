# Zucc

```shell
    # gcc -o fb_term fb_term.c -lm -lutil
    # ./fb_term /path/to/font.ttf [font_size]
```

---

`noto-fonts` For Latin, Greek, Cyrillic
`noto-fonts-cjk` For Chinese, Japanese, Korean

1. Primary font - the one you specify on the command line (Usually also a Noto-something)
2. Arabic fallback - /usr/share/fonts/noto/NotoSansArabic-Regular.ttf
3. Hebrew fallback - /usr/share/fonts/noto/NotoSansHebrew-Regular.ttf
4. Thai fallback - /usr/share/fonts/noto/NotoSansThai-Regular.ttf

`local/noto-fonts-extra` provides 2-4. 

Attempt was to cover 99% of cases (with 0 depends).