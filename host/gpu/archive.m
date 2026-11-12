#include "archive.h"

#include "../mime_host.h"

#import <Metal/Metal.h>
#include <errno.h>
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static constexpr int64_t WRITE_DELAY_NS = 5 * NSEC_PER_SEC;

/* the serializer crashes when it is written while a build adds to it: builds share the lock, a write takes it alone. */
__attribute__((objc_subclassing_restricted))
@interface MimePipelineArchive : NSObject
@end

@implementation MimePipelineArchive {
@public
    id<MTL4Compiler> compiler;
    id<MTL4PipelineDataSetSerializer> serializer;
    MTL4CompilerTaskOptions *options;
    NSURL *url;
    pthread_rwlock_t builds;
    os_unfair_lock lock; /* for the two flags. */
    bool built, scheduled;
}
@end

/* an archive holds binaries for one gpu and one system, so both name its file. */
static NSURL *archive_url(id<MTLDevice> device, const char *name)
{
    MimePaths paths;
    if (!mime_paths_init(&paths))
        return nil;
    NSString *system = NSProcessInfo.processInfo.operatingSystemVersionString;
    NSString *key = [NSString stringWithFormat:@"%@ %llu %@", device.name, device.registryID, system];
    const char *text = key.UTF8String;
    MimeSha1 sha;
    uint8_t digest[20];
    mime_sha1_init(&sha);
    mime_sha1_update(&sha, text, strlen(text));
    mime_sha1_final(&sha, digest);
    char directory[MIME_PATH_MAX], file[MIME_PATH_MAX];
    snprintf(directory, sizeof(directory), "%s/metal", paths.cache);
    if (!mime_mkdir_p(directory, 0700))
        return nil;
    snprintf(file, sizeof(file), "%s/%s-%02x%02x%02x%02x%02x%02x%02x%02x.mtl4archive", directory, name, digest[0],
             digest[1], digest[2], digest[3], digest[4], digest[5], digest[6], digest[7]);
    return [NSURL fileURLWithFileSystemRepresentation:file isDirectory:NO relativeToURL:nil];
}

static MimePipelineArchive *open_cache(id<MTLDevice> device, const char *name)
{
    MimePipelineArchive *cache = [[MimePipelineArchive alloc] init];
    MTL4PipelineDataSetSerializerDescriptor *sd = [[MTL4PipelineDataSetSerializerDescriptor alloc] init];
    sd.configuration = MTL4PipelineDataSetSerializerConfigurationCaptureBinaries;
    cache->serializer = [device newPipelineDataSetSerializerWithDescriptor:sd];
    MTL4CompilerDescriptor *cd = [[MTL4CompilerDescriptor alloc] init];
    cd.pipelineDataSetSerializer = cache->serializer;
    cache->compiler = [device newCompilerWithDescriptor:cd error:nil];
    if (!cache->compiler)
        return nil;
    pthread_rwlock_init(&cache->builds, nullptr);
    cache->lock = OS_UNFAIR_LOCK_INIT;
    cache->url = archive_url(device, name);
    NSError *error = nil;
    id<MTL4Archive> archive = cache->url ? [device newArchiveWithURL:cache->url error:&error] : nil;
    if (archive) {
        cache->options = [[MTL4CompilerTaskOptions alloc] init];
        cache->options.lookupArchives = @[archive];
    } else if (cache->url && [NSFileManager.defaultManager fileExistsAtPath:cache->url.path]) {
        mime_warn("gpu: the %s pipeline archive cannot be read, it starts over: %s", name,
                  error.localizedDescription.UTF8String);
        [NSFileManager.defaultManager removeItemAtURL:cache->url error:nil];
    }
    return cache;
}

MimePipelineCache *mime_pipeline_cache(id device, const char *name)
{
    static os_unfair_lock lock = OS_UNFAIR_LOCK_INIT;
    static NSMutableDictionary<NSString *, MimePipelineArchive *> *caches;
    @autoreleasepool {
        NSString *key = @(name);
        os_unfair_lock_lock(&lock);
        if (!caches)
            caches = [[NSMutableDictionary alloc] init];
        MimePipelineArchive *cache = caches[key];
        if (!cache) {
            cache = open_cache(device, name);
            if (cache)
                caches[key] = cache;
        }
        os_unfair_lock_unlock(&lock);
        return (__bridge MimePipelineCache *)cache;
    }
}

