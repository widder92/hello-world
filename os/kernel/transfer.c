/*
 * Dateiübertragung als Fenster: links die eigenen Dateien, rechts ein Ordner
 * auf dem Server (über SFTP, siehe sftp.h). Was dauert - verbinden, auflisten,
 * übertragen -, erledigt ein Helfer-Task, damit das Fenster bedienbar bleibt.
 */
#include "apps.h"
#include "console.h"
#include "cpu.h"
#include "fs.h"
#include "gui.h"
#include "input.h"
#include "memory.h"
#include "sftp.h"
#include "storage.h"
#include "string.h"
#include "task.h"
#include "time.h"

/* Das Fenster lässt sich vergrößern: Die beiden Listen wachsen mit. */
#define WIDTH        (app->width)
#define HEIGHT       (app->height)
#define START_WIDTH  900
#define START_HEIGHT 556
#define MARGIN       12
#define FIELD_Y      10
#define FIELD_H      26
#define PANE_W       ((WIDTH - 3 * MARGIN) / 2)
#define LEFT_X       MARGIN
#define RIGHT_X      (MARGIN + PANE_W + 12)
#define HEADER_Y     46
#define LIST_Y       68
#define ROW_H        18
#define BUTTON_H     28
#define STATUS_Y     (HEIGHT - 44)
#define BUTTON_Y     (STATUS_Y - 10 - BUTTON_H)
#define ROWS         ((BUTTON_Y - 10 - LIST_Y) / ROW_H)
#define LIST_H       (ROWS * ROW_H)
#define COLUMNS      (PANE_W / FONT_W - 1)
#define SETTINGS     "transfer.txt"

#define C_BACKGROUND RGB(246, 244, 240)
#define C_TEXT       RGB(34, 38, 48)
#define C_MUTED      RGB(120, 124, 134)
#define C_FIELD      RGB(255, 255, 255)
#define C_BORDER     RGB(190, 192, 198)
#define C_FOCUS      RGB(48, 104, 196)
#define C_SELECTED   RGB(206, 222, 248)
#define C_FOLDER     RGB(150, 100, 20)
#define C_ERROR      RGB(196, 56, 56)
#define C_GOOD       RGB(40, 130, 70)
#define C_BAR        RGB(232, 230, 224)

enum { F_HOST, F_PORT, F_USER, F_PASSWORD, FIELDS, FOCUS_LOCAL = FIELDS, FOCUS_REMOTE };

typedef enum {
    JOB_CONNECT, JOB_LIST, JOB_DOWNLOAD, JOB_UPLOAD, JOB_DELETE, JOB_MKDIR,
} job_t;

typedef enum {
    PROMPT_NONE,
    PROMPT_TRUST,                       /* Helfer wartet: Serverschlüssel annehmen? */
    PROMPT_CONFIRM,                     /* Rückfrage vor einer Aktion */
    PROMPT_NAME,                        /* Name für einen neuen Ordner */
} prompt_t;

typedef struct {
    char name[FS_NAME_MAX];
    size_t size;
    bool saved;
} local_t;

typedef struct {
    window_t *window;
    int width, height;
    bool closing;

    char field[FIELDS][96];
    size_t field_len[FIELDS];
    int focus;

    /* Verbindung - nur der Helfer-Task spricht mit dem Server */
    sftp_t *sftp;
    bool connected;
    char cwd[1024];
    sftp_entry_t *remote;               /* gehört dem Fenster-Task */
    int remote_count, remote_selected, remote_top;
    sftp_entry_t *incoming;             /* vom Helfer bereitgelegt */
    int incoming_count;
    char incoming_cwd[1024];
    bool incoming_ready;

    local_t *local;
    int local_count, local_selected, local_top;

    /* Laufender Auftrag */
    bool busy;
    job_t job;
    char job_name[256];
    bool job_directory;
    uint64_t progress_done, progress_total;
    uint64_t busy_since;

    char status[200];
    bool status_error;

    prompt_t prompt;
    char prompt_line[2][140];
    char prompt_yes[20];
    job_t confirm_job;                  /* was nach "Ja" geschieht */
    bool confirm_local;                 /* ... oder: eigene Datei löschen */
    int trust_answer;                   /* 0 offen, 1 ja, 2 nein */
    char input[64];
    size_t input_len;

    uint64_t last_click;
    int last_click_row, last_click_pane;
} app_t;

static void set_status(app_t *app, bool error, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(app->status, sizeof(app->status), fmt, ap);
    va_end(ap);
    app->status_error = error;
}

/* --- Helfer-Task ------------------------------------------------------------------------- */

static void progress(void *ctx, uint64_t done, uint64_t total)
{
    app_t *app = ctx;
    app->progress_done = done;
    app->progress_total = total;
}

