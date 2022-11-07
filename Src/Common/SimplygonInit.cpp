// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

// #include "PCH.h"

#include "SimplygonInit.h"
#include "Shared.h"
#include <stdio.h>
#include <vector>
#include <tchar.h>
#include <string>
#include "Common.h"
#include <algorithm>

Simplygon::ISimplygon* sg;
SimplygonInitClass* SimplygonInitInstance = nullptr;
extern std::vector<std::basic_string<TCHAR>> SimplygonProcessAdditionalSearchPaths;

//No this is not an error
#define STRINGIFY( s ) #s
#define TOSTRING( s ) STRINGIFY( s )

#ifdef MAX_INTEGRATION
#include <maxversion.h>
#elif MAYA_INTEGRATION
#include <maya/MTypes.h>
#endif


using namespace Simplygon;

void AddDirectoriesToSimplygonSearchPaths()
{
	std::basic_string<TCHAR> tSimplygonEnvironmentPath = GetSimplygonEnvironmentVariable( _T( SIMPLYGON_10_PATH ) );
	if( tSimplygonEnvironmentPath.size() > 0 )
	{
		AddPluginSearchPath( tSimplygonEnvironmentPath.c_str() );
	}
}

void AddPluginSearchPath( const TCHAR* tSearchpath, bool bAppendSimplygonDirectory )
{
#ifdef UNICODE
	std::wstring wSearchPath( tSearchpath );
	std::string sSearchPath;
	sSearchPath.assign( wSearchPath.begin(), wSearchPath.end() );
	Simplygon::AddSearchPath( sSearchPath.c_str() );
	SimplygonProcessAdditionalSearchPaths.push_back( wSearchPath.c_str() );

	if( bAppendSimplygonDirectory )
	{
		wSearchPath = Combine( tSearchpath, _T("Simplygon\\") );
		sSearchPath = "";
		sSearchPath.assign( wSearchPath.begin(), wSearchPath.end() );
		Simplygon::AddSearchPath( sSearchPath.c_str() );
		SimplygonProcessAdditionalSearchPaths.push_back( wSearchPath.c_str() );
	}
#else
	const char* cSearchpath = LPCTSTRToConstCharPtr( tSearchpath );
	Simplygon::AddSearchPath( cSearchpath );
	SimplygonProcessAdditionalSearchPaths.push_back( cSearchpath );

	if( bAppendSimplygonDirectory )
	{
		std::string sSearchPath = Combine( cSearchpath, "Simplygon\\" );
		Simplygon::AddSearchPath( sSearchPath.c_str() );
		SimplygonProcessAdditionalSearchPaths.push_back( sSearchPath.c_str() );
	}
#endif
}

void SimplygonInitClass::SetRelay( SimplygonEventRelay* eventRelay )
{
	this->eventRelay = eventRelay;
}

SimplygonInitClass::SimplygonInitClass()
{
	this->isSetup = false;
	this->eventRelay = nullptr;
}

SimplygonInitClass::~SimplygonInitClass()
{
	this->isSetup = false;
	this->eventRelay = nullptr;

	if( sg != nullptr )
	{
		this->DeInitialize();
	}
}

