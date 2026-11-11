; =============================================================================
; Vertrauenswürdige Wurzelzertifikate, als Daten in den Kernel eingebunden.
;
; tls_roots.bin    - die Liste der Zertifizierungsstellen, die auch Browser
;                    und Linux-Distributionen verwenden (Mozillas CA-Liste,
;                    Stand siehe Git-Historie; erzeugt mit tools/mkroots.py)
; extra-ca.bin     - beim Bauen mit EXTRA_CA=datei.pem zusätzlich aufgenommene
;                    Zertifikate (sonst leer)
; =============================================================================
bits 64

section .rdata

global tls_roots_builtin
tls_roots_builtin:
    incbin "kernel/tls_roots.bin"

global tls_roots_extra
tls_roots_extra:
    incbin "extra-ca.bin"