static bool trust(void *ctx, const char *server, const char *type,
                  const char *fingerprint, bool changed)
{
    app_t *app = ctx;
    ksnprintf(app->prompt_line[0], sizeof(app->prompt_line[0]),
              changed ? "ACHTUNG: Der Schlüssel von %s hat sich geändert! Im Zweifel abbrechen."
                      : "Erste Verbindung mit %s. Stimmt der Fingerabdruck (%s)?",
              server, type);
    strlcpy(app->prompt_line[1], fingerprint, sizeof(app->prompt_line[1]));
    strlcpy(app->prompt_yes, changed ? "Trotzdem" : "Vertrauen", sizeof(app->prompt_yes));
    app->trust_answer = 0;
    app->prompt = PROMPT_TRUST;
    while (app->trust_answer == 0 && !app->closing)
        task_sleep(50);
    app->prompt = PROMPT_NONE;
    return app->trust_answer == 1;
}

/* Liest einen Ordner und legt das Ergebnis für den Fenster-Task bereit. */
static bool load_directory(app_t *app, const char *path)
{
    sftp_entry_t *entries;
    int count;
    char real[1024];

    if (!sftp_realpath(app->sftp, path, real, sizeof(real)) ||
        !sftp_list(app->sftp, real, &entries, &count))
        return false;
    while (app->incoming_ready && !app->closing)
        task_sleep(20);                 /* das vorige Ergebnis ist noch nicht abgeholt */
    app->incoming = entries;
    app->incoming_count = count;
    strlcpy(app->incoming_cwd, real, sizeof(app->incoming_cwd));
    app->incoming_ready = true;
    return true;
}

static void remember_server(const app_t *app)
{
    char text[320];
    int n = ksnprintf(text, sizeof(text), "%s\n%s\n%s\n", app->field[F_HOST],
                      app->field[F_PORT], app->field[F_USER]);
    storage_save(STORAGE_SETTINGS, SETTINGS, text, (size_t)n);
}

static void worker(void *arg)
{
    app_t *app = arg;
    char path[1024], error[200];
    bool ok = false;

    switch (app->job) {
    case JOB_CONNECT: {
        uint32_t port = 22;
        if (app->field_len[F_PORT] > 0 &&
            (!parse_uint(app->field[F_PORT], &port) || port == 0 || port > 65535)) {
            set_status(app, true, "Der Port muss eine Zahl zwischen 1 und 65535 sein");
            break;
        }
        ssh_options_t options = {
            .host = app->field[F_HOST], .port = (uint16_t)port,
            .user = app->field[F_USER], .password = app->field[F_PASSWORD],
            .trust = trust, .trust_ctx = app,
        };
        set_status(app, false, "Verbinde mit %s ...", app->field[F_HOST]);
        app->sftp = sftp_connect(&options, error, sizeof(error));
        if (!app->sftp) {
            set_status(app, true, "%s", error);
            break;
        }
        /* Das Passwort wird nicht mehr gebraucht. */
        memset(app->field[F_PASSWORD], 0, sizeof(app->field[F_PASSWORD]));
        app->field_len[F_PASSWORD] = 0;
        ok = load_directory(app, ".");
        if (ok) {
            app->connected = true;
            app->focus = FOCUS_REMOTE;
            remember_server(app);
            set_status(app, false, "Verbunden mit %s", app->field[F_HOST]);
        }
        break;
    }
    case JOB_LIST:
        sftp_join(app->cwd, app->job_name, path, sizeof(path));
        ok = load_directory(app, path);
        if (ok)
            set_status(app, false, "");
        break;
    case JOB_DOWNLOAD: {
        char *data;
        size_t size;
        sftp_join(app->cwd, app->job_name, path, sizeof(path));
        ok = sftp_download(app->sftp, path, FS_FILE_MAX, &data, &size, progress, app);
        if (!ok)
            break;
        int err = fs_write(app->job_name, data, size, false);
        kfree(data);
        if (err != FS_OK) {
            set_status(app, true, "%s ließ sich hier nicht speichern: %s",
                       app->job_name, fs_error_text(err));
            ok = true;                  /* kein Fehler der Verbindung */
            break;
        }
        const file_t *f = fs_find(app->job_name);
        set_status(app, !(f && f->saved), f && f->saved
                       ? "%s geholt (%zu Bytes)"
                       : "%s geholt (%zu Bytes) - aber nur im Arbeitsspeicher, kein Datenträger",
                   app->job_name, size);
        break;
    }
    case JOB_UPLOAD: {
        char *data;
        size_t size;
        if (fs_read(app->job_name, &data, &size) != FS_OK) {
            set_status(app, true, "%s lässt sich nicht lesen", app->job_name);
            ok = true;
            break;
        }
        sftp_join(app->cwd, app->job_name, path, sizeof(path));
        ok = sftp_upload(app->sftp, path, data, size, progress, app);
        kfree(data);
        if (ok) {
            set_status(app, false, "%s hochgeladen (%zu Bytes)", app->job_name, size);
            ok = load_directory(app, app->cwd);
        }
        break;
    }
    case JOB_DELETE:
        sftp_join(app->cwd, app->job_name, path, sizeof(path));
        ok = app->job_directory ? sftp_rmdir(app->sftp, path)
                                : sftp_remove(app->sftp, path);
        if (ok) {
            set_status(app, false, "%s auf dem Server gelöscht", app->job_name);
            ok = load_directory(app, app->cwd);
        } else if (app->job_directory && sftp_alive(app->sftp)) {
            set_status(app, true, "Der Ordner ließ sich nicht löschen - ist er leer?");
            ok = true;
        }
        break;
    case JOB_MKDIR:
        sftp_join(app->cwd, app->job_name, path, sizeof(path));
        ok = sftp_mkdir(app->sftp, path);
        if (ok) {
            set_status(app, false, "Ordner %s angelegt", app->job_name);
            ok = load_directory(app, app->cwd);
        }
        break;
    }

    if (!ok && app->sftp) {
        set_status(app, true, "%s", sftp_error(app->sftp));
        if (!sftp_alive(app->sftp) || !app->connected) {
            sftp_close(app->sftp);
            app->sftp = NULL;
            app->connected = false;
            app->focus = F_HOST;
        }
    }
    app->progress_total = 0;
    app->busy = false;
}

