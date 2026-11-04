/*
 * images: bitmaps, effects and command lists as pixels, and the properties of the built in effects, which effects_*.c draw.
 */
#include "effects.h"
#include <float.h>

#define INF_EXTENT D2D_INF_EXTENT

typedef D2dIRect IRect;

static IRect irect_intersect(IRect a, IRect b)
{
    IRect r = {a.x0 > b.x0 ? a.x0 : b.x0, a.y0 > b.y0 ? a.y0 : b.y0, a.x1 < b.x1 ? a.x1 : b.x1, a.y1 < b.y1 ? a.y1 : b.y1};
    return r;
}
static BOOL irect_empty(IRect r) { return r.x1 <= r.x0 || r.y1 <= r.y0; }

void D2dImageDataFree(D2dImageData *d)
{
    D2dSurfaceFree(&d->surface);
    D2dFree(d->fpx);
    d->fpx = nullptr;
}

static HRESULT data_new(D2dImageData *d, IRect r)
{
    d->x = r.x0;
    d->y = r.y0;
    d->fpx = nullptr;
    int w = r.x1 - r.x0, h = r.y1 - r.y0;
    if (w < 0)
        w = 0;
    if (h < 0)
        h = 0;
    return D2dSurfaceInit(&d->surface, w, h) ? S_OK : E_OUTOFMEMORY;
}

/* ------------------------------------------------------------------ command lists */

typedef struct D2dCommandList {
    D2dObj obj;
    D2dSurface surface;
    float dpi;
    BOOL closed;
    IRect dirty;
} D2dCommandList;

static D2dClass g_cmd_class;

HRESULT D2dCommandListSurface(ID2D1CommandList *list, D2dSurface **surface, float *dpi)
{
    if (!D2dIsOfClass(list, &g_cmd_class))
        return E_INVALIDARG;
    D2dCommandList *c = (D2dCommandList *)list;
    if (c->closed)
        return D2DERR_WRONG_STATE;
    *surface = &c->surface;
    *dpi = c->dpi;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cl_stream(ID2D1CommandList *This, ID2D1CommandSink *sink)
{
    D2dCommandList *c = (D2dCommandList *)This;
    if (!sink)
        return E_INVALIDARG;
    if (!c->closed)
        return D2DERR_WRONG_STATE;
    /* the recorded pixels go out as one bitmap, since commands are drawn when they are recorded. */
    D2dBitmap *b;
    HRESULT hr = D2dBitmapCreate(c->obj.factory, (UINT32)c->surface.w, (UINT32)c->surface.h, (DXGI_FORMAT)87, D2D1_ALPHA_MODE_PREMULTIPLIED,
                                 c->dpi, c->dpi, D2D1_BITMAP_OPTIONS_NONE, TRUE, &b);
    if (FAILED(hr))
        return hr;
    memcpy(b->native, c->surface.px, (SIZE_T)c->surface.w * c->surface.h * 4);
    hr = sink->lpVtbl->BeginDraw(sink);
    if (SUCCEEDED(hr))
        hr = sink->lpVtbl->DrawBitmap(sink, (ID2D1Bitmap *)b, nullptr, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR, nullptr, nullptr);
    if (SUCCEEDED(hr))
        hr = sink->lpVtbl->EndDraw(sink);
    D2dRelease(b);
    return hr;
}
static HRESULT STDMETHODCALLTYPE cl_close(ID2D1CommandList *This)
{
    D2dCommandList *c = (D2dCommandList *)This;
    if (c->closed)
        return D2DERR_WRONG_STATE;
    c->closed = TRUE;
    return S_OK;
}
static void cl_destroy(D2dObj *o) { D2dSurfaceFree(&((D2dCommandList *)o)->surface); }
static const void *const g_cmd_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory, cl_stream, cl_close};
D2D_IIDS(g_cmd_iids, &IID_ID2D1Resource, &IID_ID2D1Image, &IID_ID2D1CommandList);
static D2dClass g_cmd_class = {"command list", g_cmd_iids, cl_destroy, nullptr};

HRESULT D2dCreateCommandList(D2dRt *rt, ID2D1CommandList **out)
{
    D2dCommandList *c = D2dNew(sizeof(*c), g_cmd_vtbl, &g_cmd_class, rt->obj.factory);
    if (!c)
        return E_OUTOFMEMORY;
    c->dpi = rt->dpi_x;
    if (!D2dSurfaceInit(&c->surface, 2048, 2048)) {
        D2dRelease(c);
        return E_OUTOFMEMORY;
    }
    *out = (ID2D1CommandList *)c;
    return S_OK;
}

/* ------------------------------------------------------------------ proxies */

typedef struct D2dProxy {
    D2dObj obj;
    const D2dProxyOps *ops;
    void *ctx;
} D2dProxy;

static const void *const g_proxy_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory};
D2D_IIDS(g_proxy_iids, &IID_ID2D1Resource, &IID_ID2D1Image);
static D2dClass g_proxy_class = {"image proxy", g_proxy_iids, nullptr, nullptr};

HRESULT D2dProxyCreate(D2dFactory *f, const D2dProxyOps *ops, void *ctx, ID2D1Image **out)
{
    D2dProxy *p = D2dNew(sizeof(*p), g_proxy_vtbl, &g_proxy_class, f);
    if (!p)
        return E_OUTOFMEMORY;
    p->ops = ops;
    p->ctx = ctx;
    *out = (ID2D1Image *)p;
    return S_OK;
}

/* ------------------------------------------------------------------ effects */

/* a property of a built in effect with its default: floats for floats and vectors, the first one for integers. */
typedef struct EffProp {
    const WCHAR *name;
    D2D1_PROPERTY_TYPE type;
    UINT32 size;
    float def[4];
} EffProp;

typedef struct EffDesc {
    const GUID *clsid;
    const WCHAR *name;
    const WCHAR *category;
    UINT32 min_inputs, max_inputs, default_inputs;
    UINT32 nprops;
    EffProp props[EFF_MAX_PROPS];
} EffDesc;

#define BIG __builtin_inff()
#define P_F(n, d) {L##n, D2D1_PROPERTY_TYPE_FLOAT, 4, {d}}
#define P_V2(n, a, b) {L##n, D2D1_PROPERTY_TYPE_VECTOR2, 8, {a, b}}
#define P_V3(n, a, b, c) {L##n, D2D1_PROPERTY_TYPE_VECTOR3, 12, {a, b, c}}
#define P_V4(n, a, b, c, d) {L##n, D2D1_PROPERTY_TYPE_VECTOR4, 16, {a, b, c, d}}
#define P_E(n, d) {L##n, D2D1_PROPERTY_TYPE_ENUM, 4, {d}}
#define P_U(n, d) {L##n, D2D1_PROPERTY_TYPE_UINT32, 4, {d}}
#define P_B(n) {L##n, D2D1_PROPERTY_TYPE_BOOL, 4, {0}}
#define P_M(n) {L##n, D2D1_PROPERTY_TYPE_MATRIX_3X2, 24, {1}}
#define P_M44(n) {L##n, D2D1_PROPERTY_TYPE_MATRIX_4X4, 64, {1}}
#define P_M5(n) {L##n, D2D1_PROPERTY_TYPE_MATRIX_5X4, 80, {1}}
#define P_CC(n) {L##n, D2D1_PROPERTY_TYPE_COLOR_CONTEXT, sizeof(void *), {0}}
#define P_I(n) {L##n, D2D1_PROPERTY_TYPE_IUNKNOWN, sizeof(void *), {0}}
/* a list of floats, kept on the heap: the default is the identity table or a 3 by 3 kernel. */
#define P_BLOB(n) {L##n, D2D1_PROPERTY_TYPE_BLOB, 0, {0}}
#define TRANSFER(c) P_BLOB(c "Table"), P_B(c "Disable")
#define GAMMA(c) P_F(c "Amplitude", 1), P_F(c "Exponent", 1), P_F(c "Offset", 0), P_B(c "Disable")
#define LINEAR(c) P_F(c "YIntercept", 0), P_F(c "Slope", 1), P_B(c "Disable")
#define P_RECT(n) {L##n, D2D1_PROPERTY_TYPE_VECTOR4, 16, {-BIG, -BIG, BIG, BIG}}
#define P_RECT0(n) {L##n, D2D1_PROPERTY_TYPE_VECTOR4, 16, {0, 0, BIG, BIG}}
#define P_I32(n, d) {L##n, D2D1_PROPERTY_TYPE_INT32, 4, {d}}

