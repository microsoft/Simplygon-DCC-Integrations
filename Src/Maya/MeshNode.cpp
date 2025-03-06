// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"

#include "MeshNode.h"
#include "MaterialNode.h"
#include "BakedMaterial.h"

#include "SimplygonCmd.h"

#include "HelperFunctions.h"
#include "SimplygonConvenienceTemplates.h"

#include <WTypes.h>
#include <iomanip>
#include <Maya/MUuid.h>

// ---------------------------------------------------------------------------------------------------------
// Set this to TRUE if you want Simplygon to duplicate the original mesh before running PolyTriangulate on it.
// This will have the effect of making the export slower, but it won't change your original file.
// Set it to FALSE if you don't care if your mesh is triangulated for you.  Duplication takes quite a while.
// ---------------------------------------------------------------------------------------------------------
static bool UseDuplicatedMesh = true;
// ---------------------------------------------------------------------------------------------------------

void MergeTwoTrianglesIntoQuad( int tri1[ 3 ], int tri2[ 3 ], int quad[ 4 ], int originalCornerIndices[ 4 ] );

MeshNode::MeshNode( SimplygonCmd* cmd, MDagPath mOriginalNode )
{
	this->cmd = cmd;
	this->materialHandler = nullptr;

	this->hasCreaseValues = false;
	this->postUpdate = false;
	this->BlendShapeCount = 0;

	// setup the pointer to the original mesh node
	this->originalNode = mOriginalNode;
	this->originalNodeShape = this->originalNode;
	this->originalNodeShape.extendToShape();

	// get the name of the node
	MFnDagNode mOriginalDagNode( this->originalNode );
	this->originalNodeName = mOriginalDagNode.name();
	this->numBadTriangulations = 0;
}

MeshNode::MeshNode( SimplygonCmd* cmd )
{
	this->cmd = cmd;
	this->materialHandler = nullptr;
	this->hasCreaseValues = false;
	this->postUpdate = false;
	this->BlendShapeCount = 0;
	this->originalNodeName = "";
	this->numBadTriangulations = 0;
}

MeshNode::~MeshNode()
{
}

MStatus MeshNode::Initialize()
{
	MStatus mStatus = MStatus::kSuccess;

	// duplicate source mesh, if specified
	if( UseDuplicatedMesh )
	{
		mStatus = DuplicateNodeWithShape( this->originalNode, this->modifiedNode, &this->modifiedNodeAdditionalNodes, "", true );
		if( !mStatus )
		{
			return mStatus;
		}
	}

	// otherwise use original (not recommended)
	else
	{
		modifiedNode = originalNode;
	}

	this->modifiedNodeShape = this->modifiedNode;
	mStatus = this->modifiedNodeShape.extendToShape();
	if( !mStatus )
	{
		return mStatus;
	}

	// triangulate mesh
	if( !this->cmd->useQuadExportImport )
	{
		mStatus = ExecuteSelectedObjectCommand( "polyTriangulate -ch 0", this->modifiedNode, MObject::kNullObj );
		if( !mStatus )
		{
			return mStatus;
		}
	}

	// fetch all shading groups from mesh
	MStringArray mShadingGroupsList;
	mStatus = ExecuteSelectedObjectCommand( "SimplygonMaya_getSGsFromSelectedObject();", this->modifiedNode, MObject::kNullObj, mShadingGroupsList );
	if( !mStatus )
	{
		return mStatus;
	}

	// store all shading groups
	const int shadingGroupsListLength = mShadingGroupsList.length();
	mMaterialNamesList.resize( shadingGroupsListLength );
	for( uint shadingGroupIndex = 0; shadingGroupIndex < mShadingGroupsList.length(); ++shadingGroupIndex )
	{
		mMaterialNamesList[ shadingGroupIndex ] = mShadingGroupsList[ shadingGroupIndex ];
	}

	return mStatus;
}

MStatus MeshNode::ExtractMeshData( MaterialHandler* materialHandler )
{
	this->materialHandler = materialHandler;

	this->MayaMesh.setObject( this->modifiedNode );
	this->MayaMesh.syncObject();

	// setup the blind data
	this->blindData.SetupBlindDataFromMesh( this->MayaMesh );

	// setup the geometry data object
	this->sgMeshData = sg->CreateGeometryData();
	this->sgMeshData->SetTriangleCount( this->MayaMesh.numPolygons() );
	this->sgMeshData->SetVertexCount( this->MayaMesh.numVertices() );

	// setup back-mapping,
	// vertices
	spRidArray sgOriginalVertexIds =
	    spRidArray::SafeCast( this->sgMeshData->AddBaseTypeUserVertexField( Simplygon::EBaseTypes::TYPES_ID_RID, "OriginalIds", 1 ) );
	const uint originalVertexCount = sgOriginalVertexIds->GetItemCount();
	for( uint vid = 0; vid < originalVertexCount; ++vid )
	{
		sgOriginalVertexIds->SetItem( vid, vid );
	}

	// triangles
	spRidArray sgOriginalTriangleIds =
	    spRidArray::SafeCast( this->sgMeshData->AddBaseTypeUserTriangleField( Simplygon::EBaseTypes::TYPES_ID_RID, "OriginalIds", 1 ) );
	const uint originalTriangleCount = sgOriginalTriangleIds->GetItemCount();
	for( uint tid = 0; tid < originalTriangleCount; ++tid )
	{
		sgOriginalTriangleIds->SetItem( tid, tid );
	}

	// setup the used uv sets
	if( !this->SetupUVSetNames() )
	{
		return MStatus::kFailure;
	}

	// setup the color sets
	if( !this->SetupColorSetNames() )
	{
		return MStatus::kFailure;
	}

	// copy vertex data
	if( !this->ExtractVertexData() )
	{
		return MStatus::kFailure;
	}

	// copy triangle data
	if( !this->ExtractTriangleData() )
	{
		return MStatus::kFailure;
	}

	// copy crease data
	if( !this->ExtractCreaseData() )
	{
		return MStatus::kFailure;
	}

	// setup the material ids of the mesh
	if( !this->ExtractTriangleMaterialData() )
	{
		return MStatus::kFailure;
	}

	// setup the generic sets of the mesh
	if( !this->SetupGenericSets() )
	{
		return MStatus::kFailure;
	}

	// lock vertices in sets or material boundaries
	if( !this->LockBoundaryVertices() )
	{
		return MStatus::kFailure;
	}

	// lock vertices in sets or material boundaries
	if( !this->FindSelectedEdges() )
	{
		return MStatus::kFailure;
	}

	// release the object pointers
	modifiedNodeShape = MDagPath();
	this->MayaMesh.setObject( MObject::kNullObj );

	// delete meshes that has been duplicated
	if( UseDuplicatedMesh )
	{
		MGlobal::select( (const MDagPath&)this->modifiedNode, MObject::kNullObj, MGlobal::kReplaceList );
		this->modifiedNode = MDagPath();

		MGlobal::executeCommand( "delete;" );

		// remove additional nodes
		RemoveNodeList( this->modifiedNodeAdditionalNodes );
	}

	this->modifiedNodeAdditionalNodes.clear();

	return MStatus::kSuccess;
}

MStatus MeshNode::ExtractMeshData_Quad( MaterialHandler* materialHandler )
{
	this->materialHandler = materialHandler;

	this->MayaMesh.setObject( this->modifiedNode );
	this->MayaMesh.syncObject();

	// setup the blind data
	this->blindData.SetupBlindDataFromMesh( this->MayaMesh );

	// setup the geometry data object
	this->sgMeshData = sg->CreateGeometryData();

	// calculate total triangle count, including nPolys
	MIntArray mNumPolygonTriangles, mPolygonTriangleVertexIndices;
	this->MayaMesh.getTriangles( mNumPolygonTriangles, mPolygonTriangleVertexIndices );

	MIntArray mPolygonIndexToTriangleIndex;
	mPolygonIndexToTriangleIndex.setLength( mNumPolygonTriangles.length() );

	uint triangleCount = 0;
	for( uint polygonIndex = 0; polygonIndex < mNumPolygonTriangles.length(); ++polygonIndex )
	{
		mPolygonIndexToTriangleIndex[ polygonIndex ] = triangleCount;

		const int numPolygonTriangles = mNumPolygonTriangles[ polygonIndex ];
		triangleCount += numPolygonTriangles;
	}

	const uint vertexCount = this->MayaMesh.numVertices();

	// assign triangle- and vertex count to geometry data
	this->sgMeshData->SetTriangleCount( triangleCount );
	this->sgMeshData->SetVertexCount( vertexCount );

	// setup the used uv sets
	if( !this->SetupUVSetNames() )
	{
		return MStatus::kFailure;
	}

	// setup the color sets
	if( !this->SetupColorSetNames() )
	{
		return MStatus::kFailure;
	}

	// copy vertex data
	if( !this->ExtractVertexData() )
	{
		return MStatus::kFailure;
	}

	// copy triangle data
	if( !this->ExtractTriangleData_Quad() )
	{
		return MStatus::kFailure;
	}

	// setup the material ids of the mesh
	if( !this->ExtractTriangleMaterialData_Quad( mPolygonIndexToTriangleIndex, mNumPolygonTriangles ) )
	{
		return MStatus::kFailure;
	}

	// copy crease data
	if( !this->ExtractCreaseData_Quad( mPolygonIndexToTriangleIndex, mNumPolygonTriangles ) )
	{
		return MStatus::kFailure;
	}

	// setup back-mapping,
	// vertices
	spRidArray sgOriginalVertexIds =
	    spRidArray::SafeCast( this->sgMeshData->AddBaseTypeUserVertexField( Simplygon::EBaseTypes::TYPES_ID_RID, "OriginalIds", 1 ) );
	const uint originalVertexCount = sgOriginalVertexIds->GetItemCount();
	for( uint vid = 0; vid < originalVertexCount; ++vid )
	{
		sgOriginalVertexIds->SetItem( vid, vid );
	}

	// triangles
	spCharArray sgQuadFlags = this->sgMeshData->GetQuadFlags();
	spRidArray sgOriginalTriangleIds =
	    spRidArray::SafeCast( this->sgMeshData->AddBaseTypeUserTriangleField( Simplygon::EBaseTypes::TYPES_ID_RID, "OriginalIds", 1 ) );

	const uint originalTriangleCount = sgOriginalTriangleIds->GetItemCount();
	uint polygonIndex = 0;
	for( uint tid = 0; tid < originalTriangleCount; ++tid )
	{
		sgOriginalTriangleIds->SetItem( tid, polygonIndex );

		const char cQuadFlag = sgQuadFlags.GetItem( tid );
		if( cQuadFlag == SG_QUADFLAG_TRIANGLE || cQuadFlag == SG_QUADFLAG_FIRST )
		{
			++polygonIndex;
		}
	}

	// release the object pointers
	modifiedNodeShape = MDagPath();
	this->MayaMesh.setObject( MObject::kNullObj );

	// delete meshes that has been duplicated
	if( UseDuplicatedMesh )
	{
		MGlobal::select( (const MDagPath&)this->modifiedNode, MObject::kNullObj, MGlobal::kReplaceList );
		MGlobal::executeCommand( "delete;" );
		this->modifiedNode = MDagPath();

		// remove additional nodes
		RemoveNodeList( this->modifiedNodeAdditionalNodes );
	}

	this->modifiedNodeAdditionalNodes.clear();

	return MStatus::kSuccess;
}

MStatus MeshNode::SetupUVSetNames()
{
	const int numUVSets = this->MayaMesh.numUVSets();

	// copy uv-set names, if any
	if( numUVSets > 0 )
	{
		MStringArray mUVSetNames;
		if( !this->MayaMesh.getUVSetNames( mUVSetNames ) )
		{
			return MStatus::kFailure;
		}

		this->UVSets.resize( numUVSets );
		for( int uvIndex = 0; uvIndex < numUVSets; ++uvIndex )
		{
			this->UVSets[ uvIndex ] = mUVSetNames[ uvIndex ];
		}
	}

	return MStatus::kSuccess;
}

MStatus MeshNode::SetupColorSetNames()
{
	const int numColorSets = this->MayaMesh.numColorSets();

	// copy color set names, if any
	if( numColorSets > 0 )
	{
		MStringArray mColorSetNames;
		if( !this->MayaMesh.getColorSetNames( mColorSetNames ) )
		{
			return MStatus::kFailure;
		}

		this->ColorSets.resize( numColorSets );
		for( int colorSetIndex = 0; colorSetIndex < numColorSets; ++colorSetIndex )
		{
			this->ColorSets[ colorSetIndex ] = mColorSetNames[ colorSetIndex ];
		}
	}

	return MStatus::kSuccess;
}

MStatus MeshNode::ExtractVertexData()
{
	MStatus mStatus;

	this->MayaMesh.updateSurface();
	this->MayaMesh.syncObject();

	const uint vertexCount = this->MayaMesh.numVertices();
	MIntArray mVertexIds;
	MIntArray mVertexCountPerPolygon;

	mStatus = this->MayaMesh.getVertices( mVertexCountPerPolygon, mVertexIds );
	if( !mStatus )
	{
		return mStatus;
	}

	spRealArray sgCoords = this->sgMeshData->GetCoords();

	// get the skinning cluster, if any
	MString mOriginalSkinClusterName = GetSkinClusterNodeName( this->originalNode );
	MString mSkinClusterName = GetSkinClusterNodeName( this->modifiedNode );

	// weight list and bone indices list
	std::vector<std::vector<double>> tmpWeightsList( vertexCount );
	std::vector<std::vector<int>> tmpIndicesList( vertexCount );

	bool bHasBones = false;

	uint mayaTupleSize = 0;

	if( mOriginalSkinClusterName.length() > 0 && mOriginalSkinClusterName == mSkinClusterName )
	{
		MObject mSelectedOriginalNode = MObject::kNullObj;

		// original skin cluster
		{
			MGlobal::selectByName( mOriginalSkinClusterName, MGlobal::kReplaceList );
			MSelectionList mSelectionList;
			MGlobal::getActiveSelectionList( mSelectionList );

			// get the dependency node at the first position (0) in the selection list. Store this dependency node in "selectedNode"
			if( !mSelectionList.isEmpty() )
			{
				mSelectionList.getDependNode( 0, mSelectedOriginalNode );
			}

			// make sure there is a dependency node
			if( mSelectedOriginalNode == MObject::kNullObj )
			{
				std::string sErrorMessage = "Skinning: No valid nodes found in skincluster: ";
				this->cmd->LogErrorToWindow( sErrorMessage + mOriginalSkinClusterName.asChar() );
				return MStatus::kFailure;
			}
		}

		MObject mSelectedDuplicateNode = MObject::kNullObj;
		// duplicate skin cluster
		{
			MGlobal::selectByName( mSkinClusterName, MGlobal::kReplaceList );
			MSelectionList mSelectionList;
			MGlobal::getActiveSelectionList( mSelectionList );

			// get the dependency node at the first position (0) in the selection list. Store this dependency node in "selectedNode"
			if( !mSelectionList.isEmpty() )
			{
				mSelectionList.getDependNode( 0, mSelectedDuplicateNode );
			}

			// make sure there is a dependency node
			if( mSelectedDuplicateNode == MObject::kNullObj )
			{
				std::string sErrorMessage = "Skinning: No valid nodes found in skincluster: ";
				this->cmd->LogErrorToWindow( sErrorMessage + mOriginalSkinClusterName.asChar() );
				return MStatus::kFailure;
			}
		}

		// create a function set for skinClusters. This stores weight per influence object for each component of the geometry that is deformed
		MFnSkinCluster mDuplicatedSkinCluster( mSelectedDuplicateNode, &mStatus );
		if( !mStatus )
		{
			return MStatus::kSuccess;
		}

		MFnSkinCluster mOriginalSkinCluster( mSelectedOriginalNode, &mStatus );
		if( !mStatus )
		{
			return MStatus::kSuccess;
		}

		MDagPathArray mInfluenceDagPaths;
		const uint numInfluences = mOriginalSkinCluster.influenceObjects( mInfluenceDagPaths, &mStatus );

		MGlobal::select( MObject::kNullObj, MGlobal::kReplaceList );

		for( uint i = 0; i < numInfluences; ++i )
		{
			 MGlobal::select( (const MDagPath&)mInfluenceDagPaths[ i ], MObject::kNullObj );
		}

		if( !this->cmd->UseCurrentPoseAsBindPose() )
		{
			mStatus = ExecuteCommand( MString( "dagPose -restore -bindPose" ) );
			
			if( mStatus == MStatus::kFailure )
			{
				std::string sErrorMessage = "Skinning: Unable to restore asset to bindpose, please verify that your asset can be reset to bindpose before "
				                            "sending it ( dagPose -restore -bindPose ), skincluster: ";
											
				sErrorMessage += mOriginalSkinClusterName.asChar();
				sErrorMessage += ".";
				this->cmd->LogErrorToWindow( sErrorMessage );
				return MStatus::kFailure;
			}
		}


		MayaMesh.updateSurface();

		std::map<uint, uint> bidToi;
		for( uint i = 0; i < numInfluences; ++i )
		{
			MString infPath = mInfluenceDagPaths[ i ].fullPathName();
			const uint infId = mOriginalSkinCluster.indexForInfluenceObject( mInfluenceDagPaths[ i ] );
			bidToi.insert( std::pair<uint, uint>( infId, i ) );
		}

		// this plug is an array (one element for each vertex in your mesh)
		MPlug wlPlug = mDuplicatedSkinCluster.findPlug( "weightList", mStatus );
		if( !mStatus )
		{
			// we can still exit gracefully
			return MStatus::kSuccess;
		}

		MPlug wPlug = mDuplicatedSkinCluster.findPlug( "weights", mStatus );
		if( !mStatus )
		{
			// we can still exit gracefully
			return MStatus::kSuccess;
		}

		MObject wlAttr = wlPlug.attribute();
		MObject wAttr = wPlug.attribute();

		const uint numElements = wlPlug.numElements();
		for( uint vid = 0; vid < numElements; vid++ )
		{
			wPlug.selectAncestorLogicalIndex( vid, wlAttr );

			MIntArray wInfIds;
			const uint bonesPerVertex = wPlug.getExistingArrayAttributeIndices( wInfIds );

			std::vector<double> weights;
			weights.resize( bonesPerVertex );

			std::vector<int> boneIndices;
			boneIndices.resize( bonesPerVertex );

			MPlug infPlug = MPlug( wPlug );
			for( uint b = 0; b < bonesPerVertex; ++b )
			{
				const uint infId = wInfIds[ b ];
				infPlug.selectAncestorLogicalIndex( infId, wAttr );

				boneIndices[ b ] = infId;
				weights[ b ] = infPlug.asDouble();

				bHasBones = true;
			}

			if( mayaTupleSize < boneIndices.size() )
			{
				mayaTupleSize = (uint)boneIndices.size();
			}

			// out of bounds check,
			// crash fix for when adding/removing
			// faces after skin cluster creation
			if( vid < tmpIndicesList.size() )
			{
				tmpIndicesList[ vid ] = boneIndices;
				tmpWeightsList[ vid ] = weights;
			}
		}

		uint maxBonesPerVertex = Simplygon::SG_NUM_SUPPORTED_BONES_PER_VERTEX;
		if( mayaTupleSize > maxBonesPerVertex )
		{
			maxBonesPerVertex = mayaTupleSize;
		}

		spSceneBoneTable sgBoneTable = this->cmd->GetSceneHandler()->sgScene->GetBoneTable();

		this->sgMeshData->AddBoneWeights( maxBonesPerVertex );
		spRealArray sgBoneWeights = this->sgMeshData->GetBoneWeights();
		spRidArray sgBoneIds = this->sgMeshData->GetBoneIds();

		int* tmpBoneIds = new int[ maxBonesPerVertex ];
		float* tmpBoneWeights = new float[ maxBonesPerVertex ];

		// for all vertices
		for( uint vid = 0; vid < vertexCount; ++vid )
		{
			// reset ids and weights
			for( uint i = 0; i < maxBonesPerVertex; ++i )
			{
				tmpBoneIds[ i ] = -1;
				tmpBoneWeights[ i ] = 0.f;
			}

			const uint bonesPerVertexCount = (uint)tmpIndicesList[ vid ].size();

			// for all bones for this vertex
			for( uint b = 0; b < bonesPerVertexCount; ++b )
			{
				// fetch maya global bone id
				const int mayaGlobalSparseBoneIndex = tmpIndicesList[ vid ][ b ];

				const std::map<uint, uint>::const_iterator& boneIterator = bidToi.find( mayaGlobalSparseBoneIndex );
				if( boneIterator == bidToi.end() )
					continue;

				const uint mayaDirectIndexedBoneIndex = boneIterator->second;
				MDagPath mayaBonePath = mInfluenceDagPaths[ mayaDirectIndexedBoneIndex ];
				const int sgGlobalBoneIndex = cmd->GetSceneHandler()->GetBoneID( mayaBonePath );

				tmpBoneIds[ b ] = sgGlobalBoneIndex;
				tmpBoneWeights[ b ] = (float)tmpWeightsList[ vid ][ b ];
			}

			// apply to field
			sgBoneIds->SetTuple( vid, tmpBoneIds );
			sgBoneWeights->SetTuple( vid, tmpBoneWeights );
		}

		delete[] tmpBoneWeights;
		delete[] tmpBoneIds;

		// make sure the mesh is up to date
		this->MayaMesh.updateSurface();
		this->MayaMesh.syncObject();
	}

	MFloatPointArray mSourceCoords;

	if( this->cmd->UseCurrentPoseAsBindPose() )
	{
		MTime mCurrentTime;

		// Get start- and end-frame from Maya
		mCurrentTime = MAnimControl::currentTime();

		MDagPath mModifiedNodeShapeDagPath( this->modifiedNodeShape );
		MFnDependencyNode mModifiedNodeShapeDependencyNode( mModifiedNodeShapeDagPath.node(), &mStatus );

		MPlug mMeshPlug;
		MObject mMeshData;

		// Get the .outMesh plug for this mesh
		mMeshPlug = mModifiedNodeShapeDependencyNode.findPlug( MString( "outMesh" ), true, &mStatus );

		// Get its value at the specified Time.
#if MAYA_API_VERSION < 20180000
		{
			mStatus = mMeshPlug.getValue( mMeshData, MDGContext( mCurrentTime ) );
		}
#else
		{
			MDGContextGuard guard( mCurrentTime );
			mStatus = mMeshPlug.getValue( mMeshData );
		}
#endif

		// Use its MFnMesh function set
		MFnMesh mMesh( mMeshData, &mStatus );

		// And query the point coordinates
		mStatus = mMesh.getPoints( mSourceCoords );
		this->originalCurrentPoseNode = mMeshData;
	}

	else
	{
		this->MayaMesh.getPoints( mSourceCoords, MSpace::kObject );
	}

	// copy coordinates
	for( uint v = 0; v < vertexCount; ++v )
	{
		const real sgCoord[ 3 ] = { mSourceCoords[ v ].x, mSourceCoords[ v ].y, mSourceCoords[ v ].z };
		sgCoords->SetTuple( v, sgCoord );
	}

	return MStatus::kSuccess;
}

void Triangulate( MIntArray mPolygonIndices, int triangleIndex, int triangleVertices[ 3 ], bool bIsConvex )
{
	const uint numPolygonVertices = mPolygonIndices.length();
	if( numPolygonVertices == 3 )
	{
		triangleVertices[ 0 ] = mPolygonIndices[ 0 ];
		triangleVertices[ 1 ] = mPolygonIndices[ 1 ];
		triangleVertices[ 2 ] = mPolygonIndices[ 2 ];
	}
	else if( numPolygonVertices == 4 )
	{
		if( triangleIndex == 0 )
		{
			triangleVertices[ 0 ] = mPolygonIndices[ 1 ];
			triangleVertices[ 1 ] = mPolygonIndices[ 2 ];
			triangleVertices[ 2 ] = mPolygonIndices[ 3 ];
		}
		else if( triangleIndex == 1 )
		{
			triangleVertices[ 0 ] = mPolygonIndices[ 1 ];
			triangleVertices[ 1 ] = mPolygonIndices[ 3 ];
			triangleVertices[ 2 ] = mPolygonIndices[ 0 ];
		}
		else
		{
			throw std::exception( "Quad export (out of bounds) - requested generation of more than two triangles for a quad." );
		}
	}
	else
	{
		triangleVertices[ 0 ] = mPolygonIndices[ 0 ];
		triangleVertices[ 1 ] = mPolygonIndices[ triangleIndex + 1 ];
		triangleVertices[ 2 ] = mPolygonIndices[ triangleIndex + 2 ];
	}
}

int GetTriangleVertices( MFnMesh& mMesh, int polygonIndex, MIntArray& mTriangulatedVertexIndices )
{
	mTriangulatedVertexIndices.clear();

	MIntArray mPolygonVertexIndices;
	mMesh.getPolygonVertices( polygonIndex, mPolygonVertexIndices );

	const uint numPolygonVertices = mPolygonVertexIndices.length();
	if( numPolygonVertices == 3 )
	{
		const int vertId0 = mPolygonVertexIndices[ 0 ];
		const int vertId1 = mPolygonVertexIndices[ 1 ];
		const int vertId2 = mPolygonVertexIndices[ 2 ];

		mTriangulatedVertexIndices.setLength( 3 );
		mTriangulatedVertexIndices[ 0 ] = vertId0;
		mTriangulatedVertexIndices[ 1 ] = vertId1;
		mTriangulatedVertexIndices[ 2 ] = vertId2;
	}
	else if( numPolygonVertices == 4 )
	{
		const int vertId0 = mPolygonVertexIndices[ 0 ];
		const int vertId1 = mPolygonVertexIndices[ 1 ];
		const int vertId2 = mPolygonVertexIndices[ 3 ];
		const int vertId3 = mPolygonVertexIndices[ 3 ];
		const int vertId4 = mPolygonVertexIndices[ 1 ];
		const int vertId5 = mPolygonVertexIndices[ 2 ];

		mTriangulatedVertexIndices.setLength( 6 );
		mTriangulatedVertexIndices[ 0 ] = vertId0;
		mTriangulatedVertexIndices[ 1 ] = vertId1;
		mTriangulatedVertexIndices[ 2 ] = vertId2;
		mTriangulatedVertexIndices[ 3 ] = vertId3;
		mTriangulatedVertexIndices[ 4 ] = vertId4;
		mTriangulatedVertexIndices[ 5 ] = vertId5;
	}
	else
	{
		throw std::exception( "Quad export - nPoly detected, only triangles and quads are supported." );
	}

	return mTriangulatedVertexIndices.length() / 3;
}

int GetTriangleNormals( MFnMesh& mMesh, int polygonIndex, MIntArray& mTriangulatedNormalIds )
{
	mTriangulatedNormalIds.clear();

	MIntArray mPolygonNormalIndices;
	mMesh.getFaceNormalIds( polygonIndex, mPolygonNormalIndices );

	const uint numPolygonVertices = mPolygonNormalIndices.length();
	if( numPolygonVertices == 3 )
	{
		const int vertId0 = mPolygonNormalIndices[ 0 ];
		const int vertId1 = mPolygonNormalIndices[ 1 ];
		const int vertId2 = mPolygonNormalIndices[ 2 ];

		mTriangulatedNormalIds.setLength( 3 );
		mTriangulatedNormalIds[ 0 ] = vertId0;
		mTriangulatedNormalIds[ 1 ] = vertId1;
		mTriangulatedNormalIds[ 2 ] = vertId2;
	}
	else if( numPolygonVertices == 4 )
	{
		const int vertId0 = mPolygonNormalIndices[ 0 ];
		const int vertId1 = mPolygonNormalIndices[ 1 ];
		const int vertId2 = mPolygonNormalIndices[ 3 ];
		const int vertId3 = mPolygonNormalIndices[ 3 ];
		const int vertId4 = mPolygonNormalIndices[ 1 ];
		const int vertId5 = mPolygonNormalIndices[ 2 ];

		mTriangulatedNormalIds.setLength( 6 );
		mTriangulatedNormalIds[ 0 ] = vertId0;
		mTriangulatedNormalIds[ 1 ] = vertId1;
		mTriangulatedNormalIds[ 2 ] = vertId2;
		mTriangulatedNormalIds[ 3 ] = vertId3;
		mTriangulatedNormalIds[ 4 ] = vertId4;
		mTriangulatedNormalIds[ 5 ] = vertId5;
	}
	else
	{
		throw std::exception( "Quad export - nPoly detected, only triangles and quads are supported." );
	}

	return mTriangulatedNormalIds.length() / 3;
}

void GetPerPolygonUVIds( MFnMesh& mMesh, int polygonIndex, MIntArray& mPolygonUVIndices, MString mUVSet )
{
	MIntArray mPolygonVertexIndices;
	mMesh.getPolygonVertices( polygonIndex, mPolygonVertexIndices );

	mPolygonUVIndices.clear();

	const uint numVertexIndices = mPolygonVertexIndices.length();
	for( uint polygonVertexIndex = 0; polygonVertexIndex < numVertexIndices; ++polygonVertexIndex )
	{
		int uvIndex = 0;
		mMesh.getPolygonUVid( polygonIndex, polygonVertexIndex, uvIndex, &mUVSet );

		mPolygonUVIndices.append( uvIndex );
	}
}

void GetPerPolygonColorIds( MFnMesh& mMesh, int polygonIndex, MIntArray& mPolygonColorIndices, MString mUVSet )
{
	MIntArray mPolygonVertexIndices;
	mMesh.getPolygonVertices( polygonIndex, mPolygonVertexIndices );

	mPolygonColorIndices.clear();

	const uint numVertexIndices = mPolygonVertexIndices.length();
	for( uint polygonVertexIndex = 0; polygonVertexIndex < numVertexIndices; ++polygonVertexIndex )
	{
		int colorIndex = 0;
		mMesh.getColorIndex( polygonIndex, polygonVertexIndex, colorIndex, &mUVSet );

		mPolygonColorIndices.append( colorIndex );
	}
}

void GetPerPolygonTangentIds( MFnMesh& mMesh, int polygonIndex, MIntArray& mPolygonTangentIndices )
{
	MIntArray mPolygonVertexIndices;
	mMesh.getPolygonVertices( polygonIndex, mPolygonVertexIndices );

	mPolygonTangentIndices.clear();

	const uint numVertexindices = mPolygonVertexIndices.length();
	for( uint polygonVertexIndex = 0; polygonVertexIndex < numVertexindices; ++polygonVertexIndex )
	{
		const int vertexIndex = mPolygonVertexIndices[ polygonVertexIndex ];
		const int tangentId = mMesh.getTangentId( polygonIndex, vertexIndex );

		mPolygonTangentIndices.append( tangentId );
	}
}

