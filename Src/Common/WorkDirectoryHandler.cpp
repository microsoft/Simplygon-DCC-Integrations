// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//#include "PCH.h"
#include "WorkDirectoryHandler.h"
#include <tchar.h>
#include <shlobj.h>
#include "Common.h"
#include <string>
#include "HelperFunctions.h"
#include <assert.h>

const TCHAR* SIMPLYGON_FOLDER = { _T("Simplygon") };
const TCHAR* SIMPLYGON_ORIGINAL_TEXTURES_FOLDER = { _T("OriginalTextures") };
const TCHAR* SIMPLYGON_BAKED_TEXTURES_FOLDER = { _T("BakedTextures") };
const TCHAR* SIMPLYGON_EXPORT_TEXTURES_FOLDER = { _T("Textures") };

WorkDirectoryHandler::WorkDirectoryHandler()
{
	std::basic_string<TCHAR> tGuidFolder = CreateGuid();

	std::basic_string<TCHAR> tSimplygonTempEnvironmentPath = _T("");

	// try to fetch the SIMPLYGON_10_TEMP path
	try
	{
		tSimplygonTempEnvironmentPath = GetSimplygonEnvironmentVariable( _T( SIMPLYGON_10_TEMP ) );
	}
	catch( const std::exception& )
	{
		// ignore error, use fallback below if not set
	}

	// use SIMPLYGON_10_TEMP if set, otherwise fall back to AppData/Local/Simplygon
	std::basic_string<TCHAR> tSimplygonTempFolder = tSimplygonTempEnvironmentPath.length() > 0 ? tSimplygonTempEnvironmentPath : GetSimplygonAppDataPath();

	// add a guid-directory to make every WorkDirectoryHandler unique
	std::basic_string<TCHAR> tRequestedWorkDirectory = Combine( tSimplygonTempFolder, tGuidFolder );

	// make sure it is unique!
	while( FileExists( tRequestedWorkDirectory ) )
	{
		tRequestedWorkDirectory = Combine( tSimplygonTempFolder, CreateGuid() );
	}

	// Create main directory
	bool bCreated = CreateFolder( tRequestedWorkDirectory );
	assert( bCreated );

	this->WorkDirectory = tRequestedWorkDirectory;
	this->ImportWorkDirectory = _T("");
	this->ExportWorkDirectory = _T("");

	// Create sub directories
	bCreated = CreateFolder( GetOriginalTexturesPath() );
	assert( bCreated );

	bCreated = CreateFolder( GetBakedTexturesPath() );
	assert( bCreated );
}

WorkDirectoryHandler::~WorkDirectoryHandler()
{
	this->RecursiveDelete( this->WorkDirectory );
}

std::basic_string<TCHAR> WorkDirectoryHandler::GetSimplygonAppDataPath()
{
	TCHAR tAppDataPath[ MAX_PATH ];

	if( SHGetFolderPath( nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, tAppDataPath ) != 0 )
	{
		return std::basic_string<TCHAR>( _T("") );
	}

	return Combine( std::basic_string<TCHAR>( tAppDataPath ), SIMPLYGON_FOLDER );
}

std::basic_string<TCHAR> WorkDirectoryHandler::GetWorkDirectory()
{
	return this->WorkDirectory;
}

std::basic_string<TCHAR> WorkDirectoryHandler::GetOriginalTexturesPath()
{
	return Combine( this->WorkDirectory, SIMPLYGON_ORIGINAL_TEXTURES_FOLDER );
}
std::basic_string<TCHAR> WorkDirectoryHandler::GetBakedTexturesPath()
{
	return Combine( this->WorkDirectory, SIMPLYGON_BAKED_TEXTURES_FOLDER );
}

void WorkDirectoryHandler::SetTextureOutputDirectoryOverride( std::basic_string<TCHAR> tOutputDirectory )
{
	this->OutputTextureDirectoryOverride = CorrectPath( tOutputDirectory );
}

std::basic_string<TCHAR> WorkDirectoryHandler::GetImportWorkDirectory()
{
	return this->ImportWorkDirectory;
}

std::basic_string<TCHAR> WorkDirectoryHandler::GetExportWorkDirectory()
{
	return this->ExportWorkDirectory;
}

std::basic_string<TCHAR> WorkDirectoryHandler::GetExportTexturesPath()
{
	return Combine( this->ExportWorkDirectory, SIMPLYGON_EXPORT_TEXTURES_FOLDER );
}

void WorkDirectoryHandler::SetImportWorkDirectory( std::basic_string<TCHAR> tImportDirectory )
{
	this->ImportWorkDirectory = CorrectPath( tImportDirectory );
}

void WorkDirectoryHandler::SetExportWorkDirectory( std::basic_string<TCHAR> tExportDirectory )
{
	this->ExportWorkDirectory = CorrectPath( tExportDirectory );
}

void WorkDirectoryHandler::RecursiveDelete( std::basic_string<TCHAR> tRootpath )
{
	const size_t pathLength = tRootpath.length();

	std::basic_string<TCHAR> tLastChar = tRootpath.substr( pathLength - 1, pathLength );

	if( tLastChar != _T("\\") && tLastChar != _T("/") )
	{
		tRootpath += _T("\\");
	}

	std::basic_string<TCHAR> tFilterString = tRootpath + _T("*");

	WIN32_FIND_DATA w32FindData;

	HANDLE hFileHandle = FindFirstFile( tFilterString.c_str(), &w32FindData );
	if( hFileHandle == INVALID_HANDLE_VALUE )
	{
		return;
	}

	do
	{
		std::basic_string<TCHAR> tToDelete = Combine( tRootpath, w32FindData.cFileName );
		if( std::basic_string<TCHAR>( w32FindData.cFileName ) == _T(".") || std::basic_string<TCHAR>( w32FindData.cFileName ) == _T("..") )
			continue;

		if( w32FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
		{
			RecursiveDelete( tToDelete );
		}
		else
		{
			DeleteFile( tToDelete.c_str() );
		}
	} while( FindNextFile( hFileHandle, &w32FindData ) != 0 );

	const BOOL bIsClosed = FindClose( hFileHandle );
	std::basic_string<TCHAR> tWin32FriendlyDirectory = tRootpath.substr( 0, pathLength );
	::RemoveDirectory( tWin32FriendlyDirectory.c_str() );
}

std::basic_string<TCHAR> WorkDirectoryHandler::GetTextureOutputDirectoryOverride()
{
	return this->OutputTextureDirectoryOverride;
}