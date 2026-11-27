// atlsmtpconnection.h - CSMTPConnection: mail sent to an SMTP server
// (RFC 5321), a CMimeMessage or raw text, the server greeted with EHLO (HELO
// when that is refused). Winsock must be started first.
//
// Connect takes a host name, and here also "host:port" for a server not on
// port 25. The overlapped parameters are taken for compatibility; every
// call finishes before it returns.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLSMTPCONNECTION_H__
#define __ATLSMTPCONNECTION_H__

#include <atlsocket.h>
#include <atlmime.h>

#ifndef ATLSMTP_CONN_TIMEOUT
#define ATLSMTP_CONN_TIMEOUT 10000
#endif
#ifndef ATLSMTP_PORT
#define ATLSMTP_PORT 25
#endif

namespace ATL {

class CSMTPConnection {
public:
  CSMTPConnection() : m_nLastReply(0) {}
  ~CSMTPConnection() { Disconnect(); }

  BOOL Connected() const { return m_socket.GetSocket() != INVALID_SOCKET; }

  BOOL Connect(LPCTSTR lpszHostName, DWORD dwTimeout = ATLSMTP_CONN_TIMEOUT) {
    if (lpszHostName == NULL || Connected()) return FALSE;
    CString strHost(lpszHostName);
    unsigned short nPort = ATLSMTP_PORT;
    int nColon = strHost.ReverseFind(_T(':'));
    if (nColon > 0 && strHost.Find(_T(':')) == nColon) {
      nPort = (unsigned short)_ttoi(strHost.Mid(nColon + 1));
      strHost = strHost.Left(nColon);
    }
    m_socket.SetSocketTimeout(dwTimeout);
    m_strBuffer.Empty();
    if (!m_socket.Connect(strHost, nPort)) {
      m_socket.Close();
      return FALSE;
    }
    if (_Reply() != 220) {
      m_socket.Close();
      return FALSE;
    }
    char szName[256] = "localhost";
    ::gethostname(szName, sizeof(szName));
    szName[sizeof(szName) - 1] = 0;
    CStringA strHello;
    strHello.Format("EHLO %s\r\n", szName);
    if (_Command(strHello) != 250) {
      strHello.Format("HELO %s\r\n", szName);
      if (_Command(strHello) != 250) {
        m_socket.Close();
        return FALSE;
      }
    }
    return TRUE;
  }

  BOOL Disconnect(DWORD dwTimeout = ATLSMTP_CONN_TIMEOUT) {
    if (!Connected()) return FALSE;
    m_socket.SetSocketTimeout(dwTimeout);
    BOOL bOk = _Command("QUIT\r\n") == 221;
    m_socket.Close();
    return bOk;
  }

  // The message to its recipients (or szRecipients, separated by commas
  // or semicolons), from its sender (or szSender).
  BOOL SendMessage(CMimeMessage& msg, LPCTSTR szRecipients = NULL, LPCTSTR szSender = NULL,
                   LPOVERLAPPED /*pOverlapped*/ = NULL) {
    CStringA strRecipients, strSender;
    if (szRecipients != NULL) {
      strRecipients = _AtlMimeBytes(szRecipients, -1, CP_ACP);
    } else {
      DWORD dwLen = msg.GetRequiredRecipientsStringLength();
      msg.GetRecipientsString(strRecipients.GetBuffer((int)dwLen), &dwLen);
      strRecipients.ReleaseBuffer();
    }
    strSender = szSender != NULL ? _AtlMimeBytes(szSender, -1, CP_ACP) : CStringA(msg.GetSender());
    CStringA strData;
    if (!msg.MakeMimeString(strData, NULL)) return FALSE;
    return _Send(strData, strRecipients, strSender);
  }
  BOOL SendRaw(LPCTSTR szRawData, DWORD dwLen, LPCTSTR szRecipients, LPCTSTR szSender,
               LPOVERLAPPED /*pOverlapped*/ = NULL) {
    if (szRawData == NULL || szRecipients == NULL || szSender == NULL) return FALSE;
    return _Send(_AtlMimeBytes(szRawData, (int)dwLen, CP_ACP), _AtlMimeBytes(szRecipients, -1, CP_ACP),
                 _AtlMimeBytes(szSender, -1, CP_ACP));
  }
  BOOL SendSimple(LPCTSTR szRecipients, LPCTSTR szSender, LPCTSTR szSubject, LPCTSTR szBody,
                  int nTextLen = -1, LPOVERLAPPED pOverlapped = NULL) {
    if (szRecipients == NULL || szSender == NULL || szBody == NULL) return FALSE;
    CMimeMessage msg;
    msg.SetSender(szSender);
    msg.SetSubject(szSubject != NULL ? szSubject : _T(""));
    msg.AddText(szBody, nTextLen);
    return SendMessage(msg, szRecipients, szSender, pOverlapped);
  }

