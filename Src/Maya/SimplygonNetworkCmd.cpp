// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"

#include "SimplygonNetworkCmd.h"
#include "SimplygonQueryCmd.h"
#include "SimplygonCmd.h"

#include "maya/MArgDatabase.h"
#include "maya/MGlobal.h"
#include "maya/MSelectionList.h"
#include "maya/MDagPath.h"
#include "maya/MFnDependencyNode.h"
#include "maya/MPlug.h"
#include "maya/M3dView.h"
#include "maya/MFnCamera.h"

#include "SimplygonInit.h"

const char* cHelperCmdOverrideUVAll = "-uva";      // all texture nodes with name                               (-uva "TextureName")
const char* cHelperCmdOverrideUVMaterial = "-uvm"; // all texture nodes with name in specified material         (-uvm "MaterialName" "TextureName")
const char* cHelperCmdOverrideUVMaterialChannnel =
    "-uvc"; // all texture nodes with name in specified material channel (-uvc "MaterialName" "ChannelName" "TextureName"

const char* cHelperCmdOverrideSRGBAll = "-sa";      // all texture nodes with name                               (-sa "TextureName")
const char* cHelperCmdOverrideSRGBMaterial = "-sm"; // all texture nodes with name in specified material         (-sm "MaterialName" "TextureName")
const char* cHelperCmdOverrideSRGBMaterialChannnel =
    "-sc"; // all texture nodes with name in specified material channel (-sc "MaterialName" "ChannelName" "TextureName"

const char* cHelperCmdOverrideUVTilingMaterialChannnel =
    "-tmc"; // all texture nodes with name in specified material channel (-tmc "MaterialName" "ChannelName" "TextureName"
const char* cHelperCmdOverrideUVOffsetMaterialChannnel =
    "-omc"; // all texture nodes with name in specified material channel (-omc "MaterialName" "ChannelName" "TextureName"

const char* cHelperCmdCreateNode = "-cn";
const char* cHelperCmdSetInput = "-si";
const char* cHelperCmdSetDefault = "-sd";
const char* cHelperCmdSetDefault1f = "-sd1";
const char* cSetChannelExitNode = "-sce";
const char* cExportToXML = "-exf";
const char* cSetSwizzle = "-swz";
const char* cSetVertexColorIndex = "-svc";
const char* cSetVertexColorChannel = "-svn";
const char* cSetGeometryFieldName = "-sgn";
const char* cSetGeometryFieldType = "-sgt";
const char* cSetGeometryFieldIndex = "-sgi";

extern SimplygonInitClass* SimplygonInitInstance;

SimplygonShadingNetworkHelperCmd::SimplygonShadingNetworkHelperCmd()
{
}

SimplygonShadingNetworkHelperCmd::~SimplygonShadingNetworkHelperCmd()
{
}

void* SimplygonShadingNetworkHelperCmd::creator()
{
	return new SimplygonShadingNetworkHelperCmd();
}

inline ShadingNodeType GetTypeFromName( MString mNodeType )
{
	for( int i = 0; i < sizeof( ShadingNetworkNodeTable ) / sizeof( ShadingNetworkNodeTable[ 0 ] ); i++ )
	{
		if( CompareStrings( mNodeType.asChar(), ShadingNetworkNodeTable[ i ] ) )
			return (ShadingNodeType)i;
	}
	return Undefined;
}

