#!/usr/bin/env python3
"""Schlüssel und Update-Pakete für HelloOS.

HelloOS installiert nur Updates, die mit dem passenden Schlüssel signiert
sind (Ed25519). Der öffentliche Teil (update-key.pub) wird in den Kernel
eingebaut und darf jeder kennen. Der geheime Teil (update-key.private) gehört
NICHT ins Repository - wer ihn hat, kann Updates für deine Installationen
ausstellen.

  update-tool.py keygen
      Erzeugt update-key.private und update-key.pub im aktuellen Verzeichnis.

  update-tool.py pack BOOTX64.EFI BUILD-NUMMER ZIELVERZEICHNIS [NEUES.txt]
      Schreibt helloos.upd (signiertes Paket) und helloos.version dorthin.
      Der Text aus NEUES.txt wird dem Benutzer vor der Installation gezeigt.
      Der Schlüssel kommt aus update-key.private oder aus der
      Umgebungsvariable HELLOOS_UPDATE_KEY (64 Hex-Zeichen).

  update-tool.py verify helloos.upd
      Prüft ein Paket gegen update-key.pub.

Braucht nur die Python-Standardbibliothek.
"""
import hashlib
import os
import secrets
import struct
import sys

# --- Ed25519 (nach dem Beispielcode in RFC 8032) --------------------------------

