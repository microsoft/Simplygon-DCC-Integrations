// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"
#include "PipelineHelper.h"
#include "Shared.h"
#include "SimplygonInit.h"

using namespace Simplygon;

extern SimplygonInitClass* SimplygonInitInstance;

PipelineHelper* PipelineHelper::pipelineHelper = nullptr;

PipelineHelper::PipelineHelper()
{
	this->pipelineCounter = 0;
}

inline PipelineHelper::~PipelineHelper()
{
	this->nameToSettingsPipeline.clear();
}

PipelineHelper* PipelineHelper::Initialize()
{
	if( sg == nullptr )
	{
		const bool bInitialized = SimplygonInitInstance->Initialize();
		if( !bInitialized )
		{
			throw std::exception( "Failed to initialize Simplygon." );
		}
	}

	return new PipelineHelper();
}

INT64 PipelineHelper::CreateSettingsPipeline( std::basic_string<TCHAR> tPipelineType )
{
	const INT64 currentCounter = pipelineCounter++;

	spPipeline sgPipeline = Simplygon::NullPtr;

	// strip "Pipeline" if any
	const size_t index = tPipelineType.find( _T("Pipeline") );
	if( index != std::basic_string<TCHAR>::npos )
	{
		tPipelineType = tPipelineType.replace( index, 8, _T("") );
	}

	if( tPipelineType == _T("Reduction") )
	{
		sgPipeline = spPipeline::SafeCast( sg->CreateReductionPipeline() );
	}
	else if( tPipelineType == _T("QuadReduction") )
	{
		sgPipeline = spPipeline::SafeCast( sg->CreateQuadReductionPipeline() );
	}
	else if( tPipelineType == _T("Aggregation") )
	{
		sgPipeline = spPipeline::SafeCast( sg->CreateAggregationPipeline() );
	}
	else if( tPipelineType == _T("Remeshing") )
	{
		sgPipeline = spPipeline::SafeCast( sg->CreateRemeshingPipeline() );
	}
	else if( tPipelineType == _T("BillboardCloudVegetation") )
	{
		sgPipeline = spPipeline::SafeCast( sg->CreateBillboardCloudVegetationPipeline() );
	}
	else if( tPipelineType == _T("BillboardCloud") )
	{
		sgPipeline = spPipeline::SafeCast( sg->CreateBillboardCloudPipeline() );
	}
	else if( tPipelineType == _T("Flipbook") )
	{
		sgPipeline = spPipeline::SafeCast( sg->CreateFlipbookPipeline() );
	}
	else if( tPipelineType == _T("ImpostorFromSingleView") )
	{
		sgPipeline = spPipeline::SafeCast( sg->CreateImpostorFromSingleViewPipeline() );
	}
	else if( tPipelineType == _T("Passthrough") )
	{
		sgPipeline = spPipeline::SafeCast( sg->CreatePassthroughPipeline() );
	}
	else if( tPipelineType == _T("HighDensityMeshReduction") )
	{
		sgPipeline = spPipeline::SafeCast( sg->CreateHighDensityMeshReductionPipeline() );
	}
	else
	{
		// not supported
		std::string sErrorMessage = "The pipeline type is not supported - ";
		sErrorMessage += LPCTSTRToConstCharPtr( tPipelineType.c_str() );

		throw std::exception( sErrorMessage.c_str() );
	}

	sgPipeline->GetPipelineSettings()->SetValidateParameterNames( true );

	this->nameToSettingsPipeline.insert( std::pair<INT64, spPipeline>( currentCounter, sgPipeline ) );

	return currentCounter;
}

bool PipelineHelper::RemoveSettingsPipeline( const INT64 pipelineId )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}
	else
	{
		this->nameToSettingsPipeline.erase( pipelineIterator );
	}

	return true;
}

bool PipelineHelper::ClearAllSettingsPipelines()
{
	this->nameToSettingsPipeline.clear();
	this->pipelineCounter = 0;
	return true;
}

INT64 PipelineHelper::LoadSettingsPipeline( std::basic_string<TCHAR> tPipelineFilePath )
{
	const INT64 currentCounter = pipelineCounter++;

	Simplygon::spPipelineSerializer sgSerializer = sg->CreatePipelineSerializer();
	if( sgSerializer.IsNull() )
	{
		std::string sErrorMessage = "Failed to create pipeline serializer.";
		throw std::exception( sErrorMessage.c_str() );
	}

	spPipeline sgPipeline = sgSerializer->LoadPipelineFromFile( LPCTSTRToConstCharPtr( tPipelineFilePath.c_str() ) );
	if( sgPipeline.IsNull() )
	{
		throw std::exception( "Could not load pipeline from file." );
	}

	sgPipeline->GetPipelineSettings()->SetValidateParameterNames( true );

	this->nameToSettingsPipeline.insert( std::pair<INT64, spPipeline>( currentCounter, sgPipeline ) );

	return currentCounter;
}