MSyntax SimplygonShadingNetworkHelperCmd::createSyntax()
{
	MStatus mStatus;
	MSyntax mSyntax;

	// add a node to the material shading network
	mStatus = mSyntax.addFlag( cHelperCmdCreateNode, "-CreateNode", MSyntax::kString, MSyntax::kString, MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cHelperCmdCreateNode );

	// link two nodes together, Name of Material, Name of first node, input slot index , name of node to connect to input slot
	mStatus = mSyntax.addFlag( cHelperCmdSetInput, "-SetInput", MSyntax::kString, MSyntax::kString, MSyntax::kUnsigned, MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cHelperCmdSetInput );

	mStatus = mSyntax.addFlag(
	    cHelperCmdSetDefault, "-SetDefault", MSyntax::kString, MSyntax::kString, MSyntax::kUnsigned, MSyntax::kDouble, MSyntax::kDouble, MSyntax::kDouble );
	mStatus = mSyntax.makeFlagMultiUse( cHelperCmdSetDefault );

	mStatus = mSyntax.addFlag(
	    cHelperCmdSetDefault1f, "-SetDefault1f", MSyntax::kString, MSyntax::kString, MSyntax::kUnsigned, MSyntax::kUnsigned, MSyntax::kDouble );
	mStatus = mSyntax.makeFlagMultiUse( cHelperCmdSetDefault1f );

	mStatus = mSyntax.addFlag( cSetChannelExitNode, "-SetExitNode", MSyntax::kString, MSyntax::kString, MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cSetChannelExitNode );

	mStatus = mSyntax.addFlag( cExportToXML, "-ExportXML", MSyntax::kString, MSyntax::kString, MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cExportToXML );

	mStatus = mSyntax.addFlag( cHelperCmdOverrideUVAll, "-SetUVAll", MSyntax::kString, MSyntax::kString ); // texture_node_name, uv_set
	mStatus = mSyntax.makeFlagMultiUse( cHelperCmdOverrideUVAll );

	mStatus = mSyntax.addFlag(
	    cHelperCmdOverrideUVMaterial, "-SetUVMaterial", MSyntax::kString, MSyntax::kString, MSyntax::kString ); // material_name, texture_node_name, uv_set
	mStatus = mSyntax.makeFlagMultiUse( cHelperCmdOverrideUVMaterial );

	mStatus = mSyntax.addFlag( cHelperCmdOverrideUVMaterialChannnel,
	                           "-SetUVMaterialChannel",
	                           MSyntax::kString,
	                           MSyntax::kString,
	                           MSyntax::kString,
	                           MSyntax::kString ); // material_name, material_channel, texture_node_name, uv_set
	mStatus = mSyntax.makeFlagMultiUse( cHelperCmdOverrideUVMaterialChannnel );

	mStatus = mSyntax.addFlag( cHelperCmdOverrideSRGBAll, "-SetSRGBAll", MSyntax::kString, MSyntax::kBoolean ); // texture_node_name, srgb
	mStatus = mSyntax.makeFlagMultiUse( cHelperCmdOverrideSRGBAll );

	mStatus = mSyntax.addFlag(
	    cHelperCmdOverrideSRGBMaterial, "-SetSRGBMaterial", MSyntax::kString, MSyntax::kString, MSyntax::kBoolean ); // material_name, texture_node_name, srgb
	mStatus = mSyntax.makeFlagMultiUse( cHelperCmdOverrideSRGBMaterial );

	mStatus = mSyntax.addFlag( cHelperCmdOverrideSRGBMaterialChannnel,
	                           "-SetSRGBMaterialChannel",
	                           MSyntax::kString,
	                           MSyntax::kString,
	                           MSyntax::kString,
	                           MSyntax::kBoolean ); // material_name, material_channel, texture_node_name, srgb
	mStatus = mSyntax.makeFlagMultiUse( cHelperCmdOverrideSRGBMaterialChannnel );

	mStatus = mSyntax.addFlag( cHelperCmdOverrideUVTilingMaterialChannnel,
	                           "-SetUVTilingMaterialChannel",
	                           MSyntax::kString,
	                           MSyntax::kString,
	                           MSyntax::kString,
	                           MSyntax::kDouble,
	                           MSyntax::kDouble ); // material_name, material_channel, texture_node_name, utiling, vtiling
	mStatus = mSyntax.makeFlagMultiUse( cHelperCmdOverrideUVTilingMaterialChannnel );

	mStatus = mSyntax.addFlag( cHelperCmdOverrideUVOffsetMaterialChannnel,
	                           "-SetUVOffsetMaterialChannel",
	                           MSyntax::kString,
	                           MSyntax::kString,
	                           MSyntax::kString,
	                           MSyntax::kDouble,
	                           MSyntax::kDouble ); // material_name, material_channel, texture_node_name, uoffset, voffset
	mStatus = mSyntax.makeFlagMultiUse( cHelperCmdOverrideUVOffsetMaterialChannnel );

	mStatus = mSyntax.addFlag( cSetSwizzle, "-Swizzle", MSyntax::kString, MSyntax::kString, MSyntax::kUnsigned, MSyntax::kUnsigned );
	mStatus = mSyntax.makeFlagMultiUse( cSetSwizzle );

	mStatus = mSyntax.addFlag( cSetVertexColorIndex, "-SetVertCol", MSyntax::kString, MSyntax::kString, MSyntax::kUnsigned );
	mStatus = mSyntax.makeFlagMultiUse( cSetVertexColorIndex );

	mStatus = mSyntax.addFlag( cSetVertexColorChannel, "-SetVertexColorName", MSyntax::kString, MSyntax::kString, MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cSetVertexColorChannel );

	mStatus = mSyntax.addFlag( cSetGeometryFieldName, "-SetGeometryFieldName", MSyntax::kString, MSyntax::kString, MSyntax::kString );
	mStatus = mSyntax.makeFlagMultiUse( cSetGeometryFieldName );

	mStatus = mSyntax.addFlag( cSetGeometryFieldType, "-SetGeometryFieldType", MSyntax::kString, MSyntax::kString, MSyntax::kLong );
	mStatus = mSyntax.makeFlagMultiUse( cSetGeometryFieldType );

	mStatus = mSyntax.addFlag( cSetGeometryFieldIndex, "-SetGeometryFieldIndex", MSyntax::kString, MSyntax::kString, MSyntax::kLong );
	mStatus = mSyntax.makeFlagMultiUse( cSetGeometryFieldIndex );

	return mSyntax;
}

MStatus
SimplygonShadingNetworkHelperCmd::SetNodeDefaultParam( MString mMaterialName, MString mNodeName, int inputIndex, double r, double g, double b, double a )
{
	ShadingMaterialProxy* materialProxy = FindMaterial( mMaterialName.asChar() );

	if( materialProxy == nullptr )
		return MStatus::kFailure;

	const ShadingNodeProxy* proxyNode = materialProxy->FindNode( mNodeName.asChar() );

	if( proxyNode == nullptr )
		return MStatus::kFailure;

	if( inputIndex > (int)proxyNode->sgShadingNode->GetParameterCount() )
		return MStatus::kFailure;

	proxyNode->sgShadingNode->SetDefaultParameter( inputIndex, (float)r, (float)g, (float)b, (float)a );

	return MStatus::kSuccess;
}

MStatus SimplygonShadingNetworkHelperCmd::SetNodeDefaultParam( MString mMaterialName, MString mNodeName, int inputIndex, int type, double v )
{
	ShadingMaterialProxy* materialProxy = FindMaterial( mMaterialName.asChar() );

	if( materialProxy == nullptr )
		return MStatus::kFailure;

	const ShadingNodeProxy* proxyNode = materialProxy->FindNode( mNodeName.asChar() );

	if( proxyNode == nullptr )
		return MStatus::kFailure;

	if( inputIndex > (int)proxyNode->sgShadingNode->GetParameterCount() )
		return MStatus::kFailure;

	const float r = proxyNode->sgShadingNode->GetDefaultParameterRed( inputIndex );
	const float g = proxyNode->sgShadingNode->GetDefaultParameterGreen( inputIndex );
	const float b = proxyNode->sgShadingNode->GetDefaultParameterBlue( inputIndex );
	const float a = proxyNode->sgShadingNode->GetDefaultParameterAlpha( inputIndex );

	switch( type )
	{
		case 0: proxyNode->sgShadingNode->SetDefaultParameter( inputIndex, (float)v, g, b, a ); break;
		case 1: proxyNode->sgShadingNode->SetDefaultParameter( inputIndex, r, (float)v, b, a ); break;
		case 2: proxyNode->sgShadingNode->SetDefaultParameter( inputIndex, r, g, (float)v, a ); break;
		case 3: proxyNode->sgShadingNode->SetDefaultParameter( inputIndex, r, g, b, (float)v ); break;
		default: return MStatus::kFailure;
	}

	return MStatus::kSuccess;
}

