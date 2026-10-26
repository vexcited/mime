/* Metal from C: the types the driver passes by value and typed objc_msgSend calls. */
#ifndef MIME_VKMETAL_MTL_H
#define MIME_VKMETAL_MTL_H

#include <objc/message.h>
#include <objc/runtime.h>
#include <stdbool.h>
#include <stdint.h>

#include "mtl_enums.h"

typedef unsigned long NSUInteger;
typedef long NSInteger;

typedef struct MTLOrigin {
    NSUInteger x, y, z;
} MTLOrigin;
typedef struct MTLSize {
    NSUInteger width, height, depth;
} MTLSize;
typedef struct MTLRegion {
    MTLOrigin origin;
    MTLSize size;
} MTLRegion;
typedef struct MTLViewport {
    double x, y, width, height, znear, zfar;
} MTLViewport;
typedef struct MTLScissorRect {
    NSUInteger x, y, width, height;
} MTLScissorRect;
typedef struct MTLClearColor {
    double r, g, b, a;
} MTLClearColor;
typedef struct MTLResourceID {
    uint64_t impl;
} MTLResourceID;
typedef struct MTL4BufferRange {
    uint64_t address, length;
} MTL4BufferRange;
typedef struct MTLSizeAndAlign {
    NSUInteger size, align;
} MTLSizeAndAlign;
typedef struct MTLTextureSwizzleChannels {
    uint8_t r, g, b, a;
} MTLTextureSwizzleChannels;
typedef struct NSRange {
    NSUInteger location, length;
} NSRange;
typedef struct MTLSamplePosition {
    float x, y;
} MTLSamplePosition;

extern id MTLCreateSystemDefaultDevice(void);
extern void *objc_autoreleasePoolPush(void);
extern void objc_autoreleasePoolPop(void *pool);

#define MTL_SEL(name)                                                                                                  \
    ({                                                                                                                 \
        static SEL s_;                                                                                                 \
        if (!s_)                                                                                                       \
            s_ = sel_registerName(name);                                                                               \
        s_;                                                                                                            \
    })
#define MTL_CLASS(name) ((id)objc_getClass(name))
/* a message with an explicit function type, for signatures the shorthands below do not cover. */
#define MTL_MSG(type, obj, name, ...) ((type)objc_msgSend)((id)(obj), MTL_SEL(name) __VA_OPT__(, ) __VA_ARGS__)

#define M_ID(o, s) MTL_MSG(id (*)(id, SEL), o, s)
#define M_ID_ID(o, s, a) MTL_MSG(id (*)(id, SEL, id), o, s, (id)(a))
#define M_ID_U(o, s, a) MTL_MSG(id (*)(id, SEL, NSUInteger), o, s, (NSUInteger)(a))
#define M_U(o, s) MTL_MSG(NSUInteger (*)(id, SEL), o, s)
#define M_U64(o, s) MTL_MSG(uint64_t (*)(id, SEL), o, s)
#define M_B(o, s) MTL_MSG(bool (*)(id, SEL), o, s)
#define M_B_U(o, s, a) MTL_MSG(bool (*)(id, SEL, NSUInteger), o, s, (NSUInteger)(a))
#define M_PTR(o, s) MTL_MSG(void *(*)(id, SEL), o, s)
#define M_RID(o, s) MTL_MSG(MTLResourceID (*)(id, SEL), o, s)
#define M_V(o, s) MTL_MSG(void (*)(id, SEL), o, s)
#define M_V_ID(o, s, a) MTL_MSG(void (*)(id, SEL, id), o, s, (id)(a))
#define M_V_U(o, s, a) MTL_MSG(void (*)(id, SEL, NSUInteger), o, s, (NSUInteger)(a))
#define M_V_I(o, s, a) MTL_MSG(void (*)(id, SEL, NSInteger), o, s, (NSInteger)(a))
#define M_V_U64(o, s, a) MTL_MSG(void (*)(id, SEL, uint64_t), o, s, (uint64_t)(a))
#define M_V_B(o, s, a) MTL_MSG(void (*)(id, SEL, bool), o, s, (bool)(a))
#define M_V_F(o, s, a) MTL_MSG(void (*)(id, SEL, float), o, s, (float)(a))
#define M_V_D(o, s, a) MTL_MSG(void (*)(id, SEL, double), o, s, (double)(a))
#define M_V_ID_U(o, s, a, b) MTL_MSG(void (*)(id, SEL, id, NSUInteger), o, s, (id)(a), (NSUInteger)(b))
#define M_V_U_U(o, s, a, b) MTL_MSG(void (*)(id, SEL, NSUInteger, NSUInteger), o, s, (NSUInteger)(a), (NSUInteger)(b))

/* an element of an Objective-C array property, like colorAttachments[i]. */
#define M_AT(o, i) M_ID_U(o, "objectAtIndexedSubscript:", i)

static inline id mtl_new(const char *cls)
{
    return M_ID(M_ID(MTL_CLASS(cls), "alloc"), "init");
}

static inline void mtl_release(id o)
{
    if (o)
        M_V(o, "release");
}

static inline id mtl_retain(id o)
{
    return o ? M_ID(o, "retain") : nullptr;
}

/* an autoreleased string. */
static inline id mtl_string(const char *s)
{
    return MTL_MSG(id (*)(id, SEL, const char *), MTL_CLASS("NSString"), "stringWithUTF8String:", s);
}

static inline const char *mtl_cstring(id s)
{
    return s ? MTL_MSG(const char *(*)(id, SEL), s, "UTF8String") : "";
}

/* an error's description for the log. */
static inline const char *mtl_error_text(id error)
{
    return error ? mtl_cstring(M_ID(error, "localizedDescription")) : "no error";
}

#endif
