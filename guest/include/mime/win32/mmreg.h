/*
 * wave formats, shared by winmm, msacm32, dsound and their callers.
 */
#ifndef MIME_WIN32_MMREG_H
#define MIME_WIN32_MMREG_H

#include <mime/win32/winbase.h>

#define WAVE_FORMAT_UNKNOWN 0x0000
#define WAVE_FORMAT_PCM 0x0001
#define WAVE_FORMAT_ADPCM 0x0002
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#define WAVE_FORMAT_ALAW 0x0006
#define WAVE_FORMAT_MULAW 0x0007
#define WAVE_FORMAT_IMA_ADPCM 0x0011
#define WAVE_FORMAT_GSM610 0x0031
#define WAVE_FORMAT_MPEG 0x0050
#define WAVE_FORMAT_MPEGLAYER3 0x0055
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE

#pragma pack(push, 1)
typedef struct tWAVEFORMATEX {
    WORD wFormatTag;
    WORD nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD nBlockAlign;
    WORD wBitsPerSample;
    WORD cbSize;
} WAVEFORMATEX, *PWAVEFORMATEX, *LPWAVEFORMATEX;
typedef const WAVEFORMATEX *LPCWAVEFORMATEX;

typedef struct waveformat_tag {
    WORD wFormatTag;
    WORD nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD nBlockAlign;
} WAVEFORMAT, *PWAVEFORMAT, *LPWAVEFORMAT;

typedef struct pcmwaveformat_tag {
    WAVEFORMAT wf;
    WORD wBitsPerSample;
} PCMWAVEFORMAT, *PPCMWAVEFORMAT, *LPPCMWAVEFORMAT;

typedef struct {
    WAVEFORMATEX Format;
    union {
        WORD wValidBitsPerSample;
        WORD wSamplesPerBlock;
        WORD wReserved;
    } Samples;
    DWORD dwChannelMask;
    GUID SubFormat;
} WAVEFORMATEXTENSIBLE, *PWAVEFORMATEXTENSIBLE;

typedef struct adpcmcoef_tag {
    short iCoef1;
    short iCoef2;
} ADPCMCOEFSET;

typedef struct adpcmwaveformat_tag {
    WAVEFORMATEX wfx;
    WORD wSamplesPerBlock;
    WORD wNumCoef;
    ADPCMCOEFSET aCoef[];
} ADPCMWAVEFORMAT, *PADPCMWAVEFORMAT;

typedef struct ima_adpcmwaveformat_tag {
    WAVEFORMATEX wfx;
    WORD wSamplesPerBlock;
} IMAADPCMWAVEFORMAT, *PIMAADPCMWAVEFORMAT;

typedef struct gsm610waveformat_tag {
    WAVEFORMATEX wfx;
    WORD wSamplesPerBlock;
} GSM610WAVEFORMAT, *PGSM610WAVEFORMAT;

typedef struct mpeglayer3waveformat_tag {
    WAVEFORMATEX wfx;
    WORD wID;
    DWORD fdwFlags;
    WORD nBlockSize;
    WORD nFramesPerBlock;
    WORD nCodecDelay;
} MPEGLAYER3WAVEFORMAT, *PMPEGLAYER3WAVEFORMAT;
#pragma pack(pop)

#define SPEAKER_FRONT_LEFT 0x1
#define SPEAKER_FRONT_RIGHT 0x2
#define SPEAKER_FRONT_CENTER 0x4
#define SPEAKER_LOW_FREQUENCY 0x8
#define SPEAKER_BACK_LEFT 0x10
#define SPEAKER_BACK_RIGHT 0x20
#define SPEAKER_SIDE_LEFT 0x200
#define SPEAKER_SIDE_RIGHT 0x400

/* KSDATAFORMAT_SUBTYPE_PCM and KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, as the low word of their guids says. */
#define MIME_KSDATAFORMAT_SUBTYPE(tag) {(tag), 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}}

#endif
