// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"
#include "SimplygonCmd.h"
#include "WorkDirectoryHandler.h"

#include "Scene.h"
#include "MeshNode.h"
#include "MaterialNode.h"
#include "HelperFunctions.h"
#include "Common.h"
#include "SimplygonProcessingModule.h"
#include "SimplygonInit.h"
#include "DataCollection.h"
#include <string>
#include "SimplygonPipelineCmd.h"
#include "PipelineHelper.h"

extern HINSTANCE MhInstPlugin;
extern Globals uiGlobals;
extern UIHookHelper uiHookHelper;

std::map<std::string, std::string> SimplygonCmd::s_GlobalMaterialDagPathToGuid;
std::map<std::string, std::string> SimplygonCmd::s_GlobalMaterialGuidToDagPath;

std::map<std::string, std::string> SimplygonCmd::s_GlobalMeshDagPathToGuid;
std::map<std::string, std::string> SimplygonCmd::s_GlobalMeshGuidToDagPath;

MaterialInfoHandler* SimplygonCmd::materialInfoHandler = nullptr;
DataCollection* DataCollection::instance = nullptr;

extern SimplygonInitClass* SimplygonInitInstance;

SimplygonCmd::SimplygonCmd()
{
	InitializeCriticalSection( &this->cs );

	SimplygonInitInstance->SetRelay( this );

	this->useQuadExportImport = false;

	this->extractionType = BATCH_PROCESSOR;
	this->inputSceneFile = "";
	this->outputSceneFile = "";

	this->meshFormatString = "{MeshName}";
	this->initialLodIndex = 1;

	this->blendshapeFormatString = "{Name}_LOD{LODIndex}";

	this->OutputTextureDirectory = "";

	this->sceneHandler = new Scene();
	this->workDirectoryHandler = new WorkDirectoryHandler();
	this->materialHandler = new MaterialHandler( this );

	this->sgPipeline = Simplygon::NullPtr;

	this->listSettings = false;
	this->creaseValues = false;
	this->skipBlendShapePostfix = false;
	this->useCurrentPoseAsBindPose = true;
	this->useOldSkinningMethod = false;

	this->runInternally = false;
	this->runSimplygonGrid = false;
	this->runIncredibuild = false;
	this->runFastbuild = false;

	this->hasProgressWindow = false;
	this->showBatchWindow = false;

	this->noMaterialMerging = false;
	this->doNotGenerateMaterial = false;

	this->mapMaterials = true;
	this->mapMeshes = true;

	this->copyTextures = true;

	this->clearGlobalMapping = true;

	DataCollection::GetInstance()->SetSceneHandler( this->sceneHandler );
	DataCollection::GetInstance()->SetMaterialHandler( this->materialHandler );
}

SimplygonCmd::~SimplygonCmd()
{
	// delete scene handler
	if( this->sceneHandler )
	{
		delete this->sceneHandler;
		this->sceneHandler = nullptr;
	}

	// delete work-directory handler
	if( this->workDirectoryHandler )
	{
		delete this->workDirectoryHandler;
		this->workDirectoryHandler = nullptr;
	}

	// delete material handler
	if( this->materialHandler )
	{
		delete this->materialHandler;
		this->materialHandler = nullptr;
	}

	this->sgPipeline = Simplygon::NullPtr;

	if( SimplygonInitInstance )
		SimplygonInitInstance->SetRelay( nullptr );

	DeleteCriticalSection( &cs );
}

void SimplygonCmd::Cleanup()
{
	// delete and reallocate scene handler
	if( this->sceneHandler )
	{
		delete this->sceneHandler;
	}

	this->sceneHandler = new Scene();

	// delete and reallocate work-directory handler
	if( this->workDirectoryHandler )
	{
		delete this->workDirectoryHandler;
	}

	this->workDirectoryHandler = new WorkDirectoryHandler();

	// delete and reallocate material-info handler
	if( this->materialHandler )
	{
		delete this->materialHandler;
	}

	this->materialHandler = new MaterialHandler( this );
}

MStatus SimplygonCmd::doIt( const MArgList& mArgList )
{
	MStatus mStatus;

	// check Simplygon handle and initialize the sdk
	if( sg == nullptr )
	{
		const bool bInitialized = SimplygonInitInstance->Initialize();
		if( !bInitialized )
		{
			return MStatus::kFailure;
		}
	}

	if( sg )
	{
		mStatus = this->RunPlugin( mArgList );

		// make sure progress window exits!
		if( !mStatus )
		{
			if( this->hasProgressWindow )
			{
				this->EndProgress();
			}
		}
	}

	else
	{
		return MStatus::kFailure;
	}

	return mStatus;
}

MStatus SimplygonCmd::redoIt()
{
	MStatus mStatus = MStatus::kSuccess;

	this->BeginProgress();
	this->EndProgress();

	this->BeginProgress();
	mStatus = this->ImportScenes();

	MGlobal::clearSelectionList();
	MGlobal::setActiveSelectionList( this->InitialSelectionList );

	this->EndProgress();

	this->Cleanup();

	return mStatus;
}

MStatus SimplygonCmd::undoIt()
{
	MStatus mStatus = MStatus::kSuccess;

	// remove the previously added LOD meshes
	this->BeginProgress();
	mStatus = this->RemoveLODMeshes();
	this->EndProgress();

	MGlobal::setActiveSelectionList( this->InitialSelectionList );

	return mStatus;
}

void SimplygonCmd::BeginProgress()
{
	if( !this->hasProgressWindow )
	{
		this->hasProgressWindow = MProgressWindow::reserve();
	}

	if( this->hasProgressWindow )
	{
		MProgressWindow::startProgress();
		MProgressWindow::setTitle( MString( "Simplygon" ) );
		MProgressWindow::setInterruptable( false );
		MProgressWindow::setProgressStatus( MString( "-------------------------------------------------------" ) );
		MProgressWindow::setProgressRange( 1, 100 );
		MProgressWindow::setProgress( 100 );
	}
}

void SimplygonCmd::EndProgress()
{
	if( this->hasProgressWindow )
	{
		MProgressWindow::endProgress();
		this->hasProgressWindow = false;
	}
}

void SimplygonCmd::SetCurrentProcess( const char* cMessage )
{
	if( this->hasProgressWindow )
	{
		MProgressWindow::setProgressStatus( MString( cMessage ) );
	}
}

void SimplygonCmd::SetCurrentProgressRange( int s, int e )
{
	if( this->hasProgressWindow )
	{
		MProgressWindow::setProgressRange( s, e );
		MProgressWindow::setProgress( s );
	}
}

