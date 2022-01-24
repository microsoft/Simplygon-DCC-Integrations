// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"
#include "resource.h"
#include "SimplygonMax.h"
#include "PipelineHelper.h"

#if( VERSION_INT <= 106 )
#include "MAXScrpt/MAXScrpt.h"
#include "MAXScrpt/MAXObj.h"
#include "MAXScrpt/numbers.h"
#include "MAXScrpt/strings.h"
// The most important one.  Defines the correct flavor of def_visible_primitive(). Must be kept at the end of includes
#include "MAXScrpt/definsfn.h"
#else
#pragma warning( disable : 4244 )

#include "MAXScript/MAXScript.h"
//#include "MAXScript/MAXObj.h"
#include "MAXScript/foundation/numbers.h"
#include "MAXScript/foundation/strings.h"
// The most important one.  Defines the correct flavor of def_visible_primitive(). Must be kept at the end of includes
#include "MAXScript/macros/define_instantiation_functions.h"
#endif

#include "MaterialInfoHandler.h"

// Basic set/get wrappings of Simplygon sdk variables
// Supported values are ints, floats, and booleans
#define sgsdk_stdvalue_variable( vartype, typeconvertfunc, varname )            \
	def_visible_primitive( sgsdk_Set##varname, "sgsdk_Set" _T( #varname ) );    \
	def_visible_primitive( sgsdk_Get##varname, "sgsdk_Get" _T( #varname ) );    \
	Value* sgsdk_Set##varname##_cf( Value** arg_list, int count )               \
	{                                                                           \
		check_arg_count( "sgsdk_Set" _T( #varname ), 1, count );                \
		SimplygonMaxInstance->Set##varname( arg_list[ 0 ]->typeconvertfunc() ); \
		return &true_value;                                                     \
	}                                                                           \
	Value* sgsdk_Get##varname##_cf( Value** arg_list, int count )               \
	{                                                                           \
		check_arg_count( "sgsdk_Get" _T( #varname ), 0, count );                \
		one_value_local( ret );                                                 \
		vl.ret = vartype::intern( SimplygonMaxInstance->Get##varname() );       \
		return_value( vl.ret );                                                 \
	}

#define sgsdk_bool_variable( varname )                                               \
	def_visible_primitive( sgsdk_Set##varname, "sgsdk_Set" _T( #varname ) );         \
	def_visible_primitive( sgsdk_Get##varname, "sgsdk_Get" _T( #varname ) );         \
	Value* sgsdk_Set##varname##_cf( Value** arg_list, int count )                    \
	{                                                                                \
		check_arg_count( "sgsdk_Set" _T( #varname ), 1, count );                     \
		SimplygonMaxInstance->Set##varname( ( arg_list[ 0 ]->to_bool() != FALSE ) ); \
		return &true_value;                                                          \
	}                                                                                \
	Value* sgsdk_Get##varname##_cf( Value** arg_list, int count )                    \
	{                                                                                \
		check_arg_count( "sgsdk_Get" _T( #varname ), 0, count );                     \
		const bool bResult = SimplygonMaxInstance->Get##varname();                   \
		return bResult ? &true_value : &false_value;                                 \
	}

#define sgsdk_int_variable( varname ) sgsdk_stdvalue_variable( Integer, to_int, varname );

#define sgsdk_float_variable( varname ) sgsdk_stdvalue_variable( Float, to_float, varname );

#define sgsdk_create_node_cf( varname )                                                                  \
	Value* sgsdk_Create##varname##_cf( Value** arg_list, int count )                                     \
	{                                                                                                    \
		check_arg_count( "sgsdk_Create"_T( #varname ), 1, count );                                       \
		one_value_local( ret );                                                                          \
		vl.ret = Integer::intern( SimplygonMaxInstance->Create##varname( arg_list[ 0 ]->to_string() ) ); \
		return_value( vl.ret );                                                                          \
	}

// list of wrapped values, see SimplygonMax.h for description of the values
sgsdk_bool_variable( ShowProgress );
sgsdk_bool_variable( RunDebugger );
sgsdk_bool_variable( LockSelectedVertices );
sgsdk_bool_variable( CanUndo );
sgsdk_int_variable( TextureCoordinateRemapping );
sgsdk_int_variable( PipelineRunMode );
sgsdk_bool_variable( AllowUnsafeImport );

// new setting pipeline
def_visible_primitive( sgsdk_CreatePipeline, "sgsdk_CreatePipeline" ); // 8.3
def_visible_primitive( sgsdk_DeletePipeline, "sgsdk_DeletePipeline" ); // 8.3
def_visible_primitive( sgsdk_ClearPipelines, "sgsdk_ClearPipelines" ); // 8.3
def_visible_primitive( sgsdk_ClonePipeline, "sgsdk_ClonePipeline" );   // 9.2

def_visible_primitive( sgsdk_LoadPipeline, "sgsdk_LoadPipeline" ); // 8.3
def_visible_primitive( sgsdk_SavePipeline, "sgsdk_SavePipeline" ); // 8.3
def_visible_primitive( sgsdk_GetSetting, "sgsdk_GetSetting" );     // 8.3
def_visible_primitive( sgsdk_SetSetting, "sgsdk_SetSetting" );     // 8.3

def_visible_primitive( sgsdk_RunPipelineOnSelection, "sgsdk_RunPipelineOnSelection" ); // 8.3
def_visible_primitive( sgsdk_RunPipelineOnFile, "sgsdk_RunPipelineOnFile" );           // 9.0

def_visible_primitive( sgsdk_GetPipelines, "sgsdk_GetPipelines" );       // 8.3
def_visible_primitive( sgsdk_GetPipelineType, "sgsdk_GetPipelineType" ); // 8.3

def_visible_primitive( sgsdk_AddMaterialCaster, "sgsdk_AddMaterialCaster" );     // 8.3
def_visible_primitive( sgsdk_AddCascadedPipeline, "sgsdk_AddCascadedPipeline" ); // 9.0

// scene import / export
def_visible_primitive( sgsdk_ExportToFile, "sgsdk_ExportToFile" );                       // 9.0
def_visible_primitive( sgsdk_ImportFromFile, "sgsdk_ImportFromFile" );                   // 9.0
def_visible_primitive( sgsdk_ClearGlobalMapping, "sgsdk_ClearGlobalMapping" );           // 9.0
def_visible_primitive( sgsdk_SetMeshNameFormat, "sgsdk_SetMeshNameFormat" );             // 9.0
def_visible_primitive( sgsdk_SetInitialLODIndex, "sgsdk_SetInitialLODIndex" );           // 9.0
def_visible_primitive( sgsdk_GetProcessedOutputPaths, "sgsdk_GetProcessedOutputPaths" ); // 9.0

// standard functions
def_visible_primitive( sgsdk_MaterialColor, "sgsdk_MaterialColor" );                         // 4.0
def_visible_primitive( sgsdk_MaterialTexture, "sgsdk_MaterialTexture" );                     // 4.0
def_visible_primitive( sgsdk_MaterialTextureMapChannel, "sgsdk_MaterialTextureMapChannel" ); // 4.1
def_visible_primitive( sgsdk_SetIsVertexColorChannel, "sgsdk_SetIsVertexColorChannel" );     // 4.1
def_visible_primitive( sgsdk_Reset, "sgsdk_Reset" );
def_visible_primitive( sgsdk_UseMaterialColorsOverride, "sgsdk_UseMaterialColorsOverride" );           // 5.2
def_visible_primitive( sgsdk_UseNonConflictingTextureNames, "sgsdk_UseNonConflictingTextureNames" );   // 5.3
def_visible_primitive( sgsdk_OverrideDefaultLODNamingPrefix, "sgsdk_OverrideDefaultLODNamingPrefix" ); // 6.1

// old (custom) channels
def_visible_primitive( sgsdk_GetTexturePathForCustomChannel, "sgsdk_GetTexturePathForCustomChannel" ); // 6.1+
def_visible_primitive( sgsdk_GetMaterialsWithCustomChannels, "sgsdk_GetMaterialsWithCustomChannels" ); // 6.1+
def_visible_primitive( sgsdk_GetCustomChannelsForMaterial, "sgsdk_GetCustomChannelsForMaterial" );     // 6.1+

// new channels
def_visible_primitive( sgsdk_GetProcessedMeshes, "sgsdk_GetProcessedMeshes" );                     // 7.0+
def_visible_primitive( sgsdk_GetMaterialForMesh, "sgsdk_GetMaterialForMesh" );                     // 7.0+
def_visible_primitive( sgsdk_GetMaterialsForMesh, "sgsdk_GetMaterialsForMesh" );                   // 9.0+
def_visible_primitive( sgsdk_GetMaterials, "sgsdk_GetMaterials" );                                 // 7.0+
def_visible_primitive( sgsdk_GetSubMaterials, "sgsdk_GetSubMaterials" );                           // 9.0+
def_visible_primitive( sgsdk_GetSubMaterialIndex, "sgsdk_GetSubMaterialIndex" );                   // 9.0+
def_visible_primitive( sgsdk_GetChannelsForMaterial, "sgsdk_GetChannelsForMaterial" );             // 7.0+
def_visible_primitive( sgsdk_GetTexturePathForChannel, "sgsdk_GetTexturePathForChannel" );         // 7.0+
def_visible_primitive( sgsdk_GetMappingChannelForChannel, "sgsdk_GetMappingChannelForChannel" );   // 8.3
def_visible_primitive( sgsdk_SetGenerateMaterial, "sgsdk_SetGenerateMaterial" );                   // 7.0+
def_visible_primitive( sgsdk_GetMeshReusesMaterial, "sgsdk_GetMeshReusesMaterial" );               // 7.0+
def_visible_primitive( sgsdk_GetMeshReusesMaterials, "sgsdk_GetMeshReusesMaterials" );             // 9.0+
def_visible_primitive( sgsdk_GetMaterialReusesSubMaterial, "sgsdk_GetMaterialReusesSubMaterial" ); // 9.0+

def_visible_primitive( sgsdk_SetTextureOutputDirectory, "sgsdk_SetTextureOutputDirectory" ); // 6.1+

def_visible_primitive( sgsdk_SelectProcessedGeometries, "sgsdk_SelectProcessedGeometries" ); // 9.0+

def_visible_primitive( sgsdk_UseShadingNetwork, "sgsdk_UseShadingNetwork" );                       // 5.4
def_visible_primitive( sgsdk_CreateMaterialMetadata, "sgsdk_CreateMaterialMetadata" );             // 5.4
def_visible_primitive( sgsdk_ConnectNodeToChannel, "sgsdk_ConnectNodeToChannel" );                 // 5.4
def_visible_primitive( sgsdk_ConnectSgChannelToNode, "sgsdk_ConnectSgChannelToNode" );             // 5.4
def_visible_primitive( sgsdk_ConnectNodes, "sgsdk_ConnectNodes" );                                 // 5.4
def_visible_primitive( sgsdk_CreateShadingTextureNode, "sgsdk_CreateShadingTextureNode" );         // 5.4
def_visible_primitive( sgsdk_CreateShadingInterpolateNode, "sgsdk_CreateShadingInterpolateNode" ); // 5.4
def_visible_primitive( sgsdk_SetUseNewMaterialSystem, "sgsdk_SetUseNewMaterialSystem" );           // 5.4
def_visible_primitive( sgsdk_SetDefaultParameter, "sgsdk_SetDefaultParameter" );                   // 5.4

def_visible_primitive( sgsdk_CreateShadingVertexColorNode, "sgsdk_CreateShadingVertexColorNode" );     // 5.4
def_visible_primitive( sgsdk_CreateShadingMultiplyNode, "sgsdk_CreateShadingMultiplyNode" );           // 5.4
def_visible_primitive( sgsdk_CreateShadingDivideNode, "sgsdk_CreateShadingDivideNode" );               // 5.4
def_visible_primitive( sgsdk_CreateShadingAddNode, "sgsdk_CreateShadingAddNode" );                     // 5.4
def_visible_primitive( sgsdk_CreateShadingSubtractNode, "sgsdk_CreateShadingSubtractNode" );           // 5.4
def_visible_primitive( sgsdk_CreateShadingClampNode, "sgsdk_CreateShadingClampNode" );                 // 5.4
def_visible_primitive( sgsdk_CreateShadingColorNode, "sgsdk_CreateShadingColorNode" );                 // 5.4
def_visible_primitive( sgsdk_CreateShadingSwizzlingNode, "sgsdk_CreateShadingSwizzlingNode" );         // 5.4
def_visible_primitive( sgsdk_CreateShadingLayeredBlendNode, "sgsdk_CreateShadingLayeredBlendNode" );   // 8.0+
def_visible_primitive( sgsdk_CreateShadingPowNode, "sgsdk_CreateShadingPowNode" );                     // 8.0+
def_visible_primitive( sgsdk_CreateShadingStepNode, "sgsdk_CreateShadingStepNode" );                   // 8.0+
def_visible_primitive( sgsdk_CreateShadingNormalize3Node, "sgsdk_CreateShadingNormalize3Node" );       // 8.2+
def_visible_primitive( sgsdk_CreateShadingSqrtNode, "sgsdk_CreateShadingSqrtNode" );                   // 8.2+
def_visible_primitive( sgsdk_CreateShadingDot3Node, "sgsdk_CreateShadingDot3Node" );                   // 8.2+
def_visible_primitive( sgsdk_CreateShadingCross3Node, "sgsdk_CreateShadingCross3Node" );               // 8.2+
def_visible_primitive( sgsdk_CreateShadingCosNode, "sgsdk_CreateShadingCosNode" );                     // 8.2+
def_visible_primitive( sgsdk_CreateShadingSinNode, "sgsdk_CreateShadingSinNode" );                     // 8.2+
def_visible_primitive( sgsdk_CreateShadingMaxNode, "sgsdk_CreateShadingMaxNode" );                     // 8.2+
def_visible_primitive( sgsdk_CreateShadingMinNode, "sgsdk_CreateShadingMinNode" );                     // 8.2+
def_visible_primitive( sgsdk_CreateShadingEqualNode, "sgsdk_CreateShadingEqualNode" );                 // 8.2+
def_visible_primitive( sgsdk_CreateShadingNotEqualNode, "sgsdk_CreateShadingNotEqualNode" );           // 8.2+
def_visible_primitive( sgsdk_CreateShadingGreaterThanNode, "sgsdk_CreateShadingGreaterThanNode" );     // 8.2+
def_visible_primitive( sgsdk_CreateShadingLessThanNode, "sgsdk_CreateShadingLessThanNode" );           // 8.2+
def_visible_primitive( sgsdk_CreateShadingGeometryFieldNode, "sgsdk_CreateShadingGeometryFieldNode" ); // 9.1+

def_visible_primitive( sgsdk_AddAttributeToNode, "sgsdk_AddAttributeToNode" );                           // 5.4
def_visible_primitive( sgsdk_VertexColorNodeSetVertexChannel, "sgsdk_VertexColorNodeSetVertexChannel" ); // 6.0
def_visible_primitive( sgsdk_SwizzlingNodeSetChannelSwizzle, "sgsdk_SwizzlingNodeSetChannelSwizzle" );   // 6.0
def_visible_primitive( sgsdk_GeometryFieldNodeSetFieldName, "sgsdk_GeometryFieldNodeSetFieldName" );     // 9.1+
def_visible_primitive( sgsdk_GeometryFieldNodeSetFieldIndex, "sgsdk_GeometryFieldNodeSetFieldIndex" );   // 9.1+
def_visible_primitive( sgsdk_GeometryFieldNodeSetFieldType, "sgsdk_GeometryFieldNodeSetFieldType" );     // 9.1+

def_visible_primitive( sgsdk_SetShadingNetworkClearInfo, "sgsdk_SetShadingNetworkClearInfo" );         // 5.4
def_visible_primitive( sgsdk_ConnectOutputToDirectXMaterial, "sgsdk_ConnectOutputToDirectXMaterial" ); // 9.0 (renamed)
def_visible_primitive( sgsdk_GetLODSwtichCameraDistance, "sgsdk_GetLODSwtichCameraDistance" );         // 6.1
def_visible_primitive( sgsdk_GetLODSwitchPixelSize, "sgsdk_GetLODSwitchPixelSize" );                   // 6.1
def_visible_primitive( sgsdk_EnableEdgeSets, "sgsdk_EnableEdgeSets" );                                 // 6.1

def_visible_primitive( sgsdk_SetMappingChannel, "sgsdk_SetMappingChannel" );                 // 6.2++
def_visible_primitive( sgsdk_SetSRGB, "sgsdk_SetSRGB" );                                     // 6.2++
def_visible_primitive( sgsdk_SetUseTangentSpaceNormals, "sgsdk_SetUseTangentSpaceNormals" ); // 9.0

def_visible_primitive( sgsdk_SetUVTiling, "sgsdk_SetUVTiling" ); // 8.2+
def_visible_primitive( sgsdk_SetUTiling, "sgsdk_SetUTiling" );   // 8.2+
def_visible_primitive( sgsdk_SetVTiling, "sgsdk_SetVTiling" );   // 8.2+
def_visible_primitive( sgsdk_SetUVOffset, "sgsdk_SetUVOffset" ); // 8.2+
def_visible_primitive( sgsdk_SetUOffset, "sgsdk_SetUOffset" );   // 8.2+
def_visible_primitive( sgsdk_SetVOffset, "sgsdk_SetVOffset" );   // 8.2+

// switches on/off reading of edge sets
Value* sgsdk_EnableEdgeSets_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_EnableEdgeSets", 1, count );

	SimplygonMaxInstance->SetEnableEdgeSets( arg_list[ 0 ]->to_bool() == TRUE );

	return &true_value;
}

// utility method for switching camera distance
Value* sgsdk_GetLODSwtichCameraDistance_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetLODSwtichCameraDistance", 1, count );

	const double distance = SimplygonMaxInstance->GetLODSwitchCameraDistance( arg_list[ 0 ]->to_int() );

	return Double::intern( distance );
}

// utility method for switching pixel size
Value* sgsdk_GetLODSwitchPixelSize_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetLODSwitchPixelSize", 1, count );

	const int pixelSize = (int)SimplygonMaxInstance->GetLODSwitchPixelSize( arg_list[ 0 ]->to_double() );

	return Integer::intern( pixelSize );
}

// connects baked textures on material channel to shader's (effectFile) texture slot
Value* sgsdk_ConnectOutputToDirectXMaterial_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_ConnectOutputToDirectXMaterial", 3, count );

	ShadingNetworkProxyWriteBack* materialProxy = SimplygonMaxInstance->GetProxyShadingNetworkWritebackMaterial();
	bool bResult = false;
	if( materialProxy == nullptr )
	{
		SimplygonMaxInstance->CreateProxyShadingNetworkWritebackMaterial( arg_list[ 0 ]->to_string(), DX11Shader );
		materialProxy = SimplygonMaxInstance->GetProxyShadingNetworkWritebackMaterial();

		std::basic_string<TCHAR> tChannel = arg_list[ 1 ]->to_string();
		materialProxy->SGChannelToShadingNode[ tChannel ] = arg_list[ 2 ]->to_string();
		bResult = true;
	}
	else
	{
		std::basic_string<TCHAR> tChannel = arg_list[ 1 ]->to_string();
		materialProxy->SGChannelToShadingNode[ tChannel ] = arg_list[ 2 ]->to_string();
		bResult = true;
	}

	return ( bResult ) ? &true_value : &false_value;
}

// enables the shading network pipeline
Value* sgsdk_UseShadingNetwork_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_UseShadingNetwork", 1, count );

	SimplygonMaxInstance->UseNewMaterialSystem = ( arg_list[ 0 ]->to_bool() == TRUE );

	return &true_value;
}

Value* sgsdk_SetShadingNetworkClearInfo_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SetShadingNetworkClearInfo", 2, count );

	SimplygonMaxInstance->SetShadingNetworkClearInfo( ( arg_list[ 0 ]->to_bool() == TRUE ), arg_list[ 1 ]->to_int() );

	return &true_value;
}

// connect functions
Value* sgsdk_ConnectNodeToChannel_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_ConnectNodeToChannel", 3, count );

	const bool bResult = SimplygonMaxInstance->ConnectRootNodeToChannel( arg_list[ 0 ]->to_int(), arg_list[ 1 ]->to_int(), arg_list[ 2 ]->to_string() );

	return ( bResult ) ? &true_value : &false_value;
}

// creates a material proxy for shader based material
Value* sgsdk_CreateMaterialMetadata_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_CreateMaterialMetadata", 1, count );
	one_value_local( ret );

	const int materialId = SimplygonMaxInstance->CreateProxyShadingNetworkMaterial( arg_list[ 0 ]->to_string(), DX11Shader );
	if( materialId < 0 )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_CreateMaterialMetadata: Material already exists (");
		tErrorMessage += arg_list[ 0 ]->to_string();
		tErrorMessage += _T(")");
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	return Integer::intern( materialId );
}

Value* sgsdk_ConnectSgChannelToNode_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_ConnectSgChannelToNode", 2, count );

	const bool bResult = SimplygonMaxInstance->ConnectSgChannelToMaterialNode( arg_list[ 0 ]->to_string(), arg_list[ 1 ]->to_string() );

	return ( bResult ) ? &true_value : &false_value;
}

Value* sgsdk_ConnectNodes_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_ConnectNodes", 3, count );

	const bool bResult = SimplygonMaxInstance->SetInputNode( arg_list[ 0 ]->to_int(), arg_list[ 1 ]->to_int(), arg_list[ 2 ]->to_int() );

	return ( bResult ) ? &true_value : &false_value;
}

Value* sgsdk_VertexColorNodeSetVertexChannel_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_VertexColorNodeSetVertexChannel", 2, count );

	const bool bResult = SimplygonMaxInstance->SetVertexColorChannel( arg_list[ 0 ]->to_int(), arg_list[ 1 ]->to_int() );

	return ( bResult ) ? &true_value : &false_value;
}

Value* sgsdk_SwizzlingNodeSetChannelSwizzle_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SwizzlingNodeSetChannelSwizzle", 3, count );

	const bool bResult = SimplygonMaxInstance->SetSwizzleChannel( arg_list[ 0 ]->to_int(), arg_list[ 1 ]->to_int(), arg_list[ 2 ]->to_int() );

	return ( bResult ) ? &true_value : &false_value;
}

Value* sgsdk_GeometryFieldNodeSetFieldName_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GeometryFieldNodeSetFieldName", 2, count );

	const bool bResult = SimplygonMaxInstance->SetGeometryFieldName( arg_list[ 0 ]->to_int(), arg_list[ 1 ]->to_string() );

	return ( bResult ) ? &true_value : &false_value;
}

Value* sgsdk_GeometryFieldNodeSetFieldIndex_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GeometryFieldNodeSetFieldIndex", 2, count );

	const bool bResult = SimplygonMaxInstance->SetGeometryFieldIndex( arg_list[ 0 ]->to_int(), arg_list[ 1 ]->to_int() );

	return ( bResult ) ? &true_value : &false_value;
}

Value* sgsdk_GeometryFieldNodeSetFieldType_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GeometryFieldNodeSetFieldType", 2, count );

	const bool bResult = SimplygonMaxInstance->SetGeometryFieldType( arg_list[ 0 ]->to_int(), arg_list[ 1 ]->to_int() );

	return ( bResult ) ? &true_value : &false_value;
}

// create node functions
sgsdk_create_node_cf( ShadingTextureNode );
sgsdk_create_node_cf( ShadingInterpolateNode );
sgsdk_create_node_cf( ShadingVertexColorNode );
sgsdk_create_node_cf( ShadingClampNode );
sgsdk_create_node_cf( ShadingMultiplyNode );
sgsdk_create_node_cf( ShadingDivideNode );
sgsdk_create_node_cf( ShadingAddNode );
sgsdk_create_node_cf( ShadingSubtractNode );
sgsdk_create_node_cf( ShadingColorNode );
sgsdk_create_node_cf( ShadingSwizzlingNode );
sgsdk_create_node_cf( ShadingLayeredBlendNode );
sgsdk_create_node_cf( ShadingPowNode );
sgsdk_create_node_cf( ShadingStepNode );
sgsdk_create_node_cf( ShadingNormalize3Node );
sgsdk_create_node_cf( ShadingSqrtNode );
sgsdk_create_node_cf( ShadingDot3Node );
sgsdk_create_node_cf( ShadingCross3Node );
sgsdk_create_node_cf( ShadingCosNode );
sgsdk_create_node_cf( ShadingSinNode );
sgsdk_create_node_cf( ShadingMaxNode );
sgsdk_create_node_cf( ShadingMinNode );
sgsdk_create_node_cf( ShadingEqualNode );
sgsdk_create_node_cf( ShadingNotEqualNode );
sgsdk_create_node_cf( ShadingGreaterThanNode );
sgsdk_create_node_cf( ShadingLessThanNode );
sgsdk_create_node_cf( ShadingGeometryFieldNode );

// default and input functions

// enables the shading network pipeline
Value* sgsdk_SetUseNewMaterialSystem_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SetUseNewMaterialSystem", 1, count );

	const bool bResult = SimplygonMaxInstance->UseNewMaterialSystem = arg_list[ 0 ]->to_bool() == TRUE;

	return ( bResult ) ? &true_value : &false_value;
}

// sets the default parameter for the given shading node
Value* sgsdk_SetDefaultParameter_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SetDefaultParameter", 6, count );

	const bool bResult = SimplygonMaxInstance->SetDefaultParameter( arg_list[ 0 ]->to_int(),
	                                                                arg_list[ 1 ]->to_int(),
	                                                                arg_list[ 2 ]->to_float(),
	                                                                arg_list[ 3 ]->to_float(),
	                                                                arg_list[ 4 ]->to_float(),
	                                                                arg_list[ 5 ]->to_float() );
	return ( bResult ) ? &true_value : &false_value;
}

