#include "uianimation_private.h"
#include <mime/win32/dcomp.h>

static const IUIAnimationVariableVtbl g_variable_vtbl;
static const IUIAnimationVariable2Vtbl g_variable2_vtbl;

UaVariable *UaVariableFrom(const void *iface, int version) {
    if (!iface)
        return nullptr;
    const void *vtbl = *(const void *const *)iface;
    if (vtbl != (version == 2 ? (const void *)&g_variable2_vtbl : (const void *)&g_variable_vtbl))
        return nullptr;
    return (UaVariable *)iface;
}

double UaVariableClip(UaVariable *variable, UINT index, double value) {
    if (variable->has_lower && value < variable->lower[index])
        return variable->lower[index];
    if (variable->has_upper && value > variable->upper[index])
        return variable->upper[index];
    return value;
}

int UaVariableRound(UaVariable *variable, double value) {
    switch (variable->rounding) {
    case UI_ANIMATION_ROUNDING_FLOOR:
        return (int)floor(value);
    case UI_ANIMATION_ROUNDING_CEILING:
        return (int)ceil(value);
    default:
        return (int)floor(value + 0.5);
    }
}

/* ------------------------------------------------------------------ the schedule of a variable */

HRESULT UaVariableAddTrack(UaVariable *variable, UaTrack *track) {
    if (variable->track_count == variable->track_capacity) {
        UINT capacity = variable->track_capacity ? variable->track_capacity * 2 : 4;
        UaTrack **tracks = UaRealloc(variable->tracks, capacity * sizeof(*tracks));
        if (!tracks)
            return E_OUTOFMEMORY;
        variable->tracks = tracks;
        variable->track_capacity = capacity;
    }
    /* after every track that starts no later, so a later storyboard wins a tie. */
    UINT at = variable->track_count;
    while (at > 0 && variable->tracks[at - 1]->start_time > track->start_time)
        at--;
    for (UINT i = variable->track_count; i > at; i--)
        variable->tracks[i] = variable->tracks[i - 1];
    variable->tracks[at] = track;
    variable->track_count++;
    return S_OK;
}

void UaVariableRemoveTrack(UaVariable *variable, UaTrack *track) {
    for (UINT i = 0; i < variable->track_count; i++) {
        if (variable->tracks[i] != track)
            continue;
        for (UINT j = i + 1; j < variable->track_count; j++)
            variable->tracks[j - 1] = variable->tracks[j];
        variable->track_count--;
        break;
    }
    /* a track taken over by this one plays on again. */
    for (UINT i = 0; i < variable->track_count; i++)
        if (variable->tracks[i]->trimmed_by == track) {
            variable->tracks[i]->trim = UA_NONE;
            variable->tracks[i]->trimmed_by = nullptr;
        }
}

void UaVariableAt(UaVariable *variable, double time, UaTrack *exclude, double *value, double *velocity) {
    for (UINT d = 0; d < variable->dimension; d++) {
        value[d] = variable->rest[d];
        velocity[d] = 0.0;
    }
    for (UINT i = 0; i < variable->track_count; i++) {
        UaTrack *track = variable->tracks[i];
        if (track == exclude || track->start_time > time)
            continue;
        if (track->trim != UA_NONE && time >= track->trim)
            continue;
        BOOL controls;
        double v[UA_MAX_DIMENSION], s[UA_MAX_DIMENSION];
        UaTrackAt(track, time, v, s, &controls);
        if (!controls)
            continue;
        for (UINT d = 0; d < variable->dimension; d++) {
            value[d] = v[d];
            velocity[d] = s[d];
        }
    }
}

void UaVariableRefresh(UaVariable *variable, double time) {
    double value[UA_MAX_DIMENSION], velocity[UA_MAX_DIMENSION];
    UaVariableAt(variable, time, nullptr, value, velocity);
    for (UINT d = 0; d < variable->dimension; d++) {
        if (value[d] != variable->value[d])
            variable->manager->changed = TRUE;
        variable->value[d] = value[d];
        variable->velocity[d] = velocity[d];
    }
    variable->current = nullptr;
    for (UINT i = 0; i < variable->track_count; i++) {
        UaTrack *track = variable->tracks[i];
        if (track->storyboard->status != UI_ANIMATION_STORYBOARD_PLAYING || track->start_time > time)
            continue;
        if (track->trim != UA_NONE && time >= track->trim)
            continue;
        if (time < track->end_time || track->held)
            variable->current = track->storyboard;
    }
}