static const EffDesc g_effects[EFF_COUNT] = {
    [EFF_FLOOD] = {&CLSID_D2D1Flood, L"Flood", L"Source", 0, 0, 0, 1, {P_V4("Color", 0, 0, 0, 1)}},
    [EFF_COLORMATRIX] = {&CLSID_D2D1ColorMatrix, L"Color Matrix", L"Color", 1, 1, 1, 3, {P_M5("ColorMatrix"), P_E("AlphaMode", 1), P_B("ClampOutput")}},
    [EFF_BLUR] = {&CLSID_D2D1GaussianBlur, L"Gaussian Blur", L"Filter", 1, 1, 1, 3, {P_F("StandardDeviation", 3), P_E("Optimization", 1), P_E("BorderMode", 0)}},
    [EFF_COMPOSITE] = {&CLSID_D2D1Composite, L"Composite", L"Composite", 1, 0xffffffff, 2, 1, {P_E("Mode", 0)}},
    [EFF_CROP] = {&CLSID_D2D1Crop, L"Crop", L"Transform", 1, 1, 1, 2, {P_RECT("Rect"), P_E("BorderMode", 0)}},
    [EFF_AFFINE] = {&CLSID_D2D12DAffineTransform, L"2D Affine Transform", L"Transform", 1, 1, 1, 4,
                    {P_E("InterpolationMode", 1), P_E("BorderMode", 0), P_M("TransformMatrix"), P_F("Sharpness", 0)}},
    [EFF_OPACITY] = {&CLSID_D2D1Opacity, L"Opacity", L"Transparency", 1, 1, 1, 1, {P_F("Opacity", 1)}},
    [EFF_TINT] = {&CLSID_D2D1Tint, L"Tint", L"Color", 1, 1, 1, 2, {P_V4("Color", 1, 1, 1, 1), P_B("ClampOutput")}},
    [EFF_SHADOW] = {&CLSID_D2D1Shadow, L"Shadow", L"Filter", 1, 1, 1, 3, {P_F("BlurStandardDeviation", 3), P_V4("Color", 0, 0, 0, 1), P_E("Optimization", 1)}},
    [EFF_BRIGHTNESS] = {&CLSID_D2D1Brightness, L"Brightness", L"Color", 1, 1, 1, 2, {P_V2("WhitePoint", 1, 1), P_V2("BlackPoint", 0, 0)}},
    [EFF_SATURATION] = {&CLSID_D2D1Saturation, L"Saturation", L"Color", 1, 1, 1, 1, {P_F("Saturation", 0.5f)}},
    [EFF_GRAYSCALE] = {&CLSID_D2D1Grayscale, L"Grayscale", L"Color", 1, 1, 1, 0, {}},
    [EFF_INVERT] = {&CLSID_D2D1Invert, L"Invert", L"Color", 1, 1, 1, 0, {}},
    [EFF_SEPIA] = {&CLSID_D2D1Sepia, L"Sepia", L"Color", 1, 1, 1, 2, {P_F("Intensity", 0.5f), P_E("AlphaMode", 1)}},
    [EFF_PREMULTIPLY] = {&CLSID_D2D1Premultiply, L"Premultiply", L"Color", 1, 1, 1, 0, {}},
    [EFF_UNPREMULTIPLY] = {&CLSID_D2D1UnPremultiply, L"UnPremultiply", L"Color", 1, 1, 1, 0, {}},
    [EFF_BLEND] = {&CLSID_D2D1Blend, L"Blend", L"Composite", 2, 2, 2, 1, {P_E("Mode", 0)}},
    [EFF_BORDER] = {&CLSID_D2D1Border, L"Border", L"Transform", 1, 1, 1, 2, {P_E("EdgeModeX", 0), P_E("EdgeModeY", 0)}},
    [EFF_ARITHMETIC] = {&CLSID_D2D1ArithmeticComposite, L"Arithmetic Composite", L"Composite", 2, 2, 2, 2,
                        {P_V4("Coefficients", 1, 0, 0, 0), P_B("ClampOutput")}},
    [EFF_HUEROTATE] = {&CLSID_D2D1HueRotation, L"Hue Rotation", L"Color", 1, 1, 1, 1, {P_F("Angle", 0)}},
    [EFF_CONTRAST] = {&CLSID_D2D1Contrast, L"Contrast", L"Photo", 1, 1, 1, 2, {P_F("Contrast", 0), P_B("ClampInput")}},
    [EFF_EXPOSURE] = {&CLSID_D2D1Exposure, L"Exposure", L"Photo", 1, 1, 1, 1, {P_F("ExposureValue", 0)}},
    [EFF_LUMINANCE] = {&CLSID_D2D1LuminanceToAlpha, L"Luminance to Alpha", L"Color", 1, 1, 1, 0, {}},
    [EFF_TILE] = {&CLSID_D2D1Tile, L"Tile", L"Transform", 1, 1, 1, 1, {P_V4("Rect", 0, 0, 100, 100)}},
    [EFF_COLORMANAGEMENT] = {&CLSID_D2D1ColorManagement, L"Color Management", L"Color", 1, 1, 1, 6,
                             {P_CC("SourceColorContext"), P_E("SourceRenderingIntent", 0), P_CC("DestinationColorContext"),
                              P_E("DestinationRenderingIntent", 0), P_E("AlphaMode", 1), P_E("Quality", 1)}},
    [EFF_3DTRANSFORM] = {&CLSID_D2D13DTransform, L"3D Transform", L"Transform", 1, 1, 1, 3,
                         {P_E("InterpolationMode", 1), P_E("BorderMode", 0), P_M44("TransformMatrix")}},
    [EFF_3DPERSPECTIVE] = {&CLSID_D2D13DPerspectiveTransform, L"3D Perspective Transform", L"Transform", 1, 1, 1, 8,
                           {P_E("InterpolationMode", 1), P_E("BorderMode", 0), P_F("Depth", 1000), P_V2("PerspectiveOrigin", 0, 0),
                            P_V3("LocalOffset", 0, 0, 0), P_V3("GlobalOffset", 0, 0, 0), P_V3("RotationOrigin", 0, 0, 0),
                            P_V3("Rotation", 0, 0, 0)}},
    [EFF_ATLAS] = {&CLSID_D2D1Atlas, L"Atlas", L"Transform", 1, 1, 1, 2, {P_RECT0("InputRect"), P_RECT0("InputPaddingRect")}},
    [EFF_BITMAPSOURCE] = {&CLSID_D2D1BitmapSource, L"Bitmap Source", L"Source", 0, 0, 0, 6,
                          {P_I("WicBitmapSource"), P_V2("Scale", 1, 1), P_E("InterpolationMode", 1), P_B("EnableDPICorrection"),
                           P_E("AlphaMode", 1), P_E("Orientation", 1)}},
    [EFF_CONVOLVE] = {&CLSID_D2D1ConvolveMatrix, L"Convolve Matrix", L"Filter", 1, 1, 1, 11,
                      {P_V2("KernelUnitLength", 1, 1), P_E("ScaleMode", 1), P_U("KernelSizeX", 3), P_U("KernelSizeY", 3), P_BLOB("KernelMatrix"),
                       P_F("Divisor", 1), P_F("Bias", 0), P_V2("KernelOffset", 0, 0), P_B("PreserveAlpha"), P_E("BorderMode", 0),
                       P_B("ClampOutput")}},
    [EFF_DIRECTIONALBLUR] = {&CLSID_D2D1DirectionalBlur, L"Directional Blur", L"Filter", 1, 1, 1, 4,
                             {P_F("StandardDeviation", 3), P_F("Angle", 0), P_E("Optimization", 1), P_E("BorderMode", 0)}},
    [EFF_DISCRETETRANSFER] = {&CLSID_D2D1DiscreteTransfer, L"Discrete Transfer", L"Color", 1, 1, 1, 9,
                              {TRANSFER("Red"), TRANSFER("Green"), TRANSFER("Blue"), TRANSFER("Alpha"), P_B("ClampOutput")}},
    [EFF_DISPLACEMENT] = {&CLSID_D2D1DisplacementMap, L"Displacement Map", L"Stylize", 2, 2, 2, 3,
                          {P_F("Scale", 0), P_E("XChannelSelect", 3), P_E("YChannelSelect", 3)}},
    [EFF_DISTANTDIFFUSE] = {&CLSID_D2D1DistantDiffuse, L"Distant-Diffuse", L"Lighting", 1, 1, 1, 7,
                            {P_F("Azimuth", 0), P_F("Elevation", 0), P_F("DiffuseConstant", 1), P_F("SurfaceScale", 1),
                             P_V3("Color", 1, 1, 1), P_V2("KernelUnitLength", 1, 1), P_E("ScaleMode", 1)}},
    [EFF_DISTANTSPECULAR] = {&CLSID_D2D1DistantSpecular, L"Distant-Specular", L"Lighting", 1, 1, 1, 8,
                             {P_F("Azimuth", 0), P_F("Elevation", 0), P_F("SpecularExponent", 1), P_F("SpecularConstant", 1),
                              P_F("SurfaceScale", 1), P_V3("Color", 1, 1, 1), P_V2("KernelUnitLength", 1, 1), P_E("ScaleMode", 1)}},
    [EFF_DPICOMPENSATION] = {&CLSID_D2D1DpiCompensation, L"DPI Compensation", L"Transform", 1, 1, 1, 3,
                             {P_E("InterpolationMode", 1), P_E("BorderMode", 0), P_V2("InputDpi", 96, 96)}},
    [EFF_GAMMATRANSFER] = {&CLSID_D2D1GammaTransfer, L"Gamma Transfer", L"Color", 1, 1, 1, 17,
                           {GAMMA("Red"), GAMMA("Green"), GAMMA("Blue"), GAMMA("Alpha"), P_B("ClampOutput")}},
    [EFF_SCALE] = {&CLSID_D2D1Scale, L"Scale", L"Transform", 1, 1, 1, 5,
                   {P_V2("Scale", 1, 1), P_V2("CenterPoint", 0, 0), P_E("InterpolationMode", 1), P_E("BorderMode", 0), P_F("Sharpness", 0)}},
    [EFF_HISTOGRAM] = {&CLSID_D2D1Histogram, L"Histogram", L"Analysis", 1, 1, 1, 3,
                       {P_U("NumBins", 256), P_E("ChannelSelect", 0), P_BLOB("HistogramOutput")}},
    [EFF_LINEARTRANSFER] = {&CLSID_D2D1LinearTransfer, L"Linear Transfer", L"Color", 1, 1, 1, 13,
                            {LINEAR("Red"), LINEAR("Green"), LINEAR("Blue"), LINEAR("Alpha"), P_B("ClampOutput")}},
    [EFF_MORPHOLOGY] = {&CLSID_D2D1Morphology, L"Morphology", L"Filter", 1, 1, 1, 3, {P_E("Mode", 0), P_U("Width", 1), P_U("Height", 1)}},
    [EFF_OPACITYMETADATA] = {&CLSID_D2D1OpacityMetadata, L"Opacity Metadata", L"Color", 1, 1, 1, 1, {P_RECT("InputOpaqueRect")}},
    [EFF_POINTDIFFUSE] = {&CLSID_D2D1PointDiffuse, L"Point-Diffuse", L"Lighting", 1, 1, 1, 6,
                          {P_V3("LightPosition", 0, 0, 0), P_F("DiffuseConstant", 1), P_F("SurfaceScale", 1), P_V3("Color", 1, 1, 1),
                           P_V2("KernelUnitLength", 1, 1), P_E("ScaleMode", 1)}},
    [EFF_POINTSPECULAR] = {&CLSID_D2D1PointSpecular, L"Point-Specular", L"Lighting", 1, 1, 1, 7,
                           {P_V3("LightPosition", 0, 0, 0), P_F("SpecularExponent", 1), P_F("SpecularConstant", 1), P_F("SurfaceScale", 1),
                            P_V3("Color", 1, 1, 1), P_V2("KernelUnitLength", 1, 1), P_E("ScaleMode", 1)}},
    [EFF_SPOTDIFFUSE] = {&CLSID_D2D1SpotDiffuse, L"Spot-Diffuse", L"Lighting", 1, 1, 1, 9,
                         {P_V3("LightPosition", 0, 0, 0), P_V3("PointsAt", 0, 0, 0), P_F("Focus", 1), P_F("LimitingConeAngle", 90),
                          P_F("DiffuseConstant", 1), P_F("SurfaceScale", 1), P_V3("Color", 1, 1, 1), P_V2("KernelUnitLength", 1, 1),
                          P_E("ScaleMode", 1)}},
    [EFF_SPOTSPECULAR] = {&CLSID_D2D1SpotSpecular, L"Spot-Specular", L"Lighting", 1, 1, 1, 10,
                          {P_V3("LightPosition", 0, 0, 0), P_V3("PointsAt", 0, 0, 0), P_F("Focus", 1), P_F("LimitingConeAngle", 90),
                           P_F("SpecularExponent", 1), P_F("SpecularConstant", 1), P_F("SurfaceScale", 1), P_V3("Color", 1, 1, 1),
                           P_V2("KernelUnitLength", 1, 1), P_E("ScaleMode", 1)}},
    [EFF_TABLETRANSFER] = {&CLSID_D2D1TableTransfer, L"Table Transfer", L"Color", 1, 1, 1, 9,
                           {TRANSFER("Red"), TRANSFER("Green"), TRANSFER("Blue"), TRANSFER("Alpha"), P_B("ClampOutput")}},
    [EFF_TURBULENCE] = {&CLSID_D2D1Turbulence, L"Turbulence", L"Source", 0, 0, 0, 7,
                        {P_V2("Offset", 0, 0), P_V2("Size", 512, 512), P_V2("BaseFrequency", 0.01f, 0.01f), P_U("NumOctaves", 1), P_I32("Seed", 0),
                         P_E("Noise", 0), P_B("Stitchable")}},
    [EFF_YCBCR] = {&CLSID_D2D1YCbCr, L"YCbCr", L"Source", 2, 2, 2, 3, {P_E("ChromaSubsampling", 0), P_M("TransformMatrix"), P_E("InterpolationMode", 1)}},
    [EFF_RGBTOHUE] = {&CLSID_D2D1RgbToHue, L"RgbToHue", L"Color", 1, 1, 1, 1, {P_E("OutputColorSpace", 0)}},
    [EFF_HUETORGB] = {&CLSID_D2D1HueToRgb, L"HueToRgb", L"Color", 1, 1, 1, 1, {P_E("InputColorSpace", 0)}},
    [EFF_CHROMAKEY] = {&CLSID_D2D1ChromaKey, L"Chroma Key", L"Color", 1, 1, 1, 4,
                       {P_V3("Color", 0, 0, 0), P_F("Tolerance", 0.1f), P_B("InvertAlpha"), P_B("Feather")}},
    [EFF_EMBOSS] = {&CLSID_D2D1Emboss, L"Emboss", L"Stylize", 1, 1, 1, 2, {P_F("Height", 1), P_F("Direction", 0)}},
    [EFF_POSTERIZE] = {&CLSID_D2D1Posterize, L"Posterize", L"Stylize", 1, 1, 1, 3,
                       {P_U("RedValueCount", 4), P_U("GreenValueCount", 4), P_U("BlueValueCount", 4)}},
    [EFF_SHARPEN] = {&CLSID_D2D1Sharpen, L"Sharpen", L"Filter", 1, 1, 1, 2, {P_F("Sharpness", 0), P_F("Threshold", 0)}},
    [EFF_STRAIGHTEN] = {&CLSID_D2D1Straighten, L"Straighten", L"Photo", 1, 1, 1, 3, {P_F("Angle", 0), P_B("MaintainSize"), P_E("ScaleMode", 1)}},
    [EFF_TEMPERATURETINT] = {&CLSID_D2D1TemperatureTint, L"Temperature And Tint", L"Photo", 1, 1, 1, 2, {P_F("Temperature", 0), P_F("Tint", 0)}},
    [EFF_VIGNETTE] = {&CLSID_D2D1Vignette, L"Vignette", L"Stylize", 1, 1, 1, 3,
                      {P_V4("Color", 0, 0, 0, 1), P_F("TransitionSize", 0.1f), P_F("Strength", 0.5f)}},
    [EFF_EDGEDETECTION] = {&CLSID_D2D1EdgeDetection, L"Edge Detection", L"Filter", 1, 1, 1, 5,
                           {P_F("Strength", 0.5f), P_F("BlurRadius", 0), P_E("Mode", 0), P_B("OverlayEdges"), P_E("AlphaMode", 1)}},
    [EFF_HIGHLIGHTSSHADOWS] = {&CLSID_D2D1HighlightsShadows, L"Highlights and Shadows", L"Photo", 1, 1, 1, 5,
                               {P_F("Highlights", 0), P_F("Shadows", 0), P_F("Clarity", 0), P_E("InputGamma", 1), P_F("MaskBlurRadius", 1.25f)}},
    [EFF_LOOKUPTABLE3D] = {&CLSID_D2D1LookupTable3D, L"Lookup Table 3D", L"Color", 1, 1, 1, 2, {P_I("Lut"), P_E("AlphaMode", 1)}},
    [EFF_ALPHAMASK] = {&CLSID_D2D1AlphaMask, L"Alpha Mask", L"Composite", 2, 2, 2, 0, {}},
    [EFF_CROSSFADE] = {&CLSID_D2D1CrossFade, L"Cross Fade", L"Composite", 2, 2, 2, 1, {P_F("Weight", 0.5f)}},
    [EFF_WHITELEVEL] = {&CLSID_D2D1WhiteLevelAdjustment, L"White Level Adjustment", L"Color", 1, 1, 1, 2,
                        {P_F("InputWhiteLevel", 80), P_F("OutputWhiteLevel", 80)}},
    [EFF_HDRTONEMAP] = {&CLSID_D2D1HdrToneMap, L"HDR Tone Map", L"Color", 1, 1, 1, 3,
                        {P_F("InputMaxLuminance", 4000), P_F("OutputMaxLuminance", 300), P_E("DisplayMode", 0)}},
};

