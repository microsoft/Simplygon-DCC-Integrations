// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once
class MaterialChannelTextureInfo
{
	public:
	std::map<std::basic_string<TCHAR>, uint> FilePaths;
};

class MaterialChannelInfo
{
	public:
	std::map<std::basic_string<TCHAR>, MaterialChannelTextureInfo> ChannelToTextureMapping;
};

class ProcessingRecord
{
	public:
	std::map<uint, std::basic_string<TCHAR>> SceneIndexToFilePath;
};

class MaterialInfoHandler
{
	private:
	std::vector<std::basic_string<TCHAR>> ProcessedOutputPaths;

	std::map<std::basic_string<TCHAR>, MaterialChannelInfo> MaterialToChannelMapping;
	std::map<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>> MeshToMaterialMapping;
	std::map<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>> MeshReuseMaterial;
	std::map<std::basic_string<TCHAR>, std::vector<int>> MeshMaterialIds;

	void AddToMap( std::basic_string<TCHAR> tMeshName,
	               std::basic_string<TCHAR> tMaterialName,
	               std::map<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>& tMeshMap )
	{
		// add mesh (and material) if missing
		std::map<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>::iterator meshIterator = tMeshMap.find( tMeshName );
		if( meshIterator == tMeshMap.end() )
		{
			std::vector<std::basic_string<TCHAR>> materials;
			materials.push_back( tMaterialName );

			tMeshMap.insert( std::pair<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>( tMeshName, materials ) );
			meshIterator = tMeshMap.find( tMeshName );
		}

		// otherwise, add material if it does not exist
		else
		{
			bool exists = false;
			std::vector<std::basic_string<TCHAR>>& tMap = meshIterator->second;
			for( uint i = 0; i < tMap.size(); ++i )
			{
				if( tMap[ i ] == tMaterialName )
				{
					exists = true;
					break;
				}
			}

			if( !exists )
			{
				tMap.push_back( tMaterialName );
			}
		}
	}

	public:
	void AddProcessedSceneFiles( std::vector<std::basic_string<TCHAR>> tOutputList ) { this->ProcessedOutputPaths = tOutputList; }
	std::vector<std::basic_string<TCHAR>> GetProcessedSceneFiles() { return this->ProcessedOutputPaths; }

	std::vector<std::basic_string<TCHAR>> GetMaterialsWithCustomChannels()
	{
		std::vector<std::basic_string<TCHAR>> materialList;

		for( std::map<std::basic_string<TCHAR>, MaterialChannelInfo>::iterator materialIterator = MaterialToChannelMapping.begin();
		     materialIterator != MaterialToChannelMapping.end();
		     materialIterator++ )
		{
			materialList.push_back( materialIterator->first );
		}

		return materialList;
	}

	std::vector<std::basic_string<TCHAR>> GetCustomChannelsForMaterial( std::basic_string<TCHAR> tMaterialName )
	{
		std::vector<std::basic_string<TCHAR>> tCustomChannelList;

		std::map<std::basic_string<TCHAR>, MaterialChannelInfo>::iterator materialIterator = MaterialToChannelMapping.find( tMaterialName );
		if( materialIterator != MaterialToChannelMapping.end() )
		{
			MaterialChannelInfo& materialChannelInfo = materialIterator->second;

			for( std::map<std::basic_string<TCHAR>, MaterialChannelTextureInfo>::iterator channelIterator = materialChannelInfo.ChannelToTextureMapping.begin();
			     channelIterator != materialChannelInfo.ChannelToTextureMapping.end();
			     channelIterator++ )
			{
				tCustomChannelList.push_back( channelIterator->first );
			}
		}

		return tCustomChannelList;
	}