// adds attributes to node, for example which shader parameter to read the mapping channel from
Value* sgsdk_AddAttributeToNode_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_AddAttributeToNode", 3, count );

	const bool bResult = SimplygonMaxInstance->AddNodeAttribute( arg_list[ 0 ]->to_int(), arg_list[ 1 ]->to_string(), arg_list[ 2 ]->to_int() );

	return ( bResult ) ? &true_value : &false_value;
}

// overrides the mapping channel for the given texture node
Value* sgsdk_SetMappingChannel_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SetMappingChannel", 2, count );

	const bool bResult = SimplygonMaxInstance->SetUV( arg_list[ 0 ]->to_int(), arg_list[ 1 ]->to_int() );

	return ( bResult ) ? &true_value : &false_value;
}

// overrides the sRGB flag for the given texture node
Value* sgsdk_SetSRGB_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SetSRGB", 2, count );

	const bool bResult = SimplygonMaxInstance->SetSRGB( arg_list[ 0 ]->to_int(), arg_list[ 1 ]->to_bool() == TRUE );

	return ( bResult ) ? &true_value : &false_value;
}

// overrides the tangent space flag for the given material
Value* sgsdk_SetUseTangentSpaceNormals_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SetUseTangentSpaceNormals", 2, count );

	const bool bResult = SimplygonMaxInstance->SetUseTangentSpaceNormals( arg_list[ 0 ]->to_string(), arg_list[ 1 ]->to_bool() == TRUE );

	return ( bResult ) ? &true_value : &false_value;
}

