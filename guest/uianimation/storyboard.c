#include "uianimation_private.h"

static const IUIAnimationStoryboardVtbl g_storyboard_vtbl;
static const IUIAnimationStoryboard2Vtbl g_storyboard2_vtbl;

UaStoryboard *UaStoryboardFrom(const void *iface, int version) {
    if (!iface)
        return nullptr;
    const void *vtbl = *(const void *const *)iface;
    if (vtbl != (version == 2 ? (const void *)&g_storyboard2_vtbl : (const void *)&g_storyboard_vtbl))
        return nullptr;
    return (UaStoryboard *)iface;
}

void *UaStoryboardIface(UaStoryboard *storyboard) { return storyboard; }

static BOOL grow(void **array, UINT *capacity, UINT count, SIZE_T size) {
    if (count < *capacity)
        return TRUE;
    UINT next = *capacity ? *capacity * 2 : 4;
    void *grown = UaRealloc(*array, next * size);
    if (!grown)
        return FALSE;
    *array = grown;
    *capacity = next;
    return TRUE;
}

/* ------------------------------------------------------------------ time */

static double loop_count(const UaLoop *loop) {
    if (loop->count >= 0.0)
        return loop->count;
    return loop->concluded >= 0.0 ? loop->concluded : UA_FOREVER;
}

/* the local time of a point in the storyboard's own time, with the loops before it played. */
double UaStoryboardExpand(UaStoryboard *storyboard, double item_time) {
    double shift = 0.0;
    for (UINT i = 0; i < storyboard->loop_count; i++) {
        UaLoop *loop = &storyboard->loops[i];
        double a = storyboard->keyframes[loop->start].time, b = storyboard->keyframes[loop->end].time;
        if (item_time < b || b <= a)
            break;
        double n = loop_count(loop);
        if (n >= UA_FOREVER)
            return UA_FOREVER;
        shift += (n - 1.0) * (b - a);
    }
    return item_time + shift;
}

/* the point of the storyboard's own time that plays at a local time, iteration says which repetition of loop. */
static double unexpand(UaStoryboard *storyboard, double local, BOOL *reversed, UINT loop_index, UINT *iteration) {
    double shift = 0.0;
    *reversed = FALSE;
    if (iteration)
        *iteration = 0;
    for (UINT i = 0; i < storyboard->loop_count; i++) {
        UaLoop *loop = &storyboard->loops[i];
        double a = storyboard->keyframes[loop->start].time, b = storyboard->keyframes[loop->end].time;
        double length = b - a;
        double begin = a + shift;
        if (local < begin || length <= 0.0)
            break;
        double n = loop_count(loop);
        if (n >= UA_FOREVER || local < begin + n * length) {
            double k = floor((local - begin) / length);
            double u = local - begin - k * length;
            if (loop->mode == UI_ANIMATION_REPEAT_MODE_ALTERNATE && ((LONGLONG)k & 1)) {
                u = length - u;
                *reversed = TRUE;
            }
            if (iteration && i == loop_index)
                *iteration = (UINT)k;
            return a + u;
        }
        if (iteration && i == loop_index)
            *iteration = n > 0.0 ? (UINT)(n - 1.0) : 0;
        shift += (n - 1.0) * length;
    }
    return local - shift;
}

double UaStoryboardLocal(UaStoryboard *storyboard, double time) {
    if (storyboard->finish_time != UA_NONE && time > storyboard->finish_time)
        return storyboard->finish_local + (time - storyboard->finish_time) * storyboard->finish_scale;
    return storyboard->skip + time - storyboard->start_time;
}

static double global_time(UaStoryboard *storyboard, double local) {
    if (local >= UA_FOREVER)
        return UA_FOREVER;
    if (storyboard->finish_time != UA_NONE && local > storyboard->finish_local)
        return storyboard->finish_time + (local - storyboard->finish_local) / storyboard->finish_scale;
    return storyboard->start_time + local - storyboard->skip;
}

double UaStoryboardEndTime(UaStoryboard *storyboard) {
    return global_time(storyboard, UaStoryboardExpand(storyboard, storyboard->end_item));
}

static void recompute_times(UaStoryboard *storyboard) {
    for (UINT i = 0; i < storyboard->track_count; i++) {
        UaTrack *track = storyboard->tracks[i];
        track->start_time = global_time(storyboard, UaStoryboardExpand(storyboard, track->begin));
        if (track->start_time < storyboard->start_time)
            track->start_time = storyboard->start_time;
        track->end_time = global_time(storyboard, UaStoryboardExpand(storyboard, track->end));
    }
}

/* ------------------------------------------------------------------ tracks */

static void item_at(UaItem *item, double offset, double *value, double *velocity) {
    UINT dimension = item->transition->dimension;
    if (offset >= item->finish - item->begin) {
        for (UINT d = 0; d < dimension; d++) {
            value[d] = item->final[d];
            velocity[d] = 0.0;
        }
        return;
    }
    if (FAILED(UaInterpValue(item->transition, offset, value)))
        for (UINT d = 0; d < dimension; d++)
            value[d] = item->final[d];
    if (FAILED(UaInterpVelocity(item->transition, offset, velocity)))
        for (UINT d = 0; d < dimension; d++)
            velocity[d] = 0.0;
}

void UaTrackAt(UaTrack *track, double time, double *value, double *velocity, BOOL *controls) {
    UaStoryboard *storyboard = track->storyboard;
    UINT dimension = track->variable->dimension;
    *controls = FALSE;
    if (time < track->start_time)
        return;
    *controls = TRUE;
    BOOL reversed;
    double x = unexpand(storyboard, UaStoryboardLocal(storyboard, time), &reversed, 0, nullptr);
    UaItem *found = nullptr;
    for (UINT i = 0; i < track->item_count; i++)
        if (track->items[i]->begin <= x)
            found = track->items[i];
    if (!found) {
        for (UINT d = 0; d < dimension; d++) {
            value[d] = track->initial[d];
            velocity[d] = 0.0;
        }
        return;
    }
    item_at(found, x - found->begin, value, velocity);
    if (reversed)
        for (UINT d = 0; d < dimension; d++)
            velocity[d] = -velocity[d];
}

