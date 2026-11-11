#!/usr/bin/env python3
"""Vergleicht die Kryptografie des Kernels mit etablierten Bibliotheken.

Baut tests/crypto_test.c zusammen mit kernel/crypto_*.c als normales Programm,
füttert es mit zufälligen Aufgaben und prüft jedes Ergebnis gegen hashlib bzw.
das Paket "cryptography".

Aufruf (im Verzeichnis os/): python3 tests/crypto_test.py
"""
import datetime
import hashlib
import hmac
import ipaddress
import re
import os
import random
import subprocess
import sys
import tempfile

from cryptography import x509
from cryptography.exceptions import InvalidTag
from cryptography.x509.oid import NameOID
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, ed25519, padding, rsa, x25519
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

# --- Bausteine für SSH: ChaCha20 und Poly1305 einzeln, AES-CTR, ECDH über P-256 ------
from cryptography.hazmat.primitives import poly1305 as ref_poly1305
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

for n in [0, 1, 4, 63, 64, 65, 200, 1500]:
    key, nonce, counter, data = rand(32), rand(12), rng.randrange(0, 200), rand(n)
    full_nonce = counter.to_bytes(4, "little") + nonce
    stream = Cipher(algorithms.ChaCha20(key, full_nonce), mode=None).encryptor()
    case("chacha20", [key, nonce, bytes([counter]), data], hexs(stream.update(data)),
         "ChaCha20 allein, %d Bytes" % n)
    key = rand(32)
    case("poly1305", [key, data], ref_poly1305.Poly1305.generate_tag(key, data).hex(),
         "Poly1305 allein, %d Bytes" % n)

for key_len in (16, 24, 32):
    for n in [1, 15, 16, 17, 33, 100, 1000, 4096]:
        key, iv, data = rand(key_len), rand(16), rand(n)
        if n == 100:
            iv = iv[:8] + bytes([0xFF] * 8)      # Übertrag im Zähler
        enc = Cipher(algorithms.AES(key), modes.CTR(iv)).encryptor()
        case("aesctr", [key, iv, data], enc.update(data).hex(),
             "AES-%d-CTR, %d Bytes" % (key_len * 8, n))

for _ in range(12):
    ours = ec.generate_private_key(ec.SECP256R1())
    theirs = ec.generate_private_key(ec.SECP256R1())
    secret = ours.private_numbers().private_value.to_bytes(32, "big")
    def xy(key):
        numbers = key.public_key().public_numbers()
        return numbers.x.to_bytes(32, "big") + numbers.y.to_bytes(32, "big")
    case("ecdh_public", [secret], xy(ours).hex(), "ECDH P-256, öffentlicher Punkt")
    case("ecdh_shared", [secret, xy(theirs)],
         ours.exchange(ec.ECDH(), theirs.public_key()).hex(),
         "ECDH P-256, gemeinsames Geheimnis")
    wrong = bytearray(xy(theirs))
    wrong[40] ^= 1
    case("ecdh_shared", [secret, bytes(wrong)], "FAIL", "ECDH: Punkt nicht auf der Kurve")
case("ecdh_public", [bytes(32)], "FAIL", "ECDH: Geheimnis 0 abgelehnt")

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


# --- SHA-2 mit wählbarem Verfahren, RSA und ECDSA (für Zertifikate) -------------------
HASHES = [(0, hashes.SHA256(), hashlib.sha256), (1, hashes.SHA384(), hashlib.sha384),
          (2, hashes.SHA512(), hashlib.sha512)]


def flip(data):
    out = bytearray(data)
    out[rng.randrange(len(out))] ^= 1 << rng.randrange(8)
    return bytes(out)


for index, _, function in HASHES:
    for n in (0, 1, 111, 112, 127, 128, 129, 1000):
        data = rand(n)
        case("hash", [bytes([index]), data], function(data).hexdigest(),
             "Hash %d, %d Bytes" % (index, n))