MStatus MeshNode::ExtractTriangleData()
{
	MStatus mStatus;

	// get the iterator that will be used to step through the triangles
	MObject mModifiedNodeObject = modifiedNode.node();
	if( !this->originalCurrentPoseNode.isNull() )
	{
		mModifiedNodeObject = this->originalCurrentPoseNode;
	}

	MItMeshPolygon mMeshPolygonIterator( this->modifiedNode.node(), &mStatus );
	if( !mStatus )
	{
		return MStatus::kFailure;
	}

	
	// Checking if the normal is locked seems to trigger a recalculation which fixes potentially faulty tangents.
	// No other call has achieved the same recalculation.
	// It only works if it's called here before fields are extracted.
	bool locked = MayaMesh.isNormalLocked( 0, &mStatus );

	// we support a maximum of (SG_NUM_SUPPORTED_TEXTURE_CHANNELS-1) uv sets
	size_t numUVSets = this->UVSets.size();
	if( numUVSets > SG_NUM_SUPPORTED_TEXTURE_CHANNELS )
	{
		numUVSets = SG_NUM_SUPPORTED_TEXTURE_CHANNELS;
	}

	// we support a maximum of (SG_NUM_SUPPORTED_COLOR_CHANNELS) color sets
	size_t numColorSets = this->ColorSets.size();
	if( numColorSets > SG_NUM_SUPPORTED_COLOR_CHANNELS )
	{
		numColorSets = SG_NUM_SUPPORTED_COLOR_CHANNELS;
	}

	// data used in the loop
	MIntArray mPolyVertices;
	MVector mTempVector;
	MColorArray mTempColors;

	std::vector<spRealArray> sgTexCoords;
	std::vector<spRealArray> sgTangents;
	std::vector<spRealArray> sgBitangents;
	sgTexCoords.resize( numUVSets );
	sgTangents.resize( numUVSets );
	sgBitangents.resize( numUVSets );

	std::vector<MFloatVectorArray> mSrcTangents;
	std::vector<MFloatVectorArray> mSrcBinormals;
	mSrcTangents.resize( numUVSets );
	mSrcBinormals.resize( numUVSets );

	for( uint uvIndex = 0; uvIndex < numUVSets; ++uvIndex )
	{
		const char* cTexCoordChannelName = this->UVSets[ uvIndex ].asChar();

		// add the channel
		this->sgMeshData->AddTexCoords( uvIndex );
		sgTexCoords[ uvIndex ] = this->sgMeshData->GetTexCoords( uvIndex );
		sgTexCoords[ uvIndex ]->SetAlternativeName( cTexCoordChannelName );

		this->sgMeshData->AddTangents( uvIndex );
		sgTangents[ uvIndex ] = this->sgMeshData->GetTangents( uvIndex );

		// this->sgMeshData->AddBitangents(uvIndex);
		sgBitangents[ uvIndex ] = this->sgMeshData->GetBitangents( uvIndex );

		// retrieve the tangents & bi-normal source arrays
		this->MayaMesh.getTangents( mSrcTangents[ uvIndex ], MSpace::kObject, &UVSets[ uvIndex ] );
		this->MayaMesh.getBinormals( mSrcBinormals[ uvIndex ], MSpace::kObject, &UVSets[ uvIndex ] );
	}

	int colorSetCount = 0;
	std::vector<spRealArray> sgColors;
	sgColors.resize( numColorSets );

	for( uint colorSetIndex = 0; colorSetIndex < numColorSets; ++colorSetIndex )
	{
		const char* cColorChannelName = this->ColorSets[ colorSetIndex ].asChar();

		// add channel
		this->sgMeshData->AddColors( colorSetCount );
		sgColors[ colorSetCount ] = sgMeshData->GetColors( colorSetCount );

		sgColors[ colorSetCount ]->SetAlternativeName( cColorChannelName );
		colorSetCount++;
	}

	this->sgMeshData->AddNormals();
	spRealArray sgNormals = this->sgMeshData->GetNormals();
	spRidArray sgVertexIds = this->sgMeshData->GetVertexIds();
	
	// step through the triangles
	std::set<int> invalidColorChannels;
	for( mMeshPolygonIterator.reset(); !mMeshPolygonIterator.isDone(); mMeshPolygonIterator.next() )
	{
		const uint tid = mMeshPolygonIterator.index();

		// get the indices of vertices used by the polygon
		mMeshPolygonIterator.getVertices( mPolyVertices );

		// the three vertices of the polygon
		const int ids[ 3 ] = { mPolyVertices[ 0 ], mPolyVertices[ 1 ], mPolyVertices[ 2 ] };
		for( uint c = 0; c < 3; ++c )
		{
			const int cid = tid * 3 + c;

			sgVertexIds->SetItem( cid, ids[ c ] );
		}

		// get the normals for the vertices
		for( uint c = 0; c < 3; ++c )
		{
			const int cid = tid * 3 + c;

			// copy normal
			mMeshPolygonIterator.getNormal( c, mTempVector, MSpace::kObject );

			const real normal[ 3 ] = { (float)mTempVector[ 0 ], (float)mTempVector[ 1 ], (float)mTempVector[ 2 ] };
			sgNormals->SetTuple( cid, normal );
		}

		// get the UVs of the specified set
		float2 tempUV = { 0 };
		for( uint UVSetIndex = 0; UVSetIndex < numUVSets; ++UVSetIndex )
		{
			// if we have uvs, get them
			if( mMeshPolygonIterator.hasUVs( UVSets[ UVSetIndex ] ) )
			{
				spRealArray sgTexCoordField = sgTexCoords[ UVSetIndex ];

				// get the uvs from the set
				for( uint c = 0; c < 3; ++c )
				{
					mMeshPolygonIterator.getUV( c, tempUV, &UVSets[ UVSetIndex ] );

					const real tempTexCoord[ 2 ] = { tempUV[ 0 ], tempUV[ 1 ] };
					sgTexCoordField->SetTuple( tid * 3 + c, tempTexCoord );
				}
			}

			// otherwise, set them to zero
			else
			{
				spRealArray sgTexCoordField = sgTexCoords[ UVSetIndex ];
				const real tempTexCoord[ 2 ] = { 0.f, 0.f };

				for( uint c = 0; c < 3; ++c )
				{
					sgTexCoordField->SetTuple( tid * 3 + c, tempTexCoord );
				}
			}

			spRealArray sgTangentField = sgTangents[ UVSetIndex ];
			spRealArray sgBiTangentField = sgBitangents[ UVSetIndex ];

			// get tangents and bi-tangents as well
			for( uint c = 0; c < 3; ++c )
			{		
				const uint tangentId = mMeshPolygonIterator.tangentIndex( c );
				const uint desinationId = tid * 3 + c;

				if( mSrcTangents[ UVSetIndex ].length() > 0 && mSrcBinormals[ UVSetIndex ].length() > 0 )
				{
					const MFloatVector& mTan = mSrcTangents[ UVSetIndex ][ tangentId ];
					const MFloatVector& mBiTan = mSrcBinormals[ UVSetIndex ][ tangentId ];
					const real tanTuple[ 3 ] = { (float)mTan[ 0 ], (float)mTan[ 1 ], (float)mTan[ 2 ] };
					const real biTanTuple[ 3 ] = { (float)mBiTan[ 0 ], (float)mBiTan[ 1 ], (float)mBiTan[ 2 ] };

					sgTangentField->SetTuple( desinationId, tanTuple );
					sgBiTangentField->SetTuple( desinationId, biTanTuple );
				}
				else
				{
					const real tan_tuple[ 3 ] = { 1, 0, 0 };
					const real bitan_tuple[ 3 ] = { 0, 1, 0 };

					sgTangentField->SetTuple( desinationId, tan_tuple );
					sgBiTangentField->SetTuple( desinationId, bitan_tuple );
				}
			}
		}

		// color sets
		uint realIndex = 0;

		for( uint colorSetIndex = 0; colorSetIndex < numColorSets; colorSetIndex++ )
		{
			mStatus = mMeshPolygonIterator.getColors( mTempColors, &this->ColorSets[ colorSetIndex ] );
			if( !mStatus )
				continue;

			real color[ 4 ] = { 0.f, 0.f, 0.f, 1.f };

			// mTempColor can hold up to 4 items (RGBA)
			for( uint c = 0; c < mTempColors.length(); ++c )
			{
				int colorIndex = -1;
				mMeshPolygonIterator.getColorIndex( c, colorIndex, &this->ColorSets[ colorSetIndex ] );

				if( colorIndex == -1 )
				{
					color[ 0 ] = color[ 1 ] = color[ 2 ] = 0.f;
					color[ 3 ] = 1.f;

					invalidColorChannels.insert( colorSetIndex );
				}
				else
				{
					mTempColors[ c ].get( MColor::MColorType::kRGB, color[ 0 ], color[ 1 ], color[ 2 ], color[ 3 ] );
				}

				sgColors[ realIndex ]->SetTuple( tid * 3 + c, color );
			}

			realIndex++;
		}
	}

	for( auto i = invalidColorChannels.begin(); i != invalidColorChannels.end(); i++ )
	{
		MString mInvalidColorChannelName = this->ColorSets[ *i ];

		std::string sWarningMessage =
		    std::string( "Invalid color found in '" ) + originalNodeName.asChar() + std::string( "." ) + mInvalidColorChannelName.asChar() + std::string( "'" );
		sWarningMessage += ", falling back to (0, 0, 0, 1). Please make sure that all vertices in a color set have valid (painted) colors!";

		this->cmd->LogWarningToWindow( sWarningMessage );
	}

	return MStatus::kSuccess;
}

int GeneratePerPolygonTangentIdField( MFnMesh& mMesh, MIntArray& mSrcTangentsCount, MIntArray& mSrcTangentIds )
{
	const int numPolygons = mMesh.numPolygons();
	MIntArray mPolygonVertexIndices;

	mSrcTangentsCount.setLength( numPolygons );

	int numTangentIds = 0;
	for( int polygonIndex = 0; polygonIndex < numPolygons; ++polygonIndex )
	{
		mMesh.getPolygonVertices( polygonIndex, mPolygonVertexIndices );
		const int numVertexIndices = mPolygonVertexIndices.length();
		mSrcTangentsCount[ polygonIndex ] = numVertexIndices;
		numTangentIds += numVertexIndices;
	}

	mSrcTangentIds.setLength( numTangentIds );

	int tangentIndexCounter = 0;
	for( int polygonIndex = 0; polygonIndex < numPolygons; ++polygonIndex )
	{
		mMesh.getPolygonVertices( polygonIndex, mPolygonVertexIndices );
		const uint numVertexindices = mPolygonVertexIndices.length();
		for( uint polygonVertexindex = 0; polygonVertexindex < numVertexindices; ++polygonVertexindex )
		{
			const int tangentIndex = mMesh.getTangentId( polygonIndex, polygonVertexindex );
			mSrcTangentIds[ tangentIndexCounter++ ] = tangentIndex;
		}
	}

	return tangentIndexCounter;
}

int GeneratePerPolygonUVIdField( MFnMesh& mMesh, MIntArray& mSrcUVsCount, MIntArray& mSrcUVsIds )
{
	const int numPolygons = mMesh.numPolygons();
	MIntArray mPolygonVertexIndices;

	mSrcUVsCount.setLength( numPolygons );

	int numUVIds = 0;
	for( int polygonIndex = 0; polygonIndex < numPolygons; ++polygonIndex )
	{
		mMesh.getPolygonVertices( polygonIndex, mPolygonVertexIndices );
		const uint numVertexindices = mPolygonVertexIndices.length();
		mSrcUVsCount[ polygonIndex ] = numVertexindices;
		numUVIds += numVertexindices;
	}

	mSrcUVsIds.setLength( numUVIds );

	int uvIndexCounter = 0;
	for( int polygonIndex = 0; polygonIndex < numPolygons; ++polygonIndex )
	{
		mMesh.getPolygonVertices( polygonIndex, mPolygonVertexIndices );
		const uint numVertexindices = mPolygonVertexIndices.length();
		for( uint polygonVertexindex = 0; polygonVertexindex < numVertexindices; ++polygonVertexindex )
		{
			int uvIndex = 0;
			const int vertexIndex = mPolygonVertexIndices[ polygonVertexindex ];
			mMesh.getPolygonUVid( polygonIndex, vertexIndex, uvIndex );
			mSrcUVsIds[ uvIndexCounter++ ] = uvIndex;
		}
	}

	return uvIndexCounter;
}

int GeneratePerPolygonColorIdField( MFnMesh& mMesh, MIntArray& mSrcColorsCount, MIntArray& mSrcColorsIds )
{
	const int numPolygons = mMesh.numPolygons();
	MIntArray mPolygonVertexIndices;

	mSrcColorsCount.setLength( numPolygons );

	int numUVIds = 0;
	for( int polygonIndex = 0; polygonIndex < numPolygons; ++polygonIndex )
	{
		mMesh.getPolygonVertices( polygonIndex, mPolygonVertexIndices );
		const uint numVertexindices = mPolygonVertexIndices.length();
		mSrcColorsCount[ polygonIndex ] = numVertexindices;
		numUVIds += numVertexindices;
	}

	mSrcColorsIds.setLength( numUVIds );

	int colorIndexCounter = 0;
	for( int polygonIndex = 0; polygonIndex < numPolygons; ++polygonIndex )
	{
		mMesh.getPolygonVertices( polygonIndex, mPolygonVertexIndices );
		const uint numVertexindices = mPolygonVertexIndices.length();
		for( uint polygonVertexindex = 0; polygonVertexindex < numVertexindices; ++polygonVertexindex )
		{
			int colorIndex = 0;
			const int vertexIndex = mPolygonVertexIndices[ polygonVertexindex ];
			mMesh.getColorIndex( polygonIndex, vertexIndex, colorIndex );

			mSrcColorsIds[ colorIndexCounter++ ] = colorIndex;
		}
	}

	return colorIndexCounter;
}

MStatus MeshNode::ExtractTriangleData_Quad()
{
	MStatus mStatus;

	// get the iterator that will be used to step through the triangles
	MObject mModifiedNodeObject = this->modifiedNode.node();
	if( !this->originalCurrentPoseNode.isNull() )
	{
		mModifiedNodeObject = this->originalCurrentPoseNode;
	}

	MItMeshPolygon mMeshPolygonIterator( this->modifiedNode.node(), &mStatus );
	if( !mStatus )
	{
		return MStatus::kFailure;
	}

	// we support a maximum of (SG_NUM_SUPPORTED_TEXTURE_CHANNELS-1) uv sets
	size_t numUVSets = this->UVSets.size();
	if( numUVSets > SG_NUM_SUPPORTED_TEXTURE_CHANNELS )
	{
		numUVSets = SG_NUM_SUPPORTED_TEXTURE_CHANNELS;
	}

	// we support a maximum of (SG_NUM_SUPPORTED_COLOR_CHANNELS) color sets
	size_t numColorSets = this->ColorSets.size();
	if( numColorSets > SG_NUM_SUPPORTED_COLOR_CHANNELS )
	{
		numColorSets = SG_NUM_SUPPORTED_COLOR_CHANNELS;
	}

	// data used in the loop
	std::vector<spRealArray> sgTexCoords;
	std::vector<spRealArray> sgTangents;
	std::vector<spRealArray> sgBitangents;
	sgTexCoords.resize( numUVSets );
	sgTangents.resize( numUVSets );
	sgBitangents.resize( numUVSets );

	std::vector<MFloatArray> mSrcUs;
	std::vector<MFloatArray> mSrcVs;
	std::vector<MFloatVectorArray> mSrcTangents;
	std::vector<MFloatVectorArray> mSrcBinormals;
	mSrcUs.resize( numUVSets );
	mSrcVs.resize( numUVSets );

	mSrcTangents.resize( numUVSets );
	mSrcBinormals.resize( numUVSets );

	std::vector<MColorArray> mSrcColors;
	mSrcColors.resize( numColorSets );

	// uvs
	for( uint uvIndex = 0; uvIndex < numUVSets; ++uvIndex )
	{
		const char* cTexCoordChannelName = this->UVSets[ uvIndex ].asChar();

		// add the channel
		this->sgMeshData->AddTexCoords( uvIndex );
		sgTexCoords[ uvIndex ] = this->sgMeshData->GetTexCoords( uvIndex );
		sgTexCoords[ uvIndex ]->SetAlternativeName( cTexCoordChannelName );

		this->sgMeshData->AddTangents( uvIndex );
		sgTangents[ uvIndex ] = this->sgMeshData->GetTangents( uvIndex );

		// this->sgMeshData->AddBitangents(uvIndex);
		sgBitangents[ uvIndex ] = this->sgMeshData->GetBitangents( uvIndex );

		// retrieve the uv-, tangents and bi-normal source arrays
		this->MayaMesh.getUVs( mSrcUs[ uvIndex ], mSrcVs[ uvIndex ], &UVSets[ uvIndex ] );
		this->MayaMesh.getTangents( mSrcTangents[ uvIndex ], MSpace::kObject, &UVSets[ uvIndex ] );
		this->MayaMesh.getBinormals( mSrcBinormals[ uvIndex ], MSpace::kObject, &UVSets[ uvIndex ] );
	}

	// colors
	std::vector<spRealArray> sgColors;
	sgColors.resize( numColorSets );

	for( uint colorSetIndex = 0; colorSetIndex < numColorSets; ++colorSetIndex )
	{
		const char* cColorChannelName = this->ColorSets[ colorSetIndex ].asChar();

		// add channel
		this->sgMeshData->AddColors( colorSetIndex );
		sgColors[ colorSetIndex ] = sgMeshData->GetColors( colorSetIndex );
		sgColors[ colorSetIndex ]->SetAlternativeName( cColorChannelName );

		this->MayaMesh.getColors( mSrcColors[ colorSetIndex ], &ColorSets[ colorSetIndex ] );
	}

	this->sgMeshData->AddNormals();
	spRealArray sgNormals = this->sgMeshData->GetNormals();
	spRidArray sgVertexIds = this->sgMeshData->GetVertexIds();

	// quad flags (for quad reducer)
	this->sgMeshData->AddQuadFlags();
	spCharArray sgQuadFlags = this->sgMeshData->GetQuadFlags();

	// build source normals field
	MFloatVectorArray mSrcNormals;
	this->MayaMesh.getNormals( mSrcNormals );

	// build source tangent indices field
	MIntArray mSrcTangentsCount;
	MIntArray mSrcTangentIds;
	const int numTangentIds = GeneratePerPolygonTangentIdField( this->MayaMesh, mSrcTangentsCount, mSrcTangentIds );

	// build source uv indices field
	MIntArray mSrcUVsCount;
	MIntArray mSrcUVsIds;
	const int numUVIds = GeneratePerPolygonUVIdField( this->MayaMesh, mSrcUVsCount, mSrcUVsIds );

	// build source color indices field
	MIntArray mSrcColorsCount;
	MIntArray mSrcColorsIds;
	const int numColorIds = GeneratePerPolygonColorIdField( this->MayaMesh, mSrcColorsCount, mSrcColorsIds );

	// fetch list of polygon indices
	MIntArray mPolygonTriangleCount;
	MIntArray mPolygonTriangleVertexIndices;
	MIntArray mPolygonVertexIndices;
	this->MayaMesh.getTriangles( mPolygonTriangleCount, mPolygonTriangleVertexIndices );

	// temp buffers
	MIntArray mPolygonNormalIndices;
	MIntArray mPolygonUVIndices;
	MIntArray mPolygonColorIndices;
	MIntArray mPolygonTangentIndices;

	MVector mTempVector;

	std::set<int> invalidColorChannels;

	const int numPolygons = this->MayaMesh.numPolygons();

	this->numBadTriangulations = 0;

	const bool bUseTriangulator = true;
	if( bUseTriangulator )
	{
		// prepare data for triangulator
		spRealArray sgCoords = this->sgMeshData->GetCoords();
		const size_t glmVertexCount = sgCoords->GetTupleCount();

		std::vector<Triangulator::vec3> glmVertices;
		SetVectorFromArray<Triangulator::vec3, 3>( glmVertices, sgCoords );

		// initialize triangulator
		const Triangulator sgTriangulator( glmVertices.data(), glmVertexCount );

		std::vector<Simplygon::Triangulator::Triangle> localTriangulatedPolygons;
		uint targetPolygonIndex = 0;

		// loop all polygons
		for( int polygonIndex = 0; polygonIndex < numPolygons; ++polygonIndex )
		{
			const int numPolygonTriangles = mPolygonTriangleCount[ polygonIndex ];
			if( numPolygonTriangles > 0 )
			{
				const bool bIsQuad = numPolygonTriangles == 2;
				const bool bIsNPoly = numPolygonTriangles > 2;
				const bool bIsConvex = this->MayaMesh.isPolygonConvex( polygonIndex );

				// resize triangle output array
				if( numPolygonTriangles != localTriangulatedPolygons.size() )
				{
					localTriangulatedPolygons.resize( numPolygonTriangles );
				}

				// per-corner vertex indices
				this->MayaMesh.getPolygonVertices( polygonIndex, mPolygonVertexIndices );
				const int numPolygonVertexIndices = mPolygonVertexIndices.length();

				this->MayaMesh.getFaceNormalIds( polygonIndex, mPolygonNormalIndices );
				const int numPolygonNormalIndices = mPolygonNormalIndices.length();

				const uint* mPolygonVertexIndicesPtr = reinterpret_cast<uint*>( &mPolygonVertexIndices[ 0 ] );

				// triangulate the face
				const bool bTriangulated =
				    sgTriangulator.TriangulatePolygon( localTriangulatedPolygons.data(), mPolygonVertexIndicesPtr, numPolygonVertexIndices );

				// TODO: read enum from TriangulatePolygon and act accordingly
				if( true /*bTriangulated*/ )
				{
					// insert the triangles into the geometry
					for( int polygonTriangleIndex = 0; polygonTriangleIndex < numPolygonTriangles; ++polygonTriangleIndex, ++targetPolygonIndex )
					{
						// assign quad flags (for quad reducer)
						const char cQuadFlagToken = bIsQuad ? ( polygonTriangleIndex == 0 ? SG_QUADFLAG_FIRST : SG_QUADFLAG_SECOND ) : SG_QUADFLAG_TRIANGLE;
						sgQuadFlags->SetItem( targetPolygonIndex, cQuadFlagToken );

						const Triangulator::Triangle* sgTriangle = &localTriangulatedPolygons[ polygonTriangleIndex ];
						this->triangulatedPolygons.push_back( *sgTriangle );

						// triangulate polygon vertex indices
						for( uint c = 0; c < 3; ++c )
						{
							const int cid = targetPolygonIndex * 3 + c;
							sgVertexIds->SetItem( cid, mPolygonVertexIndices[ sgTriangle->c[ c ] ] );
						}

						// triangulate polygon normal indices
						for( uint c = 0; c < 3; ++c )
						{
							const int cid = targetPolygonIndex * 3 + c;
							const int nid = mPolygonNormalIndices[ sgTriangle->c[ c ] ];

							mTempVector = mSrcNormals[ nid ];

							const real normal[ 3 ] = { (float)mTempVector[ 0 ], (float)mTempVector[ 1 ], (float)mTempVector[ 2 ] };
							sgNormals->SetTuple( cid, normal );
						}

						// get the UVs of the specified set
						float2 mTempUV = { 0.f, 0.f };
						for( uint UVSetIndex = 0; UVSetIndex < numUVSets; ++UVSetIndex )
						{
							// if we have uvs, get them
							if( mMeshPolygonIterator.hasUVs( UVSets[ UVSetIndex ] ) )
							{
								GetPerPolygonUVIds( this->MayaMesh, polygonIndex, mPolygonUVIndices, UVSets[ UVSetIndex ] );
								spRealArray sgTexCoordField = sgTexCoords[ UVSetIndex ];

								// get the uvs from the set
								for( uint c = 0; c < 3; ++c )
								{
									const int cid = targetPolygonIndex * 3 + c;
									const int uid = mPolygonUVIndices[ sgTriangle->c[ c ] ];

									mTempUV[ 0 ] = mSrcUs[ UVSetIndex ][ uid ];
									mTempUV[ 1 ] = mSrcVs[ UVSetIndex ][ uid ];

									const real tempTexCoord[ 2 ] = { mTempUV[ 0 ], mTempUV[ 1 ] };
									sgTexCoordField->SetTuple( cid, tempTexCoord );
								}
							}

							// otherwise, set them to zero
							else
							{
								spRealArray sgTexCoordField = sgTexCoords[ UVSetIndex ];
								const real tempTexCoord[ 2 ] = { 0.f, 0.f };

								for( uint c = 0; c < 3; ++c )
								{
									const int cid = targetPolygonIndex * 3 + c;
									sgTexCoordField->SetTuple( cid, tempTexCoord );
								}
							}

							// triangulate polygon tangent indices
							GetPerPolygonTangentIds( this->MayaMesh, polygonIndex, mPolygonTangentIndices );
							spRealArray sgTangentField = sgTangents[ UVSetIndex ];
							spRealArray sgBiTangentField = sgBitangents[ UVSetIndex ];

							// get tangents and bi-tangents as well
							for( uint c = 0; c < 3; ++c )
							{
								const uint cid = targetPolygonIndex * 3 + c;
								const uint tangentId = mPolygonTangentIndices[ sgTriangle->c[ c ] ];

								if( mSrcTangents[ UVSetIndex ].length() > 0 && mSrcBinormals[ UVSetIndex ].length() > 0 )
								{
									const MFloatVector& mTan = mSrcTangents[ UVSetIndex ][ tangentId ];
									const MFloatVector& mBiTan = mSrcBinormals[ UVSetIndex ][ tangentId ];

									const real tanTuple[ 3 ] = { mTan[ 0 ], mTan[ 1 ], mTan[ 2 ] };
									const real biTanTuple[ 3 ] = { mBiTan[ 0 ], mBiTan[ 1 ], mBiTan[ 2 ] };

									sgTangentField->SetTuple( cid, tanTuple );
									sgBiTangentField->SetTuple( cid, biTanTuple );
								}
								else
								{
									const real tanTuple[ 3 ] = { 1, 0, 0 };
									const real biTanTuple[ 3 ] = { 0, 1, 0 };

									sgTangentField->SetTuple( cid, tanTuple );
									sgBiTangentField->SetTuple( cid, biTanTuple );
								}
							}
						}

						// color sets
						uint realIndex = 0;
						real color[ 4 ] = { 0.f, 0.f, 0.f, 1.f };

						for( uint colorSetIndex = 0; colorSetIndex < numColorSets; colorSetIndex++ )
						{
							GetPerPolygonColorIds( this->MayaMesh, polygonIndex, mPolygonColorIndices, ColorSets[ colorSetIndex ] );

							for( uint c = 0; c < 3; ++c )
							{
								const int cid = targetPolygonIndex * 3 + c;
								const int colorIndex = mPolygonColorIndices[ sgTriangle->c[ c ] ];

								if( colorIndex == -1 )
								{
									color[ 0 ] = color[ 1 ] = color[ 2 ] = 0.f;
									color[ 3 ] = 1.f;

									invalidColorChannels.insert( colorSetIndex );
								}
								else
								{
									MColor tempColor = mSrcColors[ colorSetIndex ][ colorIndex ];
									tempColor.get( MColor::MColorType::kRGB, color[ 0 ], color[ 1 ], color[ 2 ], color[ 3 ] );
								}

								sgColors[ realIndex ]->SetTuple( cid, color );
							}

							realIndex++;
						}
					}
				}

				if( !bTriangulated )
				{
					++numBadTriangulations;
					// TODO: Implement verbose flag
					// std::string sWarningMessage =
					//    std::string( "Quad export - Encountered a bad polygon at index " ) + std::to_string( polygonIndex ) + std::string( "." );
					// MGlobal::displayWarning( sWarningMessage.c_str() );
				}
			}
		}
	}
	else
	{
		int targetPolygonindex = 0;
		for( int polygonIndex = 0; polygonIndex < numPolygons; ++polygonIndex )
		{
			// const uint polygonIndex = mMeshPolygonIterator.index();
			const int numPolygonTriangles = mPolygonTriangleCount[ polygonIndex ];

			this->MayaMesh.getPolygonVertices( polygonIndex, mPolygonVertexIndices );
			const int numPolygonVertexIndices = mPolygonVertexIndices.length();
			this->MayaMesh.getFaceNormalIds( polygonIndex, mPolygonNormalIndices );
			const int numPolygonNormalIndices = mPolygonNormalIndices.length();

			const bool bIsQuad = numPolygonTriangles == 2;
			const bool bIsNPoly = numPolygonTriangles > 2;
			const bool bIsConvex = this->MayaMesh.isPolygonConvex( polygonIndex );
			if( !bIsConvex )
			{
				numBadTriangulations++;
			}

			// loop number of triangles in a polygon
			for( int polygonTriangleIndex = 0; polygonTriangleIndex < numPolygonTriangles; ++polygonTriangleIndex, targetPolygonindex++ )
			{
				// assign quad flags (for quad reducer)
				const char cQuadFlagToken = bIsQuad ? ( polygonTriangleIndex == 0 ? SG_QUADFLAG_FIRST : SG_QUADFLAG_SECOND ) : SG_QUADFLAG_TRIANGLE;
				sgQuadFlags->SetItem( targetPolygonindex, cQuadFlagToken );

				// triangulate polygon vertex indices
				int triangleVertexIds[ 3 ] = { -1, -1, -1 };
				Triangulate( mPolygonVertexIndices, polygonTriangleIndex, triangleVertexIds, bIsConvex );

				for( uint c = 0; c < 3; ++c )
				{
					const int cid = targetPolygonindex * 3 + c;
					sgVertexIds->SetItem( cid, triangleVertexIds[ c ] );
				}

				// triangulate polygon normal indices
				int triangleNormalIds[ 3 ] = { -1, -1, -1 };
				Triangulate( mPolygonNormalIndices, polygonTriangleIndex, triangleNormalIds, bIsConvex );

				for( uint c = 0; c < 3; ++c )
				{
					const int cid = targetPolygonindex * 3 + c;
					const int nid = triangleNormalIds[ c ];

					mTempVector = mSrcNormals[ nid ]; 

					const real normal[ 3 ] = { (float)mTempVector[ 0 ], (float)mTempVector[ 1 ], (float)mTempVector[ 2 ] };
					sgNormals->SetTuple( cid, normal );
				}

				// get the UVs of the specified set
				float2 mTempUV = { 0.f, 0.f };
				for( uint UVSetIndex = 0; UVSetIndex < numUVSets; ++UVSetIndex )
				{
					// if we have uvs, get them
					if( mMeshPolygonIterator.hasUVs( UVSets[ UVSetIndex ] ) )
					{
						GetPerPolygonUVIds( this->MayaMesh, polygonIndex, mPolygonUVIndices, UVSets[ UVSetIndex ] );

						int triangleUVIds[ 3 ];
						Triangulate( mPolygonUVIndices, polygonTriangleIndex, triangleUVIds, bIsConvex );

						spRealArray sgTexCoordField = sgTexCoords[ UVSetIndex ];

						// get the uvs from the set
						for( uint c = 0; c < 3; ++c )
						{
							const int cid = targetPolygonindex * 3 + c;
							const int uid = triangleUVIds[ c ];

							mTempUV[ 0 ] = mSrcUs[ UVSetIndex ][ uid ];
							mTempUV[ 1 ] = mSrcVs[ UVSetIndex ][ uid ];

							const real tempTexCoord[ 2 ] = { mTempUV[ 0 ], mTempUV[ 1 ] };
							sgTexCoordField->SetTuple( cid, tempTexCoord );
						}
					}

					// otherwise, set them to zero
					else
					{
						spRealArray sgTexCoordField = sgTexCoords[ UVSetIndex ];
						const real tempTexCoord[ 2 ] = { 0.f, 0.f };

						for( uint c = 0; c < 3; ++c )
						{
							const int cid = targetPolygonindex * 3 + c;
							sgTexCoordField->SetTuple( cid, tempTexCoord );
						}
					}

					int triangleTangentIds[ 3 ] = { -1, -1, -1 };
					GetPerPolygonTangentIds( this->MayaMesh, polygonIndex, mPolygonTangentIndices );
					Triangulate( mPolygonTangentIndices, polygonTriangleIndex, triangleTangentIds, bIsConvex );

					spRealArray sgTangentField = sgTangents[ UVSetIndex ];
					spRealArray sgBiTangentField = sgBitangents[ UVSetIndex ];

					// get tangents and bi-tangents as well
					for( uint c = 0; c < 3; ++c )
					{
						const uint cid = targetPolygonindex * 3 + c;
						const uint tangentId = triangleTangentIds[ c ];

						if( mSrcTangents[ UVSetIndex ].length() > 0 && mSrcBinormals[ UVSetIndex ].length() > 0 )
						{
							const MFloatVector& mTan = mSrcTangents[ UVSetIndex ][ tangentId ];
							const MFloatVector& mBiTan = mSrcBinormals[ UVSetIndex ][ tangentId ];

							const real tanTuple[ 3 ] = { mTan[ 0 ], mTan[ 1 ], mTan[ 2 ] };
							const real biTanTuple[ 3 ] = { mBiTan[ 0 ], mBiTan[ 1 ], mBiTan[ 2 ] };

							sgTangentField->SetTuple( cid, tanTuple );
							sgBiTangentField->SetTuple( cid, biTanTuple );
						}
						else
						{
							const real tanTuple[ 3 ] = { 1, 0, 0 };
							const real biTanTuple[ 3 ] = { 0, 1, 0 };

							sgTangentField->SetTuple( cid, tanTuple );
							sgBiTangentField->SetTuple( cid, biTanTuple );
						}
					}
				}

				// color sets
				uint realIndex = 0;
				real color[ 4 ] = { 0.f, 0.f, 0.f, 1.f };

				for( uint colorSetIndex = 0; colorSetIndex < numColorSets; colorSetIndex++ )
				{
					GetPerPolygonColorIds( this->MayaMesh, polygonIndex, mPolygonColorIndices, ColorSets[ colorSetIndex ] );

					int triangleColorIds[ 3 ];
					Triangulate( mPolygonColorIndices, polygonTriangleIndex, triangleColorIds, bIsConvex );

					for( uint c = 0; c < 3; ++c )
					{
						const int cid = targetPolygonindex * 3 + c;
						const int colorIndex = triangleColorIds[ c ];

						if( colorIndex == -1 )
						{
							color[ 0 ] = color[ 1 ] = color[ 2 ] = 0.f;
							color[ 3 ] = 1.f;

							invalidColorChannels.insert( colorSetIndex );
						}
						else
						{
							MColor tempColor = mSrcColors[ colorSetIndex ][ colorIndex ];
							tempColor.get( MColor::MColorType::kRGB, color[ 0 ], color[ 1 ], color[ 2 ], color[ 3 ] );
						}

						sgColors[ realIndex ]->SetTuple( cid, color );
					}

					realIndex++;
				}
			}
		}
	}

	for( std::set<int>::const_iterator& colorSetIndexIterator = invalidColorChannels.begin(); colorSetIndexIterator != invalidColorChannels.end();
	     colorSetIndexIterator++ )
	{
		MString mInvalidColorChannelName = this->ColorSets[ *colorSetIndexIterator ];

		std::string sWarningMessage = std::string( "Invalid color found in '" ) + this->originalNodeName.asChar() + std::string( "." ) +
		                              mInvalidColorChannelName.asChar() + std::string( "'" );
		sWarningMessage += ", falling back to (0, 0, 0, 1). Please make sure that all vertices in a color set have valid (painted) colors!";

		this->cmd->LogWarningToWindow( sWarningMessage );
	}

	return MStatus::kSuccess;
}

