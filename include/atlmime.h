// atlmime.h - mail messages in MIME (RFC 2045-2049, 2822): CMimeMessage
// with its headers, text parts, attachments made from files or memory, and
// messages attached whole. WriteData writes the message to a file handle;
// CSMTPConnection (atlsmtpconnection.h) sends it.
//
// Header text outside ASCII is "B" encoded in the code page's character
// set (CP_UTF8 gives utf-8, 0 the ANSI code page). Text parts outside
// ASCII go quoted-printable; attachments go base64 unless asked for
// uuencoding or quoted-printable.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLMIME_H__
#define __ATLMIME_H__

#include <atlbase.h>
#include <atlstr.h>
#include <atlcoll.h>
#include <atlenc.h>
#include <atlfile.h>
#include <stdio.h>
#include <time.h>
#include <vector>

namespace ATL {

enum ATL_MIME_PRIORITY {
  ATL_MIME_PRIORITY_ERROR = 0,
  ATL_MIME_HIGH_PRIORITY = 1,
  ATL_MIME_NORMAL_PRIORITY = 3,
  ATL_MIME_LOW_PRIORITY = 5
};

#define ATLSMTP_BASE64_ENCODE 0
#define ATLSMTP_UUENCODE 1
#define ATLSMTP_QP_ENCODE 2

#define ATL_MIME_BOUNDARYLEN 32
#define ATL_MIME_DEFAULT_CP CP_ACP

// The character set a code page's text is labelled with.
inline LPCSTR AtlMimeCharsetFromCodePage(UINT uiCodePage, LPSTR szBuf, int nBuf) {
  if (uiCodePage == 0) uiCodePage = ::GetACP();
  switch (uiCodePage) {
    case CP_UTF8: return "utf-8";
    case 20127: return "us-ascii";
    case 28591: return "iso-8859-1";
    case 28592: return "iso-8859-2";
    case 28595: return "iso-8859-5";
    case 28597: return "iso-8859-7";
    case 28605: return "iso-8859-15";
    case 932: return "shift_jis";
    case 936: return "gb2312";
    case 949: return "ks_c_5601-1987";
    case 950: return "big5";
    case 20866: return "koi8-r";
    case 50220: return "iso-2022-jp";
    case 51932: return "euc-jp";
    default: break;
  }
  snprintf(szBuf, (size_t)nBuf, "windows-%u", uiCodePage);
  return szBuf;
}

// TCHAR text as bytes of a code page.
inline CStringA _AtlMimeBytes(LPCTSTR psz, int nLen, UINT uiCodePage) {
  CStringA str;
  if (psz == NULL) return str;
  if (nLen < 0) nLen = (int)_tcslen(psz);
#ifdef _UNICODE
  if (uiCodePage == 0) uiCodePage = CP_ACP;
  int n = ::WideCharToMultiByte(uiCodePage, 0, psz, nLen, NULL, 0, NULL, NULL);
  if (n > 0) {
    ::WideCharToMultiByte(uiCodePage, 0, psz, nLen, str.GetBuffer(n), n, NULL, NULL);
    str.ReleaseBuffer(n);
  }
#else
  (void)uiCodePage;
  str.SetString(psz, nLen);
#endif
  return str;
}

inline bool _AtlMimeIsAscii(const char* p, int n) {
  for (int i = 0; i < n; i++)
    if ((unsigned char)p[i] > 126) return false;
  return true;
}

// A header value, "B" encoded when it is not ASCII.
inline CStringA _AtlMimeHeaderText(LPCTSTR psz, UINT uiCodePage) {
  CStringA str = _AtlMimeBytes(psz, -1, uiCodePage);
  if (_AtlMimeIsAscii(str, str.GetLength())) return str;
  char szCharset[32];
  LPCSTR pszCharset = AtlMimeCharsetFromCodePage(uiCodePage, szCharset, sizeof(szCharset));
  int nLen = BEncodeGetRequiredLength(str.GetLength(), pszCharset);
  CStringA strOut;
  BEncode((BYTE*)str.GetBuffer(), str.GetLength(), strOut.GetBuffer(nLen + 1), &nLen, pszCharset);
  str.ReleaseBuffer();
  strOut.ReleaseBuffer(nLen);
  return strOut;
}

// Writes all of a buffer, waiting for an overlapped write to finish.
inline BOOL AtlMimeWrite(HANDLE hFile, LPOVERLAPPED pOverlapped, const void* pv, DWORD dwLen) {
  const BYTE* p = (const BYTE*)pv;
  while (dwLen > 0) {
    DWORD dw = 0;
    if (!::WriteFile(hFile, p, dwLen, &dw, pOverlapped)) {
      if (pOverlapped == NULL || ::GetLastError() != ERROR_IO_PENDING) return FALSE;
      if (!::GetOverlappedResult(hFile, pOverlapped, &dw, TRUE)) return FALSE;
    } else if (pOverlapped != NULL) {
      ::GetOverlappedResult(hFile, pOverlapped, &dw, TRUE);
    }
    if (dw == 0) return FALSE;
    if (pOverlapped != NULL) {
      ULONGLONG ull = ((ULONGLONG)pOverlapped->OffsetHigh << 32 | pOverlapped->Offset) + dw;
      pOverlapped->Offset = (DWORD)ull;
      pOverlapped->OffsetHigh = (DWORD)(ull >> 32);
    }
    p += dw;
    dwLen -= dw;
  }
  return TRUE;
}

// The data of a body part, encoded and broken into lines.
inline CStringA _AtlMimeEncode(const BYTE* pData, int nLen, int nEncodingScheme, LPCTSTR szName) {
  CStringA str;
  int nOut = 0;
  switch (nEncodingScheme) {
    case ATLSMTP_UUENCODE:
      nOut = UUEncodeGetRequiredLength(nLen);
      UUEncode(pData, nLen, str.GetBuffer(nOut + 1), &nOut, szName,
               ATLSMTP_UUENCODE_HEADER | ATLSMTP_UUENCODE_END);
      break;
    case ATLSMTP_QP_ENCODE:
      nOut = QPEncodeGetRequiredLength(nLen);
      QPEncode((BYTE*)pData, nLen, str.GetBuffer(nOut + 1), &nOut, 0);
      break;
    default:
      nOut = Base64EncodeGetRequiredLength(nLen);
      Base64Encode(pData, nLen, str.GetBuffer(nOut + 1), &nOut);
      break;
  }
  str.ReleaseBuffer(nOut);
  if (str.GetLength() > 0 && str[str.GetLength() - 1] != '\n') str += "\r\n";
  return str;
}

inline LPCSTR _AtlMimeEncodingName(int nEncodingScheme) {
  switch (nEncodingScheme) {
    case ATLSMTP_UUENCODE: return "x-uuencode";
    case ATLSMTP_QP_ENCODE: return "quoted-printable";
    default: return "base64";
  }
}

// ---------------------------------------------------------------------
// The parts of a message.

class CMimeBodyPart {
public:
  virtual ~CMimeBodyPart() {}
  virtual BOOL WriteData(HANDLE hFile, LPOVERLAPPED pOverlapped, LPCSTR szBoundary,
                         DWORD dwFlags = 0) {
    CStringA str;
    if (!MakeMimeString(str, szBoundary, dwFlags)) return FALSE;
    return AtlMimeWrite(hFile, pOverlapped, (LPCSTR)str, (DWORD)str.GetLength());
  }
  virtual CMimeBodyPart* Copy() = 0;
  // The part's text: its headers, a blank line and its encoded data.
  virtual BOOL MakeMimeString(CStringA& str, LPCSTR szBoundary, DWORD dwFlags = 0) = 0;
};

// Text: plain, unless SetContentType says otherwise.
class CMimeText : public CMimeBodyPart {
public:
  CMimeText() : m_uiCodePage(0), m_strContentType("text/plain") {}