for bits in (2048, 3072, 4096):
    private = rsa.generate_private_key(public_exponent=65537, key_size=bits)
    numbers = private.public_key().public_numbers()
    modulus = numbers.n.to_bytes((bits + 7) // 8, "big")
    exponent = numbers.e.to_bytes(3, "big")
    for index, algorithm, function in HASHES:
        message = rand(rng.randrange(1, 300))
        digest = function(message).digest()
        fields = [bytes([index]), modulus, exponent, digest]

        signature = private.sign(message, padding.PKCS1v15(), algorithm)
        case("rsa_pkcs1", fields + [signature], "1", "RSA-%d PKCS#1, gültig" % bits)
        case("rsa_pkcs1", fields + [flip(signature)], "0", "RSA PKCS#1, verändert")
        case("rsa_pkcs1", fields[:3] + [flip(digest), signature], "0",
             "RSA PKCS#1, falscher Hash")
        case("rsa_pss", fields + [signature], "0", "PKCS#1-Signatur als PSS")

        pss = padding.PSS(mgf=padding.MGF1(algorithm), salt_length=algorithm.digest_size)
        signature = private.sign(message, pss, algorithm)
        case("rsa_pss", fields + [signature], "1", "RSA-%d PSS, gültig" % bits)
        case("rsa_pss", fields + [flip(signature)], "0", "RSA PSS, verändert")
        case("rsa_pss", fields[:3] + [flip(digest), signature], "0",
             "RSA PSS, falscher Hash")

# Ein zu kurzer RSA-Schlüssel wird abgelehnt, auch wenn die Signatur stimmt.
weak = rsa.generate_private_key(public_exponent=65537, key_size=1024)
message = rand(50)
case("rsa_pkcs1", [bytes([0]), weak.public_key().public_numbers().n.to_bytes(128, "big"),
                   b"\x01\x00\x01", hashlib.sha256(message).digest(),
                   weak.sign(message, padding.PKCS1v15(), hashes.SHA256())],
     "0", "RSA-1024 abgelehnt")

for index, curve, size in ((0, ec.SECP256R1(), 32), (1, ec.SECP384R1(), 48)):
    for round_ in range(12):
        private = ec.generate_private_key(curve)
        numbers = private.public_key().public_numbers()
        point = numbers.x.to_bytes(size, "big") + numbers.y.to_bytes(size, "big")
        _, algorithm, function = HASHES[round_ % 3]
        message = rand(rng.randrange(1, 300))
        digest = function(message).digest()
        signature = private.sign(message, ec.ECDSA(algorithm))
        case("ecdsa", [bytes([index]), point, digest, signature], "1",
             "ECDSA Kurve %d, gültig" % index)
        # Nur der Teil des Hashwerts zählt, der so lang ist wie die Kurve.
        case("ecdsa", [bytes([index]), point, flip(digest[:size]) + digest[size:],
                       signature], "0", "ECDSA, falscher Hash")
        if len(digest) > size:
            case("ecdsa", [bytes([index]), point, digest[:size] + flip(digest[size:]),
                           signature], "1", "ECDSA, Änderung im abgeschnittenen Teil")
        other = ec.generate_private_key(curve).public_key().public_numbers()
        case("ecdsa", [bytes([index]), other.x.to_bytes(size, "big") +
                       other.y.to_bytes(size, "big"), digest, signature], "0",
             "ECDSA, falscher Schlüssel")
        # Punkt, der nicht auf der Kurve liegt
        bad = bytearray(point)
        bad[-1] ^= 1
        case("ecdsa", [bytes([index]), bytes(bad), digest, signature], "0",
             "ECDSA, Punkt nicht auf der Kurve")


# --- Zertifikate und Vertrauensketten ---------------------------------------------------
NOW = datetime.datetime(2026, 11, 11, 12, 0, 0)
DAY = datetime.timedelta(days=1)


def stamp(moment):
    return int(moment.strftime("%Y%m%d%H%M%S")).to_bytes(8, "big")


def make_key(kind):
    if kind == "rsa2048":
        return rsa.generate_private_key(public_exponent=65537, key_size=2048)
    if kind == "rsa4096":
        return rsa.generate_private_key(public_exponent=65537, key_size=4096)
    return ec.generate_private_key(ec.SECP256R1() if kind == "p256" else ec.SECP384R1())


def make_cert(name, key, issuer_name, issuer_key, algorithm, ca=False, names=(),
              start=NOW - 30 * DAY, end=NOW + 30 * DAY, constraints=True):
    builder = (x509.CertificateBuilder()
               .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, name)]))
               .issuer_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, issuer_name)]))
               .public_key(key.public_key())
               .serial_number(x509.random_serial_number())
               .not_valid_before(start).not_valid_after(end))
    if constraints:
        builder = builder.add_extension(x509.BasicConstraints(ca=ca, path_length=None),
                                        critical=True)
    if names:
        entries = [x509.IPAddress(ipaddress.ip_address(n)) if re.match(r"^[0-9.]+$", n)
                   else x509.DNSName(n) for n in names]
        builder = builder.add_extension(x509.SubjectAlternativeName(entries), critical=False)
    return builder.sign(issuer_key, algorithm).public_bytes(serialization.Encoding.DER)