bool PipelineHelper::SaveSettingsPipeline( const INT64 pipelineId, std::basic_string<TCHAR> tPipelineFilePath )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	Simplygon::spPipelineSerializer sgSerializer = sg->CreatePipelineSerializer();
	if( sgSerializer.IsNull() )
	{
		std::string sErrorMessage = "Failed to create pipeline serializer.";
		throw std::exception( sErrorMessage.c_str() );
	}

	sgSerializer->SavePipelineToFile( LPCTSTRToConstCharPtr( tPipelineFilePath.c_str() ), sgPipeline );

	return true;
}

INT64 PipelineHelper::CloneSettingsPipeline( const INT64 pipelineId )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;
	spPipeline sgClonedPipeline = sgPipeline->NewCopy();

	if( sgClonedPipeline.IsNull() )
	{
		throw std::exception( "Could not clone the given pipeline, NewCopy returned NULL." );
	}

	const INT64 currentCounter = pipelineCounter++;
	this->nameToSettingsPipeline.insert( std::pair<INT64, spPipeline>( currentCounter, sgClonedPipeline ) );

	return currentCounter;
}

bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, bool& bValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	bValue = sgPipeline->GetBoolParameter( cSettingsPath );

	return true;
}

bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, int& iValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	iValue = sgPipeline->GetIntParameter( cSettingsPath );

	return true;
}

bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, uint& uValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	uValue = sgPipeline->GetUIntParameter( cSettingsPath );

	return true;
}

bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, std::basic_string<TCHAR>& tValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	const bool bSettingExists = sgPipeline->GetParameterType( cSettingsPath ) != ESettingValueType::Invalid;
	if( bSettingExists )
	{
		spString rString = sgPipeline->GetStringParameter( cSettingsPath );
		if( !rString.IsNullOrEmpty() )
		{
			tValue = ConstCharPtrToLPCTSTR( rString );
		}
		else
		{
			tValue = _T("");
		}

		return true;
	}

	return false;
}

bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, float& fValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	fValue = (float)sgPipeline->GetDoubleParameter( cSettingsPath );

	return true;
}

bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, double& dValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	dValue = sgPipeline->GetDoubleParameter( cSettingsPath );

	return true;
}

bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EPipelineRunMode& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EPipelineRunMode)iValue;
		return true;
	}

	return false;
}

bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EChartAggregatorMode& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EChartAggregatorMode)iValue;
		return true;
	}

	return false;
}
bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ETexcoordGeneratorType& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::ETexcoordGeneratorType)iValue;
		return true;
	}

	return false;
}
bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EOcclusionMode& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EOcclusionMode)iValue;
		return true;
	}

	return false;
}
bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EStopCondition& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EStopCondition)iValue;
		return true;
	}

	return false;
}
bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EDataCreationPreferences& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EDataCreationPreferences)iValue;
		return true;
	}

	return false;
}
bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EReductionHeuristics& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EReductionHeuristics)iValue;
		return true;
	}

	return false;
}
bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EWeightsFromColorMode& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EWeightsFromColorMode)iValue;
		return true;
	}

	return false;
}
bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ESurfaceTransferMode& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::ESurfaceTransferMode)iValue;
		return true;
	}

	return false;
}
bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ERemeshingMode& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::ERemeshingMode)iValue;
		return true;
	}

	return false;
}
bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ETangentSpaceMethod& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::ETangentSpaceMethod)iValue;
		return true;
	}

	return false;
}
bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EGeometryDataFieldType& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EGeometryDataFieldType)iValue;
		return true;
	}

	return false;
}
bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EAtlasFillMode& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EAtlasFillMode)iValue;
		return true;
	}

	return false;
}
bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EDitherPatterns& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EDitherPatterns)iValue;
		return true;
	}

	return false;
}
bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EComputeVisibilityMode& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EComputeVisibilityMode)iValue;
		return true;
	}

	return false;
}

bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ESurfaceAreaScale& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::ESurfaceAreaScale)iValue;
		return true;
	}

	return false;
}

bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EImpostorType& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EImpostorType)iValue;
		return true;
	}

	return false;
}

bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ESymmetryAxis& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::ESymmetryAxis)iValue;
		return true;
	}

	return false;
}

bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EPixelFormat& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EPixelFormat)iValue;
		return true;
	}

	return false;
}

bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EColorComponent& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EColorComponent)iValue;
		return true;
	}

	return false;
}

bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EHoleFilling& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EHoleFilling)iValue;
		return true;
	}

	return false;
}

bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EImageOutputFormat& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EImageOutputFormat)iValue;
		return true;
	}

	return false;
}

bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EDDSCompressionType& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EDDSCompressionType)iValue;
		return true;
	}

	return false;
}

bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EBillboardMode& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EBillboardMode)iValue;
		return true;
	}

	return false;
}

bool PipelineHelper::GetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EOpacityType& eValue )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	int iValue = sgPipeline->GetEnumParameter( cSettingsPath );

	if( iValue >= 0 )
	{
		eValue = (Simplygon::EOpacityType)iValue;
		return true;
	}

	return false;
}

ESettingValueType PipelineHelper::GetPipelineSettingType( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->GetParameterType( cSettingsPath );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, bool bValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetBoolParameter( cSettingsPath, bValue );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, float fValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetDoubleParameter( cSettingsPath, (double)fValue );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, int iValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetIntParameter( cSettingsPath, iValue );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, INT64 uValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetUIntParameter( cSettingsPath, (uint)uValue );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, uint uValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetUIntParameter( cSettingsPath, uValue );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, double dValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetDoubleParameter( cSettingsPath, dValue );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, const TCHAR* tValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );
	const char* cValue = LPCTSTRToConstCharPtr( tValue );

	return sgPipeline->SetParameterFromString( cSettingsPath, cValue );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EPipelineRunMode eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EPipelineRunMode );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EChartAggregatorMode eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EChartAggregatorMode );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ETexcoordGeneratorType eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::ETexcoordGeneratorType );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EOcclusionMode eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EOcclusionMode );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EStopCondition eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EStopCondition );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EDataCreationPreferences eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EDataCreationPreferences );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EReductionHeuristics eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EReductionHeuristics );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EWeightsFromColorMode eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EWeightsFromColorMode );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ESurfaceTransferMode eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::ESurfaceTransferMode );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ERemeshingMode eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::ESurfaceTransferMode );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ETangentSpaceMethod eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::ETangentSpaceMethod );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EGeometryDataFieldType eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EGeometryDataFieldType );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EAtlasFillMode eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EAtlasFillMode );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EDitherPatterns eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EDitherPatterns );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EComputeVisibilityMode eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EComputeVisibilityMode );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ESurfaceAreaScale eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::ESurfaceAreaScale );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EImpostorType eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EImpostorType );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::ESymmetryAxis eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::ESymmetryAxis );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EPixelFormat eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EPixelFormat );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EColorComponent eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EColorComponent );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EHoleFilling eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EHoleFilling );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EImageOutputFormat eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EImageOutputFormat );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EDDSCompressionType eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EDDSCompressionType );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EBillboardMode eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EBillboardMode );
}

bool PipelineHelper::SetPipelineSetting( const INT64 pipelineId, std::basic_string<TCHAR> tSettingsPath, Simplygon::EOpacityType eValue )
{
	const std::map<INT64, Simplygon::spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	Simplygon::spPipeline sgPipeline = pipelineIterator->second;

	const char* cSettingsPath = LPCTSTRToConstCharPtr( tSettingsPath.c_str() );

	return sgPipeline->SetEnumParameter( cSettingsPath, (int)eValue, Simplygon::ESettingValueType::EOpacityType );
}

