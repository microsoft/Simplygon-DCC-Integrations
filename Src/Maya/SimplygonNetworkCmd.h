// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <maya/MPxCommand.h>
#include <tchar.h>
#include "maya/MStatus.h"
#include "maya/MSyntax.h"
#include "maya/MArgList.h"

class ShadingNodeProxy
{
	public:
	spShadingNode sgShadingNode;
	ShadingNodeType sgShadingNodeType;

	ShadingNodeProxy() { this->sgShadingNodeType = Undefined; }
};

class ShadingMaterialProxy
{
	public:
	std::map<std::basic_string<TCHAR>, ShadingNodeProxy*> NodeProxyLookup;
	spMaterial sgMaterial;
	ShadingNodeProxy* FindNode( std::basic_string<TCHAR> tNodeName );
};

class SimplygonShadingNetworkHelperCmd : public MPxCommand
{
	public:
	std::map<std::basic_string<TCHAR>, ShadingMaterialProxy*> MaterialProxyLookup;

	SimplygonShadingNetworkHelperCmd();
	virtual ~SimplygonShadingNetworkHelperCmd();
	MStatus CreateNode( MString mMaterialName, MString mNodeType, MString mNodeName );

	void CreateSimplygonShadingNodeForProxy( ShadingNodeProxy* nodeProxy, ShadingNodeType nodetype, MString mNodeName );
	ShadingMaterialProxy* FindMaterial( std::basic_string<TCHAR> tMaterialName );

	MStatus redoIt() override { return MStatus::kSuccess; }
	MStatus undoIt() override { return MStatus::kSuccess; }
	bool isUndoable() const override { return false; }
	MStatus doIt( const MArgList& args ) override;

	static void* creator();
	static MSyntax createSyntax();

	MStatus SetNodeInput( MString mMaterialName, MString mNodeName, int inputIndex, MString mNodeNameToConnect );
	MStatus SetNodeDefaultParam( MString mMaterialName, MString mNodeName, int inputIndex, double r, double g, double b, double a );
	MStatus SetNodeDefaultParam( MString mMaterialName, MString mNodeName, int inputIndex, int type, double v );
	MStatus SetSwizzleComponent( MString mMaterialName, MString mNodeName, int inChannel, int outChannel );
	MStatus SetVertexColorChannel( MString mMaterialName, MString mNodeName, int vertexColorIndex );
	MStatus SetVertexColorChannel( MString mMaterialName, MString mNodeName, MString mMaterialChannelName );
	MStatus SetGeometryFieldName( MString mMaterialName, MString mNodeName, MString mGeometryFieldName );
	MStatus SetGeometryFieldType( MString mMaterialName, MString mNodeName, int geometryFieldType );
	MStatus SetGeometryFieldIndex( MString mMaterialName, MString mNodeName, int geometryFieldIndex );

	MStatus SetChannelExitNode( MString mMaterialName, MString mMaterialChannelName, MString mNodeName );
	MStatus ExportXMLToFile( MString mMaterialName, MString mMaterialChannelName, MString mFilePath );
	void SetUVAll( MString mTextureNodeName, MString mTargetUVSet );
	void SetUVMaterial( MString mMaterialName, MString mTextureNodeName, MString mTargetUVSet );
	void SetUVMaterialChannel( MString mMaterialName, MString mMaterialChannelName, MString mTextureNodeName, MString mTargetUVSet );
	void SetSRGBAll( MString mTextureNodeName, bool sRGB );
	void SetSRGBMaterial( MString mMaterialName, MString mTextureNodeName, bool sRGB );
	void SetSRGBMaterialChannel( MString mMaterialName, MString mMaterialChannelName, MString mTextureNodeName, bool sRGB );
	void SetUVTiling( MString mMaterialName, MString mMaterialChannelName, MString mTextureNodeName, float uTiling, float vTiling );
	void SetUVOffset( MString mMaterialName, MString mMaterialChannelName, MString mTextureNodeName, float uOffset, float vOffset );
};