def blob(certs):
    return b"".join(len(c).to_bytes(2, "big") + c for c in certs)


def chain_case(host, certs, expected, note, moment=NOW):
    case("chain", [host.encode(), stamp(moment), blob(certs)], expected, note)


UNTRUSTED = "Aussteller des Zertifikats ist nicht vertrauenswürdig"
WRONG_NAME = "Zertifikat gilt nicht für diesen Servernamen"
EXPIRED = "Zertifikat ist abgelaufen oder noch nicht gültig (stimmt die Systemuhr?)"

root_rsa_key, root_ec_key, other_root_key = make_key("rsa4096"), make_key("p384"), make_key("p384")
root_rsa = make_cert("Test-Wurzel RSA", root_rsa_key, "Test-Wurzel RSA", root_rsa_key,
                     hashes.SHA384(), ca=True, start=NOW - 900 * DAY, end=NOW + 900 * DAY)
root_ec = make_cert("Test-Wurzel EC", root_ec_key, "Test-Wurzel EC", root_ec_key,
                    hashes.SHA384(), ca=True, start=NOW - 900 * DAY, end=NOW + 900 * DAY)
other_root = make_cert("Fremde Wurzel", other_root_key, "Fremde Wurzel", other_root_key,
                       hashes.SHA384(), ca=True)
case("roots", [blob([root_rsa, root_ec]) + b"\x00\x00"], "ok", "Test-Wurzeln anmelden")

# Zwischenzertifikate: RSA unter RSA-Wurzel, P-256 unter EC-Wurzel, zweite Ebene darunter
mid_rsa_key, mid_ec_key, mid2_key = make_key("rsa2048"), make_key("p256"), make_key("p384")
mid_rsa = make_cert("Zwischen RSA", mid_rsa_key, "Test-Wurzel RSA", root_rsa_key,
                    hashes.SHA256(), ca=True)
mid_ec = make_cert("Zwischen EC", mid_ec_key, "Test-Wurzel EC", root_ec_key,
                   hashes.SHA384(), ca=True)
mid2 = make_cert("Zwischen zwei", mid2_key, "Zwischen EC", mid_ec_key, hashes.SHA256(),
                 ca=True)
NAMES = ["server.example", "*.wild.example", "10.0.2.2"]

