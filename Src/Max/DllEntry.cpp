// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"
#include "Common.h"
#include <SimplygonInit.h>
#include <SimplygonMax.h>

extern SimplygonInitClass* SimplygonInitInstance;
extern SimplygonMax* SimplygonMaxInstance;

HINSTANCE hInstance;

TCHAR* GetString( int resourceId )
{
	static TCHAR tBuffer[ MAX_PATH ] = { 0 };

	if( hInstance )
		return LoadString( hInstance, resourceId, tBuffer, _countof( tBuffer ) ) ? tBuffer : nullptr;

	return nullptr;
}

BOOL WINAPI DllMain( HINSTANCE hinstDLL, ULONG fdwReason, LPVOID lpvReserved )
{
	if( fdwReason == DLL_PROCESS_ATTACH )
	{
		hInstance = hinstDLL;
		DisableThreadLibraryCalls( hInstance );
	}
	return ( TRUE );
}

__declspec( dllexport ) int LibNumberClasses()
{
	return 1;
}

__declspec( dllexport ) const TCHAR* LibDescription()
{
	return GetString( IDS_LIBDESCRIPTION );
}

__declspec( dllexport ) ClassDesc* LibClassDesc( int i )
{
	switch( i )
	{
		case 0: return 0;
		default: return 0;
	}
}

// This function returns a pre-defined constant indicating the version of
// the system under which it was compiled.  It is used to allow the system
// to catch obsolete DLLs.
__declspec( dllexport ) ULONG LibVersion()
{
	return VERSION_3DSMAX;
}

#pragma region PLUGIN_SEARCHPATHS
void GetPluginDir( TCHAR* tDestinationPath )
{
	TCHAR tPath[ MAX_PATH ] = { 0 };
	TCHAR tDrive[ _MAX_DRIVE ] = { 0 };
	TCHAR tDir[ _MAX_DIR ] = { 0 };
	TCHAR tFileName[ _MAX_FNAME ] = { 0 };
	TCHAR tExtension[ _MAX_EXT ] = { 0 };

	if( !GetModuleFileName( nullptr, tPath, MAX_PATH ) )
	{
		tDestinationPath[ 0 ] = _T( '\0' );
		return;
	}

	_tsplitpath_s( tPath, tDrive, _MAX_DRIVE, tDir, _MAX_DIR, tFileName, _MAX_FNAME, tExtension, _MAX_EXT );
	_tmakepath_s( tPath, _MAX_PATH, tDrive, tDir, _T(""), _T("") );

	_stprintf_s( tDestinationPath, MAX_PATH, _T("%s\\plugins\\"), tPath );
}

#pragma endregion

// This function is called once, right after your plugin has been loaded by 3ds Max.
// Perform one-time plugin initialization in this method.
// Return TRUE if you deem your plugin successfully loaded, or FALSE otherwise. If
// the function returns FALSE, the system will NOT load the plugin, it will then call FreeLibrary
// on your DLL, and send you a message.
__declspec( dllexport ) int LibInitialize( void )
{
	SimplygonInitInstance = new SimplygonInitClass();
	SimplygonMaxInstance = new SimplygonMax();

	return TRUE;
}

// This function is called once, just before the plugin is unloaded.
// Perform one-time plugin un-initialization in this method."
// The system doesn't pay attention to a return value.
__declspec( dllexport ) int LibShutdown( void )
{
	if( SimplygonMaxInstance )
	{
		delete SimplygonMaxInstance;
		SimplygonMaxInstance = nullptr;
	}

	if( SimplygonInitInstance )
	{
		SimplygonInitInstance->DeInitialize();
		delete SimplygonInitInstance;
		SimplygonInitInstance = nullptr;
	}

	return TRUE;
}