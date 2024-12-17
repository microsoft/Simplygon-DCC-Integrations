// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once
#include <map>
#include <tchar.h>
#include "MaterialChannelTextureInfo.h"

class MaterialChannelInfo
{
	public:
	std::map<std::basic_string<TCHAR>, MaterialChannelTextureInfo> ChannelToTextureMapping;
};
