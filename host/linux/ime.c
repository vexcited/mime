/*
 * the input method of the desktop. with a text input protocol sdl reports the input method's composition as
 * editing text and what it commits as text, and tells it where the caret is. a composition becomes
 * MIME_INPUT_IME events for the guest, whose window manager sends the WM_IME_* messages of the input method
 * manager; the text itself waits in a queue until the guest takes it, since an event has no room for it.
 *
 * an input method clears its preedit text and commits in one step, which sdl reports as editing text that is
 * empty and then text; a clear that no text follows is a cancelled composition. the clear is held back until the
 * next event shows which it is. typed text with no composition at all is the characters of MIME_INPUT_CHAR.
 */
#include "ui_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct Text {
    struct Text *next;
    size_t count;
    uint16_t chars[];
} Text;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Text *g_head, *g_tail;
static bool g_composing;       /* a composition is up, the guest knows of it */
static bool g_clear_held;      /* its text went empty and nothing has said why */
static uint32_t g_window;      /* the window it is for */

static void queue_text(const uint16_t *chars, size_t count)
{
    Text *t = malloc(sizeof(*t) + count * sizeof(uint16_t));
    if (!t)
        return;
    t->next = nullptr;
    t->count = count;
    memcpy(t->chars, chars, count * sizeof(uint16_t));
    pthread_mutex_lock(&g_lock);
    if (g_tail)
        g_tail->next = t;
    else
        g_head = t;
    g_tail = t;
    pthread_mutex_unlock(&g_lock);
}

static void drop_texts(void)
{
    pthread_mutex_lock(&g_lock);
    while (g_head) {
        Text *t = g_head;
        g_head = t->next;
        free(t);
    }
    g_tail = nullptr;
    pthread_mutex_unlock(&g_lock);
}

uint64_t mime_ui_ime_text(uint16_t *dst, size_t capacity, uint64_t *length)
{
    pthread_mutex_lock(&g_lock);
    Text *t = g_head;
    if (t) {
        g_head = t->next;
        if (!g_head)
            g_tail = nullptr;
    }
    pthread_mutex_unlock(&g_lock);
    *length = 0;
    if (!t)
        return MIME_HC_STATUS_NOT_FOUND;
    *length = t->count;
    if (dst && capacity)
        memcpy(dst, t->chars, MIME_MIN(capacity, t->count) * sizeof(uint16_t));
    free(t);
    return MIME_HC_STATUS_OK;
}

static void emit_ime(uint32_t flags, int32_t x, int32_t y)
{
    MimeInputEvent ev = {.type = MIME_INPUT_IME, .window_id = g_window, .flags = flags, .x = x, .y = y};
    mime_ui_emit(&ev);
}

/* the utf-16 position of the n-th character of utf-8 text, -1 staying -1. */
static int32_t units_before(const char *text, int chars)
{
    if (chars < 0)
        return -1;
    int32_t units = 0;
    const unsigned char *p = (const unsigned char *)text;
    for (int i = 0; i < chars && *p; i++) {
        unsigned char c = *p;
        int n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        units += n == 4 ? 2 : 1;
        for (int k = 0; k < n && *p; k++)
            p++;
    }
    return units;
}

/* editing text: start and length are in characters, -1 where unset. main thread. */
void mime_ime_editing(uint32_t window, const char *text, int start, int length)
{
    if (!text || !*text) {
        /* wait to see whether text follows. */
        if (g_composing)
            g_clear_held = true;
        return;
    }
    size_t max = strlen(text) + 1;
    uint16_t *units = malloc(max * sizeof(uint16_t));
    if (!units)
        return;
    size_t n = mime_utf8_to_utf16(units, max, text);
    queue_text(units, n);
    free(units);
    int32_t begin = units_before(text, start);
    int32_t end = start >= 0 && length >= 0 ? units_before(text, start + length) : -1;
    g_composing = true;
    g_clear_held = false;
    g_window = window;
    emit_ime(MIME_IME_PREEDIT, begin, end);
}

/* text the input method commits; true when it ends a composition, else the caller types it. */
bool mime_ime_commit(uint32_t window, const char *text)
{
    if (!g_composing)
        return false;
    size_t max = strlen(text) + 1;
    uint16_t *units = malloc(max * sizeof(uint16_t));
    if (!units)
        return false;
    size_t n = mime_utf8_to_utf16(units, max, text);
    queue_text(units, n);
    free(units);
    g_composing = false;
    g_clear_held = false;
    g_window = window;
    emit_ime(MIME_IME_COMMIT, 0, 0);
    return true;
}

/* a held clear that no text followed was a cancelled composition. */
void mime_ime_flush(void)
{
    if (!g_clear_held)
        return;
    g_clear_held = false;
    g_composing = false;
    drop_texts();
    emit_ime(MIME_IME_CANCELLED, 0, 0);
}

bool mime_ime_pending(void)
{
    return g_clear_held;
}

typedef struct Area {
    uint32_t window;
    MimeRect area;
    int32_t cursor;
} Area;

static void area_main(void *arg)
{
    Area *a = arg;
    mime_ui_set_text_area(a->window, a->area, a->cursor);
    free(a);
}

uint64_t mime_ui_ime_area(uint32_t window, const MimeRect *area, int32_t cursor)
{
    if (!mime_ui_has_video() || !mime_ui_has_text_input())
        return MIME_HC_STATUS_OK;
    Area *a = malloc(sizeof(*a));
    if (!a)
        return MIME_HC_STATUS_NO_MEMORY;
    *a = (Area){window, *area, cursor};
    if (!mime_ui_call_main(area_main, a))
        free(a);
    return MIME_HC_STATUS_OK;
}

static void cancel_main(void *arg)
{
    uint32_t window = (uint32_t)(uintptr_t)arg;
    drop_texts();
    g_composing = false;
    g_clear_held = false;
    mime_ui_clear_composition(window);
}

uint64_t mime_ui_ime_control(uint32_t op, uint32_t window, uint64_t *value)
{
    *value = 0;
    switch (op) {
    case MIME_IME_QUERY:
        *value = mime_ui_has_video() && mime_ui_has_text_input();
        return MIME_HC_STATUS_OK;
    case MIME_IME_CANCEL:
        if (mime_ui_has_video())
            mime_ui_call_main(cancel_main, (void *)(uintptr_t)window);
        return MIME_HC_STATUS_OK;
    }
    return MIME_HC_STATUS_INVALID;
}
