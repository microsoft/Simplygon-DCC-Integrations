// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once
#include "PCH.h"

class MaxMaterialMap
{
	public:
	std::string sgMaterialId;
	std::basic_string<TCHAR> sgMaterialName;

	AnimHandle mMaxMaterialHandle;

	size_t NumSubMaterials;    // The number of materials in this multi-material (0 if not a multimaterial)
	size_t NumActiveMaterials; // The active number of materials (can be 0 for multimaterial)

	std::map<int, int> MaxToSGMapping; // map from max local to sg material id
	std::map<int, int> SGToMaxMapping; // map from sg to max local material id

	MaxMaterialMap();
	MaxMaterialMap( AnimHandle uniqueHandle, std::basic_string<TCHAR> materialName, std::string materialId );

	void SetupFromMaterial( Mtl* mMaxMaterial );
	int GetMaxMaterialId( int sgId );
	int GetSimplygonMaterialId( int maxId ) const;
	void AddSubMaterialMapping( int first, int second );
};
