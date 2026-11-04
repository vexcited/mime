#include "uianimation_private.h"

static const IUIAnimationManagerVtbl g_manager_vtbl;
static const IUIAnimationManager2Vtbl g_manager2_vtbl;
static const IUIAnimationTimerUpdateHandlerVtbl g_update_vtbl;

static UaManager *from_update(IUIAnimationTimerUpdateHandler *iface) {
    return (UaManager *)((char *)iface - offsetof(UaManager, update_vtbl));
}

static BOOL grow(void **array, UINT *capacity, UINT count, SIZE_T size) {
    if (count < *capacity)
        return TRUE;
    UINT next = *capacity ? *capacity * 2 : 8;
    void *grown = UaRealloc(*array, next * size);
    if (!grown)
        return FALSE;
    *array = grown;
    *capacity = next;
    return TRUE;
}

BOOL UaManagerWrongThread(UaManager *manager) { return GetCurrentThreadId() != manager->thread; }

BOOL UaManagerInCallback(UaManager *manager, UINT allowed) {
    return manager->callbacks && !(manager->callbacks & allowed);
}

HRESULT UaManagerCheck(UaManager *manager, UINT allowed) {
    if (UaManagerWrongThread(manager))
        return UI_E_WRONG_THREAD;
    if (manager->shutdown)
        return UI_E_SHUTDOWN_CALLED;
    if (UaManagerInCallback(manager, allowed))
        return UI_E_ILLEGAL_REENTRANCY;
    return S_OK;
}

void UaManagerEnter(UaManager *manager, UINT callback, UINT *saved) {
    *saved = manager->callbacks;
    manager->callbacks |= callback;
}

void UaManagerLeave(UaManager *manager, UINT saved) { manager->callbacks = saved; }

double UaManagerTime(UaManager *manager) { return manager->now == UA_NONE ? 0.0 : manager->now; }

void UaManagerDetachVariable(UaManager *manager, UaVariable *variable) {
    for (UINT i = 0; i < manager->variable_count; i++)
        if (manager->variables[i] == variable) {
            manager->variables[i] = manager->variables[--manager->variable_count];
            return;
        }
}

HRESULT UaManagerSetStatus(UaManager *manager, UI_ANIMATION_MANAGER_STATUS status) {
    UI_ANIMATION_MANAGER_STATUS previous = manager->status;
    manager->status = status;
    if (previous == status)
        return S_OK;
    HRESULT hr = S_OK;
    UINT saved;
    ((IUnknown *)manager)->lpVtbl->AddRef((IUnknown *)manager);
    UaManagerEnter(manager, UA_CALLBACK_MANAGER, &saved);
    if (manager->event_handler) {
        /* both versions of the handler take the same arguments. */
        IUIAnimationManagerEventHandler *handler = (IUIAnimationManagerEventHandler *)manager->event_handler;
        hr = handler->lpVtbl->OnManagerStatusChanged(handler, status, previous);
    }
    if (SUCCEEDED(hr) && manager->timer_client)
        hr = manager->timer_client->lpVtbl->OnTimerClientStatusChanged(
            manager->timer_client,
            status == UI_ANIMATION_MANAGER_BUSY ? UI_ANIMATION_TIMER_CLIENT_BUSY : UI_ANIMATION_TIMER_CLIENT_IDLE,
            previous == UI_ANIMATION_MANAGER_BUSY ? UI_ANIMATION_TIMER_CLIENT_BUSY : UI_ANIMATION_TIMER_CLIENT_IDLE);
    UaManagerLeave(manager, saved);
    ((IUnknown *)manager)->lpVtbl->Release((IUnknown *)manager);
    return hr;
}

static void remove_scheduled(UaManager *manager, UaStoryboard *storyboard) {
    for (UINT i = 0; i < manager->scheduled_count; i++) {
        if (manager->scheduled[i] != storyboard)
            continue;
        for (UINT j = i + 1; j < manager->scheduled_count; j++)
            manager->scheduled[j - 1] = manager->scheduled[j];
        manager->scheduled_count--;
        ((IUnknown *)storyboard)->lpVtbl->Release((IUnknown *)storyboard);
        return;
    }
}

/* ends a storyboard with a status, then makes it ready to be scheduled again. */
HRESULT UaManagerFinishStoryboard(UaManager *manager, UaStoryboard *storyboard, UI_ANIMATION_STORYBOARD_STATUS status) {
    ((IUnknown *)storyboard)->lpVtbl->AddRef((IUnknown *)storyboard);
    HRESULT hr = S_OK;
    if (status == UI_ANIMATION_STORYBOARD_FINISHED && storyboard->status == UI_ANIMATION_STORYBOARD_PLAYING)
        hr = UaStoryboardCallUpdated(storyboard);
    if (SUCCEEDED(hr))
        hr = UaStoryboardSetStatus(storyboard, status);
    UaStoryboardUnschedule(storyboard);
    remove_scheduled(manager, storyboard);
    storyboard->finish_time = UA_NONE;
    if (SUCCEEDED(hr))
        hr = UaStoryboardSetStatus(storyboard, UI_ANIMATION_STORYBOARD_READY);
    ((IUnknown *)storyboard)->lpVtbl->Release((IUnknown *)storyboard);
    return hr;
}

