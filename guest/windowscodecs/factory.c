/* IWICImagingFactory, IWICImagingFactory2 and IWICComponentFactory implementations. */
#include "wic_codec.h"
#include "wic_meta.h"

typedef struct WicFactory {
    WicBase base;
    WicSlot factory;
    WicSlot component;
} WicFactory;

/* decoders. */

static HRESULT decoder_from_stream(IStream *stream, const GUID *vendor, WICDecodeOptions options,
                                   IWICBitmapDecoder **decoder)
{
    return WicDecoderFromStream(stream, vendor, options, decoder);
}

static HRESULT STDMETHODCALLTYPE fac_create_decoder_from_filename(IWICImagingFactory *This, LPCWSTR name,
                                                                  const GUID *vendor, DWORD access,
                                                                  WICDecodeOptions options,
                                                                  IWICBitmapDecoder **decoder)
{
    if (!decoder)
        return E_INVALIDARG;
    *decoder = nullptr;
    if (!name)
        return E_INVALIDARG;
    IWICStream *stream;
    HRESULT hr = WicStreamCreate(&stream);
    if (FAILED(hr))
        return hr;
    hr = stream->lpVtbl->InitializeFromFilename(stream, name, access ? access : GENERIC_READ);
    if (SUCCEEDED(hr))
        hr = decoder_from_stream((IStream *)stream, vendor, options, decoder);
    stream->lpVtbl->Release(stream);
    return hr;
}

static HRESULT STDMETHODCALLTYPE fac_create_decoder_from_stream(IWICImagingFactory *This, IStream *stream,
                                                                const GUID *vendor, WICDecodeOptions options,
                                                                IWICBitmapDecoder **decoder)
{
    if (!decoder)
        return E_INVALIDARG;
    *decoder = nullptr;
    if (!stream)
        return E_INVALIDARG;
    return decoder_from_stream(stream, vendor, options, decoder);
}

static HRESULT STDMETHODCALLTYPE fac_create_decoder_from_handle(IWICImagingFactory *This, ULONG_PTR file,
                                                                const GUID *vendor, WICDecodeOptions options,
                                                                IWICBitmapDecoder **decoder)
{
    if (!decoder)
        return E_INVALIDARG;
    *decoder = nullptr;
    HANDLE source = (HANDLE)file, copy;
    if (!source || source == INVALID_HANDLE_VALUE)
        return E_INVALIDARG;
    if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return HRESULT_FROM_WIN32(GetLastError());
    LARGE_INTEGER size, position, zero;
    zero.QuadPart = 0;
    HRESULT hr = E_FAIL;
    if (GetFileSizeEx(copy, &size) && SetFilePointerEx(copy, zero, &position, FILE_CURRENT) &&
        size.QuadPart >= position.QuadPart && size.QuadPart < 0x60000000) {
        SIZE_T total = (SIZE_T)(size.QuadPart - position.QuadPart);
        BYTE *data = WicAlloc(total ? total : 1);
        if (!data) {
            hr = E_OUTOFMEMORY;
        } else {
            DWORD got = 0;
            SIZE_T used = 0;
            hr = S_OK;
            while (used < total) {
                if (!ReadFile(copy, data + used, (DWORD)(total - used), &got, nullptr) || !got) {
                    hr = WINCODEC_ERR_STREAMREAD;
                    break;
                }
                used += got;
            }
            if (SUCCEEDED(hr)) {
                IStream *stream;
                hr = WicMemoryStreamCreate(data, total, TRUE, &stream);
                if (SUCCEEDED(hr)) {
                    hr = decoder_from_stream(stream, vendor, options, decoder);
                    stream->lpVtbl->Release(stream);
                }
            }
            WicFree(data);
        }
    }
    CloseHandle(copy);
    return hr;
}

static HRESULT STDMETHODCALLTYPE fac_create_component_info(IWICImagingFactory *This, REFCLSID clsid,
                                                           IWICComponentInfo **info)
{
    if (!info)
        return E_INVALIDARG;
    *info = nullptr;
    if (!clsid)
        return E_INVALIDARG;
    return WicComponentInfo(clsid, info);
}

