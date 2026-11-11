/*
 * Startcode: HelloOS ist ein UEFI-Programm (BOOTX64.EFI). Die Firmware lädt
 * es, schaltet den Prozessor in den 64-Bit-Modus und ruft efi_main() auf.
 *
 * Hier holen wir uns von der Firmware alles, was der Kernel später braucht
 * (Bildschirm, Speicherkarte, Uhrzeit), verabschieden die Firmware mit
 * ExitBootServices() und sind ab dann allein für die Maschine zuständig.
 */
#include "bootinfo.h"
#include "cpu.h"
#include "serial.h"
#include "string.h"

#define GIB        (1ull << 30)
#define STACK_SIZE (128 * 1024)

boot_info_t boot_info;

void stack_switch(void *stack_top, void (*entry)(void *), void *arg);

static uint8_t boot_stack[STACK_SIZE] __attribute__((aligned(16)));

static void fatal(efi_system_table_t *st, const uint16_t *message)
{
    st->con_out->output_string(st->con_out, message);
    for (;;)
        cpu_halt();
}

/* --- Bildschirm ------------------------------------------------------------- */

static bool usable_format(const efi_gop_mode_info_t *info)
{
    return info->pixel_format == EFI_PIXEL_RGB ||
           info->pixel_format == EFI_PIXEL_BGR;
}

static void setup_graphics(efi_boot_services_t *bs)
{
    static const efi_guid_t gop_guid = EFI_GOP_GUID;
    efi_gop_t *gop = NULL;

    if (EFI_ERROR(bs->locate_protocol(&gop_guid, NULL, (void **)&gop)) || !gop)
        return;

    /*
     * Die Firmware hat meist schon die native Auflösung gewählt. Nur wenn
     * die sehr groß ist (4K), nehmen wir den größten Modus bis 1920x1200 -
     * gezeichnet wird ohne Grafikbeschleunigung.
     */
    const efi_gop_mode_info_t *cur = gop->mode->info;
    bool keep = usable_format(cur) && cur->width <= 1920 && cur->height <= 1200;
    if (!keep) {
        uint32_t best = gop->mode->mode;
        uint64_t best_area = 0;
        for (uint32_t i = 0; i < gop->mode->max_mode; i++) {
            efi_gop_mode_info_t *info;
            uint64_t size;
            if (EFI_ERROR(gop->query_mode(gop, i, &size, &info)))
                continue;
            if (!usable_format(info) || info->width > 1920 ||
                info->height > 1200 || info->width < 800 || info->height < 600)
                continue;
            uint64_t area = (uint64_t)info->width * info->height;
            if (area > best_area) {
                best_area = area;
                best = i;
            }
        }
        if (best != gop->mode->mode)
            gop->set_mode(gop, best);
    }

    const efi_gop_mode_info_t *info = gop->mode->info;
    if (!usable_format(info))
        return;
    boot_info.fb_base = gop->mode->frame_buffer_base;
    boot_info.fb_width = info->width;
    boot_info.fb_height = info->height;
    boot_info.fb_stride = info->pixels_per_scan_line;
    boot_info.fb_bgr = info->pixel_format == EFI_PIXEL_BGR;
}

/* --- Kleinkram -------------------------------------------------------------- */

static void find_acpi(efi_system_table_t *st)
{
    static const efi_guid_t acpi_guid = EFI_ACPI_20_GUID;
    for (uint64_t i = 0; i < st->number_of_table_entries; i++) {
        if (memcmp(&st->configuration_table[i].guid, &acpi_guid,
                   sizeof(acpi_guid)) == 0)
            boot_info.rsdp = (uint64_t)st->configuration_table[i].table;
    }
}

static void copy_vendor(efi_system_table_t *st)
{
    const uint16_t *src = st->firmware_vendor;
    size_t i = 0;
    for (; src && src[i] && i < sizeof(boot_info.firmware_vendor) - 1; i++)
        boot_info.firmware_vendor[i] = src[i] < 0x7F ? (char)src[i] : '?';
    boot_info.firmware_vendor[i] = '\0';
}

/* Die Firmware kann genau warten - daran messen wir den Zeitstempelzähler. */
static void calibrate_tsc(efi_boot_services_t *bs)
{
    uint64_t start = rdtsc();
    bs->stall(50000);                   /* 50 ms */
    boot_info.tsc_hz = (rdtsc() - start) * 20;
}

/* --- Seitentabellen --------------------------------------------------------- */

/* Höchste Adresse, die Speicher oder Laufzeitdienste der Firmware belegen. */
static uint64_t highest_address(void)
{
    uint64_t top = 4 * GIB;             /* Geräte (APIC, PCI) liegen darunter */
    uint8_t *p = (uint8_t *)boot_info.mmap;
    uint8_t *end = p + boot_info.mmap_size;

    for (; p < end; p += boot_info.mmap_desc_size) {
        const efi_memory_descriptor_t *d = (const efi_memory_descriptor_t *)p;
        bool ram = (d->type >= EFI_LOADER_CODE &&
                    d->type <= EFI_CONVENTIONAL_MEMORY) ||
                   d->type == EFI_ACPI_RECLAIM_MEMORY ||
                   d->type == EFI_ACPI_NVS_MEMORY;
        if (!ram && !(d->attribute & EFI_MEMORY_RUNTIME))
            continue;
        uint64_t limit = d->physical_start + d->number_of_pages * EFI_PAGE_SIZE;
        if (limit > top)
            top = limit;
    }
    return top;
}

