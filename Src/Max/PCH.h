// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdio.h>
#include <process.h>
#include <io.h>
#include <vector>
#include <map>
#include <set>
#include <crtdbg.h>
#include <functional>

#include "Max.h"
#include "resource.h"
#include "istdplug.h"
#include "iparamb2.h"
#include "iparamm2.h"
#include "Simpobj.h"
#include "Simpmod.h"
#include "Simpobj.h"

#include "simpmod.h"
#include "simpobj.h"
#include "hsv.h"
#include <direct.h>
#include <commdlg.h>
#include <guplib.h>
//#include "frontend.h"
#include "tvnode.h"
#include "bmmlib.h"
#include "fltlib.h"
#include "ViewFile.h"
#include "meshadj.h"
#include "XTCObject.h"
#include "samplers.h"
#include "texutil.h"
#include "shaders.h"
#include "macrorec.h"
#include "gport.h"
#include "shadgen.h"
#include "stdmat.h"
#include "imtl.h"
#include "macrorec.h"
#include "tvutil.h"
#include "utilapi.h"
#include "IKSolver.h"
#include "ISkin.h"
#include "ISkinCodes.h"
#include "icurvctl.h"
#include "gizmo.h"
#include "gizmoimp.h"
#include "XTCObject.h"
#include "modstack.h"
#include <MeshNormalSpec.h>
#include <log.h>

#include <INamedSelectionSetManager.h>
#include <maxscript/maxscript.h>
#include <maxscript/util/listener.h>

#include <iepoly.h>
#include "polyobj.h"

#include "IFileResolutionManager.h"
#include "assetmanagement\AssetType.h"

#include "IPathConfigMgr.h"

typedef unsigned int uint;
typedef unsigned char uchar;

#include "SimplygonLoader.h"
#include "SimplygonInit.h"
#include "WorkDirectoryHandler.h"

#define DEG2RAD( DEG ) ( ( DEG ) * ( ( PI ) / ( 180.0 ) ) )
#define RAD2DEG( RAD ) ( ( RAD ) * ( ( 180.0 ) / ( PI ) ) )

extern TCHAR* GetString( int id );
extern HINSTANCE hInstance;

#define SIMPLYGON_CLASS_ID Class_ID( 0x37793a09, 0x191770f4 ) // Cloud version

using namespace Simplygon;
extern Simplygon::ISimplygon* sg;

// additional search paths for the simplygon process
extern std::vector<std::basic_string<TCHAR>> SimplygonProcessAdditionalSearchPaths;

// SgSetMacro implements a standard set function for a variable.
#define SgSetMacro( type, name ) \
	void Set##name( type v )     \
	{                            \
		this->ThreadLock();      \
		if( this->name != v )    \
		{                        \
			this->name = v;      \
			this->Modified();    \
		}                        \
		this->ThreadUnlock();    \
	}

// SgGetMacro implements a standard get function for a variable.
#define SgGetMacro( type, name ) \
	type Get##name()             \
	{                            \
		type retvalue;           \
		this->ThreadLock();      \
		retvalue = this->name;   \
		this->ThreadUnlock();    \
		return retvalue;         \
	}

// Basic value Set/Get macro methods for values in the SimplygonMax object
#define SgValueMacro( type, name ) \
	type name;                     \
	SgSetMacro( type, name ) SgGetMacro( type, name )

Matrix3 GetConversionMatrix();
Matrix3 ConvertMatrixII( Matrix3 maxMatrix );
Matrix3 GetIdentityMatrix();

std::string StringTostring( std::string str );
bool StringTobool( std::string str );
int StringToint( std::string str );
double StringTodouble( std::string str );
float StringTofloat( std::string str );
bool StringToNULL( std::basic_string<TCHAR> str );



float _log2( float n );