void UaTrackFinal(UaTrack *track, double *value) {
    UINT dimension = track->variable->dimension;
    UaItem *last = nullptr;
    for (UINT i = 0; i < track->item_count; i++)
        if (!last || track->items[i]->finish >= last->finish)
            last = track->items[i];
    for (UINT d = 0; d < dimension; d++)
        value[d] = last ? last->final[d] : track->initial[d];
}

static UaTrack *find_track(UaStoryboard *storyboard, UaVariable *variable, BOOL create) {
    for (UINT i = 0; i < storyboard->track_count; i++)
        if (storyboard->tracks[i]->variable == variable)
            return storyboard->tracks[i];
    if (!create)
        return nullptr;
    if (!grow((void **)&storyboard->tracks, &storyboard->track_capacity, storyboard->track_count,
              sizeof(*storyboard->tracks)))
        return nullptr;
    UaTrack *track = UaAlloc(sizeof(*track));
    if (!track)
        return nullptr;
    track->storyboard = storyboard;
    track->variable = variable;
    track->trim = UA_NONE;
    ((IUnknown *)variable)->lpVtbl->AddRef((IUnknown *)variable);
    storyboard->tracks[storyboard->track_count++] = track;
    return track;
}

/* ------------------------------------------------------------------ resolving the timeline */

static int compare_begin(const UaItem *a, const UaItem *b) { return a->begin < b->begin ? -1 : a->begin > b->begin; }

static void sort_items(UaItem **items, UINT count) {
    for (UINT i = 1; i < count; i++) {
        UaItem *item = items[i];
        UINT j = i;
        while (j > 0 && compare_begin(items[j - 1], item) > 0) {
            items[j] = items[j - 1];
            j--;
        }
        items[j] = item;
    }
}

static HRESULT resolve_item(UaStoryboard *storyboard, UaItem *item, double start) {
    UaTransition *t = item->transition;
    UaTrack *track = find_track(storyboard, item->variable, FALSE);
    UINT dimension = t->dimension;
    double begin = storyboard->keyframes[item->start].time;
    double value[UA_MAX_DIMENSION], velocity[UA_MAX_DIMENSION];
    /* it starts from where the latest earlier transition of the variable in this storyboard left it. */
    UaItem *previous = nullptr;
    for (UINT i = 0; i < track->item_count; i++) {
        UaItem *other = track->items[i];
        if (other == item || !other->resolved || other->begin > begin)
            continue;
        if (!previous || other->begin >= previous->begin)
            previous = other;
    }
    if (previous) {
        item_at(previous, begin - previous->begin, value, velocity);
        if (begin == previous->finish && previous->finish > previous->begin)
            UaInterpVelocity(previous->transition, previous->finish - previous->begin, velocity);
    } else {
        UaVariableAt(item->variable, start + UaStoryboardExpand(storyboard, begin) - storyboard->skip, track, value,
                     velocity);
    }
    for (UINT d = 0; d < dimension; d++) {
        if (t->value_set)
            value[d] = t->value[d];
        if (t->velocity_set)
            velocity[d] = t->velocity[d];
    }
    HRESULT hr;
    if (item->end >= 0) {
        double length = storyboard->keyframes[item->end].time - begin;
        if (length < 0.0)
            return UI_E_START_KEYFRAME_AFTER_END;
        hr = UaInterpSetDuration(t, length);
        if (FAILED(hr))
            return hr;
    }
    hr = UaInterpInit(t, value, velocity);
    if (FAILED(hr))
        return hr;
    double duration;
    hr = UaInterpDuration(t, &duration);
    if (FAILED(hr))
        return hr;
    if (!(duration >= 0.0))
        duration = 0.0;
    hr = UaInterpFinal(t, item->final);
    if (FAILED(hr))
        return hr;
    item->begin = begin;
    item->finish = begin + duration;
    item->resolved = TRUE;
    return S_OK;
}

