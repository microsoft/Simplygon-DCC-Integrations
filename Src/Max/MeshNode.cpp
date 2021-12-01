// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"
#include "MeshNode.h"

MeshNode::MeshNode()
{
	this->MaxNode = nullptr;
	this->Objects = nullptr;
	this->TriObjects = nullptr;
	this->SkinModifiers = nullptr;
	this->MorphTargetModifier = nullptr;
	this->MorphTargetData = nullptr;
	this->MeshMaterials = nullptr;
	this->sgMesh = nullptr;
}

MeshNode::~MeshNode()
{
	if( this->TriObjects )
	{
		if( this->Objects != this->TriObjects )
		{
			if( this->TriObjects != nullptr )
			{
				this->TriObjects->DeleteMe();
			}
		}

		this->TriObjects = nullptr;
	}

	if( this->Objects )
	{
		delete this->Objects;
		this->Objects = nullptr;
	}

	if( this->MeshMaterials )
	{
		delete this->MeshMaterials;
		this->MeshMaterials = nullptr;
	}
}
