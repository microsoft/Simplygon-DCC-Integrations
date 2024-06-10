// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "HelperFunctions.h"
#include <algorithm>
#include <iptypes.h>
#include <iphlpapi.h>
#include <winnls.h>
#include "Common.h"
#include <objbase.h>
#include "SgCodeAnalysisSetup.h"

std::basic_string<TCHAR> ToLower( std::basic_string<TCHAR> tString )
{
	std::basic_string<TCHAR> tStringLower = tString;
	
#if _UNICODE
	std::transform( tStringLower.begin(), tStringLower.end(), tStringLower.begin(), ( WCHAR( * )( WCHAR ) )::towlower );

#else
	std::transform( tStringLower.begin(), tStringLower.end(), tStringLower.begin(), ( char ( * )( char ) )::tolower );

#endif

	return tStringLower;
}

std::string ToUpper( std::string string )
{
	std::string stringUpper = string;

	std::transform( stringUpper.begin(), stringUpper.end(), stringUpper.begin(), ( char ( * )( char ) )::toupper );

	return stringUpper;
}

bool CompareStrings( std::basic_string<TCHAR> tString1, std::basic_string<TCHAR> tString2 )
{
	std::basic_string<TCHAR> tString1Lower = ToLower( tString1 );
	std::basic_string<TCHAR> tString2Lower = ToLower( tString2 );

	if( tString1Lower == tString2Lower )
	{
		return true;
	}
	return false;
}

bool IsSubstringPartOfString( std::basic_string<TCHAR> tSourceString, std::basic_string<TCHAR> tPartString )
{
	std::basic_string<TCHAR> tSourceStringLower = ToLower( tSourceString );
	std::basic_string<TCHAR> tPartStringLower = ToLower( tPartString );

	if( (int)tSourceStringLower.find( tPartStringLower ) >= 0 )
	{
		return true;
	}
	return false;
}

std::basic_string<TCHAR> TrimSpaces( std::basic_string<TCHAR> tSourceString )
{
	const size_t startpos = tSourceString.find_first_not_of( _T(" \t") );
	const size_t endpos = tSourceString.find_last_not_of( _T(" \t") );

	if( ( std::basic_string<TCHAR>::npos == startpos ) || ( std::basic_string<TCHAR>::npos == endpos ) )
	{
		return std::basic_string<TCHAR>( _T("") );
	}
	else
	{
		return tSourceString.substr( startpos, endpos - startpos + 1 );
	}
}

int GetSettingsStringIndex( std::vector<std::basic_string<TCHAR>>& tSettingsStrings, const TCHAR* tName, std::basic_string<TCHAR>* tResult = nullptr )
{
	std::basic_string<TCHAR> tNameString( tName );
	for( int i = 0; i < (int)tSettingsStrings.size(); ++i )
	{
		std::basic_string<TCHAR> tCurrentSettingsString = tSettingsStrings[ i ];

		// look for the '=' sign
		const size_t pos = tCurrentSettingsString.find( _T( '=' ) );
		if( pos != std::basic_string<TCHAR>::npos )
		{
			std::basic_string<TCHAR> tPath = TrimSpaces( tCurrentSettingsString.substr( 0, pos ) );
			std::basic_string<TCHAR> tValue = TrimSpaces( tCurrentSettingsString.substr( pos + 1 ) );

			if( tPath == tNameString )
			{
				// this is our setting
				if( tResult != nullptr )
				{
					( *tResult ) = tValue;
				}
				return i;
			}
		}
	}
	return -1;
}

std::basic_string<TCHAR> CreateGuid()
{
	GUID guid;
	const HRESULT hResult = CoCreateGuid( &( guid ) );
	if( hResult == S_OK )
	{
		char cGuid[ 37 ];
		sprintf( cGuid,
		         "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		         guid.Data1,
		         guid.Data2,
		         guid.Data3,
		         guid.Data4[ 0 ],
		         guid.Data4[ 1 ],
		         guid.Data4[ 2 ],
		         guid.Data4[ 3 ],
		         guid.Data4[ 4 ],
		         guid.Data4[ 5 ],
		         guid.Data4[ 6 ],
		         guid.Data4[ 7 ] );

		return ConstCharPtrToLPCTSTR( cGuid );
	}

	return EmptyGuid();
}

