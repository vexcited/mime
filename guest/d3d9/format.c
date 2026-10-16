/*
 * direct3d 9's formats as dxgi's. luminance and the formats without some
 * components read through swizzles, missing color components being 1;
 * depth formats are typeless so shaders may read them, as INTZ, DF24 and
 * DF16 ask, and the 8-bit color formats typeless for their srgb views.
 */
#include "d3d9_private.h"

#define SW(r, g, b, a) MIME_SWIZZLE(MIME_SWIZZLE_##r, MIME_SWIZZLE_##g, MIME_SWIZZLE_##b, MIME_SWIZZLE_##a)
#define C(d3d, res, view, srgb, swizzle, bytes, flags)                                                                 \
    {D3DFMT_##d3d, DXGI_FORMAT_##res, DXGI_FORMAT_##view, DXGI_FORMAT_##srgb, DXGI_FORMAT_UNKNOWN, swizzle, bytes, 1, \
     bytes, flags}
#define BC(d3d, res, view, srgb, bytes)                                                                                \
    {(D3DFORMAT)(d3d), DXGI_FORMAT_##res, DXGI_FORMAT_##view, DXGI_FORMAT_##srgb, DXGI_FORMAT_UNKNOWN, 0, bytes, 4,   \
     bytes, 0}
#define DS(d3d, res, view, depth, swizzle, bytes, flags)                                                               \
    {(D3DFORMAT)(d3d), DXGI_FORMAT_##res, DXGI_FORMAT_##view, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_##depth, swizzle,       \
     bytes, 1, bytes, flags}

#define R D9_FORMAT_RENDER
#define S D9_FORMAT_SRGB
#define F D9_FORMAT_FLOAT
#define D D9_FORMAT_DEPTH
#define ST D9_FORMAT_STENCIL
#define L D9_FORMAT_LOCKABLE

