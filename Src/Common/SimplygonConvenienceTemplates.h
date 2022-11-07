// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <vector>
#include <set>

namespace Simplygon {
enum EEnumerateNodeTypes
{
	SceneMesh = 0x01,
	SceneBone = 0x02,
	SceneCamera = 0x04,
	ScenePlane = 0x08,
	SceneLodGroup = 0x10,
	AnyNode = 0xffffffff
};

// Internal namespace, support templates for the convenience functions.
// Not recommended to use directly.
namespace internal {
// injects the data from any std::vector, as long as the tuple size match the array
template <class simplygonArr, class tupleType, class itemType, int itemsPerTuple>
bool InjectSTLVectorData( simplygonArr& destArray, const std::vector<tupleType>& srcVector )
{
	static_assert( sizeof( tupleType ) == ( sizeof( itemType ) * itemsPerTuple ),
	               "The sizes of the items do not match, tupleType size must be a itemsPerTuple multiple of itemType" );
	static_assert( std::is_default_constructible_v<tupleType>, "tupleType must be trivially default constructible" );
	static_assert( std::is_trivially_copyable_v<tupleType>, "tupleType must be trivially copyable" );
	static_assert( std::is_trivially_destructible_v<tupleType>, "tupleType must be trivially destructible" );

	// calculate the size of the array from the vector. please note that the tuple size of the output vector is allowed to differ
	// from the tuple size of this array, but it must be evenly divisible.
	const size_t vectorItemCount = srcVector.size() * itemsPerTuple;

#ifdef _DEBUG
	// calc the number of tuples in the dest array, make sure it is evenly divisible
	const size_t tupleSize = destArray->GetTupleSize();
	if( ( vectorItemCount % tupleSize ) != 0 )
	{
		// OutputDebugString("InjectSTLVectorData: Warning: The number of items are not evenly divisible with the tuple size, and the last tuple will not be
		// fully filled." );
	}
#endif
	const void* data = static_cast<const void*>( srcVector.data() );
	destArray->SetData( static_cast<const itemType*>( data ), (unsigned int)vectorItemCount );
	return true;
}

// extracts the data to any std::vector, as long as the tuple size match the array
template <class simplygonArr, class tupleType, class itemType, int itemsPerTuple>
bool ExtractSTLVectorData( std::vector<tupleType>& destVector, const simplygonArr& srcArray )
{
	static_assert( sizeof( tupleType ) == ( sizeof( itemType ) * itemsPerTuple ),
	               "The sizes of the items do not match, tupleType size must be a itemsPerTuple multiple of itemType" );
	static_assert( std::is_default_constructible_v<tupleType>, "tupleType must be trivially default constructible" );
	static_assert( std::is_trivially_copyable_v<tupleType>, "tupleType must be trivially copyable" );
	static_assert( std::is_trivially_destructible_v<tupleType>, "tupleType must be trivially destructible" );

	// get the data from the array
	auto srcData = srcArray->GetData();

	// calculate the size of the output vector. please note that the tuple size of the output vector might differ
	// from the tuple size of this array, but it must be evenly divisible.
	const size_t srcItemCount = srcData.GetItemCount();
	const size_t vectorSize = srcItemCount / itemsPerTuple;
	const size_t itemsToCopy = ( vectorSize * itemsPerTuple ); // for safety, truncates the value, but see debug check below
#ifdef _DEBUG
	if( itemsToCopy != (size_t)srcData.GetItemCount() )
	{
		// OutputDebugString("InjectSTLVectorData: Error: The number of items are not evenly divisible with the tuple size, and the last tuple will not be fully
		// filled." );
	}
#endif

	// resize the vector, and copy the data
	destVector.resize( vectorSize );
	memcpy( static_cast<void*>( destVector.data() ), static_cast<const void*>( srcData.Data() ), itemsToCopy * sizeof( itemType ) );
	return true;
}

static void EnumerateNodesRecursive( std::vector<spSceneNode>& dest, unsigned int nodeTypesFilter, spSceneNode node )
{
	// check if we want this kind of node
	bool addNode = ( nodeTypesFilter == Simplygon::EEnumerateNodeTypes::AnyNode );
	if( !addNode )
	{
		addNode |= ( ( nodeTypesFilter & Simplygon::EEnumerateNodeTypes::SceneMesh ) && ( spSceneMesh::SafeCast( node ).NonNull() ) );         // is a Mesh
		addNode |= ( ( nodeTypesFilter & Simplygon::EEnumerateNodeTypes::SceneBone ) && ( spSceneBone::SafeCast( node ).NonNull() ) );         // is a Bone
		addNode |= ( ( nodeTypesFilter & Simplygon::EEnumerateNodeTypes::SceneCamera ) && ( spSceneCamera::SafeCast( node ).NonNull() ) );     // is a Camera
		addNode |= ( ( nodeTypesFilter & Simplygon::EEnumerateNodeTypes::SceneLodGroup ) && ( spSceneLodGroup::SafeCast( node ).NonNull() ) ); // is a LodGroup
		addNode |= ( ( nodeTypesFilter & Simplygon::EEnumerateNodeTypes::ScenePlane ) && ( spScenePlane::SafeCast( node ).NonNull() ) );       // is a Plane
	}

	// add it if not filtered out
	if( addNode )
	{
		dest.emplace_back( node );
	}

	// process subscene
	unsigned int childCount = node->GetChildCount();
	for( unsigned int childIndex = 0; childIndex < childCount; ++childIndex )
	{
		spSceneNode child = node->GetChild( childIndex );
		Simplygon::internal::EnumerateNodesRecursive( dest, nodeTypesFilter, child );
	}
}

// enumerate template for scene nodes
template <class T, unsigned int F> std::vector<T> EnumerateAllSceneNodes( spScene scene )
{
	std::vector<spSceneNode> tempVec;
	Simplygon::internal::EnumerateNodesRecursive( tempVec, F, scene->GetRootNode() );

	// convert to the specific type
	std::vector<T> ret( tempVec.size() );
	for( size_t index = 0; index < tempVec.size(); ++index )
	{
		ret[ index ] = T::SafeCast( tempVec[ index ] );
	}
	return ret;
}

template <class T>
void EnumerateUpstreamShadingNetworkNodesRecursive( const spShadingNode node,
                                                    std::vector<T>& foundVec,
                                                    std::set<Simplygon::hIntf>& destSet,
                                                    const bool includeTheFirstNode = true )
{
	if( node.IsNull() )
		return;

	// make sure that we haven't already visited this node
	Simplygon::hIntf nodeHandle = node._GetHandle();
	if( destSet.find( nodeHandle ) != destSet.end() )
		return;
	destSet.insert( nodeHandle );

	// check if this can be cast to the wanted node type
	T obj = T::SafeCast( node );
	if( includeTheFirstNode && obj.NonNull() )
	{
		foundVec.emplace_back( obj );
	}

	// if we have upstream nodes, check them recursively
	spShadingFilterNode inputtableNode = spShadingFilterNode::SafeCast( node );
	if( inputtableNode )
	{
		// traverse to upstream nodes
		for( uint i = 0; i < inputtableNode->GetParameterCount(); ++i )
		{
			if( inputtableNode->GetParameterIsInputable( i ) )
			{
				spShadingNode inputNode = inputtableNode->GetInput( i );
				if( inputNode.NonNull() )
				{
					Simplygon::internal::EnumerateUpstreamShadingNetworkNodesRecursive<T>( inputNode, foundVec, destSet );
				}
			}
		}
	}
}

}

// methods to inject the data in an std::vector into a Simplygon array . Note that you can use any tuple type in the vector, as long as the tuple size and count
// is evenly divisible with the item count in the array
template <class tupleType, int itemsPerTuple> bool SetArrayFromVector( spBoolArray& destArray, const std::vector<tupleType>& srcVector )
{
	return internal::InjectSTLVectorData<spBoolArray, tupleType, bool, itemsPerTuple>( destArray, srcVector );
}
template <class tupleType, int itemsPerTuple> bool SetArrayFromVector( spCharArray& destArray, const std::vector<tupleType>& srcVector )
{
	return internal::InjectSTLVectorData<spCharArray, tupleType, char, itemsPerTuple>( destArray, srcVector );
}
template <class tupleType, int itemsPerTuple> bool SetArrayFromVector( spDoubleArray& destArray, const std::vector<tupleType>& srcVector )
{
	return internal::InjectSTLVectorData<spDoubleArray, tupleType, double, itemsPerTuple>( destArray, srcVector );
}
template <class tupleType, int itemsPerTuple> bool SetArrayFromVector( spFloatArray& destArray, const std::vector<tupleType>& srcVector )
{
	return internal::InjectSTLVectorData<spFloatArray, tupleType, float, itemsPerTuple>( destArray, srcVector );
}
template <class tupleType, int itemsPerTuple> bool SetArrayFromVector( spIntArray& destArray, const std::vector<tupleType>& srcVector )
{
	return internal::InjectSTLVectorData<spIntArray, tupleType, int, itemsPerTuple>( destArray, srcVector );
}
template <class tupleType, int itemsPerTuple> bool SetArrayFromVector( spLongArray& destArray, const std::vector<tupleType>& srcVector )
{
	return internal::InjectSTLVectorData<spLongArray, tupleType, long, itemsPerTuple>( destArray, srcVector );
}
template <class tupleType, int itemsPerTuple> bool SetArrayFromVector( spRealArray& destArray, const std::vector<tupleType>& srcVector )
{
	return internal::InjectSTLVectorData<spRealArray, tupleType, real, itemsPerTuple>( destArray, srcVector );
}
template <class tupleType, int itemsPerTuple> bool SetArrayFromVector( spRidArray& destArray, const std::vector<tupleType>& srcVector )
{
	return internal::InjectSTLVectorData<spRidArray, tupleType, rid, itemsPerTuple>( destArray, srcVector );
}
template <class tupleType, int itemsPerTuple> bool SetArrayFromVector( spShortArray& destArray, const std::vector<tupleType>& srcVector )
{
	return internal::InjectSTLVectorData<spShortArray, tupleType, short, itemsPerTuple>( destArray, srcVector );
}
template <class tupleType, int itemsPerTuple> bool SetArrayFromVector( spUnsignedCharArray& destArray, const std::vector<tupleType>& srcVector )
{
	return internal::InjectSTLVectorData<spUnsignedCharArray, tupleType, unsigned char, itemsPerTuple>( destArray, srcVector );
}
template <class tupleType, int itemsPerTuple> bool SetArrayFromVector( spUnsignedIntArray& destArray, const std::vector<tupleType>& srcVector )
{
	return internal::InjectSTLVectorData<spUnsignedIntArray, tupleType, unsigned int, itemsPerTuple>( destArray, srcVector );
}
template <class tupleType, int itemsPerTuple> bool SetArrayFromVector( spUnsignedLongArray& destArray, const std::vector<tupleType>& srcVector )
{
	return internal::InjectSTLVectorData<spUnsignedLongArray, tupleType, unsigned long, itemsPerTuple>( destArray, srcVector );
}
template <class tupleType, int itemsPerTuple> bool SetArrayFromVector( spUnsignedShortArray& destArray, const std::vector<tupleType>& srcVector )
{
	return internal::InjectSTLVectorData<spUnsignedShortArray, tupleType, unsigned short, itemsPerTuple>( destArray, srcVector );
}

// methods to extract all the data from a Simplygon array into an std::vector. Note that you can use any tuple type in the vector, as long as the tuple size and
// count is evenly divisible with the item count in the array
template <class tupleType, int itemsPerTuple> bool SetVectorFromArray( std::vector<tupleType>& destVector, const spBoolArray& srcArray )
{
	return internal::ExtractSTLVectorData<spBoolArray, tupleType, bool, itemsPerTuple>( destVector, srcArray );
}
template <class tupleType, int itemsPerTuple> bool SetVectorFromArray( std::vector<tupleType>& destVector, const spCharArray& srcArray )
{
	return internal::ExtractSTLVectorData<spCharArray, tupleType, char, itemsPerTuple>( destVector, srcArray );
}
template <class tupleType, int itemsPerTuple> bool SetVectorFromArray( std::vector<tupleType>& destVector, const spDoubleArray& srcArray )
{
	return internal::ExtractSTLVectorData<spDoubleArray, tupleType, double, itemsPerTuple>( destVector, srcArray );
}
template <class tupleType, int itemsPerTuple> bool SetVectorFromArray( std::vector<tupleType>& destVector, const spFloatArray& srcArray )
{
	return internal::ExtractSTLVectorData<spFloatArray, tupleType, float, itemsPerTuple>( destVector, srcArray );
}
template <class tupleType, int itemsPerTuple> bool SetVectorFromArray( std::vector<tupleType>& destVector, const spIntArray& srcArray )
{
	return internal::ExtractSTLVectorData<spIntArray, tupleType, int, itemsPerTuple>( destVector, srcArray );
}
template <class tupleType, int itemsPerTuple> bool SetVectorFromArray( std::vector<tupleType>& destVector, const spLongArray& srcArray )
{
	return internal::ExtractSTLVectorData<spLongArray, tupleType, long, itemsPerTuple>( destVector, srcArray );
}
template <class tupleType, int itemsPerTuple> bool SetVectorFromArray( std::vector<tupleType>& destVector, const spRealArray& srcArray )
{
	return internal::ExtractSTLVectorData<spRealArray, tupleType, real, itemsPerTuple>( destVector, srcArray );
}
template <class tupleType, int itemsPerTuple> bool SetVectorFromArray( std::vector<tupleType>& destVector, const spRidArray& srcArray )
{
	return internal::ExtractSTLVectorData<spRidArray, tupleType, rid, itemsPerTuple>( destVector, srcArray );
}
template <class tupleType, int itemsPerTuple> bool SetVectorFromArray( std::vector<tupleType>& destVector, const spShortArray& srcArray )
{
	return internal::ExtractSTLVectorData<spShortArray, tupleType, short, itemsPerTuple>( destVector, srcArray );
}
template <class tupleType, int itemsPerTuple> bool SetVectorFromArray( std::vector<tupleType>& destVector, const spUnsignedCharArray& srcArray )
{
	return internal::ExtractSTLVectorData<spUnsignedCharArray, tupleType, unsigned char, itemsPerTuple>( destVector, srcArray );
}
template <class tupleType, int itemsPerTuple> bool SetVectorFromArray( std::vector<tupleType>& destVector, const spUnsignedIntArray& srcArray )
{
	return internal::ExtractSTLVectorData<spUnsignedIntArray, tupleType, unsigned int, itemsPerTuple>( destVector, srcArray );
}
template <class tupleType, int itemsPerTuple> bool SetVectorFromArray( std::vector<tupleType>& destVector, const spUnsignedLongArray& srcArray )
{
	return internal::ExtractSTLVectorData<spUnsignedLongArray, tupleType, unsigned long, itemsPerTuple>( destVector, srcArray );
}
template <class tupleType, int itemsPerTuple> bool SetVectorFromArray( std::vector<tupleType>& destVector, const spUnsignedShortArray& srcArray )
{
	return internal::ExtractSTLVectorData<spUnsignedShortArray, tupleType, unsigned short, itemsPerTuple>( destVector, srcArray );
}

// ------------------------------------------------------------------------
// enumeration of scene nodes

// find all nodes in the scene, filtered on the filters
inline void EnumerateSceneNodes( std::vector<spSceneNode>& dest,
                                 spScene scene,
                                 unsigned int nodeTypesFilter = (unsigned int)Simplygon::EEnumerateNodeTypes::AnyNode,
                                 spSceneNode parentNode = Simplygon::NullPtr )
{
	dest.clear();
	Simplygon::internal::EnumerateNodesRecursive( dest, nodeTypesFilter, ( parentNode.NonNull() ) ? ( parentNode ) : ( scene->GetRootNode() ) );
}

// templates for finding specific node types, using the node type as parameter
template <class T> std::vector<T> EnumerateAllSceneNodes( spScene scene );
template <> inline std::vector<spSceneMesh> EnumerateAllSceneNodes<spSceneMesh>( spScene scene )
{
	return Simplygon::internal::EnumerateAllSceneNodes<spSceneMesh, Simplygon::EEnumerateNodeTypes::SceneMesh>( scene );
}
template <> inline std::vector<spSceneBone> EnumerateAllSceneNodes<spSceneBone>( spScene scene )
{
	return Simplygon::internal::EnumerateAllSceneNodes<spSceneBone, Simplygon::EEnumerateNodeTypes::SceneBone>( scene );
}
template <> inline std::vector<spSceneCamera> EnumerateAllSceneNodes<spSceneCamera>( spScene scene )
{
	return Simplygon::internal::EnumerateAllSceneNodes<spSceneCamera, Simplygon::EEnumerateNodeTypes::SceneCamera>( scene );
}
template <> inline std::vector<spSceneLodGroup> EnumerateAllSceneNodes<spSceneLodGroup>( spScene scene )
{
	return Simplygon::internal::EnumerateAllSceneNodes<spSceneLodGroup, Simplygon::EEnumerateNodeTypes::SceneLodGroup>( scene );
}
template <> inline std::vector<spScenePlane> EnumerateAllSceneNodes<spScenePlane>( spScene scene )
{
	return Simplygon::internal::EnumerateAllSceneNodes<spScenePlane, Simplygon::EEnumerateNodeTypes::ScenePlane>( scene );
}
template <> inline std::vector<spSceneNode> EnumerateAllSceneNodes<spSceneNode>( spScene scene )
{
	return Simplygon::internal::EnumerateAllSceneNodes<spSceneNode, Simplygon::EEnumerateNodeTypes::AnyNode>( scene );
}

// ------------------------------------------------------------------------
// enumeration of shading network

// Find and return all upstream nodes of the template type
// If includeFirstNodeInList is false, the parameter "node" will be included in the list, if it can be cast to T
template <class T> std::vector<T> EnumerateShadingNetworkNodes( const spShadingNode node, const bool includeFirstNodeInList = true )
{
	std::vector<T> ret;
	std::set<Simplygon::hIntf> destSet;
	Simplygon::internal::EnumerateUpstreamShadingNetworkNodesRecursive<T>( node, ret, destSet, includeFirstNodeInList );
	return ret;
}

};