void SimplygonCmd::SetCurrentProgress( int val )
{
	if( this->hasProgressWindow )
	{
		MProgressWindow::setProgress( val );
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//
// Command Arguments section
const char* cCreaseValuesFlag = "-cv";
const char* cLockSetVerticesFlag = "-lsv";
const char* cLockSetEdgesFlag = "-lse";
const char* cLockMaterialBoundaryVerticesFlag = "-mb";

const char* cRunInternally = "-ri";
const char* cRunSimplygonGrid = "-rsg";
const char* cRunIncredibuild = "-rib";
const char* cRunFastbuild = "-rfb";

const char* cMaterialColorOverride = "-mc";                      // 4.0
const char* cMaterialTextureChannelOverride = "-mtc";            // 4.1
const char* cMaterialTextureChannelNameOverride = "-tcn";        // 8.0
const char* cMaterialLayeredTextureChannelOverride = "-mlc";     // 5.2
const char* cMaterialLayeredTextureChannelNameOverride = "-lcn"; // 8.0
const char* cMaterialTextureOverride = "-mt";                    // 4.1
const char* cMaterialLayeredTextureOverride = "-mlt";            // 4.4
const char* cMaterialTextureAmbientOverride = "-mta";            // 4.0
const char* cMaterialTextureDiffuseOverride = "-mtd";            // 4.0
const char* cMaterialTextureSpecularOverride = "-mts";           // 4.0
const char* cMaterialTextureNormalsOverride = "-mtn";            // 4.0
const char* cTextureShapeUVLinkageOverride = "-tuv";             // 4.1

const char* cImportShaderXML = "-ixf";
const char* cAddShader = "-asm";
const char* cTextureOutputDirectory = "-tod";      // 6.1+
const char* cSkipBlendShapeWeightPostfix = "-swp"; // 6.2+
const char* cUseCurrentPoseAsBindPose = "-cpb";    // 6.2+ and 7.0
const char* cShowBatchWindow = "-sbw";             // 8.0
const char* cDoNotGenerateMaterial = "-dgm";       // 7.0+
const char* cUseOldSkinningMethod = "-osm";        // 8.3

const char* cSettingsObject = "-so"; // 8.0
const char* cSettingsFile = "-sf";   // 7.0

const char* cExportToFile = "-exp";   // 9.0
const char* cImportFromFile = "-imp"; // 9.0
const char* cLinkMaterials = "-lma";  // 9.0
const char* cLinkMeshes = "-lme";     // 9.0
const char* cCopyTextures = "-cte";   // 9.0

const char* cAutomaticallyClearGlobalMapping = "-acl"; // 9.0
const char* cClearGlobalMapping = "-cgm";              // 9.0

const char* cInputSceneFile = "-isf";  // 9.0
const char* cOutputSceneFile = "-osf"; // 9.0

const char* cMeshNameFormat = "-mnf";  // 9.0
const char* cInitialLodIndex = "-ili"; // 9.0

const char* cBlendShapeNameFormat = "-bnf"; // 9.0+

const char* cQuadMode = "-qm"; // 10.0

MStatus SimplygonCmd::ParseArguments( const MArgList& mArgs )
{
	MStatus mStatus = MStatus::kSuccess;
	MArgDatabase mArgData( syntax(), mArgs );

	// basic rules
	const bool bSettingsFile = mArgData.isFlagSet( cSettingsFile );
	const bool bSettingsObject = mArgData.isFlagSet( cSettingsObject );
	const bool bExportToFile = mArgData.isFlagSet( cExportToFile );
	const bool bImportFromFile = mArgData.isFlagSet( cImportFromFile );
	const bool bInputSceneFile = mArgData.isFlagSet( cInputSceneFile );
	const bool bOutputSceneFile = mArgData.isFlagSet( cOutputSceneFile );
	const bool bCopyTextures = mArgData.isFlagSet( cCopyTextures );
	const bool bLinkMaterials = mArgData.isFlagSet( cLinkMaterials );
	const bool bLinkMeshes = mArgData.isFlagSet( cLinkMeshes );

	// quad mode needs to happen before any scene import / export
	this->useQuadExportImport = mArgData.isFlagSet( cQuadMode );

	if( ( bSettingsFile || bSettingsObject ) && ( bImportFromFile || bExportToFile ) )
	{
		std::string sErrorMessage = "ParseArguments - Flags ";
		sErrorMessage += std::string( cSettingsFile ) + " and ";
		sErrorMessage += std::string( cSettingsObject );
		sErrorMessage += " are not compatible with the following flags: ";
		sErrorMessage += std::string( cExportToFile ) + ", ";
		sErrorMessage += std::string( cImportFromFile ) + ".";
		MGlobal::displayError( sErrorMessage.c_str() );
		return MStatus::kInvalidParameter;
	}

	else if( ( bInputSceneFile || bOutputSceneFile ) && ( bImportFromFile || bExportToFile ) )
	{
		std::string sErrorMessage = "ParseArguments - Flags ";
		sErrorMessage += std::string( cInputSceneFile ) + " and ";
		sErrorMessage += std::string( cOutputSceneFile );
		sErrorMessage += " are not compatible with the following flags: ";
		sErrorMessage += std::string( cExportToFile ) + ", ";
		sErrorMessage += std::string( cImportFromFile ) + ".";
		MGlobal::displayError( sErrorMessage.c_str() );
		return MStatus::kInvalidParameter;
	}

	else if( ( !bImportFromFile && !bExportToFile ) && ( bCopyTextures || bLinkMaterials || bLinkMeshes ) )
	{
		std::string sErrorMessage = "ParseArguments - Flags ";
		sErrorMessage += std::string( cLinkMaterials ) + " and ";
		sErrorMessage += std::string( cLinkMeshes );
		sErrorMessage += " are only compatible with the following flag: ";
		sErrorMessage += std::string( cImportFromFile ) + ". Flag ";
		sErrorMessage += std::string( cCopyTextures ) + " is only compatible with ";
		sErrorMessage += std::string( cImportFromFile ) + " and ";
		sErrorMessage += std::string( cExportToFile ) + ".";
		MGlobal::displayError( sErrorMessage.c_str() );
		return MStatus::kInvalidParameter;
	}

	// output mesh format
	if( mArgData.isFlagSet( cMeshNameFormat ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cMeshNameFormat );
		if( flagCount > 0 )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cMeshNameFormat, 0, mArgList );
			if( !mStatus )
				return mStatus;

			MString mFormatString = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			this->meshFormatString = mFormatString;
		}
	}

	// lod index for output mesh format
	if( mArgData.isFlagSet( cInitialLodIndex ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cInitialLodIndex );
		if( flagCount > 0 )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cInitialLodIndex, 0, mArgList );
			if( !mStatus )
				return mStatus;

			const int lodIndex = mArgList.asInt( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			this->initialLodIndex = lodIndex;
		}
	}

	//  output blendshape format
	if( mArgData.isFlagSet( cBlendShapeNameFormat ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cBlendShapeNameFormat );
		if( flagCount > 0 )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cBlendShapeNameFormat, 0, mArgList );
			if( !mStatus )
				return mStatus;

			MString mFormatString = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			this->blendshapeFormatString = mFormatString;
		}
	}

	// global mapping flag
	if( mArgData.isFlagSet( cAutomaticallyClearGlobalMapping ) )
	{
		if( mArgData.numberOfFlagUses( cAutomaticallyClearGlobalMapping ) > 0 )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cAutomaticallyClearGlobalMapping, 0, mArgList );
			if( !mStatus )
				return mStatus;

			const bool bClearMapping = mArgList.asBool( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			this->clearGlobalMapping = bClearMapping;
		}
	}

	// process scene from file
	if( mArgData.isFlagSet( cInputSceneFile ) )
	{
		this->extractionType = PROCESS_FROM_FILE;

		// fetch output path
		MString mOutputSceneFile = "";
		if( mArgData.isFlagSet( cOutputSceneFile ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cOutputSceneFile );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cOutputSceneFile, i, mArgList );
				if( !mStatus )
					return mStatus;

				this->outputSceneFile = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;
			}
		}
		else
		{
			std::string sErrorMessage = "ParseArguments::InputScene - Flag ";
			sErrorMessage += std::string( cOutputSceneFile );
			sErrorMessage += " has to be used in combination with ";
			sErrorMessage += std::string( cInputSceneFile ) + ".";
			MGlobal::displayError( sErrorMessage.c_str() );
			return MStatus::kInvalidParameter;
		}

		// fetch input path
		MString mImputSceneFile = "";
		const uint flagCount = mArgData.numberOfFlagUses( cInputSceneFile );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cInputSceneFile, i, mArgList );
			if( !mStatus )
				return mStatus;

			this->inputSceneFile = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;
		}
	}

	// export scene to file
	if( mArgData.isFlagSet( cExportToFile ) )
	{
		this->extractionType = EXPORT_TO_FILE;

		// check for link meshes/materials flag
		if( mArgData.numberOfFlagUses( cCopyTextures ) > 0 )
			this->copyTextures = mArgData.isFlagSet( cCopyTextures );
		else
			this->copyTextures = true;

		if( mArgData.numberOfFlagUses( cLinkMaterials ) > 0 )
		{
			std::string sErrorMessage = "ParseArguments::ExportToFile - Flag ";
			sErrorMessage += std::string( cLinkMaterials );
			sErrorMessage += " is not compatible with ";
			sErrorMessage += std::string( cExportToFile ) + ".";
			MGlobal::displayError( sErrorMessage.c_str() );
			return MStatus::kInvalidParameter;
		}

		else if( mArgData.numberOfFlagUses( cLinkMeshes ) > 0 )
		{
			std::string sErrorMessage = "ParseArguments::ExportToFile - Flag ";
			sErrorMessage += std::string( cLinkMeshes );
			sErrorMessage += " is not compatible with ";
			sErrorMessage += std::string( cExportToFile ) + ".";
			MGlobal::displayError( sErrorMessage.c_str() );
			return MStatus::kInvalidParameter;
		}

		const uint flagCount = mArgData.numberOfFlagUses( cExportToFile );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cExportToFile, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mExportPath = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			std::string sTargetRootDirectory = GetDirectoryOfFile( mExportPath.asChar() );
			this->workDirectoryHandler->SetExportWorkDirectory( sTargetRootDirectory );

			std::basic_string<TCHAR> tErrorMessage = _T("ParseArguments::ExportToFile - Could not export the scene");
			try
			{
				mStatus = this->ExportToFile( mExportPath.asChar() );
			}
			catch( const std::exception& ex )
			{
				mStatus = MStatus::kFailure;
				tErrorMessage += std::basic_string<TCHAR>( " - " ) + ex.what();
			}

			if( !mStatus )
			{
				MGlobal::displayError( tErrorMessage.c_str() );
				return mStatus;
			}
		}
	}

	// import scene from file
	if( mArgData.isFlagSet( cImportFromFile ) )
	{
		this->extractionType = IMPORT_FROM_FILE;

		// check for link meshes/materials flag
		if( mArgData.numberOfFlagUses( cLinkMaterials ) > 0 )
			this->mapMaterials = mArgData.isFlagSet( cLinkMaterials );
		else
			this->mapMaterials = false;

		if( mArgData.numberOfFlagUses( cLinkMeshes ) > 0 )
			this->mapMeshes = mArgData.isFlagSet( cLinkMeshes );
		else
			this->mapMeshes = false;

		if( mArgData.numberOfFlagUses( cCopyTextures ) > 0 )
			this->copyTextures = mArgData.isFlagSet( cCopyTextures );
		else
			this->copyTextures = false;

		// try to import file
		const uint flagCount = mArgData.numberOfFlagUses( cImportFromFile );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cImportFromFile, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mImportPath = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			std::string sTargetRootDirectory = GetDirectoryOfFile( mImportPath.asChar() );
			this->workDirectoryHandler->SetImportWorkDirectory( sTargetRootDirectory );

			std::basic_string<TCHAR> tErrorMessage = _T("ParseArguments::ImportFromFile - Could not import the scene");
			try
			{
				mStatus = this->ImportFromFile( mImportPath.asChar() );
			}
			catch( const std::exception& ex )
			{
				mStatus = MStatus::kFailure;
				tErrorMessage += std::basic_string<TCHAR>( " - " ) + ex.what();
			}

			if( mStatus != MStatus::kSuccess )
			{
				tErrorMessage += std::basic_string<TCHAR>( " - " );
				tErrorMessage += mImportPath.asChar();
				MGlobal::displayError( tErrorMessage.c_str() );
				return MStatus::kFailure;
			}
		}
	}

	// settings file
	if( mArgData.isFlagSet( cSettingsFile ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cSettingsFile );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cSettingsFile, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mSettingsPath = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			INT64 pipelineId = 0;
			std::vector<std::string> sErrorMessages;
			std::vector<std::string> sWarningMessages;
			try
			{
				pipelineId = PipelineHelper::Instance()->LoadSettingsPipeline( mSettingsPath.asChar(), sErrorMessages, sWarningMessages );
			}
			catch( const PipelineHelper::NullPipelineException& )
			{
				// if a nullPipelineException has been caught sErrorMessages will have a minimum of 1 entry
			}

			// Write errors and warnings to log.
			if( sErrorMessages.size() > 0 )
			{
				mStatus = MStatus::kFailure;
				for( const auto& sError : sErrorMessages )
				{
					this->LogErrorToWindow( sError );
				}
			}
			if( sWarningMessages.size() > 0 )
			{
				for( const auto& sWarning : sWarningMessages )
				{
					this->LogWarningToWindow( sWarning );
				}
			}

			if( mStatus != MStatus::kSuccess )
			{
				return mStatus;
			}

			const bool bPipelineSet = this->UseSettingsPipelineForProcessing( pipelineId );
			if( !bPipelineSet )
			{
				std::string tErrorMessage = std::basic_string<TCHAR>( " Could not assign the given pipeline id" );
				MGlobal::displayError( tErrorMessage.c_str() );
				return MStatus::kInvalidParameter;
			}
		}
	}

	// settings object
	if( mArgData.isFlagSet( cSettingsObject ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cSettingsObject );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cSettingsObject, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mPipelineId = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			const INT64 pipelineId = mPipelineId.asInt();

			std::basic_string<TCHAR> tErrorMessage = _T("ParseArguments::SettingsObject - ");

			const bool bPipelineSet = this->UseSettingsPipelineForProcessing( pipelineId );
			if( !bPipelineSet )
			{
				tErrorMessage += std::basic_string<TCHAR>( " Could not assign the given pipeline id" );
				MGlobal::displayError( tErrorMessage.c_str() );
				return MStatus::kInvalidParameter;
			}
		}
	}

	// texture directory override
	if( mArgData.isFlagSet( cTextureOutputDirectory ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cTextureOutputDirectory );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cTextureOutputDirectory, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mTextureDirectory = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			this->OutputTextureDirectory = mTextureDirectory.asChar();
			this->GetWorkDirectoryHandler()->SetTextureOutputDirectoryOverride( mTextureDirectory.asChar() );
		}
	}

	// retrieve all material color overrides
	if( mArgData.isFlagSet( cMaterialColorOverride ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cMaterialColorOverride );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cMaterialColorOverride, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMaterialName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mChannelName = mArgList.asString( 1, &mStatus );
			if( !mStatus )
				return mStatus;

			const float r = (float)mArgList.asDouble( 2, &mStatus );
			const float g = (float)mArgList.asDouble( 3, &mStatus );
			const float b = (float)mArgList.asDouble( 4, &mStatus );
			const float a = (float)mArgList.asDouble( 5, &mStatus );

			this->materialHandler->AddMaterialColorOverride( mMaterialName, mChannelName.asChar(), r, g, b, a );
		}
	}

	// retrieve all material texture overrides
	if( mArgData.isFlagSet( cMaterialTextureOverride ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cMaterialTextureOverride );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cMaterialTextureOverride, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMaterialName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mChannelName = mArgList.asString( 1, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mTextureName = mArgList.asString( 2, &mStatus );
			if( !mStatus )
				return mStatus;

			const int layer = 0;
			const int blendType = -1;

			this->materialHandler->AddMaterialTextureOverride( mMaterialName, mChannelName, mTextureName, layer, blendType );
		}
	}

	// retrieve all material texture overrides
	if( mArgData.isFlagSet( cMaterialLayeredTextureOverride ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cMaterialLayeredTextureOverride );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cMaterialLayeredTextureOverride, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMaterialName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mChannelName = mArgList.asString( 1, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mTextureName = mArgList.asString( 2, &mStatus );
			if( !mStatus )
				return mStatus;

			const int layer = mArgList.asInt( 3, &mStatus );
			if( !mStatus )
				return mStatus;

			int blendType = -1;
			MString mBlendType = mArgList.asString( 4, &mStatus ).toLowerCase();

			if( mBlendType == "add" )
				blendType = MaterialNode::MAYA_BLEND_ADD;
			else if( mBlendType == "subtract" )
				blendType = MaterialNode::MAYA_BLEND_SUBTRACT;
			else if( mBlendType == "none" )
				blendType = MaterialNode::MAYA_BLEND_NONE;
			else if( mBlendType == "multiply" )
				blendType = MaterialNode::MAYA_BLEND_MULTIPLY;
			else if( mBlendType == "over" )
				blendType = MaterialNode::MAYA_BLEND_OVER;
			else
			{
				MGlobal::displayError( MString( "An -mlt got an invalid blend type: " ) + mBlendType );
				return MStatus::kFailure;
			}

			if( !mStatus )
				return mStatus;

			this->materialHandler->AddMaterialTextureOverride( mMaterialName, mChannelName, mTextureName, layer, blendType );
		}
	}

	// retrieve all material texture overrides
	if( mArgData.isFlagSet( cMaterialTextureAmbientOverride ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cMaterialTextureAmbientOverride );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cMaterialTextureAmbientOverride, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMaterialName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mTextureName = mArgList.asString( 1, &mStatus );
			if( !mStatus )
				return mStatus;

			MGlobal::displayWarning( "Using -mta override, which is a deprecated function, use -mlt \"ambient\" instead" );

			this->materialHandler->AddMaterialTextureOverride( mMaterialName, "ambient", mTextureName );
		}
	}

	if( mArgData.isFlagSet( cMaterialTextureDiffuseOverride ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cMaterialTextureDiffuseOverride );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cMaterialTextureDiffuseOverride, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMaterialName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mTextureName = mArgList.asString( 1, &mStatus );
			if( !mStatus )
				return mStatus;

			MGlobal::displayWarning( "Using -mtd override, which is a deprecated function, use -mlt \"diffuse\" instead" );

			this->materialHandler->AddMaterialTextureOverride( mMaterialName, "diffuse", mTextureName );
		}
	}

	if( mArgData.isFlagSet( cMaterialTextureSpecularOverride ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cMaterialTextureSpecularOverride );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cMaterialTextureSpecularOverride, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMaterialName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mTextureName = mArgList.asString( 1, &mStatus );
			if( !mStatus )
				return mStatus;

			MGlobal::displayWarning( "Using -mts override, which is a deprecated function, use -mlt \"specular\" instead" );

			this->materialHandler->AddMaterialTextureOverride( mMaterialName, "specular", mTextureName );
		}
	}

	if( mArgData.isFlagSet( cMaterialTextureNormalsOverride ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cMaterialTextureNormalsOverride );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cMaterialTextureNormalsOverride, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMaterialName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mTextureName = mArgList.asString( 1, &mStatus );
			if( !mStatus )
				return mStatus;

			const bool isTangentSpace = mArgList.asBool( 2, &mStatus );
			if( !mStatus )
				return mStatus;

			this->materialHandler->AddMaterialTextureOverride( mMaterialName, "normals", mTextureName, 0, 0, isTangentSpace );
		}
	}

	// retrieve all texture shape uv linkage overrides
	if( mArgData.isFlagSet( cTextureShapeUVLinkageOverride ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cTextureShapeUVLinkageOverride );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cTextureShapeUVLinkageOverride, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mNode = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mUVSet = mArgList.asString( 1, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mTextureName = mArgList.asString( 2, &mStatus );
			if( !mStatus )
				return mStatus;

			this->materialHandler->AddTextureShapeUVLinkageOverride( mNode, mUVSet, mTextureName );
		}
	}

	// retrieve all vertex lock sets
	if( mArgData.isFlagSet( cLockSetVerticesFlag ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cLockSetVerticesFlag );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cLockSetVerticesFlag, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mSetName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			this->vertexLockSets.push_back( mSetName );
		}
	}

	// retrieve all edge lock sets
	if( mArgData.isFlagSet( cLockSetEdgesFlag ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cLockSetEdgesFlag );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cLockSetEdgesFlag, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mSetName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			this->vertexLockSets.push_back( mSetName );
		}
	}

	// retrieve all vertex lock materials
	if( mArgData.isFlagSet( cLockMaterialBoundaryVerticesFlag ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cLockMaterialBoundaryVerticesFlag );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cLockMaterialBoundaryVerticesFlag, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMaterialName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			this->vertexLockMaterials.push_back( mMaterialName );
		}
	}

	// retrieve all material texture channel overrides
	if( mArgData.isFlagSet( cMaterialTextureChannelOverride ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cMaterialTextureChannelOverride );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cMaterialTextureChannelOverride, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMaterialName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mChannelName = mArgList.asString( 1, &mStatus );
			if( !mStatus )
				return mStatus;

			const int channel = mArgList.asInt( 2, &mStatus );
			if( !mStatus )
				return mStatus;

			this->materialHandler->AddMaterialTextureChannelOverride( mMaterialName, mChannelName, 0, channel );
		}
	}

	// retrieve all layered texture channel overrides
	if( mArgData.isFlagSet( cMaterialLayeredTextureChannelOverride ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cMaterialLayeredTextureChannelOverride );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cMaterialLayeredTextureChannelOverride, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMaterialName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mChannelName = mArgList.asString( 1, &mStatus );
			if( !mStatus )
				return mStatus;

			const int layer = mArgList.asInt( 2, &mStatus );
			if( !mStatus )
				return mStatus;

			const int channel = mArgList.asInt( 3, &mStatus );
			if( !mStatus )
				return mStatus;

			this->materialHandler->AddMaterialTextureChannelOverride( mMaterialName, mChannelName, layer, channel );
		}
	}

	// retrieve all material texture channel overrides
	if( mArgData.isFlagSet( cMaterialTextureChannelNameOverride ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cMaterialTextureChannelNameOverride );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cMaterialTextureChannelNameOverride, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMaterialName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mChannelName = mArgList.asString( 1, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mChannel = mArgList.asString( 2, &mStatus );
			if( !mStatus )
				return mStatus;

			this->materialHandler->AddMaterialTextureNamedChannelOverride( mMaterialName, mChannelName, 0, mChannel );
		}
	}

	// retrieve all layered texture channel overrides
	if( mArgData.isFlagSet( cMaterialLayeredTextureChannelNameOverride ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cMaterialLayeredTextureChannelNameOverride );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cMaterialLayeredTextureChannelNameOverride, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMaterialName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mChannelName = mArgList.asString( 1, &mStatus );
			if( !mStatus )
				return mStatus;

			const int layer = mArgList.asInt( 2, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mChannel = mArgList.asString( 3, &mStatus );
			if( !mStatus )
				return mStatus;

			this->materialHandler->AddMaterialTextureNamedChannelOverride( mMaterialName, mChannelName, layer, mChannel );
		}
	}

	// shader xml related code
	if( mArgData.isFlagSet( cAddShader ) )
	{
		MArgList mArgList;
		mStatus = mArgData.getFlagArgumentList( cAddShader, 0, mArgList );

		if( !mStatus )
			return mStatus;

		MString mMaterialNames = mArgList.asString( 0, &mStatus );
		if( !mStatus )
			return mStatus;

		std::vector<std::basic_string<TCHAR>> tMaterialNames = stringSplit( mMaterialNames.asChar(), _T( '|' ) );

		for( uint i = 0; i < tMaterialNames.size(); ++i )
		{
			DataCollection::GetInstance()->GetMaterialHandler()->AddMaterialWithShadingNetworks( std::basic_string<TCHAR>( tMaterialNames[ i ].c_str() ) );
		}
	}

	// if we are getting a value, do it
	if( mArgData.isFlagSet( cImportShaderXML ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cImportShaderXML );

		for( uint i = 0; i < flagCount; i++ )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cImportShaderXML, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMaterialName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mChannelName = mArgList.asString( 1, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mXML = mArgList.asString( 2, &mStatus );
			if( !mStatus )
				return mStatus;

			spMaterial sgMaterial = sg->CreateMaterial();
			sgMaterial->SetName( mMaterialName.asChar() );

			std::basic_string<TCHAR> tMaterialName( mMaterialName.asChar() );
			std::basic_string<TCHAR> tChannelName( mChannelName.asChar() );
			std::basic_string<TCHAR> tXML( mXML.asChar() );

			FILE* fp = nullptr;

#if _MSC_VER >= 1400
			if( _tfopen_s( &fp, tXML.c_str(), _T("rb") ) != 0 )
			{
				std::string sErrorMessage = "ParseArguments - ImportShaderXML (-ixf) failed due to an invalid (unopenable) input file:\n";
				sErrorMessage += std::string( "Material name: " ) + mMaterialName.asChar() + "\n";
				sErrorMessage += std::string( "Channel name: " ) + mChannelName.asChar() + "\n";
				sErrorMessage += std::string( "XML path: " ) + mXML.asChar() + ".";
				MGlobal::displayError( sErrorMessage.c_str() );
				return MStatus::kNotFound;
			}
#else
			fp = _tfopen( tXML.c_str(), _T("rb") );
			if( fp == nullptr )
			{
				std::string sErrorMessage = "ParseArguments - ImportShaderXML (-ixf) failed due to an invalid (unopenable) input file:\n";
				sErrorMessage += std::string( "Material name: " ) + mMaterialName.asChar() + "\n";
				sErrorMessage += std::string( "Channel name: " ) + mChannelName.asChar() + "\n";
				sErrorMessage += std::string( "XML path: " ) + mXML.asChar() + ".";
				MGlobal::displayError( sErrorMessage.c_str() );
				return MStatus::kNotFound;
			}
#endif

			fseek( fp, 0, SEEK_END );
			long len = ftell( fp );
			if( len < 0 )
			{
				fclose( fp );

				std::string sErrorMessage = "ParseArguments - ImportShaderXML (-ixf) failed due to an invalid (unseekable) input file:\n";
				sErrorMessage += std::string( "Material name: " ) + mMaterialName.asChar() + "\n";
				sErrorMessage += std::string( "Channel name: " ) + mChannelName.asChar() + "\n";
				sErrorMessage += std::string( "XML path: " ) + mXML.asChar() + ".";
				MGlobal::displayError( sErrorMessage.c_str() );
				return MStatus::kFailure;
			}

			fseek( fp, 0, SEEK_SET );

			char* cFileContent = (char*)new char[ (size_t)len + 1 ];
			if( fread( cFileContent, 1, len, fp ) != len )
			{
				fclose( fp );
				delete[] cFileContent;

				std::string sErrorMessage =
				    "ParseArguments - ImportShaderXML (-ixf) failed due to an error (unreadable file content) when reading an input file:\n";
				sErrorMessage += std::string( "Material name: " ) + mMaterialName.asChar() + "\n";
				sErrorMessage += std::string( "Channel name: " ) + mChannelName.asChar() + "\n";
				sErrorMessage += std::string( "XML path: " ) + mXML.asChar() + ".";
				MGlobal::displayError( sErrorMessage.c_str() );
				return MStatus::kFailure;
			}

			cFileContent[ len ] = '\0';
			fclose( fp );

			DataCollection::GetInstance()->GetMaterialHandler()->SetupMaterialChannelNetworkFromXML(
			    tMaterialName, tChannelName, std::basic_string<TCHAR>( cFileContent ) );
		}
	}

	if( mArgData.isFlagSet( cClearGlobalMapping ) )
	{
		this->extractionType = NONE;
		this->ClearGlobalMapping();
	}

	// flags and their boolean values
	bool* flagValues[] = { &this->creaseValues,
	                       &this->skipBlendShapePostfix,
	                       &this->useCurrentPoseAsBindPose,
	                       &this->doNotGenerateMaterial,
	                       &this->showBatchWindow,
	                       &this->useOldSkinningMethod,
	                       &this->runInternally,
	                       &this->runSimplygonGrid,
	                       &this->runIncredibuild,
	                       &this->runFastbuild,
	                       nullptr };

	const char* cFlagNames[] = { cCreaseValuesFlag,
	                             cSkipBlendShapeWeightPostfix,
	                             cUseCurrentPoseAsBindPose,
	                             cDoNotGenerateMaterial,
	                             cShowBatchWindow,
	                             cUseOldSkinningMethod,
	                             cRunInternally,
	                             cRunSimplygonGrid,
	                             cRunIncredibuild,
	                             cRunFastbuild,
	                             nullptr };

	for( int index = 0; cFlagNames[ index ] != nullptr; ++index )
	{
		( *( flagValues[ index ] ) ) = mArgData.isFlagSet( cFlagNames[ index ] );
	}

	// Maya 2024 has a bug where dagPose command on models with 2 or more skinclusters
	// force currentbindpose
