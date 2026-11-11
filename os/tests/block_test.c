/*
 * Prüft die Schreibsperre für fest eingebaute Platten (block.h): Außerhalb
 * der freigegebenen Partition darf kein einziger Sektor beschrieben werden -
 * auch nicht bei Angaben am Rand oder solchen, die beim Rechnen überlaufen.
 */
#include <stdio.h>

#include "../kernel/block.h"

static int failures, checks;

static void expect(const block_device_t *dev, uint64_t lba, uint32_t count, bool allowed,
                   const char *what)
{
    checks++;
    if (block_write_allowed(dev, lba, count) != allowed) {
        failures++;
        printf("FEHLER: %s (Sektor %llu, Anzahl %u)\n", what, (unsigned long long)lba, count);
    }
}

int main(void)
{
    /* Eine Platte mit 1000000 Sektoren; die HelloOS-Partition: 821248 bis 821248+4095 */
    block_device_t dev = { .present = true, .sectors = 1000000, .internal = true };
    const uint64_t first = 821248, size = 4096;

    /* Frisch angemeldet: gar nichts ist erlaubt */
    expect(&dev, 0, 1, false, "ohne Freigabe: Sektor 0");
    expect(&dev, first, 1, false, "ohne Freigabe: in der späteren Partition");
    expect(&dev, 999999, 1, false, "ohne Freigabe: letzter Sektor");

    dev.write_first = first;
    dev.write_count = size;
    expect(&dev, first, 1, true, "erster Sektor der Partition");
    expect(&dev, first, (uint32_t)size, true, "die ganze Partition");
    expect(&dev, first + size - 1, 1, true, "letzter Sektor der Partition");
    expect(&dev, first + 100, 64, true, "mitten in der Partition");

    expect(&dev, first - 1, 1, false, "Sektor direkt davor");
    expect(&dev, first - 1, 2, false, "beginnt davor, reicht hinein");
    expect(&dev, first + size, 1, false, "Sektor direkt dahinter");
    expect(&dev, first + size - 1, 2, false, "beginnt darin, reicht hinaus");
    expect(&dev, first, (uint32_t)size + 1, false, "ein Sektor zu viel");
    expect(&dev, 0, 1, false, "Sektor 0 (Partitionstabelle)");
    expect(&dev, 2048, 8, false, "Anfang der Platte (Windows-Startbereich)");
    expect(&dev, 999999, 1, false, "letzter Sektor der Platte");
    expect(&dev, 1000000, 1, false, "hinter dem Ende der Platte");
    expect(&dev, first, 0, false, "Anzahl 0");
    expect(&dev, first, 0xFFFFFFFFu, false, "riesige Anzahl");
    expect(&dev, 0xFFFFFFFFFFFFFFFFull, 1, false, "Sektornummer am Ende des Zahlenraums");
    expect(&dev, 0xFFFFFFFFFFFFFFFFull - 10, 0xFFFFFFFFu, false, "Überlauf beim Addieren");

    /* Eine unsinnige Freigabe darf nichts öffnen */
    dev.write_first = 999000;
    dev.write_count = 5000;             /* reicht über das Ende hinaus */
    expect(&dev, 999999, 1, true, "Freigabe bis zum Ende: letzter Sektor");
    expect(&dev, 999999, 2, false, "Freigabe über das Ende hinaus: bleibt begrenzt");

    /* Alle Sektoren rund um die Grenzen einzeln durchgehen */
    dev.write_first = first;
    dev.write_count = size;
    for (uint64_t lba = first - 5000; lba < first + size + 5000; lba++) {
        bool inside = lba >= first && lba < first + size;
        checks++;
        if (block_write_allowed(&dev, lba, 1) != inside) {
            failures++;
            printf("FEHLER: Sektor %llu falsch eingestuft\n", (unsigned long long)lba);
            break;
        }
    }

    printf("%d Prüfungen der Schreibsperre, %d Fehler\n", checks, failures);
    return failures != 0;
}
