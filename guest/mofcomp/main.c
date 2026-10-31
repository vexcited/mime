/*
 * mofcomp.exe: compiles a managed object format file into the repository of the management instrumentation.
 *
 *   mofcomp [-check] [-autorecover] [-class:forceupdate] [-N:namespace] [-WMI] [-Amendment:locale] file.mof
 *
 * the work is done by the compiler of wbemcore.dll; this program reads the options, prints what the compiler reports
 * and turns the result into an exit code of 0 or 1.
 */
#include "tool.h"
#include <mime/win32/objbase.h>
#include <mime/win32/oleauto.h>
#include <mime/win32/wbemcli.h>

int tool_main(void)
{
    const WCHAR *file = nullptr, *target = nullptr;
    LONG options = 0, class_flags = 0;
    for (int i = 1; i < tu_argc; i++) {
        const WCHAR *word = tu_argv[i], *rest;
        if (word[0] != L'-' && word[0] != L'/') {
            if (file) {
                tu_eprint(L"Only one MOF file can be compiled at a time.\n");
                return 1;
            }
            file = word;
        } else if (tu_prefix(word + 1, L"check", &rest) && !*rest) {
            options |= WBEM_FLAG_CHECK_ONLY;
        } else if (tu_prefix(word + 1, L"autorecover", &rest) && !*rest) {
            options |= WBEM_FLAG_AUTORECOVER;
        } else if (tu_prefix(word + 1, L"WMI", &rest) && !*rest) {
            options |= WBEM_FLAG_WMI_CHECK;
        } else if (tu_prefix(word + 1, L"class:", &rest)) {
            if (tu_prefix(rest, L"forceupdate", &rest) || tu_prefix(rest, L"safeupdate", &rest))
                class_flags |= 0;
            else if (tu_prefix(rest, L"updateonly", &rest))
                options |= WBEM_FLAG_CLASS_UPDATE_ONLY_BIT;
        } else if (tu_prefix(word + 1, L"N:", &rest)) {
            target = rest;
        } else if (tu_prefix(word + 1, L"Amendment:", &rest) || tu_prefix(word + 1, L"MOF:", &rest) ||
                   tu_prefix(word + 1, L"MFL:", &rest) || tu_prefix(word + 1, L"U:", &rest) || tu_prefix(word + 1, L"P:", &rest) ||
                   tu_prefix(word + 1, L"A:", &rest) || tu_prefix(word + 1, L"B:", &rest)) {
            /* the options of other uses of the compiler change nothing here. */
        } else if (word[1] == L'?') {
            tu_print(L"\nMOF Compiler\n\nmofcomp [options] <file.mof>\n\n  -check              parse the file and store nothing\n"
                     L"  -N:<namespace>      the namespace the file is compiled into\n  -autorecover        keep the file for the repository to restore\n\n");
            return 0;
        } else {
            tu_eprint(L"Unknown option: %s\n", word);
            return 1;
        }
    }
    if (!file) {
        tu_eprint(L"No MOF file was given.\n");
        return 1;
    }
    tu_print(L"Microsoft (R) MOF Compiler Version 10.0.26100.1\nCopyright (c) Microsoft Corp. 1997-2006. All rights reserved.\n");
    tu_print(L"Parsing MOF file: %s\n", file);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        tu_eprint(L"Unable to initialize COM, error 0x%08X\n", (unsigned)hr);
        return 1;
    }
    IMofCompiler *compiler = nullptr;
    hr = CoCreateInstance(&CLSID_MofCompiler, nullptr, CLSCTX_INPROC_SERVER, &IID_IMofCompiler, (void **)&compiler);
    if (FAILED(hr)) {
        tu_eprint(L"Unable to create the MOF compiler, error 0x%08X\n", (unsigned)hr);
        return 1;
    }
    WBEM_COMPILE_STATUS_INFO info = {};
    WCHAR *path = tu_dup(file);
    WCHAR *space = target ? tu_dup(target) : nullptr;
    hr = compiler->lpVtbl->CompileFile(compiler, path, space, nullptr, nullptr, nullptr, options, class_flags, 0, &info);
    compiler->lpVtbl->Release(compiler);
    tu_free(path);
    tu_free(space);
    if (hr == S_OK) {
        tu_print(L"MOF file has been successfully parsed\n");
        if (!(options & WBEM_FLAG_CHECK_ONLY))
            tu_print(L"Storing data in the repository...\n");
        tu_print(L"Done!\n");
        return 0;
    }
    if (info.ObjectNum || info.FirstLine)
        tu_eprint(L"An error occurred while processing item %ld defined on lines %ld - %ld in file %s:\n", (long)info.ObjectNum,
                  (long)info.FirstLine, (long)info.LastLine, file);
    tu_eprint(L"Compiler returned error 0x%x\n", (unsigned)(info.hRes ? info.hRes : hr));
    return 1;
}