HRESULT UaStoryboardResolve(UaStoryboard *storyboard, double start) {
    storyboard->start_time = start;
    storyboard->finish_time = UA_NONE;
    for (UINT i = 0; i < storyboard->keyframe_count; i++)
        storyboard->keyframes[i].resolved = storyboard->keyframes[i].kind == 0;
    storyboard->keyframes[0].time = 0.0;
    for (UINT i = 0; i < storyboard->item_count; i++)
        storyboard->items[i]->resolved = FALSE;
    for (UINT i = 0; i < storyboard->loop_count; i++)
        storyboard->loops[i].concluded = -1.0;
    BOOL progress = TRUE;
    while (progress) {
        progress = FALSE;
        for (UINT i = 0; i < storyboard->keyframe_count; i++) {
            UaKeyframe *keyframe = &storyboard->keyframes[i];
            if (keyframe->resolved)
                continue;
            if (keyframe->kind == 1 && storyboard->keyframes[keyframe->base].resolved) {
                keyframe->time = storyboard->keyframes[keyframe->base].time + keyframe->offset;
                keyframe->resolved = progress = TRUE;
            } else if (keyframe->kind == 2 && keyframe->after->item->resolved) {
                keyframe->time = keyframe->after->item->finish;
                keyframe->resolved = progress = TRUE;
            }
        }
        for (UINT i = 0; i < storyboard->item_count; i++) {
            UaItem *item = storyboard->items[i];
            if (item->resolved || !storyboard->keyframes[item->start].resolved)
                continue;
            if (item->end >= 0 && !storyboard->keyframes[item->end].resolved)
                continue;
            /* an earlier transition of the same variable comes first, so this one starts where that left it. */
            double begin = storyboard->keyframes[item->start].time;
            BOOL wait = FALSE;
            for (UINT j = 0; j < storyboard->item_count && !wait; j++) {
                UaItem *other = storyboard->items[j];
                if (other != item && other->variable == item->variable && !other->resolved &&
                    storyboard->keyframes[other->start].resolved && storyboard->keyframes[other->start].time < begin)
                    wait = TRUE;
            }
            if (wait)
                continue;
            HRESULT hr = resolve_item(storyboard, item, start);
            if (FAILED(hr))
                return hr;
            progress = TRUE;
        }
    }
    for (UINT i = 0; i < storyboard->item_count; i++)
        if (!storyboard->items[i]->resolved)
            return UI_E_END_KEYFRAME_NOT_DETERMINED;
    for (UINT i = 0; i < storyboard->keyframe_count; i++)
        if (!storyboard->keyframes[i].resolved)
            return UI_E_END_KEYFRAME_NOT_DETERMINED;

    /* loops in order, none may overlap another. */
    for (UINT i = 1; i < storyboard->loop_count; i++) {
        UaLoop loop = storyboard->loops[i];
        UINT j = i;
        while (j > 0 &&
               storyboard->keyframes[storyboard->loops[j - 1].start].time > storyboard->keyframes[loop.start].time) {
            storyboard->loops[j] = storyboard->loops[j - 1];
            j--;
        }
        storyboard->loops[j] = loop;
    }
    for (UINT i = 0; i < storyboard->loop_count; i++) {
        UaLoop *loop = &storyboard->loops[i];
        if (storyboard->keyframes[loop->start].time > storyboard->keyframes[loop->end].time)
            return UI_E_START_KEYFRAME_AFTER_END;
        if (i > 0 && storyboard->keyframes[storyboard->loops[i - 1].end].time > storyboard->keyframes[loop->start].time)
            return UI_E_LOOPS_OVERLAP;
        loop->iteration = 0;
    }

    double end = 0.0;
    for (UINT i = 0; i < storyboard->item_count; i++)
        if (storyboard->items[i]->finish > end)
            end = storyboard->items[i]->finish;
    for (UINT i = 0; i < storyboard->loop_count; i++)
        if (storyboard->keyframes[storyboard->loops[i].end].time > end)
            end = storyboard->keyframes[storyboard->loops[i].end].time;
    storyboard->end_item = end;
    for (UINT i = 0; i < storyboard->track_count; i++) {
        UaTrack *track = storyboard->tracks[i];
        sort_items(track->items, track->item_count);
        track->begin = track->item_count ? track->items[0]->begin : 0.0;
        track->end = track->held ? end : 0.0;
        for (UINT j = 0; j < track->item_count; j++)
            if (track->items[j]->finish > track->end)
                track->end = track->items[j]->finish;
        if (track->held)
            track->begin = 0.0;
        double velocity[UA_MAX_DIMENSION];
        UaVariableAt(track->variable, start + UaStoryboardExpand(storyboard, track->begin) - storyboard->skip, track,
                     track->initial, velocity);
        track->trim = UA_NONE;
        track->trimmed_by = nullptr;
    }
    recompute_times(storyboard);
    return S_OK;
}

/* ------------------------------------------------------------------ status and events */

HRESULT UaStoryboardSetStatus(UaStoryboard *storyboard, UI_ANIMATION_STORYBOARD_STATUS status) {
    UI_ANIMATION_STORYBOARD_STATUS previous = storyboard->status;
    storyboard->previous_status = previous;
    storyboard->status = status;
    if (!storyboard->event_handler || previous == status)
        return S_OK;
    UaManager *manager = storyboard->manager;
    UINT saved;
    IUnknown *handler = storyboard->event_handler;
    handler->lpVtbl->AddRef(handler);
    ((IUnknown *)storyboard)->lpVtbl->AddRef((IUnknown *)storyboard);
    UaManagerEnter(manager, UA_CALLBACK_STORYBOARD, &saved);
    HRESULT hr;
    if (storyboard->version == 2)
        hr = ((IUIAnimationStoryboardEventHandler2 *)handler)
                 ->lpVtbl->OnStoryboardStatusChanged((IUIAnimationStoryboardEventHandler2 *)handler,
                                                     (IUIAnimationStoryboard2 *)storyboard, status, previous);
    else
        hr = ((IUIAnimationStoryboardEventHandler *)handler)
                 ->lpVtbl->OnStoryboardStatusChanged((IUIAnimationStoryboardEventHandler *)handler,
                                                     (IUIAnimationStoryboard *)storyboard, status, previous);
    UaManagerLeave(manager, saved);
    ((IUnknown *)storyboard)->lpVtbl->Release((IUnknown *)storyboard);
    handler->lpVtbl->Release(handler);
    return hr;
}

HRESULT UaStoryboardCallUpdated(UaStoryboard *storyboard) {
    if (!storyboard->event_handler)
        return S_OK;
    UaManager *manager = storyboard->manager;
    UINT saved;
    IUnknown *handler = storyboard->event_handler;
    handler->lpVtbl->AddRef(handler);
    UaManagerEnter(manager, UA_CALLBACK_UPDATED, &saved);
    HRESULT hr;
    if (storyboard->version == 2)
        hr = ((IUIAnimationStoryboardEventHandler2 *)handler)
                 ->lpVtbl->OnStoryboardUpdated((IUIAnimationStoryboardEventHandler2 *)handler,
                                               (IUIAnimationStoryboard2 *)storyboard);
    else
        hr = ((IUIAnimationStoryboardEventHandler *)handler)
                 ->lpVtbl->OnStoryboardUpdated((IUIAnimationStoryboardEventHandler *)handler,
                                               (IUIAnimationStoryboard *)storyboard);
    UaManagerLeave(manager, saved);
    handler->lpVtbl->Release(handler);
    return hr;
}

