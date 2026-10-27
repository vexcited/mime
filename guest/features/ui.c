/*
 * The windows of a request: the question when a program needs a feature, the progress of the download and the
 * install, and what came of it. They are task dialogs, as the ones Windows shows for a feature on demand. The work
 * runs on a thread of its own and the windows on the first one, so that the answer to a question and the end of the
 * work meet here.
 */
#include "featurep.h"
#include <mime/win32/commctrl.h>

#define TITLE L"Mime features"
#define ID_DOWNLOAD 101
#define ID_SKIP 102
#define PROGRESS_RANGE 1000

typedef struct UI {
    RUN *Run;
    DWORD (*Worker)(RUN *run);
    HANDLE Thread;
    HANDLE ConsentAsked, ConsentAnswered, Staged, Done;
    volatile LONG Yes;
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

/* ------------------------------------------------------------------ the question */

static HRESULT CALLBACK consent_callback(HWND window, UINT message, WPARAM wparam, LPARAM lparam, LONG_PTR data)
{
    UI *ui = (UI *)data;
    if (message == TDN_BUTTON_CLICKED) {
        ui->Yes = (int)wparam == ID_DOWNLOAD;
        return 0;
    }
    return 0;
}

static BOOL ask(UI *ui)
{
    RUN *run = ui->Run;
    WCHAR feature[96], instruction[160], content[640], download[160], skip[160], terms[256] = L"";
    name_of(run, feature, RTL_NUMBER_OF(feature));
    if (run->Caller[0])
        _snwprintf(instruction, RTL_NUMBER_OF(instruction), L"%ls on your PC needs the following feature:", run->Caller);
    else
        _snwprintf(instruction, RTL_NUMBER_OF(instruction), L"A program on your PC needs the following feature:");
    if (run->Feature->DownloadMB)
        _snwprintf(content, RTL_NUMBER_OF(content),
                   L"%ls\n\nMime gets it from Microsoft, about %lu MB. Nothing is downloaded unless you agree.", feature,
                   run->Feature->DownloadMB);
    else
        _snwprintf(content, RTL_NUMBER_OF(content),
                   L"%ls\n\nMime gets it from Microsoft. Nothing is downloaded unless you agree.", feature);
    _snwprintf(download, RTL_NUMBER_OF(download), L"Download and install this feature\nThe program carries on when it is done.");
    _snwprintf(skip, RTL_NUMBER_OF(skip), L"Skip this installation\nNothing is downloaded, the program may not work without it.");
    if (run->Feature->Terms) {
        WidenName(run->Feature->Terms, terms, RTL_NUMBER_OF(terms));
        SIZE_T used = wcslen(content);
        _snwprintf(content + used, RTL_NUMBER_OF(content) - used, L"\n\n%ls", terms);
    }
    instruction[RTL_NUMBER_OF(instruction) - 1] = content[RTL_NUMBER_OF(content) - 1] = 0;
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
    ui->Yes = FALSE;
    if (FAILED(TaskDialogIndirect(&config, &pressed, nullptr, nullptr)))
        return FALSE;
    return pressed == ID_DOWNLOAD;
}

uint32_t UiConsent(RUN *run)
{
    UI *ui = run->Ui;
    if (!ui)
        return FEATURE_OK;
    SetEvent(ui->ConsentAsked);
    WaitForSingleObject(ui->ConsentAnswered, INFINITE);
    return ui->Yes ? FEATURE_OK : FEATURE_ERROR_CANCELLED;
}

void UiStage(RUN *run)
{
    UI *ui = run->Ui;
    if (ui && !InterlockedExchange(&ui->StageSignalled, 1))
        SetEvent(ui->Staged);
}

/* ------------------------------------------------------------------ the progress */

static void megabytes(ULONGLONG bytes, WCHAR *text, SIZE_T chars)
{
    ULONGLONG tenths = bytes * 10 / (1024 * 1024);
    _snwprintf(text, chars, L"%llu.%llu MB", tenths / 10, tenths % 10);
    text[chars - 1] = 0;
}

typedef struct PROGRESS_STATE {
    UI *Ui;
    BOOL Marquee;
    WCHAR Shown[200];
    BOOL Closing;
} PROGRESS_STATE;

static void progress_text(RUN *run, WCHAR *text, SIZE_T chars, LONG *position, BOOL *known)
{
    FEATURE_STAGE stage;
    ULONGLONG done, total;
    WCHAR step[128];
    RunSnapshot(run, &stage, &done, &total, step, RTL_NUMBER_OF(step));
    *known = FALSE;
    *position = 0;
    switch (stage) {
    case FEATURE_STAGE_DOWNLOADING:
    case FEATURE_STAGE_COLLECTING:
        if (total) {
            WCHAR a[32], b[32];
            megabytes(done, a, RTL_NUMBER_OF(a));
            megabytes(total, b, RTL_NUMBER_OF(b));
            _snwprintf(text, chars, L"Downloading from Microsoft: %ls of %ls", a, b);
            *position = (LONG)(done * PROGRESS_RANGE / total);
            *known = TRUE;
        } else if (done) {
            WCHAR a[32];
            megabytes(done, a, RTL_NUMBER_OF(a));
            _snwprintf(text, chars, L"Downloading from Microsoft: %ls", a);
        } else {
            _snwprintf(text, chars, L"Searching for the files");
        }
        break;
    case FEATURE_STAGE_VERIFYING:
        _snwprintf(text, chars, L"Checking that the files are Microsoft's");
        break;
    case FEATURE_STAGE_INSTALLING:
        if (total) {
            _snwprintf(text, chars, L"%ls", step[0] ? step : L"Installing");
            *position = (LONG)(done * PROGRESS_RANGE / total);
            *known = TRUE;
        } else {
            _snwprintf(text, chars, L"%ls", step[0] ? step : L"Installing");
        }
        break;
    case FEATURE_STAGE_REMOVING:
        _snwprintf(text, chars, L"Removing the files");
        break;
    }
    text[chars - 1] = 0;
}

static HRESULT CALLBACK progress_callback(HWND window, UINT message, WPARAM wparam, LPARAM lparam, LONG_PTR data)
{
    PROGRESS_STATE *state = (PROGRESS_STATE *)data;
    UI *ui = state->Ui;
    RUN *run = ui->Run;
    switch (message) {
    case TDN_CREATED:
        SendMessageW(window, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, PROGRESS_RANGE));
        SendMessageW(window, TDM_SET_PROGRESS_BAR_MARQUEE, TRUE, 30);
        state->Marquee = TRUE;
        return 0;
    case TDN_TIMER: {
        if (WaitForSingleObject(ui->Done, 0) == WAIT_OBJECT_0) {
            if (!state->Closing) {
                state->Closing = TRUE;
                SendMessageW(window, TDM_CLICK_BUTTON, IDCANCEL, 0);
            }
            return 0;
        }
        WCHAR text[200];
        LONG position;
        BOOL known;
        progress_text(run, text, RTL_NUMBER_OF(text), &position, &known);
        if (known && state->Marquee) {
            SendMessageW(window, TDM_SET_PROGRESS_BAR_MARQUEE, FALSE, 0);
            state->Marquee = FALSE;
        } else if (!known && !state->Marquee) {
            SendMessageW(window, TDM_SET_PROGRESS_BAR_MARQUEE, TRUE, 30);
            state->Marquee = TRUE;
        }
        if (known)
            SendMessageW(window, TDM_SET_PROGRESS_BAR_POS, (WPARAM)position, 0);
        if (wcscmp(text, state->Shown)) {
            lstrcpynW(state->Shown, text, RTL_NUMBER_OF(state->Shown));
            SendMessageW(window, TDM_UPDATE_ELEMENT_TEXT, TDE_CONTENT, (LPARAM)text);
        }
        SendMessageW(window, TDM_ENABLE_BUTTON, IDCANCEL, run->Cancellable || run->Stage == FEATURE_STAGE_DOWNLOADING);
        return 0;
    }
    case TDN_BUTTON_CLICKED:
        if (WaitForSingleObject(ui->Done, 0) == WAIT_OBJECT_0)
            return 0;
        /* A cancel is a request, the window stays until the work has stopped. */
        InterlockedExchange(&run->Cancel, 1);
        SendMessageW(window, TDM_UPDATE_ELEMENT_TEXT, TDE_CONTENT, (LPARAM)L"Cancelling...");
        lstrcpynW(state->Shown, L"Cancelling...", RTL_NUMBER_OF(state->Shown));
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
    TASKDIALOGCONFIG config = {.cbSize = sizeof(config),
                               .dwFlags = TDF_SHOW_PROGRESS_BAR | TDF_CALLBACK_TIMER,
                               .dwCommonButtons = TDCBF_CANCEL_BUTTON,
                               .pszWindowTitle = TITLE,
                               .pszMainIcon = TD_INFORMATION_ICON,
                               .pszMainInstruction = instruction,
                               .pszContent = L"Searching for the files",
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
    TASKDIALOGCONFIG config = {.cbSize = sizeof(config),
                               .dwCommonButtons = TDCBF_CLOSE_BUTTON,
                               .pszWindowTitle = TITLE,
                               .pszMainIcon = error == FEATURE_ERROR_CANCELLED ? TD_INFORMATION_ICON : TD_ERROR_ICON,
                               .pszMainInstruction = instruction,
                               .pszContent = content};
    TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
}

DWORD UiRun(RUN *run, DWORD (*worker)(RUN *run))
{
    if (!run->Dialogs)
        return worker(run);
    UI ui = {.Run = run, .Worker = worker};
    ui.ConsentAsked = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ui.ConsentAnswered = CreateEventW(nullptr, FALSE, FALSE, nullptr);
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
            ui.Yes = ask(&ui);
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
    if (shown && ui.Result != FEATURE_OK)
        show_failure(&ui, ui.Result);
    CloseHandle(ui.ConsentAsked);
    CloseHandle(ui.ConsentAnswered);
    CloseHandle(ui.Staged);
    CloseHandle(ui.Done);
    return ui.Result;
}