static void start_job(app_t *app, job_t job, const char *name, bool directory)
{
    if (app->busy)
        return;
    app->job = job;
    strlcpy(app->job_name, name ? name : "", sizeof(app->job_name));
    app->job_directory = directory;
    app->progress_done = app->progress_total = 0;
    app->busy = true;
    app->busy_since = time_ms();
    if (task_create("uebertragung", worker, app, NULL) < 0) {
        app->busy = false;
        set_status(app, true, "Zu viele Tasks - bitte etwas schließen");
    }
}

/* --- Listen ----------------------------------------------------------------------------------- */

static void refresh_local(app_t *app)
{
    int n = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        const file_t *f = fs_entry(i);
        if (!f)
            continue;
        uint64_t flags = irq_save();    /* Name und Größe zusammenhängend lesen */
        if (f->used) {
            strlcpy(app->local[n].name, f->name, FS_NAME_MAX);
            app->local[n].size = f->size;
            app->local[n].saved = f->saved || f->builtin;
            n++;
        }
        irq_restore(flags);
    }
    app->local_count = n;
    if (app->local_selected >= n)
        app->local_selected = n ? n - 1 : 0;
}

/* Zeile 0 des Server-Ordners ist ".." (außer ganz oben). */
static bool has_parent(const app_t *app)
{
    return app->connected && strcmp(app->cwd, "/") != 0;
}

static int remote_rows(const app_t *app)
{
    return app->remote_count + (has_parent(app) ? 1 : 0);
}

static const sftp_entry_t *remote_entry(const app_t *app, int row)
{
    if (has_parent(app))
        row--;
    return row >= 0 && row < app->remote_count ? &app->remote[row] : NULL;
}

static void take_incoming(app_t *app)
{
    if (!app->incoming_ready)
        return;
    bool same = strcmp(app->cwd, app->incoming_cwd) == 0;
    kfree(app->remote);
    app->remote = app->incoming;
    app->remote_count = app->incoming_count;
    strlcpy(app->cwd, app->incoming_cwd, sizeof(app->cwd));
    app->incoming = NULL;
    app->incoming_ready = false;
    if (!same) {
        app->remote_selected = 0;
        app->remote_top = 0;
    } else if (app->remote_selected >= remote_rows(app)) {
        app->remote_selected = remote_rows(app) ? remote_rows(app) - 1 : 0;
    }
}

/* Hält die Auswahl im sichtbaren Ausschnitt. */
static void clamp(const app_t *app, int count, int *selected, int *top)
{
    if (*selected >= count)
        *selected = count ? count - 1 : 0;
    if (*selected < 0)
        *selected = 0;
    if (*selected < *top)
        *top = *selected;
    if (*selected >= *top + ROWS)
        *top = *selected - ROWS + 1;
    if (*top < 0)
        *top = 0;
}

/* --- Zeichnen ---------------------------------------------------------------------------------- */

static rect_t field_rect(int i)
{
    static const int x[FIELDS] = { 12, 320, 392, 580 };
    static const int w[FIELDS] = { 300, 64, 180, 180 };
    return rect_make(x[i], FIELD_Y, w[i], FIELD_H);
}

static rect_t connect_rect(void)
{
    return rect_make(768, FIELD_Y, 120, FIELD_H);
}

enum { B_UPLOAD, B_EDIT, B_DELETE_LOCAL, B_DOWNLOAD, B_MKDIR, B_DELETE_REMOTE, B_RELOAD,
       BUTTONS };
static const char *const button_label[BUTTONS] = {
    "Hochladen >>", "Bearbeiten", "Löschen",
    "<< Herunterladen", "Neuer Ordner", "Löschen", "Neu laden",
};