static BOOL dimension_values(UaVariable *variable, const double *values, int *integers, double *clipped) {
    for (UINT d = 0; d < variable->dimension; d++) {
        clipped[d] = UaVariableClip(variable, d, values[d]);
        integers[d] = UaVariableRound(variable, clipped[d]);
    }
    return TRUE;
}

HRESULT UaVariableCallHandlers(UaVariable *variable) {
    UaManager *manager = variable->manager;
    double now[UA_MAX_DIMENSION], before[UA_MAX_DIMENSION];
    int now_int[UA_MAX_DIMENSION], before_int[UA_MAX_DIMENSION];
    dimension_values(variable, variable->value, now_int, now);
    dimension_values(variable, variable->previous, before_int, before);
    BOOL changed = FALSE, integer_changed = FALSE;
    for (UINT d = 0; d < variable->dimension; d++) {
        changed |= now[d] != before[d];
        integer_changed |= now_int[d] != before_int[d];
    }
    if (!changed)
        return S_OK;
    void *storyboard = variable->current ? UaStoryboardIface(variable->current) : nullptr;
    HRESULT hr = S_OK;
    UINT saved;
    ((IUnknown *)variable)->lpVtbl->AddRef((IUnknown *)variable);
    UaManagerEnter(manager, UA_CALLBACK_VARIABLE, &saved);
    if (variable->change_handler) {
        if (variable->version == 2) {
            IUIAnimationVariableChangeHandler2 *handler =
                (IUIAnimationVariableChangeHandler2 *)variable->change_handler;
            hr = handler->lpVtbl->OnValueChanged(handler, storyboard, (IUIAnimationVariable2 *)variable, now, before,
                                                 variable->dimension);
        } else {
            IUIAnimationVariableChangeHandler *handler = (IUIAnimationVariableChangeHandler *)variable->change_handler;
            hr = handler->lpVtbl->OnValueChanged(handler, storyboard, (IUIAnimationVariable *)variable, now[0],
                                                 before[0]);
        }
    }
    if (SUCCEEDED(hr) && integer_changed && variable->integer_handler) {
        if (variable->version == 2) {
            IUIAnimationVariableIntegerChangeHandler2 *handler =
                (IUIAnimationVariableIntegerChangeHandler2 *)variable->integer_handler;
            hr = handler->lpVtbl->OnIntegerValueChanged(handler, storyboard, (IUIAnimationVariable2 *)variable, now_int,
                                                        before_int, variable->dimension);
        } else {
            IUIAnimationVariableIntegerChangeHandler *handler =
                (IUIAnimationVariableIntegerChangeHandler *)variable->integer_handler;
            hr = handler->lpVtbl->OnIntegerValueChanged(handler, storyboard, (IUIAnimationVariable *)variable,
                                                        now_int[0], before_int[0]);
        }
    }
    UaManagerLeave(manager, saved);
    ((IUnknown *)variable)->lpVtbl->Release((IUnknown *)variable);
    return hr;
}

void UaVariableZombify(UaVariable *variable) {
    if (variable->change_handler)
        variable->change_handler->lpVtbl->Release(variable->change_handler);
    if (variable->integer_handler)
        variable->integer_handler->lpVtbl->Release(variable->integer_handler);
    if (variable->curve_handler)
        variable->curve_handler->lpVtbl->Release(variable->curve_handler);
    variable->change_handler = variable->integer_handler = nullptr;
    variable->curve_handler = nullptr;
}

/* ------------------------------------------------------------------ IUIAnimationVariable */

static HRESULT check(UaVariable *variable, UINT allowed) {
    UaManager *manager = variable->manager;
    if (UaManagerWrongThread(manager))
        return UI_E_WRONG_THREAD;
    if (manager->shutdown)
        return UI_E_SHUTDOWN_CALLED;
    if (UaManagerInCallback(manager, allowed))
        return UI_E_ILLEGAL_REENTRANCY;
    return S_OK;
}

#define READABLE (UA_CALLBACK_VARIABLE | UA_CALLBACK_STORYBOARD | UA_CALLBACK_UPDATED)

static HRESULT STDMETHODCALLTYPE variable_query(IUnknown *iface, REFIID iid, void **out) {
    UaVariable *variable = (UaVariable *)iface;
    return UaQuery(iface, variable->version == 2 ? &IID_IUIAnimationVariable2 : &IID_IUIAnimationVariable, iid, out);
}

