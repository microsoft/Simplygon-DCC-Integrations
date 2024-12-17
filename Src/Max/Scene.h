// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

class Scene
{
	public:
	Scene();
	Simplygon::spScene sgScene;
	std::vector<Simplygon::spScene> sgProcessedScenes;
	Simplygon::spSceneNode FindSceneNode( INode* mMaxNode );
};
