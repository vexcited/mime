/* uiautomationcore.dll internals shared by its source files. */
#ifndef MIME_UIA_PRIVATE_H
#define MIME_UIA_PRIVATE_H

#define UIAAPI
#include <mime/win32/uiautomationcore.h>
#include <mime/win32/winuser.h>
#include <mime/win32/objidl.h>
#include <mime/win32/rpc.h>

#ifndef CO_E_OBJNOTCONNECTED
#define CO_E_OBJNOTCONNECTED ((HRESULT)0x800401FDL)
#endif
#ifndef RPC_E_SERVER_DIED_DNE
#define RPC_E_SERVER_DIED_DNE ((HRESULT)0x80010012L)
#endif
#ifndef RPC_E_CANTCALLOUT_ININPUTSYNCCALL
#define RPC_E_CANTCALLOUT_ININPUTSYNCCALL ((HRESULT)0x8001010DL)
#endif

WINUSERAPI BOOL WINAPI IsWinEventHookInstalled(DWORD event);

/* the error a provider's death or removal turns into. */
#define UIA_RPC_FAILED(hr)                                                                                             \
    ((hr) == RPC_E_DISCONNECTED || (hr) == RPC_E_SERVERFAULT || (hr) == RPC_E_SERVER_DIED ||                           \
     (hr) == RPC_E_SERVER_DIED_DNE || (hr) == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE) ||                         \
     (hr) == HRESULT_FROM_WIN32(RPC_S_CALL_FAILED) || (hr) == CO_E_OBJNOTCONNECTED)

#define VT_NODE_ARRAY (VT_ARRAY | VT_UNKNOWN)

/* ------------------------------------------------------------------ memory and values */

void UiaEnsureCom(void);
void *UiaAlloc(SIZE_T size);
void UiaFree(void *p);
WCHAR *UiaDupString(const WCHAR *s);

HRESULT UiaIntArrayToSafeArray(const int *values, int count, SAFEARRAY **out);
HRESULT UiaSafeArrayToIntArray(SAFEARRAY *array, int **values, int *count);
HRESULT UiaDoubleArrayToSafeArray(const double *values, int count, SAFEARRAY **out);
int UiaCompareRuntimeIds(const int *a, int an, const int *b, int bn);

/* the properties the schema knows. */
typedef struct PROPERTY_INFO {
    int id;
    unsigned short vt;
    unsigned char default_kind;
    int pattern;
    int availability;
    const WCHAR *name;
} PROPERTY_INFO;
const PROPERTY_INFO *UiaPropertyInfo(int id);
HRESULT UiaDefaultValue(int kind, VARIANT *out);
const WCHAR *UiaPatternName(int id);
const WCHAR *UiaEventName(int id);
const WCHAR *UiaControlTypeName(int id);
const WCHAR *UiaLocalizedControlType(int id);

/* ------------------------------------------------------------------ nodes */

typedef struct NODE NODE;
typedef struct PATTERN PATTERN;

typedef struct NODE_OPS {
    void (*destroy)(NODE *);
    /* the raw answer of the provider, empty when it has none. */
    HRESULT (*get_prop)(NODE *, PROPERTYID, VARIANT *);
    /* a null pattern when the provider has none. */
    HRESULT (*get_pattern)(NODE *, PATTERNID, PATTERN **);
    HRESULT (*navigate)(NODE *, NavigateDirection, NODE **);
    HRESULT (*runtime_id)(NODE *, int **ids, int *count);
    HRESULT (*options)(NODE *, int *);
    /* the window of the host provider, zero without one. */
    HRESULT (*host_hwnd)(NODE *, HWND *);
    HRESULT (*set_focus)(NODE *);
    HRESULT (*root_from_point)(NODE *, double x, double y, NODE **);
    HRESULT (*root_focus)(NODE *, NODE **);
    HRESULT (*fragment_root)(NODE *, NODE **);
    HRESULT (*embedded_roots)(NODE *, NODE ***, int *);
    HRESULT (*process_id)(NODE *, DWORD *);
    HRESULT (*advise)(NODE *, BOOL added, int event, SAFEARRAY *props);
    /* the provider the node stands for, null for nodes that are not providers. */
    IRawElementProviderSimple *(*provider)(NODE *);
    BOOL (*is_fragment)(NODE *);
} NODE_OPS;

