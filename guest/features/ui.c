/*
 * The windows of a request: the question when a program needs a feature, the progress of the download and the
 * install, and what came of it. They are the host desktop's own dialogs, drawn with its fonts: a drive may have no
 * font at all, fonts are what features fetch. Without a host desktop they are task dialogs, as the ones Windows shows
 * for a feature on demand. The work runs on a thread of its own and the windows on the first one, so that the answer
 * to a question and the end of the work meet here.
 */
#include "featurep.h"
#include <mime/abi.h>
#include <mime/ntuser.h>
#include <mime/win32/commctrl.h>

#define TITLE L"Windows features"
#define ID_DOWNLOAD 101
#define ID_SKIP 102
#define PROGRESS_RANGE 1000

typedef struct UI {
    RUN *Run;
    DWORD (*Worker)(RUN *run);
    HANDLE Thread;
    HANDLE ConsentAsked, ConsentAnswered, QuestionOver, Staged, Done;
    HWND Question;
    volatile LONG Yes;
    volatile LONG Abandoned;   /* The program that asked ended while the question was up. */
    volatile LONG NoInterface; /* The question could not be shown. */
    volatile LONG StageSignalled;
    DWORD Result;
} UI;

static DWORD WINAPI worker_thread(void *parameter)
{
    UI *ui = parameter;
    ui->Result = ui->Worker(ui->Run);
    SetEvent(ui->Done);
    return 0;
}

static void name_of(RUN *run, WCHAR *name, SIZE_T chars)
{
    WidenName(run->Feature->DisplayName, name, chars);
}

static PCWSTR caller_of(RUN *run)
{
    return run->Caller[0] ? run->Caller : L"The program";
}

/* ------------------------------------------------------------------ the host's dialogs */

static BOOL g_no_host; /* the host has no desktop to show them on: task dialogs then. */

static void put(USHORT *to, SIZE_T chars, PCWSTR from)
{
    SIZE_T i = 0;
    for (; from && from[i] && i + 1 < chars; i++)
        to[i] = from[i];
    to[i] = 0;
}

/* a dialog of the host, its id, or 0 when the host shows none. */
static ULONG host_dialog(ULONG kind, ULONG id, PCWSTR instruction, PCWSTR content, PCWSTR const *buttons, ULONG count,
                         LONG permille)
{
    if (g_no_host)
        return 0;
    MimeShellDialog d = {.op = MIME_SHELL_DIALOG, .id = id, .kind = kind, .button_count = count, .permille = permille};
    put(d.title, RTL_NUMBER_OF(d.title), TITLE);
    put(d.instruction, RTL_NUMBER_OF(d.instruction), instruction);
    put(d.content, RTL_NUMBER_OF(d.content), content);
    for (ULONG i = 0; i < count && i < MIME_DIALOG_BUTTONS; i++)
        put(d.buttons[i], RTL_NUMBER_OF(d.buttons[i]), buttons[i]);
    LONG r = NtUserMimeShell(MIME_SHELLOP_COMMAND, 0, (PUCHAR)&d, sizeof(d));
    if (r < 0) {
        if (kind != MIME_DIALOG_UPDATE && kind != MIME_DIALOG_END)
            g_no_host = TRUE;
        return 0;
    }
    return kind == MIME_DIALOG_UPDATE || kind == MIME_DIALOG_END ? id : (ULONG)r;
}

/* the button pressed in the dialog id, -1 when it was closed; gives up the question when stop is set. */
static LONG host_answer(ULONG id, volatile LONG *stop)
{
    for (;;) {
        MIME_SHELL_EVENT e = {};
        LONG r = NtUserMimeShell(MIME_SHELLOP_WAIT, 200, (PUCHAR)&e, sizeof(e));
        if (r == 1 && e.Kind == MIME_SHELL_EVENT_DIALOG_ANSWERED && e.Id == id)
            return e.Arg;
        if (r < 0)
            return -1;
        if (stop && *stop) {
            host_dialog(MIME_DIALOG_END, id, nullptr, nullptr, nullptr, 0, 0);
            return -1;
        }
    }
}

/* ------------------------------------------------------------------ the question */

