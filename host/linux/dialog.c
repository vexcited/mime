#include "ui_internal.h"

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

#define XMESSAGE_FIRST 10 /* xmessage ends with this code for its first button, and with 1 when it is closed. */
#define PROGRESS_MAX 8
#define HELPER_KILLED (-3)
#define NOT_SHOWN (-2)

static const char g_scene[] = {
#embed "dialog.qml"
};

typedef enum Tool { TOOL_NONE, TOOL_KDIALOG, TOOL_ZENITY, TOOL_XMESSAGE } Tool;

typedef struct Progress {
    uint32_t id;
    pid_t pid;
    int input; /* zenity reads the percentage and the text from it, -1 for the scene, which reads its file. */
    MimeShellDialog first; /* an update that leaves the title or the instruction empty keeps these. */
} Progress;

typedef struct Asked {
    uint32_t id;
    pid_t pid; /* the helper showing the question, ended when the guest takes it away. */
} Asked;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Progress g_progress[PROGRESS_MAX];
static Asked g_asked[PROGRESS_MAX];

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

/* kdialog on plasma, zenity elsewhere, either when only one is there, then xmessage. */
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
    if (on_path("qarma", path, size))
        return TOOL_ZENITY;
    /* an X program, which shows through Xwayland. */
    if (getenv("DISPLAY") && (on_path("xmessage", path, size) || on_path("gxmessage", path, size)))
        return TOOL_XMESSAGE;
    return TOOL_NONE;
}

/*
 * plasma installs no dialog program of its own, kdialog is a package apart. its qml runtime is always there, and
 * draws the scene with the controls and the icons of the desktop.
 */
static bool scene_tool(char *path, size_t size)
{
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    if (!desktop || !strstr(desktop, "KDE") || !getenv("XDG_RUNTIME_DIR") || !getenv("WAYLAND_DISPLAY"))
        return false;
    if (on_path("qml6", path, size) || on_path("qml-qt6", path, size))
        return true;
    static const char *const dirs[] = {"/usr/lib/qt6/bin", "/usr/lib64/qt6/bin", "/usr/lib/x86_64-linux-gnu/qt6/bin",
                                       "/usr/lib/aarch64-linux-gnu/qt6/bin"};
    for (size_t i = 0; i < MIME_ARRAY_COUNT(dirs); i++) {
        snprintf(path, size, "%s/qml", dirs[i]);
        if (access(path, X_OK) == 0)
            return true;
    }
    return false;
}

