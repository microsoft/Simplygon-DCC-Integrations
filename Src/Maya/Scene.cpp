// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"

#include "Scene.h"
#include "MeshNode.h"
#include "SimplygonCmd.h"
#include "HelperFunctions.h"

void Scene::ExtractSceneGraph( SimplygonCmd* _cmd )
{
	this->cmd = _cmd;

	MItDag mDagIterator( MItDag::kBreadthFirst );

	this->sgScene = sg->CreateScene();

	// get the root node
	MDagPath mRootPath;
	mDagIterator.getPath( mRootPath );
	mDagIterator.next();

	// scan the top level nodes in the scene
	while( !mDagIterator.isDone() && mDagIterator.depth() == 1 )
	{
		MDagPath mSrcpath;
		mDagIterator.getPath( mSrcpath );

		// add the scene node, with subtree
		this->SetupSimplygonSceneNode( this->sgScene->GetRootNode(), mSrcpath );

		// get to the next top level node
		mDagIterator.next();
	}
}

static void CopyNodeTransform( spSceneNode sgNode, const MFnTransform& mTransformFn )
{
	MStatus mStatus;
	MMatrix mTransformation = mTransformFn.transformationMatrix( &mStatus );
	MAssert( mStatus, "Failed to retrieve MMatrix" );

	spMatrix4x4 sgRelativeTransform = sgNode->GetRelativeTransform();

	for( uint j = 0; j < 4; ++j )
	{
		for( uint i = 0; i < 4; ++i )
		{
			sgRelativeTransform->SetElement( i, j, real( mTransformation[ i ][ j ] ) );
		}
	}
}

spSceneNode Scene::AddSimplygonSceneCamera( MDagPath mSrcpath )
{
	const MFn::Type mApiType = mSrcpath.apiType();
	if( mApiType != MFn::Type::kTransform )
		return spSceneNode();

	MDagPath mShapePath( mSrcpath );
	if( !mShapePath.hasFn( MFn::kCamera ) )
	{
		return spSceneNode();
	}
	else if( !mShapePath.isVisible() )
	{
		return spSceneNode();
	}

	MStatus mStatus = MStatus::kFailure;
	MFnCamera mCamera( mSrcpath, &mStatus );

	if( mStatus )
	{
		const char* cCameraName = mCamera.name().asChar();

		const MPoint mPos = mCamera.eyePoint( MSpace::Space::kObject );
		const MPoint mTarget = mCamera.centerOfInterestPoint( MSpace::Space::kObject );
		const double fov = mCamera.horizontalFieldOfView();
		const double scale = mCamera.cameraScale();
		const double aspectRatio = mCamera.aspectRatio();
		const double farPlane = mCamera.farClippingPlane();
		const double nearPlane = mCamera.nearClippingPlane();
		const bool bIsOrtho = mCamera.isOrtho();
		const double orthoWidth = mCamera.orthoWidth();

		spSceneCamera sgCamera = sg->CreateSceneCamera();
		sgCamera->SetCameraType( bIsOrtho ? ECameraType::Orthographic : ECameraType::Perspective );
		sgCamera->SetFieldOfView( ( real )( fov * scale ) );

		// position
		const real rPos[] = { (real)mPos.x, (real)mPos.y, (real)mPos.z };
		spRealArray sgCameraPosition = sgCamera->GetCameraPositions();
		sgCameraPosition->SetTupleCount( 1 );
		sgCameraPosition->SetTuple( 0, rPos );

		// target
		const real rTarget[] = { (real)mTarget.x, (real)mTarget.y, (real)mTarget.z };
		spRealArray sgCameraTarget = sgCamera->GetTargetPositions();
		sgCameraTarget->SetTupleCount( 1 );
		sgCameraTarget->SetTuple( 0, rTarget );

		return spSceneNode::SafeCast( sgCamera );
	}

	return spSceneNode(); // not a camera, skip it
}

bool Scene::ExistsInActiveSet( MDagPath mSourcePath )
{
	const std::vector<std::string>& sSelectionSets = FindSelectionSets( mSourcePath );

	bool bExistsInActiveSet = false;
	if( sSelectionSets.size() > 0 )
	{
		for( int i = 0; i < sSelectionSets.size(); ++i )
		{
			const std::string& sSetName = sSelectionSets.at( i );
			const std::set<std::string>::const_iterator& sSetIterator = this->cmd->activeSelectionSets.find( sSetName );
			if( sSetIterator != this->cmd->activeSelectionSets.end() )
			{
				bExistsInActiveSet = true;
				break;
			}
		}
	}

	return bExistsInActiveSet;
}

