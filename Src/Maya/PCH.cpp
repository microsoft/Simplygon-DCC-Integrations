// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"
//#include "..\CloudBatch\PluginCPP\Common.h"
#include <WTypes.h>
#include <iomanip>
#include <SimplygonInit.h>

using namespace std;
using namespace Simplygon;

extern ISimplygon* sg;
extern SimplygonInitClass* SimplygonInitInstance;

vector<string> SimplygonProcessAdditionalSearchPaths;

bool IsSamePath( const char* cPath1, const char* cPath2 )
{
	uint counter = 0;
	const char forwardslash = '/';
	const char backslash = '\\';

	const size_t len1 = strlen( cPath1 );
	const size_t len2 = strlen( cPath2 );

	if( len1 != len2 )
	{
		return false;
	}

	for( counter = 0; counter < len1; ++counter )
	{
		if( tolower( cPath1[ counter ] ) != tolower( cPath2[ counter ] ) )
		{
			if( cPath1[ counter ] == forwardslash && cPath2[ counter ] == backslash )
			{
				continue;
			}
			else if( cPath2[ counter ] == forwardslash && cPath1[ counter ] == backslash )
			{
				continue;
			}
			else
			{
				return false;
			}
		}
	}

	return true;
}

MStatus GetPathToNamedObject( MString mName, MDagPath& mDagPath )
{
	MSelectionList mSelectionList;
	if( !mSelectionList.add( mName ) )
	{
		return MStatus::kFailure;
	}
	if( !mSelectionList.getDagPath( 0, mDagPath ) )
	{
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus getFloat3PlugValue( MPlug mPlug, MFloatVector& mFloatVector )
{
	// Retrieve the value as an MObject
	//
	MObject mObject;
	mPlug.getValue( mObject );

	// Convert the MObject to a float3
	//
	MFnNumericData mNumData( mObject );
	mNumData.getData( mFloatVector[ 0 ], mFloatVector[ 1 ], mFloatVector[ 2 ] );
	return MStatus::kSuccess;
}

MStatus getFloat3asMObject( MFloatVector mFloatVector, MObject& mObject )
{
	// Convert the float value into an MObject
	//
	MFnNumericData numDataFn;
	mObject = numDataFn.create( MFnNumericData::k3Float );
	numDataFn.setData( mFloatVector[ 0 ], mFloatVector[ 1 ], mFloatVector[ 2 ] );
	return MStatus::kSuccess;
}

MStatus ExecuteCommand( MString mCommand )
{
	if( !MGlobal::executeCommand( mCommand, false ) )
	{
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus ExecuteCommand( MString mCommand, MString& mDestination )
{
	if( !MGlobal::executeCommand( mCommand, mDestination, false ) )
	{
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus ExecuteCommand( MString mCommand, MStringArray& mDestination )
{
	if( !MGlobal::executeCommand( mCommand, mDestination, false ) )
	{
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

// Execute the command and get the resulting boolean value
MStatus ExecuteCommand( MString mCommand, bool& bResult )
{
	MCommandResult mCommandResult;
#ifdef PRINT_DEBUG_INFO
	if( !MGlobal::executeCommand( mCommand, mCommandResult, true, true ) )
#else
	if( !MGlobal::executeCommand( mCommand, mCommandResult, false, false ) )
#endif
	{
		bResult = false;
		return MStatus::kFailure;
	}

	// Did the executed command return true or false?
	int bValue = 0;
	mCommandResult.getResult( bValue );
	if( bValue == 1 )
	{
		bResult = true;
	}
	else
	{
		bResult = false;
	}
	return MStatus::kSuccess;
}

void SelectDAGPath( const MDagPath& mDagPath, bool bAddToSelectionList )
{
	if( bAddToSelectionList )
	{
		MGlobal::select( mDagPath, MObject::kNullObj, MGlobal::kAddToList );
	}
	else
	{
		MGlobal::select( mDagPath, MObject::kNullObj, MGlobal::kReplaceList );
	}
}

// This function removes any node below this node, which is NOT a shape node
MStatus RemoveAllNonMeshShapeSubNodes( MDagPath mDagPath )
{
	// unselect all
	bool bHasSelection = false;

	// look through all child nodes
	for( uint i = 0; i < mDagPath.childCount(); ++i )
	{
		// get the path to the child
		MDagPath mChildDagpath = mDagPath;
		mChildDagpath.push( mDagPath.child( i ) );

		// check the type
		const MFn::Type mChildType = mChildDagpath.apiType();

		// HACK:: Orignallly was only removing object if they were not meshes.
		// if meshes are praented. then this removes child element. therefore checking for transform
		if( mChildType != MFn::kMesh && mChildType != MFn::kTransform )
		{
			// select it, we will remove it
			SelectDAGPath( mChildDagpath, bHasSelection );
			bHasSelection = true;
		}
	}

	// delete the selected objects
	if( bHasSelection )
	{
		MStringArray mReturnList;
		if( !ExecuteCommand( "delete;", mReturnList ) )
		{
			return MStatus::kFailure;
		}
	}

	return MStatus::kSuccess;
}

// This function duplicates a node with its shape, and makes sure to remove
// any other node that is below the duplicate
MStatus DuplicateNodeWithShape( MDagPath mDagPath, MDagPath& mResultingDagPath, MStringArray* mResultList, MString mDupName, bool bAlternativeDuplication )
{
	MDagPath mShapeNode = mDagPath;
	if( mShapeNode.extendToShape() )
	{
		bool bIsSelected = MGlobal::select( (const MDagPath&)mDagPath, MObject::kNullObj, MGlobal::kReplaceList );
		if( bAlternativeDuplication )
		{
			bIsSelected &= MGlobal::select( (const MDagPath&)mShapeNode, MObject::kNullObj, MGlobal::kAddToList );
		}

		// select the node and mesh to duplicate
		if( bIsSelected )
		{
			MStringArray mReturnList;

			MString mCommand = "duplicate -rc -ic";
			if( bAlternativeDuplication )
			{
				mCommand += " -po";
			}
			if( mDupName != "" )
			{
				mCommand += "-n ";
				mCommand += mDupName;
			}
			mCommand += ";";

			if( ::ExecuteCommand( mCommand, mReturnList ) )
			{
				MStatus mStatus = MStatus::kFailure;

				// we should assume the returned list is at least 2 items long. get the returned item as a
				// dag path, and the rest as a selection list, if wanted

				// we have the duplicate, get its DAG path
				if( mReturnList.length() > 1 )
				{
					// try the second item first
					mStatus = ::GetPathToNamedObject( mReturnList[ 1 ], mResultingDagPath );
				}
				if( !mStatus )
				{
					// failed with the second item, use the first
					mStatus = ::GetPathToNamedObject( mReturnList[ 0 ], mResultingDagPath );
				}

				MSanityCheck( mStatus );

				if( mStatus )
				{
					// remove all childnodes that are not mesh shapes
					if( ::RemoveAllNonMeshShapeSubNodes( mResultingDagPath ) )
					{
						// get all other returned nodes into a selection list
						if( mResultList != NULL )
						{
							mResultList->clear();
							for( uint i = 2; i < mReturnList.length(); ++i )
							{
								mResultList->append( mReturnList[ i ] );
							}
						}

						return MStatus::kSuccess;
					}
				}
			}
		}
	}

	return MStatus::kFailure;
}

MStatus ExecuteSelectedObjectCommand( const MString& mCommand, const MDagPath& mDagPath, const MObject& mComponent, MStringArray& mDestination )
{
	MStatusAssert( mDagPath.isValid(), "ExecuteSelectedObjectCommand: invalid node" );
	if( !MGlobal::select( mDagPath, mComponent, MGlobal::kReplaceList ) )
	{
		return MStatus::kFailure;
	}

	if( !MGlobal::executeCommand( mCommand, mDestination, false ) )
	{
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus ExecuteSelectedObjectCommand( const MString& mCommand, const MDagPath& mDagPath, const MObject& mComponent, MDoubleArray& mDestination )
{
	MStatusAssert( mDagPath.isValid(), "ExecuteSelectedObjectCommand: invalid node" );

	if( !MGlobal::select( mDagPath, mComponent, MGlobal::kReplaceList ) )
	{
		return MStatus::kFailure;
	}

	if( !MGlobal::executeCommand( mCommand, mDestination, false ) )
	{
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus ExecuteSelectedObjectCommand( const MString& mCommand, const MDagPath& mDagPath, const MObject& mComponent )
{
	MStatusAssert( mDagPath.isValid(), "ExecuteSelectedObjectCommand: invalid node" );

	if( !MGlobal::select( mDagPath, mComponent, MGlobal::kReplaceList ) )
	{
		return MStatus::kFailure;
	}

	if( !MGlobal::executeCommand( mCommand, false ) )
	{
		return MStatus::kFailure;
	}

	return MStatus::kSuccess;
}

MStatus RemoveConstructionHistoryOnNode( const MDagPath& mDagPath )
{
	return ::ExecuteSelectedObjectCommand( "delete -ch", mDagPath, MObject::kNullObj );
}

MStatus GetMObjectOfNamedObject( MString mName, MObject& mObject )
{
	MSelectionList mSelectionList;
	if( !mSelectionList.add( mName ) )
	{
		return MStatus::kFailure;
	}
	if( !mSelectionList.getDependNode( 0, mObject ) )
	{
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus DeleteSkinningJointsOfNode( const MDagPath& mDagPath )
{
	MSanityCheck( mDagPath.isValid() );
	MStatus mStatus;

	// select the object
	mStatus = MGlobal::select( mDagPath, MObject::kNullObj, MGlobal::kReplaceList );

	MCheckStatus( mStatus, "DeleteSkinningJointsOfNode: Node selection failed" );

	// select the skin cluster of the object
	mStatus = ::ExecuteCommand( "select `skinCluster -q -inf`" );
	if( !mStatus )
	{
		// no skin cluster, just return
		return mStatus;
	}

	// delete the skin cluster
	mStatus = ::ExecuteCommand( "delete" );
	MCheckStatus( mStatus, "DeleteSkinningJointsOfNode: Delete skin cluster failed" );

	return mStatus;
}

MString GetSkinClusterNodeName( MDagPath mMeshDagPath )
{
	MSanityCheck( mMeshDagPath.isValid() );

	mMeshDagPath.extendToShape();

	MStatus mStatus;
	MString mSkinClusterName;
	MFnDagNode mMeshDagNode( mMeshDagPath ); // path to the visible mesh

	// the deformed mesh comes into the visible mesh
	// through its "inmesh" plug
	MPlug mInMeshPlug = mMeshDagNode.findPlug( "inMesh", true, &mStatus );

	if( mStatus == MStatus::kSuccess && mInMeshPlug.isConnected() )
	{
		// walk the tree of stuff upstream from this plug
		MItDependencyGraph mDependencyIterator(
		    mInMeshPlug, MFn::kInvalid, MItDependencyGraph::kUpstream, MItDependencyGraph::kDepthFirst, MItDependencyGraph::kPlugLevel, &mStatus );

		if( mStatus )
		{
			mDependencyIterator.disablePruningOnFilter();

			for( ; !mDependencyIterator.isDone(); mDependencyIterator.next() )
			{
				MObject mNodeObject = mDependencyIterator.currentItem();

				// go until we find a skinCluster
				if( mNodeObject.apiType() == MFn::kSkinClusterFilter )
				{
					MFnDependencyNode mSkinCluster( mNodeObject );
					mSkinClusterName = mSkinCluster.name();
					break;
				}
			}
		}
	}

	return mSkinClusterName;
}

MStringArray GetSkinJointsOfNode( MDagPath mMeshDagPath )
{
	MStringArray mReturnList;
	MString mCommand = "skinCluster -q -inf";
	::ExecuteSelectedObjectCommand( mCommand, mMeshDagPath, MObject::kNullObj, mReturnList );
	return mReturnList;
}

MStatus GetMayaWorkspaceTextureFolder( MString& mDirectory )
{
	MStatus mStatus;

	// retrieve the root folder of the workspace
	MString mWorkspacePath;
	MStringArray mWorkspaceArray;

	MString mCommand = "toNativePath( `workspace -q -rootDirectory` );";
	mStatus = ::ExecuteCommand( mCommand, mWorkspaceArray );
	if( !mStatus )
	{
		MGlobal::displayError( "Failed to retrieve the workspace folder. Please set a workspace." );
		return mStatus;
	}
	mDirectory = mWorkspaceArray[ 0 ];

	// now, retrieve the textures relative path
	mCommand = "toNativePath( `workspace -q -fileRuleEntry textures`);";
	mStatus = ::ExecuteCommand( mCommand, mWorkspaceArray );
	if( !mStatus )
	{
		MGlobal::displayError( "Failed to retrieve the textures workspace folder. Please set a workspace and textures path." );
		return mStatus;
	}
	mDirectory += mWorkspaceArray[ 0 ];

	return MStatus::kSuccess;
}

void RemoveNodeList( MStringArray& mNodeList )
{
	for( uint q = 0; q < mNodeList.length(); ++q )
	{
		MString mNodeName = mNodeList[ q ];
		MDagPath mNodeDagPath;

		// get the path to the object
		if( GetPathToNamedObject( mNodeName, mNodeDagPath ) )
		{
			// if found, select and delete
			const MStatus mStatus = ExecuteSelectedObjectCommand( MString( "delete" ), mNodeDagPath, MObject::kNullObj );
			MSanityCheck( mStatus );
		}
	}
}

float ConvertFromColorToWeights( float c, float multiplier )
{
	const float exponent = _log2( multiplier ) * 2; // what we need to raise 2 to, to get multiplier squared
	return pow( 2.f, c * exponent ) / multiplier;   // get a value 1/multiplier to multiplier
}

float ConvertFromWeightsToColor( float w, float multiplier )
{
	const float min = 1 / (float)multiplier;
	const float max = (float)multiplier;

	if( w < min )
		w = min;

	else if( w > max )
		w = max;

	const float exponent = _log2( multiplier ) * 2; // what we need to raise 2 to, to get multiplier squared
	return _log2( w * multiplier ) / exponent;      // from value 1/multiplier to multiplier, get range 0->1
}

MString RemoveIllegalCharacters( MString mName )
{
	if( mName.index( ':' ) == -1 )
	{
		return mName;
	}

	MString mReturnString;
	MStringArray mSplitArray;

	const MStatus mStatus = mName.split( ':', mSplitArray );
	if( mStatus == MStatus::kSuccess )
	{
		for( int index = 0; index < (int)mSplitArray.length(); ++index )
		{
			mReturnString += mSplitArray[ index ];
		}
	}
	return mReturnString;
}

std::basic_string<TCHAR> string_format( const std::basic_string<TCHAR> fmt_str, ... )
{
	int final_n = 0, n = ( (int)fmt_str.size() ) * 2; /* reserve 2 times as much as the length of the fmt_str */
	std::basic_string<TCHAR> str;
	std::unique_ptr<TCHAR[]> formatted;
	va_list ap = nullptr;
	while( 1 )
	{
		formatted.reset( new TCHAR[ n ] ); /* wrap the plain char array into the unique_ptr */
		_tcscpy( &formatted[ 0 ], fmt_str.c_str() );
		va_start( ap, fmt_str );
		final_n = _vsntprintf( &formatted[ 0 ], n, fmt_str.c_str(), ap );
		va_end( ap );
		if( final_n < 0 || final_n >= n )
			n += abs( final_n - n + 1 );
		else
			break;
	}
	return std::basic_string<TCHAR>( formatted.get() );
}

bool contains( std::vector<std::basic_string<TCHAR>> tStringCollection, std::basic_string<TCHAR> tString )
{
	std::vector<std::basic_string<TCHAR>>::iterator stringIterator;
	for( stringIterator = tStringCollection.begin(); stringIterator < tStringCollection.end(); stringIterator++ )
	{
		if( *stringIterator == tString )
			return true;
	}

	return false;
}

int GetMayaVersion()
{
	std::string sVersionString = MGlobal::mayaVersion().asChar();
	const int versionNumber = atoi( sVersionString.c_str() );

	return versionNumber;
}

int StringToint( std::string sString )
{
	int v = 0;
	try
	{
		v = atoi( sString.c_str() );
	}
	catch( std::exception ex )
	{
		throw new std::exception( "StringToInt: failed when trying to convert to int." );
	}

	return v;
}

double StringTodouble( std::string sString )
{
	double v = 0;
	try
	{
		v = atof( sString.c_str() );
	}
	catch( std::exception ex )
	{
		throw new std::exception( "StringToInt: failed when trying to convert to double." );
	}

	return v;
}

float StringTofloat( std::string sString )
{
	return (float)StringTodouble( sString );
}

std::string StringTostring( std::string sString )
{
	return sString;
}

bool StringTobool( std::string sString )
{
	if( sString == "1" )
		return true;
	else if( sString == "true" )
		return true;
	else if( sString == "True" )
		return true;
	else if( sString == "TRUE" )
		return true;

	return false;
}

bool StringToNULL( std::basic_string<TCHAR> tString )
{
	if( tString == _T("null") )
		return true;
	else if( tString == _T("Null") )
		return true;
	else if( tString == _T("NULL") )
		return true;

	return false;
}

float _log2( float n )
{
	return log( n ) / log( 2.f );
}

bool ObjectExists( MString mObjectName )
{
	MString mCommand = "objExists(";
	mCommand += "\"";
	mCommand += mObjectName;
	mCommand += "\");";

	bool bExists = false;
	ExecuteCommand( mCommand, bExists );

	return bExists;
}

MString GetNonCollidingMeshName( MString mLodName )
{
	// set the name of the LOD
	bool bExists = ObjectExists( mLodName );

	MString mFinalLodName = mLodName;

	int index = 1;
	while( bExists )
	{
		std::basic_ostringstream<TCHAR, std::char_traits<TCHAR>, std::allocator<TCHAR>> tLodNameStream;
		tLodNameStream << mLodName.asChar() << _T("_") << std::setfill<TCHAR>( '0' ) << std::setw( 3 ) << index;
		const std::basic_string<TCHAR> tCurrentLODName = tLodNameStream.str();

		mFinalLodName = MString( tCurrentLODName.c_str() );

		bExists = ObjectExists( mFinalLodName );
		index++;
	}

	return mFinalLodName;
}

MStatus TryReuseDefaultUV( MFnMesh& mMesh, MString mRequestedUVName )
{
	MStringArray mUVSetNames;
	mMesh.getUVSetNames( mUVSetNames );

	for( uint i = 0; i < mUVSetNames.length(); ++i )
	{
		MString mUVSetName = mUVSetNames[ i ];
		if( mUVSetName == "reuse" )
		{
			// mesh.clearUVs(&uvSetName);
			return mMesh.renameUVSet( mUVSetName, mRequestedUVName );
		}
	}

	return MStatus::kFailure;
}

void GetPluginDir( char* cDestination )
{
	char Path[ MAX_PATH ] = { 0 };
	char drive[ _MAX_DRIVE ] = { 0 };
	char dir[ _MAX_DIR ] = { 0 };
	char fname[ _MAX_FNAME ] = { 0 };
	char ext[ _MAX_EXT ] = { 0 };

	if( GetModuleFileName( NULL, Path, MAX_PATH ) == NULL )
	{
		cDestination[ 0 ] = '\0';
		return;
	}

	_splitpath_s( Path, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT );
	_makepath_s( Path, _MAX_PATH, drive, dir, "", "" );

	sprintf( cDestination, "%splug-ins\\", Path );
}

int TranslateDeviationToPixels( double radius, double deviation )
{
	const double diameter = radius * 2;
	double pixelsize = diameter / deviation;

	if( pixelsize > double( INT_MAX ) )
		pixelsize = double( INT_MAX );

	return int( pixelsize );
}

double TranslateDeviationToDistance( double radius, double deviation, double fovInRadians, double screenSize )
{
	const int pixelsize = TranslateDeviationToPixels( radius, deviation );
	if( pixelsize == INT_MAX )
		return 0;

	// the size of the pixelsize compared to the size of the screen
	const double screenRatio = double( pixelsize ) / screenSize;

	// the distance of the near clipping plane if the screen size has a size of 1
	const double nearClipDistance = 1.0 / ( tan( fovInRadians / 2.0 ) );

	// the angle of the bounding sphere rendered on screen
	const double boundingSphereAngle = atan( screenRatio / nearClipDistance );

	// the distance (along the view vector) to the center of the bounding sphere
	const double distance = radius / sin( boundingSphereAngle );

	return distance;
}

MString CreateQuotedText( MString mText )
{
	int len = 0;
	const char* cText = mText.asChar( len );

	std::string sResult;

	sResult += '"';
	for( int i = 0; i < len; ++i )
	{
		if( cText[ i ] == '\\' )
			sResult += '\\';
		sResult += cText[ i ];
	}
	sResult += '"';

	return MString( sResult.c_str() );
}

MString CreateQuotedTextAndRemoveLineBreaks( MString mText )
{
	int len = 0;
	const char* cText = mText.asChar( len );

	std::string sResult;

	sResult += '"';
	for( int i = 0; i < len; ++i )
	{
		if( cText[ i ] != '\n' && cText[ i ] != '\r' )
		{
			if( cText[ i ] == '"' )
				sResult += '\\';
			sResult += cText[ i ];
		}
	}
	sResult += '"';

	return MString( sResult.c_str() );
}
