/*
 * Windows.UI.ViewManagement.UISettings: ui metrics, colors and text scale, read from user32 and the registry on
 * demand. change events are accepted but never fire, since nothing changes these settings while programs run.
 */
#include "combase_private.h"
#include <mime/win32/winuser.h>

const WCHAR CbUISettingsClass[] = L"Windows.UI.ViewManagement.UISettings";

MIME_DEFINE_GUID(IID_IUISettings, 0x85361600, 0x1C63, 0x4627, 0xBC, 0xB1, 0x3A, 0x89, 0xE0, 0xBC, 0x9C, 0x55);
MIME_DEFINE_GUID(IID_IUISettings2, 0xBAD82401, 0x2721, 0x44F9, 0xBB, 0x91, 0x2B, 0xB2, 0x28, 0xBE, 0x44, 0x2F);
MIME_DEFINE_GUID(IID_IUISettings3, 0x03021BE4, 0x5254, 0x4781, 0x81, 0x94, 0x51, 0x68, 0xF7, 0xD0, 0x6D, 0x7B);

typedef struct {
    float Width, Height;
} UI_SIZE;

typedef struct {
    BYTE A, R, G, B;
} UI_COLOR;

typedef struct {
    INT64 Value;
} EVENT_TOKEN;

typedef struct UI_SETTINGS UI_SETTINGS;

/* every interface starts with the IInspectable methods, then its own. */
#define INSPECTABLE_METHODS(type)                                                                                     \
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(type *This, REFIID riid, void **object);                               \
    ULONG(STDMETHODCALLTYPE *AddRef)(type *This);                                                                     \
    ULONG(STDMETHODCALLTYPE *Release)(type *This);                                                                    \
    HRESULT(STDMETHODCALLTYPE *GetIids)(type *This, ULONG *count, IID **iids);                                        \
    HRESULT(STDMETHODCALLTYPE *GetRuntimeClassName)(type *This, HSTRING *name);                                       \
    HRESULT(STDMETHODCALLTYPE *GetTrustLevel)(type *This, TrustLevel *level);

typedef struct SETTINGS1_VTBL SETTINGS1_VTBL;
typedef struct SETTINGS2_VTBL SETTINGS2_VTBL;
typedef struct SETTINGS3_VTBL SETTINGS3_VTBL;
typedef struct {
    const SETTINGS1_VTBL *lpVtbl;
} SETTINGS1;
typedef struct {
    const SETTINGS2_VTBL *lpVtbl;
} SETTINGS2;
typedef struct {
    const SETTINGS3_VTBL *lpVtbl;
} SETTINGS3;

struct SETTINGS1_VTBL {
    INSPECTABLE_METHODS(SETTINGS1)
    HRESULT(STDMETHODCALLTYPE *get_HandPreference)(SETTINGS1 *This, INT32 *value);
    HRESULT(STDMETHODCALLTYPE *get_CursorSize)(SETTINGS1 *This, UI_SIZE *value);
    HRESULT(STDMETHODCALLTYPE *get_ScrollBarSize)(SETTINGS1 *This, UI_SIZE *value);
    HRESULT(STDMETHODCALLTYPE *get_ScrollBarArrowSize)(SETTINGS1 *This, UI_SIZE *value);
    HRESULT(STDMETHODCALLTYPE *get_ScrollBarThumbBoxSize)(SETTINGS1 *This, UI_SIZE *value);
    HRESULT(STDMETHODCALLTYPE *get_MessageDuration)(SETTINGS1 *This, UINT32 *value);
    HRESULT(STDMETHODCALLTYPE *get_AnimationsEnabled)(SETTINGS1 *This, BOOLEAN *value);
    HRESULT(STDMETHODCALLTYPE *get_CaretBrowsingEnabled)(SETTINGS1 *This, BOOLEAN *value);
    HRESULT(STDMETHODCALLTYPE *get_CaretBlinkRate)(SETTINGS1 *This, UINT32 *value);
    HRESULT(STDMETHODCALLTYPE *get_CaretWidth)(SETTINGS1 *This, UINT32 *value);
    HRESULT(STDMETHODCALLTYPE *get_DoubleClickTime)(SETTINGS1 *This, UINT32 *value);
    HRESULT(STDMETHODCALLTYPE *get_MouseHoverTime)(SETTINGS1 *This, UINT32 *value);
    HRESULT(STDMETHODCALLTYPE *UIElementColor)(SETTINGS1 *This, INT32 type, UI_COLOR *value);
};