#if MAYA_APP_VERSION == 2024
	*( flagValues[ 2 ] ) = true;
#endif
	return mStatus;
}

void* SimplygonCmd::creator()
{
	return new SimplygonCmd();
}

MSyntax SimplygonCmd::createSyntax()
{
	MStatus mStatus;
	MSyntax mSyntax;

	mStatus = mSyntax.addFlag(
	    cMaterialColorOverride, "-MaterialColor", MSyntax::kString, MSyntax::kString, MSyntax::kDouble, MSyntax::kDouble, MSyntax::kDouble, MSyntax::kDouble );
	mStatus = mSyntax.makeFlagMultiUse( cMaterialColorOverride );
	mStatus = mSyntax.addFlag( cMaterialTextureOverride, "-MaterialTexture", MSyntax::kString, MSyntax::kString, MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cMaterialTextureOverride );
	mStatus = mSyntax.addFlag(
	    cMaterialLayeredTextureOverride, "-MaterialLayeredTexture", MSyntax::kString, MSyntax::kString, MSyntax::kString, MSyntax::kLong, MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cMaterialLayeredTextureOverride );
	mStatus = mSyntax.addFlag( cMaterialTextureAmbientOverride, "-MaterialTextureAmbient", MSyntax::kString, MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cMaterialTextureAmbientOverride );
	mStatus = mSyntax.addFlag( cMaterialTextureDiffuseOverride, "-MaterialTextureDiffuse", MSyntax::kString, MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cMaterialTextureDiffuseOverride );
	mStatus = mSyntax.addFlag( cMaterialTextureSpecularOverride, "-MaterialTextureSpecular", MSyntax::kString, MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cMaterialTextureSpecularOverride );
	mStatus = mSyntax.addFlag( cMaterialTextureNormalsOverride, "-MaterialTextureNormals", MSyntax::kString, MSyntax::kString, MSyntax::kBoolean );
	mStatus = mSyntax.makeFlagMultiUse( cMaterialTextureNormalsOverride );

	mStatus = mSyntax.addFlag( cTextureShapeUVLinkageOverride, "-TextureShapeUVLinkage", MSyntax::kString, MSyntax::kString, MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cTextureShapeUVLinkageOverride );

	mStatus = mSyntax.addFlag( cMaterialTextureChannelOverride, "-MaterialTextureChannel", MSyntax::kString, MSyntax::kString, MSyntax::kLong );
	mStatus = mSyntax.makeFlagMultiUse( cMaterialTextureChannelOverride );

	mStatus = mSyntax.addFlag(
	    cMaterialLayeredTextureChannelOverride, "-MaterialLayeredTextureChannel", MSyntax::kString, MSyntax::kString, MSyntax::kLong, MSyntax::kLong );
	mStatus = mSyntax.makeFlagMultiUse( cMaterialLayeredTextureChannelOverride );

	mStatus = mSyntax.addFlag( cMaterialTextureChannelNameOverride, "-MaterialTextureChannelName", MSyntax::kString, MSyntax::kString, MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cMaterialTextureChannelNameOverride );

	mStatus = mSyntax.addFlag( cMaterialLayeredTextureChannelNameOverride,
	                           "-MaterialLayeredTextureChannelName",
	                           MSyntax::kString,
	                           MSyntax::kString,
	                           MSyntax::kLong,
	                           MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cMaterialLayeredTextureChannelNameOverride );

	mStatus = mSyntax.addFlag( cLockSetVerticesFlag, "-LockSetVertices", MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cLockSetVerticesFlag );

	mStatus = mSyntax.addFlag( cLockSetEdgesFlag, "-LockSetEdges", MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cLockSetEdgesFlag );

	mStatus = mSyntax.addFlag( cLockMaterialBoundaryVerticesFlag, "-LockMaterialBoundaryVertices", MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cLockMaterialBoundaryVerticesFlag );

	mStatus = mSyntax.addFlag( cTextureOutputDirectory, "-TextureOutputDirectory", MSyntax::kString );

	mStatus = mSyntax.addFlag( cSkipBlendShapeWeightPostfix, "-SkipBlendShapeWeightPostfix" );

	mStatus = mSyntax.addFlag( cUseCurrentPoseAsBindPose, "-UseCurrentPoseAsBindPose" );
	mStatus = mSyntax.addFlag( cUseOldSkinningMethod, "-UseOldSkinningMethod" );

	mStatus = mSyntax.addFlag( cDoNotGenerateMaterial, "-DoNotGenerateMaterial" );

	mStatus = mSyntax.addFlag( cCreaseValuesFlag, "-CreaseValues" );

	mStatus = mSyntax.addFlag( cRunInternally, "-RunInternally" );
	mStatus = mSyntax.addFlag( cRunSimplygonGrid, "-RunSimplygonGrid" );
	mStatus = mSyntax.addFlag( cRunIncredibuild, "-RunIncredibuild" );
	mStatus = mSyntax.addFlag( cRunFastbuild, "-RunFastbuild" );

	mStatus = mSyntax.addFlag( cImportShaderXML, "-ImportShaderXML", MSyntax::kString, MSyntax::kString, MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cImportShaderXML );

	mStatus = mSyntax.addFlag( cAddShader, "-AddShaderMaterial", MSyntax::kString );

	mStatus = mSyntax.addFlag( cSettingsFile, "-SettingsFile", MSyntax::kString );

	mStatus = mSyntax.addFlag( cSettingsObject, "-SettingsObject", MSyntax::kString );
	mStatus = mSyntax.addFlag( cSettingsObject, "-SettingsObject", MSyntax::kLong );

	mStatus = mSyntax.addFlag( cShowBatchWindow, "-ShowBatchWindow" );

	mStatus = mSyntax.addFlag( cExportToFile, "-ExportToFile", MSyntax::kString );
	mStatus = mSyntax.addFlag( cImportFromFile, "-ImportFromFile", MSyntax::kString );

	mStatus = mSyntax.addFlag( cLinkMeshes, "-LinkMeshes" );
	mStatus = mSyntax.addFlag( cLinkMaterials, "-LinkMaterials" );
	mStatus = mSyntax.addFlag( cCopyTextures, "-CopyTextures" );

	mStatus = mSyntax.addFlag( cAutomaticallyClearGlobalMapping, "-AutomaticallyClearGlobalMapping", MSyntax::kBoolean );
	mStatus = mSyntax.addFlag( cClearGlobalMapping, "-ClearGlobalMapping" );

	mStatus = mSyntax.addFlag( cInputSceneFile, "-InputSceneFile", MSyntax::kString );
	mStatus = mSyntax.addFlag( cOutputSceneFile, "-ExportSceneFile", MSyntax::kString );

	mStatus = mSyntax.addFlag( cMeshNameFormat, "-MeshNameFormat", MSyntax::kString );

	mStatus = mSyntax.addFlag( cInitialLodIndex, "-InitialLodIndex", MSyntax::kLong );
	mStatus = mSyntax.addFlag( cInitialLodIndex, "-InitialLODIndex", MSyntax::kLong );

	mStatus = mSyntax.addFlag( cBlendShapeNameFormat, "-BlendShapeNameFormat", MSyntax::kString );

	mStatus = mSyntax.addFlag( cQuadMode, "-QuadMode" );

	return mSyntax;
}

bool SimplygonCmd::isUndoable() const
{
	return true;
}