MStatus MeshNode::ExtractCreaseData()
{
	if( !this->hasCreaseValues )
		return MStatus::kSuccess;

	MStatus mStatus;
	int prevIndex = 0;

	MItMeshEdge mMeshEdgeIterator( this->modifiedNode.node(), &mStatus );
	MItMeshPolygon mMeshPolyIterator( this->modifiedNode.node(), &mStatus );

	// Edge crease values.
	MUintArray mEdgeCreaseIds;
	MDoubleArray mEdgeCreaseValues;
	this->MayaMesh.getCreaseEdges( mEdgeCreaseIds, mEdgeCreaseValues );

	const uint edgeCreaseIdCount = mEdgeCreaseIds.length();

	// Add edge-crease-values to the GeometryData.
	if( edgeCreaseIdCount > 0 )
	{
		const rid edgeCount = this->sgMeshData->GetTriangleCount() * 3;
		spDoubleArray sgEdgeCreaseValues =
		    spDoubleArray::SafeCast( this->sgMeshData->AddBaseTypeUserCornerField( Simplygon::EBaseTypes::TYPES_ID_DOUBLE, "EdgeCreaseValues", 1 ) );

		// Set all crease values to 0, this is the default value.
		for( rid i = 0; i < edgeCount; i++ )
		{
			sgEdgeCreaseValues->SetItem( i, 0.f );
		}

		// Find all polygons that have creased edges.
		MIntArray mPolyIds;
		MIntArray mVertexIds;
		for( uint i = 0; i < edgeCreaseIdCount; i++ )
		{
			// edge with a set crease-value.
			const uint e_id = mEdgeCreaseIds[ i ];
			const double crease = mEdgeCreaseValues[ i ];

			mMeshEdgeIterator.setIndex( e_id, prevIndex );

			const int v_id_start = mMeshEdgeIterator.index( 0 );
			const int v_id_end = mMeshEdgeIterator.index( 1 );

			// Find the connected polygons.
			mPolyIds.clear();
			mMeshEdgeIterator.getConnectedFaces( mPolyIds );

			// Find the IDs of the half-edges that make up the Edge.
			for( uint p = 0; p < mPolyIds.length(); p++ )
			{
				const uint t_id = mPolyIds[ p ];
				mMeshPolyIterator.setIndex( mPolyIds[ p ], prevIndex );

				mMeshPolyIterator.getVertices( mVertexIds );

				for( int c = 0; c < 3; c++ )
				{
					const int next_c = ( c + 1 ) % 3;

					if( ( ( mVertexIds[ c ] == v_id_start ) && ( mVertexIds[ next_c ] == v_id_end ) ) ||
					    ( ( mVertexIds[ next_c ] == v_id_start ) && ( mVertexIds[ c ] == v_id_end ) ) )
					{
						sgEdgeCreaseValues->SetItem( t_id * 3 + c, crease );
						break;
					}
				}
			}
		}
	}

	// Vertex crease values.
	MUintArray mVertexCreaseIds;
	MDoubleArray mVertexCreaseValues;
	this->MayaMesh.getCreaseVertices( mVertexCreaseIds, mVertexCreaseValues );

	const uint vertexCreaseIdCount = mVertexCreaseIds.length();

	// Add edge-crease-values to the GeometryData.
	if( vertexCreaseIdCount > 0 )
	{
		const uint vertexCount = this->sgMeshData->GetVertexCount();
		spDoubleArray sgVertexCreaseValues =
		    spDoubleArray::SafeCast( this->sgMeshData->AddBaseTypeUserVertexField( Simplygon::EBaseTypes::TYPES_ID_DOUBLE, "VertexCreaseValues", 1 ) );

		// Set all crease values to 0, this is the default value.
		for( uint vid = 0; vid < vertexCount; vid++ )
		{
			sgVertexCreaseValues->SetItem( vid, 0.f );
		}

		// Set the data for the vertices that actually have a value set.
		for( uint i = 0; i < mVertexCreaseIds.length(); i++ )
		{
			const uint vid = mVertexCreaseIds[ i ];
			const double crease = mVertexCreaseValues[ i ];
			sgVertexCreaseValues->SetItem( vid, crease );
		}
	}

	return mStatus;
}

MStatus MeshNode::ExtractCreaseData_Quad( MIntArray& mPolygonIndexToTriangleIndex, MIntArray& mPolygonTriangleCount )
{
	if( !this->hasCreaseValues )
		return MStatus::kSuccess;

	MStatus mStatus = MStatus::kSuccess;

	const bool bEnableEdgeCrease = false;
	if( bEnableEdgeCrease )
	{
		int prevIndex = 0;

		MItMeshEdge mMeshEdgeIterator( this->modifiedNode.node(), &mStatus );
		MItMeshPolygon mMeshPolyIterator( this->modifiedNode.node(), &mStatus );

		// Edge crease values.
		MUintArray mEdgeCreaseIds;
		MDoubleArray mEdgeCreaseValues;
		this->MayaMesh.getCreaseEdges( mEdgeCreaseIds, mEdgeCreaseValues );

		const uint mEdgeCreaseCount = mEdgeCreaseIds.length();

		// Add edge-crease-values to the GeometryData.
		if( mEdgeCreaseCount > 0 )
		{
			spDoubleArray sgEdgeCreaseValues =
			    spDoubleArray::SafeCast( this->sgMeshData->AddBaseTypeUserCornerField( Simplygon::EBaseTypes::TYPES_ID_DOUBLE, "EdgeCreaseValues", 1 ) );

			const rid sgEdgeCount = sgEdgeCreaseValues.GetItemCount();

			// Set all crease values to 0, this is the default value.
			for( rid cid = 0; cid < sgEdgeCount; ++cid )
			{
				sgEdgeCreaseValues->SetItem( cid, 0.0 );
			}

			// Find all polygons that have creased edges.
			MIntArray mPolyIds;
			MIntArray mVertexIds;
			for( uint edgeIndex = 0; edgeIndex < mEdgeCreaseCount; ++edgeIndex )
			{
				// edge with a set crease-value.
				const uint mEdgeId = mEdgeCreaseIds[ edgeIndex ];
				const double mEdgeCrease = mEdgeCreaseValues[ edgeIndex ];

				mMeshEdgeIterator.setIndex( mEdgeId, prevIndex );

				const int vid0 = mMeshEdgeIterator.index( 0 );
				const int vid1 = mMeshEdgeIterator.index( 1 );

				// Find the connected polygons.
				mPolyIds.clear();
				mMeshEdgeIterator.getConnectedFaces( mPolyIds );

				// Find the IDs of the half-edges that make up the Edge.
				for( uint polygonIndex = 0; polygonIndex < mPolyIds.length(); ++polygonIndex )
				{
					const uint polygonId = mPolyIds[ polygonIndex ];
					const uint triangleId = mPolygonIndexToTriangleIndex[ polygonId ];
					const int numPolygonTriangles = mPolygonTriangleCount[ polygonId ];

					mMeshPolyIterator.setIndex( polygonId, prevIndex );
					mMeshPolyIterator.getVertices( mVertexIds );

					int triangleVertexIds[ 3 ] = { -1, -1, -1 };
					for( int triangleIndex = 0; triangleIndex < numPolygonTriangles; ++triangleIndex )
					{
						const Triangulator::Triangle& triangle = this->triangulatedPolygons[ triangleId + triangleIndex ];

						for( uint c = 0; c < 3; ++c )
						{
							const int localVertexId = triangle.c[ c ];
							const int globalVertexId = mVertexIds[ localVertexId ];

							const int nc = ( c + 1 ) % 3;
							const int localNextVertexId = triangle.c[ nc ];
							const int globalNextVertexId = mVertexIds[ localNextVertexId ];

							if( ( ( globalVertexId == vid0 ) && ( globalNextVertexId == vid1 ) ) ||
							    ( ( globalNextVertexId == vid0 ) && ( globalVertexId == vid1 ) ) )
							{
								const uint cid = ( triangleId + triangleIndex ) * 3 + c;
								sgEdgeCreaseValues->SetItem( cid, mEdgeCrease );
								break;
							}
						}
					}
				}
			}
		}
	}

	const bool bEnableVertexCrease = false;
	if( bEnableVertexCrease )
	{
		// Vertex crease values.
		MUintArray mVertexCreaseIds;
		MDoubleArray mVertexCreaseValues;
		this->MayaMesh.getCreaseVertices( mVertexCreaseIds, mVertexCreaseValues );

		const uint mVertexCreaseCount = mVertexCreaseIds.length();

		// Add edge-crease-values to the GeometryData.
		if( mVertexCreaseCount > 0 )
		{
			spDoubleArray sgVertexCreaseValues =
			    spDoubleArray::SafeCast( this->sgMeshData->AddBaseTypeUserVertexField( Simplygon::EBaseTypes::TYPES_ID_DOUBLE, "VertexCreaseValues", 1 ) );
			const uint vertexCount = sgVertexCreaseValues.GetItemCount();

			// Set all crease values to 0, this is the default value.
			for( uint vid = 0; vid < vertexCount; ++vid )
			{
				sgVertexCreaseValues->SetItem( vid, 0.0 );
			}

			// Set the data for the vertices that actually have a value set.
			for( uint creaseIndex = 0; creaseIndex < mVertexCreaseIds.length(); ++creaseIndex )
			{
				const uint vid = mVertexCreaseIds[ creaseIndex ];
				const double crease = mVertexCreaseValues[ creaseIndex ];
				sgVertexCreaseValues->SetItem( vid, crease );
			}
		}
	}

	return mStatus;
}

MStatus MeshNode::AddCreaseData()
{
	MStatus mStatus = MStatus::kSuccess;

	// Vertex Data.
	spDoubleArray sgVertexCreaseValues = spDoubleArray::SafeCast( this->sgMeshData->GetUserVertexField( "VertexCreaseValues" ) );
	if( !sgVertexCreaseValues.IsNull() )
	{
		MUintArray mVertexCreaseIds;
		MDoubleArray mVertexCreaseValues;

		MIntArray mVertexCreaseIntIds;

		const uint vertexCount = sgVertexCreaseValues->GetItemCount();
		for( uint i = 0; i < vertexCount; i++ )
		{
			const double crease = sgVertexCreaseValues->GetItem( i );

			if( crease > 0.0 )
			{
				mVertexCreaseIds.append( i );
				mVertexCreaseIntIds.append( int( i ) );
				mVertexCreaseValues.append( crease );
			}
		}

		if( mVertexCreaseIds.length() > 0 )
		{
			// setup the component set
			MFnSingleIndexedComponent mIndices;
			MObject mVertices = mIndices.create( MFn::kMeshVertComponent );
			if( !mIndices.addElements( mVertexCreaseIntIds ) )
			{
				return MStatus::kFailure;
			}

			// apply to the components
			MString mCommand = "polyCrease -createHistory 1 -vertexValue 1";
			::ExecuteSelectedObjectCommand( mCommand, this->modifiedNode, mVertices );

			mStatus = this->MayaMesh.setCreaseVertices( mVertexCreaseIds, mVertexCreaseValues );
		}
	}

	spDoubleArray sgEdgeCreaseValues = spDoubleArray::SafeCast( this->sgMeshData->GetUserCornerField( "EdgeCreaseValues" ) );
	if( !sgEdgeCreaseValues.IsNull() )
	{
		// Edge Data.
		MItMeshPolygon mMeshPolyIterator( modifiedNode.node(), &mStatus );

		int prev_index = mMeshPolyIterator.index();

		const int num_Edges = this->MayaMesh.numEdges();
		double* mayaEdgeCreaseValues = new double[ num_Edges ];
		for( int i = 0; i < num_Edges; i++ )
		{
			mayaEdgeCreaseValues[ i ] = 0.0;
		}

		MIntArray mEdgesIds;

		for( uint i = 0; i < sgEdgeCreaseValues->GetItemCount(); i++ )
		{
			const double crease = sgEdgeCreaseValues->GetItem( i );
			if( crease > 0 )
			{
				const rid t_id = i / 3;
				const rid c_id = i % 3;

				mMeshPolyIterator.setIndex( t_id, prev_index );

				MItMeshEdge mEdgeIterator( modifiedNode.node(), &mStatus );

				mMeshPolyIterator.getEdges( mEdgesIds );

				const int e_id = mEdgesIds[ c_id ];

				mEdgeIterator.setIndex( e_id, prev_index );

				if( crease > mayaEdgeCreaseValues[ e_id ] )
				{
					mayaEdgeCreaseValues[ e_id ] = crease;
				}
			}
		}

		MUintArray mEdgeCreaseIds;
		MDoubleArray mEdgeCreaseValues;

		for( int i = 0; i < num_Edges; i++ )
		{
			if( mayaEdgeCreaseValues[ i ] > 0.0 )
			{
				mEdgeCreaseIds.append( i );
				mEdgeCreaseValues.append( mayaEdgeCreaseValues[ i ] );
			}
		}

		mStatus = this->MayaMesh.setCreaseEdges( mEdgeCreaseIds, mEdgeCreaseValues );

		delete[] mayaEdgeCreaseValues;
	}

	return mStatus;
}

MStatus MeshNode::AddCreaseData_Quad( std::vector<int> PolygonToTriangleIndices, std::vector<int> PolygonTriangleCount )
{
	MStatus mStatus = MStatus::kSuccess;

	// Vertex ids.
	spRidArray sgVertexIds = this->sgMeshData->GetVertexIds();
	spCharArray sgQuadFlags = this->sgMeshData->GetQuadFlags();

	// Vertex Data.
	spDoubleArray sgVertexCreaseValues = spDoubleArray::SafeCast( this->sgMeshData->GetUserVertexField( "VertexCreaseValues" ) );
	if( !sgVertexCreaseValues.IsNull() )
	{
		MUintArray mVertexCreaseIds;
		MDoubleArray mVertexCreaseValues;

		MIntArray mVertexCreaseIntIds;

		const uint vertexCount = sgVertexCreaseValues->GetItemCount();
		for( uint vid = 0; vid < vertexCount; vid++ )
		{
			const double crease = sgVertexCreaseValues->GetItem( vid );

			if( crease > 0.0 )
			{
				mVertexCreaseIds.append( vid );
				mVertexCreaseIntIds.append( int( vid ) );
				mVertexCreaseValues.append( crease );
			}
		}

		if( mVertexCreaseIds.length() > 0 )
		{
			// setup the component set
			MFnSingleIndexedComponent mIndices;
			MObject mVertices = mIndices.create( MFn::kMeshVertComponent );
			if( !mIndices.addElements( mVertexCreaseIntIds ) )
			{
				return MStatus::kFailure;
			}

			// apply to the components
			MString mCommand = "polyCrease -createHistory 1 -vertexValue 1";
			::ExecuteSelectedObjectCommand( mCommand, this->modifiedNode, mVertices );

			mStatus = this->MayaMesh.setCreaseVertices( mVertexCreaseIds, mVertexCreaseValues );
		}
	}

	spDoubleArray sgEdgeCreaseValues = spDoubleArray::SafeCast( this->sgMeshData->GetUserCornerField( "EdgeCreaseValues" ) );
	if( !sgEdgeCreaseValues.IsNull() )
	{
		// Edge Data.
		MItMeshPolygon mMeshPolyIterator( modifiedNode.node(), &mStatus );
		MItMeshEdge mEdgeIterator( modifiedNode.node(), &mStatus );
		MIntArray mPolygonEdgeIndices;

		int previousEdgeIndex = mMeshPolyIterator.index();
		const int numEdges = this->MayaMesh.numEdges();

		double* edgeCreaseValues = new double[ numEdges ];
		for( int edgeIndex = 0; edgeIndex < numEdges; ++edgeIndex )
		{
			edgeCreaseValues[ edgeIndex ] = 0.0;
		}

		// const uint numQuadFlags = 0;

		// const uint currentPolygon = 0;
		uint currentSimplygonCornerIndex = 0;
		// uint currentMayaCornerIndex = 0;
		MIntArray mVertexIds;

		for( int polygonIndex = 0; polygonIndex < (int)PolygonToTriangleIndices.size(); ++polygonIndex )
		{
			const int triangleId = PolygonToTriangleIndices[ polygonIndex ];
			const int numPolygonTriangles = PolygonTriangleCount[ polygonIndex ];

			mMeshPolyIterator.setIndex( polygonIndex, previousEdgeIndex );
			mMeshPolyIterator.getVertices( mVertexIds );
			mMeshPolyIterator.getEdges( mPolygonEdgeIndices );

			// if triangle
			if( numPolygonTriangles == 1 )
			{
				for( uint c = 0; c < 3; ++c )
				{
					const int mEdgeIndex = mPolygonEdgeIndices[ c ];
					mEdgeIterator.setIndex( mEdgeIndex, previousEdgeIndex );

					const double sgCrease = sgEdgeCreaseValues->GetItem( currentSimplygonCornerIndex++ );
					if( sgCrease > edgeCreaseValues[ mEdgeIndex ] )
					{
						edgeCreaseValues[ mEdgeIndex ] = sgCrease;
					}
				}
			}

			// if quad
			else if( numPolygonTriangles == 2 )
			{
				int triangle1VertexIds[ 3 ] = { -1, -1, -1 };
				triangle1VertexIds[ 0 ] = sgVertexIds->GetItem( currentSimplygonCornerIndex + 0 );
				triangle1VertexIds[ 1 ] = sgVertexIds->GetItem( currentSimplygonCornerIndex + 1 );
				triangle1VertexIds[ 2 ] = sgVertexIds->GetItem( currentSimplygonCornerIndex + 2 );

				int triangle2VertexIds[ 3 ] = { -1, -1, -1 };
				triangle2VertexIds[ 0 ] = sgVertexIds->GetItem( currentSimplygonCornerIndex + 3 );
				triangle2VertexIds[ 1 ] = sgVertexIds->GetItem( currentSimplygonCornerIndex + 4 );
				triangle2VertexIds[ 2 ] = sgVertexIds->GetItem( currentSimplygonCornerIndex + 5 );

				double d[ 6 ];
				d[ 0 ] = sgEdgeCreaseValues->GetItem( currentSimplygonCornerIndex + 0 );
				d[ 1 ] = sgEdgeCreaseValues->GetItem( currentSimplygonCornerIndex + 1 );
				d[ 2 ] = sgEdgeCreaseValues->GetItem( currentSimplygonCornerIndex + 2 );
				d[ 3 ] = sgEdgeCreaseValues->GetItem( currentSimplygonCornerIndex + 3 );
				d[ 4 ] = sgEdgeCreaseValues->GetItem( currentSimplygonCornerIndex + 4 );
				d[ 5 ] = sgEdgeCreaseValues->GetItem( currentSimplygonCornerIndex + 5 );

				int quadVertexIds[ 4 ] = { -1, -1, -1, -1 };
				int originalCornerIndices[ 4 ] = { -1, -1, -1, -1 };
				MergeTwoTrianglesIntoQuad( triangle1VertexIds, triangle2VertexIds, quadVertexIds, originalCornerIndices );

				for( uint c = 0; c < 4; ++c )
				{
					// Maya ids
					const int mPolygonEdgeIndex = mPolygonEdgeIndices[ c ];
					mEdgeIterator.setIndex( mPolygonEdgeIndex, previousEdgeIndex );
					int e0 = mEdgeIterator.index( 0 );
					int e1 = mEdgeIterator.index( 1 );

					// Simplygon ids
					const int nc = ( c + 1 ) % 4;
					const int sgLocalCornerId = originalCornerIndices[ c ];
					const int sgGlobalCornerId = currentSimplygonCornerIndex + sgLocalCornerId;

					// copy crease
					const double sgCrease = sgEdgeCreaseValues->GetItem( sgGlobalCornerId );
					if( sgCrease > edgeCreaseValues[ mPolygonEdgeIndex ] )
					{
						edgeCreaseValues[ mPolygonEdgeIndex ] = sgCrease;
					}
				}

				currentSimplygonCornerIndex += 6;
			}
		}

		MUintArray mEdgeCreaseIds;
		MDoubleArray mEdgeCreaseValues;

		for( int edgeIndex = 0; edgeIndex < numEdges; ++edgeIndex )
		{
			if( edgeCreaseValues[ edgeIndex ] > 0.0 )
			{
				mEdgeCreaseIds.append( edgeIndex );
				mEdgeCreaseValues.append( edgeCreaseValues[ edgeIndex ] );
			}
		}

		mStatus = this->MayaMesh.setCreaseEdges( mEdgeCreaseIds, mEdgeCreaseValues );

		delete[] edgeCreaseValues;
	}

	return mStatus;
}

MStatus MeshNode::ExtractTriangleMaterialData()
{
	const uint TriangleCount = this->sgMeshData->GetTriangleCount();

	this->sgMeshData->AddMaterialIds();
	spRidArray sgMaterialIds = this->sgMeshData->GetMaterialIds();

	// set all triangles to a default -1 value
	for( uint tid = 0; tid < TriangleCount; ++tid )
	{
		sgMaterialIds->SetItem( tid, -1 );
	}

	mMaterialMappingIds.resize( mMaterialNamesList.size() );

	// add material to the material map.
	// At this point, a Default material is already added to the map
	for( size_t materialIndex = 0; materialIndex < mMaterialNamesList.size(); ++materialIndex )
	{
		mMaterialMappingIds[ materialIndex ] = materialHandler->GetSimplygonMaterialForShape( mMaterialNamesList[ materialIndex ], this );
	}

	MObjectArray mShaderObjects;
	MIntArray mIndices;

	if( MStatus::kSuccess == this->MayaMesh.getConnectedShaders( 0, mShaderObjects, mIndices ) )
	{
		const uint numShaders = mShaderObjects.length();
		if( numShaders > 0 )
		{
			std::string* sMapping = new std::string[ numShaders ];

			// for each shader, setup a mapping into our Materials
			for( uint shaderIndex = 0; shaderIndex < numShaders; ++shaderIndex )
			{
				MFnDependencyNode mShaderDependencyNode( mShaderObjects[ shaderIndex ] );
				MString mShaderName = mShaderDependencyNode.name();

				// find our material
				sMapping[ shaderIndex ] = std::string( "" );
				for( size_t q = 0; q < mMaterialNamesList.size(); ++q )
				{
					if( mMaterialNamesList[ q ] == mShaderName )
					{
						sMapping[ shaderIndex ] = mMaterialMappingIds[ q ];
						break;
					}
				}

				if( sMapping[ shaderIndex ] == std::string( "" ) )
				{
					MGlobal::displayWarning( MString( "Simplygon: Could not find a mapping of the material " ) + mShaderName );
				}
			}

			const uint numIndices = mIndices.length();

			// now do all triangles
			for( uint t = 0; t < numIndices; ++t )
			{
				// get shader index, map into our materials
				const int index = mIndices[ t ];

				// if valid material
				if( index >= 0 )
				{
					std::string sMaterialId = sMapping[ index ];
					// int sgMaterialIndex = this->materialHandler->GetMaterialTable()->FindMaterialId(sMaterialId.c_str());
					std::map<std::string, int>::iterator materialIdMap = this->materialHandler->MaterialIdToMaterialIndex.find( sMaterialId );

					if( materialIdMap != this->materialHandler->MaterialIdToMaterialIndex.end() && materialIdMap->second >= 0 )
					{
						sgMaterialIds->SetItem( t, materialIdMap->second );
					}
				}
				else
				{
					// use material id -1
					sgMaterialIds->SetItem( t, -1 );
				}
			}

			delete[] sMapping;
		}
	}

	// check that all triangles have received a value
	bool bHasTrianglesWithoutMaterialIds = false;
	for( uint tid = 0; tid < TriangleCount; ++tid )
	{
		if( sgMaterialIds->GetItem( tid ) < 0 )
		{
			sgMaterialIds->SetItem( tid, 0 );
			bHasTrianglesWithoutMaterialIds = true;
		}
	}

	if( bHasTrianglesWithoutMaterialIds )
	{
		MGlobal::displayWarning( "Simplygon: Not all polygons have a material id applied to them." );
	}

	return MStatus::kSuccess;
}