/*
 * Eigene Seitentabellen: Der gesamte RAM wird 1:1 abgebildet (virtuelle
 * Adresse = physische Adresse), mit 2 MiB großen Seiten. Die Tabellen der
 * Firmware wollen wir nicht weiterbenutzen - ihr Speicher gehört uns nicht.
 */
static void build_page_tables(uint64_t *pool, uint64_t gigabytes)
{
    uint64_t *pml4 = pool;
    uint64_t *pdpt = pool + 512;
    uint64_t *pd = pool + 1024;

    memset(pool, 0, (2 + gigabytes) * 4096);
    pml4[0] = (uint64_t)pdpt | 0x03;    /* vorhanden + beschreibbar */
    for (uint64_t g = 0; g < gigabytes; g++, pd += 512) {
        pdpt[g] = (uint64_t)pd | 0x03;
        for (uint64_t i = 0; i < 512; i++)
            pd[i] = ((g << 30) | (i << 21)) | 0x83;     /* + 2-MiB-Seite */
    }
    boot_info.pml4 = pml4;
    write_cr3((uint64_t)pml4);
}

/* --- Einstieg --------------------------------------------------------------- */

efi_status_t efi_main(efi_handle_t image, efi_system_table_t *st)
{
    efi_boot_services_t *bs = st->boot_services;

    bs->set_watchdog_timer(0, 0, 0, NULL);  /* sonst Neustart nach 5 Minuten */
    serial_init();
    serial_write("\nHelloOS: Start über UEFI\n");

    boot_info.runtime = st->runtime_services;
    copy_vendor(st);
    find_acpi(st);
    st->runtime_services->get_time(&boot_info.boot_time, NULL);
    calibrate_tsc(bs);
    setup_graphics(bs);

    /* Platz für die Speicherkarte besorgen (mit Reserve - sie kann wachsen) */
    uint64_t map_size = 0, map_key = 0, desc_size = 0;
    uint32_t desc_version = 0;
    bs->get_memory_map(&map_size, NULL, &map_key, &desc_size, &desc_version);
    uint64_t map_capacity = map_size + 16 * EFI_PAGE_SIZE;
    uint64_t map_address = 0;
    if (EFI_ERROR(bs->allocate_pages(EFI_ALLOCATE_ANY_PAGES, EFI_LOADER_DATA,
                                     map_capacity / EFI_PAGE_SIZE,
                                     &map_address)))
        fatal(st, u"HelloOS: Kein Speicher fuer die Speicherkarte.\r\n");
    boot_info.mmap = (efi_memory_descriptor_t *)map_address;

    /* Erste Karte: nur um zu wissen, wie groß die Seitentabellen werden */
    map_size = map_capacity;
    if (EFI_ERROR(bs->get_memory_map(&map_size, boot_info.mmap, &map_key,
                                     &desc_size, &desc_version)))
        fatal(st, u"HelloOS: Speicherkarte nicht lesbar.\r\n");
    boot_info.mmap_size = map_size;
    boot_info.mmap_desc_size = desc_size;

    uint64_t gigabytes = (highest_address() + GIB - 1) / GIB;
    if (gigabytes > 512)
        gigabytes = 512;
    uint64_t pool_address = 0;
    if (EFI_ERROR(bs->allocate_pages(EFI_ALLOCATE_ANY_PAGES, EFI_LOADER_DATA,
                                     2 + gigabytes, &pool_address)))
        fatal(st, u"HelloOS: Kein Speicher fuer die Seitentabellen.\r\n");

    /*
     * Firmware verabschieden. Das klappt nur mit einer brandaktuellen
     * Speicherkarte - darum direkt davor noch einmal holen.
     */
    efi_status_t status;
    int attempts = 0;
    do {
        map_size = map_capacity;
        status = bs->get_memory_map(&map_size, boot_info.mmap, &map_key,
                                    &desc_size, &desc_version);
        if (!EFI_ERROR(status))
            status = bs->exit_boot_services(image, map_key);
    } while (EFI_ERROR(status) && ++attempts < 4);
    if (EFI_ERROR(status))
        fatal(st, u"HelloOS: ExitBootServices fehlgeschlagen.\r\n");

    irq_disable();
    boot_info.mmap_size = map_size;
    boot_info.mmap_desc_size = desc_size;

    build_page_tables((uint64_t *)pool_address, gigabytes);
    stack_switch(boot_stack + STACK_SIZE, kmain, NULL);
    return EFI_SUCCESS;                 /* wird nie erreicht */
}
