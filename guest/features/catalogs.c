#include "featurep.h"

#define TRUST_E_BAD_DIGEST_ ((DWORD)0x80096010)

static const PCWSTR g_folders[] = {
    L"Windows\\System32\\CatRoot",
    L"Windows\\System32\\CatRoot\\{F750E6C3-38EE-11D1-85E5-00C04FC295EE}",
    /* Windows has it too, and keeps it empty. */
    L"Windows\\System32\\CatRoot\\{127D0A1D-4EF2-11D1-8608-00C04FC295EE}",
};

static void catalog_path(RUN *run, const FEATURE_FONT *catalog, WCHAR *path, SIZE_T chars)
{
    _snwprintf(path, chars, L"%ls%ls\\%hs", run->Root, g_folders[1], catalog->File);
    path[chars - 1] = 0;
}

typedef struct INSTALL {
    RUN *Run;
    const FEATURE_DEF *Feature;
    BYTE *Written;
    DWORD Installed;
    DWORD Error;
} INSTALL;

static DWORD put_catalog(INSTALL *in, const FEATURE_FONT *catalog, const BYTE *data, SIZE_T size)
{
    RUN *run = in->Run;
    if (size != catalog->Size || !HashMatches(data, size, catalog->Sha256, catalog->Sha1)) {
        _snwprintf(run->Detail, RTL_NUMBER_OF(run->Detail), L"%hs is not the catalog of this name.", catalog->File);
        run->Detail[RTL_NUMBER_OF(run->Detail) - 1] = 0;
        return TRUST_E_BAD_DIGEST_;
    }
    WCHAR path[MAX_PATH], part[MAX_PATH];
    catalog_path(run, catalog, path, RTL_NUMBER_OF(path));
    _snwprintf(part, RTL_NUMBER_OF(part), L"%ls.part", path);
    part[RTL_NUMBER_OF(part) - 1] = 0;
    HANDLE file = CreateFileW(part, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();
    DWORD written = 0;
    BOOL ok = WriteFile(file, data, (DWORD)size, &written, nullptr) && written == size;
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (ok && !MoveFileExW(part, path, MOVEFILE_REPLACE_EXISTING)) {
        ok = FALSE;
        error = GetLastError();
    }
    if (!ok) {
        DeleteFileW(part);
        return error ? error : ERROR_DISK_FULL;
    }
    return ERROR_SUCCESS;
}

static BOOL pack_entry(void *context, const char *name, const BYTE *data, SIZE_T size)
{
    INSTALL *in = context;
    const FEATURE_FONT *catalog = FeatureFindFont(in->Feature, name);
    if (!catalog)
        return TRUE;
    SIZE_T index = (SIZE_T)(catalog - in->Feature->Fonts);
    if (in->Written[index])
        return TRUE;
    in->Error = put_catalog(in, catalog, data, size);
    if (in->Error)
        return FALSE;
    in->Written[index] = TRUE;
    in->Installed++;
    RunReport(in->Run, in->Installed, in->Feature->FontCount, L"Installing the catalogs");
    return TRUE;
}

/* Only the listed catalog is deleted, another file of the same name stays. */
static DWORD remove_catalog(RUN *run, const FEATURE_FONT *catalog)
{
    WCHAR path[MAX_PATH];
    catalog_path(run, catalog, path, RTL_NUMBER_OF(path));
    if (!FileMatches(path, catalog->Sha256))
        return ERROR_SUCCESS;
    SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
    if (DeleteFileW(path) || GetLastError() == ERROR_FILE_NOT_FOUND)
        return ERROR_SUCCESS;
    return GetLastError();
}

uint32_t CatalogsInstall(RUN *run, const FEATURE_DEF *feature, PCWSTR package)
{
    BYTE *data = nullptr;
    SIZE_T size = 0;
    DWORD error = ReadPackage(package, &data, &size);
    if (error)
        return error;
    INSTALL in = {.Run = run, .Feature = feature};
    in.Written = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, feature->FontCount ? feature->FontCount : 1);
    if (!in.Written) {
        HeapFree(GetProcessHeap(), 0, data);
        return ERROR_OUTOFMEMORY;
    }
    SIZE_T root = wcslen(run->Root);
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(g_folders); i++) {
        WCHAR folder[MAX_PATH];
        _snwprintf(folder, RTL_NUMBER_OF(folder), L"%ls%ls", run->Root, g_folders[i]);
        folder[RTL_NUMBER_OF(folder) - 1] = 0;
        for (WCHAR *p = folder + root; *p; p++) {
            if (*p != L'\\')
                continue;
            *p = 0;
            CreateDirectoryW(folder, nullptr);
            *p = L'\\';
        }
        CreateDirectoryW(folder, nullptr);
    }
    if (!PackWalk(data, size, pack_entry, &in))
        error = in.Error ? in.Error : ERROR_INVALID_DATA;
    if (!error && in.Installed != feature->FontCount) {
        error = ERROR_FILE_NOT_FOUND;
        _snwprintf(run->Detail, RTL_NUMBER_OF(run->Detail), L"The package has %lu of the %lu catalogs of the feature.", in.Installed,
                   (DWORD)feature->FontCount);
        run->Detail[RTL_NUMBER_OF(run->Detail) - 1] = 0;
    }
    /* A failed install takes back what it wrote. */
    if (error)
        for (SIZE_T i = 0; i < feature->FontCount; i++)
            if (in.Written[i])
                remove_catalog(run, &feature->Fonts[i]);
    HeapFree(GetProcessHeap(), 0, in.Written);
    HeapFree(GetProcessHeap(), 0, data);
    return error;
}

BOOL CatalogsPresent(RUN *run, const FEATURE_DEF *feature)
{
    for (SIZE_T i = 0; i < feature->FontCount; i++) {
        WCHAR path[MAX_PATH];
        catalog_path(run, &feature->Fonts[i], path, RTL_NUMBER_OF(path));
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
            return FALSE;
    }
    return feature->FontCount != 0;
}

/* The folders stay, as on Windows. */
uint32_t CatalogsRemove(RUN *run, const FEATURE_DEF *feature)
{
    DWORD error = ERROR_SUCCESS;
    for (SIZE_T i = 0; i < feature->FontCount; i++) {
        RunReport(run, i, feature->FontCount, L"Removing the catalogs");
        DWORD failed = remove_catalog(run, &feature->Fonts[i]);
        if (failed)
            error = failed;
    }
    return error;
}
