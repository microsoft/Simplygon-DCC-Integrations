// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once
#include "SimplygonMaxPerVertexData.h"
#include "MaxMaterialMap.h"

class MeshNode
{
	public:
	INode* MaxNode;
	Object* Objects;
	TriObject* TriObjects;
	Modifier* SkinModifiers;
	MaxMaterialMap* MeshMaterials;
	Simplygon::spSceneMesh sgMesh;

	MeshNode();
	~MeshNode();
};