MStatus SimplygonShadingNetworkHelperCmd::CreateNode( MString mMaterialName, MString mNodeType, MString mNodeName )
{
	// get material from lookup.
	ShadingMaterialProxy* materialProxy = FindMaterial( mMaterialName.asChar() );

	if( materialProxy != nullptr )
	{
		// if node already exist for this material, skip it if already added
		ShadingNodeProxy* nodeProxy = materialProxy->FindNode( mNodeName.asChar() );

		if( nodeProxy == nullptr )
		{
			nodeProxy = new ShadingNodeProxy();
			const ShadingNodeType nodeType = GetTypeFromName( mNodeType );
			CreateSimplygonShadingNodeForProxy( nodeProxy, nodeType, mNodeName );

			// add to the material proxy
			materialProxy->NodeProxyLookup[ mNodeName.asChar() ] = nodeProxy;
		}
	}

	else // if not found create material and node
	{
		materialProxy = new ShadingMaterialProxy();
		materialProxy->sgMaterial = sg->CreateMaterial();

		ShadingNodeProxy* nodeProxy = new ShadingNodeProxy();
		const ShadingNodeType nodetype = GetTypeFromName( mNodeType );

		CreateSimplygonShadingNodeForProxy( nodeProxy, nodetype, mNodeName );

		// add to the material proxy
		materialProxy->NodeProxyLookup[ mNodeName.asChar() ] = nodeProxy;
		MaterialProxyLookup[ mMaterialName.asChar() ] = materialProxy;
	}

	return MStatus::kSuccess;
}

ShadingMaterialProxy* SimplygonShadingNetworkHelperCmd::FindMaterial( std::basic_string<TCHAR> tMaterialName )
{
	if( MaterialProxyLookup.find( tMaterialName.c_str() ) != MaterialProxyLookup.end() )
	{
		return MaterialProxyLookup[ tMaterialName.c_str() ];
	}
	return nullptr;
}