/* ------------------------------------------------------------------ scheduling */

static HRESULT has_priority(UaManager *manager, IUnknown *comparison, UaStoryboard *scheduled, UaStoryboard *incoming,
                            UI_ANIMATION_PRIORITY_EFFECT effect, BOOL fallback, BOOL *result) {
    *result = fallback;
    if (!comparison)
        return S_OK;
    /* both versions of the comparison take the same arguments. */
    IUIAnimationPriorityComparison *c = (IUIAnimationPriorityComparison *)comparison;
    UINT saved;
    UaManagerEnter(manager, UA_CALLBACK_PRIORITY, &saved);
    HRESULT hr = c->lpVtbl->HasPriority(c, UaStoryboardIface(scheduled), UaStoryboardIface(incoming), effect);
    UaManagerLeave(manager, saved);
    if (hr == S_OK)
        *result = TRUE;
    else if (hr == S_FALSE)
        *result = FALSE;
    else if (SUCCEEDED(hr))
        return UI_E_BOOLEAN_EXPECTED;
    else
        return hr;
    return S_OK;
}

static BOOL animations_disabled(UaManager *manager) {
    if (manager->mode == UI_ANIMATION_MODE_DISABLED)
        return TRUE;
    if (manager->mode == UI_ANIMATION_MODE_SYSTEM_DEFAULT) {
        BOOL enabled = TRUE;
        if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) && !enabled)
            return TRUE;
    }
    return FALSE;
}

typedef struct UaAction {
    UaTrack *old;
    UaTrack *incoming;
    BOOL cancel;
} UaAction;