static rect_t button_rect(const app_t *app, int i)
{
    static const int offset[BUTTONS] = { 0, 126, 232, 0, 146, 262, 342 };
    static const int w[BUTTONS] = { 120, 100, 76, 140, 110, 74, 90 };
    return rect_make((i < B_DOWNLOAD ? LEFT_X : RIGHT_X) + offset[i], BUTTON_Y, w[i],
                     BUTTON_H);
}

static rect_t yes_rect(const app_t *app)
{
    return rect_make(WIDTH - MARGIN - 110 - 6 - 110, STATUS_Y, 110, BUTTON_H);
}

static rect_t no_rect(const app_t *app)
{
    return rect_make(WIDTH - MARGIN - 110, STATUS_Y, 110, BUTTON_H);
}

/* Kürzt einen Text auf columns Zeichen; ".." zeigt, dass etwas fehlt. */
static void fit(const char *text, int columns, char *out, size_t size)
{
    size_t n = 0;
    int shown = 0;
    int total = gfx_text_width(text) / FONT_W;
    int limit = total > columns ? columns - 2 : columns;
    while (*text && shown < limit && n + 4 < size) {
        out[n++] = *text++;
        while (((uint8_t)*text & 0xC0) == 0x80 && n + 4 < size)
            out[n++] = *text++;
        shown++;
    }
    if (total > columns) {
        out[n++] = '.';
        out[n++] = '.';
    }
    out[n] = '\0';
}

static void format_size(uint64_t bytes, char *out, size_t size)
{
    if (bytes < 10000)
        ksnprintf(out, size, "%llu B", bytes);
    else if (bytes < 10 * 1024 * 1024)
        ksnprintf(out, size, "%llu KB", bytes / 1024);
    else
        ksnprintf(out, size, "%llu MB", bytes / (1024 * 1024));
}

static void draw_row(const app_t *app, surface_t *s, int x, int row, const char *name,
                     const char *detail, bool selected, bool focused, color_t color)
{
    char shown[200];
    int y = LIST_Y + row * ROW_H;
    if (selected)
        gfx_fill(s, x + 1, y, PANE_W - 2, ROW_H, focused ? C_SELECTED : C_BAR);
    fit(name, COLUMNS - 11, shown, sizeof(shown));
    gfx_text(s, x + 6, y + 1, shown, color, false);
    gfx_text(s, x + PANE_W - 6 - gfx_text_width(detail), y + 1, detail, C_MUTED, false);
}

static void draw_pane(const app_t *app, surface_t *s, int x, const char *title,
                      bool focused)
{
    char shown[200];
    fit(title, COLUMNS, shown, sizeof(shown));
    gfx_text(s, x + 2, HEADER_Y, shown, C_TEXT, true);
    gfx_fill(s, x, LIST_Y - 2, PANE_W, LIST_H + 4, C_FIELD);
    gfx_frame(s, x, LIST_Y - 2, PANE_W, LIST_H + 4, focused ? C_FOCUS : C_BORDER);
}