MStatus SimplygonShadingNetworkHelperCmd::doIt( const MArgList& mArgs )
{
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
		MStatus mStatus = MStatus::kSuccess;
		MArgDatabase mArgData( syntax(), mArgs );

		// create node
		if( mArgData.isFlagSet( cHelperCmdCreateNode ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cHelperCmdCreateNode );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cHelperCmdCreateNode, i, mArgList );
				if( !mStatus )
					return mStatus;

				MString mMaterialName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mNodeType = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mNodeName = mArgList.asString( 2, &mStatus );
				if( !mStatus )
					return mStatus;

				mStatus = CreateNode( mMaterialName, mNodeType, mNodeName );
				if( !mStatus )
					return mStatus;
			}
		}

		// set node input
		if( mArgData.isFlagSet( cHelperCmdSetInput ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cHelperCmdSetInput );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cHelperCmdSetInput, i, mArgList );
				if( !mStatus )
					return mStatus;

				// MSyntax::kString, MSyntax::kUnsigned, MSyntax::kString
				MString mMaterialName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mNodeName = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				const int inputIndex = mArgList.asInt( 2, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mNodeToConnectTo = mArgList.asString( 3, &mStatus );
				if( !mStatus )
					return mStatus;

				mStatus = SetNodeInput( mMaterialName, mNodeName, inputIndex, mNodeToConnectTo );
				if( !mStatus )
					return mStatus;
			}
		}

		if( mArgData.isFlagSet( cSetSwizzle ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cSetSwizzle );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cSetSwizzle, i, mArgList );
				if( !mStatus )
					return mStatus;

				// MSyntax::kString, MSyntax::kUnsigned, MSyntax::kString
				MString mMaterialName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mNodeName = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				const int inChannel = mArgList.asInt( 2, &mStatus );
				if( !mStatus )
					return mStatus;

				const int outChannel = mArgList.asInt( 3, &mStatus );
				if( !mStatus )
					return mStatus;

				mStatus = SetSwizzleComponent( mMaterialName, mNodeName, inChannel, outChannel );
				if( !mStatus )
					return mStatus;
			}
		}

		if( mArgData.isFlagSet( cSetVertexColorIndex ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cSetVertexColorIndex );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cSetVertexColorIndex, i, mArgList );
				if( !mStatus )
					return mStatus;

				// MSyntax::kString, MSyntax::kUnsigned, MSyntax::kString
				MString mMaterialName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mNodeName = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				const int vertexColorIndex = mArgList.asInt( 2, &mStatus );
				if( !mStatus )
					return mStatus;

				mStatus = SetVertexColorChannel( mMaterialName, mNodeName, vertexColorIndex );
				if( !mStatus )
					return mStatus;
			}
		}

		if( mArgData.isFlagSet( cSetVertexColorChannel ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cSetVertexColorChannel );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cSetVertexColorChannel, i, mArgList );
				if( !mStatus )
					return mStatus;

				// MSyntax::kString, MSyntax::kUnsigned, MSyntax::kString
				MString mMaterialName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mNodeName = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mVertexColorName = mArgList.asString( 2, &mStatus );
				if( !mStatus )
					return mStatus;

				mStatus = SetVertexColorChannel( mMaterialName, mNodeName, mVertexColorName );
				if( !mStatus )
					return mStatus;
			}
		}

		if( mArgData.isFlagSet( cSetGeometryFieldName ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cSetGeometryFieldName );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cSetGeometryFieldName, i, mArgList );
				if( !mStatus )
					return mStatus;

				MString mMaterialName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mNodeName = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mGeometryFieldName = mArgList.asString( 2, &mStatus );
				if( !mStatus )
					return mStatus;

				mStatus = SetGeometryFieldName( mMaterialName, mNodeName, mGeometryFieldName );
				if( !mStatus )
					return mStatus;
			}
		}

		if( mArgData.isFlagSet( cSetGeometryFieldType ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cSetGeometryFieldType );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cSetGeometryFieldType, i, mArgList );
				if( !mStatus )
					return mStatus;

				MString mMaterialName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mNodeName = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				const int geometryFieldType = mArgList.asInt( 2, &mStatus );
				if( !mStatus )
					return mStatus;

				mStatus = SetGeometryFieldType( mMaterialName, mNodeName, geometryFieldType );
				if( !mStatus )
					return mStatus;
			}
		}

		if( mArgData.isFlagSet( cSetGeometryFieldIndex ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cSetGeometryFieldIndex );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cSetGeometryFieldIndex, i, mArgList );
				if( !mStatus )
					return mStatus;

				MString mMaterialName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mNodeName = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				const int geometryFieldIndex = mArgList.asInt( 2, &mStatus );
				if( !mStatus )
					return mStatus;

				mStatus = SetGeometryFieldIndex( mMaterialName, mNodeName, geometryFieldIndex );
				if( !mStatus )
					return mStatus;
			}
		}

		// set default value on input
		if( mArgData.isFlagSet( cHelperCmdSetDefault ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cHelperCmdSetDefault );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cHelperCmdSetDefault, i, mArgList );
				if( !mStatus )
					return mStatus;

				// MSyntax::kString, MSyntax::kUnsigned, MSyntax::kString

				MString mMaterialName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mNodeName = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				const int inputIndex = mArgList.asInt( 2, &mStatus );
				if( !mStatus )
					return mStatus;

				const double r = mArgList.asDouble( 3, &mStatus );
				if( !mStatus )
					return mStatus;

				const double g = mArgList.asDouble( 4, &mStatus );
				if( !mStatus )
					return mStatus;

				const double b = mArgList.asDouble( 5, &mStatus );
				if( !mStatus )
					return mStatus;

				mStatus = SetNodeDefaultParam( mMaterialName, mNodeName, inputIndex, r, g, b, 1.0 );
				if( !mStatus )
					return mStatus;
			}
		}

		if( mArgData.isFlagSet( cHelperCmdSetDefault1f ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cHelperCmdSetDefault1f );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cHelperCmdSetDefault1f, i, mArgList );
				if( !mStatus )
					return mStatus;

				MString mMaterialName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mNodeName = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				const int inputIndex = mArgList.asInt( 2, &mStatus );
				if( !mStatus )
					return mStatus;

				const int type = mArgList.asInt( 3, &mStatus );
				if( !mStatus )
					return mStatus;

				const double v = mArgList.asDouble( 4, &mStatus );
				if( !mStatus )
					return mStatus;

				mStatus = SetNodeDefaultParam( mMaterialName, mNodeName, inputIndex, type, v );
				if( !mStatus )
					return mStatus;
			}
		}

		// set channel exit node
		if( mArgData.isFlagSet( cSetChannelExitNode ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cSetChannelExitNode );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cSetChannelExitNode, i, mArgList );
				if( !mStatus )
					return mStatus;

				// MSyntax::kString, MSyntax::kUnsigned, MSyntax::kString
				MString mMaterialName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mMaterialChannelName = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mNodeName = mArgList.asString( 2, &mStatus );
				if( !mStatus )
					return mStatus;

				// TODO : Call method and do what's required
				mStatus = SetChannelExitNode( mMaterialName, mMaterialChannelName, mNodeName );
				if( !mStatus )
					return mStatus;
			}
		}

		// create node
		if( mArgData.isFlagSet( cHelperCmdOverrideUVAll ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cHelperCmdOverrideUVAll );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cHelperCmdOverrideUVAll, i, mArgList );
				if( !mStatus )
					return mStatus;

				MString mTextureNodeName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mTargetUVSet = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				SetUVAll( mTextureNodeName, mTargetUVSet );
			}
		}

		// create node
		if( mArgData.isFlagSet( cHelperCmdOverrideUVMaterial ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cHelperCmdOverrideUVMaterial );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cHelperCmdOverrideUVMaterial, i, mArgList );
				if( !mStatus )
					return mStatus;

				MString mMaterialName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mTextureNodeName = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mTargetUVSet = mArgList.asString( 2, &mStatus );
				if( !mStatus )
					return mStatus;

				SetUVMaterial( mMaterialName, mTextureNodeName, mTargetUVSet );
			}
		}

		// create node
		if( mArgData.isFlagSet( cHelperCmdOverrideUVMaterialChannnel ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cHelperCmdOverrideUVMaterialChannnel );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cHelperCmdOverrideUVMaterialChannnel, i, mArgList );
				if( !mStatus )
					return mStatus;

				MString mMaterialName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mMaterialChannelName = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mTextureNodeName = mArgList.asString( 2, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mTargetUVSetName = mArgList.asString( 3, &mStatus );
				if( !mStatus )
					return mStatus;

				SetUVMaterialChannel( mMaterialName, mMaterialChannelName, mTextureNodeName, mTargetUVSetName );
			}
		}

		// override srgb
		if( mArgData.isFlagSet( cHelperCmdOverrideSRGBAll ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cHelperCmdOverrideSRGBAll );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cHelperCmdOverrideSRGBAll, i, mArgList );
				if( !mStatus )
					return mStatus;

				MString mTextureNodeName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				const bool isSRGB = mArgList.asBool( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				SetSRGBAll( mTextureNodeName, isSRGB );
			}
		}

		// create node
		if( mArgData.isFlagSet( cHelperCmdOverrideSRGBMaterial ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cHelperCmdOverrideSRGBMaterial );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cHelperCmdOverrideSRGBMaterial, i, mArgList );
				if( !mStatus )
					return mStatus;

				MString mMaterialName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mTextureNodeName = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				const bool isSRGB = mArgList.asBool( 2, &mStatus );
				if( !mStatus )
					return mStatus;

				SetSRGBMaterial( mMaterialName, mTextureNodeName, isSRGB );
			}
		}

		// create node
		if( mArgData.isFlagSet( cHelperCmdOverrideSRGBMaterialChannnel ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cHelperCmdOverrideSRGBMaterialChannnel );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cHelperCmdOverrideSRGBMaterialChannnel, i, mArgList );
				if( !mStatus )
					return mStatus;

				MString mMaterialName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mMaterialChannelName = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mTextureNodeName = mArgList.asString( 2, &mStatus );
				if( !mStatus )
					return mStatus;

				const bool isSRGB = mArgList.asBool( 3, &mStatus );
				if( !mStatus )
					return mStatus;

				SetSRGBMaterialChannel( mMaterialName, mMaterialChannelName, mTextureNodeName, isSRGB );
			}
		}

		if( mArgData.isFlagSet( cHelperCmdOverrideUVTilingMaterialChannnel ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cHelperCmdOverrideUVTilingMaterialChannnel );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cHelperCmdOverrideUVTilingMaterialChannnel, i, mArgList );
				if( !mStatus )
					return mStatus;

				MString mMaterialName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mMaterialChannelName = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mTextureNodeName = mArgList.asString( 2, &mStatus );
				if( !mStatus )
					return mStatus;

				const float uTiling = (float)mArgList.asDouble( 3, &mStatus );
				if( !mStatus )
					return mStatus;

				const float vTiling = (float)mArgList.asDouble( 4, &mStatus );
				if( !mStatus )
					return mStatus;

				this->SetUVTiling( mMaterialName, mMaterialChannelName, mTextureNodeName, uTiling, vTiling );
			}
		}

		if( mArgData.isFlagSet( cHelperCmdOverrideUVOffsetMaterialChannnel ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cHelperCmdOverrideUVOffsetMaterialChannnel );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cHelperCmdOverrideUVOffsetMaterialChannnel, i, mArgList );
				if( !mStatus )
					return mStatus;

				MString mMaterialName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mMaterialChannelName = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mTextureNodeName = mArgList.asString( 2, &mStatus );
				if( !mStatus )
					return mStatus;

				const float uOffset = (float)mArgList.asDouble( 3, &mStatus );
				if( !mStatus )
					return mStatus;

				const float vOffset = (float)mArgList.asDouble( 4, &mStatus );
				if( !mStatus )
					return mStatus;

				this->SetUVOffset( mMaterialName, mMaterialChannelName, mTextureNodeName, uOffset, vOffset );
			}
		}

		// export material channel to xml
		if( mArgData.isFlagSet( cExportToXML ) )
		{
			const uint flagCount = mArgData.numberOfFlagUses( cExportToXML );
			for( uint i = 0; i < flagCount; ++i )
			{
				MArgList mArgList;
				mStatus = mArgData.getFlagArgumentList( cExportToXML, i, mArgList );
				if( !mStatus )
					return mStatus;

				// MSyntax::kString, MSyntax::kUnsigned, MSyntax::kString
				MString mMaterialName = mArgList.asString( 0, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mMaterialChannelName = mArgList.asString( 1, &mStatus );
				if( !mStatus )
					return mStatus;

				MString mExportFilePath = mArgList.asString( 2, &mStatus );
				if( !mStatus )
					return mStatus;

				// TODO : Call method and do the required
				mStatus = ExportXMLToFile( mMaterialName, mMaterialChannelName, mExportFilePath );
				if( !mStatus )
					return mStatus;
			}
		}
	}

	return MStatus::kSuccess;
}