HRESULT UaManagerSchedule(UaManager *manager, UaStoryboard *storyboard, double time,
                          UI_ANIMATION_SCHEDULING_RESULT *result) {
    double now = time - manager->offset;
    if (manager->now != UA_NONE && now < manager->now)
        now = manager->now;
    double limit = storyboard->delay != UA_NONE ? storyboard->delay : manager->default_delay;
    double start = now;
    UaAction *actions = nullptr;
    UINT action_count = 0, action_capacity = 0;
    HRESULT hr = S_OK;
    BOOL settled = FALSE;
    for (int attempt = 0; attempt < 16 && !settled; attempt++) {
        hr = UaStoryboardResolve(storyboard, start);
        if (FAILED(hr))
            goto done;
        action_count = 0;
        double latest = start;
        for (UINT i = 0; i < storyboard->track_count; i++) {
            UaTrack *track = storyboard->tracks[i];
            UaVariable *variable = track->variable;
            for (UINT j = 0; j < variable->track_count; j++) {
                UaTrack *old = variable->tracks[j];
                UaStoryboard *other = old->storyboard;
                if (other == storyboard)
                    continue;
                if (old->trim != UA_NONE && old->trim <= track->start_time)
                    continue;
                if (old->end_time <= track->start_time || old->start_time >= track->end_time)
                    continue;
                double needed = old->end_time >= UA_FOREVER ? UA_FOREVER : old->end_time - track->start_time;
                UI_ANIMATION_PRIORITY_EFFECT effect =
                    needed > limit ? UI_ANIMATION_PRIORITY_EFFECT_FAILURE : UI_ANIMATION_PRIORITY_EFFECT_DELAY;
                BOOL yes;
                if (other->status == UI_ANIMATION_STORYBOARD_SCHEDULED) {
                    hr = has_priority(manager, manager->cancel_comparison, other, storyboard, effect,
                                      effect == UI_ANIMATION_PRIORITY_EFFECT_FAILURE, &yes);
                    if (FAILED(hr))
                        goto done;
                    if (yes) {
                        if (!grow((void **)&actions, &action_capacity, action_count, sizeof(*actions))) {
                            hr = E_OUTOFMEMORY;
                            goto done;
                        }
                        actions[action_count++] = (UaAction){old, track, TRUE};
                        continue;
                    }
                }
                hr = has_priority(manager, manager->trim_comparison, other, storyboard, effect,
                                  effect == UI_ANIMATION_PRIORITY_EFFECT_FAILURE, &yes);
                if (FAILED(hr))
                    goto done;
                if (!yes && old->end_time >= UA_FOREVER) {
                    hr = has_priority(manager, manager->conclude_comparison, other, storyboard, effect, TRUE, &yes);
                    if (FAILED(hr))
                        goto done;
                    if (yes) {
                        UaStoryboardConcludeLoops(other, UaManagerTime(manager));
                        if (old->end_time <= track->start_time)
                            continue;
                        needed = old->end_time - track->start_time;
                        yes = FALSE;
                    }
                }
                if (!yes) {
                    hr = has_priority(manager, manager->compress_comparison, other, storyboard, effect, FALSE, &yes);
                    if (FAILED(hr))
                        goto done;
                }
                if (yes) {
                    if (!grow((void **)&actions, &action_capacity, action_count, sizeof(*actions))) {
                        hr = E_OUTOFMEMORY;
                        goto done;
                    }
                    actions[action_count++] = (UaAction){old, track, FALSE};
                    continue;
                }
                if (start + needed > latest)
                    latest = needed >= UA_FOREVER ? UA_FOREVER : start + needed;
            }
        }
        if (latest <= start) {
            settled = TRUE;
            break;
        }
        if (latest >= UA_FOREVER || latest - now > limit) {
            if (result)
                *result = UI_ANIMATION_SCHEDULING_INSUFFICIENT_PRIORITY;
            hr = S_OK;
            goto done;
        }
        start = latest;
    }
    if (!settled) {
        if (result)
            *result = UI_ANIMATION_SCHEDULING_INSUFFICIENT_PRIORITY;
        goto done;
    }

    ((IUnknown *)storyboard)->lpVtbl->AddRef((IUnknown *)storyboard);
    for (UINT i = 0; i < action_count; i++) {
        UaAction *action = &actions[i];
        if (action->cancel) {
            if (action->old->storyboard->status == UI_ANIMATION_STORYBOARD_SCHEDULED) {
                hr = UaManagerFinishStoryboard(manager, action->old->storyboard, UI_ANIMATION_STORYBOARD_CANCELLED);
                if (FAILED(hr))
                    break;
            }
        } else if (action->old->trim == UA_NONE || action->old->trim > action->incoming->start_time) {
            action->old->trim = action->incoming->start_time;
            action->old->trimmed_by = action->incoming;
            action->old->storyboard->trimmed = TRUE;
        }
    }
    if (SUCCEEDED(hr) && !grow((void **)&manager->scheduled, &manager->scheduled_capacity, manager->scheduled_count,
                               sizeof(*manager->scheduled)))
        hr = E_OUTOFMEMORY;
    if (FAILED(hr)) {
        ((IUnknown *)storyboard)->lpVtbl->Release((IUnknown *)storyboard);
        goto done;
    }
    manager->scheduled[manager->scheduled_count++] = storyboard;
    storyboard->trimmed = FALSE;
    for (UINT i = 0; i < storyboard->track_count; i++)
        UaVariableAddTrack(storyboard->tracks[i]->variable, storyboard->tracks[i]);
    if (animations_disabled(manager))
        UaStoryboardFinishIn(storyboard, start, 0.0);
    if (manager->now == UA_NONE || start <= manager->now)
        manager->changed = TRUE;
    if (result)
        *result = UI_ANIMATION_SCHEDULING_SUCCEEDED;
    hr = UaStoryboardSetStatus(storyboard, UI_ANIMATION_STORYBOARD_SCHEDULED);
    if (SUCCEEDED(hr))
        hr = UaManagerSetStatus(manager, UI_ANIMATION_MANAGER_BUSY);
done:
    UaFree(actions);
    return hr;
}

HRESULT UaManagerProcessDeferred(UaManager *manager) {
    HRESULT hr = S_OK;
    while (manager->deferred_count && !manager->callbacks) {
        UaStoryboard *storyboard = manager->deferred[0];
        for (UINT i = 1; i < manager->deferred_count; i++)
            manager->deferred[i - 1] = manager->deferred[i];
        manager->deferred_count--;
        if (storyboard->deferred_abandon) {
            storyboard->deferred_abandon = FALSE;
            if (storyboard->status == UI_ANIMATION_STORYBOARD_SCHEDULED ||
                storyboard->status == UI_ANIMATION_STORYBOARD_PLAYING)
                hr = UaManagerFinishStoryboard(manager, storyboard,
                                               storyboard->status == UI_ANIMATION_STORYBOARD_PLAYING
                                                   ? UI_ANIMATION_STORYBOARD_TRUNCATED
                                                   : UI_ANIMATION_STORYBOARD_CANCELLED);
        }
        if (storyboard->deferred_schedule) {
            storyboard->deferred_schedule = FALSE;
            if (SUCCEEDED(hr) && (storyboard->status == UI_ANIMATION_STORYBOARD_BUILDING ||
                                  storyboard->status == UI_ANIMATION_STORYBOARD_READY))
                hr = UaManagerSchedule(manager, storyboard, storyboard->deferred_time, nullptr);
        }
        ((IUnknown *)storyboard)->lpVtbl->Release((IUnknown *)storyboard);
        if (FAILED(hr))
            break;
    }
    return hr;
}

/* ------------------------------------------------------------------ updates */