static HRESULT CALLBACK consent_callback(HWND window, UINT message, WPARAM wparam, LPARAM lparam, LONG_PTR data)
{
    UI *ui = (UI *)data;
    if (message == TDN_CREATED) {
        ui->Question = window;
        if (ui->Abandoned)
            PostMessageW(window, TDM_CLICK_BUTTON, ID_SKIP, 0);
    } else if (message == TDN_BUTTON_CLICKED) {
        ui->Yes = (int)wparam == ID_DOWNLOAD;
        return 0;
    }
    return 0;
}

static BOOL ask(UI *ui)
{
    RUN *run = ui->Run;
    WCHAR feature[96], instruction[200], content[800], download[160], skip[200], terms[256] = L"";
    name_of(run, feature, RTL_NUMBER_OF(feature));
    if (run->Font[0])
        _snwprintf(instruction, RTL_NUMBER_OF(instruction), L"%ls needs the font %ls, which comes with this feature:",
                   caller_of(run), run->Font);
    else if (run->Caller[0])
        _snwprintf(instruction, RTL_NUMBER_OF(instruction), L"%ls on your PC needs the following feature:", run->Caller);
    else
        _snwprintf(instruction, RTL_NUMBER_OF(instruction), L"A program on your PC needs the following feature:");
    if (run->Feature->DownloadMB)
        _snwprintf(content, RTL_NUMBER_OF(content),
                   L"%ls\n\nWindows gets it from Microsoft, about %lu MB. Nothing is downloaded unless you agree.", feature,
                   run->Feature->DownloadMB);
    else
        _snwprintf(content, RTL_NUMBER_OF(content),
                   L"%ls\n\nWindows gets it from Microsoft. Nothing is downloaded unless you agree.", feature);
    _snwprintf(download, RTL_NUMBER_OF(download), L"Download and install this feature\nThe program carries on when it is done.");
    if (run->Font[0]) {
        SIZE_T used = wcslen(content);
        _snwprintf(content + used, RTL_NUMBER_OF(content) - used, L" %ls cannot run without it and is closed if it is not installed.",
                   caller_of(run));
        _snwprintf(skip, RTL_NUMBER_OF(skip), L"Skip this installation\nNothing is downloaded and %ls is closed.",
                   run->Caller[0] ? run->Caller : L"the program");
    } else {
        _snwprintf(skip, RTL_NUMBER_OF(skip), L"Skip this installation\nNothing is downloaded, the program may not work without it.");
    }
    if (run->Feature->Terms) {
        WidenName(run->Feature->Terms, terms, RTL_NUMBER_OF(terms));
        SIZE_T used = wcslen(content);
        _snwprintf(content + used, RTL_NUMBER_OF(content) - used, L"\n\n%ls", terms);
    }
    instruction[RTL_NUMBER_OF(instruction) - 1] = content[RTL_NUMBER_OF(content) - 1] = skip[RTL_NUMBER_OF(skip) - 1] = 0;
    ui->Yes = FALSE;
    /* the host's buttons have one line, what each does goes under the text. */
    WCHAR host_content[1024];
    PCWSTR download_note = wcschr(download, L'\n'), skip_note = wcschr(skip, L'\n');
    _snwprintf(host_content, RTL_NUMBER_OF(host_content), L"%ls\n\nDownload: %ls\nSkip: %ls", content,
               download_note ? download_note + 1 : L"", skip_note ? skip_note + 1 : L"");
    host_content[RTL_NUMBER_OF(host_content) - 1] = 0;
    static PCWSTR const choices[] = {L"Download", L"Skip"};
    ULONG id = host_dialog(MIME_DIALOG_ASK, 0, instruction, host_content, choices, 2, 0);
    if (id)
        return host_answer(id, &ui->Abandoned) == 0 && !ui->Abandoned;
    TASKDIALOG_BUTTON buttons[] = {{ID_DOWNLOAD, download}, {ID_SKIP, skip}};
    TASKDIALOGCONFIG config = {.cbSize = sizeof(config),
                               .dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION,
                               .pszWindowTitle = TITLE,
                               .pszMainIcon = TD_INFORMATION_ICON,
                               .pszMainInstruction = instruction,
                               .pszContent = content,
                               .cButtons = RTL_NUMBER_OF(buttons),
                               .pButtons = buttons,
                               .nDefaultButton = ID_DOWNLOAD,
                               .pfCallback = consent_callback,
                               .lpCallbackData = (LONG_PTR)ui};
    int pressed = 0;
    if (FAILED(TaskDialogIndirect(&config, &pressed, nullptr, nullptr))) {
        ui->NoInterface = TRUE;
        return FALSE;
    }
    return pressed == ID_DOWNLOAD;
}

