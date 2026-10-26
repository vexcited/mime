/*
 * Windows.UI.Composition: the visual layer windows programs build trees in and show in a window of theirs. A
 * compositor made by a compositor controller owns the objects, a desktop window target puts the tree of visuals into
 * a window, and brushes show swap chains and colors. The classes keep the tree; nothing draws yet.
 *
 * every interface is a table of functions in the order of its metadata. The ones a program has not needed are
 * filled with a function that logs the object and the slot and answers E_NOTIMPL, so a program that wants more shows
 * what in the log.
 *
 * the classes are for 64 bit processes. A function of a 32 bit table takes back its own arguments, which a log only
 * stub cannot know, so the classes are not served to 32 bit programs.
 */
#include "combase_private.h"
#include <mime/winrt_object.h>
#include <mime/win32/winuser.h>

#ifdef _WIN64

const WCHAR CbCompositorControllerClass[] = L"Windows.UI.Composition.Core.CompositorController";
const WCHAR CbCompositorClass[] = L"Windows.UI.Composition.Compositor";

MIME_DEFINE_GUID(IID_ICompositorController, 0x2D75F35A, 0x70A7, 0x4395, 0xBA, 0x2D, 0xCE, 0xF0, 0xB1, 0x83, 0x99, 0xF9);
MIME_DEFINE_GUID(IID_ICompositor, 0xB403CA50, 0x7F8C, 0x4E83, 0x98, 0x5F, 0xCC, 0x45, 0x06, 0x00, 0x36, 0xD8);
MIME_DEFINE_GUID(IID_ICompositorInterop, 0x25297D5C, 0x3AD4, 0x4C9C, 0xB5, 0xCF, 0xE3, 0x6A, 0x38, 0x51, 0x23, 0x30);
MIME_DEFINE_GUID(IID_ICompositorDesktopInterop, 0x29E691FA, 0x4567, 0x4DCA, 0xB3, 0x19, 0xD0, 0xF2, 0x07, 0xEB, 0x68, 0x07);
MIME_DEFINE_GUID(IID_ICompositionObject, 0xBCB4AD45, 0x7609, 0x4550, 0x93, 0x4F, 0x16, 0x00, 0x2A, 0x68, 0xFD, 0xED);
MIME_DEFINE_GUID(IID_ICompositionTarget, 0xA1BEA8BA, 0xD726, 0x4663, 0x81, 0x29, 0x6B, 0x5E, 0x79, 0x27, 0xFF, 0xA6);
MIME_DEFINE_GUID(IID_IDesktopWindowTarget, 0x6329D6CA, 0x3366, 0x490E, 0x9D, 0xB3, 0x25, 0x31, 0x29, 0x29, 0xAC, 0x51);
MIME_DEFINE_GUID(IID_IVisual, 0x117E202D, 0xA859, 0x4C89, 0x87, 0x3B, 0xC2, 0xAA, 0x56, 0x67, 0x88, 0xE3);
MIME_DEFINE_GUID(IID_IVisual2, 0x3052B611, 0x56C3, 0x4C3E, 0x8B, 0xF3, 0xF6, 0xE1, 0xAD, 0x47, 0x3F, 0x06);
MIME_DEFINE_GUID(IID_IContainerVisual, 0x02F6BC74, 0xED20, 0x4773, 0xAF, 0xE6, 0xD4, 0x9B, 0x4A, 0x93, 0xDB, 0x32);
MIME_DEFINE_GUID(IID_ISpriteVisual, 0x08E05581, 0x1AD1, 0x4F97, 0x97, 0x57, 0x40, 0x2D, 0x76, 0xE4, 0x23, 0x3B);
MIME_DEFINE_GUID(IID_IVisualCollection, 0x8B745505, 0xFD3E, 0x4A98, 0x84, 0xA8, 0xE9, 0x49, 0x46, 0x8C, 0x6B, 0xCB);

typedef struct {
    float X, Y;
} VECTOR2;

typedef struct {
    INT64 Value;
} EVENT_TOKEN;

/* what each interface is: its iid and how many slots its table has, the inspectable ones counting their six. */
enum {
    I_CONTROLLER,
    I_COMPOSITOR,
    I_COMPOSITOR_INTEROP,
    I_DESKTOP_INTEROP,
    I_OBJECT,
    I_TARGET,
    I_DESKTOP_TARGET,
    I_VISUAL,
    I_VISUAL2,
    I_CONTAINER,
    I_SPRITE,
    I_COLLECTION,
    I_FACTORY,
    I_COUNT
};