static BOOL all_trimmed(UaStoryboard *storyboard, double time) {
    if (!storyboard->trimmed || !storyboard->track_count)
        return FALSE;
    for (UINT i = 0; i < storyboard->track_count; i++) {
        UaTrack *track = storyboard->tracks[i];
        if (track->trim == UA_NONE || track->trim > time)
            return FALSE;
    }
    return TRUE;
}

/* steps from one start or end of a storyboard to the next, so each ends in order and what it starts sees it. */
static HRESULT update_core(UaManager *manager, double target) {
    for (int guard = 0; guard < 100000; guard++) {
        double step = target;
        for (UINT i = 0; i < manager->scheduled_count; i++) {
            UaStoryboard *storyboard = manager->scheduled[i];
            double begin = storyboard->start_time;
            if (storyboard->status == UI_ANIMATION_STORYBOARD_SCHEDULED && begin < step &&
                (manager->now == UA_NONE || begin > manager->now))
                step = begin;
            double end = UaStoryboardEndTime(storyboard);
            if (end < step && (manager->now == UA_NONE || end > manager->now))
                step = end;
        }
        for (UINT i = 0; i < manager->variable_count; i++)
            UaVariableRefresh(manager->variables[i], step);
        manager->now = step;

        UINT count = manager->scheduled_count;
        UaStoryboard **list = UaAlloc((count ? count : 1) * sizeof(*list));
        if (!list)
            return E_OUTOFMEMORY;
        for (UINT i = 0; i < count; i++) {
            list[i] = manager->scheduled[i];
            ((IUnknown *)list[i])->lpVtbl->AddRef((IUnknown *)list[i]);
        }
        HRESULT hr = S_OK;
        for (UINT i = 0; i < count && SUCCEEDED(hr); i++) {
            UaStoryboard *storyboard = list[i];
            if (storyboard->status == UI_ANIMATION_STORYBOARD_SCHEDULED && storyboard->start_time <= step) {
                hr = UaStoryboardSetStatus(storyboard, UI_ANIMATION_STORYBOARD_PLAYING);
                for (UINT j = 0; j < manager->variable_count; j++)
                    UaVariableRefresh(manager->variables[j], step);
            }
            if (SUCCEEDED(hr) && storyboard->status == UI_ANIMATION_STORYBOARD_PLAYING) {
                storyboard->updated = TRUE;
                hr = UaStoryboardLoopEvents(storyboard, step);
            }
        }
        for (UINT i = 0; i < count && SUCCEEDED(hr); i++) {
            UaStoryboard *storyboard = list[i];
            if (storyboard->status != UI_ANIMATION_STORYBOARD_PLAYING &&
                storyboard->status != UI_ANIMATION_STORYBOARD_SCHEDULED)
                continue;
            if (all_trimmed(storyboard, step))
                hr = UaManagerFinishStoryboard(manager, storyboard, UI_ANIMATION_STORYBOARD_TRUNCATED);
            else if (storyboard->status == UI_ANIMATION_STORYBOARD_PLAYING && UaStoryboardEndTime(storyboard) <= step)
                hr = UaManagerFinishStoryboard(manager, storyboard, UI_ANIMATION_STORYBOARD_FINISHED);
        }
        for (UINT i = 0; i < count; i++)
            ((IUnknown *)list[i])->lpVtbl->Release((IUnknown *)list[i]);
        UaFree(list);
        if (FAILED(hr))
            return hr;
        hr = UaManagerProcessDeferred(manager);
        if (FAILED(hr))
            return hr;
        if (step >= target)
            return S_OK;
    }
    return S_OK;
}

static HRESULT update(UaManager *manager, double time, UI_ANIMATION_UPDATE_RESULT *result) {
    HRESULT hr = UaManagerCheck(manager, 0);
    if (FAILED(hr))
        return hr;
    if (result)
        *result = UI_ANIMATION_UPDATE_NO_CHANGE;
    if (time < 0.0)
        return E_INVALIDARG;
    if (manager->last_update != UA_NONE) {
        if (manager->last_update > time) {
            if (manager->last_update - time <= 86400.0)
                return UI_E_TIME_BEFORE_LAST_UPDATE;
            /* the clock of the caller started again: carry on from where the old one was. */
            manager->offset -= manager->last_update - time + 0.016;
        }
        if (manager->last_update == time)
            return S_OK;
    }
    manager->last_update = time;
    for (UINT i = 0; i < manager->variable_count; i++) {
        UaVariable *variable = manager->variables[i];
        for (UINT d = 0; d < variable->dimension; d++)
            variable->previous[d] = variable->value[d];
    }
    if (!manager->scheduled_count && !manager->changed)
        return S_OK;
    if (manager->run_state == 1) {
        if (manager->paused_at != UA_NONE && time > manager->paused_at)
            manager->offset += time - manager->paused_at;
        manager->run_state = 2;
        manager->paused_at = UA_NONE;
    } else if (manager->run_state == 0) {
        return S_OK;
    }
    ((IUnknown *)manager)->lpVtbl->AddRef((IUnknown *)manager);
    hr = update_core(manager, time - manager->offset);
    for (UINT i = 0; i < manager->scheduled_count && SUCCEEDED(hr); i++) {
        UaStoryboard *storyboard = manager->scheduled[i];
        if (storyboard->updated && storyboard->status == UI_ANIMATION_STORYBOARD_PLAYING)
            hr = UaStoryboardCallUpdated(storyboard);
        storyboard->updated = FALSE;
    }
    for (UINT i = 0; i < manager->variable_count && SUCCEEDED(hr); i++)
        hr = UaVariableCallHandlers(manager->variables[i]);
    if (SUCCEEDED(hr)) {
        for (UINT i = 0; i < manager->variable_count && result; i++) {
            UaVariable *variable = manager->variables[i];
            for (UINT d = 0; d < variable->dimension; d++)
                if (variable->value[d] != variable->previous[d])
                    *result = UI_ANIMATION_UPDATE_VARIABLES_CHANGED;
        }
        manager->changed = FALSE;
        if (!manager->scheduled_count)
            hr = UaManagerSetStatus(manager, UI_ANIMATION_MANAGER_IDLE);
    }
    ((IUnknown *)manager)->lpVtbl->Release((IUnknown *)manager);
    return hr;
}