// TODO: generate shading node list!
void SimplygonShadingNetworkHelperCmd::CreateSimplygonShadingNodeForProxy( ShadingNodeProxy* nodeProxy, ShadingNodeType nodetype, MString mNodeName )
{
	nodeProxy->sgShadingNodeType = nodetype;

	switch( nodetype )
	{
		case AddNode: nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingAddNode() ); break;
		case SubtractNode: nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingSubtractNode() ); break;
		case MultiplyNode: nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingMultiplyNode() ); break;
		case DivideNode: nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingDivideNode() ); break;
		case InterpolateNode: nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingInterpolateNode() ); break;
		case SwizzlingNode: nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingSwizzlingNode() ); break;
		case VertexColorNode: nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingVertexColorNode() ); break;
		case ClampNode: nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingClampNode() ); break;
		case TextureNode: nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingTextureNode() ); break;
		case ColorNode: nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingColorNode() ); break;
		case LayeredBlendNode: // 8.0+
			nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingLayeredBlendNode() );
			break;
		case PowNode: // 8.0+
			nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingPowNode() );
			break;
		case SqrtNode: // 8.2+
			nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingSqrtNode() );
			break;
		case Normalize3Node: // 8.2+
			nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingNormalize3Node() );
			break;
		case Dot3Node: // 8.2+
			nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingDot3Node() );
			break;
		case Cross3Node: // 8.2+
			nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingCross3Node() );
			break;
		case CosNode: // 8.2+
			nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingCosNode() );
			break;
		case SinNode: // 8.2+
			nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingSinNode() );
			break;
		case MaxNode: // 8.2+
			nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingMaxNode() );
			break;
		case MinNode: // 8.2+
			nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingMinNode() );
			break;
		case EqualNode: // 8.2+
			nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingEqualNode() );
			break;
		case NotEqualNode: // 8.2+
			nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingNotEqualNode() );
			break;
		case GreaterThanNode: // 8.2+
			nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingGreaterThanNode() );
			break;
		case LessThanNode: // 8.2+
			nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingLessThanNode() );
			break;
		case GeometryFieldNode: // 9.1+
			nodeProxy->sgShadingNode = spShadingNode::SafeCast( sg->CreateShadingGeometryFieldNode() );
			break;
	}

	if( !nodeProxy->sgShadingNode.IsNull() )
		nodeProxy->sgShadingNode->SetName( mNodeName.asChar() );
}

MStatus SimplygonShadingNetworkHelperCmd::SetNodeInput( MString mMaterialName, MString mNodeName, int inputIndex, MString mNodeNameToConnect )
{
	ShadingMaterialProxy* materialProxy = FindMaterial( mMaterialName.asChar() );

	if( materialProxy == nullptr )
		return MStatus::kFailure;

	const ShadingNodeProxy* nodeProxy = materialProxy->FindNode( mNodeName.asChar() );

	if( nodeProxy == nullptr )
		return MStatus::kFailure;

	const ShadingNodeProxy* mNodeProxyToConnect = materialProxy->FindNode( mNodeNameToConnect.asChar() );

	if( mNodeProxyToConnect == nullptr )
		return MStatus::kFailure;

	spShadingFilterNode sgFilterNode = spShadingFilterNode::SafeCast( nodeProxy->sgShadingNode );

	if( sgFilterNode.IsNull() )
		return MStatus::kFailure;

	if( inputIndex > (int)sgFilterNode->GetParameterCount() )
		return MStatus::kFailure;

	if( !sgFilterNode->GetParameterIsInputable( inputIndex ) )
		return MStatus::kFailure;

	sgFilterNode->SetInput( inputIndex, mNodeProxyToConnect->sgShadingNode );

	return MStatus::kSuccess;
}