static const D9Format FORMATS[] = {
    C(A8R8G8B8, B8G8R8A8_TYPELESS, B8G8R8A8_UNORM, B8G8R8A8_UNORM_SRGB, 0, 4, R | S),
    C(X8R8G8B8, B8G8R8A8_TYPELESS, B8G8R8A8_UNORM, B8G8R8A8_UNORM_SRGB, SW(R, G, B, ONE), 4, R | S),
    C(A8B8G8R8, R8G8B8A8_TYPELESS, R8G8B8A8_UNORM, R8G8B8A8_UNORM_SRGB, 0, 4, R | S),
    C(X8B8G8R8, R8G8B8A8_TYPELESS, R8G8B8A8_UNORM, R8G8B8A8_UNORM_SRGB, SW(R, G, B, ONE), 4, R | S),
    C(R5G6B5, B5G6R5_UNORM, B5G6R5_UNORM, UNKNOWN, 0, 2, R),
    C(X1R5G5B5, B5G5R5A1_UNORM, B5G5R5A1_UNORM, UNKNOWN, SW(R, G, B, ONE), 2, R),
    C(A1R5G5B5, B5G5R5A1_UNORM, B5G5R5A1_UNORM, UNKNOWN, 0, 2, R),
    C(A4R4G4B4, B4G4R4A4_UNORM, B4G4R4A4_UNORM, UNKNOWN, 0, 2, R),
    C(X4R4G4B4, B4G4R4A4_UNORM, B4G4R4A4_UNORM, UNKNOWN, SW(R, G, B, ONE), 2, R),
    C(A8, A8_UNORM, A8_UNORM, UNKNOWN, 0, 1, R),
    C(A2B10G10R10, R10G10B10A2_UNORM, R10G10B10A2_UNORM, UNKNOWN, 0, 4, R),
    /* blue and red trade places: reads see it right, renderings swap the two. */
    C(A2R10G10B10, R10G10B10A2_UNORM, R10G10B10A2_UNORM, UNKNOWN, SW(B, G, R, A), 4, R),
    C(G16R16, R16G16_UNORM, R16G16_UNORM, UNKNOWN, SW(R, G, ONE, ONE), 4, R),
    C(A16B16G16R16, R16G16B16A16_UNORM, R16G16B16A16_UNORM, UNKNOWN, 0, 8, R),
    C(L8, R8_UNORM, R8_UNORM, UNKNOWN, SW(R, R, R, ONE), 1, R),
    C(A8L8, R8G8_UNORM, R8G8_UNORM, UNKNOWN, SW(R, R, R, G), 2, R),
    {D3DFMT_A4L4, DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN,
     SW(R, R, R, G), 1, 1, 2, D9_FORMAT_A4L4},
    C(L16, R16_UNORM, R16_UNORM, UNKNOWN, SW(R, R, R, ONE), 2, R),
    C(V8U8, R8G8_SNORM, R8G8_SNORM, UNKNOWN, SW(R, G, ONE, ONE), 2, 0),
    C(Q8W8V8U8, R8G8B8A8_SNORM, R8G8B8A8_SNORM, UNKNOWN, 0, 4, 0),
    C(V16U16, R16G16_SNORM, R16G16_SNORM, UNKNOWN, SW(R, G, ONE, ONE), 4, 0),
    C(Q16W16V16U16, R16G16B16A16_SNORM, R16G16B16A16_SNORM, UNKNOWN, 0, 8, 0),
    C(R16F, R16_FLOAT, R16_FLOAT, UNKNOWN, SW(R, ONE, ONE, ONE), 2, R | F),
    C(G16R16F, R16G16_FLOAT, R16G16_FLOAT, UNKNOWN, SW(R, G, ONE, ONE), 4, R | F),
    C(A16B16G16R16F, R16G16B16A16_FLOAT, R16G16B16A16_FLOAT, UNKNOWN, 0, 8, R | F),
    C(R32F, R32_FLOAT, R32_FLOAT, UNKNOWN, SW(R, ONE, ONE, ONE), 4, R | F),
    C(G32R32F, R32G32_FLOAT, R32G32_FLOAT, UNKNOWN, SW(R, G, ONE, ONE), 8, R | F),
    C(A32B32G32R32F, R32G32B32A32_FLOAT, R32G32B32A32_FLOAT, UNKNOWN, 0, 16, R | F),
    BC(D3DFMT_DXT1, BC1_TYPELESS, BC1_UNORM, BC1_UNORM_SRGB, 8),
    BC(D3DFMT_DXT2, BC2_TYPELESS, BC2_UNORM, BC2_UNORM_SRGB, 16),
    BC(D3DFMT_DXT3, BC2_TYPELESS, BC2_UNORM, BC2_UNORM_SRGB, 16),
    BC(D3DFMT_DXT4, BC3_TYPELESS, BC3_UNORM, BC3_UNORM_SRGB, 16),
    BC(D3DFMT_DXT5, BC3_TYPELESS, BC3_UNORM, BC3_UNORM_SRGB, 16),
    BC(MAKEFOURCC('A', 'T', 'I', '1'), BC4_UNORM, BC4_UNORM, UNKNOWN, 8),
    BC(MAKEFOURCC('A', 'T', 'I', '2'), BC5_UNORM, BC5_UNORM, UNKNOWN, 16),
    DS(D3DFMT_D16_LOCKABLE, R16_TYPELESS, R16_UNORM, D16_UNORM, 0, 2, D | L),
    DS(D3DFMT_D16, R16_TYPELESS, R16_UNORM, D16_UNORM, 0, 2, D),
    DS(D3DFMT_D15S1, R24G8_TYPELESS, R24_UNORM_X8_TYPELESS, D24_UNORM_S8_UINT, 0, 4, D | ST),
    DS(D3DFMT_D24S8, R24G8_TYPELESS, R24_UNORM_X8_TYPELESS, D24_UNORM_S8_UINT, 0, 4, D | ST),
    DS(D3DFMT_D24X8, R24G8_TYPELESS, R24_UNORM_X8_TYPELESS, D24_UNORM_S8_UINT, 0, 4, D),
    DS(D3DFMT_D24X4S4, R24G8_TYPELESS, R24_UNORM_X8_TYPELESS, D24_UNORM_S8_UINT, 0, 4, D | ST),
    DS(D3DFMT_D32, R32_TYPELESS, R32_FLOAT, D32_FLOAT, 0, 4, D),
    DS(D3DFMT_D32F_LOCKABLE, R32_TYPELESS, R32_FLOAT, D32_FLOAT, 0, 4, D | L),
    DS(D3DFMT_D32_LOCKABLE, R32_TYPELESS, R32_FLOAT, D32_FLOAT, 0, 4, D | L),
    DS(D3DFMT_D24FS8, R32G8X24_TYPELESS, R32_FLOAT_X8X24_TYPELESS, D32_FLOAT_S8X24_UINT, 0, 8, D | ST),
    /* depth formats of the vendors' shadow and depth reading tricks, read as their depth in every component. */
    DS(MAKEFOURCC('I', 'N', 'T', 'Z'), R24G8_TYPELESS, R24_UNORM_X8_TYPELESS, D24_UNORM_S8_UINT, SW(R, R, R, R), 4,
       D | ST),
    DS(MAKEFOURCC('D', 'F', '2', '4'), R24G8_TYPELESS, R24_UNORM_X8_TYPELESS, D24_UNORM_S8_UINT, SW(R, R, R, R), 4,
       D),
    DS(MAKEFOURCC('D', 'F', '1', '6'), R16_TYPELESS, R16_UNORM, D16_UNORM, SW(R, R, R, R), 2, D),
    {(D3DFORMAT)MAKEFOURCC('N', 'U', 'L', 'L'), DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN,
     DXGI_FORMAT_UNKNOWN, 0, 4, 1, 4, R | D9_FORMAT_NULL},
};

