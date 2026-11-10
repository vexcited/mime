#include "ui_internal.h"

#include <SDL3/SDL.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define HELPER_VARIABLE "MIME_DIALOG_HELPER"
#define CLOSED 3 /* the helper's exit code for a dialog closed without a button. */
#define PROGRESS_MAX 8

typedef enum Tool { TOOL_SDL, TOOL_KDIALOG, TOOL_ZENITY } Tool;

typedef struct Progress {
    uint32_t id;
    pid_t pid;
    int input; /* zenity reads the percentage and the text from it. */
} Progress;

typedef struct Asked {
    uint32_t id;
    pid_t pid; /* the helper showing the question, ended when the guest takes it away. */
} Asked;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Progress g_progress[PROGRESS_MAX];
static Asked g_asked[PROGRESS_MAX];

/* SDL shows a message box without being brought up, so the client that starts the machine can use it. */
void mime_ui_alert(const char *text)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Mime", text, nullptr);
}

static bool on_path(const char *name, char *out, size_t size)
{
    const char *path = getenv("PATH");
    while (path && *path) {
        const char *end = strchr(path, ':');
        size_t length = end ? (size_t)(end - path) : strlen(path);
        snprintf(out, size, "%.*s/%s", (int)length, path, name);
        if (length && access(out, X_OK) == 0)
            return true;
        path = end ? end + 1 : nullptr;
    }
    return false;
}

/* the desktop's own dialogs: kdialog on plasma, zenity elsewhere, either when only one is there. */
static Tool pick_tool(char *path, size_t size)
{
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    bool plasma = desktop && strstr(desktop, "KDE");
    if (plasma && on_path("kdialog", path, size))
        return TOOL_KDIALOG;
    if (on_path("zenity", path, size))
        return TOOL_ZENITY;
    if (on_path("kdialog", path, size))
        return TOOL_KDIALOG;
    return TOOL_SDL;
}

/* zenity reads pango markup, so the text is escaped for it. */
static void put_text(char *out, size_t size, const char *text, bool markup)
{
    size_t n = strlen(out);
    for (const char *c = text; *c && n + 6 < size; c++) {
        const char *with = !markup ? nullptr : *c == '&' ? "&amp;" : *c == '<' ? "&lt;" : *c == '>' ? "&gt;" : nullptr;
        if (with) {
            memcpy(out + n, with, strlen(with));
            n += strlen(with);
        } else {
            out[n++] = *c;
        }
    }
    out[n] = 0;
}

static void dialog_text(const MimeShellDialog *d, char *out, size_t size, bool markup)
{
    char instruction[1024], content[4096];
    mime_utf16_to_utf8(instruction, sizeof(instruction), d->instruction, SIZE_MAX);
    mime_utf16_to_utf8(content, sizeof(content), d->content, SIZE_MAX);
    out[0] = 0;
    put_text(out, size, instruction, markup);
    if (instruction[0] && content[0])
        put_text(out, size, "\n\n", markup);
    put_text(out, size, content, markup);
}

/* runs a helper and waits for it; its exit code, -1 when it could not start. stdout is kept when asked for. */
static int run_helper(uint32_t id, char *const argv[], char *const envp[], char *out, size_t out_size)
{
    int pipe_fds[2] = {-1, -1};
    if (out && pipe2(pipe_fds, O_CLOEXEC) != 0)
        return -1;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (out)
        posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    pid_t pid;
    int r = posix_spawn(&pid, argv[0], &actions, nullptr, argv, envp ? envp : environ);
    posix_spawn_file_actions_destroy(&actions);
    if (out)
        close(pipe_fds[1]);
    if (r != 0) {
        if (out)
            close(pipe_fds[0]);
        return -1;
    }
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < PROGRESS_MAX; i++)
        if (!g_asked[i].id) {
            g_asked[i] = (Asked){id, pid};
            break;
        }
    pthread_mutex_unlock(&g_lock);
    if (out) {
        size_t n = 0;
        ssize_t got;
        while ((got = read(pipe_fds[0], out + n, out_size - 1 - n)) > 0 || (got < 0 && errno == EINTR))
            if (got > 0 && (n += (size_t)got) >= out_size - 1)
                break;
        out[n] = 0;
        close(pipe_fds[0]);
    }
    int status = 0;
    pid_t done;
    while ((done = waitpid(pid, &status, 0)) < 0 && errno == EINTR)
        ;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < PROGRESS_MAX; i++)
        if (g_asked[i].pid == pid)
            g_asked[i] = (Asked){};
    pthread_mutex_unlock(&g_lock);
    return done == pid && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* the button pressed, -1 when the dialog was closed without one or could not be shown. */
