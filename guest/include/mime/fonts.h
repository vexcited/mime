/*
 * fonts. Windows always has its fonts, a machine of Mime may have none of them: they are an optional download, and a
 * program that asks for one that is missing gets it or is ended.
 */
#ifndef MIME_FONTS_H
#define MIME_FONTS_H

#include <stddef.h>

/*
 * The FontSubstitutes of Windows: a request for the first name is a request for the second. Case is not significant.
 * The window manager maps with it, and the part of GDI that asks for a missing font looks for the same family. The
 * Segoe UI Variable names are the faces of the one variable font, which Mime does not tell apart.
 */
typedef struct MIME_FONT_SUBSTITUTE {
    const wchar_t *From;
    const wchar_t *To;
} MIME_FONT_SUBSTITUTE;

#define MIME_FONT_SUBSTITUTES                                                                                     \
    {L"MS Shell Dlg", L"Microsoft Sans Serif"}, {L"MS Shell Dlg 2", L"Tahoma"}, {L"Helv", L"MS Sans Serif"},     \
    {L"Helvetica", L"Arial"}, {L"Tms Rmn", L"MS Serif"}, {L"Times", L"Times New Roman"},                        \
    {L"Segoe UI Variable Text", L"Segoe UI Variable"}, {L"Segoe UI Variable Display", L"Segoe UI Variable"},    \
    {L"Segoe UI Variable Small", L"Segoe UI Variable"}, {L"Arabic Transparent", L"Arial"},                        \
    {L"Arabic Transparent Bold", L"Arial Bold"}, {L"Tahoma Armenian", L"Tahoma"},

/*
 * The FontSubstitutes of Windows that name a charset, "Arial CE,238" for "Arial,238": the old names of a face for one
 * charset. A request for the name in that charset is one for the face, and EnumFontFamilies lists the name with the
 * face.
 */
typedef struct MIME_FONT_CHARSET_SUBSTITUTE {
    const wchar_t *From;
    unsigned char FromCharset;
    const wchar_t *To;
    unsigned char ToCharset;
} MIME_FONT_CHARSET_SUBSTITUTE;

#define MIME_FONT_CHARSET_SUBSTITUTES                                                                             \
    {L"Arabic Transparent Bold", 0, L"Arial Bold", 178}, {L"Arabic Transparent", 0, L"Arial", 178},              \
    {L"Arial Baltic", 186, L"Arial", 186}, {L"Arial CE", 238, L"Arial", 238}, {L"Arial CYR", 204, L"Arial", 204}, \
    {L"Arial Greek", 161, L"Arial", 161}, {L"Arial TUR", 162, L"Arial", 162},                                    \
    {L"Courier New Baltic", 186, L"Courier New", 186}, {L"Courier New CE", 238, L"Courier New", 238},             \
    {L"Courier New CYR", 204, L"Courier New", 204}, {L"Courier New Greek", 161, L"Courier New", 161},             \
    {L"Courier New TUR", 162, L"Courier New", 162}, {L"Times New Roman Baltic", 186, L"Times New Roman", 186},    \
    {L"Times New Roman CE", 238, L"Times New Roman", 238}, {L"Times New Roman CYR", 204, L"Times New Roman", 204}, \
    {L"Times New Roman Greek", 161, L"Times New Roman", 161}, {L"Times New Roman TUR", 162, L"Times New Roman", 162},

#endif
