/* uianimation: the windows animation manager, its storyboards, variables, transitions and timer. method order matches
 * windows' vtables. */
#ifndef MIME_WIN32_UIANIMATION_H
#define MIME_WIN32_UIANIMATION_H

#include <mime/win32/objbase.h>

/* ------------------------------------------------------------------ types and errors */

typedef double UI_ANIMATION_SECONDS;

#define UI_ANIMATION_SECONDS_EVENTUALLY (-1)
#define UI_ANIMATION_SECONDS_INFINITE (-1)
#define UI_ANIMATION_REPEAT_INDEFINITELY (-1)
#define UI_ANIMATION_REPEAT_INDEFINITELY_CONCLUDE_AT_END UI_ANIMATION_REPEAT_INDEFINITELY
#define UI_ANIMATION_REPEAT_INDEFINITELY_CONCLUDE_AT_START (-2)

typedef enum UI_ANIMATION_SCHEDULING_RESULT {
    UI_ANIMATION_SCHEDULING_UNEXPECTED_FAILURE = 0,
    UI_ANIMATION_SCHEDULING_INSUFFICIENT_PRIORITY = 1,
    UI_ANIMATION_SCHEDULING_ALREADY_SCHEDULED = 2,
    UI_ANIMATION_SCHEDULING_SUCCEEDED = 3,
    UI_ANIMATION_SCHEDULING_DEFERRED = 4
} UI_ANIMATION_SCHEDULING_RESULT;

typedef enum UI_ANIMATION_STORYBOARD_STATUS {
    UI_ANIMATION_STORYBOARD_BUILDING = 0,
    UI_ANIMATION_STORYBOARD_SCHEDULED = 1,
    UI_ANIMATION_STORYBOARD_CANCELLED = 2,
    UI_ANIMATION_STORYBOARD_PLAYING = 3,
    UI_ANIMATION_STORYBOARD_TRUNCATED = 4,
    UI_ANIMATION_STORYBOARD_FINISHED = 5,
    UI_ANIMATION_STORYBOARD_READY = 6,
    UI_ANIMATION_STORYBOARD_INSUFFICIENT_PRIORITY = 7
} UI_ANIMATION_STORYBOARD_STATUS;

typedef enum UI_ANIMATION_ROUNDING_MODE {
    UI_ANIMATION_ROUNDING_NEAREST = 0,
    UI_ANIMATION_ROUNDING_FLOOR = 1,
    UI_ANIMATION_ROUNDING_CEILING = 2
} UI_ANIMATION_ROUNDING_MODE;

typedef enum UI_ANIMATION_UPDATE_RESULT {
    UI_ANIMATION_UPDATE_NO_CHANGE = 0,
    UI_ANIMATION_UPDATE_VARIABLES_CHANGED = 1
} UI_ANIMATION_UPDATE_RESULT;

typedef enum UI_ANIMATION_MANAGER_STATUS {
    UI_ANIMATION_MANAGER_IDLE = 0,
    UI_ANIMATION_MANAGER_BUSY = 1
} UI_ANIMATION_MANAGER_STATUS;

typedef enum UI_ANIMATION_MODE {
    UI_ANIMATION_MODE_DISABLED = 0,
    UI_ANIMATION_MODE_SYSTEM_DEFAULT = 1,
    UI_ANIMATION_MODE_ENABLED = 2
} UI_ANIMATION_MODE;

typedef enum UI_ANIMATION_PRIORITY_EFFECT {
    UI_ANIMATION_PRIORITY_EFFECT_FAILURE = 0,
    UI_ANIMATION_PRIORITY_EFFECT_DELAY = 1
} UI_ANIMATION_PRIORITY_EFFECT;

typedef enum UI_ANIMATION_REPEAT_MODE {
    UI_ANIMATION_REPEAT_MODE_NORMAL = 0,
    UI_ANIMATION_REPEAT_MODE_ALTERNATE = 1
} UI_ANIMATION_REPEAT_MODE;

typedef enum UI_ANIMATION_SLOPE {
    UI_ANIMATION_SLOPE_INCREASING = 0,
    UI_ANIMATION_SLOPE_DECREASING = 1
} UI_ANIMATION_SLOPE;

typedef enum UI_ANIMATION_DEPENDENCIES {
    UI_ANIMATION_DEPENDENCY_NONE = 0,
    UI_ANIMATION_DEPENDENCY_INTERMEDIATE_VALUES = 0x1,
    UI_ANIMATION_DEPENDENCY_FINAL_VALUE = 0x2,
    UI_ANIMATION_DEPENDENCY_FINAL_VELOCITY = 0x4,
    UI_ANIMATION_DEPENDENCY_DURATION = 0x8
} UI_ANIMATION_DEPENDENCIES;

typedef enum UI_ANIMATION_TIMER_CLIENT_STATUS {
    UI_ANIMATION_TIMER_CLIENT_IDLE = 0,
    UI_ANIMATION_TIMER_CLIENT_BUSY = 1
} UI_ANIMATION_TIMER_CLIENT_STATUS;

typedef enum UI_ANIMATION_IDLE_BEHAVIOR {
    UI_ANIMATION_IDLE_BEHAVIOR_CONTINUE = 0,
    UI_ANIMATION_IDLE_BEHAVIOR_DISABLE = 1
} UI_ANIMATION_IDLE_BEHAVIOR;

typedef struct UI_ANIMATION_KEYFRAME_ {
    int unused;
} *UI_ANIMATION_KEYFRAME;
#define UI_ANIMATION_KEYFRAME_STORYBOARD_START ((UI_ANIMATION_KEYFRAME) - 1)

#define UI_E_CREATE_FAILED ((HRESULT)0x802A0001L)
#define UI_E_SHUTDOWN_CALLED ((HRESULT)0x802A0002L)
#define UI_E_ILLEGAL_REENTRANCY ((HRESULT)0x802A0003L)
#define UI_E_OBJECT_SEALED ((HRESULT)0x802A0004L)
#define UI_E_VALUE_NOT_SET ((HRESULT)0x802A0005L)
#define UI_E_VALUE_NOT_DETERMINED ((HRESULT)0x802A0006L)
#define UI_E_INVALID_OUTPUT ((HRESULT)0x802A0007L)
#define UI_E_BOOLEAN_EXPECTED ((HRESULT)0x802A0008L)
#define UI_E_DIFFERENT_OWNER ((HRESULT)0x802A0009L)
#define UI_E_AMBIGUOUS_MATCH ((HRESULT)0x802A000AL)
#define UI_E_FP_OVERFLOW ((HRESULT)0x802A000BL)
#define UI_E_WRONG_THREAD ((HRESULT)0x802A000CL)
#define UI_E_STORYBOARD_ACTIVE ((HRESULT)0x802A0101L)
#define UI_E_STORYBOARD_NOT_PLAYING ((HRESULT)0x802A0102L)
#define UI_E_START_KEYFRAME_AFTER_END ((HRESULT)0x802A0103L)
#define UI_E_END_KEYFRAME_NOT_DETERMINED ((HRESULT)0x802A0104L)
#define UI_E_LOOPS_OVERLAP ((HRESULT)0x802A0105L)
#define UI_E_TRANSITION_ALREADY_USED ((HRESULT)0x802A0106L)
#define UI_E_TRANSITION_NOT_IN_STORYBOARD ((HRESULT)0x802A0107L)
#define UI_E_TRANSITION_ECLIPSED ((HRESULT)0x802A0108L)
#define UI_E_TIME_BEFORE_LAST_UPDATE ((HRESULT)0x802A0109L)
#define UI_E_TIMER_CLIENT_ALREADY_CONNECTED ((HRESULT)0x802A010AL)
#define UI_E_INVALID_DIMENSION ((HRESULT)0x802A010BL)
#define UI_E_PRIMITIVE_OUT_OF_BOUNDS ((HRESULT)0x802A010CL)

