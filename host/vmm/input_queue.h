#ifndef MIME_INPUT_QUEUE_H
#define MIME_INPUT_QUEUE_H

#include "../../include/mime/abi.h"

#include <stdbool.h>

static inline bool mime_input_is_motion(const MimeInputEvent *event)
{
    return event->type == MIME_INPUT_MOUSE_MOVE || event->type == MIME_INPUT_WINDOW_MOVED;
}

/* the caller holds the queue lock. capacity includes the unused ring slot. */
static inline bool mime_input_queue_push(MimeInputEvent *events, uint32_t capacity, uint32_t *head,
                                        uint32_t *tail, const MimeInputEvent *event)
{
    if (*head != *tail && mime_input_is_motion(event)) {
        uint32_t last = (*head + capacity - 1) % capacity;
        MimeInputEvent *previous = &events[last];
        if (previous->type == event->type && previous->window_id == event->window_id &&
            previous->modifiers == event->modifiers && previous->flags == event->flags &&
            previous->button == event->button) {
            *previous = *event;
            return true;
        }
    }

    uint32_t next = (*head + 1) % capacity;
    if (next == *tail) {
        uint32_t discard = *tail;
        while (discard != *head && !mime_input_is_motion(&events[discard]))
            discard = (discard + 1) % capacity;
        if (discard != *head) {
            /* remove motion without reordering clicks, releases, or window commands. */
            uint32_t after = (discard + 1) % capacity;
            while (after != *head) {
                events[discard] = events[after];
                discard = after;
                after = (after + 1) % capacity;
            }
            *head = discard;
            next = (*head + 1) % capacity;
        } else if (mime_input_is_motion(event)) {
            return false;
        } else {
            /* discrete input alone can still exhaust the bounded queue. */
            *tail = (*tail + 1) % capacity;
        }
    }
    events[*head] = *event;
    *head = next;
    return true;
}

#endif
