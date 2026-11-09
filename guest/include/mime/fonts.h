/*
 * fonts. Windows always has its fonts, a machine of Mime may have none of them: they are an optional download, and a
 * program that asks for one that is missing gets it or is ended.
 */
#ifndef MIME_FONTS_H
#define MIME_FONTS_H

#include <stddef.h>

/*
 * The FontSubstitutes of Windows, with the bitmap faces that are mapped to outline ones: a request for the first name
 * is a request for the second. Case is not significant. The window manager maps with it, and the part of GDI that asks
 * for a missing font looks for the same family.
 */
typedef struct MIME_FONT_SUBSTITUTE {
    const wchar_t *From;
    const wchar_t *To;
} MIME_FONT_SUBSTITUTE;

#define MIME_FONT_SUBSTITUTES                                                                                          \
    {L"MS Shell Dlg", L"Microsoft Sans Serif"}, {L"MS Shell Dlg 2", L"Tahoma"},                                       \
    {L"MS Sans Serif", L"Microsoft Sans Serif"}, {L"Helv", L"Microsoft Sans Serif"}, {L"Helvetica", L"Arial"},       \
    {L"Tms Rmn", L"Times New Roman"}, {L"Times", L"Times New Roman"}, {L"MS Serif", L"Times New Roman"},            \
    {L"Courier", L"Courier New"}, {L"System", L"Segoe UI"}, {L"Fixedsys", L"Consolas"}, {L"Terminal", L"Consolas"}, \
    {L"Small Fonts", L"Segoe UI"}, {L"Segoe UI Variable", L"Segoe UI"}, {L"Segoe UI Variable Text", L"Segoe UI"},    \
    {L"Segoe UI Variable Display", L"Segoe UI"},

#endif