	std::basic_string<TCHAR> GetTextureNameForMaterialChannel( std::basic_string<TCHAR> tMaterialName, std::basic_string<TCHAR> tMaterialChannelName )
	{
		std::map<std::basic_string<TCHAR>, MaterialChannelInfo>::iterator materialIterator = MaterialToChannelMapping.find( tMaterialName );
		if( materialIterator != MaterialToChannelMapping.end() )
		{
			MaterialChannelInfo& materialChannelInfo = materialIterator->second;

			// see if channel exists
			std::map<std::basic_string<TCHAR>, MaterialChannelTextureInfo>::iterator channelIterator =
			    materialChannelInfo.ChannelToTextureMapping.find( tMaterialChannelName );

			// if not, channel and texture
			if( channelIterator != materialChannelInfo.ChannelToTextureMapping.end() )
			{
				MaterialChannelTextureInfo& materialChannelTextureInfo = channelIterator->second;

				// see if channel exists
				std::map<std::basic_string<TCHAR>, uint>::iterator textureIterator = materialChannelTextureInfo.FilePaths.begin(); //.find(texturePath);

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

	// Deprecated as of 2019-03-20, use MeshReusesMaterials instead.
	std::basic_string<TCHAR> MeshReusesMaterial( std::basic_string<TCHAR> tMeshName )
	{
		std::map<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>::iterator meshIterator = MeshReuseMaterial.find( tMeshName );
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

	std::vector<std::basic_string<TCHAR>> MeshReusesMaterials( std::basic_string<TCHAR> tMeshName )
	{
		std::map<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>::iterator meshIterator = MeshReuseMaterial.find( tMeshName );
		if( meshIterator != MeshReuseMaterial.end() )
		{
			return meshIterator->second;
		}

		return std::vector<std::basic_string<TCHAR>>();
	}

	std::vector<int> GetMeshMaterialIds( std::basic_string<TCHAR> tMeshName )
	{
		std::map<std::basic_string<TCHAR>, std::vector<int>>::iterator meshIterator = MeshMaterialIds.find( tMeshName );
		if( meshIterator != MeshMaterialIds.end() )
		{
			return meshIterator->second;
		}

		return std::vector<int>();
	}

	void AddMaterialIds( std::basic_string<TCHAR> tMeshName, std::vector<int>& materialIds )
	{
		std::map<std::basic_string<TCHAR>, std::vector<int>>::iterator meshIterator = MeshMaterialIds.find( tMeshName );
		if( meshIterator == MeshMaterialIds.end() )
		{
			MeshMaterialIds.insert( std::pair<std::basic_string<TCHAR>, std::vector<int>>( tMeshName, materialIds ) );
		}
	}

	void AddReuse( std::basic_string<TCHAR> tMeshName, std::basic_string<TCHAR> tMaterialName )
	{
		this->AddToMap( tMeshName, tMaterialName, MeshToMaterialMapping );
		this->AddToMap( tMeshName, tMaterialName, MeshReuseMaterial );
	}

	void Add( std::basic_string<TCHAR> tMeshName )
	{
		// add mesh (and material) if missing
		std::map<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>::iterator meshIterator = MeshToMaterialMapping.find( tMeshName );
		if( meshIterator == MeshToMaterialMapping.end() )
		{
			std::vector<std::basic_string<TCHAR>> materials;

			MeshToMaterialMapping.insert( std::pair<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>( tMeshName, materials ) );
		}
	}

	void Add( std::basic_string<TCHAR> tMeshName,
	          std::basic_string<TCHAR> tMaterialName,
	          std::basic_string<TCHAR> tMaterialChannelName,
	          std::basic_string<TCHAR> tTexturePath )
	{
		this->AddToMap( tMeshName, tMaterialName, MeshToMaterialMapping );

		// see if material exists
		std::map<std::basic_string<TCHAR>, MaterialChannelInfo>::iterator materialIterator = MaterialToChannelMapping.find( tMaterialName );

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
			std::map<std::basic_string<TCHAR>, MaterialChannelTextureInfo>::iterator channelIterator =
			    materialChannelInfo.ChannelToTextureMapping.find( tMaterialChannelName );

			// if not, channel and texture
			if( channelIterator == materialChannelInfo.ChannelToTextureMapping.end() )
			{
				MaterialChannelTextureInfo mcti;
				mcti.FilePaths.insert( std::pair<std::basic_string<TCHAR>, uint>( tTexturePath, (uint)mcti.FilePaths.size() ) );

				materialChannelInfo.ChannelToTextureMapping.insert(
				    std::pair<std::basic_string<TCHAR>, MaterialChannelTextureInfo>( tMaterialChannelName, mcti ) );
			}
			else
			{
				MaterialChannelTextureInfo& materialChannelTextureInfo = channelIterator->second;

				// see if channel exists
				std::map<std::basic_string<TCHAR>, uint>::iterator textureIterator = materialChannelTextureInfo.FilePaths.find( tTexturePath );

				// if not, texture
				if( textureIterator == materialChannelTextureInfo.FilePaths.end() )
				{
					materialChannelTextureInfo.FilePaths.insert(
					    std::pair<std::basic_string<TCHAR>, uint>( tTexturePath, (uint)materialChannelTextureInfo.FilePaths.size() ) );
				}
			}
		}
	}

	std::vector<std::basic_string<TCHAR>> GetMeshes()
	{
		std::vector<std::basic_string<TCHAR>> tMeshList;

		for( std::map<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>::iterator meshIterator = MeshToMaterialMapping.begin();
		     meshIterator != MeshToMaterialMapping.end();
		     meshIterator++ )
		{
			tMeshList.push_back( meshIterator->first );
		}

		return tMeshList;
	}

	// Deprecated as of 2019-03-20, use GetMaterialsForMesh instead.
	std::basic_string<TCHAR> GetMaterialForMesh( std::basic_string<TCHAR> tMeshName )
	{
		std::map<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>::iterator meshIterator = MeshToMaterialMapping.find( tMeshName );
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

	std::vector<std::basic_string<TCHAR>> GetMaterialsForMesh( std::basic_string<TCHAR> tMeshName )
	{
		std::map<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>::iterator meshIterator = MeshToMaterialMapping.find( tMeshName );
		if( meshIterator != MeshToMaterialMapping.end() )
		{
			return meshIterator->second;
		}

		return std::vector<std::basic_string<TCHAR>>();
	}

	void Clear() { MaterialToChannelMapping.clear(); }
};
