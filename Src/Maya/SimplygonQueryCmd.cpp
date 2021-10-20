// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"

#include "SimplygonInit.h"
#include "SimplygonQueryCmd.h"
#include "SimplygonCmd.h"
#include "DataCollection.h"

#include "maya/MArgDatabase.h"
#include "maya/MGlobal.h"
#include "maya/MSelectionList.h"
#include "maya/MDagPath.h"
#include "maya/MFnDependencyNode.h"
#include "maya/MPlug.h"
#include "maya/M3dView.h"
#include "maya/MFnCamera.h"

extern SimplygonInitClass* SimplygonInitInstance;

// SimplygonQuery
const char* cGetLODSwitchDistance = "-lsd";      // 5.3
const char* cGetPixelSize = "-ps";               // 5.3
const char* cGetLODSwitchDistanceAtFOV = "-gsd"; // 6.2+ and 7.0
const char* cSetScreenSize = "-sss";
const char* cGetScreenSize = "-gss";

const char* cGetMaterials = "-gm";              // 7.0+
const char* cGetChannelsForMaterial = "-gcm";   // 7.0+
const char* cGetTexturePathForChannel = "-gtc"; // 7.0+
const char* cGetProcessedMeshes = "-gpm";       // 7.0+
const char* cSelectProcessedMeshes = "-spm";    // 9.0+
const char* cGetMaterialIdsForMesh = "-gmi";    // 8.2+
const char* cMeshReusesMaterial = "-mrm";       // 7.0+
const char* cMeshReusesMaterials = "-rms";      // 8.2+
const char* cGetMaterialForMesh = "-gmm";       // 7.0+
const char* cGetMaterialsForMesh = "-mfm";      // 8.2+

const char* cGetProcessedOutputPaths = "-gpp"; // 9.0

double SimplygonQueryCmd::GetLodSwitchDistanceAtFOV( double fov )
{
	double distance = -1.0; // future return value
	MSelectionList mSelectedObjects;

	// loop selected objects
	MGlobal::getActiveSelectionList( mSelectedObjects );
	for( uint pathIndex = 0; pathIndex < mSelectedObjects.length(); ++pathIndex )
	{
		MDagPath mDagPath;
		mSelectedObjects.getDagPath( pathIndex, mDagPath );

		MDagPath mShapePath( mDagPath );
		MString mPath = mShapePath.fullPathName();

		// conditions
		if( !mShapePath.extendToShape() )
		{
			return -1.0;
		}

		else if( !mShapePath.hasFn( MFn::kMesh ) )
		{
			return -1.0;
		}

		else if( mDagPath.hasFn( MFn::kTransform ) )
		{
			// this is the node we are looking for,
			// try to fetch parameters
			MFnDependencyNode mDepNode( mShapePath.node() );

			const uint attribCount = mDepNode.attributeCount();
			MObject mAttrObj = MObject::kNullObj;

			bool hasMaxDeviation = mDepNode.hasAttribute( "MaxDeviation" );
			bool hasSceneRadius = mDepNode.hasAttribute( "SceneRadius" );
			if( hasMaxDeviation && hasSceneRadius )
			{
				// get MaxDeviation
				double maxDeviation = 0.0;
				{
					MPlug mPlug = mDepNode.findPlug( "MaxDeviation" );
					const MStatus status = mPlug.getValue( maxDeviation );

					if( status != MStatus::kSuccess )
						continue;
				}

				// get SceneRadius
				double sceneRadius = 0.0;
				{
					MPlug mPlug = mDepNode.findPlug( "SceneRadius" );
					const MStatus mStatus = mPlug.getValue( sceneRadius );

					if( mStatus != MStatus::kSuccess )
						continue;
				}

				distance = TranslateDeviationToDistance( sceneRadius, maxDeviation, ( fov * ( PI / 180.0f ) ), this->ScreenSize );
				continue;
			}

			// 			MFnMesh mesh(shapePath.node());
			// 			path = mesh.name();
			// 			uint numVertices = mesh.numVertices();
		}
	}

	return distance;
}