/* ------------------------------------------------------------------ IUIAnimationManager */

static HRESULT STDMETHODCALLTYPE manager_query(IUnknown *iface, REFIID iid, void **out) {
    UaManager *manager = (UaManager *)iface;
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUIAnimationTimerUpdateHandler)) {
        *out = &manager->update_vtbl;
        InterlockedIncrement(&manager->refs);
        return S_OK;
    }
    return UaQuery(iface, manager->version == 2 ? &IID_IUIAnimationManager2 : &IID_IUIAnimationManager, iid, out);
}

static ULONG STDMETHODCALLTYPE manager_addref(IUnknown *iface) {
    return InterlockedIncrement(&((UaManager *)iface)->refs);
}

static void release_slot(IUnknown **slot) {
    if (*slot)
        (*slot)->lpVtbl->Release(*slot);
    *slot = nullptr;
}

static void drop_everything(UaManager *manager) {
    release_slot(&manager->event_handler);
    release_slot(&manager->cancel_comparison);
    release_slot(&manager->trim_comparison);
    release_slot(&manager->compress_comparison);
    release_slot(&manager->conclude_comparison);
    release_slot((IUnknown **)&manager->timer_client);
    while (manager->scheduled_count) {
        UaStoryboard *storyboard = manager->scheduled[manager->scheduled_count - 1];
        UaStoryboardUnschedule(storyboard);
        storyboard->status = UI_ANIMATION_STORYBOARD_READY;
        remove_scheduled(manager, storyboard);
    }
    while (manager->deferred_count) {
        UaStoryboard *storyboard = manager->deferred[--manager->deferred_count];
        storyboard->deferred_schedule = storyboard->deferred_abandon = FALSE;
        ((IUnknown *)storyboard)->lpVtbl->Release((IUnknown *)storyboard);
    }
}

static ULONG STDMETHODCALLTYPE manager_release(IUnknown *iface) {
    UaManager *manager = (UaManager *)iface;
    ULONG refs = InterlockedDecrement(&manager->refs);
    if (refs)
        return refs;
    manager->refs = 1;
    drop_everything(manager);
    UaFree(manager->variables);
    UaFree(manager->scheduled);
    UaFree(manager->deferred);
    UaFree(manager->all);
    UaFree(manager);
    UaObjectCountChange(-1);
    return 0;
}

static HRESULT create_variable(UaManager *manager, const double *value, UINT dimension, void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    HRESULT hr = UaManagerCheck(manager, 0);
    if (FAILED(hr))
        return hr;
    if (!dimension || dimension > UA_MAX_DIMENSION)
        return E_INVALIDARG;
    return UaVariableCreate(manager, value, dimension, out);
}

static HRESULT STDMETHODCALLTYPE manager_create_variable(IUIAnimationManager *iface, double value, void **out) {
    return create_variable((UaManager *)iface, &value, 1, out);
}

static HRESULT STDMETHODCALLTYPE manager2_create_vector_variable(IUIAnimationManager2 *iface, double *value,
                                                                 UINT dimension, IUIAnimationVariable2 **out) {
    if (!value)
        return E_POINTER;
    return create_variable((UaManager *)iface, value, dimension, (void **)out);
}

