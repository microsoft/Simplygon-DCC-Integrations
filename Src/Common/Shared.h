// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _SHARED_H_
#define _SHARED_H_

#include <wtypes.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <stdio.h>

// string conversion
LPCTSTR ConstCharPtrToLPCTSTR( const char* cStringToConvert );
const char* LPCTSTRToConstCharPtr( const TCHAR* tStringToConvert );
const char* LPCWSTRToConstCharPtr( const wchar_t* wStringToConvert );
const wchar_t* ConstCharPtrToLPCWSTRr( const char* cStringToConvert );

// string split / trim
std::vector<std::basic_string<TCHAR>> stringSplit( LPCTSTR tSourceString, TCHAR tDelimiter, bool bTrim = false );
std::basic_string<TCHAR>& rightTrim( std::basic_string<TCHAR>& tSourceString, const TCHAR* tTrimChars = _T(" \t\n\r\f\v") );
std::basic_string<TCHAR>& leftTrim( std::basic_string<TCHAR>& tSourceString, const TCHAR* tTrimChars = _T(" \t\n\r\f\v") );
std::basic_string<TCHAR>& Trim( std::basic_string<TCHAR>& tSourceString, const TCHAR* tTrimChars = _T(" \t\n\r\f\v") );
#endif
