// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once
#include "Shared.h"
class PipelineHelper
{
	public:
	std::map<INT64, Simplygon::spPipeline> nameToSettingsPipeline;

	INT64 LoadSettingsPipeline( std::basic_string<TCHAR> tPipelineFilePath );
	INT64 CreateSettingsPipeline( std::basic_string<TCHAR> tPipelineType );
	bool RemoveSettingsPipeline( const INT64 pipelineId );
	bool ClearAllSettingsPipelines();

	bool SaveSettingsPipeline( const INT64 pipelineId, std::basic_string<TCHAR> tPipelineFilePath );

	std::vector<INT64> GetPipelines();
	std::basic_string<TCHAR> GetPipelineType( const INT64 pipelineId );

	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, float& fValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, double& fValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, bool& bValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, int& iValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, uint& uValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, std::basic_string<TCHAR>& tValue );

	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EPipelineRunMode& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EChartAggregatorMode& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ETexcoordGeneratorType& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EOcclusionMode& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EStopCondition& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EDataCreationPreferences& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EReductionHeuristics& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EWeightsFromColorMode& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ESurfaceTransferMode& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ERemeshingMode& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ETangentSpaceMethod& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EGeometryDataFieldType& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EAtlasFillMode& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EDitherPatterns& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EComputeVisibilityMode& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ESurfaceAreaScale& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EImpostorType& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ESymmetryAxis& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EPixelFormat& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EColorComponent& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EHoleFilling& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EImageOutputFormat& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EDDSCompressionType& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EBillboardMode& eValue );
	bool GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EOpacityType& eValue );

	ESettingValueType GetPipelineSettingType( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath );

	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, bool value );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, float value );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, int value );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, INT64 value );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, double value );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, const TCHAR* value );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, uint uValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EPipelineRunMode eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EChartAggregatorMode eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ETexcoordGeneratorType eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EOcclusionMode eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EStopCondition eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EDataCreationPreferences eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EReductionHeuristics eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EWeightsFromColorMode eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ESurfaceTransferMode eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ERemeshingMode eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ETangentSpaceMethod eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EGeometryDataFieldType eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EAtlasFillMode eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EDitherPatterns eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EComputeVisibilityMode eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ESurfaceAreaScale eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EImpostorType eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ESymmetryAxis eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EPixelFormat eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EColorComponent eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EHoleFilling eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EImageOutputFormat eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EDDSCompressionType eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EBillboardMode eValue );
	bool SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EOpacityType eValue );

	int AddMaterialCaster(
	    const INT64 pipelineId,
	    std::basic_string<TCHAR> tCasterType,
	    std::basic_string<TCHAR> tDefaultOpacityChannel = std::basic_string<TCHAR>( ConstCharPtrToLPCTSTR( Simplygon::SG_MATERIAL_CHANNEL_OPACITY ) ) );

	bool AddCascadedPipeline( const INT64 pipelineId, const INT64 cascadedPipelineId );
	INT64 GetCascadedPipeline( const INT64 pipelineId, const uint childIndex );
	uint GetCascadedPipelineCount( const INT64 pipelineId );

	uint GetMaterialCasterCount( const INT64 pipelineId );
	std::basic_string<TCHAR> GetMaterialCasterType( const INT64 pipelineId, uint materialCasterindex );

	static PipelineHelper* Instance();

	private:
	PipelineHelper();
	~PipelineHelper();
	INT64 pipelineCounter;
	static PipelineHelper* Initialize();
	static PipelineHelper* pipelineHelper;
};
