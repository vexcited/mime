/*
 * patterns of a provider of this process, and the calls that name their methods. a pattern call is a method number
 * with its arguments and result as variants, so a pattern of any transport is called the same way.
 */
#include "uia.h"

typedef struct PATLOCAL {
    PATTERN base;
    IUnknown *iface;
    NODE *owner;
} PATLOCAL;

static const PATTERN_OPS g_local_ops;

static const IID *pattern_iid(PATTERNID id)
{
    switch (id) {
    case UIA_InvokePatternId: return &IID_IInvokeProvider;
    case UIA_SelectionPatternId: return &IID_ISelectionProvider;
    case UIA_ValuePatternId: return &IID_IValueProvider;
    case UIA_RangeValuePatternId: return &IID_IRangeValueProvider;
    case UIA_ScrollPatternId: return &IID_IScrollProvider;
    case UIA_ExpandCollapsePatternId: return &IID_IExpandCollapseProvider;
    case UIA_GridPatternId: return &IID_IGridProvider;
    case UIA_GridItemPatternId: return &IID_IGridItemProvider;
    case UIA_MultipleViewPatternId: return &IID_IMultipleViewProvider;
    case UIA_WindowPatternId: return &IID_IWindowProvider;
    case UIA_SelectionItemPatternId: return &IID_ISelectionItemProvider;
    case UIA_DockPatternId: return &IID_IDockProvider;
    case UIA_TablePatternId: return &IID_ITableProvider;
    case UIA_TableItemPatternId: return &IID_ITableItemProvider;
    case UIA_TextPatternId: return &IID_ITextProvider;
    case UIA_TogglePatternId: return &IID_IToggleProvider;
    case UIA_TransformPatternId: return &IID_ITransformProvider;
    case UIA_ScrollItemPatternId: return &IID_IScrollItemProvider;
    case UIA_LegacyIAccessiblePatternId: return &IID_ILegacyIAccessibleProvider;
    case UIA_ItemContainerPatternId: return &IID_IItemContainerProvider;
    case UIA_VirtualizedItemPatternId: return &IID_IVirtualizedItemProvider;
    case UIA_SynchronizedInputPatternId: return &IID_ISynchronizedInputProvider;
    case UIA_ObjectModelPatternId: return &IID_IObjectModelProvider;
    case UIA_AnnotationPatternId: return &IID_IAnnotationProvider;
    case UIA_TextPattern2Id: return &IID_ITextProvider2;
    case UIA_StylesPatternId: return &IID_IStylesProvider;
    case UIA_SpreadsheetPatternId: return &IID_ISpreadsheetProvider;
    case UIA_SpreadsheetItemPatternId: return &IID_ISpreadsheetItemProvider;
    case UIA_TransformPattern2Id: return &IID_ITransformProvider2;
    case UIA_TextChildPatternId: return &IID_ITextChildProvider;
    case UIA_DragPatternId: return &IID_IDragProvider;
    case UIA_DropTargetPatternId: return &IID_IDropTargetProvider;
    case UIA_TextEditPatternId: return &IID_ITextEditProvider;
    case UIA_CustomNavigationPatternId: return &IID_ICustomNavigationProvider;
    case UIA_SelectionPattern2Id: return &IID_ISelectionProvider2;
    case PATTERN_ID_TEXTRANGE: return &IID_ITextRangeProvider;
    }
    return nullptr;
}

HRESULT PatternFromProvider(IUnknown *provider, PATTERNID id, NODE *owner, PATTERN **out)
{
    *out = nullptr;
    const IID *iid = pattern_iid(id);
    if (!iid)
        return S_OK;
    IUnknown *iface = nullptr;
    if (FAILED(provider->lpVtbl->QueryInterface(provider, iid, (void **)&iface)) || !iface)
        return S_OK;
    PATLOCAL *p = PatternAlloc(sizeof(*p), &g_local_ops, id);
    if (!p) {
        iface->lpVtbl->Release(iface);
        return E_OUTOFMEMORY;
    }
    p->iface = iface;
    p->owner = owner;
    if (owner)
        NodeAddRef(owner);
    *out = &p->base;
    return S_OK;
}

static void local_destroy(PATTERN *pattern)
{
    PATLOCAL *p = (PATLOCAL *)pattern;
    if (p->iface)
        p->iface->lpVtbl->Release(p->iface);
    NodeRelease(p->owner);
}

static NODE *local_owner(PATTERN *pattern)
{
    return ((PATLOCAL *)pattern)->owner;
}

/* ------------------------------------------------------------------ arguments and results */

static int arg_int(const VARIANT *args, ULONG count, ULONG i)
{
    if (i >= count)
        return 0;
    if (args[i].vt == VT_I4)
        return args[i].lVal;
    VARIANT t;
    VariantInit(&t);
    int v = 0;
    if (SUCCEEDED(VariantChangeType(&t, (VARIANT *)&args[i], 0, VT_I4)))
        v = t.lVal;
    VariantClear(&t);
    return v;
}

static double arg_double(const VARIANT *args, ULONG count, ULONG i)
{
    if (i >= count)
        return 0;
    if (args[i].vt == VT_R8)
        return args[i].dblVal;
    VARIANT t;
    VariantInit(&t);
    double v = 0;
    if (SUCCEEDED(VariantChangeType(&t, (VARIANT *)&args[i], 0, VT_R8)))
        v = t.dblVal;
    VariantClear(&t);
    return v;
}

static const WCHAR *arg_string(const VARIANT *args, ULONG count, ULONG i)
{
    return i < count && args[i].vt == VT_BSTR && args[i].bstrVal ? args[i].bstrVal : L"";
}

/* the object an argument carries, as a node or a pattern of this process. */
static HRESULT arg_node(const VARIANT *args, ULONG count, ULONG i, NODE **node)
{
    *node = nullptr;
    if (i >= count || (args[i].vt != VT_UNKNOWN && args[i].vt != VT_DISPATCH))
        return S_OK;
    return NodeFromUnknown(args[i].punkVal, node);
}