bool SimplygonInitClass::Initialize()
{
	if( this->isSetup )
	{
		return true;
	}

	try
	{
		AddDirectoriesToSimplygonSearchPaths();
	}
	catch( std::exception ex )
	{
		std::basic_string<TCHAR> tErrorMessage = ConstCharPtrToLPCTSTR( ex.what() );

		if( this->eventRelay )
		{
			this->eventRelay->ErrorCallback( tErrorMessage.c_str() );
		}
		else
		{
			throw std::exception( LPCTSTRToConstCharPtr( tErrorMessage.c_str() ) );
		}

		return false;
	}

	// initialize
	const EErrorCodes initval = Simplygon::Initialize( &sg );
	if( initval != Simplygon::EErrorCodes::NoError )
	{
		std::basic_string<TCHAR> tErrorMessage = _T(" Simplygon Error : Simplygon Failed to initialize and returned the following error string: \n\n ");
		tErrorMessage += ConstCharPtrToLPCTSTR( Simplygon::GetError( initval ) );

		if( this->eventRelay )
		{
			eventRelay->ErrorCallback( tErrorMessage.c_str() );
		}
		else
		{
			throw std::exception( LPCTSTRToConstCharPtr( tErrorMessage.c_str() ) );
		}

		return false;
	}

// set default tangent space type
#ifdef MAX_INTEGRATION
	sg->SetGlobalDefaultTangentCalculatorTypeSetting( ETangentSpaceMethod::Autodesk3dsMax );
#elif MAYA_INTEGRATION
	sg->SetGlobalDefaultTangentCalculatorTypeSetting( ETangentSpaceMethod::MikkTSpace );
#endif

	// add error handler
	sg->SetErrorHandler( this );

	// set initialized flag
	this->isSetup = true;

	// Initiate telemetry
#ifdef MAX_INTEGRATION
	sg->SendTelemetry( "IntegrationInit", "3ds Max", TOSTRING( MAX_PRODUCT_YEAR_NUMBER ), "{}" );
#elif MAYA_INTEGRATION
	#ifdef MAYA_APP_VERSION
	sg->SendTelemetry( "IntegrationInit", "Maya", TOSTRING( MAYA_APP_VERSION ), "{}" );
	#else
	sg->SendTelemetry( "IntegrationInit", "Maya", TOSTRING( MAYA_API_VERSION ), "{}" );
	#endif
#endif
	return true;
}

void SimplygonInitClass::DeInitialize()
{
	Simplygon::Deinitialize( sg );
	sg = nullptr;
	this->isSetup = false;
}

void SimplygonInitClass::HandleError( Simplygon::spObject object, const char* cInterfaceName, const char* cMethodName, rid errortype, const char* cErrorText )
{
	std::basic_string<TCHAR> tErrorMessage = _T("");

	// error message from external batch, no formatting required
	if( errortype == 0 && !object.IsNull() )
	{
		tErrorMessage += ConstCharPtrToLPCTSTR( cErrorText );
	}

	// error message from within Maya (Simplygon or Internal Batch)
	else
	{
		TCHAR tInterfaceBuffer[ 512 ] = { 0 };
		_stprintf_s( tInterfaceBuffer, 512, _T("Interface: %s (%p)\n"), ConstCharPtrToLPCTSTR( cInterfaceName ), object._GetHandle() );

		tErrorMessage += _T( "An error occurred! Details:\n\n");
		tErrorMessage += tInterfaceBuffer;
		tErrorMessage += std::basic_string<TCHAR>( _T("Method: ") ) + ConstCharPtrToLPCTSTR( cMethodName );
		tErrorMessage += std::basic_string<TCHAR>( _T("\nError Type: ") ) + ConstCharPtrToLPCTSTR( std::to_string( errortype ).c_str() );
		tErrorMessage += std::basic_string<TCHAR>( _T("\nError Description: ") ) + ConstCharPtrToLPCTSTR( cErrorText );
		tErrorMessage += _T("\n");
	}

	std::basic_string<TCHAR>::size_type tPosition = 0;
	while( ( tPosition = tErrorMessage.find( _T("\n"), tPosition ) ) != std::basic_string<TCHAR>::npos )
	{
		tErrorMessage.replace( tPosition, 1, _T("\r\n") );
		tPosition += 2;
	}

	if( strcmp( cInterfaceName, "IPipeline" ) == 0 )
	{
		// do not call error handler
	}
	else if( this->eventRelay )
	{
		this->eventRelay->ErrorCallback( tErrorMessage.c_str() );
	}

	const char* cErrorMessage = LPCTSTRToConstCharPtr( tErrorMessage.c_str() );
	throw std::exception( cErrorMessage );
}

bool SimplygonInitClass::OnProgress( Simplygon::spObject subject, Simplygon::real progressPercentage )
{
	if( this->eventRelay )
	{
		eventRelay->ProgressCallback( (int)progressPercentage );
	}
	return true;
}
