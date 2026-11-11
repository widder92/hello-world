#!/usr/bin/env python3
"""Wandelt Zertifikate im PEM-Format in die Sammlung um, die HelloOS einbaut.

  mkroots.py AUSGABE.bin [--exclude TEXT ...] [PEM-Datei ...]

Format der Ausgabe: je Zertifikat zwei Bytes Länge (höchstwertiges zuerst)
und die DER-Daten, am Ende die Länge 0. Ohne PEM-Dateien entsteht eine leere
Sammlung. Zertifikate, deren Daten einen der mit --exclude genannten Texte
enthalten, werden ausgelassen.

Damit ist kernel/tls_roots.bin entstanden (die Liste vertrauenswürdiger
Zertifizierungsstellen, wie sie Mozilla pflegt und Linux-Distributionen als
"ca-certificates" ausliefern):

  mkroots.py kernel/tls_roots.bin /etc/ssl/certs/ca-certificates.crt

Ebenso entsteht beim Bauen aus EXTRA_CA=datei.pem die Zusatzsammlung für
eigene Zertifizierungsstellen. Braucht nur die Python-Standardbibliothek.
"""
import base64
import re
import sys


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    out_path, excludes, files = args[0], [], []
    i = 1
    while i < len(args):
        if args[i] == "--exclude":
            excludes.append(args[i + 1].encode())
            i += 2
        else:
            files.append(args[i])
            i += 1

    blob = bytearray()
    count = skipped = 0
    for path in files:
        with open(path) as f:
            text = f.read()
        for body in re.findall(r"-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----",
                               text, re.S):
            der = base64.b64decode(body)
            if any(e in der for e in excludes) or not 0 < len(der) < 65536:
                skipped += 1
                continue
            blob += len(der).to_bytes(2, "big") + der
            count += 1
    blob += b"\x00\x00"

    try:
        with open(out_path, "rb") as f:
            if f.read() == bytes(blob):
                return                  # unverändert: Datei nicht anfassen
    except FileNotFoundError:
        pass
    with open(out_path, "wb") as f:
        f.write(blob)
    if files:
        print("%s: %d Zertifikate, %d ausgelassen, %d Bytes"
              % (out_path, count, skipped, len(blob)))


if __name__ == "__main__":
    main()