MStatus SimplygonCmd::RegisterGlobalScripts()
{
	static char* cSimplygonScriptFunctions =
	    "proc string[] GetLink(string $shape_name)											\n"
	    "{																					\n"
	    "int $src_uv_count = size($shape_name + \".uvSet\");								\n"
	    "int $textureIndex = 0;																\n"
	    "string $returnArray[];																\n"
	    "																					\n"
	    //		"print(\"Num uvs = \" + $src_uv_count + \"\\n\");									\n"
	    "for ($i = 0; $i < $src_uv_count; $i++)												\n"
	    "	{																				\n"
	    "	string $src_attribute_name = $shape_name + \".uvSet[\" + $i + \"].uvSetName\";	\n"
	    "																					\n"
	    "	string $uvset = `getAttr($src_attribute_name)`;									\n"
	    "																					\n"
	    "	if ($uvset == \"\")																\n"
	    "		{																			\n"
	    "		continue;																	\n"
	    "		}																			\n"
	    "																					\n"
	    //		"	print(\" Found uv-linking from \" + $uvset + \"\\n\");							\n"
	    "	string $textures[] = `uvLink - query - uvSet $src_attribute_name`;				\n"
	    "																					\n"
	    //		"	print(\"  Num textures = \" + size($textures) + \"\\n\");						\n"
	    "	for ($j = 0; $j < size($textures); $j++)										\n"
	    "		{																			\n"
	    "		string $textureToConnect = $textures[$j];									\n"
	    //		"		print(\"   Texture: \" +$textureToConnect + \"\\n\");						\n"
	    "		$returnArray[$textureIndex] = ($uvset + \"<>\" + $textureToConnect);		\n"
	    "		$textureIndex++;															\n"
	    "		}																			\n"
	    "	}																				\n"
	    "																					\n"
	    "return $returnArray;																\n"
	    "}																					\n"
	    "proc CopyLink(string $shape_name_0, string $shape_name_1)							\n"
	    "{																					\n"
	    "int $src_uv_count = size($shape_name_0 + \".uvSet\");								\n"
	    "int $dst_uv_count = size($shape_name_1 + \".uvSet\");								\n"
	    "																					\n"
	    "if ($src_uv_count != $dst_uv_count)												\n"
	    "	return;																			\n"
	    "																					\n"
	    //		"print(\"Num uvs: \" + $src_uv_count + \"\\n\");									\n"
	    "for ($i = 0; $i < $src_uv_count; $i++)												\n"
	    "	{																				\n"
	    "	string $src_attribute_name = $shape_name_0 + \".uvSet[\" + $i + \"].uvSetName\";	\n"
	    "	string $dst_attribute_name = $shape_name_1 + \".uvSet[\" + $i + \"].uvSetName\";	\n"
	    "																					\n"
	    "	string $uvset = `getAttr($src_attribute_name)`;									\n"
	    "																					\n"
	    "	if ($uvset == \"\")																\n"
	    "		{																			\n"
	    "		continue;																	\n"
	    "		}																			\n"
	    "																					\n"
	    //		"	print(\" Copying uv-linking from \" + $uvset + \" to target. \\n\");				\n"
	    "	string $textures[] = `uvLink - query - uvSet $src_attribute_name`;				\n"
	    "																					\n"
	    "		for ($j = 0; $j < size($textures); $j++)									\n"
	    "			{																		\n"
	    "			string $textureToConnect = $textures[$j];								\n"
	    "																					\n"
	    //		"			print(\" Linking \" + $textureToConnect + \"\\n\");						\n"
	    "			uvLink - make - uvSet $dst_attribute_name - texture $textureToConnect;	\n"
	    "			}																		\n"
	    "	}																				\n"
	    "}																					\n"
	    "proc CreateLink(string $shape_name, string $uvToUse, string $textureToConnect)	 \n"
	    "{																				 \n"
	    //		"print(\"Creating link for \");														 \n"
	    //		"print($shape_name + \"\\n\");													 \n"

	    "int $uv_count = size($shape_name + \".uvSet\");								 \n"
	    "for ($i = 0; $i < $uv_count; $i++)												 \n"
	    "	{																			 \n"
	    "	string $src_attribute_name = $shape_name + \".uvSet[\" + $i + \"].uvSetName\";	 \n"
	    "	string $uvset = `getAttr($src_attribute_name)`;								 \n"
	    "	if ($uvset == $uvToUse)														 \n"
	    "		{																		 \n"
	    //		"		print(\" Linking \" + $uvset + \" to \" + $textureToConnect + \"\\n\");			 \n"
	    "		uvLink -make -uvSet $src_attribute_name -texture $textureToConnect;	 \n"
	    "		break;																	 \n"
	    "		}																		 \n"
	    "	}																			 \n"
	    "}																				 \n"
	    "proc string[] SimplygonMaya_getSGsFromSelectedObject()\n"
	    "	{\n"
	    "	string $shadingEngines[];\n"
	    "	string $shapeList[] = `listRelatives -s -path`; // get shape from object\n"
	    "   for ( $currentShape in $shapeList ) {\n"
	    "		if ( `objExists $currentShape` ) {\n"
	    "			string $dest_array[] = `listConnections -destination true -source false -plugs false -type \"shadingEngine\" $currentShape`;\n"
	    "			for( $eng in $dest_array ) { \n"
	    "				$shadingEngines[ size($shadingEngines) ] = $eng;\n"
	    "				}\n"
	    "			}\n"
	    "		}\n"
	    "	return stringArrayRemoveDuplicates($shadingEngines); // listConnections can return duplicates within its list.\n"
	    "	};\n"
	    "proc SimplygonMaya_copyUVSetLinks( string $srcnode )\n"
	    "   {\n"
	    "   int $src_uv_indices[] = `polyUVSet -q -allUVSetsIndices $srcnode`;\n"
	    "   string $dest_nodes[] = `ls -selection`;\n"
	    "   for( $dest in $dest_nodes )\n"
	    "		{\n"
	    "		int $dest_uv_indices[] = `polyUVSet -q -allUVSetsIndices $dest`;\n"
	    "		for( $srcinx in $src_uv_indices )\n"
	    "			{\n"
	    "			string $src_attribute_name = $srcnode+\".uvSet[\"+$srcinx+\"].uvSetName\";\n"
	    "			string $uvset = `getAttr($src_attribute_name)`;\n"
	    "			string $link_texs[] = `uvLink -query -uvSet $src_attribute_name`;\n"
	    "			for( $destinx in $dest_uv_indices )\n"
	    "				{\n"
	    "				string $dest_attribute_name = $dest+\".uvSet[\"+$destinx+\"].uvSetName\";\n"
	    "				string $dest_uvset = `getAttr($dest_attribute_name)`;\n"
	    "				if( $uvset == $dest_uvset )\n"
	    "					{\n"
	    "					for( $tex in $link_texs )\n"
	    "						{\n"
	    "						uvLink -make -uvSet $dest_attribute_name -texture $tex;\n"
	    "						}\n"
	    "					}\n"
	    "				}\n"
	    "			}\n"
	    "		}\n"
	    "	};\n"
	    "proc string[] SimplygonMaya_createPhongShader( string $shader_name )\n"
	    "	{\n"
	    "	$shader_node = `shadingNode -asShader phong -name $shader_name`;\n"
	    "	$shading_group_node = `sets -renderable true -noSurfaceShader true -empty -name ($shader_name+\"SG\")`;\n"
	    "	connectAttr -f ($shader_node+\".outColor\") ($shading_group_node+\".surfaceShader\");\n"
	    "	string $ret[];\n"
	    "    $ret[0] = $shader_node;\n"
	    "    $ret[1] = $shading_group_node;\n"
	    "	return $ret;\n"
	    "	};\n"
	    "proc SimplygonMaya_addPlacementNode( string $file_node )\n"
	    "	{\n"
	    "	string $place_node = `shadingNode -asUtility place2dTexture`;\n"
	    "	connectAttr -f ($place_node+\".coverage\") ($file_node+\".coverage\");\n"
	    "	connectAttr -f ($place_node+\".translateFrame\") ($file_node+\".translateFrame\");\n"
	    "	connectAttr -f ($place_node+\".rotateFrame\") ($file_node+\".rotateFrame\");\n"
	    "	connectAttr -f ($place_node+\".mirrorU\") ($file_node+\".mirrorU\");\n"
	    "	connectAttr -f ($place_node+\".mirrorV\") ($file_node+\".mirrorV\");\n"
	    "	connectAttr -f ($place_node+\".stagger\") ($file_node+\".stagger\");\n"
	    "	connectAttr -f ($place_node+\".wrapU\") ($file_node+\".wrapU\");\n"
	    "	connectAttr -f ($place_node+\".wrapV\") ($file_node+\".wrapV\");\n"
	    "	connectAttr -f ($place_node+\".repeatUV\") ($file_node+\".repeatUV\");\n"
	    "	connectAttr -f ($place_node+\".offset\") ($file_node+\".offset\");\n"
	    "	connectAttr -f ($place_node+\".rotateUV\") ($file_node+\".rotateUV\");\n"
	    "	connectAttr -f ($place_node+\".noiseUV\") ($file_node+\".noiseUV\");\n"
	    "	connectAttr -f ($place_node+\".vertexUvOne\") ($file_node+\".vertexUvOne\");\n"
	    "	connectAttr -f ($place_node+\".vertexUvTwo\") ($file_node+\".vertexUvTwo\");\n"
	    "	connectAttr -f ($place_node+\".vertexUvThree\") ($file_node+\".vertexUvThree\");\n"
	    "	connectAttr -f ($place_node+\".vertexCameraOne\") ($file_node+\".vertexCameraOne\");\n"
	    "	connectAttr -f ($place_node+\".outUV\") ($file_node+\".uv\");\n"
	    "	connectAttr -f ($place_node+\".outUvFilterSize\") ($file_node+\".uvFilterSize\");\n"
	    "	};\n"
	    "proc SimplygonMaya_setColorSpace( string $file_node, string $requested_color_space )\n"
	    "	{\n"
	    "	 string $currentColorSpace = `getAttr ($file_node + \".colorSpace\")`;\n"
	    "	 if($currentColorSpace != $requested_color_space)\n"
	    "	 {\n"
	    "		string $availableColorSpaces[] = `colorManagementPrefs -q -inputSpaceNames`;\n"
	    "		if ( stringArrayContains($requested_color_space, $availableColorSpaces) )\n"
	    "		{\n"
	    "			setAttr ($file_node+\".ignoreColorSpaceFileRules\") 1;\n"
	    "			setAttr ($file_node+\".colorSpace\") -type \"string\" $requested_color_space;\n"
	    "		}\n"
	    "	 }\n"
	    "	};\n"
	    "proc string[] SimplygonMaya_createPhongMaterial(string $srcshape, string $shader_name, string $ambient, string $diffuse, string $specular, "
	    "string $normals, string $transparency, string $translucence, string $translucence_depth, string $translucence_focus, string $incandescence, string $reflectedcolor, string $reflectivity, float "
	    "$base_cosine_power, string $ambient_uv, string $diffuse_uv, string $specular_uv, string $normals_uv, string $transparency_uv, string "
	    "$translucence_uv, string $translucence_depth_uv, string $translucence_focus_uv, string $incandescence_uv, string $reflectedcolor_uv, string $reflectivity_uv, int $ambient_srgb, int $diffuse_srgb, int "
	    "$specular_srgb, int $transparency_srgb, int $translucence_srgb, int $translucence_depth_srgb, int $translucence_focus_srgb, int $incandescence_srgb, int $reflectedcolor_srgb, int $reflectivity_srgb "
	    ")\n"
	    "	{\n"
	    "	string $file_node;\n"
	    "	string $shader[] = SimplygonMaya_createPhongShader($shader_name);\n"
	    "	string $shader_node = $shader[0];\n"
	    "	string $shading_group_node = $shader[1];\n"
	    "	\n"
	    "   string $ambient_file_node;"
	    "   if( $ambient != \"\"){\n"
	    "	 $ambient_file_node = `shadingNode -isColorManaged -asTexture file`;\n"
	    "	 SimplygonMaya_setColorSpace($ambient_file_node, $ambient_srgb == 1 ? \"sRGB\" : \"Raw\");\n"
	    "	 SimplygonMaya_addPlacementNode( $ambient_file_node );\n"
	    "	 setAttr ($ambient_file_node+\".fileTextureName\") -type \"string\" $ambient;\n"
	    "	 connectAttr -f ($ambient_file_node+\".outColor\") ($shader_node+\".ambientColor\");\n"
	    "	 CreateLink($srcshape, $ambient_uv, $ambient_file_node); \n"
	    "   }"
	    "	\n"
	    "   string $diffuse_file_node;"
	    "   if( $diffuse != \"\"){\n"
	    "	 $diffuse_file_node = `shadingNode -isColorManaged -asTexture file`;\n"
	    "	 SimplygonMaya_setColorSpace($diffuse_file_node, $diffuse_srgb == 1 ? \"sRGB\" : \"Raw\");\n"
	    "	 SimplygonMaya_addPlacementNode( $diffuse_file_node );\n"
	    "	 setAttr ($diffuse_file_node+\".fileTextureName\") -type \"string\" $diffuse;\n"
	    "	 setAttr ($shader_node+\".diffuse\") 1.0;\n"
	    "	 connectAttr -f ($diffuse_file_node+\".outColor\") ($shader_node+\".color\");\n"
	    "	 CreateLink($srcshape, $diffuse_uv, $diffuse_file_node); \n"
	    "   }"
	    "	\n"
	    "   string $specular_file_node;"
	    "   if( $specular != \"\"){\n"
	    "	 $specular_file_node = `shadingNode -isColorManaged -asTexture file`;\n"
	    "	 SimplygonMaya_setColorSpace($specular_file_node, $specular_srgb == 1 ? \"sRGB\" : \"Raw\");\n"
	    "	 SimplygonMaya_addPlacementNode( $specular_file_node );\n"
	    "	 setAttr ($specular_file_node+\".fileTextureName\") -type \"string\" $specular;\n"
	    "	 connectAttr -f ($specular_file_node+\".outColor\") ($shader_node+\".specularColor\");\n"
	    "    string $cosinePowerMultiplyNode = `shadingNode -asUtility multiplyDivide`;\n"
	    "	 string $plusMinusAverageNode = `shadingNode - asUtility plusMinusAverage`;\n"
	    "    connectAttr -f ($specular_file_node+\".outAlpha\") ($plusMinusAverageNode+\".input1D[0]\");\n"
	    "    setAttr ($plusMinusAverageNode+\".input1D[1]\") 1;\n"
	    "    connectAttr - f ($plusMinusAverageNode+\".output1D\") ($cosinePowerMultiplyNode+\".input1X\");\n"
	    "	 setAttr ($cosinePowerMultiplyNode+\".input2X\") $base_cosine_power;\n"
	    "    connectAttr -f ($cosinePowerMultiplyNode+\".outputX\") ($shader_node+\".cosinePower\");\n"
	    "	 CreateLink($srcshape, $specular_uv, $specular_file_node); \n"
	    "   }"
	    "	\n"
	    "   string $transparency_file_node;"
	    "   if( $transparency != \"\"){\n"
	    "	 $transparency_file_node = `shadingNode -isColorManaged -asTexture file`;\n"
	    "	 SimplygonMaya_setColorSpace($transparency_file_node, $transparency_srgb == 1 ? \"sRGB\" : \"Raw\");\n"
	    "	 SimplygonMaya_addPlacementNode( $transparency_file_node );\n"
	    "	 setAttr ($transparency_file_node+\".fileTextureName\") -type \"string\" $transparency;\n"
	    "	 connectAttr -f ($transparency_file_node+\".outTransparency\") ($shader_node+\".transparency\");\n"
	    "	 CreateLink($srcshape, $transparency_uv, $transparency_file_node); \n"
	    "   }"
	    "	\n"
	    "   string $translucence_file_node;"
	    "   if( $translucence != \"\"){\n"
	    "	 $translucence_file_node = `shadingNode -isColorManaged -asTexture file`;\n"
	    "	 SimplygonMaya_setColorSpace($translucence_file_node, $translucence_srgb == 1 ? \"sRGB\" : \"Raw\");\n"
	    "	 SimplygonMaya_addPlacementNode( $translucence_file_node );\n"
	    "	 setAttr ($translucence_file_node+\".fileTextureName\") -type \"string\" $translucence;\n"
	    "	 connectAttr -f ($translucence_file_node+\".outAlpha\") ($shader_node+\".translucence\");\n"
	    "	 CreateLink($srcshape, $translucence_uv, $translucence_file_node); \n"
	    "   }"
	    "	\n"
	    "   string $translucence_depth_file_node;"
	    "   if( $translucence_depth != \"\"){\n"
	    "	 $translucence_depth_file_node = `shadingNode -isColorManaged -asTexture file`;\n"
	    "	 SimplygonMaya_setColorSpace($translucence_depth_file_node, $translucence_depth_srgb == 1 ? \"sRGB\" : \"Raw\");\n"
	    "	 SimplygonMaya_addPlacementNode( $translucence_depth_file_node );\n"
	    "	 setAttr ($translucence_depth_file_node+\".fileTextureName\") -type \"string\" $translucence_depth;\n"
	    "	 connectAttr -f ($translucence_depth_file_node+\".outAlpha\") ($shader_node+\".translucenceDepth\");\n"
	    "	 CreateLink($srcshape, $translucence_depth_uv, $translucence_depth_file_node); \n"
	    "   }"
	    "	\n"
	    "   string $translucence_focus_file_node;"
	    "   if( $translucence_focus != \"\"){\n"
	    "	 $translucence_focus_file_node = `shadingNode -isColorManaged -asTexture file`;\n"
	    "	 SimplygonMaya_setColorSpace($translucence_focus_file_node, $translucence_focus_srgb == 1 ? \"sRGB\" : \"Raw\");\n"
	    "	 SimplygonMaya_addPlacementNode( $translucence_focus_file_node );\n"
	    "	 setAttr ($translucence_focus_file_node+\".fileTextureName\") -type \"string\" $translucence_focus;\n"
	    "	 connectAttr -f ($translucence_focus_file_node+\".outAlpha\") ($shader_node+\".translucenceFocus\");\n"
	    "	 CreateLink($srcshape, $translucence_focus_uv, $translucence_focus_file_node); \n"
	    "   }"
	    "	\n"
	    "   string $normals_file_node;"
	    "   if( $normals != \"\"){\n"
	    "	 $normals_file_node = `shadingNode -isColorManaged -asTexture file`;\n"
	    "	 SimplygonMaya_setColorSpace($normals_file_node, \"Raw\");\n"
	    "	 SimplygonMaya_addPlacementNode( $normals_file_node );\n"
	    "	 setAttr ($normals_file_node+\".fileTextureName\") -type \"string\" $normals;\n"

	    "	 string $bump_node = `shadingNode -asUtility bump2d`;\n"
	    "	 connectAttr -f ($normals_file_node+\".outAlpha\") ($bump_node+\".bumpValue\");\n"
	    "	 connectAttr -f ($bump_node+\".outNormal\") ($shader_node+\".normalCamera\");\n"
	    "	 setAttr ($bump_node+\".bumpInterp\") 1;\n"
	    "	 CreateLink($srcshape, $normals_uv, $normals_file_node); \n"
	    "   }"
	    "	\n"
	    "   string $incandescence_file_node;"
	    "   if( $incandescence != \"\"){\n"
	    "	 $incandescence_file_node = `shadingNode -isColorManaged -asTexture file`;\n"
	    "	 SimplygonMaya_setColorSpace($incandescence_file_node, $incandescence_srgb == 1 ? \"sRGB\" : \"Raw\");\n"
	    "	 SimplygonMaya_addPlacementNode( $incandescence_file_node );\n"
	    "	 setAttr ($incandescence_file_node+\".fileTextureName\") -type \"string\" $incandescence;\n"
	    "	 connectAttr -f ($incandescence_file_node+\".outColor\") ($shader_node+\".incandescence\");\n"
	    "	 CreateLink($srcshape, $incandescence_uv, $incandescence_file_node); \n"
	    "   }"
	    "	\n"
	    "   string $reflectedcolor_file_node;"
	    "   if( $reflectedcolor != \"\"){\n"
	    "	 $reflectedcolor_file_node = `shadingNode -isColorManaged -asTexture file`;\n"
	    "	 SimplygonMaya_setColorSpace($reflectedcolor_file_node, $reflectedcolor_srgb == 1 ? \"sRGB\" : \"Raw\");\n"
	    "	 SimplygonMaya_addPlacementNode( $reflectedcolor_file_node );\n"
	    "	 setAttr ($reflectedcolor_file_node+\".fileTextureName\") -type \"string\" $reflectedcolor;\n"
	    "	 connectAttr -f ($reflectedcolor_file_node+\".outColor\") ($shader_node+\".reflectedColor\");\n"
	    "	 CreateLink($srcshape, $reflectedcolor_uv, $reflectedcolor_file_node); \n"
	    "   }"
	    "	\n"
	    "   string $reflectivity_file_node;"
	    "   if( $reflectivity != \"\"){\n"
	    "	 $reflectivity_file_node = `shadingNode -isColorManaged -asTexture file`;\n"
	    "	 SimplygonMaya_setColorSpace($reflectivity_file_node, $reflectivity_srgb == 1 ? \"sRGB\" : \"Raw\");\n"
	    "	 SimplygonMaya_addPlacementNode( $reflectivity_file_node );\n"
	    "	 setAttr ($reflectivity_file_node+\".fileTextureName\") -type \"string\" $reflectivity;\n"
	    "	 connectAttr -f ($reflectivity_file_node+\".outAlpha\") ($shader_node+\".reflectivity\");\n"
	    "	 CreateLink($srcshape, $reflectivity_uv, $reflectivity_file_node); \n"
	    "   }"
	    "	\n"
	    "	return $shader;\n"
	    "	}\n"
	    "proc SimplygonMaya_copyObjectLevelBlindData( string $srcshape , string $destshape )\n"
	    "	{\n"
	    "	string $blindDataTemplates[] = `ls -type \"blindDataTemplate\"`;\n"
	    "	for( $template in $blindDataTemplates ){\n"
	    "		int $id = `getAttr ( $template + \".typeId\" )`;\n"
	    "		int $compoundSize = `getAttr -size ( $template + \".bdui\" )`;\n"
	    "		string $userInfoName;\n"
	    "		string $userInfoValue;\n"
	    "		string $attrInfoName;\n"
	    "		string $attrInfoValue;\n"
	    "		for( $i = 0; $i <= $compoundSize; $i++ ){\n"
	    "			$attrInfoName = $template;\n"
	    "			$attrInfoName += ( \".bdui[\" + $i + \"]\" );\n"
	    "			$attrInfoName += ( \".bdun\" );\n"
	    "			$userInfoName = `getAttr $attrInfoName`;\n"
	    "			$attrInfoValue = $template;\n"
	    "			$attrInfoValue += ( \".bdui[\" + $i + \"]\" );\n"
	    "			$attrInfoValue += ( \".bduv\" );\n"
	    "			$userInfoValue = `getAttr $attrInfoValue`;\n"
	    "			if( $userInfoName == \"typeTag\" ||\n"
	    "				$userInfoName == \"assocType\" ||\n"
	    "				$userInfoName == \"freeSet\" ||\n"
	    "				$userInfoName == \"dataCount\" ||\n"
	    "				$userInfoName == \"\" ) {\n"
	    "				continue;\n"
	    "				}\n"
	    "			if( $userInfoValue == \"double\" ){\n"
	    "				float $val[] = `polyQueryBlindData -id $id -associationType \"object\" -ldn $userInfoName $srcshape`;\n"
	    "				if( size($val) > 0 ) { string $result[] = `polyBlindData -id $id -associationType \"object\" -ldn $userInfoName -dbd $val[0] "
	    "$destshape`; }\n"
	    "				}\n"
	    "			else if( $userInfoValue == \"float\" ){\n"
	    "				float $val[] = `polyQueryBlindData -id $id -associationType \"object\" -ldn $userInfoName $srcshape`;\n"
	    "				if( size($val) > 0 ) { string $result[] = `polyBlindData -id $id -associationType \"object\" -ldn $userInfoName -dbd $val[0] "
	    "$destshape`; }\n"
	    "				}\n"
	    "			else if( $userInfoValue == \"string\" ){\n"
	    "				string $val[] = `polyQueryBlindData -id $id -associationType \"object\" -ldn $userInfoName $srcshape`;\n"
	    "				if( size($val) > 0 ) { string $result[] = `polyBlindData -id $id -associationType \"object\" -ldn $userInfoName -sd $val[0] "
	    "$destshape`; }\n"
	    "				}\n"
	    "			else if( $userInfoValue == \"int\" ){\n"
	    "				int $val[] = `polyQueryBlindData -id $id -associationType \"object\" -ldn $userInfoName $srcshape`;\n"
	    "				if( size($val) > 0 ) { string $result[] = `polyBlindData -id $id -associationType \"object\" -ldn $userInfoName -ind $val[0] "
	    "$destshape`; }\n"
	    "				}				\n"
	    "			else if( $userInfoValue == \"hex\" ){\n"
	    "				int $val[] = `polyQueryBlindData -id $id -associationType \"object\" -ldn $userInfoName $srcshape`;\n"
	    "				if( size($val) > 0 ) { string $result[] = `polyBlindData -id $id -associationType \"object\" -ldn $userInfoName -ind $val[0] "
	    "$destshape`;	}\n"
	    "				}		\n"
	    "			else if( $userInfoValue == \"boolean\" ){\n"
	    "				int $val[] = `polyQueryBlindData -id $id -associationType \"object\" -ldn $userInfoName $srcshape`;\n"
	    "				if( size($val) > 0 ) { string $result[] = `polyBlindData -id $id -associationType \"object\" -ldn $userInfoName -bd $val[0] "
	    "$destshape`; }\n"
	    "				}	\n"
	    "			else if( $userInfoValue == \"binary\" ){\n"
	    "				string $val[] = `polyQueryBlindData -id $id -associationType \"object\" -ldn $userInfoName $srcshape`;\n"
	    "				if( size($val) > 0 ) { string $result[] = `polyBlindData -id $id -associationType \"object\" -ldn $userInfoName -bnd $val[0] "
	    "$destshape`;	}\n"
	    "				}\n"
	    "			}\n"
	    "		};\n"
	    "	};\n"
	    "proc SimplygonMaya_copyAttributes( string $object_name , string $dest_object_name )\n"
	    "   {"
	    "	string $user_attributes[] = `listAttr -ud $object_name`;\n"
	    "	for( $attribute in $user_attributes ){\n"
	    "     if(`objExists ($dest_object_name + \".\" + $attribute)` == false)\n"
	    "		{\n"
	    "		    string $atype = `getAttr -type ($object_name + \".\" + $attribute)`;\n"
	    "			int $isKeyable = `getAttr -keyable ($object_name + \".\" + $attribute)`;\n"
	    "			int $isSettable = `getAttr -settable ($object_name + \".\" + $attribute)`;\n"
	    "           if($atype == \"string\" || $atype == \"double3\" || $atype == \"float3\")\n"
	    "			{\n"
	    "				addAttr -ln $attribute -dt $atype -keyable $isKeyable $dest_object_name;\n"
	    "			}\n"
	    "			else if($atype == \"enum\")\n"
	    "			{\n"
	    "			    string $enumFields = `addAttr -q -enumName ($object_name + \".\" + $attribute)`;\n"
	    "				addAttr -ln $attribute -at $atype -keyable $isKeyable -en $enumFields $dest_object_name ;\n"
	    "			}\n"
	    "			else if($atype == \"TdataCompound\")\n"
	    "			{\n"
	    "			}\n"
	    "			else if($atype == \"Int32Array\")\n"
	    "			{\n"
	    "			}\n"
	    "			else\n"
	    "			{\n"
	    "				if (catchQuiet (`addAttr -ln $attribute -at $atype -keyable $isKeyable $dest_object_name`))\n"
	    "				{\n"
	    "					warning(\"could not add attribute '\" + $attribute + \"' of type '\" + $atype + \"'\");\n"
	    "				}\n"
	    "			}\n"
	    "		}\n"
	    "		if (catchQuiet (`copyAttr -values -attribute $attribute $object_name $dest_object_name`))\n"
	    "		{\n"
	    "			warning(\"could not copy attribute '\" + $attribute + \"', ignoring attribute.\");\n"
	    "		}\n"
	    "	}\n"

	    " string $AttributeNames[] = {   \"doubleSided\", \"opposite\", \"smoothShading\", \"motionBlur\", \"visibleInReflections\", \"visibleInRefractions\", "
	    "\"castsShadows\", \"receiveShadows\", \"primaryVisibility\", \"geometryAntialiasingOverride\",\"antialiasingLevel\", \"shadingSamplesOverride\", "
	    "\"shadingSamples\", \"maxShadingSamples\", \"volumeSamplesOverride\", \"volumeSamples\", \"maxVisibilitySamplesOverride\", \"maxVisibilitySamples\", "
	    "\"boundingBoxScaleX\", \"boundingBoxScaleY\", \"boundingBoxScaleZ\", \"featureDisplacement\", \"initialSampleRate\", \"extraSampleRate\", "
	    "\"textureThreshold\", \"normalThreshold\" }; \n"
	    " for ($Attribute in $AttributeNames) \n"
	    "	{ \n"
	    "   $attrib = $object_name + \".\" + $Attribute; \n"
	    "   if(`objExists $attrib`) \n"
	    "	    { \n"
	    "	    float $isEnabled = `getAttr ($object_name + \".\" + $Attribute)`; \n"
	    "   	setAttr ($dest_object_name + \".\" + $Attribute) $isEnabled; \n"
	    //		"	    print ($dest_object_name + \".\" + $Attribute + \" = \" + $isEnabled + \" \\n \"); \n"
	    "	    } \n"
	    "   else  \n"
	    "       { \n"
	    //		"       print ($dest_object_name + \".\" + $Attribute + \" = Not available! \\n \"); \n"
	    "       } \n"
	    "   } \n"
	    " };\n";

	MStatus mStatus = MStatus::kSuccess;

	// install the MEL script functions
	mStatus = executeGlobalCommand( MString( cSimplygonScriptFunctions ) );
	if( !mStatus )
	{
		MGlobal::displayError( "Could not register Simplygon script functions." );
		return mStatus;
	}

	return mStatus;
}