MStatus MeshNode::ExtractTriangleMaterialData_Quad( MIntArray& mPolygonIndexToTriangleIndex, MIntArray& mPolygonTriangleCount )
{
	const uint triangleCount = this->sgMeshData->GetTriangleCount();
	spCharArray sgQuadFlags = this->sgMeshData->GetQuadFlags();

	this->sgMeshData->AddMaterialIds();
	spRidArray sgMaterialIds = this->sgMeshData->GetMaterialIds();

	// set all triangles to a default -1 value
	for( uint tid = 0; tid < triangleCount; ++tid )
	{
		sgMaterialIds->SetItem( tid, -1 );
	}

	mMaterialMappingIds.resize( mMaterialNamesList.size() );

	// add material to the material map.
	// At this point, a Default material is already added to the map
	for( size_t materialIndex = 0; materialIndex < mMaterialNamesList.size(); ++materialIndex )
	{
		mMaterialMappingIds[ materialIndex ] = materialHandler->GetSimplygonMaterialForShape( mMaterialNamesList[ materialIndex ], this );
	}

	MObjectArray mShaderObjects;
	MIntArray mPolygonMaterialIndices;

	if( this->MayaMesh.getConnectedShaders( 0, mShaderObjects, mPolygonMaterialIndices ) == MStatus::kSuccess )
	{
		const uint numShaders = mShaderObjects.length();
		if( numShaders > 0 )
		{
			std::string* sMapping = new std::string[ numShaders ];

			// for each shader, setup a mapping into our Materials
			for( uint shaderIndex = 0; shaderIndex < numShaders; ++shaderIndex )
			{
				MFnDependencyNode mShaderDependencyNode( mShaderObjects[ shaderIndex ] );
				MString mShaderName = mShaderDependencyNode.name();

				// find our material
				sMapping[ shaderIndex ] = std::string( "" );
				for( size_t q = 0; q < mMaterialNamesList.size(); ++q )
				{
					if( mMaterialNamesList[ q ] == mShaderName )
					{
						sMapping[ shaderIndex ] = mMaterialMappingIds[ q ];
						break;
					}
				}

				if( sMapping[ shaderIndex ] == std::string( "" ) )
				{
					MGlobal::displayWarning( MString( "Simplygon: Could not find a mapping of the material " ) + mShaderName );
				}
			}

			const uint numPolygonMaterialIndices = mPolygonMaterialIndices.length();

			// now do all triangles
			for( uint polygonIndex = 0; polygonIndex < numPolygonMaterialIndices; ++polygonIndex )
			{
				// get shader index, map into our materials
				const int polygonMaterialIndex = mPolygonMaterialIndices[ polygonIndex ];

				// if valid material
				if( polygonMaterialIndex >= 0 )
				{
					std::string sMaterialId = sMapping[ polygonMaterialIndex ];
					// int sgMaterialIndex = this->materialHandler->GetMaterialTable()->FindMaterialId(sMaterialId.c_str());
					std::map<std::string, int>::iterator materialIdMap = this->materialHandler->MaterialIdToMaterialIndex.find( sMaterialId );

					if( materialIdMap != this->materialHandler->MaterialIdToMaterialIndex.end() && materialIdMap->second >= 0 )
					{
						const int triangleIndex = mPolygonIndexToTriangleIndex[ polygonIndex ];
						const int numTrianglesPerPolygon = mPolygonTriangleCount[ polygonIndex ];

						const char cQuadFlag = sgQuadFlags.GetItem( triangleIndex );
						if( cQuadFlag == SG_QUADFLAG_FIRST )
						{
							sgMaterialIds->SetItem( triangleIndex, materialIdMap->second );
							sgMaterialIds->SetItem( triangleIndex + 1, materialIdMap->second );
						}
						else if( cQuadFlag == SG_QUADFLAG_SECOND )
						{
							sgMaterialIds->SetItem( triangleIndex - 1, materialIdMap->second );
							sgMaterialIds->SetItem( triangleIndex, materialIdMap->second );
						}
						else
						{
							// floodfill polygon
							for( int t = 0; t < numTrianglesPerPolygon; ++t )
							{
								sgMaterialIds->SetItem( triangleIndex + t, materialIdMap->second );
							}
						}
					}
				}
				else
				{
					// use material id -1
					const int triangleIndex = mPolygonIndexToTriangleIndex[ polygonIndex ];
					const int numTrianglesPerPolygon = mPolygonTriangleCount[ polygonIndex ];

					const char cQuadFlag = sgQuadFlags.GetItem( triangleIndex );
					if( cQuadFlag == SG_QUADFLAG_FIRST )
					{
						sgMaterialIds->SetItem( triangleIndex, -1 );
						sgMaterialIds->SetItem( triangleIndex + 1, -1 );
					}
					else if( cQuadFlag == SG_QUADFLAG_SECOND )
					{
						sgMaterialIds->SetItem( triangleIndex - 1, -1 );
						sgMaterialIds->SetItem( triangleIndex, -1 );
					}
					else
					{
						// floodfill polygon
						for( int t = 0; t < numTrianglesPerPolygon; ++t )
						{
							sgMaterialIds->SetItem( triangleIndex + t, -1 );
						}
					}
				}
			}

			delete[] sMapping;
		}
	}

	// check that all triangles have received a value,
	// if not, default invalid ones to 0.
	bool bHasTrianglesWithoutMaterialIds = false;
	for( uint tid = 0; tid < triangleCount; ++tid )
	{
		if( sgMaterialIds->GetItem( tid ) < 0 )
		{
			sgMaterialIds->SetItem( tid, 0 );
			bHasTrianglesWithoutMaterialIds = true;
		}
	}

	if( bHasTrianglesWithoutMaterialIds )
	{
		MGlobal::displayWarning( "Simplygon: Not all polygons have a material id applied to them." );
	}

	return MStatus::kSuccess;
}

MStatus MeshNode::SetupGenericSets()
{
	MStatus mStatus;

	MObjectArray mSets;
	MObjectArray mComponents;

	const int instanceNumber = this->modifiedNodeShape.instanceNumber();
	this->MayaMesh.getConnectedSetsAndMembers( instanceNumber, mSets, mComponents, false );

	for( uint i = 0; i < mSets.length(); ++i )
	{
		const MFn::Type mSetType = mSets[ i ].apiType();
		const MFn::Type mComponentType = mComponents[ i ].apiType();

		// only store sets with selected polygon components
		if( mSetType == MFn::kSet && mComponentType == MFn::kMeshPolygonComponent )
		{
			MFnSet mSet( mSets[ i ] );

			// get the set name
			MeshNodeSelectionSet selectionSet;
			selectionSet.Name = std::string( mSet.name().asChar() );

			// get the polygon indices, and store into vector
			MItMeshPolygon mMeshPolygonIterator( this->modifiedNodeShape, mComponents[ i ], &mStatus );
			if( !mStatus )
				return mStatus;

			while( !mMeshPolygonIterator.isDone() )
			{
				const uint polygonIndex = mMeshPolygonIterator.index();
				selectionSet.PolygonIndices.push_back( polygonIndex );
				mMeshPolygonIterator.next();
			}

			// add into set vector
			this->GenericSets.push_back( selectionSet );
		}
	}

	return MStatus::kSuccess;
}

bool ExistsInMIntArray( const MIntArray& mEdgeArray, int edgeID )
{
	for( uint i = 0; i < mEdgeArray.length(); ++i )
	{
		if( mEdgeArray[ i ] == edgeID )
		{
			return true;
		}
	}

	return false;
}

std::map<uint, uint>* FindEdgeIdsFromVertexPairs( MDagPath mDagPath, MObject mComponent, int vid0, int vid1 )
{
	std::map<uint, uint>* selectedEdgesMap = new std::map<uint, uint>();

	int previousVertexIndex = 0;

	// get first iterator
	MItMeshVertex mVertexIterator0( mDagPath );
	mVertexIterator0.setIndex( vid0, previousVertexIndex );

	MIntArray mConnectedEdges0;
	mVertexIterator0.getConnectedEdges( mConnectedEdges0 );

	// get second iterator
	MItMeshVertex mVertexIterator1( mDagPath );
	mVertexIterator1.setIndex( vid1, previousVertexIndex );

	MIntArray mConnectedEdges1;
	mVertexIterator1.getConnectedEdges( mConnectedEdges1 );

	for( uint i = 0; i < mConnectedEdges0.length(); ++i )
	{
		const int edgeId = mConnectedEdges0[ i ];

		const bool bIdExists = ExistsInMIntArray( mConnectedEdges1, edgeId );
		if( bIdExists )
		{
			selectedEdgesMap->insert( std::pair<uint, uint>( edgeId, edgeId ) );
		}
	}

	// return selected edges list
	return selectedEdgesMap;
}

MStatus MeshNode::FindSelectedEdges()
{
	MStatus mStatus;

	const uint triangleCount = this->sgMeshData->GetTriangleCount();
	const uint vertexCount = this->sgMeshData->GetVertexCount();
	const uint cornerCount = triangleCount * 3;

	// find all sets, add all vertices that are directly specified in these sets
	MObjectArray mSets;
	MObjectArray mComponents;

	const int instanceNumber = this->modifiedNodeShape.instanceNumber();
	this->MayaMesh.getConnectedSetsAndMembers( instanceNumber, mSets, mComponents, false );

	char cNameBuffer[ MAX_PATH ] = { 0 };
	uint numSelectionSets = 0;

	for( uint i = 0; i < mSets.length(); ++i )
	{
		const MFn::Type mSetType = mSets[ i ].apiType();
		const MFn::Type mComponentType = mComponents[ i ].apiType();

		if( mSetType == MFn::kSet )
		{
			MFnSet mSet( mSets[ i ] );

			// only lock sets that are in the vertex lock array
			MString mSetName( mSet.name() );

			// create and reset the vertex lock field
			sprintf( cNameBuffer, "SelectionSet%u", numSelectionSets );
			numSelectionSets++;

			spBoolArray sgSelectedEdgeField = spBoolArray::SafeCast( this->sgMeshData->GetUserCornerField( cNameBuffer ) );
			if( sgSelectedEdgeField.IsNull() )
			{
				// if null, create field
				sgSelectedEdgeField = spBoolArray::SafeCast( this->sgMeshData->AddBaseTypeUserCornerField( EBaseTypes::TYPES_ID_BOOL, cNameBuffer, 1 ) );
				sgSelectedEdgeField->SetAlternativeName( mSetName.asChar() );

				for( uint c = 0; c < cornerCount; ++c )
				{
					sgSelectedEdgeField->SetItem( c, false );
				}
			}

			// check for edges
			if( mComponentType == MFn::kMeshEdgeComponent )
			{
				MItMeshEdge mMeshEdgeIterator( this->modifiedNodeShape, mComponents[ i ], &mStatus );
				if( !mStatus )
					return mStatus;

				spRidArray sgVertexPairs = sg->CreateRidArray();
				sgVertexPairs->SetTupleSize( 2 );

				// fetch all vertex ids
				while( !mMeshEdgeIterator.isDone() )
				{
					const uint vIndex0 = mMeshEdgeIterator.index( 0 );
					const uint vIndex1 = mMeshEdgeIterator.index( 1 );

					int2 tuple;
					tuple[ 0 ] = vIndex0;
					tuple[ 1 ] = vIndex1;
					sgVertexPairs->AddTuple( tuple );

					tuple[ 0 ] = vIndex1;
					tuple[ 1 ] = vIndex0;
					sgVertexPairs->AddTuple( tuple );

					mMeshEdgeIterator.next();
				}

				spRidArray sgEdgeIds = sg->CreateRidArray();

				this->sgMeshData->FindEdgeIdsFromVertexPairs( sgVertexPairs, sgEdgeIds );

				for( uint e = 0; e < sgEdgeIds->GetTupleCount(); ++e )
				{
					const rid id = sgEdgeIds->GetItem( e );
					sgSelectedEdgeField->SetItem( id, true );
				}
			}
		}
	}

	return MStatus::kSuccess;
}

MStatus MeshNode::LockBoundaryVertices()
{
	MStatus mStatus = MStatus::kSuccess;

	const uint triangleCount = this->sgMeshData->GetTriangleCount();
	const uint vertexCount = this->sgMeshData->GetVertexCount();
	const uint triangleCornerCount = triangleCount * 3;

	spRidArray sgVertexIds = this->sgMeshData->GetVertexIds();

	// copy vertex locks, if any
	if( !this->vertexLockSets.empty() )
	{
		spBoolArray sgVertexLocks = sgMeshData->GetVertexLocks();
		if( sgVertexLocks.IsNull() )
		{
			this->sgMeshData->AddVertexLocks();
			sgVertexLocks = sgMeshData->GetVertexLocks();
			for( uint i = 0; i < vertexCount; ++i )
			{
				sgVertexLocks->SetItem( i, false );
			}
		}

		// find all sets, add all vertices that are directly specified in these sets
		MObjectArray mSets;
		MObjectArray mComponents;

		const int instanceNumber = this->modifiedNodeShape.instanceNumber();
		this->MayaMesh.getConnectedSetsAndMembers( instanceNumber, mSets, mComponents, false );

		for( uint setIndex = 0; setIndex < mSets.length(); ++setIndex )
		{
			const MFn::Type mSetType = mSets[ setIndex ].apiType();
			const MFn::Type mSetComponentType = mComponents[ setIndex ].apiType();

			if( mSetType == MFn::kSet )
			{
				MFnSet mSet( mSets[ setIndex ] );

				// only lock sets that are in the vertex lock array
				MString mSetName( mSet.name() );
				bool bSetExists = false;
				for( uint q = 0; q < uint( this->vertexLockSets.size() ); ++q )
				{
					if( mSetName == this->vertexLockSets[ q ] )
					{
						bSetExists = true;
						break;
					}
				}
				if( !bSetExists )
				{
					continue;
				}

				// check for vertices
				if( mSetComponentType == MFn::kMeshVertComponent )
				{
					// get the vertex indices, and lock the vertices
					MItMeshVertex mMeshVertexIterator( this->modifiedNodeShape, mComponents[ setIndex ], &mStatus );
					if( !mStatus )
					{
						return mStatus;
					}

					while( !mMeshVertexIterator.isDone() )
					{
						const int vertexId = mMeshVertexIterator.index();
						sgVertexLocks->SetItem( vertexId, true );
						mMeshVertexIterator.next();
					}
				}

				// check for edges
				if( mSetComponentType == MFn::kMeshEdgeComponent )
				{
					// get the vertex indices, and lock the vertices
					MItMeshEdge mMeshEdgeIterator( this->modifiedNodeShape, mComponents[ setIndex ], &mStatus );
					if( !mStatus )
					{
						return mStatus;
					}

					while( !mMeshEdgeIterator.isDone() )
					{
						const uint indexA = mMeshEdgeIterator.index( 0 );
						sgVertexLocks->SetItem( indexA, true );

						const uint indexB = mMeshEdgeIterator.index( 1 );
						sgVertexLocks->SetItem( indexB, true );

						mMeshEdgeIterator.next();
					}
				}
			}
		}

		bool* setVertices = new bool[ vertexCount ];
		bool* setTriangles = new bool[ triangleCount ];

		for( size_t setIndex = 0; setIndex < GenericSets.size(); ++setIndex )
		{
			const MeshNodeSelectionSet& selectionSet = GenericSets[ setIndex ];

			// only lock sets that are in the vertex lock
			MString mSetName( selectionSet.Name.c_str() );
			bool bSetExists = false;
			for( uint i = 0; i < uint( this->vertexLockSets.size() ); ++i )
			{
				if( mSetName == this->vertexLockSets[ i ] )
				{
					bSetExists = true;
					break;
				}
			}

			if( !bSetExists )
			{
				continue;
			}

			// reset the arrays
			memset( setVertices, 0, vertexCount * sizeof( bool ) );
			memset( setTriangles, 0, triangleCount * sizeof( bool ) );

			// mark all triangles and vertices that belong to this set
			for( size_t q = 0; q < selectionSet.PolygonIndices.size(); ++q )
			{
				const rid tid = selectionSet.PolygonIndices[ q ];

				// this triangle belongs to a set, mark as such
				setTriangles[ tid ] = true;

				// find the vertices and mark them to belong to this set
				for( uint e = 0; e < 3; ++e )
				{
					const int vid = sgVertexIds->GetItem( tid * 3 + e );

					// set the new set id
					setVertices[ vid ] = true;
				}
			}

			// now, walk through all triangles and look for
			// vertices that are marked as belonging to the set
			for( uint tid = 0; tid < triangleCount; ++tid )
			{
				// only do triangles that does not belong to the set
				if( setTriangles[ tid ] )
					continue;

				// find the vertices check if they belong to the set
				for( uint e = 0; e < 3; ++e )
				{
					const int vid = sgVertexIds->GetItem( tid * 3 + e );

					// if the set is on the set, mark the vertex as locked
					if( setVertices[ vid ] )
					{
						// mark the vertex as locked
						sgVertexLocks->SetItem( vid, true );
					}
				}
			}
		}

		// delete temp arrays
		delete[] setVertices;
		delete[] setTriangles;
	}

	// look for material boundary vertices
	if( !this->vertexLockMaterials.empty() )
	{
		spMaterialTable sgMaterialTable = this->cmd->GetSceneHandler()->sgScene->GetMaterialTable();
		spBoolArray sgVertexLocks = sgMeshData->GetVertexLocks();
		if( sgVertexLocks.IsNull() )
		{
			this->sgMeshData->AddVertexLocks();
			sgVertexLocks = sgMeshData->GetVertexLocks();
			for( uint vid = 0; vid < vertexCount; ++vid )
			{
				sgVertexLocks->SetItem( vid, false );
			}
		}

		std::string* sVertexSet = new std::string[ vertexCount ];

		for( size_t mid = 0; mid < mMaterialNamesList.size(); ++mid )
		{
			MaterialNode* materialNode = this->materialHandler->GetMaterial( mMaterialNamesList[ mid ] );

			// only lock sets that are in the vertex lock
			MString mSetName( materialNode->GetShadingNodeName() );
			bool bSetExists = false;
			for( uint i = 0; i < uint( this->vertexLockMaterials.size() ); ++i )
			{
				if( mSetName == this->vertexLockMaterials[ i ] )
				{
					bSetExists = true;
					break;
				}
			}

			if( !bSetExists )
			{
				continue;
			}

			std::string sMaterialId = this->mMaterialMappingIds[ mid ];

			// reset all vertices to not belonging to any material
			for( uint vid = 0; vid < vertexCount; ++vid )
			{
				sVertexSet[ vid ] = -1;
			}

			// get the arrays
			spRidArray sgMaterialIds = this->sgMeshData->GetMaterialIds();

			int sgMaterialIndex = 0;

			// find material index from guid
			for( uint k = 0; k < sgMaterialTable->GetMaterialsCount(); ++k )
			{
				if( sMaterialId == std::string( sgMaterialTable->GetMaterial( k )->GetMaterialGUID() ) )
				{
					sgMaterialIndex = k;
					break;
				}
			}

			// mark all vertices that belong to this material
			for( uint tid = 0; tid < triangleCount; ++tid )
			{
				if( sgMaterialIds->GetItem( tid ) != sgMaterialIndex )
					continue;

				// find the vertices and mark them to belong to this material
				for( uint e = 0; e < 3; ++e )
				{
					const rid vid = sgVertexIds->GetItem( tid * 3 + e );

					// set the material id
					sVertexSet[ vid ] = sMaterialId;
				}
			}

			// now, go through all triangles not belonging to the material, and lock any vertex
			// that does belong to the material
			for( uint tid = 0; tid < triangleCount; ++tid )
			{
				if( sgMaterialIds->GetItem( tid ) == sgMaterialIndex )
					continue;

				// find the vertices
				for( uint e = 0; e < 3; ++e )
				{
					const rid vid = sgVertexIds->GetItem( tid * 3 + e );
					if( sVertexSet[ vid ] == sMaterialId )
					{
						// mark the vertex as locked
						sgVertexLocks->SetItem( vid, true );
					}
				}
			}

			// done with this material
		}

		// delete temp arrays
		delete[] sVertexSet;
	}

	return MStatus::kSuccess;
}

static void
UnpackedRealArrayToPackedRealArray( const real* inRealArray, uint cornerCount, uint tupleSize, spRealArray& sgOutRealArray, spRidArray& sgOutRidArray )
{
	spRealArray sgUnpackedData = sg->CreateRealArray();
	sgUnpackedData->SetTupleSize( tupleSize );
	sgUnpackedData->SetTupleCount( cornerCount );
	sgUnpackedData->SetData( inRealArray, cornerCount * tupleSize );

	sgOutRidArray = sg->CreateRidArray();
	sgOutRealArray = spRealArray::SafeCast( sgUnpackedData->NewPackedCopy( sgOutRidArray ) );
}

MObject GetConnectedNamedPlug( const MFnDependencyNode& mDependencyNode, MString mPlugName )
{
	MStatus mStatus;

	MObject mNode = MObject::kNullObj;
	MPlug mNodePlug = mDependencyNode.findPlug( mPlugName, true, &mStatus );
	if( !mNodePlug.isNull() )
	{
		// find the shader node that is connected to the object set
		MPlugArray mConnectedPlugs;
		mNodePlug.connectedTo( mConnectedPlugs, true, false );
		if( mConnectedPlugs.length() > 0 )
		{
			// the shader node was found, use this as the
			mNode = mConnectedPlugs[ 0 ].node();
		}
	}
	return mNode;
}

MString MeshNode::GetUniqueMaterialName( MString mMaterialName )
{
	MString mNewMaterialName;
	int newMaterialIndex = 1;
	if( !this->cmd->DoNotGenerateMaterials() )
	{
		// check if original material name exists
		bool materialExists = false;
		MString mCommand = "objExists(";
		mCommand += "\"";
		mCommand += mMaterialName;
		mCommand += "\");";
		ExecuteCommand( mCommand, materialExists );

		// if it does not, return it
		if( !materialExists )
		{
			return mMaterialName;
		}

		// otherwise, generate a new
		// indexed material name.
		else
		{
			mCommand = "objExists(";
			mCommand += "\"";
			mCommand += mMaterialName;
			mCommand += newMaterialIndex;
			mCommand += "\");";
			materialExists = false;
			ExecuteCommand( mCommand, materialExists );
			while( materialExists )
			{
				++newMaterialIndex;
				mCommand = "objExists(";
				mCommand += "\"";
				mCommand += mMaterialName;
				mCommand += newMaterialIndex;
				mCommand += "\");";
				ExecuteCommand( mCommand, materialExists );
			}
		}
	}

	mNewMaterialName = mMaterialName;
	mNewMaterialName += newMaterialIndex;

	return mNewMaterialName;
}

class MaterialIndexToMayaMaterial
{
	public:
	MaterialIndexToMayaMaterial( MObject mMaterialObject )
	    : mObject( mMaterialObject )
	    , mShaderGroup( "" )
	    , bHasShaderGroup( 0 )
	{
	}

	MaterialIndexToMayaMaterial( MObject mMaterialObject, MString mMaterialShaderGroup )
	    : mObject( mMaterialObject )
	    , mShaderGroup( mMaterialShaderGroup )
	    , bHasShaderGroup( mMaterialShaderGroup.length() > 0 )
	{
	}

	~MaterialIndexToMayaMaterial() {}

	const MObject& GetMObject() { return this->mObject; }

	MString GetShaderGroup() { return this->mShaderGroup; }

	bool HasShaderGroup() { return this->bHasShaderGroup; }

	private:
	MObject mObject;
	MString mShaderGroup;
	bool bHasShaderGroup;
};

std::string GenerateFormattedName( const std::string& sFormatString, const std::string& sMeshName, const std::string& sSceneIndex )
{
	std::string sFormattedName = sFormatString;

	if( sFormattedName.length() > 0 )
	{
		const std::string meshString = "{MeshName}";
		const size_t meshStringLength = meshString.length();

		const std::string lodIndexString = "{LODIndex}";
		const size_t lodIndexStringLength = lodIndexString.length();

		bool bHasMeshName = true;
		while( bHasMeshName )
		{
			const size_t meshNamePosition = sFormattedName.find( meshString );
			bHasMeshName = meshNamePosition != std::string::npos;
			if( bHasMeshName )
			{
				sFormattedName = sFormattedName.replace(
				    sFormattedName.begin() + meshNamePosition, sFormattedName.begin() + meshNamePosition + meshStringLength, sMeshName );
			}
		}

		bool bHasLODIndex = true;
		while( bHasLODIndex )
		{
			const size_t lodIndexPosition = sFormattedName.find( lodIndexString );
			bHasLODIndex = lodIndexPosition != std::string::npos;
			if( bHasLODIndex )
			{
				sFormattedName = sFormattedName.replace(
				    sFormattedName.begin() + lodIndexPosition, sFormattedName.begin() + lodIndexPosition + lodIndexStringLength, sSceneIndex );
			}
		}
	}

	return sFormattedName;
}

std::string GenerateFormattedBlendShapeName( const std::string& sFormatString, const std::string& sMeshName, const std::string& sSceneIndex )
{
	std::string sFormattedName = sFormatString;

	if( sFormattedName.length() > 0 )
	{
		const std::string meshString = "{Name}";
		const size_t meshStringLength = meshString.length();

		const std::string lodIndexString = "{LODIndex}";
		const size_t lodIndexStringLength = lodIndexString.length();

		bool bHasMeshName = true;
		while( bHasMeshName )
		{
			const size_t meshNamePosition = sFormattedName.find( meshString );
			bHasMeshName = meshNamePosition != std::string::npos;
			if( bHasMeshName )
			{
				sFormattedName = sFormattedName.replace(
				    sFormattedName.begin() + meshNamePosition, sFormattedName.begin() + meshNamePosition + meshStringLength, sMeshName );
			}
		}

		bool bHasLODIndex = true;
		while( bHasLODIndex )
		{
			const size_t lodIndexPosition = sFormattedName.find( lodIndexString );
			bHasLODIndex = lodIndexPosition != std::string::npos;
			if( bHasLODIndex )
			{
				sFormattedName = sFormattedName.replace(
				    sFormattedName.begin() + lodIndexPosition, sFormattedName.begin() + lodIndexPosition + lodIndexStringLength, sSceneIndex );
			}
		}
	}

	return sFormattedName;
}

template <typename X, typename Y, typename Z>
MStatus AddAttribute( MFnDependencyNode& mModifiedDependencyNode, const char* cAttributeName, Y mAttributeType, Z value )
{
	MStatus mResult = MStatus::kSuccess;

	// if attribute exists, delete it
	MObject mExistingAttribute = mModifiedDependencyNode.attribute( cAttributeName, &mResult );
	if( mResult == MStatus::kSuccess )
	{
		mModifiedDependencyNode.removeAttribute( mExistingAttribute );
	}

	X mTypedAttribute;
	MObject mObject = mTypedAttribute.create( cAttributeName, cAttributeName, mAttributeType, value );
	mTypedAttribute.setStorable( true );
	mResult = mModifiedDependencyNode.addAttribute( mObject );

	return mResult;
}

MayaSgNodeMapping* MeshNode::GetInMemoryMeshMap( spSceneMesh sgMesh )
{
	Scene* sceneHandler = this->cmd->GetSceneHandler();
	if( !sceneHandler )
		return nullptr;

	else if( sgMesh.IsNull() )
		return nullptr;

	spString rNodeId = sgMesh->GetNodeGUID();
	return sceneHandler->GetMeshMap( rNodeId.c_str() );
}

void MergeTwoTrianglesIntoQuad( int tri1[ 3 ], int tri2[ 3 ], int quad[ 4 ], int originalCornerIndices[ 4 ] )
{
	int index0 = -1;
	{
		for( int t = 0; t < 3; ++t )
		{
			if( tri1[ 0 ] == tri2[ t ] )
			{
				index0 = t;
				break;
			}
		}
	}

	int index1 = -1;
	{
		for( int t = 0; t < 3; ++t )
		{
			if( tri1[ 1 ] == tri2[ t ] )
			{
				index1 = t;
				break;
			}
		}
	}

	int index2 = -1;
	{
		for( int t = 0; t < 3; ++t )
		{
			if( tri1[ 2 ] == tri2[ t ] )
			{
				index2 = t;
				break;
			}
		}
	}

	const bool bEdgeAMatch = index0 != -1 && index1 != -1;
	const bool bEdgeBMatch = index1 != -1 && index2 != -1;
	const bool bEdgeCMatch = index2 != -1 && index0 != -1;

	if( bEdgeCMatch )
	{
		quad[ 0 ] = tri2[ 2 ];
		quad[ 1 ] = tri1[ 0 ];
		quad[ 2 ] = tri1[ 1 ];
		quad[ 3 ] = tri1[ 2 ];

		originalCornerIndices[ 0 ] = 5;
		originalCornerIndices[ 1 ] = 0;
		originalCornerIndices[ 2 ] = 1;
		originalCornerIndices[ 3 ] = 2;
	}

	// TODO: implement if / when we stumble upon them!
	else if( bEdgeBMatch )
	{
		throw std::exception( "Could not generate quad from two triangles sharing edge B." );
	}
	else if( bEdgeAMatch )
	{
		throw std::exception( "Could not generate quad from two triangles sharing edge A." );
	}
	else
	{
		throw std::exception( "Could not generate quad from two triangles not sharing any edge." );
	}
}