// overrides the UV-tiling for the given texture node
Value* sgsdk_SetUVTiling_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SetUVTiling", 3, count );

	const bool bResult = SimplygonMaxInstance->SetUVTiling( arg_list[ 0 ]->to_int(), arg_list[ 1 ]->to_float(), arg_list[ 2 ]->to_float() );

	return ( bResult ) ? &true_value : &false_value;
}

// overrides U-tiling for the given texture node
Value* sgsdk_SetUTiling_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SetUTiling", 2, count );

	const bool bResult = SimplygonMaxInstance->SetUTiling( arg_list[ 0 ]->to_int(), arg_list[ 1 ]->to_float() );

	return ( bResult ) ? &true_value : &false_value;
}

// overrides V-tiling for the given texture node
Value* sgsdk_SetVTiling_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SetVTiling", 2, count );

	const bool bResult = SimplygonMaxInstance->SetVTiling( arg_list[ 0 ]->to_int(), arg_list[ 1 ]->to_float() );

	return ( bResult ) ? &true_value : &false_value;
}

// overrides UV-offset for the given texture node
Value* sgsdk_SetUVOffset_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SetUVOffset", 3, count );

	const bool bResult = SimplygonMaxInstance->SetUVOffset( arg_list[ 0 ]->to_int(), arg_list[ 1 ]->to_float(), arg_list[ 2 ]->to_float() );

	return ( bResult ) ? &true_value : &false_value;
}

