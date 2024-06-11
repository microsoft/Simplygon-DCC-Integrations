// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdio.h>

#define X( a, b ) a,
enum ShadingNodeType
{
#include "ShadingNodeTable.h"
};
#undef X

#define X( a, b ) b,
static char* ShadingNetworkNodeTable[] = {
#include "ShadingNodeTable.h"
};
#undef X

#include <process.h>
#include <io.h>

#include <map>
#include <set>
#include <list>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <memory>
#include <assert.h>
#include <CodeAnalysis/Warnings.h>
#include <SgCodeAnalysisSetup.h>
#include "CriticalSection.h"

#pragma warning( push )
#pragma warning( disable : ALL_CODE_ANALYSIS_WARNINGS )
#include <maya/MSelectionList.h>
#include <maya/MDagModifier.h>
#include <maya/MDagPath.h>
#include <maya/MDGModifier.h>
#include <maya/MFileIO.h>
#include <maya/MFloatPointArray.h>
#include <maya/MFloatVector.h>
#include <maya/MFloatVectorArray.h>
#include <maya/MFnBase.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnMesh.h>
#include <maya/MFnNumericData.h>
#include <maya/MFnNurbsCurve.h>
#include <maya/MFnSet.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MGlobal.h>
#include <maya/MIntArray.h>
#include <maya/MIOStream.h>
#include <maya/MItDag.h>
#include <maya/MItDependencyGraph.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MItMeshEdge.h>
#include <maya/MItSelectionList.h>
#include <maya/MObjectArray.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MPointArray.h>
#include <maya/MPxCommand.h>
#include <maya/MSelectionList.h>
#include <maya/MString.h>
#include <maya/MFnMesh.h>
#include <maya/MFloatPointArray.h>
#include <maya/MIntArray.h>
#include <maya/MItMeshVertex.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MFnNumericData.h>
#include <maya/MFnTransform.h>
#include <maya/MVector.h>
#include <maya/MPxCommand.h>
#include <maya/MFnMesh.h>
#include <maya/MGlobal.h>
#include <maya/MString.h>
#include <maya/MDagPath.h>
#include <maya/MIntArray.h>
#include <maya/MFloatVectorArray.h>
#include <maya/MMatrix.h>
#include <maya/MArgList.h>
#include <maya/MSyntax.h>
#include <maya/MArgDatabase.h>
#include <maya/MItMeshEdge.h>
#include <maya/MUintArray.h>
#include <maya/MFnSkinCluster.h>
#include <maya/MProgressWindow.h>
#include <maya/MFnBlendShapeDeformer.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MItGeometry.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MFnBlendShapeDeformer.h>
#include <maya/MDagPath.h>
#include <maya/MDagPathArray.h>
#include <maya/MViewportRenderer.h>
#include <maya/M3dView.h>
#include <maya/MFnCamera.h>
#include <maya/MFnAttribute.h>
#include <maya/MNamespace.h>
#include <maya/MFnStringData.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MTime.h>
#include <maya/MAnimControl.h>
#include <maya/MCommandResult.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MColorManagementUtilities.h>

#if MAYA_API_VERSION >= 20180000
#include "maya/MDGContextGuard.h"
#endif

#pragma warning( pop )
#include "SimplygonLoader.h"
#include "SimplygonInit.h"

using namespace Simplygon;

// the main sg pointer
extern Simplygon::ISimplygon* sg;
extern SimplygonInitClass* SimplygonInitInstance;

// additional search paths for the simplygon process
extern std::vector<std::string> SimplygonProcessAdditionalSearchPaths;

extern int MayaAPIVersion;

#pragma region MAYA_STANDARD_MATERIAL_CHANNEL_DEFINES

#ifndef MAYA_MATERIAL_CHANNEL_COLOR
#define MAYA_MATERIAL_CHANNEL_COLOR "color"
#endif // !MAYA_MATERIAL_CHANNEL_COLOR

#ifndef MAYA_MATERIAL_CHANNEL_TRANSPARENCY
#define MAYA_MATERIAL_CHANNEL_TRANSPARENCY "transparency"
#endif // !MAYA_MATERIAL_CHANNEL_TRANSPARENCY

#ifndef MAYA_MATERIAL_CHANNEL_AMBIENTCOLOR
#define MAYA_MATERIAL_CHANNEL_AMBIENTCOLOR "ambientColor"
#endif // !MAYA_MATERIAL_CHANNEL_AMBIENTCOLOR