  BOOL Initialize(LPCTSTR szText, int nTextLen = -1, UINT uiCodePage = 0) {
    m_uiCodePage = uiCodePage;
    m_strText = _AtlMimeBytes(szText, nTextLen, uiCodePage);
    return TRUE;
  }
  BOOL SetText(LPCTSTR szText, int nTextLen = -1) { return Initialize(szText, nTextLen, m_uiCodePage); }
  void SetContentType(LPCSTR szType) { m_strContentType = szType; }
  LPCSTR GetText() const { return m_strText; }
  int GetTextLength() const { return m_strText.GetLength(); }

  CMimeBodyPart* Copy() override { return new CMimeText(*this); }
  BOOL MakeMimeString(CStringA& str, LPCSTR /*szBoundary*/, DWORD /*dwFlags*/ = 0) override {
    char szCharset[32];
    bool bAscii = _AtlMimeIsAscii(m_strText, m_strText.GetLength());
    str.Format("Content-Type: %s;\r\n\tcharset=\"%s\"\r\nContent-Transfer-Encoding: %s\r\n\r\n",
               (LPCSTR)m_strContentType,
               bAscii ? "us-ascii" : AtlMimeCharsetFromCodePage(m_uiCodePage, szCharset, sizeof(szCharset)),
               bAscii ? "7bit" : "quoted-printable");
    if (bAscii) {
      str += _NormalizeLines(m_strText);
    } else {
      str += _AtlMimeEncode((const BYTE*)(LPCSTR)m_strText, m_strText.GetLength(), ATLSMTP_QP_ENCODE, NULL);
    }
    if (str.Right(2) != "\r\n") str += "\r\n";
    return TRUE;
  }

protected:
  UINT m_uiCodePage;
  CStringA m_strText;
  CStringA m_strContentType;

