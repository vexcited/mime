/*
 * builds printer, driver, port, monitor and print processor descriptions in caller buffers using pack.c. GetPrinter
 * and EnumPrinters share these builders for ANSI and Unicode results.
 */
#include "winspool_private.h"
#include "info.h"

static const WCHAR g_provider[] = L"Windows NT Local Print Providers";

PCWSTR InfoProvider(void)
{
    return g_provider;
}

static void driver_directory(WCHAR *path)
{
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (!n || n >= MAX_PATH)
        n = 0;
    _snwprintf(path + n, MAX_PATH, L"\\spool\\DRIVERS\\%ls\\3", SpoolEnvironmentDirectory(nullptr));
}

/* ------------------------------------------------------------------ printers */

static void build_printer(SpoolPack *p, int index, DWORD level, void **result)
{
    const SpoolPrinter *pr = SpoolPrinterAt(index);
    WCHAR text[320];
    switch (level) {
    case 0: {
        PRINTER_INFO_0W *i = SpoolPackStruct(p, sizeof(*i));
        LPWSTR name = SpoolPackString(p, pr->Name);
        if (i) {
            i->pPrinterName = name;
            i->cJobs = SpoolActiveJobs(index);
            i->dwAttributes = pr->Attributes;
            i->dwDriverVersion = 3;
            i->dwDriverMajorVersion = 10;
            i->dwDriverMinorVersion = 0;
            i->dwDriverBuild = 26100;
            i->dwDriverMajorBuild = 26100;
        }
        *result = i;
        break;
    }
    case 1: {
        PRINTER_INFO_1W *i = SpoolPackStruct(p, sizeof(*i));
        _snwprintf(text, ARRAYSIZE(text), L"%ls,%ls,%ls", pr->Name, pr->Driver, pr->Location);
        LPWSTR description = SpoolPackString(p, text), name = SpoolPackString(p, pr->Name),
               comment = SpoolPackString(p, pr->Comment);
        if (i) {
            i->Flags = PRINTER_ENUM_ICON8;
            i->pDescription = description;
            i->pName = name;
            i->pComment = comment;
        }
        *result = i;
        break;
    }
    case 2: {
        PRINTER_INFO_2W *i = SpoolPackStruct(p, sizeof(*i));
        LPWSTR name = SpoolPackString(p, pr->Name), port = SpoolPackString(p, pr->Port),
               driver = SpoolPackString(p, pr->Driver), comment = SpoolPackString(p, pr->Comment),
               location = SpoolPackString(p, pr->Location), sep = SpoolPackString(p, L""),
               processor = SpoolPackString(p, L"winprint"), datatype = SpoolPackString(p, L"RAW"),
               parameters = SpoolPackString(p, L"");
        DEVMODEW *dm = SpoolUserDevmode(index);
        LPVOID devmode = nullptr;
        if (dm) {
            if (p->ansi) {
                DEVMODEA *a = SpoolDevmodeToAnsi(dm);
                if (a) {
                    devmode = SpoolPackBlob(p, a, a->dmSize + a->dmDriverExtra);
                    SpoolFree(a);
                }
            } else {
                devmode = SpoolPackBlob(p, dm, dm->dmSize + dm->dmDriverExtra);
            }
            SpoolFree(dm);
        }
        if (i) {
            i->pPrinterName = name;
            i->pPortName = port;
            i->pDriverName = driver;
            i->pComment = comment;
            i->pLocation = location;
            i->pDevMode = devmode;
            i->pSepFile = sep;
            i->pPrintProcessor = processor;
            i->pDatatype = datatype;
            i->pParameters = parameters;
            i->Attributes = pr->Attributes;
            i->Priority = 1;
            i->DefaultPriority = 1;
            i->Status = 0;
            i->cJobs = SpoolActiveJobs(index);
            i->AveragePPM = 0;
        }
        *result = i;
        break;
    }
    case 3: {
        /* the security descriptor is not kept: every user may print. */
        void **i = SpoolPackStruct(p, sizeof(void *));
        *result = i;
        break;
    }
    case 4: {
        PRINTER_INFO_4W *i = SpoolPackStruct(p, sizeof(*i));
        LPWSTR name = SpoolPackString(p, pr->Name);
        if (i) {
            i->pPrinterName = name;
            i->pServerName = nullptr;
            i->Attributes = pr->Attributes;
        }
        *result = i;
        break;
    }
    case 5: {
        PRINTER_INFO_5W *i = SpoolPackStruct(p, sizeof(*i));
        LPWSTR name = SpoolPackString(p, pr->Name), port = SpoolPackString(p, pr->Port);
        if (i) {
            i->pPrinterName = name;
            i->pPortName = port;
            i->Attributes = pr->Attributes;
            i->DeviceNotSelectedTimeout = 15000;
            i->TransmissionRetryTimeout = 45000;
        }
        *result = i;
        break;
    }
    case 6: {
        DWORD *i = SpoolPackStruct(p, sizeof(DWORD));
        *result = i;
        break;
    }
    case 7: {
        PRINTER_INFO_7W *i = SpoolPackStruct(p, sizeof(*i));
        if (i)
            i->dwAction = 0;
        *result = i;
        break;
    }
    case 8:
    case 9: {
        PRINTER_INFO_8W *i = SpoolPackStruct(p, sizeof(*i));
        DEVMODEW *dm = SpoolUserDevmode(index);
        LPVOID devmode = nullptr;
        if (dm) {
            if (p->ansi) {
                DEVMODEA *a = SpoolDevmodeToAnsi(dm);
                if (a) {
                    devmode = SpoolPackBlob(p, a, a->dmSize + a->dmDriverExtra);
                    SpoolFree(a);
                }
            } else {
                devmode = SpoolPackBlob(p, dm, dm->dmSize + dm->dmDriverExtra);
            }
            SpoolFree(dm);
        }
        if (i)
            i->pDevMode = devmode;
        *result = i;
        break;
    }
    }
}