#ifndef MAYA_MATERIAL_CHANNEL_SPECULARCOLOR
#define MAYA_MATERIAL_CHANNEL_SPECULARCOLOR "specularColor"
#endif // !MAYA_MATERIAL_CHANNEL_SPECULARCOLOR

#ifndef MAYA_MATERIAL_CHANNEL_INCANDESCENCE
#define MAYA_MATERIAL_CHANNEL_INCANDESCENCE "incandescence"
#endif // !MAYA_MATERIAL_CHANNEL_INCANDESCENCE

#ifndef MAYA_MATERIAL_CHANNEL_NORMALCAMERA
#define MAYA_MATERIAL_CHANNEL_NORMALCAMERA "normalCamera"
#endif // !MAYA_MATERIAL_CHANNEL_NORMALCAMERA

#ifndef MAYA_MATERIAL_CHANNEL_TRANSLUECENCE
#define MAYA_MATERIAL_CHANNEL_TRANSLUECENCE "translucence"
#endif // !MAYA_MATERIAL_CHANNEL_TRANSLUECENCE

#ifndef MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_DEPTH
#define MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_DEPTH "translucenceDepth"
#endif // !MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_DEPTH

#ifndef MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_FOCUS
#define MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_FOCUS "translucenceFocus"
#endif // !MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_FOCUS

#ifndef MAYA_MATERIAL_CHANNEL_REFLECTEDCOLOR
#define MAYA_MATERIAL_CHANNEL_REFLECTEDCOLOR "reflectedColor"
#endif // !MAYA_MATERIAL_CHANNEL_REFLECTEDCOLOR

#ifndef MAYA_MATERIAL_CHANNEL_REFLECTIVITY
#define MAYA_MATERIAL_CHANNEL_REFLECTIVITY "reflectivity"
#endif // !MAYA_MATERIAL_CHANNEL_REFLECTIVITY

#pragma endregion

////////////
/* Macros */
////////////

#define PI             3.14159265358979323846
#define DEG2RAD( DEG ) ( ( DEG ) * ( ( PI ) / ( 180.0 ) ) )

// MCheckStatus (Debugging tool)
//
#ifdef _DEBUG
#define MCheckStatus( status, message )      \
	if( MS::kSuccess != status )             \
	{                                        \
		MString error( "Status failed: " );  \
		error += "Function: ";               \
		error += __FUNCTION__;               \
		error += "Message: ";                \
		error += message;                    \
		MGlobal::displayError( error );      \
		OutputDebugString( error.asChar() ); \
		OutputDebugString( "\n" );           \
		_CrtDbgBreak();                      \
		return status;                       \
	}
#else
#define MCheckStatus( status, message )      \
	if( MS::kSuccess != status )             \
	{                                        \
		MString error( "Status failed: " );  \
		error += "Function: ";               \
		error += __FUNCTION__;               \
		error += "Message: ";                \
		error += message;                    \
		MGlobal::displayError( error );      \
		OutputDebugString( error.asChar() ); \
		OutputDebugString( "\n" );           \
		return status;                       \
	}
#endif

// MAssert (Debugging tool)
//
#ifdef _DEBUG
#define MAssert( state, message )              \
	if( !(state) )                             \
	{                                          \
		MString error( "Assertion failed: " ); \
		error += "Function: ";                 \
		error += __FUNCTION__;                 \
		error += "Message: ";                  \
		error += message;                      \
		MGlobal::displayError( error );        \
		OutputDebugString( error.asChar() );   \
		OutputDebugString( "\n" );             \
		_CrtDbgBreak();                        \
	}
#else
#define MAssert( state, message )
#endif

#define MSanityCheck( state ) MAssert( (state), "" );

// MStatusAssert (Debugging tool)
//
#ifdef _DEBUG
#define MStatusAssert( state, message )        \
	if( !(state) )                             \
	{                                          \
		MString error( "Assertion failed: " ); \
		error += "Function: ";                 \
		error += __FUNCTION__;                 \
		error += "Message: ";                  \
		error += message;                      \
		MGlobal::displayError( error );        \
		OutputDebugString( error.asChar() );   \
		OutputDebugString( "\n" );             \
		_CrtDbgBreak();                        \
		return MS::kFailure;                   \
	}
#else
#define MStatusAssert( state, message )
#endif

#define MValidate( state, errorCode, message ) \
	if( !(state) )                             \
	{                                          \
		MString error( "Validation failed: " );\
		error += "Function: ";                 \
		error += __FUNCTION__;                 \
		error += "Message: ";                  \
		error += message;                      \
		MGlobal::displayError( error );        \
		OutputDebugString( error.asChar() );   \
		OutputDebugString( "\n" );             \
		return errorCode;                      \
	}


