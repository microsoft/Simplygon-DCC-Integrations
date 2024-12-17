// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"
#include "MaterialInfoHandler.h"

void MaterialInfoHandler::AddToMap( std::basic_string<TCHAR> tMeshName,
                                    std::basic_string<TCHAR> tMaterialName,
                                    std::map<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>& tMeshMap )
{
	// add mesh (and material) if missing
	std::map<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>::iterator& meshIterator = tMeshMap.find( tMeshName );
	if( meshIterator == tMeshMap.end() )
	{
		std::vector<std::basic_string<TCHAR>> tMaterials;
		tMaterials.push_back( tMaterialName );

		tMeshMap.insert( std::pair<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>( tMeshName, tMaterials ) );
		meshIterator = tMeshMap.find( tMeshName );
	}

	// otherwise, add material if it does not exist
	else
	{
		bool bExists = false;
		std::vector<std::basic_string<TCHAR>>& tMap = meshIterator->second;
		for( uint i = 0; i < tMap.size(); ++i )
		{
			if( tMap[ i ] == tMaterialName )
			{
				bExists = true;
				break;
			}
		}

		if( !bExists )
		{
			tMap.push_back( tMaterialName );
		}
	}
}

void MaterialInfoHandler::AddProcessedSceneFiles( std::vector<std::basic_string<TCHAR>> tOutputList )
{
	this->ProcessedOutputPaths = tOutputList;
}

std::vector<std::basic_string<TCHAR>> MaterialInfoHandler::GetProcessedSceneFiles()
{
	return this->ProcessedOutputPaths;
}

std::vector<std::basic_string<TCHAR>> MaterialInfoHandler::GetMaterialsWithCustomChannels()
{
	std::vector<std::basic_string<TCHAR>> materialList;

	for( std::map<std::basic_string<TCHAR>, MaterialChannelInfo>::const_iterator& materialIterator = MaterialToChannelMapping.begin();
	     materialIterator != MaterialToChannelMapping.end();
	     materialIterator++ )
	{
		materialList.push_back( materialIterator->first );
	}

	return materialList;
}

std::vector<std::basic_string<TCHAR>> MaterialInfoHandler::GetCustomChannelsForMaterial( std::basic_string<TCHAR> tMaterialName )
{
	std::vector<std::basic_string<TCHAR>> tCustomChannelList;

	std::map<std::basic_string<TCHAR>, MaterialChannelInfo>::const_iterator& materialIterator = MaterialToChannelMapping.find( tMaterialName );
	if( materialIterator != MaterialToChannelMapping.end() )
	{
		const MaterialChannelInfo& materialChannelInfo = materialIterator->second;

		for( std::map<std::basic_string<TCHAR>, MaterialChannelTextureInfo>::const_iterator& channelIterator =
		         materialChannelInfo.ChannelToTextureMapping.begin();
		     channelIterator != materialChannelInfo.ChannelToTextureMapping.end();
		     channelIterator++ )
		{
			tCustomChannelList.push_back( channelIterator->first );
		}
	}

	return tCustomChannelList;
}

std::basic_string<TCHAR> MaterialInfoHandler::GetTextureNameForMaterialChannel( std::basic_string<TCHAR> tMaterialName,
                                                                                std::basic_string<TCHAR> tMaterialChannelName )
{
	std::map<std::basic_string<TCHAR>, MaterialChannelInfo>::const_iterator& materialIterator = MaterialToChannelMapping.find( tMaterialName );
	if( materialIterator != MaterialToChannelMapping.end() )
	{
		const MaterialChannelInfo& materialChannelInfo = materialIterator->second;

		// see if channel exists
		std::map<std::basic_string<TCHAR>, MaterialChannelTextureInfo>::const_iterator& channelIterator =
		    materialChannelInfo.ChannelToTextureMapping.find( tMaterialChannelName );

		// if not, channel and texture
		if( channelIterator != materialChannelInfo.ChannelToTextureMapping.end() )
		{
			const MaterialChannelTextureInfo& materialChannelTextureInfo = channelIterator->second;

			// see if channel exists
			std::map<std::basic_string<TCHAR>, int>::const_iterator& textureIterator = materialChannelTextureInfo.FilePaths.begin(); //.find(texturePath);

			// if not, texture
			if( textureIterator != materialChannelTextureInfo.FilePaths.end() )
			{
				// return materialChannelTextureInfo.paths[0];
				return textureIterator->first;
			}
		}
	}

	return _T("");
}