/* ------------------------------------------------------------------ interface types */

typedef struct IDCompositionAnimation IDCompositionAnimation;
typedef struct IUIAnimationManager IUIAnimationManager;
typedef struct IUIAnimationVariable IUIAnimationVariable;
typedef struct IUIAnimationStoryboard IUIAnimationStoryboard;
typedef struct IUIAnimationTransition IUIAnimationTransition;
typedef struct IUIAnimationStoryboardEventHandler IUIAnimationStoryboardEventHandler;
typedef struct IUIAnimationVariableChangeHandler IUIAnimationVariableChangeHandler;
typedef struct IUIAnimationVariableIntegerChangeHandler IUIAnimationVariableIntegerChangeHandler;
typedef struct IUIAnimationManagerEventHandler IUIAnimationManagerEventHandler;
typedef struct IUIAnimationPriorityComparison IUIAnimationPriorityComparison;
typedef struct IUIAnimationManager2 IUIAnimationManager2;
typedef struct IUIAnimationVariable2 IUIAnimationVariable2;
typedef struct IUIAnimationStoryboard2 IUIAnimationStoryboard2;
typedef struct IUIAnimationTransition2 IUIAnimationTransition2;
typedef struct IUIAnimationLoopIterationChangeHandler2 IUIAnimationLoopIterationChangeHandler2;
typedef struct IUIAnimationStoryboardEventHandler2 IUIAnimationStoryboardEventHandler2;
typedef struct IUIAnimationVariableChangeHandler2 IUIAnimationVariableChangeHandler2;
typedef struct IUIAnimationVariableIntegerChangeHandler2 IUIAnimationVariableIntegerChangeHandler2;
typedef struct IUIAnimationVariableCurveChangeHandler2 IUIAnimationVariableCurveChangeHandler2;
typedef struct IUIAnimationManagerEventHandler2 IUIAnimationManagerEventHandler2;
typedef struct IUIAnimationPriorityComparison2 IUIAnimationPriorityComparison2;
typedef struct IUIAnimationTransitionLibrary IUIAnimationTransitionLibrary;
typedef struct IUIAnimationTransitionLibrary2 IUIAnimationTransitionLibrary2;
typedef struct IUIAnimationTransitionFactory IUIAnimationTransitionFactory;
typedef struct IUIAnimationInterpolator IUIAnimationInterpolator;
typedef struct IUIAnimationTransitionFactory2 IUIAnimationTransitionFactory2;
typedef struct IUIAnimationInterpolator2 IUIAnimationInterpolator2;
typedef struct IUIAnimationPrimitiveInterpolation IUIAnimationPrimitiveInterpolation;
typedef struct IUIAnimationTimer IUIAnimationTimer;
typedef struct IUIAnimationTimerUpdateHandler IUIAnimationTimerUpdateHandler;
typedef struct IUIAnimationTimerClientEventHandler IUIAnimationTimerClientEventHandler;
typedef struct IUIAnimationTimerEventHandler IUIAnimationTimerEventHandler;

/* ------------------------------------------------------------------ interfaces */

#define IUIAnimationManager_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateAnimationVariable)(T * This, double initialValue,                                 \
                                                        IUIAnimationVariable **variable);                              \
    HRESULT(STDMETHODCALLTYPE *ScheduleTransition)(T * This, IUIAnimationVariable * variable,                          \
                                                   IUIAnimationTransition * transition, double timeNow);               \
    HRESULT(STDMETHODCALLTYPE *CreateStoryboard)(T * This, IUIAnimationStoryboard * *storyboard);                      \
    HRESULT(STDMETHODCALLTYPE *FinishAllStoryboards)(T * This, double completionDeadline);                             \
    HRESULT(STDMETHODCALLTYPE *AbandonAllStoryboards)(T * This);                                                       \
    HRESULT(STDMETHODCALLTYPE *Update)(T * This, double timeNow, UI_ANIMATION_UPDATE_RESULT *updateResult);            \
    HRESULT(STDMETHODCALLTYPE *GetVariableFromTag)(T * This, IUnknown * object, UINT32 id,                             \
                                                   IUIAnimationVariable * *variable);                                  \
    HRESULT(STDMETHODCALLTYPE *GetStoryboardFromTag)(T * This, IUnknown * object, UINT32 id,                           \
                                                     IUIAnimationStoryboard * *storyboard);                            \
    HRESULT(STDMETHODCALLTYPE *GetStatus)(T * This, UI_ANIMATION_MANAGER_STATUS * status);                             \
    HRESULT(STDMETHODCALLTYPE *SetAnimationMode)(T * This, UI_ANIMATION_MODE mode);                                    \
    HRESULT(STDMETHODCALLTYPE *Pause)(T * This);                                                                       \
    HRESULT(STDMETHODCALLTYPE *Resume)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *SetManagerEventHandler)(T * This, IUIAnimationManagerEventHandler * handler);           \
    HRESULT(STDMETHODCALLTYPE *SetCancelPriorityComparison)(T * This, IUIAnimationPriorityComparison * comparison);    \
    HRESULT(STDMETHODCALLTYPE *SetTrimPriorityComparison)(T * This, IUIAnimationPriorityComparison * comparison);      \
    HRESULT(STDMETHODCALLTYPE *SetCompressPriorityComparison)(T * This, IUIAnimationPriorityComparison * comparison);  \
    HRESULT(STDMETHODCALLTYPE *SetConcludePriorityComparison)(T * This, IUIAnimationPriorityComparison * comparison);  \
    HRESULT(STDMETHODCALLTYPE *SetDefaultLongestAcceptableDelay)(T * This, double delay);                              \
    HRESULT(STDMETHODCALLTYPE *Shutdown)(T * This);

