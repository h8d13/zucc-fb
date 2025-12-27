#!/bin/sh
has_libs=$(pacman -Q | grep -i "noto")
# noto-fonts 1:2025.12.01-2
# noto-fonts-cjk 20240730-1
# noto-fonts-extra 1:2025.12.01-2
echo "$has_libs" | grep -q "noto-fonts" || { echo "Missing required noto packages"; exit 1; }
echo "$has_libs" | grep -q "noto-fonts-cjk" && echo "$has_libs" | grep -q "noto-fonts-extra"  || { echo "Missing required noto packages"; exit 1; }

f_path = "usr/share/fonts/noto"

tar -czf in/noto-fonts-bundle.tar.gz -C / \
    "$f_path/NotoSans-Regular.ttf" \
    "$f_path/NotoSansArabic-Regular.ttf" \
    "$f_path/NotoSansHebrew-Regular.ttf" \
    "$f_path/NotoSansThai-Regular.ttf" \
    "$f_path-cjk/NotoSansCJK-Regular.ttc"

echo "Created noto-fonts-bundle.tar.gz"