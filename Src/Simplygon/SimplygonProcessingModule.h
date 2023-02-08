// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include "Simplygon.h"
#include <vector>
#include <tchar.h>
#include <string>
#include <windows.h>

enum PBatchType
{
	INTERNAL,
	EXTERNAL
};

class SimplygonProcessingModule
{
	private:
	Simplygon::Observer* progressObserver;
	Simplygon::ErrorHandler* errorHandler;

	std::basic_string<TCHAR> tTextureOutputPath;
	std::basic_string<TCHAR> tWorkDirectory;
	std::basic_string<TCHAR> tExternalBatchPath;
	PBatchType batchType;

	void AddSimplygonTexture( Simplygon::spMaterial sgMaterial,
	                          Simplygon::spTextureTable sgTextureTable,
	                          std::basic_string<TCHAR> tChannelName,
	                          std::basic_string<TCHAR> tTextureFilePath,
	                          std::basic_string<TCHAR> tNamePrefix = _T("") );

	Simplygon::spScene RunReduction( const Simplygon::spScene sgInputScene, bool bBakeMaterials = false );
	Simplygon::spScene RunAggregation( const Simplygon::spScene sgInputScene, bool bBakeMaterials = false );
	Simplygon::spScene RunReductionTest_1( const Simplygon::spScene sgInputScene );

	std::vector<Simplygon::spScene> RunInternalProcess( const Simplygon::spScene sgInputScene, std::basic_string<TCHAR> tPipelineFilePath );
	std::vector<Simplygon::spScene> RunExternalProcess( const Simplygon::spScene sgInputScene, std::basic_string<TCHAR> tPipelineFilePath );
	bool RunInternalProcess( std::basic_string<TCHAR> tInputSceneFile, std::basic_string<TCHAR> tOutputSceneFile, std::basic_string<TCHAR> tPipelineFilePath );
	bool RunExternalProcess( std::basic_string<TCHAR> tInputSceneFile, std::basic_string<TCHAR> tOutputSceneFile, std::basic_string<TCHAR> tPipelineFilePath );

	Simplygon::EErrorCodes RunPipelineFromFile( std::basic_string<TCHAR> tPipelineFilePath,
	                                            std::basic_string<TCHAR> tSceneInputFilePath,
	                                            std::basic_string<TCHAR> tSceneOutputFilePath );
	Simplygon::EErrorCodes RunPipelineExternallyFromFile( std::basic_string<TCHAR> tPipelineFilePath,
	                                                      std::basic_string<TCHAR> tSceneInputFilePath,
	                                                      std::basic_string<TCHAR> tSceneOutputFilePath );

	std::vector<Simplygon::spScene> RunProcess( const Simplygon::spScene sgInputScene, std::basic_string<TCHAR> tPipelineFilePath, PBatchType batchType );
	bool RunProcess( std::basic_string<TCHAR> tInputSceneFile,
	                 std::basic_string<TCHAR> tOutputSceneFile,
	                 std::basic_string<TCHAR> tPipelineFilePath,
	                 PBatchType batchType );

	DWORD WaitForProcess( HANDLE& hProcess );
	DWORD PostProgress();

	public:
	SimplygonProcessingModule();
	~SimplygonProcessingModule();

	std::vector<Simplygon::spScene> RunPipeline( const Simplygon::spScene sgInputScene,
	                                             const Simplygon::spPipeline sgPipeline,
	                                             Simplygon::EPipelineRunMode runMode,
	                                             std::vector<std::string>& errorMessages,
	                                             std::vector<std::string>& warningMessages );

	std::vector<std::basic_string<TCHAR>> RunPipelineOnFile( std::basic_string<TCHAR> tInputSceneFile,
	                                                         std::basic_string<TCHAR> tOutputSceneFile,
	                                                         Simplygon::spPipeline sgPipeline,
	                                                         Simplygon::EPipelineRunMode runMode,
	                                                         std::vector<std::string>& errorMessages,
	                                                         std::vector<std::string>& warningMessages );

	void SetProgressObserver( Simplygon::Observer* progressObserver );
	void SetErrorHandler( Simplygon::ErrorHandler* errorHandler );

	void SetTextureOutputDirectory( std::basic_string<TCHAR> tTexturePath );
	void SetWorkDirectory( std::basic_string<TCHAR> tWorkDirectory );
	void SetExternalBatchPath( std::basic_string<TCHAR> tBatchPath );
};
