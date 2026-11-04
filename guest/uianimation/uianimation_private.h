#ifndef UIANIMATION_PRIVATE_H
#define UIANIMATION_PRIVATE_H

#include <mime/win32/uianimation.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>

#define VTBL_FN(f) ((void *)(f))

/* the time windows keeps for "not known yet". */
#define UA_NONE (-100.0)
#define UA_FOREVER (1e300)
#define UA_MAX_DIMENSION 16
#define UA_PI 3.141592653589793
#define UA_TWO_PI 6.283185307179586

double __cdecl sin(double), cos(double), asin(double), sqrt(double), floor(double), ceil(double), fabs(double);
double __cdecl fmax(double, double);

typedef struct UaManager UaManager;
typedef struct UaVariable UaVariable;
typedef struct UaStoryboard UaStoryboard;
typedef struct UaTransition UaTransition;
typedef struct UaItem UaItem;
typedef struct UaTrack UaTrack;

/* the callbacks the manager is in, a method names those it may run under. */
enum {
    UA_CALLBACK_MANAGER = 1,
    UA_CALLBACK_VARIABLE = 2,
    UA_CALLBACK_STORYBOARD = 4,
    UA_CALLBACK_UPDATED = 8,
    UA_CALLBACK_PRIORITY = 16,
};

void *UaAlloc(SIZE_T size);
void *UaRealloc(void *block, SIZE_T size);
void UaFree(void *block);
void UaObjectCountChange(LONG delta);
HRESULT UaQuery(IUnknown *object, const IID *own, REFIID iid, void **out);

/* ------------------------------------------------------------------ transitions */

typedef enum UaKind {
    UA_INSTANTANEOUS,
    UA_CONSTANT,
    UA_DISCRETE,
    UA_LINEAR,
    UA_LINEAR_SPEED,
    UA_SINUSOIDAL_VELOCITY,
    UA_SINUSOIDAL_RANGE,
    UA_ACCELERATE_DECELERATE,
    UA_REVERSAL,
    UA_CUBIC,
    UA_SMOOTH_STOP,
    UA_PARABOLIC,
    UA_CUBIC_BEZIER,
    UA_CUSTOM,
    UA_CUSTOM2,
} UaKind;

/* one dimension of a library transition, with the state windows keeps for it. */
typedef struct UaCurve {
    double p[16];
} UaCurve;

struct UaTransition {
    const void *lpVtbl;
    LONG refs;
    int version;
    UaKind kind;
    UINT dimension;
    UaCurve curve[UA_MAX_DIMENSION];
    /* the vector form of linear from speed shares one duration between its dimensions. */
    BOOL shared_speed;
    double speed;
    IUIAnimationInterpolator *custom;
    IUIAnimationInterpolator2 *custom2;
    BOOL value_set, velocity_set;
    double value[UA_MAX_DIMENSION], velocity[UA_MAX_DIMENSION];
    UaStoryboard *storyboard;
    UaItem *item;
};

UaTransition *UaTransitionFrom(const void *iface, int version);
HRESULT UaInterpInit(UaTransition *t, const double *value, const double *velocity);
HRESULT UaInterpSetDuration(UaTransition *t, double duration);
HRESULT UaInterpDuration(UaTransition *t, double *duration);
HRESULT UaInterpFinal(UaTransition *t, double *value);
HRESULT UaInterpValue(UaTransition *t, double offset, double *value);
HRESULT UaInterpVelocity(UaTransition *t, double offset, double *velocity);
HRESULT UaInterpPrimitive(UaTransition *t, IUIAnimationPrimitiveInterpolation *primitive, double begin);
HRESULT UaTransitionCreateLibrary(int version, UaKind kind, UINT dimension, const double *a, const double *b,
                                  const double *c, void **out);
HRESULT UaLibraryCreate(int version, REFIID iid, void **out);
HRESULT UaFactoryCreate(int version, REFIID iid, void **out);

/* ------------------------------------------------------------------ the manager */