static HRESULT STDMETHODCALLTYPE fac_create_decoder(IWICImagingFactory *This, REFGUID container, const GUID *vendor,
                                                    IWICBitmapDecoder **decoder)
{
    if (!decoder)
        return E_INVALIDARG;
    *decoder = nullptr;
    if (!container)
        return E_INVALIDARG;
    const WicCodec *codec = WicDecoderByContainer(container, vendor);
    if (!codec)
        return WINCODEC_ERR_COMPONENTNOTFOUND;
    return WicDecoderCreate(codec, decoder);
}

static HRESULT STDMETHODCALLTYPE fac_create_encoder(IWICImagingFactory *This, REFGUID container, const GUID *vendor,
                                                    IWICBitmapEncoder **encoder)
{
    if (!encoder)
        return E_INVALIDARG;
    *encoder = nullptr;
    if (!container)
        return E_INVALIDARG;
    const WicCodec *codec = WicEncoderByContainer(container, vendor);
    if (!codec)
        return WINCODEC_ERR_COMPONENTNOTFOUND;
    return WicEncoderCreate(codec, encoder);
}

/* the simple objects. */

static HRESULT STDMETHODCALLTYPE fac_create_palette(IWICImagingFactory *This, IWICPalette **palette)
{
    if (!palette)
        return E_INVALIDARG;
    return WicPaletteCreate(palette);
}

static HRESULT STDMETHODCALLTYPE fac_create_converter(IWICImagingFactory *This, IWICFormatConverter **converter)
{
    if (!converter)
        return E_INVALIDARG;
    return WicFormatConverterCreate(converter);
}

static HRESULT STDMETHODCALLTYPE fac_create_scaler(IWICImagingFactory *This, IWICBitmapScaler **scaler)
{
    if (!scaler)
        return E_INVALIDARG;
    return WicScalerCreate(scaler);
}

static HRESULT STDMETHODCALLTYPE fac_create_clipper(IWICImagingFactory *This, IWICBitmapClipper **clipper)
{
    if (!clipper)
        return E_INVALIDARG;
    return WicClipperCreate(clipper);
}

static HRESULT STDMETHODCALLTYPE fac_create_rotator(IWICImagingFactory *This, IWICBitmapFlipRotator **rotator)
{
    if (!rotator)
        return E_INVALIDARG;
    return WicFlipRotatorCreate(rotator);
}

static HRESULT STDMETHODCALLTYPE fac_create_stream(IWICImagingFactory *This, IWICStream **stream)
{
    if (!stream)
        return E_INVALIDARG;
    return WicStreamCreate(stream);
}

static HRESULT STDMETHODCALLTYPE fac_create_context(IWICImagingFactory *This, IWICColorContext **context)
{
    if (!context)
        return E_INVALIDARG;
    return WicColorContextCreate(context);
}

static HRESULT STDMETHODCALLTYPE fac_create_transform(IWICImagingFactory *This, IWICColorTransform **transform)
{
    if (!transform)
        return E_INVALIDARG;
    return WicColorTransformCreate(transform);
}

static HRESULT STDMETHODCALLTYPE fac_create_bitmap(IWICImagingFactory *This, UINT width, UINT height,
                                                   REFWICPixelFormatGUID format, WICBitmapCreateCacheOption option,
                                                   IWICBitmap **bitmap)
{
    if (!bitmap)
        return E_INVALIDARG;
    *bitmap = nullptr;
    if (!format)
        return E_INVALIDARG;
    return WicBitmapCreate(width, height, format, nullptr, 0, 0, FALSE, bitmap);
}

static HRESULT STDMETHODCALLTYPE fac_create_bitmap_from_source(IWICImagingFactory *This, IWICBitmapSource *source,
                                                               WICBitmapCreateCacheOption option, IWICBitmap **bitmap)
{
    if (!bitmap)
        return E_INVALIDARG;
    *bitmap = nullptr;
    if (!source)
        return E_INVALIDARG;
    return WicBitmapFromSource(source, nullptr, bitmap);
}

static HRESULT STDMETHODCALLTYPE fac_create_bitmap_from_rect(IWICImagingFactory *This, IWICBitmapSource *source,
                                                             UINT x, UINT y, UINT width, UINT height,
                                                             IWICBitmap **bitmap)
{
    if (!bitmap)
        return E_INVALIDARG;
    *bitmap = nullptr;
    if (!source)
        return E_INVALIDARG;
    if (x > 0x7fffffff || y > 0x7fffffff || width > 0x7fffffff || height > 0x7fffffff)
        return E_INVALIDARG;
    WICRect rect = {(INT)x, (INT)y, (INT)width, (INT)height};
    if (!width || !height)
        return E_INVALIDARG;
    return WicBitmapFromSource(source, &rect, bitmap);
}

