// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"
#include "SimplygonPipelineCmd.h"
#include "SimplygonInit.h"
#include "Shared.h"
#include "PipelineHelper.h"

#include "maya/MArgDatabase.h"
#include "maya/MGlobal.h"

extern SimplygonInitClass* SimplygonInitInstance;

const char* cPipeline_Create = "c";                      // 9.0
const char* cPipeline_Delete = "d";                      // 9.0
const char* cPipeline_Clone = "cln";                     // 9.2
const char* cPipeline_Load = "l";                        // 9.0
const char* cPipeline_Save = "s";                        // 9.0
const char* cPipeline_Clear = "cl";                      // 9.0
const char* cPipeline_GetSetting = "gs";                 // 9.0
const char* cPipeline_SetSetting = "ss";                 // 9.0
const char* cPipeline_Value = "v";                       // 9.0
const char* cPipeline_Type = "t";                        // 9.0
const char* cPipeline_All = "a";                         // 9.0
const char* cPipeline_AddMaterialCaster = "amc";         // 9.0
const char* cPipeline_AddCascadedPipeline = "-acp";      // 9.0
const char* cPipeline_GetCascadedPipeline = "-gcp";      // 9.0+
const char* cPipeline_GetCascadedPipelineCount = "-gcc"; // 9.0+
const char* cPipeline_GetMaterialCasterCount = "-gmc";   // 9.1+
const char* cPipeline_GetMaterialCasterType = "-gmt";    // 9.1+

SimplygonPipelineCmd::SimplygonPipelineCmd()
{
}

SimplygonPipelineCmd::~SimplygonPipelineCmd()
{
}

void SimplygonPipelineCmd::Callback( std::basic_string<TCHAR> tId, bool error, std::basic_string<TCHAR> tMessage, int progress )
{
}

MStatus SimplygonPipelineCmd::doIt( const MArgList& mArgList )
{
	// parse and setup arguments
	const MStatus mStatus = this->ParseArguments( mArgList );
	if( !mStatus )
		return mStatus;

	return MStatus::kSuccess;
}

MStatus SimplygonPipelineCmd::redoIt()
{
	return MStatus::kSuccess;
}

MStatus SimplygonPipelineCmd::undoIt()
{
	return MStatus::kSuccess;
}

bool SimplygonPipelineCmd::isUndoable() const
{
	return true;
}

void* SimplygonPipelineCmd::creator()
{
	return new SimplygonPipelineCmd();
}

template <typename T, typename Y> Y ChangeType( T value )
{
	return (Y)value;
}

template <typename T> bool ChangeTypeToBool( T value )
{
	return !!value;
}

template <typename T> bool SetSetting( INT64 pipelineId, std::basic_string<TCHAR> tPipelineSettingPath, T valueToSet, uint sgParameterType )
{
	if( sgParameterType == SG_SETTINGVALUETYPE_INT )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, ChangeType<T, int>( valueToSet ) );
	}
	else if( sgParameterType == SG_SETTINGVALUETYPE_DOUBLE )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, ChangeType<T, double>( valueToSet ) );
	}
	else if( sgParameterType == SG_SETTINGVALUETYPE_UINT )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, ChangeType<T, uint>( valueToSet ) );
	}
	else if( sgParameterType == SG_SETTINGVALUETYPE_BOOL )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, ChangeTypeToBool<T>( valueToSet ) );
	}
	else if( sgParameterType == SG_SETTINGVALUETYPE_STRING )
	{
		return PipelineHelper::Instance()->SetPipelineSetting( pipelineId, tPipelineSettingPath, valueToSet );
	}

	return false;
}

MSyntax SimplygonPipelineCmd::createSyntax()
{
	MStatus mStatus;
	MSyntax mSyntax;

	mStatus = mSyntax.addFlag( cPipeline_Create, "Create", MSyntax::kString );

	mStatus = mSyntax.addFlag( cPipeline_Delete, "Delete" );

	mStatus = mSyntax.addFlag( cPipeline_Clone, "Clone" );

	mStatus = mSyntax.addFlag( cPipeline_Load, "Load", MSyntax::kString );

	mStatus = mSyntax.addFlag( cPipeline_Save, "Save", MSyntax::kString );

	mStatus = mSyntax.addFlag( cPipeline_Clear, "Clear" );

	mStatus = mSyntax.addFlag( cPipeline_GetSetting, "GetSetting", MSyntax::kString );

	mStatus = mSyntax.addFlag( cPipeline_SetSetting, "SetSetting", MSyntax::kString );

	mStatus = mSyntax.addFlag( cPipeline_Value, "Value", MSyntax::kString );

	mStatus = mSyntax.addFlag( cPipeline_Type, "Type" );

	mStatus = mSyntax.addFlag( cPipeline_All, "All" );

	mStatus = mSyntax.addFlag( cPipeline_AddMaterialCaster, "AddMaterialCaster", MSyntax::kString );

	mStatus = mSyntax.addFlag( cPipeline_AddCascadedPipeline, "AddCascadedPipeline", MSyntax::kLong );

	mStatus = mSyntax.addFlag( cPipeline_GetCascadedPipeline, "GetCascadedPipeline", MSyntax::kLong );

	mStatus = mSyntax.addFlag( cPipeline_GetCascadedPipelineCount, "GetCascadedPipelineCount" );

	mStatus = mSyntax.addFlag( cPipeline_GetMaterialCasterCount, "GetMaterialCasterCount" );

	mStatus = mSyntax.addFlag( cPipeline_GetMaterialCasterType, "GetMaterialCasterType", MSyntax::kLong );

	mSyntax.addArg( MSyntax::kLong );

	mSyntax.enableEdit( false );
	mSyntax.enableQuery( false );

	return mSyntax;
}

MStatus GetStringValue( const MArgDatabase& mArgData, MPxCommand* mResult, MStatus& mStatus )
{
	mStatus = MStatus::kSuccess;

	const uint flagCount = mArgData.numberOfFlagUses( cPipeline_Value );
	for( uint i = 0; i < flagCount; ++i )
	{
		MArgList mArgList;
		mStatus = mArgData.getFlagArgumentList( cPipeline_Value, i, mArgList );
		if( !mStatus )
			return mStatus;

		MString mTargetValue = mArgList.asString( 0, &mStatus );
		if( !mStatus )
			return mStatus;

		mResult->setResult( mTargetValue );
	}

	return mStatus;
}