double SimplygonQueryCmd::GetLodSwitchDistance( double radius, int pixelsize )
{
	M3dView curView = M3dView::active3dView();
	const int screenheight = curView.portHeight();
	const int screenwidth = curView.portWidth();

	double fov_x;
	double fov_y;
	MDagPath curCamPath;

	curView.getCamera( curCamPath );

	MObject camObj = curCamPath.node();
	MFnCamera curCam( camObj );

	curCam.getPortFieldOfView( screenwidth, screenheight, fov_x, fov_y );

	const double screen_ratio = (double)pixelsize / (double)screenheight;

	// normalized distance to the “screen” if the height of the screen is 1.
	const double normalized_distance = 1.0 / ( tan( DEG2RAD( fov_y / 2 ) ) );

	// the view-angle of the bounding sphere rendered onscreen.
	const double bsphere_angle = atan( screen_ratio / normalized_distance );

	// The distance in real world units from the camera to the center of the bounding sphere. Not to be confused with normalized distance
	const double distance = radius / sin( bsphere_angle );

	return distance;
}

double SimplygonQueryCmd::GetPixelSize( double radius, double distance )
{
	M3dView curView = M3dView::active3dView();
	const int screenheight = curView.portHeight();
	const int screenwidth = curView.portWidth();

	double fov_x;
	double fov_y;

	MDagPath curCamPath;

	curView.getCamera( curCamPath );

	MObject camObj = curCamPath.node();
	MFnCamera curCam( camObj );

	curCam.getPortFieldOfView( screenwidth, screenheight, fov_x, fov_y );

	// the view-angle of the bounding sphere rendered onscreen.
	const double bsphere_angle = asin( radius / distance );

	// this assumes the near clipping plane is at a distance of 1. Normalized screen height of the geometry
	const double geom_view_height = tan( bsphere_angle );

	// the size of (half) the screen if the near clipping plane is at a distance of 1
	const double screen_view_height = ( tan( DEG2RAD( fov_y / 2 ) ) );

	// the ratio of the geometry's screen size compared to the actual size of the screen
	const double view_ratio = geom_view_height / screen_view_height;

	// multiply by the number of pixels on the screen
	const int pixel_size = int( view_ratio * screenheight );

	return pixel_size;
}