static int show(const MimeShellDialog *d)
{
    char tool[512], title[256], text[6144], labels[MIME_DIALOG_BUTTONS][256] = {}, out[256] = "";
    Tool kind = pick_tool(tool, sizeof(tool));
    mime_utf16_to_utf8(title, sizeof(title), d->title, SIZE_MAX);
    dialog_text(d, text, sizeof(text), kind == TOOL_ZENITY);
    uint32_t buttons = d->kind == MIME_DIALOG_ASK ? d->button_count : 0;
    for (uint32_t i = 0; i < buttons; i++)
        mime_utf16_to_utf8(labels[i], sizeof(labels[i]), d->buttons[i], SIZE_MAX);
    char *argv[16];
    int n = 0;
    char opt[MIME_DIALOG_BUTTONS][320];
    if (kind == TOOL_KDIALOG) {
        argv[n++] = tool;
        argv[n++] = "--title";
        argv[n++] = title;
        if (buttons >= 2) {
            argv[n++] = "--yes-label";
            argv[n++] = labels[0];
            argv[n++] = "--no-label";
            argv[n++] = labels[1];
            if (buttons == 3) {
                argv[n++] = "--cancel-label";
                argv[n++] = labels[2];
            }
            argv[n++] = buttons == 3 ? "--yesnocancel" : "--yesno";
        } else {
            argv[n++] = d->kind == MIME_DIALOG_ERROR ? "--error" : "--msgbox";
        }
        argv[n++] = text;
        argv[n] = nullptr;
        int code = run_helper(d->id, argv, nullptr, nullptr, 0);
        /* yes is 0, no 1 and cancel 2, which closing the window also gives. */
        if (buttons >= 2)
            return code == 0 ? 0 : code == 1 ? 1 : code == 2 && buttons == 3 ? 2 : -1;
        return code == 0 ? 0 : -1;
    }
    if (kind == TOOL_ZENITY) {
        argv[n++] = tool;
        argv[n++] = buttons >= 2 ? "--question" : d->kind == MIME_DIALOG_ERROR ? "--error" : "--info";
        snprintf(opt[0], sizeof(opt[0]), "--title=%s", title);
        argv[n++] = opt[0];
        argv[n++] = "--text";
        argv[n++] = text;
        argv[n++] = "--width=480";
        if (buttons >= 2) {
            snprintf(opt[1], sizeof(opt[1]), "--ok-label=%s", labels[0]);
            snprintf(opt[2], sizeof(opt[2]), "--cancel-label=%s", labels[1]);
            argv[n++] = opt[1];
            argv[n++] = opt[2];
            if (buttons == 3) {
                argv[n++] = "--extra-button";
                argv[n++] = labels[2];
            }
        }
        argv[n] = nullptr;
        int code = run_helper(d->id, argv, nullptr, out, sizeof(out));
        /* an extra button prints its label and ends like cancel, as closing the window does. */
        size_t length = strlen(out);
        while (length && out[length - 1] == '\n')
            out[--length] = 0;
        if (buttons == 3 && code == 1 && !strcmp(out, labels[2]))
            return 2;
        if (buttons >= 2)
            return code == 0 ? 0 : code == 1 ? 1 : -1;
        return code == 0 ? 0 : -1;
    }
    /* SDL's box, in a process of its own: it would hold the thread that draws the machine's windows. */
    char kind_text[8];
    snprintf(kind_text, sizeof(kind_text), "%u", d->kind);
    /* the running binary itself, which still starts after an update has replaced or deleted its file. */
    argv[n++] = "/proc/self/exe";
    argv[n++] = kind_text;
    argv[n++] = title;
    argv[n++] = text;
    for (uint32_t i = 0; i < buttons; i++)
        argv[n++] = labels[i];
    argv[n] = nullptr;
    /* the helper is told by its environment, built here: setenv would race the threads reading it. */
    size_t count = 0;
    while (environ[count])
        count++;
    char **envp = malloc((count + 2) * sizeof(char *));
    if (!envp)
        return -1;
    memcpy(envp, environ, count * sizeof(char *));
    envp[count] = HELPER_VARIABLE "=1";
    envp[count + 1] = nullptr;
    int code = run_helper(d->id, argv, envp, nullptr, 0);
    free(envp);
    return code >= 0 && code < CLOSED && (uint32_t)code < (buttons ? buttons : 1) ? code : -1;
}

int mime_ui_dialog_helper(int argc, char **argv)
{
    if (!getenv(HELPER_VARIABLE) || argc < 4)
        return -1;
    unsetenv(HELPER_VARIABLE);
    unsigned kind = (unsigned)atoi(argv[1]);
    int count = argc - 4 > MIME_DIALOG_BUTTONS ? MIME_DIALOG_BUTTONS : argc - 4;
    SDL_MessageBoxButtonData buttons[MIME_DIALOG_BUTTONS];
    for (int i = 0; i < count; i++)
        buttons[i] = (SDL_MessageBoxButtonData){i == 0 ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT
                                                       : i == count - 1 ? SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT
                                                                        : 0,
                                                i, argv[4 + i]};
    if (!count)
        buttons[count++] = (SDL_MessageBoxButtonData){
            SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "OK"};
    SDL_MessageBoxData box = {kind == MIME_DIALOG_ERROR ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION, nullptr,
                              argv[2], argv[3], count, buttons, nullptr};
    int pressed = -1;
    if (!SDL_ShowMessageBox(&box, &pressed) || pressed < 0)
        return CLOSED;
    return pressed;
}