static void draw(app_t *app)
{
    static const char *const hint[FIELDS] = { "Server", "Port 22", "Benutzer", "Passwort" };
    surface_t *s = window_surface(app->window);
    char text[240], detail[32];

    gfx_fill(s, 0, 0, WIDTH, HEIGHT, C_BACKGROUND);

    /* Verbindungszeile */
    for (int i = 0; i < FIELDS; i++) {
        rect_t r = field_rect(i);
        bool focused = app->focus == i && !app->connected;
        gfx_fill(s, r.x, r.y, r.w, r.h, app->connected ? C_BAR : C_FIELD);
        gfx_frame(s, r.x, r.y, r.w, r.h, focused ? C_FOCUS : C_BORDER);
        if (app->field_len[i] == 0 && !focused) {
            gfx_text(s, r.x + 6, r.y + 5, i == F_PASSWORD && app->connected ? "" : hint[i],
                     C_MUTED, false);
            continue;
        }
        if (i == F_PASSWORD) {
            size_t n = 0;
            for (const char *p = app->field[i]; *p && n + 1 < sizeof(text); p++) {
                if (((uint8_t)*p & 0xC0) != 0x80)
                    text[n++] = '*';
            }
            text[n] = '\0';
        } else {
            strlcpy(text, app->field[i], sizeof(text));
        }
        /* Passt der Text nicht ins Feld, ist sein Ende zu sehen. */
        const char *shown = text;
        int columns = (r.w - 14) / FONT_W;
        for (int extra = gfx_text_width(shown) / FONT_W - columns; extra > 0; extra--) {
            shown++;
            while (((uint8_t)*shown & 0xC0) == 0x80)
                shown++;
        }
        int end = gfx_text(s, r.x + 6, r.y + 5, shown, C_TEXT, false);
        if (focused)
            gfx_fill(s, end + 1, r.y + 5, 2, FONT_H, C_TEXT);
    }
    ui_button(s, connect_rect(), app->connected ? "Trennen" : "Verbinden", !app->connected);

    /* Links: eigene Dateien */
    draw_pane(app, s, LEFT_X, storage_available() ? "Eigene Dateien"
                                             : "Eigene Dateien (nur Arbeitsspeicher!)",
              app->focus == FOCUS_LOCAL);
    clamp(app, app->local_count, &app->local_selected, &app->local_top);
    for (int row = 0; row < ROWS && app->local_top + row < app->local_count; row++) {
        const local_t *f = &app->local[app->local_top + row];
        format_size(f->size, detail, sizeof(detail));
        draw_row(app, s, LEFT_X, row, f->name, detail,
                 app->local_top + row == app->local_selected,
                 app->focus == FOCUS_LOCAL, f->saved ? C_TEXT : C_ERROR);
    }

    /* Rechts: der Server */
    if (app->connected)
        ksnprintf(text, sizeof(text), "Server: %s", app->cwd);
    else
        strlcpy(text, "Server (nicht verbunden)", sizeof(text));
    draw_pane(app, s, RIGHT_X, text, app->focus == FOCUS_REMOTE);
    if (app->connected) {
        int rows = remote_rows(app);
        clamp(app, rows, &app->remote_selected, &app->remote_top);
        for (int row = 0; row < ROWS && app->remote_top + row < rows; row++) {
            int index = app->remote_top + row;
            const sftp_entry_t *e = remote_entry(app, index);
            bool selected = index == app->remote_selected;
            if (!e) {
                draw_row(app, s, RIGHT_X, row, "..", "eine Ebene höher", selected,
                         app->focus == FOCUS_REMOTE, C_FOLDER);
            } else if (e->directory) {
                draw_row(app, s, RIGHT_X, row, e->name, "Ordner", selected,
                         app->focus == FOCUS_REMOTE, C_FOLDER);
            } else {
                format_size(e->size, detail, sizeof(detail));
                draw_row(app, s, RIGHT_X, row, e->name, detail, selected,
                         app->focus == FOCUS_REMOTE, C_TEXT);
            }
        }
    } else {
        gfx_text(s, RIGHT_X + 12, LIST_Y + 12,
                 "Oben Server, Benutzer und Passwort eintragen,", C_MUTED, false);
        gfx_text(s, RIGHT_X + 12, LIST_Y + 12 + ROW_H,
                 "dann \"Verbinden\" (oder Enter).", C_MUTED, false);
        gfx_text(s, RIGHT_X + 12, LIST_Y + 12 + 3 * ROW_H,
                 "Die Verbindung läuft über SFTP - verschlüsselt.", C_MUTED, false);
    }

    for (int i = 0; i < BUTTONS; i++)
        ui_button(s, button_rect(app, i), button_label[i], false);

    /* Unten: Rückfrage oder Statuszeile */
    gfx_fill(s, 0, STATUS_Y - 6, WIDTH, HEIGHT - STATUS_Y + 6, C_BAR);
    if (app->prompt == PROMPT_NAME) {
        ksnprintf(text, sizeof(text), "Name des neuen Ordners: %s", app->input);
        int end = gfx_text(s, MARGIN, STATUS_Y + 6, text, C_TEXT, true);
        gfx_fill(s, end + 1, STATUS_Y + 6, 2, FONT_H, C_TEXT);
        gfx_text(s, WIDTH - MARGIN - 28 * FONT_W, STATUS_Y + 6,
                 "Enter anlegen   Esc abbrechen", C_MUTED, false);
    } else if (app->prompt != PROMPT_NONE) {
        gfx_text(s, MARGIN, STATUS_Y - 2, app->prompt_line[0], C_TEXT, true);
        gfx_text(s, MARGIN, STATUS_Y + 16, app->prompt_line[1], C_TEXT, false);
        ui_button(s, yes_rect(app), app->prompt_yes, true);
        ui_button(s, no_rect(app), "Abbrechen", false);
    } else if (app->busy && app->progress_total > 0) {
        int percent = (int)(app->progress_done * 100 / app->progress_total);
        ksnprintf(text, sizeof(text), "%s: %d %%", app->job_name, percent);
        gfx_text(s, MARGIN, STATUS_Y + 6, text, C_TEXT, false);
        int bar = WIDTH - 400 - MARGIN;
        gfx_frame(s, 400, STATUS_Y + 8, bar, 12, C_BORDER);
        gfx_fill(s, 401, STATUS_Y + 9, (bar - 2) * percent / 100, 10, C_FOCUS);
    } else if (app->busy) {
        ksnprintf(text, sizeof(text), "%s  (%llu s)",
                  app->status[0] ? app->status : "Einen Moment ...",
                  (time_ms() - app->busy_since) / 1000);
        gfx_text(s, MARGIN, STATUS_Y + 6, text, C_TEXT, false);
    } else {
        fit(app->status[0] ? app->status
                           : "Tab wechselt die Seite, Enter öffnet, Entf löscht.",
            (WIDTH - 2 * MARGIN) / FONT_W, text, sizeof(text));
        gfx_text(s, MARGIN, STATUS_Y + 6, text,
                 !app->status[0] ? C_MUTED : app->status_error ? C_ERROR : C_GOOD, false);
    }
    window_invalidate(app->window, 0, 0, WIDTH, HEIGHT);
}

