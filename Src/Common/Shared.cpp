// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "Shared.h"
#include <wtypes.h>
#include <tchar.h>
#include <iosfwd>
#include <string>
#include <iosfwd>
#include <sstream>

LPCTSTR ConstCharPtrToLPCTSTR( const char* cStringToConvert )
{
#ifndef _UNICODE
	size_t newsize = strlen( cStringToConvert );
	LPTSTR tReturnString = (LPTSTR)malloc( ( newsize + 1 ) * sizeof( TCHAR ) );
	if( tReturnString != nullptr )
	{
		memcpy( tReturnString, cStringToConvert, newsize );
		tReturnString[ newsize ] = 0;
	}

	return tReturnString;
#else
	size_t newsize = strlen( cStringToConvert ) + 1;
	LPTSTR tReturnString = (LPTSTR)malloc( newsize * sizeof( TCHAR ) );
	size_t convertedChars = 0;

	mbstowcs_s( &convertedChars, tReturnString, newsize, cStringToConvert, _TRUNCATE );
	return tReturnString;
#endif // _UNICODE
}

const char* LPCTSTRToConstCharPtr( const TCHAR* tStringToConvert )
{
#ifndef _UNICODE
	size_t wlen = _tcslen( tStringToConvert );
	char* cReturnString = (char*)malloc( ( wlen + 1 ) * sizeof( char ) );
	if( cReturnString != nullptr )
	{
		memcpy( cReturnString, tStringToConvert, wlen );
		cReturnString[ wlen ] = 0;
	}

	return cReturnString;
#else
	size_t wlen = _tcslen( tStringToConvert ) + 1;
	char* cReturnString = (char*)malloc( wlen * sizeof( char ) );
	size_t convertedChars = 0;
	wcstombs_s( &convertedChars, cReturnString, wlen, tStringToConvert, _TRUNCATE );
	return cReturnString;
#endif
}

const char* LPCWSTRToConstCharPtr( const wchar_t* wStringToConvert )
{
	const size_t wlen = wcslen( wStringToConvert ) + 1;
	char* cReturnString = (char*)malloc( wlen * sizeof( char ) );
	size_t convertedChars = 0;
	wcstombs_s( &convertedChars, cReturnString, wlen, wStringToConvert, _TRUNCATE );
	return cReturnString;
}

const wchar_t* ConstCharPtrToLPCWSTRr( const char* cStringToConvert )
{
	const size_t cSize = strlen( cStringToConvert ) + 1;
	wchar_t* wReturnString = new wchar_t[ cSize ];
	mbstowcs( wReturnString, cStringToConvert, cSize );

	return wReturnString;
}

std::vector<std::basic_string<TCHAR>> stringSplit( LPCTSTR tSourceString, TCHAR tDelimiter, bool bTrim )
{
	std::basic_string<TCHAR> tTempString( tSourceString );
	std::stringstream sStringStream( LPCTSTRToConstCharPtr( tTempString.c_str() ) );
	std::vector<std::basic_string<TCHAR>> tStringCollection;

	std::string sBuffer;
	const TCHAR tBuffer[ 2 ] = { tDelimiter, 0 };

	std::string sDelimiter = LPCTSTRToConstCharPtr( tBuffer );

	while( std::getline( sStringStream, sBuffer, sDelimiter[ 0 ] ) )
	{
		if( sBuffer != "" )
		{
			std::basic_string<TCHAR> tTempBuffer = ConstCharPtrToLPCTSTR( sBuffer.c_str() );
			if( bTrim )
				tStringCollection.push_back( Trim( tTempBuffer ) );
			else
				tStringCollection.push_back( tTempBuffer );
		}
	}

	return tStringCollection;
}

std::basic_string<TCHAR>& rightTrim( std::basic_string<TCHAR>& tSourceString, const TCHAR* tTrimChars )
{
	tSourceString.erase( tSourceString.find_last_not_of( tTrimChars ) + 1 );
	return tSourceString;
}

std::basic_string<TCHAR>& leftTrim( std::basic_string<TCHAR>& tSourceString, const TCHAR* tTrimChars )
{
	tSourceString.erase( 0, tSourceString.find_first_not_of( tTrimChars ) );
	return tSourceString;
}

std::basic_string<TCHAR>& Trim( std::basic_string<TCHAR>& tSourceString, const TCHAR* tTrimChars )
{
	return leftTrim( rightTrim( tSourceString, tTrimChars ), tTrimChars );
}