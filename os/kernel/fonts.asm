; =============================================================================
; Schriftdateien, als Daten in den Kernel eingebunden.
;
; DejaVu Sans (normal und fett), unverändert aus dem DejaVu-Projekt; Lizenz
; siehe fonts/LIZENZ-DejaVu.txt. Gelesen werden sie von kernel/truetype.c.
; =============================================================================
bits 64

section .rdata

global font_file_regular
font_file_regular:
    incbin "fonts/DejaVuSans.ttf"
global font_file_regular_end
font_file_regular_end:

global font_file_bold
font_file_bold:
    incbin "fonts/DejaVuSans-Bold.ttf"
global font_file_bold_end
font_file_bold_end:
