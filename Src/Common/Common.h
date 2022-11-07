// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once
#include <tchar.h>
#include <string>
#include <windows.h>
#include <psapi.h>
#include <WTypes.h>
#include <memory>
#include <string>
#include <vector>
#include "Shared.h"
#include <map>

#ifndef SIMPLYGON_10_PATH
#define SIMPLYGON_10_PATH "SIMPLYGON_10_PATH"
#endif

#ifndef SIMPLYGON_10_TEMP
#define SIMPLYGON_10_TEMP "SIMPLYGON_10_TEMP"
#endif

typedef unsigned int uint;

std::vector<std::basic_string<TCHAR>> FindAllFilesInDirectory( std::basic_string<TCHAR> tDirectory );
std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>> FindAllFilesAndPathsInDirectory( std::basic_string<TCHAR> tDirectory );

std::basic_string<TCHAR> GetDllPath( HINSTANCE hInstance );

std::basic_string<TCHAR> GetSimplygonEnvironmentVariable( std::basic_string<TCHAR> tEnvironmentKey );

std::basic_string<TCHAR> Combine( std::basic_string<TCHAR> tPath1, std::basic_string<TCHAR> tPath2 );
std::string CombineA( std::string sPath1, std::string sPath2 );

bool FileExists( const TCHAR* tFilePath );
bool FileExists( std::basic_string<TCHAR> tFilePath );

bool DirectoryExists( std::basic_string<TCHAR> tDirectory );
bool CreateFolder( std::string sDirectory );
bool CreateFolder( std::basic_string<WCHAR> wDirectory );

bool IsProcessRunning( std::basic_string<TCHAR> tProcessName );
bool StartBatchProcess( const TCHAR* tProcessPath, bool bShowBatchWindow );

int GetStringFromRegistry( const TCHAR* tKeyId, const TCHAR* tValueId, TCHAR* tResult );

void split_file_path( std::basic_string<TCHAR> tSourcePath,
                      std::basic_string<TCHAR>& tDirectory,
                      std::basic_string<TCHAR>& tFileName,
                      std::basic_string<TCHAR>& tFileExtension,
                      bool bExpectAbsolutePath = true );

std::basic_string<TCHAR> GetDirectoryOfFile( const TCHAR* tSourcePath, bool bExpectAbsolutePath = true );
std::basic_string<TCHAR> GetFullPathOfFile( const TCHAR* tRelativeSourcePath );

std::basic_string<TCHAR> GetNonConflictingNameInPath( const TCHAR* tSourceDirectory, const TCHAR* tSourceFileName, const TCHAR* tSourceFileExtension );
std::basic_string<TCHAR> GetNonConflictingNameInPath( const TCHAR* tSourcePath );

std::basic_string<TCHAR> GetNameOfFile( const TCHAR* tSourceFilePath, bool bExpectAbsolutePath );
std::basic_string<TCHAR> GetTitleOfFile( const TCHAR* tSourceFilePath, bool bExpectAbsolutePath );
std::basic_string<TCHAR> GetExtensionOfFile( const TCHAR* tSourceFilePath, bool bExpectAbsolutePath );

std::basic_string<TCHAR> GetFullPathOfFile( std::basic_string<TCHAR> tRelativeFilePath );
std::basic_string<TCHAR> GetNameOfFile( std::basic_string<TCHAR> tSourceFilePath, bool bExpectAbsolutePath = true );
std::basic_string<TCHAR> GetTitleOfFile( std::basic_string<TCHAR> tSourceFilePath, bool bExpectAbsolutePath = true );
std::basic_string<TCHAR> GetExtensionOfFile( std::basic_string<TCHAR> tSourceFilePath, bool bExpectAbsolutePath = true );

std::basic_string<TCHAR> CorrectPath( const std::basic_string<TCHAR>& tFilePath );