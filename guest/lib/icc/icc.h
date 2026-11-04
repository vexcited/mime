#ifndef MIME_LIB_ICC_ICC_H
#define MIME_LIB_ICC_ICC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../alloc.h"

#define ICC_SIG(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
#define ICC_HEADER_SIZE 128
#define ICC_MAX_CHANNELS 15

enum {
    ICC_INTENT_PERCEPTUAL = 0,
    ICC_INTENT_RELATIVE = 1,
    ICC_INTENT_SATURATION = 2,
    ICC_INTENT_ABSOLUTE = 3,
};

/* the errors of icc_xform_create. */
enum {
    ICC_OK = 0,
    ICC_ERR_MEMORY = -1,
    ICC_ERR_PROFILE = -2,    /* a profile is damaged or lacks the tags the transform needs. */
    ICC_ERR_COLORSPACE = -3, /* two profiles that follow each other do not share a color space. */
    ICC_ERR_ARGUMENT = -4,
};

static inline uint32_t icc_be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static inline uint16_t icc_be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline void icc_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
static inline void icc_put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/* ------------------------------------------------------------------ the tagged file */

/* the checks windows makes before it trusts a profile: header, signature, sizes and the tag table. */
bool icc_valid(const uint8_t *data, size_t size);
uint32_t icc_tag_count(const uint8_t *data);
/* the index of a tag in the table, or -1. */
int icc_tag_index(const uint8_t *data, uint32_t sig);
/* the data of a tag, checked against the size of the profile, or NULL. */
const uint8_t *icc_tag(const uint8_t *data, size_t size, uint32_t sig, uint32_t *length);
/* whether the data of the tag at an index is shared with another tag. */
bool icc_tag_shared(const uint8_t *data, uint32_t index);
/* the number of channels of a color space signature, 0 when it is not one. */
uint32_t icc_space_channels(uint32_t space);

/*
 * a profile being edited. the bytes are always a whole profile with the header size field up to date.
 * edits rebuild the tag table and data, shared data stays shared.
 */
typedef struct IccEdit {
    MimeAllocator alloc;
    uint8_t *data;
    uint32_t size;
} IccEdit;

/* a profile with a header and no tags, for files that are created empty. */
bool icc_edit_blank(IccEdit *e);
/* gives a tag a size, adding it zeroed, growing it with zeros or cutting it. a shared tag gets data of its own. */
bool icc_edit_resize(IccEdit *e, uint32_t sig, uint32_t length);
bool icc_edit_remove(IccEdit *e, uint32_t sig);
/* makes sig share the data of ref, replacing what sig had. */
bool icc_edit_reference(IccEdit *e, uint32_t sig, uint32_t ref);
/* writable data of a tag, unshared first. */
uint8_t *icc_edit_tag(IccEdit *e, uint32_t sig, uint32_t *length);

/* ------------------------------------------------------------------ building profiles */

enum {
    ICC_CURVE_SRGB,   /* the srgb curve, as a parametric curve in version 4 and a table of 1024 in version 2. */
    ICC_CURVE_LINEAR,
    ICC_CURVE_GAMMA,  /* a plain power, the gamma in IccRgbSpec. */
    ICC_CURVE_PQ,     /* smpte st 2084, 10000 nits at 1. */
    ICC_CURVE_HLG,
    ICC_CURVE_SRGB_TABLE, /* the srgb curve as a table of 1024, in either version. */
};

typedef struct IccRgbSpec {
    /* chromaticities of the primaries and the white, or colorants already in xyz d50 when xyz is set. */
    double primaries[3][2], white[2];
    const double (*xyz)[3];       /* red, green and blue colorants adapted to d50. */
    const double *media_white;    /* the wtpt tag, d50 when NULL. */
    int curve;
    double gamma;
    int version;                  /* 2 or 4. */
    const char *description;
    uint32_t device_class;        /* 'mntr' when 0. */
} IccRgbSpec;

/* an rgb matrix and curves profile, NULL when out of memory. the size goes to *size. */
uint8_t *icc_build_rgb(const MimeAllocator *a, const IccRgbSpec *spec, uint32_t *size);
/* the srgb profile windows uses for its standard color space, mime's own bytes. */
uint8_t *icc_build_srgb(const MimeAllocator *a, uint32_t *size);

/* ------------------------------------------------------------------ the color engine */

/*
 * device values are floats in 0 to 1, as the profile encodes them. a transform that ends in the profile connection
 * space gives the encoded pcs, lab or xyz, scaled to 0 to 1 the way a version 4 lut sees it.
 */
typedef struct IccXform IccXform;

typedef struct IccSource {
    const uint8_t *data;
    uint32_t size;
} IccSource;

/* xform flags. */
enum {
    ICC_XFORM_PROOF = 1, /* the profile before the last is a proof target, left with the intent of the last. */
};

/*
 * links profiles in order: the first goes from its device to the pcs, the last from the pcs to its device, those in
 * between go to their device and back, as a proof does. a device link profile goes device to device, an abstract
 * one pcs to pcs. intents has one entry per profile, or fewer and the last repeats. the profiles are copied.
 */
IccXform *icc_xform_create(const MimeAllocator *a, const IccSource *profiles, uint32_t count, const uint32_t *intents,
                           uint32_t intent_count, uint32_t flags, int *error);
void icc_xform_destroy(IccXform *x);
uint32_t icc_xform_inputs(const IccXform *x);
uint32_t icc_xform_outputs(const IccXform *x);
/* the color space signatures at both ends. */
uint32_t icc_xform_input_space(const IccXform *x);
uint32_t icc_xform_output_space(const IccXform *x);
/* converts count colors of interleaved floats. outputs are clamped to 0 to 1 unless unclamped is set. */
void icc_xform_eval(const IccXform *x, const float *in, float *out, size_t count, bool unclamped);
/* how far out of the gamut of the last profile each color is: 0 inside, up to 255. */
void icc_xform_gamut(const IccXform *x, const float *in, uint8_t *out, size_t count);
/*
 * converts count pixels of 8 bit channels with tables made on first use, in, out and stride being bytes. the
 * channels of in are inputs() wide, those of out outputs() wide. the tables are made under no lock: call
 * icc_xform_prepare8 first when several threads share the transform.
 */
bool icc_xform_prepare8(IccXform *x);
void icc_xform_eval8(const IccXform *x, const uint8_t *in, size_t in_step, uint8_t *out, size_t out_step, size_t count);
/* whether the transform changes nothing, as from one profile to the same. */
bool icc_xform_identity(const IccXform *x);

/* ------------------------------------------------------------------ helpers */

double icc_pow(double x, double y);
/* the conversions of the pcs, xyz and lab under d50. */
void icc_xyz_to_lab(const double xyz[3], double lab[3]);
void icc_lab_to_xyz(const double lab[3], double xyz[3]);

#endif