std::string CorrectedVersionString( std::string sVersionString )
{
	std::string sCorrectedVersionString = "";
	for( std::string::iterator it = sVersionString.begin(); it != sVersionString.end(); ++it )
	{
		const int c = (int)*it;
		if( c >= 0 && c <= 9 )
		{
			sCorrectedVersionString += *it;
		}
	}

	return sCorrectedVersionString;
}

MStatus SimplygonCmd::AddNodesToSelectionSet( std::string sgNodeType )
{
	const int selectionSetId = this->sceneHandler->sgScene->SelectNodes( sgNodeType.c_str() );
	if( selectionSetId >= 0 )
	{
		spSelectionSetTable sgSelectionSetTable = this->sceneHandler->sgScene->GetSelectionSetTable();
		spSelectionSet sgSceneNodes = sgSelectionSetTable->GetSelectionSet( selectionSetId );
		if( !sgSceneNodes.IsNull() )
		{
			for( uint nodeIndex = 0; nodeIndex < sgSceneNodes->GetItemCount(); ++nodeIndex )
			{
				std::string sNodeId = sgSceneNodes->GetItem( nodeIndex );
				spSceneNode sgNode = this->sceneHandler->sgScene->GetNodeByGUID( sNodeId.c_str() );
				if( sgNode.IsNull() )
				{
					continue;
				}

				const std::string sNodeName = sgNode->GetName();

				for( std::map<std::string, std::set<std::string>>::const_iterator& sSetIterator = this->selectionSets.begin();
				     sSetIterator != this->selectionSets.end();
				     sSetIterator++ )
				{
					// for the current set, check if mesh name exists
					for( const std::string& sSetNodeName : sSetIterator->second )
					{
						// if exists
						if( sNodeName == sSetNodeName )
						{
							spSelectionSet sgSelectionSetList = Simplygon::NullPtr;
							bool bAddSetToScene = false;

							// does the set exists in scene to be exported?
							spObject sgCurrentSelectionSetObject = sgSelectionSetTable->FindItem( sSetIterator->first.c_str() );
							if( !sgCurrentSelectionSetObject.IsNull() )
							{
								sgSelectionSetList = spSelectionSet::SafeCast( sgCurrentSelectionSetObject );
							}

							// create if it does not exist
							if( sgSelectionSetList.IsNull() )
							{
								sgSelectionSetList = sg->CreateSelectionSet();
								sgSelectionSetList->SetName( sSetIterator->first.c_str() );
								bAddSetToScene = true;
							}

							// add the guid of the ssf node to the ssf scene
							sgSelectionSetList->AddItem( sNodeId.c_str() );

							if( bAddSetToScene )
							{
								sgSelectionSetTable->AddItem( sgSelectionSetList );
							}

							break;
						}
					}
				}
			}
		}
	}

	return MStatus::kSuccess;
}