HRESULT UaStoryboardLoopEvents(UaStoryboard *storyboard, double time) {
    double local = UaStoryboardLocal(storyboard, time);
    for (UINT i = 0; i < storyboard->loop_count; i++) {
        UaLoop *loop = &storyboard->loops[i];
        BOOL reversed;
        UINT iteration;
        unexpand(storyboard, local, &reversed, i, &iteration);
        if (iteration == loop->iteration)
            continue;
        UINT old = loop->iteration;
        loop->iteration = iteration;
        if (!loop->handler)
            continue;
        UINT saved;
        UaManagerEnter(storyboard->manager, UA_CALLBACK_STORYBOARD, &saved);
        HRESULT hr = loop->handler->lpVtbl->OnLoopIterationChanged(loop->handler, (IUIAnimationStoryboard2 *)storyboard,
                                                                   loop->id, iteration, old);
        UaManagerLeave(storyboard->manager, saved);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

/* takes the storyboard off its variables; what it leaves them at becomes their value at rest. */
void UaStoryboardUnschedule(UaStoryboard *storyboard) {
    for (UINT i = 0; i < storyboard->track_count; i++) {
        UaTrack *track = storyboard->tracks[i];
        UaVariable *variable = track->variable;
        BOOL attached = FALSE;
        for (UINT j = 0; j < variable->track_count; j++)
            attached |= variable->tracks[j] == track;
        if (!attached)
            continue;
        if (track->trim == UA_NONE) {
            double value[UA_MAX_DIMENSION], velocity[UA_MAX_DIMENSION];
            if (track->end_time <= storyboard->manager->now)
                UaTrackFinal(track, value);
            else
                UaVariableAt(variable, storyboard->manager->now, nullptr, value, velocity);
            for (UINT d = 0; d < variable->dimension; d++)
                variable->rest[d] = value[d];
        }
        UaVariableRemoveTrack(variable, track);
        if (variable->current == storyboard)
            variable->current = nullptr;
    }
}

HRESULT UaStoryboardConcludeLoops(UaStoryboard *storyboard, double time) {
    double local = UaStoryboardLocal(storyboard, time);
    for (UINT i = 0; i < storyboard->loop_count; i++) {
        UaLoop *loop = &storyboard->loops[i];
        if (loop->count >= 0.0 || loop->concluded >= 0.0)
            continue;
        BOOL reversed;
        UINT iteration;
        double a = storyboard->keyframes[loop->start].time;
        double begin = UaStoryboardExpand(storyboard, a);
        unexpand(storyboard, local, &reversed, i, &iteration);
        double played = local < begin ? 1.0 : (double)iteration + 1.0;
        if (loop->mode == UI_ANIMATION_REPEAT_MODE_ALTERNATE) {
            BOOL even = ((LONGLONG)played & 1) == 0;
            /* ending at the start keyframe takes an even count of repetitions, at the end one an odd count. */
            if (loop->count == UI_ANIMATION_REPEAT_INDEFINITELY_CONCLUDE_AT_START ? !even : even)
                played += 1.0;
        }
        loop->concluded = played;
    }
    recompute_times(storyboard);
    return S_OK;
}

void UaStoryboardFinishIn(UaStoryboard *storyboard, double now, double deadline) {
    UaStoryboardConcludeLoops(storyboard, now);
    double base = now > storyboard->start_time ? now : storyboard->start_time;
    double local = UaStoryboardLocal(storyboard, base);
    double remaining = UaStoryboardExpand(storyboard, storyboard->end_item) - local;
    if (remaining <= deadline)
        return;
    storyboard->finish_time = base;
    storyboard->finish_local = local;
    storyboard->finish_scale = deadline > 0.0 ? remaining / deadline : 1e300;
    recompute_times(storyboard);
}

/* ------------------------------------------------------------------ IUIAnimationStoryboard */

static HRESULT check(UaStoryboard *storyboard, UINT allowed) {
    UaManager *manager = storyboard->manager;
    if (UaManagerWrongThread(manager))
        return UI_E_WRONG_THREAD;
    if (manager->shutdown)
        return UI_E_SHUTDOWN_CALLED;
    if (UaManagerInCallback(manager, allowed))
        return UI_E_ILLEGAL_REENTRANCY;
    return S_OK;
}

static HRESULT check_building(UaStoryboard *storyboard) {
    HRESULT hr = check(storyboard, UA_CALLBACK_STORYBOARD);
    if (FAILED(hr))
        return hr;
    if (storyboard->status != UI_ANIMATION_STORYBOARD_BUILDING)
        return UI_E_OBJECT_SEALED;
    return S_OK;
}

static int keyframe_index(UaStoryboard *storyboard, UI_ANIMATION_KEYFRAME cookie) {
    if (cookie == UI_ANIMATION_KEYFRAME_STORYBOARD_START)
        return 0;
    UINT_PTR index = (UINT_PTR)cookie;
    if (index == 0 || index >= storyboard->keyframe_count)
        return -1;
    return (int)index;
}

static HRESULT add_keyframe(UaStoryboard *storyboard, int kind, int base, double offset, UaTransition *after,
                            UI_ANIMATION_KEYFRAME *cookie) {
    if (!grow((void **)&storyboard->keyframes, &storyboard->keyframe_capacity, storyboard->keyframe_count,
              sizeof(*storyboard->keyframes)))
        return E_OUTOFMEMORY;
    UaKeyframe *keyframe = &storyboard->keyframes[storyboard->keyframe_count];
    keyframe->kind = kind;
    keyframe->base = base;
    keyframe->offset = offset;
    keyframe->after = after;
    keyframe->resolved = FALSE;
    keyframe->time = 0.0;
    if (cookie)
        *cookie = (UI_ANIMATION_KEYFRAME)(UINT_PTR)storyboard->keyframe_count;
    storyboard->keyframe_count++;
    return S_OK;
}

/* the time of a keyframe when only offsets lead to it, or UA_NONE. */
static double static_time(UaStoryboard *storyboard, int index) {
    double time = 0.0;
    while (index > 0) {
        UaKeyframe *keyframe = &storyboard->keyframes[index];
        if (keyframe->kind != 1)
            return UA_NONE;
        time += keyframe->offset;
        index = keyframe->base;
    }
    return time;
}

static HRESULT add_item(UaStoryboard *storyboard, const void *variable_iface, const void *transition_iface, int start,
                        int end) {
    UaVariable *variable = UaVariableFrom(variable_iface, storyboard->version);
    UaTransition *transition = UaTransitionFrom(transition_iface, storyboard->version);
    if (!variable_iface || !transition_iface)
        return E_POINTER;
    if (!variable || !transition)
        return E_INVALIDARG;
    if (variable->manager != storyboard->manager)
        return UI_E_DIFFERENT_OWNER;
    if (transition->storyboard)
        return UI_E_TRANSITION_ALREADY_USED;
    if (transition->dimension != variable->dimension)
        return UI_E_INVALID_DIMENSION;
    if (start < 0 || (end == -2))
        return E_INVALIDARG;
    if (end >= 0) {
        double a = static_time(storyboard, start), b = static_time(storyboard, end);
        if (a != UA_NONE && b != UA_NONE && a > b)
            return UI_E_START_KEYFRAME_AFTER_END;
    }
    UaTrack *track = find_track(storyboard, variable, TRUE);
    if (!track)
        return E_OUTOFMEMORY;
    if (!grow((void **)&storyboard->items, &storyboard->item_capacity, storyboard->item_count,
              sizeof(*storyboard->items)) ||
        !grow((void **)&track->items, &track->item_capacity, track->item_count, sizeof(*track->items)))
        return E_OUTOFMEMORY;
    UaItem *item = UaAlloc(sizeof(*item));
    if (!item)
        return E_OUTOFMEMORY;
    item->transition = transition;
    item->variable = variable;
    item->start = start;
    item->end = end;
    ((IUnknown *)transition)->lpVtbl->AddRef((IUnknown *)transition);
    transition->storyboard = storyboard;
    transition->item = item;
    storyboard->items[storyboard->item_count++] = item;
    track->items[track->item_count++] = item;
    return S_OK;
}

HRESULT UaStoryboardAddTransition(UaStoryboard *storyboard, UaVariable *variable, UaTransition *transition) {
    UaTrack *track = variable ? find_track(storyboard, variable, FALSE) : nullptr;
    int start = 0;
    if (track && track->item_count) {
        /* after the last transition added for the variable. */
        HRESULT hr = add_keyframe(storyboard, 2, 0, 0.0, track->items[track->item_count - 1]->transition, nullptr);
        if (FAILED(hr))
            return hr;
        start = storyboard->keyframe_count - 1;
    }
    return add_item(storyboard, variable, transition, start, -1);
}

static HRESULT STDMETHODCALLTYPE storyboard_query(IUnknown *iface, REFIID iid, void **out) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    return UaQuery(iface, storyboard->version == 2 ? &IID_IUIAnimationStoryboard2 : &IID_IUIAnimationStoryboard, iid,
                   out);
}

static ULONG STDMETHODCALLTYPE storyboard_addref(IUnknown *iface) {
    return InterlockedIncrement(&((UaStoryboard *)iface)->refs);
}

static ULONG STDMETHODCALLTYPE storyboard_release(IUnknown *iface) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    ULONG refs = InterlockedDecrement(&storyboard->refs);
    if (refs)
        return refs;
    UaManager *manager = storyboard->manager;
    for (UINT i = 0; i < storyboard->item_count; i++) {
        UaItem *item = storyboard->items[i];
        item->transition->storyboard = nullptr;
        item->transition->item = nullptr;
        ((IUnknown *)item->transition)->lpVtbl->Release((IUnknown *)item->transition);
        UaFree(item);
    }
    for (UINT i = 0; i < storyboard->track_count; i++) {
        UaTrack *track = storyboard->tracks[i];
        UaVariableRemoveTrack(track->variable, track);
        ((IUnknown *)track->variable)->lpVtbl->Release((IUnknown *)track->variable);
        UaFree(track->items);
        UaFree(track);
    }
    for (UINT i = 0; i < storyboard->loop_count; i++)
        if (storyboard->loops[i].handler)
            storyboard->loops[i].handler->lpVtbl->Release(storyboard->loops[i].handler);
    if (storyboard->tag_object)
        storyboard->tag_object->lpVtbl->Release(storyboard->tag_object);
    if (storyboard->event_handler)
        storyboard->event_handler->lpVtbl->Release(storyboard->event_handler);
    for (UINT i = 0; i < manager->all_count; i++)
        if (manager->all[i] == storyboard) {
            manager->all[i] = manager->all[--manager->all_count];
            break;
        }
    UaFree(storyboard->items);
    UaFree(storyboard->tracks);
    UaFree(storyboard->keyframes);
    UaFree(storyboard->loops);
    UaFree(storyboard);
    ((IUnknown *)manager)->lpVtbl->Release((IUnknown *)manager);
    UaObjectCountChange(-1);
    return 0;
}

