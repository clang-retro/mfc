// richedit.h - the rich edit control: its messages, notifications and
// structures.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_RICHEDIT_H
#define _WINPORT_RICHEDIT_H

#include <windows.h>

#pragma pack(push, 4)

#define _RICHEDIT_VER 0x0300
#define cchTextLimitDefault 32767
#define RICHEDIT_CLASSA "RichEdit20A"
#define RICHEDIT_CLASSW L"RichEdit20W"
#define RICHEDIT_CLASS10A "RICHEDIT"
#ifdef UNICODE
#define RICHEDIT_CLASS RICHEDIT_CLASSW
#else
#define RICHEDIT_CLASS RICHEDIT_CLASSA
#endif
#define MSFTEDIT_CLASS L"RICHEDIT50W"

#define EM_CANPASTE (WM_USER + 50)
#define EM_DISPLAYBAND (WM_USER + 51)
#define EM_EXGETSEL (WM_USER + 52)
#define EM_EXLIMITTEXT (WM_USER + 53)
#define EM_EXLINEFROMCHAR (WM_USER + 54)
#define EM_EXSETSEL (WM_USER + 55)
#define EM_FINDTEXT (WM_USER + 56)
#define EM_FORMATRANGE (WM_USER + 57)
#define EM_GETCHARFORMAT (WM_USER + 58)
#define EM_GETEVENTMASK (WM_USER + 59)
#define EM_GETOLEINTERFACE (WM_USER + 60)
#define EM_GETPARAFORMAT (WM_USER + 61)
#define EM_GETSELTEXT (WM_USER + 62)
#define EM_HIDESELECTION (WM_USER + 63)
#define EM_PASTESPECIAL (WM_USER + 64)
#define EM_REQUESTRESIZE (WM_USER + 65)
#define EM_SELECTIONTYPE (WM_USER + 66)
#define EM_SETBKGNDCOLOR (WM_USER + 67)
#define EM_SETCHARFORMAT (WM_USER + 68)
#define EM_SETEVENTMASK (WM_USER + 69)
#define EM_SETOLECALLBACK (WM_USER + 70)
#define EM_SETPARAFORMAT (WM_USER + 71)
#define EM_SETTARGETDEVICE (WM_USER + 72)
#define EM_STREAMIN (WM_USER + 73)
#define EM_STREAMOUT (WM_USER + 74)
#define EM_GETTEXTRANGE (WM_USER + 75)
#define EM_FINDWORDBREAK (WM_USER + 76)
#define EM_SETOPTIONS (WM_USER + 77)
#define EM_GETOPTIONS (WM_USER + 78)
#define EM_FINDTEXTEX (WM_USER + 79)
#define EM_GETWORDBREAKPROCEX (WM_USER + 80)
#define EM_SETWORDBREAKPROCEX (WM_USER + 81)
#define EM_SETUNDOLIMIT (WM_USER + 82)
#define EM_REDO (WM_USER + 84)
#define EM_CANREDO (WM_USER + 85)
#define EM_GETUNDONAME (WM_USER + 86)
#define EM_GETREDONAME (WM_USER + 87)
#define EM_STOPGROUPTYPING (WM_USER + 88)
#define EM_SETTEXTMODE (WM_USER + 89)
#define EM_GETTEXTMODE (WM_USER + 90)
#define EM_AUTOURLDETECT (WM_USER + 91)
#define EM_GETAUTOURLDETECT (WM_USER + 92)
#define EM_SETPALETTE (WM_USER + 93)
#define EM_GETTEXTEX (WM_USER + 94)
#define EM_GETTEXTLENGTHEX (WM_USER + 95)
#define EM_SHOWSCROLLBAR (WM_USER + 96)
#define EM_SETTEXTEX (WM_USER + 97)
#define EM_SETPUNCTUATION (WM_USER + 100)
#define EM_GETPUNCTUATION (WM_USER + 101)
#define EM_SETWORDWRAPMODE (WM_USER + 102)
#define EM_GETWORDWRAPMODE (WM_USER + 103)
#define EM_SETIMECOLOR (WM_USER + 104)
#define EM_GETIMECOLOR (WM_USER + 105)
#define EM_SETIMEOPTIONS (WM_USER + 106)
#define EM_GETIMEOPTIONS (WM_USER + 107)
#define EM_CONVPOSITION (WM_USER + 108)
#define EM_SETLANGOPTIONS (WM_USER + 120)
#define EM_GETLANGOPTIONS (WM_USER + 121)
#define EM_GETIMECOMPMODE (WM_USER + 122)
#define EM_FINDTEXTW (WM_USER + 123)
#define EM_FINDTEXTEXW (WM_USER + 124)
#define EM_SETBIDIOPTIONS (WM_USER + 200)
#define EM_GETBIDIOPTIONS (WM_USER + 201)
#define EM_SETTYPOGRAPHYOPTIONS (WM_USER + 202)
#define EM_GETTYPOGRAPHYOPTIONS (WM_USER + 203)
#define EM_SETEDITSTYLE (WM_USER + 204)
#define EM_GETEDITSTYLE (WM_USER + 205)
#define EM_GETSCROLLPOS (WM_USER + 221)
#define EM_SETSCROLLPOS (WM_USER + 222)
#define EM_SETFONTSIZE (WM_USER + 223)
#define EM_GETZOOM (WM_USER + 224)
#define EM_SETZOOM (WM_USER + 225)

