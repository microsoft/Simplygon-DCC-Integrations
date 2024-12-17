// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "Common.h"
#include <knownfolders.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <iptypes.h>
#include <iphlpapi.h>
#include <iosfwd>
#include <WTypes.h>
#include <istream>
#include <string>
#include <iostream>
#include <fstream>
#include <string>
#include "Shared.h"
#include <map>

bool FileExists( const TCHAR* tFilePath )
{
	const DWORD dwAttributes = ::GetFileAttributes( tFilePath );
	if( dwAttributes == INVALID_FILE_ATTRIBUTES )
	{
		return false;
	}
	return true;
}

bool FileExists( std::basic_string<TCHAR> tFilePath )
{
	return FileExists( tFilePath.c_str() );
}

bool IsProcessRunning( std::basic_string<TCHAR> tProcessName )
{
	unsigned long aProcesses[ 1024 ] = { 0 }, cbNeeded = 0, cProcesses = 0;
	if( !EnumProcesses( aProcesses, sizeof( aProcesses ), &cbNeeded ) )
		return false;

	cProcesses = cbNeeded / sizeof( unsigned long );
	for( unsigned long i = 0; i < cProcesses; ++i )
	{
		if( aProcesses[ i ] == 0 )
			continue;

		const HANDLE hProcess = OpenProcess( PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, 0, aProcesses[ i ] );

		TCHAR tBuffer[ MAX_PATH ] = { 0 };
		GetModuleBaseName( hProcess, nullptr, tBuffer, MAX_PATH );
		CloseHandle( hProcess );

		if( tProcessName == std::basic_string<TCHAR>( tBuffer ) )
			return true;
	}
	return false;
}

int GetStringFromRegistry( const TCHAR* tKeyId, const TCHAR* tValueId, TCHAR* tResult )
{
	HKEY hKey = nullptr;
	if( RegOpenKey( HKEY_LOCAL_MACHINE, tKeyId, &hKey ) != ERROR_SUCCESS )
		return -1;

	// read the value from the key
	DWORD dwPathLength = MAX_PATH;
	if( RegQueryValueEx( hKey, tValueId, nullptr, nullptr, (unsigned char*)tResult, &dwPathLength ) != ERROR_SUCCESS )
		return -1;

	if( tResult != nullptr )
	{
		tResult[ dwPathLength ] = 0;
	}

	// close the key
	RegCloseKey( hKey );
	return 0;
}

void split_file_path( std::basic_string<TCHAR> tSourcePath,
                      std::basic_string<TCHAR>& tDirectory,
                      std::basic_string<TCHAR>& tFileName,
                      std::basic_string<TCHAR>& tFileExtension,
                      bool bExpectAbsolutePath )
{
	TCHAR tDrive[ _MAX_DRIVE ] = { 0 };
	TCHAR tDir[ _MAX_DIR ] = { 0 };
	TCHAR tFName[ _MAX_FNAME ] = { 0 };
	TCHAR tFExt[ _MAX_EXT ] = { 0 };

	_tsplitpath_s( tSourcePath.c_str(), tDrive, _MAX_DRIVE, tDir, _MAX_DIR, tFName, _MAX_FNAME, tFExt, _MAX_EXT );

	// make sure the path is absolute (including drive)
	if( bExpectAbsolutePath && _tcslen( tDrive ) == 0 )
	{
		// relative path, convert into absolute path
		std::basic_string<TCHAR> fullpath = GetFullPathOfFile( tSourcePath.c_str() );
		_tsplitpath_s( fullpath.c_str(), tDrive, _MAX_DRIVE, tDir, _MAX_DIR, tFName, _MAX_FNAME, tFExt, _MAX_EXT );
	}

	tDirectory = std::basic_string<TCHAR>( tDrive ) + tDir;
	tFileName = std::basic_string<TCHAR>( tFName );
	tFileExtension = std::basic_string<TCHAR>( tFExt );
}

std::basic_string<TCHAR> GetDirectoryOfFile( const TCHAR* tSourcePath, bool bExpectAbsolutePath )
{
	std::basic_string<TCHAR> sdir;
	std::basic_string<TCHAR> sfname;
	std::basic_string<TCHAR> sext;
	split_file_path( std::basic_string<TCHAR>( tSourcePath ), sdir, sfname, sext, bExpectAbsolutePath );
	return sdir;
}