// overrides the U-offset for the given texture node
Value* sgsdk_SetUOffset_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SetUOffset", 2, count );

	const bool bResult = SimplygonMaxInstance->SetUOffset( arg_list[ 0 ]->to_int(), arg_list[ 1 ]->to_float() );

	return ( bResult ) ? &true_value : &false_value;
}

// overrides V-offset for the given texture node
Value* sgsdk_SetVOffset_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SetVOffset", 2, count );

	const bool bResult = SimplygonMaxInstance->SetVOffset( arg_list[ 0 ]->to_int(), arg_list[ 1 ]->to_float() );

	return ( bResult ) ? &true_value : &false_value;
}

// returns a list of processed mesh names
Value* sgsdk_GetProcessedMeshes_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetProcessedMeshes", 0, count );
	one_typed_value_local( Array * result );

	MaterialInfoHandler* materialInfoHandler = SimplygonMaxInstance->GetMaterialInfoHandler();
	std::vector<std::basic_string<TCHAR>>& tMeshList = materialInfoHandler->GetMeshes();

	const int listSize = (int)tMeshList.size();
	vl.result = new Array( listSize );

	for( int index = 0; index < listSize; ++index )
	{
		vl.result->append( new String( tMeshList[ index ].c_str() ) );
	}

	return_value( vl.result );
}

// gets a list of all baked materials
Value* sgsdk_GetMaterials_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetMaterials", 0, count );
	one_typed_value_local( Array * result );

	MaterialInfoHandler* materialInfoHandler = SimplygonMaxInstance->GetMaterialInfoHandler();
	std::vector<std::basic_string<TCHAR>>& tMaterialList = materialInfoHandler->GetMaterialsWithCustomChannels();

	const int listSize = (int)tMaterialList.size();
	vl.result = new Array( listSize );

	for( int index = 0; index < tMaterialList.size(); ++index )
	{
		vl.result->append( new String( tMaterialList[ index ].c_str() ) );
	}

	return_value( vl.result );
}

// gets a list of material channels for the specified material
Value* sgsdk_GetChannelsForMaterial_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetChannelsForMaterial", 1, count );
	one_typed_value_local( Array * result );

	std::basic_string<TCHAR> tMaterial = arg_list[ 0 ]->to_string();

	MaterialInfoHandler* materialInfoHandler = SimplygonMaxInstance->GetMaterialInfoHandler();
	std::vector<std::basic_string<TCHAR>>& tChannelList = materialInfoHandler->GetCustomChannelsForMaterial( tMaterial );

	const int listSize = (int)tChannelList.size();
	vl.result = new Array( listSize );

	for( int index = 0; index < listSize; ++index )
	{
		vl.result->append( new String( tChannelList[ index ].c_str() ) );
	}

	return_value( vl.result );
}

// gets the baked material for the specified mesh
Value* sgsdk_GetMaterialForMesh_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetMaterialForMesh", 1, count );

	std::basic_string<TCHAR> tMesh = arg_list[ 0 ]->to_string();

	MaterialInfoHandler* materialInfoHandler = SimplygonMaxInstance->GetMaterialInfoHandler();
	std::basic_string<TCHAR> tMaterial = materialInfoHandler->GetMaterialForMesh( tMesh );

	one_value_local( ret );
	vl.ret = new String( tMaterial.c_str() );
	return_value( vl.ret );
}

// gets the baked material for the specified mesh
Value* sgsdk_GetMaterialsForMesh_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetMaterialsForMesh", 1, count );
	one_typed_value_local( Array * result );

	std::basic_string<TCHAR> tMesh = arg_list[ 0 ]->to_string();

	MaterialInfoHandler* materialInfoHandler = SimplygonMaxInstance->GetMaterialInfoHandler();
	std::vector<std::basic_string<TCHAR>> tMateriala = materialInfoHandler->GetMaterialsForMesh( tMesh );

	const int listSize = (int)tMateriala.size();
	vl.result = new Array( listSize );

	for( int index = 0; index < listSize; ++index )
	{
		vl.result->append( new String( tMateriala[ index ].c_str() ) );
	}

	return_value( vl.result );
}

// gets the baked sub-materials for the specified material
Value* sgsdk_GetSubMaterials_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetSubMaterials", 1, count );
	one_typed_value_local( Array * result );

	std::basic_string<TCHAR> tMaterial = arg_list[ 0 ]->to_string();

	MaterialInfoHandler* materialInfoHandler = SimplygonMaxInstance->GetMaterialInfoHandler();
	std::map<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>> tSubMaterials = materialInfoHandler->GetSubMaterials( tMaterial );

	const int listSize = (int)tSubMaterials.size();
	vl.result = new Array( listSize );

	for( std::map<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>>::const_iterator& subMaterialIterator = tSubMaterials.begin();
	     subMaterialIterator != tSubMaterials.end();
	     subMaterialIterator++ )
	{
		vl.result->append( new String( subMaterialIterator->first.c_str() ) );
	}

	return_value( vl.result );
}

// gets the index of the specified sub-material
Value* sgsdk_GetSubMaterialIndex_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetSubMaterialIndex", 2, count );

	std::basic_string<TCHAR> tMaterial = arg_list[ 0 ]->to_string();
	std::basic_string<TCHAR> tSubMaterial = arg_list[ 1 ]->to_string();

	MaterialInfoHandler* materialInfoHandler = SimplygonMaxInstance->GetMaterialInfoHandler();
	int subMaterialIndex = materialInfoHandler->GetSubMaterialIndex( tMaterial, tSubMaterial );

	one_value_local( ret );
	return Integer::intern( subMaterialIndex );
}