std::basic_string<TCHAR> EmptyGuid()
{
	return _T("00000000-0000-0000-0000-000000000000");
}

bool GuidCompare( std::string sString1, std::string sString2 )
{
	if( sString1.length() == 0 || sString2.length() == 0 )
		return false;

	if( sString1.length() != sString2.length() )
		return false;

	sString1 = ToUpper( sString1 );
	sString2 = ToUpper( sString2 );

	return sString1 == sString2;
}

void RemoveInvalidCharacters( std::basic_string<TCHAR>& tSourceString )
{
	const TCHAR tFilter[] = { _T( ' ' ), _T( '-' ), _T( '/' ), _T( '\\' ), _T( ':' ), _T( '?' ), _T( '<' ), _T( '>' ), _T( '|' ) };
	constexpr uint numFilteredChars = _countof( tFilter );

	for( uint character = 0; character < numFilteredChars; ++character )
	{
		tSourceString.erase( std::remove( tSourceString.begin(), tSourceString.end(), tFilter[ character ] ) );
	}
}

std::basic_string<TCHAR> RemoveInvalidCharacters( const std::basic_string<TCHAR>& tSourceString )
{
	const TCHAR tFilter[] = { _T( ' ' ), _T( '-' ), _T( '/' ), _T( '\\' ), _T( ':' ), _T( '?' ), _T( '<' ), _T( '>' ), _T( '|' ) };
	constexpr uint numFilteredChars = _countof( tFilter );

	std::basic_string<TCHAR> tFinalString = tSourceString;
	for( uint character = 0; character < numFilteredChars; ++character )
	{
		tFinalString.erase(std::remove( tFinalString.begin(), tFinalString.end(), tFilter[ character ] ));
	}

	return tFinalString;
}

void ReplaceInvalidCharacters( std::basic_string<TCHAR>& tSourceString, TCHAR tNewChar )
{
	const TCHAR tFilter[] = { _T( ' ' ), _T( '-' ), _T( '/' ), _T( '\\' ), _T( ':' ), _T( '?' ), _T( '<' ), _T( '>' ), _T( '|' ) };
	constexpr uint numFilteredChars = _countof( tFilter );

	for( uint character = 0; character < numFilteredChars; ++character )
	{
		std::replace( tSourceString.begin(), tSourceString.end(), tFilter[ character ], tNewChar );
	}
}

bool CharacterFilter( TCHAR c )
{
	static std::basic_string<TCHAR> tFilter( _T("\\/:?\"<>|") );

	return std::basic_string<TCHAR>::npos != tFilter.find( &c );
}

std::basic_string<wchar_t> AppendInt( const std::basic_string<wchar_t>& str, int value )
{
	return str + std::to_wstring( value );
}

std::basic_string<char> AppendInt( const std::basic_string<char>& str, int value )
{
	return str + std::to_string( value );
}

bool ExportTextureToFile( Simplygon::ISimplygon* sg, Simplygon::spTexture sgTexture, const char* exportFilePath )
{
	auto exporter = sg->CreateImageDataExporter();

	Simplygon::EImageOutputFormat exportFormat = Simplygon::EImageOutputFormat::PNG;
	switch( sgTexture->GetImageData()->GetInputFormat() )
	{
		case Simplygon::EImageInputFormat::BMP: exportFormat = Simplygon::EImageOutputFormat::BMP;
		case Simplygon::EImageInputFormat::DDS: exportFormat = Simplygon::EImageOutputFormat::DDS;
		case Simplygon::EImageInputFormat::JPEG: exportFormat = Simplygon::EImageOutputFormat::JPEG;
		case Simplygon::EImageInputFormat::TGA: exportFormat = Simplygon::EImageOutputFormat::TGA;
		case Simplygon::EImageInputFormat::TIFF: exportFormat = Simplygon::EImageOutputFormat::TIFF;
		case Simplygon::EImageInputFormat::EXR: exportFormat = Simplygon::EImageOutputFormat::EXR;
	}

	exporter->SetImage( sgTexture->GetImageData() );
	exporter->SetExportFilePath( exportFilePath );
	exporter->SetImageFileFormat( exportFormat );
	exporter->SetDDSCompressionType( sgTexture->GetImageData()->GetDDSCompressionType() );

	if( exporter->RunExport() )
	{
		sgTexture->SetFilePath( exporter->GetExportFilePath() );
		return true;
	}
	return false;
}