static const D9Format UNKNOWN_FORMAT = {D3DFMT_UNKNOWN};

const D9Format *D9GetFormat(D3DFORMAT format)
{
    for (UINT i = 0; i < RTL_NUMBER_OF(FORMATS); i++)
        if (FORMATS[i].d3d == format)
            return &FORMATS[i];
    return &UNKNOWN_FORMAT;
}

/* the bytes of a row of blocks as programs lock them, rows a multiple of 4 bytes. */
UINT D9FormatPitch(const D9Format *f, UINT width)
{
    UINT blocks = (width + f->block - 1) / f->block;
    return D9_ALIGN(blocks * f->bytes, 4);
}

UINT D9FormatRows(const D9Format *f, UINT height) { return (height + f->block - 1) / f->block; }

void D9ConvertUpload(const D9Format *f, const BYTE *in, UINT in_pitch, BYTE *out, UINT out_pitch, UINT width,
                     UINT rows)
{
    UINT bytes = (width + f->block - 1) / f->block * f->bytes;
    for (UINT y = 0; y < rows; y++) {
        const BYTE *s = in + (SIZE_T)y * in_pitch;
        BYTE *d = out + (SIZE_T)y * out_pitch;
        if (f->flags & D9_FORMAT_A4L4) {
            for (UINT x = 0; x < width; x++) {
                d[2 * x] = (BYTE)((s[x] & 0xF) * 17);
                d[2 * x + 1] = (BYTE)((s[x] >> 4) * 17);
            }
        } else {
            memcpy(d, s, bytes);
        }
    }
}

static WORD half(float f)
{
    UINT bits;
    memcpy(&bits, &f, 4);
    UINT sign = bits >> 16 & 0x8000;
    INT exponent = (INT)(bits >> 23 & 0xFF) - 127 + 15;
    UINT mantissa = bits & 0x7FFFFF;
    if (exponent <= 0)
        return (WORD)sign;
    if (exponent >= 31)
        return (WORD)(sign | 0x7C00);
    return (WORD)(sign | exponent << 10 | (mantissa + 0x1000) >> 13);
}

static UINT bits_of(UINT value8, UINT bits) { return (value8 * ((1u << bits) - 1) + 127) / 255; }

/* a 565 color block of a single color, transparent when DXT1's alpha is under a half. */
static void color_block(BYTE *out, UINT r, UINT g, UINT b, BOOL transparent)
{
    WORD c = (WORD)(bits_of(r, 5) << 11 | bits_of(g, 6) << 5 | bits_of(b, 5));
    memcpy(out, &c, 2);
    memcpy(out + 2, &c, 2);
    memset(out + 4, transparent ? 0xFF : 0, 4);
}