MStatus SimplygonShadingNetworkHelperCmd::SetSwizzleComponent( MString mMaterialName, MString mNodeName, int inChannel, int outChannel )
{
	ShadingMaterialProxy* materialProxy = FindMaterial( mMaterialName.asChar() );

	if( materialProxy == nullptr )
		return MStatus::kFailure;

	const ShadingNodeProxy* nodeProxy = materialProxy->FindNode( mNodeName.asChar() );

	if( nodeProxy == nullptr )
		return MStatus::kFailure;

	spShadingSwizzlingNode sgSwizzlingNode = spShadingSwizzlingNode::SafeCast( nodeProxy->sgShadingNode );

	if( sgSwizzlingNode.IsNull() )
		return MStatus::kFailure;

	// test if channel indexes are in valid range
	if( ( inChannel < -1 && inChannel > 3 ) || ( outChannel < -1 && outChannel > 3 ) )
		return MStatus::kFailure;

	switch( inChannel )
	{
		case 0: sgSwizzlingNode->SetRedComponent( outChannel ); break;
		case 1: sgSwizzlingNode->SetGreenComponent( outChannel ); break;
		case 2: sgSwizzlingNode->SetBlueComponent( outChannel ); break;
		case 3: sgSwizzlingNode->SetAlphaComponent( outChannel ); break;
		default: break;
	}
	return MStatus::kSuccess;
}

MStatus SimplygonShadingNetworkHelperCmd::SetVertexColorChannel( MString mMaterialName, MString mNodeName, int vertexColorIndex )
{
	ShadingMaterialProxy* materialProxy = FindMaterial( mMaterialName.asChar() );

	if( materialProxy == nullptr )
		return MStatus::kFailure;

	const ShadingNodeProxy* proxyNode = materialProxy->FindNode( mNodeName.asChar() );

	if( proxyNode == nullptr )
		return MStatus::kFailure;

	spShadingVertexColorNode sgVertexColorNode = spShadingVertexColorNode::SafeCast( proxyNode->sgShadingNode );

	if( sgVertexColorNode.IsNull() )
		return MStatus::kFailure;

	sgVertexColorNode->SetVertexColorIndex( vertexColorIndex );

	return MStatus::kSuccess;
}

MStatus SimplygonShadingNetworkHelperCmd::SetVertexColorChannel( MString mMaterialName, MString mNodeName, MString mVertexColorChannelName )
{
	ShadingMaterialProxy* materialProxy = FindMaterial( mMaterialName.asChar() );

	if( materialProxy == nullptr )
		return MStatus::kFailure;

	const ShadingNodeProxy* proxyNode = materialProxy->FindNode( mNodeName.asChar() );

	if( proxyNode == nullptr )
		return MStatus::kFailure;

	spShadingVertexColorNode sgVertexColorNode = spShadingVertexColorNode::SafeCast( proxyNode->sgShadingNode );

	if( sgVertexColorNode.IsNull() )
		return MStatus::kFailure;

	sgVertexColorNode->SetVertexColorSet( mVertexColorChannelName.asChar() );

	return MStatus::kSuccess;
}

MStatus SimplygonShadingNetworkHelperCmd::SetGeometryFieldName( MString mMaterialName, MString mNodeName, MString mGeometryFieldName )
{
	ShadingMaterialProxy* materialProxy = FindMaterial( mMaterialName.asChar() );

	if( materialProxy == nullptr )
		return MStatus::kFailure;

	const ShadingNodeProxy* nodeProxy = materialProxy->FindNode( mNodeName.asChar() );

	if( nodeProxy == nullptr )
		return MStatus::kFailure;

	spShadingGeometryFieldNode sgGeometryFieldNode = spShadingGeometryFieldNode::SafeCast( nodeProxy->sgShadingNode );

	if( sgGeometryFieldNode.IsNull() )
		return MStatus::kFailure;

	// we need a valid string
	if( mGeometryFieldName == nullptr || mGeometryFieldName.length() == 0 )
		return MStatus::kFailure;

	sgGeometryFieldNode->SetFieldName( LPCTSTRToConstCharPtr( mGeometryFieldName.asChar() ) );

	return MStatus::kSuccess;
}

MStatus SimplygonShadingNetworkHelperCmd::SetGeometryFieldType( MString mMaterialName, MString mNodeName, int geometryFieldType )
{
	ShadingMaterialProxy* materialProxy = FindMaterial( mMaterialName.asChar() );

	if( materialProxy == nullptr )
		return MStatus::kFailure;

	const ShadingNodeProxy* nodeProxy = materialProxy->FindNode( mNodeName.asChar() );

	if( nodeProxy == nullptr )
		return MStatus::kFailure;

	spShadingGeometryFieldNode sgGeometryFieldNode = spShadingGeometryFieldNode::SafeCast( nodeProxy->sgShadingNode );

	if( sgGeometryFieldNode.IsNull() )
		return MStatus::kFailure;

	sgGeometryFieldNode->SetFieldType( geometryFieldType );

	return MStatus::kSuccess;
}

MStatus SimplygonShadingNetworkHelperCmd::SetGeometryFieldIndex( MString mMaterialName, MString mNodeName, int geometryFieldIndex )
{
	ShadingMaterialProxy* materialProxy = FindMaterial( mMaterialName.asChar() );

	if( materialProxy == nullptr )
		return MStatus::kFailure;

	const ShadingNodeProxy* nodeProxy = materialProxy->FindNode( mNodeName.asChar() );

	if( nodeProxy == nullptr )
		return MStatus::kFailure;

	spShadingGeometryFieldNode sgGeometryFieldNode = spShadingGeometryFieldNode::SafeCast( nodeProxy->sgShadingNode );

	if( sgGeometryFieldNode.IsNull() )
		return MStatus::kFailure;

	sgGeometryFieldNode->SetFieldIndex( geometryFieldIndex );

	return MStatus::kSuccess;
}

