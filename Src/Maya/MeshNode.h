// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <vector>
#include <map>

#include "Triangulator.h"
#include "BlindData.h"

class MaterialNode;
class MaterialHandler;
class SimplygonCmd;
class Mesh;
class MayaSgNodeMapping;

class MeshNodeLOD
{
	public:
	MDagPath LODNode;
	MDagPath LODNodeShape;
};

class MeshNodeSelectionSet
{
	public:
	std::string Name;                // the name of the set
	std::vector<rid> PolygonIndices; // the selected triangles in this set
};

class MeshNodeBone
{
	public:
	MDagPath boneNode;
};

// blend shape helper class
class BlendShapeInformation
{
	public:
	BlendShapeInformation( MPlug plug, float en )
	{
		this->plug = plug;
		this->en = en;
	}
	MPlug GetPlug() { return plug; }
	float GetEnvelope() { return en; }

	private:
	MPlug plug;
	float en;
};

class BlendWeight
{
	public:
	MString weightName;
	MString fieldName;
	float weight;
	uint fieldIndex;
	uint realIndex;
	uint globalIndex;

	BlendWeight()
	{
		this->weight = 0.f;
		this->fieldIndex = 0;
		this->realIndex = 0;
		this->globalIndex = 0;
	}
};

class BlendShape
{
	public:
	void Init( MString name, float envelope, uint numberOfWeights )
	{
		this->BlendWeights.clear();
		this->name = name;
		this->envelope = envelope;
		this->BlendWeights.resize( numberOfWeights );
	}

	MString name;
	std::vector<BlendWeight> BlendWeights;
	float envelope;

	BlendShape() { this->envelope = 0.f; }
};

class MeshNode
{
	public:
	MeshNode( SimplygonCmd* _cmd, MDagPath _originalNode );
	MeshNode( SimplygonCmd* _cmd );

	~MeshNode();

	bool hasCreaseValues;
	bool postUpdate;

	std::vector<MString> vertexLockSets;
	std::vector<MString> vertexLockMaterials;

	uint BlendShapeCount;

	protected:
	std::vector<Simplygon::Triangulator::Triangle> triangulatedPolygons;

	std::vector<MString> mMaterialNamesList;
	std::vector<std::string> mMaterialMappingIds; // maps from Materials -> Simplygon material IDs

	// list of UV sets used by the mesh object
	std::vector<MString> UVSets;
	// MString CurrentUVSetName;

	// list of Color sets used by the mesh object
	std::vector<MString> ColorSets;

	// list of the generic sets that has components in this object selected
	std::vector<MeshNodeSelectionSet> GenericSets;

	// blendshape structure
	std::vector<BlendShape> blendShape;

	// a geometry data structure that contains the copied geometry data of
	// the triangle mesh.
	spGeometryData sgMeshData;

	// the original node, that is the source of the mesh
	// this may not be triangulated
	MDagPath originalNode;
	MString originalNodeName;
	MDagPath originalNodeShape;

	// the duplicated, modified node, that is triangulated, and has the triangles that are indiced
	// this node only exists during processing in doIt, the mesh data is then extracted (into modifiedMeshData)
	// and that data is used for redoIt, while this node is deleted
	MObject modifiedTransform;
	MDagPath modifiedNode;
	MDagPath modifiedNodeShape;
	MObject originalCurrentPoseNode;

	MFnMesh MayaMesh; // this is here so we don't have to pass it to all extraction functions
	MStringArray modifiedNodeAdditionalNodes;

	// the LODs of the node
	std::vector<MeshNodeLOD> meshLODs;

	// the back mapping of the reduced vtx/polys to the original ids
	std::map<rid, rid> VertexBackMapping;
	std::map<rid, rid> PolygonBackMapping;

	BlindData blindData;

	MaterialHandler* materialHandler;

	SimplygonCmd* cmd;

	public:
	// duplicates and sets up the modified node. It triangulates the node, and sets up
	// which materials that are used by the node
	MStatus Initialize();

	// retrieve names of the materials used by the node
	std::vector<MString> GetMaterials() { return mMaterialNamesList; }