spSceneNode Scene::AddSimplygonSceneMesh( MDagPath mSourcePath )
{
	// if there is active selection-sets in pipeline, export those objects
	const bool bExistsInActiveSet = ExistsInActiveSet( mSourcePath );

	// if not in an active set,
	// check if the node or any parent is selected for processing
	if( !bExistsInActiveSet && this->SelectedForProcessingList.length() != 0 )
	{
		bool bSelected = this->SelectedForProcessingList.hasItem( mSourcePath );
		if( !bSelected )
		{
			MDagPath mParentpath = mSourcePath;
			while( !bSelected )
			{
				MFnDagNode mDagnode( mParentpath );

				// get the parent, if one exists
				if( mDagnode.parentCount() == 0 )
				{
					// no parent, at root
					break;
				}

				// have a parent, get the node path to the parent
				MObject mParent = mDagnode.parent( 0 );
				mDagnode.setObject( mParent );
				if( !mDagnode.getPath( mParentpath ) )
				{
					break;
				}

				// check if selected
				bSelected = this->SelectedForProcessingList.hasItem( mParentpath );
			}
		}

		if( !bSelected )
		{
			return spSceneNode(); // not selected, skip it
		}
	}

	// make sure the node has a mesh shape
	MDagPath mShapePath( mSourcePath );
	if( !mShapePath.extendToShape() )
	{
		return spSceneNode(); // no shape, skip it
	}
	else if( !mShapePath.hasFn( MFn::kMesh ) )
	{
		return spSceneNode(); // no mesh, skip it
	}
	else if( !mShapePath.isVisible() )
	{
		return spSceneNode(); // not selected, skip it
	}

	// add to list
	MayaSgNodeMapping mayaSgNodeMap;
	mayaSgNodeMap.mayaNode = new MeshNode( this->cmd, mSourcePath );
	mayaSgNodeMap.sgNode = sg->CreateSceneMesh();

	this->sceneMeshes.push_back( mayaSgNodeMap );

	// we have a mesh, return it
	return spSceneNode::SafeCast( mayaSgNodeMap.sgNode );
}

void ReplaceStringInPlace( std::string& subject, const std::string& search, const std::string& replace )
{
	size_t pos = 0;
	while( ( pos = subject.find( search, pos ) ) != std::string::npos )
	{
		subject.replace( pos, search.length(), replace );
		pos += replace.length();
	}
}

std::vector<std::string> Scene::FindSelectionSets( MDagPath modifiedNode )
{
	std::vector<std::string> sSelectionSets;

	MStringArray mSetNames;
	const MStatus mStatus = ExecuteCommand( MString( "listSets -object " ) + modifiedNode.fullPathName(), mSetNames );
	if( !mStatus )
	{
		return sSelectionSets;
	}

	MFnDagNode mOriginalDagNode( modifiedNode );
	std::string sNodeName = mOriginalDagNode.name().asChar();

	for( uint s = 0; s < mSetNames.length(); ++s )
	{
		std::string sSetName = mSetNames[ s ].asChar();
		std::set<std::string>* mObjects = nullptr;

		const std::map<std::string, std::set<std::string>>::iterator& setIterator = this->cmd->selectionSets.find( sSetName );
		const bool bHasSet = setIterator != this->cmd->selectionSets.end();
		if( bHasSet )
		{
			mObjects = &setIterator->second;
		}
		else
		{
			mObjects = new std::set<std::string>();
		}

		mObjects->insert( sNodeName );

		if( !bHasSet )
		{
			this->cmd->selectionSets.insert( std::pair<std::string, std::set<std::string>>( sSetName, *mObjects ) );
			delete mObjects;
		}

		sSelectionSets.push_back( sSetName );
	}

	return sSelectionSets;
}