static ULONG STDMETHODCALLTYPE variable_addref(IUnknown *iface) {
    return InterlockedIncrement(&((UaVariable *)iface)->refs);
}

static ULONG STDMETHODCALLTYPE variable_release(IUnknown *iface) {
    UaVariable *variable = (UaVariable *)iface;
    ULONG refs = InterlockedDecrement(&variable->refs);
    if (!refs) {
        UaVariableZombify(variable);
        if (variable->tag_object)
            variable->tag_object->lpVtbl->Release(variable->tag_object);
        UaManagerDetachVariable(variable->manager, variable);
        ((IUnknown *)variable->manager)->lpVtbl->Release((IUnknown *)variable->manager);
        UaFree(variable->tracks);
        UaFree(variable);
        UaObjectCountChange(-1);
    }
    return refs;
}

static HRESULT get_values(UaVariable *variable, const double *source, double *out, UINT count) {
    if (!out)
        return E_POINTER;
    for (UINT d = 0; d < count; d++)
        out[d] = 0.0;
    HRESULT hr = check(variable, READABLE);
    if (FAILED(hr))
        return hr;
    if (count != variable->dimension)
        return UI_E_INVALID_DIMENSION;
    for (UINT d = 0; d < count; d++)
        out[d] = UaVariableClip(variable, d, source[d]);
    return S_OK;
}

static HRESULT get_integers(UaVariable *variable, const double *source, int *out, UINT count) {
    if (!out)
        return E_POINTER;
    for (UINT d = 0; d < count; d++)
        out[d] = 0;
    HRESULT hr = check(variable, READABLE);
    if (FAILED(hr))
        return hr;
    if (count != variable->dimension)
        return UI_E_INVALID_DIMENSION;
    for (UINT d = 0; d < count; d++)
        out[d] = UaVariableRound(variable, UaVariableClip(variable, d, source[d]));
    return S_OK;
}