#define MAX_SLOTS 64

static const struct {
    const IID *Iid;
    ULONG Slots;
    BOOLEAN Inspectable;
} g_interfaces[I_COUNT] = {
    [I_CONTROLLER] = {&IID_ICompositorController, 11, TRUE},
    [I_COMPOSITOR] = {&IID_ICompositor, 30, TRUE},
    [I_COMPOSITOR_INTEROP] = {&IID_ICompositorInterop, 6, FALSE},
    [I_DESKTOP_INTEROP] = {&IID_ICompositorDesktopInterop, 5, FALSE},
    [I_OBJECT] = {&IID_ICompositionObject, 11, TRUE},
    [I_TARGET] = {&IID_ICompositionTarget, 8, TRUE},
    [I_DESKTOP_TARGET] = {&IID_IDesktopWindowTarget, 7, TRUE},
    [I_VISUAL] = {&IID_IVisual, 39, TRUE},
    [I_VISUAL2] = {&IID_IVisual2, 12, TRUE},
    [I_CONTAINER] = {&IID_IContainerVisual, 7, TRUE},
    [I_SPRITE] = {&IID_ISpriteVisual, 8, TRUE},
    [I_COLLECTION] = {&IID_IVisualCollection, 13, TRUE},
    [I_FACTORY] = {&IID_IActivationFactory, 7, TRUE},
};

/* ------------------------------------------------------------------ objects */

typedef enum {
    K_CONTROLLER,
    K_COMPOSITOR,
    K_TARGET,
    K_CONTAINER,
    K_SPRITE,
    K_COLLECTION,
    K_COUNT
} KIND;

typedef struct WUC WUC;

struct WUC {
    WRT_OBJECT Base;
    KIND Kind;
    /* the compositor that made the object, or for a compositor the controller it came from. */
    WUC *Owner;
    HWND Window;
    BOOLEAN Topmost;
    WUC *Root;
    /* a visual in a collection has a parent; a collection belongs to a container. */
    WUC *Parent;
    WUC **Children;
    ULONG ChildCount, ChildCapacity;
    VECTOR2 RelativeSize;
    LONG NextToken;
};

static const void *g_tables[I_COUNT][MAX_SLOTS];
static INIT_ONCE g_tables_once = INIT_ONCE_STATIC_INIT;

static WUC *wuc_of(void *iface)
{
    return CONTAINING_RECORD(((WRT_IFACE *)iface)->Owner, WUC, Base);
}

/* ------------------------------------------------------------------ unimplemented slots */

static const char *const g_kind_names[K_COUNT] = {"controller", "compositor", "target", "container", "sprite", "collection"};

static HRESULT unimplemented(void *This, ULONG slot)
{
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) < 400) {
        char text[120];
        _snprintf(text, sizeof(text), "composition: %s slot %u is not implemented\n", g_kind_names[wuc_of(This)->Kind], slot);
        OutputDebugStringA(text);
    }
    return E_NOTIMPL;
}

#define ALL_SLOTS(X)                                                                                                   \
    X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) \
    X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15) \
    X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) \
    X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) \
    X(32) X(33) X(34) X(35) X(36) X(37) X(38) X(39) \
    X(40) X(41) X(42) X(43) X(44) X(45) X(46) X(47) \
    X(48) X(49) X(50) X(51) X(52) X(53) X(54) X(55) \
    X(56) X(57) X(58) X(59) X(60) X(61) X(62) X(63)

#define STUB(n)                                                                                                        \
    static HRESULT STDMETHODCALLTYPE stub_##n(void *This)                                                              \
    {                                                                                                                  \
        return unimplemented(This, n);                                                                                 \
    }
ALL_SLOTS(STUB)
#undef STUB

#define STUB_ENTRY(n) [n] = (const void *)stub_##n,
static const void *const g_stubs[MAX_SLOTS] = {ALL_SLOTS(STUB_ENTRY)};
#undef STUB_ENTRY

/* ------------------------------------------------------------------ the inspectable base */

static HRESULT STDMETHODCALLTYPE base_query(void *This, REFIID riid, void **object)
{
    return WrtQuery((WRT_IFACE *)This, riid, object);
}

static ULONG STDMETHODCALLTYPE base_add_ref(void *This)
{
    return WrtAddRef((WRT_IFACE *)This);
}

static ULONG STDMETHODCALLTYPE base_release(void *This)
{
    return WrtRelease((WRT_IFACE *)This);
}