void Scene::SetupSimplygonSceneNode( spSceneNode sgParentNode, MDagPath mSourcePath )
{
	spSceneNode sgNode;
	MStatus mStatus;

	// get the dag node interface
	MFnDagNode mDagNodeFn( mSourcePath, &mStatus );
	MAssert( mStatus, "Failed to retrieve MFnDagNode" );

	const char* cNodeName = mDagNodeFn.name().asChar();

	// skip intermediate objects
	if( mDagNodeFn.isIntermediateObject() )
	{
		return;
	}

	// check for specific node types
	if( mSourcePath.hasFn( MFn::kJoint ) )
	{
		spSceneBone sgBone = sg->CreateSceneBone();

		// Create global bone-ID
		const uint boneIndex = this->AddSimplygonBone( mDagNodeFn.dagPath(), sgBone->GetNodeGUID().c_str() );
		this->sgScene->GetBoneTable()->AddBone( sgBone );

		sgNode = spSceneNode::SafeCast( sgBone );

		// retrieve the transformation matrix
		MFnTransform mTransformFn( mSourcePath, &mStatus );
		MAssert( mStatus, "Failed to retrieve MFnTransform" );

		CopyNodeTransform( sgNode, mTransformFn );
		FindSelectionSets( mSourcePath );
	}
	else if( mSourcePath.hasFn( MFn::kCamera ) )
	{
		sgNode = this->AddSimplygonSceneCamera( mSourcePath );

		// if no node was created, make a generic one
		if( sgNode.IsNull() )
		{
			return;
		}

		// retrieve the transformation matrix
		MFnTransform mTransformFn( mSourcePath, &mStatus );
		MAssert( mStatus, "Failed to retrieve MFnTransform" );

		CopyNodeTransform( sgNode, mTransformFn );
		FindSelectionSets( mSourcePath );
	}
	else if( mSourcePath.hasFn( MFn::kTransform ) )
	{
		// this is a transform node.
		// if the node is selected, check if we have a mesh beneath.
		sgNode = this->AddSimplygonSceneMesh( mSourcePath );

		// if no node was created, make a generic one
		if( sgNode.IsNull() )
		{
			sgNode = sg->CreateSceneNode();
		}

		// retrieve the transformation matrix
		MFnTransform mTransformFn( mSourcePath, &mStatus );
		MAssert( mStatus, "Failed to retrieve MFnTransform" );

		CopyNodeTransform( sgNode, mTransformFn );
	}
	else if( mSourcePath.hasFn( MFn::kBlendShape ) )
	{
		return;
	}
	else
	{
		// not recognized, skip it, and subtree
		return;
	}

	// add to parent, set name
	sgNode->SetName( cNodeName );
	sgParentNode->AddChild( sgNode );

	// retrieve children recursively
	const uint childCount = mSourcePath.childCount();
	for( uint childIndex = 0; childIndex < childCount; ++childIndex )
	{
		MObject mChildObject = mSourcePath.child( childIndex );
		mSourcePath.push( mChildObject );
		this->SetupSimplygonSceneNode( sgNode, mSourcePath );
		mSourcePath.pop();
	}
}

MayaSgNodeMapping* Scene::GetMeshMap( std::string sgNodeId )
{
	for( uint i = 0; i < this->sceneMeshes.size(); ++i )
	{
		spString rId = this->sceneMeshes[ i ].sgNode->GetNodeGUID();
		std::string sId = std::string( rId );
		if( sgNodeId == sId )
			return &this->sceneMeshes[ i ];
	}

	return nullptr;
}

uint Scene::AddSimplygonBone( MDagPath bonepath, std::string sgBoneID )
{
	std::string sJointPath = std::string( bonepath.fullPathName().asChar() );

	std::map<std::string, uint>::iterator boneIndexIterator = this->scene_joint_boneid_mapping.find( sJointPath );
	if( boneIndexIterator != scene_joint_boneid_mapping.end() )
	{
		scene_maya_sg_bone_mapping.insert( std::pair<std::string, std::string>( sJointPath, sgBoneID ) );
		scene_sg_maya_bone_mapping.insert( std::pair<std::string, std::string>( sgBoneID, sJointPath ) );
		return boneIndexIterator->second;
	}
	else
	{
		uint boneIndex = (uint)scene_joint_boneid_mapping.size();
		scene_joint_boneid_mapping.insert( std::pair<std::string, uint>( sJointPath, boneIndex ) );
		scene_boneid_joint_mapping.insert( std::pair<uint, std::string>( boneIndex, sJointPath ) );

		scene_maya_sg_bone_mapping.insert( std::pair<std::string, std::string>( sJointPath, sgBoneID ) );
		scene_sg_maya_bone_mapping.insert( std::pair<std::string, std::string>( sgBoneID, sJointPath ) );
		return boneIndex;
	}
}

std::string Scene::MayaJointToSgBoneID( MDagPath bonepath )
{
	std::string joint_path = std::string( bonepath.fullPathName().asChar() );

	std::map<std::string, std::string>::iterator it = this->scene_maya_sg_bone_mapping.find( joint_path );

	if( it != scene_maya_sg_bone_mapping.end() )
	{
		return it->second;
	}

	return std::string( "" );
}

MDagPath Scene::SgBoneIDToMayaJoint( std::string boneID )
{
	MDagPath path;

	std::map<std::string, std::string>::iterator it = this->scene_sg_maya_bone_mapping.find( boneID );
	if( it != this->scene_sg_maya_bone_mapping.end() )
	{
		GetPathToNamedObject( MString( it->second.c_str() ), path );
	}

	return path;
}

int Scene::GetBoneID( MDagPath bonepath )
{
	std::string joint_path = std::string( bonepath.fullPathName().asChar() );

	std::map<std::string, uint>::iterator it = this->scene_joint_boneid_mapping.find( joint_path );
	if( it != scene_joint_boneid_mapping.end() )
	{
		return it->second;
	}
	return -1;
}

MDagPath Scene::FindJointWithBoneID( rid boneid )
{
	MDagPath path;

	std::map<uint, std::string>::iterator it = this->scene_boneid_joint_mapping.find( boneid );
	if( it != scene_boneid_joint_mapping.end() )
	{
		GetPathToNamedObject( MString( it->second.c_str() ), path );
	}

	return path;
}

Scene::Scene()
{
	this->cmd = nullptr;
}

Scene::~Scene()
{
	this->sceneMeshes.clear();
	this->sgProcessedScenes.clear();
}