static IUnknown *arg_provider(const VARIANT *args, ULONG count, ULONG i, NODE **keep)
{
    *keep = nullptr;
    if (FAILED(arg_node(args, count, i, keep)) || !*keep)
        return nullptr;
    return (IUnknown *)(*keep)->ops->provider(*keep);
}

static IUnknown *arg_range(const VARIANT *args, ULONG count, ULONG i)
{
    if (i >= count || args[i].vt != VT_UNKNOWN || !args[i].punkVal)
        return nullptr;
    PATTERN *pattern = nullptr;
    if (FAILED(args[i].punkVal->lpVtbl->QueryInterface(args[i].punkVal, &IID_IMimeUiaPattern, (void **)&pattern)))
        return nullptr;
    IUnknown *iface = pattern->ops == &g_local_ops ? ((PATLOCAL *)pattern)->iface : nullptr;
    PatternRelease(pattern);
    return iface;
}

static void ret_int(VARIANT *ret, int v)
{
    ret->vt = VT_I4;
    ret->lVal = v;
}

static void ret_bool(VARIANT *ret, BOOL v)
{
    ret->vt = VT_BOOL;
    ret->boolVal = v ? VARIANT_TRUE : VARIANT_FALSE;
}

static void ret_double(VARIANT *ret, double v)
{
    ret->vt = VT_R8;
    ret->dblVal = v;
}

static void ret_bstr(VARIANT *ret, BSTR v)
{
    ret->vt = VT_BSTR;
    ret->bstrVal = v;
}

/* a provider returned by a call, which the call gave us a reference to. */
static HRESULT ret_provider(VARIANT *ret, IUnknown *provider)
{
    ret->vt = VT_UNKNOWN;
    ret->punkVal = nullptr;
    if (!provider)
        return S_OK;
    NODE *node = nullptr;
    HRESULT hr = NodeFromUnknown(provider, &node);
    provider->lpVtbl->Release(provider);
    ret->punkVal = (IUnknown *)node;
    return hr;
}

static HRESULT ret_providers(VARIANT *ret, SAFEARRAY *array)
{
    ret->vt = VT_ARRAY | VT_UNKNOWN;
    ret->parray = array;
    if (!array) {
        ret->vt = VT_EMPTY;
        return S_OK;
    }
    return NormalizeValue(ret);
}

static HRESULT ret_range(VARIANT *ret, IUnknown *range, NODE *owner)
{
    ret->vt = VT_UNKNOWN;
    ret->punkVal = nullptr;
    if (!range)
        return S_OK;
    PATTERN *pattern = nullptr;
    HRESULT hr = PatternFromProvider(range, PATTERN_ID_TEXTRANGE, owner, &pattern);
    range->lpVtbl->Release(range);
    ret->punkVal = (IUnknown *)pattern;
    return hr;
}

static HRESULT ret_ranges(VARIANT *ret, SAFEARRAY *array, NODE *owner)
{
    if (!array) {
        ret->vt = VT_EMPTY;
        return S_OK;
    }
    LONG lower, upper;
    SafeArrayGetLBound(array, 1, &lower);
    SafeArrayGetUBound(array, 1, &upper);
    int total = upper - lower + 1;
    SAFEARRAY *result = SafeArrayCreateVector(VT_UNKNOWN, 0, total > 0 ? (ULONG)total : 0);
    if (!result) {
        SafeArrayDestroy(array);
        return E_OUTOFMEMORY;
    }
    if (total > 0) {
        IUnknown **in, **outv;
        SafeArrayAccessData(array, (void **)&in);
        SafeArrayAccessData(result, (void **)&outv);
        for (int i = 0; i < total; i++) {
            PATTERN *pattern = nullptr;
            if (in[i])
                PatternFromProvider(in[i], PATTERN_ID_TEXTRANGE, owner, &pattern);
            outv[i] = (IUnknown *)pattern;
        }
        SafeArrayUnaccessData(result);
        SafeArrayUnaccessData(array);
    }
    SafeArrayDestroy(array);
    ret->vt = VT_ARRAY | VT_UNKNOWN;
    ret->parray = result;
    return S_OK;
}

/* ------------------------------------------------------------------ the calls */