int PipelineHelper::AddMaterialCaster( const INT64 pipelineId, std::basic_string<TCHAR> tCasterType, std::basic_string<TCHAR> tDefaultOpacityChannel )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	// strip "Caster" if any
	const size_t index = tCasterType.find( _T("Caster") );
	if( index != std::basic_string<TCHAR>::npos )
	{
		tCasterType = tCasterType.replace( index, 6, _T("") );
	}

	const char* cCasterType = LPCTSTRToConstCharPtr( tCasterType.c_str() );
	bool bAddedCaster = sgPipeline->AddMaterialCasterByType( cCasterType, 0 ).NonNull();

	if( !bAddedCaster )
	{
		// not supported
		std::string sErrorMessage = "The caster type is not supported - ";
		sErrorMessage += LPCTSTRToConstCharPtr( tCasterType.c_str() );

		throw std::exception( sErrorMessage.c_str() );
	}

	spObjectCollection sgMaterialCasterCollection = sgPipeline->GetMaterialCasters();
	if( sgMaterialCasterCollection.IsNull() || sgMaterialCasterCollection->GetItemCount() == 0 )
	{
		std::string sErrorMessage = "The caster object is invalid or empty where it is not supposed to be.";
		throw std::exception( sErrorMessage.c_str() );
	}

	const uint lastElementIndex = sgMaterialCasterCollection->GetItemCount() - 1;

	spMaterialCaster sgMaterialCaster = spMaterialCaster::SafeCast( sgMaterialCasterCollection->GetItemAsObject( lastElementIndex ) );
	if( sgMaterialCaster.NonNull() )
	{
		const char* cDefaultOpacityChannel = LPCTSTRToConstCharPtr( tDefaultOpacityChannel.c_str() );
		sgMaterialCaster->GetMaterialCasterSettings()->SetOpacityChannel( cDefaultOpacityChannel );
	}

	return (int)lastElementIndex;
}

bool PipelineHelper::AddCascadedPipeline( const INT64 pipelineId, const INT64 cascadedPipelineId )
{
	std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The first pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	pipelineIterator = this->nameToSettingsPipeline.find( cascadedPipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The second pipeline id was not found." );
	}

	spPipeline sgCascadedPipeline = pipelineIterator->second;
	sgPipeline->AddCascadedPipeline( sgCascadedPipeline );

	return true;
}

INT64 PipelineHelper::GetCascadedPipeline( const INT64 pipelineId, const uint childIndex )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The (parent) pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	INT64 currentCounter = -1;
	if( childIndex < sgPipeline->GetCascadedPipelineCount() )
	{
		spPipeline sgCascadedPipeline = sgPipeline->GetCascadedPipelineByIndex( childIndex );

		if( sgCascadedPipeline.NonNull() )
		{
			currentCounter = pipelineCounter++;

			this->nameToSettingsPipeline.insert( std::pair<INT64, spPipeline>( currentCounter, sgCascadedPipeline ) );
		}
	}

	if( currentCounter < 0 )
	{
		throw std::exception( "The child index was not found." );
	}

	return currentCounter;
}

uint PipelineHelper::GetCascadedPipelineCount( const INT64 pipelineId )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The (parent) pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;

	return sgPipeline->GetCascadedPipelineCount();
}

uint PipelineHelper::GetMaterialCasterCount( const INT64 pipelineId )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The (parent) pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;
	return sgPipeline->GetMaterialCasterCount();
}

std::basic_string<TCHAR> PipelineHelper::GetMaterialCasterType( const INT64 pipelineId, uint materialCasterindex )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The (parent) pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;
	spMaterialCaster sgMaterialCaster = sgPipeline->GetMaterialCasterByIndex( materialCasterindex );

	spString sgMaterialCasterType = sgMaterialCaster->GetClass();
	const char* cMaterialCasterType = sgMaterialCasterType.c_str();

	std::basic_string<TCHAR> tMaterialCasterType = ConstCharPtrToLPCTSTR( cMaterialCasterType );

	// remove 'I' from IColorCaster, INormalCaster
	if( tMaterialCasterType.size() > 0 && tMaterialCasterType[ 0 ] == _T( 'I' ) )
	{
		tMaterialCasterType = tMaterialCasterType.replace( 0, 1, _T("") );
	}

	return tMaterialCasterType;
}

PipelineHelper* PipelineHelper::Instance()
{
	if( !pipelineHelper )
	{
		pipelineHelper = Initialize();
	}

	return pipelineHelper;
}

std::vector<INT64> PipelineHelper::GetPipelines()
{
	std::vector<INT64> pipelineIds;
	for( std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.begin();
	     pipelineIterator != this->nameToSettingsPipeline.end();
	     pipelineIterator++ )
	{
		pipelineIds.push_back( pipelineIterator->first );
	}

	return pipelineIds;
}

std::basic_string<TCHAR> PipelineHelper::GetPipelineType( const INT64 pipelineId )
{
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = this->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == this->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	spPipeline sgPipeline = pipelineIterator->second;
	spString rClassName = sgPipeline->GetClass();
	const char* cClassName = rClassName.c_str();

	std::basic_string<TCHAR> tClassName = ConstCharPtrToLPCTSTR( cClassName );

	// remove interface character (I in IReductionPipeline)
	if( tClassName.size() > 0 )
	{
		if( tClassName[ 0 ] == 'I' )
		{
			tClassName = tClassName.substr( 1 );
		}
	}

	return tClassName;
}