#define UIA_HANDLE_MAGIC 0x314D4955

typedef struct NODE {
    const IUnknownVtbl *lpVtbl;
    DWORD magic;
    LONG refs;
    const NODE_OPS *ops;
    /* set once the provider went away. */
    volatile LONG disconnected;
} NODE;

typedef struct PATTERN_OPS {
    void (*destroy)(PATTERN *);
    HRESULT (*call)(PATTERN *, int method, VARIANT *args, ULONG count, VARIANT *ret);
    NODE *(*owner)(PATTERN *);
} PATTERN_OPS;

typedef struct PATTERN {
    const IUnknownVtbl *lpVtbl;
    DWORD magic;
    LONG refs;
    const PATTERN_OPS *ops;
    PATTERNID id;
} PATTERN;

extern const IID IID_IMimeUiaNode, IID_IMimeUiaPattern;

void *NodeAlloc(SIZE_T size, const NODE_OPS *ops);
void *PatternAlloc(SIZE_T size, const PATTERN_OPS *ops, PATTERNID id);
void NodeAddRef(NODE *n);
void NodeRelease(NODE *n);
void PatternAddRef(PATTERN *p);
void PatternRelease(PATTERN *p);

/* nodes and patterns from what providers and wrappers hand over. */
HRESULT NodeFromUnknown(IUnknown *unknown, NODE **out);
HRESULT PatternFromUnknown(IUnknown *unknown, int id, PATTERN **out);
/* a provider of this process as a node. */
HRESULT NodeFromProvider(IRawElementProviderSimple *provider, NODE **out);
/* values with providers inside turned into nodes. */
HRESULT NormalizeValue(VARIANT *value);

HRESULT NodeGetProperty(NODE *n, PROPERTYID id, BOOL ignore_default, VARIANT *out);
HRESULT NodeGetPattern(NODE *n, PATTERNID id, PATTERN **out);
HRESULT NodeRuntimeId(NODE *n, int **ids, int *count);
HRESULT NodeParentView(NODE *n, NODE **out);
HRESULT NodeNavigate(NODE *n, NavigateDirection dir, NODE **out);
BOOL NodeSame(NODE *a, NODE *b);
HRESULT NodeProcessId(NODE *n, DWORD *pid);
HRESULT NodeBoundingRect(NODE *n, UiaRect *rect, BOOL *has);
HRESULT NodeClickablePoint(NODE *n, POINT *point, BOOL *has);
HWND NodeOwnerWindow(NODE *n);

/* provider answers kept honest: wrong types are no answer. */
HRESULT UiaCoerceProperty(int id, VARIANT *value);

/* ------------------------------------------------------------------ pattern calls */