// gets the baked texture for the given material channel
Value* sgsdk_GetTexturePathForChannel_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetTexturePathForChannel", 2, count );

	std::basic_string<TCHAR> tMaterial = arg_list[ 0 ]->to_string();
	std::basic_string<TCHAR> tChannel = arg_list[ 1 ]->to_string();

	MaterialInfoHandler* materialInfoHandler = SimplygonMaxInstance->GetMaterialInfoHandler();
	std::basic_string<TCHAR> tTexturePath = materialInfoHandler->GetTextureNameForMaterialChannel( tMaterial, tChannel );

	one_value_local( ret );
	vl.ret = new String( tTexturePath.c_str() );
	return_value( vl.ret );
}

// gets the baked texture for the given material channel
Value* sgsdk_GetMappingChannelForChannel_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetMappingChannelForChannel", 2, count );

	std::basic_string<TCHAR> tMaterial = arg_list[ 0 ]->to_string();
	std::basic_string<TCHAR> tChannel = arg_list[ 1 ]->to_string();

	MaterialInfoHandler* materialInfoHandler = SimplygonMaxInstance->GetMaterialInfoHandler();
	const int mappingChannel = materialInfoHandler->GetMappingChannelForMaterialChannel( tMaterial, tChannel );

	one_value_local( ret );
	return Integer::intern( mappingChannel );
}

// specifies whether the Simplygon plugin should create a material or not at writeback
Value* sgsdk_SetGenerateMaterial_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SetGenerateMaterial", 1, count );

	SimplygonMaxInstance->SetGenerateMaterial( arg_list[ 0 ]->to_bool() == TRUE );

	return &true_value;
}

// returns the reused material, if any
Value* sgsdk_GetMeshReusesMaterial_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetMeshReusesMaterial", 1, count );

	std::basic_string<TCHAR> tMesh = arg_list[ 0 ]->to_string();
	MaterialInfoHandler* materialInfoHandler = SimplygonMaxInstance->GetMaterialInfoHandler();
	std::basic_string<TCHAR> tReuseMaterial = materialInfoHandler->MeshReusesMaterial( tMesh );

	one_value_local( ret );
	vl.ret = new String( tReuseMaterial.c_str() );
	return_value( vl.ret );
}

// returns reused materials, if any
Value* sgsdk_GetMeshReusesMaterials_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetMeshReusesMaterials", 1, count );
	one_typed_value_local( Array * result );

	std::basic_string<TCHAR> tMesh = arg_list[ 0 ]->to_string();
	MaterialInfoHandler* materialInfoHandler = SimplygonMaxInstance->GetMaterialInfoHandler();
	std::vector<std::basic_string<TCHAR>> tReuseMaterial = materialInfoHandler->MeshReusesMaterials( tMesh );

	const int listSize = (int)tReuseMaterial.size();
	vl.result = new Array( listSize );

	for( int index = 0; index < listSize; ++index )
	{
		vl.result->append( new String( tReuseMaterial[ index ].c_str() ) );
	}

	return_value( vl.result );
}

// returns the name of the reused material, if any
Value* sgsdk_GetMaterialReusesSubMaterial_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetMaterialReusesSubMaterial", 2, count );

	std::basic_string<TCHAR> tMaterial = arg_list[ 0 ]->to_string();
	std::basic_string<TCHAR> tSubMaterial = arg_list[ 1 ]->to_string();

	MaterialInfoHandler* materialInfoHandler = SimplygonMaxInstance->GetMaterialInfoHandler();
	std::basic_string<TCHAR> tReuseMaterial = materialInfoHandler->MaterialReusesSubMaterial( tMaterial, tSubMaterial );

	one_value_local( ret );
	vl.ret = new String( tReuseMaterial.c_str() );
	return_value( vl.ret );
}

Value* sgsdk_OverrideDefaultLODNamingPrefix_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_OverrideDefaultLODNamingPrefix", 1, count );
	SimplygonMaxInstance->DefaultPrefix = std::basic_string<TCHAR>( arg_list[ 0 ]->to_string() );

	return &true_value;
}

// standard function wrappers

// overrides the texture for the given material channel
Value* sgsdk_MaterialTexture_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_MaterialTexture", 4, count );

	const bool bResult = SimplygonMaxInstance->MaterialTexture(
	    arg_list[ 0 ]->to_string(), arg_list[ 1 ]->to_string(), arg_list[ 2 ]->to_string(), arg_list[ 3 ]->to_bool() == TRUE );

	return ( bResult ) ? &true_value : &false_value;
}

// overrides the mapping channel for the given material channel
Value* sgsdk_MaterialTextureMapChannel_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_MaterialTextureMapChannel", 3, count );

	const bool bResult = SimplygonMaxInstance->MaterialTextureMapChannel( arg_list[ 0 ]->to_string(), arg_list[ 1 ]->to_string(), arg_list[ 2 ]->to_int() );

	return ( bResult ) ? &true_value : &false_value;
}

// overrides a mapping channel to be handled as vertex colors instead of tex-coords
Value* sgsdk_SetIsVertexColorChannel_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SetIsVertexColorChannel", 2, count );

	const bool bResult = SimplygonMaxInstance->SetIsVertexColorChannel( arg_list[ 0 ]->to_int(), arg_list[ 1 ]->to_bool() );

	return ( bResult ) ? &true_value : &false_value;
}

// overrides the material color for the given material channel
Value* sgsdk_MaterialColor_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_MaterialColor", 6, count );

	const bool bResult = SimplygonMaxInstance->MaterialColor( arg_list[ 0 ]->to_string(),
	                                                          arg_list[ 1 ]->to_string(),
	                                                          arg_list[ 2 ]->to_float(),
	                                                          arg_list[ 3 ]->to_float(),
	                                                          arg_list[ 4 ]->to_float(),
	                                                          arg_list[ 5 ]->to_float() );

	return ( bResult ) ? &true_value : &false_value;
}

// starts export
Value* sgsdk_ExportToFile_cf( Value** arg_list, int count )
{
	// allow variable inputs (1-2)
	if( count < 1 || count > 2 )
	{
		check_arg_count( "sgsdk_ExportToFile", 1, count );
	}

	std::basic_string<TCHAR> tExportFilePath = arg_list[ 0 ]->to_string();

	// copy textures as default, use override if any
	const bool bCopyTextures = count == 2 ? arg_list[ 1 ]->to_bool() == TRUE : true;
	SimplygonMaxInstance->SetCopyTextures( bCopyTextures );

	// export scene to file
	SimplygonMaxInstance->extractionType = EXPORT_TO_FILE;
	const bool bExportedToScene = SimplygonMaxInstance->ExportSceneToFile( tExportFilePath );

	return ( bExportedToScene ) ? &true_value : &false_value;
}

// starts import
Value* sgsdk_ImportFromFile_cf( Value** arg_list, int count )
{
	if( count < 1 || count > 4 )
	{
		check_arg_count( "sgsdk_ImportFromFile", 1, count );
	}

	std::basic_string<TCHAR> tImportFilePath = arg_list[ 0 ]->to_string();

	// copy textures as default, use override if any
	const bool bCopyTextures = count >= 2 ? arg_list[ 1 ]->to_bool() == TRUE : true;
	SimplygonMaxInstance->SetCopyTextures( bCopyTextures );

	// do not link meshes as default, use override if any
	const bool bLinkMeshes = count >= 3 ? arg_list[ 2 ]->to_bool() == TRUE : false;
	SimplygonMaxInstance->SetLinkMeshes( bLinkMeshes );

	// do not link materials as default, use override if any
	const bool bLinkMaterials = count == 4 ? arg_list[ 3 ]->to_bool() == TRUE : false;
	SimplygonMaxInstance->SetLinkMaterials( bLinkMaterials );

	// import scene from file
	SimplygonMaxInstance->extractionType = IMPORT_FROM_FILE;
	const bool bImportedFromScene = SimplygonMaxInstance->ImportSceneFromFile( tImportFilePath );

	return ( bImportedFromScene ) ? &true_value : &false_value;
}

// sets clear global mapping flag
Value* sgsdk_ClearGlobalMapping_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_ClearGlobalMapping", 0, count );

	SimplygonMaxInstance->ClearGlobalMapping();

	return &true_value;
}

// sets the mesh format string
Value* sgsdk_SetMeshNameFormat_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SetMeshNameFormat", 1, count );

	SimplygonMaxInstance->SetMeshFormatString( arg_list[ 0 ]->to_string() );

	return &true_value;
}

// sets the initial LOD index (used at import)
Value* sgsdk_SetInitialLODIndex_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SetInitialLODIndex", 1, count );

	SimplygonMaxInstance->SetInitialLODIndex( arg_list[ 0 ]->to_int() );

	return &true_value;
}

