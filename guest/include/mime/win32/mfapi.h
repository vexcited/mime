/*
 * mfplat.dll: platform startup, the object factories, attribute
 * serialization, work queues, format helpers and the attribute and
 * media type guids.
 */
#ifndef MIME_WIN32_MFAPI_H
#define MIME_WIN32_MFAPI_H

#include <mime/win32/mfidl.h>

#ifdef MIME_BUILD_MFPLAT
#define MFAPI
#else
#define MFAPI DECLSPEC_IMPORT
#endif

#define MF_SDK_VERSION 0x0002
#define MF_API_VERSION 0x0070
#define MF_VERSION (MF_SDK_VERSION << 16 | MF_API_VERSION)
#define MFSTARTUP_NOSOCKET 0x1
#define MFSTARTUP_LITE MFSTARTUP_NOSOCKET
#define MFSTARTUP_FULL 0

#define MF_1_BYTE_ALIGNMENT 0x00000000
#define MF_2_BYTE_ALIGNMENT 0x00000001
#define MF_4_BYTE_ALIGNMENT 0x00000003
#define MF_8_BYTE_ALIGNMENT 0x00000007
#define MF_16_BYTE_ALIGNMENT 0x0000000F
#define MF_32_BYTE_ALIGNMENT 0x0000001F
#define MF_64_BYTE_ALIGNMENT 0x0000003F
#define MF_128_BYTE_ALIGNMENT 0x0000007F
#define MF_256_BYTE_ALIGNMENT 0x000000FF
#define MF_512_BYTE_ALIGNMENT 0x000001FF

typedef ULONGLONG MFWORKITEM_KEY;

#define MFT_ENUM_FLAG_SYNCMFT 0x00000001
#define MFT_ENUM_FLAG_ASYNCMFT 0x00000002
#define MFT_ENUM_FLAG_HARDWARE 0x00000004
#define MFT_ENUM_FLAG_FIELDOFUSE 0x00000008
#define MFT_ENUM_FLAG_LOCALMFT 0x00000010
#define MFT_ENUM_FLAG_TRANSCODE_ONLY 0x00000020
#define MFT_ENUM_FLAG_SORTANDFILTER 0x00000040
#define MFT_ENUM_FLAG_ALL 0x0000003F

typedef struct _MFT_REGISTER_TYPE_INFO {
    GUID guidMajorType;
    GUID guidSubtype;
} MFT_REGISTER_TYPE_INFO;

typedef enum _MFWaveFormatExConvertFlags {
    MFWaveFormatExConvertFlag_Normal = 0,
    MFWaveFormatExConvertFlag_ForceExtensible = 1,
} MFWaveFormatExConvertFlags;

typedef void(CALLBACK *MFPERIODICCALLBACK)(IUnknown *context);

/* ------------------------------------------------------------------ attribute keys */

MIME_DEFINE_GUID(MF_MT_MAJOR_TYPE, 0x48EBA18E, 0xF8C9, 0x4687, 0xBF, 0x11, 0x0A, 0x74, 0xC9, 0xF9, 0x6A, 0x8F);
MIME_DEFINE_GUID(MF_MT_SUBTYPE, 0xF7E34C9A, 0x42E8, 0x4714, 0xB7, 0x4B, 0xCB, 0x29, 0xD7, 0x2C, 0x35, 0xE5);
MIME_DEFINE_GUID(MF_MT_ALL_SAMPLES_INDEPENDENT, 0xC9173739, 0x5E56, 0x461C, 0xB7, 0x13, 0x46, 0xFB, 0x99, 0x5C, 0xB9,
                 0x5F);
