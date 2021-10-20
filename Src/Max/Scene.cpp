// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"
#include "Scene.h"
#include "Shared.h"

Scene::Scene()
{
	this->sgScene = nullptr;
}

spSceneNode Scene::FindSceneNode( INode* mMaxNode )
{
	if( mMaxNode == nullptr )
		return nullptr;

	const TCHAR* tNodeName = mMaxNode->GetName();
	INode* mMaxParentNode = mMaxNode->GetParentNode();

	// if we have no parent, find parent in root
	if( mMaxParentNode == nullptr )
	{
		// this is a root node, find it in the scene root
		spSceneNode sgRootNode = sgScene->GetRootNode();
		uint numChildren = sgRootNode->GetChildCount();
		for( uint i = 0; i < numChildren; ++i )
		{
			spSceneNode sgChildNode = sgRootNode->GetChild( i );
			const TCHAR* tChildNodeName = ConstCharPtrToLPCTSTR( sgChildNode->GetName() );
			if( tChildNodeName == nullptr )
			{
				// skip this node
			}
			else if( _tcscmp( tNodeName, ConstCharPtrToLPCTSTR( sgChildNode->GetName() ) ) == 0 )
			{
				return sgChildNode;
			}
		}

		// wasn't found
		return nullptr;
	}

	// we have a parent, find this parent in the simplygon scene, recursively
	spSceneNode sgParentNode = this->FindSceneNode( mMaxParentNode );
	if( sgParentNode.IsNull() )
	{
		return nullptr;
	}

	// we have found our parent, find us in our parent's list
	uint numChildren = sgParentNode->GetChildCount();
	for( uint i = 0; i < numChildren; ++i )
	{
		spSceneNode sgChildNode = sgParentNode->GetChild( i );
		if( _tcscmp( tNodeName, ConstCharPtrToLPCTSTR( sgChildNode->GetName() ) ) == 0 )
		{
			return sgChildNode;
		}
	}

	return nullptr;
}