struct SETTINGS2_VTBL {
    INSPECTABLE_METHODS(SETTINGS2)
    HRESULT(STDMETHODCALLTYPE *get_TextScaleFactor)(SETTINGS2 *This, double *value);
    HRESULT(STDMETHODCALLTYPE *add_TextScaleFactorChanged)(SETTINGS2 *This, IUnknown *handler, EVENT_TOKEN *token);
    HRESULT(STDMETHODCALLTYPE *remove_TextScaleFactorChanged)(SETTINGS2 *This, EVENT_TOKEN token);
};

struct SETTINGS3_VTBL {
    INSPECTABLE_METHODS(SETTINGS3)
    HRESULT(STDMETHODCALLTYPE *GetColorValue)(SETTINGS3 *This, INT32 type, UI_COLOR *value);
    HRESULT(STDMETHODCALLTYPE *add_ColorValuesChanged)(SETTINGS3 *This, IUnknown *handler, EVENT_TOKEN *token);
    HRESULT(STDMETHODCALLTYPE *remove_ColorValuesChanged)(SETTINGS3 *This, EVENT_TOKEN token);
};

struct UI_SETTINGS {
    SETTINGS1 Settings1;
    SETTINGS2 Settings2;
    SETTINGS3 Settings3;
    volatile LONG References;
};

static volatile LONG g_next_token;

static UI_SETTINGS *from1(SETTINGS1 *p)
{
    return CONTAINING_RECORD(p, UI_SETTINGS, Settings1);
}

static UI_SETTINGS *from2(SETTINGS2 *p)
{
    return CONTAINING_RECORD(p, UI_SETTINGS, Settings2);
}

static UI_SETTINGS *from3(SETTINGS3 *p)
{
    return CONTAINING_RECORD(p, UI_SETTINGS, Settings3);
}

/* ------------------------------------------------------------------ shared IInspectable */

static HRESULT query(UI_SETTINGS *s, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IInspectable) || IsEqualIID(riid, &IID_IAgileObject) ||
        IsEqualIID(riid, &IID_IUISettings))
        *object = &s->Settings1;
    else if (IsEqualIID(riid, &IID_IUISettings2))
        *object = &s->Settings2;
    else if (IsEqualIID(riid, &IID_IUISettings3))
        *object = &s->Settings3;
    else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&s->References);
    return S_OK;
}

static ULONG add_ref(UI_SETTINGS *s)
{
    return (ULONG)InterlockedIncrement(&s->References);
}

static ULONG release(UI_SETTINGS *s)
{
    LONG n = InterlockedDecrement(&s->References);
    if (!n)
        CbFree(s);
    return (ULONG)n;
}

static HRESULT get_iids(ULONG *count, IID **iids)
{
    if (!count || !iids)
        return E_POINTER;
    IID *list = CbAlloc(3 * sizeof(IID));
    if (!list)
        return E_OUTOFMEMORY;
    list[0] = IID_IUISettings;
    list[1] = IID_IUISettings2;
    list[2] = IID_IUISettings3;
    *count = 3;
    *iids = list;
    return S_OK;
}

static HRESULT class_name(HSTRING *name)
{
    if (!name)
        return E_POINTER;
    return WindowsCreateString(CbUISettingsClass, (UINT32)wcslen(CbUISettingsClass), name);
}