struct UaManager {
    const IUIAnimationManagerVtbl *lpVtbl;
    const IUIAnimationTimerUpdateHandlerVtbl *update_vtbl;
    LONG refs;
    int version;
    DWORD thread;
    BOOL shutdown;
    UINT callbacks;
    UI_ANIMATION_MANAGER_STATUS status;
    UI_ANIMATION_MODE mode;
    /* 2 running, 0 paused, 1 resumed until the next update. */
    int run_state;
    double paused_at;
    /* subtracted from the time an update gives, it grows by the time spent paused. */
    double offset;
    double last_update;
    double now;
    double default_delay;
    BOOL changed;
    UaVariable **variables;
    UINT variable_count, variable_capacity;
    UaStoryboard **scheduled;
    UINT scheduled_count, scheduled_capacity;
    /* every storyboard, for finding one by its tag. */
    UaStoryboard **all;
    UINT all_count, all_capacity;
    UaStoryboard **deferred;
    UINT deferred_count, deferred_capacity;
    IUnknown *event_handler;
    IUnknown *cancel_comparison, *trim_comparison, *compress_comparison, *conclude_comparison;
    IUIAnimationTimerClientEventHandler *timer_client;
};

HRESULT UaManagerCreate(int version, REFIID iid, void **out);
BOOL UaManagerWrongThread(UaManager *manager);
BOOL UaManagerInCallback(UaManager *manager, UINT allowed);
HRESULT UaManagerCheck(UaManager *manager, UINT allowed);
void UaManagerEnter(UaManager *manager, UINT callback, UINT *saved);
void UaManagerLeave(UaManager *manager, UINT saved);
HRESULT UaManagerSchedule(UaManager *manager, UaStoryboard *storyboard, double time,
                          UI_ANIMATION_SCHEDULING_RESULT *result);
HRESULT UaManagerFinishStoryboard(UaManager *manager, UaStoryboard *storyboard, UI_ANIMATION_STORYBOARD_STATUS status);
HRESULT UaManagerSetStatus(UaManager *manager, UI_ANIMATION_MANAGER_STATUS status);
HRESULT UaManagerProcessDeferred(UaManager *manager);
void UaManagerDetachVariable(UaManager *manager, UaVariable *variable);
double UaManagerTime(UaManager *manager);

/* ------------------------------------------------------------------ variables */

struct UaVariable {
    const void *lpVtbl;
    LONG refs;
    int version;
    UaManager *manager;
    UINT dimension;
    /* the value with no storyboard playing on it. */
    double rest[UA_MAX_DIMENSION];
    double value[UA_MAX_DIMENSION];
    double previous[UA_MAX_DIMENSION];
    double velocity[UA_MAX_DIMENSION];
    BOOL has_lower, has_upper;
    double lower[UA_MAX_DIMENSION], upper[UA_MAX_DIMENSION];
    UI_ANIMATION_ROUNDING_MODE rounding;
    IUnknown *tag_object;
    UINT tag_id;
    IUnknown *change_handler;
    IUnknown *integer_handler;
    IUIAnimationVariableCurveChangeHandler2 *curve_handler;
    /* the tracks of the scheduled storyboards on this variable, in the order they play. */
    UaTrack **tracks;
    UINT track_count, track_capacity;
    UaStoryboard *current;
};

HRESULT UaVariableCreate(UaManager *manager, const double *value, UINT dimension, void **out);
UaVariable *UaVariableFrom(const void *iface, int version);
void UaVariableAt(UaVariable *variable, double time, UaTrack *exclude, double *value, double *velocity);
HRESULT UaVariableCallHandlers(UaVariable *variable);
void UaVariableRefresh(UaVariable *variable, double time);
HRESULT UaVariableAddTrack(UaVariable *variable, UaTrack *track);
void UaVariableRemoveTrack(UaVariable *variable, UaTrack *track);
double UaVariableClip(UaVariable *variable, UINT index, double value);
int UaVariableRound(UaVariable *variable, double value);
void UaVariableZombify(UaVariable *variable);