static D2dClass g_effect_class;

static float *pf(D2dEffect *e, int i) { return (float *)e->props[i]; }
static UINT32 *pu(D2dEffect *e, int i) { return (UINT32 *)e->props[i]; }
static EffBlob *pb(D2dEffect *e, int i) { return (EffBlob *)e->props[i]; }

static BOOL blob_set(EffBlob *b, const void *data, UINT32 size)
{
    BYTE *copy = D2dAlloc(size);
    if (!copy)
        return FALSE;
    memcpy(copy, data, size);
    D2dFree(b->data);
    b->data = copy;
    b->size = size;
    return TRUE;
}

static void effect_defaults(D2dEffect *e)
{
    static const float table[2] = {0, 1}, kernel[9] = {0, 0, 0, 0, 1, 0, 0, 0, 0};
    const EffDesc *d = &g_effects[e->id];
    for (UINT32 i = 0; i < d->nprops; i++) {
        const EffProp *p = &d->props[i];
        switch (p->type) {
        case D2D1_PROPERTY_TYPE_FLOAT:
        case D2D1_PROPERTY_TYPE_VECTOR2:
        case D2D1_PROPERTY_TYPE_VECTOR3:
        case D2D1_PROPERTY_TYPE_VECTOR4:
            memcpy(e->props[i], p->def, p->size);
            break;
        case D2D1_PROPERTY_TYPE_ENUM:
        case D2D1_PROPERTY_TYPE_UINT32:
        case D2D1_PROPERTY_TYPE_INT32:
            *pu(e, (int)i) = (UINT32)p->def[0];
            break;
        case D2D1_PROPERTY_TYPE_MATRIX_3X2:
            pf(e, (int)i)[0] = pf(e, (int)i)[3] = 1.0f;
            break;
        case D2D1_PROPERTY_TYPE_MATRIX_4X4:
            for (int k = 0; k < 4; k++)
                pf(e, (int)i)[k * 5] = 1.0f;
            break;
        case D2D1_PROPERTY_TYPE_MATRIX_5X4:
            for (int k = 0; k < 4; k++)
                pf(e, (int)i)[k * 5] = 1.0f;
            break;
        case D2D1_PROPERTY_TYPE_BLOB:
            if (wcscmp(p->name, L"KernelMatrix") == 0)
                blob_set(pb(e, (int)i), kernel, sizeof(kernel));
            else if (wcscmp(p->name, L"HistogramOutput") != 0)
                blob_set(pb(e, (int)i), table, sizeof(table));
            break;
        default:
            break;
        }
    }
}