// gets processed output file paths
Value* sgsdk_GetProcessedOutputPaths_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetProcessedOutputPaths", 0, count );
	one_typed_value_local( Array * result );

	std::vector<std::basic_string<TCHAR>> tProcessedSceneFilePaths = SimplygonMaxInstance->GetMaterialInfoHandler()->GetProcessedSceneFiles();

	const int listSize = (int)tProcessedSceneFilePaths.size();
	vl.result = new Array( listSize );

	for( int index = 0; index < listSize; ++index )
	{
		vl.result->append( new String( tProcessedSceneFilePaths[ index ].c_str() ) );
	}

	return_value( vl.result );
}

// resets important flags to their default state
Value* sgsdk_Reset_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_Reset", 0, count );

	SimplygonMaxInstance->Reset();

	return &true_value;
}

// specifies whether material colors should be exported
Value* sgsdk_UseMaterialColorsOverride_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_UseMaterialColorsOverride", 1, count );

	SimplygonMaxInstance->UseMaterialColors = arg_list[ 0 ]->to_bool() == TRUE;

	return &true_value;
}

Value* sgsdk_UseNonConflictingTextureNames_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_UseNonConflictingTextureNames", 1, count );

	SimplygonMaxInstance->UseNonConflictingTextureNames = arg_list[ 0 ]->to_bool() == TRUE;

	return &true_value;
}

// gets the texture path for the given channel (legacy)
Value* sgsdk_GetTexturePathForCustomChannel_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetTexturePathForCustomChannel", 2, count );
	one_value_local( ret );

	MaterialInfoHandler* materialInfoHandler = SimplygonMaxInstance->GetMaterialInfoHandler();
	std::basic_string<TCHAR> tTextureName = materialInfoHandler->GetTextureNameForMaterialChannel( arg_list[ 0 ]->to_string(), arg_list[ 1 ]->to_string() );

	vl.ret = new String( tTextureName.c_str() );
	return_value( vl.ret );
}

// gets a list of materials with custom channels (legacy)
Value* sgsdk_GetMaterialsWithCustomChannels_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetMaterialsWithCustomChannels", 0, count );
	one_typed_value_local( Array * result );

	MaterialInfoHandler* materialInfoHandler = SimplygonMaxInstance->GetMaterialInfoHandler();
	std::vector<std::basic_string<TCHAR>>& tMaterialList = materialInfoHandler->GetMaterialsWithCustomChannels();

	const int listSize = (int)tMaterialList.size();
	vl.result = new Array( listSize );

	for( int index = 0; index < listSize; ++index )
	{
		vl.result->append( new String( tMaterialList[ index ].c_str() ) );
	}

	return_value( vl.result );
}

// gets custom channels for the given material
Value* sgsdk_GetCustomChannelsForMaterial_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetCustomChannelsForMaterial", 1, count );
	one_typed_value_local( Array * result );

	MaterialInfoHandler* materialInfoHandler = SimplygonMaxInstance->GetMaterialInfoHandler();
	std::vector<std::basic_string<TCHAR>>& tChannelList = materialInfoHandler->GetCustomChannelsForMaterial( arg_list[ 0 ]->to_string() );

	const int listSize = (int)tChannelList.size();
	vl.result = new Array( listSize );

	for( int m = 0; m < listSize; ++m )
	{
		vl.result->append( new String( tChannelList[ m ].c_str() ) );
	}

	return_value( vl.result );
}

// overrides the texture output directory
Value* sgsdk_SetTextureOutputDirectory_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SetTextureOutputDirectory", 1, count );

	SimplygonMaxInstance->UseNonConflictingTextureNames = arg_list[ 0 ]->to_string() != nullptr;
	SimplygonMaxInstance->TextureOutputDirectory = CorrectPath( std::basic_string<TCHAR>( arg_list[ 0 ]->to_string() ) );

	return &true_value;
}

// new settings pipeline
Value* sgsdk_CreatePipeline_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_CreatePipeline", 1, count );

	std::basic_string<TCHAR> tPipelineType = std::basic_string<TCHAR>( arg_list[ 0 ]->to_string() );

	INT64 pipelineId = -1;
	try
	{
		pipelineId = PipelineHelper::Instance()->CreateSettingsPipeline( tPipelineType );
	}
	catch( std::exception ex )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_CreatePipeline: Failed to add pipeline (");
		tErrorMessage += tPipelineType;
		tErrorMessage += _T(") - ");
		tErrorMessage += ConstCharPtrToLPCTSTR( ex.what() );
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	return Integer::intern( pipelineId );
}

Value* sgsdk_DeletePipeline_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_DeletePipeline", 1, count );

	const INT64 pipelineId = arg_list[ 0 ]->to_int64();

	bool bRemoved = false;
	try
	{
		bRemoved = PipelineHelper::Instance()->RemoveSettingsPipeline( pipelineId );
	}
	catch( std::exception ex )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_DeletePipeline: Failed to remove pipeline (");
		tErrorMessage += pipelineId;
		tErrorMessage += _T(") - ");
		tErrorMessage += ConstCharPtrToLPCTSTR( ex.what() );
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	return bRemoved ? &true_value : &false_value;
}

Value* sgsdk_ClearPipelines_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_ClearPipelines", 0, count );

	bool bRemoved = false;
	try
	{
		bRemoved = PipelineHelper::Instance()->ClearAllSettingsPipelines();
	}
	catch( std::exception ex )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_ClearPipelines: Failed to remove all pipelines");
		tErrorMessage += _T(" - ");
		tErrorMessage += ConstCharPtrToLPCTSTR( ex.what() );
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	return bRemoved ? &true_value : &false_value;
}

Value* sgsdk_LoadPipeline_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_LoadPipeline", 1, count );

	std::basic_string<TCHAR> tPipelineFilePath = std::basic_string<TCHAR>( arg_list[ 0 ]->to_string() );

	INT64 pipelineId = -1;
	try
	{
		pipelineId = PipelineHelper::Instance()->LoadSettingsPipeline( tPipelineFilePath );
	}
	catch( std::exception ex )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_LoadPipeline: Failed to load pipeline (");
		tErrorMessage += tPipelineFilePath;
		tErrorMessage += _T(") - ");
		tErrorMessage += ConstCharPtrToLPCTSTR( ex.what() );
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	return Integer::intern( pipelineId );
}

Value* sgsdk_SavePipeline_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SavePipeline", 2, count );

	const INT64 pipelineId = arg_list[ 0 ]->to_int64();
	std::basic_string<TCHAR> tPipelineFilePath = std::basic_string<TCHAR>( arg_list[ 1 ]->to_string() );

	bool bSaved = false;
	try
	{
		bSaved = PipelineHelper::Instance()->SaveSettingsPipeline( pipelineId, tPipelineFilePath );
	}
	catch( std::exception ex )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_SavePipeline: Failed to save pipeline (");
		tErrorMessage += tPipelineFilePath;
		tErrorMessage += _T(") - ");
		tErrorMessage += ConstCharPtrToLPCTSTR( ex.what() );
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	return bSaved ? &true_value : &false_value;
}

Value* sgsdk_ClonePipeline_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_ClonePipeline", 1, count );

	const INT64 pipelineId = arg_list[ 0 ]->to_int64();
	INT64 clonedPipelineId = -1;

	try
	{
		clonedPipelineId = PipelineHelper::Instance()->CloneSettingsPipeline( pipelineId );
	}
	catch( std::exception ex )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_ClonePipeline: Failed to clone pipeline (");
		tErrorMessage += pipelineId;
		tErrorMessage += _T(") - ");
		tErrorMessage += ConstCharPtrToLPCTSTR( ex.what() );
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	return Integer::intern( clonedPipelineId );
}

Value* sgsdk_GetPipelines_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetPipelines", 0, count );
	one_typed_value_local( Array * result );

	std::vector<INT64>& pipelineIds = PipelineHelper::Instance()->GetPipelines();

	const int listSize = (int)pipelineIds.size();
	vl.result = new Array( listSize );

	for( int index = 0; index < listSize; ++index )
	{
		vl.result->append( Integer::intern( pipelineIds[ index ] ) );
	}

	return_value( vl.result );
}

Value* sgsdk_GetPipelineType_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetPipelineType", 1, count );
	one_value_local( ret );

	const INT64 pipelineId = arg_list[ 0 ]->to_int64();

	std::basic_string<TCHAR> tPipelineType = _T("");
	try
	{
		tPipelineType = PipelineHelper::Instance()->GetPipelineType( pipelineId );
	}
	catch( std::exception ex )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_GetPipelineType: Failed to get type (");
		tErrorMessage += pipelineId;
		tErrorMessage += _T(") - ");
		tErrorMessage += ConstCharPtrToLPCTSTR( ex.what() );
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	vl.ret = new String( tPipelineType.c_str() );
	return_value( vl.ret );
}