bool StartBatchProcess( const TCHAR* tFilePath, bool bShowBatchWindow )
{
	PROCESS_INFORMATION piProcInfo;
	STARTUPINFO siStartInfo;

	// Set up members of the PROCESS_INFORMATION structure.
	ZeroMemory( &piProcInfo, sizeof( PROCESS_INFORMATION ) );

	// Set up members of the STARTUPINFO structure.
	ZeroMemory( &siStartInfo, sizeof( STARTUPINFO ) );
	siStartInfo.cb = sizeof( STARTUPINFO );
	siStartInfo.hStdError = INVALID_HANDLE_VALUE;
	siStartInfo.hStdInput = INVALID_HANDLE_VALUE;
	siStartInfo.hStdOutput = INVALID_HANDLE_VALUE;
	siStartInfo.dwFlags = bShowBatchWindow == true ? STARTF_USESHOWWINDOW : 0;
	siStartInfo.wShowWindow = bShowBatchWindow == true ? SW_SHOW : SW_HIDE;

	// Create the child process.
	BOOL bProcessCreated = CreateProcess( tFilePath,                                       // exe file path
	                                      nullptr,                                         // command line
	                                      nullptr,                                         // process security attributes
	                                      nullptr,                                         // primary thread security attributes
	                                      FALSE,                                           // handles are inherited
	                                      bShowBatchWindow == true ? 0 : DETACHED_PROCESS, // creation flags
	                                      nullptr,                                         // use parent's environment
	                                      GetDirectoryOfFile( tFilePath, true ).c_str(),   // use parent's current directory
	                                      &siStartInfo,                                    // STARTUPINFO pointer
	                                      &piProcInfo                                      // receives PROCESS_INFORMATION
	);

	if( !bProcessCreated )
	{
		return false;
	}

	// function succeeded, return handle to process, release handles we will not use anymore
	CloseHandle( piProcInfo.hProcess );
	CloseHandle( piProcInfo.hThread );

	return true;
}

std::basic_string<TCHAR> GetFullPathOfFile( const TCHAR* tRelativeSourcePath )
{
	TCHAR tFullPath[ MAX_PATH ] = { 0 };
	if( _tfullpath( tFullPath, tRelativeSourcePath, MAX_PATH ) == nullptr )
	{
		return std::basic_string<TCHAR>( _T("") );
	}
	return std::basic_string<TCHAR>( tFullPath );
}

std::basic_string<TCHAR> GetFullPathOfFile( std::basic_string<TCHAR> tRelativeSourcePath )
{
	return GetFullPathOfFile( (LPCTSTR)tRelativeSourcePath.c_str() );
}

std::basic_string<TCHAR> GetNonConflictingNameInPath( const TCHAR* tSourceDirectory, const TCHAR* tSourceFileName, const TCHAR* tSourceFileExtension )
{
	std::basic_string<TCHAR> tFileName = std::basic_string<TCHAR>( tSourceFileName ) + tSourceFileExtension;
	std::basic_string<TCHAR> tFilePath = std::basic_string<TCHAR>( tSourceDirectory ) + tFileName;

	// try until we find one
	int number = 1;
	while( FileExists( GetFullPathOfFile( tFilePath.c_str() ).c_str() ) )
	{
		TCHAR tNumString[ MAX_PATH ] = { 0 };
		_stprintf_s( tNumString, MAX_PATH, _T("_%d"), number );
		tFileName = std::basic_string<TCHAR>( tSourceFileName ) + tNumString + tSourceFileExtension;
		tFilePath = std::basic_string<TCHAR>( tSourceDirectory ) + tFileName;
		++number;
	}

	// done, return
	return tFileName;
}

std::basic_string<TCHAR> GetNonConflictingNameInPath( const TCHAR* tSourceFilepath )
{
	std::basic_string<TCHAR> tSourceDirectory = GetDirectoryOfFile( tSourceFilepath );
	return tSourceDirectory +
	       GetNonConflictingNameInPath( tSourceDirectory.c_str(), GetTitleOfFile( tSourceFilepath ).c_str(), GetExtensionOfFile( tSourceFilepath ).c_str() );
}