/* Takes the question away when the program that asked is gone, nobody is left to wait for the answer. */
static DWORD WINAPI watch_asker(void *parameter)
{
    UI *ui = parameter;
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, ui->Run->CallerPid);
    if (!process)
        return 0;
    HANDLE events[2] = {process, ui->QuestionOver};
    if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0) {
        ui->Abandoned = TRUE;
        if (ui->Question)
            PostMessageW(ui->Question, TDM_CLICK_BUTTON, ID_SKIP, 0);
    }
    CloseHandle(process);
    return 0;
}

uint32_t UiConsent(RUN *run)
{
    UI *ui = run->Ui;
    if (!ui)
        return FEATURE_OK;
    SetEvent(ui->ConsentAsked);
    WaitForSingleObject(ui->ConsentAnswered, INFINITE);
    if (ui->Yes)
        return FEATURE_OK;
    if (ui->Abandoned)
        return FEATURE_ERROR_ABANDONED;
    return ui->NoInterface ? FEATURE_ERROR_NO_INTERFACE : FEATURE_ERROR_CANCELLED;
}

void UiStage(RUN *run)
{
    UI *ui = run->Ui;
    if (ui && !InterlockedExchange(&ui->StageSignalled, 1))
        SetEvent(ui->Staged);
}

/* ------------------------------------------------------------------ the progress */

typedef struct PROGRESS_STATE {
    UI *Ui;
    METER Meter;
    BOOL Marquee;
    LONG Position;
    BOOL Cancelling, Closing;
    WCHAR Shown[300];
} PROGRESS_STATE;

/* The two lines under the instruction, always two so that the window keeps its size. */
static void progress_content(PROGRESS_STATE *state, WCHAR *text, SIZE_T chars, PROGRESS_VIEW *view)
{
    MeterRead(&state->Meter, state->Ui->Run, view);
    _snwprintf(text, chars, L"%ls\n%ls", view->Line, view->Detail[0] ? view->Detail : L" ");
    text[chars - 1] = 0;
}

static void set_marquee(HWND window, PROGRESS_STATE *state, BOOL on)
{
    if (state->Marquee == on)
        return;
    state->Marquee = on;
    /* The style changes first, as on Windows: a bar without it does not slide. */
    SendMessageW(window, TDM_SET_MARQUEE_PROGRESS_BAR, on, 0);
    SendMessageW(window, TDM_SET_PROGRESS_BAR_MARQUEE, on, 30);
    if (!on) {
        SendMessageW(window, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, PROGRESS_RANGE));
        SendMessageW(window, TDM_SET_PROGRESS_BAR_POS, (WPARAM)state->Position, 0);
    }
}

static void progress_update(HWND window, PROGRESS_STATE *state)
{
    RUN *run = state->Ui->Run;
    WCHAR text[300];
    PROGRESS_VIEW view;
    progress_content(state, text, RTL_NUMBER_OF(text), &view);
    set_marquee(window, state, !view.Known && !view.Permille);
    if (!state->Marquee && view.Permille != state->Position) {
        state->Position = view.Permille;
        SendMessageW(window, TDM_SET_PROGRESS_BAR_POS, (WPARAM)view.Permille, 0);
    }
    if (!state->Cancelling && wcscmp(text, state->Shown)) {
        lstrcpynW(state->Shown, text, RTL_NUMBER_OF(state->Shown));
        SendMessageW(window, TDM_UPDATE_ELEMENT_TEXT, TDE_CONTENT, (LPARAM)text);
    }
    FEATURE_STAGE stage = run->Stage;
    BOOL cancellable = run->Cancellable || stage == FEATURE_STAGE_COLLECTING || stage == FEATURE_STAGE_DOWNLOADING;
    SendMessageW(window, TDM_ENABLE_BUTTON, IDCANCEL, cancellable && !state->Cancelling);
}