MStatus SimplygonShadingNetworkHelperCmd::SetChannelExitNode( MString mMaterialName, MString mMaterialChannelName, MString mNodeName )
{
	ShadingMaterialProxy* materialproxy = FindMaterial( mMaterialName.asChar() );

	if( materialproxy == nullptr )
		return MStatus::kFailure;

	const ShadingNodeProxy* nodeproxy = materialproxy->FindNode( mNodeName.asChar() );

	if( nodeproxy == nullptr )
		return MStatus::kFailure;

	if( !materialproxy->sgMaterial->HasMaterialChannel( mMaterialChannelName.asChar() ) )
		materialproxy->sgMaterial->AddMaterialChannel( mMaterialChannelName.asChar() );

	materialproxy->sgMaterial->SetShadingNetwork( mMaterialChannelName.asChar(), nodeproxy->sgShadingNode );

	return MStatus::kSuccess;
}

MStatus SimplygonShadingNetworkHelperCmd::ExportXMLToFile( MString mMaterialName, MString mMaterialChannelName, MString mExportFilePath )
{
	const ShadingMaterialProxy* materialProxy = FindMaterial( mMaterialName.asChar() );

	if( materialProxy == nullptr )
		return MStatus::kFailure;

	FILE* fp = nullptr;

	spMaterial sgMaterial = materialProxy->sgMaterial;

	if( sgMaterial.IsNull() )
		return MStatus::kFailure;

	spString xmlString = sgMaterial->SaveShadingNetworkToXML( mMaterialChannelName.asChar() );

	if( !xmlString.IsNullOrEmpty() )
	{
		if( ( fp = _tfopen( mExportFilePath.asChar(), "w+" ) ) == nullptr )
		{
			return MStatus::kFailure;
		}

		_fputts( xmlString.c_str(), fp );

		fclose( fp );

		fp = nullptr;
	}

	return MStatus::kSuccess;
}

void SimplygonShadingNetworkHelperCmd::SetUVAll( MString mTextureNodeName, MString mTargetUVSet )
{
	for( std::map<std::basic_string<TCHAR>, ShadingMaterialProxy*>::iterator materialProxyIterator = MaterialProxyLookup.begin();
	     materialProxyIterator != MaterialProxyLookup.end();
	     materialProxyIterator++ )
	{
		const ShadingNodeProxy* nodeProxy = materialProxyIterator->second->FindNode( mTextureNodeName.asChar() );
		if( nodeProxy == nullptr )
			continue;

		else if( nodeProxy->sgShadingNodeType == TextureNode )
		{
			spShadingTextureNode sgTextureNode = spShadingTextureNode::SafeCast( nodeProxy->sgShadingNode );
			if( sgTextureNode.NonNull() )
			{
				sgTextureNode->SetTexCoordName( mTargetUVSet.asChar() );
			}
		}
	}
}

void SimplygonShadingNetworkHelperCmd::SetUVMaterial( MString mMaterialName, MString mTextureNodeName, MString mTargetUVSet )
{
	for( std::map<std::basic_string<TCHAR>, ShadingMaterialProxy*>::iterator materialProxyIterator = MaterialProxyLookup.begin();
	     materialProxyIterator != MaterialProxyLookup.end();
	     materialProxyIterator++ )
	{
		if( MString( materialProxyIterator->first.c_str() ) == mMaterialName )
		{
			const ShadingNodeProxy* nodeProxy = materialProxyIterator->second->FindNode( mTextureNodeName.asChar() );
			if( nodeProxy == nullptr )
				continue;

			else if( nodeProxy->sgShadingNodeType == TextureNode )
			{
				spShadingTextureNode sgTextureNode = spShadingTextureNode::SafeCast( nodeProxy->sgShadingNode );
				if( sgTextureNode.NonNull() )
				{
					sgTextureNode->SetTexCoordName( mTargetUVSet.asChar() );
				}
			}
		}
	}
}

void SimplygonShadingNetworkHelperCmd::SetUVMaterialChannel( MString mMaterialName,
                                                             MString mMaterialChannelName,
                                                             MString mTextureNodeName,
                                                             MString mTargetUVSet )
{
	for( std::map<std::basic_string<TCHAR>, ShadingMaterialProxy*>::iterator materialProxyIterator = MaterialProxyLookup.begin();
	     materialProxyIterator != MaterialProxyLookup.end();
	     materialProxyIterator++ )
	{
		const ShadingMaterialProxy* materialProxy = materialProxyIterator->second;
		spShadingNode sgShadingNode = materialProxy->sgMaterial->GetShadingNetwork( mMaterialChannelName.asChar() );

		if( MString( materialProxyIterator->first.c_str() ) == mMaterialName )
		{
			const ShadingNodeProxy* nodeProxy = materialProxyIterator->second->FindNode( mTextureNodeName.asChar() );
			if( nodeProxy == nullptr )
				continue;

			else if( nodeProxy->sgShadingNodeType == TextureNode )
			{
				spShadingTextureNode sgTextureNode = spShadingTextureNode::SafeCast( nodeProxy->sgShadingNode );
				if( sgTextureNode.NonNull() )
				{
					sgTextureNode->SetTexCoordName( mTargetUVSet.asChar() );
				}
			}
		}
	}
}

