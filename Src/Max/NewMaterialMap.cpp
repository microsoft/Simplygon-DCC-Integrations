// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "NewMaterialMap.h"

NewMaterialMap::NewMaterialMap( int materialId, std::string materialGuid, bool inUse )
{
	this->MaterialId = materialId;
	this->MaterialGUID = materialGuid;
	this->InUse = inUse;
}
