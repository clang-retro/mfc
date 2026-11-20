// mmreg.h - waveform format tags and the formats of the codecs the layer
// has (G.711 and IMA ADPCM), with the extensible format.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_MMREG_H
#define _WINPORT_MMREG_H

#include <mmsystem.h>

#define WAVE_FORMAT_UNKNOWN 0x0000
#define WAVE_FORMAT_ADPCM 0x0002
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#define WAVE_FORMAT_ALAW 0x0006
#define WAVE_FORMAT_MULAW 0x0007
#define WAVE_FORMAT_OKI_ADPCM 0x0010
#define WAVE_FORMAT_IMA_ADPCM 0x0011
#define WAVE_FORMAT_DVI_ADPCM 0x0011
#define WAVE_FORMAT_GSM610 0x0031
#define WAVE_FORMAT_G721_ADPCM 0x0040
#define WAVE_FORMAT_MPEGLAYER3 0x0055
#define WAVE_FORMAT_G726_ADPCM 0x0064
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE

typedef struct ima_adpcmwaveformat_tag {
  WAVEFORMATEX wfx;
  WORD wSamplesPerBlock;
} __attribute__((packed)) IMAADPCMWAVEFORMAT, *PIMAADPCMWAVEFORMAT, *LPIMAADPCMWAVEFORMAT;
typedef IMAADPCMWAVEFORMAT DVIADPCMWAVEFORMAT, *PDVIADPCMWAVEFORMAT, *LPDVIADPCMWAVEFORMAT;

typedef struct adpcmcoef_tag {
  short iCoef1;
  short iCoef2;
} ADPCMCOEFSET, *PADPCMCOEFSET, *LPADPCMCOEFSET;
typedef struct adpcmwaveformat_tag {
  WAVEFORMATEX wfx;
  WORD wSamplesPerBlock;
  WORD wNumCoef;
  ADPCMCOEFSET aCoef[1];
} __attribute__((packed)) ADPCMWAVEFORMAT, *PADPCMWAVEFORMAT, *LPADPCMWAVEFORMAT;

typedef struct gsm610waveformat_tag {
  WAVEFORMATEX wfx;
  WORD wSamplesPerBlock;
} __attribute__((packed)) GSM610WAVEFORMAT, *PGSM610WAVEFORMAT, *LPGSM610WAVEFORMAT;

// MPEG layer 3: the format is described, though no codec here reads it.
typedef struct mpeglayer3waveformat_tag {
  WAVEFORMATEX wfx;
  WORD wID;
  DWORD fdwFlags;
  WORD nBlockSize;
  WORD nFramesPerBlock;
  WORD nCodecDelay;
} __attribute__((packed)) MPEGLAYER3WAVEFORMAT, *PMPEGLAYER3WAVEFORMAT, *LPMPEGLAYER3WAVEFORMAT;
#define MPEGLAYER3_WFX_EXTRA_BYTES 12
#define MPEGLAYER3_ID_UNKNOWN 0
#define MPEGLAYER3_ID_MPEG 1
#define MPEGLAYER3_ID_CONSTANTFRAMESIZE 2
#define MPEGLAYER3_FLAG_PADDING_ISO 0x00000000
#define MPEGLAYER3_FLAG_PADDING_ON 0x00000001
#define MPEGLAYER3_FLAG_PADDING_OFF 0x00000002

typedef struct {
  WAVEFORMATEX Format;
  union {
    WORD wValidBitsPerSample;
    WORD wSamplesPerBlock;
    WORD wReserved;
  } Samples;
  DWORD dwChannelMask;
  GUID SubFormat;
} __attribute__((packed)) WAVEFORMATEXTENSIBLE, *PWAVEFORMATEXTENSIBLE;
#define SPEAKER_FRONT_LEFT 0x1
#define SPEAKER_FRONT_RIGHT 0x2
#define SPEAKER_FRONT_CENTER 0x4

#endif