  // Lines end in CRLF in mail.
  static CStringA _NormalizeLines(const CStringA& s) {
    CStringA out;
    for (int i = 0; i < s.GetLength(); i++) {
      char ch = s[i];
      if (ch == '\n' && (i == 0 || s[i - 1] != '\r'))
        out += "\r\n";
      else if (ch == '\r' && (i + 1 >= s.GetLength() || s[i + 1] != '\n'))
        out += "\r\n";
      else
        out += ch;
    }
    return out;
  }
};

// What files and memory attachments share: the name, type and encoding.
class CMimeAttachment : public CMimeBodyPart {
public:
  CMimeAttachment()
      : m_nEncodingScheme(ATLSMTP_BASE64_ENCODE), m_uiCodePage(0),
        m_strContentType("application/octet-stream") {}

  BOOL SetDisplayName(LPCTSTR szDisplayName) {
    if (szDisplayName == NULL) return FALSE;
    m_strDisplayName = szDisplayName;
    return TRUE;
  }
  LPCTSTR GetDisplayName() const { return m_strDisplayName; }
  void SetContentType(LPCTSTR szContent) {
    if (szContent != NULL) m_strContentType = _AtlMimeBytes(szContent, -1, CP_ACP);
  }
  LPCSTR GetContentType() const { return m_strContentType; }
  void SetEncodingScheme(int nScheme) { m_nEncodingScheme = nScheme; }
  int GetEncodingScheme() const { return m_nEncodingScheme; }
  void SetCodePage(UINT uiCodePage) { m_uiCodePage = uiCodePage; }

  BOOL MakeMimeString(CStringA& str, LPCSTR /*szBoundary*/, DWORD /*dwFlags*/ = 0) override {
    CAtlArray<BYTE> data;
    if (!GetData(data)) return FALSE;
    CStringA strName = _AtlMimeHeaderText(m_strDisplayName, m_uiCodePage);
    str.Format(
        "Content-Type: %s;\r\n\tname=\"%s\"\r\nContent-Transfer-Encoding: %s\r\n"
        "Content-Disposition: attachment;\r\n\tfilename=\"%s\"\r\n\r\n",
        (LPCSTR)m_strContentType, (LPCSTR)strName, _AtlMimeEncodingName(m_nEncodingScheme),
        (LPCSTR)strName);
    str += _AtlMimeEncode(data.GetCount() ? data.GetData() : NULL, (int)data.GetCount(),
                          m_nEncodingScheme, m_strDisplayName);
    return TRUE;
  }

protected:
  int m_nEncodingScheme;
  UINT m_uiCodePage;
  CString m_strDisplayName;
  CStringA m_strContentType;

  virtual BOOL GetData(CAtlArray<BYTE>& data) = 0;
};

// A file's contents, read when the message is written.
class CMimeFileAttachment : public CMimeAttachment {
public:
  BOOL Initialize(LPCTSTR szFileName, LPCTSTR szDisplayName = NULL) {
    if (szFileName == NULL) return FALSE;
    m_strFileName = szFileName;
    if (szDisplayName != NULL) {
      m_strDisplayName = szDisplayName;
    } else {
      int n = m_strFileName.GetLength();
      while (n > 0 && m_strFileName[n - 1] != '\\' && m_strFileName[n - 1] != '/') n--;
      m_strDisplayName = m_strFileName.Mid(n);
    }
    return ::GetFileAttributes(szFileName) != INVALID_FILE_ATTRIBUTES;
  }
  LPCTSTR GetFileName() const { return m_strFileName; }
  CMimeBodyPart* Copy() override { return new CMimeFileAttachment(*this); }

protected:
  CString m_strFileName;