for kind, digest in (("rsa2048", hashes.SHA256()), ("p256", hashes.SHA256()),
                     ("p384", hashes.SHA384()), ("rsa2048", hashes.SHA512())):
    key = make_key(kind)
    leaf_rsa = make_cert("server", key, "Zwischen RSA", mid_rsa_key, digest, names=NAMES)
    leaf_ec = make_cert("server", key, "Zwischen EC", mid_ec_key, digest, names=NAMES)
    leaf_deep = make_cert("server", key, "Zwischen zwei", mid2_key, digest, names=NAMES)
    leaf_direct = make_cert("server", key, "Test-Wurzel EC", root_ec_key, digest, names=NAMES)

    chain_case("server.example", [leaf_rsa, mid_rsa], "OK", "Kette über RSA (%s)" % kind)
    chain_case("server.example", [leaf_ec, mid_ec], "OK", "Kette über EC (%s)" % kind)
    chain_case("server.example", [leaf_deep, mid2, mid_ec], "OK", "zwei Zwischenstufen")
    chain_case("server.example", [leaf_deep, mid_ec, mid_rsa, mid2], "OK",
               "Zwischenzertifikate in anderer Reihenfolge")
    chain_case("server.example", [leaf_ec, mid_ec, root_ec], "OK", "Wurzel mitgeschickt")
    chain_case("server.example", [leaf_direct], "OK", "direkt von der Wurzel")
    chain_case("SERVER.Example", [leaf_ec, mid_ec], "OK", "Groß/Klein im Namen egal")
    chain_case("a.wild.example", [leaf_ec, mid_ec], "OK", "Platzhalter-Name")
    chain_case("10.0.2.2", [leaf_ec, mid_ec], "OK", "IP-Adresse")

    chain_case("anderer.example", [leaf_ec, mid_ec], WRONG_NAME, "falscher Name")
    chain_case("a.b.wild.example", [leaf_ec, mid_ec], WRONG_NAME, "Platzhalter: zwei Ebenen")
    chain_case("wild.example", [leaf_ec, mid_ec], WRONG_NAME, "Platzhalter: ohne Ebene")
    chain_case("10.0.2.3", [leaf_ec, mid_ec], WRONG_NAME, "falsche IP-Adresse")
    chain_case("server.example", [leaf_ec], UNTRUSTED, "Zwischenzertifikat fehlt")
    chain_case("server.example", [leaf_ec, mid_rsa], UNTRUSTED, "falsches Zwischenzertifikat")
    chain_case("server.example", [leaf_ec, mid_ec], EXPIRED, "abgelaufen",
               moment=NOW + 31 * DAY)
    chain_case("server.example", [leaf_ec, mid_ec], EXPIRED, "noch nicht gültig",
               moment=NOW - 31 * DAY)

    # Ein verändertes Byte im signierten Teil muss auffallen
    broken = bytearray(leaf_ec)
    broken[len(broken) // 3] ^= 0x01
    case("chain", [b"server.example", stamp(NOW), blob([bytes(broken), mid_ec])],
         None, "Zertifikat verändert")

    # Kette zu einer Wurzel, der wir nicht vertrauen; und ein selbst signiertes Zertifikat
    foreign = make_cert("server", key, "Fremde Wurzel", other_root_key, digest, names=NAMES)
    chain_case("server.example", [foreign, other_root], UNTRUSTED, "fremde Wurzel")
    selfsigned = make_cert("server", key, "server", key, digest, names=NAMES)
    chain_case("server.example", [selfsigned], UNTRUSTED, "selbst signiert")

    # Ein Serverzertifikat darf keine weiteren Zertifikate ausstellen.
    if kind != "rsa2048" or digest.name == "sha256":
        fake = make_cert("server", make_key("p256"), "server", key, hashes.SHA256(),
                         names=["boese.example"])
        chain_case("boese.example", [fake, leaf_ec, mid_ec], UNTRUSTED,
                   "Serverzertifikat als Aussteller missbraucht")

expired_mid = make_cert("Zwischen alt", mid_ec_key, "Test-Wurzel EC", root_ec_key,
                        hashes.SHA384(), ca=True, start=NOW - 60 * DAY, end=NOW - DAY)
leaf_old = make_cert("server", make_key("p256"), "Zwischen alt", mid_ec_key, hashes.SHA256(),
                     names=NAMES)
chain_case("server.example", [leaf_old, expired_mid], EXPIRED, "Zwischenzertifikat abgelaufen")

# Die echten Wurzelzertifikate aus dem Kernel: jedes muss lesbar sein und,
# sofern es ein unterstütztes Verfahren nutzt, seine eigene Signatur bestehen.
with open(os.path.join(KERNEL, "tls_roots.bin"), "rb") as f:
    store = f.read()
position = real_roots = 0
while True:
    length = int.from_bytes(store[position:position + 2], "big")
    if length == 0:
        break
    der = store[position + 2:position + 2 + length]
    position += 2 + length
    real_roots += 1
    certificate = x509.load_der_x509_certificate(der)
    public = certificate.public_key()
    supported_key = (isinstance(public, rsa.RSAPublicKey) or
                     (isinstance(public, ec.EllipticCurvePublicKey) and
                      public.curve.name in ("secp256r1", "secp384r1")))
    supported = supported_key and certificate.signature_hash_algorithm.name in (
        "sha256", "sha384", "sha512")
    case("selfsig", [der], "1" if supported else "nicht unterstuetzt",
         "echte Wurzel: " + certificate.subject.rfc4514_string()[:50])


def main():
    with tempfile.TemporaryDirectory() as tmp:
        binary = os.path.join(tmp, "crypto_test")
        sources = [os.path.join(HERE, "crypto_test.c")] + [
            os.path.join(KERNEL, name)
            for name in ("crypto_hash.c", "crypto_aead.c", "crypto_aes.c", "crypto_curve.c",
                         "crypto_pk.c", "x509.c")]
        subprocess.run(["cc", "-O2", "-Wall", "-o", binary] + sources, check=True)
        result = subprocess.run([binary], input="\n".join(c[0] for c in cases) + "\n",
                                capture_output=True, text=True, check=True)

    outputs = result.stdout.splitlines()
    failures = 0
    for (_, expected, note), actual in zip(cases, outputs):
        if expected is None:            # irgendein Fehler, nur nicht "OK"
            expected = actual if actual != "OK" else "ein Fehler"
        if actual != expected:
            failures += 1
            if failures <= 10:
                print("FEHLER: %s\n  erwartet %s\n  erhalten %s"
                      % (note, expected[:64], actual[:64]))
    if len(outputs) != len(cases):
        print("FEHLER: %d Ergebnisse für %d Aufgaben" % (len(outputs), len(cases)))
        failures += 1

    print("%d Prüfungen, %d Fehler (darunter %d echte Wurzelzertifikate)"
          % (len(cases), failures, real_roots))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
