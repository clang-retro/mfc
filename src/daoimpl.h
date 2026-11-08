// daoimpl.h - what the DAO classes' sources share.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __DAOIMPL_H__
#define __DAOIMPL_H__

#include "afximpl.h"
#include <afxdao.h>

// A BSTR made from a string for one call, freed after it.
class _AfxDaoBstr {
public:
  explicit _AfxDaoBstr(LPCTSTR psz);
  ~_AfxDaoBstr() { ::SysFreeString(m_bstr); }
  operator BSTR() const { return m_bstr; }

private:
  BSTR m_bstr;
  _AfxDaoBstr(const _AfxDaoBstr&);
  void operator=(const _AfxDaoBstr&);
};

// A BSTR the engine returned, as a string; the BSTR is freed.
CString AFXAPI _AfxDaoString(BSTR bstr);
// An optional argument left out.
VARIANT AFXAPI _AfxDaoOptional();
// A collection's item by name or by number.
COleVariant AFXAPI _AfxDaoItem(LPCTSTR lpszName);
COleVariant AFXAPI _AfxDaoItem(int nIndex);
COleVariant AFXAPI _AfxDaoLong(long l);
COleVariant AFXAPI _AfxDaoBool(BOOL b);
CString AFXAPI _AfxDaoStripBrackets(LPCTSTR lpszName);

// In an ANSI program the text in the classes' variants is ANSI, held in
// the BSTR as bytes (VT_BSTRT), as it was with DAO's ANSI interfaces. The
// library drives the engine through its Unicode ones, so a variant from
// the program is widened on its way in, and one from the engine narrowed
// on its way out. In a Unicode program both do nothing.
void AFXAPI _AfxDaoToEngine(COleVariant& var);
void AFXAPI _AfxDaoFromEngine(COleVariant& var);
// A querydef's parameter, set from a value the library made itself.
void AFXAPI _AfxDaoSetParam(CDaoQueryDef* pQueryDef, LPCTSTR lpszName, const VARIANT& value);

void AFXAPI _AfxDaoFillFieldInfo(DAOField* p, CDaoFieldInfo& info, DWORD dwOptions);
void AFXAPI _AfxDaoFillIndexInfo(DAOIndex* p, CDaoIndexInfo& info, DWORD dwOptions);

#endif  // __DAOIMPL_H__
