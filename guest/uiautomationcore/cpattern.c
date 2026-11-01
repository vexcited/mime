/*
 * the pattern objects clients get from elements: IUIAutomationInvokePattern and the others, and text ranges. their
 * Current methods ask the element's node now, their Cached methods read what was fetched with the element.
 */
#include "uia.h"

typedef struct CPAT {
    const void *const *vtbl;
    LONG refs;
    ELEMENT *element;
    PATTERN *pattern;
    PATTERNID id;
    BOOL cached;
} CPAT;

typedef struct PATDEF {
    PATTERNID id;
    const IID *iid;
    const void *const *vtbl;
} PATDEF;

static const PATDEF g_defs[];
static const void *const g_range_vtbl[];
static const IID *range_iid(void);

static HRESULT STDMETHODCALLTYPE cp_query(void *p, REFIID riid, void **out)
{
    CPAT *c = p;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown)) {
        *out = p;
    } else {
        for (const PATDEF *d = g_defs; d->iid; d++)
            if (d->vtbl == c->vtbl && IsEqualIID(riid, d->iid))
                *out = p;
        /* the newer interfaces of a pattern answer to the older ones. */
        if (!*out && c->id == UIA_TransformPattern2Id && IsEqualIID(riid, &IID_IUIAutomationTransformPattern))
            *out = p;
        if (!*out && c->id == UIA_TextPattern2Id && IsEqualIID(riid, &IID_IUIAutomationTextPattern))
            *out = p;
        if (!*out && c->id == UIA_TextEditPatternId && IsEqualIID(riid, &IID_IUIAutomationTextPattern))
            *out = p;
    }
    if (!*out)
        return E_NOINTERFACE;
    InterlockedIncrement(&c->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE cp_add_ref(void *p)
{
    return (ULONG)InterlockedIncrement(&((CPAT *)p)->refs);
}

static ULONG STDMETHODCALLTYPE cp_release(void *p)
{
    CPAT *c = p;
    LONG refs = InterlockedDecrement(&c->refs);
    if (!refs) {
        ElementRelease(c->element);
        PatternRelease(c->pattern);
        UiaFree(c);
    }
    return (ULONG)refs;
}

HRESULT ClientPatternCreate(ELEMENT *e, PATTERNID id, PATTERN *pattern, BOOL cached, REFIID riid, void **out)
{
    *out = nullptr;
    const PATDEF *def = nullptr;
    for (const PATDEF *d = g_defs; d->iid; d++)
        if (d->id == id)
            def = d;
    if (!def)
        return E_NOINTERFACE;
    CPAT *c = UiaAlloc(sizeof(*c));
    if (!c)
        return E_OUTOFMEMORY;
    c->vtbl = def->vtbl;
    c->refs = 1;
    c->element = e;
    ElementAddRef(e);
    c->pattern = pattern;
    PatternAddRef(pattern);
    c->id = id;
    c->cached = cached;
    HRESULT hr = cp_query(c, riid, out);
    cp_release(c);
    return hr;
}

static HRESULT cp_call(void *p, int method, VARIANT *args, ULONG count, VARIANT *ret)
{
    CPAT *c = p;
    if (c->cached)
        return UIA_E_NOTSUPPORTED;
    return PatternCall(c->pattern, method, args, count, ret);
}

/* ------------------------------------------------------------------ properties of patterns */

static HRESULT pv(void *p, PROPERTYID id, BOOL current, VARIANT *out)
{
    CPAT *c = p;
    VariantInit(out);
    return ElementValueEx(c->element, id, current, FALSE, out);
}

static HRESULT pv_int(void *p, PROPERTYID id, BOOL current, int *out)
{
    if (!out)
        return E_POINTER;
    *out = 0;
    VARIANT v;
    HRESULT hr = pv(p, id, current, &v);
    if (FAILED(hr))
        return hr;
    VARIANT t;
    VariantInit(&t);
    if (v.vt == VT_BOOL)
        *out = v.boolVal ? 1 : 0;
    else if (SUCCEEDED(VariantChangeType(&t, &v, 0, VT_I4)))
        *out = t.lVal;
    VariantClear(&t);
    VariantClear(&v);
    return S_OK;
}

static HRESULT pv_double(void *p, PROPERTYID id, BOOL current, double *out)
{
    if (!out)
        return E_POINTER;
    *out = 0;
    VARIANT v;
    HRESULT hr = pv(p, id, current, &v);
    if (FAILED(hr))
        return hr;
    VARIANT t;
    VariantInit(&t);
    if (SUCCEEDED(VariantChangeType(&t, &v, 0, VT_R8)))
        *out = t.dblVal;
    VariantClear(&v);
    return S_OK;
}

static HRESULT pv_bstr(void *p, PROPERTYID id, BOOL current, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT v;
    HRESULT hr = pv(p, id, current, &v);
    if (FAILED(hr))
        return hr;
    if (v.vt == VT_BSTR) {
        *out = v.bstrVal;
        v.bstrVal = nullptr;
    } else {
        *out = SysAllocString(L"");
    }
    VariantClear(&v);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT pv_element(void *p, PROPERTYID id, BOOL current, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT v;
    HRESULT hr = pv(p, id, current, &v);
    if (FAILED(hr))
        return hr;
    if (v.vt == VT_UNKNOWN && v.punkVal)
        v.punkVal->lpVtbl->QueryInterface(v.punkVal, &IID_IMimeElement, out);
    VariantClear(&v);
    return S_OK;
}

static HRESULT elements_of(VARIANT *v, void **out)
{
    ELEMENT **items = nullptr;
    int count = 0;
    if (v->vt == (VT_ARRAY | VT_UNKNOWN) && v->parray) {
        LONG lower, upper;
        IUnknown **raw;
        SafeArrayGetLBound(v->parray, 1, &lower);
        SafeArrayGetUBound(v->parray, 1, &upper);
        int total = upper - lower + 1;
        items = total > 0 ? UiaAlloc((SIZE_T)total * sizeof(ELEMENT *)) : nullptr;
        if (items && SUCCEEDED(SafeArrayAccessData(v->parray, (void **)&raw))) {
            for (int i = 0; i < total; i++) {
                ELEMENT *el = nullptr;
                if (raw[i] && SUCCEEDED(ElementFromCom(raw[i], &el)) && el)
                    items[count++] = el;
            }
            SafeArrayUnaccessData(v->parray);
        }
    }
    return ElementArrayCreate(items, count, (IUnknown **)out);
}

static HRESULT pv_elements(void *p, PROPERTYID id, BOOL current, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT v;
    HRESULT hr = pv(p, id, current, &v);
    if (FAILED(hr))
        return hr;
    hr = elements_of(&v, out);
    VariantClear(&v);
    return hr;
}

static HRESULT pv_safearray(void *p, PROPERTYID id, BOOL current, SAFEARRAY **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT v;
    HRESULT hr = pv(p, id, current, &v);
    if (FAILED(hr))
        return hr;
    if (v.vt & VT_ARRAY) {
        *out = v.parray;
        v.vt = VT_EMPTY;
    }
    VariantClear(&v);
    return S_OK;
}

#define PINT(PFX, NAME, ID)                                                                                            \
    static HRESULT STDMETHODCALLTYPE PFX##_get_Current##NAME(void *p, int *o) { return pv_int(p, ID, TRUE, o); }       \
    static HRESULT STDMETHODCALLTYPE PFX##_get_Cached##NAME(void *p, int *o) { return pv_int(p, ID, FALSE, o); }
#define PDBL(PFX, NAME, ID)                                                                                            \
    static HRESULT STDMETHODCALLTYPE PFX##_get_Current##NAME(void *p, double *o) { return pv_double(p, ID, TRUE, o); } \
    static HRESULT STDMETHODCALLTYPE PFX##_get_Cached##NAME(void *p, double *o) { return pv_double(p, ID, FALSE, o); }
#define PSTR(PFX, NAME, ID)                                                                                            \
    static HRESULT STDMETHODCALLTYPE PFX##_get_Current##NAME(void *p, BSTR *o) { return pv_bstr(p, ID, TRUE, o); }     \
    static HRESULT STDMETHODCALLTYPE PFX##_get_Cached##NAME(void *p, BSTR *o) { return pv_bstr(p, ID, FALSE, o); }
#define PELEM(PFX, NAME, ID)                                                                                           \
    static HRESULT STDMETHODCALLTYPE PFX##_get_Current##NAME(void *p, void **o)                                        \
    {                                                                                                                  \
        return pv_element(p, ID, TRUE, o);                                                                             \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE PFX##_get_Cached##NAME(void *p, void **o)                                         \
    {                                                                                                                  \
        return pv_element(p, ID, FALSE, o);                                                                            \
    }
#define PELEMS(PFX, CURRENT, CACHED, ID)                                                                               \
    static HRESULT STDMETHODCALLTYPE PFX##_##CURRENT(void *p, void **o) { return pv_elements(p, ID, TRUE, o); }       \
    static HRESULT STDMETHODCALLTYPE PFX##_##CACHED(void *p, void **o) { return pv_elements(p, ID, FALSE, o); }
#define PSAFE(PFX, CURRENT, CACHED, ID)                                                                                \
    static HRESULT STDMETHODCALLTYPE PFX##_##CURRENT(void *p, SAFEARRAY **o) { return pv_safearray(p, ID, TRUE, o); }  \
    static HRESULT STDMETHODCALLTYPE PFX##_##CACHED(void *p, SAFEARRAY **o) { return pv_safearray(p, ID, FALSE, o); }
#define CALL0(PFX, NAME, M)                                                                                            \
    static HRESULT STDMETHODCALLTYPE PFX##_##NAME(void *p) { return cp_call(p, M, nullptr, 0, nullptr); }

static VARIANT vint(int v)
{
    VARIANT r;
    VariantInit(&r);
    r.vt = VT_I4;
    r.lVal = v;
    return r;
}

static VARIANT vdbl(double v)
{
    VARIANT r;
    VariantInit(&r);
    r.vt = VT_R8;
    r.dblVal = v;
    return r;
}

static HRESULT call_element(void *p, int method, VARIANT *args, ULONG count, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT ret;
    HRESULT hr = cp_call(p, method, args, count, &ret);
    if (FAILED(hr))
        return hr;
    if (ret.vt == VT_UNKNOWN && ret.punkVal) {
        NODE *node = nullptr;
        if (SUCCEEDED(NodeFromUnknown(ret.punkVal, &node)) && node) {
            hr = ElementCreate(node, nullptr, (ELEMENT **)out);
            NodeRelease(node);
        }
    }
    VariantClear(&ret);
    return hr;
}

static HRESULT call_elements(void *p, int method, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT ret;
    HRESULT hr = cp_call(p, method, nullptr, 0, &ret);
    if (FAILED(hr))
        return hr;
    if (ret.vt == (VT_ARRAY | VT_UNKNOWN)) {
        ValueToElements(&ret, nullptr);
        hr = elements_of(&ret, out);
    } else {
        hr = ElementArrayCreate(nullptr, 0, (IUnknown **)out);
    }
    VariantClear(&ret);
    return hr;
}

static HRESULT call_int(void *p, int method, VARIANT *args, ULONG count, int *out)
{
    if (!out)
        return E_POINTER;
    VARIANT ret;
    HRESULT hr = cp_call(p, method, args, count, &ret);
    *out = SUCCEEDED(hr) && ret.vt == VT_I4 ? ret.lVal : 0;
    VariantClear(&ret);
    return hr;
}

/* ------------------------------------------------------------------ the patterns */

CALL0(Inv, Invoke, PM_Invoke_Invoke)

static HRESULT STDMETHODCALLTYPE Dock_SetDockPosition(void *p, int v)
{
    VARIANT a = vint(v);
    return cp_call(p, PM_Dock_SetDockPosition, &a, 1, nullptr);
}
PINT(Dock, DockPosition, UIA_DockDockPositionPropertyId)

CALL0(Exp, Expand, PM_ExpandCollapse_Expand)
CALL0(Exp, Collapse, PM_ExpandCollapse_Collapse)
PINT(Exp, ExpandCollapseState, UIA_ExpandCollapseExpandCollapseStatePropertyId)

static HRESULT STDMETHODCALLTYPE Grid_GetItem(void *p, int row, int column, void **out)
{
    VARIANT a[2] = {vint(row), vint(column)};
    return call_element(p, PM_Grid_GetItem, a, 2, out);
}
PINT(Grid, RowCount, UIA_GridRowCountPropertyId)
PINT(Grid, ColumnCount, UIA_GridColumnCountPropertyId)

PELEM(GridItem, ContainingGrid, UIA_GridItemContainingGridPropertyId)
PINT(GridItem, Row, UIA_GridItemRowPropertyId)
PINT(GridItem, Column, UIA_GridItemColumnPropertyId)
PINT(GridItem, RowSpan, UIA_GridItemRowSpanPropertyId)
PINT(GridItem, ColumnSpan, UIA_GridItemColumnSpanPropertyId)

static HRESULT STDMETHODCALLTYPE Mv_GetViewName(void *p, int view, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT a = vint(view), ret;
    HRESULT hr = cp_call(p, PM_MultipleView_GetViewName, &a, 1, &ret);
    if (SUCCEEDED(hr) && ret.vt == VT_BSTR) {
        *out = ret.bstrVal;
        ret.vt = VT_EMPTY;
    }
    VariantClear(&ret);
    return hr;
}
static HRESULT STDMETHODCALLTYPE Mv_SetCurrentView(void *p, int view)
{
    VARIANT a = vint(view);
    return cp_call(p, PM_MultipleView_SetCurrentView, &a, 1, nullptr);
}
PINT(Mv, CurrentView, UIA_MultipleViewCurrentViewPropertyId)
PSAFE(Mv, GetCurrentSupportedViews, GetCachedSupportedViews, UIA_MultipleViewSupportedViewsPropertyId)

static HRESULT STDMETHODCALLTYPE Om_GetUnderlyingObjectModel(void *p, IUnknown **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT ret;
    HRESULT hr = cp_call(p, PM_ObjectModel_GetUnderlyingObjectModel, nullptr, 0, &ret);
    VariantClear(&ret);
    return hr;
}

static HRESULT STDMETHODCALLTYPE Rv_SetValue(void *p, double v)
{
    VARIANT a = vdbl(v);
    return cp_call(p, PM_RangeValue_SetValue, &a, 1, nullptr);
}
PDBL(Rv, Value, UIA_RangeValueValuePropertyId)
PINT(Rv, IsReadOnly, UIA_RangeValueIsReadOnlyPropertyId)
PDBL(Rv, Maximum, UIA_RangeValueMaximumPropertyId)
PDBL(Rv, Minimum, UIA_RangeValueMinimumPropertyId)
PDBL(Rv, LargeChange, UIA_RangeValueLargeChangePropertyId)
PDBL(Rv, SmallChange, UIA_RangeValueSmallChangePropertyId)

static HRESULT STDMETHODCALLTYPE Sc_Scroll(void *p, int h, int v)
{
    VARIANT a[2] = {vint(h), vint(v)};
    return cp_call(p, PM_Scroll_Scroll, a, 2, nullptr);
}
static HRESULT STDMETHODCALLTYPE Sc_SetScrollPercent(void *p, double h, double v)
{
    VARIANT a[2] = {vdbl(h), vdbl(v)};
    return cp_call(p, PM_Scroll_SetScrollPercent, a, 2, nullptr);
}
PDBL(Sc, HorizontalScrollPercent, UIA_ScrollHorizontalScrollPercentPropertyId)
PDBL(Sc, VerticalScrollPercent, UIA_ScrollVerticalScrollPercentPropertyId)
PDBL(Sc, HorizontalViewSize, UIA_ScrollHorizontalViewSizePropertyId)
PDBL(Sc, VerticalViewSize, UIA_ScrollVerticalViewSizePropertyId)
PINT(Sc, HorizontallyScrollable, UIA_ScrollHorizontallyScrollablePropertyId)
PINT(Sc, VerticallyScrollable, UIA_ScrollVerticallyScrollablePropertyId)

CALL0(Si, ScrollIntoView, PM_ScrollItem_ScrollIntoView)

static HRESULT STDMETHODCALLTYPE Sel_GetCurrentSelection(void *p, void **out)
{
    return call_elements(p, PM_Selection_GetSelection, out);
}
static HRESULT STDMETHODCALLTYPE Sel_GetCachedSelection(void *p, void **out)
{
    return pv_elements(p, UIA_SelectionSelectionPropertyId, FALSE, out);
}
PINT(Sel, CanSelectMultiple, UIA_SelectionCanSelectMultiplePropertyId)
PINT(Sel, IsSelectionRequired, UIA_SelectionIsSelectionRequiredPropertyId)

CALL0(SelItem, Select, PM_SelectionItem_Select)
CALL0(SelItem, AddToSelection, PM_SelectionItem_AddToSelection)
CALL0(SelItem, RemoveFromSelection, PM_SelectionItem_RemoveFromSelection)
PINT(SelItem, IsSelected, UIA_SelectionItemIsSelectedPropertyId)
PELEM(SelItem, SelectionContainer, UIA_SelectionItemSelectionContainerPropertyId)

static HRESULT STDMETHODCALLTYPE Sync_StartListening(void *p, int type)
{
    VARIANT a = vint(type);
    return cp_call(p, PM_SyncInput_StartListening, &a, 1, nullptr);
}
CALL0(Sync, Cancel, PM_SyncInput_Cancel)

PELEMS(Tbl, GetCurrentRowHeaders, GetCachedRowHeaders, UIA_TableRowHeadersPropertyId)
PELEMS(Tbl, GetCurrentColumnHeaders, GetCachedColumnHeaders, UIA_TableColumnHeadersPropertyId)
static HRESULT STDMETHODCALLTYPE Tbl_get_CurrentRowOrColumnMajor(void *p, int *o) { return pv_int(p, UIA_TableRowOrColumnMajorPropertyId, TRUE, o); }
static HRESULT STDMETHODCALLTYPE Tbl_get_CachedRowOrColumnMajor(void *p, int *o) { return pv_int(p, UIA_TableRowOrColumnMajorPropertyId, FALSE, o); }

PELEMS(TblItem, GetCurrentRowHeaderItems, GetCachedRowHeaderItems, UIA_TableItemRowHeaderItemsPropertyId)
PELEMS(TblItem, GetCurrentColumnHeaderItems, GetCachedColumnHeaderItems, UIA_TableItemColumnHeaderItemsPropertyId)

CALL0(Tog, Toggle, PM_Toggle_Toggle)
PINT(Tog, ToggleState, UIA_ToggleToggleStatePropertyId)

static HRESULT STDMETHODCALLTYPE Tr_Move(void *p, double x, double y)
{
    VARIANT a[2] = {vdbl(x), vdbl(y)};
    return cp_call(p, PM_Transform_Move, a, 2, nullptr);
}
static HRESULT STDMETHODCALLTYPE Tr_Resize(void *p, double w, double h)
{
    VARIANT a[2] = {vdbl(w), vdbl(h)};
    return cp_call(p, PM_Transform_Resize, a, 2, nullptr);
}
static HRESULT STDMETHODCALLTYPE Tr_Rotate(void *p, double d)
{
    VARIANT a = vdbl(d);
    return cp_call(p, PM_Transform_Rotate, &a, 1, nullptr);
}
PINT(Tr, CanMove, UIA_TransformCanMovePropertyId)
PINT(Tr, CanResize, UIA_TransformCanResizePropertyId)
PINT(Tr, CanRotate, UIA_TransformCanRotatePropertyId)

static HRESULT STDMETHODCALLTYPE Val_SetValue(void *p, BSTR v)
{
    VARIANT a;
    VariantInit(&a);
    a.vt = VT_BSTR;
    a.bstrVal = v;
    return cp_call(p, PM_Value_SetValue, &a, 1, nullptr);
}
PSTR(Val, Value, UIA_ValueValuePropertyId)
PINT(Val, IsReadOnly, UIA_ValueIsReadOnlyPropertyId)

CALL0(Win, Close, PM_Window_Close)
static HRESULT STDMETHODCALLTYPE Win_WaitForInputIdle(void *p, int ms, BOOL *out)
{
    if (!out)
        return E_POINTER;
    VARIANT a = vint(ms), ret;
    HRESULT hr = cp_call(p, PM_Window_WaitForInputIdle, &a, 1, &ret);
    *out = SUCCEEDED(hr) && ret.vt == VT_BOOL && ret.boolVal;
    return hr;
}
static HRESULT STDMETHODCALLTYPE Win_SetWindowVisualState(void *p, int state)
{
    VARIANT a = vint(state);
    return cp_call(p, PM_Window_SetVisualState, &a, 1, nullptr);
}
PINT(Win, CanMaximize, UIA_WindowCanMaximizePropertyId)
PINT(Win, CanMinimize, UIA_WindowCanMinimizePropertyId)
PINT(Win, IsModal, UIA_WindowIsModalPropertyId)
PINT(Win, IsTopmost, UIA_WindowIsTopmostPropertyId)
PINT(Win, WindowVisualState, UIA_WindowWindowVisualStatePropertyId)
PINT(Win, WindowInteractionState, UIA_WindowWindowInteractionStatePropertyId)

static HRESULT STDMETHODCALLTYPE Lg_Select(void *p, LONG flags)
{
    VARIANT a = vint(flags);
    return cp_call(p, PM_Legacy_Select, &a, 1, nullptr);
}
CALL0(Lg, DoDefaultAction, PM_Legacy_DoDefaultAction)
static HRESULT STDMETHODCALLTYPE Lg_SetValue(void *p, LPCWSTR v)
{
    VARIANT a;
    VariantInit(&a);
    a.vt = VT_BSTR;
    a.bstrVal = SysAllocString(v);
    HRESULT hr = cp_call(p, PM_Legacy_SetValue, &a, 1, nullptr);
    VariantClear(&a);
    return hr;
}
PINT(Lg, ChildId, UIA_LegacyIAccessibleChildIdPropertyId)
PSTR(Lg, Name, UIA_LegacyIAccessibleNamePropertyId)
PSTR(Lg, Value, UIA_LegacyIAccessibleValuePropertyId)
PSTR(Lg, Description, UIA_LegacyIAccessibleDescriptionPropertyId)
PINT(Lg, Role, UIA_LegacyIAccessibleRolePropertyId)
PINT(Lg, State, UIA_LegacyIAccessibleStatePropertyId)
PSTR(Lg, Help, UIA_LegacyIAccessibleHelpPropertyId)
PSTR(Lg, KeyboardShortcut, UIA_LegacyIAccessibleKeyboardShortcutPropertyId)
PELEMS(Lg, GetCurrentSelection, GetCachedSelection, UIA_LegacyIAccessibleSelectionPropertyId)
PSTR(Lg, DefaultAction, UIA_LegacyIAccessibleDefaultActionPropertyId)
static HRESULT STDMETHODCALLTYPE Lg_GetIAccessible(void *p, IUnknown **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT ret;
    HRESULT hr = cp_call(p, PM_Legacy_GetIAccessible, nullptr, 0, &ret);
    if (SUCCEEDED(hr) && ret.vt == VT_UNKNOWN) {
        *out = ret.punkVal;
        ret.vt = VT_EMPTY;
    }
    VariantClear(&ret);
    return hr;
}

static HRESULT STDMETHODCALLTYPE Ic_FindItemByProperty(void *p, IUnknown *start, PROPERTYID id, VARIANT value, void **out)
{
    VARIANT a[3];
    VariantInit(&a[0]);
    VariantInit(&a[1]);
    ELEMENT *after = nullptr;
    if (start && SUCCEEDED(ElementFromCom(start, &after)) && after) {
        a[0].vt = VT_UNKNOWN;
        a[0].punkVal = (IUnknown *)ElementNode(after);
    }
    a[1] = vint(id);
    a[2] = value;
    HRESULT hr = call_element(p, PM_ItemContainer_FindItemByProperty, a, 3, out);
    ElementRelease(after);
    return hr;
}

CALL0(Vi, Realize, PM_Virtualized_Realize)

PINT(An, AnnotationTypeId, UIA_AnnotationAnnotationTypeIdPropertyId)
PSTR(An, AnnotationTypeName, UIA_AnnotationAnnotationTypeNamePropertyId)
PSTR(An, Author, UIA_AnnotationAuthorPropertyId)
PSTR(An, DateTime, UIA_AnnotationDateTimePropertyId)
PELEM(An, Target, UIA_AnnotationTargetPropertyId)

PINT(St, StyleId, UIA_StylesStyleIdPropertyId)
PSTR(St, StyleName, UIA_StylesStyleNamePropertyId)
PINT(St, FillColor, UIA_StylesFillColorPropertyId)
PSTR(St, FillPatternStyle, UIA_StylesFillPatternStylePropertyId)
PSTR(St, Shape, UIA_StylesShapePropertyId)
PINT(St, FillPatternColor, UIA_StylesFillPatternColorPropertyId)
PSTR(St, ExtendedProperties, UIA_StylesExtendedPropertiesPropertyId)
static HRESULT STDMETHODCALLTYPE St_GetCurrentExtendedPropertiesAsArray(void *p, void **a, int *n)
{
    if (!a || !n)
        return E_POINTER;
    *a = nullptr;
    *n = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE St_GetCachedExtendedPropertiesAsArray(void *p, void **a, int *n)
{
    return St_GetCurrentExtendedPropertiesAsArray(p, a, n);
}

static HRESULT STDMETHODCALLTYPE Ss_GetItemByName(void *p, BSTR name, void **out)
{
    VARIANT a;
    VariantInit(&a);
    a.vt = VT_BSTR;
    a.bstrVal = name;
    return call_element(p, PM_Spreadsheet_GetItemByName, &a, 1, out);
}

PSTR(SsItem, Formula, UIA_SpreadsheetItemFormulaPropertyId)
PELEMS(SsItem, GetCurrentAnnotationObjects, GetCachedAnnotationObjects, UIA_SpreadsheetItemAnnotationObjectsPropertyId)
PSAFE(SsItem, GetCurrentAnnotationTypes, GetCachedAnnotationTypes, UIA_SpreadsheetItemAnnotationTypesPropertyId)

static HRESULT STDMETHODCALLTYPE Tr2_Zoom(void *p, double z)
{
    VARIANT a = vdbl(z);
    return cp_call(p, PM_Transform2_Zoom, &a, 1, nullptr);
}
static HRESULT STDMETHODCALLTYPE Tr2_ZoomByUnit(void *p, int unit)
{
    VARIANT a = vint(unit);
    return cp_call(p, PM_Transform2_ZoomByUnit, &a, 1, nullptr);
}
PINT(Tr2, CanZoom, UIA_Transform2CanZoomPropertyId)
PDBL(Tr2, ZoomLevel, UIA_Transform2ZoomLevelPropertyId)
PDBL(Tr2, ZoomMinimum, UIA_Transform2ZoomMinimumPropertyId)
PDBL(Tr2, ZoomMaximum, UIA_Transform2ZoomMaximumPropertyId)

static HRESULT STDMETHODCALLTYPE Tc_get_TextContainer(void *p, void **out)
{
    return call_element(p, PM_TextChild_TextContainer, nullptr, 0, out);
}

PINT(Dr, IsGrabbed, UIA_DragIsGrabbedPropertyId)
PSTR(Dr, DropEffect, UIA_DragDropEffectPropertyId)
PSAFE(Dr, get_CurrentDropEffects, get_CachedDropEffects, UIA_DragDropEffectsPropertyId)
PELEMS(Dr, GetCurrentGrabbedItems, GetCachedGrabbedItems, UIA_DragGrabbedItemsPropertyId)
PSTR(Dt, DropTargetEffect, UIA_DropTargetDropTargetEffectPropertyId)
PSAFE(Dt, get_CurrentDropTargetEffects, get_CachedDropTargetEffects, UIA_DropTargetDropTargetEffectsPropertyId)

static HRESULT STDMETHODCALLTYPE Cn_Navigate(void *p, int direction, void **out)
{
    VARIANT a = vint(direction);
    return call_element(p, PM_CustomNavigation_Navigate, &a, 1, out);
}

/* ------------------------------------------------------------------ text ranges */

typedef struct CRANGE {
    const void *const *vtbl;
    LONG refs;
    ELEMENT *element;
    PATTERN *range;
} CRANGE;

typedef struct CRANGES {
    const void *const *vtbl;
    LONG refs;
    CRANGE **items;
    int count;
} CRANGES;

static const void *const g_ranges_vtbl[];

static HRESULT STDMETHODCALLTYPE tr_query(void *p, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, range_iid()) &&
        !IsEqualIID(riid, &IID_IUIAutomationTextRange2))
        return E_NOINTERFACE;
    *out = p;
    InterlockedIncrement(&((CRANGE *)p)->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE tr_add_ref(void *p)
{
    return (ULONG)InterlockedIncrement(&((CRANGE *)p)->refs);
}

static ULONG STDMETHODCALLTYPE tr_release(void *p)
{
    CRANGE *r = p;
    LONG refs = InterlockedDecrement(&r->refs);
    if (!refs) {
        ElementRelease(r->element);
        PatternRelease(r->range);
        UiaFree(r);
    }
    return (ULONG)refs;
}

static const IID *range_iid(void)
{
    return &IID_IUIAutomationTextRange;
}

HRESULT ClientRangeCreate(ELEMENT *e, PATTERN *range, BOOL cached, IUnknown **out)
{
    *out = nullptr;
    if (!range)
        return S_OK;
    CRANGE *r = UiaAlloc(sizeof(*r));
    if (!r)
        return E_OUTOFMEMORY;
    r->vtbl = g_range_vtbl;
    r->refs = 1;
    r->element = e;
    ElementAddRef(e);
    r->range = range;
    PatternAddRef(range);
    *out = (IUnknown *)r;
    return S_OK;
}

static HRESULT range_result(CRANGE *r, VARIANT *ret, void **out)
{
    *out = nullptr;
    if (ret->vt == VT_UNKNOWN && ret->punkVal) {
        PATTERN *range = nullptr;
        if (SUCCEEDED(PatternFromUnknown(ret->punkVal, PATTERN_ID_TEXTRANGE, &range)) && range) {
            HRESULT hr = ClientRangeCreate(r->element, range, FALSE, (IUnknown **)out);
            PatternRelease(range);
            return hr;
        }
    }
    return S_OK;
}

static HRESULT range_call(CRANGE *r, int method, VARIANT *args, ULONG count, VARIANT *ret)
{
    return PatternCall(r->range, method, args, count, ret);
}

static HRESULT range_returning_range(void *p, int method, VARIANT *args, ULONG count, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT ret;
    HRESULT hr = range_call(p, method, args, count, &ret);
    if (SUCCEEDED(hr))
        hr = range_result(p, &ret, out);
    VariantClear(&ret);
    return hr;
}

static VARIANT vrange(IUnknown *u)
{
    VARIANT v;
    VariantInit(&v);
    CRANGE *r = nullptr;
    if (u && SUCCEEDED(u->lpVtbl->QueryInterface(u, range_iid(), (void **)&r))) {
        v.vt = VT_UNKNOWN;
        v.punkVal = (IUnknown *)r->range;
        tr_release(r);
    }
    return v;
}

static HRESULT STDMETHODCALLTYPE TR_Clone(void *p, void **out)
{
    return range_returning_range(p, PM_Range_Clone, nullptr, 0, out);
}
static HRESULT STDMETHODCALLTYPE TR_Compare(void *p, IUnknown *other, BOOL *out)
{
    if (!out)
        return E_POINTER;
    VARIANT a = vrange(other), ret;
    HRESULT hr = range_call(p, PM_Range_Compare, &a, 1, &ret);
    *out = SUCCEEDED(hr) && ret.vt == VT_BOOL && ret.boolVal;
    return hr;
}
static HRESULT STDMETHODCALLTYPE TR_CompareEndpoints(void *p, int ep, IUnknown *other, int other_ep, int *out)
{
    if (!out)
        return E_POINTER;
    VARIANT a[3] = {vint(ep), vrange(other), vint(other_ep)}, ret;
    HRESULT hr = range_call(p, PM_Range_CompareEndpoints, a, 3, &ret);
    *out = SUCCEEDED(hr) && ret.vt == VT_I4 ? ret.lVal : 0;
    return hr;
}
static HRESULT STDMETHODCALLTYPE TR_ExpandToEnclosingUnit(void *p, int unit)
{
    VARIANT a = vint(unit);
    return range_call(p, PM_Range_ExpandToEnclosingUnit, &a, 1, nullptr);
}
static HRESULT STDMETHODCALLTYPE TR_FindAttribute(void *p, int attribute, VARIANT value, BOOL backward, void **out)
{
    VARIANT a[3] = {vint(attribute), value, vint(backward)};
    return range_returning_range(p, PM_Range_FindAttribute, a, 3, out);
}
static HRESULT STDMETHODCALLTYPE TR_FindText(void *p, BSTR text, BOOL backward, BOOL ignore_case, void **out)
{
    VARIANT a[3];
    VariantInit(&a[0]);
    a[0].vt = VT_BSTR;
    a[0].bstrVal = text;
    a[1] = vint(backward);
    a[2] = vint(ignore_case);
    return range_returning_range(p, PM_Range_FindText, a, 3, out);
}
static HRESULT STDMETHODCALLTYPE TR_GetAttributeValue(void *p, int attribute, VARIANT *out)
{
    if (!out)
        return E_POINTER;
    VARIANT a = vint(attribute);
    return range_call(p, PM_Range_GetAttributeValue, &a, 1, out);
}
static HRESULT STDMETHODCALLTYPE TR_GetBoundingRectangles(void *p, SAFEARRAY **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT ret;
    HRESULT hr = range_call(p, PM_Range_GetBoundingRectangles, nullptr, 0, &ret);
    if (SUCCEEDED(hr) && (ret.vt & VT_ARRAY)) {
        *out = ret.parray;
        ret.vt = VT_EMPTY;
    }
    VariantClear(&ret);
    return hr;
}
static HRESULT STDMETHODCALLTYPE TR_GetEnclosingElement(void *p, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT ret;
    HRESULT hr = range_call(p, PM_Range_GetEnclosingElement, nullptr, 0, &ret);
    if (SUCCEEDED(hr) && ret.vt == VT_UNKNOWN && ret.punkVal) {
        NODE *node = nullptr;
        if (SUCCEEDED(NodeFromUnknown(ret.punkVal, &node)) && node) {
            hr = ElementCreate(node, nullptr, (ELEMENT **)out);
            NodeRelease(node);
        }
    }
    VariantClear(&ret);
    return hr;
}
static HRESULT STDMETHODCALLTYPE TR_GetText(void *p, int max, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT a = vint(max), ret;
    HRESULT hr = range_call(p, PM_Range_GetText, &a, 1, &ret);
    if (SUCCEEDED(hr) && ret.vt == VT_BSTR) {
        *out = ret.bstrVal;
        ret.vt = VT_EMPTY;
    }
    VariantClear(&ret);
    return hr;
}
static HRESULT STDMETHODCALLTYPE TR_Move(void *p, int unit, int count, int *out)
{
    if (!out)
        return E_POINTER;
    VARIANT a[2] = {vint(unit), vint(count)}, ret;
    HRESULT hr = range_call(p, PM_Range_Move, a, 2, &ret);
    *out = SUCCEEDED(hr) && ret.vt == VT_I4 ? ret.lVal : 0;
    return hr;
}
static HRESULT STDMETHODCALLTYPE TR_MoveEndpointByUnit(void *p, int ep, int unit, int count, int *out)
{
    if (!out)
        return E_POINTER;
    VARIANT a[3] = {vint(ep), vint(unit), vint(count)}, ret;
    HRESULT hr = range_call(p, PM_Range_MoveEndpointByUnit, a, 3, &ret);
    *out = SUCCEEDED(hr) && ret.vt == VT_I4 ? ret.lVal : 0;
    return hr;
}
static HRESULT STDMETHODCALLTYPE TR_MoveEndpointByRange(void *p, int ep, IUnknown *other, int other_ep)
{
    VARIANT a[3] = {vint(ep), vrange(other), vint(other_ep)};
    return range_call(p, PM_Range_MoveEndpointByRange, a, 3, nullptr);
}
static HRESULT STDMETHODCALLTYPE TR_Select(void *p) { return range_call(p, PM_Range_Select, nullptr, 0, nullptr); }
static HRESULT STDMETHODCALLTYPE TR_AddToSelection(void *p) { return range_call(p, PM_Range_AddToSelection, nullptr, 0, nullptr); }
static HRESULT STDMETHODCALLTYPE TR_RemoveFromSelection(void *p) { return range_call(p, PM_Range_RemoveFromSelection, nullptr, 0, nullptr); }
static HRESULT STDMETHODCALLTYPE TR_ScrollIntoView(void *p, BOOL top)
{
    VARIANT a = vint(top);
    return range_call(p, PM_Range_ScrollIntoView, &a, 1, nullptr);
}
static HRESULT STDMETHODCALLTYPE TR_GetChildren(void *p, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT ret;
    HRESULT hr = range_call(p, PM_Range_GetChildren, nullptr, 0, &ret);
    if (SUCCEEDED(hr)) {
        ValueToElements(&ret, nullptr);
        hr = elements_of(&ret, out);
    }
    VariantClear(&ret);
    return hr;
}
static HRESULT STDMETHODCALLTYPE TR_ShowContextMenu(void *p)
{
    return UIA_E_NOTSUPPORTED;
}

#define TRSLOT(name) (const void *)TR_##name,
static const void *const g_range_vtbl[] = {(const void *)tr_query, (const void *)tr_add_ref, (const void *)tr_release,
                                           IUIAutomationTextRange2_METHODS(TRSLOT)};

static HRESULT STDMETHODCALLTYPE trs_query(void *p, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IUIAutomationTextRangeArray))
        return E_NOINTERFACE;
    *out = p;
    InterlockedIncrement(&((CRANGES *)p)->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE trs_add_ref(void *p)
{
    return (ULONG)InterlockedIncrement(&((CRANGES *)p)->refs);
}

static ULONG STDMETHODCALLTYPE trs_release(void *p)
{
    CRANGES *a = p;
    LONG refs = InterlockedDecrement(&a->refs);
    if (!refs) {
        for (int i = 0; i < a->count; i++)
            tr_release(a->items[i]);
        UiaFree(a->items);
        UiaFree(a);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE TRA_get_Length(void *p, int *out)
{
    if (!out)
        return E_POINTER;
    *out = ((CRANGES *)p)->count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE TRA_GetElement(void *p, int index, void **out)
{
    CRANGES *a = p;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (index < 0 || index >= a->count)
        return E_INVALIDARG;
    *out = a->items[index];
    tr_add_ref(a->items[index]);
    return S_OK;
}

static const void *const g_ranges_vtbl[] = {(const void *)trs_query, (const void *)trs_add_ref,
                                            (const void *)trs_release, (const void *)TRA_get_Length,
                                            (const void *)TRA_GetElement};

static HRESULT ranges_of(CPAT *c, int method, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT ret;
    HRESULT hr = cp_call(c, method, nullptr, 0, &ret);
    if (FAILED(hr))
        return hr;
    CRANGES *a = UiaAlloc(sizeof(*a));
    if (!a) {
        VariantClear(&ret);
        return E_OUTOFMEMORY;
    }
    a->vtbl = g_ranges_vtbl;
    a->refs = 1;
    if (ret.vt == (VT_ARRAY | VT_UNKNOWN) && ret.parray) {
        LONG lower, upper;
        IUnknown **raw;
        SafeArrayGetLBound(ret.parray, 1, &lower);
        SafeArrayGetUBound(ret.parray, 1, &upper);
        int total = upper - lower + 1;
        a->items = total > 0 ? UiaAlloc((SIZE_T)total * sizeof(CRANGE *)) : nullptr;
        if (a->items && SUCCEEDED(SafeArrayAccessData(ret.parray, (void **)&raw))) {
            for (int i = 0; i < total; i++) {
                PATTERN *range = nullptr;
                if (raw[i] && SUCCEEDED(PatternFromUnknown(raw[i], PATTERN_ID_TEXTRANGE, &range)) && range) {
                    IUnknown *made;
                    if (SUCCEEDED(ClientRangeCreate(c->element, range, FALSE, &made)))
                        a->items[a->count++] = (CRANGE *)made;
                    PatternRelease(range);
                }
            }
            SafeArrayUnaccessData(ret.parray);
        }
    }
    VariantClear(&ret);
    *out = a;
    return S_OK;
}

static HRESULT text_range_result(CPAT *c, int method, VARIANT *args, ULONG count, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT ret;
    HRESULT hr = cp_call(c, method, args, count, &ret);
    if (SUCCEEDED(hr) && ret.vt == VT_UNKNOWN && ret.punkVal) {
        PATTERN *range = nullptr;
        if (SUCCEEDED(PatternFromUnknown(ret.punkVal, PATTERN_ID_TEXTRANGE, &range)) && range) {
            hr = ClientRangeCreate(c->element, range, FALSE, (IUnknown **)out);
            PatternRelease(range);
        }
    }
    VariantClear(&ret);
    return hr;
}

static HRESULT STDMETHODCALLTYPE Tx_RangeFromPoint(void *p, POINT pt, void **out)
{
    VARIANT a[2] = {vdbl(pt.x), vdbl(pt.y)};
    return text_range_result(p, PM_Text_RangeFromPoint, a, 2, out);
}
static HRESULT STDMETHODCALLTYPE Tx_RangeFromChild(void *p, IUnknown *child, void **out)
{
    VARIANT a;
    VariantInit(&a);
    ELEMENT *e = nullptr;
    if (child && SUCCEEDED(ElementFromCom(child, &e)) && e) {
        a.vt = VT_UNKNOWN;
        a.punkVal = (IUnknown *)ElementNode(e);
    }
    HRESULT hr = text_range_result(p, PM_Text_RangeFromChild, &a, 1, out);
    ElementRelease(e);
    return hr;
}
static HRESULT STDMETHODCALLTYPE Tx_GetSelection(void *p, void **out) { return ranges_of(p, PM_Text_GetSelection, out); }
static HRESULT STDMETHODCALLTYPE Tx_GetVisibleRanges(void *p, void **out) { return ranges_of(p, PM_Text_GetVisibleRanges, out); }
static HRESULT STDMETHODCALLTYPE Tx_get_DocumentRange(void *p, void **out) { return text_range_result(p, PM_Text_DocumentRange, nullptr, 0, out); }
static HRESULT STDMETHODCALLTYPE Tx_get_SupportedTextSelection(void *p, int *out)
{
    return call_int(p, PM_Text_SupportedTextSelection, nullptr, 0, out);
}
static HRESULT STDMETHODCALLTYPE Tx_RangeFromAnnotation(void *p, IUnknown *annotation, void **out)
{
    return Tx_RangeFromChild(p, annotation, out) == S_OK ? S_OK : E_FAIL;
}
static HRESULT STDMETHODCALLTYPE Tx_GetCaretRange(void *p, BOOL *active, void **out)
{
    if (active)
        *active = FALSE;
    return text_range_result(p, PM_Text_GetCaretRange, nullptr, 0, out);
}
static HRESULT STDMETHODCALLTYPE Tx_GetActiveComposition(void *p, void **out)
{
    return text_range_result(p, PM_TextEdit_GetActiveComposition, nullptr, 0, out);
}
static HRESULT STDMETHODCALLTYPE Tx_GetConversionTarget(void *p, void **out)
{
    return text_range_result(p, PM_TextEdit_GetConversionTarget, nullptr, 0, out);
}
static HRESULT STDMETHODCALLTYPE Tc_get_TextRange(void *p, void **out)
{
    return text_range_result(p, PM_TextChild_TextRange, nullptr, 0, out);
}

/* ------------------------------------------------------------------ the tables */

#define Tr2_Move Tr_Move
#define Tr2_Resize Tr_Resize
#define Tr2_Rotate Tr_Rotate
#define Tr2_get_CurrentCanMove Tr_get_CurrentCanMove
#define Tr2_get_CachedCanMove Tr_get_CachedCanMove
#define Tr2_get_CurrentCanResize Tr_get_CurrentCanResize
#define Tr2_get_CachedCanResize Tr_get_CachedCanResize
#define Tr2_get_CurrentCanRotate Tr_get_CurrentCanRotate
#define Tr2_get_CachedCanRotate Tr_get_CachedCanRotate

#define V(...) {(const void *)cp_query, (const void *)cp_add_ref, (const void *)cp_release, __VA_ARGS__}
#define S1(PFX) PFX##_SLOT
#define Inv_SLOT(n) (const void *)Inv_##n,
#define Dock_SLOT(n) (const void *)Dock_##n,
#define Exp_SLOT(n) (const void *)Exp_##n,
#define Grid_SLOT(n) (const void *)Grid_##n,
#define GridItem_SLOT(n) (const void *)GridItem_##n,
#define Mv_SLOT(n) (const void *)Mv_##n,
#define Om_SLOT(n) (const void *)Om_##n,
#define Rv_SLOT(n) (const void *)Rv_##n,
#define Sc_SLOT(n) (const void *)Sc_##n,
#define Si_SLOT(n) (const void *)Si_##n,
#define Sel_SLOT(n) (const void *)Sel_##n,
#define SelItem_SLOT(n) (const void *)SelItem_##n,
#define Sync_SLOT(n) (const void *)Sync_##n,
#define Tbl_SLOT(n) (const void *)Tbl_##n,
#define TblItem_SLOT(n) (const void *)TblItem_##n,
#define Tog_SLOT(n) (const void *)Tog_##n,
#define Tr_SLOT(n) (const void *)Tr_##n,
#define Val_SLOT(n) (const void *)Val_##n,
#define Win_SLOT(n) (const void *)Win_##n,
#define Lg_SLOT(n) (const void *)Lg_##n,
#define Ic_SLOT(n) (const void *)Ic_##n,
#define Vi_SLOT(n) (const void *)Vi_##n,
#define An_SLOT(n) (const void *)An_##n,
#define St_SLOT(n) (const void *)St_##n,
#define Ss_SLOT(n) (const void *)Ss_##n,
#define SsItem_SLOT(n) (const void *)SsItem_##n,
#define Tr2_SLOT(n) (const void *)Tr2_##n,
#define Tc_SLOT(n) (const void *)Tc_##n,
#define Dr_SLOT(n) (const void *)Dr_##n,
#define Dt_SLOT(n) (const void *)Dt_##n,
#define Tx_SLOT(n) (const void *)Tx_##n,
#define Cn_SLOT(n) (const void *)Cn_##n,

static const void *const g_inv[] = V(IUIAutomationInvokePattern_METHODS(Inv_SLOT));
static const void *const g_dock[] = V(IUIAutomationDockPattern_METHODS(Dock_SLOT));
static const void *const g_exp[] = V(IUIAutomationExpandCollapsePattern_METHODS(Exp_SLOT));
static const void *const g_grid[] = V(IUIAutomationGridPattern_METHODS(Grid_SLOT));
static const void *const g_griditem[] = V(IUIAutomationGridItemPattern_METHODS(GridItem_SLOT));
static const void *const g_mv[] = V(IUIAutomationMultipleViewPattern_METHODS(Mv_SLOT));
static const void *const g_om[] = V(IUIAutomationObjectModelPattern_METHODS(Om_SLOT));
static const void *const g_rv[] = V(IUIAutomationRangeValuePattern_METHODS(Rv_SLOT));
static const void *const g_sc[] = V(IUIAutomationScrollPattern_METHODS(Sc_SLOT));
static const void *const g_si[] = V(IUIAutomationScrollItemPattern_METHODS(Si_SLOT));
static const void *const g_sel[] = V(IUIAutomationSelectionPattern_METHODS(Sel_SLOT));
static const void *const g_selitem[] = V(IUIAutomationSelectionItemPattern_METHODS(SelItem_SLOT));
static const void *const g_sync[] = V(IUIAutomationSynchronizedInputPattern_METHODS(Sync_SLOT));
static const void *const g_tbl[] = V(IUIAutomationTablePattern_METHODS(Tbl_SLOT));
static const void *const g_tblitem[] = V(IUIAutomationTableItemPattern_METHODS(TblItem_SLOT));
static const void *const g_tog[] = V(IUIAutomationTogglePattern_METHODS(Tog_SLOT));
static const void *const g_tr[] = V(IUIAutomationTransformPattern_METHODS(Tr_SLOT));
static const void *const g_val[] = V(IUIAutomationValuePattern_METHODS(Val_SLOT));
static const void *const g_win[] = V(IUIAutomationWindowPattern_METHODS(Win_SLOT));
static const void *const g_lg[] = V(IUIAutomationLegacyIAccessiblePattern_METHODS(Lg_SLOT));
static const void *const g_ic[] = V(IUIAutomationItemContainerPattern_METHODS(Ic_SLOT));
static const void *const g_vi[] = V(IUIAutomationVirtualizedItemPattern_METHODS(Vi_SLOT));
static const void *const g_an[] = V(IUIAutomationAnnotationPattern_METHODS(An_SLOT));
static const void *const g_st[] = V(IUIAutomationStylesPattern_METHODS(St_SLOT));
static const void *const g_ss[] = V(IUIAutomationSpreadsheetPattern_METHODS(Ss_SLOT));
static const void *const g_ssitem[] = V(IUIAutomationSpreadsheetItemPattern_METHODS(SsItem_SLOT));
static const void *const g_tr2[] = V(IUIAutomationTransformPattern2_METHODS(Tr2_SLOT));
static const void *const g_tc[] = V(IUIAutomationTextChildPattern_METHODS(Tc_SLOT));
static const void *const g_dr[] = V(IUIAutomationDragPattern_METHODS(Dr_SLOT));
static const void *const g_dt[] = V(IUIAutomationDropTargetPattern_METHODS(Dt_SLOT));
static const void *const g_tx[] = V(IUIAutomationTextPattern_METHODS(Tx_SLOT));
static const void *const g_tx2[] = V(IUIAutomationTextPattern2_METHODS(Tx_SLOT));
static const void *const g_txe[] = V(IUIAutomationTextEditPattern_METHODS(Tx_SLOT));
static const void *const g_cn[] = V(IUIAutomationCustomNavigationPattern_METHODS(Cn_SLOT));

static const PATDEF g_defs[] = {
    {UIA_InvokePatternId, &IID_IUIAutomationInvokePattern, g_inv},
    {UIA_SelectionPatternId, &IID_IUIAutomationSelectionPattern, g_sel},
    {UIA_ValuePatternId, &IID_IUIAutomationValuePattern, g_val},
    {UIA_RangeValuePatternId, &IID_IUIAutomationRangeValuePattern, g_rv},
    {UIA_ScrollPatternId, &IID_IUIAutomationScrollPattern, g_sc},
    {UIA_ExpandCollapsePatternId, &IID_IUIAutomationExpandCollapsePattern, g_exp},
    {UIA_GridPatternId, &IID_IUIAutomationGridPattern, g_grid},
    {UIA_GridItemPatternId, &IID_IUIAutomationGridItemPattern, g_griditem},
    {UIA_MultipleViewPatternId, &IID_IUIAutomationMultipleViewPattern, g_mv},
    {UIA_WindowPatternId, &IID_IUIAutomationWindowPattern, g_win},
    {UIA_SelectionItemPatternId, &IID_IUIAutomationSelectionItemPattern, g_selitem},
    {UIA_DockPatternId, &IID_IUIAutomationDockPattern, g_dock},
    {UIA_TablePatternId, &IID_IUIAutomationTablePattern, g_tbl},
    {UIA_TableItemPatternId, &IID_IUIAutomationTableItemPattern, g_tblitem},
    {UIA_TextPatternId, &IID_IUIAutomationTextPattern, g_tx},
    {UIA_TogglePatternId, &IID_IUIAutomationTogglePattern, g_tog},
    {UIA_TransformPatternId, &IID_IUIAutomationTransformPattern, g_tr},
    {UIA_ScrollItemPatternId, &IID_IUIAutomationScrollItemPattern, g_si},
    {UIA_LegacyIAccessiblePatternId, &IID_IUIAutomationLegacyIAccessiblePattern, g_lg},
    {UIA_ItemContainerPatternId, &IID_IUIAutomationItemContainerPattern, g_ic},
    {UIA_VirtualizedItemPatternId, &IID_IUIAutomationVirtualizedItemPattern, g_vi},
    {UIA_SynchronizedInputPatternId, &IID_IUIAutomationSynchronizedInputPattern, g_sync},
    {UIA_ObjectModelPatternId, &IID_IUIAutomationObjectModelPattern, g_om},
    {UIA_AnnotationPatternId, &IID_IUIAutomationAnnotationPattern, g_an},
    {UIA_TextPattern2Id, &IID_IUIAutomationTextPattern2, g_tx2},
    {UIA_StylesPatternId, &IID_IUIAutomationStylesPattern, g_st},
    {UIA_SpreadsheetPatternId, &IID_IUIAutomationSpreadsheetPattern, g_ss},
    {UIA_SpreadsheetItemPatternId, &IID_IUIAutomationSpreadsheetItemPattern, g_ssitem},
    {UIA_TransformPattern2Id, &IID_IUIAutomationTransformPattern2, g_tr2},
    {UIA_TextChildPatternId, &IID_IUIAutomationTextChildPattern, g_tc},
    {UIA_DragPatternId, &IID_IUIAutomationDragPattern, g_dr},
    {UIA_DropTargetPatternId, &IID_IUIAutomationDropTargetPattern, g_dt},
    {UIA_TextEditPatternId, &IID_IUIAutomationTextEditPattern, g_txe},
    {UIA_CustomNavigationPatternId, &IID_IUIAutomationCustomNavigationPattern, g_cn},
    {0, nullptr, nullptr},
};