int MaterialInfoHandler::GetMappingChannelForMaterialChannel( std::basic_string<TCHAR> tMaterialName, std::basic_string<TCHAR> tChannelName )
{
	std::map<std::basic_string<TCHAR>, MaterialChannelInfo>::const_iterator& tMaterialIterator = this->MaterialToChannelMapping.find( tMaterialName );
	if( tMaterialIterator != this->MaterialToChannelMapping.end() )
	{
		const MaterialChannelInfo& materialChannelInfo = tMaterialIterator->second;

		// see if channel exists
		std::map<std::basic_string<TCHAR>, MaterialChannelTextureInfo>::const_iterator& tChannelIterator =
		    materialChannelInfo.ChannelToTextureMapping.find( tChannelName );

		// if not, channel and texture
		if( tChannelIterator != materialChannelInfo.ChannelToTextureMapping.end() )
		{
			const MaterialChannelTextureInfo& materialChannelTextureInfo = tChannelIterator->second;

			// see if channel exists
			std::map<std::basic_string<TCHAR>, int>::const_iterator& tTextureIterator = materialChannelTextureInfo.FilePaths.begin(); //.find(texturePath);

			// if not, texture
			if( tTextureIterator != materialChannelTextureInfo.FilePaths.end() )
			{
				return tTextureIterator->second;
			}
		}
	}

	return 1;
}

std::basic_string<TCHAR> MaterialInfoHandler::MeshReusesMaterial( std::basic_string<TCHAR> tMeshName )
{
	std::map<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>::const_iterator& meshIterator = MeshReuseMaterial.find( tMeshName );
	if( meshIterator != MeshReuseMaterial.end() )
	{
		// maintain compatibility by returning first material
		if( meshIterator->second.size() > 0 )
		{
			return meshIterator->second.at( 0 );
		}
	}
	return _T("");
}

std::vector<std::basic_string<TCHAR>> MaterialInfoHandler::MeshReusesMaterials( std::basic_string<TCHAR> tMeshName )
{
	std::map<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>::const_iterator& meshIterator = MeshReuseMaterial.find( tMeshName );
	if( meshIterator != MeshReuseMaterial.end() )
	{
		return meshIterator->second;
	}

	return std::vector<std::basic_string<TCHAR>>();
}

std::vector<int> MaterialInfoHandler::GetMeshMaterialIds( std::basic_string<TCHAR> tMeshName )
{
	std::map<std::basic_string<TCHAR>, std::vector<int>>::const_iterator& meshIterator = MeshMaterialIds.find( tMeshName );
	if( meshIterator != MeshMaterialIds.end() )
	{
		return meshIterator->second;
	}

	return std::vector<int>();
}

void MaterialInfoHandler::AddMaterialIds( std::basic_string<TCHAR> tMeshName, std::vector<int>& materialIds )
{
	std::map<std::basic_string<TCHAR>, std::vector<int>>::const_iterator& meshIterator = MeshMaterialIds.find( tMeshName );
	if( meshIterator == MeshMaterialIds.end() )
	{
		MeshMaterialIds.insert( std::pair<std::basic_string<TCHAR>, std::vector<int>>( tMeshName, materialIds ) );
	}
}

void MaterialInfoHandler::AddReuse( std::basic_string<TCHAR> tMeshName, std::basic_string<TCHAR> tMaterialName )
{
	this->AddToMap( tMeshName, tMaterialName, MeshToMaterialMapping );
	this->AddToMap( tMeshName, tMaterialName, MeshReuseMaterial );
}

void MaterialInfoHandler::Add( std::basic_string<TCHAR> tMeshName )
{
	this->AddToMap( tMeshName, _T(""), MeshToMaterialMapping );
}

void MaterialInfoHandler::Add( std::basic_string<TCHAR> tMeshName, std::basic_string<TCHAR> tMaterialName, bool bReuse )
{
	this->AddToMap( tMeshName, tMaterialName, MeshToMaterialMapping );

	if( bReuse )
	{
		this->AddToMap( tMeshName, tMaterialName, MeshReuseMaterial );
	}
}

void MaterialInfoHandler::Add(
    std::basic_string<TCHAR> tMeshName, std::basic_string<TCHAR> tMaterialName, std::basic_string<TCHAR> tSubMaterialName, int subMaterialIndex, bool bReuse )
{
	this->AddToMap( tMeshName, tMaterialName, MeshToMaterialMapping );

	std::map<std::basic_string<TCHAR>, std::map<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>>>::iterator& materialIterator =
	    MaterialToSubMaterial.find( tMaterialName );
	if( materialIterator == MaterialToSubMaterial.end() )
	{
		std::map<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>> subMaterials = {
		    std::pair<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>>(
		        tSubMaterialName, std::pair<int, std::basic_string<TCHAR>>( subMaterialIndex, tSubMaterialName ) ) };

		MaterialToSubMaterial.insert(
		    std::pair<std::basic_string<TCHAR>, std::map<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>>>( tMaterialName, subMaterials ) );
	}
	else
	{
		std::map<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>>& subMaterials = materialIterator->second;
		subMaterials.insert( std::pair<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>>(
		    tSubMaterialName, std::pair<int, std::basic_string<TCHAR>>( subMaterialIndex, tSubMaterialName ) ) );
	}
}

