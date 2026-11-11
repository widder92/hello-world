/*
 * UEFI-Schnittstellen - nur der kleine Teil, den HelloOS beim Start braucht.
 * Die Reihenfolge der Felder ist durch die UEFI-Spezifikation festgelegt.
 */
#pragma once

#include <stdint.h>

typedef uint64_t efi_status_t;
typedef void *efi_handle_t;

#define EFI_SUCCESS          0
#define EFI_ERROR(status)    ((int64_t)(status) < 0)
#define EFI_BUFFER_TOO_SMALL (0x8000000000000000ull | 5)

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} efi_guid_t;

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} efi_table_header_t;

typedef struct {
    uint16_t year;
    uint8_t month, day, hour, minute, second, pad1;
    uint32_t nanosecond;
    int16_t timezone;
    uint8_t daylight, pad2;
} efi_time_t;

/* --- Speicherkarte ---------------------------------------------------------- */

enum {
    EFI_RESERVED_MEMORY = 0,
    EFI_LOADER_CODE,
    EFI_LOADER_DATA,
    EFI_BOOT_SERVICES_CODE,
    EFI_BOOT_SERVICES_DATA,
    EFI_RUNTIME_SERVICES_CODE,
    EFI_RUNTIME_SERVICES_DATA,
    EFI_CONVENTIONAL_MEMORY,            /* frei nutzbarer RAM */
    EFI_UNUSABLE_MEMORY,
    EFI_ACPI_RECLAIM_MEMORY,
    EFI_ACPI_NVS_MEMORY,
    EFI_MEMORY_MAPPED_IO,
    EFI_MEMORY_MAPPED_IO_PORTS,
    EFI_PAL_CODE,
    EFI_PERSISTENT_MEMORY,
    EFI_MEMORY_TYPE_COUNT,
};

#define EFI_MEMORY_RUNTIME 0x8000000000000000ull
#define EFI_PAGE_SIZE      4096

typedef struct {
    uint32_t type;
    uint32_t pad;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} efi_memory_descriptor_t;

/* --- Textausgabe der Firmware (nur für Fehlermeldungen beim Start) ---------- */

typedef struct efi_text_output {
    void *reset;
    efi_status_t (*output_string)(struct efi_text_output *self,
                                  const uint16_t *string);
} efi_text_output_t;

/* --- Grafik (Graphics Output Protocol) -------------------------------------- */

#define EFI_GOP_GUID \
    { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }

enum {
    EFI_PIXEL_RGB = 0,                  /* Bytes im Speicher: R, G, B, x */
    EFI_PIXEL_BGR = 1,                  /* Bytes im Speicher: B, G, R, x */
    EFI_PIXEL_BITMASK = 2,
    EFI_PIXEL_BLT_ONLY = 3,
};

typedef struct {
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t pixel_masks[4];
    uint32_t pixels_per_scan_line;
} efi_gop_mode_info_t;

typedef struct {
    uint32_t max_mode;
    uint32_t mode;
    efi_gop_mode_info_t *info;
    uint64_t size_of_info;
    uint64_t frame_buffer_base;
    uint64_t frame_buffer_size;
} efi_gop_mode_t;

typedef struct efi_gop {
    efi_status_t (*query_mode)(struct efi_gop *self, uint32_t mode,
                               uint64_t *size_of_info,
                               efi_gop_mode_info_t **info);
    efi_status_t (*set_mode)(struct efi_gop *self, uint32_t mode);
    void *blt;
    efi_gop_mode_t *mode;
} efi_gop_t;

/* --- Boot Services (nur bis ExitBootServices verfügbar) --------------------- */

enum {
    EFI_ALLOCATE_ANY_PAGES = 0,
};

typedef struct {
    efi_table_header_t header;
    void *raise_tpl;
    void *restore_tpl;
    efi_status_t (*allocate_pages)(uint32_t type, uint32_t memory_type,
                                   uint64_t pages, uint64_t *address);
    void *free_pages;
    efi_status_t (*get_memory_map)(uint64_t *map_size,
                                   efi_memory_descriptor_t *map,
                                   uint64_t *map_key,
                                   uint64_t *descriptor_size,
                                   uint32_t *descriptor_version);
    void *allocate_pool;
    void *free_pool;
    void *create_event;
    void *set_timer;
    void *wait_for_event;
    void *signal_event;
    void *close_event;
    void *check_event;
    void *install_protocol_interface;
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    void *handle_protocol;
    void *reserved;
    void *register_protocol_notify;
    void *locate_handle;
    void *locate_device_path;
    void *install_configuration_table;
    void *load_image;
    void *start_image;
    void *exit;
    void *unload_image;
    efi_status_t (*exit_boot_services)(efi_handle_t image, uint64_t map_key);
    void *get_next_monotonic_count;
    efi_status_t (*stall)(uint64_t microseconds);
    efi_status_t (*set_watchdog_timer)(uint64_t timeout, uint64_t code,
                                       uint64_t data_size, uint16_t *data);
    void *connect_controller;
    void *disconnect_controller;
    void *open_protocol;
    void *close_protocol;
    void *open_protocol_information;
    void *protocols_per_handle;
    void *locate_handle_buffer;
    efi_status_t (*locate_protocol)(const efi_guid_t *protocol,
                                    void *registration, void **interface);
} efi_boot_services_t;

/* --- Runtime Services (bleiben auch danach nutzbar) ------------------------- */

enum {
    EFI_RESET_COLD = 0,
    EFI_RESET_WARM = 1,
    EFI_RESET_SHUTDOWN = 2,
};

typedef struct {
    efi_table_header_t header;
    efi_status_t (*get_time)(efi_time_t *time, void *capabilities);
    void *set_time;
    void *get_wakeup_time;
    void *set_wakeup_time;
    void *set_virtual_address_map;
    void *convert_pointer;
    void *get_variable;
    void *get_next_variable_name;
    void *set_variable;
    void *get_next_high_monotonic_count;
    void (*reset_system)(uint32_t type, efi_status_t status,
                         uint64_t data_size, void *data);
} efi_runtime_services_t;

/* --- Systemtabelle ---------------------------------------------------------- */

#define EFI_ACPI_20_GUID \
    { 0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }

typedef struct {
    efi_guid_t guid;
    void *table;
} efi_configuration_table_t;

typedef struct {
    efi_table_header_t header;
    uint16_t *firmware_vendor;
    uint32_t firmware_revision;
    efi_handle_t console_in_handle;
    void *con_in;
    efi_handle_t console_out_handle;
    efi_text_output_t *con_out;
    efi_handle_t standard_error_handle;
    efi_text_output_t *std_err;
    efi_runtime_services_t *runtime_services;
    efi_boot_services_t *boot_services;
    uint64_t number_of_table_entries;
    efi_configuration_table_t *configuration_table;
} efi_system_table_t;
