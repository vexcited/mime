/*
 * the host's reading of a Windows shortcut file, see lnk.c.
 */
#ifndef MIME_LNK_H
#define MIME_LNK_H

#include "mime_host.h"

#include <stdio.h>

typedef struct MimeLink {
    char target[1100];     /* the path the shortcut was made for, as the guest sees it */
    char target_raw[1100]; /* the same with its variables still in it, when the shortcut keeps them */
    char relative[1100];   /* the path relative to the shortcut, when there is no other */
    char arguments[2100];
    char directory[1100];
    char description[260];
    char icon[1100];
    char icon_raw[1100];
    int icon_index;
    int show_command;
    char aumid[160];       /* System.AppUserModel.ID */
    char toast_clsid[40];  /* System.AppUserModel.ToastActivatorCLSID, in braces */
} MimeLink;

/* false when the bytes are not a shortcut or have no target. all the strings are utf-8. */
bool mime_lnk_parse(const void *data, size_t size, MimeLink *out);

#endif