std::basic_string<TCHAR> GetNameOfFile( const TCHAR* tSourceFilePath, bool bExpectAbsolutePath )
{
	std::basic_string<TCHAR> tDir;
	std::basic_string<TCHAR> tFName;
	std::basic_string<TCHAR> tFExt;
	split_file_path( std::basic_string<TCHAR>( tSourceFilePath ), tDir, tFName, tFExt, bExpectAbsolutePath );
	return tFName + tFExt;
}

std::basic_string<TCHAR> GetNameOfFile( std::basic_string<TCHAR> tSourceFilePath, bool bExpectAbsolutePath )
{
	return GetNameOfFile( (LPCTSTR)tSourceFilePath.c_str(), bExpectAbsolutePath );
}

std::basic_string<TCHAR> GetTitleOfFile( const TCHAR* tSourceFilePath, bool bExpectAbsolutePath )
{
	std::basic_string<TCHAR> tDir;
	std::basic_string<TCHAR> tFName;
	std::basic_string<TCHAR> tFExt;
	split_file_path( std::basic_string<TCHAR>( tSourceFilePath ), tDir, tFName, tFExt, bExpectAbsolutePath );
	return tFName;
}

std::basic_string<TCHAR> GetTitleOfFile( std::basic_string<TCHAR> tSourceFilePath, bool bExpectAbsolutePath )
{
	return GetTitleOfFile( (LPCTSTR)tSourceFilePath.c_str(), bExpectAbsolutePath );
}

std::basic_string<TCHAR> GetExtensionOfFile( const TCHAR* tSourceFilePath, bool bExpectAbsolutePath )
{
	std::basic_string<TCHAR> tDir;
	std::basic_string<TCHAR> tFName;
	std::basic_string<TCHAR> tFExt;
	split_file_path( std::basic_string<TCHAR>( tSourceFilePath ), tDir, tFName, tFExt, bExpectAbsolutePath );
	return tFExt;
}

std::basic_string<TCHAR> GetExtensionOfFile( std::basic_string<TCHAR> tSourceFilePath, bool bExpectAbsolutePath )
{
	return GetExtensionOfFile( (LPCTSTR)tSourceFilePath.c_str(), bExpectAbsolutePath );
}

std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>> FindAllFilesAndPathsInDirectory( std::basic_string<TCHAR> tDirectory )
{
	WIN32_FIND_DATA FindFileData;

	std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>> tFileCollection;

	// find all files in dir
	std::basic_string<TCHAR> tFilterString = Combine( tDirectory, _T("*.*") );

	HANDLE hFileHandle = FindFirstFile( tFilterString.c_str(), &FindFileData );
	if( hFileHandle == INVALID_HANDLE_VALUE )
	{
		const DWORD dwError = GetLastError();
		if( dwError == ERROR_FILE_NOT_FOUND )
		{
			// ignore for now
		}
	}

	if( hFileHandle == INVALID_HANDLE_VALUE )
	{
		return tFileCollection;
	}
	else
	{
		// list all found files in vector
		BOOL bContinue = true;
		while( bContinue )
		{
			// only add files, no dirs
			if( ( FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) == 0 )
			{
				tFileCollection.insert( std::pair<std::basic_string<TCHAR>, std::basic_string<TCHAR>>( FindFileData.cFileName, tDirectory ) );
			}

			// check next
			bContinue = FindNextFile( hFileHandle, &FindFileData );
		}

		// done, close handle
		FindClose( hFileHandle );
	}
	return tFileCollection;
}