/* written apart and renamed, so that the scene never reads half of it. */
static bool write_file(const char *path, const char *data, size_t size)
{
    char temp[560];
    snprintf(temp, sizeof(temp), "%s.new", path);
    int fd = open(temp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    bool ok = write(fd, data, size) == (ssize_t)size;
    close(fd);
    if (!ok || rename(temp, path) != 0) {
        unlink(temp);
        return false;
    }
    return true;
}

static void put_string(char *out, size_t size, const char *text)
{
    size_t n = strlen(out);
    out[n++] = '"';
    for (const unsigned char *c = (const unsigned char *)text; *c && n + 10 < size; c++) {
        if (*c == '"' || *c == '\\')
            out[n++] = '\\', out[n++] = (char)*c;
        else if (*c < 0x20)
            n += (size_t)snprintf(out + n, size - n, "\\u%04x", *c);
        else
            out[n++] = (char)*c;
    }
    out[n++] = '"';
    out[n] = 0;
}

static void put_raw(char *out, size_t size, const char *text)
{
    size_t n = strlen(out);
    snprintf(out + n, size - n, "%s", text);
}

static void put_json(char *out, size_t size, const char *key, const char *text)
{
    put_raw(out, size, ",");
    put_string(out, size, key);
    put_raw(out, size, ":");
    put_string(out, size, text);
}

static void scene_state_path(uint32_t id, char *path, size_t size)
{
    snprintf(path, size, "%s/mime-dialog-%d-%u.json", getenv("XDG_RUNTIME_DIR"), (int)getpid(), id);
}

/* what the scene shows: the instruction is its heading, as the alert of macOS has it. */
static bool scene_state(const MimeShellDialog *d, const char *path)
{
    char text[4096], json[16384];
    bool progress = d->kind == MIME_DIALOG_PROGRESS || d->kind == MIME_DIALOG_UPDATE;
    const char *kind = progress ? "progress" : d->kind == MIME_DIALOG_ASK ? "ask" : d->kind == MIME_DIALOG_ERROR ? "error" : "info";
    snprintf(json, sizeof(json), "{\"permille\":%d", progress ? d->permille : -1);
    put_json(json, sizeof(json), "kind", kind);
    mime_utf16_to_utf8(text, sizeof(text), d->title, SIZE_MAX);
    put_json(json, sizeof(json), "title", text);
    mime_utf16_to_utf8(text, sizeof(text), d->instruction, SIZE_MAX);
    put_json(json, sizeof(json), "heading", text);
    mime_utf16_to_utf8(text, sizeof(text), d->content, SIZE_MAX);
    put_json(json, sizeof(json), "text", text);
    put_raw(json, sizeof(json), ",\"buttons\":[");
    uint32_t buttons = d->kind == MIME_DIALOG_ASK ? d->button_count : 0;
    for (uint32_t i = 0; i < buttons && !progress; i++) {
        mime_utf16_to_utf8(text, sizeof(text), d->buttons[i], SIZE_MAX);
        put_raw(json, sizeof(json), i ? "," : "");
        put_string(json, sizeof(json), text);
    }
    if (!buttons && !progress)
        put_string(json, sizeof(json), "OK");
    put_raw(json, sizeof(json), "]}");
    return write_file(path, json, strlen(json));
}

typedef enum Markup { MARKUP_NONE, MARKUP_PANGO, MARKUP_QT } Markup;

/* zenity reads pango markup and kdialog rich text, so the text is escaped for them. */
static void put_text(char *out, size_t size, const char *text, Markup markup)
{
    size_t n = strlen(out);
    for (const char *c = text; *c && n + 6 < size; c++) {
        const char *with = !markup ? nullptr : *c == '&' ? "&amp;" : *c == '<' ? "&lt;" : *c == '>' ? "&gt;" : nullptr;
        if (markup == MARKUP_QT && *c == '\n')
            with = "<br>";
        if (with) {
            memcpy(out + n, with, strlen(with));
            n += strlen(with);
        } else {
            out[n++] = *c;
        }
    }
    out[n] = 0;
}

/* the instruction is a heading in bold over the content, as the alert of macOS has it. */
static void dialog_text(const MimeShellDialog *d, char *out, size_t size, Markup markup)
{
    char instruction[1024], content[4096];
    mime_utf16_to_utf8(instruction, sizeof(instruction), d->instruction, SIZE_MAX);
    mime_utf16_to_utf8(content, sizeof(content), d->content, SIZE_MAX);
    out[0] = 0;
    if (instruction[0] && markup)
        put_raw(out, size, "<b>");
    put_text(out, size, instruction, markup);
    if (instruction[0] && markup)
        put_raw(out, size, "</b>");
    if (instruction[0] && content[0])
        put_text(out, size, "\n\n", markup);
    put_text(out, size, content, markup);
}

/* the environment of the scene, which may not read a file otherwise. freed by the caller. */
static char **scene_environment(void)
{
    size_t count = 0;
    while (environ[count])
        count++;
    char **env = malloc((count + 2) * sizeof(*env));
    if (!env)
        return nullptr;
    memcpy(env, environ, count * sizeof(*env));
    env[count] = "QML_XHR_ALLOW_FILE_READ=1";
    env[count + 1] = nullptr;
    return env;
}

/* the scene and the file it reads, false when the desktop has no runtime for it. */
static bool scene_prepare(const MimeShellDialog *d, char *tool, size_t tool_size, char *scene, size_t scene_size,
                          char *state, size_t state_size)
{
    if (!scene_tool(tool, tool_size))
        return false;
    snprintf(scene, scene_size, "%s/mime-dialog.qml", getenv("XDG_RUNTIME_DIR"));
    scene_state_path(d->id, state, state_size);
    return write_file(scene, g_scene, sizeof(g_scene)) && scene_state(d, state);
}

/*
 * runs a helper and waits for it; its exit code, -1 when it could not start, HELPER_KILLED when the guest took the
 * question away. stdout is kept when asked for.
 */
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
    if (done == pid && WIFSIGNALED(status))
        return HELPER_KILLED;
    return done == pid && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* the button pressed, -1 when the dialog was closed without one or could not be shown. */
static int show_scene(const MimeShellDialog *d)
{
    char tool[512], scene[512], state[512];
    if (!scene_prepare(d, tool, sizeof(tool), scene, sizeof(scene), state, sizeof(state)))
        return NOT_SHOWN;
    char **env = scene_environment();
    char *argv[] = {tool, "-a", "widget", scene, "--", state, nullptr};
    int code = run_helper(d->id, argv, env, nullptr, 0);
    free(env);
    unlink(state);
    uint32_t buttons = d->kind == MIME_DIALOG_ASK && d->button_count ? d->button_count : 1;
    /* the scene ends with 10 and the button, and with 9 when its window is closed. */
    if (code == HELPER_KILLED || code == 9)
        return -1;
    return code >= 10 && (uint32_t)(code - 10) < buttons ? code - 10 : NOT_SHOWN;
}

static int show(const MimeShellDialog *d)
{
    int answer = show_scene(d);
    if (answer != NOT_SHOWN)
        return answer;
    char tool[512], title[256], text[6144], labels[MIME_DIALOG_BUTTONS][256] = {}, out[256] = "";
    Tool kind = pick_tool(tool, sizeof(tool));
    mime_utf16_to_utf8(title, sizeof(title), d->title, SIZE_MAX);
    dialog_text(d, text, sizeof(text), kind == TOOL_ZENITY ? MARKUP_PANGO : kind == TOOL_KDIALOG ? MARKUP_QT : MARKUP_NONE);
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
    if (kind == TOOL_XMESSAGE) {
        /* a button is its label and the code xmessage ends with, and the list takes neither a comma nor a colon. */
        char list[MIME_DIALOG_BUTTONS * 272] = "";
        for (uint32_t i = 0; i < (buttons ? buttons : 1); i++) {
            char label[256];
            mime_strlcpy(label, buttons ? labels[i] : "OK", sizeof(label));
            for (char *c = label; *c; c++)
                if (*c == ',' || *c == ':')
                    *c = ' ';
            size_t used = strlen(list);
            snprintf(list + used, sizeof(list) - used, "%s%s:%u", i ? "," : "", label, XMESSAGE_FIRST + i);
        }
        argv[n++] = tool;
        argv[n++] = "-center";
        argv[n++] = "-title";
        argv[n++] = title;
        argv[n++] = "-buttons";
        argv[n++] = list;
        argv[n++] = text;
        argv[n] = nullptr;
        int code = run_helper(d->id, argv, nullptr, nullptr, 0);
        return code >= XMESSAGE_FIRST && (uint32_t)(code - XMESSAGE_FIRST) < (buttons ? buttons : 1) ? code - XMESSAGE_FIRST
                                                                                                    : -1;
    }
    mime_warn("no qml runtime, kdialog, zenity or xmessage to show a dialog: %s: %s", title, text);
    return -1;
}

/* a client that starts the machine uses it too, with no machine up. */
void mime_ui_alert(const char *text)
{
    MimeShellDialog d = {.kind = MIME_DIALOG_ERROR};
    mime_utf8_to_utf16(d.title, MIME_ARRAY_COUNT(d.title), "Mime");
    mime_utf8_to_utf16(d.content, MIME_ARRAY_COUNT(d.content), text);
    if (getenv("WAYLAND_DISPLAY") || getenv("DISPLAY"))
        show(&d);
}

/* the dialogs are those of the desktop: no process of mime shows one. */
int mime_ui_dialog_helper(int argc, char **argv)
{
    return -1;
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

/* a window with the bar: the scene on plasma, zenity elsewhere. false when the desktop has neither. */
static bool window_progress(const MimeShellDialog *d)
{
    pthread_mutex_lock(&g_lock);
    Progress *p = nullptr;
    for (int i = 0; i < PROGRESS_MAX && !p; i++)
        if (g_progress[i].id == d->id)
            p = &g_progress[i];
    if (d->kind == MIME_DIALOG_PROGRESS && !p) {
        char tool[512], scene[512], state[512];
        for (int i = 0; i < PROGRESS_MAX && !p; i++)
            if (!g_progress[i].id)
                p = &g_progress[i];
        if (p && scene_prepare(d, tool, sizeof(tool), scene, sizeof(scene), state, sizeof(state))) {
            char **env = scene_environment();
            char *argv[] = {tool, "-a", "widget", scene, "--", state, nullptr};
            pid_t pid;
            int r = posix_spawn(&pid, tool, nullptr, nullptr, argv, env ? env : environ);
            free(env);
            if (r == 0)
                *p = (Progress){d->id, pid, -1, *d};
            else
                unlink(state);
            pthread_mutex_unlock(&g_lock);
            return r == 0;
        }
        int fds[2];
        if (!p || pick_tool(tool, sizeof(tool)) != TOOL_ZENITY || pipe2(fds, O_CLOEXEC) != 0) {
            pthread_mutex_unlock(&g_lock);
            return false;
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
            return false;
        }
        *p = (Progress){d->id, pid, fds[1], *d};
    }
    if (!p) {
        pthread_mutex_unlock(&g_lock);
        return false;
    }
    bool scene = p->input < 0;
    char state[512];
    if (scene)
        scene_state_path(d->id, state, sizeof(state));
    if (d->kind == MIME_DIALOG_END) {
        pid_t pid = p->pid;
        if (scene)
            kill(pid, SIGTERM);
        else
            close(p->input);
        *p = (Progress){};
        pthread_mutex_unlock(&g_lock);
        while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR)
            ;
        if (scene)
            unlink(state);
        return true;
    }
    MimeShellDialog merged = *d;
    if (!merged.title[0])
        memcpy(merged.title, p->first.title, sizeof(merged.title));
    if (!merged.instruction[0])
        memcpy(merged.instruction, p->first.instruction, sizeof(merged.instruction));
    if (scene) {
        scene_state(&merged, state);
        pthread_mutex_unlock(&g_lock);
        return true;
    }
    char line[4096], text[4096];
    dialog_text(&merged, text, sizeof(text), MARKUP_NONE);
    int length = snprintf(line, sizeof(line), "%s# ", d->permille >= 0 ? "" : "pulsate\n");
    if (d->permille >= 0)
        length = snprintf(line, sizeof(line), "%d\n# ", d->permille / 10);
    for (const char *c = text; *c && length < (int)sizeof(line) - 2; c++)
        line[length++] = *c == '\n' ? ' ' : *c;
    line[length++] = '\n';
    if (write(p->input, line, (size_t)length) < 0)
        ;
    pthread_mutex_unlock(&g_lock);
    return true;
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
        /* the instruction is the view's title, the content its line. */
        char content[4096];
        mime_utf16_to_utf8(content, sizeof(content), d->content, SIZE_MAX);
        /* plasma's job tracker only where no window can be shown: it sits in the notifications. */
        if (!window_progress(d))
            mime_shell_job(d, content);
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
