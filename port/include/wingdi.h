// wingdi.h - the GDI part of the portable Win32 layer: device contexts,
// pens, brushes, fonts, bitmaps, regions, palettes, paths, metafiles and
// printing.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_WINGDI_H
#define _WINPORT_WINGDI_H

#include <winbase.h>

extern "C" {

#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define PALETTERGB(r, g, b) (0x02000000 | RGB(r, g, b))
#define PALETTEINDEX(i) ((COLORREF)(0x01000000 | (DWORD)(WORD)(i)))
#define GetRValue(rgb) (LOBYTE(rgb))
#define GetGValue(rgb) (LOBYTE(((WORD)(rgb)) >> 8))
#define GetBValue(rgb) (LOBYTE((rgb) >> 16))
#define CLR_INVALID 0xFFFFFFFF
#define GDI_ERROR (0xFFFFFFFFL)
#define HGDI_ERROR ((HANDLE)(LONG_PTR)-1)

// Object types
#define OBJ_PEN 1
#define OBJ_BRUSH 2
#define OBJ_DC 3
#define OBJ_METADC 4
#define OBJ_PAL 5
#define OBJ_FONT 6
#define OBJ_BITMAP 7
#define OBJ_REGION 8
#define OBJ_METAFILE 9
#define OBJ_MEMDC 10
#define OBJ_EXTPEN 11
#define OBJ_ENHMETADC 12
#define OBJ_ENHMETAFILE 13
#define OBJ_COLORSPACE 14

// Stock objects
#define WHITE_BRUSH 0
#define LTGRAY_BRUSH 1
#define GRAY_BRUSH 2
#define DKGRAY_BRUSH 3
#define BLACK_BRUSH 4
#define NULL_BRUSH 5
#define HOLLOW_BRUSH NULL_BRUSH
#define WHITE_PEN 6
#define BLACK_PEN 7
#define NULL_PEN 8
#define OEM_FIXED_FONT 10
#define ANSI_FIXED_FONT 11
#define ANSI_VAR_FONT 12
#define SYSTEM_FONT 13
#define DEVICE_DEFAULT_FONT 14
#define DEFAULT_PALETTE 15
#define SYSTEM_FIXED_FONT 16
#define DEFAULT_GUI_FONT 17
#define DC_BRUSH 18
#define DC_PEN 19
#define STOCK_LAST 19

// Raster operations
#define R2_BLACK 1
#define R2_NOTMERGEPEN 2
#define R2_MASKNOTPEN 3
#define R2_NOTCOPYPEN 4
#define R2_MASKPENNOT 5
#define R2_NOT 6
#define R2_XORPEN 7
#define R2_NOTMASKPEN 8
#define R2_MASKPEN 9
#define R2_NOTXORPEN 10
#define R2_NOP 11
#define R2_MERGENOTPEN 12
#define R2_COPYPEN 13
#define R2_MERGEPENNOT 14
#define R2_MERGEPEN 15
#define R2_WHITE 16
#define SRCCOPY (DWORD)0x00CC0020
#define SRCPAINT (DWORD)0x00EE0086
#define SRCAND (DWORD)0x008800C6
#define SRCINVERT (DWORD)0x00660046
#define SRCERASE (DWORD)0x00440328
#define NOTSRCCOPY (DWORD)0x00330008
#define NOTSRCERASE (DWORD)0x001100A6
#define MERGECOPY (DWORD)0x00C000CA
#define MERGEPAINT (DWORD)0x00BB0226
#define PATCOPY (DWORD)0x00F00021
#define PATPAINT (DWORD)0x00FB0A09
#define PATINVERT (DWORD)0x005A0049
#define DSTINVERT (DWORD)0x00550009
#define BLACKNESS (DWORD)0x00000042
#define WHITENESS (DWORD)0x00FF0062
#define NOMIRRORBITMAP (DWORD)0x80000000
#define CAPTUREBLT (DWORD)0x40000000
#define MAKEROP4(fore, back) (DWORD)((((back) << 8) & 0xFF000000) | (fore))

// Modes
#define TRANSPARENT 1
#define OPAQUE 2
#define BKMODE_LAST 2
#define ALTERNATE 1
#define WINDING 2
#define BLACKONWHITE 1
#define WHITEONBLACK 2
#define COLORONCOLOR 3
#define HALFTONE 4
#define STRETCH_ANDSCANS BLACKONWHITE
#define STRETCH_ORSCANS WHITEONBLACK
#define STRETCH_DELETESCANS COLORONCOLOR
#define STRETCH_HALFTONE HALFTONE
#define GM_COMPATIBLE 1
#define GM_ADVANCED 2
#define MM_TEXT 1
#define MM_LOMETRIC 2
#define MM_HIMETRIC 3
#define MM_LOENGLISH 4
#define MM_HIENGLISH 5
#define MM_TWIPS 6
#define MM_ISOTROPIC 7
#define MM_ANISOTROPIC 8
#define MM_MIN MM_TEXT
#define MM_MAX MM_ANISOTROPIC
#define MM_MAX_FIXEDSCALE MM_TWIPS
#define AD_COUNTERCLOCKWISE 1
#define AD_CLOCKWISE 2
#define MWT_IDENTITY 1
#define MWT_LEFTMULTIPLY 2
#define MWT_RIGHTMULTIPLY 3

// Text alignment
#define TA_NOUPDATECP 0
#define TA_UPDATECP 1
#define TA_LEFT 0
#define TA_RIGHT 2
#define TA_CENTER 6
#define TA_TOP 0
#define TA_BOTTOM 8
#define TA_BASELINE 24
#define TA_RTLREADING 256
#define TA_MASK (TA_BASELINE + TA_CENTER + TA_UPDATECP + TA_RTLREADING)
#define VTA_BASELINE TA_BASELINE
#define VTA_LEFT TA_BOTTOM
#define VTA_RIGHT TA_TOP
#define VTA_CENTER TA_CENTER
#define VTA_BOTTOM TA_RIGHT
#define VTA_TOP TA_LEFT
#define ETO_OPAQUE 0x0002
#define ETO_CLIPPED 0x0004
#define ETO_GLYPH_INDEX 0x0010
#define ETO_PDY 0x2000
#define ETO_RTLREADING 0x0080
#define ETO_IGNORELANGUAGE 0x1000

// Regions
#define ERROR 0
#define NULLREGION 1
#define SIMPLEREGION 2
#define COMPLEXREGION 3
#define RGN_ERROR ERROR
#define RGN_AND 1
#define RGN_OR 2
#define RGN_XOR 3
#define RGN_DIFF 4
#define RGN_COPY 5
#define RGN_MIN RGN_AND
#define RGN_MAX RGN_COPY
#define DCB_RESET 0x0001
#define DCB_ACCUMULATE 0x0002
#define DCB_DIRTY DCB_ACCUMULATE
#define DCB_SET (DCB_RESET | DCB_ACCUMULATE)
#define DCB_ENABLE 0x0004
#define DCB_DISABLE 0x0008

// Pens
#define PS_SOLID 0
#define PS_DASH 1
#define PS_DOT 2
#define PS_DASHDOT 3
#define PS_DASHDOTDOT 4
#define PS_NULL 5
#define PS_INSIDEFRAME 6
#define PS_USERSTYLE 7
#define PS_ALTERNATE 8
#define PS_STYLE_MASK 0x0000000F
#define PS_ENDCAP_ROUND 0x00000000
#define PS_ENDCAP_SQUARE 0x00000100
#define PS_ENDCAP_FLAT 0x00000200
#define PS_ENDCAP_MASK 0x00000F00
#define PS_JOIN_ROUND 0x00000000
#define PS_JOIN_BEVEL 0x00001000
#define PS_JOIN_MITER 0x00002000
#define PS_JOIN_MASK 0x0000F000
#define PS_COSMETIC 0x00000000
#define PS_GEOMETRIC 0x00010000
#define PS_TYPE_MASK 0x000F0000

// Brushes
#define BS_SOLID 0
#define BS_NULL 1
#define BS_HOLLOW BS_NULL
#define BS_HATCHED 2
#define BS_PATTERN 3
#define BS_INDEXED 4
#define BS_DIBPATTERN 5
#define BS_DIBPATTERNPT 6
#define BS_PATTERN8X8 7
#define BS_DIBPATTERN8X8 8
#define HS_HORIZONTAL 0
#define HS_VERTICAL 1
#define HS_FDIAGONAL 2
#define HS_BDIAGONAL 3
#define HS_CROSS 4
#define HS_DIAGCROSS 5
#define DIB_RGB_COLORS 0
#define DIB_PAL_COLORS 1

// Fonts
#define LF_FACESIZE 32
#define LF_FULLFACESIZE 64
#define FW_DONTCARE 0
#define FW_THIN 100
#define FW_EXTRALIGHT 200
#define FW_LIGHT 300
#define FW_NORMAL 400
#define FW_MEDIUM 500
#define FW_SEMIBOLD 600
#define FW_BOLD 700
#define FW_EXTRABOLD 800
#define FW_HEAVY 900
#define FW_ULTRALIGHT FW_EXTRALIGHT
#define FW_REGULAR FW_NORMAL
#define FW_DEMIBOLD FW_SEMIBOLD
#define FW_ULTRABOLD FW_EXTRABOLD
#define FW_BLACK FW_HEAVY
#define ANSI_CHARSET 0
#define DEFAULT_CHARSET 1
#define SYMBOL_CHARSET 2
#define SHIFTJIS_CHARSET 128
#define HANGEUL_CHARSET 129
#define HANGUL_CHARSET 129
#define GB2312_CHARSET 134
#define CHINESEBIG5_CHARSET 136
#define OEM_CHARSET 255
#define JOHAB_CHARSET 130
#define HEBREW_CHARSET 177
#define ARABIC_CHARSET 178
#define GREEK_CHARSET 161
#define TURKISH_CHARSET 162
#define VIETNAMESE_CHARSET 163
#define THAI_CHARSET 222
#define EASTEUROPE_CHARSET 238
#define RUSSIAN_CHARSET 204
#define MAC_CHARSET 77
#define BALTIC_CHARSET 186
#define OUT_DEFAULT_PRECIS 0
#define OUT_STRING_PRECIS 1
#define OUT_CHARACTER_PRECIS 2
#define OUT_STROKE_PRECIS 3
#define OUT_TT_PRECIS 4
#define OUT_DEVICE_PRECIS 5
#define OUT_RASTER_PRECIS 6
#define OUT_TT_ONLY_PRECIS 7
#define OUT_OUTLINE_PRECIS 8
#define CLIP_DEFAULT_PRECIS 0
#define CLIP_CHARACTER_PRECIS 1
#define CLIP_STROKE_PRECIS 2
#define CLIP_MASK 0xf
#define CLIP_LH_ANGLES (1 << 4)
#define CLIP_TT_ALWAYS (2 << 4)
#define CLIP_EMBEDDED (8 << 4)
#define DEFAULT_QUALITY 0
#define DRAFT_QUALITY 1
#define PROOF_QUALITY 2
#define NONANTIALIASED_QUALITY 3
#define ANTIALIASED_QUALITY 4
#define CLEARTYPE_QUALITY 5
#define DEFAULT_PITCH 0
#define FIXED_PITCH 1
#define VARIABLE_PITCH 2
#define MONO_FONT 8
#define FF_DONTCARE (0 << 4)
#define FF_ROMAN (1 << 4)
#define FF_SWISS (2 << 4)
#define FF_MODERN (3 << 4)
#define FF_SCRIPT (4 << 4)
#define FF_DECORATIVE (5 << 4)
#define TMPF_FIXED_PITCH 0x01
#define TMPF_VECTOR 0x02
#define TMPF_DEVICE 0x08
#define TMPF_TRUETYPE 0x04
#define RASTER_FONTTYPE 0x0001
#define DEVICE_FONTTYPE 0x0002
#define TRUETYPE_FONTTYPE 0x0004
#define GGO_METRICS 0
#define GGO_BITMAP 1
#define GGO_NATIVE 2
#define GGO_BEZIER 3
#define GGO_GRAY2_BITMAP 4
#define GGO_GRAY4_BITMAP 5
#define GGO_GRAY8_BITMAP 6
#define GGO_GLYPH_INDEX 0x0080

// Device capabilities
#define DRIVERVERSION 0
#define TECHNOLOGY 2
#define HORZSIZE 4
#define VERTSIZE 6
#define HORZRES 8
#define VERTRES 10
#define BITSPIXEL 12
#define PLANES 14
#define NUMBRUSHES 16
#define NUMPENS 18
#define NUMMARKERS 20
#define NUMFONTS 22
#define NUMCOLORS 24
#define PDEVICESIZE 26
#define CURVECAPS 28
#define LINECAPS 30
#define POLYGONALCAPS 32
#define TEXTCAPS 34
#define CLIPCAPS 36
#define RASTERCAPS 38
#define ASPECTX 40
#define ASPECTY 42
#define ASPECTXY 44
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define SIZEPALETTE 104
#define NUMRESERVED 106
#define COLORRES 108
#define PHYSICALWIDTH 110
#define PHYSICALHEIGHT 111
#define PHYSICALOFFSETX 112
#define PHYSICALOFFSETY 113
#define SCALINGFACTORX 114
#define SCALINGFACTORY 115
#define VREFRESH 116
#define DESKTOPVERTRES 117
#define DESKTOPHORZRES 118
#define BLTALIGNMENT 119
#define SHADEBLENDCAPS 120
#define COLORMGMTCAPS 121
#define DT_PLOTTER 0
#define DT_RASDISPLAY 1
#define DT_RASPRINTER 2
#define DT_RASCAMERA 3
#define DT_CHARSTREAM 4
#define DT_METAFILE 5
#define DT_DISPFILE 6
#define RC_BITBLT 1
#define RC_BANDING 2
#define RC_SCALING 4
#define RC_BITMAP64 8
#define RC_GDI20_OUTPUT 0x0010
#define RC_DI_BITMAP 0x0080
#define RC_PALETTE 0x0100
#define RC_DIBTODEV 0x0200
#define RC_STRETCHBLT 0x0800
#define RC_FLOODFILL 0x1000
#define RC_STRETCHDIB 0x2000
#define TC_OP_CHARACTER 0x00000001
#define TC_CP_STROKE 0x00000004
#define TC_RA_ABLE 0x00002000
#define TC_SO_ABLE 0x00001000
#define TC_UA_ABLE 0x00000800
#define TC_VA_ABLE 0x00004000

// Bitmaps
#define BI_RGB 0L
#define BI_RLE8 1L
#define BI_RLE4 2L
#define BI_BITFIELDS 3L
#define BI_JPEG 4L
#define BI_PNG 5L
#define CBM_INIT 0x04L
#define AC_SRC_OVER 0x00
#define AC_SRC_ALPHA 0x01

typedef struct tagBITMAP {
  LONG bmType;
  LONG bmWidth;
  LONG bmHeight;
  LONG bmWidthBytes;
  WORD bmPlanes;
  WORD bmBitsPixel;
  LPVOID bmBits;
} BITMAP, *PBITMAP, *NPBITMAP, *LPBITMAP;
typedef struct tagRGBTRIPLE {
  BYTE rgbtBlue;
  BYTE rgbtGreen;
  BYTE rgbtRed;
} RGBTRIPLE, *PRGBTRIPLE, *LPRGBTRIPLE;
typedef struct tagRGBQUAD {
  BYTE rgbBlue;
  BYTE rgbGreen;
  BYTE rgbRed;
  BYTE rgbReserved;
} RGBQUAD, *LPRGBQUAD;
#pragma pack(push, 2)
typedef struct tagBITMAPFILEHEADER {
  WORD bfType;
  DWORD bfSize;
  WORD bfReserved1;
  WORD bfReserved2;
  DWORD bfOffBits;
} BITMAPFILEHEADER, *LPBITMAPFILEHEADER, *PBITMAPFILEHEADER;
#pragma pack(pop)
typedef struct tagBITMAPCOREHEADER {
  DWORD bcSize;
  WORD bcWidth;
  WORD bcHeight;
  WORD bcPlanes;
  WORD bcBitCount;
} BITMAPCOREHEADER, *LPBITMAPCOREHEADER, *PBITMAPCOREHEADER;
typedef struct tagBITMAPINFOHEADER {
  DWORD biSize;
  LONG biWidth;
  LONG biHeight;
  WORD biPlanes;
  WORD biBitCount;
  DWORD biCompression;
  DWORD biSizeImage;
  LONG biXPelsPerMeter;
  LONG biYPelsPerMeter;
  DWORD biClrUsed;
  DWORD biClrImportant;
} BITMAPINFOHEADER, *LPBITMAPINFOHEADER, *PBITMAPINFOHEADER;
typedef struct tagBITMAPINFO {
  BITMAPINFOHEADER bmiHeader;
  RGBQUAD bmiColors[1];
} BITMAPINFO, *LPBITMAPINFO, *PBITMAPINFO;
typedef struct tagBITMAPCOREINFO {
  BITMAPCOREHEADER bmciHeader;
  RGBTRIPLE bmciColors[1];
} BITMAPCOREINFO, *LPBITMAPCOREINFO, *PBITMAPCOREINFO;
typedef struct tagDIBSECTION {
  BITMAP dsBm;
  BITMAPINFOHEADER dsBmih;
  DWORD dsBitfields[3];
  HANDLE dshSection;
  DWORD dsOffset;
} DIBSECTION, *LPDIBSECTION, *PDIBSECTION;
typedef struct _BLENDFUNCTION {
  BYTE BlendOp;
  BYTE BlendFlags;
  BYTE SourceConstantAlpha;
  BYTE AlphaFormat;
} BLENDFUNCTION, *PBLENDFUNCTION;

typedef struct tagLOGPEN {
  UINT lopnStyle;
  POINT lopnWidth;
  COLORREF lopnColor;
} LOGPEN, *PLOGPEN, *NPLOGPEN, *LPLOGPEN;
typedef struct tagEXTLOGPEN {
  DWORD elpPenStyle;
  DWORD elpWidth;
  UINT elpBrushStyle;
  COLORREF elpColor;
  ULONG_PTR elpHatch;
  DWORD elpNumEntries;
  DWORD elpStyleEntry[1];
} EXTLOGPEN, *PEXTLOGPEN, *NPEXTLOGPEN, *LPEXTLOGPEN;
typedef struct tagLOGBRUSH {
  UINT lbStyle;
  COLORREF lbColor;
  ULONG_PTR lbHatch;
} LOGBRUSH, *PLOGBRUSH, *NPLOGBRUSH, *LPLOGBRUSH;
typedef LOGBRUSH PATTERN, *PPATTERN, *LPPATTERN;

typedef struct tagLOGFONTA {
  LONG lfHeight;
  LONG lfWidth;
  LONG lfEscapement;
  LONG lfOrientation;
  LONG lfWeight;
  BYTE lfItalic;
  BYTE lfUnderline;
  BYTE lfStrikeOut;
  BYTE lfCharSet;
  BYTE lfOutPrecision;
  BYTE lfClipPrecision;
  BYTE lfQuality;
  BYTE lfPitchAndFamily;
  CHAR lfFaceName[LF_FACESIZE];
} LOGFONTA, *PLOGFONTA, *NPLOGFONTA, *LPLOGFONTA;
typedef struct tagLOGFONTW {
  LONG lfHeight;
  LONG lfWidth;
  LONG lfEscapement;
  LONG lfOrientation;
  LONG lfWeight;
  BYTE lfItalic;
  BYTE lfUnderline;
  BYTE lfStrikeOut;
  BYTE lfCharSet;
  BYTE lfOutPrecision;
  BYTE lfClipPrecision;
  BYTE lfQuality;
  BYTE lfPitchAndFamily;
  WCHAR lfFaceName[LF_FACESIZE];
} LOGFONTW, *PLOGFONTW, *NPLOGFONTW, *LPLOGFONTW;
typedef struct tagENUMLOGFONTA {
  LOGFONTA elfLogFont;
  BYTE elfFullName[LF_FULLFACESIZE];
  BYTE elfStyle[LF_FACESIZE];
} ENUMLOGFONTA, *LPENUMLOGFONTA;
typedef struct tagENUMLOGFONTW {
  LOGFONTW elfLogFont;
  WCHAR elfFullName[LF_FULLFACESIZE];
  WCHAR elfStyle[LF_FACESIZE];
} ENUMLOGFONTW, *LPENUMLOGFONTW;

typedef struct tagTEXTMETRICA {
  LONG tmHeight;
  LONG tmAscent;
  LONG tmDescent;
  LONG tmInternalLeading;
  LONG tmExternalLeading;
  LONG tmAveCharWidth;
  LONG tmMaxCharWidth;
  LONG tmWeight;
  LONG tmOverhang;
  LONG tmDigitizedAspectX;
  LONG tmDigitizedAspectY;
  BYTE tmFirstChar;
  BYTE tmLastChar;
  BYTE tmDefaultChar;
  BYTE tmBreakChar;
  BYTE tmItalic;
  BYTE tmUnderlined;
  BYTE tmStruckOut;
  BYTE tmPitchAndFamily;
  BYTE tmCharSet;
} TEXTMETRICA, *PTEXTMETRICA, *NPTEXTMETRICA, *LPTEXTMETRICA;
typedef struct tagTEXTMETRICW {
  LONG tmHeight;
  LONG tmAscent;
  LONG tmDescent;
  LONG tmInternalLeading;
  LONG tmExternalLeading;
  LONG tmAveCharWidth;
  LONG tmMaxCharWidth;
  LONG tmWeight;
  LONG tmOverhang;
  LONG tmDigitizedAspectX;
  LONG tmDigitizedAspectY;
  WCHAR tmFirstChar;
  WCHAR tmLastChar;
  WCHAR tmDefaultChar;
  WCHAR tmBreakChar;
  BYTE tmItalic;
  BYTE tmUnderlined;
  BYTE tmStruckOut;
  BYTE tmPitchAndFamily;
  BYTE tmCharSet;
} TEXTMETRICW, *PTEXTMETRICW, *NPTEXTMETRICW, *LPTEXTMETRICW;
typedef struct tagNEWTEXTMETRICA {
  LONG tmHeight;
  LONG tmAscent;
  LONG tmDescent;
  LONG tmInternalLeading;
  LONG tmExternalLeading;
  LONG tmAveCharWidth;
  LONG tmMaxCharWidth;
  LONG tmWeight;
  LONG tmOverhang;
  LONG tmDigitizedAspectX;
  LONG tmDigitizedAspectY;
  BYTE tmFirstChar;
  BYTE tmLastChar;
  BYTE tmDefaultChar;
  BYTE tmBreakChar;
  BYTE tmItalic;
  BYTE tmUnderlined;
  BYTE tmStruckOut;
  BYTE tmPitchAndFamily;
  BYTE tmCharSet;
  DWORD ntmFlags;
  UINT ntmSizeEM;
  UINT ntmCellHeight;
  UINT ntmAvgWidth;
} NEWTEXTMETRICA, *PNEWTEXTMETRICA, *LPNEWTEXTMETRICA;
typedef struct tagNEWTEXTMETRICW {
  LONG tmHeight;
  LONG tmAscent;
  LONG tmDescent;
  LONG tmInternalLeading;
  LONG tmExternalLeading;
  LONG tmAveCharWidth;
  LONG tmMaxCharWidth;
  LONG tmWeight;
  LONG tmOverhang;
  LONG tmDigitizedAspectX;
  LONG tmDigitizedAspectY;
  WCHAR tmFirstChar;
  WCHAR tmLastChar;
  WCHAR tmDefaultChar;
  WCHAR tmBreakChar;
  BYTE tmItalic;
  BYTE tmUnderlined;
  BYTE tmStruckOut;
  BYTE tmPitchAndFamily;
  BYTE tmCharSet;
  DWORD ntmFlags;
  UINT ntmSizeEM;
  UINT ntmCellHeight;
  UINT ntmAvgWidth;
} NEWTEXTMETRICW, *PNEWTEXTMETRICW, *LPNEWTEXTMETRICW;
typedef struct tagFONTSIGNATURE {
  DWORD fsUsb[4];
  DWORD fsCsb[2];
} FONTSIGNATURE, *PFONTSIGNATURE, *LPFONTSIGNATURE;
typedef struct tagNEWTEXTMETRICEXA {
  NEWTEXTMETRICA ntmTm;
  FONTSIGNATURE ntmFontSig;
} NEWTEXTMETRICEXA;
typedef struct tagNEWTEXTMETRICEXW {
  NEWTEXTMETRICW ntmTm;
  FONTSIGNATURE ntmFontSig;
} NEWTEXTMETRICEXW;
typedef struct tagENUMLOGFONTEXA {
  LOGFONTA elfLogFont;
  BYTE elfFullName[LF_FULLFACESIZE];
  BYTE elfStyle[LF_FACESIZE];
  BYTE elfScript[LF_FACESIZE];
} ENUMLOGFONTEXA, *LPENUMLOGFONTEXA;
typedef struct tagENUMLOGFONTEXW {
  LOGFONTW elfLogFont;
  WCHAR elfFullName[LF_FULLFACESIZE];
  WCHAR elfStyle[LF_FACESIZE];
  WCHAR elfScript[LF_FACESIZE];
} ENUMLOGFONTEXW, *LPENUMLOGFONTEXW;
#define NTM_ITALIC 0x00000001
#define NTM_BOLD 0x00000020
#define NTM_REGULAR 0x00000040
typedef struct tagPANOSE {
  BYTE bFamilyType, bSerifStyle, bWeight, bProportion, bContrast, bStrokeVariation, bArmStyle,
      bLetterform, bMidline, bXHeight;
} PANOSE, *LPPANOSE;
typedef struct _OUTLINETEXTMETRICA {
  UINT otmSize;
  TEXTMETRICA otmTextMetrics;
  BYTE otmFiller;
  PANOSE otmPanoseNumber;
  UINT otmfsSelection;
  UINT otmfsType;
  int otmsCharSlopeRise;
  int otmsCharSlopeRun;
  int otmItalicAngle;
  UINT otmEMSquare;
  int otmAscent;
  int otmDescent;
  UINT otmLineGap;
  UINT otmsCapEmHeight;
  UINT otmsXHeight;
  RECT otmrcFontBox;
  int otmMacAscent;
  int otmMacDescent;
  UINT otmMacLineGap;
  UINT otmusMinimumPPEM;
  POINT otmptSubscriptSize;
  POINT otmptSubscriptOffset;
  POINT otmptSuperscriptSize;
  POINT otmptSuperscriptOffset;
  UINT otmsStrikeoutSize;
  int otmsStrikeoutPosition;
  int otmsUnderscoreSize;
  int otmsUnderscorePosition;
  PSTR otmpFamilyName;
  PSTR otmpFaceName;
  PSTR otmpStyleName;
  PSTR otmpFullName;
} OUTLINETEXTMETRICA, *POUTLINETEXTMETRICA, *LPOUTLINETEXTMETRICA;
typedef struct _OUTLINETEXTMETRICW {
  UINT otmSize;
  TEXTMETRICW otmTextMetrics;
  BYTE otmFiller;
  PANOSE otmPanoseNumber;
  UINT otmfsSelection;
  UINT otmfsType;
  int otmsCharSlopeRise;
  int otmsCharSlopeRun;
  int otmItalicAngle;
  UINT otmEMSquare;
  int otmAscent;
  int otmDescent;
  UINT otmLineGap;
  UINT otmsCapEmHeight;
  UINT otmsXHeight;
  RECT otmrcFontBox;
  int otmMacAscent;
  int otmMacDescent;
  UINT otmMacLineGap;
  UINT otmusMinimumPPEM;
  POINT otmptSubscriptSize;
  POINT otmptSubscriptOffset;
  POINT otmptSuperscriptSize;
  POINT otmptSuperscriptOffset;
  UINT otmsStrikeoutSize;
  int otmsStrikeoutPosition;
  int otmsUnderscoreSize;
  int otmsUnderscorePosition;
  PSTR otmpFamilyName;
  PSTR otmpFaceName;
  PSTR otmpStyleName;
  PSTR otmpFullName;
} OUTLINETEXTMETRICW, *POUTLINETEXTMETRICW, *LPOUTLINETEXTMETRICW;
typedef struct _ABC {
  int abcA;
  UINT abcB;
  int abcC;
} ABC, *PABC, *NPABC, *LPABC;
typedef struct _ABCFLOAT {
  FLOAT abcfA;
  FLOAT abcfB;
  FLOAT abcfC;
} ABCFLOAT, *LPABCFLOAT;
typedef struct tagKERNINGPAIR {
  WORD wFirst;
  WORD wSecond;
  int iKernAmount;
} KERNINGPAIR, *LPKERNINGPAIR;
typedef struct _FIXED {
  WORD fract;
  short value;
} FIXED;
typedef struct _MAT2 {
  FIXED eM11, eM12, eM21, eM22;
} MAT2, *LPMAT2;
typedef struct _GLYPHMETRICS {
  UINT gmBlackBoxX;
  UINT gmBlackBoxY;
  POINT gmptGlyphOrigin;
  short gmCellIncX;
  short gmCellIncY;
} GLYPHMETRICS, *LPGLYPHMETRICS;
typedef int(CALLBACK* FONTENUMPROCA)(const LOGFONTA*, const TEXTMETRICA*, DWORD, LPARAM);
typedef int(CALLBACK* FONTENUMPROCW)(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM);
typedef FONTENUMPROCA OLDFONTENUMPROCA;
typedef FONTENUMPROCW OLDFONTENUMPROCW;

typedef struct tagPALETTEENTRY {
  BYTE peRed;
  BYTE peGreen;
  BYTE peBlue;
  BYTE peFlags;
} PALETTEENTRY, *PPALETTEENTRY, *LPPALETTEENTRY;
typedef struct tagLOGPALETTE {
  WORD palVersion;
  WORD palNumEntries;
  PALETTEENTRY palPalEntry[1];
} LOGPALETTE, *PLOGPALETTE, *NPLOGPALETTE, *LPLOGPALETTE;
#define PC_RESERVED 0x01
#define PC_EXPLICIT 0x02
#define PC_NOCOLLAPSE 0x04
typedef struct tagCOLORADJUSTMENT {
  WORD caSize;
  WORD caFlags;
  WORD caIlluminantIndex;
  WORD caRedGamma;
  WORD caGreenGamma;
  WORD caBlueGamma;
  WORD caReferenceBlack;
  WORD caReferenceWhite;
  SHORT caContrast;
  SHORT caBrightness;
  SHORT caColorfulness;
  SHORT caRedGreenTint;
} COLORADJUSTMENT, *PCOLORADJUSTMENT, *LPCOLORADJUSTMENT;

typedef struct _RGNDATAHEADER {
  DWORD dwSize;
  DWORD iType;
  DWORD nCount;
  DWORD nRgnSize;
  RECT rcBound;
} RGNDATAHEADER, *PRGNDATAHEADER;
typedef struct _RGNDATA {
  RGNDATAHEADER rdh;
  char Buffer[1];
} RGNDATA, *PRGNDATA, *NPRGNDATA, *LPRGNDATA;
#define RDH_RECTANGLES 1

typedef struct tagXFORM {
  FLOAT eM11;
  FLOAT eM12;
  FLOAT eM21;
  FLOAT eM22;
  FLOAT eDx;
  FLOAT eDy;
} XFORM, *PXFORM, *LPXFORM;

#define PT_CLOSEFIGURE 0x01
#define PT_LINETO 0x02
#define PT_BEZIERTO 0x04
#define PT_MOVETO 0x06

typedef struct tagMETAFILEPICT {
  LONG mm;
  LONG xExt;
  LONG yExt;
  HMETAFILE hMF;
} METAFILEPICT, *LPMETAFILEPICT;
typedef struct tagMETAHEADER {
  WORD mtType;
  WORD mtHeaderSize;
  WORD mtVersion;
  DWORD mtSize;
  WORD mtNoObjects;
  DWORD mtMaxRecord;
  WORD mtNoParameters;
} METAHEADER, *PMETAHEADER, *LPMETAHEADER;
typedef struct tagMETARECORD {
  DWORD rdSize;
  WORD rdFunction;
  WORD rdParm[1];
} METARECORD, *PMETARECORD, *LPMETARECORD;
typedef struct tagHANDLETABLE {
  HGDIOBJ objectHandle[1];
} HANDLETABLE, *PHANDLETABLE, *LPHANDLETABLE;
typedef struct tagENHMETARECORD {
  DWORD iType;
  DWORD nSize;
  DWORD dParm[1];
} ENHMETARECORD, *PENHMETARECORD, *LPENHMETARECORD;
typedef struct tagENHMETAHEADER {
  DWORD iType;
  DWORD nSize;
  RECTL rclBounds;
  RECTL rclFrame;
  DWORD dSignature;
  DWORD nVersion;
  DWORD nBytes;
  DWORD nRecords;
  WORD nHandles;
  WORD sReserved;
  DWORD nDescription;
  DWORD offDescription;
  DWORD nPalEntries;
  SIZEL szlDevice;
  SIZEL szlMillimeters;
} ENHMETAHEADER, *PENHMETAHEADER, *LPENHMETAHEADER;
typedef int(CALLBACK* MFENUMPROC)(HDC, HANDLETABLE*, METARECORD*, int, LPARAM);
typedef int(CALLBACK* ENHMFENUMPROC)(HDC, HANDLETABLE*, const ENHMETARECORD*, int, LPARAM);
#define EMR_HEADER 1
#define EMR_EOF 14
#define META_SETMAPMODE 0x0103
#define META_SETWINDOWORG 0x020B
#define META_SETWINDOWEXT 0x020C
#define META_SETVIEWPORTORG 0x020D
#define META_SETVIEWPORTEXT 0x020E
#define META_OFFSETWINDOWORG 0x020F
#define META_SCALEWINDOWEXT 0x0410
#define META_OFFSETVIEWPORTORG 0x0211
#define META_SCALEVIEWPORTEXT 0x0412
#define META_SAVEDC 0x001E
#define META_RESTOREDC 0x0127
#define META_SELECTOBJECT 0x012D
#define META_SELECTPALETTE 0x0234
#define META_SETBKCOLOR 0x0201
#define META_SETTEXTCOLOR 0x0209
#define META_EXTTEXTOUT 0x0a32
#define META_TEXTOUT 0x0521
#define META_SETBKMODE 0x0102
#define META_SETTEXTALIGN 0x012E
#define META_SELECTCLIPREGION 0x012C
#define META_INTERSECTCLIPRECT 0x0416
#define META_EXCLUDECLIPRECT 0x0415
#define META_OFFSETCLIPRGN 0x0220

// Printing
#define CCHDEVICENAME 32
#define CCHFORMNAME 32
typedef struct _devicemodeA {
  BYTE dmDeviceName[CCHDEVICENAME];
  WORD dmSpecVersion;
  WORD dmDriverVersion;
  WORD dmSize;
  WORD dmDriverExtra;
  DWORD dmFields;
  union {
    struct {
      short dmOrientation;
      short dmPaperSize;
      short dmPaperLength;
      short dmPaperWidth;
      short dmScale;
      short dmCopies;
      short dmDefaultSource;
      short dmPrintQuality;
    };
    struct {
      POINTL dmPosition;
      DWORD dmDisplayOrientation;
      DWORD dmDisplayFixedOutput;
    };
  };
  short dmColor;
  short dmDuplex;
  short dmYResolution;
  short dmTTOption;
  short dmCollate;
  BYTE dmFormName[CCHFORMNAME];
  WORD dmLogPixels;
  DWORD dmBitsPerPel;
  DWORD dmPelsWidth;
  DWORD dmPelsHeight;
  union {
    DWORD dmDisplayFlags;
    DWORD dmNup;
  };
  DWORD dmDisplayFrequency;
  DWORD dmICMMethod;
  DWORD dmICMIntent;
  DWORD dmMediaType;
  DWORD dmDitherType;
  DWORD dmReserved1;
  DWORD dmReserved2;
  DWORD dmPanningWidth;
  DWORD dmPanningHeight;
} DEVMODEA, *PDEVMODEA, *NPDEVMODEA, *LPDEVMODEA;
typedef struct _devicemodeW {
  WCHAR dmDeviceName[CCHDEVICENAME];
  WORD dmSpecVersion;
  WORD dmDriverVersion;
  WORD dmSize;
  WORD dmDriverExtra;
  DWORD dmFields;
  union {
    struct {
      short dmOrientation;
      short dmPaperSize;
      short dmPaperLength;
      short dmPaperWidth;
      short dmScale;
      short dmCopies;
      short dmDefaultSource;
      short dmPrintQuality;
    };
    struct {
      POINTL dmPosition;
      DWORD dmDisplayOrientation;
      DWORD dmDisplayFixedOutput;
    };
  };
  short dmColor;
  short dmDuplex;
  short dmYResolution;
  short dmTTOption;
  short dmCollate;
  WCHAR dmFormName[CCHFORMNAME];
  WORD dmLogPixels;
  DWORD dmBitsPerPel;
  DWORD dmPelsWidth;
  DWORD dmPelsHeight;
  union {
    DWORD dmDisplayFlags;
    DWORD dmNup;
  };
  DWORD dmDisplayFrequency;
  DWORD dmICMMethod;
  DWORD dmICMIntent;
  DWORD dmMediaType;
  DWORD dmDitherType;
  DWORD dmReserved1;
  DWORD dmReserved2;
  DWORD dmPanningWidth;
  DWORD dmPanningHeight;
} DEVMODEW, *PDEVMODEW, *NPDEVMODEW, *LPDEVMODEW;
#define DM_SPECVERSION 0x0401
#define DM_ORIENTATION 0x00000001L
#define DM_PAPERSIZE 0x00000002L
#define DM_PAPERLENGTH 0x00000004L
#define DM_PAPERWIDTH 0x00000008L
#define DM_SCALE 0x00000010L
#define DM_COPIES 0x00000100L
#define DM_DEFAULTSOURCE 0x00000200L
#define DM_PRINTQUALITY 0x00000400L
#define DM_COLOR 0x00000800L
#define DM_DUPLEX 0x00001000L
#define DM_YRESOLUTION 0x00002000L
#define DM_TTOPTION 0x00004000L
#define DM_COLLATE 0x00008000L
#define DM_FORMNAME 0x00010000L
#define DM_BITSPERPEL 0x00040000L
#define DM_PELSWIDTH 0x00080000L
#define DM_PELSHEIGHT 0x00100000L
#define DMORIENT_PORTRAIT 1
#define DMORIENT_LANDSCAPE 2
#define DMPAPER_LETTER 1
#define DMPAPER_LEGAL 5
#define DMPAPER_A4 9
#define DMCOLOR_MONOCHROME 1
#define DMCOLOR_COLOR 2
#define DMDUP_SIMPLEX 1
#define DMDUP_VERTICAL 2
#define DMDUP_HORIZONTAL 3
#define DMRES_DRAFT (-1)
#define DMRES_LOW (-2)
#define DMRES_MEDIUM (-3)
#define DMRES_HIGH (-4)
typedef struct _DOCINFOA {
  int cbSize;
  LPCSTR lpszDocName;
  LPCSTR lpszOutput;
  LPCSTR lpszDatatype;
  DWORD fwType;
} DOCINFOA, *LPDOCINFOA;
typedef struct _DOCINFOW {
  int cbSize;
  LPCWSTR lpszDocName;
  LPCWSTR lpszOutput;
  LPCWSTR lpszDatatype;
  DWORD fwType;
} DOCINFOW, *LPDOCINFOW;
typedef BOOL(CALLBACK* ABORTPROC)(HDC, int);
#define SP_NOTREPORTED 0x4000
#define SP_ERROR (-1)
#define SP_APPABORT (-2)
#define SP_USERABORT (-3)
#define SP_OUTOFDISK (-4)
#define SP_OUTOFMEMORY (-5)
#define NEWFRAME 1
#define ABORTDOC 2
#define NEXTBAND 3
#define SETABORTPROC 9
#define STARTDOC 10
#define ENDDOC 11
#define GETPHYSPAGESIZE 12
#define GETPRINTINGOFFSET 13
#define GETSCALINGFACTOR 14
#define QUERYESCSUPPORT 8
#define SETCOPYCOUNT 17
#define PASSTHROUGH 19
#define BANDINFO 24
#define GETTECHNOLOGY 20
#define DRAFTMODE 7
#define FLUSHOUTPUT 6
#define GETCOLORTABLE 5
#define SETCOLORTABLE 4

typedef int(CALLBACK* GOBJENUMPROC)(LPVOID, LPARAM);
typedef void(CALLBACK* LINEDDAPROC)(int, int, LPARAM);

// Device contexts
HDC WINAPI CreateDCA(LPCSTR, LPCSTR, LPCSTR, const DEVMODEA*);
HDC WINAPI CreateDCW(LPCWSTR, LPCWSTR, LPCWSTR, const DEVMODEW*);
HDC WINAPI CreateICA(LPCSTR, LPCSTR, LPCSTR, const DEVMODEA*);
HDC WINAPI CreateICW(LPCWSTR, LPCWSTR, LPCWSTR, const DEVMODEW*);
HDC WINAPI CreateCompatibleDC(HDC);
BOOL WINAPI DeleteDC(HDC);
HDC WINAPI ResetDCA(HDC, const DEVMODEA*);
HDC WINAPI ResetDCW(HDC, const DEVMODEW*);
int WINAPI SaveDC(HDC);
BOOL WINAPI RestoreDC(HDC, int);
int WINAPI GetDeviceCaps(HDC, int);
HGDIOBJ WINAPI SelectObject(HDC, HGDIOBJ);
HGDIOBJ WINAPI GetCurrentObject(HDC, UINT);
BOOL WINAPI DeleteObject(HGDIOBJ);
HGDIOBJ WINAPI GetStockObject(int);
int WINAPI GetObjectA(HGDIOBJ, int, LPVOID);
int WINAPI GetObjectW(HGDIOBJ, int, LPVOID);
DWORD WINAPI GetObjectType(HGDIOBJ);
BOOL WINAPI UnrealizeObject(HGDIOBJ);
int WINAPI EnumObjects(HDC, int, GOBJENUMPROC, LPARAM);
BOOL WINAPI GdiFlush(void);
COLORREF WINAPI SetBkColor(HDC, COLORREF);
COLORREF WINAPI GetBkColor(HDC);
int WINAPI SetBkMode(HDC, int);
int WINAPI GetBkMode(HDC);
COLORREF WINAPI SetTextColor(HDC, COLORREF);
COLORREF WINAPI GetTextColor(HDC);
COLORREF WINAPI SetDCBrushColor(HDC, COLORREF);
COLORREF WINAPI GetDCBrushColor(HDC);
COLORREF WINAPI SetDCPenColor(HDC, COLORREF);
COLORREF WINAPI GetDCPenColor(HDC);
int WINAPI SetROP2(HDC, int);
int WINAPI GetROP2(HDC);
int WINAPI SetPolyFillMode(HDC, int);
int WINAPI GetPolyFillMode(HDC);
int WINAPI SetStretchBltMode(HDC, int);
int WINAPI GetStretchBltMode(HDC);
UINT WINAPI SetTextAlign(HDC, UINT);
UINT WINAPI GetTextAlign(HDC);
int WINAPI SetTextCharacterExtra(HDC, int);
int WINAPI GetTextCharacterExtra(HDC);
BOOL WINAPI SetTextJustification(HDC, int, int);
int WINAPI SetGraphicsMode(HDC, int);
int WINAPI GetGraphicsMode(HDC);
int WINAPI SetArcDirection(HDC, int);
int WINAPI GetArcDirection(HDC);
BOOL WINAPI SetMiterLimit(HDC, FLOAT, PFLOAT);
BOOL WINAPI GetMiterLimit(HDC, PFLOAT);
DWORD WINAPI SetMapperFlags(HDC, DWORD);
BOOL WINAPI SetBrushOrgEx(HDC, int, int, LPPOINT);
BOOL WINAPI GetBrushOrgEx(HDC, LPPOINT);
BOOL WINAPI SetColorAdjustment(HDC, const COLORADJUSTMENT*);
BOOL WINAPI GetColorAdjustment(HDC, LPCOLORADJUSTMENT);
UINT WINAPI SetBoundsRect(HDC, LPCRECT, UINT);
UINT WINAPI GetBoundsRect(HDC, LPRECT, UINT);
DWORD WINAPI SetLayout(HDC, DWORD);
DWORD WINAPI GetLayout(HDC);
#define LAYOUT_RTL 0x00000001
HWND WINAPI WindowFromDC(HDC);

// Coordinates
int WINAPI SetMapMode(HDC, int);
int WINAPI GetMapMode(HDC);
BOOL WINAPI SetViewportOrgEx(HDC, int, int, LPPOINT);
BOOL WINAPI GetViewportOrgEx(HDC, LPPOINT);
BOOL WINAPI OffsetViewportOrgEx(HDC, int, int, LPPOINT);
BOOL WINAPI SetViewportExtEx(HDC, int, int, LPSIZE);
BOOL WINAPI GetViewportExtEx(HDC, LPSIZE);
BOOL WINAPI ScaleViewportExtEx(HDC, int, int, int, int, LPSIZE);
BOOL WINAPI SetWindowOrgEx(HDC, int, int, LPPOINT);
BOOL WINAPI GetWindowOrgEx(HDC, LPPOINT);
BOOL WINAPI OffsetWindowOrgEx(HDC, int, int, LPPOINT);
BOOL WINAPI SetWindowExtEx(HDC, int, int, LPSIZE);
BOOL WINAPI GetWindowExtEx(HDC, LPSIZE);
BOOL WINAPI ScaleWindowExtEx(HDC, int, int, int, int, LPSIZE);
BOOL WINAPI DPtoLP(HDC, LPPOINT, int);
BOOL WINAPI LPtoDP(HDC, LPPOINT, int);
BOOL WINAPI SetWorldTransform(HDC, const XFORM*);
BOOL WINAPI GetWorldTransform(HDC, LPXFORM);
BOOL WINAPI ModifyWorldTransform(HDC, const XFORM*, DWORD);
BOOL WINAPI CombineTransform(LPXFORM, const XFORM*, const XFORM*);
BOOL WINAPI GetDCOrgEx(HDC, LPPOINT);

// Drawing
BOOL WINAPI MoveToEx(HDC, int, int, LPPOINT);
BOOL WINAPI GetCurrentPositionEx(HDC, LPPOINT);
BOOL WINAPI LineTo(HDC, int, int);
BOOL WINAPI Polyline(HDC, const POINT*, int);
BOOL WINAPI PolylineTo(HDC, const POINT*, DWORD);
BOOL WINAPI PolyPolyline(HDC, const POINT*, const DWORD*, DWORD);
BOOL WINAPI Polygon(HDC, const POINT*, int);
BOOL WINAPI PolyPolygon(HDC, const POINT*, const INT*, int);
BOOL WINAPI PolyBezier(HDC, const POINT*, DWORD);
BOOL WINAPI PolyBezierTo(HDC, const POINT*, DWORD);
BOOL WINAPI PolyDraw(HDC, const POINT*, const BYTE*, int);
BOOL WINAPI Rectangle(HDC, int, int, int, int);
BOOL WINAPI RoundRect(HDC, int, int, int, int, int, int);
BOOL WINAPI Ellipse(HDC, int, int, int, int);
BOOL WINAPI Arc(HDC, int, int, int, int, int, int, int, int);
BOOL WINAPI ArcTo(HDC, int, int, int, int, int, int, int, int);
BOOL WINAPI AngleArc(HDC, int, int, DWORD, FLOAT, FLOAT);
BOOL WINAPI Chord(HDC, int, int, int, int, int, int, int, int);
BOOL WINAPI Pie(HDC, int, int, int, int, int, int, int, int);
BOOL WINAPI LineDDA(int, int, int, int, LINEDDAPROC, LPARAM);
COLORREF WINAPI SetPixel(HDC, int, int, COLORREF);
BOOL WINAPI SetPixelV(HDC, int, int, COLORREF);
COLORREF WINAPI GetPixel(HDC, int, int);
BOOL WINAPI FloodFill(HDC, int, int, COLORREF);
BOOL WINAPI ExtFloodFill(HDC, int, int, COLORREF, UINT);
#define FLOODFILLBORDER 0
#define FLOODFILLSURFACE 1
BOOL WINAPI PatBlt(HDC, int, int, int, int, DWORD);
BOOL WINAPI BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD);
BOOL WINAPI StretchBlt(HDC, int, int, int, int, HDC, int, int, int, int, DWORD);
BOOL WINAPI MaskBlt(HDC, int, int, int, int, HDC, int, int, HBITMAP, int, int, DWORD);
BOOL WINAPI PlgBlt(HDC, const POINT*, HDC, int, int, int, int, HBITMAP, int, int);
BOOL WINAPI TransparentBlt(HDC, int, int, int, int, HDC, int, int, int, int, UINT);
BOOL WINAPI AlphaBlend(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION);
BOOL WINAPI GdiAlphaBlend(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION);
BOOL WINAPI GdiTransparentBlt(HDC, int, int, int, int, HDC, int, int, int, int, UINT);
int WINAPI StretchDIBits(HDC, int, int, int, int, int, int, int, int, const void*, const BITMAPINFO*, UINT,
                         DWORD);