static HRESULT STDMETHODCALLTYPE storyboard_add_transition(IUIAnimationStoryboard *iface, void *variable,
                                                           void *transition) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    HRESULT hr = check_building(storyboard);
    if (FAILED(hr))
        return hr;
    if (!variable || !transition)
        return E_POINTER;
    UaVariable *v = UaVariableFrom(variable, storyboard->version);
    if (!v || !UaTransitionFrom(transition, storyboard->version))
        return E_INVALIDARG;
    return UaStoryboardAddTransition(storyboard, v, transition);
}

static HRESULT STDMETHODCALLTYPE storyboard_add_keyframe_at_offset(IUIAnimationStoryboard *iface,
                                                                   UI_ANIMATION_KEYFRAME existing, double offset,
                                                                   UI_ANIMATION_KEYFRAME *keyframe) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    if (!keyframe)
        return E_POINTER;
    *keyframe = nullptr;
    HRESULT hr = check_building(storyboard);
    if (FAILED(hr))
        return hr;
    int base = keyframe_index(storyboard, existing);
    if (base < 0 || offset < 0.0)
        return E_INVALIDARG;
    return add_keyframe(storyboard, 1, base, offset, nullptr, keyframe);
}

static HRESULT STDMETHODCALLTYPE storyboard_add_keyframe_after_transition(IUIAnimationStoryboard *iface,
                                                                          void *transition,
                                                                          UI_ANIMATION_KEYFRAME *keyframe) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    if (!keyframe)
        return E_POINTER;
    *keyframe = nullptr;
    HRESULT hr = check_building(storyboard);
    if (FAILED(hr))
        return hr;
    if (!transition)
        return E_POINTER;
    UaTransition *t = UaTransitionFrom(transition, storyboard->version);
    if (!t)
        return E_INVALIDARG;
    if (t->storyboard != storyboard)
        return UI_E_TRANSITION_NOT_IN_STORYBOARD;
    return add_keyframe(storyboard, 2, 0, 0.0, t, keyframe);
}

