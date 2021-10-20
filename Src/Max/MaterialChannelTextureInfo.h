// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once
#include <map>
#include <tchar.h>

class MaterialChannelTextureInfo
{
	public:
	std::map<std::basic_string<TCHAR>, int> FilePaths;
};