MStatus SimplygonPipelineCmd::ParseArguments( const MArgList& mArgs )
{
	MStatus mStatus = MStatus::kSuccess;
	MArgDatabase mArgData( syntax(), mArgs, &mStatus );
	this->clearResult();

	// create pipeline
	if( mArgData.isFlagSet( cPipeline_Create ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cPipeline_Create );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cPipeline_Create, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mPipelineType = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			INT64 pipelineId = 0;
			try
			{
				pipelineId = PipelineHelper::Instance()->CreateSettingsPipeline( mPipelineType.asChar() );
			}
			catch( std::exception ex )
			{
				MGlobal::displayError( MString( "ParseArguments::Create failed with an error: " ) + ex.what() );
				mStatus = MStatus::kFailure;
			}
			catch( ... )
			{
				MGlobal::displayError( MString( "ParseArguments::Create failed with an unknown error." ) );
				mStatus = MStatus::kFailure;
			}

			this->setResult( (uint)pipelineId );
		}
	}

	// load pipeline
	if( mArgData.isFlagSet( cPipeline_Load ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cPipeline_Load );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cPipeline_Load, i, mArgList );
			if( !mStatus )
				return mStatus;

			MString mFilePath = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			INT64 pipelineId = 0;
			try
			{
				pipelineId = PipelineHelper::Instance()->LoadSettingsPipeline( mFilePath.asChar() );
			}
			catch( std::exception ex )
			{
				MGlobal::displayError( MString( "ParseArguments::Load failed with an error: " ) + ex.what() );
				mStatus = MStatus::kFailure;
			}
			catch( ... )
			{
				MGlobal::displayError( MString( "ParseArguments::Load failed with an unknown error." ) );
				mStatus = MStatus::kFailure;
			}

			this->setResult( (uint)pipelineId );
		}
	}

	// save pipeline
	if( mArgData.isFlagSet( cPipeline_Save ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cPipeline_Save );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cPipeline_Save, i, mArgList );
			if( !mStatus )
				return mStatus;

			int pipelineId = 0;
			mStatus = mArgData.getCommandArgument( 0, pipelineId );
			if( !mStatus )
				return mStatus;

			MString mFilePath = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			bool bSaved = false;
			try
			{
				bSaved = PipelineHelper::Instance()->SaveSettingsPipeline( pipelineId, mFilePath.asChar() );
			}
			catch( std::exception ex )
			{
				MGlobal::displayError( MString( "ParseArguments::Save failed with an error: " ) + ex.what() );
				mStatus = MStatus::kFailure;
			}
			catch( ... )
			{
				MGlobal::displayError( MString( "ParseArguments::Save failed with an unknown error." ) );
				mStatus = MStatus::kFailure;
			}

			this->setResult( bSaved );
		}
	}

	// clone pipeline
	if( mArgData.isFlagSet( cPipeline_Clone ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cPipeline_Clone );
		if( flagCount > 0 )
		{
			int pipelineId = 0;
			mStatus = mArgData.getCommandArgument( 0, pipelineId );
			if( !mStatus )
				return mStatus;

			int clonedPipelineId = -1;
			try
			{
				clonedPipelineId = (int)PipelineHelper::Instance()->CloneSettingsPipeline( pipelineId );
			}
			catch( std::exception ex )
			{
				MGlobal::displayError( MString( "ParseArguments::Clone failed with an error: " ) + ex.what() );
				mStatus = MStatus::kFailure;
			}
			catch( ... )
			{
				MGlobal::displayError( MString( "ParseArguments::Clone failed with an unknown error." ) );
				mStatus = MStatus::kFailure;
			}

			this->setResult( clonedPipelineId );
		}
	}

	// delete pipeline
	if( mArgData.isFlagSet( cPipeline_Delete ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cPipeline_Delete );
		if( flagCount > 0 )
		{
			int pipelineId = 0;
			mStatus = mArgData.getCommandArgument( 0, pipelineId );
			if( !mStatus )
				return mStatus;

			bool bRemoved = false;
			try
			{
				bRemoved = PipelineHelper::Instance()->RemoveSettingsPipeline( pipelineId );
			}
			catch( std::exception ex )
			{
				MGlobal::displayError( MString( "ParseArguments::Delete failed with an error: " ) + ex.what() );
				mStatus = MStatus::kFailure;
			}
			catch( ... )
			{
				MGlobal::displayError( MString( "ParseArguments::Delete failed with an unknown error." ) );
				mStatus = MStatus::kFailure;
			}

			this->setResult( bRemoved );
		}
	}

	// clear all pipelines
	if( mArgData.isFlagSet( cPipeline_Clear ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cPipeline_Clear );
		if( flagCount > 0 )
		{
			bool bCleared = false;
			try
			{
				bCleared = PipelineHelper::Instance()->ClearAllSettingsPipelines();
			}
			catch( std::exception ex )
			{
				MGlobal::displayError( MString( "ParseArguments::Clear failed with an error: " ) + ex.what() );
				mStatus = MStatus::kFailure;
			}
			catch( ... )
			{
				MGlobal::displayError( MString( "ParseArguments::Clear failed with an unknown error." ) );
				mStatus = MStatus::kFailure;
			}

			this->setResult( bCleared );
		}
	}

	// set setting
	if( mArgData.isFlagSet( cPipeline_SetSetting ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cPipeline_SetSetting );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cPipeline_SetSetting, i, mArgList );
			if( !mStatus )
				return mStatus;

			int pipelineId = 0;
			mStatus = mArgData.getCommandArgument( 0, pipelineId );
			if( !mStatus )
				return mStatus;

			MString mObjectParameter = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			MPxCommand mCommand;
			mStatus = GetStringValue( mArgData, &mCommand, mStatus );
			if( !mStatus )
				return mStatus;

			const MResultType mResultType = mCommand.currentResultType();
			MString mTargetValue = mCommand.currentStringResult( &mStatus );
			if( !mStatus )
				return mStatus;

			bool bSet = false;

			try
			{
				bSet = PipelineHelper::Instance()->SetPipelineSetting( pipelineId, mObjectParameter.asChar(), mTargetValue.asChar() );
			}
			catch( std::exception ex )
			{
				MGlobal::displayError( MString( "ParseArguments::SetSetting failed with an error: " ) + ex.what() );
				mStatus = MStatus::kFailure;
			}
			catch( ... )
			{
				MGlobal::displayError( MString( "ParseArguments::SetSetting failed with an unknown error." ) );
				mStatus = MStatus::kFailure;
			}

			this->setResult( bSet );
		}
	}

	// get setting
	if( mArgData.isFlagSet( cPipeline_GetSetting ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cPipeline_GetSetting );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cPipeline_GetSetting, i, mArgList );
			if( !mStatus )
				return mStatus;

			int pipelineId = 0;
			mStatus = mArgData.getCommandArgument( 0, pipelineId );
			if( !mStatus )
				return mStatus;

			MString mParameterName = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			std::basic_string<TCHAR> tPipelineSettingPath = mParameterName.asChar();

			const ESettingValueType sgParameterType = PipelineHelper::Instance()->GetPipelineSettingType( pipelineId, tPipelineSettingPath );

			bool bSet = false;
			bool bInvalidType = false;

			try
			{
				if( sgParameterType == ESettingValueType::Double )
				{
					double dValue = 0.0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, dValue );
					this->setResult( dValue );
				}
				else if( sgParameterType == ESettingValueType::Bool )
				{
					bool bValue = false;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, bValue );
					this->setResult( bValue );
				}
				else if( sgParameterType == ESettingValueType::Int )
				{
					int iValue = 0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, iValue );
					this->setResult( iValue );
				}
				else if( sgParameterType == ESettingValueType::String )
				{
					std::basic_string<TCHAR> tValue = _T("");
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, tValue );
					this->setResult( tValue.c_str() );
				}
				else if( sgParameterType == ESettingValueType::Uint )
				{
					uint uValue = false;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, uValue );

					if( uValue > INT_MAX )
					{
						std::basic_string<TCHAR> tErrorMessage = _T("ParseArguments::GetSetting: the setting (");
						tErrorMessage += tPipelineSettingPath;
						tErrorMessage += _T(") of type UINT was capped to INT_MAX due to restrictions in Maya.");
						MGlobal::displayWarning( tErrorMessage.c_str() );
					}

					uValue = uValue > INT_MAX ? (uint)INT_MAX : uValue;

					this->setResult( uValue );
				}
				else if( sgParameterType == ESettingValueType::EPipelineRunMode )
				{
					EPipelineRunMode eValue = (EPipelineRunMode)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::EChartAggregatorMode )
				{
					EChartAggregatorMode eValue = (EChartAggregatorMode)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::ETexcoordGeneratorType )
				{
					ETexcoordGeneratorType eValue = (ETexcoordGeneratorType)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::EOcclusionMode )
				{
					EOcclusionMode eValue = (EOcclusionMode)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::EStopCondition )
				{
					EStopCondition eValue = (EStopCondition)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::EDataCreationPreferences )
				{
					EDataCreationPreferences eValue = (EDataCreationPreferences)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::EReductionHeuristics )
				{
					EReductionHeuristics eValue = (EReductionHeuristics)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::EWeightsFromColorMode )
				{
					EWeightsFromColorMode eValue = (EWeightsFromColorMode)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::ESurfaceTransferMode )
				{
					ESurfaceTransferMode eValue = (ESurfaceTransferMode)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::ERemeshingMode )
				{
					ERemeshingMode eValue = (ERemeshingMode)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::ETangentSpaceMethod )
				{
					ETangentSpaceMethod eValue = (ETangentSpaceMethod)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::EGeometryDataFieldType )
				{
					EGeometryDataFieldType eValue = (EGeometryDataFieldType)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::EAtlasFillMode )
				{
					EAtlasFillMode eValue = (EAtlasFillMode)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::EDitherPatterns )
				{
					EDitherPatterns eValue = (EDitherPatterns)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::EComputeVisibilityMode )
				{
					EComputeVisibilityMode eValue = (EComputeVisibilityMode)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::ESurfaceAreaScale )
				{
					ESurfaceAreaScale eValue = (ESurfaceAreaScale)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::EImpostorType )
				{
					EImpostorType eValue = (EImpostorType)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::ESymmetryAxis )
				{
					ESymmetryAxis eValue = (ESymmetryAxis)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::EPixelFormat )
				{
					EPixelFormat eValue = (EPixelFormat)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::EColorComponent )
				{
					EColorComponent eValue = (EColorComponent)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::EHoleFilling )
				{
					EHoleFilling eValue = (EHoleFilling)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::EImageOutputFormat )
				{
					EImageOutputFormat eValue = (EImageOutputFormat)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::EDDSCompressionType )
				{
					EDDSCompressionType eValue = (EDDSCompressionType)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::EBillboardMode )
				{
					EBillboardMode eValue = (EBillboardMode)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else if( sgParameterType == ESettingValueType::EOpacityType )
				{
					EOpacityType eValue = (EOpacityType)0;
					bSet = PipelineHelper::Instance()->GetPipelineSetting( pipelineId, tPipelineSettingPath, eValue );
					this->setResult( (int)eValue );
				}
				else
				{
					bInvalidType = true;
				}
			}
			catch( const std::exception& ex )
			{
				std::basic_string<TCHAR> tErrorMessage = _T("ParseArguments::GetSetting: Failed to get setting (");
				tErrorMessage += tPipelineSettingPath;
				tErrorMessage += _T(")\n");
				tErrorMessage += ConstCharPtrToLPCTSTR( ex.what() );
				MGlobal::displayError( tErrorMessage.c_str() );
				mStatus = MStatus::kFailure;
			}

			if( !bSet )
			{
				std::basic_string<TCHAR> tErrorMessage = _T("ParseArguments::GetSetting: Failed to get setting (");
				tErrorMessage += tPipelineSettingPath;
				if( sgParameterType == ESettingValueType::Invalid )
				{
					tErrorMessage += _T(") - ");
					tErrorMessage += _T("The type is not supported and/or the setting does not exist.");
				}
				else if( bInvalidType )
				{
					tErrorMessage += _T(") - ");
					tErrorMessage += _T("The type is not supported, supported return types are: Int, UInt, Double, Boolean, String.");
				}
				else
				{
					tErrorMessage += _T(").");
				}

				MGlobal::displayError( tErrorMessage.c_str() );
				mStatus = MStatus::kFailure;
			}
		}
	}

	// get pipeline type (not setting type)
	if( mArgData.isFlagSet( cPipeline_Type ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cPipeline_Type );
		if( flagCount > 0 )
		{
			int pipelineId = 0;
			mStatus = mArgData.getCommandArgument( 0, pipelineId );
			if( !mStatus )
				return mStatus;

			std::string sPipelineType = "";
			try
			{
				sPipelineType = PipelineHelper::Instance()->GetPipelineType( pipelineId );
			}
			catch( std::exception ex )
			{
				MGlobal::displayError( MString( "ParseArguments::GetType failed with an error: " ) + ex.what() );
				mStatus = MStatus::kFailure;
			}
			catch( ... )
			{
				MGlobal::displayError( MString( "ParseArguments::GetType failed with an unknown error." ) );
				mStatus = MStatus::kFailure;
			}

			this->setResult( sPipelineType.c_str() );
		}
	}

	// get all pipeline ids
	if( mArgData.isFlagSet( cPipeline_All ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cPipeline_All );
		for( uint i = 0; i < flagCount; ++i )
		{
			std::vector<INT64> pipelineIds;
			try
			{
				pipelineIds = PipelineHelper::Instance()->GetPipelines();
			}
			catch( std::exception ex )
			{
				MGlobal::displayError( MString( "ParseArguments::GetPipelines failed with an error: " ) + ex.what() );
				mStatus = MStatus::kFailure;
			}
			catch( ... )
			{
				MGlobal::displayError( MString( "ParseArguments::GetPipelines failed with an unknown error." ) );
				mStatus = MStatus::kFailure;
			}

			for( uint p = 0; p < pipelineIds.size(); ++p )
			{
				this->appendToResult( (int)pipelineIds[ i ] );
			}
		}
	}

	// add material caster
	if( mArgData.isFlagSet( cPipeline_AddMaterialCaster ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cPipeline_AddMaterialCaster );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cPipeline_AddMaterialCaster, i, mArgList );
			if( !mStatus )
				return mStatus;

			int pipelineId = 0;
			mStatus = mArgData.getCommandArgument( 0, pipelineId );
			if( !mStatus )
				return mStatus;

			MString mMaterialCasterType = mArgList.asString( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			int casterIndex = 0;
			try
			{
				casterIndex = PipelineHelper::Instance()->AddMaterialCaster( pipelineId, mMaterialCasterType.asChar(), MAYA_MATERIAL_CHANNEL_TRANSPARENCY );
			}
			catch( std::exception ex )
			{
				MGlobal::displayError( MString( "ParseArguments::AddMaterialCaster failed with an error: " ) + ex.what() );
				mStatus = MStatus::kFailure;
			}
			catch( ... )
			{
				MGlobal::displayError( MString( "ParseArguments::AddMaterialCaster failed with an unknown error." ) );
				mStatus = MStatus::kFailure;
			}

			this->setResult( casterIndex );
		}
	}

	// add cascaded pipeline
	if( mArgData.isFlagSet( cPipeline_AddCascadedPipeline ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cPipeline_AddCascadedPipeline );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cPipeline_AddCascadedPipeline, i, mArgList );
			if( !mStatus )
				return mStatus;

			int pipelineId = 0;
			mStatus = mArgData.getCommandArgument( 0, pipelineId );
			if( !mStatus )
				return mStatus;

			const int cascadedPipelineId = mArgList.asInt( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			bool bCascadedPipelineAdded = false;
			try
			{
				bCascadedPipelineAdded = PipelineHelper::Instance()->AddCascadedPipeline( pipelineId, cascadedPipelineId );
			}
			catch( std::exception ex )
			{
				MGlobal::displayError( MString( "ParseArguments::AddCascadedPipeline failed with an error: " ) + ex.what() );
				mStatus = MStatus::kFailure;
			}
			catch( ... )
			{
				MGlobal::displayError( MString( "ParseArguments::AddCascadedPipeline failed with an unknown error." ) );
				mStatus = MStatus::kFailure;
			}

			this->setResult( bCascadedPipelineAdded );
		}
	}

	// get cascaded pipeline handle
	if( mArgData.isFlagSet( cPipeline_GetCascadedPipeline ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cPipeline_GetCascadedPipeline );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cPipeline_GetCascadedPipeline, i, mArgList );
			if( !mStatus )
				return mStatus;

			int pipelineId = 0;
			mStatus = mArgData.getCommandArgument( 0, pipelineId );
			if( !mStatus )
				return mStatus;

			const int cascadedPipelineIndex = mArgList.asInt( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			INT64 cascadedPipelineHandle = -1;
			try
			{
				cascadedPipelineHandle = PipelineHelper::Instance()->GetCascadedPipeline( pipelineId, cascadedPipelineIndex );
			}
			catch( std::exception ex )
			{
				MGlobal::displayError( MString( "ParseArguments::GetCascadedPipeline failed with an error: " ) + ex.what() );
				mStatus = MStatus::kFailure;
			}
			catch( ... )
			{
				MGlobal::displayError( MString( "ParseArguments::GetCascadedPipeline failed with an unknown error." ) );
				mStatus = MStatus::kFailure;
			}

			this->setResult( (int)cascadedPipelineHandle );
		}
	}

	// get cascaded pipeline count
	if( mArgData.isFlagSet( cPipeline_GetCascadedPipelineCount ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cPipeline_GetCascadedPipelineCount );
		for( uint i = 0; i < flagCount; ++i )
		{
			int pipelineId = 0;
			mStatus = mArgData.getCommandArgument( 0, pipelineId );
			if( !mStatus )
				return mStatus;

			INT64 numCascadedPipelines = -1;
			try
			{
				numCascadedPipelines = PipelineHelper::Instance()->GetCascadedPipelineCount( pipelineId );
			}
			catch( std::exception ex )
			{
				MGlobal::displayError( MString( "ParseArguments::GetCascadedPipelineCount failed with an error: " ) + ex.what() );
				mStatus = MStatus::kFailure;
			}
			catch( ... )
			{
				MGlobal::displayError( MString( "ParseArguments::GetCascadedPipelineCount failed with an unknown error." ) );
				mStatus = MStatus::kFailure;
			}

			this->setResult( (int)numCascadedPipelines );
		}
	}

	// get number of material casters for specified pipeline
	if( mArgData.isFlagSet( cPipeline_GetMaterialCasterCount ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cPipeline_GetMaterialCasterCount );
		if( flagCount > 0 )
		{
			int pipelineId = 0;
			mStatus = mArgData.getCommandArgument( 0, pipelineId );
			if( !mStatus )
				return mStatus;

			INT64 numMaterialCasters = 0;
			try
			{
				numMaterialCasters = PipelineHelper::Instance()->GetMaterialCasterCount( pipelineId );
			}
			catch( std::exception ex )
			{
				MGlobal::displayError( MString( "ParseArguments::GetMaterialCasterCount failed with an error: " ) + ex.what() );
				mStatus = MStatus::kFailure;
			}
			catch( ... )
			{
				MGlobal::displayError( MString( "ParseArguments::GetMaterialCasterCount failed with an unknown error." ) );
				mStatus = MStatus::kFailure;
			}

			this->setResult( (int)numMaterialCasters );
		}
	}

	// get material caster type for specified pipeline
	if( mArgData.isFlagSet( cPipeline_GetMaterialCasterType ) )
	{
		const uint flagCount = mArgData.numberOfFlagUses( cPipeline_GetMaterialCasterType );
		for( uint i = 0; i < flagCount; ++i )
		{
			MArgList mArgList;
			mStatus = mArgData.getFlagArgumentList( cPipeline_GetMaterialCasterType, i, mArgList );
			if( !mStatus )
				return mStatus;

			int pipelineId = 0;
			mStatus = mArgData.getCommandArgument( 0, pipelineId );
			if( !mStatus )
				return mStatus;

			const int materialCasterIndex = mArgList.asInt( 0, &mStatus );
			if( !mStatus )
				return mStatus;

			std::string materialCasterType = "";
			try
			{
				materialCasterType = PipelineHelper::Instance()->GetMaterialCasterType( pipelineId, materialCasterIndex );
			}
			catch( std::exception ex )
			{
				MGlobal::displayError( MString( "ParseArguments::GetMaterialCasterType failed with an error: " ) + ex.what() );
				mStatus = MStatus::kFailure;
			}
			catch( ... )
			{
				MGlobal::displayError( MString( "ParseArguments::GetMaterialCasterType failed with an unknown error." ) );
				mStatus = MStatus::kFailure;
			}

			this->setResult( materialCasterType.c_str() );
		}
	}

	return mStatus;
}