MStatus SimplygonCmd::ExtractScene()
{
	DisableBlendShapes();

	MStatus mStatus = MStatus::kSuccess;

	// retrieve the current selection
	MGlobal::getActiveSelectionList( this->InitialSelectionList );

	this->sceneHandler->SelectedForProcessingList = this->InitialSelectionList;

	const int mayaVersion = GetMayaVersion();

	spStringArray sgActiveSetArray = sg->CreateStringArray();

	if( this->sgPipeline.NonNull() )
		this->sgPipeline->GetActiveSelectionSets( sgActiveSetArray );

	// if there is active selection-sets in pipeline, export those objects
	if( sgActiveSetArray->GetItemCount() > 0 )
	{
		for( uint i = 0; i < sgActiveSetArray->GetItemCount(); ++i )
		{
			spString sgSetName = sgActiveSetArray->GetItem( i );
			const char* cSetName = sgSetName.c_str();
			this->activeSelectionSets.insert( cSetName );
		}
	}

	// if no object is selected, display error and go back to Maya
	else if( sceneHandler->SelectedForProcessingList.length() <= 0 )
	{
		MGlobal::displayError( MString( "No object was selected for processing in Simplygon. Please select an object." ) );
		return MStatus::kFailure;
	}

	this->SetCurrentProgressRange( 0, 100 );
	this->LogToWindow( _T("Traversing scene"), 10 );

	// setup the Simplygon scene tree
	this->sceneHandler->ExtractSceneGraph( this );

	this->LogToWindow( _T("Traversing scene"), 20 );

	// setup all meshes that have been added to the scene
	const size_t numMeshes = this->sceneHandler->sceneMeshes.size();
	this->SetCurrentProgressRange( 0, (int)numMeshes );

	std::string tStaticNodeText = "Setting up node ";
	for( size_t meshIndex = 0; meshIndex < numMeshes; ++meshIndex )
	{
		std::string tLogMessage = tStaticNodeText + std::to_string( meshIndex );
		this->LogToWindow( tLogMessage, (int)meshIndex );

		MayaSgNodeMapping& meshMap = this->sceneHandler->sceneMeshes[ meshIndex ];
		mStatus = meshMap.mayaNode->Initialize();
		if( !mStatus )
		{
			return mStatus;
		}

		MDagPath mMayaNode = meshMap.mayaNode->GetOriginalNode();
		std::string sgNodeGuid = meshMap.sgNode->GetNodeGUID();

		// add nodes to global mapping
		this->s_GlobalMeshDagPathToGuid.insert( std::pair<std::string, std::string>( mMayaNode.fullPathName().asChar(), sgNodeGuid ) );
		this->s_GlobalMeshGuidToDagPath.insert( std::pair<std::string, std::string>( sgNodeGuid, mMayaNode.fullPathName().asChar() ) );
	}

	this->SetCurrentProgressRange( 0, 100 );
	this->LogToWindow( _T("Setting up materials"), 40 );

	// extract all used materials from the scene meshes
	mStatus = this->ExtractSceneMaterials();
	if( !mStatus )
	{
		return mStatus;
	}

	// extract the mesh and geometry data from the nodes, and delete the duplicated, temporary nodes
	this->SetCurrentProgressRange( 0, (int)numMeshes );

	uint32_t numTriangulationWarnings = 0;
	uint32_t numMeshesWarningsFoundIn = 0;
	tStaticNodeText = "Extracting mesh ";
	for( size_t meshIndex = 0; meshIndex < numMeshes; ++meshIndex )
	{
		std::string tLogMessage = tStaticNodeText + std::to_string( meshIndex + 1 );
		this->LogToWindow( tLogMessage, (int)meshIndex );

		MayaSgNodeMapping* mayaSgNodeMap = &this->sceneHandler->sceneMeshes[ meshIndex ];
		mayaSgNodeMap->mayaNode->hasCreaseValues = this->creaseValues;
		mayaSgNodeMap->mayaNode->vertexLockSets = this->vertexLockSets;
		mayaSgNodeMap->mayaNode->vertexLockMaterials = this->vertexLockMaterials;

		if( this->useQuadExportImport )
		{
			mStatus = mayaSgNodeMap->mayaNode->ExtractMeshData_Quad( this->materialHandler );
		}
		else
		{
			mStatus = mayaSgNodeMap->mayaNode->ExtractMeshData( this->materialHandler );
		}

		if( !mStatus )
		{
			MGlobal::displayError( MString( "Simplygon: Failed to extract geometry from node " ) + mayaSgNodeMap->mayaNode->GetOriginalNode().fullPathName() );
			return mStatus;
		}

		if( mayaSgNodeMap->mayaNode->numBadTriangulations > 0 ) 
		{
			numTriangulationWarnings += mayaSgNodeMap->mayaNode->numBadTriangulations;
			++numMeshesWarningsFoundIn;
		}

		// store in node_mesh object, and scene mesh
		mayaSgNodeMap->sgMeshData = mayaSgNodeMap->mayaNode->GetGeometryData();
		mayaSgNodeMap->sgNode->SetGeometry( mayaSgNodeMap->sgMeshData );

		mStatus = mayaSgNodeMap->mayaNode->ExtractBlendShapeData();
		if( !mStatus )
		{
			MGlobal::displayError( MString( "Simplygon: Failed to extract blend shapes from node " ) +
			                       mayaSgNodeMap->mayaNode->GetOriginalNode().fullPathName() );
			return MStatus::kFailure;
		}
	}

	if( numTriangulationWarnings > 0 )
	{
		std::string bWarning = "Quad export - Found " + std::to_string( numTriangulationWarnings ) + " polygons in " +
		                       std::to_string( numMeshesWarningsFoundIn ) + " meshes which could not be optimally triangulated";

		MGlobal::displayWarning( MString( bWarning.c_str() ) );
	}

	EnableBlendShapes();

	// add node(s) to selection set(s)
	this->AddNodesToSelectionSet( "ISceneNode" );
	this->AddNodesToSelectionSet( "ISceneBone" );

	if( this->sceneHandler->sceneMeshes.size() == 0 )
	{
		MGlobal::displayError( MString( "Simplygon: no meshes selected, ignoring." ) );
		return MStatus::kFailure;
	}

	return mStatus;
}

MStatus SimplygonCmd::ExtractSceneMaterials()
{
	// all nodes are setup, collect the materials used by the nodes, and setup the materials
	this->materialHandler->Setup( this->sceneHandler->sgScene->GetMaterialTable(), this->sceneHandler->sgScene->GetTextureTable() );
	for( size_t meshIndex = 0; meshIndex < this->sceneHandler->sceneMeshes.size(); ++meshIndex )
	{
		// collect all materials in this node
		const std::vector<MString> meshMaterials = this->sceneHandler->sceneMeshes[ meshIndex ].mayaNode->GetMaterials();
		for( size_t materialIndex = 0; materialIndex < meshMaterials.size(); ++materialIndex )
		{
			// add in material handler
			if( !this->materialHandler->AddMaterial( meshMaterials[ materialIndex ] ) )
			{
				return MStatus::kFailure;
			}
		}
	}

	return MStatus::kSuccess;
}

#pragma region Logging
void SimplygonCmd::ProgressCallback( int progress )
{
	static int lastProgress = -1;
	if( progress != lastProgress )
	{
		lastProgress = progress;
		this->LogToWindow( "Processing...", progress );
	}
}

void SimplygonCmd::ErrorCallback( const char* errorMessage )
{
	this->LogErrorToWindow( errorMessage );
}

bool SimplygonCmd::UseSettingsPipelineForProcessing( const INT64 pipelineId )
{
	std::map<INT64, spPipeline>::iterator pipelineIterator = PipelineHelper::Instance()->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == PipelineHelper::Instance()->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	this->sgPipeline = pipelineIterator->second;

	return true;
}

MStatus SimplygonCmd::ExportToFile( std::basic_string<TCHAR> tExportFilePath )
{
	MStatus mStatus;

	mStatus = this->RegisterGlobalScripts();
	if( !mStatus )
		return mStatus;

	this->ClearGlobalMapping();

	mStatus = this->ExtractScene();
	if( !mStatus )
		return mStatus;

	const char* cExportFilePath = LPCTSTRToConstCharPtr( tExportFilePath.c_str() );

	const bool bSceneSaved = this->GetSceneHandler()->sgScene->SaveToFile( cExportFilePath );
	mStatus = bSceneSaved ? MStatus::kSuccess : MStatus::kFailure;

	return mStatus;
}

MStatus SimplygonCmd::ImportFromFile( std::basic_string<TCHAR> tImportFilePath )
{
	MStatus mStatus = MStatus::kFailure;

	const char* cImportFilePath = LPCTSTRToConstCharPtr( tImportFilePath.c_str() );

	spScene sgLodScene = sg->CreateScene();

	const bool bSceneLoaded = sgLodScene->LoadFromFile( cImportFilePath );
	if( bSceneLoaded )
	{
		if( this->GetSceneHandler() == nullptr )
		{
			this->sceneHandler = new Scene();
		}

		this->GetSceneHandler()->sgProcessedScenes = { sgLodScene };
		mStatus = this->ImportScenes();
	}

	return mStatus;
}

void SimplygonCmd::ClearGlobalMapping()
{
	SimplygonCmd::s_GlobalMaterialDagPathToGuid.clear();
	SimplygonCmd::s_GlobalMaterialGuidToDagPath.clear();

	SimplygonCmd::s_GlobalMeshDagPathToGuid.clear();
	SimplygonCmd::s_GlobalMeshGuidToDagPath.clear();
}

