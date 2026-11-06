// mtcore.cpp - run-time class information for the afxmt.h classes.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxmt.h>

IMPLEMENT_DYNAMIC(CSyncObject, CObject)
IMPLEMENT_DYNAMIC(CSemaphore, CSyncObject)
IMPLEMENT_DYNAMIC(CMutex, CSyncObject)
IMPLEMENT_DYNAMIC(CEvent, CSyncObject)
IMPLEMENT_DYNAMIC(CCriticalSection, CSyncObject)