  // The code of the server's last reply, and its text.
  int GetLastReplyCode() const { return m_nLastReply; }
  LPCSTR GetLastReply() const { return m_strLastReply; }

protected:
  ZEvtSyncSocket m_socket;
  CStringA m_strBuffer;
  CStringA m_strLastReply;
  int m_nLastReply;

  BOOL _Send(const CStringA& strData, const CStringA& strRecipients, const CStringA& strSender) {
    if (!Connected() || strSender.IsEmpty()) return FALSE;
    CStringA strCmd;
    strCmd.Format("MAIL FROM:<%s>\r\n", (LPCSTR)_Bare(strSender));
    if (_Command(strCmd) != 250) return _Reset();
    int nRecipients = 0;
    int nStart = 0;
    while (nStart <= strRecipients.GetLength()) {
      int nEnd = nStart;
      while (nEnd < strRecipients.GetLength() && strRecipients[nEnd] != ',' && strRecipients[nEnd] != ';')
        nEnd++;
      CStringA strOne = _Bare(strRecipients.Mid(nStart, nEnd - nStart));
      nStart = nEnd + 1;
      if (strOne.IsEmpty()) continue;
      strCmd.Format("RCPT TO:<%s>\r\n", (LPCSTR)strOne);
      int n = _Command(strCmd);
      if (n != 250 && n != 251) return _Reset();
      nRecipients++;
    }
    if (nRecipients == 0) return _Reset();
    if (_Command("DATA\r\n") != 354) return _Reset();
    CStringA strBody = _DotStuff(strData);
    DWORD dw = (DWORD)strBody.GetLength();
    if (!m_socket.Write((const unsigned char*)(LPCSTR)strBody, &dw)) return FALSE;
    return _Command(".\r\n") == 250;
  }
  BOOL _Reset() {
    _Command("RSET\r\n");
    return FALSE;
  }
  // An address without the name or brackets around it.
  static CStringA _Bare(CStringA str) {
    int nLt = str.Find('<'), nGt = str.ReverseFind('>');
    if (nLt >= 0 && nGt > nLt) str = str.Mid(nLt + 1, nGt - nLt - 1);
    str.Trim();
    return str;
  }
  // Lines starting with a dot get another; the data ends in CRLF.
  static CStringA _DotStuff(const CStringA& strIn) {
    CStringA str;
    bool bLineStart = true;
    for (int i = 0; i < strIn.GetLength(); i++) {
      char ch = strIn[i];
      if (bLineStart && ch == '.') str += '.';
      str += ch;
      bLineStart = ch == '\n';
    }
    if (str.Right(2) != "\r\n") str += "\r\n";
    return str;
  }
  int _Command(LPCSTR szCmd) {
    DWORD dw = (DWORD)strlen(szCmd);
    if (!m_socket.Write((const unsigned char*)szCmd, &dw)) return 0;
    return _Reply();
  }
  // A reply, its lines "nnn-" until "nnn ".
  int _Reply() {
    m_strLastReply.Empty();
    m_nLastReply = 0;
    for (;;) {
      int nEol;
      while ((nEol = m_strBuffer.Find('\n')) < 0) {
        char buf[512];
        DWORD dw = sizeof(buf);
        if (!m_socket.Read((const unsigned char*)buf, &dw) || dw == 0) return 0;
        m_strBuffer.Append(buf, (int)dw);
      }
      CStringA strLine = m_strBuffer.Left(nEol + 1);
      m_strBuffer = m_strBuffer.Mid(nEol + 1);
      m_strLastReply += strLine;
      if (strLine.GetLength() < 4) return 0;
      if (strLine[3] != '-') {
        m_nLastReply = atoi(strLine.Left(3));
        return m_nLastReply;
      }
    }
  }

private:
  CSMTPConnection(const CSMTPConnection&);
  CSMTPConnection& operator=(const CSMTPConnection&);
};

}  // namespace ATL

#endif  // __ATLSMTPCONNECTION_H__
