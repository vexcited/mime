/*
 * xdg-activation, for what sdl does not do: asking the compositor for a window's attention without taking the focus, and
 * raising a window with the token a notification gave, so that a click on a notification brings the program's window to
 * the front as the click of a person on that program does. the protocol has no generated header here; its requests and
 * its one event are described from the specification, as the viewporter is in ui.c.
 */
#include "ui_internal.h"

#include <stdio.h>
#include <string.h>
#include <wayland-client.h>

static const struct wl_interface token_interface;
static const struct wl_interface *no_types[] = {nullptr, nullptr, nullptr, nullptr};
static const struct wl_interface *types_get[] = {&token_interface};
static const struct wl_interface *types_activate[] = {nullptr, &wl_surface_interface};
static const struct wl_message activation_requests[] = {
    {"destroy", "", no_types},
    {"get_activation_token", "n", types_get},
    {"activate", "so", types_activate},
};
static const struct wl_interface activation_interface = {"xdg_activation_v1", 1, 3, activation_requests, 0, nullptr};
static const struct wl_interface *types_serial[] = {nullptr, &wl_seat_interface};
static const struct wl_interface *types_surface[] = {&wl_surface_interface};
static const struct wl_message token_requests[] = {
    {"set_serial", "uo", types_serial},
    {"set_app_id", "s", no_types},
    {"set_surface", "o", types_surface},
    {"commit", "", no_types},
    {"destroy", "", no_types},
};
static const struct wl_message token_events[] = {{"done", "s", no_types}};
static const struct wl_interface token_interface = {"xdg_activation_token_v1", 1, 5, token_requests, 1, token_events};

static struct wl_proxy *g_activation;
static struct wl_event_queue *g_queue;
static bool g_tried;

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                            uint32_t version)
{
    if (!strcmp(interface, activation_interface.name))
        g_activation = wl_registry_bind(registry, name, &activation_interface, 1);
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {registry_global, registry_remove};

static bool bind(struct wl_display *display)
{
    if (g_tried)
        return g_activation != nullptr;
    g_tried = true;
    g_queue = wl_display_create_queue(display);
    struct wl_display *wrapper = wl_proxy_create_wrapper(display);
    wl_proxy_set_queue((struct wl_proxy *)wrapper, g_queue);
    struct wl_registry *registry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);
    wl_registry_add_listener(registry, &registry_listener, nullptr);
    wl_display_roundtrip_queue(display, g_queue);
    return g_activation != nullptr;
}

typedef struct Token {
    char value[128];
    bool done;
} Token;

static void token_done(void *data, struct wl_proxy *proxy, const char *token)
{
    Token *t = data;
    snprintf(t->value, sizeof(t->value), "%s", token);
    t->done = true;
}

/* the compositor makes a token of the request, which has no serial and no surface: it cannot be a user's action. */
static bool make_token(struct wl_display *display, const char *app_id, Token *token)
{
    struct wl_proxy *proxy = wl_proxy_marshal_flags(g_activation, 1, &token_interface,
                                                    wl_proxy_get_version(g_activation), 0, nullptr);
    if (!proxy)
        return false;
    static const void *listener[1] = {(void *)token_done};
    wl_proxy_add_listener(proxy, (void (**)(void))listener, token);
    wl_proxy_marshal_flags(proxy, 3, nullptr, wl_proxy_get_version(proxy), 0);
    wl_display_roundtrip_queue(display, g_queue);
    wl_proxy_marshal_flags(proxy, 4, nullptr, wl_proxy_get_version(proxy), WL_MARSHAL_FLAG_DESTROY);
    return token->done;
}

static void activate(struct wl_display *display, struct wl_surface *surface, const char *token)
{
    wl_proxy_marshal_flags(g_activation, 2, nullptr, wl_proxy_get_version(g_activation), 0, token, surface);
    wl_display_flush(display);
}

/* the window asks for attention, which the compositor shows without moving the focus. true when it could be asked. */
bool mime_activation_attention(struct wl_display *display, struct wl_surface *surface, const char *app_id)
{
    if (!display || !surface || !bind(display))
        return false;
    Token token = {};
    if (!make_token(display, app_id, &token))
        return false;
    activate(display, surface, token.value);
    return true;
}

/* the window is raised with a token the desktop gave, a notification's. */
bool mime_activation_activate(struct wl_display *display, struct wl_surface *surface, const char *token)
{
    if (!display || !surface || !token || !*token || !bind(display))
        return false;
    activate(display, surface, token);
    return true;
}