void SimplygonCmd::LogErrorToWindow( std::basic_string<TCHAR> tMessage, int progress )
{
	this->LogToWindow( tMessage, progress );
	EnterCriticalSection( &this->cs );
	{
		MString mMessage = LPCTSTRToConstCharPtr(tMessage.c_str());

		MGlobal::displayError( MString( "(Simplygon): ") + mMessage );

		// Send log message to Simplygon UI.
		MString sendLogToUICommand = "SimplygonUI -SendErrorToLog ";
		sendLogToUICommand += CreateQuotedTextAndRemoveLineBreaks( mMessage );
		sendLogToUICommand += ";";
		MGlobal::executeCommand( sendLogToUICommand );
	}
	LeaveCriticalSection( &this->cs );
}

void SimplygonCmd::LogWarningToWindow( std::basic_string<TCHAR> tMessage, int progress )
{
	this->LogToWindow( tMessage, progress );
	EnterCriticalSection( &this->cs );
	{
		MString mMessage = LPCTSTRToConstCharPtr( tMessage.c_str() );

		MGlobal::displayWarning( MString( "(Simplygon): " ) + mMessage );

		// Send log message to Simplygon UI.
		MString sendLogToUICommand = "SimplygonUI -SendWarningToLog ";
		sendLogToUICommand += CreateQuotedTextAndRemoveLineBreaks( mMessage );
		sendLogToUICommand += ";";
		MGlobal::executeCommand( sendLogToUICommand );
	}
	LeaveCriticalSection( &this->cs );
}

void SimplygonCmd::LogToWindow( std::basic_string<TCHAR> tMessage, int progress )
{
	if( this->hasProgressWindow )
	{
		EnterCriticalSection( &this->cs );

		this->SetCurrentProcess( LPCTSTRToConstCharPtr( tMessage.c_str() ) );
		const int previousProgress = MProgressWindow::progress();

		if( progress != MProgressWindow::progressMax() )
		{
			MProgressWindow::setProgress( MProgressWindow::progressMax() );
		}
		else
		{
			MProgressWindow::setProgress( MProgressWindow::progressMin() );
		}
		if( progress != -1 )
		{
			MProgressWindow::setProgress( progress );
		}
		else
		{
			MProgressWindow::setProgress( previousProgress );
		}

		LeaveCriticalSection( &this->cs );
	}
}
#pragma endregion

MStatus SimplygonCmd::ProcessScene()
{
	bool bProcessingSucceeded = true;
	std::vector<std::string> errorMessages;
	std::vector<std::string> warningMessages;
	try
	{
		// fetch output texture path
		std::string sBakedTexturesPath = LPCTSTRToConstCharPtr( this->GetWorkDirectoryHandler()->GetBakedTexturesPath().c_str() );
		std::string sWorkDirectory = LPCTSTRToConstCharPtr( this->GetWorkDirectoryHandler()->GetWorkDirectory().c_str() );
		std::string sPipelineFilePath = LPCTSTRToConstCharPtr( Combine( this->GetWorkDirectoryHandler()->GetWorkDirectory(), _T("sgPipeline.json") ).c_str() );

		std::basic_string<TCHAR> tFinalExternalBatchPath = _T("");

		// if there is a environment path, use it
		std::basic_string<TCHAR> tEnvironmentPath = GetSimplygonEnvironmentVariable( SIMPLYGON_10_PATH );
		if( tEnvironmentPath.size() > 0 )
		{
			tFinalExternalBatchPath = tEnvironmentPath;
		}
		else
		{
			std::string sErrorMessage = "Invalid environment path: ";
			sErrorMessage += SIMPLYGON_10_PATH;
			throw std::exception( sErrorMessage.c_str() );
		}

		// setup Simplygon processing module
		SimplygonProcessingModule* processingModule = new SimplygonProcessingModule();
		processingModule->SetTextureOutputDirectory( sBakedTexturesPath );
		processingModule->SetWorkDirectory( sWorkDirectory );
		processingModule->SetProgressObserver( SimplygonInitInstance );
		processingModule->SetErrorHandler( SimplygonInitInstance );
		processingModule->SetExternalBatchPath( tFinalExternalBatchPath );

		// check if the pipeline is valid before saving
		if( this->sgPipeline.IsNull() )
		{
			std::string sErrorMessage = "Invalid pipeline.";
			throw std::exception( sErrorMessage.c_str() );
		}

		Simplygon::EPipelineRunMode runMode = Simplygon::EPipelineRunMode::RunInNewProcess;
		if( this->runInternally )
			runMode = Simplygon::EPipelineRunMode::RunInThisProcess;
		else if( this->runSimplygonGrid )
			runMode = Simplygon::EPipelineRunMode::RunDistributedUsingSimplygonGrid;
		else if( this->runIncredibuild )
			runMode = Simplygon::EPipelineRunMode::RunDistributedUsingIncredibuild;
		else if( this->runFastbuild )
			runMode = Simplygon::EPipelineRunMode::RunDistributedUsingFastbuild;

		const bool bSceneFromFile = this->extractionType == PROCESS_FROM_FILE;
		if( bSceneFromFile )
		{
			// original Simplygon scene from file
			std::string sInputSceneFile = CorrectPath( this->inputSceneFile.asChar() );
			std::string sOutputSceneFile = CorrectPath( this->outputSceneFile.asChar() );

			// start process with the given pipeline settings file
			std::vector<std::basic_string<TCHAR>> tOutputFileList =
			    processingModule->RunPipelineOnFile( sInputSceneFile, sOutputSceneFile, this->sgPipeline, runMode, errorMessages, warningMessages );

			this->GetMaterialInfoHandler()->AddProcessedSceneFiles( tOutputFileList );
		}
		else
		{
			// fetch original Simplygon scene
			const spScene sgOriginalScene = this->GetSceneHandler()->sgScene;

			// start process with the given pipeline settings file
			this->sceneHandler->sgProcessedScenes = processingModule->RunPipeline( sgOriginalScene, this->sgPipeline, runMode, errorMessages, warningMessages );
		}
	}
	catch( std::exception ex )
	{
		bProcessingSucceeded = false;
	}

	// Write errors and warnings to log.
	if( errorMessages.size() > 0 )
	{
		for( const auto& error : errorMessages )
		{
			this->LogErrorToWindow( error );
		}
	}
	if( warningMessages.size() > 0 )
	{
		for( const auto& warning : warningMessages )
		{
			this->LogWarningToWindow( warning );
		}
	}

	// if processing failed, cleanup and notify user.
	if( !bProcessingSucceeded )
	{
		this->Cleanup();
		return MStatus::kFailure;
	}

	return MStatus::kSuccess;
}

MStatus SimplygonCmd::RunPlugin( const MArgList& mArgList )
{
	MStatus mStatus = this->RegisterGlobalScripts();
	if( !mStatus )
		return mStatus;

	// parse and setup arguments
	this->LogToWindow( _T("Parsing command arguments...") );
	mStatus = this->ParseArguments( mArgList );
	if( !mStatus )
		return mStatus;

	// if regular run
	if( this->extractionType == BATCH_PROCESSOR )
	{
		// fix for Maya progress
		this->BeginProgress();
		this->EndProgress();

		this->BeginProgress();

		// register scripts
		this->LogToWindow( _T("Initial setup...") );

		if( SimplygonCmd::materialInfoHandler != nullptr )
		{
			delete SimplygonCmd::materialInfoHandler;
		}

		SimplygonCmd::materialInfoHandler = new MaterialInfoHandler();

		try
		{
			mStatus = this->PreExtract();
		}
		catch( const std::exception& ex )
		{
			mStatus = MStatus::kFailure;
			MGlobal::displayError( ( std::string( "PreExtract (BATCH_PROCESSOR): " ) + ex.what() ).c_str() );
		}

		if( !mStatus )
			return mStatus;

		// extract scene
		this->LogToWindow( _T("Extracting scene...") );

		try
		{
			mStatus = this->ExtractScene();
		}
		catch( const std::exception& ex )
		{
			mStatus = MStatus::kFailure;
			MGlobal::displayError( ( std::string( "ExtractScene (BATCH_PROCESSOR): " ) + ex.what() ).c_str() );
		}

		if( !mStatus )
			return mStatus;

		// process scene
		this->LogToWindow( _T("Executing Simplygon...") );
		this->SetCurrentProgressRange( 0, 100 );

		try
		{
			mStatus = this->ProcessScene();
		}
		catch( const std::exception& ex )
		{
			mStatus = MStatus::kFailure;
			MGlobal::displayError( ( std::string( "ProcessScene (BATCH_PROCESSOR): " ) + ex.what() ).c_str() );
		}

		if( mStatus )
		{
			// import processed scene(s)
			this->LogToWindow( _T("Importing scene(s)...") );
			try
			{
				mStatus = this->ImportScenes();
			}
			catch( const std::exception& ex )
			{
				mStatus = MStatus::kFailure;
				MGlobal::displayError( ( std::string( "ImportScenes (BATCH_PROCESSOR): " ) + ex.what() ).c_str() );
			}
		}

		// clear current selection
		MGlobal::clearSelectionList();

		// build new valid selection list
		MSelectionList mValidSelectionList;
		for( uint selectionIndex = 0; selectionIndex < this->InitialSelectionList.length(); ++selectionIndex )
		{
			MDagPath mTempDagPath;
			this->InitialSelectionList.getDagPath( selectionIndex, mTempDagPath );
			if( mTempDagPath.isValid() )
			{
				mValidSelectionList.add( mTempDagPath );
			}
		}

		// assign new selection to the scene
		if( mValidSelectionList.length() > 0 )
		{
			MGlobal::setActiveSelectionList( mValidSelectionList );
		}

		// if automatic clear flag is set,
		// clear global mapping data.
		if( this->clearGlobalMapping )
		{
			this->LogToWindow( _T("Clearing global mapping...") );
			this->ClearGlobalMapping();
		}

		this->LogToWindow( _T("Done!") );
		this->EndProgress();
	}

	else if( this->extractionType == PROCESS_FROM_FILE )
	{
		// fix for Maya progress
		this->BeginProgress();
		this->EndProgress();

		this->BeginProgress();

		// register scripts
		this->LogToWindow( _T("Initial setup...") );

		if( SimplygonCmd::materialInfoHandler != nullptr )
		{
			delete SimplygonCmd::materialInfoHandler;
		}

		SimplygonCmd::materialInfoHandler = new MaterialInfoHandler();

		try
		{
			mStatus = this->PreExtract();
		}
		catch( const std::exception& ex )
		{
			mStatus = MStatus::kFailure;
			MGlobal::displayError( ( std::string( "PreExtract (PROCESS_FROM_FILE): " ) + ex.what() ).c_str() );
		}

		if( !mStatus )
			return mStatus;

		// process scene
		this->LogToWindow( _T("Executing Simplygon...") );
		this->SetCurrentProgressRange( 0, 100 );

		try
		{
			mStatus = this->ProcessScene();
		}
		catch( const std::exception& ex )
		{
			mStatus = MStatus::kFailure;
			MGlobal::displayError( ( std::string( "ProcessScene (PROCESS_FROM_FILE): " ) + ex.what() ).c_str() );
		}

		if( !mStatus )
			return mStatus;

		// if automatic clear flag is set,
		// clear global mapping data.
		if( this->clearGlobalMapping )
		{
			this->LogToWindow( _T("Clearing global mapping...") );
			this->ClearGlobalMapping();
		}

		this->LogToWindow( _T("Done!") );
		this->EndProgress();
	}

	return mStatus;
}

MStatus SimplygonCmd::PreExtract()
{
	// early out in case of invalid pipeline
	if( this->sgPipeline.IsNull() )
	{
		std::string sErrorMessage =
		    "Invalid (or missing) settings pipeline, please specify a valid pipeline through \"sf\" (SettingsFile) or \"so\" (SettingsObject) flag.";
		this->LogErrorToWindow( sErrorMessage );
		return MStatus::kFailure;
	}

	// register the global MEL scripts used by Simplygon
	return MStatus::kSuccess;
}

void CollectSceneMeshes( spSceneNode sgNode, std::vector<spSceneMesh>& sgSceneMeshes )
{
	const uint numChildNodes = sgNode->GetChildCount();
	for( uint c = 0; c < numChildNodes; ++c )
	{
		spSceneNode sgSceneNode = sgNode->GetChild( c );

		// check if this is a mesh
		spSceneMesh sgMeshNode = Simplygon::spSceneMesh::SafeCast( sgSceneNode );
		if( !sgMeshNode.IsNull() )
		{
			sgSceneMeshes.push_back( sgMeshNode );
		}

		// look into the node as well
		CollectSceneMeshes( sgSceneNode, sgSceneMeshes );
	}
}

static void CopyNodeTransform( MFnTransform& mTransformFn, spSceneNode sgNode )
{
	MStatus mStatus;
	MMatrix mTransformation = mTransformFn.transformationMatrix( &mStatus );
	if( !mStatus )
		return;

	// MAssert(mStatus, "Failed to retrieve MMatrix");

	spMatrix4x4 sgRelativeTransform = sgNode->GetRelativeTransform();
	for( uint j = 0; j < 4; ++j )
	{
		for( uint i = 0; i < 4; ++i )
		{
			double d = sgRelativeTransform->GetElement( i, j );
			mTransformation( i, j ) = d;
		}
	}

	mTransformFn.set( mTransformation );
}

