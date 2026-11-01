/* memory, arrays and the schema tables. */
#include "uia.h"

int _fltused = 0x9875;

WINOLEAPI HRESULT WINAPI CoIncrementMTAUsage(void **cookie);

static INIT_ONCE g_com_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK com_init(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    void *cookie;
    CoIncrementMTAUsage(&cookie);
    return TRUE;
}

/* objects cross threads and processes by COM, which a thread with no apartment of its own may use through the MTA. */
void UiaEnsureCom(void)
{
    InitOnceExecuteOnce(&g_com_once, com_init, nullptr, nullptr);
}

void *UiaAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

void UiaFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

WCHAR *UiaDupString(const WCHAR *s)
{
    SIZE_T n = (wcslen(s) + 1) * sizeof(WCHAR);
    WCHAR *copy = UiaAlloc(n);
    if (copy)
        memcpy(copy, s, n);
    return copy;
}

static HRESULT vector_of(const void *values, int count, VARTYPE vt, SIZE_T size, SAFEARRAY **out)
{
    SAFEARRAY *array = SafeArrayCreateVector(vt, 0, (ULONG)count);
    if (!array)
        return E_OUTOFMEMORY;
    if (count) {
        void *data;
        HRESULT hr = SafeArrayAccessData(array, &data);
        if (FAILED(hr)) {
            SafeArrayDestroy(array);
            return hr;
        }
        memcpy(data, values, (SIZE_T)count * size);
        SafeArrayUnaccessData(array);
    }
    *out = array;
    return S_OK;
}

HRESULT UiaIntArrayToSafeArray(const int *values, int count, SAFEARRAY **out)
{
    return vector_of(values, count, VT_I4, sizeof(int), out);
}

HRESULT UiaDoubleArrayToSafeArray(const double *values, int count, SAFEARRAY **out)
{
    return vector_of(values, count, VT_R8, sizeof(double), out);
}

HRESULT UiaSafeArrayToIntArray(SAFEARRAY *array, int **values, int *count)
{
    *values = nullptr;
    *count = 0;
    if (!array || SafeArrayGetDim(array) != 1)
        return E_INVALIDARG;
    VARTYPE vt = 0;
    if (FAILED(SafeArrayGetVartype(array, &vt)) || (vt != VT_I4 && vt != VT_INT && vt != VT_UI4))
        return E_INVALIDARG;
    LONG lower, upper;
    SafeArrayGetLBound(array, 1, &lower);
    SafeArrayGetUBound(array, 1, &upper);
    int n = upper - lower + 1;
    if (n <= 0)
        return S_OK;
    int *copy = UiaAlloc((SIZE_T)n * sizeof(int));
    if (!copy)
        return E_OUTOFMEMORY;
    void *data;
    HRESULT hr = SafeArrayAccessData(array, &data);
    if (FAILED(hr)) {
        UiaFree(copy);
        return hr;
    }
    memcpy(copy, data, (SIZE_T)n * sizeof(int));
    SafeArrayUnaccessData(array);
    *values = copy;
    *count = n;
    return S_OK;
}

int UiaCompareRuntimeIds(const int *a, int an, const int *b, int bn)
{
    if (an != bn)
        return an < bn ? -1 : 1;
    for (int i = 0; i < an; i++)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}

/* ------------------------------------------------------------------ schema */

static const PROPERTY_INFO g_properties[] = {
#include "props.inc"
};

const PROPERTY_INFO *UiaPropertyInfo(int id)
{
    if (id < 30000 || id >= 30000 + (int)RTL_NUMBER_OF(g_properties))
        return nullptr;
    return &g_properties[id - 30000];
}

/* the defaults of the schema, by the kinds its table names. */
HRESULT UiaDefaultValue(int kind, VARIANT *v)
{
    VariantInit(v);
    switch (kind) {
    case 0:
    case 1:
        v->vt = VT_BOOL;
        v->boolVal = kind ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
    case 2:
        v->vt = VT_BSTR;
        v->bstrVal = SysAllocString(L"");
        return v->bstrVal ? S_OK : E_OUTOFMEMORY;
    case 3:
    case 4:
    case 19:
    case 22:
    case 23:
        v->vt = VT_I4;
        v->lVal = kind == 4 ? 1 : 0;
        return S_OK;
    case 5:
        v->vt = VT_ARRAY | VT_I4;
        v->parray = SafeArrayCreateVector(VT_I4, 0, 0);
        return v->parray ? S_OK : E_OUTOFMEMORY;
    case 6:
    case 7:
    case 8:
        v->vt = VT_R8;
        v->dblVal = kind == 6 ? 0.0 : kind == 7 ? 1.0 : 100.0;
        return S_OK;
    case 9:
        v->vt = VT_ARRAY | VT_R8;
        v->parray = SafeArrayCreateVector(VT_R8, 0, 0);
        return v->parray ? S_OK : E_OUTOFMEMORY;
    case 10:
        v->vt = VT_UNKNOWN;
        v->punkVal = nullptr;
        return S_OK;
    case 11:
        v->vt = VT_ARRAY | VT_UNKNOWN;
        v->parray = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
        return v->parray ? S_OK : E_OUTOFMEMORY;
    case 12:
        return S_OK;
    case 13: {
        double zero[4] = {};
        v->vt = VT_ARRAY | VT_R8;
        return UiaDoubleArrayToSafeArray(zero, 4, &v->parray);
    }
    case 14:
        v->vt = VT_ARRAY | VT_BSTR;
        v->parray = SafeArrayCreateVector(VT_BSTR, 0, 0);
        return v->parray ? S_OK : E_OUTOFMEMORY;
    case 15:
        v->vt = VT_I4;
        v->lVal = UIA_CustomControlTypeId;
        return S_OK;
    case 20:
        v->vt = VT_I4;
        v->lVal = DockPosition_None;
        return S_OK;
    case 21:
        v->vt = VT_I4;
        v->lVal = ExpandCollapseState_LeafNode;
        return S_OK;
    case 24:
    case 25:
        v->vt = VT_I4;
        v->lVal = 2;
        return S_OK;
    case 26:
        v->vt = VT_I4;
        v->lVal = 60000;
        return S_OK;
    case 27:
        v->vt = VT_I4;
        v->lVal = 80050;
        return S_OK;
    }
    return E_INVALIDARG;
}