  BOOL GetData(CAtlArray<BYTE>& data) override {
    HANDLE h = ::CreateFile(m_strFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD dwSize = ::GetFileSize(h, NULL);
    BOOL bOk = dwSize != INVALID_FILE_SIZE && data.SetCount(dwSize);
    DWORD dwRead = 0;
    if (bOk && dwSize > 0) bOk = ::ReadFile(h, data.GetData(), dwSize, &dwRead, NULL) && dwRead == dwSize;
    ::CloseHandle(h);
    return bOk;
  }
};

// Bytes in memory, copied or (bCopyData FALSE) kept by pointer.
class CMimeRawAttachment : public CMimeAttachment {
public:
  CMimeRawAttachment() : m_pvRaw(NULL), m_dwLength(0), m_bShared(false) {}
  CMimeRawAttachment(const CMimeRawAttachment& that)
      : CMimeAttachment(that), m_pvRaw(that.m_pvRaw), m_dwLength(that.m_dwLength),
        m_bShared(that.m_bShared) {
    if (!m_bShared) m_copy.Copy(that.m_copy);
  }

  BOOL Initialize(void* pData, DWORD nDataLength, BOOL bCopyData = TRUE, LPCTSTR szDisplayName = NULL) {
    if (pData == NULL && nDataLength > 0) return FALSE;
    m_dwLength = nDataLength;
    m_bShared = !bCopyData;
    if (bCopyData) {
      if (!m_copy.SetCount(nDataLength)) return FALSE;
      if (nDataLength > 0) memcpy(m_copy.GetData(), pData, nDataLength);
      m_pvRaw = NULL;
    } else {
      m_copy.RemoveAll();
      m_pvRaw = pData;
    }
    m_strDisplayName = szDisplayName != NULL ? szDisplayName : _T("rawdata");
    return TRUE;
  }
  CMimeBodyPart* Copy() override { return new CMimeRawAttachment(*this); }

protected:
  void* m_pvRaw;
  DWORD m_dwLength;
  bool m_bShared;
  CAtlArray<BYTE> m_copy;

  BOOL GetData(CAtlArray<BYTE>& data) override {
    if (!m_bShared) {
      data.Copy(m_copy);
      return TRUE;
    }
    if (!data.SetCount(m_dwLength)) return FALSE;
    if (m_dwLength > 0) memcpy(data.GetData(), m_pvRaw, m_dwLength);
    return TRUE;
  }
};

// ---------------------------------------------------------------------
// The headers: who it is from and to, its subject and priority.

class CMimeHeader : public CMimeBodyPart {
public:
  CMimeHeader() : m_nPriority(ATL_MIME_NORMAL_PRIORITY), m_uiSenderCP(0), m_uiSubjectCP(0) {}

  BOOL SetSender(LPCTSTR szSender) {
    if (szSender == NULL) return FALSE;
    m_strSender = _AtlMimeBytes(szSender, -1, CP_ACP);
    return TRUE;
  }
  LPCSTR GetSender() const { return m_strSender; }
  BOOL SetSenderName(LPCTSTR szName, UINT uiCodePage = 0) {
    if (szName == NULL) return FALSE;
    m_strSenderName = szName;
    m_uiSenderCP = uiCodePage;
    return TRUE;
  }
  LPCTSTR GetSenderName() const { return m_strSenderName; }
  BOOL SetReplyTo(LPCTSTR szReplyTo) {
    if (szReplyTo == NULL) return FALSE;
    m_strReplyTo = _AtlMimeBytes(szReplyTo, -1, CP_ACP);
    return TRUE;
  }
  LPCSTR GetReplyTo() const { return m_strReplyTo; }
  BOOL SetSubject(LPCTSTR szSubject, UINT uiCodePage = 0) {
    if (szSubject == NULL) return FALSE;
    m_strSubject = szSubject;
    m_uiSubjectCP = uiCodePage;
    return TRUE;
  }
  LPCTSTR GetSubject() const { return m_strSubject; }
  BOOL SetPriority(ATL_MIME_PRIORITY nPriority) {
    if (nPriority != ATL_MIME_HIGH_PRIORITY && nPriority != ATL_MIME_NORMAL_PRIORITY &&
        nPriority != ATL_MIME_LOW_PRIORITY)
      return FALSE;
    m_nPriority = nPriority;
    return TRUE;
  }
  ATL_MIME_PRIORITY GetPriority() const { return m_nPriority; }