/* the value once every scheduled storyboard has played, which a storyboard repeating forever leaves open. */
static HRESULT final_values(UaVariable *variable, double *out) {
    for (UINT d = 0; d < variable->dimension; d++)
        out[d] = variable->rest[d];
    double latest = -UA_FOREVER;
    for (UINT i = 0; i < variable->track_count; i++) {
        UaTrack *track = variable->tracks[i];
        if (track->trim != UA_NONE)
            continue;
        if (track->end_time >= UA_FOREVER)
            return UI_E_VALUE_NOT_DETERMINED;
        if (track->end_time >= latest) {
            latest = track->end_time;
            UaTrackFinal(track, out);
        }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE variable_get_value(IUIAnimationVariable *iface, double *value) {
    UaVariable *variable = (UaVariable *)iface;
    return get_values(variable, variable->value, value, variable->dimension);
}

static HRESULT STDMETHODCALLTYPE variable_get_previous(IUIAnimationVariable *iface, double *value) {
    UaVariable *variable = (UaVariable *)iface;
    return get_values(variable, variable->previous, value, variable->dimension);
}

static HRESULT STDMETHODCALLTYPE variable_get_final(IUIAnimationVariable *iface, double *value) {
    UaVariable *variable = (UaVariable *)iface;
    if (!value)
        return E_POINTER;
    *value = 0.0;
    HRESULT hr = check(variable, READABLE);
    if (FAILED(hr))
        return hr;
    double final[UA_MAX_DIMENSION];
    hr = final_values(variable, final);
    if (FAILED(hr))
        return hr;
    return get_values(variable, final, value, variable->dimension);
}

static HRESULT STDMETHODCALLTYPE variable_get_integer(IUIAnimationVariable *iface, int *value) {
    UaVariable *variable = (UaVariable *)iface;
    return get_integers(variable, variable->value, value, variable->dimension);
}

static HRESULT STDMETHODCALLTYPE variable_get_final_integer(IUIAnimationVariable *iface, int *value) {
    UaVariable *variable = (UaVariable *)iface;
    if (!value)
        return E_POINTER;
    *value = 0;
    HRESULT hr = check(variable, READABLE);
    if (FAILED(hr))
        return hr;
    double final[UA_MAX_DIMENSION];
    hr = final_values(variable, final);
    if (FAILED(hr))
        return hr;
    return get_integers(variable, final, value, variable->dimension);
}

static HRESULT STDMETHODCALLTYPE variable_get_previous_integer(IUIAnimationVariable *iface, int *value) {
    UaVariable *variable = (UaVariable *)iface;
    return get_integers(variable, variable->previous, value, variable->dimension);
}

static HRESULT STDMETHODCALLTYPE variable_get_current_storyboard(IUIAnimationVariable *iface, void **storyboard) {
    UaVariable *variable = (UaVariable *)iface;
    if (!storyboard)
        return E_POINTER;
    *storyboard = nullptr;
    HRESULT hr = check(variable, READABLE);
    if (FAILED(hr))
        return hr;
    if (variable->current) {
        *storyboard = UaStoryboardIface(variable->current);
        ((IUnknown *)*storyboard)->lpVtbl->AddRef((IUnknown *)*storyboard);
    }
    return S_OK;
}

static HRESULT set_bounds(UaVariable *variable, const double *bound, UINT count, BOOL upper) {
    HRESULT hr = check(variable, 0);
    if (FAILED(hr))
        return hr;
    if (count != variable->dimension)
        return UI_E_INVALID_DIMENSION;
    for (UINT d = 0; d < count; d++) {
        if (upper && variable->has_lower && bound[d] < variable->lower[d])
            return E_INVALIDARG;
        if (!upper && variable->has_upper && bound[d] > variable->upper[d])
            return E_INVALIDARG;
    }
    for (UINT d = 0; d < count; d++)
        (upper ? variable->upper : variable->lower)[d] = bound[d];
    if (upper)
        variable->has_upper = TRUE;
    else
        variable->has_lower = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE variable_set_lower(IUIAnimationVariable *iface, double bound) {
    UaVariable *variable = (UaVariable *)iface;
    double bounds[UA_MAX_DIMENSION];
    for (UINT d = 0; d < variable->dimension; d++)
        bounds[d] = bound;
    return set_bounds(variable, bounds, variable->dimension, FALSE);
}

static HRESULT STDMETHODCALLTYPE variable_set_upper(IUIAnimationVariable *iface, double bound) {
    UaVariable *variable = (UaVariable *)iface;
    double bounds[UA_MAX_DIMENSION];
    for (UINT d = 0; d < variable->dimension; d++)
        bounds[d] = bound;
    return set_bounds(variable, bounds, variable->dimension, TRUE);
}

static HRESULT STDMETHODCALLTYPE variable_set_rounding(IUIAnimationVariable *iface, UI_ANIMATION_ROUNDING_MODE mode) {
    UaVariable *variable = (UaVariable *)iface;
    HRESULT hr = check(variable, 0);
    if (FAILED(hr))
        return hr;
    if ((UINT)mode > UI_ANIMATION_ROUNDING_CEILING)
        return E_INVALIDARG;
    variable->rounding = mode;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE variable_set_tag(IUIAnimationVariable *iface, IUnknown *object, UINT32 id) {
    UaVariable *variable = (UaVariable *)iface;
    HRESULT hr = check(variable, READABLE);
    if (FAILED(hr))
        return hr;
    if (object)
        object->lpVtbl->AddRef(object);
    if (variable->tag_object)
        variable->tag_object->lpVtbl->Release(variable->tag_object);
    variable->tag_object = object;
    variable->tag_id = id;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE variable_get_tag(IUIAnimationVariable *iface, IUnknown **object, UINT32 *id) {
    UaVariable *variable = (UaVariable *)iface;
    if (object)
        *object = nullptr;
    if (id)
        *id = 0;
    HRESULT hr = check(variable, READABLE);
    if (FAILED(hr))
        return hr;
    if (!variable->tag_object && !variable->tag_id)
        return UI_E_VALUE_NOT_SET;
    if (object) {
        *object = variable->tag_object;
        if (*object)
            (*object)->lpVtbl->AddRef(*object);
    }
    if (id)
        *id = variable->tag_id;
    return S_OK;
}

static HRESULT set_handler(UaVariable *variable, IUnknown **slot, IUnknown *handler) {
    HRESULT hr = check(variable, READABLE);
    if (FAILED(hr))
        return hr;
    if (handler)
        handler->lpVtbl->AddRef(handler);
    if (*slot)
        (*slot)->lpVtbl->Release(*slot);
    *slot = handler;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE variable_set_change_handler(IUIAnimationVariable *iface, IUnknown *handler) {
    UaVariable *variable = (UaVariable *)iface;
    return set_handler(variable, &variable->change_handler, handler);
}

static HRESULT STDMETHODCALLTYPE variable_set_integer_handler(IUIAnimationVariable *iface, IUnknown *handler) {
    UaVariable *variable = (UaVariable *)iface;
    return set_handler(variable, &variable->integer_handler, handler);
}

/* ------------------------------------------------------------------ IUIAnimationVariable2 */

static HRESULT STDMETHODCALLTYPE variable2_get_dimension(IUIAnimationVariable2 *iface, UINT *dimension) {
    if (!dimension)
        return E_POINTER;
    *dimension = ((UaVariable *)iface)->dimension;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE variable2_get_value(IUIAnimationVariable2 *iface, double *value) {
    UaVariable *variable = (UaVariable *)iface;
    if (!value)
        return E_POINTER;
    *value = 0.0;
    HRESULT hr = check(variable, READABLE);
    if (FAILED(hr))
        return hr;
    *value = UaVariableClip(variable, 0, variable->value[0]);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE variable2_get_vector_value(IUIAnimationVariable2 *iface, double *value, UINT count) {
    UaVariable *variable = (UaVariable *)iface;
    return get_values(variable, variable->value, value, count);
}

static HRESULT STDMETHODCALLTYPE variable2_get_final_vector(IUIAnimationVariable2 *iface, double *value, UINT count) {
    UaVariable *variable = (UaVariable *)iface;
    if (!value)
        return E_POINTER;
    HRESULT hr = check(variable, READABLE);
    if (FAILED(hr))
        return hr;
    double final[UA_MAX_DIMENSION];
    hr = final_values(variable, final);
    if (FAILED(hr))
        return hr;
    return get_values(variable, final, value, count);
}

static HRESULT STDMETHODCALLTYPE variable2_get_previous_vector(IUIAnimationVariable2 *iface, double *value,
                                                               UINT count) {
    UaVariable *variable = (UaVariable *)iface;
    return get_values(variable, variable->previous, value, count);
}

static HRESULT STDMETHODCALLTYPE variable2_get_integer_vector(IUIAnimationVariable2 *iface, int *value, UINT count) {
    UaVariable *variable = (UaVariable *)iface;
    return get_integers(variable, variable->value, value, count);
}

static HRESULT STDMETHODCALLTYPE variable2_get_final_integer_vector(IUIAnimationVariable2 *iface, int *value,
                                                                    UINT count) {
    UaVariable *variable = (UaVariable *)iface;
    if (!value)
        return E_POINTER;
    HRESULT hr = check(variable, READABLE);
    if (FAILED(hr))
        return hr;
    double final[UA_MAX_DIMENSION];
    hr = final_values(variable, final);
    if (FAILED(hr))
        return hr;
    return get_integers(variable, final, value, count);
}

static HRESULT STDMETHODCALLTYPE variable2_get_previous_integer_vector(IUIAnimationVariable2 *iface, int *value,
                                                                       UINT count) {
    UaVariable *variable = (UaVariable *)iface;
    return get_integers(variable, variable->previous, value, count);
}

static HRESULT STDMETHODCALLTYPE variable2_set_lower_vector(IUIAnimationVariable2 *iface, double *bound, UINT count) {
    if (!bound)
        return E_POINTER;
    return set_bounds((UaVariable *)iface, bound, count, FALSE);
}

static HRESULT STDMETHODCALLTYPE variable2_set_upper_vector(IUIAnimationVariable2 *iface, double *bound, UINT count) {
    if (!bound)
        return E_POINTER;
    return set_bounds((UaVariable *)iface, bound, count, TRUE);
}

static HRESULT STDMETHODCALLTYPE variable2_set_change_handler(IUIAnimationVariable2 *iface, IUnknown *handler,
                                                              BOOL next_event) {
    UaVariable *variable = (UaVariable *)iface;
    return set_handler(variable, &variable->change_handler, handler);
}

static HRESULT STDMETHODCALLTYPE variable2_set_integer_handler(IUIAnimationVariable2 *iface, IUnknown *handler,
                                                               BOOL next_event) {
    UaVariable *variable = (UaVariable *)iface;
    return set_handler(variable, &variable->integer_handler, handler);
}

static HRESULT STDMETHODCALLTYPE variable2_set_curve_handler(IUIAnimationVariable2 *iface,
                                                             IUIAnimationVariableCurveChangeHandler2 *handler) {
    UaVariable *variable = (UaVariable *)iface;
    return set_handler(variable, (IUnknown **)&variable->curve_handler, (IUnknown *)handler);
}

/* the curve of a variable feeds the pieces of its transitions to one direct composition animation per dimension. */
typedef struct UaCurveSink {
    IUIAnimationPrimitiveInterpolation iface;
    IDCompositionAnimation **animations;
    UINT count;
    double base;
} UaCurveSink;

static HRESULT STDMETHODCALLTYPE sink_query(IUIAnimationPrimitiveInterpolation *iface, REFIID iid, void **out) {
    return UaQuery((IUnknown *)iface, &IID_IUIAnimationPrimitiveInterpolation, iid, out);
}

static ULONG STDMETHODCALLTYPE sink_addref(IUIAnimationPrimitiveInterpolation *iface) { return 2; }

static ULONG STDMETHODCALLTYPE sink_release(IUIAnimationPrimitiveInterpolation *iface) { return 1; }

static HRESULT STDMETHODCALLTYPE sink_cubic(IUIAnimationPrimitiveInterpolation *iface, UINT dimension, double begin,
                                            float constant, float linear, float quadratic, float cubic) {
    UaCurveSink *sink = (UaCurveSink *)iface;
    if (dimension >= sink->count)
        return UI_E_PRIMITIVE_OUT_OF_BOUNDS;
    IDCompositionAnimation *animation = sink->animations[dimension];
    return animation->lpVtbl->AddCubic(animation, sink->base + begin, constant, linear, quadratic, cubic);
}

static HRESULT STDMETHODCALLTYPE sink_sinusoidal(IUIAnimationPrimitiveInterpolation *iface, UINT dimension,
                                                 double begin, float bias, float amplitude, float frequency,
                                                 float phase) {
    UaCurveSink *sink = (UaCurveSink *)iface;
    if (dimension >= sink->count)
        return UI_E_PRIMITIVE_OUT_OF_BOUNDS;
    IDCompositionAnimation *animation = sink->animations[dimension];
    return animation->lpVtbl->AddSinusoidal(animation, sink->base + begin, bias, amplitude, frequency, phase);
}

static const IUIAnimationPrimitiveInterpolationVtbl g_sink_vtbl = {
    sink_query, sink_addref, sink_release, sink_cubic, sink_sinusoidal,
};

static HRESULT write_curve(UaVariable *variable, IDCompositionAnimation **animations, UINT count) {
    HRESULT hr = check(variable, READABLE);
    if (FAILED(hr))
        return hr;
    if (count != variable->dimension)
        return UI_E_INVALID_DIMENSION;
    double now = UaManagerTime(variable->manager);
    for (UINT d = 0; d < count; d++) {
        if (!animations[d])
            return E_POINTER;
        animations[d]->lpVtbl->Reset(animations[d]);
        LARGE_INTEGER frequency, begin;
        QueryPerformanceFrequency(&frequency);
        begin.QuadPart = (LONGLONG)((now + variable->manager->offset) * (double)frequency.QuadPart);
        animations[d]->lpVtbl->SetAbsoluteBeginTime(animations[d], begin);
        hr = animations[d]->lpVtbl->AddCubic(animations[d], 0.0, (float)UaVariableClip(variable, d, variable->value[d]),
                                             0.0f, 0.0f, 0.0f);
        if (FAILED(hr))
            return hr;
    }
    UaCurveSink sink = {{&g_sink_vtbl}, animations, count, 0.0};
    double last = 0.0;
    for (UINT i = 0; i < variable->track_count; i++) {
        UaTrack *track = variable->tracks[i];
        UaStoryboard *storyboard = track->storyboard;
        if (track->trim != UA_NONE || track->end_time >= UA_FOREVER || storyboard->loop_count ||
            storyboard->finish_time != UA_NONE)
            continue;
        for (UINT j = 0; j < track->item_count; j++) {
            UaItem *item = track->items[j];
            double begin = storyboard->start_time + item->begin - now;
            if (storyboard->start_time + item->finish < now)
                continue;
            sink.base = begin;
            hr = UaInterpPrimitive(item->transition, &sink.iface, 0.0);
            if (FAILED(hr))
                return hr;
            if (begin + item->finish - item->begin > last)
                last = begin + item->finish - item->begin;
        }
    }
    double final[UA_MAX_DIMENSION];
    if (SUCCEEDED(final_values(variable, final)))
        for (UINT d = 0; d < count; d++)
            animations[d]->lpVtbl->End(animations[d], last > 0.0 ? last : 0.0,
                                       (float)UaVariableClip(variable, d, final[d]));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE variable2_get_curve(IUIAnimationVariable2 *iface, IDCompositionAnimation *animation) {
    if (!animation)
        return E_POINTER;
    return write_curve((UaVariable *)iface, &animation, 1);
}

static HRESULT STDMETHODCALLTYPE variable2_get_vector_curve(IUIAnimationVariable2 *iface,
                                                            IDCompositionAnimation **animations, UINT count) {
    if (!animations)
        return E_POINTER;
    return write_curve((UaVariable *)iface, animations, count);
}

static HRESULT STDMETHODCALLTYPE variable2_get_final(IUIAnimationVariable2 *iface, double *value) {
    return variable_get_final((IUIAnimationVariable *)iface, value);
}

static const IUIAnimationVariableVtbl g_variable_vtbl = {
    VTBL_FN(variable_query),
    VTBL_FN(variable_addref),
    VTBL_FN(variable_release),
    variable_get_value,
    variable_get_final,
    variable_get_previous,
    variable_get_integer,
    variable_get_final_integer,
    variable_get_previous_integer,
    VTBL_FN(variable_get_current_storyboard),
    variable_set_lower,
    variable_set_upper,
    variable_set_rounding,
    variable_set_tag,
    variable_get_tag,
    VTBL_FN(variable_set_change_handler),
    VTBL_FN(variable_set_integer_handler),
};

static const IUIAnimationVariable2Vtbl g_variable2_vtbl = {
    VTBL_FN(variable_query),
    VTBL_FN(variable_addref),
    VTBL_FN(variable_release),
    variable2_get_dimension,
    variable2_get_value,
    variable2_get_vector_value,
    variable2_get_curve,
    variable2_get_vector_curve,
    variable2_get_final,
    variable2_get_final_vector,
    VTBL_FN(variable_get_previous),
    variable2_get_previous_vector,
    VTBL_FN(variable_get_integer),
    variable2_get_integer_vector,
    VTBL_FN(variable_get_final_integer),
    variable2_get_final_integer_vector,
    VTBL_FN(variable_get_previous_integer),
    variable2_get_previous_integer_vector,
    VTBL_FN(variable_get_current_storyboard),
    VTBL_FN(variable_set_lower),
    variable2_set_lower_vector,
    VTBL_FN(variable_set_upper),
    variable2_set_upper_vector,
    VTBL_FN(variable_set_rounding),
    VTBL_FN(variable_set_tag),
    VTBL_FN(variable_get_tag),
    VTBL_FN(variable2_set_change_handler),
    VTBL_FN(variable2_set_integer_handler),
    variable2_set_curve_handler,
};

HRESULT UaVariableCreate(UaManager *manager, const double *value, UINT dimension, void **out) {
    UaVariable *variable = UaAlloc(sizeof(*variable));
    if (!variable)
        return E_OUTOFMEMORY;
    variable->lpVtbl = manager->version == 2 ? (const void *)&g_variable2_vtbl : (const void *)&g_variable_vtbl;
    variable->refs = 1;
    variable->version = manager->version;
    variable->manager = manager;
    ((IUnknown *)manager)->lpVtbl->AddRef((IUnknown *)manager);
    variable->dimension = dimension;
    for (UINT d = 0; d < dimension; d++)
        variable->rest[d] = variable->value[d] = variable->previous[d] = value[d];
    if (manager->variable_count == manager->variable_capacity) {
        UINT capacity = manager->variable_capacity ? manager->variable_capacity * 2 : 16;
        UaVariable **variables = UaRealloc(manager->variables, capacity * sizeof(*variables));
        if (!variables) {
            ((IUnknown *)manager)->lpVtbl->Release((IUnknown *)manager);
            UaFree(variable);
            return E_OUTOFMEMORY;
        }
        manager->variables = variables;
        manager->variable_capacity = capacity;
    }
    manager->variables[manager->variable_count++] = variable;
    UaObjectCountChange(1);
    *out = variable;
    return S_OK;
}