Value* sgsdk_GetSetting_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_GetSetting", 2, count );
	one_value_local( ret );

	const INT64 pipelineId = arg_list[ 0 ]->to_int64();
	std::basic_string<TCHAR> tPipelineSettingPath = std::basic_string<TCHAR>( arg_list[ 1 ]->to_string() );

	const ESettingValueType sgParameterType = PipelineHelper::Instance()->GetPipelineSettingType( pipelineId, tPipelineSettingPath );
	if( sgParameterType == ESettingValueType::Invalid )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_GetSetting: Failed to get setting (");
		tErrorMessage += tPipelineSettingPath;
		tErrorMessage += _T(") - The setting is invalid.");

		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	Value* mValue = nullptr;
	bool bSet = false;
	bool bInvalidType = false;

	try
	{
		if( sgParameterType == ESettingValueType::Double )
		{
			double dValue = 0.0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, dValue );
			mValue = Double::intern( dValue );
		}
		else if( sgParameterType == ESettingValueType::Bool )
		{
			bool bValue = false;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, bValue );
			mValue = bValue ? &true_value : &false_value;
		}
		else if( sgParameterType == ESettingValueType::Int )
		{
			int iValue = 0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, iValue );
			mValue = Integer::intern( iValue );
		}
		else if( sgParameterType == ESettingValueType::String )
		{
			std::basic_string<TCHAR> tValue = _T("");
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, tValue );
			mValue = new String( tValue.c_str() );
		}
		else if( sgParameterType == ESettingValueType::Uint )
		{
			uint uValue = false;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, uValue );
			mValue = Integer::intern( uValue );
		}
		else if( sgParameterType == ESettingValueType::EPipelineRunMode )
		{
			EPipelineRunMode eValue = (EPipelineRunMode)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::EChartAggregatorMode )
		{
			EChartAggregatorMode eValue = (EChartAggregatorMode)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::ETexcoordGeneratorType )
		{
			ETexcoordGeneratorType eValue = (ETexcoordGeneratorType)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::EOcclusionMode )
		{
			EOcclusionMode eValue = (EOcclusionMode)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::EStopCondition )
		{
			EStopCondition eValue = (EStopCondition)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::EDataCreationPreferences )
		{
			EDataCreationPreferences eValue = (EDataCreationPreferences)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::EReductionHeuristics )
		{
			EReductionHeuristics eValue = (EReductionHeuristics)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::EWeightsFromColorMode )
		{
			EWeightsFromColorMode eValue = (EWeightsFromColorMode)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::ESurfaceTransferMode )
		{
			ESurfaceTransferMode eValue = (ESurfaceTransferMode)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::ERemeshingMode )
		{
			ERemeshingMode eValue = (ERemeshingMode)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::ETangentSpaceMethod )
		{
			ETangentSpaceMethod eValue = (ETangentSpaceMethod)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::EGeometryDataFieldType )
		{
			EGeometryDataFieldType eValue = (EGeometryDataFieldType)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::EAtlasFillMode )
		{
			EAtlasFillMode eValue = (EAtlasFillMode)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::EDitherPatterns )
		{
			EDitherPatterns eValue = (EDitherPatterns)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::EComputeVisibilityMode )
		{
			EComputeVisibilityMode eValue = (EComputeVisibilityMode)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::ESurfaceAreaScale )
		{
			ESurfaceAreaScale eValue = (ESurfaceAreaScale)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::EImpostorType )
		{
			EImpostorType eValue = (EImpostorType)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::ESymmetryAxis )
		{
			ESymmetryAxis eValue = (ESymmetryAxis)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::EPixelFormat )
		{
			EPixelFormat eValue = (EPixelFormat)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::EColorComponent )
		{
			EColorComponent eValue = (EColorComponent)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::EHoleFilling )
		{
			EHoleFilling eValue = (EHoleFilling)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::EImageOutputFormat )
		{
			EImageOutputFormat eValue = (EImageOutputFormat)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::EDDSCompressionType )
		{
			EDDSCompressionType eValue = (EDDSCompressionType)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::EBillboardMode )
		{
			EBillboardMode eValue = (EBillboardMode)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else if( sgParameterType == ESettingValueType::EOpacityType )
		{
			EOpacityType eValue = (EOpacityType)0;
			bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
			mValue = Integer::intern( (int)eValue );
		}
		else
		{
			bInvalidType = true;
		}
	}
	catch( std::exception ex )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_GetSetting: Failed to get setting (");
		tErrorMessage += tPipelineSettingPath;
		tErrorMessage += _T(")\n");
		tErrorMessage += ConstCharPtrToLPCTSTR( ex.what() );
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	if( !bSet )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_GetSetting: Failed to get setting (");
		tErrorMessage += tPipelineSettingPath;
		if( sgParameterType == ESettingValueType::Invalid )
		{
			tErrorMessage += _T(") - ");
			tErrorMessage += _T("The type is not supported and/or the setting does not exist.");
		}
		else if( bInvalidType )
		{
			tErrorMessage += _T(") - ");
			tErrorMessage += _T("The type is not supported, supported return types are: Int, UInt (through Int64), Double, Boolean, String.");
		}
		else
		{
			tErrorMessage += _T(").");
		}
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	return mValue;
}

template <typename T, typename Y> Y ChangeType( T value )
{
	return (Y)value;
}

template <typename T> bool ChangeTypeToBool( T value )
{
	return !!(int)value;
}

template <typename T> bool SetSetting( INT64 pipelineId, std::basic_string<TCHAR> tPipelineSettingPath, T valueToSet, ESettingValueType sgParameterType )
{
	if( sgParameterType == ESettingValueType::Int )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::Double )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, ChangeType<T, double>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::Uint )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, ChangeType<T, uint>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::Bool )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, ChangeTypeToBool<T>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::String )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, valueToSet );
	}
	else if( sgParameterType == ESettingValueType::EPipelineRunMode )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EPipelineRunMode)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::EChartAggregatorMode )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EChartAggregatorMode)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::ETexcoordGeneratorType )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (ETexcoordGeneratorType)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::EOcclusionMode )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EOcclusionMode)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::EStopCondition )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EStopCondition)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::EDataCreationPreferences )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EDataCreationPreferences)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::EReductionHeuristics )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EReductionHeuristics)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::EWeightsFromColorMode )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EWeightsFromColorMode)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::ESurfaceTransferMode )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (ESurfaceTransferMode)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::ERemeshingMode )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (ERemeshingMode)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::ETangentSpaceMethod )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (ETangentSpaceMethod)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::EGeometryDataFieldType )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EGeometryDataFieldType)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::EAtlasFillMode )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EAtlasFillMode)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::EDitherPatterns )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EDitherPatterns)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::EComputeVisibilityMode )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EComputeVisibilityMode)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::ESurfaceAreaScale )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (ESurfaceAreaScale)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::EImpostorType )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EImpostorType)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::ESymmetryAxis )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (ESymmetryAxis)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::EPixelFormat )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EPixelFormat)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::EColorComponent )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EColorComponent)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::EHoleFilling )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EHoleFilling)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::EImageOutputFormat )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EImageOutputFormat)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::EDDSCompressionType )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EDDSCompressionType)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::EBillboardMode )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EBillboardMode)ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == ESettingValueType::EOpacityType )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, (EOpacityType)ChangeType<T, int>( valueToSet ) );
	}
	else
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, valueToSet );
	}

	return false;
}