MStatus SimplygonQueryCmd::doIt( const MArgList& mArgs )
{
	if( sg == nullptr )
	{
		const bool bInitialized = SimplygonInitInstance->Initialize();
		if( !bInitialized )
		{
			return MStatus::kFailure;
		}
	}

	MStatus mStatus = MS::kSuccess;
	MArgDatabase mArgData( syntax(), mArgs );

	const bool bHasMaterialInfoHandler = SimplygonCmd::GetMaterialInfoHandler() != nullptr;

	// if we are getting the Simplygon version
	if( mArgData.isFlagSet( "-ver" ) )
	{
		this->setResult( MString( sg->GetVersion() ) );
		return mStatus;
	}

	if( mArgData.isFlagSet( "-gr" ) )
	{
		this->setResult( DataCollection::GetInstance()->SceneRadius );
		return mStatus;
	}

	if( mArgData.isFlagSet( cGetPixelSize ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cGetPixelSize );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cGetPixelSize, i, mArgList );
			if( !mStatus )
				return mStatus;

			const double Radius = mArgList.asDouble( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			const double CameraDistance = mArgList.asDouble( 1, &mStatus );
			if( !mStatus )
				return mStatus;

			const int pixelSize = (int)this->GetPixelSize( Radius, CameraDistance );

			clearResult();
			MString mResult;
			mResult += "PixelSize:";
			mResult += pixelSize;
			appendToResult( mResult );
		}
	}

	// retrieve all material texture overrides
	if( mArgData.isFlagSet( cGetLODSwitchDistance ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cGetLODSwitchDistance );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cGetLODSwitchDistance, i, mArgList );
			if( !mStatus )
				return mStatus;

			const int PixelSize = mArgList.asInt( 1, &mStatus );
			if( !mStatus )
				return mStatus;

			const double Radius = mArgList.asDouble( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			const double distance = this->GetLodSwitchDistance( Radius, PixelSize );

			clearResult();
			MString mResult;
			mResult += "LODSwitchDistance:";
			mResult += distance;
			appendToResult( mResult );
		}
	}

	if( mArgData.isFlagSet( cSetScreenSize ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cSetScreenSize );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cSetScreenSize, i, mArgList );
			if( !mStatus )
				return mStatus;

			const int screenSize = mArgList.asInt( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			this->SetScreenSize( screenSize );

			clearResult();
			MString mResult;
			mResult += "ScreenSize:";
			mResult += screenSize;
			appendToResult( mResult );
		}
	}

	if( mArgData.isFlagSet( cGetPixelSize ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cGetPixelSize );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cGetPixelSize, i, mArgList );
			if( !mStatus )
				return mStatus;

			const int screenSize = mArgList.asInt( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			this->SetScreenSize( screenSize );

			clearResult();
			this->setResult( screenSize );
		}
	}

	if( mArgData.isFlagSet( cGetLODSwitchDistanceAtFOV ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cGetLODSwitchDistanceAtFOV );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cGetLODSwitchDistanceAtFOV, i, mArgList );
			if( !mStatus )
				return mStatus;

			const double fov = mArgList.asDouble( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			const double distance = this->GetLodSwitchDistanceAtFOV( fov );

			clearResult();
			this->setResult( distance );
		}
	}

	if( mArgData.isFlagSet( cGetProcessedOutputPaths ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cGetProcessedOutputPaths );
		if( flagCount )
		{
			std::vector<std::basic_string<TCHAR>> tOutputList = SimplygonCmd::GetMaterialInfoHandler()->GetProcessedSceneFiles();

			clearResult();
			for( std::basic_string<TCHAR> tPath : tOutputList )
			{
				MString mPath = tPath.c_str();
				appendToResult( mPath );
			}
		}
	}

	if( mArgData.isFlagSet( cGetMaterials ) )
	{
		const std::vector<std::basic_string<TCHAR>>& tMaterialList =
		    bHasMaterialInfoHandler ? SimplygonCmd::GetMaterialInfoHandler()->GetMaterialsWithCustomChannels() : std::vector<std::basic_string<TCHAR>>();

		clearResult();
		MStringArray mResult;
		for( uint m = 0; m < tMaterialList.size(); ++m )
		{
			mResult.append( tMaterialList[ m ].c_str() );
		}

		appendToResult( mResult );
	}

	if( mArgData.isFlagSet( cGetChannelsForMaterial ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cGetChannelsForMaterial );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cGetChannelsForMaterial, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMaterialName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			const std::vector<std::basic_string<TCHAR>>& tMaterialChannels =
			    bHasMaterialInfoHandler ? SimplygonCmd::GetMaterialInfoHandler()->GetCustomChannelsForMaterial( mMaterialName.asChar() )
			                            : std::vector<std::basic_string<TCHAR>>();

			clearResult();
			MStringArray mResult;
			for( uint m = 0; m < tMaterialChannels.size(); ++m )
			{
				mResult.append( tMaterialChannels[ m ].c_str() );
			}

			appendToResult( mResult );
		}
	}

	if( mArgData.isFlagSet( cGetTexturePathForChannel ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cGetTexturePathForChannel );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cGetTexturePathForChannel, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMaterialName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			MString mMaterialChannelName = mArgList.asString( 1, &mStatus );
			if( !mStatus )
				return mStatus;

			const std::basic_string<TCHAR> tTexturePath =
			    bHasMaterialInfoHandler
			        ? SimplygonCmd::GetMaterialInfoHandler()->GetTextureNameForMaterialChannel( mMaterialName.asChar(), mMaterialChannelName.asChar() )
			        : _T("");

			clearResult();
			setResult( tTexturePath.c_str() );
		}
	}

	if( mArgData.isFlagSet( cGetProcessedMeshes ) )
	{
		const std::vector<std::basic_string<TCHAR>>& tMeshes =
		    bHasMaterialInfoHandler ? SimplygonCmd::GetMaterialInfoHandler()->GetMeshes() : std::vector<std::basic_string<TCHAR>>();

		clearResult();
		MStringArray mResult;
		for( uint m = 0; m < tMeshes.size(); ++m )
		{
			mResult.append( tMeshes[ m ].c_str() );
		}

		appendToResult( mResult );
	}

	if( mArgData.isFlagSet( cGetMaterialIdsForMesh ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cGetMaterialIdsForMesh );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cGetMaterialIdsForMesh, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMeshName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			MStringArray mMaterialIds;
			const std::vector<int>& mayaMaterialIds =
			    bHasMaterialInfoHandler ? SimplygonCmd::GetMaterialInfoHandler()->GetMeshMaterialIds( mMeshName.asChar() ) : std::vector<int>();

			mMaterialIds.setLength( (uint)mayaMaterialIds.size() );
			for( uint m = 0; m < mayaMaterialIds.size(); ++m )
			{
				mMaterialIds[ m ] = mayaMaterialIds[ m ];
			}

			clearResult();
			setResult( mMaterialIds );
		}
	}

	if( mArgData.isFlagSet( cMeshReusesMaterial ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cMeshReusesMaterial );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cMeshReusesMaterial, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMeshName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			const std::basic_string<TCHAR> tMaterialName =
			    bHasMaterialInfoHandler ? SimplygonCmd::GetMaterialInfoHandler()->MeshReusesMaterial( mMeshName.asChar() ) : _T("");

			clearResult();
			setResult( tMaterialName.c_str() );
		}
	}

	if( mArgData.isFlagSet( cMeshReusesMaterials ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cMeshReusesMaterials );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cMeshReusesMaterials, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMeshName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			MStringArray mReusedMaterials;
			const std::vector<std::basic_string<TCHAR>>& tMayaReusedMaterials =
			    bHasMaterialInfoHandler ? SimplygonCmd::GetMaterialInfoHandler()->MeshReusesMaterials( mMeshName.asChar() )
			                            : std::vector<std::basic_string<TCHAR>>();

			mReusedMaterials.setLength( (uint)tMayaReusedMaterials.size() );
			for( uint m = 0; m < tMayaReusedMaterials.size(); ++m )
			{
				mReusedMaterials[ m ] = tMayaReusedMaterials[ m ].c_str();
			}

			clearResult();
			setResult( mReusedMaterials );
		}
	}

	if( mArgData.isFlagSet( cGetMaterialForMesh ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cGetMaterialForMesh );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cGetMaterialForMesh, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMeshName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			const std::basic_string<TCHAR> tMaterial =
			    bHasMaterialInfoHandler ? SimplygonCmd::GetMaterialInfoHandler()->GetMaterialForMesh( mMeshName.asChar() ) : _T("");

			clearResult();
			setResult( tMaterial.c_str() );
		}
	}

	if( mArgData.isFlagSet( cGetMaterialsForMesh ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cGetMaterialsForMesh );
		for( uint i = 0; i < flagCount; ++i )
		{
			MStringArray mMaterialList;

			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cGetMaterialsForMesh, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mMeshName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			const std::vector<std::basic_string<TCHAR>>& tMaterialList = bHasMaterialInfoHandler
			                                                                 ? SimplygonCmd::GetMaterialInfoHandler()->GetMaterialsForMesh( mMeshName.asChar() )
			                                                                 : std::vector<std::basic_string<TCHAR>>();

			mMaterialList.setLength( (uint)tMaterialList.size() );
			for( uint m = 0; m < tMaterialList.size(); ++m )
			{
				mMaterialList[ m ] = tMaterialList[ m ].c_str();
			}

			clearResult();
			setResult( mMaterialList );
		}
	}

	if( mArgData.isFlagSet( cSelectProcessedMeshes ) )
	{
		const std::vector<std::basic_string<TCHAR>>& tMeshes =
		    bHasMaterialInfoHandler ? SimplygonCmd::GetMaterialInfoHandler()->GetMeshes() : std::vector<std::basic_string<TCHAR>>();

		clearResult();
		MSelectionList mProcessedMeshesList;

		for( const auto m : tMeshes )
		{
			mProcessedMeshesList.add( m.c_str() );
		}

		mStatus = MGlobal::selectCommand( mProcessedMeshesList );
		setResult( mProcessedMeshesList.length() > 0 ? true : false );
	}

	return mStatus;
}