static HRESULT STDMETHODCALLTYPE storyboard_add_transition_at_keyframe(IUIAnimationStoryboard *iface, void *variable,
                                                                       void *transition, UI_ANIMATION_KEYFRAME start) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    HRESULT hr = check_building(storyboard);
    if (FAILED(hr))
        return hr;
    int index = keyframe_index(storyboard, start);
    if (index < 0)
        return E_INVALIDARG;
    return add_item(storyboard, variable, transition, index, -1);
}

static HRESULT STDMETHODCALLTYPE storyboard_add_transition_between(IUIAnimationStoryboard *iface, void *variable,
                                                                   void *transition, UI_ANIMATION_KEYFRAME start,
                                                                   UI_ANIMATION_KEYFRAME end) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    HRESULT hr = check_building(storyboard);
    if (FAILED(hr))
        return hr;
    int a = keyframe_index(storyboard, start), b = keyframe_index(storyboard, end);
    if (a < 0 || b < 0)
        return E_INVALIDARG;
    return add_item(storyboard, variable, transition, a, b);
}

static HRESULT repeat(UaStoryboard *storyboard, UI_ANIMATION_KEYFRAME start, UI_ANIMATION_KEYFRAME end, double count,
                      UI_ANIMATION_REPEAT_MODE mode, IUIAnimationLoopIterationChangeHandler2 *handler, UINT_PTR id) {
    HRESULT hr = check_building(storyboard);
    if (FAILED(hr))
        return hr;
    int a = keyframe_index(storyboard, start), b = keyframe_index(storyboard, end);
    if (a < 0 || b < 0 || (UINT)mode > UI_ANIMATION_REPEAT_MODE_ALTERNATE)
        return E_INVALIDARG;
    if (count < (storyboard->version == 2 ? -2.0 : -1.0))
        return E_INVALIDARG;
    double ta = static_time(storyboard, a), tb = static_time(storyboard, b);
    if (ta != UA_NONE && tb != UA_NONE) {
        if (ta >= tb)
            return UI_E_START_KEYFRAME_AFTER_END;
        for (UINT i = 0; i < storyboard->loop_count; i++) {
            double la = static_time(storyboard, storyboard->loops[i].start);
            double lb = static_time(storyboard, storyboard->loops[i].end);
            if (la != UA_NONE && lb != UA_NONE && la < tb && ta < lb)
                return UI_E_LOOPS_OVERLAP;
        }
    }
    if (!grow((void **)&storyboard->loops, &storyboard->loop_capacity, storyboard->loop_count,
              sizeof(*storyboard->loops)))
        return E_OUTOFMEMORY;
    UaLoop *loop = &storyboard->loops[storyboard->loop_count++];
    loop->start = a;
    loop->end = b;
    loop->count = count;
    loop->mode = mode;
    loop->handler = handler;
    if (handler)
        handler->lpVtbl->AddRef(handler);
    loop->id = id;
    loop->concluded = -1.0;
    loop->iteration = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE storyboard_repeat(IUIAnimationStoryboard *iface, UI_ANIMATION_KEYFRAME start,
                                                   UI_ANIMATION_KEYFRAME end, int count) {
    return repeat((UaStoryboard *)iface, start, end, (double)count, UI_ANIMATION_REPEAT_MODE_NORMAL, nullptr, 0);
}

static HRESULT STDMETHODCALLTYPE storyboard2_repeat(IUIAnimationStoryboard2 *iface, UI_ANIMATION_KEYFRAME start,
                                                    UI_ANIMATION_KEYFRAME end, double count,
                                                    UI_ANIMATION_REPEAT_MODE mode,
                                                    IUIAnimationLoopIterationChangeHandler2 *handler, UINT_PTR id,
                                                    BOOL next_event) {
    return repeat((UaStoryboard *)iface, start, end, count, mode, handler, id);
}

static HRESULT STDMETHODCALLTYPE storyboard_hold_variable(IUIAnimationStoryboard *iface, void *variable) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    HRESULT hr = check_building(storyboard);
    if (FAILED(hr))
        return hr;
    if (!variable)
        return E_POINTER;
    UaVariable *v = UaVariableFrom(variable, storyboard->version);
    if (!v)
        return E_INVALIDARG;
    if (v->manager != storyboard->manager)
        return UI_E_DIFFERENT_OWNER;
    UaTrack *track = find_track(storyboard, v, TRUE);
    if (!track)
        return E_OUTOFMEMORY;
    track->held = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE storyboard_set_delay(IUIAnimationStoryboard *iface, double delay) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    HRESULT hr = check_building(storyboard);
    if (FAILED(hr))
        return hr;
    if (delay == UI_ANIMATION_SECONDS_EVENTUALLY)
        delay = UA_FOREVER;
    else if (delay < 0.0)
        return E_INVALIDARG;
    storyboard->delay = delay;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE storyboard2_set_skip(IUIAnimationStoryboard2 *iface, double skip) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    HRESULT hr = check_building(storyboard);
    if (FAILED(hr))
        return hr;
    if (skip < 0.0)
        return E_INVALIDARG;
    storyboard->skip = skip;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE storyboard_schedule(IUIAnimationStoryboard *iface, double time,
                                                     UI_ANIMATION_SCHEDULING_RESULT *result) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    if (result)
        *result = UI_ANIMATION_SCHEDULING_UNEXPECTED_FAILURE;
    HRESULT hr = check(storyboard, UA_CALLBACK_STORYBOARD);
    if (FAILED(hr))
        return hr;
    UaManager *manager = storyboard->manager;
    if (storyboard->status != UI_ANIMATION_STORYBOARD_BUILDING && storyboard->status != UI_ANIMATION_STORYBOARD_READY) {
        if (result)
            *result = UI_ANIMATION_SCHEDULING_ALREADY_SCHEDULED;
        return S_OK;
    }
    if (manager->callbacks) {
        /* from a storyboard event: it is scheduled once the event returns. */
        if (!storyboard->deferred_schedule) {
            storyboard->deferred_schedule = TRUE;
            storyboard->deferred_time = time;
            if (grow((void **)&manager->deferred, &manager->deferred_capacity, manager->deferred_count,
                     sizeof(*manager->deferred))) {
                ((IUnknown *)storyboard)->lpVtbl->AddRef((IUnknown *)storyboard);
                manager->deferred[manager->deferred_count++] = storyboard;
            }
            if (result)
                *result = UI_ANIMATION_SCHEDULING_DEFERRED;
            return S_OK;
        }
        if (result)
            *result = UI_ANIMATION_SCHEDULING_ALREADY_SCHEDULED;
        return S_OK;
    }
    hr = UaManagerSchedule(manager, storyboard, time, result);
    if (FAILED(hr)) {
        if (result)
            *result = UI_ANIMATION_SCHEDULING_UNEXPECTED_FAILURE;
        return hr;
    }
    return UaManagerProcessDeferred(manager);
}

