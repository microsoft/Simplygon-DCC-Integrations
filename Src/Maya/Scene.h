// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _Scene
#define _Scene

class MeshNode;
class SimplygonCmd;

class MayaSgNodeMapping
{
	public:
	MeshNode* mayaNode;        // the handler class of the extraction/write-back
	spSceneMesh sgNode;        // the sg node (unique)
	spGeometryData sgMeshData; // the sg mesh (may be shared)

	MayaSgNodeMapping() { this->mayaNode = nullptr; }
};

class Scene
{
	public:
	Scene();
	~Scene();

	// the Simplygon scene object
	spScene sgScene;

	std::vector<spScene> sgProcessedScenes;

	// the list of node_mesh objects, that list all nodes to be processed
	std::vector<MayaSgNodeMapping> sceneMeshes;
	MayaSgNodeMapping* GetMeshMap( std::string sgNodeId );

	uint AddSimplygonBone( MDagPath bonepath, std::string sgBoneID ); // returns the set bone-id. (or the existing bone-id if the bone was already added)
	int GetBoneID( MDagPath bonepath );                               // returns negative if no bone found.
	std::string MayaJointToSgBoneID( MDagPath bonepath );
	MDagPath SgBoneIDToMayaJoint( std::string boneID );
	MDagPath FindJointWithBoneID( Simplygon::rid boneid ); // returns negative if no bone found.

	// the list of nodes in the scene to be processed
	MSelectionList SelectedForProcessingList;

	// Setup the Simplygon scene from the Maya scene
	// and selected objects.
	void ExtractSceneGraph( SimplygonCmd* _cmd );

	private:
	// the mapping of all joints (bones) in the scene. Needed to keep track of global IDs.
	std::map<std::string, uint> scene_joint_boneid_mapping;
	std::map<uint, std::string> scene_boneid_joint_mapping;
	std::map<std::string, std::string> scene_maya_sg_bone_mapping;
	std::map<std::string, std::string> scene_sg_maya_bone_mapping;

	void SetupSimplygonSceneNode( spSceneNode parent, MDagPath srcpath );
	spSceneNode AddSimplygonSceneCamera( MDagPath srcpath );
	bool ExistsInActiveSet( MDagPath mSourcePath );
	spSceneNode AddSimplygonSceneMesh( MDagPath srcpath );
	std::vector<std::string> FindSelectionSets( MDagPath modifiedNode );

	SimplygonCmd* cmd;
};

#endif //_Scene