/* ------------------------------------------------------------------ image access */


static HRESULT image_get(D2dRt *rt, ID2D1Image *image, IRect want, D2dImageData *out, int depth);
static HRESULT image_bounds(D2dRt *rt, ID2D1Image *image, IRect *b, int depth);

/*
 * an effect is its own output: its ID2D1Image interface is a second vtable right after the object header, with the
 * reference count of the effect, as windows' effects answer QueryInterface for ID2D1Image.
 */
static HRESULT STDMETHODCALLTYPE ei_query(void *This, REFIID riid, void **out) { return D2dQueryInterface(D2dEffectFromImage(This), riid, out); }
static ULONG STDMETHODCALLTYPE ei_addref(void *This) { return D2dAddRef(D2dEffectFromImage(This)); }
static ULONG STDMETHODCALLTYPE ei_release(void *This) { return D2dRelease(D2dEffectFromImage(This)); }
static void STDMETHODCALLTYPE ei_factory(void *This, ID2D1Factory **f) { D2dResourceGetFactory(D2dEffectFromImage(This), f); }
const void *const d2d_effect_image_vtbl[] = {ei_query, ei_addref, ei_release, ei_factory};

HRESULT D2dEffectQueryImage(D2dObj *obj, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_ID2D1Image) || IsEqualGUID(riid, &IID_ID2D1Resource)) {
        *out = (BYTE *)obj + sizeof(D2dObj);
        D2dAddRef(obj);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static D2dEffect *effect_of(void *p)
{
    if (D2dIsOfClass(p, &g_effect_class))
        return (D2dEffect *)p;
    void *e = D2dEffectFromImage(p);
    if (D2dIsOfClass(e, &g_effect_class))
        return e;
    return nullptr;
}

/* an ID2D1Effect of this library, built in or custom. */
BOOL D2dIsEffect(const void *p) { return D2dIsOfClass(p, &g_effect_class) || (p && D2dCustomOf(p) == p); }

static HRESULT bitmap_data(D2dRt *rt, D2dBitmap *b, IRect want, D2dImageData *out)
{
    const D2dSurface *v = D2dBitmapView(b);
    if (!v)
        return E_OUTOFMEMORY;
    float sx = rt->dpi_x / b->dpi_x, sy = rt->dpi_y / b->dpi_y;
    int w = sx == 1.0f ? v->w : (int)(v->w * sx + 0.5f), h = sy == 1.0f ? v->h : (int)(v->h * sy + 0.5f);
    IRect r = {0, 0, w, h};
    if (w == v->w && h == v->h) {
        /* only the part asked for, so that effects working in bands copy what they read. */
        r = irect_intersect(r, want);
        if (irect_empty(r))
            r = (IRect){0, 0, 0, 0};
        HRESULT hr = data_new(out, r);
        if (FAILED(hr))
            return hr;
        for (int y = r.y0; y < r.y1; y++)
            memcpy(D2dSurfaceRow(&out->surface, y - r.y0), D2dSurfaceRow(v, y) + r.x0, (SIZE_T)(r.x1 - r.x0) * 4);
        return S_OK;
    }
    HRESULT hr = data_new(out, r);
    if (FAILED(hr))
        return hr;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float u = (x + 0.5f) / sx - 0.5f, vv = (y + 0.5f) / sy - 0.5f;
            int i0 = (int)D2dFloorF(u), j0 = (int)D2dFloorF(vv);
            float fu = u - i0, fv = vv - j0;
            int i1 = i0 + 1, j1 = j0 + 1;
            i0 = i0 < 0 ? 0 : (i0 >= v->w ? v->w - 1 : i0);
            i1 = i1 < 0 ? 0 : (i1 >= v->w ? v->w - 1 : i1);
            j0 = j0 < 0 ? 0 : (j0 >= v->h ? v->h - 1 : j0);
            j1 = j1 < 0 ? 0 : (j1 >= v->h ? v->h - 1 : j1);
            uint32_t p[4] = {v->px[(SIZE_T)j0 * v->w + i0], v->px[(SIZE_T)j0 * v->w + i1], v->px[(SIZE_T)j1 * v->w + i0], v->px[(SIZE_T)j1 * v->w + i1]};
            uint32_t o = 0;
            for (int s = 0; s < 32; s += 8) {
                float t = ((p[0] >> s) & 255) * (1 - fu) * (1 - fv) + ((p[1] >> s) & 255) * fu * (1 - fv) + ((p[2] >> s) & 255) * (1 - fu) * fv + ((p[3] >> s) & 255) * fu * fv;
                o |= (uint32_t)(t + 0.5f) << s;
            }
            D2dSurfaceRow(&out->surface, y)[x] = o;
        }
    }
    return S_OK;
}

