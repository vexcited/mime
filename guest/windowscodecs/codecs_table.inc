/* codec names, types, extensions, signatures and pixel formats. */

extern const WicDecoderOps wic_dec_bmp;
extern const WicDecoderOps wic_dec_gif;
extern const WicDecoderOps wic_dec_ico;
extern const WicDecoderOps wic_dec_cur;
extern const WicDecoderOps wic_dec_jpeg;
extern const WicDecoderOps wic_dec_png1;
extern const WicDecoderOps wic_dec_png;
extern const WicDecoderOps wic_dec_tiff;
extern const WicDecoderOps wic_dec_dds;
extern const WicDecoderOps wic_dec_wmp;
extern const WicEncoderOps wic_enc_bmp;
extern const WicEncoderOps wic_enc_gif;
extern const WicEncoderOps wic_enc_jpeg;
extern const WicEncoderOps wic_enc_png;
extern const WicEncoderOps wic_enc_tiff;
extern const WicEncoderOps wic_enc_dds;
extern const WicEncoderOps wic_enc_wmp;


static const BYTE dec_bmp_p0[] = {0x42, 0x4d};
static const BYTE dec_bmp_m0[] = {0xff, 0xff};
static const WicPattern dec_bmp_patterns[] = {
    {0, 2, dec_bmp_p0, dec_bmp_m0, 0},
};
static const GUID *const dec_bmp_formats[] = {
    &GUID_WICPixelFormat1bppIndexed,
    &GUID_WICPixelFormat4bppIndexed,
    &GUID_WICPixelFormat8bppIndexed,
    &GUID_WICPixelFormat16bppBGR555,
    &GUID_WICPixelFormat16bppBGR565,
    &GUID_WICPixelFormat24bppBGR,
    &GUID_WICPixelFormat32bppBGR,
    &GUID_WICPixelFormat32bppBGRA,
    &GUID_WICPixelFormat64bppRGBAFixedPoint,
};
static const BYTE dec_gif_p0[] = {0x47, 0x49, 0x46, 0x38, 0x37, 0x61};
static const BYTE dec_gif_m0[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const BYTE dec_gif_p1[] = {0x47, 0x49, 0x46, 0x38, 0x39, 0x61};
static const BYTE dec_gif_m1[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const WicPattern dec_gif_patterns[] = {
    {0, 6, dec_gif_p0, dec_gif_m0, 0},
    {0, 6, dec_gif_p1, dec_gif_m1, 0},
};
static const GUID *const dec_gif_formats[] = {
    &GUID_WICPixelFormat8bppIndexed,
};
static const BYTE dec_ico_p0[] = {0x00, 0x00, 0x01, 0x00};
static const BYTE dec_ico_m0[] = {0xff, 0xff, 0xff, 0xff};
static const WicPattern dec_ico_patterns[] = {
    {0, 4, dec_ico_p0, dec_ico_m0, 0},
};
static const GUID *const dec_ico_formats[] = {
    &GUID_WICPixelFormat32bppBGRA,
};
static const BYTE dec_cur_p0[] = {0x00, 0x00, 0x02, 0x00};
static const BYTE dec_cur_m0[] = {0xff, 0xff, 0xff, 0xff};
static const WicPattern dec_cur_patterns[] = {
    {0, 4, dec_cur_p0, dec_cur_m0, 0},
};
static const GUID *const dec_cur_formats[] = {
    &GUID_WICPixelFormat32bppBGRA,
};
static const BYTE dec_jpeg_p0[] = {0xff, 0xd8};
static const BYTE dec_jpeg_m0[] = {0xff, 0xff};
static const WicPattern dec_jpeg_patterns[] = {
    {0, 2, dec_jpeg_p0, dec_jpeg_m0, 0},
};
static const GUID *const dec_jpeg_formats[] = {
    &GUID_WICPixelFormat8bppGray,
    &GUID_WICPixelFormat24bppBGR,
    &GUID_WICPixelFormat32bppCMYK,
};
static const BYTE dec_png1_p0[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
static const BYTE dec_png1_m0[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const WicPattern dec_png1_patterns[] = {
    {0, 8, dec_png1_p0, dec_png1_m0, 0},
};
static const GUID *const dec_png1_formats[] = {
    &GUID_WICPixelFormat1bppIndexed,
    &GUID_WICPixelFormat2bppIndexed,
    &GUID_WICPixelFormat4bppIndexed,
    &GUID_WICPixelFormat8bppIndexed,
    &GUID_WICPixelFormatBlackWhite,
    &GUID_WICPixelFormat2bppGray,
    &GUID_WICPixelFormat4bppGray,
    &GUID_WICPixelFormat8bppGray,
    &GUID_WICPixelFormat16bppGray,
    &GUID_WICPixelFormat24bppBGR,
    &GUID_WICPixelFormat32bppBGRA,
    &GUID_WICPixelFormat48bppRGB,
    &GUID_WICPixelFormat64bppRGBA,
};
static const BYTE dec_png_p0[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
static const BYTE dec_png_m0[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const WicPattern dec_png_patterns[] = {
    {0, 8, dec_png_p0, dec_png_m0, 0},
};
static const GUID *const dec_png_formats[] = {
    &GUID_WICPixelFormat1bppIndexed,
    &GUID_WICPixelFormat2bppIndexed,
    &GUID_WICPixelFormat4bppIndexed,
    &GUID_WICPixelFormat8bppIndexed,
    &GUID_WICPixelFormatBlackWhite,
    &GUID_WICPixelFormat2bppGray,
    &GUID_WICPixelFormat4bppGray,
    &GUID_WICPixelFormat8bppGray,
    &GUID_WICPixelFormat16bppGray,
    &GUID_WICPixelFormat24bppBGR,
    &GUID_WICPixelFormat32bppBGRA,
    &GUID_WICPixelFormat48bppRGB,
    &GUID_WICPixelFormat64bppRGBA,
};
static const BYTE dec_tiff_p0[] = {0x49, 0x49, 0x2a, 0x00};
static const BYTE dec_tiff_m0[] = {0xff, 0xff, 0xff, 0xff};
static const BYTE dec_tiff_p1[] = {0x4d, 0x4d, 0x00, 0x2a};
static const BYTE dec_tiff_m1[] = {0xff, 0xff, 0xff, 0xff};
static const WicPattern dec_tiff_patterns[] = {
    {0, 4, dec_tiff_p0, dec_tiff_m0, 0},
    {0, 4, dec_tiff_p1, dec_tiff_m1, 0},
};
static const GUID *const dec_tiff_formats[] = {
    &GUID_WICPixelFormat1bppIndexed,
    &GUID_WICPixelFormat4bppIndexed,
    &GUID_WICPixelFormat8bppIndexed,
    &GUID_WICPixelFormatBlackWhite,
    &GUID_WICPixelFormat4bppGray,
    &GUID_WICPixelFormat8bppGray,
    &GUID_WICPixelFormat16bppGray,
    &GUID_WICPixelFormat32bppGrayFloat,
    &GUID_WICPixelFormat24bppBGR,
    &GUID_WICPixelFormat32bppBGRA,
    &GUID_WICPixelFormat32bppPBGRA,
    &GUID_WICPixelFormat48bppRGB,
    &GUID_WICPixelFormat32bppCMYK,
    &GUID_WICPixelFormat40bppCMYKAlpha,
    &GUID_WICPixelFormat64bppRGBA,
    &GUID_WICPixelFormat64bppPRGBA,
    &GUID_WICPixelFormat64bppCMYK,
    &GUID_WICPixelFormat80bppCMYKAlpha,
    &GUID_WICPixelFormat96bppRGBFloat,
    &GUID_WICPixelFormat128bppRGBAFloat,
    &GUID_WICPixelFormat128bppPRGBAFloat,
};
static const BYTE dec_dds_p0[] = {0x44, 0x44, 0x53, 0x20};
static const BYTE dec_dds_m0[] = {0xff, 0xff, 0xff, 0xff};
static const WicPattern dec_dds_patterns[] = {
    {0, 4, dec_dds_p0, dec_dds_m0, 0},
};
static const GUID *const dec_dds_formats[] = {
    &GUID_WICPixelFormat32bppBGRA,
};
static const GUID *const enc_bmp_formats[] = {
    &GUID_WICPixelFormat1bppIndexed,
    &GUID_WICPixelFormat4bppIndexed,
    &GUID_WICPixelFormat8bppIndexed,
    &GUID_WICPixelFormat16bppBGR555,
    &GUID_WICPixelFormat16bppBGR565,
    &GUID_WICPixelFormat24bppBGR,
    &GUID_WICPixelFormat32bppBGR,
    &GUID_WICPixelFormat32bppBGRA,
    &GUID_WICPixelFormat32bppPBGRA,
    &GUID_WICPixelFormat64bppRGBAFixedPoint,
    &GUID_WICPixelFormat64bppBGRAFixedPoint,
};
static const GUID *const enc_gif_formats[] = {
    &GUID_WICPixelFormat8bppIndexed,
};
static const GUID *const enc_jpeg_formats[] = {
    &GUID_WICPixelFormat8bppGray,
    &GUID_WICPixelFormat24bppBGR,
    &GUID_WICPixelFormat32bppCMYK,
};
static const GUID *const enc_png_formats[] = {
    &GUID_WICPixelFormat1bppIndexed,
    &GUID_WICPixelFormat2bppIndexed,
    &GUID_WICPixelFormat4bppIndexed,
    &GUID_WICPixelFormat8bppIndexed,
    &GUID_WICPixelFormatBlackWhite,
    &GUID_WICPixelFormat2bppGray,
    &GUID_WICPixelFormat4bppGray,
    &GUID_WICPixelFormat8bppGray,
    &GUID_WICPixelFormat16bppGray,
    &GUID_WICPixelFormat24bppBGR,
    &GUID_WICPixelFormat32bppBGRA,
    &GUID_WICPixelFormat48bppRGB,
    &GUID_WICPixelFormat48bppBGR,
    &GUID_WICPixelFormat64bppRGBA,
    &GUID_WICPixelFormat64bppBGRA,
};
static const GUID *const enc_tiff_formats[] = {
    &GUID_WICPixelFormat1bppIndexed,
    &GUID_WICPixelFormat4bppIndexed,
    &GUID_WICPixelFormat8bppIndexed,
    &GUID_WICPixelFormatBlackWhite,
    &GUID_WICPixelFormat4bppGray,
    &GUID_WICPixelFormat8bppGray,
    &GUID_WICPixelFormat16bppGray,
    &GUID_WICPixelFormat24bppBGR,
    &GUID_WICPixelFormat32bppBGRA,
    &GUID_WICPixelFormat32bppCMYK,
    &GUID_WICPixelFormat48bppRGB,
    &GUID_WICPixelFormat64bppRGBA,
};
static const GUID *const enc_dds_formats[] = {
    &GUID_WICPixelFormat32bppBGRA,
};
static const BYTE dec_wmp_p0[] = {0x49, 0x49, 0xbc, 0x01};
static const BYTE dec_wmp_p1[] = {0x49, 0x49, 0xbc, 0x00};
static const BYTE dec_wmp_m0[] = {0xff, 0xff, 0xff, 0xff};
static const WicPattern dec_wmp_patterns[] = {
    {0, 4, dec_wmp_p0, dec_wmp_m0, 0},
    {0, 4, dec_wmp_p1, dec_wmp_m0, 0},
};
static const GUID *const wmp_formats[] = {
    &GUID_WICPixelFormatBlackWhite,
    &GUID_WICPixelFormat8bppGray,
    &GUID_WICPixelFormat16bppBGR555,
    &GUID_WICPixelFormat16bppBGR565,
    &GUID_WICPixelFormat16bppGray,
    &GUID_WICPixelFormat24bppBGR,
    &GUID_WICPixelFormat24bppRGB,
    &GUID_WICPixelFormat32bppBGR,
    &GUID_WICPixelFormat32bppBGRA,
    &GUID_WICPixelFormat32bppPBGRA,
    &GUID_WICPixelFormat32bppGrayFloat,
    &GUID_WICPixelFormat48bppRGBFixedPoint,
    &GUID_WICPixelFormat16bppGrayFixedPoint,
    &GUID_WICPixelFormat32bppBGR101010,
    &GUID_WICPixelFormat48bppRGB,
    &GUID_WICPixelFormat64bppRGBA,
    &GUID_WICPixelFormat64bppPRGBA,
    &GUID_WICPixelFormat96bppRGBFixedPoint,
    &GUID_WICPixelFormat128bppRGBAFloat,
    &GUID_WICPixelFormat128bppPRGBAFloat,
    &GUID_WICPixelFormat128bppRGBFloat,
    &GUID_WICPixelFormat32bppCMYK,
    &GUID_WICPixelFormat64bppRGBAFixedPoint,
    &GUID_WICPixelFormat128bppRGBAFixedPoint,
    &GUID_WICPixelFormat64bppCMYK,
    &GUID_WICPixelFormat24bpp3Channels,
    &GUID_WICPixelFormat32bpp4Channels,
    &GUID_WICPixelFormat40bpp5Channels,
    &GUID_WICPixelFormat48bpp6Channels,
    &GUID_WICPixelFormat56bpp7Channels,
    &GUID_WICPixelFormat64bpp8Channels,
    &GUID_WICPixelFormat48bpp3Channels,
    &GUID_WICPixelFormat64bpp4Channels,
    &GUID_WICPixelFormat80bpp5Channels,
    &GUID_WICPixelFormat96bpp6Channels,
    &GUID_WICPixelFormat112bpp7Channels,
    &GUID_WICPixelFormat128bpp8Channels,
    &GUID_WICPixelFormat40bppCMYKAlpha,
    &GUID_WICPixelFormat80bppCMYKAlpha,
    &GUID_WICPixelFormat32bpp3ChannelsAlpha,
    &GUID_WICPixelFormat40bpp4ChannelsAlpha,
    &GUID_WICPixelFormat48bpp5ChannelsAlpha,
    &GUID_WICPixelFormat56bpp6ChannelsAlpha,
    &GUID_WICPixelFormat64bpp7ChannelsAlpha,
    &GUID_WICPixelFormat72bpp8ChannelsAlpha,
    &GUID_WICPixelFormat64bpp3ChannelsAlpha,
    &GUID_WICPixelFormat80bpp4ChannelsAlpha,
    &GUID_WICPixelFormat96bpp5ChannelsAlpha,
    &GUID_WICPixelFormat112bpp6ChannelsAlpha,
    &GUID_WICPixelFormat128bpp7ChannelsAlpha,
    &GUID_WICPixelFormat144bpp8ChannelsAlpha,
    &GUID_WICPixelFormat64bppRGBAHalf,
    &GUID_WICPixelFormat48bppRGBHalf,
    &GUID_WICPixelFormat32bppRGBE,
    &GUID_WICPixelFormat16bppGrayHalf,
    &GUID_WICPixelFormat32bppGrayFixedPoint,
    &GUID_WICPixelFormat64bppRGBFixedPoint,
    &GUID_WICPixelFormat128bppRGBFixedPoint,
    &GUID_WICPixelFormat64bppRGBHalf,
};

static const WicCodec g_decoders[] = {
    {&CLSID_WICBmpDecoder, &GUID_ContainerFormatBmp, L"BMP Decoder", L"1.0.0.0", L"1.0.0.0", L"image/bmp", L".bmp,.dib,.rle",
     nullptr, nullptr, L"1.0.0.0", 0, 0, 1, 0, 3,
     1, dec_bmp_patterns, WIC_ARRAY_SIZE(dec_bmp_formats), dec_bmp_formats, &wic_dec_bmp},
    {&CLSID_WICGifDecoder, &GUID_ContainerFormatGif, L"GIF Decoder", L"1.0.0.0", L"1.0.0.0", L"image/gif", L".gif",
     nullptr, nullptr, L"1.0.0.0", 1, 1, 1, 1, 3,
     2, dec_gif_patterns, WIC_ARRAY_SIZE(dec_gif_formats), dec_gif_formats, &wic_dec_gif},
    {&CLSID_WICIcoDecoder, &GUID_ContainerFormatIco, L"ICO Decoder", L"1.0.0.0", L"1.0.0.0", L"image/ico,image/x-icon", L".ico,.icon",
     nullptr, nullptr, L"1.0.0.0", 0, 1, 1, 1, 3,
     1, dec_ico_patterns, WIC_ARRAY_SIZE(dec_ico_formats), dec_ico_formats, &wic_dec_ico},
    {&CLSID_WICCurDecoder, &GUID_ContainerFormatCur, L"CUR Decoder", L"1.0.0.0", L"1.0.0.0", nullptr, L".cur",
     nullptr, nullptr, L"1.0.0.0", 0, 1, 1, 1, 3,
     1, dec_cur_patterns, WIC_ARRAY_SIZE(dec_cur_formats), dec_cur_formats, &wic_dec_cur},
    {&CLSID_WICJpegDecoder, &GUID_ContainerFormatJpeg, L"JPEG Decoder", L"1.0.0.0", L"1.0.0.0", L"image/jpeg,image/jpe,image/jpg", L".jpeg,.jpe,.jpg,.jfif,.exif",
     nullptr, nullptr, L"1.0.0.0", 0, 0, 0, 0, 3,
     1, dec_jpeg_patterns, WIC_ARRAY_SIZE(dec_jpeg_formats), dec_jpeg_formats, &wic_dec_jpeg},
    {&CLSID_WICPngDecoder1, &GUID_ContainerFormatPng, L"PNG Decoder", L"1.0.0.0", L"1.0.0.0", L"image/png", L".png",
     nullptr, nullptr, L"1.0.0.0", 0, 1, 1, 0, 1,
     1, dec_png1_patterns, WIC_ARRAY_SIZE(dec_png1_formats), dec_png1_formats, &wic_dec_png1},
    {&CLSID_WICPngDecoder2, &GUID_ContainerFormatPng, L"PNG Decoder", L"2.0.0.0", L"1.0.0.0", L"image/png", L".png",
     nullptr, nullptr, L"1.0.0.0", 0, 1, 1, 0, 2,
     1, dec_png_patterns, WIC_ARRAY_SIZE(dec_png_formats), dec_png_formats, &wic_dec_png},
    {&CLSID_WICTiffDecoder, &GUID_ContainerFormatTiff, L"TIFF Decoder", L"1.0.0.0", L"1.0.0.0", L"image/tiff,image/tif", L".tiff,.tif",
     nullptr, nullptr, L"1.0.0.0", 0, 1, 1, 1, 3,
     2, dec_tiff_patterns, WIC_ARRAY_SIZE(dec_tiff_formats), dec_tiff_formats, &wic_dec_tiff},
    {&CLSID_WICDdsDecoder, &GUID_ContainerFormatDds, L"DDS Decoder", L"1.0.0.0", L"1.0.0.0", L"image/vnd.ms-dds", L".dds",
     nullptr, nullptr, L"1.0.0.0", 0, 0, 0, 1, 3,
     1, dec_dds_patterns, WIC_ARRAY_SIZE(dec_dds_formats), dec_dds_formats, &wic_dec_dds},
    {&CLSID_WICWmpDecoder, &GUID_ContainerFormatWmp, L"WMPhoto Decoder", L"1.0.0.0", L"1.0.0.0", L"image/vnd.ms-photo", L".wdp,.jxr",
     nullptr, nullptr, L"1.0.0.0", 0, 0, 1, 0, 3,
     2, dec_wmp_patterns, WIC_ARRAY_SIZE(wmp_formats), wmp_formats, &wic_dec_wmp},
};

static const WicCodec g_encoders[] = {
    {&CLSID_WICBmpEncoder, &GUID_ContainerFormatBmp, L"BMP Encoder", L"1.0.0.0", L"1.0.0.0", L"image/bmp", L".bmp,.dib,.rle",
     nullptr, nullptr, L"1.0.0.0", 0, 0, 1, 0, 3,
     0, nullptr, WIC_ARRAY_SIZE(enc_bmp_formats), enc_bmp_formats, &wic_enc_bmp},
    {&CLSID_WICGifEncoder, &GUID_ContainerFormatGif, L"GIF Encoder", L"1.0.0.0", L"1.0.0.0", L"image/gif", L".gif",
     nullptr, nullptr, L"1.0.0.0", 1, 1, 1, 1, 3,
     0, nullptr, WIC_ARRAY_SIZE(enc_gif_formats), enc_gif_formats, &wic_enc_gif},
    {&CLSID_WICJpegEncoder, &GUID_ContainerFormatJpeg, L"JPEG Encoder", L"1.0.0.0", L"1.0.0.0", L"image/jpeg,image/jpe,image/jpg", L".jpeg,.jpe,.jpg,.jfif,.exif",
     nullptr, nullptr, L"1.0.0.0", 0, 0, 0, 0, 3,
     0, nullptr, WIC_ARRAY_SIZE(enc_jpeg_formats), enc_jpeg_formats, &wic_enc_jpeg},
    {&CLSID_WICPngEncoder, &GUID_ContainerFormatPng, L"PNG Encoder", L"1.0.0.0", L"1.0.0.0", L"image/png", L".png",
     nullptr, nullptr, L"1.0.0.0", 0, 1, 1, 0, 3,
     0, nullptr, WIC_ARRAY_SIZE(enc_png_formats), enc_png_formats, &wic_enc_png},
    {&CLSID_WICTiffEncoder, &GUID_ContainerFormatTiff, L"TIFF Encoder", L"1.0.0.0", L"1.0.0.0", L"image/tiff,image/tif", L".tiff,.tif",
     nullptr, nullptr, L"1.0.0.0", 0, 1, 1, 1, 3,
     0, nullptr, WIC_ARRAY_SIZE(enc_tiff_formats), enc_tiff_formats, &wic_enc_tiff},
    {&CLSID_WICDdsEncoder, &GUID_ContainerFormatDds, L"DDS Encoder", L"1.0.0.0", L"1.0.0.0", L"image/vnd.ms-dds", L".dds",
     nullptr, nullptr, L"1.0.0.0", 0, 0, 0, 1, 3,
     0, nullptr, WIC_ARRAY_SIZE(enc_dds_formats), enc_dds_formats, &wic_enc_dds},
    {&CLSID_WICWmpEncoder, &GUID_ContainerFormatWmp, L"WMPhoto Encoder", L"1.0.0.0", L"1.0.0.0", L"image/vnd.ms-photo", L".wdp,.jxr",
     nullptr, nullptr, L"1.0.0.0", 0, 0, 1, 0, 3,
     0, nullptr, WIC_ARRAY_SIZE(wmp_formats), wmp_formats, &wic_enc_wmp},
};