/*
 * a color as a block of the format, as ColorFill writes it: the
 * components scaled to the format's, luminance from the red, green and
 * blue weighted as BT.601 does, a guess. 0 for formats it cannot fill.
 */
UINT D9PackColor(const D9Format *f, D3DCOLOR color, BYTE out[16])
{
    UINT a = color >> 24, r = color >> 16 & 0xFF, g = color >> 8 & 0xFF, b = color & 0xFF;
    UINT l = (r * 299 + g * 587 + b * 114 + 500) / 1000;
    float fr = r / 255.0f, fg = g / 255.0f, fb = b / 255.0f, fa = a / 255.0f;
    UINT v = 0;
    WORD w[4];
    float x[4];
    memset(out, 0, 16);
    switch ((DWORD)f->d3d) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
        v = color;
        break;
    case D3DFMT_A8B8G8R8:
    case D3DFMT_X8B8G8R8:
        v = a << 24 | b << 16 | g << 8 | r;
        break;
    case D3DFMT_R5G6B5:
        v = bits_of(r, 5) << 11 | bits_of(g, 6) << 5 | bits_of(b, 5);
        break;
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
        v = (a >> 7) << 15 | bits_of(r, 5) << 10 | bits_of(g, 5) << 5 | bits_of(b, 5);
        break;
    case D3DFMT_A4R4G4B4:
    case D3DFMT_X4R4G4B4:
        v = bits_of(a, 4) << 12 | bits_of(r, 4) << 8 | bits_of(g, 4) << 4 | bits_of(b, 4);
        break;
    case D3DFMT_A8:
        v = a;
        break;
    case D3DFMT_A2B10G10R10:
        v = bits_of(a, 2) << 30 | bits_of(b, 10) << 20 | bits_of(g, 10) << 10 | bits_of(r, 10);
        break;
    case D3DFMT_A2R10G10B10:
        v = bits_of(a, 2) << 30 | bits_of(r, 10) << 20 | bits_of(g, 10) << 10 | bits_of(b, 10);
        break;
    case D3DFMT_G16R16:
        v = g * 257 << 16 | r * 257;
        break;
    case D3DFMT_A16B16G16R16:
        w[0] = (WORD)(r * 257), w[1] = (WORD)(g * 257), w[2] = (WORD)(b * 257), w[3] = (WORD)(a * 257);
        memcpy(out, w, 8);
        return f->bytes;
    case D3DFMT_L8:
        v = l;
        break;
    case D3DFMT_A8L8:
        v = a << 8 | l;
        break;
    case D3DFMT_A4L4:
        v = bits_of(a, 4) << 4 | bits_of(l, 4);
        break;
    case D3DFMT_L16:
        v = l * 257;
        break;
    case D3DFMT_R16F:
        v = half(fr);
        break;
    case D3DFMT_G16R16F:
        v = (UINT)half(fg) << 16 | half(fr);
        break;
    case D3DFMT_A16B16G16R16F:
        w[0] = half(fr), w[1] = half(fg), w[2] = half(fb), w[3] = half(fa);
        memcpy(out, w, 8);
        return f->bytes;
    case D3DFMT_R32F:
    case D3DFMT_G32R32F:
    case D3DFMT_A32B32G32R32F:
        x[0] = fr, x[1] = fg, x[2] = fb, x[3] = fa;
        memcpy(out, x, f->bytes);
        return f->bytes;
    case D3DFMT_DXT1:
        color_block(out, r, g, b, a < 128);
        return f->bytes;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
        memset(out, (int)(bits_of(a, 4) * 0x11), 8);
        color_block(out + 8, r, g, b, FALSE);
        return f->bytes;
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
        out[0] = out[1] = (BYTE)a;
        color_block(out + 8, r, g, b, FALSE);
        return f->bytes;
    default:
        return 0;
    }
    memcpy(out, &v, f->bytes);
    return f->bytes;
}