/* ------------------------------------------------------------------ storyboards */

typedef struct UaKeyframe {
    /* 0 the start, 1 an offset from another keyframe, 2 the end of a transition. */
    int kind;
    int base;
    double offset;
    UaTransition *after;
    BOOL resolved;
    double time;
} UaKeyframe;

struct UaItem {
    UaTransition *transition;
    UaVariable *variable;
    int start;
    int end;
    BOOL resolved;
    double begin, finish;
    double final[UA_MAX_DIMENSION];
};

/* what a storyboard does to one variable. */
struct UaTrack {
    UaStoryboard *storyboard;
    UaVariable *variable;
    UaItem **items;
    UINT item_count, item_capacity;
    BOOL held;
    /* in the storyboard's own time, before loops repeat it. */
    double begin, end;
    /* global times, end may be UA_FOREVER. */
    double start_time, end_time;
    /* when a later storyboard took the variable over, and its track. */
    double trim;
    UaTrack *trimmed_by;
    /* the value it holds before its first transition. */
    double initial[UA_MAX_DIMENSION];
};

typedef struct UaLoop {
    int start, end;
    double count;
    UI_ANIMATION_REPEAT_MODE mode;
    IUIAnimationLoopIterationChangeHandler2 *handler;
    UINT_PTR id;
    /* the repetitions it plays once concluded, while it repeats forever. */
    double concluded;
    UINT iteration;
} UaLoop;

struct UaStoryboard {
    const void *lpVtbl;
    LONG refs;
    int version;
    UaManager *manager;
    UI_ANIMATION_STORYBOARD_STATUS status, previous_status;
    UaKeyframe *keyframes;
    UINT keyframe_count, keyframe_capacity;
    UaItem **items;
    UINT item_count, item_capacity;
    UaTrack **tracks;
    UINT track_count, track_capacity;
    UaLoop *loops;
    UINT loop_count, loop_capacity;
    double delay;
    double skip;
    IUnknown *tag_object;
    UINT tag_id;
    IUnknown *event_handler;
    /* the scheduled storyboard. */
    double start_time;
    double end_item;
    /* finishing in a deadline plays the rest faster from finish_time on. */
    double finish_time, finish_local, finish_scale;
    BOOL trimmed;
    BOOL updated;
    BOOL deferred_schedule, deferred_abandon;
    double deferred_time;
};

HRESULT UaStoryboardCreate(UaManager *manager, void **out);
UaStoryboard *UaStoryboardFrom(const void *iface, int version);
void *UaStoryboardIface(UaStoryboard *storyboard);
HRESULT UaStoryboardAddTransition(UaStoryboard *storyboard, UaVariable *variable, UaTransition *transition);
HRESULT UaStoryboardResolve(UaStoryboard *storyboard, double start);
double UaStoryboardLocal(UaStoryboard *storyboard, double time);
double UaStoryboardEndTime(UaStoryboard *storyboard);
double UaStoryboardExpand(UaStoryboard *storyboard, double item_time);
void UaTrackAt(UaTrack *track, double time, double *value, double *velocity, BOOL *controls);
void UaTrackFinal(UaTrack *track, double *value);
HRESULT UaStoryboardSetStatus(UaStoryboard *storyboard, UI_ANIMATION_STORYBOARD_STATUS status);
HRESULT UaStoryboardCallUpdated(UaStoryboard *storyboard);
HRESULT UaStoryboardLoopEvents(UaStoryboard *storyboard, double time);
void UaStoryboardUnschedule(UaStoryboard *storyboard);
HRESULT UaStoryboardConcludeLoops(UaStoryboard *storyboard, double time);
void UaStoryboardFinishIn(UaStoryboard *storyboard, double now, double deadline);

/* ------------------------------------------------------------------ the timer */

HRESULT UaTimerCreate(REFIID iid, void **out);

#endif
