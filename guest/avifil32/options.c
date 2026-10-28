/*
 * AVISaveOptions: the dialog in which the compression of the streams of a file is chosen, and AVISaveOptionsFree.
 * The video compressors are the installable codecs of the video compression manager, which mime has none of, so the
 * only choice that video has is its frames as they are. The format of audio is chosen from the converters of the
 * audio compression manager.
 */
#include "avifil32_private.h"
#include <mime/win32/msacm.h>

#define IDC_INTERLEAVE 110
#define IDC_EVERY 111
#define IDC_STREAMS 113
#define IDC_OPTIONS 114
#define IDC_FORMAT 115

#define MESSAGE_NO_COMPRESSORS L"There are no video compressors installed. The video is saved as it is, in uncompressed frames."
#define MESSAGE_OLD_ACM L"Choosing a wave format from this dialog requires at least version 2.0 of the Microsoft Audio Compression Manager."
#define TITLE_OLD_ACM L"Bad MSACM Version"

typedef struct OptionsJob {
    int count;
    int current;
    UINT flags;
    PAVISTREAM *streams;
    LPAVICOMPRESSOPTIONS *options;
} OptionsJob;

static OptionsJob g_job;

static void free_global(void *p)
{
    if (p) {
        HGLOBAL handle = GlobalHandle(p);
        GlobalUnlock(handle);
        GlobalFree(handle);
    }
}

/* the format of the stream in the words of the dialog, a picture size or the wave format. */
static void format_text(PAVISTREAM stream, const AVISTREAMINFOW *info, WCHAR *out, int capacity)
{
    LONG size = 0;
    out[0] = 0;
    stream->lpVtbl->ReadFormat(stream, AVIStreamStart(stream), nullptr, &size);
    if (!size)
        return;
    BYTE *format = GlobalLock(GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (SIZE_T)size));
    if (!format)
        return;
    if (stream->lpVtbl->ReadFormat(stream, AVIStreamStart(stream), format, &size) == S_OK) {
        if (info->fccType == FCC_vids) {
            const BITMAPINFOHEADER *bih = (const BITMAPINFOHEADER *)format;
            WCHAR text[96];
            LONG numbers[3] = {bih->biWidth, bih->biHeight, bih->biBitCount};
            SIZE_T at = 0;
            for (int n = 0; n < 3; n++) {
                char digits[12];
                int count = 0;
                LONG value = numbers[n];
                if (value < 0) {
                    text[at++] = '-';
                    value = -value;
                }
                for (LONG v = value; v > 0 || !count; v /= 10)
                    digits[count++] = (char)('0' + v % 10);
                while (count)
                    text[at++] = (WCHAR)digits[--count];
                if (n < 2)
                    text[at++] = 'x';
            }
            text[at] = 0;
            AviCopyName(out, text, (SIZE_T)capacity);
            at = lstrlenW(out);
            if ((int)at + 1 < capacity) {
                out[at] = '\n';
                out[at + 1] = 0;
            }
            if (bih->biCompression == BI_RGB)
                AviCopyName(out + lstrlenW(out), AVI_STRING_UNCOMPRESSED, (SIZE_T)capacity - lstrlenW(out));
        } else if (info->fccType == FCC_auds && size >= (LONG)sizeof(WAVEFORMATEX) - 2) {
            ACMFORMATDETAILSW details = {0};
            ACMFORMATTAGDETAILSW tag = {0};
            details.cbStruct = sizeof(details);
            details.pwfx = (WAVEFORMATEX *)format;
            details.cbwfx = (DWORD)size;
            details.dwFormatTag = ((WAVEFORMATEX *)format)->wFormatTag;
            tag.cbStruct = sizeof(tag);
            tag.dwFormatTag = details.dwFormatTag;
            if (acmFormatTagDetailsW(nullptr, &tag, ACM_FORMATTAGDETAILSF_FORMATTAG) == 0 &&
                acmFormatDetailsW(nullptr, &details, ACM_FORMATDETAILSF_FORMAT) == 0) {
                AviCopyName(out, details.szFormat, (SIZE_T)capacity);
                SIZE_T at = lstrlenW(out);
                if ((int)at + 1 < capacity)
                    out[at++] = ' ';
                AviCopyName(out + at, tag.szFormatTag, (SIZE_T)capacity - at);
            }
        }
    }
    free_global(format);
}