static HRESULT STDMETHODCALLTYPE manager_schedule_transition(IUIAnimationManager *iface, void *variable,
                                                             void *transition, double time) {
    UaManager *manager = (UaManager *)iface;
    if (!variable || !transition)
        return E_POINTER;
    HRESULT hr = UaManagerCheck(manager, 0);
    if (FAILED(hr))
        return hr;
    UaVariable *v = UaVariableFrom(variable, manager->version);
    UaTransition *t = UaTransitionFrom(transition, manager->version);
    if (!v || !t)
        return E_INVALIDARG;
    void *object;
    hr = UaStoryboardCreate(manager, &object);
    if (FAILED(hr))
        return hr;
    UaStoryboard *storyboard = object;
    hr = UaStoryboardAddTransition(storyboard, v, t);
    if (SUCCEEDED(hr))
        hr = UaManagerSchedule(manager, storyboard, time, nullptr);
    ((IUnknown *)storyboard)->lpVtbl->Release((IUnknown *)storyboard);
    return hr;
}

static HRESULT STDMETHODCALLTYPE manager_create_storyboard(IUIAnimationManager *iface, void **out) {
    UaManager *manager = (UaManager *)iface;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    HRESULT hr = UaManagerCheck(manager, UA_CALLBACK_STORYBOARD);
    if (FAILED(hr))
        return hr;
    return UaStoryboardCreate(manager, out);
}

