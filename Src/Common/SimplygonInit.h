// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include "SimplygonLoader.h"

void AddPluginSearchPath( const TCHAR* cPath, bool appendSimplygonDirectory = true );

class SimplygonEventRelay
{
	public:
	virtual void ProgressCallback( int progress ) = 0;
	virtual void ErrorCallback( const TCHAR* cErrorMessage ) = 0;
};

class SimplygonInitClass
    : public Simplygon::Observer
    , public Simplygon::ErrorHandler
{
	private:
	bool isSetup;
	SimplygonEventRelay* eventRelay;

	public:
	bool OnProgress( Simplygon::spObject subject, Simplygon::real progressPercentage ) override;
	void
	HandleError( Simplygon::spObject object, const char* cInterfaceName, const char* cMethodName, Simplygon::rid errortype, const char* cErrorText ) override;

	void SetRelay( SimplygonEventRelay* eventRelay );
	SimplygonInitClass();
	virtual ~SimplygonInitClass();

	bool Initialize();
	void DeInitialize();
};