static HRESULT STDMETHODCALLTYPE base_iids(void *This, ULONG *count, IID **iids)
{
    return WrtGetIids((WRT_IFACE *)This, count, iids);
}

static HRESULT STDMETHODCALLTYPE base_class_name(void *This, HSTRING *name)
{
    return WrtGetRuntimeClassName((WRT_IFACE *)This, name);
}

static HRESULT STDMETHODCALLTYPE base_trust_level(void *This, TrustLevel *level)
{
    return WrtGetTrustLevel((WRT_IFACE *)This, level);
}

static void release_object(WUC *object)
{
    if (object)
        WrtRelease(&object->Base.Ifaces[0]);
}

/* ------------------------------------------------------------------ kinds */

static void destroy(WRT_OBJECT *base);

static const WRT_CLASS g_classes[K_COUNT] = {
    [K_CONTROLLER] = {CbCompositorControllerClass, 1, {&IID_ICompositorController}, 0, destroy},
    [K_COMPOSITOR] = {CbCompositorClass,
                      4,
                      {&IID_ICompositor, &IID_ICompositorInterop, &IID_ICompositorDesktopInterop, &IID_ICompositionObject},
                      0b0110,
                      destroy},
    [K_TARGET] = {L"Windows.UI.Composition.Desktop.DesktopWindowTarget",
                  3,
                  {&IID_IDesktopWindowTarget, &IID_ICompositionTarget, &IID_ICompositionObject},
                  0,
                  destroy},
    [K_CONTAINER] = {L"Windows.UI.Composition.ContainerVisual",
                     4,
                     {&IID_IContainerVisual, &IID_IVisual, &IID_IVisual2, &IID_ICompositionObject},
                     0,
                     destroy},
    [K_SPRITE] = {L"Windows.UI.Composition.SpriteVisual",
                  5,
                  {&IID_ISpriteVisual, &IID_IContainerVisual, &IID_IVisual, &IID_IVisual2, &IID_ICompositionObject},
                  0,
                  destroy},
    [K_COLLECTION] = {L"Windows.UI.Composition.VisualCollection", 1, {&IID_IVisualCollection}, 0, destroy},
};

/* the interface each slot of a class's iid list is, for building the tables of an object. */
static const int g_kind_ifaces[K_COUNT][WRT_MAX_INTERFACES] = {
    [K_CONTROLLER] = {I_CONTROLLER},
    [K_COMPOSITOR] = {I_COMPOSITOR, I_COMPOSITOR_INTEROP, I_DESKTOP_INTEROP, I_OBJECT},
    [K_TARGET] = {I_DESKTOP_TARGET, I_TARGET, I_OBJECT},
    [K_CONTAINER] = {I_CONTAINER, I_VISUAL, I_VISUAL2, I_OBJECT},
    [K_SPRITE] = {I_SPRITE, I_CONTAINER, I_VISUAL, I_VISUAL2, I_OBJECT},
    [K_COLLECTION] = {I_COLLECTION},
};

static WUC *make(KIND kind, WUC *owner)
{
    WUC *object = CbAlloc(sizeof(*object));
    if (!object)
        return nullptr;
    memset(object, 0, sizeof(*object));
    const void *tables[WRT_MAX_INTERFACES];
    for (ULONG i = 0; i < g_classes[kind].Count; i++)
        tables[i] = g_tables[g_kind_ifaces[kind][i]];
    WrtInit(&object->Base, &g_classes[kind], tables);
    object->Kind = kind;
    object->Owner = owner;
    if (owner)
        WrtAddRef(&owner->Base.Ifaces[0]);
    object->RelativeSize = (VECTOR2){0, 0};
    return object;
}

static void destroy(WRT_OBJECT *base)
{
    WUC *object = CONTAINING_RECORD(base, WUC, Base);
    for (ULONG i = 0; i < object->ChildCount; i++) {
        object->Children[i]->Parent = nullptr;
        release_object(object->Children[i]);
    }
    CbFree(object->Children);
    release_object(object->Root);
    release_object(object->Owner);
    CbFree(object);
}

/* ------------------------------------------------------------------ the compositor controller */

static HRESULT STDMETHODCALLTYPE controller_get_compositor(void *This, void **value)
{
    WUC *controller = wuc_of(This);
    if (!value)
        return E_POINTER;
    WUC *compositor = controller->Root;
    *value = &compositor->Base.Ifaces[0];
    WrtAddRef(&compositor->Base.Ifaces[0]);
    return S_OK;
}

