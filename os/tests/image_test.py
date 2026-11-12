#!/usr/bin/env python3
"""Prüft die Bild-Dekoder von HelloOS gegen eine unabhängige Referenz.

PNG: Testbilder aller Farbarten, Bittiefen und Filter, auch im
Zeilensprungverfahren, erzeugt dieses Skript selbst - samt dem erwarteten
Ergebnis. JPEG und GIF erzeugt und entschlüsselt die Bibliothek Pillow zum
Vergleich. Am Ende werden Dateien beschädigt; abstürzen darf dabei nichts.

Aufruf: image_test.py <pfad zum übersetzten image_test>
"""
import io, os, random, struct, subprocess, sys, tempfile, zlib

from PIL import Image

TOOL = sys.argv[1]
TMP = tempfile.mkdtemp(prefix="helloos-bilder-")
checks = failures = 0


def check(ok, what):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print("FEHLER:", what)


def decode(data, name="bild"):
    """Lässt HelloOS das Bild entschlüsseln: (breite, höhe, alpha, rgba) oder None."""
    path = os.path.join(TMP, name)
    out = path + ".rgba"
    with open(path, "wb") as f:
        f.write(data)
    result = subprocess.run([TOOL, "decode", path, out], capture_output=True, text=True)
    if result.returncode != 0:
        return None
    width, height, alpha = map(int, result.stdout.split())
    with open(out, "rb") as f:
        return width, height, alpha, f.read()


# --- PNG: eigener Schreiber -------------------------------------------------------

CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))


def pack_row(values, depth):
    """Werte einer Zeile (je Kanal eine Zahl) in Bytes packen."""
    if depth == 8:
        return bytes(values)
    if depth == 16:
        return b"".join(struct.pack(">H", v) for v in values)
    out, acc, bits = bytearray(), 0, 0
    for v in values:
        acc = (acc << depth) | v
        bits += depth
        if bits == 8:
            out.append(acc)
            acc = bits = 0
    if bits:
        out.append(acc << (8 - bits))
    return bytes(out)


def filter_row(kind, row, previous, bpp):
    out = bytearray()
    for i, value in enumerate(row):
        left = row[i - bpp] if i >= bpp else 0
        up = previous[i] if previous else 0
        corner = previous[i - bpp] if previous and i >= bpp else 0
        if kind == 0:
            predicted = 0
        elif kind == 1:
            predicted = left
        elif kind == 2:
            predicted = up
        elif kind == 3:
            predicted = (left + up) // 2
        else:
            p = left + up - corner
            pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
            predicted = left if pa <= pb and pa <= pc else (up if pb <= pc else corner)
        out.append((value - predicted) & 0xFF)
    return bytes([kind]) + bytes(out)


def make_png(rng, width, height, color_type, depth, interlace, key=False):
    """Liefert (Dateiinhalt, erwartete RGBA-Bytes)."""
    channels = CHANNELS[color_type]
    top = (1 << depth) - 1
    palette, trns = None, None
    if color_type == 3:
        size = min(1 << depth, rng.choice([2, 5, 16, 200, 256]))
        palette = [(rng.randrange(256), rng.randrange(256), rng.randrange(256)) for _ in range(size)]
        if key:
            trns = [rng.randrange(256) for _ in range(rng.randrange(1, size + 1))]
    pixels = [[[rng.randrange(len(palette)) if palette else rng.choice([0, top, rng.randrange(top + 1)])
                for _ in range(channels)] for _ in range(width)] for _ in range(height)]
    key_value = None
    if key and color_type in (0, 2):
        key_value = pixels[rng.randrange(height)][rng.randrange(width)]

    def rgba(p):
        if color_type == 3:
            alpha = trns[p[0]] if trns and p[0] < len(trns) else 255
            return (*palette[p[0]], alpha)
        scaled = [v * 255 // top for v in p]
        if color_type == 0:
            return (scaled[0],) * 3 + (0 if key_value == p else 255,)
        if color_type == 4:
            return (scaled[0],) * 3 + (scaled[1],)
        if color_type == 2:
            return (*scaled, 0 if key_value == p else 255)
        return tuple(scaled)

    expected = bytes(v for row in pixels for p in row for v in rgba(p))

    bpp = max(1, channels * depth // 8)
    raw = bytearray()
    if interlace:
        passes = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
    else:
        passes = [(0, 0, 1, 1)]
    for x0, y0, dx, dy in passes:
        previous = None
        for y in range(y0, height, dy):
            values = [v for x in range(x0, width, dx) for v in pixels[y][x]]
            if not values:
                break
            row = pack_row(values, depth)
            raw += filter_row(rng.randrange(5), row, previous, bpp)
            previous = row

    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, depth, color_type, 0, 0, interlace))
    if palette:
        data += chunk(b"PLTE", bytes(v for c in palette for v in c))
        if trns:
            data += chunk(b"tRNS", bytes(trns))
    elif key_value is not None:
        data += chunk(b"tRNS", b"".join(struct.pack(">H", v) for v in key_value))
    packed = zlib.compress(bytes(raw), rng.choice([0, 1, 6, 9]))
    # in mehreren Stücken ablegen, wie es echte Dateien tun
    cut = rng.randrange(1, len(packed)) if len(packed) > 1 else len(packed)
    data += chunk(b"IDAT", packed[:cut]) + chunk(b"IDAT", packed[cut:]) + chunk(b"IEND", b"")
    return data, expected