void mime_pipeline_cache_flush(MimePipelineCache *opaque)
{
    MimePipelineArchive *cache = (__bridge MimePipelineArchive *)opaque;
    if (!cache || !cache->url)
        return;
    os_unfair_lock_lock(&cache->lock);
    bool built = cache->built;
    cache->built = false;
    os_unfair_lock_unlock(&cache->lock);
    if (!built)
        return;
    pthread_rwlock_wrlock(&cache->builds);
    @autoreleasepool {
        /* a reader of the archive never sees half of one. */
        NSURL *temporary = [cache->url URLByAppendingPathExtension:@"tmp"];
        NSError *error = nil;
        if (![cache->serializer serializeAsArchiveAndFlushToURL:temporary error:&error] ||
            rename(temporary.fileSystemRepresentation, cache->url.fileSystemRepresentation) != 0) {
            /* a build just over may not be in the serializer yet, leaving it nothing to write: the next try will. */
            if (!error || error.code != MTLBinaryArchiveErrorCompilationFailure)
                mime_warn("gpu: the pipeline archive is not written: %s",
                          error ? error.localizedDescription.UTF8String : strerror(errno));
            unlink(temporary.fileSystemRepresentation);
            os_unfair_lock_lock(&cache->lock);
            cache->built = true;
            os_unfair_lock_unlock(&cache->lock);
        }
    }
    pthread_rwlock_unlock(&cache->builds);
}

static void built(MimePipelineArchive *cache)
{
    os_unfair_lock_lock(&cache->lock);
    cache->built = true;
    bool schedule = !cache->scheduled && cache->url;
    cache->scheduled = true;
    os_unfair_lock_unlock(&cache->lock);
    if (!schedule)
        return;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, WRITE_DELAY_NS), dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        os_unfair_lock_lock(&cache->lock);
        cache->scheduled = false;
        os_unfair_lock_unlock(&cache->lock);
        mime_pipeline_cache_flush((__bridge MimePipelineCache *)cache);
    });
}

typedef id (^Build)(id<MTL4Compiler> compiler, MTL4CompilerTaskOptions *options, NSError **error);

static void *build(MimePipelineCache *opaque, bool pipeline, id *error, Build make)
{
    MimePipelineArchive *cache = (__bridge MimePipelineArchive *)opaque;
    if (!cache)
        return nullptr;
    NSError *failure = nil;
    pthread_rwlock_rdlock(&cache->builds);
    id made = make(cache->compiler, cache->options, &failure);
    pthread_rwlock_unlock(&cache->builds);
    if (made && pipeline)
        built(cache);
    if (error)
        *error = failure;
    return (__bridge_retained void *)made;
}

void *mime_pipeline_cache_library(MimePipelineCache *cache, id descriptor, id *error)
{
    return build(cache, false, error, ^id(id<MTL4Compiler> compiler, MTL4CompilerTaskOptions *options, NSError **e) {
        return [compiler newLibraryWithDescriptor:descriptor error:e];
    });
}

void *mime_pipeline_cache_render(MimePipelineCache *cache, id descriptor, id *error)
{
    return build(cache, true, error, ^id(id<MTL4Compiler> compiler, MTL4CompilerTaskOptions *options, NSError **e) {
        return [compiler newRenderPipelineStateWithDescriptor:descriptor compilerTaskOptions:options error:e];
    });
}

void *mime_pipeline_cache_compute(MimePipelineCache *cache, id descriptor, id *error)
{
    return build(cache, true, error, ^id(id<MTL4Compiler> compiler, MTL4CompilerTaskOptions *options, NSError **e) {
        return [compiler newComputePipelineStateWithDescriptor:descriptor compilerTaskOptions:options error:e];
    });
}