int WINAPI SetDIBitsToDevice(HDC, int, int, DWORD, DWORD, int, int, UINT, UINT, const void*,
                             const BITMAPINFO*, UINT);
COLORREF WINAPI GetNearestColor(HDC, COLORREF);

// Paths
BOOL WINAPI BeginPath(HDC);
BOOL WINAPI EndPath(HDC);
BOOL WINAPI AbortPath(HDC);
BOOL WINAPI CloseFigure(HDC);
BOOL WINAPI FillPath(HDC);
BOOL WINAPI StrokePath(HDC);
BOOL WINAPI StrokeAndFillPath(HDC);
BOOL WINAPI FlattenPath(HDC);
BOOL WINAPI WidenPath(HDC);
int WINAPI GetPath(HDC, LPPOINT, LPBYTE, int);
HRGN WINAPI PathToRegion(HDC);
BOOL WINAPI SelectClipPath(HDC, int);

// Pens, brushes, bitmaps
HPEN WINAPI CreatePen(int, int, COLORREF);
HPEN WINAPI CreatePenIndirect(const LOGPEN*);
HPEN WINAPI ExtCreatePen(DWORD, DWORD, const LOGBRUSH*, DWORD, const DWORD*);
HBRUSH WINAPI CreateSolidBrush(COLORREF);
HBRUSH WINAPI CreateHatchBrush(int, COLORREF);
HBRUSH WINAPI CreatePatternBrush(HBITMAP);
HBRUSH WINAPI CreateBrushIndirect(const LOGBRUSH*);
HBRUSH WINAPI CreateDIBPatternBrush(HGLOBAL, UINT);
HBRUSH WINAPI CreateDIBPatternBrushPt(const void*, UINT);
HBITMAP WINAPI CreateBitmap(int, int, UINT, UINT, const void*);
HBITMAP WINAPI CreateBitmapIndirect(const BITMAP*);
HBITMAP WINAPI CreateCompatibleBitmap(HDC, int, int);
HBITMAP WINAPI CreateDiscardableBitmap(HDC, int, int);
HBITMAP WINAPI CreateDIBitmap(HDC, const BITMAPINFOHEADER*, DWORD, const void*, const BITMAPINFO*, UINT);
HBITMAP WINAPI CreateDIBSection(HDC, const BITMAPINFO*, UINT, void**, HANDLE, DWORD);
LONG WINAPI GetBitmapBits(HBITMAP, LONG, LPVOID);
LONG WINAPI SetBitmapBits(HBITMAP, DWORD, const void*);
int WINAPI GetDIBits(HDC, HBITMAP, UINT, UINT, LPVOID, LPBITMAPINFO, UINT);
int WINAPI SetDIBits(HDC, HBITMAP, UINT, UINT, const void*, const BITMAPINFO*, UINT);
UINT WINAPI GetDIBColorTable(HDC, UINT, UINT, RGBQUAD*);
UINT WINAPI SetDIBColorTable(HDC, UINT, UINT, const RGBQUAD*);
BOOL WINAPI GetBitmapDimensionEx(HBITMAP, LPSIZE);
BOOL WINAPI SetBitmapDimensionEx(HBITMAP, int, int, LPSIZE);