#define IUIAnimationVariable_METHODS(T)                                                                                \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetValue)(T * This, double *value);                                                     \
    HRESULT(STDMETHODCALLTYPE *GetFinalValue)(T * This, double *finalValue);                                           \
    HRESULT(STDMETHODCALLTYPE *GetPreviousValue)(T * This, double *previousValue);                                     \
    HRESULT(STDMETHODCALLTYPE *GetIntegerValue)(T * This, int *value);                                                 \
    HRESULT(STDMETHODCALLTYPE *GetFinalIntegerValue)(T * This, int *finalValue);                                       \
    HRESULT(STDMETHODCALLTYPE *GetPreviousIntegerValue)(T * This, int *previousValue);                                 \
    HRESULT(STDMETHODCALLTYPE *GetCurrentStoryboard)(T * This, IUIAnimationStoryboard * *storyboard);                  \
    HRESULT(STDMETHODCALLTYPE *SetLowerBound)(T * This, double bound);                                                 \
    HRESULT(STDMETHODCALLTYPE *SetUpperBound)(T * This, double bound);                                                 \
    HRESULT(STDMETHODCALLTYPE *SetRoundingMode)(T * This, UI_ANIMATION_ROUNDING_MODE mode);                            \
    HRESULT(STDMETHODCALLTYPE *SetTag)(T * This, IUnknown * object, unsigned int id);                                  \
    HRESULT(STDMETHODCALLTYPE *GetTag)(T * This, IUnknown * *object, unsigned int *id);                                \
    HRESULT(STDMETHODCALLTYPE *SetVariableChangeHandler)(T * This, IUIAnimationVariableChangeHandler * handler);       \
    HRESULT(STDMETHODCALLTYPE *SetVariableIntegerChangeHandler)(T * This,                                              \
                                                                IUIAnimationVariableIntegerChangeHandler * handler);

#define IUIAnimationStoryboard_METHODS(T)                                                                              \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *AddTransition)(T * This, IUIAnimationVariable * variable,                               \
                                              IUIAnimationTransition * transition);                                    \
    HRESULT(STDMETHODCALLTYPE *AddKeyframeAtOffset)(T * This, UI_ANIMATION_KEYFRAME existingKeyframe, double offset,   \
                                                    UI_ANIMATION_KEYFRAME *keyframe);                                  \
    HRESULT(STDMETHODCALLTYPE *AddKeyframeAfterTransition)(T * This, IUIAnimationTransition * transition,              \
                                                           UI_ANIMATION_KEYFRAME * keyframe);                          \
    HRESULT(STDMETHODCALLTYPE *AddTransitionAtKeyframe)(T * This, IUIAnimationVariable * variable,                     \
                                                        IUIAnimationTransition * transition,                           \
                                                        UI_ANIMATION_KEYFRAME startKeyframe);                          \
    HRESULT(STDMETHODCALLTYPE *AddTransitionBetweenKeyframes)(                                                         \
        T * This, IUIAnimationVariable * variable, IUIAnimationTransition * transition,                                \
        UI_ANIMATION_KEYFRAME startKeyframe, UI_ANIMATION_KEYFRAME endKeyframe);                                       \
    HRESULT(STDMETHODCALLTYPE *RepeatBetweenKeyframes)(T * This, UI_ANIMATION_KEYFRAME startKeyframe,                  \
                                                       UI_ANIMATION_KEYFRAME endKeyframe, int repetitionCount);        \
    HRESULT(STDMETHODCALLTYPE *HoldVariable)(T * This, IUIAnimationVariable * variable);                               \
    HRESULT(STDMETHODCALLTYPE *SetLongestAcceptableDelay)(T * This, double delay);                                     \
    HRESULT(STDMETHODCALLTYPE *Schedule)(T * This, double timeNow, UI_ANIMATION_SCHEDULING_RESULT *schedulingResult);  \
    HRESULT(STDMETHODCALLTYPE *Conclude)(T * This);                                                                    \
    HRESULT(STDMETHODCALLTYPE *Finish)(T * This, double completionDeadline);                                           \
    HRESULT(STDMETHODCALLTYPE *Abandon)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *SetTag)(T * This, IUnknown * object, unsigned int id);                                  \
    HRESULT(STDMETHODCALLTYPE *GetTag)(T * This, IUnknown * *object, unsigned int *id);                                \
    HRESULT(STDMETHODCALLTYPE *GetStatus)(T * This, UI_ANIMATION_STORYBOARD_STATUS * status);                          \
    HRESULT(STDMETHODCALLTYPE *GetElapsedTime)(T * This, double *elapsedTime);                                         \
    HRESULT(STDMETHODCALLTYPE *SetStoryboardEventHandler)(T * This, IUIAnimationStoryboardEventHandler * handler);

#define IUIAnimationTransition_METHODS(T)                                                                              \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetInitialValue)(T * This, double value);                                               \
    HRESULT(STDMETHODCALLTYPE *SetInitialVelocity)(T * This, double velocity);                                         \
    HRESULT(STDMETHODCALLTYPE *IsDurationKnown)(T * This);                                                             \
    HRESULT(STDMETHODCALLTYPE *GetDuration)(T * This, double *duration);

#define IUIAnimationStoryboardEventHandler_METHODS(T)                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OnStoryboardStatusChanged)(T * This, IUIAnimationStoryboard * storyboard,               \
                                                          UI_ANIMATION_STORYBOARD_STATUS newStatus,                    \
                                                          UI_ANIMATION_STORYBOARD_STATUS previousStatus);              \
    HRESULT(STDMETHODCALLTYPE *OnStoryboardUpdated)(T * This, IUIAnimationStoryboard * storyboard);

#define IUIAnimationVariableChangeHandler_METHODS(T)                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OnValueChanged)(T * This, IUIAnimationStoryboard * storyboard,                          \
                                               IUIAnimationVariable * variable, double newValue,                       \
                                               double previousValue);

#define IUIAnimationVariableIntegerChangeHandler_METHODS(T)                                                            \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OnIntegerValueChanged)(T * This, IUIAnimationStoryboard * storyboard,                   \
                                                      IUIAnimationVariable * variable, int newValue,                   \
                                                      int previousValue);

#define IUIAnimationManagerEventHandler_METHODS(T)                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OnManagerStatusChanged)(T * This, UI_ANIMATION_MANAGER_STATUS newStatus,                \
                                                       UI_ANIMATION_MANAGER_STATUS previousStatus);

#define IUIAnimationPriorityComparison_METHODS(T)                                                                      \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *HasPriority)(T * This, IUIAnimationStoryboard * scheduledStoryboard,                    \
                                            IUIAnimationStoryboard * newStoryboard,                                    \
                                            UI_ANIMATION_PRIORITY_EFFECT priorityEffect);