static const WCHAR *const g_pattern_names[] = {
    L"InvokePattern", L"SelectionPattern", L"ValuePattern", L"RangeValuePattern", L"ScrollPattern",
    L"ExpandCollapsePattern", L"GridPattern", L"GridItemPattern", L"MultipleViewPattern", L"WindowPattern",
    L"SelectionItemPattern", L"DockPattern", L"TablePattern", L"TableItemPattern", L"TextPattern", L"TogglePattern",
    L"TransformPattern", L"ScrollItemPattern", L"LegacyIAccessiblePattern", L"ItemContainerPattern",
    L"VirtualizedItemPattern", L"SynchronizedInputPattern", L"ObjectModelPattern", L"AnnotationPattern",
    L"TextPattern2", L"StylesPattern", L"SpreadsheetPattern", L"SpreadsheetItemPattern", L"TransformPattern2",
    L"TextChildPattern", L"DragPattern", L"DropTargetPattern", L"TextEditPattern", L"CustomNavigationPattern",
    L"SelectionPattern2", L"StructuredMarkupPattern",
};

const WCHAR *UiaPatternName(int id)
{
    if (id < 10000 || id >= 10000 + (int)RTL_NUMBER_OF(g_pattern_names))
        return nullptr;
    return g_pattern_names[id - 10000];
}

static const WCHAR *const g_event_names[] = {
    L"ToolTipOpened", L"ToolTipClosed", L"StructureChanged", L"MenuOpened", L"AutomationPropertyChanged",
    L"AutomationFocusChanged", L"AsyncContentLoaded", L"MenuClosed", L"LayoutInvalidated", L"Invoke_Invoked",
    L"SelectionItem_ElementAddedToSelection", L"SelectionItem_ElementRemovedFromSelection",
    L"SelectionItem_ElementSelected", L"Selection_Invalidated", L"Text_TextSelectionChanged", L"Text_TextChanged",
    L"Window_WindowOpened", L"Window_WindowClosed", L"MenuModeStart", L"MenuModeEnd", L"InputReachedTarget",
    L"InputReachedOtherElement", L"InputDiscarded", L"SystemAlert", L"LiveRegionChanged",
    L"HostedFragmentRootsInvalidated", L"Drag_DragStart", L"Drag_DragCancel", L"Drag_DragComplete",
    L"DropTarget_DragEnter", L"DropTarget_DragLeave", L"DropTarget_Dropped", L"TextEdit_TextChanged",
    L"TextEdit_ConversionTargetChanged", L"Changes", L"Notification", L"ActiveTextPositionChanged",
};

const WCHAR *UiaEventName(int id)
{
    if (id < 20000 || id >= 20000 + (int)RTL_NUMBER_OF(g_event_names))
        return nullptr;
    return g_event_names[id - 20000];
}

static const WCHAR *const g_control_types[] = {
    L"Button", L"Calendar", L"CheckBox", L"ComboBox", L"Edit", L"Hyperlink", L"Image", L"ListItem", L"List", L"Menu",
    L"MenuBar", L"MenuItem", L"ProgressBar", L"RadioButton", L"ScrollBar", L"Slider", L"Spinner", L"StatusBar", L"Tab",
    L"TabItem", L"Text", L"ToolBar", L"ToolTip", L"Tree", L"TreeItem", L"Custom", L"Group", L"Thumb", L"DataGrid",
    L"DataItem", L"Document", L"SplitButton", L"Window", L"Pane", L"Header", L"HeaderItem", L"Table", L"TitleBar",
    L"Separator", L"SemanticZoom", L"AppBar",
};

const WCHAR *UiaControlTypeName(int id)
{
    if (id < 50000 || id >= 50000 + (int)RTL_NUMBER_OF(g_control_types))
        return nullptr;
    return g_control_types[id - 50000];
}

static const WCHAR *const g_localized_types[] = {
    L"button", L"calendar", L"check box", L"combo box", L"edit", L"hyperlink", L"image", L"list item", L"list", L"menu",
    L"menu bar", L"menu item", L"progress bar", L"radio button", L"scroll bar", L"slider", L"spinner", L"status bar",
    L"tab", L"tab item", L"text", L"tool bar", L"tool tip", L"tree", L"tree item", L"custom", L"group", L"thumb",
    L"data grid", L"data item", L"document", L"split button", L"window", L"pane", L"header", L"header item", L"table",
    L"title bar", L"separator", L"semantic zoom", L"app bar",
};

const WCHAR *UiaLocalizedControlType(int id)
{
    if (id < 50000 || id >= 50000 + (int)RTL_NUMBER_OF(g_localized_types))
        return nullptr;
    return g_localized_types[id - 50000];
}