MStatus MeshNode::WritebackGeometryData(
    spScene sgProcessedScene, size_t logicalLodIndex, spSceneMesh sgProcessedMesh, MaterialHandler* materialHandler, MDagPath& mResultPath )
{
	MStatus mStatus = MStatus::kSuccess;

	const bool bHasMeshMap = this->originalNode.isValid();
	const MayaSgNodeMapping* inMemoryMeshMap = this->GetInMemoryMeshMap( sgProcessedMesh );

	this->materialHandler = materialHandler;
	this->sgMeshData = sgProcessedMesh->GetGeometry();

	const uint vertexCount = this->sgMeshData->GetVertexCount();
	const uint triangleCount = this->sgMeshData->GetTriangleCount();
	const uint cornerCount = triangleCount * 3;
	
	spRidArray sgVertexIds = this->sgMeshData->GetVertexIds();
	spRealArray sgCoords = this->sgMeshData->GetCoords();

	// Create a new field data object, since we don't have a create function, we copy a pre-existing one and empty it
	spFieldData cornerFieldData = this->sgMeshData->GetCorners();
	spFieldData fieldDataBase;
	if( !cornerFieldData.IsNull() )
	{
		fieldDataBase = cornerFieldData->NewCopy( false );
		fieldDataBase->Clear();
		fieldDataBase->RemoveAllFields();
		spRealArray cornerCoords = spRealArray::SafeCast( fieldDataBase->AddBaseTypeField( EBaseTypes::TYPES_ID_REAL, 3, "CornerCoords" ) );
		cornerCoords->SetTupleCount( cornerCount );
		cornerCoords->IndexedCopy( sgCoords, sgVertexIds, 0 );
	}

	spString rProcessedMeshName = sgProcessedMesh->GetName();
	const char* cProcessedMeshName = rProcessedMeshName.c_str();

	if( triangleCount == 0 )
	{
		std::string sWarningMessage = "Zero triangle mesh detected when importing node: ";
		sWarningMessage += cProcessedMeshName;
		sWarningMessage += "!";

		MGlobal::displayWarning( sWarningMessage.c_str() );
		return MStatus::kSuccess;
	}

	MFloatPointArray mMeshVertices; // the vertices
	MIntArray mMeshPolygonsCount;   // the number of vertices per polygon (always 3 in our case)
	MIntArray mMeshTriangles;       // the vertices used by each triangle

	// copy vertices
	mMeshVertices.setLength( vertexCount );
	for( uint v = 0; v < vertexCount; ++v )
	{
		spRealData sgCoord = sgCoords->GetTuple( v );
		mMeshVertices.set( v, sgCoord[ 0 ], sgCoord[ 1 ], sgCoord[ 2 ] );
	}

	// copy triangle indices
	mMeshTriangles.setLength( triangleCount * 3 );
	mMeshPolygonsCount.setLength( triangleCount );

	uint cornerIndex = 0;
	for( uint tid = 0; tid < triangleCount; ++tid )
	{
		// copy the triangle
		for( uint c = 0; c < 3; ++c )
		{
			const rid vid = sgVertexIds->GetItem( cornerIndex );

			// add to triangle vertex list
			mMeshTriangles.set( vid, cornerIndex );
			cornerIndex++;
		}

		// add another triangle as polygon
		mMeshPolygonsCount.set( 3, tid );
	}

	this->modifiedTransform = this->MayaMesh.create( vertexCount, triangleCount, mMeshVertices, mMeshPolygonsCount, mMeshTriangles );

	// fetch and clear non-wanted uvs
	MStringArray mUVSetNames;
	const int uvCount = this->MayaMesh.numUVSets();
	if( uvCount > 0 )
	{
		mStatus = this->MayaMesh.getUVSetNames( mUVSetNames );

		MString mUVSetName = mUVSetNames[ 0 ];
		mStatus = this->MayaMesh.renameUVSet( mUVSetNames[ 0 ], MString( "reuse" ) );
	}

	// fetch all color sets
	MStringArray mColorSetNames;
	const int colorCount = this->MayaMesh.numColorSets();
	if( colorCount > 0 )
	{
		this->MayaMesh.getColorSetNames( mColorSetNames );
	}

	MString mMeshName = bHasMeshMap ? RemoveIllegalCharacters( this->originalNodeName ) : RemoveIllegalCharacters( MString( sgProcessedMesh->GetName() ) );
	std::string sFormattedMeshName = GenerateFormattedName( this->cmd->meshFormatString.asChar(), mMeshName.asChar(), std::to_string( logicalLodIndex ) );
	MString mFormattedMeshName = GetNonCollidingMeshName( MString( sFormattedMeshName.c_str() ) );

	MFnDagNode mModifiedDagNode( modifiedTransform );
	mFormattedMeshName = mModifiedDagNode.setName( mFormattedMeshName );

	// set the parent if there is a mesh mapping
	// copy the original transformation, if any
	if( bHasMeshMap )
	{
		MFnDagNode mOriginalDagNode( this->originalNode );
		for( uint p = 0; p < mOriginalDagNode.parentCount(); ++p )
		{
			MObject mParentObject = mOriginalDagNode.parent( 0 );
			MFnDagNode mParentDagNode( mParentObject );
			mParentDagNode.addChild( modifiedTransform );
		}

		MFnTransform mOriginalTransformation( this->originalNode.node() );
		MFnTransform mModifiedTransformation( modifiedTransform );
		mModifiedTransformation.set( mOriginalTransformation.transformation() );
	}

	else
	{
		this->postUpdate = true;
	}

	// setup the modified node handles
	mStatus = MDagPath::getAPathTo( modifiedTransform, this->modifiedNode );
	this->modifiedNodeShape = this->modifiedNode;

	mStatus = this->modifiedNodeShape.extendToShape();
	if( !mStatus )
	{
		std::string sErrorMessage = "Could not get shape when importing node: ";
		sErrorMessage += cProcessedMeshName;
		sErrorMessage += "!";

		MGlobal::displayError( sErrorMessage.c_str() );
		return mStatus;
	}

	// add the LOD info object
	MeshNodeLOD meshLOD;
	meshLOD.LODNode = this->modifiedNode;
	meshLOD.LODNodeShape = this->modifiedNodeShape;
	this->meshLODs.push_back( meshLOD );

	// setup the back mapping of the mesh
	this->SetupBackMapping();

	// setup materials
	spTextureTable sgTextureTable = sgProcessedScene->GetTextureTable();
	spMaterialTable sgMaterialTable = sgProcessedScene->GetMaterialTable();
	spRidArray sgMaterialIds = this->sgMeshData->GetMaterialIds();

	const bool bHasMaterialsInMaterialTable = sgMaterialTable.NonNull() ? sgMaterialTable->GetMaterialsCount() > 0 : false;
	bool bHasUnmappedMaterials = false;
	std::string sUnmappedMaterialTexCoordName = "";
	std::set<int> sgUniqueMaterialIndices;
	std::map<int, MaterialIndexToMayaMaterial*> sgUniqueMaterialMapping;

	if( !sgMaterialIds.IsNull() && bHasMaterialsInMaterialTable )
	{
		// go through each material index and store all unique
		for( uint tid = 0; tid < triangleCount; ++tid )
		{
			const int mid = sgMaterialIds->GetItem( tid );
			if( mid < 0 )
				continue;

			else if( mid >= (int)sgMaterialTable->GetMaterialsCount() )
			{
				std::string sErrorMessage = "Writeback of material(s) failed due to an out-of-range material id when importing node ";
				sErrorMessage += mMeshName.asChar();
				sErrorMessage += "!";

				MGlobal::displayError( sErrorMessage.c_str() );
				return MStatus::kFailure;
			}

			sgUniqueMaterialIndices.insert( mid );
		}

		for( std::set<int>::const_iterator& mIterator = sgUniqueMaterialIndices.begin(); mIterator != sgUniqueMaterialIndices.end(); mIterator++ )
		{
			int mid = *mIterator;

			spMaterial sgMaterial = sgMaterialTable->GetMaterial( mid );
			spString rMaterialName = sgMaterial->GetName();
			spString rMaterialId = sgMaterial->GetMaterialGUID();

			std::string n = rMaterialName.c_str();

			// is this a new material?
			if( !this->cmd->mapMaterials )
			{
				bHasUnmappedMaterials = true;
				sgUniqueMaterialMapping.insert( std::pair<int, MaterialIndexToMayaMaterial*>( mid, new MaterialIndexToMayaMaterial( MObject::kNullObj ) ) );
			}
			else
			{
				std::map<std::string, std::string>::const_iterator& gGuidToMaterialMap = this->cmd->s_GlobalMaterialGuidToDagPath.find( rMaterialName.c_str() );

				std::map<std::string, StandardMaterial*>::const_iterator& guidToMaterialIterator =
				    this->materialHandler->MaterialIdToStandardMaterial.find( rMaterialId.c_str() );

				const bool bHasGuidMap = gGuidToMaterialMap != this->cmd->s_GlobalMaterialGuidToDagPath.end();

				if( guidToMaterialIterator != this->materialHandler->MaterialIdToStandardMaterial.end() )
				{
					bHasUnmappedMaterials = true;
					sgUniqueMaterialMapping.insert( std::pair<int, MaterialIndexToMayaMaterial*>( mid, new MaterialIndexToMayaMaterial( MObject::kNullObj ) ) );
				}
				else if( !bHasGuidMap )
				{
					MObject mMaterialObject = MObject::kNullObj;
					if( GetMObjectOfNamedObject( rMaterialName.c_str(), mMaterialObject ) && this->cmd->extractionType != BATCH_PROCESSOR )
					{
						sgUniqueMaterialMapping.insert(
						    std::pair<int, MaterialIndexToMayaMaterial*>( mid, new MaterialIndexToMayaMaterial( mMaterialObject ) ) );
					}
					else
					{
						bHasUnmappedMaterials = true;
						sgUniqueMaterialMapping.insert(
						    std::pair<int, MaterialIndexToMayaMaterial*>( mid, new MaterialIndexToMayaMaterial( MObject::kNullObj ) ) );
					}
				}
				else
				{
					MObject mMaterialObject = MObject::kNullObj;
					MString mMappedMaterialName = gGuidToMaterialMap->first.c_str();
					MString mMappedShaderGroupName = gGuidToMaterialMap->second.c_str();

					if( GetMObjectOfNamedObject( mMappedMaterialName, mMaterialObject ) )
					{
						sgUniqueMaterialMapping.insert(
						    std::pair<int, MaterialIndexToMayaMaterial*>( mid, new MaterialIndexToMayaMaterial( mMaterialObject, mMappedShaderGroupName ) ) );
					}
					else
					{
						bHasUnmappedMaterials = true;
						sgUniqueMaterialMapping.insert(
						    std::pair<int, MaterialIndexToMayaMaterial*>( mid, new MaterialIndexToMayaMaterial( MObject::kNullObj ) ) );
					}
				}
			}

			// loop through all material channels to create a uv-to-texture map
			const uint channelCount = sgMaterial->GetMaterialChannelCount();
			for( uint c = 0; c < channelCount; ++c )
			{
				spString rChannelName = sgMaterial->GetMaterialChannelFromIndex( c );
				const char* cChannelName = rChannelName;

				spShadingNode sgExitNode = sgMaterial->GetShadingNetwork( cChannelName );
				if( sgExitNode.IsNull() )
					continue;

				// fetch all textures for this material channel
				std::map<std::basic_string<TCHAR>, spShadingTextureNode> texNodeMap;
				this->materialHandler->FindAllUpStreamTextureNodes( sgExitNode, texNodeMap );

				// fetch texture id and uv for each texture node
				for( std::map<std::basic_string<TCHAR>, spShadingTextureNode>::const_iterator& texIterator = texNodeMap.begin();
				     texIterator != texNodeMap.end();
				     texIterator++ )
				{
					spString rTexCoordName = texIterator->second->GetTexCoordName();
					if( rTexCoordName.IsNullOrEmpty() )
						continue;

					const char* cTexCoordName = rTexCoordName;
					sUnmappedMaterialTexCoordName = rTexCoordName;
					break;
				}
			}
		}
	}

	// setup all UVs on the mesh, name them correctly
	for( uint uvSetIndex = 0; uvSetIndex < SG_NUM_SUPPORTED_TEXTURE_CHANNELS; ++uvSetIndex )
	{
		spRealArray sgTexCoords = this->sgMeshData->GetTexCoords( uvSetIndex );

		if( sgTexCoords.IsNull() || sgTexCoords->GetItemCount() == 0 )
			continue;

		spString sgTexCoordName = sgTexCoords->GetAlternativeName();
		const char* cUVNameBuffer = sgTexCoordName.c_str() ? sgTexCoordName.c_str() : "(null)";

		MIntArray mMeshTrianglesUV; // the uv-coordinates used by each triangle
		MFloatArray mMeshUArray;    // the u-coords
		MFloatArray mMeshVArray;    // the v-coords

		// make an indexed, packed copy based on both the UV and 3d Coord, to avoid referencing the same UVs if the coords are not the same
		spRidArray sgIndices = sg->CreateRidArray();
		auto fieldDataPerUV = fieldDataBase->NewCopy( true );
		fieldDataPerUV->AddField( sgTexCoords );
		auto fieldDataPerUVPackedCopy = fieldDataPerUV->NewPackedCopy( sgIndices );
		spRealArray sgIndicedTexCoords = spRealArray::SafeCast( fieldDataPerUVPackedCopy->GetField( sgTexCoords->GetName() ) );

		if( !sgIndicedTexCoords.IsNull() )
		{
			const uint tupleCount = sgIndicedTexCoords->GetTupleCount();
			mMeshUArray.setLength( tupleCount );
			mMeshVArray.setLength( tupleCount );

			for( uint i = 0; i < tupleCount; ++i )
			{
				// get the uvs
				spRealData sgIndicedTexCoord = sgIndicedTexCoords->GetTuple( i );

				// set the uvs
				mMeshUArray.set( sgIndicedTexCoord[ 0 ], i );
				mMeshVArray.set( sgIndicedTexCoord[ 1 ], i );
			}

			mMeshTrianglesUV.setLength( triangleCount * 3 );
			for( uint tid = 0; tid < triangleCount; ++tid )
			{
				for( uint c = 0; c < 3; ++c )
				{
					const int cid = tid * 3 + c;

					// set the uv index of the triangle
					mMeshTrianglesUV.set( sgIndices->GetItem( cid ), cid );
				}
			}
		}

		MString mUVSet;

		// make an extra copy of correct type to avoid in-loop casts
		mStatus = mUVSetNames.clear();
		mStatus = this->MayaMesh.getUVSetNames( mUVSetNames );

		MString mUVNameBuffer = cUVNameBuffer;
		for( uint uvIndex = 0; uvIndex < mUVSetNames.length(); ++uvIndex )
		{
			if( mUVSetNames[ uvIndex ] == mUVNameBuffer )
			{
				mUVSet = cUVNameBuffer;
			}
		}

		if( mUVSet.length() == 0 )
		{
			mStatus = TryReuseDefaultUV( this->MayaMesh, mUVNameBuffer );
			if( mStatus )
			{
				mUVSet = mUVNameBuffer;
			}
			else
			{
				mUVSet = this->MayaMesh.createUVSetWithName( mUVNameBuffer );
			}
		}

		mStatus = this->MayaMesh.setUVs( mMeshUArray, mMeshVArray, &mUVSet );
		mStatus = this->MayaMesh.assignUVs( mMeshPolygonsCount, mMeshTrianglesUV, &mUVSet );
	}

	// setup all Colors on the mesh, name them correctly
	for( uint colorSetIndex = 0; colorSetIndex < SG_NUM_SUPPORTED_COLOR_CHANNELS; ++colorSetIndex )
	{
		spRealArray sgVertexColors = this->sgMeshData->GetColors( colorSetIndex );

		if( sgVertexColors.IsNull() || sgVertexColors->GetItemCount() == 0 )
			continue;

		spString sgColorName = sgVertexColors->GetAlternativeName();
		const char* cVertexColorNameBuffer = sgColorName.c_str() ? sgColorName.c_str() : "(null)";

		// make an indexed, packed copy
		spRidArray sgIndices = sg->CreateRidArray();
		spRealArray sgIndicedColors = spRealArray::SafeCast( sgVertexColors->NewPackedCopy( sgIndices ) );
		const uint tupleCount = sgIndicedColors->GetTupleCount();

		MIntArray mColorIndices( cornerCount ); // the uv-coordinates used by each triangle
		MColorArray mColorsArray( tupleCount );

		for( uint i = 0; i < tupleCount; i++ )
		{
			spRealData sgColor = sgIndicedColors->GetTuple( i );

			// get the colors
			mColorsArray[ i ] = MColor( sgColor[ 0 ], sgColor[ 1 ], sgColor[ 2 ], sgColor[ 3 ] );
		}

		for( uint t = 0; t < triangleCount; ++t )
		{
			for( uint v = 0; v < 3; ++v )
			{
				const int cid = t * 3 + v;
				const rid vid = sgIndices->GetItem( cid );

				// set the color index of the triangle
				mColorIndices[ cid ] = vid;
			}
		}

		MString mColorSetName = MString( cVertexColorNameBuffer );
		MString mTmpColorSetName = this->MayaMesh.createColorSetWithName( mColorSetName );

		if( mTmpColorSetName != mColorSetName )
		{
			// delete the old set
			this->MayaMesh.deleteColorSet( mColorSetName );

			// create a new set
			MString mNewColorSetName = this->MayaMesh.createColorSetWithName( mColorSetName );

			// delete the previous set as well (we can't rename it)
			this->MayaMesh.deleteColorSet( mTmpColorSetName );
			mTmpColorSetName = mNewColorSetName;
		}

		mStatus = this->MayaMesh.setCurrentColorSetName( mTmpColorSetName );
		mStatus = this->MayaMesh.setColors( mColorsArray, &mTmpColorSetName );
		mStatus = this->MayaMesh.assignColors( mColorIndices, &mTmpColorSetName );
	}

	// if all materials are known and we have a mesh map
	// try to use currently set uv- and color-set.
	if( bHasMeshMap && !bHasUnmappedMaterials )
	{
		MFnMesh mOriginalMesh;
		mOriginalMesh.setObject( this->originalNode );
		mOriginalMesh.syncObject();

		MString mOriginalUvSetName;
		mStatus = mOriginalMesh.getCurrentUVSetName( mOriginalUvSetName );
		if( mStatus )
		{
			mStatus = this->MayaMesh.setCurrentUVSetName( mOriginalUvSetName );
		}

		MString mOriginalColorSetName;
		mStatus = mOriginalMesh.getCurrentColorSetName( mOriginalColorSetName );
		if( mStatus )
		{
			mStatus = this->MayaMesh.setCurrentUVSetName( mOriginalColorSetName );
		}
	}
	else
	{
		mStatus = this->MayaMesh.setCurrentUVSetName( sUnmappedMaterialTexCoordName.c_str() );
	}

	// apply normals, if any
	if( !this->sgMeshData->GetNormals().IsNull() )
	{
		MCheckStatus( this->WritebackNormals(), "Could not write normals and smoothing to mesh." );
	}

	this->MayaMesh.updateSurface();

	// apply crease data
	mStatus = this->AddCreaseData();
	if( !mStatus )
	{
		std::string sErrorMessage = "Failed to map crease data when importing node: ";
		sErrorMessage += cProcessedMeshName;
		sErrorMessage += "!";

		MGlobal::displayError( sErrorMessage.c_str() );
		return mStatus;
	}

	this->MayaMesh.updateSurface();
	this->MayaMesh.syncObject();

	// if we have a mesh map,
	// copy as many properties from original as we can
	if( bHasMeshMap )
	{
		// copy attribute data
		MString mCommand = "SimplygonMaya_copyAttributes( \"";
		mCommand += this->originalNode.fullPathName();
		mCommand += "\" , \"";
		mCommand += this->modifiedNode.fullPathName();
		mCommand += "\");";

		mStatus = ::ExecuteCommand( mCommand );
		if( mStatus != MStatus::kSuccess )
		{
			std::string sErrorMessage = "Failed to map attributes when importing node: ";
			sErrorMessage += cProcessedMeshName;
			sErrorMessage += "!";

			MGlobal::displayError( sErrorMessage.c_str() );
			return mStatus;
		}

		// copy vertex and triangle blind data
		if( inMemoryMeshMap )
		{
			BlindData& inMemoryBlindData = inMemoryMeshMap->mayaNode->blindData;
			inMemoryBlindData.ApplyBlindDataToMesh( this->MayaMesh, this->VertexBackMapping, this->PolygonBackMapping );
		}

		// copy object level blind data
		mCommand = "SimplygonMaya_copyObjectLevelBlindData( \"";
		mCommand += this->originalNodeShape.fullPathName();
		mCommand += "\" , \"";
		mCommand += this->modifiedNodeShape.fullPathName();
		mCommand += "\");";

		mStatus = ::ExecuteCommand( mCommand );
		if( mStatus != MStatus::kSuccess )
		{
			std::string sErrorMessage = "Failed to map object level blind-data when importing node: ";
			sErrorMessage += cProcessedMeshName;
			sErrorMessage += "!";

			MGlobal::displayError( sErrorMessage.c_str() );
			return mStatus;
		}
	}

	if( bHasMaterialsInMaterialTable )
	{
		// setup material
		std::vector<int>* faceMaterialIds = new std::vector<int>();
		faceMaterialIds->resize( triangleCount );

		int currentMaterialIndex = 0;
		for( std::set<int>::const_iterator& mIterator = sgUniqueMaterialIndices.begin(); mIterator != sgUniqueMaterialIndices.end(); mIterator++ )
		{
			const int mid = *mIterator;

			spMaterial sgMaterial = sgMaterialTable->GetMaterial( mid );
			std::string sMaterialId = sgMaterial->GetMaterialGUID();

			spString rMaterialName = sgMaterial->GetName();
			const char* cMaterialName = rMaterialName.c_str();
			const bool bHasMaterialName = cMaterialName ? strlen( cMaterialName ) > 0 : false;

			MString mShadingGroupName = "";

			StandardMaterial* existingStandardMaterial = nullptr;
			StandardMaterial* standardMaterial = nullptr;

			const std::map<int, MaterialIndexToMayaMaterial*>::const_iterator& materialIndexToMObject = sgUniqueMaterialMapping.find( mid );

			// if mapping exists, reuse original
			if( materialIndexToMObject != sgUniqueMaterialMapping.end() && materialIndexToMObject->second->GetMObject() != MObject::kNullObj )
			{
				MaterialIndexToMayaMaterial* materialMap = materialIndexToMObject->second;

				// if direct mapping found, use it
				if( materialMap->HasShaderGroup() )
				{
					mShadingGroupName = materialMap->GetShaderGroup();
				}

				// otherwise, resolve shader group based on material name
				else
				{
					MObject mMaterialObject = materialMap->GetMObject();

					MFnDependencyNode mShaderGroupDependencyNode( mMaterialObject );
					std::basic_string<TCHAR> tMaterialName( mShaderGroupDependencyNode.name().asChar() );

					MPlugArray mMaterialPlugs;
					mStatus = mShaderGroupDependencyNode.getConnections( mMaterialPlugs );

					bool bNotFound = true;
					for( uint materialPlugIndex = 0; materialPlugIndex < mMaterialPlugs.length(); ++materialPlugIndex )
					{
						MPlug mMaterialPlug = mMaterialPlugs[ materialPlugIndex ];
						std::string sPlugName = mMaterialPlug.name().asChar();

						MPlugArray mConnectionPlugs;

						// get output plugs
						mMaterialPlug.connectedTo( mConnectionPlugs, false, true );

						for( uint connectionPlugIndex = 0; connectionPlugIndex < mConnectionPlugs.length(); ++connectionPlugIndex )
						{
							std::string sConnectionPlugName = mConnectionPlugs[ connectionPlugIndex ].name().asChar();

							MObject mPlugMaterialObject = mConnectionPlugs[ connectionPlugIndex ].node();
							const MFn::Type mConnectionPlugType = mPlugMaterialObject.apiType();
							if( mConnectionPlugType != MFn::kShadingEngine )
								continue;

							// store reference
							MFnDependencyNode mPlugDependencyNode( mPlugMaterialObject );
							mShadingGroupName = mPlugDependencyNode.name( &mStatus ).asChar();

							// 						MObject mShaderGroup = GetConnectedNamedPlug(mPlugDependencyNode, "surfaceShader");
							// 						MFnDependencyNode mShaderGroupDependencyNode(mShaderGroup);
							// 						MString mMaterialName(mShaderGroupDependencyNode.name().asChar());

							bNotFound = false;
							break;
						}

						if( !bNotFound )
						{
							break;
						}
					}
				}
			}

			// else, create a new material for the specific material id
			else if( bHasMaterialName )
			{
				MString mStandardMaterialName = GetUniqueMaterialName( cMaterialName );

				standardMaterial = new StandardMaterial( this->cmd, sgTextureTable );
				standardMaterial->NodeName = mStandardMaterialName;
				standardMaterial->sgMaterial = sgMaterial;

				spString rSgMaterialId = standardMaterial->sgMaterial->GetMaterialGUID();
				const char* cSgMaterialId = rSgMaterialId;

				const std::map<std::string, StandardMaterial*>::const_iterator& guidToMaterialIterator =
				    this->materialHandler->MaterialIdToStandardMaterial.find( sMaterialId );

				// has this material been handled before?
				if( guidToMaterialIterator != this->materialHandler->MaterialIdToStandardMaterial.end() )
				{
					// reuse previously handled material
					existingStandardMaterial = guidToMaterialIterator->second;

					if( existingStandardMaterial )
					{
						// store shading group name for material assignment
						mShadingGroupName = existingStandardMaterial->ShaderGroupName;
					}
				}
				else
				{
					// material doesn't exist, create new material
					mStatus = standardMaterial->CreatePhong( this->modifiedNodeShape, mFormattedMeshName, mStandardMaterialName, true );
					if( !mStatus )
					{
						return mStatus;
					}

					if( !this->cmd->DoNotGenerateMaterials() && this->cmd->extractionType != BATCH_PROCESSOR )
					{
						std::string sWarningMessage = "StandardMaterial::CreatePhong - Generating unmapped material: ";
						sWarningMessage += std::string( mStandardMaterialName.asChar() ) + " (";
						sWarningMessage += std::string( standardMaterial->ShaderGroupName.asChar() ) + ").";

						MGlobal::displayWarning( sWarningMessage.c_str() );
					}

					// add to mapping, in case id shows up later
					this->materialHandler->MaterialIdToStandardMaterial.insert( std::pair<std::string, StandardMaterial*>( cSgMaterialId, standardMaterial ) );

					// store shading group name for material assignment
					mShadingGroupName = standardMaterial->ShaderGroupName;
				}
			}

			MIntArray* mMayaMaterialIds = new MIntArray();

			// find and append triangles with the current material id
			for( uint tid = 0; tid < triangleCount; ++tid )
			{
				const int sgMaterialIndex = sgMaterialIds->GetItem( tid );
				if( sgMaterialIndex == mid )
				{
					mMayaMaterialIds->append( tid );
					faceMaterialIds->at( tid ) = (int)currentMaterialIndex;
				}
			}

			// setup the component set
			MFnSingleIndexedComponent mFaceIndices;
			MObject mFaces = mFaceIndices.create( MFn::kMeshPolygonComponent );
			if( !mFaceIndices.addElements( *mMayaMaterialIds ) )
			{
				std::string sErrorMessage = "Failed to map material ids when importing node: ";
				sErrorMessage += cProcessedMeshName;
				sErrorMessage += "!";

				MGlobal::displayError( sErrorMessage.c_str() );
				return MStatus::kFailure;
			}

			delete mMayaMaterialIds;

			// apply material (named material is required)
			if( bHasMaterialName && !this->cmd->DoNotGenerateMaterials() )
			{
				MString mCommand = "sets -e -forceElement " + mShadingGroupName;
				::ExecuteSelectedObjectCommand( mCommand, this->modifiedNode, mFaces );
			}

			const bool bReusingOriginalMaterial = !standardMaterial && !existingStandardMaterial;

			// if new material, extract mapping for alter use
			if( standardMaterial )
			{
				standardMaterial->ExtractMapping( modifiedNodeShape );
			}

			// if reusing created material, copy uv-linking
			if( existingStandardMaterial )
			{
				existingStandardMaterial->ImportMapping( modifiedNodeShape );
				this->cmd->GetMaterialInfoHandler()->AddReuse( mFormattedMeshName.asChar(), existingStandardMaterial->ShaderGroupName.asChar() );
			}

			// if reusing original material
			if( bReusingOriginalMaterial )
			{
				this->cmd->GetMaterialInfoHandler()->AddReuse( mFormattedMeshName.asChar(), mShadingGroupName.asChar() );
			}

			currentMaterialIndex++;
		}

		// clear material mapping
		sgUniqueMaterialMapping.erase( sgUniqueMaterialMapping.begin(), sgUniqueMaterialMapping.end() );

		// add face material ids for later use
		this->cmd->GetMaterialInfoHandler()->AddMaterialIds( mFormattedMeshName.asChar(), *faceMaterialIds );

		// done with the sets
		delete faceMaterialIds;
	}
	else
	{
		this->cmd->GetMaterialInfoHandler()->Add( mFormattedMeshName.asChar() );
	}

	// add to all generic sets
	mStatus = this->AddToGenericSets();
	if( !mStatus )
	{
		std::string sErrorMessage = "Failed to map mesh data to generic sets when importing node: ";
		sErrorMessage += cProcessedMeshName;
		sErrorMessage += "!";

		MGlobal::displayError( sErrorMessage.c_str() );
		return mStatus;
	}

	// copy the uv linkage from the original node
	if( bHasMeshMap && this->cmd->mapMaterials && !bHasUnmappedMaterials )
	{
		MString mCommand = "SimplygonMaya_copyUVSetLinks(\"" + this->originalNodeShape.fullPathName() + "\");";
		mStatus = ::ExecuteSelectedObjectCommand( mCommand, this->modifiedNodeShape, MObject::kNullObj );
	}

	// try to find stored original meshdata from extraction
	if( inMemoryMeshMap )
	{
		const std::vector<BlendShape>& inMemoryBlendShapes = inMemoryMeshMap->mayaNode->blendShape;
		for( uint b = 0; b < inMemoryBlendShapes.size(); ++b )
		{
			const BlendShape& theBlendShape = inMemoryBlendShapes[ b ];

			std::vector<MString> mDeleteTargetQueue;

			MString mBaseObjectName = mFormattedMeshName;

			// create mel command to be able to find the base and targets
			MString mMelConnectObjectsCommand;

			MString mBlendShapeName =
			    GenerateFormattedBlendShapeName( this->cmd->blendshapeFormatString.asChar(), theBlendShape.name.asChar(), std::to_string( logicalLodIndex ) )
			        .c_str();

			// create the blendShape
			mMelConnectObjectsCommand = MString( "blendShape -n " ) + mBlendShapeName + " " + mBaseObjectName;
			ExecuteCommand( mMelConnectObjectsCommand );

			mMelConnectObjectsCommand = "blendShape -edit ";
			for( uint f = 0; f < theBlendShape.BlendWeights.size(); ++f )
			{
				const BlendWeight& bw = theBlendShape.BlendWeights[ f ];
				spRealArray sgTargetCoords = spRealArray::SafeCast( this->sgMeshData->GetUserVertexField( bw.fieldName.asChar() ) );

				const bool bHasBlendShapeData = !sgTargetCoords.IsNull();
				if( bHasBlendShapeData )
				{
					// set up a vertex array
					MFloatPointArray mTargetBlendShapeVertexField;
					mTargetBlendShapeVertexField.setLength( vertexCount );

					for( uint vid = 0; vid < vertexCount; ++vid )
					{
						spRealData sgTargetCoord = sgTargetCoords->GetTuple( vid );
						spRealData sgCoordinate = sgCoords->GetTuple( vid );

						// the field is relative, add the vertex coord to it
						mTargetBlendShapeVertexField[ vid ] = MFloatPoint(
						    sgTargetCoord[ 0 ] + sgCoordinate[ 0 ], sgTargetCoord[ 1 ] + sgCoordinate[ 1 ], sgTargetCoord[ 2 ] + sgCoordinate[ 2 ] );
					}

					// create the target mesh
					MStatus mResult;
					MFnMesh mTargetMesh;

					MObject mTargetTransform = mTargetMesh.create(
					    vertexCount, triangleCount, mTargetBlendShapeVertexField, mMeshPolygonsCount, mMeshTriangles, MObject::kNullObj, &mResult );

					// set target name
					MString mTargetObjectName;

					if( this->cmd->SkipBlendShapeWeightPostfix() )
					{
						mTargetObjectName = bw.weightName;
					}
					else
					{
						mTargetObjectName = GenerateFormattedBlendShapeName(
						                        this->cmd->blendshapeFormatString.asChar(), bw.weightName.asChar(), std::to_string( logicalLodIndex ) )
						                        .c_str();
					}

					mDeleteTargetQueue.push_back( mTargetObjectName );

					// set the name of the target mesh
					MFnDagNode mTargetDagNode( mTargetTransform );
					mTargetObjectName = mTargetDagNode.setName( mTargetObjectName );

					MDagPath mTargetDagPath;
					mStatus = MDagPath::getAPathTo( mTargetTransform, mTargetDagPath );

					mTargetDagPath.extendToShape();

					// add target and weight on the specified index
					mMelConnectObjectsCommand += MString( " -t " ) + mBaseObjectName + " " + ( bw.realIndex ) + " " + mTargetObjectName + " " +
					                             theBlendShape.envelope + " -w " + bw.realIndex + " " + bw.weight + " ";
				}
			}

			mMelConnectObjectsCommand += " " + mBlendShapeName;
			const MStatus mCommandResult = ExecuteCommand( mMelConnectObjectsCommand );

			for( size_t e = 0; e < mDeleteTargetQueue.size(); ++e )
			{
				ExecuteCommand( MString( "delete " ) + mDeleteTargetQueue[ e ] );
			}

			mDeleteTargetQueue.clear();
		}
	}

	// setup the skinning cluster
	mStatus = this->AddSkinning( sgProcessedScene );
	if( !mStatus )
	{
		std::string sErrorMessage = "Failed to map skinning data when importing node: ";
		sErrorMessage += cProcessedMeshName;
		sErrorMessage += "!";

		MGlobal::displayError( sErrorMessage.c_str() );
		return mStatus;
	}

	// set the current node as result
	mResultPath = meshLOD.LODNode;

	// fetch dependency node so that we can write custom attributes
	// such as scene radius, lod index etc.
	MFnDependencyNode mModifiedDependencyNode( this->modifiedNode.node() );

	// max deviation
	if( true )
	{
		const char* cAttributeName = "MaxDeviation";
		spRealArray sgMaxDeviation = spRealArray::SafeCast( sgProcessedScene->GetCustomField( cAttributeName ) );
		if( !sgMaxDeviation.IsNull() )
		{
			const real maxDev = sgMaxDeviation->GetItem( 0 );
			AddAttribute<MFnNumericAttribute, MFnNumericData::Type, float>( mModifiedDependencyNode, cAttributeName, MFnNumericData::Type::kFloat, maxDev );
		}
	}

	// scene radius
	if( true )
	{
		const char* cAttributeName = "SceneRadius";
		const real sceneRadius = sgProcessedScene->GetRadius();
		AddAttribute<MFnNumericAttribute, MFnNumericData::Type, float>( mModifiedDependencyNode, cAttributeName, MFnNumericData::Type::kFloat, sceneRadius );
	}

	// scene meshes radius
	if( true )
	{
		const char* cAttributeName = "SceneMeshesRadius";
		const real sceneMeshesRadius = GetSceneMeshesRadius( sgProcessedScene );
		AddAttribute<MFnNumericAttribute, MFnNumericData::Type, float>(
		    mModifiedDependencyNode, cAttributeName, MFnNumericData::Type::kFloat, sceneMeshesRadius );
	}

	// processed meshes radius
	if( true )
	{
		const char* cAttributeName = "ProcessedMeshesRadius";
		auto sgProcessedMeshesExtents = sgProcessedScene->GetCustomFieldProcessedMeshesExtents();
		if( !sgProcessedMeshesExtents.IsNull() )
		{
			const real processedMeshesRadius = sgProcessedMeshesExtents->GetBoundingSphereRadius();
			AddAttribute<MFnNumericAttribute, MFnNumericData::Type, float>(
			    mModifiedDependencyNode, cAttributeName, MFnNumericData::Type::kFloat, processedMeshesRadius );
		}
	}

	// lod index
	if( true )
	{
		const char* cAttributeName = "LODIndex";
		AddAttribute<MFnNumericAttribute, MFnNumericData::Type, int>(
		    mModifiedDependencyNode, cAttributeName, MFnNumericData::Type::kInt, (int)logicalLodIndex );
	}

	// original node name
	if( true )
	{
		const char* cAttributeName = "OriginalNodeName";
		spString rMeshName = sgProcessedMesh->GetName();
		MString mOriginalNodeName = rMeshName.c_str();

		MFnStringData mStringData;
		MObject mStringObject = mStringData.create( mOriginalNodeName );
		AddAttribute<MFnTypedAttribute, MFnData::Type, MObject>( mModifiedDependencyNode, cAttributeName, MFnData::kString, mStringObject );
	}

	// intended node name
	if( true )
	{
		const char* cAttributeName = "IntendedNodeName";
		MString mIntendedNodeName = sFormattedMeshName.c_str();

		MFnStringData mStringData;
		MObject mStringObject = mStringData.create( mIntendedNodeName );
		AddAttribute<MFnTypedAttribute, MFnData::Type, MObject>( mModifiedDependencyNode, cAttributeName, MFnData::kString, mStringObject );
	}

	// imported node name
	if( true )
	{
		const char* cAttributeName = "ImportedNodeName";
		MString mImportedNodeName = mFormattedMeshName;

		MFnStringData mStringData;
		MObject mStringObject = mStringData.create( mImportedNodeName );
		AddAttribute<MFnTypedAttribute, MFnData::Type, MObject>( mModifiedDependencyNode, cAttributeName, MFnData::kString, mStringObject );
	}

	// transfer original UUID as new attribute
	if( this->originalNode.isValid() )
	{
		const char* cAttributeName = "OriginalUUID";

		MFnDependencyNode mOriginalDependencyNode( this->originalNode.node() );
		MUuid mUUID = mOriginalDependencyNode.uuid( &mStatus );

		if( mStatus == MStatus::kSuccess )
		{
			MFnStringData mStringData;
			MObject mStringObject = mStringData.create( mUUID.asString() );
			AddAttribute<MFnTypedAttribute, MFnData::Type, MObject>( mModifiedDependencyNode, cAttributeName, MFnData::kString, mStringObject );
		}
	}

	return MStatus::kSuccess;
}

