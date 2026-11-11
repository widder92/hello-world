#!/usr/bin/env python3
"""Erzeugt kernel/font_data.c: Bitmap-Schriften (8x16, 8 Bit Deckkraft je Pixel)
für die Zeichen U+0000 bis U+00FF aus DejaVu Sans Mono.

Die erzeugte Datei liegt im Repository; dieses Skript braucht man nur, wenn
man die Schrift ändern möchte (benötigt Pillow und die DejaVu-Schriften).
"""
import sys
from PIL import Image, ImageDraw, ImageFont

CELL_W, CELL_H = 8, 16
SIZE = 13
BASELINE = 12
FONT_DIR = "/usr/share/fonts/truetype/dejavu/"
FONTS = [("font_regular", "DejaVuSansMono.ttf"),
         ("font_bold", "DejaVuSansMono-Bold.ttf")]

NOTICE = """\
/*
 * Automatisch erzeugt von tools/mkfont.py - nicht von Hand bearbeiten.
 *
 * Gerasterte Zeichen (8x16 Pixel, 8 Bit Deckkraft) aus DejaVu Sans Mono.
 *
 * Fonts are (c) Bitstream (see below). DejaVu changes are in public domain.
 *
 * Copyright (c) 2003 by Bitstream, Inc. All Rights Reserved. Bitstream Vera
 * is a trademark of Bitstream, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of the fonts accompanying this license ("Fonts") and associated
 * documentation files (the "Font Software"), to reproduce and distribute the
 * Font Software, including without limitation the rights to use, copy, merge,
 * publish, distribute, and/or sell copies of the Font Software, and to permit
 * persons to whom the Font Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright and trademark notices and this permission notice shall
 * be included in all copies of one or more of the Font Software typefaces.
 *
 * The Font Software may be modified, altered, or added to, and in particular
 * the designs of glyphs or characters in the Fonts may be modified and
 * additional glyphs or characters may be added to the Fonts, only if the
 * fonts are renamed to names not containing either the words "Bitstream" or
 * the word "Vera".
 *
 * This License becomes null and void to the extent applicable to Fonts or
 * Font Software that has been modified and is distributed under the
 * "Bitstream Vera" names.
 *
 * The Font Software may be sold as part of a larger software package but no
 * copy of one or more of the Font Software typefaces may be sold by itself.
 *
 * THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * COPYRIGHT, PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL BITSTREAM
 * OR THE GNOME FOUNDATION BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR
 * CONSEQUENTIAL DAMAGES, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM
 * OTHER DEALINGS IN THE FONT SOFTWARE.
 */
"""


def render(path):
    font = ImageFont.truetype(path, SIZE)
    glyphs = []
    for code in range(256):
        img = Image.new("L", (CELL_W, CELL_H), 0)
        printable = 0x20 < code < 0x7F or code > 0xA0
        if printable:
            ImageDraw.Draw(img).text((0, BASELINE), chr(code), font=font,
                                     fill=255, anchor="ls")
        glyphs.append(img.tobytes())
    return glyphs


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "kernel/font_data.c"
    with open(out, "w") as f:
        f.write(NOTICE)
        f.write('#include "gfx.h"\n')
        for name, filename in FONTS:
            glyphs = render(FONT_DIR + filename)
            f.write("\nconst uint8_t %s[256][FONT_W * FONT_H] = {\n" % name)
            for code, data in enumerate(glyphs):
                if not any(data):
                    continue
                f.write("    [0x%02X] = {" % code)
                for i, b in enumerate(data):
                    if i % CELL_W == 0:
                        f.write("\n        ")
                    f.write("%3d," % b)
                f.write("\n    },\n")
            f.write("};\n")


if __name__ == "__main__":
    main()