#define IUIAnimationManager2_METHODS(T)                                                                                \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateAnimationVectorVariable)(T * This, double *initialValue, unsigned int cDimension, \
                                                              IUIAnimationVariable2 **variable);                       \
    HRESULT(STDMETHODCALLTYPE *CreateAnimationVariable)(T * This, double initialValue,                                 \
                                                        IUIAnimationVariable2 **variable);                             \
    HRESULT(STDMETHODCALLTYPE *ScheduleTransition)(T * This, IUIAnimationVariable2 * variable,                         \
                                                   IUIAnimationTransition2 * transition, double timeNow);              \
    HRESULT(STDMETHODCALLTYPE *CreateStoryboard)(T * This, IUIAnimationStoryboard2 * *storyboard);                     \
    HRESULT(STDMETHODCALLTYPE *FinishAllStoryboards)(T * This, double completionDeadline);                             \
    HRESULT(STDMETHODCALLTYPE *AbandonAllStoryboards)(T * This);                                                       \
    HRESULT(STDMETHODCALLTYPE *Update)(T * This, double timeNow, UI_ANIMATION_UPDATE_RESULT *updateResult);            \
    HRESULT(STDMETHODCALLTYPE *GetVariableFromTag)(T * This, IUnknown * object, UINT32 id,                             \
                                                   IUIAnimationVariable2 * *variable);                                 \
    HRESULT(STDMETHODCALLTYPE *GetStoryboardFromTag)(T * This, IUnknown * object, unsigned int id,                     \
                                                     IUIAnimationStoryboard2 **storyboard);                            \
    HRESULT(STDMETHODCALLTYPE *EstimateNextEventTime)(T * This, double *seconds);                                      \
    HRESULT(STDMETHODCALLTYPE *GetStatus)(T * This, UI_ANIMATION_MANAGER_STATUS * status);                             \
    HRESULT(STDMETHODCALLTYPE *SetAnimationMode)(T * This, UI_ANIMATION_MODE mode);                                    \
    HRESULT(STDMETHODCALLTYPE *Pause)(T * This);                                                                       \
    HRESULT(STDMETHODCALLTYPE *Resume)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *SetManagerEventHandler)(T * This, IUIAnimationManagerEventHandler2 * handler,           \
                                                       BOOL fRegisterForNextAnimationEvent);                           \
    HRESULT(STDMETHODCALLTYPE *SetCancelPriorityComparison)(T * This, IUIAnimationPriorityComparison2 * comparison);   \
    HRESULT(STDMETHODCALLTYPE *SetTrimPriorityComparison)(T * This, IUIAnimationPriorityComparison2 * comparison);     \
    HRESULT(STDMETHODCALLTYPE *SetCompressPriorityComparison)(T * This, IUIAnimationPriorityComparison2 * comparison); \
    HRESULT(STDMETHODCALLTYPE *SetConcludePriorityComparison)(T * This, IUIAnimationPriorityComparison2 * comparison); \
    HRESULT(STDMETHODCALLTYPE *SetDefaultLongestAcceptableDelay)(T * This, double delay);                              \
    HRESULT(STDMETHODCALLTYPE *Shutdown)(T * This);

#define IUIAnimationVariable2_METHODS(T)                                                                               \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetDimension)(T * This, unsigned int *dimension);                                       \
    HRESULT(STDMETHODCALLTYPE *GetValue)(T * This, double *value);                                                     \
    HRESULT(STDMETHODCALLTYPE *GetVectorValue)(T * This, double *value, unsigned int cDimension);                      \
    HRESULT(STDMETHODCALLTYPE *GetCurve)(T * This, IDCompositionAnimation * animation);                                \
    HRESULT(STDMETHODCALLTYPE *GetVectorCurve)(T * This, IDCompositionAnimation * *animation,                          \
                                               unsigned int cDimension);                                               \
    HRESULT(STDMETHODCALLTYPE *GetFinalValue)(T * This, double *finalValue);                                           \
    HRESULT(STDMETHODCALLTYPE *GetFinalVectorValue)(T * This, double *finalValue, unsigned int cDimension);            \
    HRESULT(STDMETHODCALLTYPE *GetPreviousValue)(T * This, double *previousValue);                                     \
    HRESULT(STDMETHODCALLTYPE *GetPreviousVectorValue)(T * This, double *previousValue, unsigned int cDimension);      \
    HRESULT(STDMETHODCALLTYPE *GetIntegerValue)(T * This, int *value);                                                 \
    HRESULT(STDMETHODCALLTYPE *GetIntegerVectorValue)(T * This, int *value, unsigned int cDimension);                  \
    HRESULT(STDMETHODCALLTYPE *GetFinalIntegerValue)(T * This, int *finalValue);                                       \
    HRESULT(STDMETHODCALLTYPE *GetFinalIntegerVectorValue)(T * This, int *finalValue, unsigned int cDimension);        \
    HRESULT(STDMETHODCALLTYPE *GetPreviousIntegerValue)(T * This, int *previousValue);                                 \
    HRESULT(STDMETHODCALLTYPE *GetPreviousIntegerVectorValue)(T * This, int *previousValue, unsigned int cDimension);  \
    HRESULT(STDMETHODCALLTYPE *GetCurrentStoryboard)(T * This, IUIAnimationStoryboard2 * *storyboard);                 \
    HRESULT(STDMETHODCALLTYPE *SetLowerBound)(T * This, double bound);                                                 \
    HRESULT(STDMETHODCALLTYPE *SetLowerBoundVector)(T * This, double *bound, unsigned int cDimension);                 \
    HRESULT(STDMETHODCALLTYPE *SetUpperBound)(T * This, double bound);                                                 \
    HRESULT(STDMETHODCALLTYPE *SetUpperBoundVector)(T * This, double *bound, unsigned int cDimension);                 \
    HRESULT(STDMETHODCALLTYPE *SetRoundingMode)(T * This, UI_ANIMATION_ROUNDING_MODE mode);                            \
    HRESULT(STDMETHODCALLTYPE *SetTag)(T * This, IUnknown * object, unsigned int id);                                  \
    HRESULT(STDMETHODCALLTYPE *GetTag)(T * This, IUnknown * *object, unsigned int *id);                                \
    HRESULT(STDMETHODCALLTYPE *SetVariableChangeHandler)(T * This, IUIAnimationVariableChangeHandler2 * handler,       \
                                                         BOOL fRegisterForNextAnimationEvent);                         \
    HRESULT(STDMETHODCALLTYPE *SetVariableIntegerChangeHandler)(                                                       \
        T * This, IUIAnimationVariableIntegerChangeHandler2 * handler, BOOL fRegisterForNextAnimationEvent);           \
    HRESULT(STDMETHODCALLTYPE *SetVariableCurveChangeHandler)(T * This,                                                \
                                                              IUIAnimationVariableCurveChangeHandler2 * handler);

#define IUIAnimationStoryboard2_METHODS(T)                                                                             \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *AddTransition)(T * This, IUIAnimationVariable2 * variable,                              \
                                              IUIAnimationTransition2 * transition);                                   \
    HRESULT(STDMETHODCALLTYPE *AddKeyframeAtOffset)(T * This, UI_ANIMATION_KEYFRAME existingKeyframe, double offset,   \
                                                    UI_ANIMATION_KEYFRAME *keyframe);                                  \
    HRESULT(STDMETHODCALLTYPE *AddKeyframeAfterTransition)(T * This, IUIAnimationTransition2 * transition,             \
                                                           UI_ANIMATION_KEYFRAME * keyframe);                          \
    HRESULT(STDMETHODCALLTYPE *AddTransitionAtKeyframe)(T * This, IUIAnimationVariable2 * variable,                    \
                                                        IUIAnimationTransition2 * transition,                          \
                                                        UI_ANIMATION_KEYFRAME startKeyframe);                          \
    HRESULT(STDMETHODCALLTYPE *AddTransitionBetweenKeyframes)(                                                         \
        T * This, IUIAnimationVariable2 * variable, IUIAnimationTransition2 * transition,                              \
        UI_ANIMATION_KEYFRAME startKeyframe, UI_ANIMATION_KEYFRAME endKeyframe);                                       \
    HRESULT(STDMETHODCALLTYPE *RepeatBetweenKeyframes)(                                                                \
        T * This, UI_ANIMATION_KEYFRAME startKeyframe, UI_ANIMATION_KEYFRAME endKeyframe, double cRepetition,          \
        UI_ANIMATION_REPEAT_MODE repeatMode, IUIAnimationLoopIterationChangeHandler2 *pIterationChangeHandler,         \
        UINT_PTR id, BOOL fRegisterForNextAnimationEvent);                                                             \
    HRESULT(STDMETHODCALLTYPE *HoldVariable)(T * This, IUIAnimationVariable2 * variable);                              \
    HRESULT(STDMETHODCALLTYPE *SetLongestAcceptableDelay)(T * This, double delay);                                     \
    HRESULT(STDMETHODCALLTYPE *SetSkipDuration)(T * This, double secondsDuration);                                     \
    HRESULT(STDMETHODCALLTYPE *Schedule)(T * This, double timeNow, UI_ANIMATION_SCHEDULING_RESULT *schedulingResult);  \
    HRESULT(STDMETHODCALLTYPE *Conclude)(T * This);                                                                    \
    HRESULT(STDMETHODCALLTYPE *Finish)(T * This, double completionDeadline);                                           \
    HRESULT(STDMETHODCALLTYPE *Abandon)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *SetTag)(T * This, IUnknown * object, unsigned int id);                                  \
    HRESULT(STDMETHODCALLTYPE *GetTag)(T * This, IUnknown * *object, unsigned int *id);                                \
    HRESULT(STDMETHODCALLTYPE *GetStatus)(T * This, UI_ANIMATION_STORYBOARD_STATUS * status);                          \
    HRESULT(STDMETHODCALLTYPE *GetElapsedTime)(T * This, double *elapsedTime);                                         \
    HRESULT(STDMETHODCALLTYPE *SetStoryboardEventHandler)(T * This, IUIAnimationStoryboardEventHandler2 * handler,     \
                                                          BOOL fRegisterStatusChangeForNextAnimationEvent,             \
                                                          BOOL fRegisterUpdateForNextAnimationEvent);