/* copies the part of src that falls into want, with transparent edges. */
static HRESULT crop_data(const D2dImageData *src, IRect want, D2dImageData *out)
{
    HRESULT hr = data_new(out, want);
    if (FAILED(hr))
        return hr;
    if (src->fpx && out->surface.w > 0 && out->surface.h > 0) {
        out->fpx = D2dAllocZero((SIZE_T)out->surface.w * out->surface.h * 16);
        if (!out->fpx)
            return E_OUTOFMEMORY;
    }
    for (int y = want.y0; y < want.y1; y++) {
        int sy = y - src->y;
        if (sy < 0 || sy >= src->surface.h)
            continue;
        int x0 = want.x0 > src->x ? want.x0 : src->x, x1 = want.x1 < src->x + src->surface.w ? want.x1 : src->x + src->surface.w;
        if (x1 <= x0)
            continue;
        memcpy(D2dSurfaceRow(&out->surface, y - want.y0) + (x0 - want.x0), D2dSurfaceRow(&src->surface, sy) + (x0 - src->x), (SIZE_T)(x1 - x0) * 4);
        if (out->fpx)
            memcpy(out->fpx + ((SIZE_T)(y - want.y0) * out->surface.w + (x0 - want.x0)) * 4,
                   src->fpx + ((SIZE_T)sy * src->surface.w + (x0 - src->x)) * 4, (SIZE_T)(x1 - x0) * 16);
    }
    return S_OK;
}

/* an input of an effect over an area, or transparent black for a missing one. */
static HRESULT input_area(D2dRt *rt, D2dEffect *e, UINT32 i, IRect want, D2dImageData *out, int depth)
{
    if (i >= e->input_count || !e->inputs[i]) {
        return data_new(out, want);
    }
    D2dImageData raw = {};
    HRESULT hr = image_get(rt, e->inputs[i], want, &raw, depth + 1);
    if (FAILED(hr))
        return hr;
    hr = crop_data(&raw, want, out);
    D2dImageDataFree(&raw);
    return hr;
}


static HRESULT effect_render(D2dRt *rt, D2dEffect *e, IRect want, D2dImageData *out, int depth);

static HRESULT effect_bounds(D2dRt *rt, D2dEffect *e, IRect *b, int depth)
{
    if (e->id != EFF_FLOOD && e->id != EFF_COLORMANAGEMENT)
        return D2dFxBounds(rt, e, b, depth);
    IRect in = {0, 0, 0, 0};
    if (e->id == EFF_FLOOD) {
        in.x0 = in.y0 = -INF_EXTENT;
        in.x1 = in.y1 = INF_EXTENT;
    } else if (e->input_count && e->inputs[0]) {
        image_bounds(rt, e->inputs[0], &in, depth + 1);
    }
    *b = in;
    return S_OK;
}

static HRESULT image_bounds(D2dRt *rt, ID2D1Image *image, IRect *b, int depth)
{
    if (depth > 16)
        return D2DERR_CYCLIC_GRAPH;
    D2dBitmap *bm = D2dBitmapOf(image);
    if (bm) {
        b->x0 = b->y0 = 0;
        b->x1 = (int)(bm->width * rt->dpi_x / bm->dpi_x + 0.5f);
        b->y1 = (int)(bm->height * rt->dpi_y / bm->dpi_y + 0.5f);
        return S_OK;
    }
    D2dEffect *e = effect_of(image);
    if (e)
        return effect_bounds(rt, e, b, depth);
    if (D2dCustomOf(image))
        return D2dCustomBounds(rt, image, b, depth);
    if (D2dIsOfClass(image, &g_proxy_class)) {
        D2dProxy *p = (D2dProxy *)image;
        return p->ops->bounds(p->ctx, b, depth);
    }
    if (D2dIsOfClass(image, &g_cmd_class)) {
        D2dCommandList *c = (D2dCommandList *)image;
        b->x0 = b->y0 = 0;
        b->x1 = c->surface.w;
        b->y1 = c->surface.h;
        return S_OK;
    }
    return E_INVALIDARG;
}

static HRESULT image_get(D2dRt *rt, ID2D1Image *image, IRect want, D2dImageData *out, int depth)
{
    memset(out, 0, sizeof(*out));
    if (depth > 16)
        return D2DERR_CYCLIC_GRAPH;
    D2dBitmap *bm = D2dBitmapOf(image);
    if (bm)
        return bitmap_data(rt, bm, want, out);
    D2dEffect *e = effect_of(image);
    if (e)
        return effect_render(rt, e, want, out, depth);
    if (D2dCustomOf(image))
        return D2dCustomRender(rt, image, want, out, depth);
    if (D2dIsOfClass(image, &g_proxy_class)) {
        D2dProxy *p = (D2dProxy *)image;
        return p->ops->get(p->ctx, want, out, depth);
    }
    if (D2dIsOfClass(image, &g_cmd_class)) {
        D2dCommandList *c = (D2dCommandList *)image;
        IRect r = {0, 0, c->surface.w, c->surface.h};
        HRESULT hr = data_new(out, r);
        if (SUCCEEDED(hr))
            memcpy(out->surface.px, c->surface.px, (SIZE_T)r.x1 * r.y1 * 4);
        return hr;
    }
    return E_INVALIDARG;
}

HRESULT D2dImageGet(D2dRt *rt, ID2D1Image *image, D2dIRect want, D2dImageData *out, int depth)
{
    return image_get(rt, image, want, out, depth);
}

HRESULT D2dImageBoundsPx(D2dRt *rt, ID2D1Image *image, D2dIRect *b, int depth)
{
    return image_bounds(rt, image, b, depth);
}

HRESULT D2dRenderImage(D2dRt *rt, ID2D1Image *image, D2dImageData *out)
{
    IRect want = {0, 0, rt->draw ? rt->draw->w : 0, rt->draw ? rt->draw->h : 0};
    IRect b;
    if (SUCCEEDED(image_bounds(rt, image, &b, 0)))
        want = D2dBitmapOf(image) ? b : irect_intersect(b, (IRect){want.x0 - 64, want.y0 - 64, want.x1 + 64, want.y1 + 64});
    return image_get(rt, image, want, out, 0);
}

HRESULT D2dImageBounds(D2dRt *rt, ID2D1Image *image, D2D1_RECT_F *bounds)
{
    IRect b;
    HRESULT hr = image_bounds(rt, image, &b, 0);
    if (FAILED(hr))
        return hr;
    float inv = 96.0f / rt->dpi_x, invy = 96.0f / rt->dpi_y;
    if (b.x0 <= -INF_EXTENT) {
        bounds->left = bounds->top = -FLT_MAX;
        bounds->right = bounds->bottom = FLT_MAX;
        return S_OK;
    }
    bounds->left = b.x0 * inv;
    bounds->right = b.x1 * inv;
    bounds->top = b.y0 * invy;
    bounds->bottom = b.y1 * invy;
    return S_OK;
}

