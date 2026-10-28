/*
 * How far a request is, read the same way by the window and the console: one bar over the download, the check and
 * the install, with the speed of the download and the time it has left.
 */
#include "featurep.h"

#define BAR_WIDTH 58

/* The thousandths of the bar the download and the check take, the install has the rest. */
static void shares(RUN *run, LONG *download, LONG *verify)
{
    switch (run->Feature->Handler) {
    case FEATURE_HANDLER_WEBVIEW2:
        *download = 550;
        *verify = 50;
        break;
    case FEATURE_HANDLER_NETFX:
        *download = 300;
        *verify = 50;
        break;
    default:
        *download = 900;
        *verify = 20;
        break;
    }
    /* A file on the drive is no download. */
    if (run->Source[0])
        *download = 0;
}

static void megabytes(ULONGLONG bytes, WCHAR *text, SIZE_T chars)
{
    ULONGLONG tenths = bytes * 10 / (1024 * 1024);
    _snwprintf(text, chars, L"%llu.%llu MB", tenths / 10, tenths % 10);
    text[chars - 1] = 0;
}

static void time_left(ULONGLONG seconds, WCHAR *text, SIZE_T chars)
{
    if (seconds < 10)
        _snwprintf(text, chars, L"a few seconds left");
    else if (seconds < 60)
        _snwprintf(text, chars, L"about %llu seconds left", (seconds + 4) / 5 * 5);
    else if (seconds < 120)
        _snwprintf(text, chars, L"about a minute left");
    else
        _snwprintf(text, chars, L"about %llu minutes left", (seconds + 59) / 60);
    text[chars - 1] = 0;
}

/* The speed over the last seconds, smoothed so that one slow read does not swing it. */
static void measure_rate(METER *meter, ULONGLONG bytes)
{
    ULONGLONG tick = GetTickCount64();
    if (!meter->RateTick || bytes < meter->RateBytes) {
        meter->RateTick = tick;
        meter->RateBytes = bytes;
        meter->Rate = 0;
        return;
    }
    ULONGLONG elapsed = tick - meter->RateTick;
    if (elapsed < 1000)
        return;
    ULONGLONG sample = (bytes - meter->RateBytes) * 1000 / elapsed;
    meter->Rate = meter->Rate ? (meter->Rate * 3 + sample) / 4 : sample;
    meter->RateTick = tick;
    meter->RateBytes = bytes;
}

void MeterRead(METER *meter, RUN *run, PROGRESS_VIEW *view)
{
    FEATURE_STAGE stage;
    ULONGLONG done, total;
    WCHAR step[128];
    RunSnapshot(run, &stage, &done, &total, step, RTL_NUMBER_OF(step));
    LONG download, verify;
    shares(run, &download, &verify);
    LONG install = 1000 - download - verify;
    if (total && done > total)
        done = total;
    LONG permille = 0;
    view->Known = TRUE;
    view->Detail[0] = 0;
    switch (stage) {
    case FEATURE_STAGE_COLLECTING:
    case FEATURE_STAGE_DOWNLOADING: {
        if (run->Source[0]) {
            _snwprintf(view->Line, RTL_NUMBER_OF(view->Line), L"Reading the file");
            break;
        }
        if (!done && !total) {
            _snwprintf(view->Line, RTL_NUMBER_OF(view->Line), L"Connecting to Microsoft");
            _snwprintf(view->Detail, RTL_NUMBER_OF(view->Detail), L"Waiting for the server to answer");
            view->Known = FALSE;
            break;
        }
        _snwprintf(view->Line, RTL_NUMBER_OF(view->Line), L"Downloading from Microsoft");
        measure_rate(meter, done);
        WCHAR have[32], all[32];
        megabytes(done, have, RTL_NUMBER_OF(have));
        if (total) {
            permille = (LONG)(done * (ULONGLONG)download / total);
            megabytes(total, all, RTL_NUMBER_OF(all));
            _snwprintf(view->Detail, RTL_NUMBER_OF(view->Detail), L"%ls of %ls", have, all);
        } else {
            view->Known = FALSE;
            _snwprintf(view->Detail, RTL_NUMBER_OF(view->Detail), L"%ls", have);
        }
        if (meter->Rate) {
            WCHAR rate[32], left[48];
            SIZE_T used = wcslen(view->Detail);
            megabytes(meter->Rate, rate, RTL_NUMBER_OF(rate));
            _snwprintf(view->Detail + used, RTL_NUMBER_OF(view->Detail) - used, L", %ls/s", rate);
            if (total) {
                used = wcslen(view->Detail);
                time_left((total - done) / meter->Rate, left, RTL_NUMBER_OF(left));
                _snwprintf(view->Detail + used, RTL_NUMBER_OF(view->Detail) - used, L", %ls", left);
            }
        }
        break;
    }
    case FEATURE_STAGE_VERIFYING:
        permille = download;
        _snwprintf(view->Line, RTL_NUMBER_OF(view->Line), L"Checking that the files are Microsoft's");
        break;
    case FEATURE_STAGE_INSTALLING:
        permille = download + verify + (total ? (LONG)(done * (ULONGLONG)install / total) : 0);
        _snwprintf(view->Line, RTL_NUMBER_OF(view->Line), L"%ls", step[0] ? step : L"Installing the files");
        break;
    case FEATURE_STAGE_REMOVING:
        view->Known = FALSE;
        _snwprintf(view->Line, RTL_NUMBER_OF(view->Line), L"Removing the files");
        break;
    }
    view->Line[RTL_NUMBER_OF(view->Line) - 1] = view->Detail[RTL_NUMBER_OF(view->Detail) - 1] = 0;
    if (run->Finished)
        permille = 1000;
    /* The bar never goes back, a download that starts over keeps what it showed. */
    if (permille > meter->Permille)
        meter->Permille = permille;
    view->Permille = meter->Permille;
    if (!view->Detail[0] && view->Known)
        _snwprintf(view->Detail, RTL_NUMBER_OF(view->Detail), L"%ld%% done", view->Permille / 10);
}

/* The bar of dism, redrawn in place: [=====     42.0%          ]. */
void ConsoleProgress(RUN *run, BOOL force)
{
    if (!run->Console)
        return;
    ULONGLONG tick = GetTickCount64();
    if (!force && tick - run->ConsoleTick < 250)
        return;
    PROGRESS_VIEW view;
    MeterRead(&run->ConsoleMeter, run, &view);
    if (!view.Known && !view.Permille)
        return;
    if (!force && view.Permille == run->PrintedPermille)
        return;
    run->ConsoleTick = tick;
    run->PrintedPermille = view.Permille;
    WCHAR bar[BAR_WIDTH + 1], percent[16];
    int filled = view.Permille * BAR_WIDTH / 1000;
    for (int i = 0; i < BAR_WIDTH; i++)
        bar[i] = i < filled ? L'=' : L' ';
    bar[BAR_WIDTH] = 0;
    int n = _snwprintf(percent, RTL_NUMBER_OF(percent), L"%ld.%ld%%", view.Permille / 10, view.Permille % 10);
    memcpy(bar + (BAR_WIDTH - n) / 2, percent, n * sizeof(WCHAR));
    tu_print(L"\r[%ls]", bar);
    tu_flush();
    run->BarShown = TRUE;
}

/* Ends the line of the bar, before the next text. */
void ConsoleProgressEnd(RUN *run)
{
    if (run->BarShown) {
        tu_print(L"\n");
        tu_flush();
        run->BarShown = FALSE;
    }
}