/* --- Aktionen ---------------------------------------------------------------------------------- */

static const local_t *selected_local(const app_t *app)
{
    return app->local_selected < app->local_count ? &app->local[app->local_selected]
                                                  : NULL;
}

static void ask(app_t *app, job_t job, const char *yes, const char *line0,
                const char *line1)
{
    strlcpy(app->prompt_line[0], line0, sizeof(app->prompt_line[0]));
    strlcpy(app->prompt_line[1], line1, sizeof(app->prompt_line[1]));
    strlcpy(app->prompt_yes, yes, sizeof(app->prompt_yes));
    app->confirm_job = job;
    app->confirm_local = false;
    app->job_directory = false;
    app->prompt = PROMPT_CONFIRM;
}

static bool need_connection(app_t *app)
{
    if (!app->connected)
        set_status(app, true, "Erst mit einem Server verbinden");
    return app->connected;
}

static void action_connect(app_t *app)
{
    if (app->connected) {
        /* Trennen: Der Helfer ist nicht beschäftigt, also darf das Fenster schließen. */
        sftp_close(app->sftp);
        app->sftp = NULL;
        app->connected = false;
        app->focus = F_HOST;
        set_status(app, false, "Verbindung getrennt");
    } else if (app->field_len[F_HOST] == 0 || app->field_len[F_USER] == 0) {
        set_status(app, true, "Server und Benutzer müssen eingetragen sein");
    } else {
        start_job(app, JOB_CONNECT, NULL, false);
    }
}

static void action_upload(app_t *app)
{
    const local_t *f = selected_local(app);
    char line[140];
    if (!need_connection(app) || !f)
        return;
    strlcpy(app->job_name, f->name, sizeof(app->job_name));
    for (int i = 0; i < app->remote_count; i++) {
        if (strcmp(app->remote[i].name, f->name) == 0) {
            ksnprintf(line, sizeof(line), "%s gibt es auf dem Server schon.", f->name);
            ask(app, JOB_UPLOAD, "Ersetzen", line,
                "Die Datei dort wird überschrieben - das lässt sich nicht rückgängig machen.");
            return;
        }
    }
    start_job(app, JOB_UPLOAD, f->name, false);
}

static void action_download(app_t *app)
{
    const sftp_entry_t *e = remote_entry(app, app->remote_selected);
    char line[140];
    if (!need_connection(app) || !e)
        return;
    if (e->directory) {
        set_status(app, true, "Ordner lassen sich nicht als Ganzes holen - nur einzelne Dateien");
        return;
    }
    strlcpy(app->job_name, e->name, sizeof(app->job_name));
    if (fs_find(e->name)) {
        ksnprintf(line, sizeof(line), "%s gibt es bei den eigenen Dateien schon.", e->name);
        ask(app, JOB_DOWNLOAD, "Ersetzen", line, "Die eigene Fassung wird überschrieben.");
        return;
    }
    start_job(app, JOB_DOWNLOAD, e->name, false);
}

static void action_open_remote(app_t *app)
{
    const sftp_entry_t *e = remote_entry(app, app->remote_selected);
    if (!app->connected || app->remote_selected >= remote_rows(app))
        return;
    if (!e)
        start_job(app, JOB_LIST, "..", true);
    else if (e->directory)
        start_job(app, JOB_LIST, e->name, true);
    else
        action_download(app);
}

static void action_edit(app_t *app)
{
    const local_t *f = selected_local(app);
    if (f && !gui_launch_with("editor", f->name))
        set_status(app, true, "Der Editor ließ sich nicht starten");
}

static void action_delete_local(app_t *app)
{
    const local_t *f = selected_local(app);
    char line[140];
    if (!f)
        return;
    strlcpy(app->job_name, f->name, sizeof(app->job_name));
    ksnprintf(line, sizeof(line), "Eigene Datei %s löschen?", f->name);
    ask(app, JOB_DELETE, "Löschen", line, "Sie wird auch vom Datenträger entfernt.");
    app->confirm_local = true;
}

static void action_delete_remote(app_t *app)
{
    const sftp_entry_t *e = remote_entry(app, app->remote_selected);
    char line[140];
    if (!need_connection(app) || !e)
        return;
    strlcpy(app->job_name, e->name, sizeof(app->job_name));
    ksnprintf(line, sizeof(line), "%s %s auf dem Server löschen?",
              e->directory ? "Ordner" : "Datei", e->name);
    ask(app, JOB_DELETE, "Löschen", line, "Das lässt sich nicht rückgängig machen.");
    app->job_directory = e->directory;
}