#define PATTERN_METHODS(X)                                                                                             \
    X(Invoke_Invoke)                                                                                                   \
    X(Selection_GetSelection) X(Selection_CanSelectMultiple) X(Selection_IsSelectionRequired)                          \
    X(Value_SetValue) X(Value_Value) X(Value_IsReadOnly)                                                               \
    X(RangeValue_SetValue) X(RangeValue_Value) X(RangeValue_IsReadOnly) X(RangeValue_Maximum) X(RangeValue_Minimum)    \
    X(RangeValue_LargeChange) X(RangeValue_SmallChange)                                                                \
    X(Scroll_Scroll) X(Scroll_SetScrollPercent) X(Scroll_HorizontalScrollPercent) X(Scroll_VerticalScrollPercent)      \
    X(Scroll_HorizontalViewSize) X(Scroll_VerticalViewSize) X(Scroll_HorizontallyScrollable)                           \
    X(Scroll_VerticallyScrollable)                                                                                     \
    X(ExpandCollapse_Expand) X(ExpandCollapse_Collapse) X(ExpandCollapse_State)                                        \
    X(Grid_GetItem) X(Grid_RowCount) X(Grid_ColumnCount)                                                               \
    X(GridItem_Row) X(GridItem_Column) X(GridItem_RowSpan) X(GridItem_ColumnSpan) X(GridItem_ContainingGrid)          \
    X(MultipleView_GetViewName) X(MultipleView_SetCurrentView) X(MultipleView_CurrentView)                             \
    X(MultipleView_GetSupportedViews)                                                                                  \
    X(Window_SetVisualState) X(Window_Close) X(Window_WaitForInputIdle) X(Window_CanMaximize) X(Window_CanMinimize)    \
    X(Window_IsModal) X(Window_VisualState) X(Window_InteractionState) X(Window_IsTopmost)                             \
    X(SelectionItem_Select) X(SelectionItem_AddToSelection) X(SelectionItem_RemoveFromSelection)                       \
    X(SelectionItem_IsSelected) X(SelectionItem_SelectionContainer)                                                    \
    X(Dock_SetDockPosition) X(Dock_DockPosition)                                                                       \
    X(Table_GetRowHeaders) X(Table_GetColumnHeaders) X(Table_RowOrColumnMajor)                                         \
    X(TableItem_GetRowHeaderItems) X(TableItem_GetColumnHeaderItems)                                                   \
    X(Toggle_Toggle) X(Toggle_State)                                                                                   \
    X(Transform_Move) X(Transform_Resize) X(Transform_Rotate) X(Transform_CanMove) X(Transform_CanResize)              \
    X(Transform_CanRotate)                                                                                             \
    X(ScrollItem_ScrollIntoView)                                                                                       \
    X(Legacy_Select) X(Legacy_DoDefaultAction) X(Legacy_SetValue) X(Legacy_GetIAccessible) X(Legacy_ChildId)           \
    X(Legacy_Name) X(Legacy_Value) X(Legacy_Description) X(Legacy_Role) X(Legacy_State) X(Legacy_Help)                 \
    X(Legacy_KeyboardShortcut) X(Legacy_GetSelection) X(Legacy_DefaultAction)                                          \
    X(ItemContainer_FindItemByProperty)                                                                                \
    X(Virtualized_Realize)                                                                                             \
    X(SyncInput_StartListening) X(SyncInput_Cancel)                                                                    \
    X(Text_GetSelection) X(Text_GetVisibleRanges) X(Text_RangeFromChild) X(Text_RangeFromPoint)                        \
    X(Text_DocumentRange) X(Text_SupportedTextSelection) X(Text_RangeFromAnnotation) X(Text_GetCaretRange)             \
    X(Range_Clone) X(Range_Compare) X(Range_CompareEndpoints) X(Range_ExpandToEnclosingUnit) X(Range_FindAttribute)    \
    X(Range_FindText) X(Range_GetAttributeValue) X(Range_GetBoundingRectangles) X(Range_GetEnclosingElement)           \
    X(Range_GetText) X(Range_Move) X(Range_MoveEndpointByUnit) X(Range_MoveEndpointByRange) X(Range_Select)            \
    X(Range_AddToSelection) X(Range_RemoveFromSelection) X(Range_ScrollIntoView) X(Range_GetChildren)                  \
    X(Annotation_TypeId) X(Annotation_TypeName) X(Annotation_Author) X(Annotation_DateTime) X(Annotation_Target)       \
    X(Styles_StyleId) X(Styles_StyleName) X(Styles_FillColor) X(Styles_FillPatternStyle) X(Styles_Shape)               \
    X(Styles_FillPatternColor) X(Styles_ExtendedProperties)                                                            \
    X(Spreadsheet_GetItemByName) X(SpreadsheetItem_Formula) X(SpreadsheetItem_GetAnnotationObjects)                    \
    X(SpreadsheetItem_GetAnnotationTypes)                                                                              \
    X(Drag_IsGrabbed) X(Drag_DropEffect) X(Drag_DropEffects) X(Drag_GetGrabbedItems)                                   \
    X(DropTarget_Effect) X(DropTarget_Effects)                                                                         \
    X(Transform2_CanZoom) X(Transform2_ZoomLevel) X(Transform2_ZoomMinimum) X(Transform2_ZoomMaximum)                  \
    X(Transform2_Zoom) X(Transform2_ZoomByUnit)                                                                        \
    X(Selection2_FirstSelectedItem) X(Selection2_LastSelectedItem) X(Selection2_CurrentSelectedItem)                   \
    X(Selection2_ItemCount)                                                                                            \
    X(TextChild_TextContainer) X(TextChild_TextRange)                                                                  \
    X(TextEdit_GetActiveComposition) X(TextEdit_GetConversionTarget)                                                   \
    X(CustomNavigation_Navigate)                                                                                       \
    X(ObjectModel_GetUnderlyingObjectModel)