static SIZE_T printer_size(int index, DWORD level, BOOL ansi)
{
    SpoolPack p;
    void *r;
    SpoolPackBegin(&p, nullptr, 0, TRUE, ansi);
    build_printer(&p, index, level, &r);
    return SpoolPackNeeded(&p);
}

BOOL InfoGetPrinter(int index, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed, BOOL ansi)
{
    SIZE_T total = printer_size(index, level, ansi);
    *needed = (DWORD)total;
    if (!buffer || size < total)
        return SpoolFail(ERROR_INSUFFICIENT_BUFFER);
    SpoolPack p;
    void *r;
    memset(buffer, 0, size);
    SpoolPackBegin(&p, buffer, total, FALSE, ansi);
    build_printer(&p, index, level, &r);
    return TRUE;
}

/* the structure array comes first, then strings; pointers are written as each entry is built. */
BOOL InfoEnumPrinters(const int *indexes, int count, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                      LPDWORD returned, BOOL ansi)
{
    SpoolPack p;
    SIZE_T total = 0;
    SpoolPackBegin(&p, nullptr, 0, TRUE, ansi);
    void *r;
    for (int i = 0; i < count; i++)
        build_printer(&p, indexes[i], level, &r);
    total = SpoolPackNeeded(&p);
    *needed = (DWORD)total;
    *returned = 0;
    if (!count)
        return TRUE;
    if (!buffer || size < total)
        return SpoolFail(ERROR_INSUFFICIENT_BUFFER);
    memset(buffer, 0, size);
    SpoolPackBegin(&p, buffer, total, FALSE, ansi);
    for (int i = 0; i < count; i++)
        build_printer(&p, indexes[i], level, &r);
    *returned = (DWORD)count;
    return TRUE;
}

/* local printers appear under one print provider container. */
static void build_container(SpoolPack *p, void **result)
{
    PRINTER_INFO_1W *i = SpoolPackStruct(p, sizeof(*i));
    LPWSTR description = SpoolPackString(p, g_provider), name = SpoolPackString(p, g_provider),
           comment = SpoolPackString(p, L"");
    if (i) {
        i->Flags = PRINTER_ENUM_CONTAINER | PRINTER_ENUM_ICON1 | PRINTER_ENUM_EXPAND;
        i->pDescription = description;
        i->pName = name;
        i->pComment = comment;
    }
    *result = i;
}

BOOL InfoEnumContainers(LPBYTE buffer, DWORD size, LPDWORD needed, LPDWORD returned, BOOL ansi)
{
    SpoolPack p;
    void *r;
    SpoolPackBegin(&p, nullptr, 0, TRUE, ansi);
    build_container(&p, &r);
    SIZE_T total = SpoolPackNeeded(&p);
    *needed = (DWORD)total;
    *returned = 0;
    if (!buffer || size < total)
        return SpoolFail(ERROR_INSUFFICIENT_BUFFER);
    memset(buffer, 0, size);
    SpoolPackBegin(&p, buffer, total, FALSE, ansi);
    build_container(&p, &r);
    *returned = 1;
    return TRUE;
}

/* ------------------------------------------------------------------ drivers */