// Fonts and text
HFONT WINAPI CreateFontA(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                         LPCSTR);
HFONT WINAPI CreateFontW(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                         LPCWSTR);
HFONT WINAPI CreateFontIndirectA(const LOGFONTA*);
HFONT WINAPI CreateFontIndirectW(const LOGFONTW*);
BOOL WINAPI TextOutA(HDC, int, int, LPCSTR, int);
BOOL WINAPI TextOutW(HDC, int, int, LPCWSTR, int);
BOOL WINAPI ExtTextOutA(HDC, int, int, UINT, const RECT*, LPCSTR, UINT, const INT*);
BOOL WINAPI ExtTextOutW(HDC, int, int, UINT, const RECT*, LPCWSTR, UINT, const INT*);
BOOL WINAPI GetTextExtentPoint32A(HDC, LPCSTR, int, LPSIZE);
BOOL WINAPI GetTextExtentPoint32W(HDC, LPCWSTR, int, LPSIZE);
BOOL WINAPI GetTextExtentPointA(HDC, LPCSTR, int, LPSIZE);
BOOL WINAPI GetTextExtentPointW(HDC, LPCWSTR, int, LPSIZE);
BOOL WINAPI GetTextExtentExPointA(HDC, LPCSTR, int, int, LPINT, LPINT, LPSIZE);
BOOL WINAPI GetTextExtentExPointW(HDC, LPCWSTR, int, int, LPINT, LPINT, LPSIZE);
BOOL WINAPI GetTextMetricsA(HDC, LPTEXTMETRICA);
BOOL WINAPI GetTextMetricsW(HDC, LPTEXTMETRICW);
int WINAPI GetTextFaceA(HDC, int, LPSTR);
int WINAPI GetTextFaceW(HDC, int, LPWSTR);
BOOL WINAPI GetCharWidthA(HDC, UINT, UINT, LPINT);
BOOL WINAPI GetCharWidthW(HDC, UINT, UINT, LPINT);
BOOL WINAPI GetCharWidth32A(HDC, UINT, UINT, LPINT);
BOOL WINAPI GetCharWidth32W(HDC, UINT, UINT, LPINT);
BOOL WINAPI GetCharABCWidthsA(HDC, UINT, UINT, LPABC);
BOOL WINAPI GetCharABCWidthsW(HDC, UINT, UINT, LPABC);
DWORD WINAPI GetKerningPairsA(HDC, DWORD, LPKERNINGPAIR);
DWORD WINAPI GetKerningPairsW(HDC, DWORD, LPKERNINGPAIR);
UINT WINAPI GetOutlineTextMetricsA(HDC, UINT, LPOUTLINETEXTMETRICA);
UINT WINAPI GetOutlineTextMetricsW(HDC, UINT, LPOUTLINETEXTMETRICW);
DWORD WINAPI GetGlyphOutlineA(HDC, UINT, UINT, LPGLYPHMETRICS, DWORD, LPVOID, const MAT2*);
DWORD WINAPI GetGlyphOutlineW(HDC, UINT, UINT, LPGLYPHMETRICS, DWORD, LPVOID, const MAT2*);
DWORD WINAPI GetFontData(HDC, DWORD, DWORD, LPVOID, DWORD);
DWORD WINAPI GetFontLanguageInfo(HDC);
BOOL WINAPI GetAspectRatioFilterEx(HDC, LPSIZE);
int WINAPI EnumFontFamiliesA(HDC, LPCSTR, FONTENUMPROCA, LPARAM);
int WINAPI EnumFontFamiliesW(HDC, LPCWSTR, FONTENUMPROCW, LPARAM);
int WINAPI EnumFontFamiliesExA(HDC, LPLOGFONTA, FONTENUMPROCA, LPARAM, DWORD);
int WINAPI EnumFontFamiliesExW(HDC, LPLOGFONTW, FONTENUMPROCW, LPARAM, DWORD);
int WINAPI EnumFontsA(HDC, LPCSTR, FONTENUMPROCA, LPARAM);
int WINAPI EnumFontsW(HDC, LPCWSTR, FONTENUMPROCW, LPARAM);
int WINAPI AddFontResourceA(LPCSTR);
int WINAPI AddFontResourceW(LPCWSTR);
#define GCP_DBCS 0x0001
#define GCP_REORDER 0x0002
#define FLI_MASK 0x103B

