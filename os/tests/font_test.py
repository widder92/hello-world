#!/usr/bin/env python3
"""Prüft den Schriftrenderer von HelloOS gegen FreeType (über Pillow).

Verglichen wird in großer Schriftgröße: Dort spielt das Hinting, das FreeType
anwendet und HelloOS nicht, keine Rolle mehr, und jeder Fehler beim Lesen der
Umrisse, bei zusammengesetzten Zeichen oder beim Füllen fällt auf.

Aufruf: font_test.py <pfad zum übersetzten font_test> <schriftdatei>...
"""
import subprocess, sys

from PIL import Image, ImageChops, ImageDraw, ImageFont

TOOL = sys.argv[1]
FONTS = sys.argv[2:]
SIZE = 160
checks = failures = 0


def check(ok, what):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print("FEHLER:", what)


def ours(font, size, oblique, codepoints):
    """Zeichen von HelloOS rastern lassen: {cp: (umriss, vorschub, b, h, links, oben, werte)}"""
    args = [TOOL, "render", font, str(size), "1" if oblique else "0"] + ["%X" % cp for cp in codepoints]
    lines = subprocess.run(args, capture_output=True, text=True, check=True).stdout.split("\n")
    result = {}
    for i in range(0, len(lines) - 1, 2):
        head = lines[i].split()
        if len(head) < 7:
            continue
        cp = int(head[0][2:], 16)
        glyph, advance, width, height, left, top = map(int, head[1:7])
        result[cp] = (glyph, advance, width, height, left, top, bytes.fromhex(lines[i + 1]))
    return result


def compare(font_path):
    reference = ImageFont.truetype(font_path, SIZE)
    # Latein mit Umlauten und Akzenten, Satzzeichen, Griechisch, Kyrillisch, Symbole
    codepoints = list(range(0x21, 0x7F)) + list(range(0xA1, 0x180)) + \
        list(range(0x391, 0x3CA)) + list(range(0x410, 0x450)) + \
        [0x2013, 0x2014, 0x2018, 0x201C, 0x201E, 0x2022, 0x2026, 0x20AC, 0x2122, 0x2190, 0x2192, 0x2713]
    codepoints = [cp for cp in codepoints if cp not in (0xAD, 0x3A2)]
    rendered = ours(font_path, SIZE, False, codepoints)
    worst = 0.0
    for cp in codepoints:
        name = "%s U+%04X" % (font_path.split("/")[-1], cp)
        if cp not in rendered:
            check(False, name + ": nicht gerastert")
            continue
        glyph, advance, width, height, left, top, values = rendered[cp]
        check(glyph != 0, name + ": kein Umriss gefunden")

        # Referenz auf eine Fläche zeichnen, in der die Grundlinie bei (60, 200) liegt
        image = Image.new("L", (320, 280), 0)
        ImageDraw.Draw(image).text((60, 200), chr(cp), fill=255, font=reference, anchor="ls")
        mine = Image.new("L", (320, 280), 0)
        if width and height:
            mine.paste(Image.frombytes("L", (width, height), values), (60 + left, 200 + top))

        # FreeType rückt Umrisse beim Hinting um bis zu einen Bildpunkt zurecht;
        # deshalb zählt die beste Übereinstimmung bei kleiner Verschiebung.
        a = image.tobytes()
        ink = sum(a) / 255.0
        ratio = 1.0
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                b = ImageChops.offset(mine, dx, dy).tobytes()
                difference = sum(abs(x - y) for x, y in zip(a, b)) / 255.0
                # im Verhältnis zur gefüllten Fläche: Kantenunterschiede sind normal,
                # ein fehlender oder verformter Teil fällt sofort auf
                ratio = min(ratio, difference / max(ink, 200.0))
        worst = max(worst, ratio)
        check(ratio < 0.14, name + ": weicht zu %.1f %% von der Referenz ab" % (100 * ratio))

        expected = reference.getlength(chr(cp))
        check(abs(advance - expected) <= 1.5, name + ": Vorschub %d statt %.1f" % (advance, expected))
    return worst


def small_sizes(font_path):
    """In Lesegrößen: Zeichen bleiben im Rahmen der Zeile und haben plausible Schwärzung."""
    for size in (11, 13, 15, 18, 24, 32):
        rendered = ours(font_path, size, False, [ord(c) for c in "Hgjxäß§"])
        for cp, (glyph, advance, width, height, left, top, values) in rendered.items():
            name = "%s U+%04X bei %d" % (font_path.split("/")[-1], cp, size)
            check(0 < advance <= size * 1.2, name + ": Vorschub %d" % advance)
            check(top >= -size * 1.1 and top + height <= size * 0.5, name + ": ragt aus der Zeile")
            check(max(values) > 150, name + ": zu blass")
    # Kursiv: oben nach rechts geneigt, unten nicht verschoben
    upright = ours(font_path, 100, False, [ord("l")])[ord("l")]
    slanted = ours(font_path, 100, True, [ord("l")])[ord("l")]
    check(slanted[2] > upright[2] + 8, "Kursiv: das Zeichen wird breiter (geneigt)")
    check(abs(slanted[4] - upright[4]) <= 2, "Kursiv: der Fuß bleibt an seinem Platz")


for font in FONTS:
    worst = compare(font)
    small_sizes(font)
    result = subprocess.run([TOOL, "fuzz", font, "300"], capture_output=True, text=True)
    check(result.returncode == 0, "beschädigte Schriftdateien: Absturz oder Speicherfehler\n" + result.stderr[-1500:])
print("%d Prüfungen am Schriftrenderer, %d Fehler" % (checks, failures))
sys.exit(1 if failures else 0)