static void stream_chosen(HWND dialog)
{
    AVISTREAMINFOW info = {0};
    WCHAR text[120];
    g_job.current = (int)SendDlgItemMessageW(dialog, IDC_STREAMS, CB_GETCURSEL, 0, 0);
    if (g_job.current < 0 || AVIStreamInfoW(g_job.streams[g_job.current], &info, AVISTREAMINFOW_SIZE))
        return;
    format_text(g_job.streams[g_job.current], &info, text, 120);
    SetDlgItemTextW(dialog, IDC_FORMAT, text);
    EnableWindow(GetDlgItem(dialog, IDC_OPTIONS), info.fccType == FCC_auds || info.fccType == FCC_vids);
}

/* the choices of one stream: the frames of a video as they are, the format a sound is converted to. */
static BOOL stream_options(HWND owner)
{
    AVISTREAMINFOW info = {0};
    LPAVICOMPRESSOPTIONS options = g_job.options[g_job.current];
    if (AVIStreamInfoW(g_job.streams[g_job.current], &info, AVISTREAMINFOW_SIZE))
        return FALSE;
    if (info.fccType == FCC_vids) {
        if (!(options->dwFlags & AVICOMPRESSF_VALID)) {
            memset(options, 0, sizeof(*options));
            options->fccHandler = FCC_DIB;
            options->dwQuality = (DWORD)-1;
        }
        if (MessageBoxW(owner, MESSAGE_NO_COMPRESSORS, L"Compression Options", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
            return FALSE;
        options->fccHandler = FCC_DIB;
        options->dwKeyFrameEvery = 0;
        options->dwBytesPerSecond = 0;
        options->dwQuality = (DWORD)-1;
        options->dwFlags &= ~(DWORD)(AVICOMPRESSF_KEYFRAMES | AVICOMPRESSF_DATARATE);
        options->dwFlags |= AVICOMPRESSF_VALID;
        return TRUE;
    }
    if (info.fccType == FCC_auds) {
        ACMFORMATCHOOSEW choose = {0};
        DWORD largest = 0;
        LONG native_size = 0;
        MMRESULT result;
        if (acmGetVersion() < 0x02000000) {
            MessageBoxW(owner, MESSAGE_OLD_ACM, TITLE_OLD_ACM, MB_ICONHAND);
            return FALSE;
        }
        choose.cbStruct = sizeof(choose);
        choose.hwndOwner = owner;
        choose.fdwStyle = options->dwFlags & AVICOMPRESSF_VALID ? ACMFORMATCHOOSE_STYLEF_INITTOWFXSTRUCT : 0;
        acmMetrics(nullptr, ACM_METRIC_MAX_SIZE_FORMAT, &largest);
        /* the format is kept in memory the caller can free with AVISaveOptionsFree. */
        if ((!options->cbFormat || !options->lpFormat) && largest) {
            options->lpFormat = GlobalLock(GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, largest));
            options->cbFormat = largest;
        } else if (options->cbFormat < largest && largest) {
            void *old = options->lpFormat;
            HGLOBAL grown = GlobalReAlloc(GlobalHandle(old), largest, GMEM_MOVEABLE | GMEM_ZEROINIT);
            options->lpFormat = grown ? GlobalLock(grown) : old;
            options->cbFormat = grown ? largest : options->cbFormat;
        }
        if (!options->lpFormat)
            return FALSE;
        choose.pwfx = options->lpFormat;
        choose.cbwfx = options->cbFormat;
        PAVISTREAM stream = g_job.streams[g_job.current];
        stream->lpVtbl->ReadFormat(stream, AVIStreamStart(stream), nullptr, &native_size);
        if (native_size < 18)
            native_size = 18;
        choose.pwfxEnum = GlobalLock(GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (SIZE_T)native_size));
        if (choose.pwfxEnum) {
            stream->lpVtbl->ReadFormat(stream, AVIStreamStart(stream), choose.pwfxEnum, &native_size);
            choose.fdwEnum |= ACM_FORMATENUMF_CONVERT;
        }
        result = acmFormatChooseW(&choose);
        if (!result)
            options->dwFlags |= AVICOMPRESSF_VALID;
        else if (result != ACMERR_CANCELED)
            MessageBeep(0);
        free_global(choose.pwfxEnum);
        return result == 0;
    }
    return FALSE;
}