#define IUIAnimationTransition2_METHODS(T)                                                                             \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetDimension)(T * This, unsigned int *dimension);                                       \
    HRESULT(STDMETHODCALLTYPE *SetInitialValue)(T * This, double value);                                               \
    HRESULT(STDMETHODCALLTYPE *SetInitialVectorValue)(T * This, double *value, unsigned int cDimension);               \
    HRESULT(STDMETHODCALLTYPE *SetInitialVelocity)(T * This, double velocity);                                         \
    HRESULT(STDMETHODCALLTYPE *SetInitialVectorVelocity)(T * This, double *velocity, unsigned int cDimension);         \
    HRESULT(STDMETHODCALLTYPE *IsDurationKnown)(T * This);                                                             \
    HRESULT(STDMETHODCALLTYPE *GetDuration)(T * This, double *duration);

#define IUIAnimationLoopIterationChangeHandler2_METHODS(T)                                                             \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OnLoopIterationChanged)(T * This, IUIAnimationStoryboard2 * storyboard, UINT_PTR id,    \
                                                       unsigned int newIterationCount,                                 \
                                                       unsigned int oldIterationCount);

#define IUIAnimationStoryboardEventHandler2_METHODS(T)                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OnStoryboardStatusChanged)(T * This, IUIAnimationStoryboard2 * storyboard,              \
                                                          UI_ANIMATION_STORYBOARD_STATUS newStatus,                    \
                                                          UI_ANIMATION_STORYBOARD_STATUS previousStatus);              \
    HRESULT(STDMETHODCALLTYPE *OnStoryboardUpdated)(T * This, IUIAnimationStoryboard2 * storyboard);

#define IUIAnimationVariableChangeHandler2_METHODS(T)                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OnValueChanged)(T * This, IUIAnimationStoryboard2 * storyboard,                         \
                                               IUIAnimationVariable2 * variable, double *newValue,                     \
                                               double *previousValue, unsigned int cDimension);

#define IUIAnimationVariableIntegerChangeHandler2_METHODS(T)                                                           \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OnIntegerValueChanged)(T * This, IUIAnimationStoryboard2 * storyboard,                  \
                                                      IUIAnimationVariable2 * variable, int *newValue,                 \
                                                      int *previousValue, unsigned int cDimension);

#define IUIAnimationVariableCurveChangeHandler2_METHODS(T)                                                             \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OnCurveChanged)(T * This, IUIAnimationVariable2 * variable);

#define IUIAnimationManagerEventHandler2_METHODS(T)                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OnManagerStatusChanged)(T * This, UI_ANIMATION_MANAGER_STATUS newStatus,                \
                                                       UI_ANIMATION_MANAGER_STATUS previousStatus);

#define IUIAnimationPriorityComparison2_METHODS(T)                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *HasPriority)(T * This, IUIAnimationStoryboard2 * scheduledStoryboard,                   \
                                            IUIAnimationStoryboard2 * newStoryboard,                                   \
                                            UI_ANIMATION_PRIORITY_EFFECT priorityEffect);

#define IUIAnimationTransitionLibrary_METHODS(T)                                                                       \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateInstantaneousTransition)(T * This, double finalValue,                             \
                                                              IUIAnimationTransition **transition);                    \
    HRESULT(STDMETHODCALLTYPE *CreateConstantTransition)(T * This, double duration,                                    \
                                                         IUIAnimationTransition **transition);                         \
    HRESULT(STDMETHODCALLTYPE *CreateDiscreteTransition)(T * This, double delay, double finalValue, double hold,       \
                                                         IUIAnimationTransition **transition);                         \
    HRESULT(STDMETHODCALLTYPE *CreateLinearTransition)(T * This, double duration, double finalValue,                   \
                                                       IUIAnimationTransition **transition);                           \
    HRESULT(STDMETHODCALLTYPE *CreateLinearTransitionFromSpeed)(T * This, double speed, double finalValue,             \
                                                                IUIAnimationTransition **transition);                  \
    HRESULT(STDMETHODCALLTYPE *CreateSinusoidalTransitionFromVelocity)(T * This, double duration, double period,       \
                                                                       IUIAnimationTransition **transition);           \
    HRESULT(STDMETHODCALLTYPE *CreateSinusoidalTransitionFromRange)(                                                   \
        T * This, double duration, double minimumValue, double maximumValue, double period, UI_ANIMATION_SLOPE slope,  \
        IUIAnimationTransition **transition);                                                                          \
    HRESULT(STDMETHODCALLTYPE *CreateAccelerateDecelerateTransition)(                                                  \
        T * This, double duration, double finalValue, double accelerationRatio, double decelerationRatio,              \
        IUIAnimationTransition **transition);                                                                          \
    HRESULT(STDMETHODCALLTYPE *CreateReversalTransition)(T * This, double duration,                                    \
                                                         IUIAnimationTransition **transition);                         \
    HRESULT(STDMETHODCALLTYPE *CreateCubicTransition)(T * This, double duration, double finalValue,                    \
                                                      double finalVelocity, IUIAnimationTransition **transition);      \
    HRESULT(STDMETHODCALLTYPE *CreateSmoothStopTransition)(T * This, double maximumDuration, double finalValue,        \
                                                           IUIAnimationTransition **transition);                       \
    HRESULT(STDMETHODCALLTYPE *CreateParabolicTransitionFromAcceleration)(                                             \
        T * This, double finalValue, double finalVelocity, double acceleration, IUIAnimationTransition **transition);

