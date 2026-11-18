#!/usr/bin/env python3
"""Macht aus einer Datei ein Zeichenfeld für C.

  embed.py EINGABE NAME AUSGABE

Schreibt "static const char NAME[] = { ... , 0 };" - so kann der Kernel (der
keine Dateien nachladen kann, bevor es ein Dateisystem gibt) Texte wie den in
JavaScript geschriebenen Teil des Browsers fest einbauen. Die Ausgabe ändert
sich nur, wenn sich der Inhalt ändert.
"""
import os
import sys


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    source, name, target = sys.argv[1:]
    with open(source, "rb") as f:
        data = f.read()
    lines = ["/* erzeugt aus %s - nicht von Hand ändern */" % os.path.basename(source),
             "static const char %s[] = {" % name]
    for i in range(0, len(data), 20):
        lines.append("    " + ",".join(str(b if b < 128 else b - 256) for b in data[i:i + 20]) + ",")
    lines.append("    0\n};\n")
    text = "\n".join(lines)
    try:
        with open(target) as f:
            if f.read() == text:
                return
    except OSError:
        pass
    with open(target, "w") as f:
        f.write(text)


if __name__ == "__main__":
    main()