MIME_DEFINE_GUID(MF_MT_FIXED_SIZE_SAMPLES, 0xB8EBEFAF, 0xB718, 0x4E04, 0xB0, 0xA9, 0x11, 0x67, 0x75, 0xE3, 0x32, 0x1B);
MIME_DEFINE_GUID(MF_MT_COMPRESSED, 0x3AFD0CEE, 0x18F2, 0x4BA5, 0xA1, 0x10, 0x8B, 0xEA, 0x50, 0x2E, 0x1F, 0x92);
MIME_DEFINE_GUID(MF_MT_SAMPLE_SIZE, 0xDAD3AB78, 0x1990, 0x408B, 0xBC, 0xE2, 0xEB, 0xA6, 0x73, 0xDA, 0xCC, 0x10);
MIME_DEFINE_GUID(MF_MT_USER_DATA, 0xB6BC765F, 0x4C3B, 0x40A4, 0xBD, 0x51, 0x25, 0x35, 0xB6, 0x6F, 0xE0, 0x9D);
MIME_DEFINE_GUID(MF_MT_FRAME_SIZE, 0x1652C33D, 0xD6B2, 0x4012, 0xB8, 0x34, 0x72, 0x03, 0x08, 0x49, 0xA3, 0x7D);
MIME_DEFINE_GUID(MF_MT_FRAME_RATE, 0xC459A2E8, 0x3D2C, 0x4E44, 0xB1, 0x32, 0xFE, 0xE5, 0x15, 0x6C, 0x7B, 0xB0);
MIME_DEFINE_GUID(MF_MT_FRAME_RATE_RANGE_MIN, 0xD2E7558C, 0xDC1F, 0x403F, 0x9A, 0x72, 0xD2, 0x8B, 0xB1, 0xEB, 0x3B,
                 0x5E);
MIME_DEFINE_GUID(MF_MT_FRAME_RATE_RANGE_MAX, 0xE3371D41, 0xB4CF, 0x4A05, 0xBD, 0x4E, 0x20, 0xB8, 0x8B, 0xB2, 0xC4,
                 0xD6);
MIME_DEFINE_GUID(MF_MT_PIXEL_ASPECT_RATIO, 0xC6376A1E, 0x8D0A, 0x4027, 0xBE, 0x45, 0x6D, 0x9A, 0x0A, 0xD3, 0x9B, 0xB6);
MIME_DEFINE_GUID(MF_MT_INTERLACE_MODE, 0xE2724BB8, 0xE676, 0x4806, 0xB4, 0xB2, 0xA8, 0xD6, 0xEF, 0xB4, 0x4C, 0xCD);
MIME_DEFINE_GUID(MF_MT_DEFAULT_STRIDE, 0x644B4E48, 0x1E02, 0x4516, 0xB0, 0xEB, 0xC0, 0x1C, 0xA9, 0xD4, 0x9A, 0xC6);
MIME_DEFINE_GUID(MF_MT_AVG_BITRATE, 0x20332624, 0xFB0D, 0x4D9E, 0xBD, 0x0D, 0xCB, 0xF6, 0x78, 0x6C, 0x10, 0x2E);
MIME_DEFINE_GUID(MF_MT_VIDEO_ROTATION, 0xC380465D, 0x2271, 0x428C, 0x9B, 0x83, 0xEC, 0xEA, 0x3B, 0x4A, 0x85, 0xC1);
MIME_DEFINE_GUID(MF_MT_VIDEO_NOMINAL_RANGE, 0xC21B8EE5, 0xB956, 0x4071, 0x8D, 0xAF, 0x32, 0x5E, 0xDF, 0x5C, 0xAB, 0x11);
MIME_DEFINE_GUID(MF_MT_YUV_MATRIX, 0x3E23D450, 0x2C75, 0x4D25, 0xA0, 0x0E, 0xB9, 0x16, 0x70, 0xD1, 0x23, 0x27);
MIME_DEFINE_GUID(MF_MT_TRANSFER_FUNCTION, 0x5FB0FCE9, 0xBE5C, 0x4935, 0xA8, 0x11, 0xEC, 0x83, 0x8F, 0x8E, 0xED, 0x93);
MIME_DEFINE_GUID(MF_MT_VIDEO_PRIMARIES, 0xDBFBE4D7, 0x0740, 0x4EE0, 0x81, 0x92, 0x85, 0x0A, 0xB0, 0xE2, 0x19, 0x35);
MIME_DEFINE_GUID(MF_MT_MPEG_SEQUENCE_HEADER, 0x3C036DE7, 0x3AD0, 0x4C9E, 0x92, 0x16, 0xEE, 0x6D, 0x6A, 0xC2, 0x1C,
                 0xB3);