static HRESULT STDMETHODCALLTYPE fac_create_bitmap_from_memory(IWICImagingFactory *This, UINT width, UINT height,
                                                               REFWICPixelFormatGUID format, UINT stride, UINT size,
                                                               BYTE *buffer, IWICBitmap **bitmap)
{
    if (!bitmap)
        return E_INVALIDARG;
    *bitmap = nullptr;
    if (!buffer || !format)
        return E_INVALIDARG;
    if (!width || !height)
        return E_INVALIDARG;
    UINT row;
    if (!WicFindFormat(format))
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    if (!WicRowBytes(format, width, &row))
        return INTSAFE_E_ARITHMETIC_OVERFLOW;
    if (stride < row)
        return E_INVALIDARG;
    UINT64 need = (UINT64)stride * (height - 1) + row;
    if (need > 0xFFFFFFFFull)
        return INTSAFE_E_ARITHMETIC_OVERFLOW;
    if (size < need)
        return WINCODEC_ERR_INSUFFICIENTBUFFER;
    return WicBitmapCreate(width, height, format, buffer, stride, size, TRUE, bitmap);
}

HRESULT WicBitmapFromHBITMAP(HBITMAP bitmap, HPALETTE palette, WICBitmapAlphaChannelOption option, IWICBitmap **out);
HRESULT WicBitmapFromHICON(HICON icon, IWICBitmap **out);

static HRESULT STDMETHODCALLTYPE fac_create_bitmap_from_hbitmap(IWICImagingFactory *This, HBITMAP bitmap,
                                                                HPALETTE palette,
                                                                WICBitmapAlphaChannelOption option,
                                                                IWICBitmap **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!bitmap)
        return E_INVALIDARG;
    return WicBitmapFromHBITMAP(bitmap, palette, option, out);
}

static HRESULT STDMETHODCALLTYPE fac_create_bitmap_from_hicon(IWICImagingFactory *This, HICON icon, IWICBitmap **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!icon)
        return E_INVALIDARG;
    return WicBitmapFromHICON(icon, out);
}

static HRESULT STDMETHODCALLTYPE fac_create_enumerator(IWICImagingFactory *This, DWORD types, DWORD options,
                                                       IEnumUnknown **enumerator)
{
    if (!enumerator)
        return E_INVALIDARG;
    *enumerator = nullptr;
    if (types & ~(DWORD)WICAllComponents)
        return E_INVALIDARG;
    return WicEnumerateComponents(types, options, enumerator);
}

static HRESULT STDMETHODCALLTYPE fac_fast_from_decoder(IWICImagingFactory *This, IWICBitmapDecoder *decoder,
                                                       IWICFastMetadataEncoder **encoder)
{
    if (!decoder || !encoder)
        return E_INVALIDARG;
    return WicMetaFastEncoderFromDecoder(decoder, encoder);
}

static HRESULT STDMETHODCALLTYPE fac_fast_from_frame(IWICImagingFactory *This, IWICBitmapFrameDecode *frame,
                                                     IWICFastMetadataEncoder **encoder)
{
    if (!frame || !encoder)
        return E_INVALIDARG;
    return WicMetaFastEncoderFromFrame(frame, encoder);
}

static HRESULT STDMETHODCALLTYPE fac_create_query_writer(IWICImagingFactory *This, REFGUID format, const GUID *vendor,
                                                         IWICMetadataQueryWriter **query)
{
    if (!query)
        return E_INVALIDARG;
    *query = nullptr;
    if (!format)
        return E_INVALIDARG;
    return WicMetaQueryWriterCreate(format, vendor, query);
}

static HRESULT STDMETHODCALLTYPE fac_create_query_writer_from_reader(IWICImagingFactory *This,
                                                                     IWICMetadataQueryReader *reader,
                                                                     const GUID *vendor,
                                                                     IWICMetadataQueryWriter **query)
{
    if (!query)
        return E_INVALIDARG;
    *query = nullptr;
    if (!reader)
        return E_INVALIDARG;
    return WicMetaQueryWriterFromReader(reader, vendor, query);
}