Value* sgsdk_SetSetting_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SetSetting", 3, count );

	const INT64 pipelineId = arg_list[ 0 ]->to_int64();
	std::basic_string<TCHAR> tPipelineSettingPath = std::basic_string<TCHAR>( arg_list[ 1 ]->to_string() );

	ValueMetaClass* mValueTag = arg_list[ 2 ]->tag;

	const ESettingValueType sgParameterType = PipelineHelper::Instance()->GetPipelineSettingType( pipelineId, tPipelineSettingPath );
	if( sgParameterType == ESettingValueType::Invalid )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_SetSetting: Failed to set setting (");
		tErrorMessage += tPipelineSettingPath;
		tErrorMessage += _T(") - The setting is invalid.");

		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	bool bSet = false;

	try
	{
		if( mValueTag == class_tag( Float ) )
		{
			bSet = SetSetting<float>( pipelineId, tPipelineSettingPath, arg_list[ 2 ]->to_float(), sgParameterType );
		}
		else if( mValueTag == class_tag( Double ) )
		{
			bSet = SetSetting<double>( pipelineId, tPipelineSettingPath, arg_list[ 2 ]->to_double(), sgParameterType );
		}
		else if( mValueTag == class_tag( Integer ) )
		{
			bSet = SetSetting<int>( pipelineId, tPipelineSettingPath, arg_list[ 2 ]->to_int(), sgParameterType );
		}
		else if( mValueTag == class_tag( Integer64 ) )
		{
			bSet = SetSetting<INT64>( pipelineId, tPipelineSettingPath, arg_list[ 2 ]->to_int64(), sgParameterType );
		}
		else if( mValueTag == class_tag( String ) )
		{
			bSet = PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, arg_list[ 2 ]->to_string() );
		}
		else if( mValueTag == class_tag( Boolean ) )
		{
			bSet = SetSetting<bool>( pipelineId, tPipelineSettingPath, arg_list[ 2 ]->to_bool() == TRUE, sgParameterType );
		}
		else
		{
			std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_SetSetting: Unsupported value type (");
			tErrorMessage += mValueTag->name;
			tErrorMessage += _T(") - Supported input types are: Float (through double), Double, Int, Int64 (through UInt), Boolean and String.");
			throw UserThrownError( tErrorMessage.c_str(), TRUE );
		}
	}

	catch( std::exception ex )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_SetSetting: Failed to set setting (");
		tErrorMessage += tPipelineSettingPath;
		tErrorMessage += _T(")\n");
		tErrorMessage += ConstCharPtrToLPCTSTR( ex.what() );

		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	if( !bSet )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_SetSetting: Failed to set setting (");
		tErrorMessage += tPipelineSettingPath;
		tErrorMessage += _T(").");
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	return bSet ? &true_value : &false_value;
}

Value* sgsdk_RunPipelineOnSelection_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_RunPipelineOnSelection", 1, count );

	INT64 pipelineId = -1;
	std::basic_string<TCHAR> tPipelineFilePath = _T("");

	ValueMetaClass* mValueTag = arg_list[ 0 ]->tag;

	if( mValueTag == class_tag( Integer ) )
	{
		pipelineId = (INT64)arg_list[ 0 ]->to_int();
	}
	else if( mValueTag == class_tag( Integer64 ) )
	{
		pipelineId = arg_list[ 0 ]->to_int64();
	}
	else if( mValueTag == class_tag( String ) )
	{
		tPipelineFilePath = std::basic_string<TCHAR>( arg_list[ 0 ]->to_string() );
	}
	else
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_RunPipelineOnSelection: Unsupported value type (");
		tErrorMessage += mValueTag->name;
		tErrorMessage += _T(") - Supported types are int/int64 (from CreatePipeline) and string (pipeline file path).");
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	if( pipelineId >= 0 )
	{
		SimplygonMaxInstance->UseSettingsPipelineForProcessing( pipelineId );
	}
	else if( tPipelineFilePath.size() > 0 )
	{
		try
		{
			pipelineId = PipelineHelper::Instance()->LoadSettingsPipeline( tPipelineFilePath );
			SimplygonMaxInstance->UseSettingsPipelineForProcessing( pipelineId );
		}
		catch( std::exception ex )
		{
			std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_RunPipelineOnSelection: Failed to load pipeline (");
			tErrorMessage += tPipelineFilePath;
			tErrorMessage += _T(")\n");
			tErrorMessage += ConstCharPtrToLPCTSTR( ex.what() );
			throw UserThrownError( tErrorMessage.c_str(), TRUE );
		}
	}
	else
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_RunPipelineOnSelection: Could not find a valid pipeline input.");
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	SimplygonMaxInstance->extractionType = BATCH_PROCESSOR;
	bool bProcessed = false;
	try
	{
		bProcessed = SimplygonMaxInstance->ProcessSelectedGeometries();
	}
	catch( std::exception ex )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_RunPipelineOnSelection: processing failed - ");
		tErrorMessage += ConstCharPtrToLPCTSTR( ex.what() );
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	return ( bProcessed ) ? &true_value : &false_value;
}

Value* sgsdk_RunPipelineOnFile_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_RunPipelineOnFile", 3, count );

	INT64 pipelineId = -1;
	std::basic_string<TCHAR> tPipelineFilePath = _T("");

	const ValueMetaClass* mValueTag = arg_list[ 0 ]->tag;

	if( mValueTag == class_tag( Integer ) )
	{
		pipelineId = (INT64)arg_list[ 0 ]->to_int();
	}
	else if( mValueTag == class_tag( Integer64 ) )
	{
		pipelineId = arg_list[ 0 ]->to_int64();
	}
	else if( mValueTag == class_tag( String ) )
	{
		tPipelineFilePath = std::basic_string<TCHAR>( arg_list[ 0 ]->to_string() );
	}
	else
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_RunPipelineOnFile: Unsupported value type (");
		tErrorMessage += mValueTag->name;
		tErrorMessage += _T(") - Supported types are int/int64 (from CreatePipeline) and string (pipeline file path).");
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	if( pipelineId >= 0 )
	{
		SimplygonMaxInstance->UseSettingsPipelineForProcessing( pipelineId );
	}
	else if( tPipelineFilePath.size() > 0 )
	{
		try
		{
			pipelineId = PipelineHelper::Instance()->LoadSettingsPipeline( tPipelineFilePath );
			SimplygonMaxInstance->UseSettingsPipelineForProcessing( pipelineId );
		}
		catch( std::exception ex )
		{
			std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_RunPipelineOnFile: Failed to load pipeline (");
			tErrorMessage += tPipelineFilePath;
			tErrorMessage += _T(")\n");
			tErrorMessage += ConstCharPtrToLPCTSTR( ex.what() );
			throw UserThrownError( tErrorMessage.c_str(), TRUE );
		}
	}
	else
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_RunPipelineOnFile: Could not find a valid pipeline input.");
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	bool bProcessed = false;
	try
	{
		bProcessed = SimplygonMaxInstance->ProcessSceneFromFile( arg_list[ 1 ]->to_string(), arg_list[ 2 ]->to_string() );
	}
	catch( std::exception ex )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_RunPipelineOnFile: processing failed - ");
		tErrorMessage += ConstCharPtrToLPCTSTR( ex.what() );
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	return ( bProcessed ) ? &true_value : &false_value;
}

Value* sgsdk_AddMaterialCaster_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_AddMaterialCaster", 2, count );
	one_value_local( ret );

	const INT64 pipelineId = arg_list[ 0 ]->to_int64();
	std::basic_string<TCHAR> tMaterialCasterType = std::basic_string<TCHAR>( arg_list[ 1 ]->to_string() );

	int casterIndex = 0;
	try
	{
		casterIndex = PipelineHelper::Instance()->AddMaterialCaster( pipelineId, tMaterialCasterType );
	}
	catch( std::exception ex )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_AddMaterialCaster: Failed to add material caster for pipeline (");
		tErrorMessage += pipelineId;
		tErrorMessage += _T(") - ");
		tErrorMessage += ConstCharPtrToLPCTSTR( ex.what() );
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	return Integer::intern( casterIndex );
}

Value* sgsdk_AddCascadedPipeline_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_AddCascadedPipeline", 2, count );

	const INT64 pipelineId = arg_list[ 0 ]->to_int64();
	const INT64 cascadedPipelineId = arg_list[ 1 ]->to_int64();

	bool bAdded = false;
	try
	{
		bAdded = PipelineHelper::Instance()->AddCascadedPipeline( pipelineId, cascadedPipelineId );
	}
	catch( std::exception ex )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("sgsdk_AddCascadedPipeline: Failed to add cascaded pipeline for pipeline (");
		tErrorMessage += pipelineId;
		tErrorMessage += _T(") - ");
		tErrorMessage += ConstCharPtrToLPCTSTR( ex.what() );
		throw UserThrownError( tErrorMessage.c_str(), TRUE );
	}

	return bAdded ? &true_value : &false_value;
}

Value* sgsdk_SelectProcessedGeometries_cf( Value** arg_list, int count )
{
	check_arg_count( "sgsdk_SelectProcessedGeometries", 0, count );

	Interface* mMaxInterface = GetCOREInterface();
	mMaxInterface->ClearNodeSelection( FALSE );

	MaterialInfoHandler* materialInfoHandler = SimplygonMaxInstance->GetMaterialInfoHandler();
	if( materialInfoHandler )
	{
		INodeTab mNodeTab;
		const std::vector<std::basic_string<TCHAR>>& tMeshNameList = materialInfoHandler->GetMeshes();
		for( const std::basic_string<TCHAR>& tMeshName : tMeshNameList )
		{
			INode* mProcessedMeshNode = mMaxInterface->GetINodeByName( tMeshName.c_str() );
			if( mProcessedMeshNode && !Animatable::IsDeleted( mProcessedMeshNode ) )
			{
				mNodeTab.AppendNode( mProcessedMeshNode );
			}
		}

		if( mNodeTab.Count() > 0 )
		{
			mMaxInterface->SelectNodeTab( mNodeTab, TRUE, FALSE );
		}
	}

	return mMaxInterface->GetSelNodeCount() > 0 ? &true_value : &false_value;
}