  BOOL AddRecipient(LPCTSTR szAddress, LPCTSTR szName = NULL, UINT uiCodePage = 0) {
    return _Add(m_to, szAddress, szName, uiCodePage);
  }
  BOOL AddCc(LPCTSTR szAddress, LPCTSTR szName = NULL, UINT uiCodePage = 0) {
    return _Add(m_cc, szAddress, szName, uiCodePage);
  }
  BOOL AddBcc(LPCTSTR szAddress) { return _Add(m_bcc, szAddress, NULL, 0); }
  void ClearRecipients() { m_to.clear(); }
  void ClearCc() { m_cc.clear(); }
  void ClearBcc() { m_bcc.clear(); }

  // Every address it goes to, To, Cc and Bcc, separated by commas, and
  // the room that takes with its NUL.
  DWORD GetRequiredRecipientsStringLength() const {
    return (DWORD)_RecipientsString().GetLength() + 1;
  }
  BOOL GetRecipientsString(LPSTR szRecip, LPDWORD pdwLen) {
    if (pdwLen == NULL) return FALSE;
    CStringA str = _RecipientsString();
    if (szRecip == NULL || *pdwLen < (DWORD)str.GetLength() + 1) {
      *pdwLen = (DWORD)str.GetLength() + 1;
      return FALSE;
    }
    memcpy(szRecip, (LPCSTR)str, (size_t)str.GetLength() + 1);
    *pdwLen = (DWORD)str.GetLength();
    return TRUE;
  }

  CMimeBodyPart* Copy() override { return new CMimeHeader(*this); }
  BOOL MakeMimeString(CStringA& str, LPCSTR /*szBoundary*/, DWORD /*dwFlags*/ = 0) override {
    str.Empty();
    _AppendHeaders(str);
    return TRUE;
  }

protected:
  struct _Recipient {
    CStringA strAddress;
    CString strName;
    UINT uiCodePage;
  };
  std::vector<_Recipient> m_to, m_cc, m_bcc;
  CStringA m_strSender, m_strReplyTo;
  CString m_strSenderName, m_strSubject;
  ATL_MIME_PRIORITY m_nPriority;
  UINT m_uiSenderCP, m_uiSubjectCP;