bool CalculateNumPolygonsAndVertexIds( spCharArray sgQuadFlags, uint& numPolygons, uint& numVertexIds )
{
	bool bHasInvalidQuadFlags = false;
	numPolygons = 0;
	numVertexIds = 0;

	const uint numQuadFlags = sgQuadFlags.GetItemCount();
	for( uint tid = 0; tid < numQuadFlags; ++tid )
	{
		const char cQuadFlag = sgQuadFlags->GetItem( tid );
		if( cQuadFlag == SG_QUADFLAG_TRIANGLE )
		{
			numPolygons++;
			numVertexIds += 3;
		}
		else if( cQuadFlag == SG_QUADFLAG_FIRST )
		{
			// check next quad link, if any
			if( tid < numQuadFlags - 1 )
			{
				if( sgQuadFlags->GetItem( tid + 1 ) != SG_QUADFLAG_SECOND )
				{
					bHasInvalidQuadFlags = true;
				}
			}
			// if no next, mark as invalid
			else
			{
				bHasInvalidQuadFlags = true;
			}

			numPolygons++;
			numVertexIds += 4;
		}
		else if( cQuadFlag == SG_QUADFLAG_SECOND )
		{
			// passthrough
		}
		else
		{
			bHasInvalidQuadFlags = true;
			break;
		}
	}

	return bHasInvalidQuadFlags;
}

void SetQuadFlagsToTriangles( spCharArray sgQuadFlags )
{
	for( uint quadFlagIndex = 0; quadFlagIndex < sgQuadFlags.GetItemCount(); ++quadFlagIndex )
	{
		sgQuadFlags.SetItem( quadFlagIndex, SG_QUADFLAG_TRIANGLE );
	}
}

MStatus MeshNode::WritebackGeometryData_Quad(
    spScene sgProcessedScene, size_t logicalLodIndex, spSceneMesh sgProcessedMesh, MaterialHandler* materialHandler, MDagPath& mResultPath )
{
	MStatus mStatus = MStatus::kSuccess;

	const bool bHasMeshMap = this->originalNode.isValid();
	const MayaSgNodeMapping* inMemoryMeshMap = this->GetInMemoryMeshMap( sgProcessedMesh );

	this->materialHandler = materialHandler;
	this->sgMeshData = sgProcessedMesh->GetGeometry();

	const uint vertexCount = this->sgMeshData->GetVertexCount();
	const bool bHasPolygons = this->sgMeshData->GetTriangleCount() > 0;
	const uint triangleCount = this->sgMeshData->GetTriangleCount();

	spString rProcessedMeshName = sgProcessedMesh->GetName();
	const char* cProcessedMeshName = rProcessedMeshName.c_str();

	if( !bHasPolygons )
	{
		std::string sWarningMessage = "Zero triangle mesh detected when importing node ";
		sWarningMessage += cProcessedMeshName;
		sWarningMessage += "!";

		MGlobal::displayWarning( sWarningMessage.c_str() );
		return MStatus::kSuccess;
	}

	MFloatPointArray mVertexPositions; // the vertices
	MIntArray mVertexCountPerPolygon;  // the number of vertices per polygon
	MIntArray mVertexIds;              // the vertices used by each triangle

	spRidArray sgVertexIds = this->sgMeshData->GetVertexIds();
	spRealArray sgCoords = this->sgMeshData->GetCoords();

	// quad flags
	spCharArray sgQuadFlags = this->sgMeshData->GetQuadFlags();
	if( sgQuadFlags.IsNull() )
	{
		// if no quad flags; generate quad flags as import depends on the field,
		// mark all entries in field as SG_QUADFLAG_TRIANGLE
		this->sgMeshData->AddQuadFlags();
		sgQuadFlags = this->sgMeshData->GetQuadFlags();

		SetQuadFlagsToTriangles( sgQuadFlags );

		// output a warning to the user
		std::string sWarningMessage = "QuadFlags not detected in geometry (";
		sWarningMessage += cProcessedMeshName;
		sWarningMessage += "), assuming that all polygons are triangles!";
		MGlobal::displayWarning( sWarningMessage.c_str() );
	}

	const uint numQuadFlags = sgQuadFlags.GetItemCount();

	// quad detection loop
	uint numPolygons = 0;
	uint numVertexIds = 0;

	bool bHasInvalidQuadFlags = CalculateNumPolygonsAndVertexIds( sgQuadFlags, numPolygons, numVertexIds );
	if( bHasInvalidQuadFlags )
	{
		// repair quad flags (assume triangles)
		SetQuadFlagsToTriangles( sgQuadFlags );
		bHasInvalidQuadFlags = CalculateNumPolygonsAndVertexIds( sgQuadFlags, numPolygons, numVertexIds );

		// output warning message
		std::string sWarningMessage = "Quad import - found invalid quad flags in geometry (";
		sWarningMessage += cProcessedMeshName;
		sWarningMessage += "), assuming that all polygons are triangles!";
		MGlobal::displayWarning( sWarningMessage.c_str() );
	}

	std::vector<int> PolygonToTriangleIndices;
	std::vector<int> PolygonTriangleCount;
	PolygonToTriangleIndices.resize( numPolygons );
	PolygonTriangleCount.resize( numPolygons );

	// copy vertices
	mVertexPositions.setLength( vertexCount );
	for( uint v = 0; v < vertexCount; ++v )
	{
		spRealData sgCoord = sgCoords->GetTuple( v );
		mVertexPositions.set( v, sgCoord[ 0 ], sgCoord[ 1 ], sgCoord[ 2 ] );
	}

	// copy triangle indices
	mVertexIds.setLength( numVertexIds );
	mVertexCountPerPolygon.setLength( numPolygons );

	uint currentSimplygonCornerIndex = 0;
	uint currentMayaCornerIndex = 0;

	uint currentPolygon = 0;
	for( uint tid = 0; tid < numQuadFlags; ++tid )
	{
		const char cQuadFlag = sgQuadFlags->GetItem( tid );

		// per triangle
		if( cQuadFlag == SG_QUADFLAG_TRIANGLE )
		{
			for( uint c = 0; c < 3; ++c )
			{
				const rid vid = sgVertexIds->GetItem( currentSimplygonCornerIndex );
				currentSimplygonCornerIndex++;

				mVertexIds.set( vid, currentMayaCornerIndex++ );
			}

			PolygonToTriangleIndices[ currentPolygon ] = tid;
			PolygonTriangleCount[ currentPolygon ] = 1;
			mVertexCountPerPolygon.set( 3, currentPolygon++ );
		}

		// per quad
		else if( cQuadFlag == SG_QUADFLAG_FIRST )
		{
			// fetch ids of triangle one
			rid triangleOneVertexIds[ 3 ] = { -1, -1, -1 };
			triangleOneVertexIds[ 0 ] = sgVertexIds->GetItem( currentSimplygonCornerIndex + 0 );
			triangleOneVertexIds[ 1 ] = sgVertexIds->GetItem( currentSimplygonCornerIndex + 1 );
			triangleOneVertexIds[ 2 ] = sgVertexIds->GetItem( currentSimplygonCornerIndex + 2 );

			// fetch ids of triangle two
			rid triangleTwoVertexIds[ 3 ] = { -1, -1, -1 };
			triangleTwoVertexIds[ 0 ] = sgVertexIds->GetItem( currentSimplygonCornerIndex + 3 );
			triangleTwoVertexIds[ 1 ] = sgVertexIds->GetItem( currentSimplygonCornerIndex + 4 );
			triangleTwoVertexIds[ 2 ] = sgVertexIds->GetItem( currentSimplygonCornerIndex + 5 );

			// consume 6 Simplygon indices in this loop
			currentSimplygonCornerIndex += 6;

			// merge the two triangles into a quad
			int originalCornerIndices[ 4 ] = { 0, 0, 0, 0 };
			int quadVertexIds[ 4 ] = { 0, 0, 0, 0 };
			MergeTwoTrianglesIntoQuad( triangleOneVertexIds, triangleTwoVertexIds, quadVertexIds, originalCornerIndices );

			// store new quad indices to Maya's vertex index array,
			// consume one Maya vertex index per corner (4)
			for( uint c = 0; c < 4; ++c )
			{
				const rid vid = quadVertexIds[ c ];
				mVertexIds.set( vid, currentMayaCornerIndex++ );
			}

			// store number of vertex indices for this polygon index,
			// increase polygon index for next iteration
			PolygonToTriangleIndices[ currentPolygon ] = tid;
			PolygonTriangleCount[ currentPolygon ] = 2;
			mVertexCountPerPolygon.set( 4, currentPolygon++ );
		}
	}

	// this->modifiedTransform = this->MayaMesh.create( vertexCount, triangleCount, mVertexPositions, mVertexCountPerPolygon, mVertexIds );
	this->modifiedTransform =
	    this->MayaMesh.create( vertexCount, numPolygons, mVertexPositions, mVertexCountPerPolygon, mVertexIds, MObject::kNullObj, &mStatus );
	if( mStatus != MStatus::kSuccess )
	{
		std::string sErrorMessage = "Quad import - creation of a Maya mesh failed, this is usually caused by invalid mesh data, such as invalid vertex indices "
		                            "or mismatch in field size: ";
		sErrorMessage += cProcessedMeshName;
		sErrorMessage += "!";

		MGlobal::displayError( sErrorMessage.c_str() );
		return mStatus;
	}

	// fetch and clear non-wanted uvs
	MStringArray mUVSetNames;
	const int uvCount = this->MayaMesh.numUVSets();
	if( uvCount > 0 )
	{
		mStatus = this->MayaMesh.getUVSetNames( mUVSetNames );

		MString mUVSetName = mUVSetNames[ 0 ];
		mStatus = this->MayaMesh.renameUVSet( mUVSetNames[ 0 ], MString( "reuse" ) );
	}

	// fetch all color sets
	MStringArray mColorSetNames;
	const int colorCount = this->MayaMesh.numColorSets();
	if( colorCount > 0 )
	{
		this->MayaMesh.getColorSetNames( mColorSetNames );
	}

	MString mMeshName = bHasMeshMap ? RemoveIllegalCharacters( this->originalNodeName ) : RemoveIllegalCharacters( MString( sgProcessedMesh->GetName() ) );
	std::string sFormattedMeshName = GenerateFormattedName( this->cmd->meshFormatString.asChar(), mMeshName.asChar(), std::to_string( logicalLodIndex ) );
	MString mFormattedMeshName = GetNonCollidingMeshName( MString( sFormattedMeshName.c_str() ) );

	MFnDagNode mModifiedDagNode( modifiedTransform );
	mFormattedMeshName = mModifiedDagNode.setName( mFormattedMeshName );

	// set the parent if there is a mesh mapping
	// copy the original transformation, if any
	if( bHasMeshMap )
	{
		MFnDagNode mOriginalDagNode( this->originalNode );
		for( uint p = 0; p < mOriginalDagNode.parentCount(); ++p )
		{
			MObject mParentObject = mOriginalDagNode.parent( 0 );
			MFnDagNode mParentDagNode( mParentObject );
			mParentDagNode.addChild( modifiedTransform );
		}

		MFnTransform mOriginalTransformation( this->originalNode.node() );
		MFnTransform mModifiedTransformation( modifiedTransform );
		mModifiedTransformation.set( mOriginalTransformation.transformation() );
	}

	else
	{
		this->postUpdate = true;
	}

	// setup the modified node handles
	mStatus = MDagPath::getAPathTo( modifiedTransform, this->modifiedNode );
	this->modifiedNodeShape = this->modifiedNode;

	mStatus = this->modifiedNodeShape.extendToShape();
	if( !mStatus )
	{
		std::string sErrorMessage = "Could not get shape when importing node: ";
		sErrorMessage += cProcessedMeshName;
		sErrorMessage += "!";

		MGlobal::displayError( sErrorMessage.c_str() );
		return mStatus;
	}

	// add the LOD info object
	MeshNodeLOD meshLOD;
	meshLOD.LODNode = this->modifiedNode;
	meshLOD.LODNodeShape = this->modifiedNodeShape;
	this->meshLODs.push_back( meshLOD );

	// setup the back mapping of the mesh
	this->SetupBackMapping_Quad();

	// setup materials
	spTextureTable sgTextureTable = sgProcessedScene->GetTextureTable();
	spMaterialTable sgMaterialTable = sgProcessedScene->GetMaterialTable();
	spRidArray sgMaterialIds = this->sgMeshData->GetMaterialIds();

	const bool bHasMaterialsInMaterialTable = sgMaterialTable.NonNull() ? sgMaterialTable->GetMaterialsCount() > 0 : false;
	bool bHasUnmappedMaterials = false;
	std::string sUnmappedMaterialTexCoordName = "";
	std::set<int> sgUniqueMaterialIndices;
	std::map<int, MaterialIndexToMayaMaterial*> sgUniqueMaterialMapping;

	if( !sgMaterialIds.IsNull() && bHasMaterialsInMaterialTable )
	{
		// go through each material index and store all unique
		for( uint tid = 0; tid < triangleCount; ++tid )
		{
			const int mid = sgMaterialIds->GetItem( tid );
			if( mid < 0 )
				continue;

			else if( mid >= (int)sgMaterialTable->GetMaterialsCount() )
			{
				std::string sErrorMessage = "Writeback of material(s) failed due to an out-of-range material id when importing node ";
				sErrorMessage += mMeshName.asChar();
				sErrorMessage += "!";

				MGlobal::displayError( sErrorMessage.c_str() );
				return MStatus::kFailure;
			}

			sgUniqueMaterialIndices.insert( mid );
		}

		for( std::set<int>::const_iterator& mIterator = sgUniqueMaterialIndices.begin(); mIterator != sgUniqueMaterialIndices.end(); mIterator++ )
		{
			int mid = *mIterator;

			spMaterial sgMaterial = sgMaterialTable->GetMaterial( mid );
			spString rMaterialName = sgMaterial->GetName();
			spString rMaterialId = sgMaterial->GetMaterialGUID();

			// is this a new material?
			if( !this->cmd->mapMaterials )
			{
				bHasUnmappedMaterials = true;
				sgUniqueMaterialMapping.insert( std::pair<int, MaterialIndexToMayaMaterial*>( mid, new MaterialIndexToMayaMaterial( MObject::kNullObj ) ) );
			}
			else
			{
				std::map<std::string, std::string>::const_iterator& gGuidToMaterialMap = this->cmd->s_GlobalMaterialGuidToDagPath.find( rMaterialName.c_str() );

				std::map<std::string, StandardMaterial*>::const_iterator& guidToMaterialIterator =
				    this->materialHandler->MaterialIdToStandardMaterial.find( rMaterialId.c_str() );

				const bool bHasGuidMap = gGuidToMaterialMap != this->cmd->s_GlobalMaterialGuidToDagPath.end();

				if( guidToMaterialIterator != this->materialHandler->MaterialIdToStandardMaterial.end() )
				{
					bHasUnmappedMaterials = true;
					sgUniqueMaterialMapping.insert( std::pair<int, MaterialIndexToMayaMaterial*>( mid, new MaterialIndexToMayaMaterial( MObject::kNullObj ) ) );
				}
				else if( !bHasGuidMap )
				{
					MObject mMaterialObject = MObject::kNullObj;
					if( GetMObjectOfNamedObject( rMaterialName.c_str(), mMaterialObject ) && this->cmd->extractionType != BATCH_PROCESSOR )
					{
						sgUniqueMaterialMapping.insert(
						    std::pair<int, MaterialIndexToMayaMaterial*>( mid, new MaterialIndexToMayaMaterial( mMaterialObject ) ) );
					}
					else
					{
						bHasUnmappedMaterials = true;
						sgUniqueMaterialMapping.insert(
						    std::pair<int, MaterialIndexToMayaMaterial*>( mid, new MaterialIndexToMayaMaterial( MObject::kNullObj ) ) );
					}
				}
				else
				{
					MObject mMaterialObject = MObject::kNullObj;
					MString mMappedMaterialName = gGuidToMaterialMap->first.c_str();
					MString mMappedShaderGroupName = gGuidToMaterialMap->second.c_str();

					if( GetMObjectOfNamedObject( mMappedMaterialName, mMaterialObject ) )
					{
						sgUniqueMaterialMapping.insert(
						    std::pair<int, MaterialIndexToMayaMaterial*>( mid, new MaterialIndexToMayaMaterial( mMaterialObject, mMappedShaderGroupName ) ) );
					}
					else
					{
						bHasUnmappedMaterials = true;
						sgUniqueMaterialMapping.insert(
						    std::pair<int, MaterialIndexToMayaMaterial*>( mid, new MaterialIndexToMayaMaterial( MObject::kNullObj ) ) );
					}
				}
			}

			// loop through all material channels to create a uv-to-texture map
			const uint channelCount = sgMaterial->GetMaterialChannelCount();
			for( uint c = 0; c < channelCount; ++c )
			{
				spString rChannelName = sgMaterial->GetMaterialChannelFromIndex( c );
				const char* cChannelName = rChannelName;

				spShadingNode sgExitNode = sgMaterial->GetShadingNetwork( cChannelName );
				if( sgExitNode.IsNull() )
					continue;

				// fetch all textures for this material channel
				std::map<std::basic_string<TCHAR>, spShadingTextureNode> texNodeMap;
				this->materialHandler->FindAllUpStreamTextureNodes( sgExitNode, texNodeMap );

				// fetch texture id and uv for each texture node
				for( std::map<std::basic_string<TCHAR>, spShadingTextureNode>::const_iterator& texIterator = texNodeMap.begin();
				     texIterator != texNodeMap.end();
				     texIterator++ )
				{
					spString rTexCoordName = texIterator->second->GetTexCoordName();
					if( rTexCoordName.IsNullOrEmpty() )
						continue;

					const char* cTexCoordName = rTexCoordName;
					sUnmappedMaterialTexCoordName = rTexCoordName;
					break;
				}
			}
		}
	}

	// setup all UVs on the mesh, name them correctly
	for( uint uvSetIndex = 0; uvSetIndex < SG_NUM_SUPPORTED_TEXTURE_CHANNELS; ++uvSetIndex )
	{
		spRealArray sgTexCoords = this->sgMeshData->GetTexCoords( uvSetIndex );

		if( sgTexCoords.IsNull() || sgTexCoords->GetItemCount() == 0 )
			continue;

		spString sgTexCoordName = sgTexCoords->GetAlternativeName();
		const char* cUVNameBuffer = sgTexCoordName.c_str() ? sgTexCoordName.c_str() : "(null)";

		MIntArray mMeshTrianglesUV; // the uv-coordinates used by each triangle
		MFloatArray mMeshUArray;    // the u-coords
		MFloatArray mMeshVArray;    // the v-coords

		// make an indexed, packed copy
		spRidArray sgIndices = sg->CreateRidArray();
		spRealArray sgIndicedTexCoords = spRealArray::SafeCast( sgTexCoords->NewPackedCopy( sgIndices ) );

		if( !sgIndicedTexCoords.IsNull() )
		{
			const uint tupleCount = sgIndicedTexCoords->GetTupleCount();
			mMeshUArray.setLength( tupleCount );
			mMeshVArray.setLength( tupleCount );

			for( uint i = 0; i < tupleCount; ++i )
			{
				// get the uv coordinate
				spRealData sgIndicedTexCoord = sgIndicedTexCoords->GetTuple( i );

				// set the uv coordinate
				mMeshUArray.set( sgIndicedTexCoord[ 0 ], i );
				mMeshVArray.set( sgIndicedTexCoord[ 1 ], i );
			}

			// convert Simplygon triangles into Quads,
			// and generate UV index list
			int currentSimplygonUVCornerIndex = 0;
			int currentMayaUVCornerIndex = 0;

			mMeshTrianglesUV.setLength( numVertexIds );
			for( uint tid = 0; tid < numQuadFlags; ++tid )
			{
				const char cQuadFlag = sgQuadFlags->GetItem( tid );

				// triangle
				if( cQuadFlag == SG_QUADFLAG_TRIANGLE )
				{
					for( uint c = 0; c < 3; ++c )
					{
						const rid vid = sgIndices->GetItem( currentSimplygonUVCornerIndex );
						currentSimplygonUVCornerIndex++;

						// mVertexIds.set( vid, currentMayaCornerIndex++ );
						mMeshTrianglesUV.set( vid, currentMayaUVCornerIndex++ );
					}
				}

				// quad
				else if( cQuadFlag == SG_QUADFLAG_FIRST )
				{
					rid triangleOneVertexIds[ 3 ] = { -1, -1, -1 };
					triangleOneVertexIds[ 0 ] = sgIndices->GetItem( currentSimplygonUVCornerIndex + 0 );
					triangleOneVertexIds[ 1 ] = sgIndices->GetItem( currentSimplygonUVCornerIndex + 1 );
					triangleOneVertexIds[ 2 ] = sgIndices->GetItem( currentSimplygonUVCornerIndex + 2 );

					rid triangleTwoVertexIds[ 3 ] = { -1, -1, -1 };
					triangleTwoVertexIds[ 0 ] = sgIndices->GetItem( currentSimplygonUVCornerIndex + 3 );
					triangleTwoVertexIds[ 1 ] = sgIndices->GetItem( currentSimplygonUVCornerIndex + 4 );
					triangleTwoVertexIds[ 2 ] = sgIndices->GetItem( currentSimplygonUVCornerIndex + 5 );

					currentSimplygonUVCornerIndex += 6;

					int originalCornerIndices[ 4 ] = { 0, 0, 0, 0 };
					int quadVertexIds[ 4 ] = { 0, 0, 0, 0 };
					MergeTwoTrianglesIntoQuad( triangleOneVertexIds, triangleTwoVertexIds, quadVertexIds, originalCornerIndices );

					for( uint c = 0; c < 4; ++c )
					{
						const rid vid = quadVertexIds[ c ];
						mMeshTrianglesUV.set( vid, currentMayaUVCornerIndex++ );
					}
				}
			}
		}

		MString mUVSet;

		// make an extra copy of correct type to avoid in-loop casts
		mStatus = mUVSetNames.clear();
		mStatus = this->MayaMesh.getUVSetNames( mUVSetNames );

		MString mUVNameBuffer = cUVNameBuffer;
		for( uint uvIndex = 0; uvIndex < mUVSetNames.length(); ++uvIndex )
		{
			if( mUVSetNames[ uvIndex ] == mUVNameBuffer )
			{
				mUVSet = cUVNameBuffer;
			}
		}

		if( mUVSet.length() == 0 )
		{
			mStatus = TryReuseDefaultUV( this->MayaMesh, mUVNameBuffer );
			if( mStatus )
			{
				mUVSet = mUVNameBuffer;
			}
			else
			{
				mUVSet = this->MayaMesh.createUVSetWithName( mUVNameBuffer );
			}
		}

		mStatus = this->MayaMesh.setUVs( mMeshUArray, mMeshVArray, &mUVSet );
		mStatus = this->MayaMesh.assignUVs( mVertexCountPerPolygon, mMeshTrianglesUV, &mUVSet );
	}

	// setup all Colors on the mesh, name them correctly
	for( uint colorSetIndex = 0; colorSetIndex < SG_NUM_SUPPORTED_COLOR_CHANNELS; ++colorSetIndex )
	{
		spRealArray sgVertexColors = this->sgMeshData->GetColors( colorSetIndex );

		if( sgVertexColors.IsNull() || sgVertexColors->GetItemCount() == 0 )
			continue;

		spString sgColorName = sgVertexColors->GetAlternativeName();
		const char* cVertexColorNameBuffer = sgColorName.c_str() ? sgColorName.c_str() : "(null)";

		// make an indexed, packed copy
		spRidArray sgIndices = sg->CreateRidArray();
		spRealArray sgIndicedColors = spRealArray::SafeCast( sgVertexColors->NewPackedCopy( sgIndices ) );
		const uint numColorIndices = sgIndicedColors->GetTupleCount();

		MIntArray mColorIndices( numVertexIds );
		MColorArray mColorsArray( numColorIndices );

		// read color from Simplygon,
		// store in Maya-array
		for( uint i = 0; i < numColorIndices; i++ )
		{
			spRealData sgColor = sgIndicedColors->GetTuple( i );
			mColorsArray[ i ] = MColor( sgColor[ 0 ], sgColor[ 1 ], sgColor[ 2 ], sgColor[ 3 ] );
		}

		// convert Simplygon triangles into Quads,
		// and generate color index list
		int currentSimplygonColorCornerIndex = 0;
		int currentMayaColorCornerIndex = 0;

		for( uint tid = 0; tid < numQuadFlags; ++tid )
		{
			const char cQuadFlag = sgQuadFlags->GetItem( tid );

			// triangle
			if( cQuadFlag == SG_QUADFLAG_TRIANGLE )
			{
				for( uint c = 0; c < 3; ++c )
				{
					const rid vid = sgIndices->GetItem( currentSimplygonColorCornerIndex++ );
					mColorIndices[ currentMayaColorCornerIndex++ ] = vid;
				}
			}
			// quad
			else if( cQuadFlag == SG_QUADFLAG_FIRST )
			{
				rid triangleOneVertexIds[ 3 ] = { -1, -1, -1 };
				triangleOneVertexIds[ 0 ] = sgIndices->GetItem( currentSimplygonColorCornerIndex + 0 );
				triangleOneVertexIds[ 1 ] = sgIndices->GetItem( currentSimplygonColorCornerIndex + 1 );
				triangleOneVertexIds[ 2 ] = sgIndices->GetItem( currentSimplygonColorCornerIndex + 2 );

				rid triangleTwoVertexIds[ 3 ] = { -1, -1, -1 };
				triangleTwoVertexIds[ 0 ] = sgIndices->GetItem( currentSimplygonColorCornerIndex + 3 );
				triangleTwoVertexIds[ 1 ] = sgIndices->GetItem( currentSimplygonColorCornerIndex + 4 );
				triangleTwoVertexIds[ 2 ] = sgIndices->GetItem( currentSimplygonColorCornerIndex + 5 );

				currentSimplygonColorCornerIndex += 6;

				int originalCornerIndices[ 4 ] = { 0, 0, 0, 0 };
				int quadVertexIds[ 4 ] = { 0, 0, 0, 0 };
				MergeTwoTrianglesIntoQuad( triangleOneVertexIds, triangleTwoVertexIds, quadVertexIds, originalCornerIndices );

				for( uint c = 0; c < 4; ++c )
				{
					const rid vid = quadVertexIds[ c ];
					mColorIndices[ currentMayaColorCornerIndex++ ] = vid;
				}
			}
		}

		MString mColorSetName = MString( cVertexColorNameBuffer );
		MString mTmpColorSetName = this->MayaMesh.createColorSetWithName( mColorSetName );

		if( mTmpColorSetName != mColorSetName )
		{
			// delete the old set
			this->MayaMesh.deleteColorSet( mColorSetName );

			// create a new set
			MString mNewColorSetName = this->MayaMesh.createColorSetWithName( mColorSetName );

			// delete the previous set as well (we can't rename it)
			this->MayaMesh.deleteColorSet( mTmpColorSetName );
			mTmpColorSetName = mNewColorSetName;
		}

		mStatus = this->MayaMesh.setCurrentColorSetName( mTmpColorSetName );
		mStatus = this->MayaMesh.setColors( mColorsArray, &mTmpColorSetName );
		mStatus = this->MayaMesh.assignColors( mColorIndices, &mTmpColorSetName );
	}

	// if all materials are known and we have a mesh map
	// try to use currently set uv- and color-set.
	if( bHasMeshMap && !bHasUnmappedMaterials )
	{
		MFnMesh mOriginalMesh;
		mOriginalMesh.setObject( this->originalNode );
		mOriginalMesh.syncObject();

		MString mOriginalUvSetName;
		mStatus = mOriginalMesh.getCurrentUVSetName( mOriginalUvSetName );
		if( mStatus )
		{
			mStatus = this->MayaMesh.setCurrentUVSetName( mOriginalUvSetName );
		}

		MString mOriginalColorSetName;
		mStatus = mOriginalMesh.getCurrentColorSetName( mOriginalColorSetName );
		if( mStatus )
		{
			mStatus = this->MayaMesh.setCurrentUVSetName( mOriginalColorSetName );
		}
	}
	else
	{
		mStatus = this->MayaMesh.setCurrentUVSetName( sUnmappedMaterialTexCoordName.c_str() );
	}

	// apply normals, if any
	if( !this->sgMeshData->GetNormals().IsNull() )
	{
		MCheckStatus( this->WritebackNormals(), "Could not write normals and smoothing to mesh." );
	}

	this->MayaMesh.updateSurface();

	// apply crease data
	mStatus = this->AddCreaseData_Quad( PolygonToTriangleIndices, PolygonTriangleCount );
	if( !mStatus )
	{
		std::string sErrorMessage = "Failed to map crease data when importing node: ";
		sErrorMessage += cProcessedMeshName;
		sErrorMessage += "!";

		MGlobal::displayError( sErrorMessage.c_str() );
		return mStatus;
	}

	this->MayaMesh.updateSurface();
	this->MayaMesh.syncObject();

	// if we have a mesh map,
	// copy as many properties from original as we can
	if( bHasMeshMap )
	{
		// copy attribute data
		MString mCommand = "SimplygonMaya_copyAttributes( \"";
		mCommand += this->originalNode.fullPathName();
		mCommand += "\" , \"";
		mCommand += this->modifiedNode.fullPathName();
		mCommand += "\");";

		mStatus = ::ExecuteCommand( mCommand );
		if( mStatus != MStatus::kSuccess )
		{
			std::string sErrorMessage = "Failed to map attributes when importing node: ";
			sErrorMessage += cProcessedMeshName;
			sErrorMessage += "!";

			MGlobal::displayError( sErrorMessage.c_str() );
			return mStatus;
		}

		// copy object level blind data
		mCommand = "SimplygonMaya_copyObjectLevelBlindData( \"";
		mCommand += this->originalNodeShape.fullPathName();
		mCommand += "\" , \"";
		mCommand += this->modifiedNodeShape.fullPathName();
		mCommand += "\");";

		mStatus = ::ExecuteCommand( mCommand );
		if( mStatus != MStatus::kSuccess )
		{
			std::string sErrorMessage = "Failed to map object level blind-data when importing node: ";
			sErrorMessage += cProcessedMeshName;
			sErrorMessage += "!";

			MGlobal::displayError( sErrorMessage.c_str() );
			return mStatus;
		}

		// TODO: copy vertex and triangle blind data
		if( inMemoryMeshMap )
		{
			BlindData& inMemoryBlindData = inMemoryMeshMap->mayaNode->blindData;
			inMemoryBlindData.ApplyBlindDataToMesh( this->MayaMesh, this->VertexBackMapping, this->PolygonBackMapping );
		}
	}

	if( bHasMaterialsInMaterialTable )
	{
		// setup material
		std::vector<int>* faceMaterialIds = new std::vector<int>();
		faceMaterialIds->resize( numPolygons );

		int currentMaterialIndex = 0;
		for( std::set<int>::const_iterator& mIterator = sgUniqueMaterialIndices.begin(); mIterator != sgUniqueMaterialIndices.end(); mIterator++ )
		{
			const int mid = *mIterator;

			spMaterial sgMaterial = sgMaterialTable->GetMaterial( mid );
			std::string sMaterialId = sgMaterial->GetMaterialGUID();

			spString rMaterialName = sgMaterial->GetName();
			const char* cMaterialName = rMaterialName.c_str();
			const bool bHasMaterialName = cMaterialName ? strlen( cMaterialName ) > 0 : false;

			MString mShadingGroupName = "";

			StandardMaterial* existingStandardMaterial = nullptr;
			StandardMaterial* standardMaterial = nullptr;

			const std::map<int, MaterialIndexToMayaMaterial*>::const_iterator& materialIndexToMObject = sgUniqueMaterialMapping.find( mid );

			// if mapping exists, reuse original
			if( materialIndexToMObject != sgUniqueMaterialMapping.end() && materialIndexToMObject->second->GetMObject() != MObject::kNullObj )
			{
				MaterialIndexToMayaMaterial* materialMap = materialIndexToMObject->second;

				// if direct mapping found, use it
				if( materialMap->HasShaderGroup() )
				{
					mShadingGroupName = materialMap->GetShaderGroup();
				}

				// otherwise, resolve shader group based on material name
				else
				{
					MObject mMaterialObject = materialMap->GetMObject();

					MFnDependencyNode mShaderGroupDependencyNode( mMaterialObject );
					std::basic_string<TCHAR> tMaterialName( mShaderGroupDependencyNode.name().asChar() );

					MPlugArray mMaterialPlugs;
					mStatus = mShaderGroupDependencyNode.getConnections( mMaterialPlugs );

					bool bNotFound = true;
					for( uint materialPlugIndex = 0; materialPlugIndex < mMaterialPlugs.length(); ++materialPlugIndex )
					{
						MPlug mMaterialPlug = mMaterialPlugs[ materialPlugIndex ];
						std::string sPlugName = mMaterialPlug.name().asChar();

						MPlugArray mConnectionPlugs;

						// get output plugs
						mMaterialPlug.connectedTo( mConnectionPlugs, false, true );

						for( uint connectionPlugIndex = 0; connectionPlugIndex < mConnectionPlugs.length(); ++connectionPlugIndex )
						{
							std::string sConnectionPlugName = mConnectionPlugs[ connectionPlugIndex ].name().asChar();

							MObject mPlugMaterialObject = mConnectionPlugs[ connectionPlugIndex ].node();
							const MFn::Type mConnectionPlugType = mPlugMaterialObject.apiType();
							if( mConnectionPlugType != MFn::kShadingEngine )
								continue;

							// store reference
							MFnDependencyNode mPlugDependencyNode( mPlugMaterialObject );
							mShadingGroupName = mPlugDependencyNode.name( &mStatus ).asChar();

							// 						MObject mShaderGroup = GetConnectedNamedPlug(mPlugDependencyNode, "surfaceShader");
							// 						MFnDependencyNode mShaderGroupDependencyNode(mShaderGroup);
							// 						MString mMaterialName(mShaderGroupDependencyNode.name().asChar());

							bNotFound = false;
							break;
						}

						if( !bNotFound )
						{
							break;
						}
					}
				}
			}

			// else, create a new material for the specific material id
			else if( bHasMaterialName )
			{
				MString mStandardMaterialName = GetUniqueMaterialName( cMaterialName );

				standardMaterial = new StandardMaterial( this->cmd, sgTextureTable );
				standardMaterial->NodeName = mStandardMaterialName;
				standardMaterial->sgMaterial = sgMaterial;

				spString rSgMaterialId = standardMaterial->sgMaterial->GetMaterialGUID();
				const char* cSgMaterialId = rSgMaterialId;

				const std::map<std::string, StandardMaterial*>::const_iterator& guidToMaterialIterator =
				    this->materialHandler->MaterialIdToStandardMaterial.find( sMaterialId );

				// has this material been handled before?
				if( guidToMaterialIterator != this->materialHandler->MaterialIdToStandardMaterial.end() )
				{
					// reuse previously handled material
					existingStandardMaterial = guidToMaterialIterator->second;

					if( existingStandardMaterial )
					{
						// store shading group name for material assignment
						mShadingGroupName = existingStandardMaterial->ShaderGroupName;
					}
				}
				else
				{
					// material doesn't exist, create new material
					mStatus = standardMaterial->CreatePhong( this->modifiedNodeShape, mFormattedMeshName, mStandardMaterialName, true );
					if( !mStatus )
					{
						return mStatus;
					}

					if( !this->cmd->DoNotGenerateMaterials() && this->cmd->extractionType != BATCH_PROCESSOR )
					{
						std::string sWarningMessage = "StandardMaterial::CreatePhong - Generating unmapped material: ";
						sWarningMessage += std::string( mStandardMaterialName.asChar() ) + " (";
						sWarningMessage += std::string( standardMaterial->ShaderGroupName.asChar() ) + ").";

						MGlobal::displayWarning( sWarningMessage.c_str() );
					}

					// add to mapping, in case id shows up later
					this->materialHandler->MaterialIdToStandardMaterial.insert( std::pair<std::string, StandardMaterial*>( cSgMaterialId, standardMaterial ) );

					// store shading group name for material assignment
					mShadingGroupName = standardMaterial->ShaderGroupName;
				}
			}

			MIntArray* mMayaMaterialIds = new MIntArray();

			uint polygonIndex = 0;

			// find and append polygons with the current material id
			for( uint tid = 0; tid < triangleCount; ++tid )
			{
				const int sgMaterialIndex = sgMaterialIds->GetItem( tid );
				const char cQuadFlag = sgQuadFlags->GetItem( tid );

				// per triangle
				if( cQuadFlag == SG_QUADFLAG_TRIANGLE )
				{
					if( sgMaterialIndex == mid )
					{
						mMayaMaterialIds->append( polygonIndex );
						faceMaterialIds->at( polygonIndex ) = (int)currentMaterialIndex;
					}
					polygonIndex++;
				}

				// per quad
				else if( cQuadFlag == SG_QUADFLAG_FIRST )
				{
					if( sgMaterialIndex == mid )
					{
						mMayaMaterialIds->append( polygonIndex );
						faceMaterialIds->at( polygonIndex ) = (int)currentMaterialIndex;
					}
					polygonIndex++;
				}
			}

			// setup the component set
			MFnSingleIndexedComponent mFaceIndices;
			MObject mFaces = mFaceIndices.create( MFn::kMeshPolygonComponent );
			if( !mFaceIndices.addElements( *mMayaMaterialIds ) )
			{
				std::string sErrorMessage = "Failed to map material ids when importing node: ";
				sErrorMessage += cProcessedMeshName;
				sErrorMessage += "!";

				MGlobal::displayError( sErrorMessage.c_str() );
				return MStatus::kFailure;
			}

			delete mMayaMaterialIds;

			// apply material (named material is required)
			if( bHasMaterialName && !this->cmd->DoNotGenerateMaterials() )
			{
				MString mCommand = "sets -e -forceElement " + mShadingGroupName;
				::ExecuteSelectedObjectCommand( mCommand, this->modifiedNode, mFaces );
			}

			const bool bReusingOriginalMaterial = !standardMaterial && !existingStandardMaterial;

			// if new material, extract mapping for alter use
			if( standardMaterial )
			{
				standardMaterial->ExtractMapping( this->modifiedNodeShape );
			}

			// if reusing created material, copy uv-linking
			if( existingStandardMaterial )
			{
				existingStandardMaterial->ImportMapping( this->modifiedNodeShape );
				this->cmd->GetMaterialInfoHandler()->AddReuse( mFormattedMeshName.asChar(), existingStandardMaterial->ShaderGroupName.asChar() );
			}

			// if reusing original material
			if( bReusingOriginalMaterial )
			{
				this->cmd->GetMaterialInfoHandler()->AddReuse( mFormattedMeshName.asChar(), mShadingGroupName.asChar() );
			}

			currentMaterialIndex++;
		}

		// clear material mapping
		sgUniqueMaterialMapping.erase( sgUniqueMaterialMapping.begin(), sgUniqueMaterialMapping.end() );

		// add face material ids for later use
		this->cmd->GetMaterialInfoHandler()->AddMaterialIds( mFormattedMeshName.asChar(), *faceMaterialIds );

		// done with the sets
		delete faceMaterialIds;
	}
	else
	{
		this->cmd->GetMaterialInfoHandler()->Add( mFormattedMeshName.asChar() );
	}

	// add to all generic sets
	mStatus = this->AddToGenericSets();
	if( !mStatus )
	{
		std::string sErrorMessage = "Failed to map mesh data to generic sets when importing node: ";
		sErrorMessage += cProcessedMeshName;
		sErrorMessage += "!";

		MGlobal::displayError( sErrorMessage.c_str() );
		return mStatus;
	}

	// copy the uv linkage from the original node
	if( bHasMeshMap && this->cmd->mapMaterials && !bHasUnmappedMaterials )
	{
		MString mCommand = "SimplygonMaya_copyUVSetLinks(\"" + this->originalNodeShape.fullPathName() + "\");";
		mStatus = ::ExecuteSelectedObjectCommand( mCommand, this->modifiedNodeShape, MObject::kNullObj );
	}

	// try to find stored original meshdata from extraction
	if( inMemoryMeshMap )
	{
		const std::vector<BlendShape>& inMemoryBlendShapes = inMemoryMeshMap->mayaNode->blendShape;
		for( uint b = 0; b < inMemoryBlendShapes.size(); ++b )
		{
			const BlendShape& theBlendShape = inMemoryBlendShapes[ b ];

			std::vector<MString> mDeleteTargetQueue;

			MString mBaseObjectName = mFormattedMeshName;

			// create mel command to be able to find the base and targets
			MString mMelConnectObjectsCommand;

			MString mBlendShapeName =
			    GenerateFormattedBlendShapeName( this->cmd->blendshapeFormatString.asChar(), theBlendShape.name.asChar(), std::to_string( logicalLodIndex ) )
			        .c_str();

			// create the blendShape
			mMelConnectObjectsCommand = MString( "blendShape -n " ) + mBlendShapeName + " " + mBaseObjectName;
			ExecuteCommand( mMelConnectObjectsCommand );

			mMelConnectObjectsCommand = "blendShape -edit ";
			for( uint f = 0; f < theBlendShape.BlendWeights.size(); ++f )
			{
				const BlendWeight& bw = theBlendShape.BlendWeights[ f ];
				spRealArray sgTargetCoords = spRealArray::SafeCast( this->sgMeshData->GetUserVertexField( bw.fieldName.asChar() ) );

				const bool bHasBlendShapeData = !sgTargetCoords.IsNull();
				if( bHasBlendShapeData )
				{
					// set up a vertex array
					MFloatPointArray mTargetBlendShapeVertexField;
					mTargetBlendShapeVertexField.setLength( vertexCount );

					for( uint vid = 0; vid < vertexCount; ++vid )
					{
						spRealData sgTargetCoord = sgTargetCoords->GetTuple( vid );
						spRealData sgCoordinate = sgCoords->GetTuple( vid );

						// the field is relative, add the vertex coord to it
						mTargetBlendShapeVertexField[ vid ] = MFloatPoint(
						    sgTargetCoord[ 0 ] + sgCoordinate[ 0 ], sgTargetCoord[ 1 ] + sgCoordinate[ 1 ], sgTargetCoord[ 2 ] + sgCoordinate[ 2 ] );
					}

					// create the target mesh
					MStatus mResult;
					MFnMesh mTargetMesh;

					MObject mTargetTransform = mTargetMesh.create(
					    vertexCount, numPolygons, mTargetBlendShapeVertexField, mVertexCountPerPolygon, mVertexIds, MObject::kNullObj, &mResult );

					// set target name
					MString mTargetObjectName;

					if( this->cmd->SkipBlendShapeWeightPostfix() )
					{
						mTargetObjectName = bw.weightName;
					}
					else
					{
						mTargetObjectName = GenerateFormattedBlendShapeName(
						                        this->cmd->blendshapeFormatString.asChar(), bw.weightName.asChar(), std::to_string( logicalLodIndex ) )
						                        .c_str();
					}

					mDeleteTargetQueue.push_back( mTargetObjectName );

					// set the name of the target mesh
					MFnDagNode mTargetDagNode( mTargetTransform );
					mTargetObjectName = mTargetDagNode.setName( mTargetObjectName );

					MDagPath mTargetDagPath;
					mStatus = MDagPath::getAPathTo( mTargetTransform, mTargetDagPath );

					mTargetDagPath.extendToShape();

					// add target and weight on the specified index
					mMelConnectObjectsCommand += MString( " -t " ) + mBaseObjectName + " " + ( bw.realIndex ) + " " + mTargetObjectName + " " +
					                             theBlendShape.envelope + " -w " + bw.realIndex + " " + bw.weight + " ";
				}
			}

			mMelConnectObjectsCommand += " " + mBlendShapeName;
			const MStatus mCommandResult = ExecuteCommand( mMelConnectObjectsCommand );

			for( size_t e = 0; e < mDeleteTargetQueue.size(); ++e )
			{
				ExecuteCommand( MString( "delete " ) + mDeleteTargetQueue[ e ] );
			}

			mDeleteTargetQueue.clear();
		}
	}

	// setup the skinning cluster
	mStatus = this->AddSkinning( sgProcessedScene );
	if( !mStatus )
	{
		std::string sErrorMessage = "Failed to map skinning data when importing node: ";
		sErrorMessage += cProcessedMeshName;
		sErrorMessage += "!";

		MGlobal::displayError( sErrorMessage.c_str() );
		return mStatus;
	}

	// set the current node as result
	mResultPath = meshLOD.LODNode;

	// fetch dependency node so that we can write custom attributes
	// such as scene radius, lod index etc.
	MFnDependencyNode mModifiedDependencyNode( this->modifiedNode.node() );

	// max deviation
	if( true )
	{
		const char* cAttributeName = "MaxDeviation";
		spRealArray sgMaxDeviation = spRealArray::SafeCast( sgProcessedScene->GetCustomField( cAttributeName ) );
		if( !sgMaxDeviation.IsNull() )
		{
			const real maxDev = sgMaxDeviation->GetItem( 0 );
			AddAttribute<MFnNumericAttribute, MFnNumericData::Type, float>( mModifiedDependencyNode, cAttributeName, MFnNumericData::Type::kFloat, maxDev );
		}
	}

	// scene radius
	if( true )
	{
		const char* cAttributeName = "SceneRadius";
		const real sceneRadius = sgProcessedScene->GetRadius();
		AddAttribute<MFnNumericAttribute, MFnNumericData::Type, float>( mModifiedDependencyNode, cAttributeName, MFnNumericData::Type::kFloat, sceneRadius );
	}

	// scene meshes radius
	if( true )
	{
		const char* cAttributeName = "SceneMeshesRadius";
		const real sceneMeshesRadius = GetSceneMeshesRadius( sgProcessedScene );
		AddAttribute<MFnNumericAttribute, MFnNumericData::Type, float>(
		    mModifiedDependencyNode, cAttributeName, MFnNumericData::Type::kFloat, sceneMeshesRadius );
	}

	// processed meshes radius
	if( true )
	{
		const char* cAttributeName = "ProcessedMeshesRadius";
		auto sgProcessedMeshesExtents = sgProcessedScene->GetCustomFieldProcessedMeshesExtents();
		if( !sgProcessedMeshesExtents.IsNull() )
		{
			const real processedMeshesRadius = sgProcessedMeshesExtents->GetBoundingSphereRadius();
			AddAttribute<MFnNumericAttribute, MFnNumericData::Type, float>(
			    mModifiedDependencyNode, cAttributeName, MFnNumericData::Type::kFloat, processedMeshesRadius );
		}
	}

	// lod index
	if( true )
	{
		const char* cAttributeName = "LODIndex";
		AddAttribute<MFnNumericAttribute, MFnNumericData::Type, int>(
		    mModifiedDependencyNode, cAttributeName, MFnNumericData::Type::kInt, (int)logicalLodIndex );
	}

	// original node name
	if( true )
	{
		const char* cAttributeName = "OriginalNodeName";
		spString rMeshName = sgProcessedMesh->GetName();
		MString mOriginalNodeName = rMeshName.c_str();

		MFnStringData mStringData;
		MObject mStringObject = mStringData.create( mOriginalNodeName );
		AddAttribute<MFnTypedAttribute, MFnData::Type, MObject>( mModifiedDependencyNode, cAttributeName, MFnData::kString, mStringObject );
	}

	// intended node name
	if( true )
	{
		const char* cAttributeName = "IntendedNodeName";
		MString mIntendedNodeName = sFormattedMeshName.c_str();

		MFnStringData mStringData;
		MObject mStringObject = mStringData.create( mIntendedNodeName );
		AddAttribute<MFnTypedAttribute, MFnData::Type, MObject>( mModifiedDependencyNode, cAttributeName, MFnData::kString, mStringObject );
	}

	// imported node name
	if( true )
	{
		const char* cAttributeName = "ImportedNodeName";
		MString mImportedNodeName = mFormattedMeshName;

		MFnStringData mStringData;
		MObject mStringObject = mStringData.create( mImportedNodeName );
		AddAttribute<MFnTypedAttribute, MFnData::Type, MObject>( mModifiedDependencyNode, cAttributeName, MFnData::kString, mStringObject );
	}

	// transfer original UUID as new attribute
	if( this->originalNode.isValid() )
	{
		const char* cAttributeName = "OriginalUUID";

		MFnDependencyNode mOriginalDependencyNode( this->originalNode.node() );
		MUuid mUUID = mOriginalDependencyNode.uuid( &mStatus );

		if( mStatus == MStatus::kSuccess )
		{
			MFnStringData mStringData;
			MObject mStringObject = mStringData.create( mUUID.asString() );
			AddAttribute<MFnTypedAttribute, MFnData::Type, MObject>( mModifiedDependencyNode, cAttributeName, MFnData::kString, mStringObject );
		}
	}

	return MStatus::kSuccess;
}