typedef enum PATTERN_METHOD {
#define X(name) PM_##name,
    PATTERN_METHODS(X)
#undef X
    PM_COUNT
} PATTERN_METHOD;

/* a text range is a pattern whose id is this. */
#define PATTERN_ID_TEXTRANGE 0x7FFF0001

HRESULT PatternCall(PATTERN *p, int method, VARIANT *args, ULONG count, VARIANT *ret);
/* wraps what a provider returned for a pattern, null when it has no such interface. */
HRESULT PatternFromProvider(IUnknown *provider, PATTERNID id, NODE *owner, PATTERN **out);
/* the property a pattern property id is answered by, and the method that gets it. */
int PatternMethodOfProperty(PROPERTYID id);

/* ------------------------------------------------------------------ the transports */

/* a wrapper for a provider's node, served to other threads and processes. */
HRESULT ServerWrapNode(NODE *node, IDispatch **out);
HRESULT RemoteNodeFromDispatch(IDispatch *dispatch, NODE **out);
HRESULT RemotePatternFromDispatch(IDispatch *dispatch, int id, PATTERN **out);
/* runs on the thread a wrapper was made on. */
typedef HRESULT (*UIA_THREAD_FN)(void *);
HRESULT UiaRunOnThread(DWORD thread_id, UIA_THREAD_FN fn, void *context);
extern const IID IID_IMimeUiaServer;

/* ------------------------------------------------------------------ native providers */

HRESULT ProxyNodeFromWindow(HWND hwnd, NODE **out);
HRESULT ProxyDesktopNode(NODE **out);
HRESULT ProxyNodeFromPoint(POINT pt, NODE **out);
HRESULT ProxyNodeFromFocus(NODE **out);
HRESULT ProxyNodeFromAccessible(IAccessible *acc, LONG child, NODE **out);
/* the node of a window: its provider when it answers WM_GETOBJECT, its proxy otherwise. */
HRESULT UiaNodeForWindow(HWND hwnd, NODE **out);
BOOL ProxyHasServerSideProvider(HWND hwnd);

/* ------------------------------------------------------------------ providers and events */

HRESULT ProviderOfNode(NODE *node, IRawElementProviderSimple **out);
HRESULT AccessibleWrapProvider(IRawElementProviderSimple *provider, IAccessible **out, LONG *child, HWND hwnd);
void UiaEventsProcessDetach(void);

typedef struct EVENT_ARGS {
    EventArgsType type;
    int event_id;
    PROPERTYID property;
    VARIANT old_value, new_value;
    StructureChangeType structure;
    int *runtime_id;
    int runtime_len;
    TextEditChangeType edit_type;
    SAFEARRAY *edit_data;
    AsyncContentLoadedState loaded_state;
    double loaded_percent;
    int notification_kind, notification_processing;
    BSTR display, activity;
    int change_count;
    UiaChangeInfo *changes;
} EVENT_ARGS;

/* delivers an event of a provider of this process to the clients that listen. */
HRESULT UiaDispatchEvent(NODE *source, const EVENT_ARGS *args);
BOOL UiaAnyClientListening(void);
/* clients register interest where providers of every process see it. */
HRESULT UiaRegistryAdd(DWORD kind, int event_id, int scope, HWND window, const int *props, int nprops, const int *rid,
                       int rid_len, IUnknown *sink, DWORD *cookie);
void UiaRegistryRemove(DWORD cookie);
void UiaRegistryAddWindow(DWORD cookie, HWND hwnd);
void UiaRegistryRemoveWindow(DWORD cookie, HWND hwnd);
/* providers announce events that ask for them. */
void UiaProviderWatchStart(void);
void UiaProviderWatchRemember(NODE *root);

typedef enum EVENT_KIND {
    EK_AUTOMATION = 1,
    EK_PROPERTY,
    EK_STRUCTURE,
    EK_FOCUS,
    EK_TEXT_EDIT,
    EK_CHANGES,
    EK_NOTIFICATION,
    EK_ACTIVE_TEXT,
} EVENT_KIND;

/* the dispids of an event sink. */
#define SINK_DISPID_EVENT 1


/* ------------------------------------------------------------------ the client objects */