  static BOOL _Add(std::vector<_Recipient>& a, LPCTSTR szAddress, LPCTSTR szName, UINT uiCodePage) {
    if (szAddress == NULL || *szAddress == 0) return FALSE;
    _Recipient r;
    r.strAddress = _AtlMimeBytes(szAddress, -1, CP_ACP);
    r.strName = szName != NULL ? szName : _T("");
    r.uiCodePage = uiCodePage;
    a.push_back(r);
    return TRUE;
  }
  static CStringA _Mailbox(const CStringA& strAddress, const CString& strName, UINT uiCodePage) {
    if (strName.IsEmpty()) return strAddress;
    CStringA strOut;
    strOut.Format("\"%s\" <%s>", (LPCSTR)_AtlMimeHeaderText(strName, uiCodePage), (LPCSTR)strAddress);
    return strOut;
  }
  static void _AppendList(CStringA& str, LPCSTR szField, const std::vector<_Recipient>& a) {
    if (a.empty()) return;
    str += szField;
    str += ": ";
    for (size_t i = 0; i < a.size(); i++) {
      if (i > 0) str += ",\r\n\t";
      str += _Mailbox(a[i].strAddress, a[i].strName, a[i].uiCodePage);
    }
    str += "\r\n";
  }
  CStringA _RecipientsString() const {
    CStringA str;
    const std::vector<_Recipient>* lists[] = {&m_to, &m_cc, &m_bcc};
    for (const std::vector<_Recipient>* p : lists)
      for (size_t i = 0; i < p->size(); i++) {
        if (!str.IsEmpty()) str += ",";
        str += (*p)[i].strAddress;
      }
    return str;
  }
  void _AppendHeaders(CStringA& str) {
    static const char* const kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    time_t t = time(NULL);
    struct tm tmUtc = *gmtime(&t);
    CStringA strDate;
    strDate.Format("Date: %s, %d %s %04d %02d:%02d:%02d +0000\r\n", kDays[tmUtc.tm_wday],
                   tmUtc.tm_mday, kMonths[tmUtc.tm_mon], tmUtc.tm_year + 1900, tmUtc.tm_hour,
                   tmUtc.tm_min, tmUtc.tm_sec);
    str += strDate;
    if (!m_strSender.IsEmpty()) {
      str += "From: ";
      str += _Mailbox(m_strSender, m_strSenderName, m_uiSenderCP);
      str += "\r\n";
    }
    _AppendList(str, "To", m_to);
    _AppendList(str, "Cc", m_cc);
    if (!m_strReplyTo.IsEmpty()) str += "Reply-To: " + m_strReplyTo + "\r\n";
    str += "Subject: " + _AtlMimeHeaderText(m_strSubject, m_uiSubjectCP) + "\r\n";
    static const char* const kPriority[] = {"", "1 (Highest)", "", "3 (Normal)", "", "5 (Lowest)"};
    str += "X-Priority: ";
    str += kPriority[m_nPriority];
    str += "\r\n";
    str += "X-Mailer: ATL MIME\r\nMIME-Version: 1.0\r\n";
  }
};

// ---------------------------------------------------------------------
// The message: its headers and its parts, in order.

class CMimeMessage : public CMimeHeader {
public:
  CMimeMessage() : m_nBoundary(0) {}
  CMimeMessage(const CMimeMessage& that) : CMimeHeader(that), m_nBoundary(0) { _CopyParts(that); }
  CMimeMessage& operator=(const CMimeMessage& that) {
    if (this != &that) {
      CMimeHeader::operator=(that);
      _Free();
      _CopyParts(that);
    }
    return *this;
  }
  ~CMimeMessage() { _Free(); }

  void RemoveParts() { _Free(); }
  size_t GetPartCount() const { return m_parts.GetCount(); }

  // Text, at nPos (1 for the first part; -1 or past the end for last).
  BOOL AddText(LPCTSTR szText, int nTextLen = -1, int nPos = 1, UINT uiCodePage = 0) {
    if (szText == NULL) return FALSE;
    CMimeText* p = new CMimeText;
    p->Initialize(szText, nTextLen, uiCodePage);
    _Insert(p, nPos);
    return TRUE;
  }
  BOOL AddMimeBodyPart(CMimeBodyPart* pPart, int nPos = -1) {
    if (pPart == NULL) return FALSE;
    _Insert(pPart->Copy(), nPos);
    return TRUE;
  }
  BOOL AttachFile(LPCTSTR szFileName, LPCTSTR szDisplayName = NULL,
                  LPCTSTR szContentType = _T("application/octet-stream"),
                  int nEncodingScheme = ATLSMTP_BASE64_ENCODE, UINT uiCodepage = 0) {
    CMimeFileAttachment* p = new CMimeFileAttachment;
    if (!p->Initialize(szFileName, szDisplayName)) {
      delete p;
      return FALSE;
    }
    p->SetContentType(szContentType);
    p->SetEncodingScheme(nEncodingScheme);
    p->SetCodePage(uiCodepage);
    m_parts.Add(p);
    return TRUE;
  }
  BOOL AttachRaw(void* pRawData, DWORD dwDataLength, int nEncodingScheme = ATLSMTP_BASE64_ENCODE,
                 BOOL bCopyData = TRUE, LPCTSTR szDisplayName = NULL,
                 LPCTSTR szContentType = _T("application/octet-stream"), UINT uiCodepage = 0) {
    CMimeRawAttachment* p = new CMimeRawAttachment;
    if (!p->Initialize(pRawData, dwDataLength, bCopyData, szDisplayName)) {
      delete p;
      return FALSE;
    }
    p->SetContentType(szContentType);
    p->SetEncodingScheme(nEncodingScheme);
    p->SetCodePage(uiCodepage);
    m_parts.Add(p);
    return TRUE;
  }
  // Another message, copied, as a message/rfc822 part.
  BOOL AttachMessage(CMimeMessage* pMsg) {
    if (pMsg == NULL) return FALSE;
    m_parts.Add(new _AttachedMessage(*pMsg));
    return TRUE;
  }
  // The name an attached copy of this message has.
  void SetDisplayName(LPCTSTR szDisplayName) {
    m_strDisplayName = szDisplayName != NULL ? szDisplayName : _T("");
  }
  LPCTSTR GetDisplayName() const { return m_strDisplayName; }

