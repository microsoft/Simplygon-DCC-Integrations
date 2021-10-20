// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"

#include "MeshNode.h"
#include "MaterialNode.h"
#include "BakedMaterial.h"

#include "SimplygonCmd.h"

#include "HelperFunctions.h"
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
}

MeshNode::MeshNode( SimplygonCmd* cmd )
{
	this->cmd = cmd;
	this->materialHandler = nullptr;
	this->hasCreaseValues = false;
	this->postUpdate = false;
	this->BlendShapeCount = 0;
	this->originalNodeName = "";
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
	mStatus = ExecuteSelectedObjectCommand( "polyTriangulate -ch 0", this->modifiedNode, MObject::kNullObj );
	if( !mStatus )
	{
		return mStatus;
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

	// setup the color sets.
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
		MString mNodePath = this->modifiedNode.fullPathName();
		MGlobal::select( this->modifiedNode, MObject::kNullObj, MGlobal::kReplaceList );
		this->modifiedNode = MDagPath();

		MGlobal::executeCommand( "delete;" );

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
			MGlobal::select( mInfluenceDagPaths[ i ], MObject::kNullObj );
		}

		if( !this->cmd->UseCurrentPoseAsBindPose() )
		{
			ExecuteCommand( MString( "dagPose -restore -bindPose" ) );
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
		MPlug wlPlug = mDuplicatedSkinCluster.findPlug( "weightList" );
		MPlug wPlug = mDuplicatedSkinCluster.findPlug( "weights" );
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
		mMeshPlug = mModifiedNodeShapeDependencyNode.findPlug( MString( "outMesh" ), &mStatus );

		// Get its value at the specified Time.
		mStatus = mMeshPlug.getValue( mMeshData, MDGContext( mCurrentTime ) );

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

					const real tanTuple[ 3 ] = { mTan[ 0 ], mTan[ 1 ], mTan[ 2 ] };
					const real biTanTuple[ 3 ] = { mBiTan[ 0 ], mBiTan[ 1 ], mBiTan[ 2 ] };

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

MStatus MeshNode::ExtractCreaseData()
{
	if( !this->hasCreaseValues )
		return MStatus::kSuccess;

	MStatus mStatus;
	int prevIndex = 0;

	MItMeshEdge mMeshEdgeIterator( modifiedNode.node(), &mStatus );
	MItMeshPolygon mMeshPolyIterator( modifiedNode.node(), &mStatus );

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
			TriMeshSelectionSet selectionSet;
			selectionSet.Name = std::string( mSet.name().asChar() );

			// get the polygon indices, and store into vector
			MItMeshPolygon mMeshPolygonIterator( this->modifiedNodeShape, mComponents[ i ], &mStatus );
			if( !mStatus )
				return mStatus;

			while( !mMeshPolygonIterator.isDone() )
			{
				const uint triangleIndex = mMeshPolygonIterator.index();
				selectionSet.Triangles.push_back( triangleIndex );
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
	const uint cornerCount = triangleCount * 3;

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
		MObjectArray components;

		const int instanceNumber = this->modifiedNodeShape.instanceNumber();
		this->MayaMesh.getConnectedSetsAndMembers( instanceNumber, mSets, components, false );

		for( uint setIndex = 0; setIndex < mSets.length(); ++setIndex )
		{
			const MFn::Type mSetType = mSets[ setIndex ].apiType();
			const MFn::Type mSetComponentType = components[ setIndex ].apiType();

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
					MItMeshVertex mMeshVertexIterator( this->modifiedNodeShape, components[ setIndex ], &mStatus );
					if( !mStatus )
					{
						return mStatus;
					}

					while( !mMeshVertexIterator.isDone() )
					{
						const uint vertexId = mMeshVertexIterator.index();
						sgVertexLocks->SetItem( vertexId, true );
						mMeshVertexIterator.next();
					}
				}

				// check for edges
				if( mSetComponentType == MFn::kMeshEdgeComponent )
				{
					// get the vertex indices, and lock the vertices
					MItMeshEdge mMeshEdgeIterator( this->modifiedNodeShape, components[ setIndex ], &mStatus );
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
		bool* setTriangles = new bool[ vertexCount ];

		for( uint setIndex = 0; setIndex < GenericSets.size(); ++setIndex )
		{
			const TriMeshSelectionSet& selectionSet = GenericSets[ setIndex ];

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
			for( uint q = 0; q < selectionSet.Triangles.size(); ++q )
			{
				const rid tid = selectionSet.Triangles[ q ];

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
			for( uint i = 0; i < vertexCount; ++i )
			{
				sgVertexLocks->SetItem( i, false );
			}
		}

		std::string* sVertexSet = new std::string[ vertexCount ];

		for( uint mid = 0; mid < mMaterialNamesList.size(); ++mid )
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
			for( uint i = 0; i < vertexCount; ++i )
			{
				sVertexSet[ i ] = -1;
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
	MObject mNode = MObject::kNullObj;
	MPlug mNodePlug = mDependencyNode.findPlug( mPlugName );
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

	spRidArray sgVertexIds = this->sgMeshData->GetVertexIds();
	spRealArray sgCoords = this->sgMeshData->GetCoords();

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
	this->modifiedNode = MDagPath::getAPathTo( modifiedTransform );
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
		this->WritebackNormals();
		// this->WritebackNormals_Deprecated();
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
			inMemoryBlindData.ApplyBlindDataToMesh( this->MayaMesh, this->VertexBackMapping, this->TriangleBackMapping );
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
				spRealArray sgTargetCoords = spRealArray::SafeCast( this->sgMeshData->GetUserCornerField( bw.fieldName.asChar() ) );

				const bool bHasBlendShapeData = !sgTargetCoords.IsNull();
				if( bHasBlendShapeData )
				{
					// set up a vertex array
					MFloatPointArray mTargetBlendShapeVertexField;
					mTargetBlendShapeVertexField.setLength( vertexCount );

					for( uint tid = 0; tid < triangleCount; ++tid )
					{
						for( uint c = 0; c < 3; ++c )
						{
							const rid cid = tid * 3 + c;
							const rid vid = sgVertexIds->GetItem( cid );

							spRealData sgTargetCoord = sgTargetCoords->GetTuple( cid );
							spRealData sgCoordinate = sgCoords->GetTuple( vid );

							// the field is relative, add the vertex coord to it
							mTargetBlendShapeVertexField[ vid ] = MFloatPoint(
							    sgTargetCoord[ 0 ] + sgCoordinate[ 0 ], sgTargetCoord[ 1 ] + sgCoordinate[ 1 ], sgTargetCoord[ 2 ] + sgCoordinate[ 2 ] );
						}
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

					MDagPath mTargetDagPath = MDagPath::getAPathTo( mTargetTransform );
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

void MeshNode::WritebackNormals()
{
	const uint vertexCount = this->sgMeshData->GetVertexCount();
	const uint triangleCount = this->sgMeshData->GetTriangleCount();
	const uint cornerCount = triangleCount * 3;

	spRidArray sgVertexIds = this->sgMeshData->GetVertexIds();
	spRealArray sgNormals = this->sgMeshData->GetNormals();

	// collect all vertex normals
	VertexNormal* vertexNormals = new VertexNormal[ vertexCount ];
	for( uint vid = 0; vid < vertexCount; ++vid )
	{
		vertexNormals[ vid ].isInitialized = false;
	}

	// do all vertices of all triangles
	for( uint tid = 0; tid < triangleCount; ++tid )
	{
		for( uint c = 0; c < 3; ++c )
		{
			const uint cid = tid * 3 + c;
			const int vid = sgVertexIds->GetItem( cid );

			// set the normal for the face-vertex
			spRealData sgNormal = sgNormals->GetTuple( cid );

			// if not initialized, set up
			if( !vertexNormals[ vid ].isInitialized )
			{
				vertexNormals[ vid ].isInitialized = true;
				vertexNormals[ vid ].normal[ 0 ] = sgNormal[ 0 ];
				vertexNormals[ vid ].normal[ 1 ] = sgNormal[ 1 ];
				vertexNormals[ vid ].normal[ 2 ] = sgNormal[ 2 ];
				vertexNormals[ vid ].isPerVertex = true;
			}
			else if( vertexNormals[ vid ].isPerVertex )
			{
				// compare normals
				const double dot = vertexNormals[ vid ].normal[ 0 ] * sgNormal[ 0 ] + vertexNormals[ vid ].normal[ 1 ] * sgNormal[ 1 ] +
				                   vertexNormals[ vid ].normal[ 2 ] * sgNormal[ 2 ];

				if( dot < 0.99 || dot > 1.01 )
				{
					vertexNormals[ vid ].isPerVertex = false;
				}
			}
		}
	}

	MVectorArray mNormals;  // all the normals
	MIntArray mTriangleIds; // the triangle a specific normal should be placed in
	MIntArray mVertexIds;   // the vertex a specific normal should be placed in

	mNormals.setSizeIncrement( cornerCount );
	mTriangleIds.setSizeIncrement( cornerCount );
	mVertexIds.setSizeIncrement( cornerCount );

	// do all vertices of all triangles
	for( uint tid = 0; tid < triangleCount; ++tid )
	{
		for( uint c = 0; c < 3; ++c )
		{
			const uint cid = tid * 3 + c;
			const int vid = sgVertexIds->GetItem( cid );

			if( vertexNormals[ vid ].isInitialized && !vertexNormals[ vid ].isPerVertex )
			{
				// set the normal index of the triangle
				mTriangleIds.append( tid );
				mVertexIds.append( vid );

				// set the normal for the face-vertex
				spRealData sgNormal = sgNormals->GetTuple( cid );
				mNormals.append( MVector( sgNormal ) );
			}
		}
	}

	if( mNormals.length() > 0 )
	{
		this->MayaMesh.setFaceVertexNormals( mNormals, mTriangleIds, mVertexIds );
	}

	mNormals.clear();
	mTriangleIds.clear();
	mVertexIds.clear();

	mNormals.setSizeIncrement( vertexCount );
	mTriangleIds.setSizeIncrement( vertexCount );
	mVertexIds.setSizeIncrement( vertexCount );

	// set all per-vertex normals
	for( uint vid = 0; vid < vertexCount; ++vid )
	{
		if( vertexNormals[ vid ].isInitialized && vertexNormals[ vid ].isPerVertex )
		{
			mVertexIds.append( vid );
			mNormals.append( MVector( vertexNormals[ vid ].normal ) );
		}
	}

	if( mNormals.length() > 0 )
	{
		this->MayaMesh.setVertexNormals( mNormals, mVertexIds );
	}

	delete[] vertexNormals;
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
		MGlobal::select( this->meshLODs[ meshIndex ].LODNode, MObject::kNullObj, MGlobal::kReplaceList );
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
		for( size_t q = 0; q < GenericSets[ setIndex ].Triangles.size(); ++q )
		{
			const rid sgOriginalId = GenericSets[ setIndex ].Triangles[ q ];
			const std::map<rid, rid>::const_iterator& triangleMapIterator = this->TriangleBackMapping.find( sgOriginalId );
			if( triangleMapIterator == this->TriangleBackMapping.end() )
			{
				continue; // removed triangle, skip
			}

			const rid sgReducedId = triangleMapIterator->second;
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
	this->TriangleBackMapping.clear();

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
		this->TriangleBackMapping.insert( std::pair<rid, rid>( sgOriginalId, tid ) );
	}
}

MStatus MeshNode::ResetTweaks()
{
	MPlug mMeshTweakPlug;
	MPlug mTweak;
	MObject mTweakData;
	MStatus mStatus;

	mMeshTweakPlug = this->MayaMesh.findPlug( "pnts" );
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

				// use guid-mappen bones if possible,
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
			mStatus = MGlobal::select( mBoneDagPath, MObject::kNullObj );
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

	mStatus = MGlobal::select( this->modifiedNodeShape, MObject::kNullObj );
	if( !mStatus )
		return mStatus;

	mStatus = ExecuteCommand( MString( "dagPose -restore -bindPose" ) );

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
		MFnBlendShapeDeformer mBlendShapeDeformer( mDependencyIterator.item() );

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

		spRealArray sgWeights = spRealArray::SafeCast( this->sgMeshData->AddBaseTypeUserCornerField( Simplygon::EBaseTypes::TYPES_ID_REAL, cBuffer, 3 ) );
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

	const uint TriangleCount = this->sgMeshData->GetTriangleCount();

	for( uint tid = 0; tid < TriangleCount; ++tid )
	{
		for( uint c = 0; c < 3; ++c )
		{
			const rid cid = tid * 3 + c;
			const rid vid = sgTriangleIndices->GetItem( cid );

			spRealData sgBlendWeight = sgCoords->GetTuple( vid );

			// make relative
			const MPoint& mP = mList[ vid ];
			const real weight[ 3 ] = { (float)mP.x - sgBlendWeight[ 0 ], (float)mP.y - sgBlendWeight[ 1 ], (float)mP.z - sgBlendWeight[ 2 ] };

			sgWeights->SetTuple( cid, weight );
		}
	}
}

// global function to disable blend shapes while copying information
void DisableBlendShapes()
{
	BlendShapePlugs.clear();
	MItDependencyNodes mBlendShapeDependencyIterator( MFn::kBlendShape );
	while( !mBlendShapeDependencyIterator.isDone() )
	{
		MFnBlendShapeDeformer mBlendShapeDeformer( mBlendShapeDependencyIterator.item() );

		// get the envelope attribute plug
		MPlug mPlug = mBlendShapeDeformer.findPlug( "en" );

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