typedef struct Shown {
    MimeShellDialog dialog;
} Shown;

static void *dialog_thread(void *arg)
{
    Shown *s = arg;
    mime_set_thread_name("mime-dialog");
    int answer = show(&s->dialog);
    MimeInputEvent ev = {.type = MIME_INPUT_SHELL, .pad = s->dialog.id, .flags = MIME_SHELL_EVENT_DIALOG_ANSWERED,
                         .x = answer};
    mime_ui_emit(&ev);
    free(s);
    return nullptr;
}

/* zenity's progress window, for desktops without plasma's job server. */
static void zenity_progress(const MimeShellDialog *d, const char *text)
{
    pthread_mutex_lock(&g_lock);
    Progress *p = nullptr;
    for (int i = 0; i < PROGRESS_MAX && !p; i++)
        if (g_progress[i].id == d->id)
            p = &g_progress[i];
    if (d->kind == MIME_DIALOG_PROGRESS && !p) {
        char tool[512];
        for (int i = 0; i < PROGRESS_MAX && !p; i++)
            if (!g_progress[i].id)
                p = &g_progress[i];
        int fds[2];
        if (!p || pick_tool(tool, sizeof(tool)) != TOOL_ZENITY || pipe2(fds, O_CLOEXEC) != 0) {
            pthread_mutex_unlock(&g_lock);
            return;
        }
        char title[256], option[320];
        mime_utf16_to_utf8(title, sizeof(title), d->title, SIZE_MAX);
        snprintf(option, sizeof(option), "--title=%s", title);
        char *argv[] = {tool, "--progress", option, "--auto-close", "--no-cancel", "--width=480", nullptr};
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, fds[0], STDIN_FILENO);
        pid_t pid;
        int r = posix_spawn(&pid, tool, &actions, nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&actions);
        close(fds[0]);
        if (r != 0) {
            close(fds[1]);
            pthread_mutex_unlock(&g_lock);
            return;
        }
        *p = (Progress){d->id, pid, fds[1]};
    }
    if (!p) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    if (d->kind == MIME_DIALOG_END) {
        close(p->input);
        pid_t pid = p->pid;
        *p = (Progress){};
        pthread_mutex_unlock(&g_lock);
        while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR)
            ;
        return;
    }
    char line[4096];
    int length = snprintf(line, sizeof(line), "%s# ", d->permille >= 0 ? "" : "pulsate\n");
    if (d->permille >= 0)
        length = snprintf(line, sizeof(line), "%d\n# ", d->permille / 10);
    for (const char *c = text; *c && length < (int)sizeof(line) - 2; c++)
        line[length++] = *c == '\n' ? ' ' : *c;
    line[length++] = '\n';
    if (write(p->input, line, (size_t)length) < 0)
        ;
    pthread_mutex_unlock(&g_lock);
}

uint64_t mime_ui_dialog(const MimeShellDialog *d)
{
    if (!getenv("WAYLAND_DISPLAY") && !getenv("DISPLAY"))
        return MIME_HC_STATUS_UNSUPPORTED;
    if (d->kind == MIME_DIALOG_END) {
        /* a question the guest takes away: its helper ends, and the answer is that it was closed. */
        pthread_mutex_lock(&g_lock);
        for (int i = 0; i < PROGRESS_MAX; i++)
            if (g_asked[i].id == d->id)
                kill(g_asked[i].pid, SIGTERM);
        pthread_mutex_unlock(&g_lock);
    }
    if (d->kind == MIME_DIALOG_PROGRESS || d->kind == MIME_DIALOG_UPDATE || d->kind == MIME_DIALOG_END) {
        char text[4096];
        dialog_text(d, text, sizeof(text), false);
        /* the instruction is the view's title, the content its line. */
        char content[4096];
        mime_utf16_to_utf8(content, sizeof(content), d->content, SIZE_MAX);
        if (!mime_shell_job(d, content))
            zenity_progress(d, text);
        return MIME_HC_STATUS_OK;
    }
    if (d->kind < MIME_DIALOG_ASK || d->kind > MIME_DIALOG_INFO)
        return MIME_HC_STATUS_INVALID;
    Shown *s = malloc(sizeof(*s));
    if (!s)
        return MIME_HC_STATUS_NO_MEMORY;
    s->dialog = *d;
    pthread_t t;
    if (pthread_create(&t, nullptr, dialog_thread, s) != 0) {
        free(s);
        return MIME_HC_STATUS_NO_MEMORY;
    }
    pthread_detach(t);
    return MIME_HC_STATUS_OK;
}