  CMimeBodyPart* Copy() override { return new CMimeMessage(*this); }

  // The whole message, its own boundary between the parts.
  BOOL MakeMimeString(CStringA& str, LPCSTR /*szBoundary*/ = NULL, DWORD dwFlags = 0) override {
    str.Empty();
    _AppendHeaders(str);
    if (m_parts.IsEmpty()) {
      str += "Content-Type: text/plain;\r\n\tcharset=\"us-ascii\"\r\n\r\n";
      return TRUE;
    }
    char szBoundary[ATL_MIME_BOUNDARYLEN + 1];
    _MakeBoundary(szBoundary);
    str += "Content-Type: multipart/mixed;\r\n\tboundary=\"";
    str += szBoundary;
    str += "\"\r\n\r\nThis is a multi-part message in MIME format.\r\n";
    for (size_t i = 0; i < m_parts.GetCount(); i++) {
      CStringA strPart;
      if (!m_parts[i]->MakeMimeString(strPart, szBoundary, dwFlags)) return FALSE;
      str += "\r\n--";
      str += szBoundary;
      str += "\r\n";
      str += strPart;
    }
    str += "\r\n--";
    str += szBoundary;
    str += "--\r\n";
    return TRUE;
  }

protected:
  CAtlArray<CMimeBodyPart*> m_parts;
  CString m_strDisplayName;
  ULONG m_nBoundary;

  // A copy of another message, held until the part goes.
  class _AttachedMessage : public CMimeBodyPart {
  public:
    explicit _AttachedMessage(const CMimeMessage& msg) : m_pMsg(new CMimeMessage(msg)) {}
    ~_AttachedMessage() { delete m_pMsg; }
    CMimeBodyPart* Copy() override { return new _AttachedMessage(*m_pMsg); }
    BOOL MakeMimeString(CStringA& str, LPCSTR /*szBoundary*/, DWORD dwFlags = 0) override {
      CStringA strName = _AtlMimeHeaderText(m_pMsg->GetDisplayName(), 0);
      CStringA strBody;
      if (!m_pMsg->MakeMimeString(strBody, NULL, dwFlags)) return FALSE;
      str = "Content-Type: message/rfc822";
      if (!strName.IsEmpty()) str += ";\r\n\tname=\"" + strName + "\"";
      str += "\r\nContent-Transfer-Encoding: 7bit\r\nContent-Disposition: attachment";
      if (!strName.IsEmpty()) str += ";\r\n\tfilename=\"" + strName + "\"";
      str += "\r\n\r\n";
      str += strBody;
      return TRUE;
    }

  private:
    CMimeMessage* m_pMsg;
    _AttachedMessage(const _AttachedMessage&);
    _AttachedMessage& operator=(const _AttachedMessage&);
  };

  void _Insert(CMimeBodyPart* p, int nPos) {
    if (nPos < 1 || (size_t)nPos > m_parts.GetCount())
      m_parts.Add(p);
    else
      m_parts.InsertAt((size_t)nPos - 1, p);
  }
  void _Free() {
    for (size_t i = 0; i < m_parts.GetCount(); i++) delete m_parts[i];
    m_parts.RemoveAll();
  }
  void _CopyParts(const CMimeMessage& that) {
    m_strDisplayName = that.m_strDisplayName;
    for (size_t i = 0; i < that.m_parts.GetCount(); i++) m_parts.Add(that.m_parts[i]->Copy());
  }
  // A boundary no encoded data can hold: "----=_" and hex.
  void _MakeBoundary(char* sz) {
    static LONG s_nCount = 0;
    LONG n = ::InterlockedIncrement(&s_nCount);
    snprintf(sz, ATL_MIME_BOUNDARYLEN + 1, "------=_NextPart_%08lX.%06lX",
             (unsigned long)::GetTickCount(), (unsigned long)(n & 0xFFFFFF));
  }
};

}  // namespace ATL

#endif  // __ATLMIME_H__