#define EN_MSGFILTER 0x0700
#define EN_REQUESTRESIZE 0x0701
#define EN_SELCHANGE 0x0702
#define EN_DROPFILES 0x0703
#define EN_PROTECTED 0x0704
#define EN_CORRECTTEXT 0x0705
#define EN_STOPNOUNDO 0x0706
#define EN_IMECHANGE 0x0707
#define EN_SAVECLIPBOARD 0x0708
#define EN_OLEOPFAILED 0x0709
#define EN_OBJECTPOSITIONS 0x070a
#define EN_LINK 0x070b
#define EN_DRAGDROPDONE 0x070c
#define EN_PARAGRAPHEXPANDED 0x070d

#define ENM_NONE 0x00000000
#define ENM_CHANGE 0x00000001
#define ENM_UPDATE 0x00000002
#define ENM_SCROLL 0x00000004
#define ENM_SCROLLEVENTS 0x00000008
#define ENM_DRAGDROPDONE 0x00000010
#define ENM_PARAGRAPHEXPANDED 0x00000020
#define ENM_KEYEVENTS 0x00010000
#define ENM_MOUSEEVENTS 0x00020000
#define ENM_REQUESTRESIZE 0x00040000
#define ENM_SELCHANGE 0x00080000
#define ENM_DROPFILES 0x00100000
#define ENM_PROTECTED 0x00200000
#define ENM_CORRECTTEXT 0x00400000
#define ENM_IMECHANGE 0x00800000
#define ENM_LANGCHANGE 0x01000000
#define ENM_OBJECTPOSITIONS 0x02000000
#define ENM_LINK 0x04000000

#define ES_SAVESEL 0x00008000
#define ES_SUNKEN 0x00004000
#define ES_DISABLENOSCROLL 0x00002000
#define ES_SELECTIONBAR 0x01000000
#define ES_NOOLEDRAGDROP 0x00000008
#define ES_EX_NOCALLOLEINIT 0x01000000
#define ES_VERTICAL 0x00400000
#define ES_NOIME 0x00080000
#define ES_SELFIME 0x00040000

#define ECO_AUTOWORDSELECTION 0x00000001
#define ECO_AUTOVSCROLL 0x00000040
#define ECO_AUTOHSCROLL 0x00000080
#define ECO_NOHIDESEL 0x00000100
#define ECO_READONLY 0x00000800
#define ECO_WANTRETURN 0x00001000
#define ECO_SAVESEL 0x00008000
#define ECO_SELECTIONBAR 0x01000000
#define ECO_VERTICAL 0x00400000
#define ECOOP_SET 0x0001
#define ECOOP_OR 0x0002
#define ECOOP_AND 0x0003
#define ECOOP_XOR 0x0004