static HRESULT STDMETHODCALLTYPE fac_create_image_encoder(IWICImagingFactory2 *This, ID2D1Device *device,
                                                          IWICImageEncoder **encoder)
{
    if (encoder)
        *encoder = nullptr;
    return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

#define FACTORY_PREFIX                                                                                                 \
    VTBL_FN(fac_create_decoder_from_filename), VTBL_FN(fac_create_decoder_from_stream),                                \
        VTBL_FN(fac_create_decoder_from_handle), VTBL_FN(fac_create_component_info), VTBL_FN(fac_create_decoder),      \
        VTBL_FN(fac_create_encoder), VTBL_FN(fac_create_palette), VTBL_FN(fac_create_converter),                       \
        VTBL_FN(fac_create_scaler), VTBL_FN(fac_create_clipper), VTBL_FN(fac_create_rotator),                          \
        VTBL_FN(fac_create_stream), VTBL_FN(fac_create_context), VTBL_FN(fac_create_transform),                        \
        VTBL_FN(fac_create_bitmap), VTBL_FN(fac_create_bitmap_from_source), VTBL_FN(fac_create_bitmap_from_rect),      \
        VTBL_FN(fac_create_bitmap_from_memory), VTBL_FN(fac_create_bitmap_from_hbitmap),                               \
        VTBL_FN(fac_create_bitmap_from_hicon), VTBL_FN(fac_create_enumerator), VTBL_FN(fac_fast_from_decoder),         \
        VTBL_FN(fac_fast_from_frame), VTBL_FN(fac_create_query_writer), VTBL_FN(fac_create_query_writer_from_reader)

static const IWICImagingFactory2Vtbl g_factory_vtbl = {
    WIC_IUNKNOWN_FUNCS, FACTORY_PREFIX, fac_create_image_encoder,
};

/* the component factory. */

static HRESULT STDMETHODCALLTYPE cfac_create_reader(IWICComponentFactory *This, REFGUID format, const GUID *vendor,
                                                    DWORD options, IStream *stream, IWICMetadataReader **reader)
{
    if (!reader)
        return E_INVALIDARG;
    *reader = nullptr;
    if (!format)
        return E_INVALIDARG;
    return WicMetaCreateReader(format, vendor, options, stream, reader);
}

static HRESULT STDMETHODCALLTYPE cfac_create_reader_from_container(IWICComponentFactory *This, REFGUID container,
                                                                   const GUID *vendor, DWORD options, IStream *stream,
                                                                   IWICMetadataReader **reader)
{
    if (!reader)
        return E_INVALIDARG;
    *reader = nullptr;
    if (!container || !stream)
        return E_INVALIDARG;
    return WicMetaCreateReaderFromContainer(container, vendor, options, stream, reader);
}

static HRESULT STDMETHODCALLTYPE cfac_create_writer(IWICComponentFactory *This, REFGUID format, const GUID *vendor,
                                                    DWORD options, IWICMetadataWriter **writer)
{
    if (!writer)
        return E_INVALIDARG;
    *writer = nullptr;
    if (!format)
        return E_INVALIDARG;
    return WicMetaCreateWriter(format, vendor, options, writer);
}

static HRESULT STDMETHODCALLTYPE cfac_writer_from_reader(IWICComponentFactory *This, IWICMetadataReader *reader,
                                                         const GUID *vendor, IWICMetadataWriter **writer)
{
    if (!writer)
        return E_INVALIDARG;
    *writer = nullptr;
    if (!reader)
        return E_INVALIDARG;
    return WicMetaWriterFromReader(reader, vendor, writer);
}

static HRESULT STDMETHODCALLTYPE cfac_query_reader_from_blocks(IWICComponentFactory *This,
                                                               IWICMetadataBlockReader *blocks,
                                                               IWICMetadataQueryReader **query)
{
    if (!query)
        return E_INVALIDARG;
    *query = nullptr;
    if (!blocks)
        return E_INVALIDARG;
    return WicQueryFromBlocks(blocks, query);
}

static HRESULT STDMETHODCALLTYPE cfac_query_writer_from_blocks(IWICComponentFactory *This,
                                                               IWICMetadataBlockWriter *blocks,
                                                               IWICMetadataQueryWriter **query)
{
    if (!query)
        return E_INVALIDARG;
    *query = nullptr;
    if (!blocks)
        return E_INVALIDARG;
    return WicMetaQueryWriterFromBlocks(blocks, query);
}

static HRESULT STDMETHODCALLTYPE cfac_property_bag(IWICComponentFactory *This, PROPBAG2 *props, UINT count,
                                                  IPropertyBag2 **bag)
{
    if (!bag)
        return E_INVALIDARG;
    *bag = nullptr;
    /* create an option bag from the container encoder's property definitions. */
    const WicCodec *codec = WicEncoderAt(0);
    (void)props;
    (void)count;
    return codec ? WicEncoderPropertyBag(codec, bag) : E_FAIL;
}

static const IWICComponentFactoryVtbl g_component_vtbl = {
    WIC_IUNKNOWN_FUNCS, FACTORY_PREFIX,
    cfac_create_reader, cfac_create_reader_from_container, cfac_create_writer, cfac_writer_from_reader,
    cfac_query_reader_from_blocks, cfac_query_writer_from_blocks, cfac_property_bag,
};

static const WicIfaceEntry g_factory_ifaces[] = {
    WIC_ENTRY(WicFactory, IID_IWICImagingFactory, factory),
    WIC_ENTRY(WicFactory, IID_IWICImagingFactory2, factory),
    WIC_ENTRY(WicFactory, IID_IWICComponentFactory, component),
};
/* both factory versions expose both sets of interfaces, as on Windows. */
static const WicClass g_factory1_class = {nullptr, g_factory_ifaces, 3};
static const WicClass g_factory2_class = {nullptr, g_factory_ifaces, 3};

static HRESULT factory_create(const WicClass *cls, REFIID riid, void **object)
{
    WicFactory *f = WicNew(sizeof(*f), cls);
    if (!f)
        return E_OUTOFMEMORY;
    WIC_SLOT(f, factory, &g_factory_vtbl);
    WIC_SLOT(f, component, &g_component_vtbl);
    HRESULT hr = WicQueryBase(&f->base, riid, object);
    WicRelease(&f->factory);
    return hr;
}

HRESULT WicCreateFactoryObject(REFIID riid, void **object) { return factory_create(&g_factory2_class, riid, object); }
HRESULT WicCreateFactoryObject1(REFIID riid, void **object) { return factory_create(&g_factory1_class, riid, object); }
HRESULT WicCreateFactoryObject2(REFIID riid, void **object) { return factory_create(&g_factory2_class, riid, object); }

DECLSPEC_EXPORT HRESULT WINAPI WICCreateImagingFactory_Proxy(UINT version, IWICImagingFactory **factory)
{
    if (!factory)
        return E_INVALIDARG;
    *factory = nullptr;
    /* 0x236 and 0x237 are the Windows 7 and Windows 8 SDK versions. */
    if (version != WINCODEC_SDK_VERSION1 && version != WINCODEC_SDK_VERSION2)
        return WINCODEC_ERR_UNSUPPORTEDVERSION;
    return factory_create(version == WINCODEC_SDK_VERSION1 ? &g_factory1_class : &g_factory2_class,
                          &IID_IWICImagingFactory, (void **)factory);
}

DECLSPEC_EXPORT HRESULT WINAPI WICCreateColorContext_Proxy(IWICImagingFactory *factory, IWICColorContext **context)
{
    if (!factory || !context)
        return E_INVALIDARG;
    return factory->lpVtbl->CreateColorContext(factory, context);
}

DECLSPEC_EXPORT HRESULT WINAPI WICSetEncoderFormat_Proxy(IWICBitmapSource *source, IWICPalette *palette,
                                                         IWICBitmapFrameEncode *frame, IWICBitmapSource **result)
{
    if (!source || !frame || !result)
        return E_INVALIDARG;
    *result = nullptr;
    GUID format, wanted;
    HRESULT hr = source->lpVtbl->GetPixelFormat(source, &format);
    if (FAILED(hr))
        return hr;
    wanted = format;
    hr = frame->lpVtbl->SetPixelFormat(frame, &wanted);
    if (FAILED(hr))
        return hr;
    if (IsEqualGUID(&wanted, &format)) {
        source->lpVtbl->AddRef(source);
        *result = source;
        return S_OK;
    }
    IWICFormatConverter *converter;
    hr = WicFormatConverterCreate(&converter);
    if (FAILED(hr))
        return hr;
    hr = converter->lpVtbl->Initialize(converter, source, &wanted, WICBitmapDitherTypeNone, palette, 0.0,
                                       WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        converter->lpVtbl->Release(converter);
        return hr;
    }
    *result = (IWICBitmapSource *)converter;
    return S_OK;
}
