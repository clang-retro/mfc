// dbdaoerr.h - DAO's error codes, as HRESULTs in FACILITY_CONTROL.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_DBDAOERR_H
#define _WINPORT_DBDAOERR_H

#include <windows.h>

#define MAKE_DAO_SCODE(err) MAKE_SCODE(SEVERITY_ERROR, FACILITY_CONTROL, err)
#define E_DAO_InternalError MAKE_DAO_SCODE(3000)
#define E_DAO_InvalidParameter MAKE_DAO_SCODE(3001)
#define E_DAO_CantBegin MAKE_DAO_SCODE(3002)
#define E_DAO_TransTooDeep MAKE_DAO_SCODE(3003)
#define E_DAO_FileNotFound MAKE_DAO_SCODE(3024)
#define E_DAO_NoCurrentRecord MAKE_DAO_SCODE(3021)
#define E_DAO_DuplicateKey MAKE_DAO_SCODE(3022)
#define E_DAO_ObjectNotFound MAKE_DAO_SCODE(3011)
#define E_DAO_ItemNotFound MAKE_DAO_SCODE(3265)
#define E_DAO_UpdateNoCurrentRec MAKE_DAO_SCODE(3020)

#endif