std::vector<std::basic_string<TCHAR>> FindAllFilesInDirectory( std::basic_string<TCHAR> tDirectory )
{
	WIN32_FIND_DATA FindFileData;

	std::vector<std::basic_string<TCHAR>> tFileCollection;

	// find all files in dir
	std::basic_string<TCHAR> tFilterString = Combine( tDirectory, _T("*.*") );

	HANDLE hFileHandle = FindFirstFile( tFilterString.c_str(), &FindFileData );
	if( hFileHandle == INVALID_HANDLE_VALUE )
	{
		const DWORD error = GetLastError();
		if( error == ERROR_FILE_NOT_FOUND )
		{ // ignore for now
		}
	}

	if( hFileHandle == INVALID_HANDLE_VALUE )
	{
		return tFileCollection;
	}
	else
	{
		// list all found files in vector
		BOOL bContinue = true;
		while( bContinue )
		{
			// only add files, no dirs
			if( ( FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) == 0 )
			{
				tFileCollection.push_back( std::basic_string<TCHAR>( FindFileData.cFileName ) );
			}

			// check next
			bContinue = FindNextFile( hFileHandle, &FindFileData );
		}

		// done, close handle
		FindClose( hFileHandle );
	}
	return tFileCollection;
}

std::basic_string<TCHAR> Combine( std::basic_string<TCHAR> tPath1, std::basic_string<TCHAR> tPath2 )
{
	TCHAR tBuffer[ MAX_PATH ] = { 0 };
	PathCombine( tBuffer, tPath1.c_str(), tPath2.c_str() );

	return tBuffer;
}

std::string CombineA( std::string sPath1, std::string sPath2 )
{
	char cBuffer[ MAX_PATH ] = { 0 };
	PathCombineA( cBuffer, sPath1.c_str(), sPath2.c_str() );

	return cBuffer;
}

size_t GetSizeOfFile( const std::wstring& wFilePath )
{
	struct _stat fileinfo;
	_wstat( wFilePath.c_str(), &fileinfo );
	return fileinfo.st_size;
}

std::string CorrectPathA( const std::string& sFilePath )
{
	const size_t numChars = sFilePath.length();

	if( numChars == 0 )
		return "";

	std::string sReturnString;
	sReturnString.resize( numChars );

	for( size_t i = 0; i < numChars; ++i )
	{
		const char& t = sFilePath[ i ];
		if( t == '/' )
		{
			sReturnString[ i ] = '\\';
		}
		else
		{
			sReturnString[ i ] = t;
		}
	}

	return sReturnString;
}

std::basic_string<TCHAR> CorrectPath( const std::basic_string<TCHAR>& tFilePath )
{
	const size_t numChars = tFilePath.length();

	if( numChars == 0 )
		return _T("");

	std::basic_string<TCHAR> tReturnString;
	tReturnString.resize( numChars );

	for( size_t i = 0; i < numChars; ++i )
	{
		const TCHAR& t = tFilePath[ i ];
		if( t == _T( '/' ) )
		{
			tReturnString[ i ] = _T( '\\' );
		}
		else
		{
			tReturnString[ i ] = t;
		}
	}

	return tReturnString;
}

std::basic_string<TCHAR> GetDllPath( HINSTANCE hInstance )
{
	TCHAR tPath[ MAX_PATH ] = { 0 };
	TCHAR tDrive[ _MAX_DRIVE ] = { 0 };
	TCHAR tDir[ _MAX_DIR ] = { 0 };
	TCHAR tFName[ _MAX_FNAME ] = { 0 };
	TCHAR tFExt[ _MAX_EXT ] = { 0 };

	if( GetModuleFileName( hInstance, tPath, MAX_PATH ) == 0 )
	{
		return _T("");
	}

	_tsplitpath_s( tPath, tDrive, _MAX_DRIVE, tDir, _MAX_DIR, tFName, _MAX_FNAME, tFExt, _MAX_EXT );
	_tmakepath_s( tPath, _MAX_PATH, tDrive, tDir, _T(""), _T("") );

	return tPath;
}