#define IF(T) T *i = (T *)p->iface
#define CALL(method, T, fn, ...)                                                                                       \
    case method: {                                                                                                     \
        IF(T);                                                                                                         \
        return i->lpVtbl->fn(i, ##__VA_ARGS__);                                                                        \
    }
#define GET_INT(method, T, fn)                                                                                         \
    case method: {                                                                                                     \
        IF(T);                                                                                                         \
        int v = 0;                                                                                                     \
        HRESULT hr = i->lpVtbl->fn(i, &v);                                                                             \
        if (SUCCEEDED(hr))                                                                                             \
            ret_int(ret, v);                                                                                           \
        return hr;                                                                                                     \
    }
#define GET_ENUM(method, T, fn, Type)                                                                                  \
    case method: {                                                                                                     \
        IF(T);                                                                                                         \
        Type v = 0;                                                                                                    \
        HRESULT hr = i->lpVtbl->fn(i, &v);                                                                             \
        if (SUCCEEDED(hr))                                                                                             \
            ret_int(ret, (int)v);                                                                                      \
        return hr;                                                                                                     \
    }
#define GET_BOOL(method, T, fn)                                                                                        \
    case method: {                                                                                                     \
        IF(T);                                                                                                         \
        BOOL v = FALSE;                                                                                                \
        HRESULT hr = i->lpVtbl->fn(i, &v);                                                                             \
        if (SUCCEEDED(hr))                                                                                             \
            ret_bool(ret, v);                                                                                          \
        return hr;                                                                                                     \
    }
#define GET_DOUBLE(method, T, fn)                                                                                      \
    case method: {                                                                                                     \
        IF(T);                                                                                                         \
        double v = 0;                                                                                                  \
        HRESULT hr = i->lpVtbl->fn(i, &v);                                                                             \
        if (SUCCEEDED(hr))                                                                                             \
            ret_double(ret, v);                                                                                        \
        return hr;                                                                                                     \
    }
#define GET_BSTR(method, T, fn)                                                                                        \
    case method: {                                                                                                     \
        IF(T);                                                                                                         \
        BSTR v = nullptr;                                                                                              \
        HRESULT hr = i->lpVtbl->fn(i, &v);                                                                             \
        if (SUCCEEDED(hr))                                                                                             \
            ret_bstr(ret, v);                                                                                          \
        return hr;                                                                                                     \
    }
#define GET_PROVIDER(method, T, fn)                                                                                    \
    case method: {                                                                                                     \
        IF(T);                                                                                                         \
        IRawElementProviderSimple *v = nullptr;                                                                        \
        HRESULT hr = i->lpVtbl->fn(i, &v);                                                                             \
        if (SUCCEEDED(hr))                                                                                             \
            hr = ret_provider(ret, (IUnknown *)v);                                                                     \
        return hr;                                                                                                     \
    }
#define GET_PROVIDERS(method, T, fn)                                                                                   \
    case method: {                                                                                                     \
        IF(T);                                                                                                         \
        SAFEARRAY *v = nullptr;                                                                                        \
        HRESULT hr = i->lpVtbl->fn(i, &v);                                                                             \
        if (SUCCEEDED(hr))                                                                                             \
            hr = ret_providers(ret, v);                                                                                \
        return hr;                                                                                                     \
    }
#define GET_ARRAY(method, T, fn)                                                                                       \
    case method: {                                                                                                     \
        IF(T);                                                                                                         \
        SAFEARRAY *v = nullptr;                                                                                        \
        HRESULT hr = i->lpVtbl->fn(i, &v);                                                                             \
        if (SUCCEEDED(hr) && v) {                                                                                      \
            VARTYPE vt = 0;                                                                                            \
            SafeArrayGetVartype(v, &vt);                                                                               \
            ret->vt = VT_ARRAY | vt;                                                                                   \
            ret->parray = v;                                                                                           \
        }                                                                                                              \
        return hr;                                                                                                     \
    }

static HRESULT local_call(PATTERN *pattern, int method, VARIANT *args, ULONG count, VARIANT *ret)
{
    PATLOCAL *p = (PATLOCAL *)pattern;
    VARIANT scratch;
    VariantInit(&scratch);
    if (!ret)
        ret = &scratch;
    if (p->owner && p->owner->disconnected)
        return UIA_E_ELEMENTNOTAVAILABLE;
    HRESULT result = E_NOTIMPL;
    switch (method) {
        CALL(PM_Invoke_Invoke, IInvokeProvider, Invoke)

        case PM_Selection_GetSelection: {
            IF(ISelectionProvider);
            SAFEARRAY *v = nullptr;
            HRESULT hr = i->lpVtbl->GetSelection(i, &v);
            if (SUCCEEDED(hr))
                hr = ret_providers(ret, v);
            return hr;
        }
        GET_BOOL(PM_Selection_CanSelectMultiple, ISelectionProvider, get_CanSelectMultiple)
        GET_BOOL(PM_Selection_IsSelectionRequired, ISelectionProvider, get_IsSelectionRequired)

        case PM_Value_SetValue: {
            IF(IValueProvider);
            return i->lpVtbl->SetValue(i, arg_string(args, count, 0));
        }
        GET_BSTR(PM_Value_Value, IValueProvider, get_Value)
        GET_BOOL(PM_Value_IsReadOnly, IValueProvider, get_IsReadOnly)

        CALL(PM_RangeValue_SetValue, IRangeValueProvider, SetValue, arg_double(args, count, 0))
        GET_DOUBLE(PM_RangeValue_Value, IRangeValueProvider, get_Value)
        GET_BOOL(PM_RangeValue_IsReadOnly, IRangeValueProvider, get_IsReadOnly)
        GET_DOUBLE(PM_RangeValue_Maximum, IRangeValueProvider, get_Maximum)
        GET_DOUBLE(PM_RangeValue_Minimum, IRangeValueProvider, get_Minimum)
        GET_DOUBLE(PM_RangeValue_LargeChange, IRangeValueProvider, get_LargeChange)
        GET_DOUBLE(PM_RangeValue_SmallChange, IRangeValueProvider, get_SmallChange)

        CALL(PM_Scroll_Scroll, IScrollProvider, Scroll, (ScrollAmount)arg_int(args, count, 0),
             (ScrollAmount)arg_int(args, count, 1))
        CALL(PM_Scroll_SetScrollPercent, IScrollProvider, SetScrollPercent, arg_double(args, count, 0),
             arg_double(args, count, 1))
        GET_DOUBLE(PM_Scroll_HorizontalScrollPercent, IScrollProvider, get_HorizontalScrollPercent)
        GET_DOUBLE(PM_Scroll_VerticalScrollPercent, IScrollProvider, get_VerticalScrollPercent)
        GET_DOUBLE(PM_Scroll_HorizontalViewSize, IScrollProvider, get_HorizontalViewSize)
        GET_DOUBLE(PM_Scroll_VerticalViewSize, IScrollProvider, get_VerticalViewSize)
        GET_BOOL(PM_Scroll_HorizontallyScrollable, IScrollProvider, get_HorizontallyScrollable)
        GET_BOOL(PM_Scroll_VerticallyScrollable, IScrollProvider, get_VerticallyScrollable)

        CALL(PM_ExpandCollapse_Expand, IExpandCollapseProvider, Expand)
        CALL(PM_ExpandCollapse_Collapse, IExpandCollapseProvider, Collapse)
        GET_ENUM(PM_ExpandCollapse_State, IExpandCollapseProvider, get_ExpandCollapseState, ExpandCollapseState)

        case PM_Grid_GetItem: {
            IF(IGridProvider);
            IRawElementProviderSimple *v = nullptr;
            HRESULT hr = i->lpVtbl->GetItem(i, arg_int(args, count, 0), arg_int(args, count, 1), &v);
            if (SUCCEEDED(hr))
                hr = ret_provider(ret, (IUnknown *)v);
            return hr;
        }
        GET_INT(PM_Grid_RowCount, IGridProvider, get_RowCount)
        GET_INT(PM_Grid_ColumnCount, IGridProvider, get_ColumnCount)
        GET_INT(PM_GridItem_Row, IGridItemProvider, get_Row)
        GET_INT(PM_GridItem_Column, IGridItemProvider, get_Column)
        GET_INT(PM_GridItem_RowSpan, IGridItemProvider, get_RowSpan)
        GET_INT(PM_GridItem_ColumnSpan, IGridItemProvider, get_ColumnSpan)
        GET_PROVIDER(PM_GridItem_ContainingGrid, IGridItemProvider, get_ContainingGrid)

        case PM_MultipleView_GetViewName: {
            IF(IMultipleViewProvider);
            BSTR v = nullptr;
            HRESULT hr = i->lpVtbl->GetViewName(i, arg_int(args, count, 0), &v);
            if (SUCCEEDED(hr))
                ret_bstr(ret, v);
            return hr;
        }
        CALL(PM_MultipleView_SetCurrentView, IMultipleViewProvider, SetCurrentView, arg_int(args, count, 0))
        GET_INT(PM_MultipleView_CurrentView, IMultipleViewProvider, get_CurrentView)
        GET_ARRAY(PM_MultipleView_GetSupportedViews, IMultipleViewProvider, GetSupportedViews)

        CALL(PM_Window_SetVisualState, IWindowProvider, SetVisualState, (WindowVisualState)arg_int(args, count, 0))
        CALL(PM_Window_Close, IWindowProvider, Close)
        case PM_Window_WaitForInputIdle: {
            IF(IWindowProvider);
            BOOL v = FALSE;
            HRESULT hr = i->lpVtbl->WaitForInputIdle(i, arg_int(args, count, 0), &v);
            if (SUCCEEDED(hr))
                ret_bool(ret, v);
            return hr;
        }
        GET_BOOL(PM_Window_CanMaximize, IWindowProvider, get_CanMaximize)
        GET_BOOL(PM_Window_CanMinimize, IWindowProvider, get_CanMinimize)
        GET_BOOL(PM_Window_IsModal, IWindowProvider, get_IsModal)
        GET_ENUM(PM_Window_VisualState, IWindowProvider, get_WindowVisualState, WindowVisualState)
        GET_ENUM(PM_Window_InteractionState, IWindowProvider, get_WindowInteractionState, WindowInteractionState)
        GET_BOOL(PM_Window_IsTopmost, IWindowProvider, get_IsTopmost)

        CALL(PM_SelectionItem_Select, ISelectionItemProvider, Select)
        CALL(PM_SelectionItem_AddToSelection, ISelectionItemProvider, AddToSelection)
        CALL(PM_SelectionItem_RemoveFromSelection, ISelectionItemProvider, RemoveFromSelection)
        GET_BOOL(PM_SelectionItem_IsSelected, ISelectionItemProvider, get_IsSelected)
        GET_PROVIDER(PM_SelectionItem_SelectionContainer, ISelectionItemProvider, get_SelectionContainer)

        CALL(PM_Dock_SetDockPosition, IDockProvider, SetDockPosition, (DockPosition)arg_int(args, count, 0))
        GET_ENUM(PM_Dock_DockPosition, IDockProvider, get_DockPosition, DockPosition)

        GET_PROVIDERS(PM_Table_GetRowHeaders, ITableProvider, GetRowHeaders)
        GET_PROVIDERS(PM_Table_GetColumnHeaders, ITableProvider, GetColumnHeaders)
        GET_ENUM(PM_Table_RowOrColumnMajor, ITableProvider, get_RowOrColumnMajor, RowOrColumnMajor)
        GET_PROVIDERS(PM_TableItem_GetRowHeaderItems, ITableItemProvider, GetRowHeaderItems)
        GET_PROVIDERS(PM_TableItem_GetColumnHeaderItems, ITableItemProvider, GetColumnHeaderItems)

        CALL(PM_Toggle_Toggle, IToggleProvider, Toggle)
        GET_ENUM(PM_Toggle_State, IToggleProvider, get_ToggleState, ToggleState)

        CALL(PM_Transform_Move, ITransformProvider, Move, arg_double(args, count, 0), arg_double(args, count, 1))
        CALL(PM_Transform_Resize, ITransformProvider, Resize, arg_double(args, count, 0), arg_double(args, count, 1))
        CALL(PM_Transform_Rotate, ITransformProvider, Rotate, arg_double(args, count, 0))
        GET_BOOL(PM_Transform_CanMove, ITransformProvider, get_CanMove)
        GET_BOOL(PM_Transform_CanResize, ITransformProvider, get_CanResize)
        GET_BOOL(PM_Transform_CanRotate, ITransformProvider, get_CanRotate)

        CALL(PM_ScrollItem_ScrollIntoView, IScrollItemProvider, ScrollIntoView)

        CALL(PM_Legacy_Select, ILegacyIAccessibleProvider, Select, arg_int(args, count, 0))
        CALL(PM_Legacy_DoDefaultAction, ILegacyIAccessibleProvider, DoDefaultAction)
        CALL(PM_Legacy_SetValue, ILegacyIAccessibleProvider, SetValue, arg_string(args, count, 0))
        case PM_Legacy_GetIAccessible: {
            IF(ILegacyIAccessibleProvider);
            IAccessible *v = nullptr;
            HRESULT hr = i->lpVtbl->GetIAccessible(i, &v);
            if (SUCCEEDED(hr)) {
                ret->vt = VT_UNKNOWN;
                ret->punkVal = (IUnknown *)v;
            }
            return hr;
        }
        GET_INT(PM_Legacy_ChildId, ILegacyIAccessibleProvider, get_ChildId)
        GET_BSTR(PM_Legacy_Name, ILegacyIAccessibleProvider, get_Name)
        GET_BSTR(PM_Legacy_Value, ILegacyIAccessibleProvider, get_Value)
        GET_BSTR(PM_Legacy_Description, ILegacyIAccessibleProvider, get_Description)
        GET_BSTR(PM_Legacy_Help, ILegacyIAccessibleProvider, get_Help)
        GET_BSTR(PM_Legacy_KeyboardShortcut, ILegacyIAccessibleProvider, get_KeyboardShortcut)
        GET_BSTR(PM_Legacy_DefaultAction, ILegacyIAccessibleProvider, get_DefaultAction)
        case PM_Legacy_Role: {
            IF(ILegacyIAccessibleProvider);
            DWORD v = 0;
            HRESULT hr = i->lpVtbl->get_Role(i, &v);
            if (SUCCEEDED(hr))
                ret_int(ret, (int)v);
            return hr;
        }
        case PM_Legacy_State: {
            IF(ILegacyIAccessibleProvider);
            DWORD v = 0;
            HRESULT hr = i->lpVtbl->get_State(i, &v);
            if (SUCCEEDED(hr))
                ret_int(ret, (int)v);
            return hr;
        }
        GET_ARRAY(PM_Legacy_GetSelection, ILegacyIAccessibleProvider, GetSelection)

        case PM_ItemContainer_FindItemByProperty: {
            IF(IItemContainerProvider);
            NODE *keep;
            IRawElementProviderSimple *after = (IRawElementProviderSimple *)arg_provider(args, count, 0, &keep);
            VARIANT none;
            VariantInit(&none);
            IRawElementProviderSimple *found = nullptr;
            HRESULT hr = i->lpVtbl->FindItemByProperty(i, after, arg_int(args, count, 1), count > 2 ? args[2] : none,
                                                       &found);
            NodeRelease(keep);
            if (SUCCEEDED(hr))
                hr = ret_provider(ret, (IUnknown *)found);
            return hr;
        }
        CALL(PM_Virtualized_Realize, IVirtualizedItemProvider, Realize)
        CALL(PM_SyncInput_StartListening, ISynchronizedInputProvider, StartListening,
             (SynchronizedInputType)arg_int(args, count, 0))
        CALL(PM_SyncInput_Cancel, ISynchronizedInputProvider, Cancel)

        case PM_Text_GetSelection: {
            IF(ITextProvider);
            SAFEARRAY *v = nullptr;
            HRESULT hr = i->lpVtbl->GetSelection(i, &v);
            return SUCCEEDED(hr) ? ret_ranges(ret, v, p->owner) : hr;
        }
        case PM_Text_GetVisibleRanges: {
            IF(ITextProvider);
            SAFEARRAY *v = nullptr;
            HRESULT hr = i->lpVtbl->GetVisibleRanges(i, &v);
            return SUCCEEDED(hr) ? ret_ranges(ret, v, p->owner) : hr;
        }
        case PM_Text_RangeFromChild: {
            IF(ITextProvider);
            NODE *keep;
            IRawElementProviderSimple *child = (IRawElementProviderSimple *)arg_provider(args, count, 0, &keep);
            ITextRangeProvider *range = nullptr;
            HRESULT hr = i->lpVtbl->RangeFromChild(i, child, &range);
            NodeRelease(keep);
            return SUCCEEDED(hr) ? ret_range(ret, (IUnknown *)range, p->owner) : hr;
        }
        case PM_Text_RangeFromPoint: {
            IF(ITextProvider);
            UiaPoint point = {arg_double(args, count, 0), arg_double(args, count, 1)};
            ITextRangeProvider *range = nullptr;
            HRESULT hr = i->lpVtbl->RangeFromPoint(i, point, &range);
            return SUCCEEDED(hr) ? ret_range(ret, (IUnknown *)range, p->owner) : hr;
        }
        case PM_Text_DocumentRange: {
            IF(ITextProvider);
            ITextRangeProvider *range = nullptr;
            HRESULT hr = i->lpVtbl->get_DocumentRange(i, &range);
            return SUCCEEDED(hr) ? ret_range(ret, (IUnknown *)range, p->owner) : hr;
        }
        GET_ENUM(PM_Text_SupportedTextSelection, ITextProvider, get_SupportedTextSelection, SupportedTextSelection)
        case PM_Text_RangeFromAnnotation: {
            IF(ITextProvider2);
            NODE *keep;
            IRawElementProviderSimple *annotation = (IRawElementProviderSimple *)arg_provider(args, count, 0, &keep);
            ITextRangeProvider *range = nullptr;
            HRESULT hr = i->lpVtbl->RangeFromAnnotation(i, annotation, &range);
            NodeRelease(keep);
            return SUCCEEDED(hr) ? ret_range(ret, (IUnknown *)range, p->owner) : hr;
        }
        case PM_Text_GetCaretRange: {
            IF(ITextProvider2);
            BOOL active = FALSE;
            ITextRangeProvider *range = nullptr;
            HRESULT hr = i->lpVtbl->GetCaretRange(i, &active, &range);
            if (FAILED(hr))
                return hr;
            (void)active;
            return ret_range(ret, (IUnknown *)range, p->owner);
        }

        case PM_Range_Clone: {
            IF(ITextRangeProvider);
            ITextRangeProvider *range = nullptr;
            HRESULT hr = i->lpVtbl->Clone(i, &range);
            return SUCCEEDED(hr) ? ret_range(ret, (IUnknown *)range, p->owner) : hr;
        }
        case PM_Range_Compare: {
            IF(ITextRangeProvider);
            BOOL v = FALSE;
            HRESULT hr = i->lpVtbl->Compare(i, (ITextRangeProvider *)arg_range(args, count, 0), &v);
            if (SUCCEEDED(hr))
                ret_bool(ret, v);
            return hr;
        }
        case PM_Range_CompareEndpoints: {
            IF(ITextRangeProvider);
            int v = 0;
            HRESULT hr = i->lpVtbl->CompareEndpoints(i, (TextPatternRangeEndpoint)arg_int(args, count, 0),
                                                     (ITextRangeProvider *)arg_range(args, count, 1),
                                                     (TextPatternRangeEndpoint)arg_int(args, count, 2), &v);
            if (SUCCEEDED(hr))
                ret_int(ret, v);
            return hr;
        }
        CALL(PM_Range_ExpandToEnclosingUnit, ITextRangeProvider, ExpandToEnclosingUnit,
             (TextUnit)arg_int(args, count, 0))
        case PM_Range_FindAttribute: {
            IF(ITextRangeProvider);
            VARIANT none;
            VariantInit(&none);
            ITextRangeProvider *range = nullptr;
            HRESULT hr = i->lpVtbl->FindAttribute(i, arg_int(args, count, 0), count > 1 ? args[1] : none,
                                                  arg_int(args, count, 2), &range);
            return SUCCEEDED(hr) ? ret_range(ret, (IUnknown *)range, p->owner) : hr;
        }
        case PM_Range_FindText: {
            IF(ITextRangeProvider);
            ITextRangeProvider *range = nullptr;
            HRESULT hr = i->lpVtbl->FindText(i, (BSTR)arg_string(args, count, 0), arg_int(args, count, 1),
                                             arg_int(args, count, 2), &range);
            return SUCCEEDED(hr) ? ret_range(ret, (IUnknown *)range, p->owner) : hr;
        }
        case PM_Range_GetAttributeValue: {
            IF(ITextRangeProvider);
            VARIANT v;
            VariantInit(&v);
            HRESULT hr = i->lpVtbl->GetAttributeValue(i, arg_int(args, count, 0), &v);
            if (SUCCEEDED(hr))
                *ret = v;
            return hr;
        }
        GET_ARRAY(PM_Range_GetBoundingRectangles, ITextRangeProvider, GetBoundingRectangles)
        case PM_Range_GetEnclosingElement: {
            IF(ITextRangeProvider);
            IRawElementProviderSimple *v = nullptr;
            HRESULT hr = i->lpVtbl->GetEnclosingElement(i, &v);
            return SUCCEEDED(hr) ? ret_provider(ret, (IUnknown *)v) : hr;
        }
        case PM_Range_GetText: {
            IF(ITextRangeProvider);
            BSTR v = nullptr;
            HRESULT hr = i->lpVtbl->GetText(i, arg_int(args, count, 0), &v);
            if (SUCCEEDED(hr))
                ret_bstr(ret, v);
            return hr;
        }
        case PM_Range_Move: {
            IF(ITextRangeProvider);
            int v = 0;
            HRESULT hr = i->lpVtbl->Move(i, (TextUnit)arg_int(args, count, 0), arg_int(args, count, 1), &v);
            if (SUCCEEDED(hr))
                ret_int(ret, v);
            return hr;
        }
        case PM_Range_MoveEndpointByUnit: {
            IF(ITextRangeProvider);
            int v = 0;
            HRESULT hr = i->lpVtbl->MoveEndpointByUnit(i, (TextPatternRangeEndpoint)arg_int(args, count, 0),
                                                       (TextUnit)arg_int(args, count, 1), arg_int(args, count, 2), &v);
            if (SUCCEEDED(hr))
                ret_int(ret, v);
            return hr;
        }
        CALL(PM_Range_MoveEndpointByRange, ITextRangeProvider, MoveEndpointByRange,
             (TextPatternRangeEndpoint)arg_int(args, count, 0), (ITextRangeProvider *)arg_range(args, count, 1),
             (TextPatternRangeEndpoint)arg_int(args, count, 2))
        CALL(PM_Range_Select, ITextRangeProvider, Select)
        CALL(PM_Range_AddToSelection, ITextRangeProvider, AddToSelection)
        CALL(PM_Range_RemoveFromSelection, ITextRangeProvider, RemoveFromSelection)
        CALL(PM_Range_ScrollIntoView, ITextRangeProvider, ScrollIntoView, arg_int(args, count, 0))
        GET_PROVIDERS(PM_Range_GetChildren, ITextRangeProvider, GetChildren)

        GET_INT(PM_Annotation_TypeId, IAnnotationProvider, get_AnnotationTypeId)
        GET_BSTR(PM_Annotation_TypeName, IAnnotationProvider, get_AnnotationTypeName)
        GET_BSTR(PM_Annotation_Author, IAnnotationProvider, get_Author)
        GET_BSTR(PM_Annotation_DateTime, IAnnotationProvider, get_DateTime)
        GET_PROVIDER(PM_Annotation_Target, IAnnotationProvider, get_Target)

        GET_INT(PM_Styles_StyleId, IStylesProvider, get_StyleId)
        GET_BSTR(PM_Styles_StyleName, IStylesProvider, get_StyleName)
        GET_INT(PM_Styles_FillColor, IStylesProvider, get_FillColor)
        GET_BSTR(PM_Styles_FillPatternStyle, IStylesProvider, get_FillPatternStyle)
        GET_BSTR(PM_Styles_Shape, IStylesProvider, get_Shape)
        GET_INT(PM_Styles_FillPatternColor, IStylesProvider, get_FillPatternColor)
        GET_BSTR(PM_Styles_ExtendedProperties, IStylesProvider, get_ExtendedProperties)

        case PM_Spreadsheet_GetItemByName: {
            IF(ISpreadsheetProvider);
            IRawElementProviderSimple *v = nullptr;
            HRESULT hr = i->lpVtbl->GetItemByName(i, arg_string(args, count, 0), &v);
            return SUCCEEDED(hr) ? ret_provider(ret, (IUnknown *)v) : hr;
        }
        GET_BSTR(PM_SpreadsheetItem_Formula, ISpreadsheetItemProvider, get_Formula)
        GET_PROVIDERS(PM_SpreadsheetItem_GetAnnotationObjects, ISpreadsheetItemProvider, GetAnnotationObjects)
        GET_ARRAY(PM_SpreadsheetItem_GetAnnotationTypes, ISpreadsheetItemProvider, GetAnnotationTypes)

        GET_BOOL(PM_Drag_IsGrabbed, IDragProvider, get_IsGrabbed)
        GET_BSTR(PM_Drag_DropEffect, IDragProvider, get_DropEffect)
        GET_ARRAY(PM_Drag_DropEffects, IDragProvider, get_DropEffects)
        GET_PROVIDERS(PM_Drag_GetGrabbedItems, IDragProvider, GetGrabbedItems)
        GET_BSTR(PM_DropTarget_Effect, IDropTargetProvider, get_DropTargetEffect)
        GET_ARRAY(PM_DropTarget_Effects, IDropTargetProvider, get_DropTargetEffects)

        GET_BOOL(PM_Transform2_CanZoom, ITransformProvider2, get_CanZoom)
        GET_DOUBLE(PM_Transform2_ZoomLevel, ITransformProvider2, get_ZoomLevel)
        GET_DOUBLE(PM_Transform2_ZoomMinimum, ITransformProvider2, get_ZoomMinimum)
        GET_DOUBLE(PM_Transform2_ZoomMaximum, ITransformProvider2, get_ZoomMaximum)
        CALL(PM_Transform2_Zoom, ITransformProvider2, Zoom, arg_double(args, count, 0))
        CALL(PM_Transform2_ZoomByUnit, ITransformProvider2, ZoomByUnit, (ZoomUnit)arg_int(args, count, 0))

        GET_PROVIDER(PM_Selection2_FirstSelectedItem, ISelectionProvider2, get_FirstSelectedItem)
        GET_PROVIDER(PM_Selection2_LastSelectedItem, ISelectionProvider2, get_LastSelectedItem)
        GET_PROVIDER(PM_Selection2_CurrentSelectedItem, ISelectionProvider2, get_CurrentSelectedItem)
        GET_INT(PM_Selection2_ItemCount, ISelectionProvider2, get_ItemCount)

        GET_PROVIDER(PM_TextChild_TextContainer, ITextChildProvider, get_TextContainer)
        case PM_TextChild_TextRange: {
            IF(ITextChildProvider);
            ITextRangeProvider *range = nullptr;
            HRESULT hr = i->lpVtbl->get_TextRange(i, &range);
            return SUCCEEDED(hr) ? ret_range(ret, (IUnknown *)range, p->owner) : hr;
        }
        case PM_TextEdit_GetActiveComposition: {
            IF(ITextEditProvider);
            ITextRangeProvider *range = nullptr;
            HRESULT hr = i->lpVtbl->GetActiveComposition(i, &range);
            return SUCCEEDED(hr) ? ret_range(ret, (IUnknown *)range, p->owner) : hr;
        }
        case PM_TextEdit_GetConversionTarget: {
            IF(ITextEditProvider);
            ITextRangeProvider *range = nullptr;
            HRESULT hr = i->lpVtbl->GetConversionTarget(i, &range);
            return SUCCEEDED(hr) ? ret_range(ret, (IUnknown *)range, p->owner) : hr;
        }
        case PM_CustomNavigation_Navigate: {
            IF(ICustomNavigationProvider);
            IRawElementProviderSimple *v = nullptr;
            HRESULT hr = i->lpVtbl->Navigate(i, (NavigateDirection)arg_int(args, count, 0), &v);
            return SUCCEEDED(hr) ? ret_provider(ret, (IUnknown *)v) : hr;
        }
    }
    return result;
}

static const PATTERN_OPS g_local_ops = {local_destroy, local_call, local_owner};

HRESULT PatternCall(PATTERN *p, int method, VARIANT *args, ULONG count, VARIANT *ret)
{
    if (ret)
        VariantInit(ret);
    if (method < 0 || method >= PM_COUNT)
        return E_INVALIDARG;
    return p->ops->call(p, method, args, count, ret);
}

int PatternMethodOfProperty(PROPERTYID id)
{
    switch (id) {
    case UIA_ValueValuePropertyId: return PM_Value_Value;
    case UIA_ValueIsReadOnlyPropertyId: return PM_Value_IsReadOnly;
    case UIA_RangeValueValuePropertyId: return PM_RangeValue_Value;
    case UIA_RangeValueIsReadOnlyPropertyId: return PM_RangeValue_IsReadOnly;
    case UIA_RangeValueMinimumPropertyId: return PM_RangeValue_Minimum;
    case UIA_RangeValueMaximumPropertyId: return PM_RangeValue_Maximum;
    case UIA_RangeValueLargeChangePropertyId: return PM_RangeValue_LargeChange;
    case UIA_RangeValueSmallChangePropertyId: return PM_RangeValue_SmallChange;
    case UIA_ScrollHorizontalScrollPercentPropertyId: return PM_Scroll_HorizontalScrollPercent;
    case UIA_ScrollHorizontalViewSizePropertyId: return PM_Scroll_HorizontalViewSize;
    case UIA_ScrollVerticalScrollPercentPropertyId: return PM_Scroll_VerticalScrollPercent;
    case UIA_ScrollVerticalViewSizePropertyId: return PM_Scroll_VerticalViewSize;
    case UIA_ScrollHorizontallyScrollablePropertyId: return PM_Scroll_HorizontallyScrollable;
    case UIA_ScrollVerticallyScrollablePropertyId: return PM_Scroll_VerticallyScrollable;
    case UIA_SelectionSelectionPropertyId: return PM_Selection_GetSelection;
    case UIA_SelectionCanSelectMultiplePropertyId: return PM_Selection_CanSelectMultiple;
    case UIA_SelectionIsSelectionRequiredPropertyId: return PM_Selection_IsSelectionRequired;
    case UIA_GridRowCountPropertyId: return PM_Grid_RowCount;
    case UIA_GridColumnCountPropertyId: return PM_Grid_ColumnCount;
    case UIA_GridItemRowPropertyId: return PM_GridItem_Row;
    case UIA_GridItemColumnPropertyId: return PM_GridItem_Column;
    case UIA_GridItemRowSpanPropertyId: return PM_GridItem_RowSpan;
    case UIA_GridItemColumnSpanPropertyId: return PM_GridItem_ColumnSpan;
    case UIA_GridItemContainingGridPropertyId: return PM_GridItem_ContainingGrid;
    case UIA_DockDockPositionPropertyId: return PM_Dock_DockPosition;
    case UIA_ExpandCollapseExpandCollapseStatePropertyId: return PM_ExpandCollapse_State;
    case UIA_MultipleViewCurrentViewPropertyId: return PM_MultipleView_CurrentView;
    case UIA_MultipleViewSupportedViewsPropertyId: return PM_MultipleView_GetSupportedViews;
    case UIA_WindowCanMaximizePropertyId: return PM_Window_CanMaximize;
    case UIA_WindowCanMinimizePropertyId: return PM_Window_CanMinimize;
    case UIA_WindowWindowVisualStatePropertyId: return PM_Window_VisualState;
    case UIA_WindowWindowInteractionStatePropertyId: return PM_Window_InteractionState;
    case UIA_WindowIsModalPropertyId: return PM_Window_IsModal;
    case UIA_WindowIsTopmostPropertyId: return PM_Window_IsTopmost;
    case UIA_SelectionItemIsSelectedPropertyId: return PM_SelectionItem_IsSelected;
    case UIA_SelectionItemSelectionContainerPropertyId: return PM_SelectionItem_SelectionContainer;
    case UIA_TableRowHeadersPropertyId: return PM_Table_GetRowHeaders;
    case UIA_TableColumnHeadersPropertyId: return PM_Table_GetColumnHeaders;
    case UIA_TableRowOrColumnMajorPropertyId: return PM_Table_RowOrColumnMajor;
    case UIA_TableItemRowHeaderItemsPropertyId: return PM_TableItem_GetRowHeaderItems;
    case UIA_TableItemColumnHeaderItemsPropertyId: return PM_TableItem_GetColumnHeaderItems;
    case UIA_ToggleToggleStatePropertyId: return PM_Toggle_State;
    case UIA_TransformCanMovePropertyId: return PM_Transform_CanMove;
    case UIA_TransformCanResizePropertyId: return PM_Transform_CanResize;
    case UIA_TransformCanRotatePropertyId: return PM_Transform_CanRotate;
    case UIA_LegacyIAccessibleChildIdPropertyId: return PM_Legacy_ChildId;
    case UIA_LegacyIAccessibleNamePropertyId: return PM_Legacy_Name;
    case UIA_LegacyIAccessibleValuePropertyId: return PM_Legacy_Value;
    case UIA_LegacyIAccessibleDescriptionPropertyId: return PM_Legacy_Description;
    case UIA_LegacyIAccessibleRolePropertyId: return PM_Legacy_Role;
    case UIA_LegacyIAccessibleStatePropertyId: return PM_Legacy_State;
    case UIA_LegacyIAccessibleHelpPropertyId: return PM_Legacy_Help;
    case UIA_LegacyIAccessibleKeyboardShortcutPropertyId: return PM_Legacy_KeyboardShortcut;
    case UIA_LegacyIAccessibleSelectionPropertyId: return PM_Legacy_GetSelection;
    case UIA_LegacyIAccessibleDefaultActionPropertyId: return PM_Legacy_DefaultAction;
    case UIA_AnnotationAnnotationTypeIdPropertyId: return PM_Annotation_TypeId;
    case UIA_AnnotationAnnotationTypeNamePropertyId: return PM_Annotation_TypeName;
    case UIA_AnnotationAuthorPropertyId: return PM_Annotation_Author;
    case UIA_AnnotationDateTimePropertyId: return PM_Annotation_DateTime;
    case UIA_AnnotationTargetPropertyId: return PM_Annotation_Target;
    case UIA_StylesStyleIdPropertyId: return PM_Styles_StyleId;
    case UIA_StylesStyleNamePropertyId: return PM_Styles_StyleName;
    case UIA_StylesFillColorPropertyId: return PM_Styles_FillColor;
    case UIA_StylesFillPatternStylePropertyId: return PM_Styles_FillPatternStyle;
    case UIA_StylesShapePropertyId: return PM_Styles_Shape;
    case UIA_StylesFillPatternColorPropertyId: return PM_Styles_FillPatternColor;
    case UIA_StylesExtendedPropertiesPropertyId: return PM_Styles_ExtendedProperties;
    case UIA_SpreadsheetItemFormulaPropertyId: return PM_SpreadsheetItem_Formula;
    case UIA_SpreadsheetItemAnnotationObjectsPropertyId: return PM_SpreadsheetItem_GetAnnotationObjects;
    case UIA_SpreadsheetItemAnnotationTypesPropertyId: return PM_SpreadsheetItem_GetAnnotationTypes;
    case UIA_Transform2CanZoomPropertyId: return PM_Transform2_CanZoom;
    case UIA_Transform2ZoomLevelPropertyId: return PM_Transform2_ZoomLevel;
    case UIA_Transform2ZoomMinimumPropertyId: return PM_Transform2_ZoomMinimum;
    case UIA_Transform2ZoomMaximumPropertyId: return PM_Transform2_ZoomMaximum;
    case UIA_DragIsGrabbedPropertyId: return PM_Drag_IsGrabbed;
    case UIA_DragDropEffectPropertyId: return PM_Drag_DropEffect;
    case UIA_DragDropEffectsPropertyId: return PM_Drag_DropEffects;
    case UIA_DragGrabbedItemsPropertyId: return PM_Drag_GetGrabbedItems;
    case UIA_DropTargetDropTargetEffectPropertyId: return PM_DropTarget_Effect;
    case UIA_DropTargetDropTargetEffectsPropertyId: return PM_DropTarget_Effects;
    case UIA_Selection2FirstSelectedItemPropertyId: return PM_Selection2_FirstSelectedItem;
    case UIA_Selection2LastSelectedItemPropertyId: return PM_Selection2_LastSelectedItem;
    case UIA_Selection2CurrentSelectedItemPropertyId: return PM_Selection2_CurrentSelectedItem;
    case UIA_Selection2ItemCountPropertyId: return PM_Selection2_ItemCount;
    }
    return -1;
}

/* a pattern from an object that crossed from another thread or process or from a provider of this one. */
HRESULT PatternFromUnknown(IUnknown *unknown, int id, PATTERN **out)
{
    *out = nullptr;
    if (!unknown)
        return S_OK;
    if (SUCCEEDED(unknown->lpVtbl->QueryInterface(unknown, &IID_IMimeUiaPattern, (void **)out)))
        return S_OK;
    IDispatch *dispatch = nullptr;
    if (SUCCEEDED(unknown->lpVtbl->QueryInterface(unknown, &IID_IDispatch, (void **)&dispatch))) {
        HRESULT hr = RemotePatternFromDispatch(dispatch, id, out);
        dispatch->lpVtbl->Release(dispatch);
        return hr;
    }
    return PatternFromProvider(unknown, id, nullptr, out);
}