def test_png():
    rng = random.Random(1)
    combos = [(0, d) for d in (1, 2, 4, 8, 16)] + [(2, 8), (2, 16)] + \
             [(3, d) for d in (1, 2, 4, 8)] + [(4, 8), (4, 16), (6, 8), (6, 16)]
    samples = []
    for color_type, depth in combos:
        for interlace in (0, 1):
            for key in (False, True):
                if key and color_type in (4, 6):
                    continue
                for _ in range(3):
                    width, height = rng.choice([(1, 1), (7, 5), (8, 8), (9, 17), (33, 21), (64, 3)])
                    data, expected = make_png(rng, width, height, color_type, depth, interlace, key)
                    got = decode(data, "t.png")
                    what = "PNG Farbart %d, %d Bit, Zeilensprung %d, %dx%d" % (color_type, depth, interlace, width, height)
                    check(got is not None and got[0] == width and got[1] == height, what + ": Größe")
                    check(got is not None and got[3] == expected, what + ": Inhalt")
                    samples.append(data)
    return samples


# --- JPEG und GIF: Vergleich mit Pillow ---------------------------------------------

def test_image(rng, width, height, mode):
    """Ein Bild mit weichen Verläufen und etwas Struktur."""
    image = Image.new(mode, (width, height))
    px = image.load()
    a, b, c = rng.random() * 6, rng.random() * 6, rng.randrange(256)
    for y in range(height):
        for x in range(width):
            r = int(127 + 120 * __import__("math").sin(a * x / width + b * y / height))
            g = (x * 255 // max(1, width - 1) + c) % 256 if (x // 9 + y // 7) % 2 else y * 255 // max(1, height - 1)
            bl = (r + g) // 2
            px[x, y] = r if mode == "L" else (r, g, bl)
    return image


def test_jpeg():
    rng = random.Random(2)
    samples = []
    for mode in ("RGB", "L"):
        for progressive in (False, True):
            for subsampling in ((0, 1, 2) if mode == "RGB" else (0,)):
                for width, height in ((64, 64), (37, 53), (130, 17), (8, 8), (1, 1), (201, 99)):
                    for quality, optimize in ((90, False), (60, True)):
                        image = test_image(rng, width, height, mode)
                        buffer = io.BytesIO()
                        options = dict(quality=quality, optimize=optimize, progressive=progressive)
                        if mode == "RGB":
                            options["subsampling"] = subsampling
                        if rng.random() < 0.4:
                            options["restart_marker_blocks"] = rng.choice([1, 3, 8])
                        image.save(buffer, "JPEG", **options)
                        data = buffer.getvalue()
                        reference = Image.open(io.BytesIO(data)).convert("RGBA").tobytes()
                        got = decode(data, "t.jpg")
                        what = "JPEG %s %s Unterabtastung %d, %dx%d, Qualität %d" % (
                            mode, "progressiv" if progressive else "üblich", subsampling, width, height, quality)
                        check(got is not None and got[:2] == (width, height), what + ": Größe")
                        if got is None:
                            continue
                        diffs = [abs(x - y) for x, y in zip(got[3], reference)]
                        worst, mean = max(diffs), sum(diffs) / len(diffs)
                        check(worst <= 4, what + ": größte Abweichung %d, im Mittel %.2f" % (worst, mean))
                        samples.append(data)
    return samples


def test_gif():
    rng = random.Random(3)
    samples = []
    for width, height in ((1, 1), (13, 7), (64, 64), (100, 37)):
        for colors in (2, 16, 256):
            for interlace in (False, True):
                for transparent in (False, True):
                    image = test_image(rng, width, height, "RGB").quantize(colors)
                    buffer = io.BytesIO()
                    options = dict(interlace=interlace)
                    if transparent:
                        options["transparency"] = 0
                    image.save(buffer, "GIF", **options)
                    data = buffer.getvalue()
                    reference = Image.open(io.BytesIO(data)).convert("RGBA").tobytes()
                    got = decode(data, "t.gif")
                    what = "GIF %dx%d, %d Farben, Zeilensprung %s, durchsichtig %s" % (width, height, colors, interlace, transparent)
                    check(got is not None and got[:2] == (width, height), what + ": Größe")
                    if got is None:
                        continue
                    # Wo das Bild durchsichtig ist, ist die Farbe darunter egal
                    same = all(got[3][i + 3] == reference[i + 3] and
                               (reference[i + 3] == 0 or got[3][i:i + 3] == reference[i:i + 3])
                               for i in range(0, len(reference), 4))
                    check(same, what + ": Inhalt")
                    samples.append(data)
    return samples


def test_scale():
    image = Image.new("RGB", (40, 20))
    px = image.load()
    for y in range(20):
        for x in range(40):
            px[x, y] = (255, 0, 0) if x < 20 else (0, 0, 255)
    buffer = io.BytesIO()
    image.save(buffer, "PNG")
    path = os.path.join(TMP, "s.png")
    with open(path, "wb") as f:
        f.write(buffer.getvalue())
    out = path + ".rgba"
    result = subprocess.run([TOOL, "scale", path, "4", "2", out], capture_output=True, text=True)
    check(result.returncode == 0 and result.stdout.split()[:2] == ["4", "2"], "Verkleinern: Größe")
    data = open(out, "rb").read()
    check(data[0:3] == b"\xff\x00\x00" and data[12:15] == b"\x00\x00\xff", "Verkleinern: Farben")

    # Halb durchsichtiges Rot auf weißem Grund ergibt Rosa
    half = Image.new("RGBA", (8, 8), (255, 0, 0, 128))
    buffer = io.BytesIO()
    half.save(buffer, "PNG")
    with open(path, "wb") as f:
        f.write(buffer.getvalue())
    result = subprocess.run([TOOL, "scale", path, "2", "2", out], capture_output=True, text=True)
    data = open(out, "rb").read()
    check(result.returncode == 0 and data[0] == 255 and 120 <= data[1] <= 135 and 120 <= data[2] <= 135,
          "Verkleinern: Transparenz wird mit dem Hintergrund gemischt")


def test_fuzz(samples, rounds):
    rng = random.Random(4)
    for index, data in enumerate(rng.sample(samples, min(len(samples), 40))):
        path = os.path.join(TMP, "f%d" % index)
        with open(path, "wb") as f:
            f.write(data)
        result = subprocess.run([TOOL, "fuzz", path, str(rounds)], capture_output=True, text=True)
        check(result.returncode == 0, "beschädigte Dateien: Absturz oder Speicherfehler\n" + result.stderr[-2000:])


png = test_png()
jpeg = test_jpeg()
gif = test_gif()
test_scale()
test_fuzz(png, 300)
test_fuzz(jpeg, 300)
test_fuzz(gif, 300)
# Kein Bild: muss abgelehnt werden
check(decode(b"Das ist kein Bild.", "t.txt") is None, "Text wird nicht als Bild angenommen")
print("%d Prüfungen an den Bild-Dekodern, %d Fehler" % (checks, failures))
sys.exit(1 if failures else 0)