MIME_DEFINE_GUID(MF_MT_MPEG2_PROFILE, 0xAD76A80B, 0x2D5C, 0x4E0B, 0xB3, 0x75, 0x64, 0xE5, 0x20, 0x13, 0x70, 0x36);
MIME_DEFINE_GUID(MF_MT_MPEG2_LEVEL, 0x96F66574, 0x11C5, 0x4015, 0x86, 0x66, 0xBF, 0xF5, 0x16, 0x43, 0x6D, 0xA7);
MIME_DEFINE_GUID(MF_MT_AUDIO_NUM_CHANNELS, 0x37E48BF5, 0x645E, 0x4C5B, 0x89, 0xDE, 0xAD, 0xA9, 0xE2, 0x9B, 0x69, 0x6A);
MIME_DEFINE_GUID(MF_MT_AUDIO_SAMPLES_PER_SECOND, 0x5FAEEAE7, 0x0290, 0x4C31, 0x9E, 0x8A, 0xC5, 0x34, 0xF6, 0x8D, 0x9D,
                 0xBA);
MIME_DEFINE_GUID(MF_MT_AUDIO_FLOAT_SAMPLES_PER_SECOND, 0xFB3B724A, 0xCFB5, 0x4319, 0xAE, 0xFE, 0x6E, 0x42, 0xB2, 0x40,
                 0x61, 0x32);
MIME_DEFINE_GUID(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 0x1AAB75C8, 0xCFEF, 0x451C, 0xAB, 0x95, 0xAC, 0x03, 0x4B, 0x8E, 0x17,
                 0x31);
MIME_DEFINE_GUID(MF_MT_AUDIO_BLOCK_ALIGNMENT, 0x322DE230, 0x9EEB, 0x43BD, 0xAB, 0x7A, 0xFF, 0x41, 0x22, 0x51, 0x54,
                 0x1D);
MIME_DEFINE_GUID(MF_MT_AUDIO_BITS_PER_SAMPLE, 0xF2DEB57F, 0x40FA, 0x4764, 0xAA, 0x33, 0xED, 0x4F, 0x2D, 0x1F, 0xF6,
                 0x69);
MIME_DEFINE_GUID(MF_MT_AUDIO_VALID_BITS_PER_SAMPLE, 0xD9BF8D6A, 0x9530, 0x4B7C, 0x9D, 0xDF, 0xFF, 0x6F, 0xD5, 0x8B,
                 0xBD, 0x06);
MIME_DEFINE_GUID(MF_MT_AUDIO_SAMPLES_PER_BLOCK, 0xAAB15AAC, 0xE13A, 0x4995, 0x92, 0x22, 0x50, 0x1E, 0xA1, 0x5C, 0x68,
                 0x77);
MIME_DEFINE_GUID(MF_MT_AUDIO_CHANNEL_MASK, 0x55FB5765, 0x644A, 0x4CAF, 0x84, 0x79, 0x93, 0x89, 0x83, 0xBB, 0x15, 0x88);
MIME_DEFINE_GUID(MF_MT_AUDIO_PREFER_WAVEFORMATEX, 0xA901AABA, 0xE037, 0x458A, 0xBD, 0xF6, 0x54, 0x5B, 0xE2, 0x07, 0x40,
                 0x42);
MIME_DEFINE_GUID(MF_MT_AAC_PAYLOAD_TYPE, 0xBFBABE79, 0x7434, 0x4D1C, 0x94, 0xF0, 0x72, 0xA3, 0xB9, 0xE1, 0x71, 0x88);
MIME_DEFINE_GUID(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x7632F0E6, 0x9538, 0x4D61, 0xAC, 0xDA, 0xEA, 0x29, 0xC8,
                 0xC1, 0x44, 0x56);
MIME_DEFINE_GUID(MFSampleExtension_CleanPoint, 0x9CDF01D8, 0xA0F0, 0x43BA, 0xB0, 0x77, 0xEA, 0xA0, 0x6C, 0xBD, 0x72,
                 0x8A);
MIME_DEFINE_GUID(MFSampleExtension_Discontinuity, 0x9CDF01D9, 0xA0F0, 0x43BA, 0xB0, 0x77, 0xEA, 0xA0, 0x6C, 0xBD,
                 0x72, 0x8A);