static INT_PTR CALLBACK options_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_INITDIALOG) {
        if (g_job.count == 1) {
            /* a single stream has nothing to choose from, its options are all there is. */
            g_job.current = 0;
            EndDialog(dialog, stream_options(dialog));
            return TRUE;
        }
        for (int i = 0; i < g_job.count; i++) {
            AVISTREAMINFOW info = {0};
            AVIStreamInfoW(g_job.streams[i], &info, AVISTREAMINFOW_SIZE);
            SendDlgItemMessageW(dialog, IDC_STREAMS, CB_ADDSTRING, 0, (LPARAM)info.szName);
        }
        SendDlgItemMessageW(dialog, IDC_STREAMS, CB_SETCURSEL, 0, 0);
        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDC_STREAMS, CBN_SELCHANGE), (LPARAM)dialog);
        DWORD every = 1;
        BOOL interleave = FALSE;
        if (g_job.options[0]->dwFlags & AVICOMPRESSF_VALID) {
            every = g_job.options[0]->dwInterleaveEvery;
            interleave = (g_job.options[0]->dwFlags & AVICOMPRESSF_INTERLEAVE) != 0;
        }
        CheckDlgButton(dialog, IDC_INTERLEAVE, interleave);
        SetDlgItemInt(dialog, IDC_EVERY, every, FALSE);
        EnableWindow(GetDlgItem(dialog, IDC_EVERY), interleave);
        return TRUE;
    }
    if (message != WM_COMMAND)
        return FALSE;
    switch (LOWORD(wparam)) {
    case IDOK: {
        /* the interleaving is the same for all the streams. */
        BOOL interleave = IsDlgButtonChecked(dialog, IDC_INTERLEAVE) != 0;
        UINT every = GetDlgItemInt(dialog, IDC_EVERY, nullptr, FALSE);
        for (int i = 0; i < g_job.count; i++) {
            g_job.options[i]->dwInterleaveEvery = every;
            if (interleave)
                g_job.options[i]->dwFlags |= AVICOMPRESSF_INTERLEAVE;
            else
                g_job.options[i]->dwFlags &= ~(DWORD)AVICOMPRESSF_INTERLEAVE;
        }
        EndDialog(dialog, 1);
        return FALSE;
    }
    case IDCANCEL:
        EndDialog(dialog, 0);
        return FALSE;
    case IDC_INTERLEAVE:
        EnableWindow(GetDlgItem(dialog, IDC_EVERY), IsDlgButtonChecked(dialog, IDC_INTERLEAVE));
        return FALSE;
    case IDC_STREAMS:
        if (HIWORD(wparam) == CBN_SELCHANGE)
            stream_chosen(dialog);
        return FALSE;
    case IDC_OPTIONS:
        stream_options(dialog);
        stream_chosen(dialog);
        return FALSE;
    }
    return FALSE;
}

INT_PTR WINAPI AVISaveOptions(HWND owner, UINT flags, int count, PAVISTREAM *streams, LPAVICOMPRESSOPTIONS *options)
{
    AVICOMPRESSOPTIONS *saved;
    INT_PTR result;
    if (count <= 0 || !streams || !options)
        return 0;
    saved = AviAlloc(sizeof(*saved) * (SIZE_T)count);
    if (!saved)
        return 0;
    g_job.count = count;
    g_job.current = -1;
    g_job.flags = flags;
    g_job.streams = streams;
    g_job.options = options;
    for (int i = 0; i < count; i++)
        saved[i] = *options[i];
    result = DialogBoxParamW(AviModule, MAKEINTRESOURCEW(110), owner, options_proc, 0);
    /* the choices of a dialog that was not accepted are given up. */
    if (!result)
        for (int i = 0; i < count; i++)
            *options[i] = saved[i];
    AviFree(saved);
    return result == -1 ? 0 : result;
}

HRESULT WINAPI AVISaveOptionsFree(int count, LPAVICOMPRESSOPTIONS *options)
{
    for (int i = count - 1; i >= 0; i--) {
        free_global(options[i]->lpParms);
        options[i]->lpParms = nullptr;
        free_global(options[i]->lpFormat);
        options[i]->lpFormat = nullptr;
    }
    return S_OK;
}