void MaterialInfoHandler::Add( std::basic_string<TCHAR> tMeshName,
                               std::basic_string<TCHAR> tMaterialName,
                               std::basic_string<TCHAR> tMaterialChannelName,
                               std::basic_string<TCHAR> tTexturePath )
{
	this->AddToMap( tMeshName, tMaterialName, MeshToMaterialMapping );

	// see if material exists
	std::map<std::basic_string<TCHAR>, MaterialChannelInfo>::iterator& materialIterator = MaterialToChannelMapping.find( tMaterialName );

	// if not, add material, channel and texture
	if( materialIterator == MaterialToChannelMapping.end() )
	{
		MaterialChannelTextureInfo mcti;
		mcti.FilePaths.insert( std::pair<std::basic_string<TCHAR>, uint>( tTexturePath, (uint)mcti.FilePaths.size() ) );

		MaterialChannelInfo mci;
		mci.ChannelToTextureMapping.insert( std::pair<std::basic_string<TCHAR>, MaterialChannelTextureInfo>( tMaterialChannelName, mcti ) );

		MaterialToChannelMapping.insert( std::pair<std::basic_string<TCHAR>, MaterialChannelInfo>( tMaterialName, mci ) );
	}

	// otherwise
	else
	{
		MaterialChannelInfo& materialChannelInfo = materialIterator->second;

		// see if channel exists
		std::map<std::basic_string<TCHAR>, MaterialChannelTextureInfo>::iterator& channelIterator =
		    materialChannelInfo.ChannelToTextureMapping.find( tMaterialChannelName );

		// if not, channel and texture
		if( channelIterator == materialChannelInfo.ChannelToTextureMapping.end() )
		{
			MaterialChannelTextureInfo mcti;
			mcti.FilePaths.insert( std::pair<std::basic_string<TCHAR>, uint>( tTexturePath, (uint)mcti.FilePaths.size() ) );

			materialChannelInfo.ChannelToTextureMapping.insert( std::pair<std::basic_string<TCHAR>, MaterialChannelTextureInfo>( tMaterialChannelName, mcti ) );
		}
		else
		{
			MaterialChannelTextureInfo& materialChannelTextureInfo = channelIterator->second;

			// see if channel exists
			std::map<std::basic_string<TCHAR>, int>::iterator& textureIterator = materialChannelTextureInfo.FilePaths.find( tTexturePath );

			// if not, texture
			if( textureIterator == materialChannelTextureInfo.FilePaths.end() )
			{
				materialChannelTextureInfo.FilePaths.insert(
				    std::pair<std::basic_string<TCHAR>, uint>( tTexturePath, (uint)materialChannelTextureInfo.FilePaths.size() ) );
			}
		}
	}
}

void MaterialInfoHandler::Add( std::basic_string<TCHAR> tMeshName,
                               std::basic_string<TCHAR> tMaterialName,
                               std::basic_string<TCHAR> tChannelName,
                               std::basic_string<TCHAR> tFilePath,
                               int mappingChannel )
{
	this->AddToMap( tMeshName, tMaterialName, MeshToMaterialMapping );

	// see if material exists
	std::map<std::basic_string<TCHAR>, MaterialChannelInfo>::iterator& tMaterialIterator = this->MaterialToChannelMapping.find( tMaterialName );

	// if not, add material, channel and texture
	if( tMaterialIterator == this->MaterialToChannelMapping.end() )
	{
		MaterialChannelTextureInfo mcti;
		mcti.FilePaths.insert( std::pair<std::basic_string<TCHAR>, uint>( tFilePath, mappingChannel ) );

		MaterialChannelInfo mci;
		mci.ChannelToTextureMapping.insert( std::pair<std::basic_string<TCHAR>, MaterialChannelTextureInfo>( tChannelName, mcti ) );

		this->MaterialToChannelMapping.insert( std::pair<std::basic_string<TCHAR>, MaterialChannelInfo>( tMaterialName, mci ) );
	}

	// otherwise
	else
	{
		MaterialChannelInfo& materialChannelInfo = tMaterialIterator->second;

		// see if channel exists
		std::map<std::basic_string<TCHAR>, MaterialChannelTextureInfo>::iterator& tChannelIterator =
		    materialChannelInfo.ChannelToTextureMapping.find( tChannelName );

		// if not, channel and texture
		if( tChannelIterator == materialChannelInfo.ChannelToTextureMapping.end() )
		{
			MaterialChannelTextureInfo mcti;
			mcti.FilePaths.insert( std::pair<std::basic_string<TCHAR>, uint>( tFilePath, mappingChannel ) );

			materialChannelInfo.ChannelToTextureMapping.insert( std::pair<std::basic_string<TCHAR>, MaterialChannelTextureInfo>( tChannelName, mcti ) );
		}
		else
		{
			MaterialChannelTextureInfo& materialChannelTextureInfo = tChannelIterator->second;

			// see if channel exists
			std::map<std::basic_string<TCHAR>, int>::const_iterator& tTextureIterator = materialChannelTextureInfo.FilePaths.find( tFilePath );

			// if not, texture
			if( tTextureIterator == materialChannelTextureInfo.FilePaths.end() )
			{
				materialChannelTextureInfo.FilePaths.insert( std::pair<std::basic_string<TCHAR>, uint>( tFilePath, mappingChannel ) );
			}
		}
	}
}

