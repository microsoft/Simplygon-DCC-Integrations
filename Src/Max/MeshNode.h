// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once
#include "SimplygonMaxPerVertexData.h"
#include "MaxMaterialMap.h"

class MorpherWrapper;

class MeshNode
{
	public:
	INode* MaxNode;
	Object* Objects;
	TriObject* TriObjects;
	PolyObject* PolyObjects;
	Modifier* SkinModifiers;
	Modifier* MorphTargetModifier;
	Modifier* TurboSmoothModifier;
	MorpherWrapper* MorphTargetData;

	MaxMaterialMap* MeshMaterials;
	Simplygon::spSceneMesh sgMesh;

	MeshNode();
	~MeshNode();
};