static HRESULT STDMETHODCALLTYPE manager_finish_all(IUIAnimationManager *iface, double deadline) {
    UaManager *manager = (UaManager *)iface;
    HRESULT hr = UaManagerCheck(manager, 0);
    if (FAILED(hr))
        return hr;
    if (deadline < 0.0)
        return E_INVALIDARG;
    for (UINT i = 0; i < manager->scheduled_count; i++)
        UaStoryboardFinishIn(manager->scheduled[i], UaManagerTime(manager), deadline);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE manager_abandon_all(IUIAnimationManager *iface) {
    UaManager *manager = (UaManager *)iface;
    HRESULT hr = UaManagerCheck(manager, 0);
    if (FAILED(hr))
        return hr;
    while (manager->scheduled_count && SUCCEEDED(hr)) {
        UaStoryboard *storyboard = manager->scheduled[manager->scheduled_count - 1];
        hr = UaManagerFinishStoryboard(manager, storyboard,
                                       storyboard->status == UI_ANIMATION_STORYBOARD_PLAYING
                                           ? UI_ANIMATION_STORYBOARD_TRUNCATED
                                           : UI_ANIMATION_STORYBOARD_CANCELLED);
    }
    if (SUCCEEDED(hr))
        hr = UaManagerSetStatus(manager, UI_ANIMATION_MANAGER_IDLE);
    return hr;
}

static HRESULT STDMETHODCALLTYPE manager_update(IUIAnimationManager *iface, double time,
                                                UI_ANIMATION_UPDATE_RESULT *result) {
    return update((UaManager *)iface, time, result);
}

static HRESULT find_by_tag(UaManager *manager, IUnknown *object, UINT32 id, BOOL storyboards, void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    HRESULT hr = UaManagerCheck(manager, UA_CALLBACK_VARIABLE | UA_CALLBACK_STORYBOARD | UA_CALLBACK_UPDATED);
    if (FAILED(hr))
        return hr;
    IUnknown *found = nullptr;
    UINT count = storyboards ? manager->all_count : manager->variable_count;
    for (UINT i = 0; i < count; i++) {
        IUnknown *candidate;
        IUnknown *tag_object;
        UINT tag_id;
        if (storyboards) {
            UaStoryboard *storyboard = manager->all[i];
            candidate = (IUnknown *)storyboard, tag_object = storyboard->tag_object, tag_id = storyboard->tag_id;
        } else {
            UaVariable *variable = manager->variables[i];
            candidate = (IUnknown *)variable, tag_object = variable->tag_object, tag_id = variable->tag_id;
        }
        if (tag_object != object || tag_id != id || (!tag_object && !tag_id))
            continue;
        if (found)
            return UI_E_AMBIGUOUS_MATCH;
        found = candidate;
    }
    if (!found)
        return UI_E_VALUE_NOT_SET;
    found->lpVtbl->AddRef(found);
    *out = found;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE manager_variable_from_tag(IUIAnimationManager *iface, IUnknown *object, UINT32 id,
                                                           void **out) {
    return find_by_tag((UaManager *)iface, object, id, FALSE, out);
}

static HRESULT STDMETHODCALLTYPE manager_storyboard_from_tag(IUIAnimationManager *iface, IUnknown *object, UINT32 id,
                                                             void **out) {
    return find_by_tag((UaManager *)iface, object, id, TRUE, out);
}

static HRESULT STDMETHODCALLTYPE manager_get_status(IUIAnimationManager *iface, UI_ANIMATION_MANAGER_STATUS *status) {
    UaManager *manager = (UaManager *)iface;
    if (!status)
        return E_POINTER;
    *status = UI_ANIMATION_MANAGER_IDLE;
    HRESULT hr = UaManagerCheck(manager, 0);
    if (FAILED(hr))
        return hr;
    *status = manager->status;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE manager_set_mode(IUIAnimationManager *iface, UI_ANIMATION_MODE mode) {
    UaManager *manager = (UaManager *)iface;
    HRESULT hr = UaManagerCheck(manager, 0);
    if (FAILED(hr))
        return hr;
    if ((UINT)mode > UI_ANIMATION_MODE_ENABLED)
        return E_INVALIDARG;
    manager->mode = mode;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE manager_pause(IUIAnimationManager *iface) {
    UaManager *manager = (UaManager *)iface;
    HRESULT hr = UaManagerCheck(manager, 0);
    if (FAILED(hr))
        return hr;
    if (manager->run_state == 2)
        manager->paused_at = manager->last_update;
    manager->run_state = 0;
    return UaManagerSetStatus(manager, UI_ANIMATION_MANAGER_IDLE);
}

static HRESULT STDMETHODCALLTYPE manager_resume(IUIAnimationManager *iface) {
    UaManager *manager = (UaManager *)iface;
    HRESULT hr = UaManagerCheck(manager, 0);
    if (FAILED(hr))
        return hr;
    if (manager->run_state == 0)
        manager->run_state = 1;
    if (!manager->scheduled_count)
        return S_OK;
    return UaManagerSetStatus(manager, UI_ANIMATION_MANAGER_BUSY);
}

static HRESULT set_slot(UaManager *manager, IUnknown **slot, IUnknown *value) {
    HRESULT hr = UaManagerCheck(manager, 0);
    if (FAILED(hr))
        return hr;
    if (value)
        value->lpVtbl->AddRef(value);
    release_slot(slot);
    *slot = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE manager_set_event_handler(IUIAnimationManager *iface, IUnknown *handler) {
    UaManager *manager = (UaManager *)iface;
    return set_slot(manager, &manager->event_handler, handler);
}

static HRESULT STDMETHODCALLTYPE manager2_set_event_handler(IUIAnimationManager2 *iface, IUnknown *handler,
                                                            BOOL next_event) {
    UaManager *manager = (UaManager *)iface;
    return set_slot(manager, &manager->event_handler, handler);
}

static HRESULT STDMETHODCALLTYPE manager_set_cancel(IUIAnimationManager *iface, IUnknown *comparison) {
    UaManager *manager = (UaManager *)iface;
    return set_slot(manager, &manager->cancel_comparison, comparison);
}

static HRESULT STDMETHODCALLTYPE manager_set_trim(IUIAnimationManager *iface, IUnknown *comparison) {
    UaManager *manager = (UaManager *)iface;
    return set_slot(manager, &manager->trim_comparison, comparison);
}

static HRESULT STDMETHODCALLTYPE manager_set_compress(IUIAnimationManager *iface, IUnknown *comparison) {
    UaManager *manager = (UaManager *)iface;
    return set_slot(manager, &manager->compress_comparison, comparison);
}

static HRESULT STDMETHODCALLTYPE manager_set_conclude(IUIAnimationManager *iface, IUnknown *comparison) {
    UaManager *manager = (UaManager *)iface;
    return set_slot(manager, &manager->conclude_comparison, comparison);
}

static HRESULT STDMETHODCALLTYPE manager_set_default_delay(IUIAnimationManager *iface, double delay) {
    UaManager *manager = (UaManager *)iface;
    HRESULT hr = UaManagerCheck(manager, 0);
    if (FAILED(hr))
        return hr;
    if (delay == UI_ANIMATION_SECONDS_EVENTUALLY)
        delay = UA_FOREVER;
    else if (delay < 0.0)
        return E_INVALIDARG;
    manager->default_delay = delay;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE manager_shutdown(IUIAnimationManager *iface) {
    UaManager *manager = (UaManager *)iface;
    if (UaManagerWrongThread(manager))
        return UI_E_WRONG_THREAD;
    if (manager->shutdown)
        return UI_E_SHUTDOWN_CALLED;
    if (UaManagerInCallback(manager, 0))
        return UI_E_ILLEGAL_REENTRANCY;
    manager->shutdown = TRUE;
    drop_everything(manager);
    for (UINT i = 0; i < manager->variable_count; i++)
        UaVariableZombify(manager->variables[i]);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE manager2_estimate_next_event(IUIAnimationManager2 *iface, double *seconds) {
    UaManager *manager = (UaManager *)iface;
    if (!seconds)
        return E_POINTER;
    *seconds = UI_ANIMATION_SECONDS_INFINITE;
    HRESULT hr = UaManagerCheck(manager, 0);
    if (FAILED(hr))
        return hr;
    double now = UaManagerTime(manager), next = UA_FOREVER;
    for (UINT i = 0; i < manager->scheduled_count; i++) {
        UaStoryboard *storyboard = manager->scheduled[i];
        if (storyboard->status == UI_ANIMATION_STORYBOARD_PLAYING) {
            *seconds = 0.0;
            return S_OK;
        }
        if (storyboard->start_time < next)
            next = storyboard->start_time;
    }
    if (next < UA_FOREVER)
        *seconds = next > now ? next - now : 0.0;
    return S_OK;
}

static const IUIAnimationManagerVtbl g_manager_vtbl = {
    VTBL_FN(manager_query),
    VTBL_FN(manager_addref),
    VTBL_FN(manager_release),
    VTBL_FN(manager_create_variable),
    VTBL_FN(manager_schedule_transition),
    VTBL_FN(manager_create_storyboard),
    manager_finish_all,
    manager_abandon_all,
    manager_update,
    VTBL_FN(manager_variable_from_tag),
    VTBL_FN(manager_storyboard_from_tag),
    manager_get_status,
    manager_set_mode,
    manager_pause,
    manager_resume,
    VTBL_FN(manager_set_event_handler),
    VTBL_FN(manager_set_cancel),
    VTBL_FN(manager_set_trim),
    VTBL_FN(manager_set_compress),
    VTBL_FN(manager_set_conclude),
    manager_set_default_delay,
    manager_shutdown,
};

static const IUIAnimationManager2Vtbl g_manager2_vtbl = {
    VTBL_FN(manager_query),
    VTBL_FN(manager_addref),
    VTBL_FN(manager_release),
    manager2_create_vector_variable,
    VTBL_FN(manager_create_variable),
    VTBL_FN(manager_schedule_transition),
    VTBL_FN(manager_create_storyboard),
    VTBL_FN(manager_finish_all),
    VTBL_FN(manager_abandon_all),
    VTBL_FN(manager_update),
    VTBL_FN(manager_variable_from_tag),
    VTBL_FN(manager_storyboard_from_tag),
    manager2_estimate_next_event,
    VTBL_FN(manager_get_status),
    VTBL_FN(manager_set_mode),
    VTBL_FN(manager_pause),
    VTBL_FN(manager_resume),
    VTBL_FN(manager2_set_event_handler),
    VTBL_FN(manager_set_cancel),
    VTBL_FN(manager_set_trim),
    VTBL_FN(manager_set_compress),
    VTBL_FN(manager_set_conclude),
    VTBL_FN(manager_set_default_delay),
    VTBL_FN(manager_shutdown),
};

/* ------------------------------------------------------------------ IUIAnimationTimerUpdateHandler */

static HRESULT STDMETHODCALLTYPE update_query(IUIAnimationTimerUpdateHandler *iface, REFIID iid, void **out) {
    return manager_query((IUnknown *)from_update(iface), iid, out);
}

static ULONG STDMETHODCALLTYPE update_addref(IUIAnimationTimerUpdateHandler *iface) {
    return manager_addref((IUnknown *)from_update(iface));
}

static ULONG STDMETHODCALLTYPE update_release(IUIAnimationTimerUpdateHandler *iface) {
    return manager_release((IUnknown *)from_update(iface));
}

static HRESULT STDMETHODCALLTYPE update_on_update(IUIAnimationTimerUpdateHandler *iface, double time,
                                                  UI_ANIMATION_UPDATE_RESULT *result) {
    return update(from_update(iface), time, result);
}

static HRESULT STDMETHODCALLTYPE update_set_client(IUIAnimationTimerUpdateHandler *iface,
                                                   IUIAnimationTimerClientEventHandler *handler) {
    UaManager *manager = from_update(iface);
    HRESULT hr = UaManagerCheck(manager, 0);
    if (FAILED(hr))
        return hr;
    if (manager->timer_client)
        return UI_E_TIMER_CLIENT_ALREADY_CONNECTED;
    if (handler)
        handler->lpVtbl->AddRef(handler);
    manager->timer_client = handler;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE update_clear_client(IUIAnimationTimerUpdateHandler *iface) {
    UaManager *manager = from_update(iface);
    HRESULT hr = UaManagerCheck(manager, 0);
    if (FAILED(hr))
        return hr;
    release_slot((IUnknown **)&manager->timer_client);
    return S_OK;
}

static const IUIAnimationTimerUpdateHandlerVtbl g_update_vtbl = {
    update_query, update_addref, update_release, update_on_update, update_set_client, update_clear_client,
};

HRESULT UaManagerCreate(int version, REFIID iid, void **out) {
    UaManager *manager = UaAlloc(sizeof(*manager));
    if (!manager)
        return E_OUTOFMEMORY;
    manager->lpVtbl = version == 2 ? (const void *)&g_manager2_vtbl : (const void *)&g_manager_vtbl;
    manager->update_vtbl = &g_update_vtbl;
    manager->refs = 1;
    manager->version = version;
    manager->thread = GetCurrentThreadId();
    manager->status = UI_ANIMATION_MANAGER_IDLE;
    manager->mode = UI_ANIMATION_MODE_SYSTEM_DEFAULT;
    manager->run_state = 2;
    manager->paused_at = UA_NONE;
    manager->last_update = UA_NONE;
    manager->now = UA_NONE;
    UaObjectCountChange(1);
    HRESULT hr = manager_query((IUnknown *)manager, iid, out);
    manager_release((IUnknown *)manager);
    return hr;
}