/* MF_MT_INTERLACE_MODE and MF_MT_VIDEO_NOMINAL_RANGE values. */
#define MFVideoInterlace_Progressive 2
#define MFNominalRange_Unknown 0
#define MFNominalRange_0_255 1
#define MFNominalRange_16_235 2
#define MFNominalRange_48_208 3

/* when the device captured a sample, on the clock of MFGetSystemTime, and whether it came damaged. */
MIME_DEFINE_GUID(MFSampleExtension_DeviceReferenceSystemTime, 0x6523775A, 0xBA2D, 0x405F, 0xB2, 0xC5, 0x01, 0xFF, 0x88,
                 0xE2, 0xE8, 0xF6);
MIME_DEFINE_GUID(MFSampleExtension_FrameCorruption, 0xB4DD4A8C, 0x0BEB, 0x44C4, 0x8B, 0x75, 0xB0, 0x2B, 0x91, 0x3B, 0x04,
                 0xF0);

MIME_DEFINE_GUID(MFSampleExtension_Token, 0x8294DA66, 0xF328, 0x4805, 0xB5, 0x51, 0x00, 0xDE, 0xB4, 0xC5, 0x7A, 0x61);
MIME_DEFINE_GUID(MF_SA_D3D11_AWARE, 0x206B4FC8, 0xFCF9, 0x4C51, 0xAF, 0xE3, 0x97, 0x64, 0x36, 0x9E, 0x33, 0xA0);
MIME_DEFINE_GUID(MF_SA_D3D11_BINDFLAGS, 0xEACF97AD, 0x065C, 0x4408, 0xBE, 0xE3, 0xFD, 0xCB, 0xFD, 0x12, 0x8B, 0xE2);
MIME_DEFINE_GUID(MF_LOW_LATENCY, 0x9C27891A, 0xED7A, 0x40E1, 0x88, 0xE8, 0xB2, 0x27, 0x27, 0xA0, 0x24, 0xEE);
MIME_DEFINE_GUID(MF_TRANSFORM_CATEGORY_Attribute, 0xCEABBA49, 0x506D, 0x4757, 0xA6, 0xFF, 0x66, 0xC1, 0x84, 0x98, 0x7E,
                 0x4E);

/* ------------------------------------------------------------------ media types and formats */

MIME_DEFINE_GUID(MFMediaType_Default, 0x81A412E6, 0x8103, 0x4B06, 0x85, 0x7F, 0x18, 0x62, 0x78, 0x10, 0x24, 0xAC);
MIME_DEFINE_GUID(MFMediaType_Audio, 0x73647561, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);
MIME_DEFINE_GUID(MFMediaType_Video, 0x73646976, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);
MIME_DEFINE_GUID(MFMediaType_Protected, 0x7B4B6FE6, 0x9D04, 0x4494, 0xBE, 0x14, 0x7E, 0x0B, 0xD0, 0x76, 0xC8, 0xE4);
MIME_DEFINE_GUID(MFMediaType_SAMI, 0xE69669A0, 0x3DCD, 0x40CB, 0x9E, 0x2E, 0x37, 0x08, 0x38, 0x7C, 0x06, 0x16);
MIME_DEFINE_GUID(MFMediaType_Script, 0x72178C22, 0xE45B, 0x11D5, 0xBC, 0x2A, 0x00, 0xB0, 0xD0, 0xF3, 0xF4, 0xAB);
MIME_DEFINE_GUID(MFMediaType_Image, 0x72178C23, 0xE45B, 0x11D5, 0xBC, 0x2A, 0x00, 0xB0, 0xD0, 0xF3, 0xF4, 0xAB);
MIME_DEFINE_GUID(MFMediaType_HTML, 0x72178C24, 0xE45B, 0x11D5, 0xBC, 0x2A, 0x00, 0xB0, 0xD0, 0xF3, 0xF4, 0xAB);
MIME_DEFINE_GUID(MFMediaType_Binary, 0x72178C25, 0xE45B, 0x11D5, 0xBC, 0x2A, 0x00, 0xB0, 0xD0, 0xF3, 0xF4, 0xAB);
MIME_DEFINE_GUID(MFMediaType_FileTransfer, 0x72178C26, 0xE45B, 0x11D5, 0xBC, 0x2A, 0x00, 0xB0, 0xD0, 0xF3, 0xF4, 0xAB);
MIME_DEFINE_GUID(MFMediaType_Stream, 0xE436EB83, 0x524F, 0x11CE, 0x9F, 0x53, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70);