std::basic_string<TCHAR> GetSimplygonEnvironmentVariable( std::basic_string<TCHAR> tEnvironmentKey )
{
	const DWORD dwMaxNumChars = 1024;
	std::basic_string<TCHAR> tValueBuffer;
	tValueBuffer.resize( dwMaxNumChars, 0 );

	DWORD dwCharsRead = 0;

	try
	{
		dwCharsRead = GetEnvironmentVariable( tEnvironmentKey.c_str(), &tValueBuffer[ 0 ], dwMaxNumChars );
	}
	catch( const std::exception ex )
	{
		std::string sErrorMessage = "Could not read the Simplygon environment variable: ";
		sErrorMessage += ex.what();

		throw std::exception( sErrorMessage.c_str() );
	}

	if( dwCharsRead )
	{
		tValueBuffer.resize( dwCharsRead );
		tValueBuffer = CorrectPath( tValueBuffer );

		std::basic_string<TCHAR> tExpandedValueBuffer;
		tExpandedValueBuffer.resize( dwMaxNumChars, 0 );

		const DWORD dwCharsExpandedLength = ExpandEnvironmentStrings( tValueBuffer.c_str(), &tExpandedValueBuffer[ 0 ], dwMaxNumChars );
		if( dwCharsExpandedLength )
		{
			tExpandedValueBuffer.resize( dwCharsExpandedLength );
			tValueBuffer = tExpandedValueBuffer.c_str();
		}

		if( tValueBuffer.back() != _T( '\\' ) )
			tValueBuffer += _T( '\\' );

		return tValueBuffer;
	}
	else
	{
		std::string sErrorMessage = "The Simplygon environment variable is missing or points to an invalid location: ";
		sErrorMessage += LPCTSTRToConstCharPtr( tEnvironmentKey.c_str() );

		throw std::exception( sErrorMessage.c_str() );
	}

	return _T("");
}

bool DefaultFileCreated( std::basic_string<TCHAR> tFilePath )
{
	bool bFileCreated = false;
	if( !FileExists( tFilePath ) )
	{
		std::ofstream outFile( tFilePath, std::ios::app );
		if( outFile.is_open() )
		{
			bFileCreated = true;
		}

		outFile.close();
	}

	return bFileCreated;
}

bool DirectoryExists( std::basic_string<TCHAR> tDirectory )
{
	const DWORD dwAttributes = GetFileAttributes( tDirectory.c_str() );
	if( dwAttributes == INVALID_FILE_ATTRIBUTES )
		return false; // something is wrong with your path!

	if( dwAttributes & FILE_ATTRIBUTE_DIRECTORY )
		return true; // this is a directory!

	return false; // this is not a directory!
}

bool CreateFolder( std::basic_string<WCHAR> wDirectory )
{
	std::string sDirectory = LPCWSTRToConstCharPtr( wDirectory.c_str() );
	return CreateFolder( sDirectory );
}

bool CreateFolder( std::string sDirectory )
{
	// make sure all slashes are valid
	sDirectory = CorrectPathA( sDirectory );

	// append trailing slash, if missing
	const size_t trailingSlashPos = sDirectory.find_last_of( '\\' );
	if( sDirectory.length() > 0 && sDirectory.back() != '\\' )
	{
		sDirectory.append( 1, '\\' );
	}

	std::vector<std::string> sSubDirectories;
	while( true )
	{
		const size_t numChars = sDirectory.length();
		size_t pos = sDirectory.find_first_of( '\\' );

		if( pos == std::string::npos )
		{
			// if no backslash, try to get last part of path
			if( numChars > 0 )
			{
				pos = numChars;
			}
			// otherwise, stop here
			else
			{
				break;
			}
		}

		else if( pos == 0 )
		{
			sDirectory = sDirectory.substr( 1, numChars - 1 );
			continue;
		}

		sSubDirectories.push_back( sDirectory.substr( 0, pos ) );
		sDirectory = sDirectory.substr( pos, numChars - pos );
	}

	if( !sSubDirectories.size() )
		return true;

	std::string sCurrentPath = sSubDirectories[ 0 ];

	for( size_t p = 1; p < sSubDirectories.size(); ++p )
	{
		sCurrentPath = CombineA( sCurrentPath, sSubDirectories[ p ] );

		const bool bCreated = CreateDirectoryA( sCurrentPath.c_str(), nullptr ) == TRUE;
		if( !bCreated )
		{
			const DWORD dwError = GetLastError();

			if( dwError == ERROR_ALREADY_EXISTS )
			{
				// ignore for now
			}
			else
			{
				return false;
			}
		}
	}

	return true;
}