#define WB_CLASSIFY 3
#define WB_MOVEWORDLEFT 4
#define WB_MOVEWORDRIGHT 5
#define WB_LEFTBREAK 6
#define WB_RIGHTBREAK 7
#define WB_MOVEWORDPREV 4
#define WB_MOVEWORDNEXT 5
#define WB_PREVBREAK 6
#define WB_NEXTBREAK 7
#define WBF_WORDWRAP 0x010
#define WBF_WORDBREAK 0x020
#define WBF_OVERFLOW 0x040
#define WBF_LEVEL1 0x080
#define WBF_LEVEL2 0x100
#define WBF_CUSTOM 0x200

typedef LONG(CALLBACK* EDITWORDBREAKPROCEX)(char* pchText, LONG cchText, BYTE bCharSet, INT action);

typedef struct _charformat {
  UINT cbSize;
  DWORD dwMask;
  DWORD dwEffects;
  LONG yHeight;
  LONG yOffset;
  COLORREF crTextColor;
  BYTE bCharSet;
  BYTE bPitchAndFamily;
  char szFaceName[LF_FACESIZE];
} CHARFORMATA;
typedef struct _charformatw {
  UINT cbSize;
  DWORD dwMask;
  DWORD dwEffects;
  LONG yHeight;
  LONG yOffset;
  COLORREF crTextColor;
  BYTE bCharSet;
  BYTE bPitchAndFamily;
  WCHAR szFaceName[LF_FACESIZE];
} CHARFORMATW;

typedef struct _charformat2a : CHARFORMATA {
  WORD wWeight;
  SHORT sSpacing;
  COLORREF crBackColor;
  LCID lcid;
  DWORD dwReserved;
  SHORT sStyle;
  WORD wKerning;
  BYTE bUnderlineType;
  BYTE bAnimation;
  BYTE bRevAuthor;
  BYTE bReserved1;
} CHARFORMAT2A;
typedef struct _charformat2w : CHARFORMATW {
  WORD wWeight;
  SHORT sSpacing;
  COLORREF crBackColor;
  LCID lcid;
  DWORD dwReserved;
  SHORT sStyle;
  WORD wKerning;
  BYTE bUnderlineType;
  BYTE bAnimation;
  BYTE bRevAuthor;
  BYTE bReserved1;
} CHARFORMAT2W;

#ifdef UNICODE
#define CHARFORMAT CHARFORMATW
#define CHARFORMAT2 CHARFORMAT2W
#else
#define CHARFORMAT CHARFORMATA
#define CHARFORMAT2 CHARFORMAT2A
#endif

#define CFM_BOLD 0x00000001
#define CFM_ITALIC 0x00000002
#define CFM_UNDERLINE 0x00000004
#define CFM_STRIKEOUT 0x00000008
#define CFM_PROTECTED 0x00000010
#define CFM_LINK 0x00000020
#define CFM_SMALLCAPS 0x00000040
#define CFM_ALLCAPS 0x00000080
#define CFM_HIDDEN 0x00000100
#define CFM_OUTLINE 0x00000200
#define CFM_SHADOW 0x00000400
#define CFM_EMBOSS 0x00000800
#define CFM_IMPRINT 0x00001000
#define CFM_DISABLED 0x00002000
#define CFM_REVISED 0x00004000
#define CFM_BACKCOLOR 0x04000000
#define CFM_LCID 0x02000000
#define CFM_UNDERLINETYPE 0x00800000
#define CFM_WEIGHT 0x00400000
#define CFM_SPACING 0x00200000
#define CFM_KERNING 0x00100000
#define CFM_STYLE 0x00080000
#define CFM_ANIMATION 0x00040000
#define CFM_REVAUTHOR 0x00008000
#define CFM_SIZE 0x80000000
#define CFM_COLOR 0x40000000
#define CFM_FACE 0x20000000
#define CFM_OFFSET 0x10000000
#define CFM_CHARSET 0x08000000
#define CFM_SUBSCRIPT (CFE_SUBSCRIPT | CFE_SUPERSCRIPT)
#define CFM_SUPERSCRIPT CFM_SUBSCRIPT
#define CFM_EFFECTS (CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_COLOR | CFM_STRIKEOUT | CFE_PROTECTED | CFM_LINK)
#define CFM_ALL (CFM_EFFECTS | CFM_SIZE | CFM_FACE | CFM_OFFSET | CFM_CHARSET)
#define CFM_EFFECTS2                                                                                           \
  (CFM_EFFECTS | CFM_DISABLED | CFM_SMALLCAPS | CFM_ALLCAPS | CFM_HIDDEN | CFM_OUTLINE | CFM_SHADOW |         \
   CFM_EMBOSS | CFM_IMPRINT | CFM_DISABLED | CFM_REVISED | CFM_SUBSCRIPT | CFM_SUPERSCRIPT | CFM_BACKCOLOR)