std::vector<std::basic_string<TCHAR>> MaterialInfoHandler::GetMeshes()
{
	std::vector<std::basic_string<TCHAR>> tMeshList;

	for( std::map<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>::const_iterator& meshIterator = MeshToMaterialMapping.begin();
	     meshIterator != MeshToMaterialMapping.end();
	     meshIterator++ )
	{
		tMeshList.push_back( meshIterator->first );
	}

	return tMeshList;
}

std::basic_string<TCHAR> MaterialInfoHandler::GetMaterialForMesh( std::basic_string<TCHAR> tMeshName )
{
	std::map<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>::const_iterator& meshIterator = MeshToMaterialMapping.find( tMeshName );
	if( meshIterator != MeshToMaterialMapping.end() )
	{
		// maintain compatibility by returning first material
		if( meshIterator->second.size() > 0 )
		{
			return meshIterator->second.at( 0 );
		}
	}

	return _T("");
}

std::vector<std::basic_string<TCHAR>> MaterialInfoHandler::GetMaterialsForMesh( std::basic_string<TCHAR> tMeshName )
{
	std::map<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>::const_iterator& meshIterator = MeshToMaterialMapping.find( tMeshName );
	if( meshIterator != MeshToMaterialMapping.end() )
	{
		return meshIterator->second;
	}

	return std::vector<std::basic_string<TCHAR>>();
}

std::map<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>> MaterialInfoHandler::GetSubMaterials( std::basic_string<TCHAR> tMaterialName )
{
	std::map<std::basic_string<TCHAR>, std::map<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>>>::const_iterator& materialIterator =
	    MaterialToSubMaterial.find( tMaterialName );
	if( materialIterator != MaterialToSubMaterial.end() )
	{
		return materialIterator->second;
	}

	return std::map<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>>();
}

int MaterialInfoHandler::GetSubMaterialIndex( std::basic_string<TCHAR> tMaterialName, std::basic_string<TCHAR> tSubMaterialName )
{
	std::map<std::basic_string<TCHAR>, std::map<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>>>::const_iterator& materialIterator =
	    MaterialToSubMaterial.find( tMaterialName );
	if( materialIterator != MaterialToSubMaterial.end() )
	{
		const std::map<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>>& subMaterials = materialIterator->second;

		std::map<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>>::const_iterator& subMaterialMap = subMaterials.find( tSubMaterialName );
		if( subMaterialMap != subMaterials.end() )
		{
			return subMaterialMap->second.first;
		}
	}

	return 0;
}

std::basic_string<TCHAR> MaterialInfoHandler::MaterialReusesSubMaterial( std::basic_string<TCHAR> tMaterialName, std::basic_string<TCHAR> tSubMaterialName )
{
	std::map<std::basic_string<TCHAR>, std::map<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>>>::const_iterator& materialIterator =
	    MaterialToSubMaterial.find( tMaterialName );
	if( materialIterator != MaterialToSubMaterial.end() )
	{
		const std::map<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>>& subMaterials = materialIterator->second;

		std::map<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>>::const_iterator& subMaterialMap = subMaterials.find( tSubMaterialName );
		if( subMaterialMap != subMaterials.end() )
		{
			return subMaterialMap->second.second;
		}
	}

	return _T("");
}

void MaterialInfoHandler::Clear()
{
	this->ProcessedOutputPaths.clear();
	this->MaterialToChannelMapping.clear();
	this->MeshToMaterialMapping.clear();
	this->MeshReuseMaterial.clear();
	this->MeshMaterialIds.clear();
	this->MaterialToSubMaterial.clear();
}
