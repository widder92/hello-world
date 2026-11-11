#!/usr/bin/env python3
"""Vergleicht die Kryptografie des Kernels mit etablierten Bibliotheken.

Baut tests/crypto_test.c zusammen mit kernel/crypto_*.c als normales Programm,
füttert es mit zufälligen Aufgaben und prüft jedes Ergebnis gegen hashlib bzw.
das Paket "cryptography".

Aufruf (im Verzeichnis os/): python3 tests/crypto_test.py
"""
import hashlib
import hmac
import os
import random
import subprocess
import sys
import tempfile

from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ed25519, x25519
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from cryptography.hazmat.primitives.kdf.hkdf import HKDFExpand

HERE = os.path.dirname(os.path.abspath(__file__))
KERNEL = os.path.join(HERE, "..", "kernel")
rng = random.Random(20261111)


def rand(n):
    return bytes(rng.getrandbits(8) for _ in range(n))


def hexs(b):
    return b.hex() if b else "-"


cases = []          # (Befehlszeile, erwartete Ausgabe, Beschreibung)


def case(command, fields, expected, note):
    cases.append((command + " " + " ".join(hexs(f) for f in fields), expected, note))


# --- Hashfunktionen -----------------------------------------------------------
for n in list(range(0, 140)) + [255, 256, 257, 1000, 4000]:
    data = rand(n)
    case("sha256", [data], hashlib.sha256(data).hexdigest(), "SHA-256, %d Bytes" % n)
    case("sha512", [data], hashlib.sha512(data).hexdigest(), "SHA-512, %d Bytes" % n)

for key_len in (0, 1, 20, 32, 63, 64, 65, 200):
    key, data = rand(key_len), rand(rng.randrange(0, 300))
    case("hmac", [key, data], hmac.new(key, data, hashlib.sha256).hexdigest(),
         "HMAC, Schlüssel %d Bytes" % key_len)

for _ in range(20):
    salt, ikm, info = rand(rng.randrange(0, 40)), rand(32), rand(rng.randrange(0, 60))
    length = rng.randrange(1, 33)
    prk = hmac.new(salt, ikm, hashlib.sha256).digest()
    okm = HKDFExpand(hashes.SHA256(), length, info).derive(prk)
    case("hkdf", [salt, ikm, info, bytes([length])], okm.hex(), "HKDF")

# --- ChaCha20-Poly1305 -----------------------------------------------------------
for n in [0, 1, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 1000, 4000]:
    for aad_len in (0, 5, 13, 16, 21):
        key, nonce, aad, plain = rand(32), rand(12), rand(aad_len), rand(n)
        sealed = ChaCha20Poly1305(key).encrypt(nonce, plain, aad)
        case("seal", [key, nonce, aad, plain], sealed.hex(),
             "Verschlüsseln, %d Bytes" % n)
        case("open", [key, nonce, aad, sealed], hexs(plain),
             "Entschlüsseln, %d Bytes" % n)
        # Ein gekipptes Bit muss auffallen
        broken = bytearray(sealed)
        broken[rng.randrange(len(broken))] ^= 1 << rng.randrange(8)
        case("open", [key, nonce, aad, bytes(broken)], "FAIL", "Manipulation erkennen")

# --- X25519 -------------------------------------------------------------------------
BASE = bytes([9]) + bytes(31)
for _ in range(40):
    private = x25519.X25519PrivateKey.generate()
    scalar = private.private_bytes(serialization.Encoding.Raw,
                                   serialization.PrivateFormat.Raw,
                                   serialization.NoEncryption())
    public = private.public_key().public_bytes(serialization.Encoding.Raw,
                                               serialization.PublicFormat.Raw)
    case("x25519", [scalar, BASE], public.hex(), "X25519, öffentlicher Schlüssel")

    other = x25519.X25519PrivateKey.generate().public_key()
    other_bytes = other.public_bytes(serialization.Encoding.Raw,
                                     serialization.PublicFormat.Raw)
    case("x25519", [scalar, other_bytes], private.exchange(other).hex(),
         "X25519, gemeinsames Geheimnis")

# Prüfwert aus RFC 7748, Abschnitt 5.2
case("x25519",
     [bytes.fromhex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4"),
      bytes.fromhex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c")],
     "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552",
     "X25519, RFC 7748")

# --- Ed25519 -------------------------------------------------------------------------
for i in range(40):
    private = ed25519.Ed25519PrivateKey.generate()
    public = private.public_key().public_bytes(serialization.Encoding.Raw,
                                               serialization.PublicFormat.Raw)
    message = rand(rng.randrange(0, 600))
    signature = private.sign(message)
    case("ed25519", [public, signature, message], "1", "Ed25519, gültig")

    broken = bytearray(signature)
    broken[rng.randrange(64)] ^= 1 << rng.randrange(8)
    case("ed25519", [public, bytes(broken), message], "0", "Ed25519, Signatur verändert")
    if message:
        changed = bytearray(message)
        changed[rng.randrange(len(changed))] ^= 1
        case("ed25519", [public, signature, bytes(changed)], "0",
             "Ed25519, Nachricht verändert")
    wrong = ed25519.Ed25519PrivateKey.generate().public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    case("ed25519", [wrong, signature, message], "0", "Ed25519, falscher Schlüssel")

# Prüfwert aus RFC 8032, Abschnitt 7.1 (Test 2)
case("ed25519",
     [bytes.fromhex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c"),
      bytes.fromhex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
                    "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"),
      bytes.fromhex("72")],
     "1", "Ed25519, RFC 8032")


def main():
    with tempfile.TemporaryDirectory() as tmp:
        binary = os.path.join(tmp, "crypto_test")
        sources = [os.path.join(HERE, "crypto_test.c")] + [
            os.path.join(KERNEL, name)
            for name in ("crypto_hash.c", "crypto_aead.c", "crypto_curve.c")]
        subprocess.run(["cc", "-O2", "-Wall", "-o", binary] + sources, check=True)
        result = subprocess.run([binary], input="\n".join(c[0] for c in cases) + "\n",
                                capture_output=True, text=True, check=True)

    outputs = result.stdout.splitlines()
    failures = 0
    for (_, expected, note), actual in zip(cases, outputs):
        if actual != expected:
            failures += 1
            if failures <= 10:
                print("FEHLER: %s\n  erwartet %s\n  erhalten %s"
                      % (note, expected[:64], actual[:64]))
    if len(outputs) != len(cases):
        print("FEHLER: %d Ergebnisse für %d Aufgaben" % (len(outputs), len(cases)))
        failures += 1

    print("%d Prüfungen, %d Fehler" % (len(cases), failures))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