static HRESULT CALLBACK progress_callback(HWND window, UINT message, WPARAM wparam, LPARAM lparam, LONG_PTR data)
{
    PROGRESS_STATE *state = (PROGRESS_STATE *)data;
    UI *ui = state->Ui;
    switch (message) {
    case TDN_CREATED:
        SendMessageW(window, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, PROGRESS_RANGE));
        progress_update(window, state);
        return 0;
    case TDN_TIMER:
        if (WaitForSingleObject(ui->Done, 0) == WAIT_OBJECT_0) {
            if (!state->Closing) {
                state->Closing = TRUE;
                SendMessageW(window, TDM_CLICK_BUTTON, IDCANCEL, 0);
            }
            return 0;
        }
        progress_update(window, state);
        return 0;
    case TDN_BUTTON_CLICKED:
        if (WaitForSingleObject(ui->Done, 0) == WAIT_OBJECT_0)
            return 0;
        /* A cancel is a request, the window stays until the work has stopped. */
        InterlockedExchange(&ui->Run->Cancel, 1);
        state->Cancelling = TRUE;
        SendMessageW(window, TDM_UPDATE_ELEMENT_TEXT, TDE_CONTENT, (LPARAM)L"Cancelling...\nThe step that is running stops first.");
        SendMessageW(window, TDM_ENABLE_BUTTON, IDCANCEL, FALSE);
        return 1;
    }
    return 0;
}

static void show_progress(UI *ui)
{
    RUN *run = ui->Run;
    WCHAR feature[96], instruction[160];
    name_of(run, feature, RTL_NUMBER_OF(feature));
    _snwprintf(instruction, RTL_NUMBER_OF(instruction), run->Remove ? L"Removing %ls" : L"Installing %ls", feature);
    instruction[RTL_NUMBER_OF(instruction) - 1] = 0;
    PROGRESS_STATE state = {.Ui = ui};
    PROGRESS_VIEW view;
    progress_content(&state, state.Shown, RTL_NUMBER_OF(state.Shown), &view);
    ULONG id = host_dialog(MIME_DIALOG_PROGRESS, 0, instruction, state.Shown, nullptr, 0,
                           !view.Known && !view.Permille ? -1 : (LONG)view.Permille);
    if (id) {
        /* the host's view has no cancel: it follows the work until it ends. */
        while (WaitForSingleObject(ui->Done, 250) == WAIT_TIMEOUT) {
            WCHAR text[300];
            progress_content(&state, text, RTL_NUMBER_OF(text), &view);
            host_dialog(MIME_DIALOG_UPDATE, id, instruction, text, nullptr, 0,
                        !view.Known && !view.Permille ? -1 : (LONG)view.Permille);
        }
        host_dialog(MIME_DIALOG_END, id, nullptr, nullptr, nullptr, 0, 0);
        return;
    }
    TASKDIALOGCONFIG config = {.cbSize = sizeof(config),
                               .dwFlags = TDF_SHOW_PROGRESS_BAR | TDF_CALLBACK_TIMER,
                               .dwCommonButtons = TDCBF_CANCEL_BUTTON,
                               .pszWindowTitle = TITLE,
                               .pszMainIcon = TD_INFORMATION_ICON,
                               .pszMainInstruction = instruction,
                               .pszContent = state.Shown,
                               /* Wide enough for the longest line of a download, the text changes in place. */
                               .cxWidth = 260,
                               .pfCallback = progress_callback,
                               .lpCallbackData = (LONG_PTR)&state};
    if (FAILED(TaskDialogIndirect(&config, nullptr, nullptr, nullptr)))
        WaitForSingleObject(ui->Done, INFINITE);
}

/* ------------------------------------------------------------------ the end */

static void show_failure(UI *ui, DWORD error)
{
    RUN *run = ui->Run;
    WCHAR feature[96], instruction[160], message[300], content[560];
    name_of(run, feature, RTL_NUMBER_OF(feature));
    _snwprintf(instruction, RTL_NUMBER_OF(instruction), error == FEATURE_ERROR_CANCELLED ? L"%ls was not installed" : L"%ls could not be installed", feature);
    instruction[RTL_NUMBER_OF(instruction) - 1] = 0;
    ErrorMessage(error, message, RTL_NUMBER_OF(message));
    if (run->Detail[0])
        _snwprintf(content, RTL_NUMBER_OF(content), L"%ls\n\n%ls", message, run->Detail);
    else
        _snwprintf(content, RTL_NUMBER_OF(content), L"%ls", message);
    content[RTL_NUMBER_OF(content) - 1] = 0;
    ULONG id = host_dialog(error == FEATURE_ERROR_CANCELLED ? MIME_DIALOG_INFO : MIME_DIALOG_ERROR, 0, instruction, content,
                           nullptr, 0, 0);
    if (id) {
        host_answer(id, nullptr);
        return;
    }
    TASKDIALOGCONFIG config = {.cbSize = sizeof(config),
                               .dwCommonButtons = TDCBF_CLOSE_BUTTON,
                               .pszWindowTitle = TITLE,
                               .pszMainIcon = error == FEATURE_ERROR_CANCELLED ? TD_INFORMATION_ICON : TD_ERROR_ICON,
                               .pszMainInstruction = instruction,
                               .pszContent = content};
    TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
}

