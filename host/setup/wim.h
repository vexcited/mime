/* Windows Imaging Format reader for WIM and ESD files. */
#ifndef MIME_WIM_H
#define MIME_WIM_H

#include "../mime_host.h"

typedef struct MimeReader {
    void *ctx;
    bool (*read_at)(void *ctx, uint64_t offset, void *buf, size_t len);
    uint64_t size;
} MimeReader;

typedef struct MimeWim MimeWim;

typedef struct MimeWimDentry {
    char *name;
    uint32_t attributes;
    uint8_t hash[20]; /* SHA-1 of the unnamed data stream, zero when empty. */
    uint64_t creation_time;
    uint64_t write_time;
    struct MimeWimDentry *parent;
    struct MimeWimDentry *first_child;
    struct MimeWimDentry *next_sibling;
} MimeWimDentry;

typedef struct MimeWimImageInfo {
    uint32_t index; /* 1 based. */
    char name[128];
    char edition[64];
    char arch[16];
    uint32_t build;
} MimeWimImageInfo;

#define MIME_FILE_ATTRIBUTE_DIRECTORY 0x10
#define MIME_FILE_ATTRIBUTE_REPARSE_POINT 0x400

MimeWim *mime_wim_open(MimeReader reader);
void mime_wim_close(MimeWim *wim);

uint32_t mime_wim_image_count(const MimeWim *wim);
bool mime_wim_image_info(const MimeWim *wim, uint32_t index, MimeWimImageInfo *info);
bool mime_wim_select_image(MimeWim *wim, uint32_t index);

MimeWimDentry *mime_wim_root(MimeWim *wim);
MimeWimDentry *mime_wim_lookup(MimeWim *wim, const char *path);
uint64_t mime_wim_blob_size(MimeWim *wim, const uint8_t hash[20]);
/* sort extractions by this key to read front to back and reuse solid chunks containing multiple files. */
uint64_t mime_wim_blob_order(MimeWim *wim, const uint8_t hash[20]);

typedef bool (*MimeWimSink)(void *ctx, const void *data, size_t size);

/* streams a blob through sink and verifies its sha-1. */
bool mime_wim_read_blob(MimeWim *wim, const uint8_t hash[20], MimeWimSink sink, void *ctx);
bool mime_wim_extract_file(MimeWim *wim, const MimeWimDentry *dentry, const char *host_path);

#endif