/* Der Benutzer hat die Rückfrage bejaht. */
static void confirmed(app_t *app)
{
    app->prompt = PROMPT_NONE;
    if (app->confirm_local) {
        if (fs_remove(app->job_name) == FS_OK)
            set_status(app, false, "%s gelöscht", app->job_name);
        else
            set_status(app, true, "%s ließ sich nicht löschen", app->job_name);
        return;
    }
    char name[256];
    strlcpy(name, app->job_name, sizeof(name));
    start_job(app, app->confirm_job, name, app->job_directory);
}

static void button_pressed(app_t *app, int button)
{
    switch (button) {
    case B_UPLOAD:        action_upload(app); break;
    case B_EDIT:          action_edit(app); break;
    case B_DELETE_LOCAL:  action_delete_local(app); break;
    case B_DOWNLOAD:      action_download(app); break;
    case B_DELETE_REMOTE: action_delete_remote(app); break;
    case B_RELOAD:
        if (need_connection(app))
            start_job(app, JOB_LIST, ".", true);
        break;
    case B_MKDIR:
        if (need_connection(app)) {
            app->prompt = PROMPT_NAME;
            app->input[0] = '\0';
            app->input_len = 0;
        }
        break;
    }
}

/* Tippen in ein Textfeld; liefert false, wenn die Taste nichts mit Text zu tun hat. */
static bool edit_text(char *text, size_t *len, size_t size, int key, bool spaces)
{
    if (key == '\b') {
        while (*len > 0 && ((uint8_t)text[*len - 1] & 0xC0) == 0x80)
            (*len)--;
        if (*len > 0)
            (*len)--;
    } else if (key >= (spaces ? 32 : 33) && key < 0x800 && key != 127) {
        if (*len + 3 >= size)
            return true;
        if (key < 0x80) {
            text[(*len)++] = (char)key;
        } else {
            text[(*len)++] = (char)(0xC0 | (key >> 6));
            text[(*len)++] = (char)(0x80 | (key & 0x3F));
        }
    } else {
        return false;
    }
    text[*len] = '\0';
    return true;
}

static void key_pressed(app_t *app, int key, int mods)
{
    if (app->prompt == PROMPT_TRUST || app->prompt == PROMPT_CONFIRM) {
        bool yes = key == 'j' || key == 'J';
        bool no = key == 'n' || key == 'N' || key == 27;
        if (app->prompt == PROMPT_TRUST && (yes || no))
            app->trust_answer = yes ? 1 : 2;
        else if (yes)
            confirmed(app);
        else if (no)
            app->prompt = PROMPT_NONE;
        return;
    }
    if (app->prompt == PROMPT_NAME) {
        if (key == 27) {
            app->prompt = PROMPT_NONE;
        } else if (key == '\n') {
            app->prompt = PROMPT_NONE;
            if (app->input_len > 0)
                start_job(app, JOB_MKDIR, app->input, true);
        } else if (key != '/') {
            edit_text(app->input, &app->input_len, sizeof(app->input), key, true);
        }
        return;
    }
    if (app->busy)
        return;

    if (key == '\t') {
        if (app->connected)
            app->focus = app->focus == FOCUS_LOCAL ? FOCUS_REMOTE : FOCUS_LOCAL;
        else if (mods & MOD_SHIFT)
            app->focus = app->focus == 0 ? FOCUS_LOCAL : app->focus - 1;
        else
            app->focus = app->focus >= FOCUS_LOCAL ? 0 : app->focus + 1;
        return;
    }
    if (app->focus < FIELDS) {
        if (key == '\n')
            action_connect(app);
        else
            edit_text(app->field[app->focus], &app->field_len[app->focus],
                      sizeof(app->field[0]), key, app->focus == F_PASSWORD);
        return;
    }

    bool local = app->focus == FOCUS_LOCAL;
    int *selected = local ? &app->local_selected : &app->remote_selected;
    int count = local ? app->local_count : remote_rows(app);
    switch (key) {
    case KEY_UP:        (*selected)--; break;
    case KEY_DOWN:      (*selected)++; break;
    case KEY_PAGE_UP:   *selected -= ROWS - 1; break;
    case KEY_PAGE_DOWN: *selected += ROWS - 1; break;
    case KEY_HOME:      *selected = 0; break;
    case KEY_END:       *selected = count - 1; break;
    case KEY_LEFT:      app->focus = FOCUS_LOCAL; break;
    case KEY_RIGHT:     app->focus = FOCUS_REMOTE; break;
    case KEY_DELETE:
        if (local)
            action_delete_local(app);
        else
            action_delete_remote(app);
        break;
    case '\n':
        if (local)
            action_edit(app);
        else
            action_open_remote(app);
        break;
    default:
        break;
    }
    if (*selected < 0)
        *selected = 0;
    if (*selected >= count)
        *selected = count ? count - 1 : 0;
}