/* A program that asked for a font does not go on without it, gdi32 ends it when this returns. */
static void show_closing(UI *ui, DWORD error)
{
    RUN *run = ui->Run;
    WCHAR instruction[160], message[300], content[700];
    _snwprintf(instruction, RTL_NUMBER_OF(instruction), L"%ls is closed", caller_of(run));
    instruction[RTL_NUMBER_OF(instruction) - 1] = 0;
    if (error == FEATURE_ERROR_CANCELLED) {
        _snwprintf(content, RTL_NUMBER_OF(content),
                   L"It cannot run without the font %ls, which was not installed. It asks again the next time it starts.",
                   run->Font);
    } else {
        ErrorMessage(error, message, RTL_NUMBER_OF(message));
        _snwprintf(content, RTL_NUMBER_OF(content), L"It cannot run without the font %ls, which could not be installed.\n\n%ls%ls%ls",
                   run->Font, message, run->Detail[0] ? L"\n\n" : L"", run->Detail);
    }
    content[RTL_NUMBER_OF(content) - 1] = 0;
    ULONG id = host_dialog(MIME_DIALOG_ERROR, 0, instruction, content, nullptr, 0, 0);
    if (id) {
        host_answer(id, nullptr);
        run->Told = TRUE;
        return;
    }
    TASKDIALOGCONFIG config = {.cbSize = sizeof(config),
                               .dwCommonButtons = TDCBF_CLOSE_BUTTON,
                               .pszWindowTitle = TITLE,
                               .pszMainIcon = TD_ERROR_ICON,
                               .pszMainInstruction = instruction,
                               .pszContent = content};
    run->Told = SUCCEEDED(TaskDialogIndirect(&config, nullptr, nullptr, nullptr));
}

DWORD UiRun(RUN *run, DWORD (*worker)(RUN *run))
{
    if (!run->Dialogs)
        return worker(run);
    UI ui = {.Run = run, .Worker = worker};
    ui.ConsentAsked = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ui.ConsentAnswered = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ui.QuestionOver = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ui.Staged = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ui.Done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    run->Ui = &ui;
    ui.Thread = CreateThread(nullptr, 0, worker_thread, &ui, 0, nullptr);
    if (!ui.Thread) {
        run->Ui = nullptr;
        return worker(run);
    }
    BOOL shown = FALSE;
    HANDLE events[3] = {ui.ConsentAsked, ui.Staged, ui.Done};
    for (;;) {
        DWORD which = WaitForMultipleObjects(3, events, FALSE, INFINITE);
        if (which == WAIT_OBJECT_0) {
            HANDLE watcher = ui.Run->CallerPid ? CreateThread(nullptr, 0, watch_asker, &ui, 0, nullptr) : nullptr;
            ui.Yes = ask(&ui);
            SetEvent(ui.QuestionOver);
            if (watcher) {
                WaitForSingleObject(watcher, INFINITE);
                CloseHandle(watcher);
            }
            SetEvent(ui.ConsentAnswered);
        } else if (which == WAIT_OBJECT_0 + 1) {
            shown = TRUE;
            show_progress(&ui);
            break;
        } else {
            break;
        }
    }
    WaitForSingleObject(ui.Thread, INFINITE);
    CloseHandle(ui.Thread);
    run->Ui = nullptr;
    if (run->Font[0] && ui.Result != FEATURE_OK && ui.Result != FEATURE_ERROR_ABANDONED &&
        ui.Result != FEATURE_ERROR_NO_INTERFACE)
        show_closing(&ui, ui.Result);
    else if (shown && ui.Result != FEATURE_OK)
        show_failure(&ui, ui.Result);
    CloseHandle(ui.ConsentAsked);
    CloseHandle(ui.ConsentAnswered);
    CloseHandle(ui.QuestionOver);
    CloseHandle(ui.Staged);
    CloseHandle(ui.Done);
    return ui.Result;
}
