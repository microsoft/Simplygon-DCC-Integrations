// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once
#include <string>
#include <tchar.h>
#include "stdmat.h"

class MaterialInfo
{
	public:
	std::string MaterialId;
	std::basic_string<TCHAR> MaterialName;

#if MAX_VERSION_MAJOR < 23
	StdMat2* MaxMaterialReference;
#else
	Mtl* MaxPhysicalMaterialReference;
#endif

	MaterialInfo( std::basic_string<TCHAR> tMaterialName )
	{
		this->MaterialName = tMaterialName;

#if MAX_VERSION_MAJOR < 23
		this->MaxMaterialReference = nullptr;
#else
		this->MaxPhysicalMaterialReference = nullptr;
#endif
		this->MaterialId = "";
	}

	friend bool operator==( const MaterialInfo& matInfo1, const MaterialInfo& matInfo2 );
};