static void mouse_pressed(app_t *app, int x, int y)
{
    if (app->prompt == PROMPT_TRUST || app->prompt == PROMPT_CONFIRM) {
        bool yes = rect_contains(yes_rect(app), x, y);
        bool no = rect_contains(no_rect(app), x, y);
        if (app->prompt == PROMPT_TRUST && (yes || no))
            app->trust_answer = yes ? 1 : 2;
        else if (yes)
            confirmed(app);
        else if (no)
            app->prompt = PROMPT_NONE;
        return;
    }
    if (app->busy || app->prompt != PROMPT_NONE)
        return;

    if (rect_contains(connect_rect(), x, y)) {
        action_connect(app);
        return;
    }
    for (int i = 0; i < FIELDS && !app->connected; i++) {
        if (rect_contains(field_rect(i), x, y)) {
            app->focus = i;
            return;
        }
    }
    for (int i = 0; i < BUTTONS; i++) {
        if (rect_contains(button_rect(app, i), x, y)) {
            button_pressed(app, i);
            return;
        }
    }

    /* Klick in eine Liste: auswählen; zwei Klicks kurz hintereinander öffnen */
    if (y < LIST_Y || y >= LIST_Y + LIST_H)
        return;
    int pane = x >= LEFT_X && x < LEFT_X + PANE_W ? FOCUS_LOCAL
             : x >= RIGHT_X && x < RIGHT_X + PANE_W ? FOCUS_REMOTE : -1;
    if (pane < 0)
        return;
    int row = (y - LIST_Y) / ROW_H;
    int index = (pane == FOCUS_LOCAL ? app->local_top : app->remote_top) + row;
    int count = pane == FOCUS_LOCAL ? app->local_count : remote_rows(app);
    app->focus = pane;
    if (index >= count)
        return;
    if (pane == FOCUS_LOCAL)
        app->local_selected = index;
    else
        app->remote_selected = index;

    uint64_t now = time_ms();
    bool twice = app->last_click_pane == pane && app->last_click_row == index &&
                 now - app->last_click < 450;
    app->last_click = twice ? 0 : now;
    app->last_click_pane = pane;
    app->last_click_row = index;
    if (twice) {
        if (pane == FOCUS_LOCAL)
            action_edit(app);
        else
            action_open_remote(app);
    }
}

/* Server und Benutzer der letzten Verbindung vorbelegen (nie das Passwort). */
static void load_settings(app_t *app)
{
    char *text;
    size_t size;
    if (!storage_load(STORAGE_SETTINGS, SETTINGS, 512, &text, &size))
        return;
    const char *p = text;
    for (int i = 0; i < F_PASSWORD; i++) {
        size_t n = 0;
        while (*p && *p != '\n' && n + 1 < sizeof(app->field[i])) {
            if ((uint8_t)*p > 32)
                app->field[i][n++] = *p;
            p++;
        }
        app->field[i][n] = '\0';
        app->field_len[i] = n;
        while (*p && *p != '\n')
            p++;
        if (*p == '\n')
            p++;
    }
    kfree(text);
    if (app->field_len[F_HOST] && app->field_len[F_USER])
        app->focus = F_PASSWORD;
}

void transfer_app(void *arg)
{
    (void)arg;
    app_t *app = kzalloc(sizeof(*app));
    if (app) {
        app->width = START_WIDTH;
        app->height = START_HEIGHT;
        app->local = kmalloc(FS_MAX_FILES * sizeof(local_t));
        app->window = app->local ? window_create("Dateien übertragen", WIDTH, HEIGHT) : NULL;
    }
    if (!app || !app->window) {
        if (app)
            kfree(app->local);
        kfree(app);
        return;
    }
    /* Kleiner als zu Beginn passt die Kopfzeile nicht mehr hinein. */
    window_set_resizable(app->window, START_WIDTH, 300);
    load_settings(app);

    for (;;) {
        take_incoming(app);
        refresh_local(app);
        draw(app);

        event_t ev;
        if (!window_get_event(app->window, &ev, app->busy ? 200 : 1000))
            continue;                   /* regelmäßig neu zeichnen: Fortschritt, neue Dateien */
        if (ev.type == EV_CLOSE)
            break;
        if (ev.type == EV_RESIZE && window_resize(app->window, ev.x, ev.y)) {
            app->width = ev.x;
            app->height = ev.y;
        }
        if (ev.type == EV_KEY)
            key_pressed(app, ev.key, ev.mods);
        else if (ev.type == EV_MOUSE_DOWN)
            mouse_pressed(app, ev.x, ev.y);
    }

    /* Fenster sofort schließen; auf einen noch laufenden Helfer aber warten. */
    app->closing = true;
    window_destroy(app->window);
    while (app->busy)
        task_sleep(100);
    sftp_close(app->sftp);
    memset(app->field[F_PASSWORD], 0, sizeof(app->field[F_PASSWORD]));
    kfree(app->incoming);
    kfree(app->remote);
    kfree(app->local);
    kfree(app);
}