void* SimplygonQueryCmd::creator()
{
	return new SimplygonQueryCmd();
}

MSyntax SimplygonQueryCmd::createSyntax()
{
	MStatus mStatus;
	MSyntax mSyntax;
	mStatus = mSyntax.addFlag( "-get", "-GetSetting", MSyntax::kString );

	mStatus = mSyntax.addFlag( "-ver", "-version" );
	mStatus = mSyntax.addFlag( "-ver", "-Version" );

	mStatus = mSyntax.addFlag( "-gr", "-GetRadius" );

	mStatus = mSyntax.addFlag( cGetLODSwitchDistance, "-GetLODSwitchDistance", MSyntax::kDouble, MSyntax::kDouble );
	mStatus = mSyntax.addFlag( cGetPixelSize, "-GetPixelSize", MSyntax::kLong, MSyntax::kDouble );
	mStatus = mSyntax.addFlag( cGetLODSwitchDistanceAtFOV, "-GetLODSwitchDistanceAtFOV", MSyntax::kDouble );
	mStatus = mSyntax.addFlag( cGetScreenSize, "-GetScreenSize" );
	mStatus = mSyntax.addFlag( cSetScreenSize, "-SetScreenSize", MSyntax::kLong );

	mStatus = mSyntax.addFlag( cGetProcessedOutputPaths, "-GetProcessedOutputPaths" );
	mStatus = mSyntax.makeFlagMultiUse( cGetProcessedOutputPaths );

	mStatus = mSyntax.addFlag( cGetProcessedMeshes, "-GetProcessedMeshes" );
	mStatus = mSyntax.makeFlagMultiUse( cGetProcessedMeshes );

	mStatus = mSyntax.addFlag( cSelectProcessedMeshes, "-SelectProcessedMeshes" );
	mStatus = mSyntax.makeFlagMultiUse( cSelectProcessedMeshes );

	mStatus = mSyntax.addFlag( cGetMaterialForMesh, "-GetMaterialForMesh", MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cGetMaterialForMesh );

	mStatus = mSyntax.addFlag( cGetMaterialsForMesh, "-GetMaterialsForMesh", MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cGetMaterialsForMesh );

	mStatus = mSyntax.addFlag( cMeshReusesMaterial, "-MeshReusesMaterial", MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cMeshReusesMaterial );

	mStatus = mSyntax.addFlag( cMeshReusesMaterials, "-MeshReusesMaterials", MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cMeshReusesMaterials );

	mStatus = mSyntax.addFlag( cGetMaterialIdsForMesh, "-GetMaterialIdsForMesh", MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cGetMaterialIdsForMesh );

	mStatus = mSyntax.addFlag( cGetMaterials, "-GetMaterials" );
	mStatus = mSyntax.makeFlagMultiUse( cGetMaterials );

	mStatus = mSyntax.addFlag( cGetChannelsForMaterial, "-GetChannelsForMaterial", MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cGetChannelsForMaterial );

	mStatus = mSyntax.addFlag( cGetTexturePathForChannel, "-GetTexturePathForChannel", MSyntax::kString, MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cGetTexturePathForChannel );

	return mSyntax;
}

void SimplygonQueryCmd::SetScreenSize( int screenSize )
{
	this->ScreenSize = screenSize;
}

int SimplygonQueryCmd::GetScreenSize()
{
	return this->ScreenSize;
}

SimplygonQueryCmd::SimplygonQueryCmd()
{
	this->ScreenSize = 1024;
}

SimplygonQueryCmd::~SimplygonQueryCmd()
{
}