static HRESULT effect_render(D2dRt *rt, D2dEffect *e, IRect want, D2dImageData *out, int depth)
{
    if (e->id != EFF_FLOOD && e->id != EFF_COLORMANAGEMENT)
        return D2dFxRender(rt, e, want, out, depth);
    IRect natural;
    effect_bounds(rt, e, &natural, depth);
    IRect area = irect_intersect(want, natural);
    if (irect_empty(area))
        area = (IRect){want.x0, want.y0, want.x0, want.y0};
    HRESULT hr = S_OK;
    switch (e->id) {
    case EFF_FLOOD: {
        hr = data_new(out, area);
        if (FAILED(hr))
            break;
        const float *c = pf(e, 0);
        uint32_t px = D2dPackPremul(c[0], c[1], c[2], c[3]);
        for (int i = 0; i < out->surface.w * out->surface.h; i++)
            out->surface.px[i] = px;
        break;
    }
    case EFF_COLORMANAGEMENT: {
        D2dImageData in;
        hr = input_area(rt, e, 0, area, &in, depth);
        if (SUCCEEDED(hr)) {
            D2dColorManage(*(ID2D1ColorContext **)e->props[0], *pu(e, 1), *(ID2D1ColorContext **)e->props[2], *pu(e, 3),
                           *pu(e, 4), &in.surface);
            *out = in;
        }
        break;
    }
    default:
        break;
    }
    return hr;
}

/* ------------------------------------------------------------------ drawing images */

void D2dDrawImageCore(D2dRt *rt, ID2D1Image *image, const D2D1_POINT_2F *offset, const D2D1_RECT_F *source,
                      D2D1_INTERPOLATION_MODE mode, UINT32 composite)
{
    if (D2dClipIsEmpty(rt))
        return;
    if (!rt->draw) {
        rt->error = rt->error ? rt->error : D2DERR_WRONG_STATE;
        return;
    }
    D2dEffect *eff = effect_of(image);
    if (!eff && !D2dBitmapOf(image) && !D2dIsOfClass(image, &g_cmd_class) && !D2dCustomOf(image) &&
        !D2dIsOfClass(image, &g_proxy_class)) {
        rt->error = rt->error ? rt->error : E_INVALIDARG;
        return;
    }
    float sx = rt->dpi_x / 96.0f, sy = rt->dpi_y / 96.0f;
    IRect bounds;
    HRESULT hr = image_bounds(rt, image, &bounds, 0);
    if (FAILED(hr)) {
        rt->error = rt->error ? rt->error : hr;
        return;
    }
    IRect src = bounds;
    D2D1_POINT_2F topleft;
    if (source) {
        IRect s = {(int)D2dFloorF(source->left * sx), (int)D2dFloorF(source->top * sy), (int)(source->right * sx + 0.999f),
                   (int)(source->bottom * sy + 0.999f)};
        src = irect_intersect(bounds, s);
        topleft.x = source->left;
        topleft.y = source->top;
    } else {
        topleft.x = topleft.y = 0.0f;
    }
    if (irect_empty(src))
        return;
    /* the area worth rendering: what the target can show of the source, mapped back into image space. */
    D2dMatrix world, inv;
    D2dWorldMatrix(rt, &world);
    D2D1_POINT_2F off = offset ? *offset : (D2D1_POINT_2F){0, 0};
    D2dMatrix px_to_user = {1.0f / sx, 0, 0, 1.0f / sy, off.x - topleft.x, off.y - topleft.y};
    D2dMatrix px_to_dev;
    D2dMatMul(&px_to_dev, &px_to_user, &world);
    IRect want = src;
    if (D2dMatInvert(&inv, &px_to_dev)) {
        D2D1_POINT_2F c[4] = {{0, 0}, {(float)rt->draw->w, 0}, {(float)rt->draw->w, (float)rt->draw->h}, {0, (float)rt->draw->h}};
        float x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX;
        for (int i = 0; i < 4; i++) {
            D2D1_POINT_2F p = D2dMatApply(&inv, c[i].x, c[i].y);
            x0 = D2dMinF(x0, p.x); y0 = D2dMinF(y0, p.y); x1 = D2dMaxF(x1, p.x); y1 = D2dMaxF(y1, p.y);
        }
        IRect t = {(int)D2dFloorF(x0) - 2, (int)D2dFloorF(y0) - 2, (int)(x1 + 0.999f) + 2, (int)(y1 + 0.999f) + 2};
        want = irect_intersect(src, t);
    }
    if (irect_empty(want))
        return;
    D2dImageData data;
    hr = image_get(rt, image, want, &data, 0);
    if (FAILED(hr)) {
        rt->error = rt->error ? rt->error : hr;
        return;
    }
    /* the pixels cut to the source rectangle, drawn through the transform of the target. */
    IRect avail = {data.x, data.y, data.x + data.surface.w, data.y + data.surface.h};
    IRect use = irect_intersect(avail, src);
    if (!irect_empty(use)) {
        MimeVecRect clamp = {use.x0 - data.x, use.y0 - data.y, use.x1 - data.x, use.y1 - data.y};
        D2dMatrix tex_to_dev = {px_to_dev._11, px_to_dev._12, px_to_dev._21, px_to_dev._22,
                                px_to_dev._31 + data.x * px_to_dev._11 + data.y * px_to_dev._21,
                                px_to_dev._32 + data.x * px_to_dev._12 + data.y * px_to_dev._22};
        BOOL aligned = D2dAbsF(tex_to_dev._11 - 1) < 1e-6f && D2dAbsF(tex_to_dev._22 - 1) < 1e-6f && tex_to_dev._12 == 0 && tex_to_dev._21 == 0 &&
                       D2dAbsF(tex_to_dev._31 - D2dFloorF(tex_to_dev._31 + 0.5f)) < 1e-4f && D2dAbsF(tex_to_dev._32 - D2dFloorF(tex_to_dev._32 + 0.5f)) < 1e-4f;
        if (aligned && composite == D2D1_COMPOSITE_MODE_SOURCE_OVER) {
            /* pixel for pixel, no sampling. */
            D2dSurface view = {clamp.right - clamp.left, clamp.bottom - clamp.top, nullptr};
            D2dSurface cut;
            if (D2dSurfaceInit(&cut, view.w, view.h)) {
                for (int y = 0; y < cut.h; y++)
                    memcpy(D2dSurfaceRow(&cut, y), D2dSurfaceRow(&data.surface, y + clamp.top) + clamp.left, (SIZE_T)cut.w * 4);
                D2dBlendSurface(rt, &cut, (int)D2dFloorF(tex_to_dev._31 + 0.5f) + clamp.left, (int)D2dFloorF(tex_to_dev._32 + 0.5f) + clamp.top, 1.0f);
                D2dSurfaceFree(&cut);
            }
        } else {
            MimeVecPolys polys;
            mime_vec_polys_init(&polys, &d2d_allocator);
            D2D1_POINT_2F q[4] = {{(float)clamp.left, (float)clamp.top}, {(float)clamp.right, (float)clamp.top}, {(float)clamp.right, (float)clamp.bottom}, {(float)clamp.left, (float)clamp.bottom}};
            MimeVecPoint pts[4];
            for (int i = 0; i < 4; i++) {
                D2D1_POINT_2F p = D2dMatApply(&tex_to_dev, q[i].x, q[i].y);
                pts[i].x = p.x;
                pts[i].y = p.y;
            }
            mime_vec_polys_add_sub(&polys, pts, 4, true);
            D2D1_PRIMITIVE_BLEND saved = rt->state.blend;
            if (composite == D2D1_COMPOSITE_MODE_SOURCE_COPY || composite == D2D1_COMPOSITE_MODE_BOUNDED_SOURCE_COPY)
                rt->state.blend = D2D1_PRIMITIVE_BLEND_COPY;
            else if (composite == D2D1_COMPOSITE_MODE_PLUS)
                rt->state.blend = D2D1_PRIMITIVE_BLEND_ADD;
            extern void D2dDrawTexture(D2dRt *, const D2dSurface *, const D2dMatrix *, const MimeVecPolys *, float, D2D1_INTERPOLATION_MODE, const MimeVecRect *);
            D2dDrawTexture(rt, &data.surface, &tex_to_dev, &polys, 1.0f, mode, &clamp);
            rt->state.blend = saved;
            mime_vec_polys_free(&polys);
        }
    }
    D2dImageDataFree(&data);
}