// Regions and clipping
HRGN WINAPI CreateRectRgn(int, int, int, int);
HRGN WINAPI CreateRectRgnIndirect(const RECT*);
HRGN WINAPI CreateEllipticRgn(int, int, int, int);
HRGN WINAPI CreateEllipticRgnIndirect(const RECT*);
HRGN WINAPI CreateRoundRectRgn(int, int, int, int, int, int);
HRGN WINAPI CreatePolygonRgn(const POINT*, int, int);
HRGN WINAPI CreatePolyPolygonRgn(const POINT*, const INT*, int, int);
HRGN WINAPI ExtCreateRegion(const XFORM*, DWORD, const RGNDATA*);
int WINAPI CombineRgn(HRGN, HRGN, HRGN, int);
BOOL WINAPI EqualRgn(HRGN, HRGN);
int WINAPI OffsetRgn(HRGN, int, int);
BOOL WINAPI SetRectRgn(HRGN, int, int, int, int);
int WINAPI GetRgnBox(HRGN, LPRECT);
BOOL WINAPI PtInRegion(HRGN, int, int);
BOOL WINAPI RectInRegion(HRGN, const RECT*);
DWORD WINAPI GetRegionData(HRGN, DWORD, LPRGNDATA);
BOOL WINAPI FillRgn(HDC, HRGN, HBRUSH);
BOOL WINAPI FrameRgn(HDC, HRGN, HBRUSH, int, int);
BOOL WINAPI InvertRgn(HDC, HRGN);
BOOL WINAPI PaintRgn(HDC, HRGN);
int WINAPI SelectClipRgn(HDC, HRGN);
int WINAPI ExtSelectClipRgn(HDC, HRGN, int);
int WINAPI GetClipRgn(HDC, HRGN);
int WINAPI GetClipBox(HDC, LPRECT);
int WINAPI IntersectClipRect(HDC, int, int, int, int);
int WINAPI ExcludeClipRect(HDC, int, int, int, int);
int WINAPI OffsetClipRgn(HDC, int, int);
BOOL WINAPI PtVisible(HDC, int, int);
BOOL WINAPI RectVisible(HDC, const RECT*);