#define CFM_ALL2                                                                                                \
  (CFM_ALL | CFM_EFFECTS2 | CFM_BACKCOLOR | CFM_LCID | CFM_UNDERLINETYPE | CFM_WEIGHT | CFM_REVAUTHOR |       \
   CFM_SPACING | CFM_KERNING | CFM_STYLE | CFM_ANIMATION)
#define CFE_BOLD 0x0001
#define CFE_ITALIC 0x0002
#define CFE_UNDERLINE 0x0004
#define CFE_STRIKEOUT 0x0008
#define CFE_PROTECTED 0x0010
#define CFE_LINK 0x0020
#define CFE_AUTOCOLOR 0x40000000
#define CFE_AUTOBACKCOLOR CFM_BACKCOLOR
#define CFE_SUBSCRIPT 0x00010000
#define CFE_SUPERSCRIPT 0x00020000
#define CFE_SMALLCAPS CFM_SMALLCAPS
#define CFE_ALLCAPS CFM_ALLCAPS
#define CFE_HIDDEN CFM_HIDDEN
#define CFE_OUTLINE CFM_OUTLINE
#define CFE_SHADOW CFM_SHADOW
#define CFE_EMBOSS CFM_EMBOSS
#define CFE_IMPRINT CFM_IMPRINT
#define CFE_DISABLED CFM_DISABLED
#define CFE_REVISED CFM_REVISED
#define yHeightCharPtsMost 1638
#define CFU_UNDERLINENONE 0
#define CFU_UNDERLINE 1
#define CFU_UNDERLINEWORD 2
#define CFU_UNDERLINEDOUBLE 3
#define CFU_UNDERLINEDOTTED 4

#define SCF_SELECTION 0x0001
#define SCF_WORD 0x0002
#define SCF_DEFAULT 0x0000
#define SCF_ALL 0x0004
#define SCF_USEUIRULES 0x0008

typedef struct _charrange {
  LONG cpMin;
  LONG cpMax;
} CHARRANGE;

typedef struct _textrange {
  CHARRANGE chrg;
  LPSTR lpstrText;
} TEXTRANGEA;
typedef struct _textrangew {
  CHARRANGE chrg;
  LPWSTR lpstrText;
} TEXTRANGEW;

typedef DWORD(CALLBACK* EDITSTREAMCALLBACK)(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb);
typedef struct _editstream {
  DWORD_PTR dwCookie;
  DWORD dwError;
  EDITSTREAMCALLBACK pfnCallback;
} EDITSTREAM;

#define SF_TEXT 0x0001
#define SF_RTF 0x0002
#define SF_RTFNOOBJS 0x0003
#define SF_TEXTIZED 0x0004
#define SF_UNICODE 0x0010
#define SF_USECODEPAGE 0x0020
#define SFF_SELECTION 0x8000
#define SFF_PLAINRTF 0x4000
#define SFF_PERSISTVIEWSCALE 0x2000
#define SFF_KEEPDOCINFO 0x1000

typedef struct _findtext {
  CHARRANGE chrg;
  LPCSTR lpstrText;
} FINDTEXTA;
typedef struct _findtextw {
  CHARRANGE chrg;
  LPCWSTR lpstrText;
} FINDTEXTW;
typedef struct _findtextexa {
  CHARRANGE chrg;
  LPCSTR lpstrText;
  CHARRANGE chrgText;
} FINDTEXTEXA;
typedef struct _findtextexw {
  CHARRANGE chrg;
  LPCWSTR lpstrText;
  CHARRANGE chrgText;
} FINDTEXTEXW;