void SimplygonShadingNetworkHelperCmd::SetSRGBAll( MString mTextureNodeName, bool sRGB )
{
	for( std::map<std::basic_string<TCHAR>, ShadingMaterialProxy*>::iterator materialProxyIterator = MaterialProxyLookup.begin();
	     materialProxyIterator != MaterialProxyLookup.end();
	     materialProxyIterator++ )
	{
		const ShadingNodeProxy* nodeProxy = materialProxyIterator->second->FindNode( mTextureNodeName.asChar() );
		if( nodeProxy == nullptr )
			continue;

		else if( nodeProxy->sgShadingNodeType == TextureNode )
		{
			spShadingTextureNode sgTextureNode = spShadingTextureNode::SafeCast( nodeProxy->sgShadingNode );
			if( sgTextureNode.NonNull() )
			{
				sgTextureNode->SetColorSpaceOverride( sRGB ? Simplygon::EImageColorSpace::sRGB : Simplygon::EImageColorSpace::Linear );
			}
		}
	}
}

void SimplygonShadingNetworkHelperCmd::SetSRGBMaterial( MString mMaterialName, MString mTextureNodeName, bool sRGB )
{
	for( std::map<std::basic_string<TCHAR>, ShadingMaterialProxy*>::iterator materialProxyIterator = MaterialProxyLookup.begin();
	     materialProxyIterator != MaterialProxyLookup.end();
	     materialProxyIterator++ )
	{
		if( MString( materialProxyIterator->first.c_str() ) == mMaterialName )
		{
			const ShadingNodeProxy* nodeProxy = materialProxyIterator->second->FindNode( mTextureNodeName.asChar() );
			if( nodeProxy == nullptr )
				continue;

			else if( nodeProxy->sgShadingNodeType == TextureNode )
			{
				spShadingTextureNode sgTextureNode = spShadingTextureNode::SafeCast( nodeProxy->sgShadingNode );
				if( sgTextureNode.NonNull() )
				{
					sgTextureNode->SetColorSpaceOverride( sRGB ? Simplygon::EImageColorSpace::sRGB : Simplygon::EImageColorSpace::Linear );
				}
			}
		}
	}
}

void SimplygonShadingNetworkHelperCmd::SetSRGBMaterialChannel( MString mMaterialName, MString mMaterialChannelName, MString mTextureNodeName, bool sRGB )
{
	for( std::map<std::basic_string<TCHAR>, ShadingMaterialProxy*>::iterator materialProxyIterator = MaterialProxyLookup.begin();
	     materialProxyIterator != MaterialProxyLookup.end();
	     materialProxyIterator++ )
	{
		const ShadingMaterialProxy* materialProxy = materialProxyIterator->second;
		spShadingNode sgShadingNode = materialProxy->sgMaterial->GetShadingNetwork( mMaterialChannelName.asChar() );

		if( MString( materialProxyIterator->first.c_str() ) == mMaterialName )
		{
			const ShadingNodeProxy* nodeProxy = materialProxyIterator->second->FindNode( mTextureNodeName.asChar() );
			if( nodeProxy == nullptr )
				continue;

			else if( nodeProxy->sgShadingNodeType == TextureNode )
			{
				spShadingTextureNode sgTextureNode = spShadingTextureNode::SafeCast( nodeProxy->sgShadingNode );
				if( sgTextureNode.NonNull() )
				{
					sgTextureNode->SetColorSpaceOverride( sRGB ? Simplygon::EImageColorSpace::sRGB : Simplygon::EImageColorSpace::Linear );
				}
			}
		}
	}
}

void SimplygonShadingNetworkHelperCmd::SetUVTiling(
    MString mMaterialName, MString mMaterialChannelName, MString mTextureNodeName, float uTiling, float vTiling )
{
	for( std::map<std::basic_string<TCHAR>, ShadingMaterialProxy*>::iterator materialProxyIterator = MaterialProxyLookup.begin();
	     materialProxyIterator != MaterialProxyLookup.end();
	     materialProxyIterator++ )
	{
		const ShadingMaterialProxy* materialProxy = materialProxyIterator->second;
		spShadingNode sgShadingNode = materialProxy->sgMaterial->GetShadingNetwork( mMaterialChannelName.asChar() );

		if( MString( materialProxyIterator->first.c_str() ) == mMaterialName )
		{
			const ShadingNodeProxy* nodeProxy = materialProxyIterator->second->FindNode( mTextureNodeName.asChar() );
			if( nodeProxy == nullptr )
				continue;

			else if( nodeProxy->sgShadingNodeType == TextureNode )
			{
				spShadingTextureNode sgTextureNode = spShadingTextureNode::SafeCast( nodeProxy->sgShadingNode );
				if( sgTextureNode.NonNull() )
				{
					sgTextureNode->SetTileU( uTiling );
					sgTextureNode->SetTileV( vTiling );
				}
			}
		}
	}
}

void SimplygonShadingNetworkHelperCmd::SetUVOffset(
    MString mMaterialName, MString mMaterialChannelName, MString mTextureNodeName, float uOffset, float vOffset )
{
	for( std::map<std::basic_string<TCHAR>, ShadingMaterialProxy*>::iterator materialProxyIterator = MaterialProxyLookup.begin();
	     materialProxyIterator != MaterialProxyLookup.end();
	     materialProxyIterator++ )
	{
		const ShadingMaterialProxy* materialProxy = materialProxyIterator->second;
		spShadingNode sgShadingNode = materialProxy->sgMaterial->GetShadingNetwork( mMaterialChannelName.asChar() );

		if( MString( materialProxyIterator->first.c_str() ) == mMaterialName )
		{
			const ShadingNodeProxy* nodeProxy = materialProxyIterator->second->FindNode( mTextureNodeName.asChar() );
			if( nodeProxy == nullptr )
				continue;

			else if( nodeProxy->sgShadingNodeType == TextureNode )
			{
				spShadingTextureNode sgTextureNode = spShadingTextureNode::SafeCast( nodeProxy->sgShadingNode );
				if( sgTextureNode.NonNull() )
				{
					sgTextureNode->SetOffsetU( uOffset );
					sgTextureNode->SetOffsetV( vOffset );
				}
			}
		}
	}
}

ShadingNodeProxy* ShadingMaterialProxy::FindNode( std::basic_string<TCHAR> tNodeName )
{
	if( NodeProxyLookup.find( tNodeName.c_str() ) != NodeProxyLookup.end() )
	{
		return NodeProxyLookup[ tNodeName.c_str() ];
	}
	return nullptr;
}