// Palettes
HPALETTE WINAPI CreatePalette(const LOGPALETTE*);
HPALETTE WINAPI CreateHalftonePalette(HDC);
HPALETTE WINAPI SelectPalette(HDC, HPALETTE, BOOL);
UINT WINAPI RealizePalette(HDC);
UINT WINAPI GetPaletteEntries(HPALETTE, UINT, UINT, LPPALETTEENTRY);
UINT WINAPI SetPaletteEntries(HPALETTE, UINT, UINT, const PALETTEENTRY*);
UINT WINAPI GetSystemPaletteEntries(HDC, UINT, UINT, LPPALETTEENTRY);
BOOL WINAPI AnimatePalette(HPALETTE, UINT, UINT, const PALETTEENTRY*);
BOOL WINAPI ResizePalette(HPALETTE, UINT);
UINT WINAPI GetNearestPaletteIndex(HPALETTE, COLORREF);
BOOL WINAPI UpdateColors(HDC);
#define SYSPAL_ERROR 0
#define SYSPAL_STATIC 1
#define SYSPAL_NOSTATIC 2
#define SYSPAL_NOSTATIC256 3
UINT WINAPI GetSystemPaletteUse(HDC);

// Metafiles
HDC WINAPI CreateMetaFileA(LPCSTR);
HDC WINAPI CreateMetaFileW(LPCWSTR);
HMETAFILE WINAPI CloseMetaFile(HDC);
BOOL WINAPI DeleteMetaFile(HMETAFILE);
HMETAFILE WINAPI CopyMetaFileA(HMETAFILE, LPCSTR);
HMETAFILE WINAPI CopyMetaFileW(HMETAFILE, LPCWSTR);
HMETAFILE WINAPI GetMetaFileA(LPCSTR);
HMETAFILE WINAPI GetMetaFileW(LPCWSTR);
UINT WINAPI GetMetaFileBitsEx(HMETAFILE, UINT, LPVOID);
HMETAFILE WINAPI SetMetaFileBitsEx(UINT, const BYTE*);
BOOL WINAPI PlayMetaFile(HDC, HMETAFILE);
BOOL WINAPI PlayMetaFileRecord(HDC, LPHANDLETABLE, LPMETARECORD, UINT);
BOOL WINAPI EnumMetaFile(HDC, HMETAFILE, MFENUMPROC, LPARAM);
HDC WINAPI CreateEnhMetaFileA(HDC, LPCSTR, const RECT*, LPCSTR);
HDC WINAPI CreateEnhMetaFileW(HDC, LPCWSTR, const RECT*, LPCWSTR);
HENHMETAFILE WINAPI CloseEnhMetaFile(HDC);
BOOL WINAPI DeleteEnhMetaFile(HENHMETAFILE);
HENHMETAFILE WINAPI CopyEnhMetaFileA(HENHMETAFILE, LPCSTR);
HENHMETAFILE WINAPI CopyEnhMetaFileW(HENHMETAFILE, LPCWSTR);
HENHMETAFILE WINAPI GetEnhMetaFileA(LPCSTR);
HENHMETAFILE WINAPI GetEnhMetaFileW(LPCWSTR);
UINT WINAPI GetEnhMetaFileBits(HENHMETAFILE, UINT, LPBYTE);
HENHMETAFILE WINAPI SetEnhMetaFileBits(UINT, const BYTE*);
UINT WINAPI GetEnhMetaFileHeader(HENHMETAFILE, UINT, LPENHMETAHEADER);
BOOL WINAPI PlayEnhMetaFile(HDC, HENHMETAFILE, const RECT*);
BOOL WINAPI PlayEnhMetaFileRecord(HDC, LPHANDLETABLE, const ENHMETARECORD*, UINT);
BOOL WINAPI EnumEnhMetaFile(HDC, HENHMETAFILE, ENHMFENUMPROC, LPVOID, const RECT*);
HENHMETAFILE WINAPI SetWinMetaFileBits(UINT, const BYTE*, HDC, const METAFILEPICT*);
UINT WINAPI GetWinMetaFileBits(HENHMETAFILE, UINT, LPBYTE, INT, HDC);
BOOL WINAPI GdiComment(HDC, UINT, const BYTE*);

