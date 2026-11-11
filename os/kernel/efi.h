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
#define EFI_NOT_FOUND        (0x8000000000000000ull | 14)

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

/* --- Tastatur der Firmware (nur für die Rückfrage des Start-Assistenten) ----- */

typedef struct {
    uint16_t scan_code;
    uint16_t character;
} efi_input_key_t;

typedef struct efi_text_input {
    void *reset;
    efi_status_t (*read_key)(struct efi_text_input *self, efi_input_key_t *key);
    void *wait_for_key;
} efi_text_input_t;

/* --- Dateien über die Firmware (nur für den Start-Assistenten) ---------------- */

#define EFI_LOADED_IMAGE_GUID \
    { 0x5b1b31a1, 0x9562, 0x11d2, { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_DEVICE_PATH_GUID \
    { 0x09576e91, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_FILE_SYSTEM_GUID \
    { 0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_FILE_INFO_GUID \
    { 0x09576e92, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_VOLUME_LABEL_GUID \
    { 0xdb47d7d3, 0xfe81, 0x11d3, { 0x9a, 0x35, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }
#define EFI_PCI_IO_GUID \
    { 0x4cf5b200, 0x68b8, 0x4ca5, { 0x9e, 0xec, 0xb2, 0x3e, 0x3f, 0x50, 0x02, 0x9a } }
#define EFI_GLOBAL_VARIABLE_GUID \
    { 0x8be4df61, 0x93ca, 0x11d2, { 0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c } }

typedef struct {
    uint32_t revision;
    efi_handle_t parent;
    void *system_table;
    efi_handle_t device;                /* Datenträger, von dem das Programm geladen wurde */
    void *file_path;
    void *reserved;
    uint32_t load_options_size;
    void *load_options;
    void *image_base;
    uint64_t image_size;
} efi_loaded_image_t;

/* Ein Glied eines "Gerätepfads": beschreibt, wie die Firmware etwas erreicht. */
typedef struct {
    uint8_t type, subtype;
    uint8_t length[2];
} efi_device_path_t;

#define EFI_FILE_MODE_READ   0x0000000000000001ull
#define EFI_FILE_MODE_WRITE  0x0000000000000002ull
#define EFI_FILE_MODE_CREATE 0x8000000000000000ull
#define EFI_FILE_DIRECTORY   0x0000000000000010ull

typedef struct efi_file {
    uint64_t revision;
    efi_status_t (*open)(struct efi_file *self, struct efi_file **result,
                         const uint16_t *name, uint64_t mode, uint64_t attributes);
    efi_status_t (*close)(struct efi_file *self);
    efi_status_t (*remove)(struct efi_file *self);
    efi_status_t (*read)(struct efi_file *self, uint64_t *size, void *buffer);
    efi_status_t (*write)(struct efi_file *self, uint64_t *size, const void *buffer);
    void *get_position;
    void *set_position;
    efi_status_t (*get_info)(struct efi_file *self, const efi_guid_t *type,
                             uint64_t *size, void *buffer);
    void *set_info;
    efi_status_t (*flush)(struct efi_file *self);
} efi_file_t;

typedef struct efi_file_system {
    uint64_t revision;
    efi_status_t (*open_volume)(struct efi_file_system *self, efi_file_t **root);
} efi_file_system_t;

typedef struct {
    uint64_t size;                      /* Größe dieser Auskunft */
    uint64_t file_size;
    uint64_t physical_size;
    efi_time_t created, accessed, modified;
    uint64_t attribute;
    uint16_t name[1];
} efi_file_info_t;

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
    efi_status_t (*allocate_pool)(uint32_t memory_type, uint64_t size, void **buffer);
    efi_status_t (*free_pool)(void *buffer);
    void *create_event;
    void *set_timer;
    void *wait_for_event;
    void *signal_event;
    void *close_event;
    void *check_event;
    void *install_protocol_interface;
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    efi_status_t (*handle_protocol)(efi_handle_t handle, const efi_guid_t *protocol,
                                    void **interface);
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
    efi_status_t (*connect_controller)(efi_handle_t controller, efi_handle_t *driver,
                                       void *remaining_path, uint8_t recursive);
    void *disconnect_controller;
    void *open_protocol;
    void *close_protocol;
    void *open_protocol_information;
    void *protocols_per_handle;
    efi_status_t (*locate_handle_buffer)(uint32_t search_type, const efi_guid_t *protocol,
                                         void *search_key, uint64_t *count,
                                         efi_handle_t **buffer);
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
    efi_status_t (*get_variable)(const uint16_t *name, const efi_guid_t *vendor,
                                 uint32_t *attributes, uint64_t *size, void *data);
    void *get_next_variable_name;
    efi_status_t (*set_variable)(const uint16_t *name, const efi_guid_t *vendor,
                                 uint32_t attributes, uint64_t size, const void *data);
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
    struct efi_text_input *con_in;
    efi_handle_t console_out_handle;
    efi_text_output_t *con_out;
    efi_handle_t standard_error_handle;
    efi_text_output_t *std_err;
    efi_runtime_services_t *runtime_services;
    efi_boot_services_t *boot_services;
    uint64_t number_of_table_entries;
    efi_configuration_table_t *configuration_table;
} efi_system_table_t;