	// extract mesh data into the geometry data object, and also sets up the indices into the vector
	// of materials, which material is used by which triangle.
	MStatus ExtractMeshData( MaterialHandler* material_handler );
	MStatus ExtractMeshData_Quad( MaterialHandler* material_handler );

	// extracts blend shape data
	MStatus ExtractBlendShapeData();

	// store blend shape weights
	void OutputBaseTargetWeights( MFnBlendShapeDeformer& fn, MObject& Base );

	// store blend shape target vertices into a field
	void OutputTarget( MObject& target, spRealArray vField );

	// get a mesh map, if available
	MayaSgNodeMapping* GetInMemoryMeshMap( spSceneMesh sgMesh );

	// create a mesh data object from the (possibly modified) geometry data
	MStatus WritebackGeometryData( spScene sgProcessedScene, size_t lodIndex, spSceneMesh sgMesh, MaterialHandler* material_handler, MDagPath& result_path );
	MStatus
	WritebackGeometryData_Quad( spScene sgProcessedScene, size_t lodIndex, spSceneMesh sgMesh, MaterialHandler* material_handler, MDagPath& result_path );

	// write back normals
	void WritebackNormals();
	void WritebackNormals_Quad( uint numPolygons, uint numVertexIds );

	void WritebackNormals_Deprecated();

	// delete the modified mesh data, used for undo
	MStatus DeleteModifiedMeshDatas();

	// retrieve the path to the original node
	MDagPath GetOriginalNode() { return originalNode; };

	// retrieve the path to the modified node
	MObject& GetModifiedTransform() { return modifiedTransform; };

	// retrieve the path to the modified node
	MDagPath GetModifiedNode() { return modifiedNode; };

	// retrieve the geometry data object
	// spGeometryData GetGeometryData() { return Geom; };
	spGeometryData GetGeometryData() { return sgMeshData; };

	// get the mapped uv sets
	std::vector<MString>& GetUVSets() { return UVSets; };

	MString GetOriginalNodeName();

	MDagPath GetOriginalNodeShape();

	// Suboptimal triangulations counter
	uint32_t numBadTriangulations;

	protected:
	// get the used UV set names
	MStatus SetupUVSetNames();

	// get the used color set names
	MStatus SetupColorSetNames();

	// extract the vertex data
	MStatus ExtractVertexData();

	// extract the triangle data
	MStatus ExtractTriangleData();
	MStatus ExtractTriangleData_Quad();

	// extract the material data per-triangle
	MStatus ExtractTriangleMaterialData();
	MStatus ExtractTriangleMaterialData_Quad( MIntArray& mPolygonIndexToTriangleIndex, MIntArray& mPolygonTriangleCount );

	// extract the crease data. Both edge and vertex.
	MStatus ExtractCreaseData();
	MStatus ExtractCreaseData_Quad( MIntArray& mPolygonIndexToTriangleIndex, MIntArray& mPolygonTriangleCount );

	// Write the crease data back to the Maya scene.
	MStatus AddCreaseData();
	MStatus AddCreaseData_Quad( std::vector<int> PolygonToTriangleIndices, std::vector<int> PolygonTriangleCount );

	// extract all generic sets that has components of this object
	MStatus SetupGenericSets();

	// lock border vertices on set or material boundaries
	MStatus LockBoundaryVertices();

	// get a uniquely indexed material name
	MString GetUniqueMaterialName( MString mMaterialName );

	// find selected edges in selection sets
	MStatus FindSelectedEdges();

	// adds the mesh components to the sets the original belongs to
	MStatus AddToGenericSets();

	// setup a back mapping from the forward mapping id array
	void SetupBackMapping();
	void SetupBackMapping_Quad();

	// reset all tweaks on an object (set them to 0)
	MStatus ResetTweaks();

	// adds skinning cluster to the modified node
	MStatus AddSkinning( spScene sgProcessedScene );

	void CopyColorFieldToWeightsField( spRealArray field, bool RemoveOriginalField );
};

void DisableBlendShapes();
void EnableBlendShapes();