#ifndef MIME_GPU_ARCHIVE_H
#define MIME_GPU_ARCHIVE_H

#include <objc/objc.h>

/*
 * Metal 4 builds through a compiler that keeps every pipeline it makes, written to an archive in mime's cache that the
 * next run looks them up in: a pipeline found there takes a fraction of a millisecond instead of tens. one cache per
 * name, for the life of the process.
 */
typedef struct MimePipelineCache MimePipelineCache;

MimePipelineCache *mime_pipeline_cache(id device, const char *name);

/*
 * each builds from an MTL4LibraryDescriptor or an MTL4PipelineDescriptor and returns a retained object, or null with
 * the error. builds run together, the archive is written between them.
 */
void *mime_pipeline_cache_library(MimePipelineCache *cache, id descriptor, id *error);
void *mime_pipeline_cache_render(MimePipelineCache *cache, id descriptor, id *error);
void *mime_pipeline_cache_compute(MimePipelineCache *cache, id descriptor, id *error);

/* writes the archive now if pipelines were built since the last write; it is also written a few seconds after them. */
void mime_pipeline_cache_flush(MimePipelineCache *cache);

#endif