typedef struct COND COND;
COND *CondNew(int type);
void CondAddRef(COND *c);
void CondRelease(COND *c);
int CondType(COND *c);
int CondProperty(COND *c);
int CondFlags(COND *c);
int CondCount(COND *c);
COND *CondChild(COND *c, int i);
const VARIANT *CondValue(COND *c);
HRESULT CondProperty_(int property, const VARIANT *value, int flags, COND **out);
HRESULT CondGroup(int type, COND **items, int count, COND **out);
HRESULT CondNot(COND *child, COND **out);
HRESULT CondFromStruct(const UiaCondition *in, COND **out);
HRESULT CondMatch(COND *c, NODE *n, BOOL *match);

HRESULT ViewParent(NODE *n, COND *view, NODE **out);
HRESULT ViewChildren(NODE *n, COND *view, NODE ***list, int *count);
HRESULT ViewStep(NODE *n, COND *view, int direction, NODE **out);
void NodeListFree(NODE **list, int count);
HRESULT NodeFind(NODE *root, int scope, COND *condition, BOOL first, int options, NODE *ancestors_root, NODE ***list,
                 int *count);

/* what a client wants fetched with an element. */
typedef struct REQUEST {
    COND *view;
    int scope;
    int *props;
    int nprops;
    int *patterns;
    int npatterns;
    int mode;
} REQUEST;

void RequestInit(REQUEST *r);
void RequestClear(REQUEST *r);
HRESULT RequestCopy(REQUEST *to, const REQUEST *from);
HRESULT RequestFromStruct(const UiaCacheRequest *in, REQUEST *out);

typedef struct ELEMENT ELEMENT;
HRESULT ElementCreate(NODE *node, const REQUEST *request, ELEMENT **out);
NODE *ElementNode(ELEMENT *e);
HRESULT ElementFromCom(IUnknown *unknown, ELEMENT **out);
void ElementAddRef(ELEMENT *e);
void ElementRelease(ELEMENT *e);
HRESULT ElementArrayCreate(ELEMENT **items, int count, IUnknown **out);
/* elements for nodes, the nodes given up. */
HRESULT ElementsFromNodes(NODE **nodes, int count, const REQUEST *request, ELEMENT ***items);
HRESULT ElementValueEx(ELEMENT *e, PROPERTYID id, BOOL current, BOOL ignore_default, VARIANT *out);
HRESULT RequestCreate(IUnknown **out);
HRESULT ControlViewCondition(COND **out);
HRESULT ContentViewCondition(COND **out);
HRESULT WalkerCreate(COND *cond, IUnknown **out);
HRESULT ElementCachedValue(ELEMENT *e, PROPERTYID id, VARIANT *out);
HRESULT ElementCachedPattern(ELEMENT *e, PATTERNID id, PATTERN **out, BOOL *cached);
/* makes the node-valued parts of a value elements. */
HRESULT ValueToElements(VARIANT *v, const REQUEST *request);
/* the flat api turns a tree of cached elements into data. */
HRESULT ElementTreeToData(ELEMENT *root, const REQUEST *request, SAFEARRAY **data, BSTR *structure);
HRESULT ClientPatternCreate(ELEMENT *e, PATTERNID id, PATTERN *pattern, BOOL cached, REFIID riid, void **out);
HRESULT ClientRangeCreate(ELEMENT *e, PATTERN *range, BOOL cached, IUnknown **out);
IUnknown *UiaClientRegistry(void);
IUnknown *UiaNotSupportedValue(void);
IUnknown *UiaMixedValue(void);
/* the condition or request an interface pointer of a client stands for. */
HRESULT CondFromCom(IUnknown *unknown, COND **out);
HRESULT RequestFromCom(IUnknown *unknown, REQUEST *out);
HRESULT CondToCom(COND *c, IUnknown **out);

extern const IID IID_IMimeElement;

/* ------------------------------------------------------------------ the client */

HRESULT UiaClientNodeFromWindow(HWND hwnd, NODE **out);
HRESULT UiaClientNodeFromPoint(POINT pt, NODE **out);
HRESULT UiaClientNodeFromFocus(NODE **out);
void UiaProviderTableAdd(HWND hwnd);
BOOL UiaProviderTableHas(HWND hwnd);
void UiaSetErrorDescription(const WCHAR *text);
HRESULT UiaClientRootNode(NODE **out);

#endif