#ifdef PRINT_DEBUG_INFO
static MStatus executeGlobalCommand( const MString& command )
{
	return MGlobal::executeCommand( command, true );
}

#else
static MStatus executeGlobalCommand( const MString& command )
{
	return MGlobal::executeCommand( command, false );
}

#endif

int GetMayaVersion();

void nop();

float ConvertFromColorToWeights( float c, float multiplier = 8.f );
float ConvertFromWeightsToColor( float w, float multiplier = 8.f );

// compare filenames, ignore lower/uppercase and forward/backslash
bool IsSamePath( const char* cPathA, const char* cPathB );

MStatus GetPathToNamedObject( MString name, MDagPath& path );
MStatus getFloat3PlugValue( MPlug plug, MFloatVector& value );
MStatus getFloat3asMObject( MFloatVector value, MObject& object );
void SelectDAGPath( const MDagPath& node, bool add_to_list = true );
MStatus RemoveAllNonMeshShapeSubNodes( MDagPath node );
MStatus ExecuteCommand( MString mCommand );
MStatus ExecuteCommand( MString mCommand, MString& mDestination );
MStatus ExecuteCommand( MString mCommand, MStringArray& mDestination );
MStatus ExecuteCommand( MString mCommand, bool& result );
MStatus DuplicateNodeWithShape( MDagPath mDagPath, MDagPath& mResultingDagPath, MStringArray* mResultList, MString mDupName, bool bAlternativeDuplication );

MStatus ExecuteSelectedObjectCommand( const MString& mCommand, const MDagPath& mDagPath, const MObject& mComponent, MStringArray& mDestination );
MStatus ExecuteSelectedObjectCommand( const MString& mCommand, const MDagPath& mDagPath, const MObject& mComponent, MDoubleArray& mDestination );
MStatus ExecuteSelectedObjectCommand( const MString& mCommand, const MDagPath& mDagPath, const MObject& mComponent );
MStatus RemoveConstructionHistoryOnNode( const MDagPath& mDagPath );
MStatus GetMObjectOfNamedObject( MString name, MObject& ob );
MStatus DeleteSkinningJointsOfNode( const MDagPath& mDagPath );

// find the skinCluster node name of the meshnode (if one exists)
MString GetSkinClusterNodeName( MDagPath mDagPath );
MStringArray GetSkinJointsOfNode( MDagPath mDagPath );

MStatus GetMayaWorkspaceTextureFolder( MString& dest );
MString RemoveIllegalCharacters( MString name );

void RemoveNodeList( MStringArray& slist );

std::string GetSettingsFilePath();
std::basic_string<TCHAR> string_format( const std::basic_string<TCHAR> fmt_str, ... );
bool CompareStrings( std::basic_string<TCHAR> str1, std::basic_string<TCHAR> str2 );

// LPCTSTR ConstCharPtrToLPCTSTR(const char * stringToConvert);
// const char* LPCTSTRToConstCharPtr(LPCTSTR stringToConvert);

std::string StringTostring( std::string str );
bool StringTobool( std::string str );
int StringToint( std::string str );
double StringTodouble( std::string str );
float StringTofloat( std::string str );
bool StringToNULL( std::basic_string<TCHAR> str );

float _log2( float n );

class VertexNormal
{
	public:
	bool isInitialized;
	bool isPerVertex;
	double3 normal;
};

bool ObjectExists( MString object );

MString GetNonCollidingMeshName( MString lodName );

MStatus TryReuseDefaultUV( MFnMesh& mesh, MString requestedName );

void GetPluginDir( char* dest );

int TranslateDeviationToPixels( double radius, double deviation );

double TranslateDeviationToDistance( double radius, double deviation, double fovInRadians, double screenSize );

#ifndef Globals_
#define Globals_

class Globals
{
	public:
	Globals();
	~Globals();
	void Lock();
	void UnLock();

	private:
	CriticalSection uiLock;
};

#endif // !Globals_

#ifndef UIHookHelper_
#define UIHookHelper_
class UIHookHelper
{
	public:
	UIHookHelper();
	~UIHookHelper();

	void RegisterUICallback();

	void ReadPresets( bool loop = true );

	private:
	HANDLE updateThreadHandle;
	bool killUpdateThread;

	static unsigned long __stdcall theFunction( void* v );
};
#endif // !UIHookHelper_

MString CreateQuotedText( MString text );
MString CreateQuotedTextAndRemoveLineBreaks( MString text );