/* audio subtypes are a wave format tag in the base guid, video ones a fourcc or d3d format. */
MIME_DEFINE_GUID(MFAudioFormat_Base, 0x00000000, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);
#define MFVideoFormat_Base MFAudioFormat_Base
#define MIME_MF_FORMAT_GUID(name, code)                                                                                \
    MIME_DEFINE_GUID(name, code, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71)
MIME_MF_FORMAT_GUID(MFAudioFormat_PCM, WAVE_FORMAT_PCM);
MIME_MF_FORMAT_GUID(MFAudioFormat_Float, WAVE_FORMAT_IEEE_FLOAT);
MIME_MF_FORMAT_GUID(MFAudioFormat_MP3, WAVE_FORMAT_MPEGLAYER3);
MIME_MF_FORMAT_GUID(MFAudioFormat_MPEG, WAVE_FORMAT_MPEG);
MIME_MF_FORMAT_GUID(MFAudioFormat_AAC, 0x1610);
MIME_MF_FORMAT_GUID(MFAudioFormat_Opus, 0x704F);
MIME_MF_FORMAT_GUID(MFVideoFormat_RGB32, 22);
MIME_MF_FORMAT_GUID(MFVideoFormat_ARGB32, 21);
MIME_MF_FORMAT_GUID(MFVideoFormat_RGB24, 20);
MIME_MF_FORMAT_GUID(MFVideoFormat_NV12, 0x3231564E);
MIME_MF_FORMAT_GUID(MFVideoFormat_YUY2, 0x32595559);
MIME_MF_FORMAT_GUID(MFVideoFormat_I420, 0x30323449);
MIME_MF_FORMAT_GUID(MFVideoFormat_IYUV, 0x56555949);
MIME_MF_FORMAT_GUID(MFVideoFormat_YV12, 0x32315659);
MIME_MF_FORMAT_GUID(MFVideoFormat_UYVY, 0x59565955);
MIME_MF_FORMAT_GUID(MFVideoFormat_P010, 0x30313050);
MIME_MF_FORMAT_GUID(MFVideoFormat_MJPG, 0x47504A4D);
MIME_MF_FORMAT_GUID(MFVideoFormat_H264, 0x34363248);
MIME_MF_FORMAT_GUID(MFVideoFormat_HEVC, 0x43564548);
MIME_MF_FORMAT_GUID(MFVideoFormat_VP80, 0x30385056);
MIME_MF_FORMAT_GUID(MFVideoFormat_VP90, 0x30395056);
MIME_MF_FORMAT_GUID(MFVideoFormat_AV1, 0x31305641);

MIME_DEFINE_GUID(MFT_CATEGORY_VIDEO_DECODER, 0xD6C02D4B, 0x6833, 0x45B4, 0x97, 0x1A, 0x05, 0xA4, 0xB0, 0x4B, 0xAB,
                 0x91);
MIME_DEFINE_GUID(MFT_CATEGORY_VIDEO_ENCODER, 0xF79EAC7D, 0xE545, 0x4387, 0xBD, 0xEE, 0xD6, 0x47, 0xD7, 0xBD, 0xE4,
                 0x2A);
MIME_DEFINE_GUID(MFT_CATEGORY_VIDEO_EFFECT, 0x12E17C21, 0x532C, 0x4A6E, 0x8A, 0x1C, 0x40, 0x82, 0x5A, 0x73, 0x63, 0x97);
MIME_DEFINE_GUID(MFT_CATEGORY_MULTIPLEXER, 0x059C561E, 0x05AE, 0x4B61, 0xB6, 0x9D, 0x55, 0xB6, 0x1E, 0xE5, 0x4A, 0x7B);
MIME_DEFINE_GUID(MFT_CATEGORY_DEMULTIPLEXER, 0xA8700A7A, 0x939B, 0x44C5, 0x99, 0xD7, 0x76, 0x22, 0x6B, 0x23, 0xB3,
                 0xF1);