static HRESULT STDMETHODCALLTYPE storyboard_conclude(IUIAnimationStoryboard *iface) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    HRESULT hr = check(storyboard, UA_CALLBACK_STORYBOARD);
    if (FAILED(hr))
        return hr;
    if (storyboard->status != UI_ANIMATION_STORYBOARD_SCHEDULED &&
        storyboard->status != UI_ANIMATION_STORYBOARD_PLAYING)
        return UI_E_STORYBOARD_NOT_PLAYING;
    return UaStoryboardConcludeLoops(storyboard, UaManagerTime(storyboard->manager));
}

static HRESULT STDMETHODCALLTYPE storyboard_finish(IUIAnimationStoryboard *iface, double deadline) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    HRESULT hr = check(storyboard, UA_CALLBACK_STORYBOARD);
    if (FAILED(hr))
        return hr;
    if (deadline < 0.0)
        return E_INVALIDARG;
    if (storyboard->status != UI_ANIMATION_STORYBOARD_SCHEDULED &&
        storyboard->status != UI_ANIMATION_STORYBOARD_PLAYING)
        return UI_E_STORYBOARD_NOT_PLAYING;
    UaStoryboardFinishIn(storyboard, UaManagerTime(storyboard->manager), deadline);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE storyboard_abandon(IUIAnimationStoryboard *iface) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    HRESULT hr = check(storyboard, UA_CALLBACK_STORYBOARD);
    if (FAILED(hr))
        return hr;
    UaManager *manager = storyboard->manager;
    if (storyboard->status != UI_ANIMATION_STORYBOARD_SCHEDULED &&
        storyboard->status != UI_ANIMATION_STORYBOARD_PLAYING)
        return S_OK;
    if (manager->callbacks) {
        if (!storyboard->deferred_abandon) {
            storyboard->deferred_abandon = TRUE;
            if (grow((void **)&manager->deferred, &manager->deferred_capacity, manager->deferred_count,
                     sizeof(*manager->deferred))) {
                ((IUnknown *)storyboard)->lpVtbl->AddRef((IUnknown *)storyboard);
                manager->deferred[manager->deferred_count++] = storyboard;
            }
        }
        return S_OK;
    }
    hr = UaManagerFinishStoryboard(manager, storyboard,
                                   storyboard->status == UI_ANIMATION_STORYBOARD_PLAYING
                                       ? UI_ANIMATION_STORYBOARD_TRUNCATED
                                       : UI_ANIMATION_STORYBOARD_CANCELLED);
    if (SUCCEEDED(hr) && !manager->scheduled_count)
        hr = UaManagerSetStatus(manager, UI_ANIMATION_MANAGER_IDLE);
    if (FAILED(hr))
        return hr;
    return UaManagerProcessDeferred(manager);
}