/* nothing is drawn after a commit; the tree is the whole state. */
static HRESULT STDMETHODCALLTYPE controller_commit(void *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE controller_add_commit_needed(void *This, IUnknown *handler, EVENT_TOKEN *token)
{
    if (!token)
        return E_POINTER;
    token->Value = InterlockedIncrement(&wuc_of(This)->NextToken);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE controller_remove_commit_needed(void *This, EVENT_TOKEN token)
{
    return S_OK;
}

/* ------------------------------------------------------------------ the compositor */

static HRESULT new_visual(KIND kind, void *This, void **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    WUC *visual = make(kind, wuc_of(This));
    if (!visual)
        return E_OUTOFMEMORY;
    *value = &visual->Base.Ifaces[0];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE compositor_create_container_visual(void *This, void **value)
{
    return new_visual(K_CONTAINER, This, value);
}

static HRESULT STDMETHODCALLTYPE compositor_create_sprite_visual(void *This, void **value)
{
    return new_visual(K_SPRITE, This, value);
}

/* a program that did not ask for a controller gets the compositor of its own. */
static HRESULT STDMETHODCALLTYPE object_get_compositor(void *This, void **value)
{
    WUC *object = wuc_of(This);
    if (!value)
        return E_POINTER;
    WUC *compositor = object->Kind == K_COMPOSITOR ? object : object->Owner;
    *value = &compositor->Base.Ifaces[0];
    WrtAddRef(&compositor->Base.Ifaces[0]);
    return S_OK;
}

/* the window must be one of the calling process's. */
static HRESULT STDMETHODCALLTYPE desktop_create_target(void *This, HWND window, BOOL topmost, void **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    DWORD process = 0;
    if (!window || !IsWindow(window))
        return E_INVALIDARG;
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId())
        return E_ACCESSDENIED;
    WUC *target = make(K_TARGET, wuc_of(This));
    if (!target)
        return E_OUTOFMEMORY;
    target->Window = window;
    target->Topmost = !!topmost;
    *value = &target->Base.Ifaces[0];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE desktop_ensure_on_thread(void *This, DWORD thread)
{
    return S_OK;
}

/* ------------------------------------------------------------------ targets and visuals */

static HRESULT STDMETHODCALLTYPE target_get_root(void *This, void **value)
{
    WUC *target = wuc_of(This);
    if (!value)
        return E_POINTER;
    *value = nullptr;
    if (target->Root) {
        *value = &target->Root->Base.Ifaces[0];
        WrtAddRef(&target->Root->Base.Ifaces[0]);
    }
    return S_OK;
}

/* the root is any visual, which the caller holds as its default interface. */
static HRESULT STDMETHODCALLTYPE target_put_root(void *This, void *visual)
{
    WUC *target = wuc_of(This);
    WUC *root = visual ? wuc_of(visual) : nullptr;
    if (root && root->Kind != K_CONTAINER && root->Kind != K_SPRITE)
        return E_INVALIDARG;
    if (root)
        WrtAddRef(&root->Base.Ifaces[0]);
    release_object(target->Root);
    target->Root = root;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE target_get_is_topmost(void *This, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    *value = wuc_of(This)->Topmost;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual2_get_relative_size(void *This, VECTOR2 *value)
{
    if (!value)
        return E_POINTER;
    *value = wuc_of(This)->RelativeSize;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual2_put_relative_size(void *This, VECTOR2 value)
{
    wuc_of(This)->RelativeSize = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE container_get_children(void *This, void **value)
{
    WUC *container = wuc_of(This);
    if (!value)
        return E_POINTER;
    *value = nullptr;
    WUC *collection = make(K_COLLECTION, container->Owner);
    if (!collection)
        return E_OUTOFMEMORY;
    /* the collection holds its container, which holds the children. */
    collection->Parent = container;
    WrtAddRef(&container->Base.Ifaces[0]);
    *value = &collection->Base.Ifaces[0];
    return S_OK;
}

/* ------------------------------------------------------------------ the children of a visual */

static HRESULT STDMETHODCALLTYPE collection_get_count(void *This, INT32 *value)
{
    if (!value)
        return E_POINTER;
    *value = (INT32)wuc_of(This)->Parent->ChildCount;
    return S_OK;
}

static LONG child_index(WUC *container, WUC *child)
{
    for (ULONG i = 0; i < container->ChildCount; i++)
        if (container->Children[i] == child)
            return (LONG)i;
    return -1;
}

/* puts the child at index, taking it out of its place first. */
static HRESULT insert(WUC *container, void *visual, ULONG index)
{
    WUC *child = wuc_of(visual);
    if (child->Kind != K_CONTAINER && child->Kind != K_SPRITE)
        return E_INVALIDARG;
    for (WUC *up = container; up; up = up->Parent)
        if (up == child)
            return E_INVALIDARG;
    LONG at = child_index(container, child);
    if (at >= 0) {
        memmove(&container->Children[at], &container->Children[at + 1], (container->ChildCount - at - 1) * sizeof(WUC *));
        container->ChildCount--;
        if ((ULONG)at < index)
            index--;
    } else {
        if (child->Parent) {
            WUC *old = child->Parent;
            LONG from = child_index(old, child);
            if (from >= 0) {
                memmove(&old->Children[from], &old->Children[from + 1], (old->ChildCount - from - 1) * sizeof(WUC *));
                old->ChildCount--;
                release_object(child);
            }
        }
        if (container->ChildCount == container->ChildCapacity) {
            ULONG capacity = container->ChildCapacity ? container->ChildCapacity * 2 : 8;
            WUC **grown = CbAlloc(capacity * sizeof(WUC *));
            if (!grown)
                return E_OUTOFMEMORY;
            if (container->Children)
                memcpy(grown, container->Children, container->ChildCount * sizeof(WUC *));
            CbFree(container->Children);
            container->Children = grown;
            container->ChildCapacity = capacity;
        }
        WrtAddRef(&child->Base.Ifaces[0]);
    }
    if (index > container->ChildCount)
        index = container->ChildCount;
    memmove(&container->Children[index + 1], &container->Children[index], (container->ChildCount - index) * sizeof(WUC *));
    container->Children[index] = child;
    container->ChildCount++;
    child->Parent = container;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE collection_insert_above(void *This, void *visual, void *sibling)
{
    WUC *container = wuc_of(This)->Parent;
    LONG at = sibling ? child_index(container, wuc_of(sibling)) : -1;
    return insert(container, visual, at >= 0 ? (ULONG)at + 1 : container->ChildCount);
}

static HRESULT STDMETHODCALLTYPE collection_insert_at_bottom(void *This, void *visual)
{
    return insert(wuc_of(This)->Parent, visual, 0);
}

static HRESULT STDMETHODCALLTYPE collection_insert_at_top(void *This, void *visual)
{
    WUC *container = wuc_of(This)->Parent;
    return insert(container, visual, container->ChildCount);
}

static HRESULT STDMETHODCALLTYPE collection_insert_below(void *This, void *visual, void *sibling)
{
    WUC *container = wuc_of(This)->Parent;
    LONG at = sibling ? child_index(container, wuc_of(sibling)) : -1;
    return insert(container, visual, at >= 0 ? (ULONG)at : 0);
}

static HRESULT STDMETHODCALLTYPE collection_remove(void *This, void *visual)
{
    WUC *container = wuc_of(This)->Parent;
    WUC *child = wuc_of(visual);
    LONG at = child_index(container, child);
    if (at < 0)
        return S_OK;
    memmove(&container->Children[at], &container->Children[at + 1], (container->ChildCount - at - 1) * sizeof(WUC *));
    container->ChildCount--;
    child->Parent = nullptr;
    release_object(child);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE collection_remove_all(void *This)
{
    WUC *container = wuc_of(This)->Parent;
    while (container->ChildCount) {
        WUC *child = container->Children[--container->ChildCount];
        child->Parent = nullptr;
        release_object(child);
    }
    return S_OK;
}

/* ------------------------------------------------------------------ the tables */

static void fill(int iface, int slot, const void *function)
{
    g_tables[iface][slot] = function;
}

static BOOL CALLBACK build_tables(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    for (int i = 0; i < I_COUNT; i++) {
        ULONG first = 0;
        if (g_interfaces[i].Inspectable) {
            g_tables[i][0] = (const void *)base_query;
            g_tables[i][1] = (const void *)base_add_ref;
            g_tables[i][2] = (const void *)base_release;
            g_tables[i][3] = (const void *)base_iids;
            g_tables[i][4] = (const void *)base_class_name;
            g_tables[i][5] = (const void *)base_trust_level;
            first = 6;
        } else {
            g_tables[i][0] = (const void *)base_query;
            g_tables[i][1] = (const void *)base_add_ref;
            g_tables[i][2] = (const void *)base_release;
            first = 3;
        }
        for (ULONG s = first; s < g_interfaces[i].Slots; s++)
            g_tables[i][s] = g_stubs[s];
    }
    fill(I_CONTROLLER, 6, (const void *)controller_get_compositor);
    fill(I_CONTROLLER, 7, (const void *)controller_commit);
    fill(I_CONTROLLER, 9, (const void *)controller_add_commit_needed);
    fill(I_CONTROLLER, 10, (const void *)controller_remove_commit_needed);
    fill(I_COMPOSITOR, 9, (const void *)compositor_create_container_visual);
    fill(I_COMPOSITOR, 22, (const void *)compositor_create_sprite_visual);
    fill(I_OBJECT, 6, (const void *)object_get_compositor);
    fill(I_DESKTOP_INTEROP, 3, (const void *)desktop_create_target);
    fill(I_DESKTOP_INTEROP, 4, (const void *)desktop_ensure_on_thread);
    fill(I_TARGET, 6, (const void *)target_get_root);
    fill(I_TARGET, 7, (const void *)target_put_root);
    fill(I_DESKTOP_TARGET, 6, (const void *)target_get_is_topmost);
    fill(I_VISUAL2, 10, (const void *)visual2_get_relative_size);
    fill(I_VISUAL2, 11, (const void *)visual2_put_relative_size);
    fill(I_CONTAINER, 6, (const void *)container_get_children);
    fill(I_COLLECTION, 6, (const void *)collection_get_count);
    fill(I_COLLECTION, 7, (const void *)collection_insert_above);
    fill(I_COLLECTION, 8, (const void *)collection_insert_at_bottom);
    fill(I_COLLECTION, 9, (const void *)collection_insert_at_top);
    fill(I_COLLECTION, 10, (const void *)collection_insert_below);
    fill(I_COLLECTION, 11, (const void *)collection_remove);
    fill(I_COLLECTION, 12, (const void *)collection_remove_all);
    return TRUE;
}

/* ------------------------------------------------------------------ the factories */

/* both classes are made with no arguments; the controller brings the compositor it hands out. */
typedef struct {
    WRT_OBJECT Base;
    BOOLEAN Controller;
} FACTORY;

static HRESULT STDMETHODCALLTYPE factory_activate(void *This, void **instance)
{
    if (!instance)
        return E_POINTER;
    *instance = nullptr;
    FACTORY *factory = CONTAINING_RECORD(((WRT_IFACE *)This)->Owner, FACTORY, Base);
    WUC *compositor = make(K_COMPOSITOR, nullptr);
    if (!compositor)
        return E_OUTOFMEMORY;
    if (!factory->Controller) {
        *instance = &compositor->Base.Ifaces[0];
        return S_OK;
    }
    WUC *controller = make(K_CONTROLLER, nullptr);
    if (!controller) {
        release_object(compositor);
        return E_OUTOFMEMORY;
    }
    /* the controller keeps its compositor in the field a target keeps its root in. */
    controller->Root = compositor;
    *instance = &controller->Base.Ifaces[0];
    return S_OK;
}

static void factory_destroy(WRT_OBJECT *object)
{
    object->Refs = 1;
}

static const WRT_CLASS g_controller_factory_class = {CbCompositorControllerClass, 1, {&IID_IActivationFactory}, 0,
                                                     factory_destroy};
static const WRT_CLASS g_compositor_factory_class = {CbCompositorClass, 1, {&IID_IActivationFactory}, 0, factory_destroy};
static FACTORY g_controller_factory = {.Controller = TRUE}, g_compositor_factory;
static INIT_ONCE g_factories_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK make_factories(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    InitOnceExecuteOnce(&g_tables_once, build_tables, nullptr, nullptr);
    g_tables[I_FACTORY][6] = (const void *)factory_activate;
    const void *tables[1] = {g_tables[I_FACTORY]};
    WrtInit(&g_controller_factory.Base, &g_controller_factory_class, tables);
    WrtInit(&g_compositor_factory.Base, &g_compositor_factory_class, tables);
    return TRUE;
}

HRESULT CbCompositorControllerFactory(REFIID riid, void **factory)
{
    InitOnceExecuteOnce(&g_factories_once, make_factories, nullptr, nullptr);
    return WrtQuery(&g_controller_factory.Base.Ifaces[0], riid, factory);
}

HRESULT CbCompositorFactory(REFIID riid, void **factory)
{
    InitOnceExecuteOnce(&g_factories_once, make_factories, nullptr, nullptr);
    return WrtQuery(&g_compositor_factory.Base.Ifaces[0], riid, factory);
}

#endif