static void build_driver(SpoolPack *p, const SpoolPrinter *pr, DWORD level, void **result)
{
    WCHAR directory[MAX_PATH], file[MAX_PATH + 40];
    driver_directory(directory);
    switch (level) {
    case 1: {
        DRIVER_INFO_1W *i = SpoolPackStruct(p, sizeof(*i));
        LPWSTR name = SpoolPackString(p, pr->Driver);
        if (i)
            i->pName = name;
        *result = i;
        break;
    }
    case 2:
    case 3: {
        DRIVER_INFO_3W *i = SpoolPackStruct(p, level == 2 ? sizeof(DRIVER_INFO_2W) : sizeof(DRIVER_INFO_3W));
        LPWSTR name = SpoolPackString(p, pr->Driver), env = SpoolPackString(p, SpoolNativeEnvironment());
        _snwprintf(file, ARRAYSIZE(file), L"%ls\\mimepdf.dll", directory);
        LPWSTR path = SpoolPackString(p, file), data = SpoolPackString(p, file), config = SpoolPackString(p, file);
        LPWSTR help = nullptr, depend = nullptr, monitor = nullptr, datatype = nullptr;
        if (level == 3) {
            help = SpoolPackString(p, L"");
            depend = SpoolPackString(p, L"");
            monitor = SpoolPackString(p, L"");
            datatype = SpoolPackString(p, L"RAW");
        }
        if (i) {
            i->cVersion = 3;
            i->pName = name;
            i->pEnvironment = env;
            i->pDriverPath = path;
            i->pDataFile = data;
            i->pConfigFile = config;
            if (level == 3) {
                i->pHelpFile = help;
                i->pDependentFiles = depend;
                i->pMonitorName = monitor;
                i->pDefaultDataType = datatype;
            }
        }
        *result = i;
        break;
    }
    }
}

BOOL InfoDriverLevel(DWORD level)
{
    return level >= 1 && level <= 3;
}

BOOL InfoGetDriver(int index, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed, BOOL ansi)
{
    const SpoolPrinter *pr = SpoolPrinterAt(index);
    SpoolPack p;
    void *r;
    SpoolPackBegin(&p, nullptr, 0, TRUE, ansi);
    build_driver(&p, pr, level, &r);
    SIZE_T total = SpoolPackNeeded(&p);
    *needed = (DWORD)total;
    if (!buffer || size < total)
        return SpoolFail(ERROR_INSUFFICIENT_BUFFER);
    memset(buffer, 0, size);
    SpoolPackBegin(&p, buffer, total, FALSE, ansi);
    build_driver(&p, pr, level, &r);
    return TRUE;
}

/* ------------------------------------------------------------------ ports, monitors, processors and data types */

static const struct {
    PCWSTR Name, Description;
    DWORD Type;
} g_ports[] = {
    {L"FILE:", L"Print to File", PORT_TYPE_WRITE},
    {L"PORTPROMPT:", L"Local Port", PORT_TYPE_WRITE},
    {L"nul:", L"Local Port", PORT_TYPE_WRITE},
};

static const PCWSTR g_datatypes[] = {L"RAW", L"RAW [FF appended]", L"RAW [FF auto]", MIME_DATATYPE_BITMAP};

static void build_names(SpoolPack *p, DWORD kind, int index, DWORD level, void **result)
{
    switch (kind) {
    case INFO_PORT:
        if (level == 1) {
            PORT_INFO_1W *i = SpoolPackStruct(p, sizeof(*i));
            LPWSTR name = SpoolPackString(p, g_ports[index].Name);
            if (i)
                i->pName = name;
            *result = i;
        } else {
            PORT_INFO_2W *i = SpoolPackStruct(p, sizeof(*i));
            LPWSTR name = SpoolPackString(p, g_ports[index].Name), monitor = SpoolPackString(p, L"Local Port"),
                   description = SpoolPackString(p, g_ports[index].Description);
            if (i) {
                i->pPortName = name;
                i->pMonitorName = monitor;
                i->pDescription = description;
                i->fPortType = g_ports[index].Type;
            }
            *result = i;
        }
        break;
    case INFO_MONITOR: {
        if (level == 1) {
            MONITOR_INFO_1W *i = SpoolPackStruct(p, sizeof(*i));
            LPWSTR name = SpoolPackString(p, L"Local Port");
            if (i)
                i->pName = name;
            *result = i;
        } else {
            LPWSTR *i = SpoolPackStruct(p, 3 * sizeof(LPWSTR));
            LPWSTR name = SpoolPackString(p, L"Local Port"), env = SpoolPackString(p, SpoolNativeEnvironment()),
                   dll = SpoolPackString(p, L"localspl.dll");
            if (i) {
                i[0] = name;
                i[1] = env;
                i[2] = dll;
            }
            *result = i;
        }
        break;
    }
    case INFO_PROCESSOR: {
        PRINTPROCESSOR_INFO_1W *i = SpoolPackStruct(p, sizeof(*i));
        LPWSTR name = SpoolPackString(p, L"winprint");
        if (i)
            i->pName = name;
        *result = i;
        break;
    }
    case INFO_DATATYPE: {
        DATATYPES_INFO_1W *i = SpoolPackStruct(p, sizeof(*i));
        LPWSTR name = SpoolPackString(p, g_datatypes[index]);
        if (i)
            i->pName = name;
        *result = i;
        break;
    }
    case INFO_DRIVER: {
        build_driver(p, SpoolPrinterAt(index), level, result);
        break;
    }
    }
}