static HRESULT trust_level(TrustLevel *level)
{
    if (!level)
        return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT add_handler(IUnknown *handler, EVENT_TOKEN *token)
{
    if (!handler || !token)
        return E_INVALIDARG;
    token->Value = InterlockedIncrement(&g_next_token);
    return S_OK;
}

#define INSPECTABLE_THUNKS(n)                                                                                         \
    static HRESULT STDMETHODCALLTYPE query##n(SETTINGS##n *This, REFIID riid, void **object)                          \
    {                                                                                                                 \
        return query(from##n(This), riid, object);                                                                    \
    }                                                                                                                 \
    static ULONG STDMETHODCALLTYPE add_ref##n(SETTINGS##n *This)                                                      \
    {                                                                                                                 \
        return add_ref(from##n(This));                                                                                \
    }                                                                                                                 \
    static ULONG STDMETHODCALLTYPE release##n(SETTINGS##n *This)                                                      \
    {                                                                                                                 \
        return release(from##n(This));                                                                                \
    }                                                                                                                 \
    static HRESULT STDMETHODCALLTYPE iids##n(SETTINGS##n *This, ULONG *count, IID **iids)                             \
    {                                                                                                                 \
        return get_iids(count, iids);                                                                                 \
    }                                                                                                                 \
    static HRESULT STDMETHODCALLTYPE name##n(SETTINGS##n *This, HSTRING *name)                                        \
    {                                                                                                                 \
        return class_name(name);                                                                                      \
    }                                                                                                                 \
    static HRESULT STDMETHODCALLTYPE trust##n(SETTINGS##n *This, TrustLevel *level)                                   \
    {                                                                                                                 \
        return trust_level(level);                                                                                    \
    }

INSPECTABLE_THUNKS(1)
INSPECTABLE_THUNKS(2)
INSPECTABLE_THUNKS(3)

/* ------------------------------------------------------------------ values */

static DWORD read_dword(HKEY root, LPCWSTR path, LPCWSTR name, DWORD fallback)
{
    DWORD value, size = sizeof(value), type;
    HKEY key;
    if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE, &key))
        return fallback;
    LONG error = RegQueryValueExW(key, name, nullptr, &type, (BYTE *)&value, &size);
    RegCloseKey(key);
    return !error && type == REG_DWORD && size == sizeof(value) ? value : fallback;
}

static UI_COLOR color(BYTE r, BYTE g, BYTE b)
{
    return (UI_COLOR){0xFF, r, g, b};
}

static UI_COLOR from_colorref(COLORREF c)
{
    return color(GetRValue(c), GetGValue(c), GetBValue(c));
}

/* the accent as dwm stores it, 0xAABBGGRR, or the default blue. */
static UI_COLOR accent(void)
{
    DWORD abgr = read_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", 0xFFD47800);
    return color((BYTE)abgr, (BYTE)(abgr >> 8), (BYTE)(abgr >> 16));
}

/* lighter shades mix in white and darker ones black, in steps of a fifth. */
static UI_COLOR shade(UI_COLOR c, int step)
{
    int target = step > 0 ? 255 : 0, amount = step > 0 ? step : -step;
    c.R = (BYTE)(c.R + (target - c.R) * amount / 5);
    c.G = (BYTE)(c.G + (target - c.G) * amount / 5);
    c.B = (BYTE)(c.B + (target - c.B) * amount / 5);
    return c;
}

static BOOL light_theme(void)
{
    return read_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      L"AppsUseLightTheme", 1) != 0;
}

static HRESULT size_value(UI_SIZE *value, int x, int y)
{
    if (!value)
        return E_POINTER;
    value->Width = (float)GetSystemMetrics(x);
    value->Height = (float)GetSystemMetrics(y);
    return S_OK;
}

static HRESULT uint_value(UINT32 *value, UINT32 n)
{
    if (!value)
        return E_POINTER;
    *value = n;
    return S_OK;
}

static UINT32 spi_value(UINT action, UINT32 fallback)
{
    UINT32 value = fallback;
    return SystemParametersInfoW(action, 0, &value, 0) ? value : fallback;
}

static HRESULT STDMETHODCALLTYPE hand_preference(SETTINGS1 *This, INT32 *value)
{
    if (!value)
        return E_POINTER;
    /* right handed is 1; left handed menus are a separate user setting. */
    *value = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? 0 : 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cursor_size(SETTINGS1 *This, UI_SIZE *value)
{
    return size_value(value, SM_CXCURSOR, SM_CYCURSOR);
}

static HRESULT STDMETHODCALLTYPE scroll_bar_size(SETTINGS1 *This, UI_SIZE *value)
{
    return size_value(value, SM_CXVSCROLL, SM_CYHSCROLL);
}

static HRESULT STDMETHODCALLTYPE scroll_bar_arrow_size(SETTINGS1 *This, UI_SIZE *value)
{
    return size_value(value, SM_CXHSCROLL, SM_CYVSCROLL);
}

static HRESULT STDMETHODCALLTYPE scroll_bar_thumb_size(SETTINGS1 *This, UI_SIZE *value)
{
    return size_value(value, SM_CXHTHUMB, SM_CYVTHUMB);
}

static HRESULT STDMETHODCALLTYPE message_duration(SETTINGS1 *This, UINT32 *value)
{
    return uint_value(value, spi_value(0x2016 /* SPI_GETMESSAGEDURATION */, 5));
}

static HRESULT STDMETHODCALLTYPE animations_enabled(SETTINGS1 *This, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    *value = spi_value(SPI_GETCLIENTAREAANIMATION, TRUE) != 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE caret_browsing(SETTINGS1 *This, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE caret_blink_rate(SETTINGS1 *This, UINT32 *value)
{
    return uint_value(value, GetCaretBlinkTime());
}

static HRESULT STDMETHODCALLTYPE caret_width(SETTINGS1 *This, UINT32 *value)
{
    return uint_value(value, spi_value(SPI_GETCARETWIDTH, 1));
}

static HRESULT STDMETHODCALLTYPE double_click_time(SETTINGS1 *This, UINT32 *value)
{
    return uint_value(value, GetDoubleClickTime());
}

static HRESULT STDMETHODCALLTYPE mouse_hover_time(SETTINGS1 *This, UINT32 *value)
{
    return uint_value(value, spi_value(SPI_GETMOUSEHOVERTIME, 400));
}

static HRESULT STDMETHODCALLTYPE element_color(SETTINGS1 *This, INT32 type, UI_COLOR *value)
{
    static const BYTE system[13] = {
        COLOR_ACTIVECAPTION, COLOR_BACKGROUND, COLOR_BTNFACE,      COLOR_BTNTEXT,     COLOR_CAPTIONTEXT,
        COLOR_GRAYTEXT,      COLOR_HIGHLIGHT,  COLOR_HIGHLIGHTTEXT, COLOR_HOTLIGHT,    COLOR_INACTIVECAPTION,
        COLOR_INACTIVECAPTIONTEXT, COLOR_WINDOW, COLOR_WINDOWTEXT,
    };
    if (!value)
        return E_POINTER;
    if (type >= 0 && type < (INT32)sizeof(system))
        *value = from_colorref(GetSysColor(system[type]));
    else if (type == 1000)
        *value = accent();
    else if (type > 1000 && type <= 1012) {
        /* the fluent text, non text and background colors of the light or dark theme. */
        BOOL light = light_theme();
        BYTE ink = light ? 0 : 255, page = light ? 255 : 32;
        *value = type >= 1010 ? color(page, page, page) : color(ink, ink, ink);
    } else
        return E_INVALIDARG;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE text_scale_factor(SETTINGS2 *This, double *value)
{
    if (!value)
        return E_POINTER;
    DWORD percent = read_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Accessibility", L"TextScaleFactor", 100);
    if (percent < 100 || percent > 225)
        percent = 100;
    *value = percent / 100.0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE add_text_scale_changed(SETTINGS2 *This, IUnknown *handler, EVENT_TOKEN *token)
{
    return add_handler(handler, token);
}

static HRESULT STDMETHODCALLTYPE remove_text_scale_changed(SETTINGS2 *This, EVENT_TOKEN token)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE color_value(SETTINGS3 *This, INT32 type, UI_COLOR *value)
{
    if (!value)
        return E_POINTER;
    BOOL light = light_theme();
    UI_COLOR a = accent();
    switch (type) {
    case 0:
        *value = light ? color(255, 255, 255) : color(0, 0, 0);
        return S_OK;
    case 1:
        *value = light ? color(0, 0, 0) : color(255, 255, 255);
        return S_OK;
    case 2:
    case 3:
    case 4:
        *value = shade(a, type - 5);
        return S_OK;
    case 5:
        *value = a;
        return S_OK;
    case 6:
    case 7:
    case 8:
        *value = shade(a, type - 5);
        return S_OK;
    case 9:
        *value = color((BYTE)(255 - a.R), (BYTE)(255 - a.G), (BYTE)(255 - a.B));
        return S_OK;
    default:
        return E_INVALIDARG;
    }
}

static HRESULT STDMETHODCALLTYPE add_color_changed(SETTINGS3 *This, IUnknown *handler, EVENT_TOKEN *token)
{
    return add_handler(handler, token);
}

static HRESULT STDMETHODCALLTYPE remove_color_changed(SETTINGS3 *This, EVENT_TOKEN token)
{
    return S_OK;
}

static const SETTINGS1_VTBL g_settings1 = {
    query1,           add_ref1,          release1,          iids1,
    name1,            trust1,            hand_preference,   cursor_size,
    scroll_bar_size,  scroll_bar_arrow_size, scroll_bar_thumb_size, message_duration,
    animations_enabled, caret_browsing,  caret_blink_rate,  caret_width,
    double_click_time, mouse_hover_time, element_color,
};

static const SETTINGS2_VTBL g_settings2 = {
    query2, add_ref2, release2, iids2, name2, trust2, text_scale_factor, add_text_scale_changed,
    remove_text_scale_changed,
};

static const SETTINGS3_VTBL g_settings3 = {
    query3, add_ref3, release3, iids3, name3, trust3, color_value, add_color_changed, remove_color_changed,
};

/* ------------------------------------------------------------------ factory */

static HRESULT STDMETHODCALLTYPE factory_query(IActivationFactory *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IInspectable) ||
        IsEqualIID(riid, &IID_IActivationFactory) || IsEqualIID(riid, &IID_IAgileObject)) {
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

/* the factory is static, references do not affect its lifetime. */
static ULONG STDMETHODCALLTYPE factory_add_ref(IActivationFactory *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE factory_release(IActivationFactory *This)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE factory_iids(IActivationFactory *This, ULONG *count, IID **iids)
{
    if (!count || !iids)
        return E_POINTER;
    *count = 0;
    *iids = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE factory_name(IActivationFactory *This, HSTRING *name)
{
    return class_name(name);
}

static HRESULT STDMETHODCALLTYPE factory_trust(IActivationFactory *This, TrustLevel *level)
{
    return trust_level(level);
}

static HRESULT STDMETHODCALLTYPE factory_activate(IActivationFactory *This, IInspectable **instance)
{
    if (!instance)
        return E_POINTER;
    UI_SETTINGS *s = CbAlloc(sizeof(*s));
    if (!s) {
        *instance = nullptr;
        return E_OUTOFMEMORY;
    }
    s->Settings1.lpVtbl = &g_settings1;
    s->Settings2.lpVtbl = &g_settings2;
    s->Settings3.lpVtbl = &g_settings3;
    s->References = 1;
    *instance = (IInspectable *)&s->Settings1;
    return S_OK;
}

static const IActivationFactoryVtbl g_factory_vtbl = {
    factory_query, factory_add_ref, factory_release, factory_iids, factory_name, factory_trust, factory_activate,
};

static IActivationFactory g_factory = {&g_factory_vtbl};

HRESULT CbUISettingsFactory(REFIID riid, void **factory)
{
    return factory_query(&g_factory, riid, factory);
}
