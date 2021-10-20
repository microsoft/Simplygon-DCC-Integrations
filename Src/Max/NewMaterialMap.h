// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once
#include <string>
class NewMaterialMap
{
	public:
	int MaterialId;
	std::string MaterialGUID;
	bool InUse;

	NewMaterialMap( int materialId, std::string materialGuid, bool inUse );
};