MStatus SimplygonCmd::ImportScenes()
{
	MStatus mStatus = MStatus::kSuccess;

	std::vector<spScene>& sgScenes = this->sceneHandler->sgProcessedScenes;

	// early out
	if( sgScenes.size() == 0 )
		return MStatus::kFailure;

	else if( SimplygonCmd::materialInfoHandler != nullptr )
	{
		delete SimplygonCmd::materialInfoHandler;
	}

	SimplygonCmd::materialInfoHandler = new MaterialInfoHandler();

	// create the new, modified, mesh data objects
	const size_t numProcessedGeometries = sgScenes.size();
	std::string tText = " - mesh ";
	std::string tSceneText = "Importing scene ";

	for( size_t physicalLodIndex = 0; physicalLodIndex < numProcessedGeometries; ++physicalLodIndex )
	{
		const size_t logicalLodIndex = physicalLodIndex + this->initialLodIndex;

		std::string tLogSceneMessage = tSceneText + std::to_string( logicalLodIndex );
		this->SetCurrentProcess( tLogSceneMessage.c_str() );

		// load the processed scene from file
		spScene sgProcessedScene = sgScenes[ physicalLodIndex ];

		this->SetCurrentProgressRange( 0, (int)this->GetSceneHandler()->sceneMeshes.size() );

		const float sceneRadius = (float)sgProcessedScene->GetRadius();
		const float storedSceneRadius = DataCollection::GetInstance()->SceneRadius;

		// store largest scene radius
		DataCollection::GetInstance()->SceneRadius = sceneRadius > storedSceneRadius ? sceneRadius : storedSceneRadius;

		std::vector<spSceneMesh> sgProcessedMeshes;
		const spSceneNode sgRootNode = sgProcessedScene->GetRootNode();
		CollectSceneMeshes( sgRootNode, sgProcessedMeshes );

		// import meshes
		std::map<std::string, MeshNode*> meshNodesThatNeedsParents;
		for( uint meshIndex = 0; meshIndex < sgProcessedMeshes.size(); ++meshIndex )
		{
			std::string tLogMessage = tSceneText + std::to_string( logicalLodIndex ) + tText + std::to_string( meshIndex + 1 );
			this->LogToWindow( tLogMessage, (int)meshIndex );

			MDagPath mDagPath;
			spSceneMesh sgProcessedSceneMesh = sgProcessedMeshes[ meshIndex ];
			spString cSgNodeGuid = sgProcessedSceneMesh->GetNodeGUID();

			// if mapMeshes is enabled, try to find mesh map
			MeshNode* newMeshNode = nullptr;
			if( this->mapMeshes )
			{
				// try to get global guid map
				std::map<std::string, std::string>::const_iterator& sgMeshToMayaMeshMap = this->s_GlobalMeshGuidToDagPath.find( cSgNodeGuid.c_str() );

				// if not found, use fallback if appropriate
				if( sgMeshToMayaMeshMap != this->s_GlobalMeshGuidToDagPath.end() )
				{
					// try to use name based search for mesh map
					MString mObjectToFind = sgMeshToMayaMeshMap->second.c_str();
					MObject mObject = MObject::kNullObj;

					// see if we can find a mapped mesh through guid mapping
					mStatus = GetMObjectOfNamedObject( mObjectToFind, mObject );

					// if yes, use it as reference
					if( mStatus )
					{
						MFnDagNode mObjectDagNode( mObject );

						MDagPath mObjectDagPath;
						mStatus = mObjectDagNode.getPath( mObjectDagPath );
						if( mStatus )
						{
							newMeshNode = new MeshNode( this, mObjectDagPath );
						}
						else
						{
							MGlobal::displayWarning( "ImportScenes(): Could not resolve mesh map through global guid mapping. Object seems to exist but "
							                         "returned an error while getting the dag path. Could this be caused by modifications to the original "
							                         "scene while not clearing global mapping? Trying to import mesh without mesh map..." );
							newMeshNode = new MeshNode( this );
						}
					}
					// if no, look after matching object name,
					// do not use fallback if BATCH_PROCESSOR
					else if( this->extractionType != BATCH_PROCESSOR )
					{
						MGlobal::displayWarning( "ImportScenes(): Could not find mesh map by global guid map." );

						mObjectToFind = sgProcessedSceneMesh->GetName();

						// see if we can find a mapped mesh by name
						mStatus = GetMObjectOfNamedObject( mObjectToFind, mObject );

						// if yes, use it as reference
						if( mStatus )
						{
							MFnDagNode mObjectDagNode( mObject );

							MDagPath mObjectDagPath;
							mStatus = mObjectDagNode.getPath( mObjectDagPath );
							if( mStatus )
							{
								newMeshNode = new MeshNode( this, mObjectDagPath );
							}
							else
							{
								MGlobal::displayWarning(
								    "ImportScenes(): Could not resolve mesh map by name. Object seems to exist but returned an error while getting the dag "
								    "path. Could this be caused by modifications to the original scene? Trying to import mesh without mesh map..." );
								newMeshNode = new MeshNode( this );
							}
						}
						// if no, ignore mesh map entirely, treat as new mesh
						else
						{
							MGlobal::displayWarning( "ImportScenes(): Could not find mesh map by name, treating as new mesh." );
							newMeshNode = new MeshNode( this );
						}
					}
					else
					{
						// if no global guid map was found,
						// handle as new mesh.
						newMeshNode = new MeshNode( this );
					}
				}
				else
				{
					// do not use fallback if BATCH_PROCESSOR
					if( this->extractionType != BATCH_PROCESSOR )
					{
						MGlobal::displayWarning( "ImportScenes(): Could not find mesh map by global guid map." );

						MString mObjectToFind = sgProcessedSceneMesh->GetName();
						MObject mObject = MObject::kNullObj;

						// see if we can find a mapped mesh by name
						mStatus = GetMObjectOfNamedObject( mObjectToFind, mObject );

						// if yes, use it as reference
						if( mStatus )
						{
							MFnDagNode mObjectDagNode( mObject );

							MDagPath mObjectDagPath;
							mStatus = mObjectDagNode.getPath( mObjectDagPath );
							if( mStatus )
							{
								newMeshNode = new MeshNode( this, mObjectDagPath );
							}
							else
							{
								MGlobal::displayWarning(
								    "ImportScenes(): Could not resolve mesh map by name. Object seems to exist but returned an error while getting the dag "
								    "path. "
								    "Could this be caused by modifications to the original scene? Trying to import mesh without mesh map..." );
								newMeshNode = new MeshNode( this );
							}
						}
						// if no, ignore mesh map entirely, treat as new mesh
						else
						{
							if( this->extractionType != BATCH_PROCESSOR )
							{
								MGlobal::displayWarning( "ImportScenes(): Could not find mesh map by name, trying to import mesh without mesh map..." );
							}

							newMeshNode = new MeshNode( this );
						}
					}
					else
					{
						// if no global guid map was found,
						// handle as new mesh.
						newMeshNode = new MeshNode( this );
					}
				}
			}
			else
			{
				newMeshNode = new MeshNode( this );
			}

			if( this->useQuadExportImport )
			{
				mStatus =
				    newMeshNode->WritebackGeometryData_Quad( sgProcessedScene, logicalLodIndex, sgProcessedSceneMesh, this->GetMaterialHandler(), mDagPath );
			}
			else
			{
				mStatus = newMeshNode->WritebackGeometryData( sgProcessedScene, logicalLodIndex, sgProcessedSceneMesh, this->GetMaterialHandler(), mDagPath );
			}

			if( !mStatus )
			{
				MGlobal::displayError( "ImportScenes(): WritebackGeometryData failed." );
				return mStatus;
			}

			else if( mDagPath.isValid() )
			{
				this->appendToResult( mDagPath.fullPathName() );
			}

			meshNodesThatNeedsParents.insert( std::pair<std::string, MeshNode*>( cSgNodeGuid, newMeshNode ) );
		}

		// for unmapped meshes, copy transformation and link parent(s)
		for( std::map<std::string, MeshNode*>::iterator& meshIterator = meshNodesThatNeedsParents.begin(); meshIterator != meshNodesThatNeedsParents.end();
		     meshIterator++ )
		{
			MeshNode* meshNodeMap = meshIterator->second;

			if( meshNodeMap->postUpdate )
			{
				// fetch processed Simplygon mesh
				spSceneNode sgMesh = sgProcessedScene->GetNodeByGUID( meshIterator->first.c_str() );
				if( sgMesh.IsNull() )
					continue;

				// copy transformation from processed Simplygon mesh to Maya mesh
				MFnTransform mModifiedTransformation( meshNodeMap->GetModifiedTransform() );
				CopyNodeTransform( mModifiedTransformation, sgMesh );

				// fetch processed parent mesh
				spSceneNode sgParent = sgMesh->GetParent();
				if( sgParent.IsNull() )
					continue;

				// fetch mesh map for parent mesh
				std::map<std::string, MeshNode*>::iterator& parentMeshMap = meshNodesThatNeedsParents.find( sgParent->GetNodeGUID().c_str() );
				if( parentMeshMap == meshNodesThatNeedsParents.end() )
					continue;

				// link parent
				MObject mParentObject = parentMeshMap->second->GetModifiedTransform();
				MFnDagNode mModifiedParentDagNode( mParentObject );

				// std::string sPath = mModifiedParentDagNode.fullPathName().asChar();

				MObject& mTransform = meshNodeMap->GetModifiedTransform();
				MFnDagNode mModifiedDagNode( mTransform );

				// do not add child / parent if it already exists
				if( !mModifiedParentDagNode.hasChild( mTransform ) )
				{
					if( !mModifiedDagNode.hasParent( mParentObject ) )
					{
						mModifiedParentDagNode.addChild( mTransform );
					}
				}
			}
		}
	}

	ExecuteCommand( MString( "select -cl;" ) );

	return mStatus;
}

MStatus SimplygonCmd::RemoveLODMeshes()
{
	MStatus mStatus = MStatus::kSuccess;

	for( size_t meshIndex = 0; meshIndex < this->sceneHandler->sceneMeshes.size(); ++meshIndex )
	{
		const MayaSgNodeMapping* mayaSgNodeMap = &this->sceneHandler->sceneMeshes[ meshIndex ];
		if( mayaSgNodeMap )
		{
			mStatus = mayaSgNodeMap->mayaNode->DeleteModifiedMeshDatas();
			if( !mStatus )
				return mStatus;
		}
	}

	return mStatus;
}

#pragma region GetHandlers
Scene* SimplygonCmd::GetSceneHandler()
{
	return this->sceneHandler;
}

MaterialHandler* SimplygonCmd::GetMaterialHandler()
{
	return this->materialHandler;
}

MaterialInfoHandler* SimplygonCmd::GetMaterialInfoHandler()
{
	return SimplygonCmd::materialInfoHandler;
}

WorkDirectoryHandler* SimplygonCmd::GetWorkDirectoryHandler()
{
	return this->workDirectoryHandler;
}

#pragma endregion

bool SimplygonCmd::GetMergeIdenticallySetupMaterials()
{
	return !this->noMaterialMerging;
}

bool SimplygonCmd::SkipBlendShapeWeightPostfix()
{
	return this->skipBlendShapePostfix;
}

bool SimplygonCmd::UseCurrentPoseAsBindPose()
{
	return this->useCurrentPoseAsBindPose;
}

bool SimplygonCmd::UseOldSkinningMethod()
{
	return this->useOldSkinningMethod;
}

bool SimplygonCmd::DoNotGenerateMaterials()
{
	return this->doNotGenerateMaterial;
}

static int GetSettingsStringIndex( std::vector<std::string>& sSettingsStrings, const char* cName, std::string* sDestinationString = nullptr )
{
	std::string sNameString( cName );
	for( int i = 0; i < (int)sSettingsStrings.size(); ++i )
	{
		std::string sSettingsString = sSettingsStrings[ i ];

		// look for the '=' sign
		const std::string::size_type v = sSettingsString.find( '=' );
		if( v != std::string::npos )
		{
			std::string path = TrimSpaces( sSettingsString.substr( 0, v ) );
			std::string value = TrimSpaces( sSettingsString.substr( v + 1 ) );

			if( path == sNameString )
			{
				// this is our setting
				if( sDestinationString != nullptr )
				{
					( *sDestinationString ) = value;
				}
				return i;
			}
		}
	}
	return -1;
}

spShadingNode FindUpstreamNodeByName( spShadingNode sgShadingNode, std::basic_string<TCHAR> tNodeName );
static spShadingNode FindUpstreamNodeByName( spShadingNode sgShadingNode, std::basic_string<TCHAR> tNodeName )
{
	if( sgShadingNode.IsNull() )
		return Simplygon::NullPtr;

	if( strcmp( sgShadingNode->GetName(), tNodeName.c_str() ) == 0 )
	{
		return sgShadingNode;
	}

	spShadingFilterNode sgShadingFilterNode = Simplygon::spShadingFilterNode::SafeCast( sgShadingNode );
	if( !sgShadingFilterNode.IsNull() )
	{
		for( uint i = 0; i < sgShadingFilterNode->GetParameterCount(); ++i )
		{
			if( sgShadingFilterNode->GetParameterIsInputable( i ) )
			{
				if( !sgShadingFilterNode->GetInput( i ).IsNull() )
				{
					spShadingNode sgUpstreamNode = FindUpstreamNodeByName( sgShadingFilterNode->GetInput( i ), tNodeName );
					if( !sgUpstreamNode.IsNull() )
						return sgUpstreamNode;
				}
			}
		}
	}
	return Simplygon::NullPtr;
}

Globals::Globals()
{
}

Globals::~Globals()
{
}

void Globals::Lock()
{
	uiLock.Enter();
}

void Globals::UnLock()
{
	uiLock.Leave();
}

UIHookHelper::UIHookHelper()
{
	this->updateThreadHandle = nullptr;
	this->killUpdateThread = false;
}

UIHookHelper::~UIHookHelper()
{
	this->killUpdateThread = true;
	// TerminateThread(m_UpdateThread, 0);
	WaitForSingleObject( updateThreadHandle, INFINITE );
	CloseHandle( updateThreadHandle );
	this->updateThreadHandle = nullptr;
}

void UIHookHelper::RegisterUICallback()
{
	// if poller not started, start it
	if( this->updateThreadHandle == nullptr )
	{
		DWORD dwThreadId;
		this->updateThreadHandle = CreateThread( nullptr, 0, (unsigned long( __stdcall* )( void* ))UIHookHelper::theFunction, this, 0, &dwThreadId );
		if( this->updateThreadHandle == nullptr )
		{
			// failed
		}
	}
}

void UIHookHelper::ReadPresets( bool loop /*= true*/ )
{
	while( !this->killUpdateThread )
	{
		uiGlobals.Lock();

		std::vector<std::basic_string<TCHAR>> tPresets;
		for( std::basic_string<TCHAR> tPreset : tPresets )
		{
			bool isPreset = false;
			if( tPreset.find( ".preset" ) != std::string::npos )
				isPreset = true;

			if( isPreset )
			{
				MString mResult = tPreset.c_str();
			}
		}

		uiGlobals.UnLock();
		if( loop )
		{
			for( int i = 0; i < 100; ++i )
			{
				if( !this->killUpdateThread )
				{
					Sleep( 100 );
				}
				else
				{
					break;
				}
			}
		}
		else
		{
			break;
		}
	}
}

unsigned long __stdcall UIHookHelper::theFunction( void* v )
{
	UIHookHelper* c = (UIHookHelper*)v;
	if( c )
	{
		try
		{
			c->ReadPresets();
		}
		catch( ... )
		{
		}
	}
	return 0;
}