#define IUIAnimationTransitionLibrary2_METHODS(T)                                                                      \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateInstantaneousTransition)(T * This, double finalValue,                             \
                                                              IUIAnimationTransition2 **transition);                   \
    HRESULT(STDMETHODCALLTYPE *CreateInstantaneousVectorTransition)(                                                   \
        T * This, double *finalValue, unsigned int cDimension, IUIAnimationTransition2 **transition);                  \
    HRESULT(STDMETHODCALLTYPE *CreateConstantTransition)(T * This, double duration,                                    \
                                                         IUIAnimationTransition2 **transition);                        \
    HRESULT(STDMETHODCALLTYPE *CreateDiscreteTransition)(T * This, double delay, double finalValue, double hold,       \
                                                         IUIAnimationTransition2 **transition);                        \
    HRESULT(STDMETHODCALLTYPE *CreateDiscreteVectorTransition)(T * This, double delay, double *finalValue,             \
                                                               unsigned int cDimension, double hold,                   \
                                                               IUIAnimationTransition2 **transition);                  \
    HRESULT(STDMETHODCALLTYPE *CreateLinearTransition)(T * This, double duration, double finalValue,                   \
                                                       IUIAnimationTransition2 **transition);                          \
    HRESULT(STDMETHODCALLTYPE *CreateLinearVectorTransition)(                                                          \
        T * This, double duration, double *finalValue, unsigned int cDimension, IUIAnimationTransition2 **transition); \
    HRESULT(STDMETHODCALLTYPE *CreateLinearTransitionFromSpeed)(T * This, double speed, double finalValue,             \
                                                                IUIAnimationTransition2 **transition);                 \
    HRESULT(STDMETHODCALLTYPE *CreateLinearVectorTransitionFromSpeed)(                                                 \
        T * This, double speed, double *finalValue, unsigned int cDimension, IUIAnimationTransition2 **transition);    \
    HRESULT(STDMETHODCALLTYPE *CreateSinusoidalTransitionFromVelocity)(T * This, double duration, double period,       \
                                                                       IUIAnimationTransition2 **transition);          \
    HRESULT(STDMETHODCALLTYPE *CreateSinusoidalTransitionFromRange)(                                                   \
        T * This, double duration, double minimumValue, double maximumValue, double period, UI_ANIMATION_SLOPE slope,  \
        IUIAnimationTransition2 **transition);                                                                         \
    HRESULT(STDMETHODCALLTYPE *CreateAccelerateDecelerateTransition)(                                                  \
        T * This, double duration, double finalValue, double accelerationRatio, double decelerationRatio,              \
        IUIAnimationTransition2 **transition);                                                                         \
    HRESULT(STDMETHODCALLTYPE *CreateReversalTransition)(T * This, double duration,                                    \
                                                         IUIAnimationTransition2 **transition);                        \
    HRESULT(STDMETHODCALLTYPE *CreateCubicTransition)(T * This, double duration, double finalValue,                    \
                                                      double finalVelocity, IUIAnimationTransition2 **transition);     \
    HRESULT(STDMETHODCALLTYPE *CreateCubicVectorTransition)(T * This, double duration, double *finalValue,             \
                                                            double *finalVelocity, unsigned int cDimension,            \
                                                            IUIAnimationTransition2 **transition);                     \
    HRESULT(STDMETHODCALLTYPE *CreateSmoothStopTransition)(T * This, double maximumDuration, double finalValue,        \
                                                           IUIAnimationTransition2 **transition);                      \
    HRESULT(STDMETHODCALLTYPE *CreateParabolicTransitionFromAcceleration)(                                             \
        T * This, double finalValue, double finalVelocity, double acceleration, IUIAnimationTransition2 **transition); \
    HRESULT(STDMETHODCALLTYPE *CreateCubicBezierLinearTransition)(T * This, double duration, double finalValue,        \
                                                                  double x1, double y1, double x2, double y2,          \
                                                                  IUIAnimationTransition2 **ppTransition);             \
    HRESULT(STDMETHODCALLTYPE *CreateCubicBezierLinearVectorTransition)(                                               \
        T * This, double duration, double *finalValue, unsigned int cDimension, double x1, double y1, double x2,       \
        double y2, IUIAnimationTransition2 **ppTransition);

#define IUIAnimationTransitionFactory_METHODS(T)                                                                       \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateTransition)(T * This, IUIAnimationInterpolator * interpolator,                    \
                                                 IUIAnimationTransition * *transition);

#define IUIAnimationInterpolator_METHODS(T)                                                                            \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetInitialValueAndVelocity)(T * This, double initialValue, double initialVelocity);     \
    HRESULT(STDMETHODCALLTYPE *SetDuration)(T * This, double duration);                                                \
    HRESULT(STDMETHODCALLTYPE *GetDuration)(T * This, double *duration);                                               \
    HRESULT(STDMETHODCALLTYPE *GetFinalValue)(T * This, double *value);                                                \
    HRESULT(STDMETHODCALLTYPE *InterpolateValue)(T * This, double offset, double *value);                              \
    HRESULT(STDMETHODCALLTYPE *InterpolateVelocity)(T * This, double offset, double *velocity);                        \
    HRESULT(STDMETHODCALLTYPE *GetDependencies)(T * This, UI_ANIMATION_DEPENDENCIES * initialValueDependencies,        \
                                                UI_ANIMATION_DEPENDENCIES * initialVelocityDependencies,               \
                                                UI_ANIMATION_DEPENDENCIES * durationDependencies);

#define IUIAnimationTransitionFactory2_METHODS(T)                                                                      \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateTransition)(T * This, IUIAnimationInterpolator2 * interpolator,                   \
                                                 IUIAnimationTransition2 * *transition);

#define IUIAnimationInterpolator2_METHODS(T)                                                                           \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetDimension)(T * This, unsigned int *dimension);                                       \
    HRESULT(STDMETHODCALLTYPE *SetInitialValueAndVelocity)(T * This, double *initialValue, double *initialVelocity,    \
                                                           unsigned int cDimension);                                   \
    HRESULT(STDMETHODCALLTYPE *SetDuration)(T * This, double duration);                                                \
    HRESULT(STDMETHODCALLTYPE *GetDuration)(T * This, double *duration);                                               \
    HRESULT(STDMETHODCALLTYPE *GetFinalValue)(T * This, double *value, unsigned int cDimension);                       \
    HRESULT(STDMETHODCALLTYPE *InterpolateValue)(T * This, double offset, double *value, unsigned int cDimension);     \
    HRESULT(STDMETHODCALLTYPE *InterpolateVelocity)(T * This, double offset, double *velocity,                         \
                                                    unsigned int cDimension);                                          \
    HRESULT(STDMETHODCALLTYPE *GetPrimitiveInterpolation)(                                                             \
        T * This, IUIAnimationPrimitiveInterpolation * interpolation, unsigned int cDimension);                        \
    HRESULT(STDMETHODCALLTYPE *GetDependencies)(T * This, UI_ANIMATION_DEPENDENCIES * initialValueDependencies,        \
                                                UI_ANIMATION_DEPENDENCIES * initialVelocityDependencies,               \
                                                UI_ANIMATION_DEPENDENCIES * durationDependencies);