// find the shared and matching corners of two triangles. up to 3 corners, returns the count
inline uint findSharedCorners( uint tri0id, uint tri1id, const Simplygon::rid *vertexIds, uint tri0corners[3], uint tri1corners[3] )
{
	const uint start0 = tri0id*3;
	const uint start1 = tri1id*3;
	const uint end0 = start0+3;
	const uint end1 = start1+3;

	uint found = 0;

	for( uint corner0 = start0; corner0 < end0 ; ++corner0 )
	{
		for( uint corner1 = start1; corner1 < end1 ; ++corner1 )
		{
			if( vertexIds[corner0] == vertexIds[corner1] )
			{
				tri0corners[found] = corner0;
				tri1corners[found] = corner1;
				++found;
			}
		}
	}

	return found;
}

// assuming normals are normalized, this will do a fuzzy compare 
inline bool equalNormals( const float *normal0 , const float *normal1 )
{
	return (normal0[0] * normal1[0]) + (normal0[1] * normal1[1]) + (normal0[2] * normal1[2]) > 0.999998f; // > cos(0.1 degrees)
}

// checks if the normals on the edge connecting the triangles are continuous
inline bool isEdgeSmooth( uint tri0id, uint tri1id, const Simplygon::rid *vertexIds, const Simplygon::real *normals )
{
	// find the shared corners. only allow exactly 2 shared corners
	uint tri0corners[3];
	uint tri1corners[3];
	if( findSharedCorners(tri0id, tri1id, vertexIds, tri0corners, tri1corners) != 2 )
		return false;

	// compare normals of the corner pairs
	for( uint inx=0; inx<2; ++inx )
		if( !equalNormals( &normals[tri0corners[inx]*3] , &normals[tri1corners[inx]*3] ) )
			return false;

	return true;
}

// checks if the normals of an edge between two polygons (any combination of triangles or quads) are continuous
inline bool isEdgeSmooth( uint polygon0id, uint polygon1id, const uint *polygonSizes, const uint *polygonFirstTriangleIds, const Simplygon::rid *vertexIds, const Simplygon::real *normals )
{
	const uint tri0start = polygonFirstTriangleIds[polygon0id]; // first or only triangle of the poly
	const uint tri0end = tri0start + polygonSizes[polygon0id] - 2; // 3 or 4 corners -> 1 or 2 triangles

	const uint tri1start = polygonFirstTriangleIds[polygon1id]; // first or only triangle of the poly
	const uint tri1end = tri1start + polygonSizes[polygon1id] - 2; // 3 or 4 corners -> 1 or 2 triangles

	bool isSmooth = false;
	for( uint tri0 = tri0start; tri0 < tri0end; ++tri0 )
	{
		for( uint tri1 = tri1start; tri1 < tri1end; ++tri1 )
		{
			if( isEdgeSmooth( tri0, tri1, vertexIds, normals ) )
				return true; // found a smooth edge
		}
	}

	// no smooth edge found
	return false;
}

MStatus MeshNode::WritebackNormals()
{
	MStatus status;
	 
	const uint vertexCount = this->sgMeshData->GetVertexCount();
	const uint triangleCount = this->sgMeshData->GetTriangleCount();
	const uint polygonCount = this->MayaMesh.numPolygons( &status );
	MCheckStatus( status, "Internal Maya MFnMesh error, could not retrieve numPolygons()" );
	const uint faceVertexCount = this->MayaMesh.numFaceVertices( &status );
	MCheckStatus( status, "Internal Maya MFnMesh error, could not retrieve numFaceVertices()" );

	// early out if the mesh is empty.
	if( triangleCount == 0 )
		return MStatus::kSuccess;

	// get triangles and normal arrays
	spRidData sgVertexIds = this->sgMeshData->GetVertexIds()->GetData();
	const Simplygon::rid * const vertexIds = sgVertexIds.Data();
	spRealData sgNormals = this->sgMeshData->GetNormals()->GetData();
	const Simplygon::real * const normals = sgNormals.Data();

	// set up polygon -> triangles mapping
	std::vector<uint> polygonSizes(polygonCount);
	std::vector<uint> polygonFirstTriangleIds(polygonCount);

	// decode tris or quads/tris
	if( spCharArray sgQuadFlagsArray = this->sgMeshData->GetQuadFlags() )
	{
		// quads & tris: count up each polygon and map to triangles
		spCharData sgQuadFlags = sgQuadFlagsArray->GetData();
		MValidate( !sgQuadFlags.IsNullOrEmpty(), MStatus::kInvalidParameter, "The quad flags data field is invalid, either nullptr or empty." );
		const char *quadFlags = sgQuadFlags.Data();

		// set up all the polygons
		uint destPolygonInx = 0;
		for( uint tid = 0; tid < triangleCount; )
		{
			MSanityCheck( destPolygonInx < polygonCount ); 

			const auto quadFlag = quadFlags[tid];
			if( quadFlag == SG_QUADFLAG_TRIANGLE )
			{
				// step the dest 3 indices, and the source 1 triangle
				polygonSizes[destPolygonInx] = 3;
				polygonFirstTriangleIds[destPolygonInx] = tid;
				++tid;
			}
			else if( quadFlag == SG_QUADFLAG_FIRST )
			{
				// step the dest 4 indices, and the source 2 triangles (because of quad)
				polygonSizes[destPolygonInx] = 4;
				polygonFirstTriangleIds[destPolygonInx] = tid;
				tid += 2;
			}
			else
			{
				MValidate( false, MStatus::kInvalidParameter, "The quad flags have invalid formatting, or is out of sync.")
			}

			++destPolygonInx;
		}

		MValidate( destPolygonInx == polygonCount, MStatus::kInvalidParameter, "The input quad data does not match the expected data in the MFnMesh" );
	}
	else
	{
		// no quads, only tris: straight 1:1 mapping of the corners

		// validate assumption about 1:1 mapping (all polys are tris, and number of face vtx must equal triangle corners)
		MValidate( polygonCount == triangleCount, MStatus::kInvalidParameter, "No quad information exists in the returned mesh, but not all polygons are triangles." );

		for( uint inx=0; inx<polygonCount; ++inx )
		{
			polygonSizes[inx] = 3;
			polygonFirstTriangleIds[inx] = inx;
		}
	}

	MVectorArray mNormals( faceVertexCount );  // all the normals
	MIntArray mPolygonIds( faceVertexCount ); // the polygon a specific normal should be placed in
	MIntArray mVertexIds( faceVertexCount ); // the vertex a specific normal should be placed in
	
	// copy the normals to the polygons of the mesh 
	for( uint pid = 0, faceVertexInx = 0; pid < polygonCount; ++pid )
	{
		const auto polygonSize = polygonSizes[pid];
		const auto polygonFirstTriangleId = polygonFirstTriangleIds[pid];
		const auto baseCornerIndex = polygonFirstTriangleId*3;

		// NOTE: the maya plugin seems to write out the quads rotated: (5-0-1-2), adhere to this order
		static constexpr uint triangleIndices[3] = {0,1,2};
		static constexpr uint quadIndices[4] = {5,0,1,2};
		const uint * const polygonCornerIndices = ( polygonSize == 3 ) ? triangleIndices : quadIndices;

		for( uint c = 0; c < polygonSize; ++c, ++faceVertexInx )
		{
			const uint cid = baseCornerIndex + polygonCornerIndices[c];
			const Simplygon::rid vid = vertexIds[ cid ];
	
			// set the normal index of the triangle
			mPolygonIds[faceVertexInx] = pid;
			mVertexIds[faceVertexInx] = vid;
			mNormals[faceVertexInx] = MVector( &normals[cid*3] );
		}
	}
	MCheckStatus( this->MayaMesh.setFaceVertexNormals( mNormals, mPolygonIds, mVertexIds ), "could not apply face vertex normals" );

	// mark edges as smooth/hard
	MItMeshEdge edgeIter( this->MayaMesh.object(), &status );
	if( status )
	{
		const uint edgeCount = this->MayaMesh.numEdges();
		MIntArray edgeIds( edgeCount );
		MIntArray edgeSmooths( edgeCount );
		MIntArray faceList;
	
		// iterate the edges, and mark all which are continuous as smooth
		while( !edgeIter.isDone( &status ) && status )
		{
			const uint edgeIndex = edgeIter.index();
			edgeIds[edgeIndex] = edgeIndex;

			// assume hard edge
			edgeSmooths[edgeIndex] = 0;

			// get the connected faces, and check if the normals match
			edgeIter.getConnectedFaces(faceList,&status);
			if( status )
			{
				// only consider manifold edges (an edge with exactly 2 polygons), these are the only one which can possibly be smooth
				if( faceList.length() == 2 )
				{
					if( isEdgeSmooth( faceList[0], faceList[1], polygonSizes.data(), polygonFirstTriangleIds.data(), vertexIds, normals ) )
						edgeSmooths[edgeIndex] = 1;
				}
			}

			edgeIter.next();
		}

		MCheckStatus( this->MayaMesh.setEdgeSmoothings(edgeIds, edgeSmooths), "setEdgeSmoothings() failed" );
		MCheckStatus( this->MayaMesh.cleanupEdgeSmoothing(), "cleanupEdgeSmoothing() failed." );
	}

	return MStatus::kSuccess;
}