int InfoCount(DWORD kind)
{
    switch (kind) {
    case INFO_PORT:
        return (int)ARRAYSIZE(g_ports);
    case INFO_MONITOR:
    case INFO_PROCESSOR:
        return 1;
    case INFO_DATATYPE:
        return (int)ARRAYSIZE(g_datatypes);
    case INFO_DRIVER:
        return SpoolPrinterCount();
    }
    return 0;
}

BOOL InfoEnumNames(DWORD kind, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed, LPDWORD returned, BOOL ansi)
{
    int count = InfoCount(kind);
    SpoolPack p;
    void *r;
    SpoolPackBegin(&p, nullptr, 0, TRUE, ansi);
    for (int i = 0; i < count; i++)
        build_names(&p, kind, i, level, &r);
    SIZE_T total = SpoolPackNeeded(&p);
    *needed = (DWORD)total;
    *returned = 0;
    if (!count)
        return TRUE;
    if (!buffer || size < total)
        return SpoolFail(ERROR_INSUFFICIENT_BUFFER);
    memset(buffer, 0, size);
    SpoolPackBegin(&p, buffer, total, FALSE, ansi);
    for (int i = 0; i < count; i++)
        build_names(&p, kind, i, level, &r);
    *returned = (DWORD)count;
    return TRUE;
}

/* ------------------------------------------------------------------ forms */

static void build_form(SpoolPack *p, const SpoolPaper *paper, DWORD level, void **result)
{
    FORM_INFO_1W *i = SpoolPackStruct(p, level == 2 ? sizeof(FORM_INFO_2W) : sizeof(FORM_INFO_1W));
    LPWSTR name = SpoolPackString(p, paper->Name);
    if (i) {
        i->Flags = FORM_BUILTIN;
        i->pName = name;
        /* the size is in thousandths of a millimeter, the imageable area is the whole of it. */
        i->Size.cx = paper->Width * 100;
        i->Size.cy = paper->Height * 100;
        i->ImageableArea.left = 0;
        i->ImageableArea.top = 0;
        i->ImageableArea.right = i->Size.cx;
        i->ImageableArea.bottom = i->Size.cy;
        if (level == 2) {
            FORM_INFO_2W *i2 = (FORM_INFO_2W *)i;
            i2->StringType = 0;
            i2->pDisplayName = name;
        }
    }
    *result = i;
}

BOOL InfoEnumForms(DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed, LPDWORD returned, BOOL ansi)
{
    int count = SpoolPaperCount();
    SpoolPack p;
    void *r;
    SpoolPackBegin(&p, nullptr, 0, TRUE, ansi);
    for (int i = 0; i < count; i++)
        build_form(&p, SpoolPaperAt(i), level, &r);
    SIZE_T total = SpoolPackNeeded(&p);
    *needed = (DWORD)total;
    *returned = 0;
    if (!buffer || size < total)
        return SpoolFail(ERROR_INSUFFICIENT_BUFFER);
    memset(buffer, 0, size);
    SpoolPackBegin(&p, buffer, total, FALSE, ansi);
    for (int i = 0; i < count; i++)
        build_form(&p, SpoolPaperAt(i), level, &r);
    *returned = (DWORD)count;
    return TRUE;
}

BOOL InfoGetForm(const SpoolPaper *paper, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed, BOOL ansi)
{
    SpoolPack p;
    void *r;
    SpoolPackBegin(&p, nullptr, 0, TRUE, ansi);
    build_form(&p, paper, level, &r);
    SIZE_T total = SpoolPackNeeded(&p);
    *needed = (DWORD)total;
    if (!buffer || size < total)
        return SpoolFail(ERROR_INSUFFICIENT_BUFFER);
    memset(buffer, 0, size);
    SpoolPackBegin(&p, buffer, total, FALSE, ansi);
    build_form(&p, paper, level, &r);
    return TRUE;
}