/* ------------------------------------------------------------------ the effect object */

#define EF D2dEffect *e = (D2dEffect *)This
#define SYS(i) ((i) >= 0x80000000u)

static const EffDesc *desc_of(const D2dEffect *e) { return &g_effects[e->id]; }

static UINT32 STDMETHODCALLTYPE ef_count(ID2D1Effect *This) { EF; return desc_of(e)->nprops; }

static HRESULT STDMETHODCALLTYPE ef_name(ID2D1Effect *This, UINT32 index, WCHAR *name, UINT32 count)
{
    EF;
    if (index >= desc_of(e)->nprops)
        return E_INVALIDARG;
    const WCHAR *n = desc_of(e)->props[index].name;
    UINT32 len = (UINT32)wcslen(n);
    if (count < len + 1)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    memcpy(name, n, (len + 1) * sizeof(WCHAR));
    return S_OK;
}
static UINT32 STDMETHODCALLTYPE ef_name_length(ID2D1Effect *This, UINT32 index)
{
    EF;
    return index < desc_of(e)->nprops ? (UINT32)wcslen(desc_of(e)->props[index].name) : 0;
}

static D2D1_PROPERTY_TYPE sys_type(UINT32 i)
{
    switch (i) {
    case D2D1_PROPERTY_CLSID: return D2D1_PROPERTY_TYPE_CLSID;
    case D2D1_PROPERTY_DISPLAYNAME:
    case D2D1_PROPERTY_AUTHOR:
    case D2D1_PROPERTY_CATEGORY:
    case D2D1_PROPERTY_DESCRIPTION: return D2D1_PROPERTY_TYPE_STRING;
    case D2D1_PROPERTY_INPUTS: return D2D1_PROPERTY_TYPE_ARRAY;
    case D2D1_PROPERTY_CACHED: return D2D1_PROPERTY_TYPE_BOOL;
    case D2D1_PROPERTY_PRECISION: return D2D1_PROPERTY_TYPE_ENUM;
    case D2D1_PROPERTY_MIN_INPUTS:
    case D2D1_PROPERTY_MAX_INPUTS: return D2D1_PROPERTY_TYPE_UINT32;
    default: return D2D1_PROPERTY_TYPE_UNKNOWN;
    }
}

static D2D1_PROPERTY_TYPE STDMETHODCALLTYPE ef_type(ID2D1Effect *This, UINT32 index)
{
    EF;
    if (SYS(index))
        return sys_type(index);
    return index < desc_of(e)->nprops ? desc_of(e)->props[index].type : D2D1_PROPERTY_TYPE_UNKNOWN;
}

static UINT32 STDMETHODCALLTYPE ef_index(ID2D1Effect *This, const WCHAR *name)
{
    EF;
    if (!name)
        return 0xffffffffu;
    for (UINT32 i = 0; i < desc_of(e)->nprops; i++) {
        if (wcscmp(desc_of(e)->props[i].name, name) == 0)
            return i;
    }
    static const WCHAR *sys[] = {L"Clsid", L"DisplayName", L"Author", L"Category", L"Description", L"Inputs", L"Cached", L"Precision", L"MinInputs", L"MaxInputs"};
    (void)sys;
    return 0xffffffffu;
}

static HRESULT sys_value(D2dEffect *e, UINT32 index, void *data, UINT32 size, UINT32 *needed)
{
    const EffDesc *d = desc_of(e);
    static const WCHAR author[] = L"Microsoft Corporation", description[] = L"";
    switch (index) {
    case D2D1_PROPERTY_CLSID:
        *needed = 16;
        if (size == 16)
            memcpy(data, d->clsid, 16);
        return S_OK;
    case D2D1_PROPERTY_DISPLAYNAME:
        *needed = (UINT32)(wcslen(d->name) + 1) * 2;
        if (size >= *needed)
            memcpy(data, d->name, *needed);
        return S_OK;
    case D2D1_PROPERTY_AUTHOR:
        *needed = sizeof(author);
        if (size >= *needed)
            memcpy(data, author, *needed);
        return S_OK;
    case D2D1_PROPERTY_CATEGORY:
        *needed = (UINT32)(wcslen(d->category) + 1) * 2;
        if (size >= *needed)
            memcpy(data, d->category, *needed);
        return S_OK;
    case D2D1_PROPERTY_DESCRIPTION:
        *needed = sizeof(description);
        if (size >= *needed)
            memcpy(data, description, *needed);
        return S_OK;
    case D2D1_PROPERTY_CACHED:
        *needed = 4;
        if (size == 4)
            *(BOOL *)data = e->cached;
        return S_OK;
    case D2D1_PROPERTY_PRECISION:
        *needed = 4;
        if (size == 4)
            *(UINT32 *)data = e->precision;
        return S_OK;
    case D2D1_PROPERTY_MIN_INPUTS:
        *needed = 4;
        if (size == 4)
            *(UINT32 *)data = d->min_inputs;
        return S_OK;
    case D2D1_PROPERTY_MAX_INPUTS:
        *needed = 4;
        if (size == 4)
            *(UINT32 *)data = d->max_inputs;
        return S_OK;
    default:
        return D2DERR_INVALID_PROPERTY;
    }
}