void MeshNode::WritebackNormals_Deprecated()
{
	const uint vertexCount = this->sgMeshData->GetVertexCount();
	const uint triangleCount = this->sgMeshData->GetTriangleCount();
	const uint cornerCount = triangleCount * 3;

	spRidArray sgVertexIds = this->sgMeshData->GetVertexIds();
	spRealArray sgNormals = this->sgMeshData->GetNormals();

	MVectorArray mNormals( cornerCount );  // all the normals
	MIntArray mTriangleIds( cornerCount ); // the triangle a specific normal should be placed in
	MIntArray mVertexIds( cornerCount );   // the vertex a specific normal should be placed in

	// for all triangles
	for( uint tid = 0; tid < triangleCount; ++tid )
	{
		// for all corners
		for( uint c = 0; c < 3; ++c )
		{
			const int cid = tid * 3 + c;
			const int vid = sgVertexIds->GetItem( cid );

			// get corner normal
			spRealData sgNormal = sgNormals->GetTuple( cid );

			mNormals[ cid ] = MVector( sgNormal.Data() );
			mTriangleIds[ cid ] = tid;
			mVertexIds[ cid ] = vid;
		}
	}

	const MStatus bNormalSet = this->MayaMesh.setFaceVertexNormals( mNormals, mTriangleIds, mVertexIds );
}

MStatus MeshNode::DeleteModifiedMeshDatas()
{
	MGlobal::clearSelectionList();

	// delete all the nodes created in the WritebackGeometryData calls
	for( size_t meshIndex = 0; meshIndex < this->meshLODs.size(); ++meshIndex )
	{
		MGlobal::select( (const MDagPath&)this->meshLODs[ meshIndex ].LODNode, MObject::kNullObj, MGlobal::kReplaceList );

		this->meshLODs[ meshIndex ].LODNode = MDagPath();
		this->meshLODs[ meshIndex ].LODNodeShape = MDagPath();
		MGlobal::executeCommand( "delete;" );
	}

	this->meshLODs.clear();

	return MStatus::kSuccess;
}

MStatus MeshNode::AddToGenericSets()
{
	for( size_t setIndex = 0; setIndex < this->GenericSets.size(); ++setIndex )
	{
		MIntArray mSelectedTriangles;

		// setup the selected triangles
		for( size_t q = 0; q < GenericSets[ setIndex ].PolygonIndices.size(); ++q )
		{
			const rid sgOriginalId = GenericSets[ setIndex ].PolygonIndices[ q ];
			const std::map<rid, rid>::const_iterator& polygonMapIterator = this->PolygonBackMapping.find( sgOriginalId );
			if( polygonMapIterator == this->PolygonBackMapping.end() )
			{
				continue; // removed triangle, skip
			}

			const rid sgReducedId = polygonMapIterator->second;
			mSelectedTriangles.append( sgReducedId );
		}

		// if no triangles are left, skip
		if( mSelectedTriangles.length() == 0 )
		{
			continue;
		}

		// setup the component set
		MFnSingleIndexedComponent mFaceIndices;
		MObject mFaces = mFaceIndices.create( MFn::kMeshPolygonComponent );
		if( !mFaceIndices.addElements( mSelectedTriangles ) )
		{
			return MStatus::kFailure;
		}

		// apply to the components
		MString mCommand = "sets -add " + MString( this->GenericSets[ setIndex ].Name.c_str() );
		::ExecuteSelectedObjectCommand( mCommand, this->modifiedNode, mFaces );
	}

	return MStatus::kSuccess;
}

void MeshNode::SetupBackMapping()
{
	this->VertexBackMapping.clear();
	this->PolygonBackMapping.clear();

	// vertex mapping
	spRidArray sgOriginalVertexIds = spRidArray::SafeCast( this->sgMeshData->GetUserVertexField( "OriginalIds" ) );
	if( sgOriginalVertexIds.IsNull() )
		return;

	const uint sgOriginalVertexCount = sgOriginalVertexIds->GetItemCount();
	for( uint vid = 0; vid < sgOriginalVertexCount; ++vid )
	{
		const rid sgOriginalId = sgOriginalVertexIds->GetItem( vid );
		this->VertexBackMapping.insert( std::pair<rid, rid>( sgOriginalId, vid ) );
	}

	// triangle mapping
	spRidArray sgOriginalTriangleIds = spRidArray::SafeCast( this->sgMeshData->GetUserTriangleField( "OriginalIds" ) );
	if( sgOriginalTriangleIds.IsNull() )
		return;

	const uint sgOriginalTriangleCount = sgOriginalTriangleIds->GetItemCount();
	for( uint tid = 0; tid < sgOriginalTriangleCount; ++tid )
	{
		const rid sgOriginalId = sgOriginalTriangleIds->GetItem( tid );
		this->PolygonBackMapping.insert( std::pair<rid, rid>( sgOriginalId, tid ) );
	}
}

void MeshNode::SetupBackMapping_Quad()
{
	this->VertexBackMapping.clear();
	this->PolygonBackMapping.clear();

	// vertex mapping
	spRidArray sgOriginalVertexIds = spRidArray::SafeCast( this->sgMeshData->GetUserVertexField( "OriginalIds" ) );
	if( sgOriginalVertexIds.IsNull() )
		return;

	const uint sgOriginalVertexCount = sgOriginalVertexIds->GetItemCount();
	for( uint sgReducedVertexIndex = 0; sgReducedVertexIndex < sgOriginalVertexCount; ++sgReducedVertexIndex )
	{
		const rid sgOriginalVertexIndex = sgOriginalVertexIds->GetItem( sgReducedVertexIndex );
		this->VertexBackMapping.insert( std::pair<rid, rid>( sgOriginalVertexIndex, sgReducedVertexIndex ) );
	}

	// triangle mapping
	spCharArray sgQuadFlags = this->sgMeshData->GetQuadFlags();
	spRidArray sgOriginalTriangleIds = spRidArray::SafeCast( this->sgMeshData->GetUserTriangleField( "OriginalIds" ) );
	if( sgOriginalTriangleIds.IsNull() )
		return;

	uint sgReducedPolygonIndex = 0;
	const uint sgOriginalTriangleCount = sgOriginalTriangleIds->GetItemCount();
	for( uint sgReducedTriangleIndex = 0; sgReducedTriangleIndex < sgOriginalTriangleCount; ++sgReducedTriangleIndex )
	{
		// see if triangle is triangle or part of quad
		const char cQuadFlag = sgQuadFlags.GetItem( sgReducedTriangleIndex );
		if( cQuadFlag == SG_QUADFLAG_TRIANGLE || cQuadFlag == SG_QUADFLAG_FIRST )
		{
			// if so, store original polygon id from custom field, per-polygon
			const rid sgOriginalTriangleIndex = sgOriginalTriangleIds->GetItem( sgReducedTriangleIndex );
			this->PolygonBackMapping.insert( std::pair<rid, rid>( sgOriginalTriangleIndex, sgReducedPolygonIndex++ ) );
		}
	}
}

MStatus MeshNode::ResetTweaks()
{
	MPlug mMeshTweakPlug;
	MPlug mTweak;
	MObject mTweakData;
	MStatus mStatus;

	mMeshTweakPlug = this->MayaMesh.findPlug( "pnts", true, &mStatus );
	if( !mMeshTweakPlug.isNull() )
	{
		const uint numElements = mMeshTweakPlug.numElements();

		// reset data
		for( uint i = 0; i < numElements; i++ )
		{
			mTweak = mMeshTweakPlug.elementByPhysicalIndex( i );
			if( !mTweak.isNull() )
			{
				MFloatVector value;

				value[ 0 ] = 0.f;
				value[ 1 ] = 0.f;
				value[ 2 ] = 0.f;
				mStatus = getFloat3asMObject( value, mTweakData );
				MSanityCheck( mStatus );

				mStatus = mTweak.setValue( mTweakData );
				MSanityCheck( mStatus );
			}
		}
	}

	return MStatus::kSuccess;
}

MStatus MeshNode::AddSkinning( spScene sgProcessedScene )
{
	MStatus mStatus;

	spRealArray sgBoneWeights = this->sgMeshData->GetBoneWeights();
	spRidArray sgBoneIds = this->sgMeshData->GetBoneIds();
	spSceneBoneTable sgBoneTable = sgProcessedScene->GetBoneTable();

	if( sgBoneIds.IsNull() || sgBoneWeights.IsNull() )
		return MStatus::kSuccess;

	const uint numBones = sgBoneTable->GetBonesCount();
	const uint vertexCount = this->sgMeshData->GetVertexCount();
	const uint bonesTupleSize = sgBoneIds->GetTupleSize();

	Scene* sceneHandler = this->cmd->GetSceneHandler();

	// list to hold all valid bone paths
	std::map<std::string, std::string> sgBonesInUseMap;

	// allocate variable to hold the bone ids per vertex
	for( uint vid = 0; vid < vertexCount; ++vid )
	{
		// get all bone ids per vertex
		spRidData sgBoneId = sgBoneIds->GetTuple( vid );

		for( uint i = 0; i < bonesTupleSize; ++i )
		{
			const int globalBoneId = sgBoneId[ i ];

			// if valid
			if( globalBoneId >= 0 && globalBoneId < (int)numBones )
			{
				spSceneBone sgBone = sgBoneTable->GetBone( globalBoneId );
				std::string sGlobalBoneId = sgBone->GetNodeGUID();

				const MDagPath& mMayaBonePath = sceneHandler != nullptr ? sceneHandler->SgBoneIDToMayaJoint( sGlobalBoneId ) : MDagPath();

				// use guid-mapped bones if possible,
				// otherwise, fallback to name-based search
				if( mMayaBonePath.isValid() )
				{
					sgBonesInUseMap.insert( std::pair<std::string, std::string>( sGlobalBoneId, mMayaBonePath.fullPathName().asChar() ) );
				}
				else
				{
					sgBonesInUseMap.insert( std::pair<std::string, std::string>( sGlobalBoneId, sgBone->GetName() ) );
				}
			}
		}
	}

	// if no bones, quit
	if( sgBonesInUseMap.size() == 0 )
		return MStatus::kSuccess;

	// clear selection, then add all bones and the modified node
	MGlobal::select( MObject::kNullObj, MGlobal::kReplaceList );
	MGlobal::clearSelectionList();

	bool bAllBonesSkipped = true;
	for( std::map<std::string, std::string>::iterator boneMapIterator = sgBonesInUseMap.begin(); boneMapIterator != sgBonesInUseMap.end(); ++boneMapIterator )
	{
		MDagPath mBoneDagPath;
		mStatus = GetPathToNamedObject( boneMapIterator->second.c_str(), mBoneDagPath );
		if( !mStatus )
		{
			std::string sWarningMessage = "AddSkinning - Could not resolve joint (";
			sWarningMessage += boneMapIterator->second + ") for mesh (";
			sWarningMessage += std::string( this->MayaMesh.fullPathName().asChar() ) + "), ignoring.";
			MGlobal::displayWarning( sWarningMessage.c_str() );
			continue;
		}

		if( mBoneDagPath.isValid() )
		{
			mStatus = MGlobal::select( (const MDagPath&)mBoneDagPath, MObject::kNullObj );
			if( !mStatus )
				return mStatus;

			bAllBonesSkipped = false;
		}
	}

	if( bAllBonesSkipped )
	{
		std::string sWarningMessage = "AddSkinning - Skipping generation of SkinCluster for mesh (";
		sWarningMessage += std::string( this->MayaMesh.fullPathName().asChar() ) + ") due to unmapped joints.";
		MGlobal::displayWarning( sWarningMessage.c_str() );
		return MStatus::kSuccess;
	}

	mStatus = MGlobal::select( (const MDagPath&)this->modifiedNodeShape, MObject::kNullObj );
	if( !mStatus )
		return mStatus;

	// Maya 2024 and 2025 has a bug where dagPose command on models with 2 or more skinclusters
#if MAYA_APP_VERSION != 2024 && MAYA_APP_VERSION != 2025
	mStatus = ExecuteCommand( MString( "dagPose -restore -bindPose" ) );
#else
	std::string sWarningMessage = "AddSkinning - 'dagPose -restore -bindpose' is broken in Maya 2024 and 2025, using current pose instead!";
	MGlobal::displayWarning( sWarningMessage.c_str() );
#endif

	// create the skinCluster
	MStringArray mSkinClusterNameArray;
	MString mSkinClusterName;

	char cMaxInfluenses[ 20 ];
	sprintf( cMaxInfluenses, "%u", bonesTupleSize );
	mStatus = ExecuteCommand( MString( "skinCluster -tsb -mi " ) + MString( cMaxInfluenses ), mSkinClusterNameArray );
	if( !mStatus )
		return mStatus;

	mSkinClusterName = mSkinClusterNameArray[ 0 ];
	const char* cSkinClusterName = mSkinClusterName.asChar();
	MGlobal::selectByName( mSkinClusterName, MGlobal::kReplaceList );

	MSelectionList mSelectionList;
	MGlobal::getActiveSelectionList( mSelectionList );
	MObject mSelectedNode = MObject::kNullObj;

	if( !mSelectionList.isEmpty() )
	{
		mSelectionList.getDependNode( 0, mSelectedNode );
	}

	if( mSelectedNode == MObject::kNullObj )
	{
		return MStatus::kFailure;
	}

	MFnSkinCluster mSkinCluster( mSelectedNode, &mStatus );
	MDagPathArray mInfluenceDagPaths;
	const uint numInfluences = mSkinCluster.influenceObjects( mInfluenceDagPaths, &mStatus );

	if( this->cmd->UseOldSkinningMethod() )
	{
		const bool zeroWeights = true;
		if( zeroWeights )
		{
			MItMeshVertex mItVert( modifiedNodeShape, MObject::kNullObj, &mStatus );
			MFnSingleIndexedComponent mSelVerts;
			MObject mSelVertsObject = mSelVerts.create( MFn::kMeshVertComponent, &mStatus );

			MIntArray mIndices( numInfluences );
			MDoubleArray mWeights( numInfluences );

			// fetch bones and store zeroed out weights
			for( uint i = 0; i < numInfluences; ++i )
			{
				MString mInfluencePath = mInfluenceDagPaths[ i ].fullPathName();
				const uint mInfluenceIndex = mSkinCluster.indexForInfluenceObject( mInfluenceDagPaths[ i ] );

				mIndices[ i ] = (int)mInfluenceIndex;
				mWeights[ i ] = 0.0;
			}

			// store vertex ids
			MIntArray mSelectedVertices( vertexCount );
			for( uint vid = 0; vid < vertexCount; ++vid )
			{
				mSelectedVertices[ vid ] = vid;
			}

			mSelVerts.addElements( mSelectedVertices );

			// update weights with new information
			mSkinCluster.setWeights( this->modifiedNodeShape, mSelVertsObject, mIndices, mWeights, false, 0 );
		}

		MItMeshVertex mItVert( modifiedNodeShape, MObject::kNullObj, &mStatus );

		for( uint vid = 0; vid < vertexCount; ++vid )
		{
			MFnSingleIndexedComponent mSelVert;
			MObject selVertsObject = mSelVert.create( MFn::kMeshVertComponent, &mStatus );

			mSelVert.addElement( vid );

			spRealData sgBoneWeight = sgBoneWeights->GetTuple( vid );
			spRidData sgBoneId = sgBoneIds->GetTuple( vid );

			MIntArray mIndices( bonesTupleSize );
			MDoubleArray mWeights( bonesTupleSize );

			// Assign the weights to the skin vertex
			uint counter = 0;
			for( uint boneIndex = 0; boneIndex < bonesTupleSize; boneIndex++ )
			{
				const int globalBoneIndex = sgBoneId[ boneIndex ];
				const double globalBoneWeight = sgBoneWeight[ boneIndex ];

				if( globalBoneIndex >= 0 && globalBoneIndex < (int)numBones )
				{
					spSceneBone sgBone = sgBoneTable->GetBone( globalBoneIndex );
					std::string sGlobalBoneID = sgBone->GetNodeGUID();

					MDagPath mBoneDagPath;
					mStatus = GetPathToNamedObject( sgBone->GetName().c_str(), mBoneDagPath );

					int mMayaGlobalBoneIndex = mSkinCluster.indexForInfluenceObject( mBoneDagPath );

					mIndices[ counter ] = mMayaGlobalBoneIndex;
					mWeights[ counter ] = globalBoneWeight;
					counter++;
				}
			}

			mIndices.setLength( counter );
			mWeights.setLength( counter );

			mSkinCluster.setWeights( this->modifiedNodeShape, selVertsObject, mIndices, mWeights, false, 0 );
		}
	}
	else
	{
		const uint BatchSize = vertexCount;

		MIntArray mIndices( numInfluences );
		MDoubleArray mWeights( numInfluences * BatchSize );

		// reset indices and weights
		for( uint i = 0; i < numInfluences; ++i )
		{
			mIndices[ i ] = i;
		}

		for( uint i = 0; i < numInfluences * BatchSize; ++i )
		{
			mWeights[ i ] = 0.0;
		}

		uint BatchIndex = 0;
		uint BatchStart = 0;

		MFnSingleIndexedComponent mSelVerts;
		MObject selVertsObject = mSelVerts.create( MFn::kMeshVertComponent, &mStatus );

		for( uint vid = 0; vid < vertexCount; ++vid )
		{
			mSelVerts.addElement( vid );

			spRealData sgBoneWeight = sgBoneWeights->GetTuple( vid );
			spRidData sgBoneId = sgBoneIds->GetTuple( vid );

			// Find max weight for this vertex
			double totalBoneWeight = 0.0;
			for( uint boneIndex = 0; boneIndex < bonesTupleSize; ++boneIndex )
			{
				const int globalBoneIndex = sgBoneId[ boneIndex ];
				if( globalBoneIndex >= 0 && globalBoneIndex < (int)numBones )
				{
					totalBoneWeight += (double)sgBoneWeight[ boneIndex ];
				}
			}

			if( totalBoneWeight > 0.f )
			{
				// Assign the weights to the skin vertex
				for( uint boneIndex = 0; boneIndex < bonesTupleSize; ++boneIndex )
				{
					const int globalBoneIndex = sgBoneId[ boneIndex ];
					if( globalBoneIndex >= 0 && globalBoneIndex < (int)numBones )
					{
						spSceneBone sgBone = sgBoneTable->GetBone( globalBoneIndex );
						std::string sGlobalBoneID = sgBone->GetNodeGUID();

						MDagPath mBoneDagPath;
						std::map<std::string, std::string>::iterator boneMapIterator = sgBonesInUseMap.find( sGlobalBoneID );

						mStatus = boneMapIterator != sgBonesInUseMap.end() ? GetPathToNamedObject( boneMapIterator->second.c_str(), mBoneDagPath )
						                                                   : GetPathToNamedObject( sgBone->GetName().c_str(), mBoneDagPath );

						const int mMayaGlobalBoneIndex = mSkinCluster.indexForInfluenceObject( mBoneDagPath );

						const double globalBoneWeight = (double)sgBoneWeight[ boneIndex ];
						mWeights[ BatchStart + mMayaGlobalBoneIndex ] = globalBoneWeight / totalBoneWeight;
					}
				}
			}

			BatchIndex += 1;
			BatchStart += numInfluences;

			if( BatchIndex == BatchSize )
			{
				// Set them on the skin cluster
				mSkinCluster.setWeights( this->modifiedNodeShape, selVertsObject, mIndices, mWeights, false, 0 );

				// Reset all the batch weights
				for( uint i = 0; i < numInfluences * BatchSize; ++i )
				{
					mWeights[ i ] = 0.0;
				}

				// Clear the batch data
				selVertsObject = mSelVerts.create( MFn::kMeshVertComponent, &mStatus );
				BatchStart = 0;
				BatchIndex = 0;
			}
		}

		if( BatchIndex != 0 )
		{
			mWeights.setLength( BatchIndex * numInfluences );

			// Set them on the skin cluster
			mSkinCluster.setWeights( this->modifiedNodeShape, selVertsObject, mIndices, mWeights, false, 0 );
		}
	}

	return MStatus::kSuccess;
}

void MeshNode::CopyColorFieldToWeightsField( spRealArray sgColors, bool RemoveOriginalField )
{
	// add weights field
	spRealArray sgWeights = sgMeshData->GetVertexWeights();
	if( sgWeights.IsNull() )
	{
		sgMeshData->AddVertexWeights();
		sgWeights = sgMeshData->GetVertexWeights();

		for( uint i = 0; i < sgWeights->GetItemCount(); ++i )
		{
			sgWeights->SetItem( i, 1.f );
		}
	}

	// convert vertex color data to intensity and assign as weight
	spRidArray sgVertexIds = sgMeshData->GetVertexIds();

	// get tuple size
	const uint tupleSize = sgColors->GetTupleSize();

	// check at most 3 channels
	uint checkTupleCount = 3;
	if( checkTupleCount > tupleSize )
	{
		checkTupleCount = tupleSize;
	}

	// per triangle
	for( uint t = 0; t < sgMeshData->GetTriangleCount(); ++t )
	{
		// per vertex in triangle
		for( uint v = 0; v < 3; ++v )
		{
			// tuple size must be > 1 (otherwise ignore)
			if( tupleSize > 1 )
			{
				// get vertex color
				spRealData sgColor = sgColors->GetTuple( t * 3 + v );

				// calculate intensity by choosing largest component (except alpha)
				real intensity = sgColor[ 0 ];
				for( uint s = 1; s < checkTupleCount; ++s )
				{
					if( sgColor[ s ] > intensity )
					{
						intensity = sgColor[ s ];
					}
				}

				// clamp the value, make into range 1->2
				if( intensity < 0 )
				{
					intensity = 0;
				}
				else if( intensity > 1 )
				{
					intensity = 1;
				}

				// retrieve the current weight of the vertex
				const rid vid = sgVertexIds->GetItem( t * 3 + v );
				sgWeights->SetItem( vid, intensity );
			}
		}
	}
}

float MeshNode::GetSceneMeshesRadius( spScene sgScene )
{
	float result = 0.f;
	const rid ssId = sgScene->SelectNodes( "SceneMesh" );
	spExtents extents = sg->CreateExtents();

	if( sgScene->CalculateExtentsOfSelectionSetId( extents, ssId ) )
		result = extents->GetBoundingSphereRadius();

	sgScene->GetSelectionSetTable()->RemoveSelectionSet( ssId );

	return result;
}

MString MeshNode::GetOriginalNodeName()
{
	return originalNodeName;
}

MDagPath MeshNode::GetOriginalNodeShape()
{
	return originalNodeShape;
}

MStatus MeshNode::ExtractBlendShapeData()
{
	MString mMeshNodeName = originalNode.fullPathName();
	MString mMeshNodeShapeName = originalNodeShape.fullPathName();

	if( mMeshNodeName == nullptr || mMeshNodeShapeName == nullptr )
		return MStatus::kSuccess;

	// fetch all blend shapes from the scene
	MItDependencyNodes mDependencyIterator( MFn::kBlendShape );
	while( !mDependencyIterator.isDone() )
	{
		// attach the function set to the object
		MFnBlendShapeDeformer mBlendShapeDeformer( mDependencyIterator.thisNode() );

		// get a list of base objects
		MObjectArray mBaseObjects;
		mBlendShapeDeformer.getBaseObjects( mBaseObjects );

		// loop through each base object connected to the blend shape
		for( uint i = 0; i < mBaseObjects.length(); ++i )
		{
			// get the base shape
			MObject mBase = mBaseObjects[ i ];
			MObject mTmp;

			MDagPathArray mAllDagPaths;
			MDagPath::getAllPathsTo( mBase, mAllDagPaths );

			bool found = false;
			for( uint d = 0; d < mAllDagPaths.length(); ++d )
			{
				// MString mPath = mAllDagPaths[d].fullPathName();
				if( mAllDagPaths[ d ] == originalNodeShape )
				{
					found = true;
					OutputBaseTargetWeights( mBlendShapeDeformer, mBase );
					break;
				}
			}

			if( !found )
			{
				// something went wrong
				// return MStatus::kFailure;
			}
		}

		// get next blend shapes
		mDependencyIterator.next();
	}

	return MStatus::kSuccess;
}

std::vector<BlendShapeInformation> BlendShapePlugs;
void MeshNode::OutputBaseTargetWeights( MFnBlendShapeDeformer& mBlendShapeDeformer, MObject& mBase )
{
	char cBuffer[ MAX_PATH ] = { 0 };

	// fetch number of weights
	const uint nWeights = mBlendShapeDeformer.numWeights();

	MIntArray mIntArray;
	const MStatus mResult = mBlendShapeDeformer.weightIndexList( mIntArray );

	uint blendCount = 0;
	for( uint i = 0; i < nWeights; ++i )
	{
		const uint realIndex = mIntArray[ i ];
		if( blendCount < ( realIndex + 1 ) )
		{
			blendCount = ( realIndex + 1 );
		}
	}

	const float en = 1.0f; // fn.envelope();

	BlendShape theBlendShape;
	theBlendShape.Init( mBlendShapeDeformer.name(), en, blendCount );
	mBlendShapeDeformer.setEnvelope( 1.0f );

	// zero out all weights
	for( uint i = 0; i < nWeights; ++i )
	{
		const int realIndex = mIntArray[ i ];
		theBlendShape.BlendWeights[ i ].weight = mBlendShapeDeformer.weight( realIndex );
		mBlendShapeDeformer.setWeight( realIndex, 0.0f );
	}

	for( uint i = 0; i < nWeights; ++i )
	{
		const int realIndex = mIntArray[ i ];

		// maximum blend for this weight element
		mBlendShapeDeformer.setWeight( realIndex, 1.0f );

		// create target field
		sprintf( cBuffer, "%s%u", "BlendShapeTargetVertexField", BlendShapeCount ); //(previously realIndex)
		theBlendShape.BlendWeights[ i ].fieldName = MString( cBuffer );

		MString mCommand = MString( "aliasAttr -q " ) + theBlendShape.name + MString( ".w[" ) + realIndex + "]";
		MString mWeightName;
		ExecuteCommand( mCommand, mWeightName );

		theBlendShape.BlendWeights[ i ].weightName = mWeightName;
		theBlendShape.BlendWeights[ i ].fieldIndex = i;
		theBlendShape.BlendWeights[ i ].realIndex = realIndex;
		theBlendShape.BlendWeights[ i ].globalIndex = BlendShapeCount;
		BlendShapeCount++;

		spRealArray sgWeights = spRealArray::SafeCast( this->sgMeshData->AddBaseTypeUserVertexField( Simplygon::EBaseTypes::TYPES_ID_REAL, cBuffer, 3 ) );
		sgWeights->SetAlternativeName( theBlendShape.name.asChar() );

		// extract target data
		OutputTarget( mBase, sgWeights );

		// restore weight to zero
		mBlendShapeDeformer.setWeight( realIndex, 0.0f );
	}

	// restore envelope
	mBlendShapeDeformer.setEnvelope( 0.0f );

	// restore all weights
	for( uint i = 0; i < nWeights; ++i )
	{
		const int realIndex = mIntArray[ i ];
		mBlendShapeDeformer.setWeight( realIndex, theBlendShape.BlendWeights[ i ].weight );
	}

	this->blendShape.push_back( theBlendShape );
}

void MeshNode::OutputTarget( MObject& mTarget, spRealArray sgWeights )
{
	std::vector<MPoint> mList;
	MItGeometry mGeometryIterator( mTarget );

	while( !mGeometryIterator.isDone() )
	{
		MPoint mP = mGeometryIterator.position();
		mList.push_back( mP );

		mGeometryIterator.next();
	}

	spRealArray sgCoords = this->sgMeshData->GetCoords();
	spRidArray sgTriangleIndices = this->sgMeshData->GetVertexIds();

	const uint VertexCount = this->sgMeshData->GetVertexCount();

	for( uint vid = 0; vid < VertexCount; ++vid )
	{
		spRealData sgBlendWeight = sgCoords->GetTuple( vid );

		// make relative
		const MPoint& mP = mList[ vid ];
		const real weight[ 3 ] = { (float)mP.x - sgBlendWeight[ 0 ], (float)mP.y - sgBlendWeight[ 1 ], (float)mP.z - sgBlendWeight[ 2 ] };

		sgWeights->SetTuple( vid, weight );
	}
}

// global function to disable blend shapes while copying information
void DisableBlendShapes()
{
	MStatus mStatus;

	BlendShapePlugs.clear();
	MItDependencyNodes mBlendShapeDependencyIterator( MFn::kBlendShape );
	while( !mBlendShapeDependencyIterator.isDone() )
	{
		MFnBlendShapeDeformer mBlendShapeDeformer( mBlendShapeDependencyIterator.thisNode() );

		// get the envelope attribute plug
		MPlug mPlug = mBlendShapeDeformer.findPlug( "en", true, &mStatus );
		if( mStatus != MStatus::kSuccess )
			continue;

		// store result
		const float en = mPlug.asFloat();
		BlendShapePlugs.push_back( BlendShapeInformation( mPlug, en ) );

		// set to 0 to disable blending
		mPlug.setFloat( 0.0f );

		mBlendShapeDependencyIterator.next();
	}
}

void EnableBlendShapes()
{
	for( uint p = 0; p < BlendShapePlugs.size(); ++p )
	{
		BlendShapePlugs[ p ].GetPlug().setFloat( BlendShapePlugs[ p ].GetEnvelope() );
	}
}