#define IUIAnimationPrimitiveInterpolation_METHODS(T)                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *AddCubic)(T * This, unsigned int dimension, double beginOffset,                         \
                                         float constantCoefficient, float linearCoefficient,                           \
                                         float quadraticCoefficient, float cubicCoefficient);                          \
    HRESULT(STDMETHODCALLTYPE *AddSinusoidal)(T * This, unsigned int dimension, double beginOffset, float bias,        \
                                              float amplitude, float frequency, float phase);

#define IUIAnimationTimer_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetTimerUpdateHandler)(T * This, IUIAnimationTimerUpdateHandler * updateHandler,        \
                                                      UI_ANIMATION_IDLE_BEHAVIOR idleBehavior);                        \
    HRESULT(STDMETHODCALLTYPE *SetTimerEventHandler)(T * This, IUIAnimationTimerEventHandler * handler);               \
    HRESULT(STDMETHODCALLTYPE *Enable)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *Disable)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *IsEnabled)(T * This);                                                                   \
    HRESULT(STDMETHODCALLTYPE *GetTime)(T * This, double *seconds);                                                    \
    HRESULT(STDMETHODCALLTYPE *SetFrameRateThreshold)(T * This, unsigned int framesPerSecond);

#define IUIAnimationTimerUpdateHandler_METHODS(T)                                                                      \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OnUpdate)(T * This, double timeNow, UI_ANIMATION_UPDATE_RESULT *result);                \
    HRESULT(STDMETHODCALLTYPE *SetTimerClientEventHandler)(T * This, IUIAnimationTimerClientEventHandler * handler);   \
    HRESULT(STDMETHODCALLTYPE *ClearTimerClientEventHandler)(T * This);

#define IUIAnimationTimerClientEventHandler_METHODS(T)                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OnTimerClientStatusChanged)(T * This, UI_ANIMATION_TIMER_CLIENT_STATUS newStatus,       \
                                                           UI_ANIMATION_TIMER_CLIENT_STATUS previousStatus);

#define IUIAnimationTimerEventHandler_METHODS(T)                                                                       \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OnPreUpdate)(T * This);                                                                 \
    HRESULT(STDMETHODCALLTYPE *OnPostUpdate)(T * This);                                                                \
    HRESULT(STDMETHODCALLTYPE *OnRenderingTooSlow)(T * This, UINT32 framesPerSecond);

MIME_COM_INTERFACE(IUIAnimationManager)
MIME_COM_INTERFACE(IUIAnimationVariable)
MIME_COM_INTERFACE(IUIAnimationStoryboard)
MIME_COM_INTERFACE(IUIAnimationTransition)
MIME_COM_INTERFACE(IUIAnimationStoryboardEventHandler)
MIME_COM_INTERFACE(IUIAnimationVariableChangeHandler)
MIME_COM_INTERFACE(IUIAnimationVariableIntegerChangeHandler)
MIME_COM_INTERFACE(IUIAnimationManagerEventHandler)
MIME_COM_INTERFACE(IUIAnimationPriorityComparison)
MIME_COM_INTERFACE(IUIAnimationManager2)
MIME_COM_INTERFACE(IUIAnimationVariable2)
MIME_COM_INTERFACE(IUIAnimationStoryboard2)
MIME_COM_INTERFACE(IUIAnimationTransition2)
MIME_COM_INTERFACE(IUIAnimationLoopIterationChangeHandler2)
MIME_COM_INTERFACE(IUIAnimationStoryboardEventHandler2)
MIME_COM_INTERFACE(IUIAnimationVariableChangeHandler2)
MIME_COM_INTERFACE(IUIAnimationVariableIntegerChangeHandler2)
MIME_COM_INTERFACE(IUIAnimationVariableCurveChangeHandler2)
MIME_COM_INTERFACE(IUIAnimationManagerEventHandler2)
MIME_COM_INTERFACE(IUIAnimationPriorityComparison2)
MIME_COM_INTERFACE(IUIAnimationTransitionLibrary)
MIME_COM_INTERFACE(IUIAnimationTransitionLibrary2)
MIME_COM_INTERFACE(IUIAnimationTransitionFactory)
MIME_COM_INTERFACE(IUIAnimationInterpolator)
MIME_COM_INTERFACE(IUIAnimationTransitionFactory2)
MIME_COM_INTERFACE(IUIAnimationInterpolator2)
MIME_COM_INTERFACE(IUIAnimationPrimitiveInterpolation)
MIME_COM_INTERFACE(IUIAnimationTimer)
MIME_COM_INTERFACE(IUIAnimationTimerUpdateHandler)
MIME_COM_INTERFACE(IUIAnimationTimerClientEventHandler)
MIME_COM_INTERFACE(IUIAnimationTimerEventHandler)

/* ------------------------------------------------------------------ guids */

MIME_DEFINE_GUID(CLSID_UIAnimationManager, 0x4C1FC63A, 0x695C, 0x47E8, 0xA3, 0x39, 0x1A, 0x19, 0x4B, 0xE3, 0xD0, 0xB8);
MIME_DEFINE_GUID(CLSID_UIAnimationManager2, 0xD25D8842, 0x8884, 0x4A4A, 0xB3, 0x21, 0x09, 0x13, 0x14, 0x37, 0x9B, 0xDD);
MIME_DEFINE_GUID(CLSID_UIAnimationTransitionLibrary, 0x1D6322AD, 0xAA85, 0x4EF5, 0xA8, 0x28, 0x86, 0xD7, 0x10, 0x67,
                 0xD1, 0x45);
MIME_DEFINE_GUID(CLSID_UIAnimationTransitionLibrary2, 0x812F944A, 0xC5C8, 0x4CD9, 0xB0, 0xA6, 0xB3, 0xDA, 0x80, 0x2F,
                 0x22, 0x8D);
MIME_DEFINE_GUID(CLSID_UIAnimationTransitionFactory, 0x8A9B1CDD, 0xFCD7, 0x419C, 0x8B, 0x44, 0x42, 0xFD, 0x17, 0xDB,
                 0x18, 0x87);
MIME_DEFINE_GUID(CLSID_UIAnimationTransitionFactory2, 0x84302F97, 0x7F7B, 0x4040, 0xB1, 0x90, 0x72, 0xAC, 0x9D, 0x18,
                 0xE4, 0x20);
MIME_DEFINE_GUID(CLSID_UIAnimationTimer, 0xBFCD4A0C, 0x06B6, 0x4384, 0xB7, 0x68, 0x0D, 0xAA, 0x79, 0x2C, 0x38, 0x0E);
MIME_DEFINE_GUID(IID_IUIAnimationManager, 0x9169896C, 0xAC8D, 0x4E7D, 0x94, 0xE5, 0x67, 0xFA, 0x4D, 0xC2, 0xF2, 0xE8);
MIME_DEFINE_GUID(IID_IUIAnimationVariable, 0x8CEEB155, 0x2849, 0x4CE5, 0x94, 0x48, 0x91, 0xFF, 0x70, 0xE1, 0xE4, 0xD9);
MIME_DEFINE_GUID(IID_IUIAnimationStoryboard, 0xA8FF128F, 0x9BF9, 0x4AF1, 0x9E, 0x67, 0xE5, 0xE4, 0x10, 0xDE, 0xFB,
                 0x84);
