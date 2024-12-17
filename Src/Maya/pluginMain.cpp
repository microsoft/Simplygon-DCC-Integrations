// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//#include <SgCodeAnalysisSetup.h>
// SG_DISABLE_CA_WARNINGS
//
// Copyright (C) Microsoft
//
// File: pluginMain.cpp
//
// Author: Maya Plug-in Wizard 2.0
//
#include "PCH.h"

#include "SimplygonCmd.h"
#include "SimplygonPipelineCmd.h"
#include "Common.h"
#include "HelperFunctions.h"
#include "SimplygonQueryCmd.h"
#include "SimplygonNetworkCmd.h"

#include <maya/MFnPlugin.h>
#include <maya/MSceneMessage.h>
#include "SimplygonInit.h"

MString mGlobalPluginPath;

//
//	Description:
//		this method is called when the plug-in is loaded into Maya.  It
//		registers all of the services that this plug-in provides with
//		Maya.
//
//	Arguments:
//		obj - a handle to the plug-in object (use MFnPlugin to access it)
//

UIHookHelper uiHookHelper;
Globals uiGlobals;

extern SimplygonInitClass* SimplygonInitInstance;

// TODO: investigate a proper shutdown of Simplygon when Maya exits. 
// For now (and as before) just ignore deinit of Simplygon as Maya 
// does not seem to have any way to hook in callbacks before exit.

// Keep method for further investigation.
static MCallbackId mOnExitCallbackId;
void OnMayaExitCallback( void* clientData )
{
	if( sg != nullptr )
	{
		//SimplygonInitInstance->DeInitialize();
	}
}

MStatus initializePlugin( MObject mObject )
{
	MStatus mStatus = MStatus::kSuccess;

	MFnPlugin mPlugin( mObject, "Microsoft", GetHeaderVersion(), "Any" );
	mGlobalPluginPath = mPlugin.loadPath();

	SimplygonInitInstance = new SimplygonInitClass();

	// register the commands
	mStatus = mPlugin.registerCommand( "Simplygon", SimplygonCmd::creator, SimplygonCmd::createSyntax );
	if( !mStatus )
	{
		mStatus.perror( "registerCommand - Simplygon" );
		return mStatus;
	}

	mStatus = mPlugin.registerCommand( "SimplygonQuery", SimplygonQueryCmd::creator, SimplygonQueryCmd::createSyntax );
	if( !mStatus )
	{
		mStatus.perror( "registerCommand - SimplygonQuery" );
		return mStatus;
	}

	mStatus = mPlugin.registerCommand( "SimplygonShadingNetwork", SimplygonShadingNetworkHelperCmd::creator, SimplygonShadingNetworkHelperCmd::createSyntax );
	if( !mStatus )
	{
		mStatus.perror( "registerCommand - SimplygonShadingNetwork" );
		return mStatus;
	}

	mStatus = mPlugin.registerCommand( "SimplygonPipeline", SimplygonPipelineCmd::creator, SimplygonPipelineCmd::createSyntax );
	if( !mStatus )
	{
		mStatus.perror( "registerCommand - SimplygonPipeline" );
		return mStatus;
	}

	mOnExitCallbackId = MSceneMessage::addCallback( MSceneMessage::kMayaExiting, OnMayaExitCallback, nullptr, &mStatus );

	return mStatus;
}

//
//	Description:
//		this method is called when the plug-in is unloaded from Maya. It
//		unregisters all of the services that it was providing.
//
//	Arguments:
//		obj - a handle to the plug-in object (use MFnPlugin to access it)
//
MStatus uninitializePlugin( MObject mObject )
{
	MStatus mStatus = MStatus::kSuccess;
	MFnPlugin mPlugin( mObject );

	mStatus = mPlugin.deregisterCommand( "Simplygon" );
	if( !mStatus )
	{
		mStatus.perror( "deregisterCommand" );
		return mStatus;
	}

	mStatus = mPlugin.deregisterCommand( "SimplygonQuery" );
	if( !mStatus )
	{
		mStatus.perror( "deregisterCommand" );
		return mStatus;
	}

	mStatus = mPlugin.deregisterCommand( "SimplygonShadingNetwork" );
	if( !mStatus )
	{
		mStatus.perror( "deregisterCommand" );
		return mStatus;
	}

	mStatus = mPlugin.deregisterCommand( "SimplygonPipeline" );
	if( !mStatus )
	{
		mStatus.perror( "deregisterCommand" );
		return mStatus;
	}

	if( sg != nullptr )
	{
		SimplygonInitInstance->DeInitialize();
	}

	mStatus = MSceneMessage::removeCallback( mOnExitCallbackId );

	return mStatus;
}

// SG_ENABLE_CA_WARNINGS