typedef struct _formatrange {
  HDC hdc;
  HDC hdcTarget;
  RECT rc;
  RECT rcPage;
  CHARRANGE chrg;
} FORMATRANGE;

#define MAX_TAB_STOPS 32
#define lDefaultTab 720

typedef struct _paraformat {
  UINT cbSize;
  DWORD dwMask;
  WORD wNumbering;
  WORD wEffects;
  LONG dxStartIndent;
  LONG dxRightIndent;
  LONG dxOffset;
  WORD wAlignment;
  SHORT cTabCount;
  LONG rgxTabs[MAX_TAB_STOPS];
} PARAFORMAT;

typedef struct _paraformat2 : PARAFORMAT {
  LONG dySpaceBefore;
  LONG dySpaceAfter;
  LONG dyLineSpacing;
  SHORT sStyle;
  BYTE bLineSpacingRule;
  BYTE bOutlineLevel;
  WORD wShadingWeight;
  WORD wShadingStyle;
  WORD wNumberingStart;
  WORD wNumberingStyle;
  WORD wNumberingTab;
  WORD wBorderSpace;
  WORD wBorderWidth;
  WORD wBorders;
} PARAFORMAT2;

#define PFM_STARTINDENT 0x00000001
#define PFM_RIGHTINDENT 0x00000002
#define PFM_OFFSET 0x00000004
#define PFM_ALIGNMENT 0x00000008
#define PFM_TABSTOPS 0x00000010
#define PFM_NUMBERING 0x00000020
#define PFM_OFFSETINDENT 0x80000000
#define PFM_SPACEBEFORE 0x00000040
#define PFM_SPACEAFTER 0x00000080
#define PFM_LINESPACING 0x00000100
#define PFM_STYLE 0x00000400
#define PFM_BORDER 0x00000800
#define PFM_SHADING 0x00001000
#define PFM_NUMBERINGSTYLE 0x00002000
#define PFM_NUMBERINGTAB 0x00004000
#define PFM_NUMBERINGSTART 0x00008000
#define PFM_RTLPARA 0x00010000
#define PFM_KEEP 0x00020000
#define PFM_KEEPNEXT 0x00040000
#define PFM_PAGEBREAKBEFORE 0x00080000
#define PFM_NOLINENUMBER 0x00100000
#define PFM_NOWIDOWCONTROL 0x00200000
#define PFM_DONOTHYPHEN 0x00400000
#define PFM_SIDEBYSIDE 0x00800000
#define PFM_TABLE 0x40000000
#define PFM_ALL                                                                                      \
  (PFM_STARTINDENT | PFM_RIGHTINDENT | PFM_OFFSET | PFM_ALIGNMENT | PFM_TABSTOPS | PFM_NUMBERING | \
   PFM_OFFSETINDENT | PFM_RTLPARA)
#define PFM_EFFECTS                                                                                \
  (PFM_RTLPARA | PFM_KEEP | PFM_KEEPNEXT | PFM_TABLE | PFM_PAGEBREAKBEFORE | PFM_NOLINENUMBER |  \
   PFM_NOWIDOWCONTROL | PFM_DONOTHYPHEN | PFM_SIDEBYSIDE | PFM_TABLE)
#define PFM_ALL2                                                                                       \
  (PFM_ALL | PFM_EFFECTS | PFM_SPACEBEFORE | PFM_SPACEAFTER | PFM_LINESPACING | PFM_STYLE | PFM_SHADING | \
   PFM_BORDER | PFM_NUMBERINGTAB | PFM_NUMBERINGSTART | PFM_NUMBERINGSTYLE)
#define PFN_BULLET 0x0001
#define PFA_LEFT 0x0001
#define PFA_RIGHT 0x0002
#define PFA_CENTER 0x0003
#define PFA_JUSTIFY 0x0004
#define PFA_FULL_INTERWORD 0x0004

typedef struct _msgfilter {
  NMHDR nmhdr;
  UINT msg;
  WPARAM wParam;
  LPARAM lParam;
} MSGFILTER;