MIME_DEFINE_GUID(IID_IUIAnimationTransition, 0xDC6CE252, 0xF731, 0x41CF, 0xB6, 0x10, 0x61, 0x4B, 0x6C, 0xA0, 0x49,
                 0xAD);
MIME_DEFINE_GUID(IID_IUIAnimationStoryboardEventHandler, 0x3D5C9008, 0xEC7C, 0x4364, 0x9F, 0x8A, 0x9A, 0xF3, 0xC5, 0x8C,
                 0xBA, 0xE6);
MIME_DEFINE_GUID(IID_IUIAnimationVariableChangeHandler, 0x6358B7BA, 0x87D2, 0x42D5, 0xBF, 0x71, 0x82, 0xE9, 0x19, 0xDD,
                 0x58, 0x62);
MIME_DEFINE_GUID(IID_IUIAnimationVariableIntegerChangeHandler, 0xBB3E1550, 0x356E, 0x44B0, 0x99, 0xDA, 0x85, 0xAC, 0x60,
                 0x17, 0x86, 0x5E);
MIME_DEFINE_GUID(IID_IUIAnimationManagerEventHandler, 0x783321ED, 0x78A3, 0x4366, 0xB5, 0x74, 0x6A, 0xF6, 0x07, 0xA6,
                 0x47, 0x88);
MIME_DEFINE_GUID(IID_IUIAnimationPriorityComparison, 0x83FA9B74, 0x5F86, 0x4618, 0xBC, 0x6A, 0xA2, 0xFA, 0xC1, 0x9B,
                 0x3F, 0x44);
MIME_DEFINE_GUID(IID_IUIAnimationManager2, 0xD8B6F7D4, 0x4109, 0x4D3F, 0xAC, 0xEE, 0x87, 0x99, 0x26, 0x96, 0x8C, 0xB1);
MIME_DEFINE_GUID(IID_IUIAnimationVariable2, 0x4914B304, 0x96AB, 0x44D9, 0x9E, 0x77, 0xD5, 0x10, 0x9B, 0x7E, 0x74, 0x66);
MIME_DEFINE_GUID(IID_IUIAnimationStoryboard2, 0xAE289CD2, 0x12D4, 0x4945, 0x94, 0x19, 0x9E, 0x41, 0xBE, 0x03, 0x4D,
                 0xF2);
MIME_DEFINE_GUID(IID_IUIAnimationTransition2, 0x62FF9123, 0xA85A, 0x4E9B, 0xA2, 0x18, 0x43, 0x5A, 0x93, 0xE2, 0x68,
                 0xFD);
MIME_DEFINE_GUID(IID_IUIAnimationLoopIterationChangeHandler2, 0x2D3B15A4, 0x4762, 0x47AB, 0xA0, 0x30, 0xB2, 0x32, 0x21,
                 0xDF, 0x3A, 0xE0);
MIME_DEFINE_GUID(IID_IUIAnimationStoryboardEventHandler2, 0xBAC5F55A, 0xBA7C, 0x414C, 0xB5, 0x99, 0xFB, 0xF8, 0x50,
                 0xF5, 0x53, 0xC6);
MIME_DEFINE_GUID(IID_IUIAnimationVariableChangeHandler2, 0x63ACC8D2, 0x6EAE, 0x4BB0, 0xB8, 0x79, 0x58, 0x6D, 0xD8, 0xCF,
                 0xBE, 0x42);
MIME_DEFINE_GUID(IID_IUIAnimationVariableIntegerChangeHandler2, 0x829B6CF1, 0x4F3A, 0x4412, 0xAE, 0x09, 0xB2, 0x43,
                 0xEB, 0x4C, 0x6B, 0x58);
MIME_DEFINE_GUID(IID_IUIAnimationVariableCurveChangeHandler2, 0x72895E91, 0x0145, 0x4C21, 0x91, 0x92, 0x5A, 0xAB, 0x40,
                 0xED, 0xDF, 0x80);
MIME_DEFINE_GUID(IID_IUIAnimationManagerEventHandler2, 0xF6E022BA, 0xBFF3, 0x42EC, 0x90, 0x33, 0xE0, 0x73, 0xF3, 0x3E,
                 0x83, 0xC3);
MIME_DEFINE_GUID(IID_IUIAnimationPriorityComparison2, 0x5B6D7A37, 0x4621, 0x467C, 0x8B, 0x05, 0x70, 0x13, 0x1D, 0xE6,
                 0x2D, 0xDB);
MIME_DEFINE_GUID(IID_IUIAnimationTransitionLibrary, 0xCA5A14B1, 0xD24F, 0x48B8, 0x8F, 0xE4, 0xC7, 0x81, 0x69, 0xBA,
                 0x95, 0x4E);
MIME_DEFINE_GUID(IID_IUIAnimationTransitionLibrary2, 0x03CFAE53, 0x9580, 0x4EE3, 0xB3, 0x63, 0x2E, 0xCE, 0x51, 0xB4,
                 0xAF, 0x6A);
MIME_DEFINE_GUID(IID_IUIAnimationTransitionFactory, 0xFCD91E03, 0x3E3B, 0x45AD, 0xBB, 0xB1, 0x6D, 0xFC, 0x81, 0x53,
                 0x74, 0x3D);
MIME_DEFINE_GUID(IID_IUIAnimationInterpolator, 0x7815CBBA, 0xDDF7, 0x478C, 0xA4, 0x6C, 0x7B, 0x6C, 0x73, 0x8B, 0x79,
                 0x78);
MIME_DEFINE_GUID(IID_IUIAnimationTransitionFactory2, 0x937D4916, 0xC1A6, 0x42D5, 0x88, 0xD8, 0x30, 0x34, 0x4D, 0x6E,
                 0xFE, 0x31);
MIME_DEFINE_GUID(IID_IUIAnimationInterpolator2, 0xEA76AFF8, 0xEA22, 0x4A23, 0xA0, 0xEF, 0xA6, 0xA9, 0x66, 0x70, 0x35,
                 0x18);
MIME_DEFINE_GUID(IID_IUIAnimationPrimitiveInterpolation, 0xBAB20D63, 0x4361, 0x45DA, 0xA2, 0x4F, 0xAB, 0x85, 0x08, 0x84,
                 0x6B, 0x5B);
MIME_DEFINE_GUID(IID_IUIAnimationTimer, 0x6B0EFAD1, 0xA053, 0x41D6, 0x90, 0x85, 0x33, 0xA6, 0x89, 0x14, 0x46, 0x65);
MIME_DEFINE_GUID(IID_IUIAnimationTimerUpdateHandler, 0x195509B7, 0x5D5E, 0x4E3E, 0xB2, 0x78, 0xEE, 0x37, 0x59, 0xB3,
                 0x67, 0xAD);
MIME_DEFINE_GUID(IID_IUIAnimationTimerClientEventHandler, 0xBEDB4DB6, 0x94FA, 0x4BFB, 0xA4, 0x7F, 0xEF, 0x2D, 0x9E,
                 0x40, 0x8C, 0x25);
MIME_DEFINE_GUID(IID_IUIAnimationTimerEventHandler, 0x274A7DEA, 0xD771, 0x4095, 0xAB, 0xBD, 0x8D, 0xF7, 0xAB, 0xD2,
                 0x3C, 0xE3);

#endif