// Printing
int WINAPI StartDocA(HDC, const DOCINFOA*);
int WINAPI StartDocW(HDC, const DOCINFOW*);
int WINAPI EndDoc(HDC);
int WINAPI StartPage(HDC);
int WINAPI EndPage(HDC);
int WINAPI AbortDoc(HDC);
int WINAPI SetAbortProc(HDC, ABORTPROC);
int WINAPI Escape(HDC, int, int, LPCSTR, LPVOID);
int WINAPI ExtEscape(HDC, int, int, LPCSTR, int, LPSTR);
#define DocumentProperties __WINPORT_AW(DocumentProperties)

#define CreateDC __WINPORT_AW(CreateDC)
#define CreateIC __WINPORT_AW(CreateIC)
#define ResetDC __WINPORT_AW(ResetDC)
#define GetObject __WINPORT_AW(GetObject)
#define CreateFont __WINPORT_AW(CreateFont)
#define CreateFontIndirect __WINPORT_AW(CreateFontIndirect)
#define TextOut __WINPORT_AW(TextOut)
#define ExtTextOut __WINPORT_AW(ExtTextOut)
#define GetTextExtentPoint32 __WINPORT_AW(GetTextExtentPoint32)
#define GetTextExtentPoint __WINPORT_AW(GetTextExtentPoint)
#define GetTextExtentExPoint __WINPORT_AW(GetTextExtentExPoint)
#define GetTextMetrics __WINPORT_AW(GetTextMetrics)
#define GetTextFace __WINPORT_AW(GetTextFace)
#define GetCharWidth __WINPORT_AW(GetCharWidth)
#define GetCharWidth32 __WINPORT_AW(GetCharWidth32)
#define GetCharABCWidths __WINPORT_AW(GetCharABCWidths)
#define GetKerningPairs __WINPORT_AW(GetKerningPairs)
#define GetOutlineTextMetrics __WINPORT_AW(GetOutlineTextMetrics)
#define GetGlyphOutline __WINPORT_AW(GetGlyphOutline)
#define EnumFontFamilies __WINPORT_AW(EnumFontFamilies)
#define EnumFontFamiliesEx __WINPORT_AW(EnumFontFamiliesEx)
#define EnumFonts __WINPORT_AW(EnumFonts)
#define AddFontResource __WINPORT_AW(AddFontResource)
#define CreateMetaFile __WINPORT_AW(CreateMetaFile)
#define CopyMetaFile __WINPORT_AW(CopyMetaFile)
#define GetMetaFile __WINPORT_AW(GetMetaFile)
#define CreateEnhMetaFile __WINPORT_AW(CreateEnhMetaFile)
#define CopyEnhMetaFile __WINPORT_AW(CopyEnhMetaFile)
#define GetEnhMetaFile __WINPORT_AW(GetEnhMetaFile)
#define StartDoc __WINPORT_AW(StartDoc)
#define LOGFONT __WINPORT_AW(LOGFONT)
#define PLOGFONT __WINPORT_AW(PLOGFONT)
#define LPLOGFONT __WINPORT_AW(LPLOGFONT)
#define NPLOGFONT __WINPORT_AW(NPLOGFONT)
#define ENUMLOGFONT __WINPORT_AW(ENUMLOGFONT)
#define LPENUMLOGFONT __WINPORT_AW(LPENUMLOGFONT)
#define TEXTMETRIC __WINPORT_AW(TEXTMETRIC)
#define PTEXTMETRIC __WINPORT_AW(PTEXTMETRIC)
#define LPTEXTMETRIC __WINPORT_AW(LPTEXTMETRIC)
#define NPTEXTMETRIC __WINPORT_AW(NPTEXTMETRIC)
#define NEWTEXTMETRIC __WINPORT_AW(NEWTEXTMETRIC)
#define PNEWTEXTMETRIC __WINPORT_AW(PNEWTEXTMETRIC)
#define LPNEWTEXTMETRIC __WINPORT_AW(LPNEWTEXTMETRIC)
#define NEWTEXTMETRICEX __WINPORT_AW(NEWTEXTMETRICEX)
#define ENUMLOGFONTEX __WINPORT_AW(ENUMLOGFONTEX)
#define LPENUMLOGFONTEX __WINPORT_AW(LPENUMLOGFONTEX)
#define OUTLINETEXTMETRIC __WINPORT_AW(OUTLINETEXTMETRIC)
#define LPOUTLINETEXTMETRIC __WINPORT_AW(LPOUTLINETEXTMETRIC)
#define FONTENUMPROC __WINPORT_AW(FONTENUMPROC)
#define OLDFONTENUMPROC __WINPORT_AW(OLDFONTENUMPROC)
#define DEVMODE __WINPORT_AW(DEVMODE)
#define PDEVMODE __WINPORT_AW(PDEVMODE)
#define LPDEVMODE __WINPORT_AW(LPDEVMODE)
#define NPDEVMODE __WINPORT_AW(NPDEVMODE)
#define DOCINFO __WINPORT_AW(DOCINFO)
#define LPDOCINFO __WINPORT_AW(LPDOCINFO)

}  // extern "C"

#endif
