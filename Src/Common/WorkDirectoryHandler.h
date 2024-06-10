// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once
#include <tchar.h>
#include <string>

class WorkDirectoryHandler
{
	public:
	WorkDirectoryHandler();
	~WorkDirectoryHandler();

	std::basic_string<TCHAR> GetWorkDirectory();
	std::basic_string<TCHAR> GetSimplygonAppDataPath();
	std::basic_string<TCHAR> GetOriginalTexturesPath();
	std::basic_string<TCHAR> GetBakedTexturesPath();
	void SetTextureOutputDirectoryOverride( std::basic_string<TCHAR> tOutputDirectory );
	std::basic_string<TCHAR> GetTextureOutputDirectoryOverride();

	std::basic_string<TCHAR> GetImportWorkDirectory();
	std::basic_string<TCHAR> GetExportWorkDirectory();
	std::basic_string<TCHAR> GetExportTexturesPath();

	void SetImportWorkDirectory( std::basic_string<TCHAR> tImportDirectory );
	void SetExportWorkDirectory( std::basic_string<TCHAR> tExportDirectory );

	private:
	std::basic_string<TCHAR> WorkDirectory;
	std::basic_string<TCHAR> OutputTextureDirectoryOverride;

	std::basic_string<TCHAR> ImportWorkDirectory;
	std::basic_string<TCHAR> ExportWorkDirectory;

	void RecursiveDelete( std::basic_string<TCHAR> rootPath );
};