MIME_DEFINE_GUID(MFT_CATEGORY_AUDIO_DECODER, 0x9EA73FB4, 0xEF7A, 0x4559, 0x8D, 0x5D, 0x71, 0x9D, 0x8F, 0x04, 0x26,
                 0xC7);
MIME_DEFINE_GUID(MFT_CATEGORY_AUDIO_ENCODER, 0x91C64BD0, 0xF91E, 0x4D8C, 0x92, 0x76, 0xDB, 0x24, 0x82, 0x79, 0xD9,
                 0x75);
MIME_DEFINE_GUID(MFT_CATEGORY_AUDIO_EFFECT, 0x11064C48, 0x3648, 0x4ED0, 0x93, 0x2E, 0x05, 0xCE, 0x8A, 0xC8, 0x11, 0xB7);
MIME_DEFINE_GUID(MFT_CATEGORY_VIDEO_PROCESSOR, 0x302EA3FC, 0xAA5F, 0x47F9, 0x9F, 0x7A, 0xC2, 0x18, 0x8B, 0xB1, 0x63,
                 0x02);
MIME_DEFINE_GUID(MFT_CATEGORY_OTHER, 0x90175D57, 0xB7EA, 0x4901, 0xAE, 0xB3, 0x93, 0x3A, 0x87, 0x47, 0x75, 0x6F);

/* ------------------------------------------------------------------ functions */

MFAPI HRESULT WINAPI MFStartup(ULONG version, DWORD flags);
MFAPI HRESULT WINAPI MFShutdown(void);
MFAPI HRESULT WINAPI MFLockPlatform(void);
MFAPI HRESULT WINAPI MFUnlockPlatform(void);
MFAPI MFTIME WINAPI MFGetSystemTime(void);
MFAPI LONGLONG WINAPI MFllMulDiv(LONGLONG a, LONGLONG b, LONGLONG c, LONGLONG d);
MFAPI HRESULT WINAPI MFFrameRateToAverageTimePerFrame(UINT32 numerator, UINT32 denominator, UINT64 *time);
MFAPI HRESULT WINAPI MFAverageTimePerFrameToFrameRate(UINT64 time, UINT32 *numerator, UINT32 *denominator);
MFAPI void *WINAPI MFHeapAlloc(SIZE_T size, ULONG flags, char *file, int line, int type);
MFAPI void WINAPI MFHeapFree(void *p);

MFAPI HRESULT WINAPI MFCreateAttributes(IMFAttributes **attributes, UINT32 size);
MFAPI HRESULT WINAPI MFGetAttributesAsBlobSize(IMFAttributes *attributes, UINT32 *size);
MFAPI HRESULT WINAPI MFGetAttributesAsBlob(IMFAttributes *attributes, UINT8 *buffer, UINT size);
MFAPI HRESULT WINAPI MFInitAttributesFromBlob(IMFAttributes *attributes, const UINT8 *buffer, UINT size);
MFAPI HRESULT WINAPI MFSerializeAttributesToStream(IMFAttributes *attributes, DWORD options, IStream *stream);
MFAPI HRESULT WINAPI MFDeserializeAttributesFromStream(IMFAttributes *attributes, DWORD options, IStream *stream);

MFAPI HRESULT WINAPI MFCreateMediaType(IMFMediaType **type);
MFAPI HRESULT WINAPI MFCreateWaveFormatExFromMFMediaType(IMFMediaType *type, WAVEFORMATEX **format, UINT32 *size,
                                                         UINT32 flags);
MFAPI HRESULT WINAPI MFInitMediaTypeFromWaveFormatEx(IMFMediaType *type, const WAVEFORMATEX *format, UINT32 size);

MFAPI HRESULT WINAPI MFCreateMemoryBuffer(DWORD max_length, IMFMediaBuffer **buffer);
MFAPI HRESULT WINAPI MFCreateAlignedMemoryBuffer(DWORD max_length, DWORD alignment, IMFMediaBuffer **buffer);
MFAPI HRESULT WINAPI MFCreateMediaBufferWrapper(IMFMediaBuffer *buffer, DWORD offset, DWORD length,
                                                IMFMediaBuffer **wrapper);