static HRESULT STDMETHODCALLTYPE ef_set(ID2D1Effect *This, UINT32 index, D2D1_PROPERTY_TYPE type, const BYTE *data, UINT32 size)
{
    EF;
    if (!data)
        return E_INVALIDARG;
    if (SYS(index)) {
        if (index == (UINT32)D2D1_PROPERTY_CACHED && size == 4) {
            e->cached = *(const BOOL *)data != 0;
            return S_OK;
        }
        if (index == (UINT32)D2D1_PROPERTY_PRECISION && size == 4) {
            e->precision = *(const UINT32 *)data;
            return S_OK;
        }
        return D2DERR_INVALID_PROPERTY;
    }
    if (index >= desc_of(e)->nprops)
        return D2DERR_INVALID_PROPERTY;
    const EffProp *p = &desc_of(e)->props[index];
    if (type != D2D1_PROPERTY_TYPE_UNKNOWN && type != p->type)
        return E_INVALIDARG;
    if (p->type == D2D1_PROPERTY_TYPE_BLOB) {
        if (!size || size % 4)
            return E_INVALIDARG;
        HRESULT hr = D2dFxCheckValue(e, index, (BYTE *)data, size);
        if (FAILED(hr))
            return hr;
        return blob_set(pb(e, (int)index), data, size) ? S_OK : E_OUTOFMEMORY;
    }
    if (size != p->size)
        return E_INVALIDARG;
    BYTE value[80];
    memcpy(value, data, size);
    HRESULT hr = D2dFxCheckValue(e, index, value, size);
    if (FAILED(hr))
        return hr;
    data = value;
    if (p->type == D2D1_PROPERTY_TYPE_COLOR_CONTEXT || p->type == D2D1_PROPERTY_TYPE_IUNKNOWN) {
        IUnknown *now = *(IUnknown *const *)data, *old = *(IUnknown **)e->props[index];
        D2D_ADDREF(now);
        D2D_RELEASE(old);
    }
    memcpy(e->props[index], data, size);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ef_set_by_name(ID2D1Effect *This, const WCHAR *name, D2D1_PROPERTY_TYPE type, const BYTE *data, UINT32 size)
{
    UINT32 i = ef_index(This, name);
    if (i == 0xffffffffu)
        return D2DERR_INVALID_PROPERTY;
    return ef_set(This, i, type, data, size);
}
static HRESULT STDMETHODCALLTYPE ef_get(ID2D1Effect *This, UINT32 index, D2D1_PROPERTY_TYPE type, BYTE *data, UINT32 size)
{
    EF;
    if (!data)
        return E_INVALIDARG;
    if (SYS(index)) {
        UINT32 need;
        HRESULT hr = sys_value(e, index, data, size, &need);
        if (FAILED(hr))
            return hr;
        return size < need ? HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) : S_OK;
    }
    if (index >= desc_of(e)->nprops)
        return D2DERR_INVALID_PROPERTY;
    const EffProp *p = &desc_of(e)->props[index];
    if (type != D2D1_PROPERTY_TYPE_UNKNOWN && type != p->type)
        return E_INVALIDARG;
    if (e->id == EFF_HISTOGRAM && index == 2)
        return D2dFxHistogram(e, data, size);
    if (p->type == D2D1_PROPERTY_TYPE_BLOB) {
        const EffBlob *b = pb(e, (int)index);
        if (size < b->size)
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        if (b->size)
            memcpy(data, b->data, b->size);
        return S_OK;
    }
    if (size < p->size)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    memcpy(data, e->props[index], p->size);
    if (p->type == D2D1_PROPERTY_TYPE_COLOR_CONTEXT || p->type == D2D1_PROPERTY_TYPE_IUNKNOWN) {
        IUnknown *u = *(IUnknown **)data;
        D2D_ADDREF(u);
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ef_get_by_name(ID2D1Effect *This, const WCHAR *name, D2D1_PROPERTY_TYPE type, BYTE *data, UINT32 size)
{
    UINT32 i = ef_index(This, name);
    if (i == 0xffffffffu)
        return D2DERR_INVALID_PROPERTY;
    return ef_get(This, i, type, data, size);
}
static UINT32 STDMETHODCALLTYPE ef_value_size(ID2D1Effect *This, UINT32 index)
{
    EF;
    if (SYS(index)) {
        UINT32 need = 0;
        BYTE tmp[1];
        sys_value(e, index, tmp, 0, &need);
        return need;
    }
    if (index >= desc_of(e)->nprops)
        return 0;
    if (e->id == EFF_HISTOGRAM && index == 2)
        return *pu(e, 0) * 4;
    return desc_of(e)->props[index].type == D2D1_PROPERTY_TYPE_BLOB ? pb(e, (int)index)->size : desc_of(e)->props[index].size;
}
static HRESULT STDMETHODCALLTYPE ef_subprops(ID2D1Effect *This, UINT32 index, ID2D1Properties **out)
{
    if (out)
        *out = nullptr;
    return D2DERR_NO_SUBPROPERTIES;
}

static void STDMETHODCALLTYPE ef_set_input(ID2D1Effect *This, UINT32 index, ID2D1Image *input, BOOL invalidate)
{
    EF;
    if (index >= 8)
        return;
    D2D_ADDREF(input);
    if (index < e->input_count)
        D2D_RELEASE(e->inputs[index]);
    e->inputs[index] = input;
    if (index >= e->input_count)
        e->input_count = index + 1;
}
static HRESULT STDMETHODCALLTYPE ef_set_input_count(ID2D1Effect *This, UINT32 count)
{
    EF;
    const EffDesc *d = desc_of(e);
    if (count < d->min_inputs || count > d->max_inputs || count > 8)
        return E_INVALIDARG;
    for (UINT32 i = count; i < e->input_count; i++)
        D2D_RELEASE(e->inputs[i]);
    e->input_count = count;
    return S_OK;
}
static void STDMETHODCALLTYPE ef_get_input(ID2D1Effect *This, UINT32 index, ID2D1Image **out)
{
    EF;
    *out = index < e->input_count ? e->inputs[index] : nullptr;
    D2D_ADDREF(*out);
}
static UINT32 STDMETHODCALLTYPE ef_get_input_count(ID2D1Effect *This) { EF; return e->input_count; }
static void STDMETHODCALLTYPE ef_get_output(ID2D1Effect *This, ID2D1Image **out)
{
    EF;
    if (out)
        D2dEffectQueryImage(&e->obj, &IID_ID2D1Image, (void **)out);
}

static void effect_destroy(D2dObj *o)
{
    D2dEffect *e = (D2dEffect *)o;
    for (UINT32 i = 0; i < e->input_count; i++)
        D2D_RELEASE(e->inputs[i]);
    for (UINT32 i = 0; i < desc_of(e)->nprops; i++) {
        D2D1_PROPERTY_TYPE t = desc_of(e)->props[i].type;
        if (t == D2D1_PROPERTY_TYPE_COLOR_CONTEXT || t == D2D1_PROPERTY_TYPE_IUNKNOWN) {
            IUnknown *u = *(IUnknown **)e->props[i];
            D2D_RELEASE(u);
        } else if (t == D2D1_PROPERTY_TYPE_BLOB) {
            D2dFree(pb(e, (int)i)->data);
        }
    }
    D2dFree(e->histogram);
}

static const void *const g_effect_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, ef_count, ef_name, ef_name_length,
                                            ef_type, ef_index, ef_set_by_name, ef_set, ef_get_by_name, ef_get, ef_value_size,
                                            ef_subprops, ef_set_input, ef_set_input_count, ef_get_input, ef_get_input_count, ef_get_output};
D2D_IIDS(g_effect_iids, &IID_ID2D1Properties, &IID_ID2D1Effect);
static D2dClass g_effect_class = {"effect", g_effect_iids, effect_destroy, D2dEffectQueryImage};

HRESULT D2dCreateEffect(D2dRt *rt, REFCLSID clsid, ID2D1Effect **out)
{
    return D2dCreateEffectF(rt->obj.factory, rt->dpi_x, clsid, out);
}

HRESULT D2dCreateEffectF(D2dFactory *f, float dpi, REFCLSID clsid, ID2D1Effect **out)
{
    for (int i = 0; i < EFF_COUNT; i++) {
        if (!IsEqualGUID(clsid, g_effects[i].clsid))
            continue;
        D2dEffect *e = D2dNew(sizeof(*e), g_effect_vtbl, &g_effect_class, f);
        if (!e)
            return E_OUTOFMEMORY;
        e->image_vtbl = d2d_effect_image_vtbl;
        e->id = i;
        e->input_count = g_effects[i].default_inputs;
        e->precision = D2D1_BUFFER_PRECISION_UNKNOWN;
        effect_defaults(e);
        *out = (ID2D1Effect *)e;
        return S_OK;
    }
    return D2dCustomCreate(f, dpi, clsid, out);
}

HRESULT D2dEffectEnumerate(D2dFactory *f, CLSID *list, UINT32 count, UINT32 *returned, UINT32 *registered)
{
    UINT32 n = 0;
    for (int i = 0; i < EFF_COUNT; i++) {
        if (list && n < count)
            list[n] = *g_effects[i].clsid;
        n++;
    }
    n += D2dCustomList(f, list ? list + (n < count ? n : count) : nullptr, n < count ? count - n : 0);
    UINT32 shown = n < count ? n : count;
    if (returned)
        *returned = list ? shown : 0;
    if (registered)
        *registered = n;
    return list && count < n ? HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) : S_OK;
}

HRESULT D2dEffectUnregister(D2dFactory *f, REFCLSID clsid)
{
    for (int i = 0; i < EFF_COUNT; i++) {
        if (IsEqualGUID(clsid, g_effects[i].clsid))
            return D2DERR_INVALID_CALL;
    }
    return D2dCustomUnregister(f, clsid);
}

HRESULT D2dEffectProperties(D2dFactory *f, REFCLSID clsid, ID2D1Properties **out)
{
    for (int i = 0; i < EFF_COUNT; i++) {
        if (IsEqualGUID(clsid, g_effects[i].clsid)) {
            D2dEffect *e = D2dNew(sizeof(*e), g_effect_vtbl, &g_effect_class, f);
            if (!e)
                return E_OUTOFMEMORY;
            e->image_vtbl = d2d_effect_image_vtbl;
            e->id = i;
            e->input_count = g_effects[i].default_inputs;
            effect_defaults(e);
            *out = (ID2D1Properties *)e;
            return S_OK;
        }
    }
    return D2dCustomProperties(f, clsid, out);
}