P = 2**255 - 19
L = 2**252 + 27742317777372353535851937790883648493
D = -121665 * pow(121666, P - 2, P) % P
SQRT_M1 = pow(2, (P - 1) // 4, P)


def _inv(x):
    return pow(x, P - 2, P)


def _add(a, b):
    A = (a[1] - a[0]) * (b[1] - b[0]) % P
    B = (a[1] + a[0]) * (b[1] + b[0]) % P
    C = 2 * a[3] * b[3] * D % P
    Dd = 2 * a[2] * b[2] % P
    E, F, G, H = B - A, Dd - C, Dd + C, B + A
    return (E * F % P, G * H % P, F * G % P, E * H % P)


def _mul(s, point):
    result = (0, 1, 1, 0)
    while s > 0:
        if s & 1:
            result = _add(result, point)
        point = _add(point, point)
        s >>= 1
    return result


def _recover_x(y, sign):
    x2 = (y * y - 1) * _inv(D * y * y + 1) % P
    x = pow(x2, (P + 3) // 8, P)
    if (x * x - x2) % P != 0:
        x = x * SQRT_M1 % P
    if (x * x - x2) % P != 0:
        return None
    if (x & 1) != sign:
        x = P - x
    return x


_GY = 4 * _inv(5) % P
_GX = _recover_x(_GY, 0)
G = (_GX, _GY, 1, _GX * _GY % P)


def _compress(point):
    zinv = _inv(point[2])
    x, y = point[0] * zinv % P, point[1] * zinv % P
    return (y | ((x & 1) << 255)).to_bytes(32, "little")


def _decompress(data):
    y = int.from_bytes(data, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    return None if x is None else (x, y, 1, x * y % P)


def _expand(seed):
    h = hashlib.sha512(seed).digest()
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def _hash_mod_l(data):
    return int.from_bytes(hashlib.sha512(data).digest(), "little") % L


def public_key(seed):
    a, _ = _expand(seed)
    return _compress(_mul(a, G))


def sign(seed, message):
    a, prefix = _expand(seed)
    A = _compress(_mul(a, G))
    r = _hash_mod_l(prefix + message)
    R = _compress(_mul(r, G))
    h = _hash_mod_l(R + A + message)
    s = (r + h * a) % L
    return R + s.to_bytes(32, "little")


def verify(public, message, signature):
    A = _decompress(public)
    R = _decompress(signature[:32])
    s = int.from_bytes(signature[32:], "little")
    if A is None or R is None or s >= L:
        return False
    h = _hash_mod_l(signature[:32] + public + message)
    left = _mul(s, G)
    right = _add(R, _mul(h, A))
    return _compress(left) == _compress(right)


# --- Paketformat ------------------------------------------------------------------
#
#   0   64  Signatur über alles, was folgt
#   64   8  Kennung "HELLOUPD"
#   72   4  Formatversion (2)
#   76   4  Build-Nummer
#   80   4  Länge der Startdatei
#   84   4  Länge des Textes "Was ist neu"
#   88   8  reserviert (0)
#   96   …  der Text (UTF-8), danach die neue BOOTX64.EFI

MAGIC = b"HELLOUPD"
NOTES_MAX = 4096
PRIVATE_FILE = "update-key.private"
PUBLIC_FILE = "update-key.pub"


def load_seed():
    text = os.environ.get("HELLOOS_UPDATE_KEY", "").strip()
    if not text and os.path.exists(PRIVATE_FILE):
        with open(PRIVATE_FILE) as f:
            text = f.read().strip()
    if not text:
        sys.exit("Kein Schlüssel: weder %s noch HELLOOS_UPDATE_KEY vorhanden.\n"
                 "Mit 'make update-key' einen erzeugen." % PRIVATE_FILE)
    try:
        seed = bytes.fromhex(text)
    except ValueError:
        seed = b""
    if len(seed) != 32:
        sys.exit("Der Schlüssel muss aus 64 Hex-Zeichen bestehen.")
    return seed


def cmd_keygen():
    if os.path.exists(PRIVATE_FILE) or os.path.exists(PUBLIC_FILE):
        sys.exit("Es gibt schon einen Schlüssel (%s / %s) - nichts geändert."
                 % (PRIVATE_FILE, PUBLIC_FILE))
    seed = secrets.token_bytes(32)
    fd = os.open(PRIVATE_FILE, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(seed.hex() + "\n")
    with open(PUBLIC_FILE, "w") as f:
        f.write(public_key(seed).hex() + "\n")
    print("Geheimer Schlüssel:     %s  (sicher aufbewahren, nicht einchecken!)"
          % PRIVATE_FILE)
    print("Öffentlicher Schlüssel: %s  (ins Repository einchecken)" % PUBLIC_FILE)


def read_notes(path):
    if not path or not os.path.exists(path):
        return b""
    with open(path, "rb") as f:
        notes = f.read().strip()
    if len(notes) > NOTES_MAX:
        sys.exit("%s ist zu lang (höchstens %d Bytes)." % (path, NOTES_MAX))
    return notes


def cmd_config(out_path, build, url, key, notes_path):
    """Schreibt die Einstellungen des Updaters als C-Datei (nur bei Änderung)."""
    def c_string(data):
        out = []
        for b in data:
            if b == 0x0A:
                out.append("\\n")
            elif b in (0x22, 0x5C):
                out.append("\\" + chr(b))
            elif 0x20 <= b < 0x7F:
                out.append(chr(b))
            else:
                out.append("\\%03o" % b)       # Umlaute usw. als Oktalfolge
        return '"' + "".join(out) + '"'

    text = ("#define OS_BUILD %d\n#define UPDATE_URL %s\n"
            "#define UPDATE_KEY_HEX %s\n#define RELEASE_NOTES %s\n"
            % (int(build), c_string(url.encode()), c_string(key.strip().encode()),
               c_string(read_notes(notes_path))))
    if os.path.exists(out_path):
        with open(out_path) as f:
            if f.read() == text:
                return
    with open(out_path, "w") as f:
        f.write(text)


def cmd_pack(efi_path, build, out_dir, notes_path=None):
    seed = load_seed()
    if os.path.exists(PUBLIC_FILE):
        with open(PUBLIC_FILE) as f:
            if f.read().strip() != public_key(seed).hex():
                sys.exit("Der geheime Schlüssel passt nicht zu %s - HelloOS würde "
                         "dieses Paket ablehnen." % PUBLIC_FILE)
    with open(efi_path, "rb") as f:
        payload = f.read()
    notes = read_notes(notes_path)
    body = (MAGIC + struct.pack("<IIII", 2, int(build), len(payload), len(notes)) +
            bytes(8) + notes + payload)
    package = sign(seed, body) + body
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "helloos.upd"), "wb") as f:
        f.write(package)
    with open(os.path.join(out_dir, "helloos.version"), "w") as f:
        f.write("%d\n" % int(build))
    print("Update-Paket für Build %d: %d Bytes, signiert mit Schlüssel %s..."
          % (int(build), len(package), public_key(seed).hex()[:16]))


def cmd_verify(package_path):
    with open(PUBLIC_FILE) as f:
        public = bytes.fromhex(f.read().strip())
    with open(package_path, "rb") as f:
        package = f.read()
    body = package[64:]
    ok = (len(package) >= 96 and body[:8] == MAGIC and
          verify(public, body, package[:64]))
    if not ok:
        sys.exit("Signatur UNGÜLTIG")
    _, build, size, notes_size = struct.unpack("<IIII", body[8:24])
    print("Signatur gültig: Build %d, Startdatei %d Bytes" % (build, size))
    if notes_size:
        print(body[32:32 + notes_size].decode("utf-8", "replace"))


def main():
    args = sys.argv[1:]
    if args[:1] == ["keygen"] and len(args) == 1:
        cmd_keygen()
    elif args[:1] == ["pack"] and len(args) in (4, 5):
        cmd_pack(*args[1:])
    elif args[:1] == ["config"] and len(args) == 6:
        cmd_config(*args[1:])
    elif args[:1] == ["verify"] and len(args) == 2:
        cmd_verify(args[1])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