static HRESULT STDMETHODCALLTYPE storyboard_set_tag(IUIAnimationStoryboard *iface, IUnknown *object, UINT32 id) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    HRESULT hr = check(storyboard, UA_CALLBACK_STORYBOARD);
    if (FAILED(hr))
        return hr;
    if (object)
        object->lpVtbl->AddRef(object);
    if (storyboard->tag_object)
        storyboard->tag_object->lpVtbl->Release(storyboard->tag_object);
    storyboard->tag_object = object;
    storyboard->tag_id = id;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE storyboard_get_tag(IUIAnimationStoryboard *iface, IUnknown **object, UINT32 *id) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    if (object)
        *object = nullptr;
    if (id)
        *id = 0;
    HRESULT hr = check(storyboard, UA_CALLBACK_STORYBOARD | UA_CALLBACK_VARIABLE | UA_CALLBACK_UPDATED);
    if (FAILED(hr))
        return hr;
    if (!storyboard->tag_object && !storyboard->tag_id)
        return UI_E_VALUE_NOT_SET;
    if (object) {
        *object = storyboard->tag_object;
        if (*object)
            (*object)->lpVtbl->AddRef(*object);
    }
    if (id)
        *id = storyboard->tag_id;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE storyboard_get_status(IUIAnimationStoryboard *iface,
                                                       UI_ANIMATION_STORYBOARD_STATUS *status) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    if (!status)
        return E_POINTER;
    *status = UI_ANIMATION_STORYBOARD_BUILDING;
    HRESULT hr = check(storyboard, 0);
    if (FAILED(hr))
        return hr;
    *status = storyboard->status;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE storyboard_get_elapsed(IUIAnimationStoryboard *iface, double *elapsed) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    if (!elapsed)
        return E_POINTER;
    *elapsed = 0.0;
    HRESULT hr = check(storyboard, UA_CALLBACK_STORYBOARD | UA_CALLBACK_VARIABLE | UA_CALLBACK_UPDATED);
    if (FAILED(hr))
        return hr;
    if (storyboard->status != UI_ANIMATION_STORYBOARD_PLAYING)
        return UI_E_STORYBOARD_NOT_PLAYING;
    double local = UaStoryboardLocal(storyboard, UaManagerTime(storyboard->manager));
    *elapsed = local > 0.0 ? local : 0.0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE storyboard_set_event_handler(IUIAnimationStoryboard *iface, IUnknown *handler) {
    UaStoryboard *storyboard = (UaStoryboard *)iface;
    HRESULT hr = check(storyboard, UA_CALLBACK_STORYBOARD);
    if (FAILED(hr))
        return hr;
    if (handler)
        handler->lpVtbl->AddRef(handler);
    if (storyboard->event_handler)
        storyboard->event_handler->lpVtbl->Release(storyboard->event_handler);
    storyboard->event_handler = handler;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE storyboard2_set_event_handler(IUIAnimationStoryboard2 *iface, IUnknown *handler,
                                                               BOOL status_next, BOOL update_next) {
    return storyboard_set_event_handler((IUIAnimationStoryboard *)iface, handler);
}

static const IUIAnimationStoryboardVtbl g_storyboard_vtbl = {
    VTBL_FN(storyboard_query),
    VTBL_FN(storyboard_addref),
    VTBL_FN(storyboard_release),
    VTBL_FN(storyboard_add_transition),
    storyboard_add_keyframe_at_offset,
    VTBL_FN(storyboard_add_keyframe_after_transition),
    VTBL_FN(storyboard_add_transition_at_keyframe),
    VTBL_FN(storyboard_add_transition_between),
    storyboard_repeat,
    VTBL_FN(storyboard_hold_variable),
    storyboard_set_delay,
    storyboard_schedule,
    storyboard_conclude,
    storyboard_finish,
    storyboard_abandon,
    storyboard_set_tag,
    storyboard_get_tag,
    storyboard_get_status,
    storyboard_get_elapsed,
    VTBL_FN(storyboard_set_event_handler),
};

static const IUIAnimationStoryboard2Vtbl g_storyboard2_vtbl = {
    VTBL_FN(storyboard_query),
    VTBL_FN(storyboard_addref),
    VTBL_FN(storyboard_release),
    VTBL_FN(storyboard_add_transition),
    VTBL_FN(storyboard_add_keyframe_at_offset),
    VTBL_FN(storyboard_add_keyframe_after_transition),
    VTBL_FN(storyboard_add_transition_at_keyframe),
    VTBL_FN(storyboard_add_transition_between),
    storyboard2_repeat,
    VTBL_FN(storyboard_hold_variable),
    VTBL_FN(storyboard_set_delay),
    storyboard2_set_skip,
    VTBL_FN(storyboard_schedule),
    VTBL_FN(storyboard_conclude),
    VTBL_FN(storyboard_finish),
    VTBL_FN(storyboard_abandon),
    VTBL_FN(storyboard_set_tag),
    VTBL_FN(storyboard_get_tag),
    VTBL_FN(storyboard_get_status),
    VTBL_FN(storyboard_get_elapsed),
    VTBL_FN(storyboard2_set_event_handler),
};

HRESULT UaStoryboardCreate(UaManager *manager, void **out) {
    if (!grow((void **)&manager->all, &manager->all_capacity, manager->all_count, sizeof(*manager->all)))
        return E_OUTOFMEMORY;
    UaStoryboard *storyboard = UaAlloc(sizeof(*storyboard));
    if (!storyboard)
        return E_OUTOFMEMORY;
    storyboard->lpVtbl = manager->version == 2 ? (const void *)&g_storyboard2_vtbl : (const void *)&g_storyboard_vtbl;
    storyboard->refs = 1;
    storyboard->version = manager->version;
    storyboard->manager = manager;
    ((IUnknown *)manager)->lpVtbl->AddRef((IUnknown *)manager);
    storyboard->status = UI_ANIMATION_STORYBOARD_BUILDING;
    storyboard->delay = UA_NONE;
    storyboard->start_time = UA_NONE;
    storyboard->finish_time = UA_NONE;
    if (FAILED(add_keyframe(storyboard, 0, 0, 0.0, nullptr, nullptr))) {
        ((IUnknown *)manager)->lpVtbl->Release((IUnknown *)manager);
        UaFree(storyboard);
        return E_OUTOFMEMORY;
    }
    manager->all[manager->all_count++] = storyboard;
    UaObjectCountChange(1);
    *out = storyboard;
    return S_OK;
}