MFAPI HRESULT WINAPI MFSetMinimumMemoryAlignment(DWORD alignment);
MFAPI HRESULT WINAPI MFCreateSample(IMFSample **sample);
MFAPI HRESULT WINAPI MFCreateDXGIDeviceManager(UINT *token, IMFDXGIDeviceManager **manager);
MFAPI HRESULT WINAPI MFLockDXGIDeviceManager(UINT *token, IMFDXGIDeviceManager **manager);
MFAPI HRESULT WINAPI MFUnlockDXGIDeviceManager(void);
MFAPI HRESULT WINAPI MFCreateDXGISurfaceBuffer(REFIID riid, IUnknown *surface, UINT subresource, BOOL bottom_up,
                                               IMFMediaBuffer **buffer);

MFAPI HRESULT WINAPI MFCreateAsyncResult(IUnknown *object, IMFAsyncCallback *callback, IUnknown *state,
                                         IMFAsyncResult **result);
MFAPI HRESULT WINAPI MFInvokeCallback(IMFAsyncResult *result);
MFAPI HRESULT WINAPI MFAllocateWorkQueue(DWORD *queue);
MFAPI HRESULT WINAPI MFLockWorkQueue(DWORD queue);
MFAPI HRESULT WINAPI MFUnlockWorkQueue(DWORD queue);
MFAPI HRESULT WINAPI MFPutWorkItem(DWORD queue, IMFAsyncCallback *callback, IUnknown *state);
MFAPI HRESULT WINAPI MFPutWorkItem2(DWORD queue, LONG priority, IMFAsyncCallback *callback, IUnknown *state);
MFAPI HRESULT WINAPI MFPutWorkItemEx(DWORD queue, IMFAsyncResult *result);
MFAPI HRESULT WINAPI MFPutWorkItemEx2(DWORD queue, LONG priority, IMFAsyncResult *result);
MFAPI HRESULT WINAPI MFScheduleWorkItem(IMFAsyncCallback *callback, IUnknown *state, INT64 timeout,
                                        MFWORKITEM_KEY *key);
MFAPI HRESULT WINAPI MFScheduleWorkItemEx(IMFAsyncResult *result, INT64 timeout, MFWORKITEM_KEY *key);
MFAPI HRESULT WINAPI MFCancelWorkItem(MFWORKITEM_KEY key);

MFAPI HRESULT WINAPI MFCreateEventQueue(IMFMediaEventQueue **queue);
MFAPI HRESULT WINAPI MFCreateMediaEvent(MediaEventType type, REFGUID extended, HRESULT status, const PROPVARIANT *value,
                                        IMFMediaEvent **event);
MFAPI HRESULT WINAPI MFCreateCollection(IMFCollection **collection);
MFAPI HRESULT WINAPI MFCreatePresentationDescriptor(DWORD count, IMFStreamDescriptor **streams,
                                                    IMFPresentationDescriptor **descriptor);
MFAPI HRESULT WINAPI MFCreateStreamDescriptor(DWORD identifier, DWORD count, IMFMediaType **types,
                                              IMFStreamDescriptor **descriptor);
MFAPI HRESULT WINAPI MFCreateMFByteStreamOnStream(IStream *stream, IMFByteStream **byte_stream);
MFAPI HRESULT WINAPI MFTEnumEx(GUID category, UINT32 flags, const MFT_REGISTER_TYPE_INFO *input,
                               const MFT_REGISTER_TYPE_INFO *output, IMFActivate ***activates, UINT32 *count);
MFAPI HRESULT WINAPI MFTEnum2(GUID category, UINT32 flags, const MFT_REGISTER_TYPE_INFO *input,
                              const MFT_REGISTER_TYPE_INFO *output, IMFAttributes *attributes,
                              IMFActivate ***activates, UINT32 *count);
MFAPI HRESULT WINAPI MFTEnum(GUID category, UINT32 flags, MFT_REGISTER_TYPE_INFO *input,
                             MFT_REGISTER_TYPE_INFO *output, IMFAttributes *attributes, CLSID **clsids, UINT32 *count);

#endif
