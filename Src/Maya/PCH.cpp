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

bool IsSamePath( const char* path1, const char* path2 )
{
	uint counter = 0;
	const char forwardslash = '/';
	const char backslash = '\\';

	const size_t len1 = strlen( path1 );
	const size_t len2 = strlen( path2 );

	if( len1 != len2 )
	{
		return false;
	}

	for( counter = 0; counter < len1; ++counter )
	{
		if( tolower( path1[ counter ] ) != tolower( path2[ counter ] ) )
		{
			if( path1[ counter ] == forwardslash && path2[ counter ] == backslash )
			{
				continue;
			}
			else if( path2[ counter ] == forwardslash && path1[ counter ] == backslash )
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

MStatus GetPathToNamedObject( MString name, MDagPath& path )
{
	MSelectionList list;
	if( !list.add( name ) )
	{
		return MStatus::kFailure;
	}
	if( !list.getDagPath( 0, path ) )
	{
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus getFloat3PlugValue( MPlug plug, MFloatVector& value )
{
	// Retrieve the value as an MObject
	//
	MObject object;
	plug.getValue( object );

	// Convert the MObject to a float3
	//
	MFnNumericData numDataFn( object );
	numDataFn.getData( value[ 0 ], value[ 1 ], value[ 2 ] );
	return MStatus::kSuccess;
}

MStatus getFloat3asMObject( MFloatVector value, MObject& object )
{
	// Convert the float value into an MObject
	//
	MFnNumericData numDataFn;
	object = numDataFn.create( MFnNumericData::k3Float );
	numDataFn.setData( value[ 0 ], value[ 1 ], value[ 2 ] );
	return MStatus::kSuccess;
}

MStatus ExecuteCommand( MString cmd )
{
	if( !MGlobal::executeCommand( cmd, false ) )
	{
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus ExecuteCommand( MString cmd, MString& dest )
{
	if( !MGlobal::executeCommand( cmd, dest, false ) )
	{
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus ExecuteCommand( MString cmd, MStringArray& dest )
{
	if( !MGlobal::executeCommand( cmd, dest, false ) )
	{
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

// Execute the command and get the resulting boolean value
MStatus ExecuteCommand( MString cmd, bool& result )
{
	MCommandResult commandResult;
#ifdef PRINT_DEBUG_INFO
	if( !MGlobal::executeCommand( cmd, commandResult, true, true ) )
#else
	if( !MGlobal::executeCommand( cmd, commandResult, false, false ) )
#endif
	{
		result = false;
		return MStatus::kFailure;
	}

	// Did the executed command return true or false?
	int boolValue = 0;
	commandResult.getResult( boolValue );
	if( boolValue == 1 )
	{
		result = true;
	}
	else
	{
		result = false;
	}
	return MStatus::kSuccess;
}

void SelectDAGPath( MDagPath node, bool add_to_list )
{
	if( add_to_list )
	{
		// Required for BinSkim compat
		// TODO: Deprecated method, should be replaced!
		SG_DISABLE_SPECIFIC_BEGIN( 4996 )
		MGlobal::select( node, MObject::kNullObj, MGlobal::kAddToList );
		SG_DISABLE_SPECIFIC_END
	}
	else
	{
		// Required for BinSkim compat
		// TODO: Deprecated method, should be replaced!
		SG_DISABLE_SPECIFIC_BEGIN( 4996 )
		MGlobal::select( node, MObject::kNullObj, MGlobal::kReplaceList );
		SG_DISABLE_SPECIFIC_END
	}
}

// This function removes any node below this node, which is NOT a shape node
MStatus RemoveAllNonMeshShapeSubNodes( MDagPath node )
{
	// unselect all
	bool anything_selected = false;

	// look through all child nodes
	for( uint i = 0; i < node.childCount(); ++i )
	{
		// get the path to the child
		MDagPath child_path = node;
		child_path.push( node.child( i ) );

		// check the type
		const MFn::Type child_type = child_path.apiType();

		// HACK:: Orignallly was only removing object if they were not meshes.
		// if meshes are praented. then this removes child element. therefore checking for transform
		if( child_type != MFn::kMesh && child_type != MFn::kTransform )
		{
			// select it, we will remove it
			SelectDAGPath( child_path, anything_selected );
			anything_selected = true;
		}
	}

	// delete the selected objects
	if( anything_selected )
	{
		MStringArray retlist;
		if( !ExecuteCommand( "delete;", retlist ) )
		{
			return MStatus::kFailure;
		}
	}

	return MStatus::kSuccess;
}

// This function duplicates a node with its shape, and makes sure to remove
// any other node that is below the duplicate
MStatus DuplicateNodeWithShape( MDagPath node, MDagPath& resultNode, MStringArray* slist, MString dupName, bool alternative_duplicate )
{
	MDagPath shapeNode = node;
	if( shapeNode.extendToShape() )
	{
		// Required for BinSkim compat
		// TODO: Deprecated method, should be replaced!
		SG_DISABLE_SPECIFIC_BEGIN( 4996 )
		bool selected = MGlobal::select( node, MObject::kNullObj, MGlobal::kReplaceList );
		SG_DISABLE_SPECIFIC_END
		if( alternative_duplicate )
		{
			// Required for BinSkim compat
			// TODO: Deprecated method, should be replaced!
			SG_DISABLE_SPECIFIC_BEGIN( 4996 )
			selected &= MGlobal::select( shapeNode, MObject::kNullObj, MGlobal::kAddToList );
			SG_DISABLE_SPECIFIC_END
		}

		// select the node and mesh to duplicate
		if( selected )
		{
			MStringArray retlist;

			MString cmd = "duplicate -rc -ic";
			if( alternative_duplicate )
			{
				cmd += " -po";
			}
			if( dupName != "" )
			{
				cmd += "-n ";
				cmd += dupName;
			}
			cmd += ";";

			if( ::ExecuteCommand( cmd, retlist ) )
			{
				MStatus succ = MStatus::kFailure;

				// we should assume the returned list is at least 2 items long. get the returned item as a
				// dag path, and the rest as a selection list, if wanted

				// we have the duplicate, get its DAG path
				if( retlist.length() > 1 )
				{
					// try the second item first
					succ = ::GetPathToNamedObject( retlist[ 1 ], resultNode );
				}
				if( !succ )
				{
					// failed with the second item, use the first
					succ = ::GetPathToNamedObject( retlist[ 0 ], resultNode );
				}

				MSanityCheck( succ );

				if( succ )
				{
					// remove all childnodes that are not mesh shapes
					if( ::RemoveAllNonMeshShapeSubNodes( resultNode ) )
					{
						// get all other returned nodes into a selection list
						if( slist != NULL )
						{
							slist->clear();
							for( uint i = 2; i < retlist.length(); ++i )
							{
								slist->append( retlist[ i ] );
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

MStatus ExecuteSelectedObjectCommand( MString cmd, MDagPath node, MObject component, MStringArray& dest )
{
	MStatusAssert( node.isValid(), "ExecuteSelectedObjectCommand: invalid node" );

	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	if( !MGlobal::select( node, component, MGlobal::kReplaceList ) )
	{
		return MStatus::kFailure;
	}
	SG_DISABLE_SPECIFIC_END

	if( !MGlobal::executeCommand( cmd, dest, false ) )
	{
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus ExecuteSelectedObjectCommand( MString cmd, MDagPath node, MObject component, MDoubleArray& dest )
{
	MStatusAssert( node.isValid(), "ExecuteSelectedObjectCommand: invalid node" );

	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	if( !MGlobal::select( node, component, MGlobal::kReplaceList ) )
	{
		return MStatus::kFailure;
	}
	SG_DISABLE_SPECIFIC_END

	if( !MGlobal::executeCommand( cmd, dest, false ) )
	{
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus ExecuteSelectedObjectCommand( MString cmd, MDagPath node, MObject component )
{
	MStatusAssert( node.isValid(), "ExecuteSelectedObjectCommand: invalid node" );

	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	if( !MGlobal::select( node, component, MGlobal::kReplaceList ) )
	{
		return MStatus::kFailure;
	}
	SG_DISABLE_SPECIFIC_END

	if( !MGlobal::executeCommand( cmd, false ) )
	{
		return MStatus::kFailure;
	}

	return MStatus::kSuccess;
}

MStatus RemoveConstructionHistoryOnNode( MDagPath node )
{
	return ::ExecuteSelectedObjectCommand( "delete -ch", node, MObject::kNullObj );
}

MStatus GetMObjectOfNamedObject( MString name, MObject& ob )
{
	MSelectionList list;
	if( !list.add( name ) )
	{
		return MStatus::kFailure;
	}
	if( !list.getDependNode( 0, ob ) )
	{
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus DeleteSkinningJointsOfNode( MDagPath node )
{
	MSanityCheck( node.isValid() );
	MStatus mStatus;

	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	// select the object
	mStatus = MGlobal::select( node, MObject::kNullObj, MGlobal::kReplaceList );
	SG_DISABLE_SPECIFIC_END
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

	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	// the deformed mesh comes into the visible mesh
	// through its "inmesh" plug
	MPlug mInMeshPlug = mMeshDagNode.findPlug( "inMesh", &mStatus );
	SG_DISABLE_SPECIFIC_END

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
				// Required for BinSkim compat
				// TODO: Deprecated method, should be replaced!
				SG_DISABLE_SPECIFIC_BEGIN( 4996 )
				MObject mNodeObject = mDependencyIterator.thisNode();
				SG_DISABLE_SPECIFIC_END

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

MStringArray GetSkinJointsOfNode( MDagPath meshnode )
{
	MStringArray ret;
	MString cmd = "skinCluster -q -inf";
	::ExecuteSelectedObjectCommand( cmd, meshnode, MObject::kNullObj, ret );
	return ret;
}

MStatus GetMayaWorkspaceTextureFolder( MString& dest )
{
	MStatus result;

	// retrieve the root folder of the workspace
	MString wspath;
	MStringArray wsarray;
	MString cmd = "toNativePath( `workspace -q -rootDirectory` );";
	result = ::ExecuteCommand( cmd, wsarray );
	if( !result )
	{
		MGlobal::displayError( "Failed to retrieve the workspace folder. Please set a workspace." );
		return result;
	}
	dest = wsarray[ 0 ];

	// now, retrieve the textures relative path
	cmd = "toNativePath( `workspace -q -fileRuleEntry textures`);";
	result = ::ExecuteCommand( cmd, wsarray );
	if( !result )
	{
		MGlobal::displayError( "Failed to retrieve the textures workspace folder. Please set a workspace and textures path." );
		return result;
	}
	dest += wsarray[ 0 ];

	return MStatus::kSuccess;
}

void RemoveNodeList( MStringArray& slist )
{
	for( uint q = 0; q < slist.length(); ++q )
	{
		MString nodename = slist[ q ];
		MDagPath p;

		// get the path to the object
		if( GetPathToNamedObject( nodename, p ) )
		{
			// if found, select and delete
			const MStatus mStatus = ExecuteSelectedObjectCommand( MString( "delete" ), p, MObject::kNullObj );
			MSanityCheck( mStatus );
		}
	}
}

float ConvertFromColorToWeights( float c, float multiplier )
{
	const float exponent = _log2( multiplier ) * 2;     // what we need to raise 2 to, to get multiplier squared
	return pow( 2.f, c * exponent ) / multiplier; // get a value 1/multiplier to multiplier
}

float ConvertFromWeightsToColor( float w, float multiplier )
{
	const float min = 1 / (float)multiplier;
	const float max = (float)multiplier;

	if( w < min )
		w = min;

	else if( w > max )
		w = max;

	const float exponent = _log2( multiplier ) * 2;  // what we need to raise 2 to, to get multiplier squared
	return _log2( w * multiplier ) / exponent; // from value 1/multiplier to multiplier, get range 0->1
}

MString RemoveIllegalCharacters( MString name )
{
	if( name.index( ':' ) == -1 )
	{
		return name;
	}

	MString returnString;
	MStringArray splitString;

	const MStatus mStatus = name.split( ':', splitString );
	if( mStatus == MStatus::kSuccess )
	{
		for( int index = 0; index < (int)splitString.length(); index++ )
		{
			returnString += splitString[ index ];
		}
	}
	return returnString;
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

bool contains( std::vector<std::basic_string<TCHAR>> strCollection, std::basic_string<TCHAR> val )
{
	std::vector<std::basic_string<TCHAR>>::iterator it;
	for( it = strCollection.begin(); it < strCollection.end(); it++ )
	{
		if( *it == val )
			return true;
	}

	return false;
}

int GetMayaVersion()
{
	std::string verString = MGlobal::mayaVersion().asChar();
	const int ver = atoi( verString.c_str() );

	return ver;
}

int StringToint( std::string str )
{
	int v = 0;
	try
	{
		v = atoi( str.c_str() );
	}
	catch( std::exception ex )
	{
		throw new std::exception( "StringToInt: failed when trying to convert to int." );
	}

	return v;
}

double StringTodouble( std::string str )
{
	double v = 0;
	try
	{
		v = atof( str.c_str() );
	}
	catch( std::exception ex )
	{
		throw new std::exception( "StringToInt: failed when trying to convert to double." );
	}

	return v;
}

float StringTofloat( std::string str )
{
	return (float)StringTodouble( str );
}

std::string StringTostring( std::string str )
{
	return str;
}

bool StringTobool( std::string str )
{
	if( str == "1" )
		return true;
	else if( str == "true" )
		return true;
	else if( str == "True" )
		return true;
	else if( str == "TRUE" )
		return true;

	return false;
}

bool StringToNULL( std::basic_string<TCHAR> str )
{
	if( str == _T("null") )
		return true;
	else if( str == _T("Null") )
		return true;
	else if( str == _T("NULL") )
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

MStatus TryReuseDefaultUV( MFnMesh& mesh, MString requestedName )
{
	MStringArray UVSetNames;
	mesh.getUVSetNames( UVSetNames );

	for( uint s = 0; s < UVSetNames.length(); ++s )
	{
		MString uvSetName = UVSetNames[ s ];
		if( uvSetName == "reuse" )
		{
			// mesh.clearUVs(&uvSetName);
			return mesh.renameUVSet( uvSetName, requestedName );
		}
	}

	return MStatus::kFailure;
}

void GetPluginDir( char* dest )
{
	char Path[ MAX_PATH ] = { 0 };
	char drive[ _MAX_DRIVE ] = { 0 };
	char dir[ _MAX_DIR ] = { 0 };
	char fname[ _MAX_FNAME ] = { 0 };
	char ext[ _MAX_EXT ] = { 0 };

	if( GetModuleFileName( NULL, Path, MAX_PATH ) == NULL )
	{
		dest[ 0 ] = '\0';
		return;
	}

	_splitpath_s( Path, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT );
	_makepath_s( Path, _MAX_PATH, drive, dir, "", "" );

	sprintf( dest, "%splug-ins\\", Path );
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

MString CreateQuotedText( MString text )
{
	int len;
	const char* t = text.asChar( len );

	std::string r;

	r += '"';
	for( int i = 0; i < len; ++i )
	{
		if( t[ i ] == '\\' )
			r += '\\';
		r += t[ i ];
	}
	r += '"';

	return MString( r.c_str() );
}
