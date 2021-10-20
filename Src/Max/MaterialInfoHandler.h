// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once
class MaterialChannelTextureInfo
{
	public:
	std::map<std::basic_string<TCHAR>, int> FilePaths;
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
	std::map<std::basic_string<TCHAR>, std::map<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>>> MaterialToSubMaterial;

	void AddToMap( std::basic_string<TCHAR> tMeshName,
	               std::basic_string<TCHAR> tMaterialName,
	               std::map<std::basic_string<TCHAR>, std::vector<std::basic_string<TCHAR>>>& tMeshMap );

	public:
	void AddProcessedSceneFiles( std::vector<std::basic_string<TCHAR>> tOutputList );
	std::vector<std::basic_string<TCHAR>> GetProcessedSceneFiles();

	std::vector<std::basic_string<TCHAR>> GetMaterialsWithCustomChannels();

	std::vector<std::basic_string<TCHAR>> GetCustomChannelsForMaterial( std::basic_string<TCHAR> tMaterialName );

	std::basic_string<TCHAR> GetTextureNameForMaterialChannel( std::basic_string<TCHAR> tMaterialName, std::basic_string<TCHAR> tMaterialChannelName );

	int GetMappingChannelForMaterialChannel( std::basic_string<TCHAR> tMaterialName, std::basic_string<TCHAR> tChannelName );

	std::basic_string<TCHAR> MeshReusesMaterial( std::basic_string<TCHAR> tMeshName );

	std::vector<std::basic_string<TCHAR>> MeshReusesMaterials( std::basic_string<TCHAR> tMeshName );

	std::vector<int> GetMeshMaterialIds( std::basic_string<TCHAR> tMeshName );

	void AddMaterialIds( std::basic_string<TCHAR> tMeshName, std::vector<int>& materialIds );

	void AddReuse( std::basic_string<TCHAR> tMeshName, std::basic_string<TCHAR> tMaterialName );

	void Add( std::basic_string<TCHAR> tMeshName );

	void Add( std::basic_string<TCHAR> tMeshName, std::basic_string<TCHAR> tMaterialName, bool bReuse = true );

	void Add( std::basic_string<TCHAR> tMeshName,
	          std::basic_string<TCHAR> tMaterialName,
	          std::basic_string<TCHAR> tSubMaterialName,
	          int subMaterialIndex,
	          bool bReuse = true );

	void Add( std::basic_string<TCHAR> tMeshName,
	          std::basic_string<TCHAR> tMaterialName,
	          std::basic_string<TCHAR> tMaterialChannelName,
	          std::basic_string<TCHAR> tTexturePath );

	void Add( std::basic_string<TCHAR> tMeshName,
	          std::basic_string<TCHAR> tMaterialName,
	          std::basic_string<TCHAR> tChannelName,
	          std::basic_string<TCHAR> tFilePath,
	          int mappingChannel );

	std::vector<std::basic_string<TCHAR>> GetMeshes();

	std::basic_string<TCHAR> GetMaterialForMesh( std::basic_string<TCHAR> tMeshName );

	std::vector<std::basic_string<TCHAR>> GetMaterialsForMesh( std::basic_string<TCHAR> tMeshName );

	std::map<std::basic_string<TCHAR>, std::pair<int, std::basic_string<TCHAR>>> GetSubMaterials( std::basic_string<TCHAR> tMaterialName );

	int GetSubMaterialIndex( std::basic_string<TCHAR> tMaterialName, std::basic_string<TCHAR> tSubMaterialName );

	std::basic_string<TCHAR> MaterialReusesSubMaterial( std::basic_string<TCHAR> tMaterialName, std::basic_string<TCHAR> tSubMaterialName );

	void Clear();
};