typedef struct _reqresize {
  NMHDR nmhdr;
  RECT rc;
} REQRESIZE;

typedef struct _selchange {
  NMHDR nmhdr;
  CHARRANGE chrg;
  WORD seltyp;
} SELCHANGE;
#define SEL_EMPTY 0x0000
#define SEL_TEXT 0x0001
#define SEL_OBJECT 0x0002
#define SEL_MULTICHAR 0x0004
#define SEL_MULTIOBJECT 0x0008

typedef struct _endropfiles {
  NMHDR nmhdr;
  HANDLE hDrop;
  LONG cp;
  BOOL fProtected;
} ENDROPFILES;

typedef struct _enprotected {
  NMHDR nmhdr;
  UINT msg;
  WPARAM wParam;
  LPARAM lParam;
  CHARRANGE chrg;
} ENPROTECTED;

typedef struct _ensaveclipboard {
  NMHDR nmhdr;
  LONG cObjectCount;
  LONG cch;
} ENSAVECLIPBOARD;

typedef struct _enoleopfailed {
  NMHDR nmhdr;
  LONG iob;
  LONG lOper;
  HRESULT hr;
} ENOLEOPFAILED;

typedef struct _objectpositions {
  NMHDR nmhdr;
  LONG cObjectCount;
  LONG* pcpPositions;
} OBJECTPOSITIONS;

typedef struct _enlink {
  NMHDR nmhdr;
  UINT msg;
  WPARAM wParam;
  LPARAM lParam;
  CHARRANGE chrg;
} ENLINK;

typedef struct _encorrecttext {
  NMHDR nmhdr;
  CHARRANGE chrg;
  WORD seltyp;
} ENCORRECTTEXT;

typedef struct _punctuation {
  UINT iSize;
  LPSTR szPunctuation;
} PUNCTUATION;

typedef struct _compcolor {
  COLORREF crText;
  COLORREF crBackground;
  DWORD dwEffects;
} COMPCOLOR;

#define CF_RTF TEXT("Rich Text Format")
#define CF_RTFNOOBJS TEXT("Rich Text Format Without Objects")
#define CF_RETEXTOBJ TEXT("RichEdit Text and Objects")

typedef struct _repastespecial {
  DWORD dwAspect;
  DWORD_PTR dwParam;
} REPASTESPECIAL;

typedef enum _undonameid { UID_UNKNOWN = 0, UID_TYPING = 1, UID_DELETE = 2, UID_DRAGDROP = 3, UID_CUT = 4,
  UID_PASTE = 5 } UNDONAMEID;

#define ST_DEFAULT 0
#define ST_KEEPUNDO 1
#define ST_SELECTION 2
typedef struct _settextex {
  DWORD flags;
  UINT codepage;
} SETTEXTEX;

#define GT_DEFAULT 0
#define GT_USECRLF 1
#define GT_SELECTION 2
typedef struct _gettextex {
  DWORD cb;
  DWORD flags;
  UINT codepage;
  LPCSTR lpDefaultChar;
  LPBOOL lpUsedDefChar;
} GETTEXTEX;

#define GTL_DEFAULT 0
#define GTL_USECRLF 1
#define GTL_PRECISE 2
#define GTL_CLOSE 4
#define GTL_NUMCHARS 8
#define GTL_NUMBYTES 16
typedef struct _gettextlengthex {
  DWORD flags;
  UINT codepage;
} GETTEXTLENGTHEX;

#define TM_PLAINTEXT 1
#define TM_RICHTEXT 2
#define TM_SINGLELEVELUNDO 4
#define TM_MULTILEVELUNDO 8
#define TM_SINGLECODEPAGE 16
#define TM_MULTICODEPAGE 32

#ifdef UNICODE
#define TEXTRANGE TEXTRANGEW
#define FINDTEXT FINDTEXTW
#define FINDTEXTEX FINDTEXTEXW
#else
#define TEXTRANGE TEXTRANGEA
#define FINDTEXT FINDTEXTA
#define FINDTEXTEX FINDTEXTEXA
#endif

#pragma pack(pop)

#endif
