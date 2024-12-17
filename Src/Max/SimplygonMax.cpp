// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"
#include <locale.h>
#include <IDxMaterial.h>
#include <imtl.h>
#include <RTMax.h>
#include <scene/IPhysicalCamera.h>
#include "SimplygonMax.h"
#include <pbbitmap.h>
#include "SimplygonMaxPerVertexData.h"
#include "MaxMaterialNode.h"
#include "NormalCalculator.h"
#include "HelperFunctions.h"
#include "resource_.h"
#include "Common.h"
#include <iomanip>
#include <sstream>
#include <iosfwd>
#include "HelperFunctions.h"
#include "SimplygonConvenienceTemplates.h"
#include "Common.h"
#include <SimplygonProcessingModule.h>
#include "SimplygonInit.h"
#include "PipelineHelper.h"
#include <fstream>

#include "SimplygonLoader.h"
#include "MaterialInfoHandler.h"

#include "NewMaterialMap.h"
#include "MaterialInfo.h"
#include "ImportedTexture.h"

#include "Scene.h"
#include "MeshNode.h"

#include "Common.h"

#include <algorithm>
#include "IColorCorrectionMgr.h"

using namespace Simplygon;

#if( VERSION_INT < 420 )
#ifndef GNORMAL_CLASS_ID
#define GNORMAL_CLASS_ID Class_ID( 0x243e22c6, 0X63F6A014 )
#endif
#endif

#define MaxNumCopyRetries             10u
#define MaxNumMorphTargets            100u
#define MaxNumProgressiveMorphTargets 25u

extern HINSTANCE hInstance;

SimplygonMax* SimplygonMaxInstance = nullptr;
extern SimplygonInitClass* SimplygonInitInstance;

// clamp was deprecated in Max 2024, replacing it with std::clamp
#if MAX_VERSION_MAJOR >= 26
#define _CLAMP( val, min, max ) std::clamp( val, min, max )
#else
#define _CLAMP( val, min, max ) clamp( val, min, max )
#endif

#define TURBOSMOOTH_CLASS_ID       Class_ID( 225606462L, 1226647975L )
#define MORPHER_CLASS_ID           Class_ID( 398157908L, 2781586083L )
#define PHYSICAL_MATERIAL_CLASS_ID Class_ID( 1030429932L, 3735928833L )

PBBitmap* SetupMaxTexture( std::basic_string<TCHAR> tFilePath );
bool TextureHasAlpha( const char* tTextureFilePath );
float GetBitmapTextureGamma( BitmapTex* mBitmapTex );
void GetImageFullFilePath( const TCHAR* tPath, TCHAR* tDestinationPath );
spShadingColorNode CreateColorShadingNetwork( float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f );

class ProgressiveMorphTarget
{
	private:
	INode* targetNode;

	public:
	std::vector<Point3> targetDeltas;
	float targetWeight;

	ProgressiveMorphTarget( size_t vertexCount, float weight )
	    : ProgressiveMorphTarget( nullptr, vertexCount, weight )
	{
	}

	ProgressiveMorphTarget( INode* targetNode, size_t vertexCount, float weight )
	{
		this->targetNode = targetNode;
		this->targetDeltas.resize( vertexCount );
		this->targetWeight = weight;
	}
};

class MorphChannel
{
	private:
	IParamBlock* paramBlock;
	bool isValid;

	int localIndex;
	int vertexCount;

	MorphChannelMetaData* settings;
	INode* sourceNode;
	std::vector<ProgressiveMorphTarget*> morphTargets;

	TSTR name;

	void InitParameters( INode* mSourceNode, IParamBlock* mWeightParamBlock, int channelIndex, MorphChannelMetaData* morpherSettings, TimeValue t )
	{
		this->localIndex = channelIndex;
		this->paramBlock = mWeightParamBlock;
		this->sourceNode = mSourceNode;
		this->vertexCount = 0;
		this->settings = morpherSettings;
		this->isValid = true;
	}

	public:
	MorphChannel( INode* mSourceNode,
	              IParamBlock* mWeightParamBlock,
	              int channelIndex,
	              std::vector<INode*>& mMorphTargets,
	              MorphChannelMetaData* morpherSettings,
	              std::vector<float>& mMorphTargetWeights,
	              TimeValue t )
	{
		InitParameters( mSourceNode, mWeightParamBlock, channelIndex, morpherSettings, t );

		const ObjectState& mSourceObjectState = mSourceNode->EvalWorldState( t );
		this->vertexCount = mSourceObjectState.obj->NumPoints();

		this->name = mMorphTargets.size() > 0 ? mMorphTargets.at( 0 )->GetName() : _T("ProgressiveMorph");

		this->morphTargets.reserve( mMorphTargets.size() );

		for( size_t progressiveMorphIndex = 0; progressiveMorphIndex < mMorphTargets.size(); ++progressiveMorphIndex )
		{
			INode* mMorphTargetNode = mMorphTargets.at( progressiveMorphIndex );
			const ObjectState& mTargetObjectState = mMorphTargetNode->EvalWorldState( t );

			if( this->vertexCount != mSourceObjectState.obj->NumPoints() )
			{
				this->isValid = false;
				continue;
			}

			const float& weight = mMorphTargetWeights.at( progressiveMorphIndex );
			ProgressiveMorphTarget* progressiveMorphTarget = new ProgressiveMorphTarget( mMorphTargetNode, this->vertexCount, weight );
			for( int vertexIndex = 0; vertexIndex < this->vertexCount; ++vertexIndex )
			{
				const Point3& sp = mSourceObjectState.obj->GetPoint( vertexIndex );
				const Point3& tp = mTargetObjectState.obj->GetPoint( vertexIndex );
				const Point3& fp = tp - sp;
				progressiveMorphTarget->targetDeltas[ vertexIndex ] = fp;
			}

			this->morphTargets.push_back( progressiveMorphTarget );
		}

		if( mWeightParamBlock )
		{
			const int numWeightParams = mWeightParamBlock->NumParams();
			if( numWeightParams > 0 )
			{
				ParamType mParamBlockType = mWeightParamBlock->GetParameterType( 0 );
				if( mParamBlockType == TYPE_FLOAT )
				{
					this->settings->morphWeight = mWeightParamBlock->GetFloat( 0 );
				}
			}
		}
	}

	MorphChannel( INode* mSourceNode,
	              IParamBlock* mWeightParamBlock,
	              int channelIndex,
	              std::basic_string<TCHAR> channelName,
	              std::vector<Point3>& mMorphPoints,
	              MorphChannelMetaData* morpherSettings,
	              float weight,
	              TimeValue t )
	{
		InitParameters( mSourceNode, mWeightParamBlock, channelIndex, morpherSettings, t );

		const ObjectState& mSourceObjectState = mSourceNode->EvalWorldState( t );
		this->vertexCount = mSourceObjectState.obj->NumPoints();

		this->name = channelName.c_str();

		this->morphTargets.reserve( 1 );
		this->isValid = this->vertexCount == mMorphPoints.size();

		ProgressiveMorphTarget* progressiveMorphTarget = new ProgressiveMorphTarget( this->vertexCount, weight );
		for( int vertexIndex = 0; vertexIndex < this->vertexCount; ++vertexIndex )
		{
			const Point3& sp = mSourceObjectState.obj->GetPoint( vertexIndex );
			const Point3& tp = mMorphPoints.at( vertexIndex );
			const Point3& fp = tp - sp;
			progressiveMorphTarget->targetDeltas[ vertexIndex ] = fp;
		}

		this->morphTargets.push_back( progressiveMorphTarget );

		if( mWeightParamBlock )
		{
			const int numWeightParams = mWeightParamBlock->NumParams();
			if( numWeightParams > 0 )
			{
				ParamType mParamBlockType = mWeightParamBlock->GetParameterType( 0 );
				if( mParamBlockType == TYPE_FLOAT )
				{
					this->settings->morphWeight = mWeightParamBlock->GetFloat( 0 );
				}
			}
		}
	}

	MorphChannel( INode* mSourceNode,
	              IParamBlock* mWeightParamBlock,
	              int channelIndex,
	              std::basic_string<TCHAR> channelName,
	              std::vector<std::vector<Point3>>& mMorphPointsPerTarget,
	              MorphChannelMetaData* morpherSettings,
	              std::vector<float> mMorphWeights,
	              TimeValue t )
	{
		InitParameters( mSourceNode, mWeightParamBlock, channelIndex, morpherSettings, t );

		const ObjectState& mSourceObjectState = mSourceNode->EvalWorldState( t );
		this->vertexCount = mSourceObjectState.obj->NumPoints();

		this->name = channelName.c_str();

		this->morphTargets.reserve( mMorphWeights.size() );

		for( size_t progressiveMorphIndex = 0; progressiveMorphIndex < mMorphPointsPerTarget.size(); ++progressiveMorphIndex )
		{
			const std::vector<Point3>& mMorphPoints = mMorphPointsPerTarget[ progressiveMorphIndex ];

			if( this->vertexCount != mMorphPoints.size() )
			{
				this->isValid = false;
				continue;
			}

			const float& weight = mMorphWeights.at( progressiveMorphIndex );
			ProgressiveMorphTarget* progressiveMorphTarget = new ProgressiveMorphTarget( this->vertexCount, weight );
			for( int vertexIndex = 0; vertexIndex < this->vertexCount; ++vertexIndex )
			{
				const Point3& sp = mSourceObjectState.obj->GetPoint( vertexIndex );
				const Point3& tp = mMorphPoints.at( vertexIndex );
				const Point3& fp = tp - sp;
				progressiveMorphTarget->targetDeltas[ vertexIndex ] = fp;
			}

			this->morphTargets.push_back( progressiveMorphTarget );
		}

		if( mWeightParamBlock )
		{
			const int numWeightParams = mWeightParamBlock->NumParams();
			if( numWeightParams > 0 )
			{
				ParamType mParamBlockType = mWeightParamBlock->GetParameterType( 0 );
				if( mParamBlockType == TYPE_FLOAT )
				{
					this->settings->morphWeight = mWeightParamBlock->GetFloat( 0 );
				}
			}
		}
	}

	~MorphChannel()
	{
		for( ProgressiveMorphTarget* progressiveMorphTarget : this->morphTargets )
		{
			if( progressiveMorphTarget )
				delete progressiveMorphTarget;
		}

		this->morphTargets.clear();
	}

	MorphChannelMetaData* GetSettings() { return this->settings; }

	int GetVertexCount() { return this->vertexCount; }

	TSTR GetName() { return this->name; }
	int GetIndex() { return this->localIndex; }

	bool IsValid() { return this->isValid; }

	size_t NumProgressiveMorphTargets() const { return morphTargets.size(); }
	ProgressiveMorphTarget* GetProgressiveMorphTarget( size_t progressiveMorphIndex ) const
	{
		return progressiveMorphIndex < NumProgressiveMorphTargets() ? morphTargets.at( progressiveMorphIndex ) : nullptr;
	}
};

class MorpherChannelSettings
{
	public:
	std::vector<MorphChannelMetaData*> channels;
};

class MorpherWrapper
{
	public:
	GlobalMorpherSettings globalSettings;

	MorpherWrapper( Modifier* mMorphModifier, INode* mSourceNode, TimeValue t )
	{
		this->modifier = mMorphModifier;
		this->sourceNode = mSourceNode;
		this->currentTime = t;

		const int numReferences = mMorphModifier->NumRefs();

		if( numReferences > MaxNumMorphTargets )
		{
			// fetch global settings
			ReferenceTarget* mGlobalSettingsReferenceTarget = mMorphModifier->GetReference( 0 );

			if( mGlobalSettingsReferenceTarget )
			{
				MSTR mClassName;
				mGlobalSettingsReferenceTarget->GetClassName( mClassName );
				if( mClassName == L"ParamBlock" )
				{
					IParamBlock* mParamBlock = dynamic_cast<IParamBlock*>( mGlobalSettingsReferenceTarget );
					if( mParamBlock )
					{
						const int numParams = mParamBlock->NumParams();

						if( numParams == NumGlobalSettings )
						{
							ParamType mType = mParamBlock->GetParameterType( UseLimits );
							if( mType == TYPE_INT )
							{
								this->globalSettings.useLimits = mParamBlock->GetInt( UseLimits ) > 0;
							}

							mType = mParamBlock->GetParameterType( SpinnerMin );
							if( mType == TYPE_FLOAT )
							{
								this->globalSettings.spinnerMin = mParamBlock->GetFloat( SpinnerMin );
							}

							mType = mParamBlock->GetParameterType( SpinnerMax );
							if( mType == TYPE_FLOAT )
							{
								this->globalSettings.spinnerMax = mParamBlock->GetFloat( SpinnerMax );
							}

							mType = mParamBlock->GetParameterType( UseSelection );
							if( mType == TYPE_INT )
							{
								this->globalSettings.useSelection = mParamBlock->GetInt( UseSelection ) > 0;
							}

							mType = mParamBlock->GetParameterType( ValueIncrements );
							if( mType == TYPE_INT )
							{
								this->globalSettings.valueIncrements = mParamBlock->GetInt( ValueIncrements );
							}

							mType = mParamBlock->GetParameterType( AutoLoadTargets );
							if( mType == TYPE_INT )
							{
								this->globalSettings.autoLoadTargets = mParamBlock->GetInt( AutoLoadTargets ) > 0;
							}

							if( mType == TYPE_BOOL )
							{
								// auto b = mParamBlock->GetInt( j ) > 0;
							}
						}
					}
				}
			}

			morphTargetChannels.reserve( MaxNumMorphTargets );

			const ulong uniqueHandle = mSourceNode->GetHandle();

			MorpherChannelSettings morpherSettings;

			// fetch active morph targets from MaxScript
			SimplygonMaxInstance->GetActiveMorphChannels( uniqueHandle, &morpherSettings );
			SimplygonMaxInstance->GetActiveMorphTargetTension( uniqueHandle, &morpherSettings );
			SimplygonMaxInstance->GetActiveMinLimits( uniqueHandle, &morpherSettings );
			SimplygonMaxInstance->GetActiveMaxLimits( uniqueHandle, &morpherSettings );
			SimplygonMaxInstance->GetActiveUseVertexSelections( uniqueHandle, &morpherSettings );
			SimplygonMaxInstance->GetActiveUseLimits( uniqueHandle, &morpherSettings );

			for( size_t activeMorphChannelListIndex = 0; activeMorphChannelListIndex < morpherSettings.channels.size(); ++activeMorphChannelListIndex )
			{
				const int morphChannelIndex = morpherSettings.channels[ activeMorphChannelListIndex ]->GetIndex();

				ReferenceTarget* mMorphChannelsReferenceTarget = mMorphModifier->GetReference( morphChannelIndex );
				if( mMorphChannelsReferenceTarget )
				{
					// fetch target from paramblock
					MSTR mReferenceClassName;
					mMorphChannelsReferenceTarget->GetClassName( mReferenceClassName );
					if( mReferenceClassName == L"ParamBlock" )
					{
						IParamBlock* mWeightParamBlock = dynamic_cast<IParamBlock*>( mMorphChannelsReferenceTarget );
						if( mWeightParamBlock )
						{
							const int morpTargetIndex = ( morphChannelIndex + MaxNumMorphTargets );

							std::basic_string<TCHAR> mMorphChannelName = _T("");
							std::vector<std::vector<Point3>> mProgressiveMorphPoints;
							std::vector<float> mProgressiveWeights;
							std::vector<Point3> mTemporaryMorphPoints;

							SimplygonMaxInstance->GetMorphChannelName( uniqueHandle, morphChannelIndex, mMorphChannelName );
							SimplygonMaxInstance->GetMorphChannelPoints( uniqueHandle, mTemporaryMorphPoints, morphChannelIndex );
							SimplygonMaxInstance->GetActiveMorphTargetProgressiveWeights( uniqueHandle, morphChannelIndex, mProgressiveWeights );

							// if there are morph points,
							// see if there are progressive morphs
							if( mTemporaryMorphPoints.size() > 0 )
							{
								mProgressiveMorphPoints.push_back( mTemporaryMorphPoints );

								for( uint progressiveIndex = 0; progressiveIndex < MaxNumProgressiveMorphTargets; ++progressiveIndex )
								{
									const int progressiveMorphTargetIndex = ( morpTargetIndex + MaxNumMorphTargets + progressiveIndex );

									ReferenceTarget* mProgressiveMorphTargetReference = mMorphModifier->GetReference( progressiveMorphTargetIndex );
									if( mProgressiveMorphTargetReference )
									{
										INode* mProgressiveMorphTarget = dynamic_cast<INode*>( mProgressiveMorphTargetReference );
										if( mProgressiveMorphTarget )
										{
											std::vector<Point3> mTemporaryProgressiveMorphMoints;
											const ObjectState& mTargetObjectState = mProgressiveMorphTarget->EvalWorldState( t );

											const int numVertices = mTargetObjectState.obj->NumPoints();
											if( numVertices > 0 )
											{
												mTemporaryProgressiveMorphMoints.resize( numVertices );

												for( int vid = 0; vid < numVertices; ++vid )
												{
													const Point3& mMorphPoint = mTargetObjectState.obj->GetPoint( vid );
													mTemporaryProgressiveMorphMoints[ vid ] = mMorphPoint;
												}

												mProgressiveMorphPoints.push_back( mTemporaryProgressiveMorphMoints );
											}
										}
									}
								}

								MorphChannelMetaData* morphChannelMetaData = morpherSettings.channels[ activeMorphChannelListIndex ];
								morphTargetChannels.push_back( new MorphChannel( mSourceNode,
								                                                 mWeightParamBlock,
								                                                 morphChannelIndex,
								                                                 mMorphChannelName,
								                                                 mProgressiveMorphPoints,
								                                                 morphChannelMetaData,
								                                                 mProgressiveWeights,
								                                                 this->currentTime ) );
							}
							else
							{
								// if morph channel has data, but no target reference
								std::vector<Point3> mMorphPoints;
								SimplygonMaxInstance->GetMorphChannelPoints( uniqueHandle, mMorphPoints, morphChannelIndex );

								if( mMorphPoints.size() > 0 )
								{
									std::basic_string<TCHAR> channelName = _T("");
									SimplygonMaxInstance->GetMorphChannelName( uniqueHandle, morphChannelIndex, channelName );

									MorphChannelMetaData* morphChannelMetaData = morpherSettings.channels[ activeMorphChannelListIndex ];
									const float weight = mProgressiveWeights.at( 0 );

									morphTargetChannels.push_back( new MorphChannel( mSourceNode,
									                                                 mWeightParamBlock,
									                                                 morphChannelIndex,
									                                                 channelName,
									                                                 mMorphPoints,
									                                                 morphChannelMetaData,
									                                                 weight,
									                                                 this->currentTime ) );
								}
							}
						}
					}
				}
			}
		}
	}

	~MorpherWrapper()
	{
		for( MorphChannel* morphChannel : this->morphTargetChannels )
		{
			if( morphChannel )
				delete morphChannel;
		}

		this->morphTargetChannels.clear();
	}

	static void ApplyGlobalSettings( Modifier* mMorphModifier, GlobalMorpherSettings settings, TimeValue t )
	{
		const int numReferences = mMorphModifier->NumRefs();
		if( numReferences > MaxNumMorphTargets )
		{
			// fetch global settings
			ReferenceTarget* mGlobalSettingsReferenceTarget = mMorphModifier->GetReference( 0 );

			if( mGlobalSettingsReferenceTarget )
			{
				MSTR mClassName;
				mGlobalSettingsReferenceTarget->GetClassName( mClassName );
				if( mClassName == L"ParamBlock" )
				{
					IParamBlock* mParamBlock = dynamic_cast<IParamBlock*>( mGlobalSettingsReferenceTarget );
					if( mParamBlock )
					{
						const int numParams = mParamBlock->NumParams();

						if( numParams == NumGlobalSettings )
						{
							ParamType mType = mParamBlock->GetParameterType( UseLimits );
							if( mType == TYPE_INT )
							{
								mParamBlock->SetValue( UseLimits, t, (int)settings.useLimits );
							}

							mType = mParamBlock->GetParameterType( SpinnerMin );
							if( mType == TYPE_FLOAT )
							{
								mParamBlock->SetValue( SpinnerMin, t, settings.spinnerMin );
							}

							mType = mParamBlock->GetParameterType( SpinnerMax );
							if( mType == TYPE_FLOAT )
							{
								mParamBlock->SetValue( SpinnerMax, t, settings.spinnerMax );
							}

							mType = mParamBlock->GetParameterType( UseSelection );
							if( mType == TYPE_INT )
							{
								mParamBlock->SetValue( UseSelection, t, (int)settings.useSelection );
							}

							mType = mParamBlock->GetParameterType( ValueIncrements );
							if( mType == TYPE_INT )
							{
								mParamBlock->SetValue( ValueIncrements, t, settings.valueIncrements );
							}

							mType = mParamBlock->GetParameterType( AutoLoadTargets );
							if( mType == TYPE_INT )
							{
								mParamBlock->SetValue( AutoLoadTargets, t, (int)settings.autoLoadTargets );
							}
						}
					}
				}
			}
		}
	}

	size_t NumChannels() const { return morphTargetChannels.size(); };

	MorphChannel* GetChannel( size_t morpTargetIndex ) const { return morpTargetIndex < NumChannels() ? morphTargetChannels.at( morpTargetIndex ) : nullptr; }

	private:
	INode* sourceNode;
	Modifier* modifier;
	std::vector<MorphChannel*> morphTargetChannels;
	TimeValue currentTime;
};

void MaterialNodes::PopulateTextureNode( spShadingTextureNode sgTextureNode,
                                         BitmapTex* mBitmapTex,
                                         std::basic_string<TCHAR>& tMaxMappingChannel,
                                         std::basic_string<TCHAR>& tTextureName,
                                         TimeValue& time,
                                         const bool isSRGB )
{
	sgTextureNode->SetTextureName( LPCTSTRToConstCharPtr( tTextureName.c_str() ) );
	sgTextureNode->SetTexCoordName( LPCTSTRToConstCharPtr( tMaxMappingChannel.c_str() ) );
	sgTextureNode->SetColorSpaceOverride( isSRGB ? Simplygon::EImageColorSpace::sRGB : Simplygon::EImageColorSpace::Linear );

	if( mBitmapTex != nullptr )
	{
		StdUVGen* mUVObject = mBitmapTex->GetUVGen();
		float uScale = mUVObject->GetUScl( time );
		float vScale = mUVObject->GetVScl( time );

		// compensate offset for center scaled tiling, uses mod to clamp values
		float uOffset = mod( -mUVObject->GetUOffs( time ) - ( ( uScale - 1.0f ) * ( 0.5f + mUVObject->GetUOffs( time ) ) ), 1.0f );
		float vOffset = mod( -mUVObject->GetVOffs( time ) - ( ( vScale - 1.0f ) * ( 0.5f + mUVObject->GetVOffs( time ) ) ), 1.0f );

		sgTextureNode->SetTileU( uScale );
		sgTextureNode->SetTileV( vScale );
		sgTextureNode->SetOffsetU( uOffset );
		sgTextureNode->SetOffsetV( vOffset );
	}
	else
	{
		sgTextureNode->SetTileU( 1 );
		sgTextureNode->SetTileV( 1 );
		sgTextureNode->SetOffsetU( 0 );
		sgTextureNode->SetOffsetV( 0 );
	}
}

spShadingTextureNode MaterialNodes::CreateTextureNode(
    BitmapTex* mBitmapTex, std::basic_string<TCHAR>& tMaxMappingChannel, std::basic_string<TCHAR>& tTextureName, TimeValue& time, const bool isSRGB )
{
	spShadingTextureNode sgTextureNode = sg->CreateShadingTextureNode();

	PopulateTextureNode( sgTextureNode, mBitmapTex, tMaxMappingChannel, tTextureName, time, isSRGB );

	return sgTextureNode;
}

spShadingNode
MaterialNodes::GetShadingNode( MaterialNodes::TextureData& textureData, std::basic_string<TCHAR> tMaxMappingChannel, const int channelId, TimeValue time )
{
	spShadingNode sgFinalizedTextureNode;
	spShadingTextureNode sgTextureNode = CreateTextureNode( textureData.mBitmap, tMaxMappingChannel, textureData.mTextureName, time, textureData.mIsSRGB );

	bool premultipliedFlag = textureData.mPremultipliedAlpha;
	if( channelId == ID_OP )
	{
		// opacity overrides menu settings
		premultipliedFlag = false;
		sgFinalizedTextureNode = sgTextureNode;
	}
	else
	{
		if( textureData.mAlphaSource == ALPHA_RGB )
		{
			spShadingSwizzlingNode sgRedSwizzleNode = sg->CreateShadingSwizzlingNode();
			sgRedSwizzleNode->SetInput( 0, sgTextureNode );
			sgRedSwizzleNode->SetInput( 1, sgTextureNode );
			sgRedSwizzleNode->SetInput( 2, sgTextureNode );
			sgRedSwizzleNode->SetInput( 3, sgTextureNode );

			sgRedSwizzleNode->SetRedComponent( 0 );
			sgRedSwizzleNode->SetGreenComponent( 0 );
			sgRedSwizzleNode->SetBlueComponent( 0 );
			sgRedSwizzleNode->SetAlphaComponent( 0 );

			spShadingSwizzlingNode sgGreenSwizzleNode = sg->CreateShadingSwizzlingNode();
			sgGreenSwizzleNode->SetInput( 0, sgTextureNode );
			sgGreenSwizzleNode->SetInput( 1, sgTextureNode );
			sgGreenSwizzleNode->SetInput( 2, sgTextureNode );
			sgGreenSwizzleNode->SetInput( 3, sgTextureNode );

			sgGreenSwizzleNode->SetRedComponent( 1 );
			sgGreenSwizzleNode->SetGreenComponent( 1 );
			sgGreenSwizzleNode->SetBlueComponent( 1 );
			sgGreenSwizzleNode->SetAlphaComponent( 1 );

			spShadingSwizzlingNode sgBlueSwizzleNode = sg->CreateShadingSwizzlingNode();
			sgBlueSwizzleNode->SetInput( 0, sgTextureNode );
			sgBlueSwizzleNode->SetInput( 1, sgTextureNode );
			sgBlueSwizzleNode->SetInput( 2, sgTextureNode );
			sgBlueSwizzleNode->SetInput( 3, sgTextureNode );

			sgBlueSwizzleNode->SetRedComponent( 2 );
			sgBlueSwizzleNode->SetGreenComponent( 2 );
			sgBlueSwizzleNode->SetBlueComponent( 2 );
			sgBlueSwizzleNode->SetAlphaComponent( 2 );

			spShadingAddNode sgAddRGNode = sg->CreateShadingAddNode();
			sgAddRGNode->SetInput( 0, sgRedSwizzleNode );
			sgAddRGNode->SetInput( 1, sgGreenSwizzleNode );

			spShadingAddNode sgAddRGBNode = sg->CreateShadingAddNode();
			sgAddRGBNode->SetInput( 0, sgAddRGNode );
			sgAddRGBNode->SetInput( 1, sgBlueSwizzleNode );

			spShadingColorNode sg1Through3Node = sg->CreateShadingColorNode();
			sg1Through3Node->SetDefaultParameter( 0, 3, 3, 3, 3 );

			spShadingDivideNode sgDivideNode = sg->CreateShadingDivideNode();
			sgDivideNode->SetInput( 0, sgAddRGBNode );
			sgDivideNode->SetInput( 1, sg1Through3Node );

			spShadingSwizzlingNode sgFinalSwizzleNode = sg->CreateShadingSwizzlingNode();
			sgFinalSwizzleNode->SetInput( 0, sgTextureNode );
			sgFinalSwizzleNode->SetInput( 1, sgTextureNode );
			sgFinalSwizzleNode->SetInput( 2, sgTextureNode );
			sgFinalSwizzleNode->SetInput( 3, sgDivideNode );

			sgFinalSwizzleNode->SetRedComponent( 0 );
			sgFinalSwizzleNode->SetGreenComponent( 1 );
			sgFinalSwizzleNode->SetBlueComponent( 2 );
			sgFinalSwizzleNode->SetAlphaComponent( 3 );

			sgFinalizedTextureNode = sgFinalSwizzleNode;
		}
		else if( textureData.mAlphaSource == ALPHA_NONE )
		{
			spShadingColorNode sgOneNode = sg->CreateShadingColorNode();
			sgOneNode->SetColor( 1.0f, 1.0f, 1.0f, 1.0f );

			spShadingSwizzlingNode sgTextureColorSwizzleNode = sg->CreateShadingSwizzlingNode();
			sgTextureColorSwizzleNode->SetInput( 0, sgTextureNode );
			sgTextureColorSwizzleNode->SetInput( 1, sgTextureNode );
			sgTextureColorSwizzleNode->SetInput( 2, sgTextureNode );
			sgTextureColorSwizzleNode->SetInput( 3, sgOneNode );

			sgTextureColorSwizzleNode->SetRedComponent( 0 );
			sgTextureColorSwizzleNode->SetGreenComponent( 1 );
			sgTextureColorSwizzleNode->SetBlueComponent( 2 );
			sgTextureColorSwizzleNode->SetAlphaComponent( 3 );

			sgFinalizedTextureNode = sgTextureColorSwizzleNode;
		}
		else
		{
			sgFinalizedTextureNode = sgTextureNode;
		}
	}

	if( premultipliedFlag == false )
	{
		spShadingSwizzlingNode sgAlphaSourceNode = sg->CreateShadingSwizzlingNode();
		sgAlphaSourceNode->SetInput( 0, sgFinalizedTextureNode );
		sgAlphaSourceNode->SetInput( 1, sgFinalizedTextureNode );
		sgAlphaSourceNode->SetInput( 2, sgFinalizedTextureNode );
		sgAlphaSourceNode->SetInput( 3, sgFinalizedTextureNode );
		sgAlphaSourceNode->SetRedComponent( 3 );
		sgAlphaSourceNode->SetGreenComponent( 3 );
		sgAlphaSourceNode->SetBlueComponent( 3 );
		sgAlphaSourceNode->SetAlphaComponent( 3 );

		spShadingMultiplyNode sgMultiplyNode = sg->CreateShadingMultiplyNode();
		sgMultiplyNode->SetInput( 0, sgFinalizedTextureNode );
		sgMultiplyNode->SetInput( 1, sgAlphaSourceNode );

		spShadingSwizzlingNode sgAlphaSwizzleNode = sg->CreateShadingSwizzlingNode();
		sgAlphaSwizzleNode->SetInput( 0, sgMultiplyNode );
		sgAlphaSwizzleNode->SetInput( 1, sgMultiplyNode );
		sgAlphaSwizzleNode->SetInput( 2, sgMultiplyNode );
		sgAlphaSwizzleNode->SetInput( 3, sgAlphaSourceNode );
		sgAlphaSwizzleNode->SetRedComponent( 0 );
		sgAlphaSwizzleNode->SetGreenComponent( 1 );
		sgAlphaSwizzleNode->SetBlueComponent( 2 );
		sgAlphaSwizzleNode->SetAlphaComponent( 3 );

		sgFinalizedTextureNode = sgAlphaSwizzleNode;
	}
	return sgFinalizedTextureNode;
}

spShadingNode MaterialNodes::SetUpMultiplyShadingNode( spShadingNode sgInputNodes[ 2 ],
                                                       MaterialNodes::MultiplyNodeAlphaFrom alphaFrom,
                                                       std::basic_string<TCHAR>& tMaterialName,
                                                       TimeValue time )
{
	spShadingNode sgSelectedAlphaSourceNode = sg->CreateShadingSwizzlingNode();
	if( alphaFrom == MaterialNodes::MultiplyNodeAlphaFrom::AlphaFirstSource )
	{
		spShadingSwizzlingNode sgSelectedAlphaNode = sg->CreateShadingSwizzlingNode();
		sgSelectedAlphaNode->SetInput( 0, sgInputNodes[ 0 ] );
		sgSelectedAlphaNode->SetInput( 1, sgInputNodes[ 0 ] );
		sgSelectedAlphaNode->SetInput( 2, sgInputNodes[ 0 ] );
		sgSelectedAlphaNode->SetInput( 3, sgInputNodes[ 0 ] );
		sgSelectedAlphaNode->SetRedComponent( 3 );
		sgSelectedAlphaNode->SetGreenComponent( 3 );
		sgSelectedAlphaNode->SetBlueComponent( 3 );
		sgSelectedAlphaNode->SetAlphaComponent( 3 );

		sgSelectedAlphaSourceNode = sgSelectedAlphaNode;
	}
	else if( alphaFrom == MaterialNodes::MultiplyNodeAlphaFrom::AlphaSecondSource )
	{
		spShadingSwizzlingNode sgSelectedAlphaNode = sg->CreateShadingSwizzlingNode();
		sgSelectedAlphaNode->SetInput( 0, sgInputNodes[ 1 ] );
		sgSelectedAlphaNode->SetInput( 1, sgInputNodes[ 1 ] );
		sgSelectedAlphaNode->SetInput( 2, sgInputNodes[ 1 ] );
		sgSelectedAlphaNode->SetInput( 3, sgInputNodes[ 1 ] );
		sgSelectedAlphaNode->SetRedComponent( 3 );
		sgSelectedAlphaNode->SetGreenComponent( 3 );
		sgSelectedAlphaNode->SetBlueComponent( 3 );
		sgSelectedAlphaNode->SetAlphaComponent( 3 );

		sgSelectedAlphaSourceNode = sgSelectedAlphaNode;
	}
	else // if( node.mAlphaFrom == MultiplyNode::MultiplyNodeAlphaFrom::AlphaBlendSource )
	{
		spShadingSwizzlingNode sgTexture0AlphaNode = sg->CreateShadingSwizzlingNode();
		sgTexture0AlphaNode->SetInput( 0, sgInputNodes[ 0 ] );
		sgTexture0AlphaNode->SetInput( 1, sgInputNodes[ 0 ] );
		sgTexture0AlphaNode->SetInput( 2, sgInputNodes[ 0 ] );
		sgTexture0AlphaNode->SetInput( 3, sgInputNodes[ 0 ] );
		sgTexture0AlphaNode->SetRedComponent( 3 );
		sgTexture0AlphaNode->SetGreenComponent( 3 );
		sgTexture0AlphaNode->SetBlueComponent( 3 );
		sgTexture0AlphaNode->SetAlphaComponent( 3 );

		spShadingSwizzlingNode sgTexture1AlphaNode = sg->CreateShadingSwizzlingNode();
		sgTexture1AlphaNode->SetInput( 0, sgInputNodes[ 1 ] );
		sgTexture1AlphaNode->SetInput( 1, sgInputNodes[ 1 ] );
		sgTexture1AlphaNode->SetInput( 2, sgInputNodes[ 1 ] );
		sgTexture1AlphaNode->SetInput( 3, sgInputNodes[ 1 ] );
		sgTexture1AlphaNode->SetRedComponent( 3 );
		sgTexture1AlphaNode->SetGreenComponent( 3 );
		sgTexture1AlphaNode->SetBlueComponent( 3 );
		sgTexture1AlphaNode->SetAlphaComponent( 3 );

		spShadingMultiplyNode sgMultAlphaNode = sg->CreateShadingMultiplyNode();
		sgMultAlphaNode->SetInput( 0, sgTexture0AlphaNode );
		sgMultAlphaNode->SetInput( 1, sgTexture1AlphaNode );

		sgSelectedAlphaSourceNode = sgMultAlphaNode;
	}

	spShadingColorNode sgOneNode = sg->CreateShadingColorNode();
	sgOneNode->SetColor( 1.0f, 1.0f, 1.0f, 1.0f );

	spShadingColorNode sgZeroNode = sg->CreateShadingColorNode();
	sgZeroNode->SetColor( 0.0f, 0.0f, 0.0f, 1.0f );

	spShadingMultiplyNode sgMultipliedTexturesNode = sg->CreateShadingMultiplyNode();
	sgMultipliedTexturesNode->SetInput( 0, sgInputNodes[ 0 ] );
	sgMultipliedTexturesNode->SetInput( 1, sgInputNodes[ 1 ] );

	spShadingSwizzlingNode sgFinalAlphaSwizzleNode = sg->CreateShadingSwizzlingNode();
	sgFinalAlphaSwizzleNode->SetInput( 0, sgMultipliedTexturesNode );
	sgFinalAlphaSwizzleNode->SetInput( 1, sgMultipliedTexturesNode );
	sgFinalAlphaSwizzleNode->SetInput( 2, sgMultipliedTexturesNode );
	sgFinalAlphaSwizzleNode->SetInput( 3, sgSelectedAlphaSourceNode );
	sgFinalAlphaSwizzleNode->SetRedComponent( 0 );
	sgFinalAlphaSwizzleNode->SetGreenComponent( 1 );
	sgFinalAlphaSwizzleNode->SetBlueComponent( 2 );
	sgFinalAlphaSwizzleNode->SetAlphaComponent( 3 );

	return sgFinalAlphaSwizzleNode;
}

spShadingNode MaterialNodes::SetUpTintShadingNode(
    spShadingNode& sgInputNode, std::basic_string<TCHAR>& tMaterialName, Color& redChannel, Color& greenChannel, Color& blueChannel, TimeValue time )
{
	spShadingColorNode sgOneNode = sg->CreateShadingColorNode();
	sgOneNode->SetColor( 1.0f, 1.0f, 1.0f, 1.0f );

	spShadingColorNode sgZeroNode = sg->CreateShadingColorNode();
	sgZeroNode->SetColor( 0.f, 0.f, 0.f, 0.f );

	spShadingSwizzlingNode sgSrcRedNode = sg->CreateShadingSwizzlingNode();
	sgSrcRedNode->SetInput( 0, sgInputNode );
	sgSrcRedNode->SetInput( 1, sgInputNode );
	sgSrcRedNode->SetInput( 2, sgInputNode );
	sgSrcRedNode->SetInput( 3, sgOneNode );
	sgSrcRedNode->SetRedComponent( 0 );
	sgSrcRedNode->SetGreenComponent( 0 );
	sgSrcRedNode->SetBlueComponent( 0 );
	sgSrcRedNode->SetAlphaComponent( 0 );

	spShadingSwizzlingNode sgSrcGreenNode = sg->CreateShadingSwizzlingNode();
	sgSrcGreenNode->SetInput( 0, sgInputNode );
	sgSrcGreenNode->SetInput( 1, sgInputNode );
	sgSrcGreenNode->SetInput( 2, sgInputNode );
	sgSrcGreenNode->SetInput( 3, sgOneNode );
	sgSrcGreenNode->SetRedComponent( 1 );
	sgSrcGreenNode->SetGreenComponent( 1 );
	sgSrcGreenNode->SetBlueComponent( 1 );
	sgSrcGreenNode->SetAlphaComponent( 1 );

	spShadingSwizzlingNode sgSrcBlueNode = sg->CreateShadingSwizzlingNode();
	sgSrcBlueNode->SetInput( 0, sgInputNode );
	sgSrcBlueNode->SetInput( 1, sgInputNode );
	sgSrcBlueNode->SetInput( 2, sgInputNode );
	sgSrcBlueNode->SetInput( 3, sgOneNode );
	sgSrcBlueNode->SetRedComponent( 2 );
	sgSrcBlueNode->SetGreenComponent( 2 );
	sgSrcBlueNode->SetBlueComponent( 2 );
	sgSrcBlueNode->SetAlphaComponent( 2 );

	spShadingColorNode sgTintChannel0Node = sg->CreateShadingColorNode();
	sgTintChannel0Node->SetDefaultParameter( 0, redChannel.r, redChannel.g, redChannel.b, 1 );

	spShadingColorNode sgTintChannel1Node = sg->CreateShadingColorNode();
	sgTintChannel1Node->SetDefaultParameter( 0, greenChannel.r, greenChannel.g, greenChannel.b, 1 );

	spShadingColorNode sgTintChannel2Node = sg->CreateShadingColorNode();
	sgTintChannel2Node->SetDefaultParameter( 0, blueChannel.r, blueChannel.g, blueChannel.b, 1 );

	/*
	    The MAX's tint formula is:
	    if our texture color is SRC and R, G, B colors are TRG0, TRG1, TRG2 the formula looks as:

	    COL.R = SRC.R *TRG0.R + SRC.G *TRG1.R + SRC.B *TRG2.R
	    COL.G = SRC.R *TRG0.G + SRC.G *TRG1.G + SRC.B *TRG2.G
	    COL.B = SRC.R *TRG0.B + SRC.G *TRG1.B + SRC.B *TRG2.B

	    if with have just clear R, G, B colors it changes to

	    COL.R = SRC.R *R
	    COL.G = SRC.G *G
	    COL.B = SRC.B *B


	    c2.r = c.r*col[0].r + c.g*col[1].r + c.b*col[2].r;
	    c2.g = c.r*col[0].g + c.g*col[1].g + c.b*col[2].g;
	    c2.b = c.r*col[0].b + c.g*col[1].b + c.b*col[2].b;
	    c2.a = c.a;
	*/

	spShadingMultiplyNode sgSrcRed_x_Tint0Node = sg->CreateShadingMultiplyNode();
	sgSrcRed_x_Tint0Node->SetInput( 0, sgSrcRedNode );
	sgSrcRed_x_Tint0Node->SetInput( 1, sgTintChannel0Node );

	spShadingMultiplyNode sgSrcGreen_x_Tint1Node = sg->CreateShadingMultiplyNode();
	sgSrcGreen_x_Tint1Node->SetInput( 0, sgSrcGreenNode );
	sgSrcGreen_x_Tint1Node->SetInput( 1, sgTintChannel1Node );

	spShadingMultiplyNode sgSrcBlue_x_Tint2Node = sg->CreateShadingMultiplyNode();
	sgSrcBlue_x_Tint2Node->SetInput( 0, sgSrcBlueNode );
	sgSrcBlue_x_Tint2Node->SetInput( 1, sgTintChannel2Node );

	spShadingAddNode sgAddColumn0Node = sg->CreateShadingAddNode();
	sgAddColumn0Node->SetInput( 0, sgSrcRed_x_Tint0Node );
	sgAddColumn0Node->SetInput( 1, sgSrcGreen_x_Tint1Node );

	spShadingAddNode sgAddColumn1Node = sg->CreateShadingAddNode();
	sgAddColumn1Node->SetInput( 0, sgAddColumn0Node );
	sgAddColumn1Node->SetInput( 1, sgSrcBlue_x_Tint2Node );

	spShadingSwizzlingNode sgAlphaSwizzleNode = sg->CreateShadingSwizzlingNode();
	sgAlphaSwizzleNode->SetInput( 0, sgAddColumn1Node );
	sgAlphaSwizzleNode->SetInput( 1, sgAddColumn1Node );
	sgAlphaSwizzleNode->SetInput( 2, sgAddColumn1Node );
	sgAlphaSwizzleNode->SetInput( 3, sgInputNode );
	sgAlphaSwizzleNode->SetRedComponent( 0 );
	sgAlphaSwizzleNode->SetGreenComponent( 1 );
	sgAlphaSwizzleNode->SetBlueComponent( 2 );
	sgAlphaSwizzleNode->SetAlphaComponent( 3 );

	return sgAlphaSwizzleNode;
}

spShadingNode MaterialNodes::SetUpBitmapShadingNode( std::basic_string<TCHAR>& tMaterialName,
                                                     std::basic_string<TCHAR>& tMaxMappingChannel,
                                                     MaterialNodes::TextureData& rTextureData,
                                                     const int channelId,
                                                     TimeValue time )
{
	spShadingNode sgShadingNode = GetShadingNode( rTextureData, tMaxMappingChannel, channelId, time );

	return sgShadingNode;
}

spShadingNode MaterialNodes::SetUpCompositeShadingNode( std::vector<spShadingNode>& TextureNodes,
                                                        std::vector<spShadingNode>& MaskNodes,
                                                        std::vector<ETextureBlendType>& TextureBlendTypes,
                                                        std::vector<float>& Opacity,
                                                        std::basic_string<TCHAR>& tMaterialName,
                                                        TimeValue time )
{
	spShadingColorNode sgOneNode = sg->CreateShadingColorNode();
	sgOneNode->SetColor( 1.0f, 1.0f, 1.0f, 1.0f );

	spShadingColorNode sgZeroNode = sg->CreateShadingColorNode();
	sgZeroNode->SetColor( 0.f, 0.f, 0.f, 0.f );

	spShadingLayeredBlendNode sgLayeredBlendNode = sg->CreateShadingLayeredBlendNode();
	sgLayeredBlendNode->SetInputCount( static_cast<uint>( TextureNodes.size() ) );
	for( uint index = 0; index < static_cast<uint>( TextureNodes.size() ); ++index )
	{
		spShadingNode sgTextureChannelNode = TextureNodes[ index ];
		spShadingNode sgMaskChannelNode = MaskNodes[ index ];

		spShadingNode sgMaskAlpha;
		if( !sgMaskChannelNode.IsNull() )
		{
			// Extract Mask
			////////////////////////////////////////////////////////////////////////////////////////////////////
			spShadingSwizzlingNode sgRedExtract = sg->CreateShadingSwizzlingNode();
			sgRedExtract->SetInput( 0, sgMaskChannelNode );
			sgRedExtract->SetInput( 1, sgMaskChannelNode );
			sgRedExtract->SetInput( 2, sgMaskChannelNode );
			sgRedExtract->SetInput( 3, sgOneNode );
			sgRedExtract->SetRedComponent( 0 );
			sgRedExtract->SetGreenComponent( 0 );
			sgRedExtract->SetBlueComponent( 0 );
			sgRedExtract->SetAlphaComponent( 0 );

			spShadingSwizzlingNode sgGreenExtract = sg->CreateShadingSwizzlingNode();
			sgGreenExtract->SetInput( 0, sgMaskChannelNode );
			sgGreenExtract->SetInput( 1, sgMaskChannelNode );
			sgGreenExtract->SetInput( 2, sgMaskChannelNode );
			sgGreenExtract->SetInput( 3, sgOneNode );
			sgGreenExtract->SetRedComponent( 1 );
			sgGreenExtract->SetGreenComponent( 1 );
			sgGreenExtract->SetBlueComponent( 1 );
			sgGreenExtract->SetAlphaComponent( 1 );

			spShadingSwizzlingNode sgBlueExtract = sg->CreateShadingSwizzlingNode();
			sgBlueExtract->SetInput( 0, sgMaskChannelNode );
			sgBlueExtract->SetInput( 1, sgMaskChannelNode );
			sgBlueExtract->SetInput( 2, sgMaskChannelNode );
			sgBlueExtract->SetInput( 3, sgOneNode );
			sgBlueExtract->SetRedComponent( 2 );
			sgBlueExtract->SetGreenComponent( 2 );
			sgBlueExtract->SetBlueComponent( 2 );
			sgBlueExtract->SetAlphaComponent( 2 );

			spShadingAddNode sgRedGreenAdd = sg->CreateShadingAddNode();
			sgRedGreenAdd->SetInput( 0, sgRedExtract );
			sgRedGreenAdd->SetInput( 1, sgGreenExtract );

			spShadingAddNode sgRGBlueAdd = sg->CreateShadingAddNode();
			sgRGBlueAdd->SetInput( 0, sgRedGreenAdd );
			sgRGBlueAdd->SetInput( 1, sgBlueExtract );

			spShadingColorNode sgThree = sg->CreateShadingColorNode();
			sgThree->SetColor( 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f, 1.0f );

			spShadingMultiplyNode sgRGB_div_3 = sg->CreateShadingMultiplyNode();
			sgRGB_div_3->SetInput( 0, sgRGBlueAdd );
			sgRGB_div_3->SetInput( 1, sgThree );

			spShadingClampNode sgClampNode = sg->CreateShadingClampNode();
			sgClampNode->SetInput( 0, sgRGB_div_3 );
			sgClampNode->SetInput( 1, sgZeroNode );
			sgClampNode->SetInput( 2, sgOneNode );

			spShadingSwizzlingNode sgAlphaMaskNode = sg->CreateShadingSwizzlingNode();
			sgAlphaMaskNode->SetInput( 0, sgClampNode );
			sgAlphaMaskNode->SetInput( 1, sgClampNode );
			sgAlphaMaskNode->SetInput( 2, sgClampNode );
			sgAlphaMaskNode->SetInput( 3, sgClampNode );
			sgAlphaMaskNode->SetRedComponent( 0 );
			sgAlphaMaskNode->SetGreenComponent( 0 );
			sgAlphaMaskNode->SetBlueComponent( 0 );
			sgAlphaMaskNode->SetAlphaComponent( 0 );

			sgMaskAlpha = sgAlphaMaskNode;
		}
		else
		{
			sgMaskAlpha = sgOneNode;
		}

		spShadingSwizzlingNode sgTextureAlphaSource = sg->CreateShadingSwizzlingNode();
		sgTextureAlphaSource->SetInput( 0, sgTextureChannelNode );
		sgTextureAlphaSource->SetInput( 1, sgTextureChannelNode );
		sgTextureAlphaSource->SetInput( 2, sgTextureChannelNode );
		sgTextureAlphaSource->SetInput( 3, sgTextureChannelNode );
		sgTextureAlphaSource->SetRedComponent( 3 );
		sgTextureAlphaSource->SetGreenComponent( 3 );
		sgTextureAlphaSource->SetBlueComponent( 3 );
		sgTextureAlphaSource->SetAlphaComponent( 3 );

		spShadingMultiplyNode sgAlphaMultiplyNode = sg->CreateShadingMultiplyNode();
		sgAlphaMultiplyNode->SetInput( 0, sgMaskAlpha );
		sgAlphaMultiplyNode->SetInput( 1, sgTextureAlphaSource );

		const float nodeAlpha = Opacity[ index ] / 100.0f;

		spShadingColorNode sgOpacityFactor = sg->CreateShadingColorNode();
		sgOpacityFactor->SetColor( nodeAlpha, nodeAlpha, nodeAlpha, nodeAlpha );

		spShadingMultiplyNode sMono_x_OpacityFactor = sg->CreateShadingMultiplyNode();
		sMono_x_OpacityFactor->SetInput( 0, sgAlphaMultiplyNode );
		sMono_x_OpacityFactor->SetInput( 1, sgOpacityFactor );

		spShadingSwizzlingNode sgTextureNode = sg->CreateShadingSwizzlingNode();
		sgTextureNode->SetInput( 0, sgTextureChannelNode );
		sgTextureNode->SetInput( 1, sgTextureChannelNode );
		sgTextureNode->SetInput( 2, sgTextureChannelNode );
		sgTextureNode->SetInput( 3, sMono_x_OpacityFactor );
		sgTextureNode->SetRedComponent( 0 );
		sgTextureNode->SetGreenComponent( 1 );
		sgTextureNode->SetBlueComponent( 2 );
		sgTextureNode->SetAlphaComponent( 3 );

		// First should always be alpha
		// if( TextureBlendTypes[ index ] == ETextureBlendType::Alpha || index == 0 )
		{
			sgLayeredBlendNode->SetInput( index, sgTextureNode );
			sgLayeredBlendNode->SetPerInputBlendType( index, TextureBlendTypes[ index ] );
		}
	}

	spShadingSwizzlingNode sgAlphaSourceNode = sg->CreateShadingSwizzlingNode();
	sgAlphaSourceNode->SetInput( 0, sgLayeredBlendNode );
	sgAlphaSourceNode->SetInput( 1, sgLayeredBlendNode );
	sgAlphaSourceNode->SetInput( 2, sgLayeredBlendNode );
	sgAlphaSourceNode->SetInput( 3, sgLayeredBlendNode );
	sgAlphaSourceNode->SetRedComponent( 3 );
	sgAlphaSourceNode->SetGreenComponent( 3 );
	sgAlphaSourceNode->SetBlueComponent( 3 );
	sgAlphaSourceNode->SetAlphaComponent( 3 );

	spShadingMultiplyNode sgMultiplyNode = sg->CreateShadingMultiplyNode();
	sgMultiplyNode->SetInput( 0, sgLayeredBlendNode );
	sgMultiplyNode->SetInput( 1, sgAlphaSourceNode );

	spShadingSwizzlingNode sgAlphaSwizzle = sg->CreateShadingSwizzlingNode();
	sgAlphaSwizzle->SetInput( 0, sgMultiplyNode );
	sgAlphaSwizzle->SetInput( 1, sgMultiplyNode );
	sgAlphaSwizzle->SetInput( 2, sgMultiplyNode );
	sgAlphaSwizzle->SetInput( 3, sgAlphaSourceNode );
	sgAlphaSwizzle->SetRedComponent( 0 );
	sgAlphaSwizzle->SetGreenComponent( 1 );
	sgAlphaSwizzle->SetBlueComponent( 2 );
	sgAlphaSwizzle->SetAlphaComponent( 3 );

	return sgAlphaSwizzle;
}

spShadingNode MaterialNodes::ReWireColorCorrectionNode( spShadingNode& sgInputNode,
                                                        eMaxColorCorrectionSwizzle red,
                                                        eMaxColorCorrectionSwizzle green,
                                                        eMaxColorCorrectionSwizzle blue,
                                                        eMaxColorCorrectionSwizzle alpha )
{
	spShadingSwizzlingNode sgSwizzleNode = sg->CreateShadingSwizzlingNode();

	eMaxColorCorrectionSwizzle swizzleChannels[ 4 ] = { red, green, blue, alpha };

	void ( *RGBAComponentLambda[ 4 ] )( spShadingSwizzlingNode&,
	                                    int ) = { []( spShadingSwizzlingNode& sgSwizzle, int index ) -> void { sgSwizzle->SetRedComponent( index ); },
	                                              []( spShadingSwizzlingNode& sgSwizzle, int index ) -> void { sgSwizzle->SetGreenComponent( index ); },
	                                              []( spShadingSwizzlingNode& sgSwizzle, int index ) -> void { sgSwizzle->SetBlueComponent( index ); },
	                                              []( spShadingSwizzlingNode& sgSwizzle, int index ) -> void { sgSwizzle->SetAlphaComponent( index ); } };

	spShadingColorNode sgOneNode = sg->CreateShadingColorNode();
	sgOneNode->SetColor( 1.0f, 1.0f, 1.0f, 1.0f );

	spShadingColorNode sgZeroNode = sg->CreateShadingColorNode();
	sgZeroNode->SetColor( 0.0f, 0.0f, 0.0f, 0.0f );

	for( unsigned int i = 0; i < 4; ++i )
	{
		if( swizzleChannels[ i ] == eMaxColorCorrectionSwizzle::Red )
		{
			sgSwizzleNode->SetInput( i, sgInputNode );
			( *RGBAComponentLambda[ i ] )( sgSwizzleNode, 0 );
		}
		else if( swizzleChannels[ i ] == eMaxColorCorrectionSwizzle::Green )
		{
			sgSwizzleNode->SetInput( i, sgInputNode );
			( *RGBAComponentLambda[ i ] )( sgSwizzleNode, 1 );
		}
		else if( swizzleChannels[ i ] == eMaxColorCorrectionSwizzle::Blue )
		{
			sgSwizzleNode->SetInput( i, sgInputNode );
			( *RGBAComponentLambda[ i ] )( sgSwizzleNode, 2 );
		}
		else if( swizzleChannels[ i ] == eMaxColorCorrectionSwizzle::Alpha )
		{
			sgSwizzleNode->SetInput( i, sgInputNode );
			( *RGBAComponentLambda[ i ] )( sgSwizzleNode, 3 );
		}
		else if( swizzleChannels[ i ] == eMaxColorCorrectionSwizzle::invRed )
		{
			spShadingSubtractNode sgOneMinusTextureNode = sg->CreateShadingSubtractNode();
			sgOneMinusTextureNode->SetInput( 0, sgOneNode );
			sgOneMinusTextureNode->SetInput( 1, sgInputNode );

			sgSwizzleNode->SetInput( i, sgOneMinusTextureNode );
			( *RGBAComponentLambda[ i ] )( sgSwizzleNode, 0 );
		}
		else if( swizzleChannels[ i ] == eMaxColorCorrectionSwizzle::invGreen )
		{
			spShadingSubtractNode sgOneMinusTextureNode = sg->CreateShadingSubtractNode();
			sgOneMinusTextureNode->SetInput( 0, sgOneNode );
			sgOneMinusTextureNode->SetInput( 1, sgInputNode );

			sgSwizzleNode->SetInput( i, sgOneMinusTextureNode );
			( *RGBAComponentLambda[ i ] )( sgSwizzleNode, 1 );
		}
		else if( swizzleChannels[ i ] == eMaxColorCorrectionSwizzle::invBlue )
		{
			spShadingSubtractNode sgOneMinusTextureNode = sg->CreateShadingSubtractNode();
			sgOneMinusTextureNode->SetInput( 0, sgOneNode );
			sgOneMinusTextureNode->SetInput( 1, sgInputNode );

			sgSwizzleNode->SetInput( i, sgOneMinusTextureNode );
			( *RGBAComponentLambda[ i ] )( sgSwizzleNode, 2 );
		}
		else if( swizzleChannels[ i ] == eMaxColorCorrectionSwizzle::invAlpha )
		{
			spShadingSubtractNode sgOneMinusTextureNode = sg->CreateShadingSubtractNode();
			sgOneMinusTextureNode->SetInput( 0, sgOneNode );
			sgOneMinusTextureNode->SetInput( 1, sgInputNode );

			sgSwizzleNode->SetInput( i, sgOneMinusTextureNode );
			( *RGBAComponentLambda[ i ] )( sgSwizzleNode, 3 );
		}
		else if( swizzleChannels[ i ] == eMaxColorCorrectionSwizzle::Monochrome )
		{
			spShadingSwizzlingNode sgRedExtractNode = sg->CreateShadingSwizzlingNode();
			sgRedExtractNode->SetInput( 0, sgInputNode );
			sgRedExtractNode->SetInput( 1, sgInputNode );
			sgRedExtractNode->SetInput( 2, sgInputNode );
			sgRedExtractNode->SetInput( 3, sgOneNode );
			sgRedExtractNode->SetRedComponent( 0 );
			sgRedExtractNode->SetGreenComponent( 0 );
			sgRedExtractNode->SetBlueComponent( 0 );
			sgRedExtractNode->SetAlphaComponent( 0 );

			spShadingSwizzlingNode sgGreenExtractNode = sg->CreateShadingSwizzlingNode();
			sgGreenExtractNode->SetInput( 0, sgInputNode );
			sgGreenExtractNode->SetInput( 1, sgInputNode );
			sgGreenExtractNode->SetInput( 2, sgInputNode );
			sgGreenExtractNode->SetInput( 3, sgOneNode );
			sgGreenExtractNode->SetRedComponent( 1 );
			sgGreenExtractNode->SetGreenComponent( 1 );
			sgGreenExtractNode->SetBlueComponent( 1 );
			sgGreenExtractNode->SetAlphaComponent( 1 );

			spShadingSwizzlingNode sgBlueExtractNode = sg->CreateShadingSwizzlingNode();
			sgBlueExtractNode->SetInput( 0, sgInputNode );
			sgBlueExtractNode->SetInput( 1, sgInputNode );
			sgBlueExtractNode->SetInput( 2, sgInputNode );
			sgBlueExtractNode->SetInput( 3, sgOneNode );
			sgBlueExtractNode->SetRedComponent( 2 );
			sgBlueExtractNode->SetGreenComponent( 2 );
			sgBlueExtractNode->SetBlueComponent( 2 );
			sgBlueExtractNode->SetAlphaComponent( 2 );

			spShadingAddNode sgRed_GreenAddNode = sg->CreateShadingAddNode();
			sgRed_GreenAddNode->SetInput( 0, sgRedExtractNode );
			sgRed_GreenAddNode->SetInput( 1, sgGreenExtractNode );

			spShadingAddNode sgRG_BlueAddNode = sg->CreateShadingAddNode();
			sgRG_BlueAddNode->SetInput( 0, sgRed_GreenAddNode );
			sgRG_BlueAddNode->SetInput( 1, sgBlueExtractNode );

			spShadingColorNode sgOneThruThreeNode = sg->CreateShadingColorNode();
			sgOneThruThreeNode->SetColor( 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f, 1.0f );

			spShadingMultiplyNode sgRGB_x_3Node = sg->CreateShadingMultiplyNode();
			sgRGB_x_3Node->SetInput( 0, sgRG_BlueAddNode );
			sgRGB_x_3Node->SetInput( 1, sgOneThruThreeNode );

			spShadingSwizzlingNode sgMonoNode = sg->CreateShadingSwizzlingNode();
			sgMonoNode->SetInput( 0, sgRGB_x_3Node );
			sgMonoNode->SetInput( 1, sgRGB_x_3Node );
			sgMonoNode->SetInput( 2, sgRGB_x_3Node );
			sgMonoNode->SetInput( 3, sgRGB_x_3Node );
			sgMonoNode->SetRedComponent( 0 );
			sgMonoNode->SetGreenComponent( 1 );
			sgMonoNode->SetBlueComponent( 2 );
			sgMonoNode->SetAlphaComponent( 3 );

			sgSwizzleNode->SetInput( i, sgMonoNode );
			( *RGBAComponentLambda[ i ] )( sgSwizzleNode, 0 );
		}
		else if( swizzleChannels[ i ] == eMaxColorCorrectionSwizzle::One )
		{
			sgSwizzleNode->SetInput( i, sgOneNode );
			( *RGBAComponentLambda[ i ] )( sgSwizzleNode, 0 );
		}
		else if( swizzleChannels[ i ] == eMaxColorCorrectionSwizzle::Zero )
		{
			sgSwizzleNode->SetInput( i, sgZeroNode );
			( *RGBAComponentLambda[ i ] )( sgSwizzleNode, 0 );
		}
		else
		{
			sgSwizzleNode->SetInput( i, sgZeroNode );
			( *RGBAComponentLambda[ i ] )( sgSwizzleNode, 0 );
		}
	}
	return sgSwizzleNode;
}

spShadingNode MaterialNodes::SetUpColorCorrectionShadingNode( spShadingNode sgInputNode,
                                                              ColorCorrectionData& ColorCorrectionData,
                                                              std::basic_string<TCHAR>& tMaterialName,
                                                              TimeValue time )
{
	spShadingColorNode sgOneNode = sg->CreateShadingColorNode();
	sgOneNode->SetColor( 1.f, 1.f, 1.f, 1.f );

	spShadingColorNode sgZeroNode = sg->CreateShadingColorNode();
	sgZeroNode->SetColor( 0.f, 0.f, 0.f, 0.f );

	// rewire colorchannels
	spShadingNode sgRewiredTexturedNode;
	if( ColorCorrectionData.mRewireMode == eMaxRewireMode::reWireNormal )
	{
		sgRewiredTexturedNode = sgInputNode;
	}
	else if( ColorCorrectionData.mRewireMode == eMaxRewireMode::reWireMonochrome )
	{
		spShadingSwizzlingNode sgRedExtractNode = sg->CreateShadingSwizzlingNode();
		sgRedExtractNode->SetInput( 0, sgInputNode );
		sgRedExtractNode->SetInput( 1, sgInputNode );
		sgRedExtractNode->SetInput( 2, sgInputNode );
		sgRedExtractNode->SetInput( 3, sgOneNode );
		sgRedExtractNode->SetRedComponent( 0 );
		sgRedExtractNode->SetGreenComponent( 0 );
		sgRedExtractNode->SetBlueComponent( 0 );
		sgRedExtractNode->SetAlphaComponent( 0 );

		spShadingSwizzlingNode sgGreenExtractNode = sg->CreateShadingSwizzlingNode();
		sgGreenExtractNode->SetInput( 0, sgInputNode );
		sgGreenExtractNode->SetInput( 1, sgInputNode );
		sgGreenExtractNode->SetInput( 2, sgInputNode );
		sgGreenExtractNode->SetInput( 3, sgOneNode );
		sgGreenExtractNode->SetRedComponent( 1 );
		sgGreenExtractNode->SetGreenComponent( 1 );
		sgGreenExtractNode->SetBlueComponent( 1 );
		sgGreenExtractNode->SetAlphaComponent( 1 );

		spShadingSwizzlingNode sgBlueExtractNode = sg->CreateShadingSwizzlingNode();
		sgBlueExtractNode->SetInput( 0, sgInputNode );
		sgBlueExtractNode->SetInput( 1, sgInputNode );
		sgBlueExtractNode->SetInput( 2, sgInputNode );
		sgBlueExtractNode->SetInput( 3, sgOneNode );
		sgBlueExtractNode->SetRedComponent( 2 );
		sgBlueExtractNode->SetGreenComponent( 2 );
		sgBlueExtractNode->SetBlueComponent( 2 );
		sgBlueExtractNode->SetAlphaComponent( 2 );

		spShadingAddNode sgRed_GreenAddNode = sg->CreateShadingAddNode();
		sgRed_GreenAddNode->SetInput( 0, sgRedExtractNode );
		sgRed_GreenAddNode->SetInput( 1, sgGreenExtractNode );

		spShadingAddNode sgRG_BlueAddNode = sg->CreateShadingAddNode();
		sgRG_BlueAddNode->SetInput( 0, sgRed_GreenAddNode );
		sgRG_BlueAddNode->SetInput( 1, sgBlueExtractNode );

		spShadingColorNode sgOneThroughThreeNode = sg->CreateShadingColorNode();
		sgOneThroughThreeNode->SetColor( 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f, 1.f );

		spShadingMultiplyNode sgRGB_x_3Node = sg->CreateShadingMultiplyNode();
		sgRGB_x_3Node->SetInput( 0, sgRG_BlueAddNode );
		sgRGB_x_3Node->SetInput( 1, sgOneThroughThreeNode );
		////////////////////////////////////////////////////////////////////////////////////////////////////

		spShadingSwizzlingNode sgMonoNode = sg->CreateShadingSwizzlingNode();
		sgMonoNode->SetInput( 0, sgRGB_x_3Node );
		sgMonoNode->SetInput( 1, sgRGB_x_3Node );
		sgMonoNode->SetInput( 2, sgRGB_x_3Node );
		sgMonoNode->SetInput( 3, sgRGB_x_3Node );
		sgMonoNode->SetRedComponent( 0 );
		sgMonoNode->SetGreenComponent( 0 );
		sgMonoNode->SetBlueComponent( 0 );
		sgMonoNode->SetAlphaComponent( 0 );

		sgRewiredTexturedNode = sgMonoNode;
	}
	else if( ColorCorrectionData.mRewireMode == eMaxRewireMode::reWireInvert )
	{
		spShadingSubtractNode sgOneMinusTextureNode = sg->CreateShadingSubtractNode();
		sgOneMinusTextureNode->SetInput( 0, sgOneNode );
		sgOneMinusTextureNode->SetInput( 1, sgInputNode );

		sgRewiredTexturedNode = sgOneMinusTextureNode;
	}
	else if( ColorCorrectionData.mRewireMode == eMaxRewireMode::reWireCustom )
	{
		sgRewiredTexturedNode = ReWireColorCorrectionNode( sgInputNode,
		                                                   (eMaxColorCorrectionSwizzle)ColorCorrectionData.mRewireR,
		                                                   (eMaxColorCorrectionSwizzle)ColorCorrectionData.mRewireG,
		                                                   (eMaxColorCorrectionSwizzle)ColorCorrectionData.mRewireB,
		                                                   (eMaxColorCorrectionSwizzle)ColorCorrectionData.mRewireA );
	}

	float hueStrength = ColorCorrectionData.mHueTintStrength / 100.0f;
	float hslShift = ColorCorrectionData.mHueShift / 360.0f;
	float brightness = ColorCorrectionData.mBrightness / 100.0f;
	float contrast = ColorCorrectionData.mContrast / 100;
	float saturation = ColorCorrectionData.mSaturation / 100.0f;

	spShadingColorNode sgHSLInputNode = sg->CreateShadingColorNode();
	sgHSLInputNode->SetColor( hslShift, saturation, 0.0f, 1.0f );

	spShadingColorCorrectionNode sgShadingHSLTintNode = sg->CreateShadingColorCorrectionNode();
	sgShadingHSLTintNode->SetInput( 0, sgRewiredTexturedNode );
	sgShadingHSLTintNode->SetInput( 1, sgHSLInputNode );

	spShadingNode sgLightnessModifiedNode = sgShadingHSLTintNode;
	// ( value - 0.5f ) * ( 1.0f + m_Contrast ) + 0.5f + m_Brightness;
	if( ColorCorrectionData.mLightnessMode == 0 )
	{
		if( brightness != 1.0f || contrast != 1.0f )
		{
			spShadingColorNode sg05Node = sg->CreateShadingColorNode();
			sg05Node->SetColor( 0.5f, 0.5f, 0.5f, 1.0f );

			spShadingSubtractNode sgSubtract05Node = sg->CreateShadingSubtractNode();
			sgSubtract05Node->SetInput( 0, sgShadingHSLTintNode );
			sgSubtract05Node->SetInput( 1, sg05Node );

			spShadingColorNode sgContrastNode = sg->CreateShadingColorNode();
			sgContrastNode->SetColor( contrast, contrast, contrast, 1.0f );

			spShadingAddNode sgAddContrastOneNode = sg->CreateShadingAddNode();
			sgAddContrastOneNode->SetInput( 0, sgOneNode );
			sgAddContrastOneNode->SetInput( 1, sgContrastNode );

			spShadingMultiplyNode sgMultiplyNode = sg->CreateShadingMultiplyNode();
			sgMultiplyNode->SetInput( 0, sgSubtract05Node );
			sgMultiplyNode->SetInput( 1, sgAddContrastOneNode );

			spShadingColorNode sgBrightnessNode = sg->CreateShadingColorNode();
			sgBrightnessNode->SetColor( brightness, brightness, brightness, brightness );

			spShadingAddNode sgBrightness05_AddNode = sg->CreateShadingAddNode();
			sgBrightness05_AddNode->SetInput( 0, sg05Node );
			sgBrightness05_AddNode->SetInput( 1, sgBrightnessNode );

			spShadingAddNode sgFinalAddNode = sg->CreateShadingAddNode();
			sgFinalAddNode->SetInput( 0, sgBrightness05_AddNode );
			sgFinalAddNode->SetInput( 1, sgMultiplyNode );

			spShadingSwizzlingNode sgSwizzleOpacityNode = sg->CreateShadingSwizzlingNode();
			sgSwizzleOpacityNode->SetInput( 0, sgFinalAddNode );
			sgSwizzleOpacityNode->SetInput( 1, sgFinalAddNode );
			sgSwizzleOpacityNode->SetInput( 2, sgFinalAddNode );
			sgSwizzleOpacityNode->SetInput( 3, sgShadingHSLTintNode );
			sgSwizzleOpacityNode->SetRedComponent( 0 );
			sgSwizzleOpacityNode->SetGreenComponent( 1 );
			sgSwizzleOpacityNode->SetBlueComponent( 2 );
			sgSwizzleOpacityNode->SetAlphaComponent( 3 );

			sgLightnessModifiedNode = sgSwizzleOpacityNode;
		}
	}
	else
	{
		Color globalGainRGB = Color( ColorCorrectionData.mGainRGB, ColorCorrectionData.mGainRGB, ColorCorrectionData.mGainRGB );
		Color globalGammaRGB = Color( ColorCorrectionData.mGammaRGB, ColorCorrectionData.mGammaRGB, ColorCorrectionData.mGammaRGB );
		Color globalPivotRGB = Color( ColorCorrectionData.mPivotRGB, ColorCorrectionData.mPivotRGB, ColorCorrectionData.mPivotRGB );
		Color globalLiftRGB = Color( ColorCorrectionData.mLiftRGB, ColorCorrectionData.mLiftRGB, ColorCorrectionData.mLiftRGB );

		spShadingColorNode sgGlobalGainRGBNode = sg->CreateShadingColorNode();
		sgGlobalGainRGBNode->SetColor( globalGainRGB.r / 100.0f, globalGainRGB.g / 100.0f, globalGainRGB.b / 100.0f, 1.0f );

		spShadingColorNode sgGlobalGammaRGBNode = sg->CreateShadingColorNode();
		sgGlobalGammaRGBNode->SetColor( globalGammaRGB.r, globalGammaRGB.g, globalGammaRGB.b, 1.0f );

		spShadingColorNode sgGlobalPivotRGBNode = sg->CreateShadingColorNode();
		sgGlobalPivotRGBNode->SetColor( globalPivotRGB.r, globalPivotRGB.g, globalPivotRGB.b, 1.0f );

		spShadingColorNode sgGlobalLiftRGBNode = sg->CreateShadingColorNode();
		sgGlobalLiftRGBNode->SetColor( globalLiftRGB.r, globalLiftRGB.g, globalLiftRGB.b, 1.0f );

		spShadingNode sgGlobalLightNode = MaterialNodes::GetColorCorrectionLightSettings(
		    ColorCorrectionData, sgShadingHSLTintNode, sgGlobalGainRGBNode, sgGlobalGammaRGBNode, sgGlobalPivotRGBNode, sgGlobalLiftRGBNode );

		Color gainRGB = Color( ColorCorrectionData.mGainR, ColorCorrectionData.mGainG, ColorCorrectionData.mGainB );
		Color gammaRGB = Color( ColorCorrectionData.mGammaR, ColorCorrectionData.mGammaG, ColorCorrectionData.mGammaB );
		Color pivotRGB = Color( ColorCorrectionData.mPivotR, ColorCorrectionData.mPivotG, ColorCorrectionData.mPivotB );
		Color liftRGB = Color( ColorCorrectionData.mLiftR, ColorCorrectionData.mLiftG, ColorCorrectionData.mLiftB );

		spShadingColorNode sgGainRGBNode = sg->CreateShadingColorNode();
		sgGainRGBNode->SetColor( gainRGB.r / 100.0f, gainRGB.g / 100.0f, gainRGB.b / 100.0f, 1.0f );

		spShadingColorNode sgGammaRGBNode = sg->CreateShadingColorNode();
		sgGammaRGBNode->SetColor( gammaRGB.r, gammaRGB.g, gammaRGB.b, 1.0f );

		spShadingColorNode sgPivotRGBNode = sg->CreateShadingColorNode();
		sgPivotRGBNode->SetColor( pivotRGB.r, pivotRGB.g, pivotRGB.b, 1.0f );

		spShadingColorNode sgLiftRGBNode = sg->CreateShadingColorNode();
		sgLiftRGBNode->SetColor( liftRGB.r, liftRGB.g, liftRGB.b, 1.0f );

		spShadingNode FinalNode = MaterialNodes::GetColorCorrectionLightSettings(
		    ColorCorrectionData, sgGlobalLightNode, sgGainRGBNode, sgGammaRGBNode, sgPivotRGBNode, sgLiftRGBNode );

		spShadingSwizzlingNode sgSwizzleChannelNode = sg->CreateShadingSwizzlingNode();
		sgSwizzleChannelNode->SetInput( 0, ColorCorrectionData.mEnableR == true ? FinalNode : sgGlobalLightNode );
		sgSwizzleChannelNode->SetInput( 1, ColorCorrectionData.mEnableG == true ? FinalNode : sgGlobalLightNode );
		sgSwizzleChannelNode->SetInput( 2, ColorCorrectionData.mEnableB == true ? FinalNode : sgGlobalLightNode );
		sgSwizzleChannelNode->SetInput( 3, sgShadingHSLTintNode );
		sgSwizzleChannelNode->SetRedComponent( 0 );
		sgSwizzleChannelNode->SetGreenComponent( 1 );
		sgSwizzleChannelNode->SetBlueComponent( 2 );
		sgSwizzleChannelNode->SetAlphaComponent( 3 );

		sgLightnessModifiedNode = sgSwizzleChannelNode;
	}

	spShadingClampNode sgClampNode = sg->CreateShadingClampNode();
	sgClampNode->SetInput( 0, sgLightnessModifiedNode );
	sgClampNode->SetInput( 1, sgZeroNode );
	sgClampNode->SetInput( 2, sgOneNode );

	return sgClampNode;
}

const bool MaterialNodes::GetData( IParamBlock2* mParamBlock, const ParamID paramId, const TimeValue time, std::vector<AColor>* outValue )
{
	int itemCount = mParamBlock->Count( paramId );
	for( int i = 0; i < itemCount; ++i )
	{
		outValue->emplace_back( mParamBlock->GetAColor( paramId, time, i ) );
	}
	return itemCount != 0;
}

const bool MaterialNodes::GetData( IParamBlock2* mParamBlock, const ParamID paramId, const TimeValue time, std::vector<int>* outValue )
{
	int itemCount = mParamBlock->Count( paramId );
	for( int i = 0; i < itemCount; ++i )
	{
		outValue->emplace_back( mParamBlock->GetInt( paramId, time, i ) );
	}
	return itemCount != 0;
}

const bool MaterialNodes::GetData( IParamBlock2* mParamBlock, const ParamID paramId, const TimeValue time, std::vector<float>* outValue )
{
	int itemCount = mParamBlock->Count( paramId );
	for( int i = 0; i < itemCount; ++i )
	{
		outValue->emplace_back( mParamBlock->GetFloat( paramId, time, i ) );
	}
	return itemCount != 0;
}

const bool MaterialNodes::GetData( IParamBlock2* mParamBlock, const ParamID paramId, const TimeValue time, std::vector<std::basic_string<TCHAR>>* outValue )
{
	int itemCount = mParamBlock->Count( paramId );
	for( int i = 0; i < itemCount; ++i )
	{
		outValue->emplace_back( mParamBlock->GetStr( paramId, time, i ) );
	}
	return itemCount != 0;
}

const bool MaterialNodes::GetData( IParamBlock2* mParamBlock, const ParamID paramId, const TimeValue time, std::vector<Color>* outValue )
{
	int itemCount = mParamBlock->Count( paramId );
	for( int i = 0; i < itemCount; ++i )
	{
		outValue->emplace_back( mParamBlock->GetColor( paramId, time, i ) );
	}
	return itemCount != 0;
}

const bool MaterialNodes::GetData( IParamBlock2* mParamBlock, const ParamID paramId, const TimeValue time, std::vector<eMaxBlendMode>* outValue )
{
	int itemCount = mParamBlock->Count( paramId );
	for( int i = 0; i < itemCount; ++i )
	{
		outValue->emplace_back( static_cast<eMaxBlendMode>( mParamBlock->GetInt( paramId, time, i ) ) );
	}
	return itemCount != 0;
}

spShadingNode MaterialNodes::RunTintNode( Texmap* mTexMap, MaterialNodes::MaterialChannelData& mMaterialChannel )
{
	spShadingNode ShadingNode;

	Color mRedChannel;
	Color mGreenChannel;
	Color mBlueChannel;

	GetTexMapProperty<Color>( mTexMap, _T("red"), mMaterialChannel.mTime, &mRedChannel );
	GetTexMapProperty<Color>( mTexMap, _T("green"), mMaterialChannel.mTime, &mGreenChannel );
	GetTexMapProperty<Color>( mTexMap, _T("blue"), mMaterialChannel.mTime, &mBlueChannel );

	int isEnabled = 0;
	const bool bEnableMapsFound = GetTexMapProperty<int>( mTexMap, _T("map1Enabled"), mMaterialChannel.mTime, &isEnabled );
	const bool bIsEnabled = isEnabled == 1;

	Texmap* mSubTexMap = mTexMap->GetSubTexmap( 0 );
	ShadingNode = SimplygonMaxInstance->CreateSgMaterial( mSubTexMap, mMaterialChannel );
	if( ShadingNode == NullPtr )
	{
		return NullPtr;
	}

	return MaterialNodes::SetUpTintShadingNode( ShadingNode, mMaterialChannel.mMaterialName, mRedChannel, mGreenChannel, mBlueChannel, mMaterialChannel.mTime );
}

spShadingNode
MaterialNodes::RunBitmapNode( Texmap* mTexMap, MaterialNodes::MaterialChannelData& mMaterialChannel, TextureSettingsOverride* textureSettingsOverride )
{
	const char* materialName = LPCTSTRToConstCharPtr( mMaterialChannel.mMaterialName.c_str() );
	const char* channelName = LPCTSTRToConstCharPtr( mMaterialChannel.mChannelName.c_str() );

	std::basic_string<TCHAR> tMaxMappingChannel = SimplygonMaxInstance->SetupMaxMappingChannel( materialName, channelName, mTexMap );

	// allocate and writedata to TextureData
	MaterialNodes::TextureData mTextureData( mTexMap );

	bool bIsSRGB = false;

	// change sRGB based on gamma
	const float gamma = GetBitmapTextureGamma( mTextureData.mBitmap );
	if( gamma <= 2.21000000f && gamma >= 2.19000000f )
	{
		bIsSRGB = true;
	}
	// GetImageFullFilePath( mTextureData.mBitmap->GetMapName(), tTexturePath );
	std::basic_string<TCHAR> tTexturePath = mTextureData.mBitmap->GetMapName();

	// if the path length is > 0, try to import texture to temporary directory, if the full path can not be resolved in ImportTexture a stand-in texture will be
	// written and warning message outputted.
	if( tTexturePath.length() > 0 )
	{
		// if normal map, disable sRGB
		if( mMaterialChannel.mMaxChannelId == ID_BU )
		{
			bIsSRGB = false;
		}

		if( mMaterialChannel.mMaterialTextureOverrides )
		{
			std::basic_string<TCHAR> tTexturePathOverride = _T("");
			for( size_t channelIndex = 0; channelIndex < mMaterialChannel.mMaterialTextureOverrides->size(); ++channelIndex )
			{
				const MaterialTextureOverride& textureOverride = ( *mMaterialChannel.mMaterialTextureOverrides )[ channelIndex ];
				if( strcmp( materialName, LPCTSTRToConstCharPtr( textureOverride.MaterialName.c_str() ) ) == 0 )
				{
					if( strcmp( channelName, LPCTSTRToConstCharPtr( textureOverride.MappingChannelName.c_str() ) ) == 0 )
					{
						tTexturePathOverride = textureOverride.TextureFileName;
						break;
					}
				}
			}

			if( tTexturePathOverride.length() > 0 )
			{
				tTexturePath = tTexturePathOverride;
				//_stprintf_s( tTexturePath, _T("%s"), tTexturePathOverride.c_str() );
			}
		}

		if( bIsSRGB )
		{
			IColorCorrectionMgr* idispGamMgr = (IColorCorrectionMgr*)GetCOREInterface( COLORCORRECTIONMGR_INTERFACE );
			if( idispGamMgr )
			{
				auto mode = idispGamMgr->GetColorCorrectionMode();

				if( mode == IColorCorrectionMgr::CorrectionMode::kNONE )
				{
					bIsSRGB = false;
				}
				else if( mode == IColorCorrectionMgr::CorrectionMode::kGAMMA )
				{
					float gammasetting = idispGamMgr->GetGamma();
					if( !( gammasetting < 2.3f && gammasetting > 2.1f ) )
					{
						bIsSRGB = false;
					}
				}
			}
		}

		mTextureData.mTexturePathWithName = SimplygonMaxInstance->ImportTexture( tTexturePath );
		mTextureData.mTextureName = GetTitleOfFile( mTextureData.mTexturePathWithName );
		mTextureData.mTextureExtension = GetExtensionOfFile( mTextureData.mTexturePathWithName );
		mTextureData.mTextureNameWithExtension = mTextureData.mTextureName + mTextureData.mTextureExtension;
		mTextureData.mIsSRGB = bIsSRGB;
		mTextureData.mUseAlphaAsTransparency = HasActiveTransparency( mTextureData.mBitmap );
		mTextureData.mHasAlpha = TextureHasAlpha( LPCTSTRToConstCharPtr( mTextureData.mTextureNameWithExtension.c_str() ) );
		mTextureData.mPremultipliedAlpha = mTextureData.mBitmap->GetPremultAlpha( TRUE ) == TRUE;
		mTextureData.mAlphaSource = mTextureData.mBitmap->GetAlphaSource();
		if( textureSettingsOverride )
		{
			mTextureData.mAlphaSource =
			    textureSettingsOverride->mEnabledAlphaSourceOverride ? textureSettingsOverride->mAlphaSource : mTextureData.mAlphaSource;

			mTextureData.mIsSRGB = textureSettingsOverride->mEnabledSRGBOverride ? textureSettingsOverride->mSRGB : mTextureData.mIsSRGB;

			mTextureData.mPremultipliedAlpha =
			    textureSettingsOverride->mEnabledPremultOverride ? textureSettingsOverride->mPremultipliedAlpha : mTextureData.mPremultipliedAlpha;
		}

		// map TextureData
		SimplygonMaxInstance->CreateAndLinkTexture( mTextureData );

		// setup the bitmapNode
		return MaterialNodes::SetUpBitmapShadingNode(
		    mMaterialChannel.mMaterialName, tMaxMappingChannel, mTextureData, mMaterialChannel.mMaxChannelId, mMaterialChannel.mTime );
	}
	// nodes with null/empty paths
	else
	{
#if MAX_VERSION_MAJOR >= 26
		MSTR nodeInfo = mTexMap->GetFullName( true );
#else
		MSTR nodeInfo = mTexMap->GetFullName();
#endif

		mMaterialChannel.mWarningMessage += _T("An empty (or unknown) material node with id: ");
		mMaterialChannel.mWarningMessage += nodeInfo;
		mMaterialChannel.mWarningMessage += _T(" was detected in material ");
		mMaterialChannel.mWarningMessage += mMaterialChannel.mMaterialName;
		mMaterialChannel.mWarningMessage += _T(" on channel ");
		mMaterialChannel.mWarningMessage += mMaterialChannel.mChannelName;

		return NullPtr;
	}
}

spShadingNode MaterialNodes::RunMultiplyNode( Texmap* mTexMap, MaterialNodes::MaterialChannelData& mMaterialChannel )
{
	// first get texmap
	spShadingNode mShadingNodes[ 2 ];
	Color mColors[ 2 ];

	//
	const bool bHasColor[ 2 ] = { GetTexMapProperty<Color>( mTexMap, _T("color1"), mMaterialChannel.mTime, &mColors[ 0 ] ),
	                              GetTexMapProperty<Color>( mTexMap, _T("color2"), mMaterialChannel.mTime, &mColors[ 1 ] ) };

	//
	int bIsEnabled[ 2 ] = { 0, 0 };
	const bool bEnableMapsFound[ 2 ] = { GetTexMapProperty<int>( mTexMap, _T("map1Enabled"), mMaterialChannel.mTime, &bIsEnabled[ 0 ] ),
	                                     GetTexMapProperty<int>( mTexMap, _T("map2Enabled"), mMaterialChannel.mTime, &bIsEnabled[ 1 ] ) };

	//
	int alphaFrom = 0;
	const bool bAlphaFromFound = GetTexMapProperty<int>( mTexMap, _T("alphaFrom"), mMaterialChannel.mTime, &alphaFrom );
	MaterialNodes::MultiplyNodeAlphaFrom mAlphaFrom = (MaterialNodes::MultiplyNodeAlphaFrom)alphaFrom;

	// Map TextureData
	for( int i = 0; i < 2; ++i )
	{
		Texmap* mSubTexMap = mTexMap->GetSubTexmap( i );
		bool bWriteColor = !bIsEnabled[ i ];

		if( bIsEnabled[ i ] )
		{
			mShadingNodes[ i ] = SimplygonMaxInstance->CreateSgMaterial( mSubTexMap, mMaterialChannel );
			if( mShadingNodes[ i ] == NullPtr )
			{
				return NullPtr;
			}
		}

		// if the slot is disabled, write the colors for the corresponding slots
		if( bWriteColor )
		{
			spShadingColorNode sgColorNode = sg->CreateShadingColorNode();
			sgColorNode->SetColor( mColors[ i ].r, mColors[ i ].g, mColors[ i ].b, 1.0f );

			mShadingNodes[ i ] = sgColorNode;
		}
	}

	return MaterialNodes::SetUpMultiplyShadingNode( mShadingNodes, mAlphaFrom, mMaterialChannel.mMaterialName, mMaterialChannel.mTime );
}

spShadingNode MaterialNodes::RunCompositeNode( Texmap* mTexMap, MaterialNodes::MaterialChannelData& mMaterialChannel )
{
	std::vector<int> textureEnableFlags;
	std::vector<int> maskEnableFlags;
	std::vector<MaterialNodes::eMaxBlendMode> maxBlendMode;
	std::vector<std::basic_string<TCHAR>> mLayerName;
	std::vector<float> textureOpacity;

	MaterialNodes::GetTexMapProperties<int>( mTexMap, _T("mapEnabled"), mMaterialChannel.mTime, &textureEnableFlags );
	MaterialNodes::GetTexMapProperties<int>( mTexMap, _T("maskEnabled"), mMaterialChannel.mTime, &maskEnableFlags );
	MaterialNodes::GetTexMapProperties<eMaxBlendMode>( mTexMap, _T("blendMode"), mMaterialChannel.mTime, &maxBlendMode );
	MaterialNodes::GetTexMapProperties<std::basic_string<TCHAR>>( mTexMap, _T("layerName"), mMaterialChannel.mTime, &mLayerName );
	MaterialNodes::GetTexMapProperties<float>( mTexMap, _T("opacity"), mMaterialChannel.mTime, &textureOpacity );

	std::vector<bool> mMaskEnabled;
	std::vector<ETextureBlendType> mTextureBlendmodes;
	std::vector<float> mTextureOpacity;

	std::vector<spShadingNode> mTextureNodes;
	std::vector<spShadingNode> mMaskNodes;
	for( int i = 0, j = 0; i < textureEnableFlags.size() * 2; i += 2, ++j )
	{
		// skip inactive layers
		if( !textureEnableFlags[ j ] )
		{
			continue;
		}

		if( mTexMap->GetSubTexmap( i ) )
		{
			Texmap* mSubTexMap = mTexMap->GetSubTexmap( i );

			spShadingNode shadingNode = SimplygonMaxInstance->CreateSgMaterial( mSubTexMap, mMaterialChannel );
			if( shadingNode == NullPtr )
			{
				return NullPtr;
			}
			mTextureNodes.emplace_back( shadingNode );
		}
		else
		{
			// no texture, continue to next
			continue;
		}

		if( mTexMap->GetSubTexmap( i + 1 ) )
		{
			Texmap* mSubTexMap = mTexMap->GetSubTexmap( i + 1 );

			// masks needs to be forced to opaque and nonSrgb
			MaterialNodes::TextureSettingsOverride textureOverride = MaterialNodes::TextureSettingsOverride();
			textureOverride.mEnabledAlphaSourceOverride = true;
			textureOverride.mAlphaSource = ALPHA_NONE;

			textureOverride.mEnabledSRGBOverride = true;
			textureOverride.mSRGB = false;

			spShadingNode shadingNode = SimplygonMaxInstance->CreateSgMaterial( mSubTexMap, mMaterialChannel, &textureOverride );
			if( shadingNode == NullPtr )
			{
				return NullPtr;
			}

			mMaskNodes.emplace_back( shadingNode );
		}
		else
		{
			spShadingColorNode sgColorNode;
			mMaskNodes.emplace_back( sgColorNode );
		}

		mMaskEnabled.emplace_back( maskEnableFlags[ j ] > 0 );
		mTextureOpacity.emplace_back( textureOpacity[ j ] );

		switch( maxBlendMode[ j ] )
		{
			case MaterialNodes::eMaxBlendMode::eNormal:
			{
				mTextureBlendmodes.emplace_back( ETextureBlendType::Alpha );
				break;
			}
			// case eMaxBlendMode::eAddition:
			// {
			// 	mTextureBlendmodes.emplace_back( ETextureBlendType::Add );
			// 	break;
			// }
			// case eMaxBlendMode::eSubtract:
			// {
			// 	mTextureBlendmodes.emplace_back( ETextureBlendType::Subtract );
			// 	break;
			// }
			// case eMaxBlendMode::eMultiply:
			// {
			// 	mTextureBlendmodes.emplace_back( ETextureBlendType::Multiply );
			// 	break;
			// }
			default:
			{
				// not supported
				GlobalLogMaterialNodeMessage( mTexMap,
				                              mMaterialChannel.mMaterialName,
				                              mMaterialChannel.mChannelName,
				                              true,
				                              L"Blending mode unsupported, " + mLayerName[ j ] + L" defaulting to Normal blending mode." );
				mTextureBlendmodes.emplace_back( ETextureBlendType::Alpha );
				break;
			}
		}
	}

	if( !mTextureBlendmodes.empty() )
	{
		mTextureBlendmodes[ 0 ] = ETextureBlendType::Replace;
	}

	return MaterialNodes::SetUpCompositeShadingNode(
	    mTextureNodes, mMaskNodes, mTextureBlendmodes, mTextureOpacity, mMaterialChannel.mMaterialName, mMaterialChannel.mTime );
}

spShadingNode MaterialNodes::RunColorCorrectionNode( Texmap* mTexMap, MaterialNodes::MaterialChannelData& mMaterialChannel )
{
	int enableRed = 0;
	int enableGreen = 0;
	int enableBlue = 0;

	ColorCorrectionData colorCorrectionData;
	AColor mColor = AColor( 0.0f, 0.0f, 0.0f, 1.0f );
	spShadingNode node;

	GetTexMapProperty<AColor>( mTexMap, _T("color"), mMaterialChannel.mTime, &mColor );
	GetTexMapProperty<float>( mTexMap, _T("brightness"), mMaterialChannel.mTime, &colorCorrectionData.mBrightness );
	GetTexMapProperty<float>( mTexMap, _T("contrast"), mMaterialChannel.mTime, &colorCorrectionData.mContrast );
	GetTexMapProperty<int>( mTexMap, _T("rewireMode"), mMaterialChannel.mTime, &colorCorrectionData.mRewireMode );
	GetTexMapProperty<int>( mTexMap, _T("rewireR"), mMaterialChannel.mTime, &colorCorrectionData.mRewireR );
	GetTexMapProperty<int>( mTexMap, _T("rewireG"), mMaterialChannel.mTime, &colorCorrectionData.mRewireG );
	GetTexMapProperty<int>( mTexMap, _T("rewireB"), mMaterialChannel.mTime, &colorCorrectionData.mRewireB );
	GetTexMapProperty<int>( mTexMap, _T("rewireA"), mMaterialChannel.mTime, &colorCorrectionData.mRewireA );

	GetTexMapProperty<int>( mTexMap, _T("exposureMode"), mMaterialChannel.mTime, &colorCorrectionData.mExposureMode );
	GetTexMapProperty<float>( mTexMap, _T("gainRGB"), mMaterialChannel.mTime, &colorCorrectionData.mGainRGB );
	GetTexMapProperty<float>( mTexMap, _T("gainR"), mMaterialChannel.mTime, &colorCorrectionData.mGainR );
	GetTexMapProperty<float>( mTexMap, _T("gainG"), mMaterialChannel.mTime, &colorCorrectionData.mGainG );
	GetTexMapProperty<float>( mTexMap, _T("gainB"), mMaterialChannel.mTime, &colorCorrectionData.mGainB );
	GetTexMapProperty<float>( mTexMap, _T("gammaRGB"), mMaterialChannel.mTime, &colorCorrectionData.mGammaRGB );
	GetTexMapProperty<float>( mTexMap, _T("gammaR"), mMaterialChannel.mTime, &colorCorrectionData.mGammaR );
	GetTexMapProperty<float>( mTexMap, _T("gammaG"), mMaterialChannel.mTime, &colorCorrectionData.mGammaG );
	GetTexMapProperty<float>( mTexMap, _T("gammaB"), mMaterialChannel.mTime, &colorCorrectionData.mGammaB );
	GetTexMapProperty<float>( mTexMap, _T("hueShift"), mMaterialChannel.mTime, &colorCorrectionData.mHueShift );
	GetTexMapProperty<AColor>( mTexMap, _T("tint"), mMaterialChannel.mTime, &colorCorrectionData.mHueTint );
	GetTexMapProperty<float>( mTexMap, _T("tintStrength"), mMaterialChannel.mTime, &colorCorrectionData.mHueTintStrength );
	GetTexMapProperty<float>( mTexMap, _T("liftRGB"), mMaterialChannel.mTime, &colorCorrectionData.mLiftRGB );
	GetTexMapProperty<float>( mTexMap, _T("liftR"), mMaterialChannel.mTime, &colorCorrectionData.mLiftR );
	GetTexMapProperty<float>( mTexMap, _T("liftG"), mMaterialChannel.mTime, &colorCorrectionData.mLiftG );
	GetTexMapProperty<float>( mTexMap, _T("liftB"), mMaterialChannel.mTime, &colorCorrectionData.mLiftB );
	GetTexMapProperty<int>( mTexMap, _T("lightnessMode"), mMaterialChannel.mTime, &colorCorrectionData.mLightnessMode );
	GetTexMapProperty<float>( mTexMap, _T("saturation"), mMaterialChannel.mTime, &colorCorrectionData.mSaturation );
	GetTexMapProperty<float>( mTexMap, _T("pivotRGB"), mMaterialChannel.mTime, &colorCorrectionData.mPivotRGB );
	GetTexMapProperty<float>( mTexMap, _T("pivotR"), mMaterialChannel.mTime, &colorCorrectionData.mPivotR );
	GetTexMapProperty<float>( mTexMap, _T("pivotG"), mMaterialChannel.mTime, &colorCorrectionData.mPivotG );
	GetTexMapProperty<float>( mTexMap, _T("pivotB"), mMaterialChannel.mTime, &colorCorrectionData.mPivotB );
	GetTexMapProperty<float>( mTexMap, _T("printerLights"), mMaterialChannel.mTime, &colorCorrectionData.mPrinterLights );

	GetTexMapProperty<int>( mTexMap, _T("enableR"), mMaterialChannel.mTime, &enableRed );
	GetTexMapProperty<int>( mTexMap, _T("enableG"), mMaterialChannel.mTime, &enableGreen );
	GetTexMapProperty<int>( mTexMap, _T("enableB"), mMaterialChannel.mTime, &enableBlue );

	colorCorrectionData.mEnableR = enableRed == 1;
	colorCorrectionData.mEnableG = enableGreen == 1;
	colorCorrectionData.mEnableB = enableBlue == 1;

	Texmap* mSubTexMap = mTexMap->GetSubTexmap( 0 );
	bool bWriteColor = mSubTexMap == nullptr;
	if( mSubTexMap )
	{
		node = SimplygonMaxInstance->CreateSgMaterial( mSubTexMap, mMaterialChannel );
		if( node == NullPtr )
		{
			return NullPtr;
		}
	}

	if( bWriteColor )
	{
		spShadingColorNode sgColorNode = sg->CreateShadingColorNode();
		sgColorNode->SetColor( mColor.r, mColor.g, mColor.b, mColor.a );

		node = sgColorNode;
	}

	return MaterialNodes::SetUpColorCorrectionShadingNode( node, colorCorrectionData, mMaterialChannel.mMaterialName, mMaterialChannel.mTime );
}

spShadingNode MaterialNodes::GetColorCorrectionLightSettings( ColorCorrectionData& sgColorCorrectionData,
                                                              spShadingNode sgInputColor,
                                                              spShadingColorNode sgGainRGBNode,
                                                              spShadingColorNode sgGammaRGBNode,
                                                              spShadingColorNode sgPivotRGBNode,
                                                              spShadingColorNode sgLiftRGBNode )
{
	spShadingColorNode sgOneNode = sg->CreateShadingColorNode();
	sgOneNode->SetColor( 1.0f, 1.0f, 1.0f, 1.0f );

	spShadingNode sgReturnNode;
	switch( sgColorCorrectionData.mExposureMode )
	{
		case 0: // Gain
		{
			// m_Pivot* pow( value * m_Gain / m_Pivot, 1.0f / m_Gamma ) + m_Lift;
			spShadingMultiplyNode sgHSLXGainNode = sg->CreateShadingMultiplyNode();
			sgHSLXGainNode->SetInput( 0, sgInputColor );
			sgHSLXGainNode->SetInput( 1, sgGainRGBNode );

			spShadingDivideNode sgMulDivPivotNode = sg->CreateShadingDivideNode();
			sgMulDivPivotNode->SetInput( 0, sgHSLXGainNode );
			sgMulDivPivotNode->SetInput( 1, sgPivotRGBNode );

			spShadingDivideNode sgOneDivGammaNode = sg->CreateShadingDivideNode();
			sgOneDivGammaNode->SetInput( 0, sgOneNode );
			sgOneDivGammaNode->SetInput( 1, sgGammaRGBNode );

			spShadingPowNode sgPowNode = sg->CreateShadingPowNode();
			sgPowNode.SetInput( 0, sgMulDivPivotNode );
			sgPowNode.SetInput( 1, sgOneDivGammaNode );

			spShadingMultiplyNode sgMultiplyNode = sg->CreateShadingMultiplyNode();
			sgMultiplyNode->SetInput( 0, sgPowNode );
			sgMultiplyNode->SetInput( 1, sgPivotRGBNode );

			spShadingAddNode sgAddNode = sg->CreateShadingAddNode();
			sgAddNode.SetInput( 0, sgMultiplyNode );
			sgAddNode.SetInput( 1, sgLiftRGBNode );

			// add clamp here

			spShadingSwizzlingNode sgSwizzleOpacity = sg->CreateShadingSwizzlingNode();
			sgSwizzleOpacity->SetInput( 0, sgAddNode );
			sgSwizzleOpacity->SetInput( 1, sgAddNode );
			sgSwizzleOpacity->SetInput( 2, sgAddNode );
			sgSwizzleOpacity->SetInput( 3, sgInputColor );
			sgSwizzleOpacity->SetRedComponent( 0 );
			sgSwizzleOpacity->SetGreenComponent( 1 );
			sgSwizzleOpacity->SetBlueComponent( 2 );
			sgSwizzleOpacity->SetAlphaComponent( 3 );

			sgReturnNode = sgSwizzleOpacity;
			break;
		}
		case 1: // FStops
		{
			// m_Pivot* pow( value * pow( 2.0f, m_Gain ) / m_Pivot, 1.0f / m_Gamma ) + m_Lift;

			spShadingColorNode sg2Node = sg->CreateShadingColorNode();
			sg2Node->SetColor( 2.0f, 2.0f, 2.0f, 1.0f );

			spShadingPowNode sg2PowGainNode = sg->CreateShadingPowNode();
			sg2PowGainNode.SetInput( 0, sg2Node );
			sg2PowGainNode.SetInput( 1, sgGainRGBNode );

			spShadingMultiplyNode sgXPowNode = sg->CreateShadingMultiplyNode();
			sgXPowNode->SetInput( 0, sgInputColor );
			sgXPowNode->SetInput( 1, sg2PowGainNode );

			spShadingDivideNode sgMulDivPivotNode = sg->CreateShadingDivideNode();
			sgMulDivPivotNode->SetInput( 0, sgXPowNode );
			sgMulDivPivotNode->SetInput( 1, sgPivotRGBNode );

			spShadingDivideNode sgOneDivGammaNode = sg->CreateShadingDivideNode();
			sgOneDivGammaNode->SetInput( 0, sgOneNode );
			sgOneDivGammaNode->SetInput( 1, sgGammaRGBNode );

			spShadingPowNode sgPowNode = sg->CreateShadingPowNode();
			sgPowNode.SetInput( 0, sgMulDivPivotNode );
			sgPowNode.SetInput( 1, sgOneDivGammaNode );

			spShadingMultiplyNode sgMultiplyNode = sg->CreateShadingMultiplyNode();
			sgMultiplyNode->SetInput( 0, sgPowNode );
			sgMultiplyNode->SetInput( 1, sgPivotRGBNode );

			spShadingPowNode sgAddNode = sg->CreateShadingPowNode();
			sgAddNode.SetInput( 0, sgMultiplyNode );
			sgAddNode.SetInput( 1, sgLiftRGBNode );

			spShadingSwizzlingNode sgSwizzleOpacity = sg->CreateShadingSwizzlingNode();
			sgSwizzleOpacity->SetInput( 0, sgAddNode );
			sgSwizzleOpacity->SetInput( 1, sgAddNode );
			sgSwizzleOpacity->SetInput( 2, sgAddNode );
			sgSwizzleOpacity->SetInput( 3, sgInputColor );
			sgSwizzleOpacity->SetRedComponent( 0 );
			sgSwizzleOpacity->SetGreenComponent( 1 );
			sgSwizzleOpacity->SetBlueComponent( 2 );
			sgSwizzleOpacity->SetAlphaComponent( 3 );

			sgReturnNode = sgSwizzleOpacity;
			break;
		}
		case 2: // Printer
		{
			// m_Pivot* pow( value * pow( m_Printer, m_Gain ) / m_Pivot, 1.0f / m_Gamma ) + m_Lift;

			spShadingColorNode sg2Node = sg->CreateShadingColorNode();
			sg2Node->SetColor( 2.0f, 2.0f, 2.0f, 1.0f );

			spShadingPowNode sg2PowGainNode = sg->CreateShadingPowNode();
			sg2PowGainNode.SetInput( 0, sg2Node );
			sg2PowGainNode.SetInput( 1, sgGainRGBNode );

			spShadingMultiplyNode sgXPowNode = sg->CreateShadingMultiplyNode();
			sgXPowNode->SetInput( 0, sgInputColor );
			sgXPowNode->SetInput( 1, sg2PowGainNode );

			spShadingDivideNode sgMulDivPivotNode = sg->CreateShadingDivideNode();
			sgMulDivPivotNode->SetInput( 0, sgXPowNode );
			sgMulDivPivotNode->SetInput( 1, sgPivotRGBNode );

			spShadingDivideNode sgOneDivGammaNode = sg->CreateShadingDivideNode();
			sgOneDivGammaNode->SetInput( 0, sgOneNode );
			sgOneDivGammaNode->SetInput( 1, sgGammaRGBNode );

			spShadingPowNode sgPowNode = sg->CreateShadingPowNode();
			sgPowNode.SetInput( 0, sgMulDivPivotNode );
			sgPowNode.SetInput( 1, sgOneDivGammaNode );

			spShadingMultiplyNode sgMultiplyNode = sg->CreateShadingMultiplyNode();
			sgMultiplyNode->SetInput( 0, sgPowNode );
			sgMultiplyNode->SetInput( 1, sgPivotRGBNode );

			spShadingPowNode sgAddNode = sg->CreateShadingPowNode();
			sgAddNode.SetInput( 0, sgMultiplyNode );
			sgAddNode.SetInput( 1, sgLiftRGBNode );

			sgReturnNode = sgAddNode;
			break;
		}
		default:
		{
			spShadingColorNode sgBlackNode = sg->CreateShadingColorNode();
			sgBlackNode->SetColor( 0.0f, 0.0f, 0.0f, 1.0f );

			sgReturnNode = sgBlackNode;
		}
	}

	return sgReturnNode;
}

class PhysicalMaterial
{
	private:
	std::map<std::basic_string<TCHAR>, int> intProps;
	std::map<std::basic_string<TCHAR>, float> floatProps;
	std::map<std::basic_string<TCHAR>, bool> boolProps;
	std::map<std::basic_string<TCHAR>, Point4*> point4Props;
	std::map<std::basic_string<TCHAR>, Texmap*> texmapProps;

	std::basic_string<TCHAR> tMaterialProperties;

	SimplygonMax* MaxReference = nullptr;

	public:
	PhysicalMaterial( SimplygonMax* mMaxReference )
	    : MaxReference( mMaxReference )
	{
	}

	void CreateMaterialChannel( spMaterial sgMaterial, std::basic_string<TCHAR> tMaterialChannel )
	{
		const char* cChannelName = LPCTSTRToConstCharPtr( tMaterialChannel.c_str() );

		if( !sgMaterial->HasMaterialChannel( cChannelName ) )
		{
			sgMaterial->AddMaterialChannel( cChannelName );
		}
	}

	Texmap* GetMap( std::basic_string<TCHAR> tMapName )
	{
		std::map<std::basic_string<TCHAR>, Texmap*>::const_iterator& mTexMap = this->texmapProps.find( tMapName );
		if( mTexMap != this->texmapProps.end() )
		{
			return mTexMap->second;
		}

		return nullptr;
	}

	Point4* GetColor4( std::basic_string<TCHAR> tColor4Name )
	{
		std::map<std::basic_string<TCHAR>, Point4*>::const_iterator& mTexMap = this->point4Props.find( tColor4Name );
		if( mTexMap != this->point4Props.end() )
		{
			return mTexMap->second;
		}

		return nullptr;
	}

	float GetFloat( std::basic_string<TCHAR> tFloatName, bool& bValueFound )
	{
		std::map<std::basic_string<TCHAR>, float>::const_iterator& mTexMap = this->floatProps.find( tFloatName );

		bValueFound = mTexMap != this->floatProps.end();
		if( bValueFound )
		{
			return mTexMap->second;
		}

		return 0.f;
	}

	float GetFloat( std::basic_string<TCHAR> tFloatName )
	{
		bool bValueFound = false;
		return GetFloat( tFloatName, bValueFound );
	}

	bool GetBool( std::basic_string<TCHAR> tBoolName, bool& bValueFound )
	{
		std::map<std::basic_string<TCHAR>, bool>::const_iterator& mTexMap = this->boolProps.find( tBoolName );

		bValueFound = mTexMap != this->boolProps.end();
		if( bValueFound )
		{
			return mTexMap->second;
		}

		return false;
	}

	bool GetBool( std::basic_string<TCHAR> tBoolName )
	{
		bool bValueFound = false;
		return GetBool( tBoolName, bValueFound );
	}

	bool HasValidTexMap( Texmap* mTexMap, bool bIsEnabled )
	{
		if( bIsEnabled && mTexMap )
		{
			return true;
		}

		return false;
	}

	bool TexmapHasAlphaAsTransparency( Texmap* mTexMap )
	{
		if( mTexMap == nullptr )
		{
			return false;
		}
		else if( mTexMap->ClassID() == Class_ID( BMTEX_CLASS_ID, 0 ) )
		{
			TCHAR tTexturepath[ MAX_PATH ];
			BitmapTex* mBitmapTex = (BitmapTex*)mTexMap;

			if( mBitmapTex )
			{
				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturepath );

				// const bool bUseAlphaAsTransparency = HasActiveTransparency( mBitmapTex );

				if( /*bUseAlphaAsTransparency &&*/ _tcslen( tTexturepath ) > 0 )
				{
					return TextureHasAlpha( LPCTSTRToConstCharPtr( tTexturepath ) );
				}
			}
		}
		else if( mTexMap->ClassID() == GNORMAL_CLASS_ID )
		{
			TCHAR tTexturepath[ MAX_PATH ];
			BitmapTex* mBitmapTex = (BitmapTex*)mTexMap->GetSubTexmap( 0 );

			if( mBitmapTex )
			{
				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturepath );

				const bool bUseAlphaAsTransparency = HasActiveTransparency( mBitmapTex );

				if( bUseAlphaAsTransparency && _tcslen( tTexturepath ) > 0 )
				{
					return TextureHasAlpha( LPCTSTRToConstCharPtr( tTexturepath ) );
				}
			}
		}
		return false;
	}

	bool IsSRGB( BitmapTex* mBitmapTex )
	{
		// change sRGB based on gamma
		const float gamma = GetBitmapTextureGamma( mBitmapTex );
		if( gamma <= 2.21000000f && gamma >= 2.19000000f )
		{
			return true;
		}

		return false;
	}

	std::basic_string<TCHAR> GetMappingChannelAsString( Texmap* mTexMap )
	{
		// set mapping channel to 1
		std::basic_string<TCHAR> tMappingChannel = _T("1");

		// or explicit channels
		if( mTexMap->GetUVWSource() == UVWSRC_EXPLICIT )
		{
			const int maxChannel = mTexMap->GetMapChannel();
			TCHAR tMappingChannelBuffer[ 8 ] = { 0 };
			_stprintf_s( tMappingChannelBuffer, _T("%d"), maxChannel );

			tMappingChannel = tMappingChannelBuffer;
		}

		return tMappingChannel;
	}

	bool ConvertToSimplygonMaterial( spMaterial sgMaterial, TimeValue& time )
	{
		spString rMaterialName = sgMaterial->GetName();
		const char* cMaterialName = rMaterialName.c_str();
		std::basic_string<TCHAR> tMaterialName = ConstCharPtrToLPCTSTR( rMaterialName.c_str() );

		const long int maxChannelID = -1;

		// save stack memory, declare once
		TCHAR tTexturePath[ MAX_PATH ] = { 0 };

		// if there's an override, read in the properties of mapped nodes into the shading network
		ShadingNetworkProxy* materialProxy = MaxReference->GetProxyShadingNetworkMaterial( tMaterialName );
		if( materialProxy )
		{
			// initialize shading network
			MaxReference->InitializeNodesInNodeTable();
			MaxReference->SetupMaterialWithCustomShadingNetwork( sgMaterial, materialProxy );

			// for all channels in the override material
			for( std::map<std::basic_string<TCHAR>, int>::iterator& channelIterator = materialProxy->ShadingNodeToSGChannel.begin();
			     channelIterator != materialProxy->ShadingNodeToSGChannel.end();
			     channelIterator++ )
			{
				std::basic_string<TCHAR> tChannelName = channelIterator->first;
				const char* cChannelName = LPCTSTRToConstCharPtr( tChannelName.c_str() );
				{
					// fetch all texture nodes
					std::map<int, NodeProxy*> textureNodeProxies;
					MaxReference->GetSpShadingNodesFromTable( ShadingTextureNode, tChannelName, materialProxy, textureNodeProxies );

					// for each texture node, read original file path, mapping channel and sRGB
					for( std::map<int, NodeProxy*>::iterator& textureProxyIterator = textureNodeProxies.begin();
					     textureProxyIterator != textureNodeProxies.end();
					     textureProxyIterator++ )
					{
						const NodeProxy* nodeProxy = textureProxyIterator->second;
						spShadingTextureNode sgTextureNode = spShadingTextureNode::SafeCast( nodeProxy->ShadingExitNode );

						if( sgTextureNode.NonNull() )
						{
							spString rNodeName = sgTextureNode->GetName();
							const char* cNodeName = rNodeName.c_str();
							std::basic_string<TCHAR> tNodeName = ConstCharPtrToLPCTSTR( cNodeName );

							Texmap* mTexMap = this->GetMap( tNodeName.c_str() );
							if( !mTexMap )
								continue;

							BitmapTex* mBitmapTex = nullptr;
							Class_ID nodeClassId = mTexMap->ClassID();

							// see if there are nodes in between slot and texture
							if( nodeClassId == Class_ID( BMTEX_CLASS_ID, 0 ) )
							{
								mBitmapTex = (BitmapTex*)mTexMap;
							}
							else if( nodeClassId == GNORMAL_CLASS_ID )
							{
								// get first texmap of the normal node
								mTexMap = mTexMap->GetSubTexmap( 0 );

								if( mTexMap )
								{
									// check that the texmap is a bitmaptex,
									// if so, use it for this material channel
									Class_ID normalNodeClassId = mTexMap->ClassID();
									if( normalNodeClassId == Class_ID( BMTEX_CLASS_ID, 0 ) )
									{
										mBitmapTex = (BitmapTex*)mTexMap;
									}
									else
									{
										MaxReference->LogMaterialNodeMessage( mTexMap, tMaterialName, tChannelName );
									}
								}
							}
							else
							{
								MaxReference->LogMaterialNodeMessage( mTexMap, tMaterialName, tChannelName );
							}

							// if we have an actual texture node,
							// fetch gama, texture path, mapping channel,
							// and create representing shading network.
							if( mBitmapTex )
							{
								const bool bSRGB = IsSRGB( mBitmapTex );
								GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

								// if the texture path was found
								if( _tcslen( tTexturePath ) > 0 )
								{
									// fetch mapping channel
									std::basic_string<TCHAR> tMappingChannel = GetMappingChannelAsString( mTexMap );

									std::basic_string<TCHAR> tTexturePathOverride = _T("");
									if( tTexturePathOverride.length() > 0 )
									{
										_stprintf_s( tTexturePath, _T("%s"), tTexturePathOverride.c_str() );
									}

									// import texture
									std::basic_string<TCHAR> tTexturePathWithName = MaxReference->ImportTexture( std::basic_string<TCHAR>( tTexturePath ) );
									std::basic_string<TCHAR> tTextureName = GetTitleOfFile( tTexturePathWithName );
									std::basic_string<TCHAR> tTextureExtension = GetExtensionOfFile( tTexturePathWithName );
									std::basic_string<TCHAR> tTextureNameWithExtension = tTextureName + tTextureExtension;

									// update texture node properties
									MaterialNodes::PopulateTextureNode( sgTextureNode, mBitmapTex, tMappingChannel, tTextureName, time, bSRGB );

									// assign mapping channel if not already set
									if( !( !sgTextureNode->GetTexCoordName().IsNullOrEmpty() && !sgTextureNode->GetTexCoordName().c_str() == NULL ) )
									{
										sgTextureNode->SetTexCoordName( LPCTSTRToConstCharPtr( tMappingChannel.c_str() ) );
									}

									// shading network overrides
									if( nodeProxy->UVOverride != -1 )
									{
										TCHAR tMappingChannelBuffer[ 8 ] = { 0 };
										_stprintf( tMappingChannelBuffer, _T("%d"), nodeProxy->UVOverride );
										sgTextureNode->SetTexCoordName( LPCTSTRToConstCharPtr( tMappingChannelBuffer ) );
									}

									if( nodeProxy->uTilingOverride )
									{
										sgTextureNode->SetTileU( nodeProxy->uTiling );
									}
									if( nodeProxy->vTilingOverride )
									{
										sgTextureNode->SetTileV( nodeProxy->vTiling );
									}

									if( nodeProxy->uOffsetOverride )
									{
										sgTextureNode->SetOffsetU( nodeProxy->uOffset );
									}
									if( nodeProxy->vOffsetOverride )
									{
										sgTextureNode->SetOffsetV( -nodeProxy->vOffset );
									}

									if( nodeProxy->isSRGBOverride )
									{
										sgTextureNode->SetColorSpaceOverride( nodeProxy->isSRGB ? Simplygon::EImageColorSpace::sRGB
										                                                        : Simplygon::EImageColorSpace::Linear );
									}

									// create texture and add it to scene
									spTexture sgTexture;

									// do lookup in case this texture is already in use
									std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
									    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
									const bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

									if( bTextureInUse )
									{
										sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingFilePath(
										    LPCTSTRToConstCharPtr( tTexturePathWithName.c_str() ) );
									}
									else
									{
										sgTexture = sg->CreateTexture();
										sgTexture->SetName( LPCTSTRToConstCharPtr( tTextureName.c_str() ) );
										sgTexture->SetFilePath( LPCTSTRToConstCharPtr( tTexturePathWithName.c_str() ) );

										MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->AddTexture( sgTexture );
									}

									// if the texture was not already in scene, copy it to local work folder
									if( !bTextureInUse )
									{
										const spString rTexturePathWithName = sgTexture->GetFilePath();
										MaxReference->LoadedTexturePathToID.insert( std::pair<std::basic_string<TCHAR>, std::basic_string<TCHAR>>(
										    tTexturePathWithName, ConstCharPtrToLPCTSTR( rTexturePathWithName.c_str() ) ) );
									}
								}
							}
						}
					}
				}
				{
					// fetch all color nodes
					std::map<int, NodeProxy*> colorNodeProxies;
					MaxReference->GetSpShadingNodesFromTable( ShadingColorNode, tChannelName, materialProxy, colorNodeProxies );

					// for each color node, read original color
					for( std::map<int, NodeProxy*>::iterator& colorProxyIterator = colorNodeProxies.begin(); colorProxyIterator != colorNodeProxies.end();
					     colorProxyIterator++ )
					{
						const NodeProxy* nodeProxy = colorProxyIterator->second;
						spShadingColorNode sgColorNode = spShadingColorNode::SafeCast( nodeProxy->ShadingExitNode );

						if( sgColorNode.NonNull() )
						{
							spString rNodeName = sgColorNode->GetName();
							const char* cNodeName = rNodeName.c_str();
							std::basic_string<TCHAR> tNodeName = ConstCharPtrToLPCTSTR( cNodeName );

							// try to fetch color values
							Point4* mColor = this->GetColor4( tNodeName.c_str() );
							if( mColor )
							{
								sgColorNode->SetDefaultParameter( 0, mColor->x, mColor->y, mColor->z, mColor->w );
							}

							// if no color, see if there's a float value
							else
							{
								bool bHasFloatValue = false;
								float mFloatValue = this->GetFloat( tNodeName.c_str(), bHasFloatValue );

								if( bHasFloatValue )
								{
									sgColorNode->SetDefaultParameter( 0, mFloatValue, mFloatValue, mFloatValue, 1.0f );
								}
							}
						}
					}
				}
			}
		}

		// continue with static material pipeline,
		// if channel exists, ignore

		// Base Color
		if( !sgMaterial->HasMaterialChannel( "base_weight" ) )
		{
			const char* cChannelName = "base_weight";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			float mBaseWeight = this->GetFloat( _T("base_weight") );
			Texmap* mBaseWeightMap = this->GetMap( _T("base_weight_map") );
			bool mBaseWeightMapOn = this->GetBool( _T("base_weight_map_on") );

			if( HasValidTexMap( mBaseWeightMap, mBaseWeightMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mBaseWeightMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mBaseWeightMap && mBaseWeightMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mBaseWeightMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = CreateColorShadingNetwork( mBaseWeight, mBaseWeight, mBaseWeight, 1.0f );

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
		}
		if( !sgMaterial->HasMaterialChannel( "base_color" ) )
		{
			const char* cChannelName = "base_color";
			std::basic_string<TCHAR> tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			Point4* mBaseColor = this->GetColor4( _T("base_color") );
			Texmap* mBaseColorMap = this->GetMap( _T("base_color_map") );
			bool mBaseColorMapOn = this->GetBool( _T("base_color_map_on") );

			if( HasValidTexMap( mBaseColorMap, mBaseColorMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mBaseColorMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else if( mBaseColor )
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mBaseColorMap && mBaseColorMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mBaseColorMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = CreateColorShadingNetwork( mBaseColor->x, mBaseColor->y, mBaseColor->z, mBaseColor->w );

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mBaseColorMap && mBaseColorMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mBaseColorMap, tMaterialName, tChannelName );
				}
			}
		}

		// Reflections
		if( !sgMaterial->HasMaterialChannel( "reflectivity" ) )
		{
			const char* cChannelName = "reflectivity";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			float mReflectivity = this->GetFloat( _T("reflectivity") );
			Texmap* mReflectivityMap = this->GetMap( _T("reflectivity_map") );
			bool mReflectivityMapOn = this->GetBool( _T("reflectivity_map_on") );

			if( HasValidTexMap( mReflectivityMap, mReflectivityMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mReflectivityMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mReflectivityMap && mReflectivityMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mReflectivityMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = CreateColorShadingNetwork( mReflectivity, mReflectivity, mReflectivity, 1.0f );

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
		}
		if( !sgMaterial->HasMaterialChannel( "refl_color" ) )
		{
			const char* cChannelName = "refl_color";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			Point4* mReflColor = this->GetColor4( _T("refl_color") );
			Texmap* mReflColorMap = this->GetMap( _T("refl_color_map") );
			bool mReflColorMapOn = this->GetBool( _T("refl_color_map_on") );

			if( HasValidTexMap( mReflColorMap, mReflColorMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mReflColorMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else if( mReflColor )
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mReflColorMap && mReflColorMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mReflColorMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = CreateColorShadingNetwork( mReflColor->x, mReflColor->y, mReflColor->z, mReflColor->w );

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mReflColorMap && mReflColorMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mReflColorMap, tMaterialName, tChannelName );
				}
			}
		}
		if( !sgMaterial->HasMaterialChannel( "roughness" ) )
		{
			const char* cChannelName = "roughness";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			Texmap* mRougnessMap = this->GetMap( _T("roughness_map") );
			float mRoughness = this->GetFloat( _T("roughness") );
			bool mRoughnessMapOn = this->GetBool( _T("roughness_map_on") );
			bool mRoughnessInv = this->GetBool( _T("roughness_inv") );

			if( HasValidTexMap( mRougnessMap, mRoughnessMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mRougnessMap, maxChannelID, cMaterialName, cChannelName );

				spShadingNode sgExitNode;
				if( mRoughnessInv )
				{
					spShadingColorNode sgNegativeNode = sg->CreateShadingColorNode();
					sgNegativeNode->SetColor( -1.0f, -1.0f, -1.0f, 1.0f );

					spShadingColorNode sgPositiveNode = sg->CreateShadingColorNode();
					sgPositiveNode->SetColor( 1.0f, 1.0f, 1.0f, 1.0f );

					spShadingMultiplyNode sgMultiplyNode = sg->CreateShadingMultiplyNode();
					sgMultiplyNode->SetInput( 0, sgNegativeNode );
					sgMultiplyNode->SetInput( 1, sgShadingNode );

					spShadingAddNode sgAddNode = sg->CreateShadingAddNode();
					sgAddNode->SetInput( 0, sgMultiplyNode );
					sgAddNode->SetInput( 1, sgPositiveNode );

					sgExitNode = (spShadingNode)sgAddNode;
				}
				else
				{
					sgExitNode = (spShadingNode)sgShadingNode;
				}

				sgMaterial->SetShadingNetwork( cChannelName, sgExitNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mRougnessMap && mRoughnessMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mRougnessMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode;
				if( mRoughnessInv )
				{
					sgColorNode = CreateColorShadingNetwork( 1.0f - mRoughness, 1.0f - mRoughness, 1.0f - mRoughness, 1.0f );
				}
				else
				{
					sgColorNode = CreateColorShadingNetwork( mRoughness, mRoughness, mRoughness, 1.0f );
				}

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
		}
		if( !sgMaterial->HasMaterialChannel( "diff_rough" ) )
		{
			const char* cChannelName = "diff_rough";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			Texmap* mDiffRoughMap = this->GetMap( _T("diff_rough_map") );
			float mDiffRoughness = this->GetFloat( _T("diff_roughness") );
			bool mDiffRoughMapOn = this->GetBool( _T("diff_rough_map_on") );

			if( HasValidTexMap( mDiffRoughMap, mDiffRoughMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mDiffRoughMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mDiffRoughMap && mDiffRoughMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mDiffRoughMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = CreateColorShadingNetwork( mDiffRoughness, mDiffRoughness, mDiffRoughness, 1.0f );

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
		}
		if( !sgMaterial->HasMaterialChannel( "metalness" ) )
		{
			const char* cChannelName = "metalness";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			Texmap* mMetalnessMap = this->GetMap( _T("metalness_map") );
			float mMetalness = this->GetFloat( _T("metalness") );
			bool mMetalnessMapOn = this->GetBool( _T("metalness_map_on") );

			if( HasValidTexMap( mMetalnessMap, mMetalnessMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mMetalnessMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mMetalnessMap && mMetalnessMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mMetalnessMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = CreateColorShadingNetwork( mMetalness, mMetalness, mMetalness, 1.0f );

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
		}
		if( !sgMaterial->HasMaterialChannel( "trans_ior" ) )
		{
			const char* cChannelName = "trans_ior";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			Texmap* mTransIORMap = this->GetMap( _T("trans_ior_map") );
			float mTransIOR = this->GetFloat( _T("trans_ior") );
			bool mTransIORMapOn = this->GetBool( _T("trans_ior_map_on") );

			if( HasValidTexMap( mTransIORMap, mTransIORMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mTransIORMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mTransIORMap && mTransIORMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mTransIORMap, tMaterialName, tChannelName );
				}

				// fit IOR of 0 - 50 into 0 - 1
				float divisor = 50.0f;
				float mCorrectedIOR = _CLAMP( mTransIOR / divisor, 0.0f, 1.0f );
				spShadingColorNode sgScaleColorNode = CreateColorShadingNetwork( mCorrectedIOR, mCorrectedIOR, mCorrectedIOR, 1.0f );

				sgMaterial->SetShadingNetwork( cChannelName, sgScaleColorNode );
			}
		}

		// Transparency
		if( !sgMaterial->HasMaterialChannel( "transparency" ) )
		{
			const char* cChannelName = "transparency";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			float mTransparency = this->GetFloat( _T("transparency") );
			Texmap* mTransparencyMap = this->GetMap( _T("transparency_map") );
			bool mTransparencyMapOn = this->GetBool( _T("transparency_map_on") );

			if( HasValidTexMap( mTransparencyMap, mTransparencyMapOn ) )
			{
				// hijack ID_OP flag for use in transparency
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mTransparencyMap, ID_OP, cMaterialName, cChannelName );

				bool bTextureHasAlpha = true;
				bool bHasActiveTransparency = false;
				int alphaSource = ALPHA_FILE;

				// bitmap check
				if( mTransparencyMap->ClassID() == Class_ID( BMTEX_CLASS_ID, 0 ) )
				{
					BitmapTex* bitmapTex = (BitmapTex*)mTransparencyMap;
					bTextureHasAlpha = TexmapHasAlphaAsTransparency( mTransparencyMap );
					bHasActiveTransparency = HasActiveTransparency( bitmapTex );
					alphaSource = bitmapTex->GetAlphaSource();
				}

				if( bHasActiveTransparency == FALSE || alphaSource == ALPHA_RGB )
				{
					spShadingSwizzlingNode sgRedSwizzleNode = sg->CreateShadingSwizzlingNode();
					sgRedSwizzleNode->SetInput( 0, sgShadingNode );
					sgRedSwizzleNode->SetInput( 1, sgShadingNode );
					sgRedSwizzleNode->SetInput( 2, sgShadingNode );
					sgRedSwizzleNode->SetInput( 3, sgShadingNode );

					sgRedSwizzleNode->SetRedComponent( 0 );
					sgRedSwizzleNode->SetGreenComponent( 0 );
					sgRedSwizzleNode->SetBlueComponent( 0 );
					sgRedSwizzleNode->SetAlphaComponent( 0 );

					spShadingSwizzlingNode sgGreenSwizzleNode = sg->CreateShadingSwizzlingNode();
					sgGreenSwizzleNode->SetInput( 0, sgShadingNode );
					sgGreenSwizzleNode->SetInput( 1, sgShadingNode );
					sgGreenSwizzleNode->SetInput( 2, sgShadingNode );
					sgGreenSwizzleNode->SetInput( 3, sgShadingNode );

					sgGreenSwizzleNode->SetRedComponent( 1 );
					sgGreenSwizzleNode->SetGreenComponent( 1 );
					sgGreenSwizzleNode->SetBlueComponent( 1 );
					sgGreenSwizzleNode->SetAlphaComponent( 1 );

					spShadingSwizzlingNode sgBlueSwizzleNode = sg->CreateShadingSwizzlingNode();
					sgBlueSwizzleNode->SetInput( 0, sgShadingNode );
					sgBlueSwizzleNode->SetInput( 1, sgShadingNode );
					sgBlueSwizzleNode->SetInput( 2, sgShadingNode );
					sgBlueSwizzleNode->SetInput( 3, sgShadingNode );

					sgBlueSwizzleNode->SetRedComponent( 2 );
					sgBlueSwizzleNode->SetGreenComponent( 2 );
					sgBlueSwizzleNode->SetBlueComponent( 2 );
					sgBlueSwizzleNode->SetAlphaComponent( 2 );

					spShadingAddNode sgAddRGNode = sg->CreateShadingAddNode();
					sgAddRGNode->SetInput( 0, sgRedSwizzleNode );
					sgAddRGNode->SetInput( 1, sgGreenSwizzleNode );

					spShadingAddNode sgAddRGBNode = sg->CreateShadingAddNode();
					sgAddRGBNode->SetInput( 0, sgAddRGNode );
					sgAddRGBNode->SetInput( 1, sgBlueSwizzleNode );

					spShadingColorNode sg1Through3Node = sg->CreateShadingColorNode();
					sg1Through3Node->SetDefaultParameter( 0, 3, 3, 3, 3 );

					spShadingDivideNode sgDivideNode = sg->CreateShadingDivideNode();
					sgDivideNode->SetInput( 0, sgAddRGBNode );
					sgDivideNode->SetInput( 1, sg1Through3Node );

					spShadingSwizzlingNode sgFinalSwizzleNode = sg->CreateShadingSwizzlingNode();
					sgFinalSwizzleNode->SetInput( 0, sgShadingNode );
					sgFinalSwizzleNode->SetInput( 1, sgShadingNode );
					sgFinalSwizzleNode->SetInput( 2, sgShadingNode );
					sgFinalSwizzleNode->SetInput( 3, sgDivideNode );

					sgFinalSwizzleNode->SetRedComponent( 0 );
					sgFinalSwizzleNode->SetGreenComponent( 1 );
					sgFinalSwizzleNode->SetBlueComponent( 2 );
					sgFinalSwizzleNode->SetAlphaComponent( 3 );

					sgShadingNode = sgFinalSwizzleNode;
				}
				else if( alphaSource == ALPHA_NONE )
				{
					spShadingColorNode sgOneNode = sg->CreateShadingColorNode();
					sgOneNode->SetColor( 1, 1, 1, 1 );

					sgShadingNode = sgOneNode;
				}

				spShadingSwizzlingNode sgSwizzleNode = sg->CreateShadingSwizzlingNode();
				sgSwizzleNode->SetInput( 0, sgShadingNode );
				sgSwizzleNode->SetInput( 1, sgShadingNode );
				sgSwizzleNode->SetInput( 2, sgShadingNode );
				sgSwizzleNode->SetInput( 3, sgShadingNode );
				if( bTextureHasAlpha )
				{
					sgSwizzleNode->SetRedComponent( 3 );
					sgSwizzleNode->SetGreenComponent( 3 );
					sgSwizzleNode->SetBlueComponent( 3 );
					sgSwizzleNode->SetAlphaComponent( 3 );
				}
				else
				{
					sgSwizzleNode->SetRedComponent( 0 );
					sgSwizzleNode->SetGreenComponent( 0 );
					sgSwizzleNode->SetBlueComponent( 0 );
					sgSwizzleNode->SetAlphaComponent( 0 );
				}
				sgMaterial->SetShadingNetwork( cChannelName, sgSwizzleNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mTransparencyMap && mTransparencyMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mTransparencyMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgScaleColorNode = CreateColorShadingNetwork( mTransparency, mTransparency, mTransparency, 1.0f );

				sgMaterial->SetShadingNetwork( cChannelName, sgScaleColorNode );
			}
		}
		if( !sgMaterial->HasMaterialChannel( "trans_color" ) )
		{
			const char* cChannelName = "trans_color";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			Point4* mTransColor = this->GetColor4( _T("trans_color") );
			Texmap* mTransColorMap = this->GetMap( _T("trans_color_map") );
			bool mTransColorMapOn = this->GetBool( _T("trans_color_map_on") );

			if( HasValidTexMap( mTransColorMap, mTransColorMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mTransColorMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else if( mTransColor )
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mTransColorMap && mTransColorMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mTransColorMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgScaleColorNode = CreateColorShadingNetwork( mTransColor->x, mTransColor->y, mTransColor->z, mTransColor->w );

				sgMaterial->SetShadingNetwork( cChannelName, sgScaleColorNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mTransColorMap && mTransColorMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mTransColorMap, tMaterialName, tChannelName );
				}
			}
		}
		if( !sgMaterial->HasMaterialChannel( "trans_depth" ) )
		{
			const char* cChannelName = "trans_depth";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			float mTransDepth = this->GetFloat( _T("trans_depth") );

			// fit depth of 0 - 1000 into 0 - 1
			float convertedDepth = _CLAMP( mTransDepth / 1000.f, 0.0f, 1.0f );
			spShadingColorNode sgScaleColorNode = CreateColorShadingNetwork( convertedDepth, convertedDepth, convertedDepth, 1.0f );

			sgMaterial->SetShadingNetwork( cChannelName, sgScaleColorNode );
		}
		if( !sgMaterial->HasMaterialChannel( "trans_rough" ) )
		{
			const char* cChannelName = "trans_rough";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			float mTransRoughness = 0.0f;
			Texmap* mTransRoughMap = nullptr;
			bool mTransRoughMapOn = false;

			bool mTransRoughnessInv = false;
			bool mTransRoughnessLock = this->GetBool( _T("trans_roughness_lock") );

			if( mTransRoughnessLock )
			{
				mTransRoughness = this->GetFloat( _T("roughness") );
				mTransRoughMap = this->GetMap( _T("roughness_map") );
				mTransRoughMapOn = this->GetBool( _T("roughness_map_on") );
				mTransRoughnessInv = this->GetBool( _T("roughness_inv") );
			}
			else
			{
				mTransRoughness = this->GetFloat( _T("trans_roughness") );
				mTransRoughMap = this->GetMap( _T("trans_rough_map") );
				mTransRoughMapOn = this->GetBool( _T("trans_rough_map_on") );
				mTransRoughnessInv = this->GetBool( _T("trans_roughness_inv") );
			}

			if( HasValidTexMap( mTransRoughMap, mTransRoughMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mTransRoughMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mTransRoughMap && mTransRoughMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mTransRoughMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode;
				if( mTransRoughnessInv )
				{
					sgColorNode = CreateColorShadingNetwork( 1.0f - mTransRoughness, 1.0f - mTransRoughness, 1.0f - mTransRoughness, 1.0f );
				}
				else
				{
					sgColorNode = CreateColorShadingNetwork( mTransRoughness, mTransRoughness, mTransRoughness, 1.0f );
				}

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
		}

		// Sub-Surface Scattering
		if( !sgMaterial->HasMaterialChannel( "sss_scatter" ) )
		{
			const char* cChannelName = "sss_scatter";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			Texmap* mScatteringMap = this->GetMap( _T("scattering_map") );
			float mScattering = this->GetFloat( _T("scattering") );
			bool mScatteringMapOn = this->GetBool( _T("scattering_map_on") );

			if( HasValidTexMap( mScatteringMap, mScatteringMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mScatteringMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mScatteringMap && mScatteringMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mScatteringMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = CreateColorShadingNetwork( mScattering, mScattering, mScattering, 1.0f );

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
		}
		if( !sgMaterial->HasMaterialChannel( "sss_color" ) )
		{
			const char* cChannelName = "sss_color";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			Point4* mSSSColor = this->GetColor4( _T("sss_color") );
			Texmap* mSSSColorMap = this->GetMap( _T("sss_color_map") );
			bool mSSSColorMapOn = this->GetBool( _T("sss_color_map_on") );

			if( HasValidTexMap( mSSSColorMap, mSSSColorMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mSSSColorMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else if( mSSSColor )
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mSSSColorMap && mSSSColorMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mSSSColorMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = CreateColorShadingNetwork( mSSSColor->x, mSSSColor->y, mSSSColor->z, mSSSColor->w );

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mSSSColorMap && mSSSColorMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mSSSColorMap, tMaterialName, tChannelName );
				}
			}
		}
		if( !sgMaterial->HasMaterialChannel( "sss_scatter_color" ) )
		{
			const char* cChannelName = "sss_scatter_color";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			Point4* mSSSScatterColor = this->GetColor4( _T("sss_scatter_color") );

			if( mSSSScatterColor )
			{
				this->CreateMaterialChannel( sgMaterial, tChannelName );

				spShadingColorNode sgColorNode =
				    CreateColorShadingNetwork( mSSSScatterColor->x, mSSSScatterColor->y, mSSSScatterColor->z, mSSSScatterColor->w );

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
		}
		if( !sgMaterial->HasMaterialChannel( "sss_scale" ) )
		{
			const char* cChannelName = "sss_scale";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			Texmap* mSSSScaleMap = this->GetMap( _T("sss_scale_map") );
			float mSSSDepth = this->GetFloat( _T("sss_depth") );
			float mSSSScale = this->GetFloat( _T("sss_scale") );
			bool mSSSScaleMapOn = this->GetBool( _T("sss_scale_map_on") );

			if( HasValidTexMap( mSSSScaleMap, mSSSScaleMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mSSSScaleMap, maxChannelID, cMaterialName, cChannelName );

				// not sure if combining is preferred?!
				spShadingColorNode sgScaleColorNode = sg->CreateShadingColorNode();

				// fit depth of 0 - 1000 into 0.0 - 1.0, truncate everything larger than 1000
				// combine depth and scale into same texture
				float correctedDepth = _CLAMP( mSSSDepth / 1000.0f, 0.0f, 1.0f );
				float combinedScaleAndDepth = mSSSScale * correctedDepth;

				sgScaleColorNode->SetColor( combinedScaleAndDepth, combinedScaleAndDepth, combinedScaleAndDepth, 1.0f );

				spShadingMultiplyNode sgMultiplyNode = sg->CreateShadingMultiplyNode();
				sgMultiplyNode->SetInput( 0, sgScaleColorNode );
				sgMultiplyNode->SetInput( 1, sgShadingNode );

				sgMaterial->SetShadingNetwork( cChannelName, sgMultiplyNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mSSSScaleMap && mSSSScaleMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mSSSScaleMap, tMaterialName, tChannelName );
				}

				// fit depth of 0 - 1000 into 0.0 - 1.0, truncate everything larger than 1000
				// combine depth and scale into same texture
				float correctedDepth = _CLAMP( mSSSDepth / 1000.0f, 0.0f, 1.0f );
				float combinedScaleAndDepth = mSSSScale * correctedDepth;

				spShadingColorNode sgScaleColorNode = CreateColorShadingNetwork( combinedScaleAndDepth, combinedScaleAndDepth, combinedScaleAndDepth, 1.0f );

				sgMaterial->SetShadingNetwork( cChannelName, sgScaleColorNode );
			}
		}

		// Emission
		if( !sgMaterial->HasMaterialChannel( "emission" ) )
		{
			const char* cChannelName = "emission";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			float mEmission = this->GetFloat( _T("emission") );
			Texmap* mEmissionMap = this->GetMap( _T("emission_map") );
			bool mEmissionMapOn = this->GetBool( _T("emission_map_on") );

			if( HasValidTexMap( mEmissionMap, mEmissionMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mEmissionMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mEmissionMap && mEmissionMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mEmissionMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = CreateColorShadingNetwork( mEmission, mEmission, mEmission, 1.0f );

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
		}
		if( !sgMaterial->HasMaterialChannel( "emit_color" ) )
		{
			const char* cChannelName = "emit_color";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			Point4* mEmitColor = this->GetColor4( _T("emit_color") );
			Texmap* mEmitColorMap = this->GetMap( _T("emit_color_map") );
			bool mEmitColorMapOn = this->GetBool( _T("emit_color_map_on") );

			if( HasValidTexMap( mEmitColorMap, mEmitColorMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mEmitColorMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else if( mEmitColor )
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mEmitColorMap && mEmitColorMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mEmitColorMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = CreateColorShadingNetwork( mEmitColor->x, mEmitColor->y, mEmitColor->z, mEmitColor->w );

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
		}
		if( !sgMaterial->HasMaterialChannel( "emit_luminance" ) )
		{
			const char* cChannelName = "emit_luminance";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			float divisor = 10000;
			float mEmitLuminance = this->GetFloat( _T("emit_luminance") );

			// fit 0 - 10000 into 0.0 - 1.0, truncate everything larger than 10000
			float correctedLuminance = _CLAMP( mEmitLuminance / divisor, 0.0f, 1.0f );

			spShadingColorNode sgColorNode = CreateColorShadingNetwork( mEmitLuminance, mEmitLuminance, mEmitLuminance, 1.0f );

			sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
		}
		if( !sgMaterial->HasMaterialChannel( "emit_kelvin" ) )
		{
			const char* cChannelName = "emit_kelvin";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			float divisor = 1.78516805f;
			float mEmitKelvin = this->GetFloat( _T("emit_kelvin") );

			// Kelvin (0 - 20000 to RGB)
			Color mKelvinAsColor = Color::FromKelvinTemperature( mEmitKelvin );

			// fit kelvin RGB of 0.0 - 1.78516805f into 0.0 - 1.0
			// correct RGB values of maximum 1.78516805f using divisor
			spShadingColorNode sgColorNode =
			    CreateColorShadingNetwork( mKelvinAsColor.r / divisor, mKelvinAsColor.g / divisor, mKelvinAsColor.b / divisor, 1.0f );

			sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
		}

		// Special Maps
		if( !sgMaterial->HasMaterialChannel( "bump" ) )
		{
			const char* cChannelName = "bump";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			Texmap* mBumpMap = this->GetMap( _T("bump_map") );
			float mBumpMapAmt = this->GetFloat( _T("bump_map_amt") );
			bool mBumpMapOn = this->GetBool( _T("bump_map_on") );

			if( HasValidTexMap( mBumpMap, mBumpMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mBumpMap, ID_BU, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mBumpMap && mBumpMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mBumpMap, tMaterialName, tChannelName );
				}
			}
		}
		if( !sgMaterial->HasMaterialChannel( "coat_bump" ) )
		{
			const char* cChannelName = "coat_bump";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			Texmap* mCoatBumpMap = this->GetMap( _T("coat_bump_map") );
			float mClearCoatBumpMapAmt = true ? 1.0f : this->GetFloat( _T("clearcoat_bump_map_amt") );
			bool mCoatBumpMapOn = this->GetBool( _T("coat_bump_map_on") );

			if( HasValidTexMap( mCoatBumpMap, mCoatBumpMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mCoatBumpMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mCoatBumpMap && mCoatBumpMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mCoatBumpMap, tMaterialName, tChannelName );
				}
			}
		}
		if( !sgMaterial->HasMaterialChannel( "displacement" ) )
		{
			const char* cChannelName = "displacement";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			float mDisplacementMapAmt = this->GetFloat( _T("displacement_map_amt") );
			Texmap* mDisplacementMap = this->GetMap( _T("displacement_map") );
			bool mDisplacementMapOn = this->GetBool( _T("displacement_map_on") );

			if( HasValidTexMap( mDisplacementMap, mDisplacementMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mDisplacementMap, maxChannelID, cMaterialName, cChannelName );

				spShadingColorNode sgColorNode = sg->CreateShadingColorNode();
				sgColorNode->SetColor( mDisplacementMapAmt, mDisplacementMapAmt, mDisplacementMapAmt, 1.0f );

				spShadingMultiplyNode sgMultiplyNode = sg->CreateShadingMultiplyNode();
				sgMultiplyNode->SetInput( 0, sgColorNode );
				sgMultiplyNode->SetInput( 1, sgShadingNode );

				sgMaterial->SetShadingNetwork( cChannelName, sgMultiplyNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mDisplacementMap && mDisplacementMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mDisplacementMap, tMaterialName, tChannelName );
				}
			}
		}
		if( !sgMaterial->HasMaterialChannel( "cutout" ) )
		{
			const char* cChannelName = "cutout";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			Texmap* mCutoutMap = this->GetMap( _T("cutout_map") );
			bool mCutoutMapOn = this->GetBool( _T("cutout_map_on") );

			if( HasValidTexMap( mCutoutMap, mCutoutMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mCutoutMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mCutoutMap && mCutoutMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mCutoutMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = CreateColorShadingNetwork( 1.f, 1.f, 1.f, 1.f );

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
		}

		// Clearcoat
		if( !sgMaterial->HasMaterialChannel( "coating" ) )
		{
			const char* cChannelName = "coating";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			float mCoating = this->GetFloat( _T("coating") );
			Texmap* mCoatMap = this->GetMap( _T("coat_map") );
			bool mCoatMapOn = this->GetBool( _T("coat_map_on") );

			if( HasValidTexMap( mCoatMap, mCoatMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mCoatMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mCoatMap && mCoatMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mCoatMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = CreateColorShadingNetwork( mCoating, mCoating, mCoating, 1.0f );

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
		}
		if( !sgMaterial->HasMaterialChannel( "coat_color" ) )
		{
			const char* cChannelName = "coat_color";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			Point4* mCoatColor = this->GetColor4( _T("coat_color") );
			Texmap* mCoatColorMap = this->GetMap( _T("coat_color_map") );
			bool mCoatColorMapOn = this->GetBool( _T("coat_color_map_on") );

			if( HasValidTexMap( mCoatColorMap, mCoatColorMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mCoatColorMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else if( mCoatColor )
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mCoatColorMap && mCoatColorMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mCoatColorMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = CreateColorShadingNetwork( mCoatColor->x, mCoatColor->y, mCoatColor->z, mCoatColor->w );

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mCoatColorMap && mCoatColorMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mCoatColorMap, tMaterialName, tChannelName );
				}
			}
		}
		if( !sgMaterial->HasMaterialChannel( "coat_roughness" ) )
		{
			const char* cChannelName = "coat_roughness";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			float mCoatRoughness = this->GetFloat( _T("coat_roughness") );
			Texmap* mCoatRoughMap = this->GetMap( _T("coat_rough_map") );
			bool mCoatRoughMapOn = this->GetBool( _T("coat_rough_map_on") );
			bool mCoatRoughnessInv = this->GetBool( _T("coat_roughness_inv") );

			if( HasValidTexMap( mCoatRoughMap, mCoatRoughMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mCoatRoughMap, maxChannelID, cMaterialName, cChannelName );

				spShadingNode sgExitNode;
				if( mCoatRoughnessInv )
				{
					spShadingColorNode sgNegativeNode = sg->CreateShadingColorNode();
					sgNegativeNode->SetColor( -1.0f, -1.0f, -1.0f, 1.0f );

					spShadingColorNode sgPositiveNode = sg->CreateShadingColorNode();
					sgPositiveNode->SetColor( 1.0f, 1.0f, 1.0f, 1.0f );

					spShadingMultiplyNode sgMultiplyNode = sg->CreateShadingMultiplyNode();
					sgMultiplyNode->SetInput( 0, sgNegativeNode );
					sgMultiplyNode->SetInput( 1, sgShadingNode );

					spShadingAddNode sgAddNode = sg->CreateShadingAddNode();
					sgAddNode->SetInput( 0, sgMultiplyNode );
					sgAddNode->SetInput( 1, sgPositiveNode );

					sgExitNode = (spShadingNode)sgAddNode;
				}
				else
				{
					sgExitNode = (spShadingNode)sgShadingNode;
				}

				sgMaterial->SetShadingNetwork( cChannelName, sgExitNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mCoatRoughMap && mCoatRoughMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mCoatRoughMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode;

				if( mCoatRoughnessInv )
				{
					sgColorNode = CreateColorShadingNetwork( 1.0f - mCoatRoughness, 1.0f - mCoatRoughness, 1.0f - mCoatRoughness, 1.0f );
				}
				else
				{
					sgColorNode = CreateColorShadingNetwork( mCoatRoughness, mCoatRoughness, mCoatRoughness, 1.0f );
				}

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
		}
		if( !sgMaterial->HasMaterialChannel( "coat_ior" ) )
		{
			const char* cChannelName = "coat_ior";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			float divisor = 5.0f;
			float mCoatIOR = this->GetFloat( _T("coat_ior") );

			// fit IOR of value 0 - 5 into 0 - 1
			float correctedCoatIOR = _CLAMP( mCoatIOR / divisor, 0.0f, 1.0f );

			spShadingColorNode sgColorNode = CreateColorShadingNetwork( correctedCoatIOR, correctedCoatIOR, correctedCoatIOR, 1.0f );

			sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
		}

		// new to max 2023
#if MAX_VERSION_MAJOR >= 25

		// Clearcoat
		if( !sgMaterial->HasMaterialChannel( "coat_affect_color" ) )
		{
			const char* cChannelName = "coat_affect_color";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			float mCoatAffectColor = this->GetFloat( _T("coat_affect_color") );

			spShadingColorNode sgColorNode = CreateColorShadingNetwork( mCoatAffectColor, mCoatAffectColor, mCoatAffectColor, 1.0f );

			sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
		}
		if( !sgMaterial->HasMaterialChannel( "coat_affect_roughness" ) )
		{
			const char* cChannelName = "coat_affect_roughness";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			float mCoatAffectRoughness = this->GetFloat( _T("coat_affect_roughness") );

			spShadingColorNode sgColorNode = CreateColorShadingNetwork( mCoatAffectRoughness, mCoatAffectRoughness, mCoatAffectRoughness, 1.0f );

			sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
		}

		// Sheen Maps
		if( !sgMaterial->HasMaterialChannel( "sheen" ) )
		{
			const char* cChannelName = "sheen";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			float mSheenWeight = this->GetFloat( _T("sheen") );
			Texmap* mSheenWeightMap = this->GetMap( _T("sheen_map") );
			bool mSheenWeightMapOn = this->GetBool( _T("sheen_map_on") );

			if( HasValidTexMap( mSheenWeightMap, mSheenWeightMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mSheenWeightMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mSheenWeightMap && mSheenWeightMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mSheenWeightMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = CreateColorShadingNetwork( mSheenWeight, mSheenWeight, mSheenWeight, 1.0f );

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
		}
		if( !sgMaterial->HasMaterialChannel( "sheen_color" ) )
		{
			const char* cChannelName = "sheen_color";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			Point4* mSheenColor = this->GetColor4( _T("sheen_color") );
			Texmap* mSheenColorMap = this->GetMap( _T("sheen_color_map") );
			bool mSheenColorMapOn = this->GetBool( _T("sheen_color_map_on") );

			if( HasValidTexMap( mSheenColorMap, mSheenColorMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mSheenColorMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mSheenColorMap && mSheenColorMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mSheenColorMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = CreateColorShadingNetwork( mSheenColor->x, mSheenColor->y, mSheenColor->z, 1.0f );

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
		}
		if( !sgMaterial->HasMaterialChannel( "sheen_roughness" ) )
		{
			const char* cChannelName = "sheen_roughness";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			float mSheenRoughness = this->GetFloat( _T("sheen_roughness") );
			Texmap* mSheenRoughMap = this->GetMap( _T("sheen_rough_map") );
			bool mSheenRoughMapOn = this->GetBool( _T("sheen_rough_map_on") );
			bool mSheenRoughnessInv = this->GetBool( _T("sheen_roughness_inv") );

			if( HasValidTexMap( mSheenRoughMap, mSheenRoughMapOn ) )
			{
				MaterialNodes::TextureSettingsOverride textureSettingsOverride = MaterialNodes::TextureSettingsOverride();
				textureSettingsOverride.mEnabledAlphaSourceOverride = true;
				textureSettingsOverride.mAlphaSource = ALPHA_FILE;

				spShadingNode sgShadingNode =
				    MaxReference->CreateSgMaterialPBRChannel( mSheenRoughMap, maxChannelID, cMaterialName, cChannelName, &textureSettingsOverride );

				spShadingNode sgExitNode;
				if( mSheenRoughnessInv )
				{
					spShadingColorNode sgNegativeNode = sg->CreateShadingColorNode();
					sgNegativeNode->SetColor( -1.0f, -1.0f, -1.0f, 1.0f );

					spShadingColorNode sgPositiveNode = sg->CreateShadingColorNode();
					sgPositiveNode->SetColor( 1.0f, 1.0f, 1.0f, 1.0f );

					spShadingMultiplyNode sgMultiplyNode = sg->CreateShadingMultiplyNode();
					sgMultiplyNode->SetInput( 0, sgNegativeNode );
					sgMultiplyNode->SetInput( 1, sgShadingNode );

					spShadingAddNode sgAddNode = sg->CreateShadingAddNode();
					sgAddNode->SetInput( 0, sgMultiplyNode );
					sgAddNode->SetInput( 1, sgPositiveNode );

					sgExitNode = (spShadingNode)sgAddNode;
				}
				else
				{
					sgExitNode = sgShadingNode;
				}

				sgMaterial->SetShadingNetwork( cChannelName, sgExitNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mSheenRoughMap && mSheenRoughMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mSheenRoughMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode;

				if( mSheenRoughnessInv )
				{
					sgColorNode = CreateColorShadingNetwork( 1.0f - mSheenRoughness, 1.0f - mSheenRoughness, 1.0f - mSheenRoughness, 1.0f );
				}
				else
				{
					sgColorNode = CreateColorShadingNetwork( mSheenRoughness, mSheenRoughness, mSheenRoughness, 1.0f );
				}

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
		}

		// Thin Film
		if( !sgMaterial->HasMaterialChannel( "thin_film" ) )
		{
			const char* cChannelName = "thin_film";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			float mThinFilmWeight = this->GetFloat( _T("thin_film") );
			float mThinFilmThickness = this->GetFloat( _T("thin_film_thickness") );
			Texmap* mThinFilmWeightMap = this->GetMap( _T("thin_film_map") );
			bool mThinFilmWeightMapOn = this->GetBool( _T("thin_film_map_on") );

			if( HasValidTexMap( mThinFilmWeightMap, mThinFilmWeightMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mThinFilmWeightMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mThinFilmWeightMap && mThinFilmWeightMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mThinFilmWeightMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = CreateColorShadingNetwork( mThinFilmWeight, mThinFilmWeight, mThinFilmWeight, 1.0f );

				sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
			}
		}
		if( !sgMaterial->HasMaterialChannel( "thin_film_ior" ) )
		{
			const char* cChannelName = "thin_film_ior";
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			Texmap* mThinFilmIORMap = this->GetMap( _T("thin_film_ior_map") );
			float mThinFilmIOR = this->GetFloat( _T("thin_film_ior") );
			bool mThinFilmIORMapOn = this->GetBool( _T("thin_film_ior_map_on") );

			if( HasValidTexMap( mThinFilmIORMap, mThinFilmIORMapOn ) )
			{
				spShadingNode sgShadingNode = MaxReference->CreateSgMaterialPBRChannel( mThinFilmIORMap, maxChannelID, cMaterialName, cChannelName );
				sgMaterial->SetShadingNetwork( cChannelName, sgShadingNode );
			}
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mThinFilmIORMap && mThinFilmIORMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mThinFilmIORMap, tMaterialName, tChannelName );
				}

				// fit IOR of 0 - 5 into 0 - 1
				float divisor = 5.0f;
				float mCorrectedIOR = _CLAMP( mThinFilmIOR / divisor, 0.0f, 1.0f );

				spShadingColorNode sgScaleColorNode = CreateColorShadingNetwork( mCorrectedIOR, mCorrectedIOR, mCorrectedIOR, 1.0f );

				sgMaterial->SetShadingNetwork( cChannelName, sgScaleColorNode );
			}
		}
#endif
		return true;
	}

	void ReadPropertiesFromMaterial( Mtl* mMaxMaterial )
	{
		const int numReferences = mMaxMaterial->NumRefs();
		for( int i = 0; i < numReferences; i++ )
		{
			ReferenceTarget* referenceTarget = mMaxMaterial->GetReference( i );

			if( !referenceTarget )
			{
				continue;
			}

			MSTR mClassName;
			referenceTarget->GetClassName( mClassName );
			if( mClassName == L"ParamBlock2" )
			{
				Class_ID mClassId = referenceTarget->ClassID();
				IParamBlock2* mParamBlock = dynamic_cast<IParamBlock2*>( referenceTarget );
				if( !mParamBlock )
					continue;

				const int numParams = mParamBlock->NumParams();
				for( int j = 0; j < numParams; ++j )
				{
					const ParamID mParamId = mParamBlock->IndextoID( j );

					const ParamDef& mParamDef = mParamBlock->GetParamDef( mParamId );
					PB2Value& mParamValue = mParamBlock->GetPB2Value( mParamId, 0 );

					std::basic_string<TCHAR> tFloatMember = _T("");
					switch( mParamDef.type )
					{
						case TYPE_FLOAT:
							tFloatMember = _T("float ");
							tFloatMember += mParamDef.int_name;
							tFloatMember += _T(" = 0.f;\n");

							floatProps.insert( std::pair<std::basic_string<TCHAR>, float>( mParamDef.int_name, mParamValue.f ) );

							tMaterialProperties += tFloatMember;
							break;
						case TYPE_INT:
							tFloatMember = _T("int ");
							tFloatMember += mParamDef.int_name;
							tFloatMember += _T(" = 0;\n");

							intProps.insert( std::pair<std::basic_string<TCHAR>, int>( mParamDef.int_name, mParamValue.i ) );

							tMaterialProperties += tFloatMember;
							break;
						case TYPE_BOOL:
							tFloatMember = _T("bool ");
							tFloatMember += mParamDef.int_name;
							tFloatMember += _T(" = false;\n");

							boolProps.insert( std::pair<std::basic_string<TCHAR>, bool>( mParamDef.int_name, mParamValue.i == 1 ) );

							tMaterialProperties += tFloatMember;
							break;
						case TYPE_FRGBA:
							tFloatMember = _T("Point4* ");
							tFloatMember += mParamDef.int_name;
							tFloatMember += _T(" = nullptr;\n");

							point4Props.insert( std::pair<std::basic_string<TCHAR>, Point4*>( mParamDef.int_name, mParamValue.p4 ) );

							tMaterialProperties += tFloatMember;
							break;
						case TYPE_TEXMAP:
							tFloatMember = _T("Texmap* ");
							tFloatMember += mParamDef.int_name;
							tFloatMember += _T(" = nullptr;\n");

							texmapProps.insert( std::pair<std::basic_string<TCHAR>, Texmap*>( mParamDef.int_name, mParamBlock->GetTexmap( mParamId ) ) );

							tMaterialProperties += tFloatMember;
							break;
						default: break;
					}
				}
			}
		}
	}
};

const int kEffectFilePBlockIndex = 1;
const int kEffectFileParamID = 0;

SimplygonMax::SimplygonMax()
{
	this->UILock = CreateMutex( nullptr, FALSE, nullptr );

	this->MaxInterface = nullptr;
	this->CurrentTime = 0;

	this->MaxScriptLocale = _create_locale( LC_ALL, "en-US" );

	this->extractionType = BATCH_PROCESSOR;
	this->TextureCoordinateRemapping = 0;
	this->LockSelectedVertices = false;

	this->sgPipeline = Simplygon::NullPtr;

	this->SettingsObjectName = _T("");

	this->meshFormatString = _T("{MeshName}");
	this->initialLodIndex = 1;

	this->workDirectoryHandler = nullptr;
	this->SceneHandler = nullptr;

	this->UseMaterialColors = false;
	this->UseNonConflictingTextureNames = true;
	this->UseNewMaterialSystem = false;
	this->GenerateMaterial = true;

	this->EdgeSetsEnabled = true;

	this->CanUndo = true;
	this->MaxNumBonesPerVertex = Simplygon::SG_NUM_SUPPORTED_BONES_PER_VERTEX;
	this->RunDebugger = false;

	this->PipelineRunMode = 1;

	this->QuadMode = false;

	this->AllowUnsafeImport = false;

	this->ShowProgress = true;

	this->SelectedMeshCount = 0;

	this->SpawnError = 0;
	this->SpawnThreadExitValue = 0;
	this->SpawnThreadHandle = INVALID_HANDLE_VALUE;
	this->SpawnThreadID = 0;

	this->logProgress = 0;
	this->tLogMessage = nullptr;

	this->mapMaterials = true;
	this->mapMeshes = true;
	this->copyTextures = true;

	this->numBadTriangulations = 0;

	this->Reset();

	this->materialInfoHandler = new MaterialInfoHandler();
}

// progress callback for progress reported by the optimization
void SimplygonMax::ProgressCallback( int progress )
{
	static int lastProgress = -1;
	if( progress != lastProgress )
	{
		lastProgress = progress;
		this->Callback( _T(""), false, _T("Processing..."), progress );
	}
}

// error callback for errors reported by the optimization
void SimplygonMax::ErrorCallback( const TCHAR* tErrorMessage )
{
	this->Callback( _T(""), true, tErrorMessage, 100 );
}

SimplygonMax::~SimplygonMax()
{
	if( this->materialInfoHandler )
	{
		delete this->materialInfoHandler;
	}

	this->CleanUp();

	this->GlobalGuidToMaxNodeMap.clear();
	this->GlobalMaxToSgMaterialMap.clear();
	this->GlobalSgToMaxMaterialMap.clear();
	this->GlobalExportedMaterialMap.clear();

	_free_locale( this->MaxScriptLocale );
}

void SimplygonMax::Reset()
{
	if( sg )
	{
		// make sure default tangent space is set before proceeding,
		// keep in mind that Pipelines will override this setting.
		sg->SetGlobalDefaultTangentCalculatorTypeSetting( ETangentSpaceMethod::Autodesk3dsMax );
	}

	this->extractionType = BATCH_PROCESSOR;
	this->TextureCoordinateRemapping = 0;
	this->LockSelectedVertices = false;

	this->sgPipeline = Simplygon::NullPtr;
	this->inputSceneFile = _T("");
	this->outputSceneFile = _T("");

	this->SettingsObjectName = _T("");
	this->meshFormatString = _T("{MeshName}");
	this->initialLodIndex = 1;

	this->CanUndo = true;
	this->ShowProgress = true;
	this->RunDebugger = false;

	this->PipelineRunMode = 1;

	this->QuadMode = false;
	this->numBadTriangulations = 0;

	this->AllowUnsafeImport = false;

	this->TextureCoordinateRemapping = 0;
	this->MaxNumBonesPerVertex = Simplygon::SG_NUM_SUPPORTED_BONES_PER_VERTEX;
	this->LockSelectedVertices = false;

	this->UseMaterialColors = false;
	this->UseNonConflictingTextureNames = true;
	this->GenerateMaterial = true;

	this->EdgeSetsEnabled = true;

	this->mapMaterials = true;
	this->mapMeshes = true;
	this->copyTextures = true;

	this->MaterialColorOverrides.clear();
	this->MaterialTextureOverrides.clear();
	this->MaterialChannelOverrides.clear();
	this->MaxVertexColorOverrides.clear();

	this->CachedMaterialInfos.clear();

	this->GlobalExportedMaterialMap.clear();
	this->GlobalMaxToSgMaterialMap.clear();

	this->ShadingTextureNodeToPath.clear();

	this->SelectionSetEdgesMap.clear();

	this->DefaultPrefix = std::basic_string<TCHAR>( _T("_LOD") );

	this->ClearShadingNetworkInfo( true );

	this->TextureOutputDirectory = _T("");

	this->materialProxyTable.clear();
	this->materialProxyWritebackTable.clear();

	this->SetUseNewMaterialSystem( false );

	this->GlobalGuidToMaxNodeMap.clear();

	this->RequiredCleanUp();
}

bool SimplygonMax::Initialize()
{
	// redirect Simplygon events to this instance
	SimplygonInitInstance->SetRelay( this );

	// initialize Simplygon, if needed
	const bool bInitialized = SimplygonInitInstance->Initialize();
	if( !bInitialized )
	{
		return false;
	}

	this->MaxInterface = GetCOREInterface();

	return true;
}

bool SimplygonMax::ProcessSelectedGeometries()
{
	if( !this->Initialize() )
	{
		return false;
	}

	const bool bProcessingSucceeded = this->ProcessLODMeshes();

	this->CleanUp();

	return bProcessingSucceeded;
}

bool SimplygonMax::ProcessSceneFromFile( std::basic_string<TCHAR> tImportFilePath, std::basic_string<TCHAR> tExportFilePath )
{
	if( !this->Initialize() )
	{
		return false;
	}

	this->inputSceneFile = tImportFilePath;
	this->outputSceneFile = tExportFilePath;

	if( !this->workDirectoryHandler )
	{
		this->workDirectoryHandler = new WorkDirectoryHandler();
	}

	const bool bProcessingSucceeded = this->RunSimplygonProcess();
	if( !bProcessingSucceeded )
	{
		this->LogToWindow( _T("Optimization failed!"), Error );
		return false;
	}

	this->CleanUp();

	return bProcessingSucceeded;
}

bool SimplygonMax::ExportSceneToFile( std::basic_string<TCHAR> tExportFilePath )
{
	if( !this->Initialize() )
	{
		return false;
	}

	if( this->workDirectoryHandler == nullptr )
	{
		this->workDirectoryHandler = new WorkDirectoryHandler();
	}

	std::basic_string<TCHAR> tTargetRootDirectory = GetDirectoryOfFile( tExportFilePath.c_str() );
	tTargetRootDirectory = CorrectPath( tTargetRootDirectory );

	const bool bFolderCreated = CreateFolder( tTargetRootDirectory );
	if( !bFolderCreated )
		return false;

	this->workDirectoryHandler->SetExportWorkDirectory( tTargetRootDirectory );

	this->ClearGlobalMapping();

	const bool bSceneExtracted = this->ExtractScene();
	if( !bSceneExtracted )
	{
		return false;
	}
	// store mapping in scene, for cross session import
	this->WriteMaterialMappingAttribute();

	const bool bSceneSaved = this->SceneHandler->sgScene->SaveToFile( LPCTSTRToConstCharPtr( tExportFilePath.c_str() ) );

	return bSceneSaved;
}

bool SimplygonMax::ImportSceneFromFile( std::basic_string<TCHAR> tImportFilePath )
{
	if( !this->Initialize() )
	{
		return false;
	}

	if( this->workDirectoryHandler == nullptr )
	{
		this->workDirectoryHandler = new WorkDirectoryHandler();
	}

	spScene sgLodScene = sg->CreateScene();

	const bool bSceneLoaded = sgLodScene->LoadFromFile( LPCTSTRToConstCharPtr( tImportFilePath.c_str() ) );
	if( !bSceneLoaded )
		return false;

	if( this->GetSceneHandler() == nullptr )
	{
		this->SceneHandler = new Scene();
	}

	this->GetSceneHandler()->sgProcessedScenes = { sgLodScene };

	std::basic_string<TCHAR> sTargetRootDirectory = CorrectPath( GetDirectoryOfFile( tImportFilePath.c_str() ) );

	const bool bFolderCreated = CreateFolder( sTargetRootDirectory );
	if( !bFolderCreated )
		return false;

	this->workDirectoryHandler->SetImportWorkDirectory( sTargetRootDirectory );

	const bool bImported = ImportProcessedScenes();
	if( bImported )
	{
		// force an update in the viewport!
		this->MaxInterface->RedrawViews( this->CurrentTime );
	}

	return bImported;
}

#pragma region OVERRIDES
bool SimplygonMax::MaterialTexture( const TCHAR* tMaterialName, const TCHAR* tChannelName, const TCHAR* tTextureFilePath, const bool bSRGB )
{
	// valid input?
	if( !tMaterialName || !tChannelName || !tTextureFilePath )
		return false;

	// already exists in list?
	for( size_t i = 0; i < this->MaterialTextureOverrides.size(); ++i )
	{
		if( _tcscmp( tMaterialName, this->MaterialTextureOverrides[ i ].MaterialName.c_str() ) == 0 &&
		    _tcscmp( tChannelName, this->MaterialTextureOverrides[ i ].MappingChannelName.c_str() ) == 0 )
			return true;
	}

	// if not in list, create override
	MaterialTextureOverride materialTexture;
	materialTexture.MaterialName = tMaterialName;
	materialTexture.MappingChannelName = tChannelName;
	materialTexture.TextureFileName = tTextureFilePath;
	materialTexture.IsSRGB = bSRGB;

	// and add to list
	this->MaterialTextureOverrides.push_back( materialTexture );
	return true;
}

bool SimplygonMax::MaterialTextureMapChannel( const TCHAR* tMaterialName, const TCHAR* tChannelName, int mapChannel )
{
	// valid input?
	if( !tMaterialName || !tChannelName )
		return false;

	// already exists in list?
	for( size_t i = 0; i < this->MaterialChannelOverrides.size(); ++i )
	{
		if( _tcscmp( tMaterialName, this->MaterialChannelOverrides[ i ].MaterialName.c_str() ) == 0 &&
		    _tcscmp( tChannelName, this->MaterialChannelOverrides[ i ].MappingChannelName.c_str() ) == 0 )
			return true;
	}

	// if not in list, create override
	MaterialTextureMapChannelOverride materialTextureMapChannel;
	materialTextureMapChannel.MaterialName = tMaterialName;
	materialTextureMapChannel.MappingChannelName = tChannelName;
	materialTextureMapChannel.MappingChannel = mapChannel;

	// and add to list
	this->MaterialChannelOverrides.push_back( materialTextureMapChannel );
	return true;
}

bool SimplygonMax::SetIsVertexColorChannel( int maxChannel, BOOL bIsVertexColor )
{
	// valid input?
	if( maxChannel < 3 ) // we can only override channel 3 and above (0 is vc, 1 is uv, 2 is reserved, 3(+) are uv coords in max)
		return false;

	// already exists in list?
	for( std::vector<int>::const_iterator& vertexColorIterator = this->MaxVertexColorOverrides.begin();
	     vertexColorIterator != this->MaxVertexColorOverrides.end();
	     vertexColorIterator++ )
	{
		if( *vertexColorIterator == maxChannel )
		{
			if( bIsVertexColor )
			{
				// already set, ignore
				return true;
			}
			else
			{
				// remove the old one as no override is required
				this->MaxVertexColorOverrides.erase( vertexColorIterator );
				return true;
			}
		}
	}

	// if not in list, add it
	if( bIsVertexColor )
	{
		this->MaxVertexColorOverrides.push_back( maxChannel );
	}

	return true;
}

bool SimplygonMax::MaterialColor( const TCHAR* tMaterialName, const TCHAR* tChannelName, float r, float g, float b, float a )
{
	// valid input?
	if( !tMaterialName || !tChannelName )
		return false;

	// already exists in list?
	for( size_t i = 0; i < this->MaterialColorOverrides.size(); ++i )
	{
		if( _tcscmp( tMaterialName, this->MaterialColorOverrides[ i ].MaterialName.c_str() ) == 0 &&
		    _tcscmp( tChannelName, this->MaterialColorOverrides[ i ].MappingChannelName.c_str() ) == 0 )
			return true;
	}

	// if not in list, create override
	MaterialColorOverride materialColor;
	materialColor.MaterialName = tMaterialName;
	materialColor.MappingChannelName = tChannelName;
	materialColor.SetColorRGBA( r, g, b, a );

	// and add to list
	this->MaterialColorOverrides.push_back( materialColor );
	return true;
}
#pragma endregion

std::set<std::basic_string<TCHAR>> SimplygonMax::GetSetsForNode( INode* mMaxNode )
{
	std::set<std::basic_string<TCHAR>> sNodeSelectionSets;

	// get the current node's handle
	const unsigned long meshHandle = mMaxNode->GetHandle();

	// search all sets for the specific handle,
	// and populate the list if present
	for( std::map<std::basic_string<TCHAR>, std::set<unsigned long>>::const_iterator& setIterator = this->SelectionSetObjectsMap.begin();
	     setIterator != this->SelectionSetObjectsMap.end();
	     setIterator++ )
	{
		std::set<unsigned long>::const_iterator& handleIterator = setIterator->second.find( meshHandle );
		if( handleIterator != setIterator->second.end() )
		{
			sNodeSelectionSets.insert( setIterator->first );
		}
	}

	// return the list of sets for this node
	return sNodeSelectionSets;
}

bool SimplygonMax::NodeExistsInActiveSets( INode* mMaxNode )
{
	// fetch all sets for this specific node
	const std::set<std::basic_string<TCHAR>>& sMeshSelectionSets = GetSetsForNode( mMaxNode );

	bool bExistsInActiveSet = false;
	if( sMeshSelectionSets.size() > 0 )
	{
		// for all sets that this node is present in
		for( std::set<std::basic_string<TCHAR>>::const_iterator& setIterator = sMeshSelectionSets.begin(); setIterator != sMeshSelectionSets.end();
		     setIterator++ )
		{
			// match the set with the active sets in the pipeline,
			// and return true if there's a match
			std::set<std::basic_string<TCHAR>>::const_iterator& sActiveSetsIterator = this->SelectionSetsActiveInPipeline.find( setIterator->c_str() );
			if( sActiveSetsIterator != this->SelectionSetsActiveInPipeline.end() )
			{
				bExistsInActiveSet = true;
				break;
			}
		}
	}

	return bExistsInActiveSet;
}

// main scene graph
bool SimplygonMax::CreateSceneGraph( INode* mMaxNode, spSceneNode sgNode, std::vector<std::pair<INode*, spSceneMesh>>& mMaxSgMeshList, spScene sgScene )
{
	// create a new scene node at the same level as the maxNode
	spSceneNode sgCreatedNode = Simplygon::NullPtr;
	bool bPostAddCameraToSelectionSet = false;

	// is this node a mesh?
	const bool bIsMesh = this->QuadMode ? this->IsMesh_Quad( mMaxNode ) : this->IsMesh( mMaxNode );

	// if so, does it exist in an active set?
	const bool bMeshExistsInSet = bIsMesh ? NodeExistsInActiveSets( mMaxNode ) : false;

	// if there are active selection-sets in pipeline, mark for export
	// otherwise, go by viewport selection
	const bool bExportMesh = bMeshExistsInSet ? true : mMaxNode->Selected() != 0;

	// if this node is a mesh, add it to list for later use
	if( bIsMesh && bExportMesh )
	{
		spSceneMesh sgMesh = sg->CreateSceneMesh();

		sgCreatedNode = spSceneMesh::SafeCast( sgMesh );

		mMaxSgMeshList.push_back( std::pair<INode*, spSceneMesh>( mMaxNode, sgMesh ) );
	}
	else if( this->IsCamera( mMaxNode ) )
	{
		sgCreatedNode = this->AddCamera( mMaxNode );
		if( sgCreatedNode.IsNull() )
		{
			sgCreatedNode = sg->CreateSceneNode();
		}
		else
		{
			bPostAddCameraToSelectionSet = true;
		}
	}
	else
	{
		sgCreatedNode = sg->CreateSceneNode();
	}

	// assign node properties
	const char* cNodeName = LPCTSTRToConstCharPtr( mMaxNode->GetName() );
	sgCreatedNode->SetName( cNodeName );

	ULONG mUniquehandle = mMaxNode->GetHandle();
	sgCreatedNode->SetUserData( "MAX_UniqueHandle", &mUniquehandle, sizeof( ULONG ) );

	sgNode->AddChild( sgCreatedNode );

	// copy node transformations
	Matrix3 mParentMatrix;
	mParentMatrix.IdentityMatrix();

	INode* mMaxParentNode = mMaxNode->GetParentNode();

	// parent
	if( mMaxParentNode != nullptr )
	{
		if( mMaxParentNode->GetObjTMAfterWSM( this->CurrentTime ).IsIdentity() )
		{
			mParentMatrix = Inverse( mMaxParentNode->GetObjTMBeforeWSM( this->CurrentTime ) );
		}
		else
		{
			mParentMatrix = mMaxParentNode->GetObjectTM( this->CurrentTime );
		}
	}

	// child
	Matrix3 mNodeMatrix;
	if( mMaxNode->GetObjTMAfterWSM( this->CurrentTime ).IsIdentity() )
	{
		mNodeMatrix = Inverse( mMaxNode->GetObjTMBeforeWSM( this->CurrentTime ) );
	}
	else
	{
		mNodeMatrix = mMaxNode->GetObjectTM( this->CurrentTime );
	}

	mNodeMatrix = mNodeMatrix * Inverse( mParentMatrix );

	spMatrix4x4 sgRelativeTransform = sgCreatedNode->GetRelativeTransform();
	const Point4& r0 = mNodeMatrix.GetColumn( 0 );
	const Point4& r1 = mNodeMatrix.GetColumn( 1 );
	const Point4& r2 = mNodeMatrix.GetColumn( 2 );

	sgRelativeTransform->SetElement( 0, 0, r0.x );
	sgRelativeTransform->SetElement( 0, 1, r1.x );
	sgRelativeTransform->SetElement( 0, 2, r2.x );
	sgRelativeTransform->SetElement( 1, 0, r0.y );
	sgRelativeTransform->SetElement( 1, 1, r1.y );
	sgRelativeTransform->SetElement( 1, 2, r2.y );
	sgRelativeTransform->SetElement( 2, 0, r0.z );
	sgRelativeTransform->SetElement( 2, 1, r1.z );
	sgRelativeTransform->SetElement( 2, 2, r2.z );
	sgRelativeTransform->SetElement( 3, 0, r0.w );
	sgRelativeTransform->SetElement( 3, 1, r1.w );
	sgRelativeTransform->SetElement( 3, 2, r2.w );

	// node mapping
	const spString rNodeId = sgCreatedNode->GetNodeGUID();
	const char* cNodeId = rNodeId.c_str();

	this->MaxSgNodeMap.insert( std::pair<INode*, std::string>( mMaxNode, cNodeId ) );
	this->SgMaxNodeMap.insert( std::pair<std::string, INode*>( cNodeId, mMaxNode ) );

	// note: do not append unselected meshes to global node map,
	// as these will cause the skinning code to find non-appropriate skinCluster
	// for Aggregation and Remeshing.
	if( !( bIsMesh && !bExportMesh ) )
	{
		this->GlobalGuidToMaxNodeMap.insert(
		    std::pair<std::string, GlobalMeshMap>( cNodeId, GlobalMeshMap( cNodeId, mMaxNode->GetName(), mMaxNode->GetHandle() ) ) );
	}

	if( bPostAddCameraToSelectionSet )
	{
		this->MakeCameraTargetRelative( mMaxNode, sgCreatedNode );
		this->AddToObjectSelectionSet( mMaxNode );
	}

	for( int childIndex = 0; childIndex < mMaxNode->NumberOfChildren(); ++childIndex )
	{
		INode* mMaxChildNode = mMaxNode->GetChildNode( childIndex );
		if( !this->CreateSceneGraph( mMaxChildNode, sgCreatedNode, mMaxSgMeshList, sgScene ) )
		{
			return false;
		}
	}

	// end of create scene graph
	return true;
}

void SimplygonMax::PopulateActiveSets()
{
	spStringArray sgActiveSetArray = sg->CreateStringArray();

	// if there's a pipeline, fetch active selection-sets
	if( this->sgPipeline.NonNull() )
	{
		this->sgPipeline->GetActiveSelectionSets( sgActiveSetArray );
	}

	// make sure the active set is empty before populating
	this->SelectionSetsActiveInPipeline.clear();

	// if there are active selection-sets in pipeline,
	// store data for the scene graph generation
	if( sgActiveSetArray->GetItemCount() > 0 )
	{
		for( uint i = 0; i < sgActiveSetArray->GetItemCount(); ++i )
		{
			spString sgSetName = sgActiveSetArray->GetItem( i );
			const char* cSetName = sgSetName.c_str();
			this->SelectionSetsActiveInPipeline.insert( ConstCharPtrToLPCTSTR( cSetName ) );
		}
	}
}

void SimplygonMax::WriteMaterialMappingAttribute()
{
	// calculate required memory for "MAX_MaterialMappingData" attribute
	uint attributeDataSize = 0;

	const size_t numMaterials = this->GlobalExportedMaterialMap.size();
	attributeDataSize += sizeof( size_t ); // numMaterials

	for( size_t materialIndex = 0; materialIndex < numMaterials; ++materialIndex )
	{
		MaxMaterialMap* materialMap = this->GlobalExportedMaterialMap[ materialIndex ];

		// sgMaterialName
		const size_t numSubMaterials = materialMap->MaxToSGMapping.size();
		const char* cMaterialName = LPCTSTRToConstCharPtr( materialMap->sgMaterialName.c_str() );
		const size_t numMaterialNameChars = strlen( cMaterialName ) + 1; // trailing zero is always included in string

		// sgMaterialId
		const char* cMaterialId = materialMap->sgMaterialId.c_str();
		const size_t numMaterialIdChars = strlen( cMaterialId ) + 1;

		attributeDataSize += sizeof( AnimHandle );                                             // uniqueHandle
		attributeDataSize += sizeof( size_t );                                                 // materialId length
		attributeDataSize += sizeof( char ) * (uint)numMaterialIdChars;                        // materialId
		attributeDataSize += sizeof( size_t );                                                 // materialName length
		attributeDataSize += sizeof( char ) * (uint)numMaterialNameChars;                      // materialName
		attributeDataSize += sizeof( size_t );                                                 // numSubMaterials
		attributeDataSize += (uint)materialMap->MaxToSGMapping.size() * ( sizeof( int ) * 2 ); // numSubMaterials * <int, int>
	}

	// write data to attribute memory
	uchar* attributeData = new uchar[ attributeDataSize ];
	uint attributeDataOffset = 0;

	// write numMaterials
	memcpy_s( &attributeData[ attributeDataOffset ], sizeof( size_t ), &numMaterials, sizeof( size_t ) );
	attributeDataOffset += sizeof( size_t );

	// write materials and sub-material indices
	for( size_t materialIndex = 0; materialIndex < numMaterials; ++materialIndex )
	{
		MaxMaterialMap* mMap = this->GlobalExportedMaterialMap[ materialIndex ];
		const size_t numSubMaterials = mMap->MaxToSGMapping.size();

		// write uniqueHandle
		const AnimHandle uniqueHandle = mMap->mMaxMaterialHandle;
		memcpy_s( &attributeData[ attributeDataOffset ], sizeof( AnimHandle ), &uniqueHandle, sizeof( AnimHandle ) );
		attributeDataOffset += sizeof( AnimHandle );

		// write sgMaterialId
		const char* cMaterialId = mMap->sgMaterialId.c_str();
		const size_t numMaterialIdChars = strlen( cMaterialId ) + 1;
		const size_t materialIdSize = sizeof( char ) * numMaterialIdChars;

		memcpy_s( &attributeData[ attributeDataOffset ], sizeof( size_t ), &materialIdSize, sizeof( size_t ) );
		attributeDataOffset += sizeof( size_t );

		memcpy_s( &attributeData[ attributeDataOffset ], materialIdSize, cMaterialId, materialIdSize );
		attributeDataOffset += (uint)materialIdSize;

		// write sgMaterialName
		const char* cMaterialName = LPCTSTRToConstCharPtr( mMap->sgMaterialName.c_str() );
		const size_t numMaterialNameChars = strlen( cMaterialName ) + 1;
		const size_t materialNameSize = sizeof( char ) * numMaterialNameChars;

		memcpy_s( &attributeData[ attributeDataOffset ], sizeof( size_t ), &materialNameSize, sizeof( size_t ) );
		attributeDataOffset += sizeof( size_t );

		memcpy_s( &attributeData[ attributeDataOffset ], materialNameSize, cMaterialName, materialNameSize );
		attributeDataOffset += (uint)materialNameSize;

		// write numSubMaterials
		memcpy_s( &attributeData[ attributeDataOffset ], sizeof( size_t ), &numSubMaterials, sizeof( size_t ) );
		attributeDataOffset += sizeof( size_t );

		// write subMaterial mapping indices
		for( std::map<int, int>::const_iterator& subIndexmap = mMap->MaxToSGMapping.begin(); subIndexmap != mMap->MaxToSGMapping.end(); subIndexmap++ )
		{
			// first
			memcpy_s( &attributeData[ attributeDataOffset ], sizeof( int ), &subIndexmap->first, sizeof( int ) );
			attributeDataOffset += sizeof( int );

			// second
			memcpy_s( &attributeData[ attributeDataOffset ], sizeof( int ), &subIndexmap->second, sizeof( int ) );
			attributeDataOffset += sizeof( int );
		}
	}

	// write attribute to sgScene
	spScene sgScene = this->SceneHandler->sgScene;
	sgScene->SetUserData( "MAX_MaterialMappingData", attributeData, attributeDataSize );

	delete[] attributeData;
}

void SimplygonMax::ReadMaterialMappingAttribute( spScene sgScene )
{
	// make sure mapping data is cleared
	this->CleanUpGlobalMaterialMappingData();

	spUnsignedCharData sgMappingData = sgScene->GetUserData( "MAX_MaterialMappingData" );
	if( sgMappingData.IsNullOrEmpty() )
		return;

	uint dataOffset = 0;
	const unsigned char* cData = sgMappingData.Data();

	// read numMaterials
	size_t numMaterials = 0;
	memcpy_s( &numMaterials, sizeof( size_t ), &cData[ dataOffset ], sizeof( size_t ) );
	dataOffset += sizeof( size_t );

	for( size_t materialIndex = 0; materialIndex < numMaterials; ++materialIndex )
	{
		// read uniqueHandle
		AnimHandle uniqueHandle = 0;
		memcpy_s( &uniqueHandle, sizeof( AnimHandle ), &cData[ dataOffset ], sizeof( AnimHandle ) );
		dataOffset += sizeof( AnimHandle );

		// read sgMaterialId
		size_t materialIdLength = 0;
		memcpy_s( &materialIdLength, sizeof( size_t ), &cData[ dataOffset ], sizeof( size_t ) );
		dataOffset += sizeof( size_t );

		size_t materialIdSize = sizeof( char ) * materialIdLength;
		char* cMaterialIdBuffer = new char[ materialIdLength ];
		memcpy_s( cMaterialIdBuffer, materialIdSize, &cData[ dataOffset ], materialIdSize );
		dataOffset += (uint)materialIdSize;

		// read sgMaterialName
		size_t nameLength = 0;
		memcpy_s( &nameLength, sizeof( size_t ), &cData[ dataOffset ], sizeof( size_t ) );
		dataOffset += sizeof( size_t );

		size_t nameSize = sizeof( char ) * nameLength;
		char* cMaterialNameBuffer = new char[ nameLength ];
		memcpy_s( cMaterialNameBuffer, nameSize, &cData[ dataOffset ], nameSize );
		dataOffset += (uint)nameSize;

		// read numSubMaterials
		size_t numSubMaterials = 0;
		memcpy_s( &numSubMaterials, sizeof( size_t ), &cData[ dataOffset ], sizeof( size_t ) );
		dataOffset += sizeof( size_t );

		const TCHAR* tMaterialName = ConstCharPtrToLPCTSTR( cMaterialNameBuffer );

		MaxMaterialMap* mMap = new MaxMaterialMap( uniqueHandle, tMaterialName, cMaterialIdBuffer );
		delete[] cMaterialNameBuffer;
		delete[] cMaterialIdBuffer;

		// read sub-material map
		for( size_t subMaterialIndex = 0; subMaterialIndex < numSubMaterials; ++subMaterialIndex )
		{
			// read first
			int first = 0;
			memcpy_s( &first, sizeof( int ), &cData[ dataOffset ], sizeof( int ) );
			dataOffset += sizeof( int );

			int second = 0;
			memcpy_s( &second, sizeof( int ), &cData[ dataOffset ], sizeof( int ) );
			dataOffset += sizeof( int );

			mMap->AddSubMaterialMapping( first, second );
		}

		this->GlobalExportedMaterialMap.push_back( mMap );
	}
}

// Optimize geometries
bool SimplygonMax::ProcessLODMeshes()
{
	this->LogToWindow( _T("Running Simplygon Max Plugin...") );

	// fetch current time, for bone extraction
	this->CurrentTime = this->MaxInterface->GetTime();

	// find and store all active sets in pipeline
	this->PopulateActiveSets();

	// correct number of bones per vertex
	this->MaxNumBonesPerVertex =
	    this->MaxNumBonesPerVertex > Simplygon::SG_NUM_SUPPORTED_BONES_PER_VERTEX ? Simplygon::SG_NUM_SUPPORTED_BONES_PER_VERTEX : this->MaxNumBonesPerVertex;

	// create Simplygon scene
	this->SceneHandler = new Scene();
	this->SceneHandler->sgScene = sg->CreateScene();

	this->materialInfoHandler->Clear();
	this->UsedShaderReferences.clear();

	// create a new work-directory
	this->workDirectoryHandler = new WorkDirectoryHandler();

	std::vector<std::pair<INode*, spSceneMesh>> mMaxSgMeshList;

	// max root node
	INode* mMaxRootNode = MaxInterface->GetRootNode();
	spSceneNode sgRootNode = this->SceneHandler->sgScene->GetRootNode();

	// find selection sets
	this->LogToWindow( _T("Finding edge sets...") );

	if( this->EdgeSetsEnabled )
	{
		this->FindSelectedEdges();
	}

	this->FindSelectedObjects();

	// create scene graph
	this->LogToWindow( _T("Creating scene graph...") );

	if( !this->CreateSceneGraph( mMaxRootNode, sgRootNode, mMaxSgMeshList, this->SceneHandler->sgScene ) )
	{
		return false;
	}

	// create mapping data (export only)
	this->SelectedMeshCount = mMaxSgMeshList.size();
	this->SelectedMeshNodes.resize( this->SelectedMeshCount );

	for( size_t meshIndex = 0; meshIndex < this->SelectedMeshCount; ++meshIndex )
	{
		const std::pair<INode*, spSceneMesh>& maxSgMeshmap = mMaxSgMeshList[ meshIndex ];

		MeshNode* meshNode = new MeshNode();
		meshNode->MaxNode = maxSgMeshmap.first;
		meshNode->sgMesh = maxSgMeshmap.second;

		this->SelectedMeshNodes[ meshIndex ] = meshNode;
	}

	// extract all geometries
	this->LogToWindow( _T("Extracting geometries...") );

	const bool bGeometriesExtracted = this->ExtractAllGeometries();
	if( !bGeometriesExtracted )
	{
		this->LogToWindow( _T("Extraction failed!"), Error );
		return false;
	}

	// store mapping in scene, for cross session import
	this->WriteMaterialMappingAttribute();

	// run Simplygon
	this->LogToWindow( _T("Execute process...") );

	const bool bProcessingSucceeded = this->RunSimplygonProcess();
	if( !bProcessingSucceeded )
	{
		this->LogToWindow( _T("Optimization failed!"), Error );
		return false;
	}

	// begin undo hold
	if( this->CanUndo )
	{
		theHold.Begin();
	}

	// import Simplygon scene
	this->LogToWindow( _T("Importing scene...") );

	const bool bImportSceneSucceeded = this->ImportProcessedScenes();
	if( !bImportSceneSucceeded )
	{
		this->LogToWindow( _T("Import scene failed!"), Error );
		return false;
	}

	this->LogToWindow( _T("Importing done!") );

	// force an update in the viewport!
	this->MaxInterface->RedrawViews( this->CurrentTime );
	return true;
}

// Optimize geometries
bool SimplygonMax::ExtractScene()
{
	this->LogToWindow( _T("Extracting scene...") );

	// fetch current time, for bone extraction
	this->CurrentTime = this->MaxInterface->GetTime();

	// find and store all active sets in pipeline
	this->PopulateActiveSets();

	// correct number of bones per vertex
	this->MaxNumBonesPerVertex =
	    this->MaxNumBonesPerVertex > Simplygon::SG_NUM_SUPPORTED_BONES_PER_VERTEX ? Simplygon::SG_NUM_SUPPORTED_BONES_PER_VERTEX : this->MaxNumBonesPerVertex;

	// make sure previous mappings are cleared
	this->RequiredCleanUp();

	// create Simplygon scene
	this->SceneHandler = new Scene();
	this->SceneHandler->sgScene = sg->CreateScene();

	this->materialInfoHandler->Clear();
	this->UsedShaderReferences.clear();

	// create a new work-directory
	if( this->workDirectoryHandler == nullptr )
	{
		this->workDirectoryHandler = new WorkDirectoryHandler();
	}

	std::vector<std::pair<INode*, spSceneMesh>> mMaxSgMeshList;

	// max root node
	INode* mMaxRootNode = this->MaxInterface->GetRootNode();
	spSceneNode sgRootNode = this->SceneHandler->sgScene->GetRootNode();

	// create scene graph
	this->LogToWindow( _T("Creating scene graph...") );

	if( !this->CreateSceneGraph( mMaxRootNode, sgRootNode, mMaxSgMeshList, this->SceneHandler->sgScene ) )
	{
		return false;
	}

	// create mapping data (export only)
	this->SelectedMeshCount = mMaxSgMeshList.size();
	this->SelectedMeshNodes.resize( this->SelectedMeshCount );

	for( size_t meshIndex = 0; meshIndex < this->SelectedMeshCount; ++meshIndex )
	{
		const std::pair<INode*, spSceneMesh>& maxSgMeshmap = mMaxSgMeshList[ meshIndex ];

		MeshNode* meshNode = new MeshNode();
		meshNode->MaxNode = maxSgMeshmap.first;
		meshNode->sgMesh = maxSgMeshmap.second;

		this->SelectedMeshNodes[ meshIndex ] = meshNode;
	}

	// find selection sets
	this->LogToWindow( _T("Finding edge sets...") );

	if( this->EdgeSetsEnabled )
	{
		this->FindSelectedEdges();
	}

	this->FindSelectedObjects();

	// extract all geometries
	this->LogToWindow( _T("Extracting geometries...") );

	const bool bGeometriesExtracted = this->ExtractAllGeometries();
	if( !bGeometriesExtracted )
	{
		this->LogToWindow( _T("Extraction failed!"), Error );
		return false;
	}

	return true;
}

inline void AddNodeToSet( std::set<INode*>& mMaxNodeSet, INode* mMaxNode )
{
	if( mMaxNodeSet.find( mMaxNode ) == mMaxNodeSet.end() )
	{
		mMaxNodeSet.insert( mMaxNode );
	}
}

// Returns true if the specified has selected children
bool SimplygonMax::HasSelectedChildren( INode* mMaxNode )
{
	if( mMaxNode->Selected() )
	{
		return true;
	}

	for( int childIndex = 0; childIndex < mMaxNode->NumberOfChildren(); ++childIndex )
	{
		INode* mMaxChildNode = mMaxNode->GetChildNode( childIndex ); // Get the child at the given index in the given node.
		const bool bHasSelectedChildren = HasSelectedChildren( mMaxChildNode );
		if( bHasSelectedChildren )
		{
			return true;
		}
	}

	return false;
}

// Returns true if node (INode) is a tri mesh
bool SimplygonMax::IsMesh( INode* mMaxNode )
{
	Object* mMaxObject = mMaxNode->GetObjectRef();
	ObjectState mMaxObjectState = mMaxNode->EvalWorldState( this->CurrentTime );
	if( !mMaxObjectState.obj )
	{
		return false; // we have no object, skip it, go on
	}

	if( !ObjectStateIsValidAndCanConvertToType( mMaxObjectState, triObjectClassID ) )
	{
		return false; // this is not a tri-mesh object, skip it, go on
	}

	return true;
}

// Returns true if node (INode) is a poly mesh
bool SimplygonMax::IsMesh_Quad( INode* mMaxNode )
{
	Object* mMaxObject = mMaxNode->GetObjectRef();
	ObjectState mMaxObjectState = mMaxNode->EvalWorldState( this->CurrentTime );
	if( !mMaxObjectState.obj )
	{
		return false; // we have no object, skip it, go on
	}

	if( !ObjectStateIsValidAndCanConvertToType( mMaxObjectState, polyObjectClassID ) )
	{
		return false; // this is not a poly-mesh object, skip it, go on
	}

	return true;
}

// Returns true if node (INode) is a camera
bool SimplygonMax::IsCamera( INode* mMaxNode )
{
	const TCHAR* tNodeName = mMaxNode->GetName();
	const TCHAR* tClassName = mMaxNode->ClassName();

	Object* mMaxObject = mMaxNode->GetObjectRef();
	if( !mMaxObject )
		return false;

	Class_ID mClassId = mMaxObject->ClassID();
	SClass_ID mSuperClassId = mMaxObject->SuperClassID();

	// make sure node is camera
	if( mSuperClassId != SClass_ID( CAMERA_CLASS_ID ) )
	{
		return false;
	}
	else if( mClassId != Class_ID( SIMPLE_CAM_CLASS_ID, 0L ) && mClassId != Class_ID( LOOKAT_CAM_CLASS_ID, 0L ) &&
	         mClassId != MaxSDK::IPhysicalCamera::GetClassID() )
	{
		return false;
	}

	ObjectState mMaxObjectState = mMaxNode->EvalWorldState( this->CurrentTime );
	if( !mMaxObjectState.obj )
	{
		return false;
	}

	if( !ObjectStateIsValidAndCanConvertToType( mMaxObjectState, mClassId ) && !dynamic_cast<MaxSDK::IPhysicalCamera*>( mMaxObjectState.obj ) )
	{
		return false;
	}

	return true;
}

spSceneNode SimplygonMax::AddCamera( INode* mMaxNode )
{
	const TCHAR* tNodeName = mMaxNode->GetName();
	const TCHAR* tClassName = mMaxNode->ClassName();

	Object* mMaxObject = mMaxNode->GetObjectRef();
	if( !mMaxObject )
		return spSceneNode();

	Class_ID mClassId = mMaxObject->ClassID();
	SClass_ID mSuperClassId = mMaxObject->SuperClassID();

	ObjectState mMaxObjectState = mMaxNode->EvalWorldState( this->CurrentTime );
	if( !mMaxObjectState.obj )
	{
		return spSceneNode();
	}

	const bool bIsPhysical = mClassId == MaxSDK::IPhysicalCamera::GetClassID();
	if( !ObjectStateIsValidAndCanConvertToType( mMaxObjectState, mClassId ) && !bIsPhysical )
	{
		return spSceneNode();
	}

	GenCamera* mMaxGeneralCamera =
	    bIsPhysical ? dynamic_cast<GenCamera*>( mMaxObjectState.obj ) : (GenCamera*)mMaxObjectState.obj->ConvertToType( this->CurrentTime, mClassId );

	const int mCamType = mMaxGeneralCamera->Type();
	if( mCamType == FREE_CAMERA )
	{
	}
	else if( mCamType == TARGETED_CAMERA )
	{
	}
	else if( mCamType == PARALLEL_CAMERA )
	{
		return spSceneNode();
	}

	const bool bIsOrtho = mMaxGeneralCamera->IsOrtho() == TRUE;
	const float fov = mMaxGeneralCamera->GetFOV( this->CurrentTime );

	spSceneCamera sgCamera = sg->CreateSceneCamera();
	sgCamera->SetCameraType( bIsOrtho ? ECameraType::Orthographic : ECameraType::Perspective );
	sgCamera->SetFieldOfView( (real)fov );

	return spSceneNode::SafeCast( sgCamera );
}

void SimplygonMax::MakeCameraTargetRelative( INode* mMaxNode, spSceneNode sgNode )
{
	spSceneCamera sgCamera = spSceneCamera::SafeCast( sgNode );
	if( sgCamera.IsNull() )
		return;

	ViewParams mCameraViewParams;

	this->MaxInterface->GetViewParamsFromNode( mMaxNode, mCameraViewParams, this->CurrentTime );

	/* No need to extract camera transformation, use node transformation instead

	// The affine TM transforms from world coords to view coords
	// so we need the inverse of this matrix
	Matrix3 mAffineTM = mCameraViewParams.affineTM;
	Matrix3 mCoordSysTM = Inverse( mAffineTM );

	// The Z axis of this matrix is the view direction.
	Point3 mTarget = -1 * mCoordSysTM.GetRow( 2 );
	Point3 mPos = mCoordSysTM.GetRow( 3 );
	*/

	// position - use 0, 0, 0 as relative position
	// const real rPos[] = { (real)mPos.x, (real)mPos.y, (real)mPos.z };
	const real rPos[] = { 0.f, 0.f, 0.f };
	spRealArray sgCameraPosition = sgCamera->GetCameraPositions();
	sgCameraPosition->SetTupleCount( 1 );
	sgCameraPosition->SetTuple( 0, rPos );

	// target - use 0, 0, -1 as relative target
	// const real rTarget[] = { (real)mTarget.x, (real)mTarget.y, (real)mTarget.z };
	const real rTarget[] = { 0.f, 0.f, -1.f * mCameraViewParams.farRange };
	spRealArray sgCameraTarget = sgCamera->GetTargetPositions();
	sgCameraTarget->SetTupleCount( 1 );
	sgCameraTarget->SetTuple( 0, rTarget );
}

// Exports all selected Max geometries to Simplygon geometries
bool SimplygonMax::ExtractAllGeometries()
{
	// extract the geometry data
	this->numBadTriangulations = 0;
	if( this->QuadMode == true )
	{
		uint32_t oldNumBadTriangulations = 0;
		uint32_t numBadTriangulationMesh = 0;
		for( size_t meshIndex = 0; meshIndex < this->SelectedMeshCount; ++meshIndex )
		{
			if( !this->ExtractGeometry_Quad( meshIndex ) )
			{
				return false;
			}

			if( numBadTriangulations > oldNumBadTriangulations )
			{
				oldNumBadTriangulations = numBadTriangulations;
				++numBadTriangulationMesh;
			}
		}

		if( numBadTriangulations > 0 )
		{
			std::string sWarning = "Quad export - found " + std::to_string( numBadTriangulations ) + " polygons in " +
			                       std::to_string( numBadTriangulationMesh ) + " meshes which could not be optimally triangulated";
			LogToWindow( ConstCharPtrToLPCWSTRr( sWarning.c_str() ), ErrorType::Warning );
		}
	}
	else
	{
		for( size_t meshIndex = 0; meshIndex < this->SelectedMeshCount; ++meshIndex )
		{
			if( !this->ExtractGeometry( meshIndex ) )
			{
				return false;
			}
		}
	}
	return true;
}

// Switches out a scene node with a bone node
spSceneBone SimplygonMax::ReplaceNodeWithBone( spSceneNode sgNode )
{
	spSceneBone sgBoneNode = sg->CreateSceneBone();
	sgBoneNode->SetName( sgNode->GetName().c_str() );
	sgBoneNode->SetOriginalName( sgNode->GetOriginalName().c_str() );
	sgBoneNode->GetRelativeTransform()->DeepCopy( sgNode->GetRelativeTransform() );
	sgBoneNode->SetNodeGUID( sgNode->GetNodeGUID().c_str() );

	while( sgNode->GetChildCount() > 0 )
	{
		sgBoneNode->AddChild( sgNode->GetChild( 0 ) );
	}

	spSceneNode sgParentNode = sgNode->GetParent();

	int targetChildIndex = -1;
	std::vector<spSceneNode> sgNodeList;
	for( uint childIndex = 0; childIndex < sgParentNode->GetChildCount(); ++childIndex )
	{
		if( sgParentNode->GetChild( childIndex ) == sgNode )
		{
			targetChildIndex = childIndex;
			sgNodeList.push_back( sgParentNode->GetChild( childIndex ) );
		}
		else
		{
			sgNodeList.push_back( sgParentNode->GetChild( childIndex ) );
		}
	}

	sgParentNode->RemoveChildren();
	for( uint childIndex = 0; childIndex < (uint)sgNodeList.size(); ++childIndex )
	{
		if( targetChildIndex == childIndex )
		{
			sgParentNode->AddChild( sgBoneNode );
		}
		else
		{
			sgParentNode->AddChild( sgNodeList[ childIndex ] );
		}
	}

	sgNode->RemoveFromParent();

	return sgBoneNode;
}

// Add a bone to array, map and bone table
int SimplygonMax::AddBone( INode* mMaxBoneNode )
{
	spSceneBoneTable sgBoneTable = this->SceneHandler->sgScene->GetBoneTable();
	const std::map<INode*, std::string>::const_iterator& boneIterator = this->MaxBoneToSgBone.find( mMaxBoneNode );

	// if bone does not exist, add
	if( boneIterator == this->MaxBoneToSgBone.end() )
	{
		// add bone to selection set (if match)
		this->AddToObjectSelectionSet( mMaxBoneNode );

		spSceneNode sgNodeToBeReplaced = this->SceneHandler->FindSceneNode( mMaxBoneNode );
		spSceneBone sgBoneNode = ReplaceNodeWithBone( sgNodeToBeReplaced );

		// add bone to bone table
		const rid sgBoneIndex = sgBoneTable->AddBone( sgBoneNode );

		const spString rBoneId = sgBoneNode->GetNodeGUID();
		const char* cBoneId = rBoneId.c_str();

		// global map
		this->MaxBoneToSgBone.insert( std::pair<INode*, std::string>( mMaxBoneNode, cBoneId ) );
		this->SgBoneToMaxBone.insert( std::pair<std::string, INode*>( cBoneId, mMaxBoneNode ) );

		// local map
		this->SgBoneIdToIndex.insert( std::pair<std::string, int>( cBoneId, sgBoneIndex ) );

		return sgBoneIndex;
	}

	// otherwise return existing id
	else
	{
		const std::map<std::string, int>::const_iterator& bIterator = this->SgBoneIdToIndex.find( boneIterator->second );
		if( bIterator != this->SgBoneIdToIndex.end() )
		{
			return bIterator->second;
		}
	}

	return 0;
}

// Get Max bone by id
INode* SimplygonMax::GetMaxBoneById( std::string sBoneId )
{
	return this->SgBoneToMaxBone[ sBoneId ];
}

typedef std::set<SimplygonMaxPerVertexSkinningBone, std::greater<SimplygonMaxPerVertexSkinningBone>> skinning_bone_set;

// Finds all selected edges in a scene (find selected meshes and selected sets of edges for these meshes)
// Note: Geometry needs to be an editable mesh! (a check for this is done in the MaxScript)
void SimplygonMax::FindSelectedEdges()
{
	// Get all selected sets information (name and edge indices)

	// Create a string with the correct MaxScript code for receiving the desired information about the selection sets in Max
	std::basic_string<TCHAR> tMaxScriptCreateADictionaryArray = _T("");
	tMaxScriptCreateADictionaryArray += _T("my_class = \"\"\n");
	tMaxScriptCreateADictionaryArray += _T("final = #()\n");
	tMaxScriptCreateADictionaryArray += _T("index = 1\n");
	tMaxScriptCreateADictionaryArray += _T("for m in selection do\n");
	tMaxScriptCreateADictionaryArray += _T("(\n");
	tMaxScriptCreateADictionaryArray += _T("\tmy_class = classof m\n");
	tMaxScriptCreateADictionaryArray += _T("\tif my_class as string == \"Editable_mesh\" then\n");
	tMaxScriptCreateADictionaryArray += _T("\t(\n");
	tMaxScriptCreateADictionaryArray += _T("\t\thandle = m.handle\n");
	tMaxScriptCreateADictionaryArray += _T("\t\tfor setName in m.edges.selSetNames do\n");
	tMaxScriptCreateADictionaryArray += _T("\t\t(\n");
	tMaxScriptCreateADictionaryArray += _T("\t\t\tfinal[index] = setName;\n");
	tMaxScriptCreateADictionaryArray += _T("\t\t\tfinal[index+1] = handle as string\n");
	tMaxScriptCreateADictionaryArray += _T("\t\t\tindex = index + 2\n");
	tMaxScriptCreateADictionaryArray += _T("\t\t\tfor ed in m.edges[setName] do\n");
	tMaxScriptCreateADictionaryArray += _T("\t\t\t(\n");
	tMaxScriptCreateADictionaryArray += _T("\t\t\t\tfinal[index] = ed.index as string\n");
	tMaxScriptCreateADictionaryArray += _T("\t\t\t\tindex = index + 1\n");
	tMaxScriptCreateADictionaryArray += _T("\t\t\t)\n");
	tMaxScriptCreateADictionaryArray += _T("\t\t\tfinal[index] = \"ENDSET\"\n");
	tMaxScriptCreateADictionaryArray += _T("\t\t\tindex = index + 1\n");
	tMaxScriptCreateADictionaryArray += _T("\t\t)\n");
	tMaxScriptCreateADictionaryArray += _T("\t)\n");
	// tMaxScriptCreateADictionaryArray += _T("\telse print \" \"\n");
	tMaxScriptCreateADictionaryArray += _T(")\n");
	tMaxScriptCreateADictionaryArray += _T("final[index] = \"END\"\n");
	tMaxScriptCreateADictionaryArray += _T("final\n");

	BOOL bQuietErrors = FALSE;
	FPValue mFpvCreateDictionary;
	const TSTR tScript( tMaxScriptCreateADictionaryArray.c_str() );

#if MAX_VERSION_MAJOR < 24
	const BOOL bActionSucceeded = ExecuteMAXScriptScript( tScript.data(), bQuietErrors, &mFpvCreateDictionary );
#else
	const BOOL bActionSucceeded = ExecuteMAXScriptScript( tScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors, &mFpvCreateDictionary );
#endif

	if( !bActionSucceeded )
		return;

	int counter = 0;
	SelectionSetEdgePair selectionsetEdgePair; // will hold both the selected mesh's index (as int) and the selected mesh's edge indices (as vector<int>)

	// Clear the selection sets map
	if( this->SelectionSetEdgesMap.size() != 0 )
	{
		this->SelectionSetEdgesMap.clear();
	}

	if( mFpvCreateDictionary.type == TYPE_STRING_TAB )
	{
		// loop through the "string-tab"
		while( _tcscmp( ( *mFpvCreateDictionary.s_tab )[ counter ], _T("END") ) != 0 )
		{
			std::vector<int> tempSelectedSetEdgesIndices;
			const TCHAR* tSetName;

			unsigned long selectedMeshHandle = 0;
			int i = 0;

			// for each selected mesh's selection set
			while( _tcscmp( ( *mFpvCreateDictionary.s_tab )[ counter ], _T("ENDSET") ) != 0 )
			{
				// get selectionSet name (edge selection set)
				if( i == 0 )
				{
					tSetName = ( *mFpvCreateDictionary.s_tab )[ counter ];
				}
				// get selected mesh index
				else if( i == 1 )
				{
					selectedMeshHandle = _tstol( ( *mFpvCreateDictionary.s_tab )[ counter ] );
				}
				// get selectionSet indices (edge selection set)
				else
				{
					int index = _tstol( ( *mFpvCreateDictionary.s_tab )[ counter ] );
					tempSelectedSetEdgesIndices.push_back( index );
				}
				counter++;
				i++;
			}

			selectionsetEdgePair.first = selectedMeshHandle;
			selectionsetEdgePair.second = tempSelectedSetEdgesIndices;
			std::pair<std::basic_string<TCHAR>, SelectionSetEdgePair> tempSelectedSetPair( tSetName, selectionsetEdgePair );
			SelectionSetEdgesMap.insert( tempSelectedSetPair ); // map will contain selected mesh's name, selectionSets name and indices
			counter++;
		}
	}
}

// Finds all selected objects in a scene
void SimplygonMax::FindSelectedObjects()
{
	std::basic_string<TCHAR> tMaxScriptCreateADictionaryArray = _T("");
	tMaxScriptCreateADictionaryArray += _T(" final = #()\n");
	tMaxScriptCreateADictionaryArray += _T("	index = 1\n");
	tMaxScriptCreateADictionaryArray += _T("	for setKVP in selectionSets do (\n");
	tMaxScriptCreateADictionaryArray += _T("		ItemName = setKVP.name\n");
	tMaxScriptCreateADictionaryArray += _T("		final[index] = ItemName\n");
	tMaxScriptCreateADictionaryArray += _T("		index = index + 1\n");
	// tMaxScriptCreateADictionaryArray += _T("		print (\"Found set: \" + ItemName)\n");
	tMaxScriptCreateADictionaryArray += _T("		for j = 1 to selectionSets[ItemName].count do ( \n");
	// tMaxScriptCreateADictionaryArray += _T("			--print (\"Name: \" + selectionSets[ItemName][j].name)\n");
	// tMaxScriptCreateADictionaryArray += _T("			--print (\"Class: \" + classof selectionSets[ItemName][j] as string)\n");
	// tMaxScriptCreateADictionaryArray += _T("			--print (\"Handle: \" + selectionSets[ItemName][j].inode.handle as string)\n");
	// tMaxScriptCreateADictionaryArray += _T("			--print (\"Object: \" + selectionSets[ItemName][j] as string)\n");
	tMaxScriptCreateADictionaryArray += _T("			final[index] = selectionSets[ItemName][j].inode.handle\n");
	tMaxScriptCreateADictionaryArray += _T("			index = index + 1\n");
	tMaxScriptCreateADictionaryArray += _T("			)\n");
	tMaxScriptCreateADictionaryArray += _T("          \n");
	tMaxScriptCreateADictionaryArray += _T("			final[index] = \"ENDSET\"\n");
	tMaxScriptCreateADictionaryArray += _T("			index = index + 1\n");
	tMaxScriptCreateADictionaryArray += _T("			);\n");
	tMaxScriptCreateADictionaryArray += _T("           \n");
	tMaxScriptCreateADictionaryArray += _T(" final[index] = \"END\"\n");
	tMaxScriptCreateADictionaryArray += _T(" final\n");

	BOOL bQuietErrors = FALSE;
	FPValue mFpvCreateDictionary;
	const TSTR tScript( tMaxScriptCreateADictionaryArray.c_str() );

#if MAX_VERSION_MAJOR < 24
	const BOOL bActionSucceeded = ExecuteMAXScriptScript( tScript.data(), bQuietErrors, &mFpvCreateDictionary );
#else
	const BOOL bActionSucceeded = ExecuteMAXScriptScript( tScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors, &mFpvCreateDictionary );
#endif

	if( !bActionSucceeded )
		return;

	// Clear the selection sets map
	if( this->SelectionSetObjectsMap.size() != 0 )
	{
		this->SelectionSetObjectsMap.clear();
	}

	if( mFpvCreateDictionary.type == TYPE_FPVALUE_TAB_BV )
	{
		int counter = 0;
		FPValue* mSetEntry = ( *mFpvCreateDictionary.fpv_tab )[ counter ];

		// if not the end of list
		while( !( mSetEntry->type == TYPE_STRING && _tcscmp( mSetEntry->s, _T("END") ) == 0 ) )
		{
			const TCHAR* tSetName;
			unsigned long meshHandle = 0;
			std::set<unsigned long> selectionSetPair;

			int index = 0;

			// if not the end of the set
			while( !( mSetEntry->type == TYPE_STRING && _tcscmp( mSetEntry->s, _T("ENDSET") ) == 0 ) )
			{
				// pick out set name at the first index in the set
				if( index == 0 )
				{
					tSetName = mSetEntry->s;
				}
				// pick out the mesh handles for the set
				else
				{
					meshHandle = mSetEntry->i;
					selectionSetPair.insert( meshHandle );
				}

				counter++;
				index++;
				mSetEntry = ( *mFpvCreateDictionary.fpv_tab )[ counter ];
			}

			// create set from name and handle list
			std::pair<std::basic_string<TCHAR>, std::set<unsigned long>> tempSelectedSetPair( tSetName, selectionSetPair );
			this->SelectionSetObjectsMap.insert( tempSelectedSetPair );

			// update pointers for next loop
			counter++;
			index++;
			mSetEntry = ( *mFpvCreateDictionary.fpv_tab )[ counter ];
		}
	}
}

// Calculates and returns relative node transformation
Matrix3 SimplygonMax::GetRelativeTransformation( INode* mMaxNode )
{
	const Matrix3& mParentNodeTransform = mMaxNode->GetParentTM( this->CurrentTime );
	Matrix3 mNodeTransform;

	if( mMaxNode->GetObjTMAfterWSM( this->CurrentTime ).IsIdentity() )
	{
		mNodeTransform = Inverse( mMaxNode->GetObjTMBeforeWSM( this->CurrentTime ) );
	}
	else
	{
		mNodeTransform = mMaxNode->GetObjectTM( this->CurrentTime );
	}

	mNodeTransform = mNodeTransform * Inverse( mParentNodeTransform );

	return mNodeTransform;
}

// Add node to Simplygon selection set, if it matches any selection set
void SimplygonMax::AddToObjectSelectionSet( INode* mMaxNode )
{
	std::map<INode*, std::string>::const_iterator& maxToSgNodeMap = this->MaxSgNodeMap.find( mMaxNode );
	if( maxToSgNodeMap == this->MaxSgNodeMap.end() )
		return;

	std::string sNodeId = maxToSgNodeMap->second;
	spSceneNode sgNode = this->SceneHandler->sgScene->GetNodeByGUID( sNodeId.c_str() );

	if( sgNode.IsNull() )
	{
		return;
	}

	spSelectionSetTable sgSelectionSetTable = this->SceneHandler->sgScene->GetSelectionSetTable();

	for( std::map<std::basic_string<TCHAR>, std::set<unsigned long>>::const_iterator& setIterator = this->SelectionSetObjectsMap.begin();
	     setIterator != this->SelectionSetObjectsMap.end();
	     setIterator++ )
	{
		const char* cSetName = LPCTSTRToConstCharPtr( setIterator->first.c_str() );

		// for the current set, check if mesh name exists
		for( unsigned long meshHandle : setIterator->second )
		{
			if( meshHandle == mMaxNode->GetHandle() )
			{
				spSelectionSet sgSelectionSetList;
				bool bAddSetToScene = false;

				// does the set exists in scene to be exported?
				spObject sgCurrentSelectionSetObject = sgSelectionSetTable->FindItem( cSetName );
				if( !sgCurrentSelectionSetObject.IsNull() )
				{
					sgSelectionSetList = spSelectionSet::SafeCast( sgCurrentSelectionSetObject );
				}

				// create if it does not exist
				if( sgSelectionSetList.IsNull() )
				{
					sgSelectionSetList = sg->CreateSelectionSet();
					sgSelectionSetList->SetName( cSetName );
					bAddSetToScene = true;
				}

				// add the guid of the node to the selection set
				sgSelectionSetList->AddItem( sNodeId.c_str() );

				if( bAddSetToScene )
				{
					sgSelectionSetTable->AddItem( sgSelectionSetList );
				}
			}
		} // end handle loop
	}     // end set loop
}

#pragma region MORPHER
void SimplygonMax::RegisterMorphScripts()
{
	static BOOL bMorphScriptInitialized = FALSE;

	if( bMorphScriptInitialized )
		return;

	std::basic_string<TCHAR> tMorphScript = _T("");

	tMorphScript += _T("fn Simplygon_GetActiveMorphChannels nodeHandle =																				   \n");
	tMorphScript += _T("(																																   \n");
	// tMorphScript += _T("	print \"\nSimplygon_GetActiveMorphChannels()\"																				   \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle																						   \n");
	tMorphScript += _T("	activeMorphChannels = #()																									   \n");
	tMorphScript += _T("	for channelIndex = 1 to 100 do																								   \n");
	tMorphScript += _T("	(																															   \n");
	tMorphScript += _T("		if WM3_MC_HasData obj.morpher channelIndex then																			   \n");
	tMorphScript += _T("		(																														   \n");
	tMorphScript += _T("			if WM3_MC_IsValid obj.morpher channelIndex and WM3_MC_IsActive obj.morpher channelIndex then						   \n");
	tMorphScript += _T("			(																													   \n");
	tMorphScript += _T("				append activeMorphChannels channelIndex																			   \n");
	tMorphScript += _T("			)																													   \n");
	tMorphScript += _T("		)																														   \n");
	tMorphScript += _T("	)																															   \n");
	tMorphScript += _T("	activeMorphChannels																											   \n");
	tMorphScript += _T(")																																   \n");
	tMorphScript += _T("fn Simplygon_GetProgressiveWeights nodeHandle channelIndex =																	   \n");
	tMorphScript += _T("(																																   \n");
	// tMorphScript += _T("	print \"\nSimplygon_GetProgressiveWeights()\"																				   \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle																						   \n");
	tMorphScript += _T("	progressiveMorphTargetWeights = #()																							   \n");
	tMorphScript += _T("	if WM3_MC_HasData obj.morpher channelIndex then																				   \n");
	tMorphScript += _T("	(																															   \n");
	tMorphScript += _T("		if WM3_MC_IsValid obj.morpher channelIndex and WM3_MC_IsActive obj.morpher channelIndex then							   \n");
	tMorphScript += _T("		(																														   \n");
	tMorphScript += _T("			numProgressiveMorphTargets = WM3_NumberOfProgressiveMorphs obj.morpher channelIndex									   \n");
	tMorphScript += _T("			for progressiveIndex = 1 to numProgressiveMorphTargets do															   \n");
	tMorphScript += _T("			(																													   \n");
	tMorphScript += _T("				progressiveMorphTarget = WM3_GetProgressiveMorphNode obj.morpher channelIndex progressiveIndex					   \n");
	tMorphScript += _T("				if progressiveMorphTarget != undefined then																		   \n");
	tMorphScript += _T("				(																												   \n");
	tMorphScript += _T("					progressiveMorphTargetWeight = WM3_GetProgressiveMorphWeight obj.morpher channelIndex progressiveMorphTarget   \n");
	tMorphScript += _T("					append progressiveMorphTargetWeights progressiveMorphTargetWeight					 						   \n");
	tMorphScript += _T("				)																												   \n");
	tMorphScript += _T("				else																											   \n");
	tMorphScript += _T("				(																												   \n");
	tMorphScript += _T("					append progressiveMorphTargetWeights ( 100.0 as float )														   \n");
	tMorphScript += _T("				)																												   \n");
	tMorphScript += _T("			)																													   \n");
	tMorphScript += _T("		)																														   \n");
	tMorphScript += _T("	)																															   \n");
	tMorphScript += _T("	progressiveMorphTargetWeights																								   \n");
	tMorphScript += _T(")																																   \n");
	tMorphScript += _T("fn Simplygon_SetProgressiveWeights nodeHandle channelIndex progressiveIndex progressiveWeight =									   \n");
	tMorphScript += _T("(																																   \n");
	// tMorphScript += _T("	print \"\nSimplygon_SetProgressiveWeights()\"																				   \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle																						   \n");
	tMorphScript += _T("	returnValue = False																											   \n");
	tMorphScript += _T("	if WM3_MC_HasData obj.morpher channelIndex then																				   \n");
	tMorphScript += _T("	(																															   \n");
	tMorphScript += _T("		if WM3_MC_IsValid obj.morpher channelIndex then																			   \n");
	tMorphScript += _T("		(																														   \n");
	tMorphScript += _T("			numProgressiveMorphTargets = WM3_NumberOfProgressiveMorphs obj.morpher channelIndex									   \n");
	tMorphScript += _T("			if progressiveIndex <= numProgressiveMorphTargets then																   \n");
	tMorphScript += _T("			(																													   \n");
	tMorphScript += _T("				progressiveMorphTarget = WM3_GetProgressiveMorphNode obj.morpher channelIndex progressiveIndex					   \n");
	tMorphScript += _T("				WM3_SetProgressiveMorphWeight obj.morpher channelIndex progressiveMorphTarget progressiveWeight					   \n");
	tMorphScript += _T("				returnValue = True																								   \n");
	tMorphScript += _T("			)																													   \n");
	tMorphScript += _T("		)																														   \n");
	tMorphScript += _T("	)																															   \n");
	tMorphScript += _T("	returnValue																													   \n");
	tMorphScript += _T(")																																   \n");
	tMorphScript += _T("fn Simplygon_GetMorphTensions nodeHandle =																						   \n");
	tMorphScript += _T("(																																   \n");
	// tMorphScript += _T("	print \"\nSimplygon_GetMorphTension()\"																						   \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle																						   \n");
	tMorphScript += _T("	morphChannelTensions = #()																									   \n");
	tMorphScript += _T("	for channelIndex = 1 to 100 do																								   \n");
	tMorphScript += _T("	(																															   \n");
	tMorphScript += _T("		if WM3_MC_HasData obj.morpher channelIndex then																			   \n");
	tMorphScript += _T("		(																														   \n");
	tMorphScript += _T("			if WM3_MC_IsValid obj.morpher channelIndex and WM3_MC_IsActive obj.morpher channelIndex then						   \n");
	tMorphScript += _T("			(																													   \n");
	tMorphScript += _T("				morphChannelTension = WM3_GetProgressiveMorphTension obj.morpher channelIndex									   \n");
	tMorphScript += _T("				append morphChannelTensions morphChannelTension																	   \n");
	tMorphScript += _T("			)																													   \n");
	tMorphScript += _T("		)																														   \n");
	tMorphScript += _T("	)																															   \n");
	tMorphScript += _T("	morphChannelTensions																										   \n");
	tMorphScript += _T(")																																   \n");
	tMorphScript += _T("fn Simplygon_SetMorphTension nodeHandle channelIndex morphChannelTension =														   \n");
	tMorphScript += _T("(																																   \n");
	// tMorphScript += _T("	print \"\nSimplygon_SetMorphTension()\"																						   \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle																						   \n");
	tMorphScript += _T("	returnValue = False																											   \n");
	tMorphScript += _T("	if WM3_MC_HasData obj.morpher channelIndex then																				   \n");
	tMorphScript += _T("	(																															   \n");
	tMorphScript += _T("		if WM3_MC_IsValid obj.morpher channelIndex then																			   \n");
	tMorphScript += _T("		(																														   \n");
	tMorphScript += _T("			WM3_SetProgressiveMorphTension obj.morpher channelIndex morphChannelTension											   \n");
	tMorphScript += _T("			returnValue = True																									   \n");
	tMorphScript += _T("		)																														   \n");
	tMorphScript += _T("	)																															   \n");
	tMorphScript += _T("	returnValue																													   \n");
	tMorphScript += _T(")																																   \n");
	tMorphScript += _T("fn Simplygon_GetMorpChannelWeights nodeHandle =																					   \n");
	tMorphScript += _T("(																																   \n");
	// tMorphScript += _T("	print \"\nSimplygon_GetMorpChannelWeights()\"																				   \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle																						   \n");
	tMorphScript += _T("	morphChannelWeights = #()																									   \n");
	tMorphScript += _T("	for channelIndex = 1 to 100 do																								   \n");
	tMorphScript += _T("	(																															   \n");
	tMorphScript += _T("		if WM3_MC_HasData obj.morpher channelIndex then																			   \n");
	tMorphScript += _T("		(																														   \n");
	tMorphScript += _T("			if WM3_MC_IsValid obj.morpher channelIndex and WM3_MC_IsActive obj.morpher channelIndex then						   \n");
	tMorphScript += _T("			(																													   \n");
	tMorphScript += _T("				morphChannelWeight = WM3_MC_GetValue  obj.morpher channelIndex													   \n");
	tMorphScript += _T("				append morphChannelWeights morphChannelWeight																	   \n");
	tMorphScript += _T("			)																													   \n");
	tMorphScript += _T("		)																														   \n");
	tMorphScript += _T("	)																															   \n");
	tMorphScript += _T("	morphChannelWeights																											   \n");
	tMorphScript += _T(")																																   \n");
	tMorphScript += _T("fn Simplygon_SetMorphTarget nodeHandle geometryHandle channelIndex =															   \n");
	tMorphScript += _T("( 																																   \n");
	// tMorphScript += _T("	print \"\nSimplygon_SetMorphTarget()\"																    					   \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle																						   \n");
	tMorphScript += _T("	target = maxOps.getNodeByHandle geometryHandle 																				   \n");
	tMorphScript += _T("	WM3_MC_BuildFromNode obj.morpher channelIndex target							   											   \n");
	tMorphScript += _T(")																																   \n");
	tMorphScript += _T("fn Simplygon_SetMorpChannelWeights nodeHandle channelIndex weight =																   \n");
	tMorphScript += _T("(																																   \n");
	// tMorphScript += _T("	print \"\nSimplygon_SetMorpChannelWeights()\"																				   \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle																						   \n");
	tMorphScript += _T("    if WM3_MC_IsValid obj.morpher channelIndex then																				   \n");
	tMorphScript += _T("	( 																															   \n");
	tMorphScript += _T("		WM3_MC_SetValue obj.morpher channelIndex weight 																		   \n");
	tMorphScript += _T("	)																															   \n");
	tMorphScript += _T(")			   																													   \n");
	tMorphScript += _T("fn Simplygon_AddProgressiveMorphTarget nodeHandle channelIndex geometryHandle =													   \n");
	tMorphScript += _T("( 																																   \n");
	// tMorphScript += _T("	print \"\nSimplygon_SetProgressiveMorphTarget()\" 																			   \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle 																					   \n");
	tMorphScript += _T("	target = maxOps.getNodeByHandle geometryHandle																				   \n");
	tMorphScript += _T("    if WM3_MC_HasData obj.morpher channelIndex then																				   \n");
	tMorphScript += _T("	(																															   \n");
	tMorphScript += _T("        if WM3_MC_IsValid obj.morpher channelIndex then																			   \n");
	tMorphScript += _T("		( 																														   \n");
	tMorphScript += _T("			WM3_AddProgressiveMorphNode obj.morpher channelIndex target 														   \n");
	tMorphScript += _T("		) 																														   \n");
	tMorphScript += _T("	) 																															   \n");
	tMorphScript += _T(")																																   \n");
	tMorphScript += _T("fn Simplygon_GetMorphPoints nodeHandle channelIndex =																			   \n");
	tMorphScript += _T("(																																   \n");
	// tMorphScript += _T("	print \"\nSimplygon_GetMorphPoints()\"																						   \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle																						   \n");
	tMorphScript += _T("																																   \n");
	tMorphScript += _T("	morphPoints = #()																											   \n");
	tMorphScript += _T("	if WM3_MC_HasData obj.morpher channelIndex and WM3_MC_IsValid obj.morpher channelIndex then									   \n");
	tMorphScript += _T("	(																															   \n");
	tMorphScript += _T("		numVerts = WM3_MC_NumPts obj.morpher channelIndex																		   \n");
	tMorphScript += _T("		morphPoints.count = numVerts																							   \n");
	tMorphScript += _T("		for v = 1 to numVerts do																								   \n");
	tMorphScript += _T("		(																														   \n");
	tMorphScript += _T("			morphPoints[v] = WM3_MC_GetMorphPoint obj.morpher channelIndex (v - 1)												   \n");
	tMorphScript += _T("		)																														   \n");
	tMorphScript += _T("	)																															   \n");
	tMorphScript += _T("	morphPoints																												       \n");
	tMorphScript += _T(")																																   \n");
	tMorphScript += _T("fn Simplygon_GetChannelName nodeHandle channelIndex =																			   \n");
	tMorphScript += _T("(																																   \n");
	// tMorphScript += _T("	print \"\nSimplygon_GetChannelName()\" 																						   \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle																						   \n");
	tMorphScript += _T("	name = undefined 																											   \n");
	tMorphScript += _T("	if WM3_MC_HasData obj.morpher channelIndex and WM3_MC_IsValid obj.morpher channelIndex then									   \n");
	tMorphScript += _T("	(																															   \n");
	tMorphScript += _T("		name = WM3_MC_GetName obj.morpher channelIndex																			   \n");
	tMorphScript += _T("	) 																															   \n");
	tMorphScript += _T("	name 																														   \n");
	tMorphScript += _T(")																																   \n");
	tMorphScript += _T("fn Simplygon_GetChannelUseVertexSelection nodeHandle channelIndex =                                   							   \n");
	tMorphScript += _T("(                                                                                                     							   \n");
	// tMorphScript += _T( "	print \"\nSimplygon_GetChannelUseVertexSelection() \" \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle                                                           							   \n");
	tMorphScript += _T("                                                                                                      							   \n");
	tMorphScript += _T("	bUseVertexSelection = False                                                                       							   \n");
	tMorphScript += _T("	if WM3_MC_HasData obj.morpher channelIndex and WM3_MC_IsValid obj.morpher channelIndex then       							   \n");
	tMorphScript += _T("	(                                                                                                 							   \n");
	tMorphScript += _T("		bUseVertexSelection = WM3_MC_GetUseVertexSel obj.morpher channelIndex                         							   \n");
	tMorphScript += _T("	)                                                                                                 							   \n");
	tMorphScript += _T("	bUseVertexSelection                                                                               							   \n");
	tMorphScript += _T(")                                                                                                     							   \n");
	tMorphScript += _T("fn Simplygon_SetChannelUseVertexSelection nodeHandle channelIndex bUseVertexSelection =               							   \n");
	tMorphScript += _T("(                                                                                                     							   \n");
	// tMorphScript += _T( "	print \"\nSimplygon_SetChannelUseVertexSelection() \" \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle                                                           							   \n");
	tMorphScript += _T("                                                                                                      							   \n");
	tMorphScript += _T("	if WM3_MC_HasData obj.morpher channelIndex and WM3_MC_IsValid obj.morpher channelIndex then       							   \n");
	tMorphScript += _T("	(                                                                                                 							   \n");
	tMorphScript += _T("		WM3_MC_SetUseVertexSel obj.morpher channelIndex bUseVertexSelection                           							   \n");
	tMorphScript += _T("	)                                                                                                 							   \n");
	tMorphScript += _T(")                                                                                                     							   \n");
	tMorphScript += _T("fn Simplygon_GetChannelMinLimit nodeHandle channelIndex =                                             							   \n");
	tMorphScript += _T("(                                                                                                     							   \n");
	// tMorphScript += _T( "	print \"\nSimplygon_GetChannelMinLimit() \" \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle                                                           							   \n");
	tMorphScript += _T("                                                                                                      							   \n");
	tMorphScript += _T("	minLimit = 0.0                                                                                    							   \n");
	tMorphScript += _T("	if WM3_MC_HasData obj.morpher channelIndex and WM3_MC_IsValid obj.morpher channelIndex then       							   \n");
	tMorphScript += _T("	(                                                                                                 							   \n");
	tMorphScript += _T("		minLimit = WM3_MC_GetLimitMIN obj.morpher channelIndex                                        							   \n");
	tMorphScript += _T("	)                                                                                                 							   \n");
	tMorphScript += _T("	minLimit                                                                                          							   \n");
	tMorphScript += _T(")                                                                                                     							   \n");
	tMorphScript += _T("fn Simplygon_SetChannelMinLimit nodeHandle channelIndex minLimit =                                    							   \n");
	tMorphScript += _T("(                                                                                                     							   \n");
	// tMorphScript += _T( "	print \"\nSimplygon_SetChannelMinLimit() \" \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle                                                           							   \n");
	tMorphScript += _T("                                                                                                      							   \n");
	tMorphScript += _T("	if WM3_MC_HasData obj.morpher channelIndex and WM3_MC_IsValid obj.morpher channelIndex then       							   \n");
	tMorphScript += _T("	(                                                                                                 							   \n");
	tMorphScript += _T("		WM3_MC_SetLimitMIN obj.morpher channelIndex minLimit                                          							   \n");
	tMorphScript += _T("	)                                                                                                 							   \n");
	tMorphScript += _T(")                                                                                                     							   \n");
	tMorphScript += _T("fn Simplygon_GetChannelMaxLimit nodeHandle channelIndex =                                             							   \n");
	tMorphScript += _T("(                                                                                                     							   \n");
	// tMorphScript += _T( "	print \"\nSimplygon_GetChannelMaxLimit() \" \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle                                                           							   \n");
	tMorphScript += _T("                                                                                                      							   \n");
	tMorphScript += _T("	maxLimit = 0.0                                                                                    							   \n");
	tMorphScript += _T("	if WM3_MC_HasData obj.morpher channelIndex and WM3_MC_IsValid obj.morpher channelIndex then       							   \n");
	tMorphScript += _T("	(                                                                                                 							   \n");
	tMorphScript += _T("		maxLimit = WM3_MC_GetLimitMAX obj.morpher channelIndex                                        							   \n");
	tMorphScript += _T("	)                                                                                                 							   \n");
	tMorphScript += _T("	maxLimit                                                                                          							   \n");
	tMorphScript += _T(")                                                                                                     							   \n");
	tMorphScript += _T("fn Simplygon_SetChannelMaxLimit nodeHandle channelIndex maxLimit =                                    							   \n");
	tMorphScript += _T("(                                                                                                     							   \n");
	// tMorphScript += _T( "	print \"\nSimplygon_SetChannelMaxLimit() \" \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle                                                           							   \n");
	tMorphScript += _T("                                                                                                      							   \n");
	tMorphScript += _T("	if WM3_MC_HasData obj.morpher channelIndex and WM3_MC_IsValid obj.morpher channelIndex then       							   \n");
	tMorphScript += _T("	(                                                                                                 							   \n");
	tMorphScript += _T("		WM3_MC_SetLimitMAX obj.morpher channelIndex maxLimit                                          							   \n");
	tMorphScript += _T("	)                                                                                                 							   \n");
	tMorphScript += _T(")                                                                                                     							   \n");
	tMorphScript += _T("fn Simplygon_GetChannelUseLimits nodeHandle channelIndex =                                            							   \n");
	tMorphScript += _T("(                                                                                                     							   \n");
	// tMorphScript += _T( "	print \"\nSimplygon_GetChannelUseLimits() \" \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle                                                           							   \n");
	tMorphScript += _T("                                                                                                      							   \n");
	tMorphScript += _T("	bUseLimits = False                                                                                							   \n");
	tMorphScript += _T("	if WM3_MC_HasData obj.morpher channelIndex and WM3_MC_IsValid obj.morpher channelIndex then       							   \n");
	tMorphScript += _T("	(                                                                                                 							   \n");
	tMorphScript += _T("		bUseLimits = WM3_MC_GetUseLimits obj.morpher channelIndex                                     							   \n");
	tMorphScript += _T("	)                                                                                                 							   \n");
	tMorphScript += _T("	bUseLimits                                                                                        							   \n");
	tMorphScript += _T(")                                                                                                     							   \n");
	tMorphScript += _T("fn Simplygon_SetChannelUseLimits nodeHandle channelIndex bUseLimits =                                 							   \n");
	tMorphScript += _T("(                                                                                                     							   \n");
	// tMorphScript += _T( "	print \"\nSimplygon_SetChannelUseLimits() \" \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle                                                           							   \n");
	tMorphScript += _T("                                                                                                      							   \n");
	tMorphScript += _T("	if WM3_MC_HasData obj.morpher channelIndex and WM3_MC_IsValid obj.morpher channelIndex then       							   \n");
	tMorphScript += _T("	(                                                                                                 							   \n");
	tMorphScript += _T("		WM3_MC_SetUseLimits obj.morpher channelIndex bUseLimits                                       							   \n");
	tMorphScript += _T("	)                                                                                                 							   \n");
	tMorphScript += _T(")                                                                                                     							   \n");
	tMorphScript += _T("fn Simplygon_GetUseVertexSelections nodeHandle =                                                            					   \n");
	tMorphScript += _T("(                                                                                                           					   \n");
	// tMorphScript += _T( "	print \"\nSimplygon_GetUseVertexSelections() \" \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle                                                                 					   \n");
	tMorphScript += _T("	bUseVertexSelectionArray = #()                                                                          					   \n");
	tMorphScript += _T("	for channelIndex = 1 to 100 do                                                                          					   \n");
	tMorphScript += _T("	(                                                                                                       					   \n");
	tMorphScript += _T("		if WM3_MC_HasData obj.morpher channelIndex then                                                     					   \n");
	tMorphScript += _T("		(                                                                                                   					   \n");
	tMorphScript += _T("			if WM3_MC_IsValid obj.morpher channelIndex and WM3_MC_IsActive obj.morpher channelIndex then    					   \n");
	tMorphScript += _T("			(                                                                                               					   \n");
	tMorphScript += _T("				bUseVertexSelection = WM3_MC_GetUseVertexSel  obj.morpher channelIndex                      					   \n");
	tMorphScript += _T("				append bUseVertexSelectionArray bUseVertexSelection                                         					   \n");
	tMorphScript += _T("			)                                                                                               					   \n");
	tMorphScript += _T("		)                                                                                                   					   \n");
	tMorphScript += _T("	)                                                                                                       					   \n");
	tMorphScript += _T("	bUseVertexSelectionArray                                                                                					   \n");
	tMorphScript += _T(")                                                                                                           					   \n");
	tMorphScript += _T("fn Simplygon_GetMinLimits nodeHandle =                                                                      					   \n");
	tMorphScript += _T("(                                                                                                           					   \n");
	// tMorphScript += _T("	print \"\nSimplygon_GetMinLimits() \"                                                                   					   \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle                                                                 					   \n");
	tMorphScript += _T("	minLimitsArray = #()                                                                                    					   \n");
	tMorphScript += _T("	for channelIndex = 1 to 100 do                                                                          					   \n");
	tMorphScript += _T("	(                                                                                                       					   \n");
	tMorphScript += _T("		if WM3_MC_HasData obj.morpher channelIndex then                                                     					   \n");
	tMorphScript += _T("		(                                                                                                   					   \n");
	tMorphScript += _T("			if WM3_MC_IsValid obj.morpher channelIndex and WM3_MC_IsActive obj.morpher channelIndex then    					   \n");
	tMorphScript += _T("			(                                                                                               					   \n");
	tMorphScript += _T("				minLimit = WM3_MC_GetLimitMIN obj.morpher channelIndex                                      					   \n");
	tMorphScript += _T("				append minLimitsArray minLimit                                                              					   \n");
	tMorphScript += _T("			)                                                                                               					   \n");
	tMorphScript += _T("		)                                                                                                   					   \n");
	tMorphScript += _T("	)                                                                                                       					   \n");
	tMorphScript += _T("	minLimitsArray                                                                                          					   \n");
	tMorphScript += _T(")                                                                                                           					   \n");
	tMorphScript += _T("fn Simplygon_GetMaxLimits nodeHandle =                                                                      					   \n");
	tMorphScript += _T("(                                                                                                           					   \n");
	// tMorphScript += _T( "	print \"\nSimplygon_GetMaxLimits() \" \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle                                                                 					   \n");
	tMorphScript += _T("	maxLimitsArray = #()                                                                                    					   \n");
	tMorphScript += _T("	for channelIndex = 1 to 100 do                                                                          					   \n");
	tMorphScript += _T("	(                                                                                                       					   \n");
	tMorphScript += _T("		if WM3_MC_HasData obj.morpher channelIndex then                                                     					   \n");
	tMorphScript += _T("		(                                                                                                   					   \n");
	tMorphScript += _T("			if WM3_MC_IsValid obj.morpher channelIndex and WM3_MC_IsActive obj.morpher channelIndex then    					   \n");
	tMorphScript += _T("			(                                                                                               					   \n");
	tMorphScript += _T("				maxLimit = WM3_MC_GetLimitMAX obj.morpher channelIndex                                      					   \n");
	tMorphScript += _T("				append maxLimitsArray maxLimit                                                              					   \n");
	tMorphScript += _T("			)                                                                                               					   \n");
	tMorphScript += _T("		)                                                                                                   					   \n");
	tMorphScript += _T("	)                                                                                                       					   \n");
	tMorphScript += _T("	maxLimitsArray                                                                                          					   \n");
	tMorphScript += _T(")                                                                                                           					   \n");
	tMorphScript += _T("fn Simplygon_GetUseLimits nodeHandle =                                                                      					   \n");
	tMorphScript += _T("(                                                                                                           					   \n");
	// tMorphScript += _T( "	print \"\nSimplygon_GetUseLimits() \" \n");
	tMorphScript += _T("	obj = maxOps.getNodeByHandle nodeHandle                                                                 					   \n");
	tMorphScript += _T("	bUseLimitsArray = #()                                                                                   					   \n");
	tMorphScript += _T("	for channelIndex = 1 to 100 do                                                                          					   \n");
	tMorphScript += _T("	(                                                                                                       					   \n");
	tMorphScript += _T("		if WM3_MC_HasData obj.morpher channelIndex then                                                     					   \n");
	tMorphScript += _T("		(                                                                                                   					   \n");
	tMorphScript += _T("			if WM3_MC_IsValid obj.morpher channelIndex and WM3_MC_IsActive obj.morpher channelIndex then    					   \n");
	tMorphScript += _T("			(                                                                                               					   \n");
	tMorphScript += _T("				bUseLimits = WM3_MC_GetUseLimits  obj.morpher channelIndex                                  					   \n");
	tMorphScript += _T("				append bUseLimitsArray bUseLimits                                                           					   \n");
	tMorphScript += _T("			)                                                                                               					   \n");
	tMorphScript += _T("		)                                                                                                   					   \n");
	tMorphScript += _T("	)                                                                                                       					   \n");
	tMorphScript += _T("	bUseLimitsArray                                                                                         					   \n");
	tMorphScript += _T(")                                                                                                           					   \n");

	BOOL bQuietErrors = FALSE;
	const TSTR tRegisterMorphFunctionScript( tMorphScript.c_str() );

// register script
#if MAX_VERSION_MAJOR < 24
	bMorphScriptInitialized = ExecuteMAXScriptScript( tRegisterMorphFunctionScript.data(), bQuietErrors );
#else
	bMorphScriptInitialized = ExecuteMAXScriptScript( tRegisterMorphFunctionScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors );
#endif
}

void SimplygonMax::GetActiveMorphChannels( ulong uniqueHandle, MorpherChannelSettings* morpherSettings )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_stprintf_s( tExecuteMorphTargetQueryScript, MAX_PATH, _T("Simplygon_GetActiveMorphChannels %u"), uniqueHandle );

	const TSTR tGetActiveMorphChannelsScript( tExecuteMorphTargetQueryScript );
	FPValue mActiveMorphTargetList;

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tGetActiveMorphChannelsScript.data(), bQuietErrors, &mActiveMorphTargetList );
#else
	const BOOL bExecutedMorphTargetScript =
	    ExecuteMAXScriptScript( tGetActiveMorphChannelsScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors, &mActiveMorphTargetList );
#endif

	if( !bExecutedMorphTargetScript )
		return;

	// if result is of correct type, loop content
	if( mActiveMorphTargetList.type == TYPE_INT_TAB )
	{
		morpherSettings->channels.clear();
		morpherSettings->channels.resize( mActiveMorphTargetList.i_tab->Count() );

		for( int i = 0; i < mActiveMorphTargetList.i_tab->Count(); ++i )
		{
			const int index = ( *mActiveMorphTargetList.i_tab )[ i ];
			morpherSettings->channels[ i ] = new MorphChannelMetaData( index - 1, index );
		}
	}
	else
	{
		morpherSettings->channels.resize( 0 );
	}
}

void SimplygonMax::GetMorphChannelWeights( ulong uniqueHandle, std::vector<float>& mActiveMorphChannelWeights )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_stprintf_s( tExecuteMorphTargetQueryScript, MAX_PATH, _T("Simplygon_GetMorpChannelWeights %u"), uniqueHandle );

	const TSTR tGetActiveMorphChannelWeightsScript( tExecuteMorphTargetQueryScript );
	FPValue mActiveMorphTargetWeightsList;

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tGetActiveMorphChannelWeightsScript.data(), bQuietErrors, &mActiveMorphTargetWeightsList );
#else
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript(
	    tGetActiveMorphChannelWeightsScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors, &mActiveMorphTargetWeightsList );
#endif

	if( !bExecutedMorphTargetScript )
		return;

	// if result is of correct type, loop content
	if( mActiveMorphTargetWeightsList.type == TYPE_FLOAT_TAB )
	{
		mActiveMorphChannelWeights.resize( mActiveMorphTargetWeightsList.f_tab->Count() );

		for( int i = 0; i < mActiveMorphTargetWeightsList.f_tab->Count(); ++i )
		{
			const float weight = ( *mActiveMorphTargetWeightsList.f_tab )[ i ];
			mActiveMorphChannelWeights[ i ] = weight;
		}
	}
	else
	{
		mActiveMorphChannelWeights.resize( 0 );
	}
}

void SimplygonMax::GetMorphChannelPoints( ulong uniqueHandle, std::vector<Point3>& mMorphChannelPoints, size_t channelIndex )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_stprintf_s( tExecuteMorphTargetQueryScript, MAX_PATH, _T("Simplygon_GetMorphPoints %u %zu"), uniqueHandle, channelIndex );

	const TSTR tGetActiveMorphChannelPointsScript( tExecuteMorphTargetQueryScript );
	FPValue mActiveMorphTargetPointsList;

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tGetActiveMorphChannelPointsScript.data(), bQuietErrors, &mActiveMorphTargetPointsList );
#else
	const BOOL bExecutedMorphTargetScript =
	    ExecuteMAXScriptScript( tGetActiveMorphChannelPointsScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors, &mActiveMorphTargetPointsList );
#endif

	if( !bExecutedMorphTargetScript )
		return;

	// if result is of correct type, loop content
	if( mActiveMorphTargetPointsList.type == TYPE_POINT3_TAB_BV )
	{
		mMorphChannelPoints.resize( mActiveMorphTargetPointsList.p_tab->Count() );

		for( int i = 0; i < mActiveMorphTargetPointsList.p_tab->Count(); ++i )
		{
			const Point3* mPoint = ( *mActiveMorphTargetPointsList.p_tab )[ i ];
			mMorphChannelPoints[ i ] = *mPoint;
		}
	}
	else
	{
		mMorphChannelPoints.resize( 0 );
	}
}

bool SimplygonMax::GetMorphChannelName( ulong uniqueHandle, size_t channelIndex, std::basic_string<TCHAR>& name )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_stprintf_s( tExecuteMorphTargetQueryScript, MAX_PATH, _T("Simplygon_GetChannelName %u %zu"), uniqueHandle, channelIndex );

	const TSTR tGetActiveMorphChannelNameScript( tExecuteMorphTargetQueryScript );
	FPValue mActiveChannelName;

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tGetActiveMorphChannelNameScript.data(), bQuietErrors, &mActiveChannelName );
#else
	const BOOL bExecutedMorphTargetScript =
	    ExecuteMAXScriptScript( tGetActiveMorphChannelNameScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors, &mActiveChannelName );
#endif

	if( !bExecutedMorphTargetScript )
		return false;

	// if result is of correct type, loop content
	if( mActiveChannelName.type == TYPE_STRING )
	{
		name = mActiveChannelName.s;
		return true;
	}

	return false;
}

void SimplygonMax::GetActiveMorphTargetProgressiveWeights( ulong uniqueHandle, size_t channelIndex, std::vector<float>& mActiveProgressiveWeights )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_stprintf_s( tExecuteMorphTargetQueryScript, MAX_PATH, _T("Simplygon_GetProgressiveWeights %u %zu"), uniqueHandle, channelIndex );

	const TSTR tGetActiveProgressiveWeightsScript( tExecuteMorphTargetQueryScript );
	FPValue mActiveMorphTargetTensionsList;

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tGetActiveProgressiveWeightsScript.data(), bQuietErrors, &mActiveMorphTargetTensionsList );
#else
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript(
	    tGetActiveProgressiveWeightsScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors, &mActiveMorphTargetTensionsList );
#endif

	if( !bExecutedMorphTargetScript )
		return;

	// if result is of correct type, loop content
	if( mActiveMorphTargetTensionsList.type == TYPE_FLOAT_TAB )
	{
		mActiveProgressiveWeights.resize( mActiveMorphTargetTensionsList.f_tab->Count() );

		for( int i = 0; i < mActiveMorphTargetTensionsList.f_tab->Count(); ++i )
		{
			const float tension = ( *mActiveMorphTargetTensionsList.f_tab )[ i ];
			mActiveProgressiveWeights[ i ] = tension;
		}
	}
	else
	{
		mActiveProgressiveWeights.resize( 0 );
	}
}

void SimplygonMax::GetActiveMorphTargetTension( ulong uniqueHandle, MorpherChannelSettings* morpherSettings )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_stprintf_s( tExecuteMorphTargetQueryScript, MAX_PATH, _T("Simplygon_GetMorphTensions %u"), uniqueHandle );

	const TSTR tGetActiveMorphChannelTensionScript( tExecuteMorphTargetQueryScript );
	FPValue mActiveMorphTargetTensionsList;

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tGetActiveMorphChannelTensionScript.data(), bQuietErrors, &mActiveMorphTargetTensionsList );
#else
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript(
	    tGetActiveMorphChannelTensionScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors, &mActiveMorphTargetTensionsList );
#endif

	if( !bExecutedMorphTargetScript )
		return;

	// if result is of correct type, loop content
	if( mActiveMorphTargetTensionsList.type == TYPE_FLOAT_TAB )
	{
		for( int i = 0; i < mActiveMorphTargetTensionsList.f_tab->Count(); ++i )
		{
			const float tension = ( *mActiveMorphTargetTensionsList.f_tab )[ i ];
			morpherSettings->channels[ i ]->tension = tension;
		}
	}
}

void SimplygonMax::SetMorphChannelTension( ulong uniqueHandle, size_t channelIndex, float tension )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_sntprintf_l(
	    tExecuteMorphTargetQueryScript, MAX_PATH, _T("Simplygon_SetMorphTension %u %zu %f"), this->MaxScriptLocale, uniqueHandle, channelIndex, tension );

	const TSTR tSetMorphTensionScript( tExecuteMorphTargetQueryScript );

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tSetMorphTensionScript.data(), bQuietErrors );
#else
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tSetMorphTensionScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors );
#endif
}

void SimplygonMax::SetMorphTarget( ulong uniqueHandle, ulong uniqueTargetHandle, size_t channelIndex )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_stprintf_s( tExecuteMorphTargetQueryScript, MAX_PATH, _T("Simplygon_SetMorphTarget %u %u %zu"), uniqueHandle, uniqueTargetHandle, channelIndex );

	const TSTR tSetMorphTargetScript( tExecuteMorphTargetQueryScript );

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tSetMorphTargetScript.data(), bQuietErrors );
#else
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tSetMorphTargetScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors );
#endif
}

void SimplygonMax::SetMorphChannelWeight( ulong uniqueHandle, size_t channelIndex, float weight )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_sntprintf_l(
	    tExecuteMorphTargetQueryScript, MAX_PATH, _T("Simplygon_SetMorpChannelWeights %u %zu %f"), this->MaxScriptLocale, uniqueHandle, channelIndex, weight );

	const TSTR tSetMorphChannelWeightScript( tExecuteMorphTargetQueryScript );

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tSetMorphChannelWeightScript.data(), bQuietErrors );
#else
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tSetMorphChannelWeightScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors );
#endif
}

void SimplygonMax::AddProgressiveMorphTarget( ulong uniqueHandle, ulong uniqueTargetHandle, size_t channelIndex )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_stprintf_s(
	    tExecuteMorphTargetQueryScript, MAX_PATH, _T("Simplygon_AddProgressiveMorphTarget %u %zu %u"), uniqueHandle, channelIndex, uniqueTargetHandle );

	const TSTR tSetMorphTargetScript( tExecuteMorphTargetQueryScript );

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tSetMorphTargetScript.data(), bQuietErrors );
#else
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tSetMorphTargetScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors );
#endif
}

void SimplygonMax::SetProgressiveMorphTargetWeight( ulong uniqueHandle, size_t channelIndex, size_t progressiveIndex, float weight )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_sntprintf_l( tExecuteMorphTargetQueryScript,
	              MAX_PATH,
	              _T("Simplygon_SetProgressiveWeights %u %zu %zu %f"),
	              this->MaxScriptLocale,
	              uniqueHandle,
	              channelIndex,
	              progressiveIndex,
	              weight );

	const TSTR tSetMorphTargetScript( tExecuteMorphTargetQueryScript );

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tSetMorphTargetScript.data(), bQuietErrors );
#else
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tSetMorphTargetScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors );
#endif
}

void SimplygonMax::GetActiveUseVertexSelections( ulong uniqueHandle, MorpherChannelSettings* morpherSettings )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_stprintf_s( tExecuteMorphTargetQueryScript, MAX_PATH, _T("Simplygon_GetUseVertexSelections %u"), uniqueHandle );

	const TSTR tGetActiveVertexSelectionScript( tExecuteMorphTargetQueryScript );
	FPValue mActiveVertexSelectionList;

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tGetActiveVertexSelectionScript.data(), bQuietErrors, &mActiveVertexSelectionList );
#else
	const BOOL bExecutedMorphTargetScript =
	    ExecuteMAXScriptScript( tGetActiveVertexSelectionScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors, &mActiveVertexSelectionList );
#endif

	if( !bExecutedMorphTargetScript )
		return;

	// if result is of correct type, loop content
	if( mActiveVertexSelectionList.type == TYPE_BOOL_TAB )
	{
		for( int i = 0; i < mActiveVertexSelectionList.b_tab->Count(); ++i )
		{
			const bool bVertexSelection = ( *mActiveVertexSelectionList.b_tab )[ i ];
			morpherSettings->channels.at( i )->useVertexSelection = bVertexSelection;
		}
	}
}

void SimplygonMax::SetChannelUseVertexSelection( ulong uniqueHandle, size_t channelIndex, bool bUseVertexSelection )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_stprintf_s( tExecuteMorphTargetQueryScript,
	             MAX_PATH,
	             _T("Simplygon_SetChannelUseVertexSelection %u %zu %s"),
	             uniqueHandle,
	             channelIndex,
	             bUseVertexSelection ? _T("True") : _T ("False") );

	const TSTR tSetMorphTargetScript( tExecuteMorphTargetQueryScript );

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tSetMorphTargetScript.data(), bQuietErrors );
#else
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tSetMorphTargetScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors );
#endif
}

void SimplygonMax::GetActiveMinLimits( ulong uniqueHandle, MorpherChannelSettings* morpherSettings )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_stprintf_s( tExecuteMorphTargetQueryScript, MAX_PATH, _T("Simplygon_GetMinLimits %u"), uniqueHandle );

	const TSTR tGetActiveMorphChannelMinLimitsScript( tExecuteMorphTargetQueryScript );
	FPValue mActiveMorphTargetMinLimitList;

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript =
	    ExecuteMAXScriptScript( tGetActiveMorphChannelMinLimitsScript.data(), bQuietErrors, &mActiveMorphTargetMinLimitList );
#else
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript(
	    tGetActiveMorphChannelMinLimitsScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors, &mActiveMorphTargetMinLimitList );
#endif

	if( !bExecutedMorphTargetScript )
		return;

	// if result is of correct type, loop content
	if( mActiveMorphTargetMinLimitList.type == TYPE_FLOAT_TAB )
	{
		for( int i = 0; i < mActiveMorphTargetMinLimitList.f_tab->Count(); ++i )
		{
			const float minLimit = ( *mActiveMorphTargetMinLimitList.f_tab )[ i ];
			morpherSettings->channels.at( i )->minLimit = minLimit;
		}
	}
}

void SimplygonMax::SetChannelMinLimit( ulong uniqueHandle, size_t channelIndex, float minLimit )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_sntprintf_l(
	    tExecuteMorphTargetQueryScript, MAX_PATH, _T("Simplygon_SetChannelMinLimit %u %zu %f"), this->MaxScriptLocale, uniqueHandle, channelIndex, minLimit );

	const TSTR tSetMorphTargetScript( tExecuteMorphTargetQueryScript );

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tSetMorphTargetScript.data(), bQuietErrors );
#else
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tSetMorphTargetScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors );
#endif
}

void SimplygonMax::GetActiveMaxLimits( ulong uniqueHandle, MorpherChannelSettings* morpherSettings )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_stprintf_s( tExecuteMorphTargetQueryScript, MAX_PATH, _T("Simplygon_GetMaxLimits %u"), uniqueHandle );

	const TSTR tGetActiveMorphChannelMaxLimitsScript( tExecuteMorphTargetQueryScript );
	FPValue mActiveMorphTargetMaxLimitList;

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript =
	    ExecuteMAXScriptScript( tGetActiveMorphChannelMaxLimitsScript.data(), bQuietErrors, &mActiveMorphTargetMaxLimitList );
#else
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript(
	    tGetActiveMorphChannelMaxLimitsScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors, &mActiveMorphTargetMaxLimitList );
#endif

	if( !bExecutedMorphTargetScript )
		return;

	// if result is of correct type, loop content
	if( mActiveMorphTargetMaxLimitList.type == TYPE_FLOAT_TAB )
	{
		for( int i = 0; i < mActiveMorphTargetMaxLimitList.f_tab->Count(); ++i )
		{
			const float maxLimit = ( *mActiveMorphTargetMaxLimitList.f_tab )[ i ];
			morpherSettings->channels.at( i )->maxLimit = maxLimit;
		}
	}
}

void SimplygonMax::SetChannelMaxLimit( ulong uniqueHandle, size_t channelIndex, float maxLimit )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_sntprintf_l(
	    tExecuteMorphTargetQueryScript, MAX_PATH, _T("Simplygon_SetChannelMaxLimit %u %zu %f"), this->MaxScriptLocale, uniqueHandle, channelIndex, maxLimit );

	const TSTR tSetMorphTargetScript( tExecuteMorphTargetQueryScript );

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tSetMorphTargetScript.data(), bQuietErrors );
#else
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tSetMorphTargetScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors );
#endif
}

void SimplygonMax::GetActiveUseLimits( ulong uniqueHandle, MorpherChannelSettings* morpherSettings )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_stprintf_s( tExecuteMorphTargetQueryScript, MAX_PATH, _T("Simplygon_GetUseLimits %u"), uniqueHandle );

	const TSTR tGetActiveUseLimitsScript( tExecuteMorphTargetQueryScript );
	FPValue mActiveUseLimitsList;

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tGetActiveUseLimitsScript.data(), bQuietErrors, &mActiveUseLimitsList );
#else
	const BOOL bExecutedMorphTargetScript =
	    ExecuteMAXScriptScript( tGetActiveUseLimitsScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors, &mActiveUseLimitsList );
#endif

	if( !bExecutedMorphTargetScript )
		return;

	// if result is of correct type, loop content
	if( mActiveUseLimitsList.type == TYPE_BOOL_TAB )
	{
		for( int i = 0; i < mActiveUseLimitsList.b_tab->Count(); ++i )
		{
			const bool bUseLimit = ( *mActiveUseLimitsList.b_tab )[ i ];
			morpherSettings->channels.at( i )->useLimits = bUseLimit;
		}
	}
}

void SimplygonMax::SetChannelUseLimits( ulong uniqueHandle, size_t channelIndex, bool bUseLimits )
{
	// construct morph target query script
	TCHAR tExecuteMorphTargetQueryScript[ MAX_PATH ] = { 0 };
	_stprintf_s( tExecuteMorphTargetQueryScript,
	             MAX_PATH,
	             _T("Simplygon_SetChannelUseLimits %u %zu %s"),
	             uniqueHandle,
	             channelIndex,
	             bUseLimits ? _T("True") : _T ("False") );

	const TSTR tSetMorphTargetScript( tExecuteMorphTargetQueryScript );

	BOOL bQuietErrors = FALSE;

	// execute morph target query script
#if MAX_VERSION_MAJOR < 24
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tSetMorphTargetScript.data(), bQuietErrors );
#else
	const BOOL bExecutedMorphTargetScript = ExecuteMAXScriptScript( tSetMorphTargetScript.data(), MAXScript::ScriptSource::NotSpecified, bQuietErrors );
#endif
}

#pragma endregion

// Creates Simplygon geometry from Max geometry
bool SimplygonMax::ExtractGeometry( size_t meshIndex )
{
	// fetch max and sg meshes
	MeshNode* meshNode = this->SelectedMeshNodes[ meshIndex ];

	INode* mMaxNode = meshNode->MaxNode;
	spSceneMesh sgMesh = meshNode->sgMesh;

	spGeometryData sgMeshData = sg->CreateGeometryData();
	sgMesh->SetGeometry( sgMeshData );

	this->LogToWindow( std::basic_string<TCHAR>( _T("Extracting node: ") ) + mMaxNode->GetName() );

	// skinning modifiers
	Object* mMaxObject = mMaxNode->GetObjectRef();

	// check if the object has a skinning modifier
	// start by checking if it is a derived object
	if( mMaxObject != nullptr && mMaxObject->SuperClassID() == GEN_DERIVOB_CLASS_ID )
	{
		IDerivedObject* mMaxDerivedObject = static_cast<IDerivedObject*>( mMaxObject );

		// derived object, look through the modifier list for a skinning modifier
		for( int modifierIndex = 0; modifierIndex < mMaxDerivedObject->NumModifiers(); ++modifierIndex )
		{
			Modifier* mModifier = mMaxDerivedObject->GetModifier( modifierIndex );
			if( mModifier != nullptr && mModifier->ClassID() == SKIN_CLASSID )
			{
				meshNode->SkinModifiers = mModifier;
				break;
			}
		}

		// derived object, look through the modifier list for a morph modifier
		for( int modifierIndex = 0; modifierIndex < mMaxDerivedObject->NumModifiers(); ++modifierIndex )
		{
			Modifier* mModifier = mMaxDerivedObject->GetModifier( modifierIndex );
			if( mModifier != nullptr && mModifier->ClassID() == MORPHER_CLASS_ID && mModifier->IsEnabled() )
			{
				RegisterMorphScripts();
				meshNode->MorphTargetModifier = mModifier;
				break;
			}
		}
	}

	// if there is a morph modifier, temporarily disable if enabled, and store current state
	BOOL bMorphTargetModifier = FALSE;
	if( meshNode->MorphTargetModifier != nullptr )
	{
		bMorphTargetModifier = meshNode->MorphTargetModifier->IsEnabled();
		meshNode->MorphTargetModifier->DisableMod();

		meshNode->MorphTargetData = new MorpherWrapper( meshNode->MorphTargetModifier, mMaxNode, this->CurrentTime );
	}

	// if there is a skinning modifier, temporarily disable if enabled, and store current state
	BOOL bSkinModifierEnabled = FALSE;
	if( meshNode->SkinModifiers != nullptr )
	{
		bSkinModifierEnabled = meshNode->SkinModifiers->IsEnabled();
		meshNode->SkinModifiers->DisableMod();
	}

	ObjectState mMaxNodeObjectState = mMaxNode->EvalWorldState( this->CurrentTime );
	TriObject* mMaxTriObject = (TriObject*)SafeConvertToType( mMaxNodeObjectState, this->CurrentTime, triObjectClassID );
	if( !mMaxTriObject )
		return false;

	meshNode->Objects = mMaxNodeObjectState.obj;
	meshNode->TriObjects = mMaxTriObject;

	// extract mesh data
	Mesh& mMaxMesh = mMaxTriObject->GetMesh();

	const uint vertexCount = mMaxMesh.numVerts;
	const uint triangleCount = mMaxMesh.numFaces;
	const uint cornerCount = triangleCount * 3;

	sgMeshData->SetVertexCount( vertexCount );
	sgMeshData->SetTriangleCount( triangleCount );

	// copy vertex locks
	if( this->LockSelectedVertices )
	{
		spBoolArray sgVertexLocks = sgMeshData->GetVertexLocks();
		if( sgVertexLocks.IsNull() )
		{
			sgMeshData->AddVertexLocks();
			sgVertexLocks = sgMeshData->GetVertexLocks();
		}

		for( uint vid = 0; vid < vertexCount; ++vid )
		{
#if MAX_VERSION_MAJOR >= 26
			sgVertexLocks->SetItem( vid, mMaxMesh.VertSel()[ vid ] > 0 );
#else
			sgVertexLocks->SetItem( vid, mMaxMesh.vertSel[ vid ] > 0 );
#endif
		}
	}

	// coords
	spRealArray sgCoords = sgMeshData->GetCoords();
	for( uint vid = 0; vid < vertexCount; ++vid )
	{
		const Point3& mCoord = mMaxMesh.getVert( vid );
		const float coord[ 3 ] = { mCoord.x, mCoord.y, mCoord.z };
		sgCoords->SetTuple( vid, coord );
	}

	// triangle indices
	spRidArray sgVertexIds = sgMeshData->GetVertexIds();
	for( uint tid = 0; tid < triangleCount; ++tid )
	{
		for( uint c = 0; c < 3; ++c )
		{
			const uint cid = tid * 3 + c;
			const uint tIndex = mMaxMesh.faces[ tid ].v[ c ];

			sgVertexIds->SetItem( cid, tIndex );
		}
	}

	// extract mapping channels (vertex colors and UVs)
	this->ExtractMapping( meshIndex, mMaxMesh );

	// shading groups
	spUnsignedIntArray sgShadingGroups = spUnsignedIntArray::SafeCast( sgMeshData->GetUserTriangleField( "ShadingGroupIds" ) );
	if( sgShadingGroups.IsNull() )
	{
		sgShadingGroups = spUnsignedIntArray::SafeCast( sgMeshData->AddBaseTypeUserTriangleField( EBaseTypes::TYPES_ID_UINT, "ShadingGroupIds", 1 ) );
	}

	for( uint tid = 0; tid < triangleCount; ++tid )
	{
		sgShadingGroups->SetItem( tid, mMaxMesh.faces[ tid ].smGroup );
	}

	// add material to material map
	const MaxMaterialMap* materialMap = this->AddMaterial( mMaxNode->GetMtl(), sgMeshData );
	if( materialMap != nullptr && materialMap->NumActiveMaterials > 0 )
	{
		spRidArray sgMaterialIds = sgMeshData->GetMaterialIds();
		spRidArray sgParentMaterialIds;

		if( sgMaterialIds.IsNull() )
		{
			sgMeshData->AddMaterialIds();
			sgMaterialIds = sgMeshData->GetMaterialIds();
		}

		for( uint tid = 0; tid < triangleCount; ++tid )
		{
			const int mid = materialMap->GetSimplygonMaterialId( mMaxMesh.getFaceMtlIndex( tid ) );
			sgMaterialIds->SetItem( tid, mid );
		}
	}

	// skinning
	if( meshNode->SkinModifiers != nullptr )
	{
		this->LogToWindow( _T("Setting up skinning data...") );

		ISkin* mSkin = (ISkin*)meshNode->SkinModifiers->GetInterface( I_SKIN );
		ISkinContextData* mSkinContextData = mSkin->GetContextInterface( mMaxNode );

		// first pass decides if there is a dummy node in the root
		const uint numBones = mSkin->GetNumBones();

		if( numBones > 0 )
		{
			bool bHasExcessiveNodesInRoot = true;
			for( uint boneIndex = 0; boneIndex < numBones; ++boneIndex )
			{
				INode* mBoneNode = mSkin->GetBone( boneIndex );

				if( mBoneNode->IsRootNode() )
				{
					bHasExcessiveNodesInRoot = false;
					break;
				}

				INode* mParentNode = mBoneNode->GetParentNode();
				if( mParentNode->IsRootNode() )
				{
					bHasExcessiveNodesInRoot = false;
					break;
				}
			}

			// second pass
			for( uint boneIndex = 0; boneIndex < numBones; ++boneIndex )
			{
				INode* mBoneNode = mSkin->GetBone( boneIndex );

				if( bHasExcessiveNodesInRoot )
				{
					while( mBoneNode->GetParentNode()->GetParentNode()->IsRootNode() ==
					       0 ) // Traverse upwards in the node tree (the traverse will stop when child of root is reached)
					{
						const int sgBoneIndex = this->AddBone( mBoneNode );
						mBoneNode = mBoneNode->GetParentNode();
					}
				}
				else
				{
					while( mBoneNode->GetParentNode()->IsRootNode() ==
					       0 ) // Traverse upwards in the node tree (the traverse will stop when child of root is reached)
					{
						const int sgBoneIndex = this->AddBone( mBoneNode );
						mBoneNode = mBoneNode->GetParentNode();
					}
				}
			}

			// count the maximum bones used by any vertex
			uint maxBonesPerVertex = 1;
			for( uint vid = 0; vid < vertexCount; ++vid )
			{
				const int numBonesForThisVertex = mSkinContextData->GetNumAssignedBones( vid ); // number of bones affecting the vertex
				if( (uint)numBonesForThisVertex > maxBonesPerVertex )
				{
					maxBonesPerVertex = (uint)numBonesForThisVertex;
				}
			}

			// lower bones per vertex count if lower than specified value, otherwise use previous limit
			if( !( maxBonesPerVertex < this->MaxNumBonesPerVertex ) )
			{
				maxBonesPerVertex = this->MaxNumBonesPerVertex;
			}

			sgMeshData->AddBoneWeights( maxBonesPerVertex );

			spRidArray sgBoneIds = sgMeshData->GetBoneIds();
			spRealArray sgBoneWeights = sgMeshData->GetBoneWeights();

			int* boneIds = new int[ maxBonesPerVertex ];
			float* boneWeights = new float[ maxBonesPerVertex ];

			// get the data, place into array
			for( uint vid = 0; vid < vertexCount; ++vid )
			{
				const int numAssignedBones = mSkinContextData->GetNumAssignedBones( vid ); // number of bones affecting the vertex
				skinning_bone_set vtx_bones;

				// get all bones
				int* srcBoneIds = new int[ numAssignedBones ];
				float* srcBoneWeights = new float[ numAssignedBones ];
				for( int b = 0; b < numAssignedBones; ++b )
				{
					const int bIndex = mSkinContextData->GetAssignedBone( vid, b ); // Get the index for one of the bones affecting vertex v
					if( bIndex == -1 )
						continue;

					srcBoneIds[ b ] = this->AddBone( mSkin->GetBone( bIndex ) );
					srcBoneWeights[ b ] = mSkinContextData->GetBoneWeight( vid, b );
				}

				// extract the most important bones
				uint boneIndex = 0;
				for( ; boneIndex < maxBonesPerVertex; ++boneIndex )
				{
					// look through the list, find the largest weight value
					int largestIndex = -1;
					float largestWeight = 0;
					bool largestFound = false;
					for( int b = 0; b < numAssignedBones; ++b )
					{
						if( srcBoneWeights[ b ] > largestWeight )
						{
							largestFound = true;
							largestIndex = b;
							largestWeight = srcBoneWeights[ b ];
						}
					}

					if( !largestFound )
						break;

					// add into tuple
					boneIds[ boneIndex ] = srcBoneIds[ largestIndex ];
					boneWeights[ boneIndex ] = srcBoneWeights[ largestIndex ];

					// mark as used
					srcBoneIds[ largestIndex ] = -1;
					srcBoneWeights[ largestIndex ] = float( -1 );
				}

				delete[] srcBoneIds;
				delete[] srcBoneWeights;

				// reset the rest of the tuple
				for( ; boneIndex < maxBonesPerVertex; ++boneIndex )
				{
					boneIds[ boneIndex ] = -1;
					boneWeights[ boneIndex ] = 0.f;
				}

				// apply to field
				sgBoneIds->SetTuple( vid, boneIds );
				sgBoneWeights->SetTuple( vid, boneWeights );
			}

			delete[] boneIds;
			delete[] boneWeights;
		}
	}

	// normals
	spRealArray sgNormals = sgMeshData->GetNormals();
	if( sgNormals.IsNull() )
	{
		sgMeshData->AddNormals();
		sgNormals = sgMeshData->GetNormals();
	}

	// compute normals
	ComputeVertexNormals( sgMeshData );

	// copy explicit normals
	MeshNormalSpec* mMeshNormals = mMaxMesh.GetSpecifiedNormals();
	if( mMeshNormals )
	{
		// for each face
		for( uint tid = 0; tid < triangleCount; ++tid )
		{
			// for each corner
			for( uint c = 0; c < 3; ++c )
			{
				const uint cid = tid * 3 + c;

				// get normal index
				const int normalIndex = mMeshNormals->GetNormalIndex( tid, c );

				// ignore if invalid
				if( normalIndex < 0 || normalIndex >= mMeshNormals->GetNumNormals() )
					continue;

				// is the normal explicit and valid?
				if( mMeshNormals->GetNormalExplicit( normalIndex ) && sgVertexIds->GetItem( cid ) >= 0 )
				{
					const Point3& mNormal = mMeshNormals->Normal( normalIndex );
					const float normal[ 3 ] = { mNormal.x, mNormal.y, mNormal.z };
					sgNormals->SetTuple( cid, normal );
				}
			}
		}
	}

	// morph targets
	if( meshNode->MorphTargetData )
	{
		const ulong uniqueHandle = mMaxNode->GetHandle();
		const spString rSgMeshId = sgMesh->GetNodeGUID();
		const char* cSgMeshId = rSgMeshId.c_str();

		const std::map<std::string, GlobalMeshMap>::iterator& meshMap = this->GlobalGuidToMaxNodeMap.find( cSgMeshId );

		MorpherMetaData* morpherMetaData = meshMap->second.CreateMorpherMetaData();
		morpherMetaData->globalSettings = meshNode->MorphTargetData->globalSettings;

		std::vector<MorphChannelMetaData*>& morphTargetMetaData = morpherMetaData->morphTargetMetaData;

		TCHAR tTargetVertexFieldName[ MAX_PATH ] = { 0 };
		for( size_t activeChannelIndex = 0; activeChannelIndex < meshNode->MorphTargetData->NumChannels(); ++activeChannelIndex )
		{
			MorphChannel* morphChannel = meshNode->MorphTargetData->GetChannel( activeChannelIndex );
			if( morphChannel && morphChannel->IsValid() )
			{
				const int morphChannelIndex = morphChannel->GetIndex() - 1;

				MorphChannelMetaData* morphChannelMetaData = morphChannel->GetSettings();

				for( size_t progressiveIndex = 0; progressiveIndex < morphChannel->NumProgressiveMorphTargets(); ++progressiveIndex )
				{
					ProgressiveMorphTarget* progressiveMorphTarget = morphChannel->GetProgressiveMorphTarget( progressiveIndex );

					_stprintf_s( tTargetVertexFieldName, MAX_PATH, _T("%s%u_%zu"), _T("BlendShapeTargetVertexField"), morphChannelIndex, progressiveIndex );
					const char* cTargetVertexFieldName = LPCTSTRToConstCharPtr( tTargetVertexFieldName );

					spRealArray sgMorphTargetDeltas =
					    spRealArray::SafeCast( sgMeshData->AddBaseTypeUserVertexField( Simplygon::EBaseTypes::TYPES_ID_REAL, cTargetVertexFieldName, 3 ) );

					const TSTR tMorphTargetName = morphChannel->GetName();
					const char* cMorphTargetName = LPCTSTRToConstCharPtr( tMorphTargetName );
					sgMorphTargetDeltas->SetAlternativeName( cMorphTargetName );

					std::vector<Point3>& morphTargetVertices = progressiveMorphTarget->targetDeltas;
					for( int vid = 0; vid < morphChannel->GetVertexCount(); ++vid )
					{
						const Point3& mCoord = morphTargetVertices[ vid ];
						const float coord[ 3 ] = { mCoord.x, mCoord.y, mCoord.z };
						sgMorphTargetDeltas->SetTuple( vid, coord );
					}

					morphChannelMetaData->AddProgressiveMorphTarget(
					    progressiveIndex, std::basic_string<TCHAR>( tMorphTargetName ), progressiveMorphTarget->targetWeight );
				}

				morphTargetMetaData.push_back( morphChannelMetaData );
			}
		}
	}

	// loop through the map of selected sets
	this->LogToWindow( _T("Loop through selection sets...") );

	// selection sets
	this->AddToObjectSelectionSet( mMaxNode );
	this->AddEdgeCollapse( mMaxNode, sgMeshData );

	// re-enable morph in the original geometry
	if( bMorphTargetModifier )
	{
		meshNode->MorphTargetModifier->EnableMod();
	}

	// re-enable skinning in the original geometry
	if( bSkinModifierEnabled )
	{
		meshNode->SkinModifiers->EnableMod();
	}

	if( bMorphTargetModifier || bSkinModifierEnabled )
	{
		mMaxNode->EvalWorldState( this->CurrentTime );
	}

	return true;
}

// Creates Simplygon geometry from Max geometry
bool SimplygonMax::ExtractGeometry_Quad( size_t meshIndex )
{
	// fetch max and sg meshes
	MeshNode* meshNode = this->SelectedMeshNodes[ meshIndex ];

	INode* mMaxNode = meshNode->MaxNode;
	spSceneMesh sgMesh = meshNode->sgMesh;

	spGeometryData sgMeshData = sg->CreateGeometryData();
	sgMesh->SetGeometry( sgMeshData );
	sgMeshData->AddQuadFlags();
	spCharArray sgQuadFlags = sgMeshData->GetQuadFlags();

	this->LogToWindow( std::basic_string<TCHAR>( _T("Extracting node: ") ) + mMaxNode->GetName() );

	// skinning modifiers
	Object* mMaxObject = mMaxNode->GetObjectRef();

	// check if the object has a skinning modifier
	// start by checking if it is a derived object
	if( mMaxObject != nullptr && mMaxObject->SuperClassID() == GEN_DERIVOB_CLASS_ID )
	{
		IDerivedObject* mMaxDerivedObject = static_cast<IDerivedObject*>( mMaxObject );

		// derived object, look through the modifier list for a skinning modifier
		for( int modifierIndex = 0; modifierIndex < mMaxDerivedObject->NumModifiers(); ++modifierIndex )
		{
			Modifier* mModifier = mMaxDerivedObject->GetModifier( modifierIndex );
			if( mModifier != nullptr && mModifier->ClassID() == SKIN_CLASSID )
			{
				meshNode->SkinModifiers = mModifier;
				break;
			}
		}

		// derived object, look through the modifier list for a morph modifier
		for( int modifierIndex = 0; modifierIndex < mMaxDerivedObject->NumModifiers(); ++modifierIndex )
		{
			Modifier* mModifier = mMaxDerivedObject->GetModifier( modifierIndex );
			if( mModifier != nullptr && mModifier->ClassID() == MORPHER_CLASS_ID && mModifier->IsEnabled() )
			{
				RegisterMorphScripts();
				meshNode->MorphTargetModifier = mModifier;
				break;
			}
		}

		for( int modifierIndex = 0; modifierIndex < mMaxDerivedObject->NumModifiers(); ++modifierIndex )
		{
			Modifier* mModifier = mMaxDerivedObject->GetModifier( modifierIndex );
			if( mModifier != nullptr && mModifier->ClassID() == TURBOSMOOTH_CLASS_ID && mModifier->IsEnabled() )
			{
				meshNode->TurboSmoothModifier = mModifier;
				break;
			}
		}
	}

	// if there is a morph modifier, temporarily disable if enabled, and store current state
	BOOL bMorphTargetModifier = FALSE;
	if( meshNode->MorphTargetModifier != nullptr )
	{
		bMorphTargetModifier = meshNode->MorphTargetModifier->IsEnabled();
		meshNode->MorphTargetModifier->DisableMod();

		meshNode->MorphTargetData = new MorpherWrapper( meshNode->MorphTargetModifier, mMaxNode, this->CurrentTime );
	}

	// if there is a skinning modifier, temporarily disable if enabled, and store current state
	BOOL bSkinModifierEnabled = FALSE;
	if( meshNode->SkinModifiers != nullptr )
	{
		bSkinModifierEnabled = meshNode->SkinModifiers->IsEnabled();
		meshNode->SkinModifiers->DisableMod();
	}

	BOOL bTurboSmoothModifierEnabled = FALSE;
	if( meshNode->TurboSmoothModifier != nullptr )
	{
		bTurboSmoothModifierEnabled = meshNode->TurboSmoothModifier->IsEnabled();
		meshNode->TurboSmoothModifier->DisableMod();
	}

	ObjectState mMaxNodeObjectState = mMaxNode->EvalWorldState( this->CurrentTime );
	PolyObject* mMaxPolyObject = (PolyObject*)SafeConvertToType( mMaxNodeObjectState, this->CurrentTime, polyObjectClassID );
	if( !mMaxPolyObject )
		return false;

	meshNode->Objects = mMaxNodeObjectState.obj;
	meshNode->PolyObjects = mMaxPolyObject;

	// extract mesh data
	MNMesh& mMaxMesh = mMaxPolyObject->GetMesh();

	const uint vertexCount = mMaxMesh.VNum();
	const uint polygonCount = mMaxMesh.FNum();
	const uint triangleCount = mMaxMesh.TriNum();

	sgMeshData->SetVertexCount( vertexCount );
	sgMeshData->SetTriangleCount( triangleCount );

	uint32_t currentQuadFlagTriangleIndex = 0;
	uint32_t currentVertexIndex = 0;

	spRidArray sgVertexIds = sgMeshData->GetVertexIds();
	spRealArray sgCoords = sgMeshData->GetCoords();
	for( uint vid = 0; vid < vertexCount; ++vid )
	{
		const MNVert& mCoord = mMaxMesh.v[ vid ];
		const float coord[ 3 ] = { mCoord.p.x, mCoord.p.y, mCoord.p.z };
		sgCoords->SetTuple( vid, coord );
	}

	std::vector<Triangulator::vec3> sgGLMVertices;
	SetVectorFromArray<Triangulator::vec3, 3>( sgGLMVertices, sgCoords );

	std::vector<Triangulator::vec3> sgTexCoords;
	sgTexCoords.resize( vertexCount );

	std::vector<Triangulator::Triangle> sgGlobalPolygonTriangles;
	sgGlobalPolygonTriangles.reserve( triangleCount );

	std::vector<Triangulator::Triangle> sgLocalPolygonTriangles;
	const Triangulator& sgTriangulator = Triangulator( sgGLMVertices.data(), vertexCount );

	uint sgPolygonIndex = 0;
	for( uint32_t polygonIndex = 0; polygonIndex < polygonCount; ++polygonIndex )
	{
		const int deg = mMaxMesh.F( polygonIndex )->deg;
		const bool bIsQuad = deg == 4;
		sgLocalPolygonTriangles.resize( deg - 2 );

		const uint* indexArray = reinterpret_cast<const uint*>( mMaxMesh.F( polygonIndex )->vtx );
		const bool triangulationFailed = !sgTriangulator.TriangulatePolygon( sgLocalPolygonTriangles.data(), indexArray, deg );

		if( triangulationFailed )
		{
			++numBadTriangulations;
		}

		for( int i = 0; i < sgLocalPolygonTriangles.size(); ++i, ++sgPolygonIndex )
		{
			auto localTriangle = sgLocalPolygonTriangles[ i ];
			const char cQuadFlagToken = bIsQuad ? ( i == 0 ? SG_QUADFLAG_FIRST : SG_QUADFLAG_SECOND ) : SG_QUADFLAG_TRIANGLE;
			sgQuadFlags->SetItem( sgPolygonIndex, cQuadFlagToken );

			for( uint c = 0; c < 3; ++c )
			{
				int cid = sgPolygonIndex * 3 + c;
				int localIndex = localTriangle.c[ c ];
				sgVertexIds->SetItem( cid, indexArray[ localIndex ] );
			}

			sgGlobalPolygonTriangles.emplace_back( localTriangle );
		}

		for( int i = 0; i < deg; ++i )
		{
			int index = mMaxMesh.F( polygonIndex )->vtx[ i ];
			Point3& vertex = mMaxMesh.v[ index ].p;
			sgTexCoords[ index ] = glm::vec3( vertex.x, vertex.y, vertex.z );
		}
	}

	std::vector<uint32_t> mPolygonIndexToTriangleIndex;
	mPolygonIndexToTriangleIndex.resize( polygonCount );

	std::vector<uint32_t> mNumPolygonTriangles;
	mNumPolygonTriangles.resize( polygonCount );

	uint triCount = 0;
	for( uint polygonIndex = 0; polygonIndex < polygonCount; ++polygonIndex )
	{
		IntTab triangles;
		mMaxMesh.F( polygonIndex )->GetTriangles( triangles );
		uint32_t numTri = triangles.Count() / 3;

		mPolygonIndexToTriangleIndex[ polygonIndex ] = triCount;

		mNumPolygonTriangles[ polygonIndex ] = numTri;
		triCount += numTri;
	}

	// copy vertex locks
	if( this->LockSelectedVertices )
	{
		spBoolArray sgVertexLocks = sgMeshData->GetVertexLocks();
		if( sgVertexLocks.IsNull() )
		{
			sgMeshData->AddVertexLocks();
			sgVertexLocks = sgMeshData->GetVertexLocks();
		}

		for( uint vid = 0; vid < vertexCount; ++vid )
		{
			BitArray selectedBitArray;
			mMaxMesh.getVertexSel( selectedBitArray );
			sgVertexLocks->SetItem( vid, selectedBitArray[ vid ] > 0 );
		}
	}

	// extract mapping channels (vertex colors and UVs)
	this->ExtractMapping_Quad( meshIndex, mMaxMesh, sgGlobalPolygonTriangles, mPolygonIndexToTriangleIndex, mNumPolygonTriangles );

	// shading groups
	spUnsignedIntArray sgShadingGroups = spUnsignedIntArray::SafeCast( sgMeshData->GetUserTriangleField( "ShadingGroupIds" ) );
	if( sgShadingGroups.IsNull() )
	{
		sgShadingGroups = spUnsignedIntArray::SafeCast( sgMeshData->AddBaseTypeUserTriangleField( EBaseTypes::TYPES_ID_UINT, "ShadingGroupIds", 1 ) );
	}

	for( uint polyIndex = 0; polyIndex < mPolygonIndexToTriangleIndex.size(); ++polyIndex )
	{
		uint startTriangleIndex = mPolygonIndexToTriangleIndex[ polyIndex ];
		uint countTriangleIndex = mNumPolygonTriangles[ polyIndex ];
		auto smoothingGroup = mMaxMesh.F( polyIndex )->smGroup;

		for( uint i = startTriangleIndex; i < startTriangleIndex + countTriangleIndex; ++i )
		{
			sgShadingGroups->SetItem( i, smoothingGroup );
		}
	}

	// add material to material map
	const MaxMaterialMap* materialMap = this->AddMaterial( mMaxNode->GetMtl(), sgMeshData );
	if( materialMap != nullptr && materialMap->NumActiveMaterials > 0 )
	{
		spRidArray sgMaterialIds = sgMeshData->GetMaterialIds();
		spRidArray sgParentMaterialIds;

		if( sgMaterialIds.IsNull() )
		{
			sgMeshData->AddMaterialIds();
			sgMaterialIds = sgMeshData->GetMaterialIds();
		}

		for( uint polyIndex = 0; polyIndex < mPolygonIndexToTriangleIndex.size(); ++polyIndex )
		{
			uint startTriangleIndex = mPolygonIndexToTriangleIndex[ polyIndex ];
			uint countTriangleIndex = mNumPolygonTriangles[ polyIndex ];

			const int mid = materialMap->GetSimplygonMaterialId( mMaxMesh.F( polyIndex )->material );
			for( uint i = startTriangleIndex; i < startTriangleIndex + countTriangleIndex; ++i )
			{
				sgMaterialIds->SetItem( i, mid );
			}
		}
	}

	// skinning
	if( meshNode->SkinModifiers != nullptr )
	{
		this->LogToWindow( _T("Setting up skinning data...") );

		ISkin* mSkin = (ISkin*)meshNode->SkinModifiers->GetInterface( I_SKIN );
		ISkinContextData* mSkinContextData = mSkin->GetContextInterface( mMaxNode );

		// first pass decides if there is a dummy node in the root
		const uint numBones = mSkin->GetNumBones();

		if( numBones > 0 )
		{
			bool bHasExcessiveNodesInRoot = true;
			for( uint boneIndex = 0; boneIndex < numBones; ++boneIndex )
			{
				INode* mBoneNode = mSkin->GetBone( boneIndex );

				if( mBoneNode->IsRootNode() )
				{
					bHasExcessiveNodesInRoot = false;
					break;
				}

				INode* mParentNode = mBoneNode->GetParentNode();
				if( mParentNode->IsRootNode() )
				{
					bHasExcessiveNodesInRoot = false;
					break;
				}
			}

			// second pass
			for( uint boneIndex = 0; boneIndex < numBones; ++boneIndex )
			{
				INode* mBoneNode = mSkin->GetBone( boneIndex );

				if( bHasExcessiveNodesInRoot )
				{
					while( mBoneNode->GetParentNode()->GetParentNode()->IsRootNode() ==
					       0 ) // Traverse upwards in the node tree (the traverse will stop when child of root is reached)
					{
						const int sgBoneIndex = this->AddBone( mBoneNode );
						mBoneNode = mBoneNode->GetParentNode();
					}
				}
				else
				{
					while( mBoneNode->GetParentNode()->IsRootNode() ==
					       0 ) // Traverse upwards in the node tree (the traverse will stop when child of root is reached)
					{
						const int sgBoneIndex = this->AddBone( mBoneNode );
						mBoneNode = mBoneNode->GetParentNode();
					}
				}
			}

			// count the maximum bones used by any vertex
			uint maxBonesPerVertex = 1;
			for( uint vid = 0; vid < vertexCount; ++vid )
			{
				const int numBonesForThisVertex = mSkinContextData->GetNumAssignedBones( vid ); // number of bones affecting the vertex
				if( (uint)numBonesForThisVertex > maxBonesPerVertex )
				{
					maxBonesPerVertex = (uint)numBonesForThisVertex;
				}
			}

			// lower bones per vertex count if lower than specified value, otherwise use previous limit
			if( !( maxBonesPerVertex < this->MaxNumBonesPerVertex ) )
			{
				maxBonesPerVertex = this->MaxNumBonesPerVertex;
			}

			sgMeshData->AddBoneWeights( maxBonesPerVertex );

			spRidArray sgBoneIds = sgMeshData->GetBoneIds();
			spRealArray sgBoneWeights = sgMeshData->GetBoneWeights();

			int* boneIds = new int[ maxBonesPerVertex ];
			float* boneWeights = new float[ maxBonesPerVertex ];

			// get the data, place into array
			for( uint vid = 0; vid < vertexCount; ++vid )
			{
				const int numAssignedBones = mSkinContextData->GetNumAssignedBones( vid ); // number of bones affecting the vertex
				skinning_bone_set vtx_bones;

				// get all bones
				int* srcBoneIds = new int[ numAssignedBones ];
				float* srcBoneWeights = new float[ numAssignedBones ];
				for( int b = 0; b < numAssignedBones; ++b )
				{
					const int bIndex = mSkinContextData->GetAssignedBone( vid, b ); // Get the index for one of the bones affecting vertex v
					if( bIndex == -1 )
						continue;

					srcBoneIds[ b ] = this->AddBone( mSkin->GetBone( bIndex ) );
					srcBoneWeights[ b ] = mSkinContextData->GetBoneWeight( vid, b );
				}

				// extract the most important bones
				uint boneIndex = 0;
				for( ; boneIndex < maxBonesPerVertex; ++boneIndex )
				{
					// look through the list, find the largest weight value
					int largestIndex = -1;
					float largestWeight = 0;
					bool largestFound = false;
					for( int b = 0; b < numAssignedBones; ++b )
					{
						if( srcBoneWeights[ b ] > largestWeight )
						{
							largestFound = true;
							largestIndex = b;
							largestWeight = srcBoneWeights[ b ];
						}
					}

					if( !largestFound )
						break;

					// add into tuple
					boneIds[ boneIndex ] = srcBoneIds[ largestIndex ];
					boneWeights[ boneIndex ] = srcBoneWeights[ largestIndex ];

					// mark as used
					srcBoneIds[ largestIndex ] = -1;
					srcBoneWeights[ largestIndex ] = float( -1 );
				}

				delete[] srcBoneIds;
				delete[] srcBoneWeights;

				// reset the rest of the tuple
				for( ; boneIndex < maxBonesPerVertex; ++boneIndex )
				{
					boneIds[ boneIndex ] = -1;
					boneWeights[ boneIndex ] = 0.f;
				}

				// apply to field
				sgBoneIds->SetTuple( vid, boneIds );
				sgBoneWeights->SetTuple( vid, boneWeights );
			}

			delete[] boneIds;
			delete[] boneWeights;
		}
	}

	// normals
	spRealArray sgNormals = sgMeshData->GetNormals();
	if( sgNormals.IsNull() )
	{
		sgMeshData->AddNormals();
		sgNormals = sgMeshData->GetNormals();
	}

	// compute normals
	ComputeVertexNormals( sgMeshData );

	// copy explicit normals
	MNNormalSpec* mMeshNormals = mMaxMesh.GetSpecifiedNormals();
	if( mMeshNormals )
	{
		uint32_t numFaces = mMeshNormals->GetNumFaces();
		MNNormalFace* faceArray = mMeshNormals->GetFaceArray();

		uint32_t sgIndex = 0;
		for( uint32_t fid = 0; fid < numFaces; ++fid )
		{
			uint startTriangleIndex = mPolygonIndexToTriangleIndex[ fid ];
			uint numTriangleIndex = mNumPolygonTriangles[ fid ];

			for( uint triIndex = startTriangleIndex; triIndex < startTriangleIndex + numTriangleIndex; ++triIndex )
			{
				Triangulator::Triangle& localTri = sgGlobalPolygonTriangles[ triIndex ];

				for( int c = 0; c < 3; ++c, ++sgIndex )
				{
					int localIndex = localTri.c[ c ];

					// get normal index
					const int normalIndex = faceArray[ fid ].GetNormalID( localIndex );

					// ignore if invalid
					if( normalIndex < 0 || normalIndex >= mMeshNormals->GetNumNormals() )
					{
						continue;
					}

					// is the normal explicit and valid?
					if( mMeshNormals->GetNormalExplicit( normalIndex ) )
					{
						const Point3& mNormal = mMeshNormals->Normal( normalIndex );
						const float normal[ 3 ] = { mNormal.x, mNormal.y, mNormal.z };
						sgNormals->SetTuple( sgIndex, normal );
					}
				}
			}
		}
	}

	// morph targets
	if( false /*meshNode->MorphTargetData*/ )
	{
		const ulong uniqueHandle = mMaxNode->GetHandle();
		const spString rSgMeshId = sgMesh->GetNodeGUID();
		const char* cSgMeshId = rSgMeshId.c_str();

		const std::map<std::string, GlobalMeshMap>::iterator& meshMap = this->GlobalGuidToMaxNodeMap.find( cSgMeshId );

		MorpherMetaData* morpherMetaData = meshMap->second.CreateMorpherMetaData();
		morpherMetaData->globalSettings = meshNode->MorphTargetData->globalSettings;

		std::vector<MorphChannelMetaData*>& morphTargetMetaData = morpherMetaData->morphTargetMetaData;

		TCHAR tTargetVertexFieldName[ MAX_PATH ] = { 0 };
		for( size_t activeChannelIndex = 0; activeChannelIndex < meshNode->MorphTargetData->NumChannels(); ++activeChannelIndex )
		{
			MorphChannel* morphChannel = meshNode->MorphTargetData->GetChannel( activeChannelIndex );
			if( morphChannel && morphChannel->IsValid() )
			{
				const int morphChannelIndex = morphChannel->GetIndex() - 1;

				MorphChannelMetaData* morphChannelMetaData = morphChannel->GetSettings();

				for( size_t progressiveIndex = 0; progressiveIndex < morphChannel->NumProgressiveMorphTargets(); ++progressiveIndex )
				{
					ProgressiveMorphTarget* progressiveMorphTarget = morphChannel->GetProgressiveMorphTarget( progressiveIndex );

					_stprintf_s( tTargetVertexFieldName, MAX_PATH, _T("%s%u_%zu"), _T("BlendShapeTargetVertexField"), morphChannelIndex, progressiveIndex );
					const char* cTargetVertexFieldName = LPCTSTRToConstCharPtr( tTargetVertexFieldName );

					spRealArray sgMorphTargetDeltas =
					    spRealArray::SafeCast( sgMeshData->AddBaseTypeUserVertexField( Simplygon::EBaseTypes::TYPES_ID_REAL, cTargetVertexFieldName, 3 ) );

					const TSTR tMorphTargetName = morphChannel->GetName();
					const char* cMorphTargetName = LPCTSTRToConstCharPtr( tMorphTargetName );
					sgMorphTargetDeltas->SetAlternativeName( cMorphTargetName );

					std::vector<Point3>& morphTargetVertices = progressiveMorphTarget->targetDeltas;
					for( int vid = 0; vid < morphChannel->GetVertexCount(); ++vid )
					{
						const Point3& mCoord = morphTargetVertices[ vid ];
						const float coord[ 3 ] = { mCoord.x, mCoord.y, mCoord.z };
						sgMorphTargetDeltas->SetTuple( vid, coord );
					}

					morphChannelMetaData->AddProgressiveMorphTarget(
					    progressiveIndex, std::basic_string<TCHAR>( tMorphTargetName ), progressiveMorphTarget->targetWeight );
				}

				morphTargetMetaData.push_back( morphChannelMetaData );
			}
		}
	}

	// loop through the map of selected sets
	this->LogToWindow( _T("Loop through selection sets...") );

	// selection sets
	this->AddToObjectSelectionSet( mMaxNode );
	// this->AddEdgeCollapse_Quad( mMaxNode, sgMeshData );

	// re-enable morph in the original geometry
	if( bMorphTargetModifier )
	{
		meshNode->MorphTargetModifier->EnableMod();
	}

	// re-enable skinning in the original geometry
	if( bSkinModifierEnabled )
	{
		meshNode->SkinModifiers->EnableMod();
	}

	if( bTurboSmoothModifierEnabled )
	{
		meshNode->TurboSmoothModifier->EnableMod();
	}

	if( bMorphTargetModifier || bSkinModifierEnabled )
	{
		mMaxNode->EvalWorldState( this->CurrentTime );
	}

	return true;
}

void SimplygonMax::AddEdgeCollapse_Quad( INode* mMaxNode, spGeometryData sgMeshData )
{
	const uint cornerCount = sgMeshData->GetTriangleCount() * 3;
	const ulong uniqueHandle = mMaxNode->GetHandle();

	TCHAR tSelectionSetNameBuffer[ MAX_PATH ] = { 0 };
	uint numSelectionSets = 0;

	std::map<std::basic_string<TCHAR>, SelectionSetEdgePair>::const_iterator& selectionSetIterator = this->SelectionSetEdgesMap.begin();
	while( !( selectionSetIterator == this->SelectionSetEdgesMap.end() ) )
	{
		const SelectionSetEdgePair& tempSelectionSetPair = selectionSetIterator->second;
		if( uniqueHandle == tempSelectionSetPair.first )
		{
			_stprintf_s( tSelectionSetNameBuffer, MAX_PATH, _T("SelectionSet%u"), numSelectionSets );
			numSelectionSets++;

			spBoolArray sgSelectedEdgeField = spBoolArray::SafeCast( sgMeshData->GetUserCornerField( LPCTSTRToConstCharPtr( tSelectionSetNameBuffer ) ) );
			if( sgSelectedEdgeField.IsNull() )
			{
				sgSelectedEdgeField = spBoolArray::SafeCast(
				    sgMeshData->AddBaseTypeUserCornerField( EBaseTypes::TYPES_ID_BOOL, LPCTSTRToConstCharPtr( tSelectionSetNameBuffer ), 1 ) );
				sgSelectedEdgeField->SetAlternativeName( LPCTSTRToConstCharPtr( selectionSetIterator->first.c_str() ) );

				for( uint c = 0; c < cornerCount; ++c )
				{
					sgSelectedEdgeField->SetItem( c, false );
				}
			}

			const std::vector<int>& edgeIndices = tempSelectionSetPair.second;
			for( uint c = 0; c < (uint)edgeIndices.size(); c++ )
			{
				sgSelectedEdgeField->SetItem( edgeIndices[ c ] - 1, true );
			}

			// if this field is named force
			TCHAR tForceCollapseNameBuffer[ MAX_PATH ] = { 0 };
			const float selectedEdgeValue = -1.0f;
			const float defaultEdgeValue = 1.0f;

			_stprintf( tForceCollapseNameBuffer, _T("ForceCollapseAlongEdge") );

			if( _tcscmp( selectionSetIterator->first.c_str(), tForceCollapseNameBuffer ) == 0 )
			{
				spRealArray sgSelectedEdgeWeights = spRealArray::SafeCast( sgMeshData->GetUserCornerField( LPCTSTRToConstCharPtr( _T("EdgeWeights") ) ) );
				if( sgSelectedEdgeWeights.IsNull() )
				{
					sgSelectedEdgeWeights = spRealArray::SafeCast(
					    sgMeshData->AddBaseTypeUserCornerField( EBaseTypes::TYPES_ID_REAL, LPCTSTRToConstCharPtr( _T("EdgeWeights") ), 1 ) );
					sgSelectedEdgeWeights->SetAlternativeName( LPCTSTRToConstCharPtr( selectionSetIterator->first.c_str() ) );
				}

				for( uint c = 0; c < cornerCount; ++c )
				{
					sgSelectedEdgeWeights->SetItem( c, defaultEdgeValue );
				}

				for( uint c = 0; c < (uint)edgeIndices.size(); ++c )
				{
					sgSelectedEdgeWeights->SetItem( edgeIndices[ c ] - 1, selectedEdgeValue );
				}
			}
		}

		// go to next set
		selectionSetIterator++;
	}
}

void SimplygonMax::AddEdgeCollapse( INode* mMaxNode, spGeometryData sgMeshData )
{
	const uint cornerCount = sgMeshData->GetTriangleCount() * 3;
	const ulong uniqueHandle = mMaxNode->GetHandle();

	TCHAR tSelectionSetNameBuffer[ MAX_PATH ] = { 0 };
	uint numSelectionSets = 0;

	std::map<std::basic_string<TCHAR>, SelectionSetEdgePair>::const_iterator& selectionSetIterator = this->SelectionSetEdgesMap.begin();
	while( !( selectionSetIterator == this->SelectionSetEdgesMap.end() ) )
	{
		const SelectionSetEdgePair& tempSelectionSetPair = selectionSetIterator->second;
		if( uniqueHandle == tempSelectionSetPair.first )
		{
			_stprintf_s( tSelectionSetNameBuffer, MAX_PATH, _T("SelectionSet%u"), numSelectionSets );
			numSelectionSets++;

			spBoolArray sgSelectedEdgeField = spBoolArray::SafeCast( sgMeshData->GetUserCornerField( LPCTSTRToConstCharPtr( tSelectionSetNameBuffer ) ) );
			if( sgSelectedEdgeField.IsNull() )
			{
				sgSelectedEdgeField = spBoolArray::SafeCast(
				    sgMeshData->AddBaseTypeUserCornerField( EBaseTypes::TYPES_ID_BOOL, LPCTSTRToConstCharPtr( tSelectionSetNameBuffer ), 1 ) );
				sgSelectedEdgeField->SetAlternativeName( LPCTSTRToConstCharPtr( selectionSetIterator->first.c_str() ) );

				for( uint c = 0; c < cornerCount; ++c )
				{
					sgSelectedEdgeField->SetItem( c, false );
				}
			}

			const std::vector<int>& edgeIndices = tempSelectionSetPair.second;
			for( uint c = 0; c < (uint)edgeIndices.size(); c++ )
			{
				sgSelectedEdgeField->SetItem( edgeIndices[ c ] - 1, true );
			}

			// if this field is named force
			TCHAR tForceCollapseNameBuffer[ MAX_PATH ] = { 0 };
			const float selectedEdgeValue = -1.0f;
			const float defaultEdgeValue = 1.0f;

			_stprintf( tForceCollapseNameBuffer, _T("ForceCollapseAlongEdge") );

			if( _tcscmp( selectionSetIterator->first.c_str(), tForceCollapseNameBuffer ) == 0 )
			{
				spRealArray sgSelectedEdgeWeights = spRealArray::SafeCast( sgMeshData->GetUserCornerField( LPCTSTRToConstCharPtr( _T("EdgeWeights") ) ) );
				if( sgSelectedEdgeWeights.IsNull() )
				{
					sgSelectedEdgeWeights = spRealArray::SafeCast(
					    sgMeshData->AddBaseTypeUserCornerField( EBaseTypes::TYPES_ID_REAL, LPCTSTRToConstCharPtr( _T("EdgeWeights") ), 1 ) );
					sgSelectedEdgeWeights->SetAlternativeName( LPCTSTRToConstCharPtr( selectionSetIterator->first.c_str() ) );
				}

				for( uint c = 0; c < cornerCount; ++c )
				{
					sgSelectedEdgeWeights->SetItem( c, defaultEdgeValue );
				}

				for( uint c = 0; c < (uint)edgeIndices.size(); ++c )
				{
					sgSelectedEdgeWeights->SetItem( edgeIndices[ c ] - 1, selectedEdgeValue );
				}
			}
		}

		// go to next set
		selectionSetIterator++;
	}
}

bool SimplygonMax::ExtractMapping_Quad( size_t meshIndex,
                                        MNMesh& mMaxMesh,
                                        std::vector<Triangulator::Triangle>& polygonTriangles,
                                        std::vector<uint32_t> mPolygonIndexToTriangleIndex,
                                        std::vector<uint32_t> mNumPolygonTriangles )
{
	TCHAR tBuffer[ MAX_PATH ] = { 0 };

	const MeshNode* meshNode = this->SelectedMeshNodes[ meshIndex ];

	spGeometryData sgMeshData = meshNode->sgMesh->GetGeometry();

	int numberOfColMaps = 0;
	int numberOfUVMaps = 0;

	// extract all channels
	int numChannels = mMaxMesh.MNum();
	for( int maxChannel = -2; maxChannel < numChannels; ++maxChannel )
	{
		if( mMaxMesh.M( maxChannel ) == nullptr )
			continue; // map not in the mesh

		// the map is in the mesh, get it and check which type it is
		const MNMap& mMaxMeshMap = *mMaxMesh.M( maxChannel );

		if( mMaxMeshMap.numv == 0 )
			continue;

		bool bIsVertexColor = false;
		bool bIsTexCoord = true;

		int sgChannel = -1;

		// mapping channel -2 is always alpha
		if( maxChannel == -2 )
		{
			bIsVertexColor = true;
			bIsTexCoord = false;
		}
		// mapping channel -1 is always illumination
		else if( maxChannel == -1 )
		{
			bIsVertexColor = true;
			bIsTexCoord = false;
		}
		// mapping channel 0 is always vertex color
		else if( maxChannel == 0 )
		{
			bIsVertexColor = true;
			bIsTexCoord = false;
		}
		// mapping channel 1 is always uv
		else if( maxChannel == 1 )
		{
			bIsVertexColor = false;
			bIsTexCoord = true;
		}
		// mapping channel 2 is ? (reserved)
		else if( maxChannel == 2 )
		{
			bIsVertexColor = false;
			bIsTexCoord = true;
		}
		else
		{
			bool bVertexColorOverrideFound = false;

			for( size_t c = 0; c < this->MaxVertexColorOverrides.size(); ++c )
			{
				if( this->MaxVertexColorOverrides[ c ] == maxChannel )
				{
					bVertexColorOverrideFound = true;
					bIsVertexColor = true;
					bIsTexCoord = false;
				}
			}
		}

		if( bIsTexCoord )
		{
			sgChannel = numberOfUVMaps;

			if( numberOfUVMaps < (int)( SG_NUM_SUPPORTED_TEXTURE_CHANNELS - 1 ) )
			{
				numberOfUVMaps++;

				// add the channel if it does not already exist
				spRealArray sgTexCoords = sgMeshData->GetTexCoords( sgChannel );
				if( sgTexCoords.IsNull() )
				{
					sgMeshData->AddTexCoords( sgChannel );
					sgTexCoords = sgMeshData->GetTexCoords( sgChannel );
				}

				// assign alternative name (for back-mapping purposes)
				_stprintf_s( tBuffer, MAX_PATH, _T("%d"), maxChannel );
				sgTexCoords->SetAlternativeName( LPCTSTRToConstCharPtr( tBuffer ) );

				// copy the uv data
				real texcoord[ 2 ] = { 0 };

				uint32_t texCoordIndex = 0;
				for( int fid = 0; fid < mMaxMeshMap.FNum(); ++fid )
				{
					MNMapFace& mMaxMapFace = *mMaxMesh.MF( maxChannel, fid );
					uint startTriangleIndex = mPolygonIndexToTriangleIndex[ fid ];
					uint numTriangleIndex = mNumPolygonTriangles[ fid ];

					for( uint triIndex = startTriangleIndex; triIndex < startTriangleIndex + numTriangleIndex; ++triIndex )
					{
						Triangulator::Triangle& localTri = polygonTriangles[ triIndex ];

						for( int c = 0; c < 3; ++c )
						{
							auto localIndex = localTri.c[ c ];

							const rid tv = mMaxMapFace.tv[ localIndex ];
							const UVVert& mMaxUVVert = mMaxMesh.MV( maxChannel, tv );

							// remap tex-coords
							if( this->TextureCoordinateRemapping == 0 )
							{
								texcoord[ 0 ] = mMaxUVVert.x;
								texcoord[ 1 ] = mMaxUVVert.y;
							}
							else if( this->TextureCoordinateRemapping == 1 )
							{
								texcoord[ 0 ] = mMaxUVVert.x;
								texcoord[ 1 ] = mMaxUVVert.z;
							}
							else if( this->TextureCoordinateRemapping == 2 )
							{
								texcoord[ 0 ] = mMaxUVVert.y;
								texcoord[ 1 ] = mMaxUVVert.z;
							}

							sgTexCoords->SetTuple( texCoordIndex++, texcoord );
						}
					}
				}
			}
		}

		// add a color set
		else if( bIsVertexColor )
		{
			sgChannel = numberOfColMaps;

			if( numberOfColMaps < (int)( SG_NUM_SUPPORTED_COLOR_CHANNELS - 1 ) )
			{
				numberOfColMaps++;

				// add the channel if it does not already exist
				spRealArray sgVertexColors = sgMeshData->GetColors( sgChannel );

				if( sgVertexColors.IsNull() )
				{
					sgMeshData->AddColors( sgChannel );
					sgVertexColors = sgMeshData->GetColors( sgChannel );
				}

				// assign alternative name (for use in Simplygon)
				_stprintf_s( tBuffer, MAX_PATH, _T("%d"), maxChannel );
				sgVertexColors->SetAlternativeName( LPCTSTRToConstCharPtr( tBuffer ) );

				// copy the data, store as RGB data
				uint32_t texCoordIndex = 0;
				for( int fid = 0; fid < mMaxMeshMap.FNum(); ++fid )
				{
					MNMapFace& mMaxMapFace = *mMaxMesh.MF( maxChannel, fid );
					uint startTriangleIndex = mPolygonIndexToTriangleIndex[ fid ];
					uint numTriangleIndex = mNumPolygonTriangles[ fid ];

					for( uint triIndex = startTriangleIndex; triIndex < startTriangleIndex + numTriangleIndex; ++triIndex )
					{
						Triangulator::Triangle& localTri = polygonTriangles[ triIndex ];

						for( int c = 0; c < 3; ++c )
						{
							auto localIndex = localTri.c[ c ];
							const rid tv = mMaxMapFace.tv[ localIndex ];

							const UVVert& mMaxUVVert = mMaxMesh.MV( maxChannel, tv );

							const real coord[ 4 ] = { mMaxUVVert.x, mMaxUVVert.y, mMaxUVVert.z, 1.0f };
							sgVertexColors->SetTuple( texCoordIndex++, coord );
						}
					}
				}
			}
		}
		else
		{
			// unspecified channel type
			// if this section is reached,
			// there is probably a bug...
		}
	}

	// done extracting mesh data
	return true;
}

// extracts mapping channels (vertex colors and UVs)
bool SimplygonMax::ExtractMapping( size_t meshIndex, Mesh& mMaxMesh )
{
	TCHAR tBuffer[ MAX_PATH ] = { 0 };

	const MeshNode* meshNode = this->SelectedMeshNodes[ meshIndex ];

	spGeometryData sgMeshData = meshNode->sgMesh->GetGeometry();

	int numberOfColMaps = 0;
	int numberOfUVMaps = 0;

	// extract all channels
	for( int maxChannel = -2; maxChannel < mMaxMesh.getNumMaps(); ++maxChannel )
	{
		if( !mMaxMesh.mapSupport( maxChannel ) )
			continue; // map not in the mesh

		// the map is in the mesh, get it and check which type it is
		const MeshMap& mMaxMeshMap = mMaxMesh.Map( maxChannel );

		bool bIsVertexColor = false;
		bool bIsTexCoord = true;

		int sgChannel = -1;

		// mapping channel -2 is always alpha
		if( maxChannel == -2 )
		{
			bIsVertexColor = true;
			bIsTexCoord = false;
		}
		// mapping channel -1 is always illumination
		else if( maxChannel == -1 )
		{
			bIsVertexColor = true;
			bIsTexCoord = false;
		}
		// mapping channel 0 is always vertex color
		else if( maxChannel == 0 )
		{
			bIsVertexColor = true;
			bIsTexCoord = false;
		}
		// mapping channel 1 is always uv
		else if( maxChannel == 1 )
		{
			bIsVertexColor = false;
			bIsTexCoord = true;
		}
		// mapping channel 2 is ? (reserved)
		else if( maxChannel == 2 )
		{
			bIsVertexColor = false;
			bIsTexCoord = true;
		}
		else
		{
			bool bVertexColorOverrideFound = false;

			for( size_t c = 0; c < this->MaxVertexColorOverrides.size(); ++c )
			{
				if( this->MaxVertexColorOverrides[ c ] == maxChannel )
				{
					bVertexColorOverrideFound = true;
					bIsVertexColor = true;
					bIsTexCoord = false;
				}
			}
		}

		if( bIsTexCoord )
		{
			sgChannel = numberOfUVMaps;

			if( numberOfUVMaps < (int)( SG_NUM_SUPPORTED_TEXTURE_CHANNELS - 1 ) )
			{
				numberOfUVMaps++;

				// add the channel if it does not already exist
				spRealArray sgTexCoords = sgMeshData->GetTexCoords( sgChannel );
				if( sgTexCoords.IsNull() )
				{
					sgMeshData->AddTexCoords( sgChannel );
					sgTexCoords = sgMeshData->GetTexCoords( sgChannel );
				}

				// assign alternative name (for back-mapping purposes)
				_stprintf_s( tBuffer, MAX_PATH, _T("%d"), maxChannel );
				sgTexCoords->SetAlternativeName( LPCTSTRToConstCharPtr( tBuffer ) );

				// copy the uv data
				real texcoord[ 2 ] = { 0 };

				for( int tid = 0; tid < mMaxMeshMap.fnum; ++tid )
				{
					for( uint c = 0; c < 3; ++c )
					{
						const rid tv = mMaxMeshMap.tf[ tid ].t[ c ];

						// remap tex-coords
						if( this->TextureCoordinateRemapping == 0 )
						{
							texcoord[ 0 ] = mMaxMeshMap.tv[ tv ].x;
							texcoord[ 1 ] = mMaxMeshMap.tv[ tv ].y;
						}
						else if( this->TextureCoordinateRemapping == 1 )
						{
							texcoord[ 0 ] = mMaxMeshMap.tv[ tv ].x;
							texcoord[ 1 ] = mMaxMeshMap.tv[ tv ].z;
						}
						else if( this->TextureCoordinateRemapping == 2 )
						{
							texcoord[ 0 ] = mMaxMeshMap.tv[ tv ].y;
							texcoord[ 1 ] = mMaxMeshMap.tv[ tv ].z;
						}

						sgTexCoords->SetTuple( tid * 3 + c, texcoord );
					}
				}
			}
		}

		// add a color set
		else if( bIsVertexColor )
		{
			sgChannel = numberOfColMaps;

			if( numberOfColMaps < (int)( SG_NUM_SUPPORTED_COLOR_CHANNELS - 1 ) )
			{
				numberOfColMaps++;

				// add the channel if it does not already exist
				spRealArray sgVertexColors = sgMeshData->GetColors( sgChannel );

				if( sgVertexColors.IsNull() )
				{
					sgMeshData->AddColors( sgChannel );
					sgVertexColors = sgMeshData->GetColors( sgChannel );
				}

				// assign alternative name (for use in Simplygon)
				_stprintf_s( tBuffer, MAX_PATH, _T("%d"), maxChannel );
				sgVertexColors->SetAlternativeName( LPCTSTRToConstCharPtr( tBuffer ) );

				// copy the data, store as RGB data
				for( int tid = 0; tid < mMaxMeshMap.fnum; ++tid )
				{
					for( uint c = 0; c < 3; ++c )
					{
						const rid tv = mMaxMeshMap.tf[ tid ].t[ c ];
						const real coord[ 4 ] = { mMaxMeshMap.tv[ tv ].x, mMaxMeshMap.tv[ tv ].y, mMaxMeshMap.tv[ tv ].z, 1.0f };
						sgVertexColors->SetTuple( tid * 3 + c, coord );
					}
				}
			}
		}
		else
		{
			// unspecified channel type
			// if this section is reached,
			// there is probably a bug...
		}
	}

	// done extracting mesh data
	return true;
}

void CollectSceneMeshes( spSceneNode sgNode, std::vector<spSceneMesh>& sgSceneMeshes )
{
	const uint numChildNodes = sgNode->GetChildCount();
	for( uint childIndex = 0; childIndex < numChildNodes; ++childIndex )
	{
		spSceneNode sgSceneNode = sgNode->GetChild( childIndex );

		// check if this is a mesh
		spSceneMesh sgMeshNode = Simplygon::spSceneMesh::SafeCast( sgSceneNode );
		if( !sgMeshNode.IsNull() )
		{
			sgSceneMeshes.push_back( sgMeshNode );
		}

		// look into the node as well
		CollectSceneMeshes( sgSceneNode, sgSceneMeshes );
	}
}

bool SimplygonMax::ImportProcessedScenes()
{
	// only use this fallback on user request (sgsdk_AllowUnsafeImport()),
	// may result in unexpected behavior if used incorrectly.
	const bool bAllowFallbackToSceneMapping = this->AllowUnsafeImport ? this->GlobalExportedMaterialMap.size() == 0 : false;

	std::vector<spScene>& sgScenes = this->SceneHandler->sgProcessedScenes;

	// early out
	if( sgScenes.size() == 0 )
		return false;

	// create the new, modified, mesh data objects
	const size_t numProcessedGeometries = sgScenes.size();
	for( size_t physicalLodIndex = 0; physicalLodIndex < numProcessedGeometries; ++physicalLodIndex )
	{
		const size_t logicalLodIndex = physicalLodIndex + this->initialLodIndex;

		// load the processed scene from file
		spScene sgProcessedScene = sgScenes[ physicalLodIndex ];

		// if there isn't any in-memory material mapping data,
		// and AllowUnsafeImport is true, fetch mapping from scene.
		if( bAllowFallbackToSceneMapping )
		{
			ReadMaterialMappingAttribute( sgProcessedScene );
		}

		std::vector<spSceneMesh> sgProcessedMeshes;
		const spSceneNode sgRootNode = sgProcessedScene->GetRootNode();
		CollectSceneMeshes( sgRootNode, sgProcessedMeshes );

		// import meshes
		std::map<std::string, INode*> meshNodesThatNeedsParents;
		for( size_t meshIndex = 0; meshIndex < sgProcessedMeshes.size(); ++meshIndex )
		{
			spSceneMesh sgProcessedMesh = sgProcessedMeshes[ meshIndex ];

			const spString rNodeGuid = sgProcessedMesh->GetNodeGUID();

			bool bWriteBackSucceeded = false;
			if( QuadMode == true )
			{
				bWriteBackSucceeded = this->WritebackGeometry_Quad( sgProcessedScene, logicalLodIndex, sgProcessedMesh, meshNodesThatNeedsParents );
			}
			else
			{
				bWriteBackSucceeded = this->WritebackGeometry( sgProcessedScene, logicalLodIndex, sgProcessedMesh, meshNodesThatNeedsParents );
			}

			if( !bWriteBackSucceeded )
			{
				return false;
			}
		}

		// for unmapped meshes, copy transformation and link parent(s)
		for( std::map<std::string, INode*>::const_iterator& meshIterator = meshNodesThatNeedsParents.begin(); meshIterator != meshNodesThatNeedsParents.end();
		     meshIterator++ )
		{
			spSceneNode sgMesh = sgProcessedScene->GetNodeByGUID( meshIterator->first.c_str() );
			if( sgMesh.IsNull() )
				continue;

			// fetch processed parent mesh
			spSceneNode sgParent = sgMesh->GetParent();
			if( sgParent.IsNull() )
				continue;

			// fetch mesh map for parent mesh
			std::map<std::string, INode*>::const_iterator& parentMeshMap = meshNodesThatNeedsParents.find( sgParent->GetNodeGUID().c_str() );
			if( parentMeshMap == meshNodesThatNeedsParents.end() )
				continue;

			INode* mMaxNode = meshIterator->second;
			INode* mMaxParentNode = parentMeshMap->second;

			// only link to root if no parent,
			// link unmapped parents when all meshes are imported.
			spString rRootNodeName = sgRootNode->GetName();
			const char* cRootNodeName = rRootNodeName.c_str();

			spSceneNode sgParentNode = sgMesh->GetParent();
			spString rParentNodeName = sgParentNode->GetName();
			const char* cParentNodeName = rParentNodeName.c_str();

			const bool bPossibleMaxRootNode = std::string( rParentNodeName.c_str() ) == std::string( "Scene Root" );

			if( sgMesh->GetParent().IsNull() || bPossibleMaxRootNode )
			{
				INode* mMaxRootNode = this->MaxInterface->GetRootNode();
				mMaxRootNode->AttachChild( mMaxNode );
			}
			else
			{
				mMaxParentNode->AttachChild( mMaxNode );
			}
		}
	}

	return true;
}

TSTR SimplygonMax::GetNonCollidingMeshName( TSTR tNodeName )
{
	if( !this->MaxInterface->GetINodeByName( tNodeName ) )
		return tNodeName;

	const size_t lodIndex = 1;

	// create a lod name (lodname001)
	std::basic_ostringstream<TCHAR, std::char_traits<TCHAR>, std::allocator<TCHAR>> tLodNameStream;
	tLodNameStream << tNodeName << _T("_") << std::setfill<TCHAR>( '0' ) << std::setw( 3 ) << lodIndex;
	std::basic_string<TCHAR> tLodName = tLodNameStream.str();

	TSTR tTempLodName = TSTR( tLodName.c_str() );

	// see if it exists
	const bool bNodeExists = ( this->MaxInterface->GetINodeByName( tTempLodName ) != nullptr );

	if( !bNodeExists )
	{
		return tLodName.c_str();
	}

	// otherwise, add indexing (lodname001_001)
	else
	{
		MaxInterface->MakeNameUnique( tTempLodName );

		return tTempLodName;
	}
}

TSTR SimplygonMax::GetUniqueNameForLOD( TSTR tNodeName, size_t lodIndex )
{
	// create a lod name (lodname001)
	std::basic_ostringstream<TCHAR, std::char_traits<TCHAR>, std::allocator<TCHAR>> tLodNameStream;
	tLodNameStream << tNodeName << DefaultPrefix << std::setfill<TCHAR>( '0' ) << std::setw( 3 ) << lodIndex;
	std::basic_string<TCHAR> tLodName = tLodNameStream.str();

	TSTR tTempLodName = TSTR( tLodName.c_str() );

	// see if it exists
	const bool bNodeExists = ( this->MaxInterface->GetINodeByName( tTempLodName ) != nullptr );

	if( !bNodeExists )
	{
		return tLodName.c_str();
	}

	// otherwise, add indexing (lodname001_001)
	else
	{
		std::basic_ostringstream<TCHAR, std::char_traits<TCHAR>, std::allocator<TCHAR>> tNewLodNameStream;
		tNewLodNameStream << tLodName << '_' << std::setfill<TCHAR>( '0' ) << std::setw( 3 ) << 0;
		std::basic_string<TCHAR> tNewLodName = tNewLodNameStream.str();

		TSTR tNewTempLodName = TSTR( tNewLodName.c_str() );
		MaxInterface->MakeNameUnique( tNewTempLodName );

		return tNewTempLodName;
	}
}

TSTR SimplygonMax::GetUniqueNameForProxy( int lodIndex )
{
	std::basic_ostringstream<TCHAR, std::char_traits<TCHAR>, std::allocator<TCHAR>> tLodNameStream;
	tLodNameStream << _T("Simplygon_Proxy") << DefaultPrefix << std::setfill<TCHAR>( '0' ) << std::setw( 3 ) << lodIndex;
	std::basic_string<TCHAR> tLodName = tLodNameStream.str();

	TSTR tTempLodName = tLodName.c_str();
	const bool bNodeExists = ( this->MaxInterface->GetINodeByName( tTempLodName ) != nullptr );

	if( !bNodeExists )
	{
		return tTempLodName;
	}
	else
	{
		std::basic_ostringstream<TCHAR, std::char_traits<TCHAR>, std::allocator<TCHAR>> tNewLodNameStream;
		tNewLodNameStream << tLodName.c_str() << '_' << std::setfill<TCHAR>( '0' ) << std::setw( 3 ) << 0;
		std::basic_string<TCHAR> tNewLodName = tNewLodNameStream.str();

		TSTR tNewTempLodName = TSTR( tNewLodName.c_str() );
		MaxInterface->MakeNameUnique( tNewTempLodName );

		return tNewTempLodName;
	}
}

TSTR SimplygonMax::GetUniqueMaterialName( TSTR tMaterialName )
{
	bool bMaterialExists = GetExistingMaterial( std::basic_string<TCHAR>( tMaterialName ) ) != nullptr;
	if( !bMaterialExists )
	{
		return tMaterialName;
	}

	int materialIndex = 1;
	TSTR tTempMaterialName = tMaterialName;

	while( bMaterialExists )
	{
		// create a material name (MaterialName001)
		std::basic_ostringstream<TCHAR, std::char_traits<TCHAR>, std::allocator<TCHAR>> tMaterialNameStream;
		tMaterialNameStream << tMaterialName << std::setfill<TCHAR>( '0' ) << std::setw( 3 ) << materialIndex++;

		// see if it exists
		bMaterialExists = GetExistingMaterial( tMaterialNameStream.str() ) != nullptr;
		if( !bMaterialExists )
		{
			tTempMaterialName = (TSTR)( tMaterialNameStream.str().c_str() );
		}
	}

	return tTempMaterialName;
}

Mtl* SimplygonMax::GetExistingMappedMaterial( std::string sMaterialId )
{
	std::map<std::string, Mtl*>::const_iterator& mIt = this->GlobalSgToMaxMaterialMap.find( sMaterialId );
	if( mIt == this->GlobalSgToMaxMaterialMap.end() )
		return nullptr;

	return (Mtl*)mIt->second;
}

Mtl* SimplygonMax::GetExistingMaterial( std::basic_string<TCHAR> tMaterialName )
{
	MtlBaseLib* mSceneMaterials = this->MaxInterface->GetSceneMtls();
	if( !mSceneMaterials )
		return nullptr;

	const int numMaterials = mSceneMaterials->Count();
	if( !numMaterials )
		return nullptr;

	const int materialIndex = mSceneMaterials->FindMtlByName( TSTR( tMaterialName.c_str() ) );
	if( materialIndex == -1 )
	{
		// look for sub materials
		for( int mid = 0; mid < numMaterials; ++mid )
		{
			MtlBase* mMaxMaterial = static_cast<MtlBase*>( ( *mSceneMaterials )[ mid ] );
			if( mMaxMaterial && mMaxMaterial->IsMultiMtl() )
			{
				MultiMtl* mMaxMultiMaterial = (MultiMtl*)mMaxMaterial;

				for( int smid = 0; smid < mMaxMultiMaterial->NumSubMtls(); ++smid )
				{
					MtlBase* mMaxSubMaterial = mMaxMultiMaterial->GetSubMtl( smid );
					if( !mMaxSubMaterial )
						continue;

					std::basic_string<TCHAR> tMaxMaterialName = mMaxSubMaterial->GetName();
					if( tMaxMaterialName == tMaterialName )
					{
						return (Mtl*)mMaxSubMaterial;
					}
				}
			}
		}

		// if no sub materials, return null
		return nullptr;
	}

	// if material was found in lib, return it
	MtlBase* mtl = static_cast<MtlBase*>( ( *mSceneMaterials )[ materialIndex ] );

	return (Mtl*)mtl;
}

std::basic_string<TCHAR>
GenerateFormattedName( const std::basic_string<TCHAR>& tFormatString, const std::basic_string<TCHAR>& tMeshName, const std::basic_string<TCHAR>& tSceneIndex )
{
	std::basic_string<TCHAR> tFormattedName = tFormatString;

	if( tFormattedName.length() > 0 )
	{
		const std::basic_string<TCHAR> tMeshString = _T("{MeshName}");
		const size_t meshStringLength = tMeshString.length();

		const std::basic_string<TCHAR> tLodIndexString = _T("{LODIndex}");
		const size_t lodIndexStringLength = tLodIndexString.length();

		bool bHasMeshName = true;
		while( bHasMeshName )
		{
			const size_t meshNamePosition = tFormattedName.find( tMeshString );
			bHasMeshName = meshNamePosition != std::string::npos;
			if( bHasMeshName )
			{
				tFormattedName = tFormattedName.replace(
				    tFormattedName.begin() + meshNamePosition, tFormattedName.begin() + meshNamePosition + meshStringLength, tMeshName );
			}
		}

		bool bHasLODIndex = true;
		while( bHasLODIndex )
		{
			const size_t lodIndexPosition = tFormattedName.find( tLodIndexString );
			bHasLODIndex = lodIndexPosition != std::string::npos;
			if( bHasLODIndex )
			{
				tFormattedName = tFormattedName.replace(
				    tFormattedName.begin() + lodIndexPosition, tFormattedName.begin() + lodIndexPosition + lodIndexStringLength, tSceneIndex );
			}
		}
	}

	return tFormattedName;
}

bool SimplygonMax::WritebackGeometry_Quad( spScene sgProcessedScene,
                                           size_t logicalLodIndex,
                                           spSceneMesh sgProcessedMesh,
                                           std::map<std::string, INode*>& meshNodesThatNeedsParents )
{
	// clean up locally used mapping
	this->ImportedUvNameToMaxIndex.clear();
	this->ImportedMaxIndexToUv.clear();

	const spString rSgMeshId = sgProcessedMesh->GetNodeGUID();
	const char* cSgMeshId = rSgMeshId.c_str();

	// try to find mesh map via global lookup
	const std::map<std::string, GlobalMeshMap>::const_iterator& globalMeshMap = this->GlobalGuidToMaxNodeMap.find( cSgMeshId );
	bool bHasGlobalMeshMap = ( this->mapMeshes || this->extractionType == BATCH_PROCESSOR ) ? globalMeshMap != this->GlobalGuidToMaxNodeMap.end() : nullptr;

	// try to find original Max mesh from map handle
	INode* mMappedMaxNode = bHasGlobalMeshMap ? this->MaxInterface->GetINodeByHandle( globalMeshMap->second.GetMaxId() ) : nullptr;
	if( mMappedMaxNode )
	{
		// if the name does not match, try to get Max mesh by name (fallback)
		if( mMappedMaxNode->GetName() != globalMeshMap->second.GetName() )
		{
			mMappedMaxNode = bHasGlobalMeshMap ? this->MaxInterface->GetINodeByName( globalMeshMap->second.GetName().c_str() ) : nullptr;
		}
	}
	// only use this fallback on user request (sgsdk_AllowUnsafeImport()),
	// may result in unexpected behavior if used incorrectly.
	else if( this->AllowUnsafeImport )
	{
		spUnsignedCharData sgUniqueHandleData = sgProcessedMesh->GetUserData( "MAX_UniqueHandle" );
		if( !sgUniqueHandleData.IsNullOrEmpty() )
		{
			const ULONG* mUniquehandle = (const ULONG*)sgUniqueHandleData.Data();
			mMappedMaxNode = this->MaxInterface->GetINodeByHandle( *mUniquehandle );
		}
	}

	bHasGlobalMeshMap = mMappedMaxNode != nullptr;
	const bool bHasSceneMeshMap = bHasGlobalMeshMap ? globalMeshMap != this->GlobalGuidToMaxNodeMap.end() : false;

	// fetch the geometry from the mesh node
	spGeometryData sgMeshData = sgProcessedMesh->GetGeometry();
	spString rProcessedMeshName = sgProcessedMesh->GetName();
	const char* cProcessedMeshName = rProcessedMeshName.c_str();
	std::basic_string<TCHAR> tMaxOriginalNodeName = ConstCharPtrToLPCTSTR( cProcessedMeshName );

	if( sgMeshData->GetTriangleCount() == 0 )
	{
		std::basic_string<TCHAR> tWarningMessage = _T("Zero triangle mesh detected when importing node: ");
		tWarningMessage += tMaxOriginalNodeName;
		this->LogToWindow( tWarningMessage, Warning );
		return true;
	}

	// create new max node and assign the same material as before
	PolyObject* mNewMaxQuadObject = CreateEditablePolyObject();
	INode* mNewMaxNode = this->MaxInterface->CreateObjectNode( mNewMaxQuadObject );
	MNMesh& mNewMaxMesh = mNewMaxQuadObject->GetMesh();

	Mtl* mOriginalMaxMaterial = nullptr;
	MaxMaterialMap* globalMaterialMap = nullptr;

	// if mesh map, map back the mesh to the original node's location
	if( bHasGlobalMeshMap )
	{
		INode* mOriginalMaxNode = mMappedMaxNode;
		tMaxOriginalNodeName = mOriginalMaxNode->GetName();

		mNewMaxNode->SetMtl( mOriginalMaxNode->GetMtl() );
		mOriginalMaxMaterial = mOriginalMaxNode->GetMtl();

		if( mOriginalMaxMaterial )
		{
			// only use material map if we want to map original materials
			globalMaterialMap = this->mapMaterials ? this->GetGlobalMaterialMap( mOriginalMaxMaterial ) : nullptr;

			// allow unsafe mapping, if enabled
			if( !globalMaterialMap && this->AllowUnsafeImport )
			{
				globalMaterialMap = this->mapMaterials ? this->GetGlobalMaterialMapUnsafe( mOriginalMaxMaterial ) : nullptr;
			}
		}

		INode* mOriginalMaxParentNode = mOriginalMaxNode->GetParentNode();
		if( !mOriginalMaxParentNode->IsRootNode() )
		{
			mOriginalMaxParentNode->AttachChild( mNewMaxNode );
		}

		// Set the transform
		mNewMaxNode->SetNodeTM( this->CurrentTime, mOriginalMaxNode->GetNodeTM( this->CurrentTime ) );

		// set the same wire-frame color
		mNewMaxNode->SetWireColor( mOriginalMaxNode->GetWireColor() );

		// Set the other node properties:
		mNewMaxNode->CopyProperties( mOriginalMaxNode );
		mNewMaxNode->FlagForeground( this->CurrentTime, FALSE );
		mNewMaxNode->SetObjOffsetPos( mOriginalMaxNode->GetObjOffsetPos() );
		mNewMaxNode->SetObjOffsetRot( mOriginalMaxNode->GetObjOffsetRot() );
		mNewMaxNode->SetObjOffsetScale( mOriginalMaxNode->GetObjOffsetScale() );
	}

	// if no mapping found, insert the mesh into the root of the scene
	// TODO: copy transforms if no map was found.
	// Also link parents as in the Simplygon scene.
	else
	{
		Matrix3 mNodeMatrix;

		// sg to Max conversion
		spMatrix4x4 sgTransform = sg->CreateMatrix4x4();
		sgProcessedMesh->EvaluateDefaultGlobalTransformation( sgTransform );

		const Point4 mRow0(
		    sgTransform->GetElement( 0, 0 ), sgTransform->GetElement( 1, 0 ), sgTransform->GetElement( 2, 0 ), sgTransform->GetElement( 3, 0 ) );
		const Point4 mRow1(
		    sgTransform->GetElement( 0, 1 ), sgTransform->GetElement( 1, 1 ), sgTransform->GetElement( 2, 1 ), sgTransform->GetElement( 3, 1 ) );
		const Point4 mRow2(
		    sgTransform->GetElement( 0, 2 ), sgTransform->GetElement( 1, 2 ), sgTransform->GetElement( 2, 2 ), sgTransform->GetElement( 3, 2 ) );

		mNodeMatrix.SetColumn( 0, mRow0 );
		mNodeMatrix.SetColumn( 1, mRow1 );
		mNodeMatrix.SetColumn( 2, mRow2 );

		mNewMaxNode->SetNodeTM( this->CurrentTime, mNodeMatrix );

		meshNodesThatNeedsParents.insert( std::pair<std::string, INode*>( cSgMeshId, mNewMaxNode ) );
	}

	spCharArray sgQuadFlags = sgMeshData->GetQuadFlags();
	if( sgQuadFlags.IsNull() )
	{
		sgMeshData->AddQuadFlags();
		sgQuadFlags = sgMeshData->GetQuadFlags();

		for( uint i = 0; i < sgQuadFlags->GetItemCount(); ++i )
		{
			sgQuadFlags->SetItem( i, SG_QUADFLAG_TRIANGLE );
		}

		std::string sWarning = "QuadFlags not detected in geometry (";
		sWarning += cProcessedMeshName;
		sWarning += "), assuming that all polygons are triangles!";
		LogToWindow( ConstCharPtrToLPCWSTRr( sWarning.c_str() ), ErrorType::Warning );
	}

	bool bHasInvalidQuadFlags = false;

	uint cornerCount = 0;
	uint triangleCount = 0;
	uint faceCount = 0;
	for( uint fid = 0; fid < sgQuadFlags.GetItemCount(); fid++ )
	{
		char q1 = sgQuadFlags.GetItem( fid );
		if( q1 == SG_QUADFLAG_FIRST )
		{
			char q2 = sgQuadFlags.GetItem( ++fid );
			if( q2 == SG_QUADFLAG_SECOND )
			{
				++faceCount;
				cornerCount += 4;
				triangleCount += 2;
			}
			else
			{
				bHasInvalidQuadFlags = true;
				break;
			}
		}
		else if( q1 == SG_QUADFLAG_TRIANGLE )
		{
			++faceCount;
			++triangleCount;
			cornerCount += 3;
		}
		else
		{
			bHasInvalidQuadFlags = true;
			break;
		}
	}

	if( bHasInvalidQuadFlags == true )
	{
		std::string sError = "QuadFlags import - invalid quad flags in geometry (";
		sError += cProcessedMeshName;
		sError += ")";

		LogToWindow( ConstCharPtrToLPCWSTRr( sError.c_str() ), ErrorType::Error );
		return false;
	}

	// mesh data
	const uint vertexCount = sgMeshData->GetVertexCount();

	mNewMaxMesh.setNumVerts( vertexCount );

	spRealArray sgCoords = sgMeshData->GetCoords();
	spRidArray sgVertexIds = sgMeshData->GetVertexIds();
	spRidArray sgMaterialIds = sgMeshData->GetMaterialIds();
	spUnsignedIntArray sgShadingGroups = spUnsignedIntArray::SafeCast( sgMeshData->GetUserTriangleField( "ShadingGroupIds" ) );

	// setup vertices
	for( uint vid = 0; vid < vertexCount; ++vid )
	{
		spRealData sgCoord = sgCoords->GetTuple( vid );
		const Point3 p( sgCoord[ 0 ], sgCoord[ 1 ], sgCoord[ 2 ] );
		mNewMaxMesh.v[ vid ].p = p;
	}

	uint sgCornerIndex = 0;
	std::vector<int> gFacesIndices;
	for( uint tid = 0; tid < sgQuadFlags.GetItemCount(); ++tid )
	{
		char q1 = sgQuadFlags.GetItem( tid );
		if( q1 == SG_QUADFLAG_FIRST )
		{
			char q2 = sgQuadFlags.GetItem( ++tid );
			if( q2 == SG_QUADFLAG_SECOND )
			{
				int indices[ 6 ] = { sgVertexIds.GetItem( sgCornerIndex++ ),
				                     sgVertexIds.GetItem( sgCornerIndex++ ),
				                     sgVertexIds.GetItem( sgCornerIndex++ ),
				                     sgVertexIds.GetItem( sgCornerIndex++ ),
				                     sgVertexIds.GetItem( sgCornerIndex++ ),
				                     sgVertexIds.GetItem( sgCornerIndex++ ) };

				int quadIndices[ 4 ];
				ConvertToQuad( indices, quadIndices );

				const uint smoothingGroup = sgShadingGroups.IsNull() ? 1 : sgShadingGroups->GetItem( tid );
				int mid = 0;
				if( !sgMaterialIds.IsNull() && globalMaterialMap )
				{
					mid = globalMaterialMap->GetMaxMaterialId( sgMaterialIds->GetItem( tid ) );
				}
				else if( !sgMaterialIds.IsNull() )
				{
					mid = sgMaterialIds->GetItem( tid );
				}

				int faceIndex = mNewMaxMesh.NewQuad( quadIndices, smoothingGroup, static_cast<MtlID>( mid >= 0 ? mid : 0 ) );
				gFacesIndices.emplace_back( faceIndex );
			}
		}
		else if( q1 == SG_QUADFLAG_TRIANGLE )
		{
			rid indices[ 3 ] = { sgVertexIds.GetItem( sgCornerIndex++ ), sgVertexIds.GetItem( sgCornerIndex++ ), sgVertexIds.GetItem( sgCornerIndex++ ) };

			const uint smoothingGroup = sgShadingGroups.IsNull() ? 1 : sgShadingGroups->GetItem( tid );
			int mid = 0;
			if( !sgMaterialIds.IsNull() && globalMaterialMap )
			{
				mid = globalMaterialMap->GetMaxMaterialId( sgMaterialIds->GetItem( tid ) );
			}
			else if( !sgMaterialIds.IsNull() )
			{
				mid = sgMaterialIds->GetItem( tid );
			}

			int faceIndex = mNewMaxMesh.NewTri( indices, smoothingGroup, static_cast<MtlID>( mid >= 0 ? mid : 0 ) );
			gFacesIndices.emplace_back( faceIndex );
		}
	}

	const bool bWritebackMappingSucceeded = this->WritebackMapping_Quad( logicalLodIndex, faceCount, cornerCount, mNewMaxMesh, sgProcessedMesh );
	if( !bWritebackMappingSucceeded )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("Error - Writeback of mapping channel failed when importing node: ");
		tErrorMessage += tMaxOriginalNodeName;
		tErrorMessage += _T(".");
		this->LogToWindow( tErrorMessage, Error );
		return false;
	}

	// build normals
	mNewMaxMesh.buildNormals();

	// specify normals, if any.
	// TODO: merge shared normals!
	spRealArray sgNormals = sgMeshData->GetNormals();
	if( !sgNormals.IsNull() )
	{
		if( mNewMaxMesh.GetSpecifiedNormals() )
		{
			mNewMaxMesh.ClearSpecifiedNormals();
		}

		mNewMaxMesh.SpecifyNormals();
		MNNormalSpec* mNormalSpec = mNewMaxMesh.GetSpecifiedNormals();

		if( mNormalSpec )
		{
			mNormalSpec->Initialize();
			if( mNormalSpec->FAlloc( faceCount ) )
			{
				mNormalSpec->SetParent( &mNewMaxMesh );
				mNormalSpec->CheckNormals();
			}

			mNormalSpec->SetNumFaces( faceCount );
			mNormalSpec->SetNumNormals( cornerCount ); // per-corner normals

			//
			uint sgIndex = 0;
			uint mIndex = 0;
			for( uint fid = 0; fid < faceCount; ++fid )
			{
				MNNormalFace& mNormalFace = mNormalSpec->Face( fid );
				uint deg = mNewMaxMesh.F( gFacesIndices[ fid ] )->deg;

				mNormalFace.SetDegree( deg );
				mNormalFace.SpecifyAll();

				if( deg == 4 )
				{
					rid indices[ 6 ] = { sgVertexIds.GetItem( sgIndex + 0 ),
					                     sgVertexIds.GetItem( sgIndex + 1 ),
					                     sgVertexIds.GetItem( sgIndex + 2 ),
					                     sgVertexIds.GetItem( sgIndex + 3 ),
					                     sgVertexIds.GetItem( sgIndex + 4 ),
					                     sgVertexIds.GetItem( sgIndex + 5 ) };

					int quadIndices[ 4 ];
					int originalIndices[ 4 ];
					ConvertToQuad( indices, quadIndices, sgIndex, originalIndices );

					for( uint c = 0; c < 4; c++ )
					{
						int quadIndex = quadIndices[ c ];
						int orgQuadIndex = originalIndices[ c ];

						spRealData sgNormal = sgNormals->GetTuple( orgQuadIndex );
						Point3 mNormal = Point3( sgNormal[ 0 ], sgNormal[ 1 ], sgNormal[ 2 ] );

						mNormalSpec->Normal( mIndex ) = mNormal;
						mNormalSpec->SetNormalExplicit( mIndex, true );

						mNormalFace.SetNormalID( c, mIndex );
						mIndex++;
					}
					sgIndex += 6;
				}
				else if( deg == 3 )
				{
					for( uint c = 0; c < 3; c++ )
					{
						spRealData sgNormal = sgNormals->GetTuple( sgIndex++ );
						Point3 mNormal = Point3( sgNormal[ 0 ], sgNormal[ 1 ], sgNormal[ 2 ] );

						mNormalSpec->Normal( mIndex ) = mNormal;
						mNormalSpec->SetNormalExplicit( mIndex, true );

						mNormalFace.SetNormalID( c, mIndex++ );
					}
				}
			}
			mNormalSpec->CheckNormals();
		}
	}

	// create a valid mesh name
	std::basic_string<TCHAR> tMeshName = bHasGlobalMeshMap ? tMaxOriginalNodeName.c_str() : tMaxOriginalNodeName.c_str();

	// format mesh name based on user string and lod index
	std::basic_string<TCHAR> tFormattedMeshName =
	    GenerateFormattedName( this->meshFormatString, tMeshName, ConstCharPtrToLPCTSTR( std::to_string( logicalLodIndex ).c_str() ) );

	// make unique, if needed
	TSTR tIndexedNodeName = GetNonCollidingMeshName( tFormattedMeshName.c_str() );

	// assign name to Max mesh node
	mNewMaxNode->SetName( tIndexedNodeName );

	bool bMeshHasNewMappedMaterials = false;
	spTextureTable sgTextureTable = sgProcessedScene->GetTextureTable();
	spMaterialTable sgMaterialTable = sgProcessedScene->GetMaterialTable();
	std::map<int, std::string> sNewMaterialIndexToIdMap;
	std::set<int> sgMIds;

	int maximumMaterialIndex = 0;

	// find material that does not already exist
	std::map<int, NewMaterialMap*> newMaterialIndicesMap;

	if( !sgMaterialIds.IsNull() && sgMaterialTable.GetMaterialsCount() > 0 )
	{
		for( uint tid = 0; tid < faceCount; ++tid )
		{
			const int mid = sgMaterialIds->GetItem( tid );
			if( mid < 0 )
				continue;

			if( mid > maximumMaterialIndex )
			{
				maximumMaterialIndex = mid;
			}

			sgMIds.insert( mid );

			if( mid >= static_cast<int>( sgMaterialTable->GetMaterialsCount() ) )
			{
				std::basic_string<TCHAR> tErrorMessage = _T("Writeback of material(s) failed due to an out-of-range material id when importing node ");
				tErrorMessage += tIndexedNodeName;
				tErrorMessage += _T("!");
				this->LogToWindow( tErrorMessage, Error );
				return false;
			}

			spMaterial sgMaterial = sgMaterialTable->GetMaterial( mid );
			const spString rMaterialGuid = sgMaterial->GetMaterialGUID();
			const char* cMaterialGuid = rMaterialGuid.c_str();

			sNewMaterialIndexToIdMap.insert( std::pair<int, std::string>( mid, cMaterialGuid ) );

			const MaxMaterialMap* subMaterialMap = this->mapMaterials ? this->GetGlobalMaterialMap( cMaterialGuid ) : nullptr;

			const bool bPossibleMaterialLOD = subMaterialMap == nullptr;
			if( bPossibleMaterialLOD )
			{
				// loop through all material channels to create a uv-to-texture map
				/*
				const uint channelCount = sgMaterial->GetChannelCount();
				for (uint c = 0; c < channelCount; ++c)
				    {
				    const spString rChannelName = sgMaterial->GetChannelFromIndex(c);
				    const char* cChannelName = rChannelName.c_str();

				    spShadingNode sgExitNode = sgMaterial->GetShadingNetwork(cChannelName);
				    if (sgExitNode.IsNull())
				        continue;

				    // fetch all textures for this material channel
				    std::map<std::basic_string<TCHAR>, spShadingTextureNode> texNodeMap;
				    FindAllUpStreamTextureNodes(sgExitNode, texNodeMap);

				    // fetch texture id and uv for each texture node
				    for (std::map<std::basic_string<TCHAR>, spShadingTextureNode>::const_iterator& texIterator = texNodeMap.begin(); texIterator !=
				texNodeMap.end(); texIterator++)
				        {
				        const spString rNewMaterialName = texIterator->second->GetTexCoordName();
				        if (rNewMaterialName.IsNull())
				            continue;

				        if (!rNewMaterialName.c_str())
				            continue;

				        const char* cTexCoordName = rNewMaterialName.c_str();
				        sNewMappedMaterialTexCoordName = cTexCoordName;

				        const spString rTextureName = texIterator->second->GetTextureName();
				        const char* cTextureName = rTextureName.c_str();

				        spTexture sgTexture = SafeCast<ITexture>(sgTextureTable->FindItem(cTextureName));
				        const spString rTextureId = sgTexture->GetName();
				        const char* cTextureId = rTextureId.c_str();

				        //sgTextureToUvSet.insert(std::pair<std::string, std::string>(cTextureId, cTexCoordName));
				        }
				    }*/

				newMaterialIndicesMap.insert( std::pair<int, NewMaterialMap*>( mid, new NewMaterialMap( mid, cMaterialGuid, true ) ) );

				// this material is new!
				bMeshHasNewMappedMaterials = true;
			}
		}
	}

	// add new material if one exists
	if( bMeshHasNewMappedMaterials )
	{
		// const int numMaterials = sgMIds.size();
		const bool bIsMultiMaterial = sgMIds.size() > 1;
		const bool bIsSingleMaterial = sgMIds.size() == 1;

		if( bIsMultiMaterial )
		{
			const TSTR tIndexedMaterialName = GetUniqueMaterialName( _T("SimplygonMultiMaterial") );

			mOriginalMaxMaterial = NewDefaultMultiMtl();
			mOriginalMaxMaterial->SetName( tIndexedMaterialName );
			( (MultiMtl*)mOriginalMaxMaterial )->SetNumSubMtls( 0 );

			for( std::set<int>::const_iterator& mIterator = sgMIds.begin(); mIterator != sgMIds.end(); mIterator++ )
			{
				const int mid = *mIterator;

				spMaterial sgMaterial = sgMaterialTable->GetMaterial( mid );
				std::string sMaterialId = sgMaterial->GetMaterialGUID().c_str();

				const spString rMaterialName = sgMaterial->GetName();
				const char* cMaterialName = rMaterialName.c_str();
				std::basic_string<TCHAR> tMaterialName = ConstCharPtrToLPCTSTR( rMaterialName.c_str() );

				// create the material
				Mtl* mNewMaxMaterial = this->CreateMaterial(
				    sgProcessedScene, sgProcessedMesh, logicalLodIndex, std::basic_string<TCHAR>( tIndexedNodeName ), tMaterialName, mid );

				if( mNewMaxMaterial )
				{
					//( (MultiMtl*)mOriginalMaxMaterial )->SetSubMtlAndName( physicalMaterialIndex, mNewMaxMaterial, mNewMaxMaterial->GetName() );
					( (MultiMtl*)mOriginalMaxMaterial )->AddMtl( mNewMaxMaterial, mid, mNewMaxMaterial->GetName() );
				}
			}

			// remove automatically created sub-material
			( (MultiMtl*)mOriginalMaxMaterial )->RemoveMtl( 0 );
		}

		else if( bIsSingleMaterial )
		{
			const std::set<int>::const_iterator& mIterator = sgMIds.begin();

			const int mid = *mIterator;

			spMaterial sgMaterial = sgMaterialTable->GetMaterial( mid );
			std::string sMaterialId = sgMaterial->GetMaterialGUID().c_str();

			const spString rMaterialName = sgMaterial->GetName();
			const char* cMaterialName = rMaterialName.c_str();
			std::basic_string<TCHAR> tMaterialName = ConstCharPtrToLPCTSTR( rMaterialName.c_str() );

			// create the material
			Mtl* mNewMaxMaterial =
			    this->CreateMaterial( sgProcessedScene, sgProcessedMesh, logicalLodIndex, std::basic_string<TCHAR>( tIndexedNodeName ), tMaterialName, mid );

			if( mNewMaxMaterial )
			{
				mOriginalMaxMaterial = mNewMaxMaterial;
			}
		}

		if( this->GetGenerateMaterial() )
		{
			mNewMaxNode->SetMtl( mOriginalMaxMaterial );
		}

		newMaterialIndicesMap.clear();

		// add mesh and materials to material info handler, if any
		if( mOriginalMaxMaterial != nullptr )
		{
			std::basic_string<TCHAR> tMaterialName = mOriginalMaxMaterial->GetName();

			for( int subMaterialIndex = 0; subMaterialIndex < mOriginalMaxMaterial->NumSubMtls(); ++subMaterialIndex )
			{
				Mtl* mMaxSubMaterial = mOriginalMaxMaterial->GetSubMtl( subMaterialIndex );

				if( mMaxSubMaterial != nullptr )
				{
					std::basic_string<TCHAR> tSubMaterialName = mMaxSubMaterial->GetName();
					this->materialInfoHandler->Add( std::basic_string<TCHAR>( tIndexedNodeName ), tMaterialName, tSubMaterialName, subMaterialIndex, false );
				}
			}

			this->materialInfoHandler->Add( std::basic_string<TCHAR>( tIndexedNodeName ), tMaterialName, false );
		}

		// if no material, add mesh
		else
		{
			this->materialInfoHandler->Add( std::basic_string<TCHAR>( tIndexedNodeName ) );
		}
	}
	else
	{
		const bool bHasMeshMapMaterial = mOriginalMaxMaterial != nullptr;
		if( !mOriginalMaxMaterial )
		{
			const bool bIsMultiMaterial = sgMIds.size() > 1;
			const bool bIsSingleMaterial = sgMIds.size() == 1;

			if( bIsMultiMaterial )
			{
				const TSTR tIndexedMaterialName = GetUniqueMaterialName( _T("SimplygonMultiMaterial") );

				mOriginalMaxMaterial = NewDefaultMultiMtl();
				mOriginalMaxMaterial->SetName( tIndexedMaterialName );
				( (MultiMtl*)mOriginalMaxMaterial )->SetNumSubMtls( maximumMaterialIndex + 1 );

				for( std::set<int>::const_iterator& mIterator = sgMIds.begin(); mIterator != sgMIds.end(); mIterator++ )
				{
					const int mid = *mIterator;

					spMaterial sgMaterial = sgMaterialTable->GetMaterial( mid );
					std::string sMaterialId = sgMaterial->GetMaterialGUID().c_str();

					const spString rMaterialName = sgMaterial->GetName();
					const char* cMaterialName = rMaterialName.c_str();
					const TCHAR* tMaterialName = ConstCharPtrToLPCTSTR( cMaterialName );

					// fetch global guid map
					globalMaterialMap = this->GetGlobalMaterialMap( sMaterialId );
					if( !globalMaterialMap )
					{
						std::basic_string<TCHAR> tWarningMessage = _T("Multi-material '");
						tWarningMessage += tIndexedMaterialName + _T( "', sub-material '" );
						tWarningMessage += tMaterialName;
						tWarningMessage += _T("' - Could not find a material map between Simplygon and 3ds Max, ignoring material.");
						this->LogToWindow( tWarningMessage, Warning );

						continue;
					}

					Mtl* mGlobalMaxMaterial = nullptr;

					// get mapped material from in-memory guid map
					mGlobalMaxMaterial = GetExistingMappedMaterial( globalMaterialMap->sgMaterialId );
					if( !mGlobalMaxMaterial )
					{
						// if not there, fallback to name based fetch
						mGlobalMaxMaterial = GetExistingMaterial( globalMaterialMap->sgMaterialName );
						if( !mGlobalMaxMaterial )
						{
							std::basic_string<TCHAR> tWarningMessage = _T("Multi-material '");
							tWarningMessage += tIndexedMaterialName + _T( "', sub-material '" );
							tWarningMessage += tMaterialName;
							tWarningMessage +=
							    _T("' - There is mapping data that indicates that the current scene should contain original materials, are you importing the ")
							    _T("asset into an empty (or incorrect) scene? For multi-materials to get reused properly the original mesh has to exist ")
							    _T("in the current scene. Without the original mesh the sub-materials will get assigned to a generated multi-material, as ")
							    _T("long as there isn't any mapping data that indicates something else. Ignoring material.");
							this->LogToWindow( tWarningMessage, Warning );
						}
					}

					if( mGlobalMaxMaterial )
					{
						( (MultiMtl*)mOriginalMaxMaterial )->SetSubMtlAndName( mid, mGlobalMaxMaterial, mGlobalMaxMaterial->GetName() );
					}
				}

				// remove material-slots that are not in use
				for( int mid = maximumMaterialIndex; mid >= 0; mid-- )
				{
					std::set<int>::const_iterator& midIterator = sgMIds.find( mid );
					if( midIterator == sgMIds.end() )
					{
						( (MultiMtl*)mOriginalMaxMaterial )->RemoveMtl( mid );
					}
				}
			}

			else if( bIsSingleMaterial )
			{
				const std::set<int>::const_iterator& mIterator = sgMIds.begin();

				const int mid = *mIterator;

				spMaterial sgMaterial = sgMaterialTable->GetMaterial( mid );
				std::string sMaterialId = sgMaterial->GetMaterialGUID().c_str();

				const spString rMaterialName = sgMaterial->GetName();
				const char* cMaterialName = rMaterialName.c_str();
				const TCHAR* tMaterialName = ConstCharPtrToLPCTSTR( cMaterialName );

				globalMaterialMap = this->GetGlobalMaterialMap( sMaterialId );

				if( globalMaterialMap )
				{
					// get mapped material from in-memory guid map
					Mtl* mGlobalMaxMaterial = GetExistingMappedMaterial( globalMaterialMap->sgMaterialId );
					if( !mGlobalMaxMaterial )
					{
						// if not there, fallback to name based fetch
						mGlobalMaxMaterial = GetExistingMaterial( globalMaterialMap->sgMaterialName );

						if( !mGlobalMaxMaterial )
						{
							std::basic_string<TCHAR> tWarningMessage =
							    _T("There is mapping data that indicates that the current scene should contain original materials, are you importing the ")
							    _T("asset into an empty (or incorrect) scene? For multi-materials to get reused properly the original mesh has to exist ")
							    _T("in the current scene. Without the original mesh the sub-materials will get assigned to a generated multi-material, as ")
							    _T("long as there isn't any mapping data that indicates something else. Ignoring single-material...");
							this->LogToWindow( tWarningMessage, Warning );
						}
					}
					mOriginalMaxMaterial = mGlobalMaxMaterial;
				}
				else
				{
					std::basic_string<TCHAR> tWarningMessage = _T( "Single-material '" );
					tWarningMessage += tMaterialName;
					tWarningMessage += _T("' - Could not find a material map between Simplygon and 3ds Max, ignoring material.");
					this->LogToWindow( tWarningMessage, Warning );
				}
			}
		}

		if( this->GetGenerateMaterial() )
		{
			mNewMaxNode->SetMtl( mOriginalMaxMaterial );
		}

		// add original mesh and materials to material info handler, if any
		if( mOriginalMaxMaterial != nullptr )
		{
			std::basic_string<TCHAR> tMaterialName = mOriginalMaxMaterial->GetName();

			bool bHasActiveSubMaterials = false;
			for( int subMaterialIndex = 0; subMaterialIndex < mOriginalMaxMaterial->NumSubMtls(); ++subMaterialIndex )
			{
				Mtl* mMaxSubMaterial = mOriginalMaxMaterial->GetSubMtl( subMaterialIndex );

				if( mMaxSubMaterial != nullptr )
				{
					std::basic_string<TCHAR> tSubMaterialName = mMaxSubMaterial->GetName();
					this->materialInfoHandler->Add( std::basic_string<TCHAR>( tIndexedNodeName ), tMaterialName, tSubMaterialName, subMaterialIndex, true );
				}
			}

			this->materialInfoHandler->Add(
			    std::basic_string<TCHAR>( tIndexedNodeName ), tMaterialName, mOriginalMaxMaterial->NumSubMtls() > 0 ? bHasMeshMapMaterial : false );
		}

		// if no material, add mesh
		else
		{
			this->materialInfoHandler->Add( std::basic_string<TCHAR>( tIndexedNodeName ) );
		}
	}

	// if the original object had morph targets, add a morph modifier
	// after the new mesh in the modifier stack
	if( false /*bHasSceneMeshMap && globalMeshMap->second.HasMorpherMetaData()*/ )
	{
		RegisterMorphScripts();

		const ulong uniqueHandle = mNewMaxNode->GetHandle();

		const MorpherMetaData* morpherMetaData = globalMeshMap->second.GetMorpherMetaData();
		const std::vector<MorphChannelMetaData*>& morphChannelMetaData = morpherMetaData->morphTargetMetaData;

		Modifier* mNewMorpherModifier = (Modifier*)this->MaxInterface->CreateInstance( OSM_CLASS_ID, MORPHER_CLASS_ID );

		// make the object into a derived object
		IDerivedObject* mDerivedObject = CreateDerivedObject();
		mDerivedObject->TransferReferences( mNewMaxQuadObject );
		mDerivedObject->ReferenceObject( mNewMaxQuadObject );

		mDerivedObject->AddModifier( mNewMorpherModifier );

		TCHAR tTargetVertexFieldNameBuffer[ MAX_PATH ] = { 0 };
		TCHAR tMorphTargetNameBuffer[ MAX_PATH ] = { 0 };

		for( const MorphChannelMetaData* morphChannelMetaData : morphChannelMetaData )
		{
			const size_t originalMorphChannelIndex = morphChannelMetaData->GetOriginalIndex();
			const size_t maxMorphChannelIndex = originalMorphChannelIndex + 1;

			SetMorphChannelWeight( uniqueHandle, maxMorphChannelIndex, morphChannelMetaData->morphWeight );

			std::map<size_t, float> logicalProgressiveIndexToWeight;
			size_t numValidProgressiveMorphTargets = 0;

			for( const MorphTargetMetaData* morphTargetMetaData : morphChannelMetaData->morphTargetMetaData )
			{
				const size_t originalMorphTargetIndex = morphTargetMetaData->GetIndex();

				_stprintf_s( tTargetVertexFieldNameBuffer,
				             MAX_PATH,
				             _T("%s%zu_%zu"),
				             _T("BlendShapeTargetVertexField"),
				             originalMorphChannelIndex,
				             originalMorphTargetIndex );
				const char* cTargetVertexFieldName = LPCTSTRToConstCharPtr( tTargetVertexFieldNameBuffer );

				spRealArray sgMorphTargetDeltas = spRealArray::SafeCast( sgMeshData->GetUserVertexField( cTargetVertexFieldName ) );
				if( sgMorphTargetDeltas.NonNull() )
				{
					spString sgMorphTargetName = sgMorphTargetDeltas->GetAlternativeName();
					std::basic_string<TCHAR> tMorphTargetName = ConstCharPtrToLPCTSTR( sgMorphTargetName.c_str() );
					_stprintf_s( tMorphTargetNameBuffer,
					             MAX_PATH,
					             _T("%s_MorphTarget_%s_%zu_%zu"),
					             tMeshName.c_str(),
					             tMorphTargetName.c_str(),
					             originalMorphChannelIndex,
					             originalMorphTargetIndex );

					std::basic_string<TCHAR> tFormattedMorphTargetName = GenerateFormattedName(
					    this->meshFormatString, tMorphTargetNameBuffer, ConstCharPtrToLPCTSTR( std::to_string( logicalLodIndex ).c_str() ) );

					TSTR tIndexedMorphTargetName = GetNonCollidingMeshName( tFormattedMorphTargetName.c_str() );

					// create new max node and assign the same material as before
					PolyObject* mMorphTargetTriObject = CreateEditablePolyObject();
					INode* mMorpTargetNode = this->MaxInterface->CreateObjectNode( mMorphTargetTriObject );
					MNMesh& mMorphTargetMesh = mMorphTargetTriObject->GetMesh();

					mMorphTargetMesh.setNumVerts( vertexCount );
					mMorphTargetMesh.setNumFaces( faceCount );

					// setup vertices
					for( uint vid = 0; vid < vertexCount; ++vid )
					{
						spRealData sgCoord = sgCoords->GetTuple( vid );
						spRealData sgMorphDelta = sgMorphTargetDeltas->GetTuple( vid );

						const Point3 mMorphVertex( sgCoord[ 0 ] + sgMorphDelta[ 0 ], sgCoord[ 1 ] + sgMorphDelta[ 1 ], sgCoord[ 2 ] + sgMorphDelta[ 2 ] );
						mMorphTargetMesh.V( vid )->p = mMorphVertex;
					}

					uint vIndex = 0;
					for( uint fid = 0; fid < faceCount; ++fid )
					{
						int deg = mMorphTargetMesh.F( fid )->deg;

						if( deg == 4 )
						{
							rid indices[ 6 ] = { sgVertexIds.GetItem( vIndex++ ),
							                     sgVertexIds.GetItem( vIndex++ ),
							                     sgVertexIds.GetItem( vIndex++ ),
							                     sgVertexIds.GetItem( vIndex++ ),
							                     sgVertexIds.GetItem( vIndex++ ),
							                     sgVertexIds.GetItem( vIndex++ ) };

							int quadIndices[ 4 ];
							ConvertToQuad( indices, quadIndices );

							for( int c = 0; c < deg; ++c )
							{
								mMorphTargetMesh.F( fid )->vtx[ c ] = sgVertexIds->GetItem( quadIndices[ c ] );
							}
						}
						else
						{
							rid triIndices[ 3 ] = { sgVertexIds.GetItem( vIndex++ ), sgVertexIds.GetItem( vIndex++ ), sgVertexIds.GetItem( vIndex++ ) };

							for( int c = 0; c < deg; ++c )
							{
								mMorphTargetMesh.F( fid )->vtx[ c ] = sgVertexIds->GetItem( triIndices[ c ] );
							}
						}

						mMorphTargetMesh.F( fid )->SetFlag( EDGE_ALL );
					}

					INode* mOriginalMaxNode = mMappedMaxNode;
					INode* mOriginalMaxParentNode = mOriginalMaxNode->GetParentNode();

					mMorpTargetNode->SetName( tIndexedMorphTargetName );

					if( !mOriginalMaxParentNode->IsRootNode() )
					{
						mOriginalMaxParentNode->AttachChild( mMorpTargetNode );
					}

					// Set the transform
					mMorpTargetNode->SetNodeTM( this->CurrentTime, mOriginalMaxNode->GetNodeTM( this->CurrentTime ) );

					// set the same wire-frame color
					mMorpTargetNode->SetWireColor( mOriginalMaxNode->GetWireColor() );

					// Set the other node properties:
					mMorpTargetNode->FlagForeground( this->CurrentTime, FALSE );
					mMorpTargetNode->SetObjOffsetPos( mOriginalMaxNode->GetObjOffsetPos() );
					mMorpTargetNode->SetObjOffsetRot( mOriginalMaxNode->GetObjOffsetRot() );
					mMorpTargetNode->SetObjOffsetScale( mOriginalMaxNode->GetObjOffsetScale() );

					// set as regular morph target
					if( numValidProgressiveMorphTargets == 0 )
					{
						SetMorphTarget( uniqueHandle, mMorpTargetNode->GetHandle(), originalMorphChannelIndex + 1 );
					}

					// set as progressive morph target
					else
					{
						AddProgressiveMorphTarget( uniqueHandle, mMorpTargetNode->GetHandle(), originalMorphChannelIndex + 1 );
					}

					// store indices for later
					logicalProgressiveIndexToWeight.insert( std::pair<size_t, float>( numValidProgressiveMorphTargets + 1, morphTargetMetaData->weight ) );
					numValidProgressiveMorphTargets++;
				}
			}

			// progressive morph target parameter update for the specific morph channel,
			// must be done when all progressive targets have been written,
			// or the weights will be recalculated.
			for( const std::pair<size_t, float>& progressiveIndexWeightPair : logicalProgressiveIndexToWeight )
			{
				SetProgressiveMorphTargetWeight( uniqueHandle, maxMorphChannelIndex, progressiveIndexWeightPair.first, progressiveIndexWeightPair.second );
			}

			// apply per-channel settings (some of these are order dependent!)
			SetMorphChannelTension( uniqueHandle, maxMorphChannelIndex, morphChannelMetaData->tension );
			SetChannelUseLimits( uniqueHandle, maxMorphChannelIndex, morphChannelMetaData->useLimits );
			SetChannelMinLimit( uniqueHandle, maxMorphChannelIndex, morphChannelMetaData->minLimit );
			SetChannelMaxLimit( uniqueHandle, maxMorphChannelIndex, morphChannelMetaData->maxLimit );
			SetChannelUseVertexSelection( uniqueHandle, maxMorphChannelIndex, morphChannelMetaData->useVertexSelection );
		}

		// apply global settings
		MorpherWrapper::ApplyGlobalSettings( mNewMorpherModifier, morpherMetaData->globalSettings, 0 );
	}

	// if the original object had skinning, add a skinning modifier
	// after the new mesh in the modifier stack
	spSceneBoneTable sgBoneTable = sgProcessedScene->GetBoneTable();
	spRidArray sgBoneIds = sgMeshData->GetBoneIds();

	const bool bHasSkinning = sgBoneIds.IsNull() ? false : sgBoneIds->GetItemCount() > 0;

	// skinning modifiers
	if( bHasSkinning )
	{
		// make the object into a derived object
		IDerivedObject* mDerivedObject = CreateDerivedObject();
		mDerivedObject->TransferReferences( mNewMaxQuadObject );
		mDerivedObject->ReferenceObject( mNewMaxQuadObject );

		// add the skinning modifier
		Modifier* mNewSkinModifier = (Modifier*)this->MaxInterface->CreateInstance( OSM_CLASS_ID, SKIN_CLASSID );

#if MAX_VERSION_MAJOR < 24
		ModContext* mSkinModContext = new ModContext( new Matrix3( 1 ), nullptr, nullptr );
#else
		ModContext* mSkinModContext = new ModContext( new Matrix3(), nullptr, nullptr );
#endif

		mDerivedObject->AddModifier( mNewSkinModifier, mSkinModContext );
		ISkinImportData* mSkinImportData = (ISkinImportData*)mNewSkinModifier->GetInterface( I_SKINIMPORTDATA );

		bool bHasInvalidBoneReference = false;

		std::map<INode*, int> boneToIdInUseMap;
		std::map<int, INode*> boneIdToBoneInUseMap;

		const uint numBonesPerVertex = sgBoneIds->GetTupleSize();
		for( uint vid = 0; vid < vertexCount; ++vid )
		{
			spRidData sgBoneId = sgBoneIds->GetTuple( vid );

			for( uint b = 0; b < numBonesPerVertex; ++b )
			{
				const int globalBoneIndex = sgBoneId[ b ];

				if( globalBoneIndex >= 0 )
				{
					const std::map<int, INode*>::const_iterator& boneIterator = boneIdToBoneInUseMap.find( globalBoneIndex );
					if( boneIterator != boneIdToBoneInUseMap.end() )
						continue;

					spSceneBone sgBone = sgBoneTable->GetBone( globalBoneIndex );

					const spString rBoneId = sgBone->GetNodeGUID();
					const char* cBoneId = rBoneId.c_str();
					const spString rBoneName = sgBone->GetName();
					const char* cBoneName = rBoneName.c_str();

					INode* mBone = this->MaxInterface->GetINodeByName( ConstCharPtrToLPCTSTR( cBoneName ) );
					if( !mBone )
					{
						bHasInvalidBoneReference = true;
						break;
					}

					boneToIdInUseMap.insert( std::pair<INode*, int>( mBone, globalBoneIndex ) );
					boneIdToBoneInUseMap.insert( std::pair<int, INode*>( globalBoneIndex, mBone ) );
				}
			}
		}

		if( bHasInvalidBoneReference )
		{
			boneToIdInUseMap.clear();
			boneIdToBoneInUseMap.clear();

			std::basic_string<TCHAR> tWarningMessage = tIndexedNodeName;
			tWarningMessage +=
			    _T(" - Mapping data indicates reuse of existing bone hierarchy but was unable to get a valid bone reference. Ignoring skinning...");

			this->LogToWindow( tWarningMessage, Warning );
		}
		else
		{
			if( bHasGlobalMeshMap )
			{
				INode* mOriginalMaxNode = mMappedMaxNode;

				Modifier* skinModifier = nullptr;
				Object* mMaxObject = mOriginalMaxNode->GetObjectRef();

				// check if the object has a skinning modifier
				// start by checking if it is a derived object
				if( mMaxObject != nullptr && mMaxObject->SuperClassID() == GEN_DERIVOB_CLASS_ID )
				{
					IDerivedObject* mMaxDerivedObject = dynamic_cast<IDerivedObject*>( mMaxObject );

					// derived object, look through the modifier list for a skinning modifier
					for( int modifierIndex = 0; modifierIndex < mMaxDerivedObject->NumModifiers(); ++modifierIndex )
					{
						Modifier* mModifier = mMaxDerivedObject->GetModifier( modifierIndex );
						if( mModifier != nullptr && mModifier->ClassID() == SKIN_CLASSID )
						{
							// found a skin, use it
							skinModifier = mModifier;
							break;
						}
					}
				}

				if( skinModifier )
				{
					// add all bones from the original mesh
					ISkin* mSkin = (ISkin*)skinModifier->GetInterface( I_SKIN );
					for( int boneIndex = 0; boneIndex < mSkin->GetNumBones(); ++boneIndex )
					{
						INode* mBone = mSkin->GetBone( boneIndex );
						if( boneToIdInUseMap.find( mBone ) != boneToIdInUseMap.end() )
						{
							mSkinImportData->AddBoneEx( mBone, FALSE );

							Matrix3 mTransformation;
							mSkin->GetBoneInitTM( mBone, mTransformation, false );
							mSkinImportData->SetBoneTm( mBone, mTransformation, mTransformation );
						}
					}
				}
			}
			else
			{
				// copy bone transformations for every bone
				for( std::map<std::string, GlobalMeshMap>::const_iterator& nodeIterator = this->GlobalGuidToMaxNodeMap.begin();
				     nodeIterator != this->GlobalGuidToMaxNodeMap.end();
				     nodeIterator++ )
				{
					INode* mOriginalMaxNode = this->MaxInterface->GetINodeByHandle( nodeIterator->second.GetMaxId() );
					if( mOriginalMaxNode )
					{
						// if the name does not match, try to get Max mesh by name (fallback)
						if( mOriginalMaxNode->GetName() != nodeIterator->second.GetName() )
						{
							mOriginalMaxNode = this->MaxInterface->GetINodeByName( nodeIterator->second.GetName().c_str() );
						}
					}

					// if null, possible root node
					if( !mOriginalMaxNode )
						continue;

					Modifier* mCurrentSkinModifier = nullptr;

					// skinning modifiers
					Object* mMaxObject = mOriginalMaxNode->GetObjectRef();

					// check if the object has a skinning modifier
					// start by checking if it is a derived object
					if( mMaxObject != nullptr && mMaxObject->SuperClassID() == GEN_DERIVOB_CLASS_ID )
					{
						IDerivedObject* mMaxDerivedObject = dynamic_cast<IDerivedObject*>( mMaxObject );

						// derived object, look through the modifier list for a skinning modifier
						for( int modifierIndex = 0; modifierIndex < mMaxDerivedObject->NumModifiers(); ++modifierIndex )
						{
							Modifier* mModifier = mMaxDerivedObject->GetModifier( modifierIndex );
							if( mModifier != nullptr && mModifier->ClassID() == SKIN_CLASSID )
							{
								// found a skin, use it
								mCurrentSkinModifier = mModifier;
								break;
							}
						}
					}

					if( mCurrentSkinModifier == nullptr )
						continue;

					// add all bones from the original mesh
					ISkin* mSkin = (ISkin*)mCurrentSkinModifier->GetInterface( I_SKIN );
					for( int boneIndex = 0; boneIndex < mSkin->GetNumBones(); ++boneIndex )
					{
						INode* mBone = mSkin->GetBone( boneIndex );

						std::map<INode*, int>::iterator& boneInUseIterator = boneToIdInUseMap.find( mBone );
						if( boneInUseIterator != boneToIdInUseMap.end() )
						{
							mSkinImportData->AddBoneEx( mBone, FALSE );

							Matrix3 mTransformation;
							mSkin->GetBoneInitTM( mBone, mTransformation, false );
							mSkinImportData->SetBoneTm( mBone, mTransformation, mTransformation );

							boneToIdInUseMap.erase( boneInUseIterator );
						}
					}
				}
			}

			// update the derived object. this is needed to create a context data
			ObjectState MObjectState = mDerivedObject->Eval( 0 );

			spRealArray sgBoneWeights = sgMeshData->GetBoneWeights();

			// set the bone weights per-vertex
			for( uint vid = 0; vid < vertexCount; ++vid )
			{
				Tab<INode*> mBonesArray;
				Tab<float> mWeightsArray;
				mBonesArray.SetCount( numBonesPerVertex );
				mWeightsArray.SetCount( numBonesPerVertex );

				// get the tuple data
				spRidData sgBoneId = sgBoneIds->GetTuple( vid );
				spRealData sgBoneWeight = sgBoneWeights->GetTuple( vid );

				uint boneCount = 0;
				for( uint boneIndex = 0; boneIndex < numBonesPerVertex; ++boneIndex )
				{
					const int globalBoneIndex = sgBoneId[ boneIndex ];

					if( globalBoneIndex >= 0 )
					{
						spSceneBone sgBone = sgBoneTable->GetBone( globalBoneIndex );

						const std::map<int, INode*>::const_iterator& boneIterator = boneIdToBoneInUseMap.find( globalBoneIndex );
						if( boneIterator == boneIdToBoneInUseMap.end() )
							continue;

						INode* mBone = boneIterator->second;

						mBonesArray[ boneCount ] = mBone;
						mWeightsArray[ boneCount ] = sgBoneWeight[ boneIndex ];
						++boneCount;
					}
				}

				// resize arrays to actual count of bones added
				mBonesArray.SetCount( boneCount );
				mWeightsArray.SetCount( boneCount );

				// set the vertex weights
				// TODO: set all in same call!
				const bool bWeightAdded = mSkinImportData->AddWeights( mNewMaxNode, vid, mBonesArray, mWeightsArray ) == TRUE;
				if( !bWeightAdded )
				{
					std::basic_string<TCHAR> tWarningMessage = tIndexedNodeName;
					tWarningMessage += _T(" - Could not add bone weights to the given node, ignoring weights.");

					this->LogToWindow( tWarningMessage, Warning );
					return false;
				}
			}
		}
	}
	// clear shading network info
	this->ClearShadingNetworkInfo();

	mNewMaxMesh.InvalidateGeomCache();
	// mNewMaxMesh.InvalidateTopologyCache();

	// add custom attributes

	// max deviation
	spRealArray sgMaxDeviation = sgProcessedScene->GetCustomFieldMaxDeviation();
	if( !sgMaxDeviation.IsNull() )
	{
		const real maxDev = sgMaxDeviation->GetItem( 0 );
		mNewMaxNode->SetUserPropFloat( _T("MaxDeviation"), maxDev );
	}

	// scene radius
	const real sceneRadius = sgProcessedScene->GetRadius();
	mNewMaxNode->SetUserPropFloat( _T("SceneRadius"), sceneRadius );

	// scene meshes radius
	const real sceneMeshesRadius = GetSceneMeshesRadius( sgProcessedScene );
	mNewMaxNode->SetUserPropFloat( _T("SceneMeshesRadius"), sceneMeshesRadius );

	// processed meshes radius
	auto sgProcessedMeshesExtents = sgProcessedScene->GetCustomFieldProcessedMeshesExtents();
	if( !sgProcessedMeshesExtents.IsNull() )
	{
		const real processedMeshesRadius = sgProcessedMeshesExtents->GetBoundingSphereRadius();
		mNewMaxNode->SetUserPropFloat( _T("ProcessedMeshesRadius"), processedMeshesRadius );
	}

	// original node name
	mNewMaxNode->SetUserPropString( _T("OriginalNodeName"), tMaxOriginalNodeName.c_str() );

	// intended node name
	mNewMaxNode->SetUserPropString( _T("IntendedNodeName"), tFormattedMeshName.c_str() );

	// imported node name
	mNewMaxNode->SetUserPropString( _T("ImportedNodeName"), tIndexedNodeName );

	return true;
}

// Write back data to max
bool SimplygonMax::WritebackGeometry( spScene sgProcessedScene,
                                      size_t logicalLodIndex,
                                      spSceneMesh sgProcessedMesh,
                                      std::map<std::string, INode*>& meshNodesThatNeedsParents )
{
	// clean up locally used mapping
	this->ImportedUvNameToMaxIndex.clear();
	this->ImportedMaxIndexToUv.clear();

	const spString rSgMeshId = sgProcessedMesh->GetNodeGUID();
	const char* cSgMeshId = rSgMeshId.c_str();

	// try to find mesh map via global lookup
	const std::map<std::string, GlobalMeshMap>::const_iterator& globalMeshMap = this->GlobalGuidToMaxNodeMap.find( cSgMeshId );
	bool bHasGlobalMeshMap = ( this->mapMeshes || this->extractionType == BATCH_PROCESSOR ) ? globalMeshMap != this->GlobalGuidToMaxNodeMap.end() : nullptr;

	// try to find original Max mesh from map handle
	INode* mMappedMaxNode = bHasGlobalMeshMap ? this->MaxInterface->GetINodeByHandle( globalMeshMap->second.GetMaxId() ) : nullptr;
	if( mMappedMaxNode )
	{
		// if the name does not match, try to get Max mesh by name (fallback)
		if( mMappedMaxNode->GetName() != globalMeshMap->second.GetName() )
		{
			mMappedMaxNode = bHasGlobalMeshMap ? this->MaxInterface->GetINodeByName( globalMeshMap->second.GetName().c_str() ) : nullptr;
		}
	}
	// only use this fallback on user request (sgsdk_AllowUnsafeImport()),
	// may result in unexpected behavior if used incorrectly.
	else if( this->AllowUnsafeImport )
	{
		spUnsignedCharData sgUniqueHandleData = sgProcessedMesh->GetUserData( "MAX_UniqueHandle" );
		if( !sgUniqueHandleData.IsNullOrEmpty() )
		{
			const ULONG* mUniquehandle = (const ULONG*)sgUniqueHandleData.Data();
			mMappedMaxNode = this->MaxInterface->GetINodeByHandle( *mUniquehandle );
		}
	}

	bHasGlobalMeshMap = mMappedMaxNode != nullptr;
	const bool bHasSceneMeshMap = bHasGlobalMeshMap ? globalMeshMap != this->GlobalGuidToMaxNodeMap.end() : false;

	// fetch the geometry from the mesh node
	spGeometryData sgMeshData = sgProcessedMesh->GetGeometry();
	spString rProcessedMeshName = sgProcessedMesh->GetName();
	const char* cProcessedMeshName = rProcessedMeshName.c_str();
	std::basic_string<TCHAR> tMaxOriginalNodeName = ConstCharPtrToLPCTSTR( cProcessedMeshName );

	if( sgMeshData->GetTriangleCount() == 0 )
	{
		std::basic_string<TCHAR> tWarningMessage = _T("Zero triangle mesh detected when importing node: ");
		tWarningMessage += tMaxOriginalNodeName;
		this->LogToWindow( tWarningMessage, Warning );
		return true;
	}

	// create new max node and assign the same material as before
	TriObject* mNewMaxTriObject = CreateNewTriObject();
	INode* mNewMaxNode = this->MaxInterface->CreateObjectNode( mNewMaxTriObject );
	Mesh& mNewMaxMesh = mNewMaxTriObject->GetMesh();

	Mtl* mOriginalMaxMaterial = nullptr;
	MaxMaterialMap* globalMaterialMap = nullptr;

	// if mesh map, map back the mesh to the original node's location
	if( bHasGlobalMeshMap )
	{
		INode* mOriginalMaxNode = mMappedMaxNode;
		tMaxOriginalNodeName = mOriginalMaxNode->GetName();

		mNewMaxNode->SetMtl( mOriginalMaxNode->GetMtl() );
		mOriginalMaxMaterial = mOriginalMaxNode->GetMtl();

		if( mOriginalMaxMaterial )
		{
			// only use material map if we want to map original materials
			globalMaterialMap = this->mapMaterials ? this->GetGlobalMaterialMap( mOriginalMaxMaterial ) : nullptr;

			// allow unsafe mapping, if enabled
			if( !globalMaterialMap && this->AllowUnsafeImport )
			{
				globalMaterialMap = this->mapMaterials ? this->GetGlobalMaterialMapUnsafe( mOriginalMaxMaterial ) : nullptr;
			}
		}

		INode* mOriginalMaxParentNode = mOriginalMaxNode->GetParentNode();
		if( !mOriginalMaxParentNode->IsRootNode() )
		{
			mOriginalMaxParentNode->AttachChild( mNewMaxNode );
		}

		// Set the transform
		mNewMaxNode->SetNodeTM( this->CurrentTime, mOriginalMaxNode->GetNodeTM( this->CurrentTime ) );

		// set the same wire-frame color
		mNewMaxNode->SetWireColor( mOriginalMaxNode->GetWireColor() );

		// Set the other node properties:
		mNewMaxNode->CopyProperties( mOriginalMaxNode );
		mNewMaxNode->FlagForeground( this->CurrentTime, FALSE );
		mNewMaxNode->SetObjOffsetPos( mOriginalMaxNode->GetObjOffsetPos() );
		mNewMaxNode->SetObjOffsetRot( mOriginalMaxNode->GetObjOffsetRot() );
		mNewMaxNode->SetObjOffsetScale( mOriginalMaxNode->GetObjOffsetScale() );
	}

	// if no mapping found, insert the mesh into the root of the scene
	// TODO: copy transforms if no map was found.
	// Also link parents as in the Simplygon scene.
	else
	{
		Matrix3 mNodeMatrix;

		// sg to Max conversion
		spMatrix4x4 sgTransform = sg->CreateMatrix4x4();
		sgProcessedMesh->EvaluateDefaultGlobalTransformation( sgTransform );

		const Point4 mRow0(
		    sgTransform->GetElement( 0, 0 ), sgTransform->GetElement( 1, 0 ), sgTransform->GetElement( 2, 0 ), sgTransform->GetElement( 3, 0 ) );
		const Point4 mRow1(
		    sgTransform->GetElement( 0, 1 ), sgTransform->GetElement( 1, 1 ), sgTransform->GetElement( 2, 1 ), sgTransform->GetElement( 3, 1 ) );
		const Point4 mRow2(
		    sgTransform->GetElement( 0, 2 ), sgTransform->GetElement( 1, 2 ), sgTransform->GetElement( 2, 2 ), sgTransform->GetElement( 3, 2 ) );

		mNodeMatrix.SetColumn( 0, mRow0 );
		mNodeMatrix.SetColumn( 1, mRow1 );
		mNodeMatrix.SetColumn( 2, mRow2 );

		mNewMaxNode->SetNodeTM( this->CurrentTime, mNodeMatrix );

		meshNodesThatNeedsParents.insert( std::pair<std::string, INode*>( cSgMeshId, mNewMaxNode ) );
	}

	// mesh data
	const uint vertexCount = sgMeshData->GetVertexCount();
	const uint triangleCount = sgMeshData->GetTriangleCount();
	const uint cornerCount = triangleCount * 3;

	mNewMaxMesh.setNumVerts( vertexCount );
	mNewMaxMesh.setNumFaces( triangleCount );

	spRealArray sgCoords = sgMeshData->GetCoords();
	spRidArray sgVertexIds = sgMeshData->GetVertexIds();
	spRidArray sgMaterialIds = sgMeshData->GetMaterialIds();
	spUnsignedIntArray sgShadingGroups = spUnsignedIntArray::SafeCast( sgMeshData->GetUserTriangleField( "ShadingGroupIds" ) );

	// setup vertices
	for( uint vid = 0; vid < vertexCount; ++vid )
	{
		spRealData sgCoord = sgCoords->GetTuple( vid );
		const Point3 vert( sgCoord[ 0 ], sgCoord[ 1 ], sgCoord[ 2 ] );
		mNewMaxMesh.setVert( vid, vert );
	}

	// add the triangles
	for( uint tid = 0; tid < triangleCount; ++tid )
	{
		for( uint c = 0; c < 3; ++c )
		{
			mNewMaxMesh.faces[ tid ].v[ c ] = sgVertexIds->GetItem( tid * 3 + c );
		}

		// set face flags
		mNewMaxMesh.faces[ tid ].flags |= EDGE_ALL; // this->CombinedGeometryDataEdgeFlags[t];

		// copy material id
		if( !sgMaterialIds.IsNull() && globalMaterialMap )
		{
			const int mid = globalMaterialMap->GetMaxMaterialId( sgMaterialIds->GetItem( tid ) );

			// if material id is valid, assign, otherwise 0
			mNewMaxMesh.setFaceMtlIndex( tid, static_cast<MtlID>( mid >= 0 ? mid : 0 ) );
		}
		else if( !sgMaterialIds.IsNull() )
		{
			const int mid = sgMaterialIds->GetItem( tid );

			// if material id is valid, assign, otherwise 0

			mNewMaxMesh.setFaceMtlIndex( tid, static_cast<MtlID>( mid >= 0 ? mid + 0 : 0 ) );
		}
		else
		{
			mNewMaxMesh.setFaceMtlIndex( tid, 0 );
		}

		// copy smoothing groups, if any, otherwise 1
		mNewMaxMesh.faces[ tid ].smGroup = sgShadingGroups.IsNull() ? 1 : sgShadingGroups->GetItem( tid );
	}

	const bool bWritebackMappingSucceeded = this->WritebackMapping( logicalLodIndex, mNewMaxMesh, sgProcessedMesh );
	if( !bWritebackMappingSucceeded )
	{
		std::basic_string<TCHAR> tErrorMessage = _T("Error - Writeback of mapping channel failed when importing node: ");
		tErrorMessage += tMaxOriginalNodeName;
		tErrorMessage += _T(".");
		this->LogToWindow( tErrorMessage, Error );
		return false;
	}

	// build normals
	mNewMaxMesh.buildNormals();
	mNewMaxMesh.SpecifyNormals();

	// specify normals, if any.
	// TODO: merge shared normals!
	spRealArray sgNormals = sgMeshData->GetNormals();
	if( !sgNormals.IsNull() )
	{
		MeshNormalSpec* mNormalSpec = mNewMaxMesh.GetSpecifiedNormals();
		if( mNormalSpec )
		{
			mNormalSpec->ClearAndFree();
			mNormalSpec->SetNumFaces( triangleCount );
			mNormalSpec->SetNumNormals( cornerCount ); // per-corner normals
			mNormalSpec->SetAllExplicit();

			// fill in the normals from the normal array
			Point3* mNormals = mNormalSpec->GetNormalArray();
			MeshNormalFace* mFaces = mNormalSpec->GetFaceArray();

			for( uint tid = 0; tid < triangleCount; ++tid )
			{
				for( uint c = 0; c < 3; ++c )
				{
					// for each corner, get normal and fill in struct
					const int cid = tid * 3 + c;

					// get normal
					spRealData sgNormal = sgNormals->GetTuple( cid );
					mNormals[ cid ] = Point3( sgNormal[ 0 ], sgNormal[ 1 ], sgNormal[ 2 ] );

					// set face corner
					mFaces[ tid ].SpecifyNormalID( c, cid );
				}
			}

			mNormalSpec->CheckNormals();
		}
	}

	// create a valid mesh name
	std::basic_string<TCHAR> tMeshName = bHasGlobalMeshMap ? tMaxOriginalNodeName.c_str() : tMaxOriginalNodeName.c_str();

	// format mesh name based on user string and lod index
	std::basic_string<TCHAR> tFormattedMeshName =
	    GenerateFormattedName( this->meshFormatString, tMeshName, ConstCharPtrToLPCTSTR( std::to_string( logicalLodIndex ).c_str() ) );

	// make unique, if needed
	TSTR tIndexedNodeName = GetNonCollidingMeshName( tFormattedMeshName.c_str() );

	// assign name to Max mesh node
	mNewMaxNode->SetName( tIndexedNodeName );

	bool bMeshHasNewMappedMaterials = false;
	spTextureTable sgTextureTable = sgProcessedScene->GetTextureTable();
	spMaterialTable sgMaterialTable = sgProcessedScene->GetMaterialTable();
	std::map<int, std::string> sNewMaterialIndexToIdMap;
	std::set<int> sgMIds;

	int maximumMaterialIndex = 0;

	// find material that does not already exist
	std::map<int, NewMaterialMap*> newMaterialIndicesMap;

	// if there are no material ids or empty materialcount skip mapping material back
	if( !sgMaterialIds.IsNull() && sgMaterialTable.GetMaterialsCount() > 0 )
	{
		for( uint tid = 0; tid < triangleCount; ++tid )
		{
			const int mid = sgMaterialIds->GetItem( tid );
			if( mid < 0 )
				continue;

			if( mid > maximumMaterialIndex )
			{
				maximumMaterialIndex = mid;
			}

			sgMIds.insert( mid );

			if( mid >= static_cast<int>( sgMaterialTable->GetMaterialsCount() ) )
			{
				std::basic_string<TCHAR> tErrorMessage = _T("Writeback of material(s) failed due to an out-of-range material id when importing node ");
				tErrorMessage += tIndexedNodeName;
				tErrorMessage += _T("!");
				this->LogToWindow( tErrorMessage, Error );
				return false;
			}

			spMaterial sgMaterial = sgMaterialTable->GetMaterial( mid );
			const spString rMaterialGuid = sgMaterial->GetMaterialGUID();
			const char* cMaterialGuid = rMaterialGuid.c_str();

			sNewMaterialIndexToIdMap.insert( std::pair<int, std::string>( mid, cMaterialGuid ) );

			const MaxMaterialMap* subMaterialMap = this->mapMaterials ? this->GetGlobalMaterialMap( cMaterialGuid ) : nullptr;

			const bool bPossibleMaterialLOD = subMaterialMap == nullptr;
			if( bPossibleMaterialLOD )
			{
				// loop through all material channels to create a uv-to-texture map
				/*
				const uint channelCount = sgMaterial->GetChannelCount();
				for (uint c = 0; c < channelCount; ++c)
				    {
				    const spString rChannelName = sgMaterial->GetChannelFromIndex(c);
				    const char* cChannelName = rChannelName.c_str();

				    spShadingNode sgExitNode = sgMaterial->GetShadingNetwork(cChannelName);
				    if (sgExitNode.IsNull())
				        continue;

				    // fetch all textures for this material channel
				    std::map<std::basic_string<TCHAR>, spShadingTextureNode> texNodeMap;
				    FindAllUpStreamTextureNodes(sgExitNode, texNodeMap);

				    // fetch texture id and uv for each texture node
				    for (std::map<std::basic_string<TCHAR>, spShadingTextureNode>::const_iterator& texIterator = texNodeMap.begin(); texIterator !=
				texNodeMap.end(); texIterator++)
				        {
				        const spString rNewMaterialName = texIterator->second->GetTexCoordName();
				        if (rNewMaterialName.IsNull())
				            continue;

				        if (!rNewMaterialName.c_str())
				            continue;

				        const char* cTexCoordName = rNewMaterialName.c_str();
				        sNewMappedMaterialTexCoordName = cTexCoordName;

				        const spString rTextureName = texIterator->second->GetTextureName();
				        const char* cTextureName = rTextureName.c_str();

				        spTexture sgTexture = SafeCast<ITexture>(sgTextureTable->FindItem(cTextureName));
				        const spString rTextureId = sgTexture->GetName();
				        const char* cTextureId = rTextureId.c_str();

				        //sgTextureToUvSet.insert(std::pair<std::string, std::string>(cTextureId, cTexCoordName));
				        }
				    }*/

				newMaterialIndicesMap.insert( std::pair<int, NewMaterialMap*>( mid, new NewMaterialMap( mid, cMaterialGuid, true ) ) );

				// this material is new!
				bMeshHasNewMappedMaterials = true;
			}
		}
	}

	// add new material if one exists
	if( bMeshHasNewMappedMaterials )
	{
		// const int numMaterials = sgMIds.size();
		const bool bIsMultiMaterial = sgMIds.size() > 1;
		const bool bIsSingleMaterial = sgMIds.size() == 1;

		if( bIsMultiMaterial )
		{
			const TSTR tIndexedMaterialName = GetUniqueMaterialName( _T("SimplygonMultiMaterial") );

			mOriginalMaxMaterial = NewDefaultMultiMtl();
			mOriginalMaxMaterial->SetName( tIndexedMaterialName );
			( (MultiMtl*)mOriginalMaxMaterial )->SetNumSubMtls( 0 );

			for( std::set<int>::const_iterator& mIterator = sgMIds.begin(); mIterator != sgMIds.end(); mIterator++ )
			{
				const int mid = *mIterator;

				spMaterial sgMaterial = sgMaterialTable->GetMaterial( mid );
				std::string sMaterialId = sgMaterial->GetMaterialGUID().c_str();

				const spString rMaterialName = sgMaterial->GetName();
				const char* cMaterialName = rMaterialName.c_str();
				std::basic_string<TCHAR> tMaterialName = ConstCharPtrToLPCTSTR( rMaterialName.c_str() );

				// create the material
				Mtl* mNewMaxMaterial = this->CreateMaterial(
				    sgProcessedScene, sgProcessedMesh, logicalLodIndex, std::basic_string<TCHAR>( tIndexedNodeName ), tMaterialName, mid );

				if( mNewMaxMaterial )
				{
					//( (MultiMtl*)mOriginalMaxMaterial )->SetSubMtlAndName( physicalMaterialIndex, mNewMaxMaterial, mNewMaxMaterial->GetName() );
					( (MultiMtl*)mOriginalMaxMaterial )->AddMtl( mNewMaxMaterial, mid, mNewMaxMaterial->GetName() );
				}
			}

			// remove automatically created sub-material
			( (MultiMtl*)mOriginalMaxMaterial )->RemoveMtl( 0 );
		}

		else if( bIsSingleMaterial )
		{
			const std::set<int>::const_iterator& mIterator = sgMIds.begin();

			const int mid = *mIterator;

			spMaterial sgMaterial = sgMaterialTable->GetMaterial( mid );
			std::string sMaterialId = sgMaterial->GetMaterialGUID().c_str();

			const spString rMaterialName = sgMaterial->GetName();
			const char* cMaterialName = rMaterialName.c_str();
			std::basic_string<TCHAR> tMaterialName = ConstCharPtrToLPCTSTR( rMaterialName.c_str() );

			// create the material
			Mtl* mNewMaxMaterial =
			    this->CreateMaterial( sgProcessedScene, sgProcessedMesh, logicalLodIndex, std::basic_string<TCHAR>( tIndexedNodeName ), tMaterialName, mid );

			if( mNewMaxMaterial )
			{
				mOriginalMaxMaterial = mNewMaxMaterial;
			}
		}

		if( this->GetGenerateMaterial() )
		{
			mNewMaxNode->SetMtl( mOriginalMaxMaterial );
		}

		newMaterialIndicesMap.clear();

		// add mesh and materials to material info handler, if any
		if( mOriginalMaxMaterial != nullptr )
		{
			std::basic_string<TCHAR> tMaterialName = mOriginalMaxMaterial->GetName();

			for( int subMaterialIndex = 0; subMaterialIndex < mOriginalMaxMaterial->NumSubMtls(); ++subMaterialIndex )
			{
				Mtl* mMaxSubMaterial = mOriginalMaxMaterial->GetSubMtl( subMaterialIndex );

				if( mMaxSubMaterial != nullptr )
				{
					std::basic_string<TCHAR> tSubMaterialName = mMaxSubMaterial->GetName();
					this->materialInfoHandler->Add( std::basic_string<TCHAR>( tIndexedNodeName ), tMaterialName, tSubMaterialName, subMaterialIndex, false );
				}
			}

			this->materialInfoHandler->Add( std::basic_string<TCHAR>( tIndexedNodeName ), tMaterialName, false );
		}

		// if no material, add mesh
		else
		{
			this->materialInfoHandler->Add( std::basic_string<TCHAR>( tIndexedNodeName ) );
		}
	}
	else
	{
		const bool bHasMeshMapMaterial = mOriginalMaxMaterial != nullptr;
		if( !mOriginalMaxMaterial )
		{
			const bool bIsMultiMaterial = sgMIds.size() > 1;
			const bool bIsSingleMaterial = sgMIds.size() == 1;

			if( bIsMultiMaterial )
			{
				const TSTR tIndexedMaterialName = GetUniqueMaterialName( _T("SimplygonMultiMaterial") );

				mOriginalMaxMaterial = NewDefaultMultiMtl();
				mOriginalMaxMaterial->SetName( tIndexedMaterialName );
				( (MultiMtl*)mOriginalMaxMaterial )->SetNumSubMtls( maximumMaterialIndex + 1 );

				for( std::set<int>::const_iterator& mIterator = sgMIds.begin(); mIterator != sgMIds.end(); mIterator++ )
				{
					const int mid = *mIterator;

					spMaterial sgMaterial = sgMaterialTable->GetMaterial( mid );
					std::string sMaterialId = sgMaterial->GetMaterialGUID().c_str();

					const spString rMaterialName = sgMaterial->GetName();
					const char* cMaterialName = rMaterialName.c_str();
					const TCHAR* tMaterialName = ConstCharPtrToLPCTSTR( cMaterialName );

					// fetch global guid map
					globalMaterialMap = this->GetGlobalMaterialMap( sMaterialId );
					if( !globalMaterialMap )
					{
						std::basic_string<TCHAR> tWarningMessage = _T("Multi-material '");
						tWarningMessage += tIndexedMaterialName + _T( "', sub-material '" );
						tWarningMessage += tMaterialName;
						tWarningMessage += _T("' - Could not find a material map between Simplygon and 3ds Max, ignoring material.");
						this->LogToWindow( tWarningMessage, Warning );

						continue;
					}

					Mtl* mGlobalMaxMaterial = nullptr;

					// get mapped material from in-memory guid map
					mGlobalMaxMaterial = GetExistingMappedMaterial( globalMaterialMap->sgMaterialId );
					if( !mGlobalMaxMaterial )
					{
						// if not there, fallback to name based fetch
						mGlobalMaxMaterial = GetExistingMaterial( globalMaterialMap->sgMaterialName );
						if( !mGlobalMaxMaterial )
						{
							std::basic_string<TCHAR> tWarningMessage = _T("Multi-material '");
							tWarningMessage += tIndexedMaterialName + _T( "', sub-material '" );
							tWarningMessage += tMaterialName;
							tWarningMessage +=
							    _T("' - There is mapping data that indicates that the current scene should contain original materials, are you importing the ")
							    _T("asset into an empty (or incorrect) scene? For multi-materials to get reused properly the original mesh has to exist ")
							    _T("in the current scene. Without the original mesh the sub-materials will get assigned to a generated multi-material, as ")
							    _T("long as there isn't any mapping data that indicates something else. Ignoring material.");
							this->LogToWindow( tWarningMessage, Warning );
						}
					}

					if( mGlobalMaxMaterial )
					{
						( (MultiMtl*)mOriginalMaxMaterial )->SetSubMtlAndName( mid, mGlobalMaxMaterial, mGlobalMaxMaterial->GetName() );
					}
				}

				// remove material-slots that are not in use
				for( int mid = maximumMaterialIndex; mid >= 0; mid-- )
				{
					std::set<int>::const_iterator& midIterator = sgMIds.find( mid );
					if( midIterator == sgMIds.end() )
					{
						( (MultiMtl*)mOriginalMaxMaterial )->RemoveMtl( mid );
					}
				}
			}

			else if( bIsSingleMaterial )
			{
				const std::set<int>::const_iterator& mIterator = sgMIds.begin();

				const int mid = *mIterator;

				spMaterial sgMaterial = sgMaterialTable->GetMaterial( mid );
				std::string sMaterialId = sgMaterial->GetMaterialGUID().c_str();

				const spString rMaterialName = sgMaterial->GetName();
				const char* cMaterialName = rMaterialName.c_str();
				const TCHAR* tMaterialName = ConstCharPtrToLPCTSTR( cMaterialName );

				globalMaterialMap = this->GetGlobalMaterialMap( sMaterialId );

				if( globalMaterialMap )
				{
					// get mapped material from in-memory guid map
					Mtl* mGlobalMaxMaterial = GetExistingMappedMaterial( globalMaterialMap->sgMaterialId );
					if( !mGlobalMaxMaterial )
					{
						// if not there, fallback to name based fetch
						mGlobalMaxMaterial = GetExistingMaterial( globalMaterialMap->sgMaterialName );

						if( !mGlobalMaxMaterial )
						{
							std::basic_string<TCHAR> tWarningMessage =
							    _T("There is mapping data that indicates that the current scene should contain original materials, are you importing the ")
							    _T("asset into an empty (or incorrect) scene? For multi-materials to get reused properly the original mesh has to exist ")
							    _T("in the current scene. Without the original mesh the sub-materials will get assigned to a generated multi-material, as ")
							    _T("long as there isn't any mapping data that indicates something else. Ignoring single-material...");
							this->LogToWindow( tWarningMessage, Warning );
						}
					}
					mOriginalMaxMaterial = mGlobalMaxMaterial;
				}
				else
				{
					std::basic_string<TCHAR> tWarningMessage = _T( "Single-material '" );
					tWarningMessage += tMaterialName;
					tWarningMessage += _T("' - Could not find a material map between Simplygon and 3ds Max, ignoring material.");
					this->LogToWindow( tWarningMessage, Warning );
				}
			}
		}

		if( this->GetGenerateMaterial() )
		{
			mNewMaxNode->SetMtl( mOriginalMaxMaterial );
		}

		// add original mesh and materials to material info handler, if any
		if( mOriginalMaxMaterial != nullptr )
		{
			std::basic_string<TCHAR> tMaterialName = mOriginalMaxMaterial->GetName();

			bool bHasActiveSubMaterials = false;
			for( int subMaterialIndex = 0; subMaterialIndex < mOriginalMaxMaterial->NumSubMtls(); ++subMaterialIndex )
			{
				Mtl* mMaxSubMaterial = mOriginalMaxMaterial->GetSubMtl( subMaterialIndex );

				if( mMaxSubMaterial != nullptr )
				{
					std::basic_string<TCHAR> tSubMaterialName = mMaxSubMaterial->GetName();
					this->materialInfoHandler->Add( std::basic_string<TCHAR>( tIndexedNodeName ), tMaterialName, tSubMaterialName, subMaterialIndex, true );
				}
			}

			this->materialInfoHandler->Add(
			    std::basic_string<TCHAR>( tIndexedNodeName ), tMaterialName, mOriginalMaxMaterial->NumSubMtls() > 0 ? bHasMeshMapMaterial : false );
		}

		// if no material, add mesh
		else
		{
			this->materialInfoHandler->Add( std::basic_string<TCHAR>( tIndexedNodeName ) );
		}
	}

	// if the original object had morph targets, add a morph modifier
	// after the new mesh in the modifier stack
	if( bHasSceneMeshMap && globalMeshMap->second.HasMorpherMetaData() )
	{
		RegisterMorphScripts();

		const ulong uniqueHandle = mNewMaxNode->GetHandle();

		const MorpherMetaData* morpherMetaData = globalMeshMap->second.GetMorpherMetaData();
		const std::vector<MorphChannelMetaData*>& morphChannelMetaData = morpherMetaData->morphTargetMetaData;

		Modifier* mNewMorpherModifier = (Modifier*)this->MaxInterface->CreateInstance( OSM_CLASS_ID, MORPHER_CLASS_ID );

		// make the object into a derived object
		IDerivedObject* mDerivedObject = CreateDerivedObject();
		mDerivedObject->TransferReferences( mNewMaxTriObject );
		mDerivedObject->ReferenceObject( mNewMaxTriObject );

		mDerivedObject->AddModifier( mNewMorpherModifier );

		TCHAR tTargetVertexFieldNameBuffer[ MAX_PATH ] = { 0 };
		TCHAR tMorphTargetNameBuffer[ MAX_PATH ] = { 0 };

		for( const MorphChannelMetaData* morphChannelMetaData : morphChannelMetaData )
		{
			const size_t originalMorphChannelIndex = morphChannelMetaData->GetOriginalIndex();
			const size_t maxMorphChannelIndex = originalMorphChannelIndex + 1;

			SetMorphChannelWeight( uniqueHandle, maxMorphChannelIndex, morphChannelMetaData->morphWeight );

			std::map<size_t, float> logicalProgressiveIndexToWeight;
			size_t numValidProgressiveMorphTargets = 0;

			for( const MorphTargetMetaData* morphTargetMetaData : morphChannelMetaData->morphTargetMetaData )
			{
				const size_t originalMorphTargetIndex = morphTargetMetaData->GetIndex();

				_stprintf_s( tTargetVertexFieldNameBuffer,
				             MAX_PATH,
				             _T("%s%zu_%zu"),
				             _T("BlendShapeTargetVertexField"),
				             originalMorphChannelIndex,
				             originalMorphTargetIndex );
				const char* cTargetVertexFieldName = LPCTSTRToConstCharPtr( tTargetVertexFieldNameBuffer );

				spRealArray sgMorphTargetDeltas = spRealArray::SafeCast( sgMeshData->GetUserVertexField( cTargetVertexFieldName ) );
				if( sgMorphTargetDeltas.NonNull() )
				{
					spString sgMorphTargetName = sgMorphTargetDeltas->GetAlternativeName();
					std::basic_string<TCHAR> tMorphTargetName = ConstCharPtrToLPCTSTR( sgMorphTargetName.c_str() );
					_stprintf_s( tMorphTargetNameBuffer,
					             MAX_PATH,
					             _T("%s_MorphTarget_%s_%zu_%zu"),
					             tMeshName.c_str(),
					             tMorphTargetName.c_str(),
					             originalMorphChannelIndex,
					             originalMorphTargetIndex );

					std::basic_string<TCHAR> tFormattedMorphTargetName = GenerateFormattedName(
					    this->meshFormatString, tMorphTargetNameBuffer, ConstCharPtrToLPCTSTR( std::to_string( logicalLodIndex ).c_str() ) );

					TSTR tIndexedMorphTargetName = GetNonCollidingMeshName( tFormattedMorphTargetName.c_str() );

					// create new max node and assign the same material as before
					TriObject* mMorphTargetTriObject = CreateNewTriObject();
					INode* mMorpTargetNode = this->MaxInterface->CreateObjectNode( mMorphTargetTriObject );
					Mesh& mMorphTargetMesh = mMorphTargetTriObject->GetMesh();

					mMorphTargetMesh.setNumVerts( vertexCount );
					mMorphTargetMesh.setNumFaces( triangleCount );

					// setup vertices
					for( uint vid = 0; vid < vertexCount; ++vid )
					{
						spRealData sgCoord = sgCoords->GetTuple( vid );
						spRealData sgMorphDelta = sgMorphTargetDeltas->GetTuple( vid );

						const Point3 mMorphVertex( sgCoord[ 0 ] + sgMorphDelta[ 0 ], sgCoord[ 1 ] + sgMorphDelta[ 1 ], sgCoord[ 2 ] + sgMorphDelta[ 2 ] );
						mMorphTargetMesh.setVert( vid, mMorphVertex );
					}

					for( uint tid = 0; tid < triangleCount; ++tid )
					{
						for( uint c = 0; c < 3; ++c )
						{
							mMorphTargetMesh.faces[ tid ].v[ c ] = sgVertexIds->GetItem( tid * 3 + c );
						}

						mMorphTargetMesh.faces[ tid ].flags |= EDGE_ALL;
					}

					INode* mOriginalMaxNode = mMappedMaxNode;
					INode* mOriginalMaxParentNode = mOriginalMaxNode->GetParentNode();

					mMorpTargetNode->SetName( tIndexedMorphTargetName );

					if( !mOriginalMaxParentNode->IsRootNode() )
					{
						mOriginalMaxParentNode->AttachChild( mMorpTargetNode );
					}

					// Set the transform
					mMorpTargetNode->SetNodeTM( this->CurrentTime, mOriginalMaxNode->GetNodeTM( this->CurrentTime ) );

					// set the same wire-frame color
					mMorpTargetNode->SetWireColor( mOriginalMaxNode->GetWireColor() );

					// Set the other node properties:
					mMorpTargetNode->FlagForeground( this->CurrentTime, FALSE );
					mMorpTargetNode->SetObjOffsetPos( mOriginalMaxNode->GetObjOffsetPos() );
					mMorpTargetNode->SetObjOffsetRot( mOriginalMaxNode->GetObjOffsetRot() );
					mMorpTargetNode->SetObjOffsetScale( mOriginalMaxNode->GetObjOffsetScale() );

					// set as regular morph target
					if( numValidProgressiveMorphTargets == 0 )
					{
						SetMorphTarget( uniqueHandle, mMorpTargetNode->GetHandle(), originalMorphChannelIndex + 1 );
					}

					// set as progressive morph target
					else
					{
						AddProgressiveMorphTarget( uniqueHandle, mMorpTargetNode->GetHandle(), originalMorphChannelIndex + 1 );
					}

					// store indices for later
					logicalProgressiveIndexToWeight.insert( std::pair<size_t, float>( numValidProgressiveMorphTargets + 1, morphTargetMetaData->weight ) );
					numValidProgressiveMorphTargets++;
				}
			}

			// progressive morph target parameter update for the specific morph channel,
			// must be done when all progressive targets have been written,
			// or the weights will be recalculated.
			for( const std::pair<size_t, float>& progressiveIndexWeightPair : logicalProgressiveIndexToWeight )
			{
				SetProgressiveMorphTargetWeight( uniqueHandle, maxMorphChannelIndex, progressiveIndexWeightPair.first, progressiveIndexWeightPair.second );
			}

			// apply per-channel settings (some of these are order dependent!)
			SetMorphChannelTension( uniqueHandle, maxMorphChannelIndex, morphChannelMetaData->tension );
			SetChannelUseLimits( uniqueHandle, maxMorphChannelIndex, morphChannelMetaData->useLimits );
			SetChannelMinLimit( uniqueHandle, maxMorphChannelIndex, morphChannelMetaData->minLimit );
			SetChannelMaxLimit( uniqueHandle, maxMorphChannelIndex, morphChannelMetaData->maxLimit );
			SetChannelUseVertexSelection( uniqueHandle, maxMorphChannelIndex, morphChannelMetaData->useVertexSelection );
		}

		// apply global settings
		MorpherWrapper::ApplyGlobalSettings( mNewMorpherModifier, morpherMetaData->globalSettings, 0 );
	}

	// if the original object had skinning, add a skinning modifier
	// after the new mesh in the modifier stack
	spSceneBoneTable sgBoneTable = sgProcessedScene->GetBoneTable();
	spRidArray sgBoneIds = sgMeshData->GetBoneIds();

	const bool bHasSkinning = sgBoneIds.IsNull() ? false : sgBoneIds->GetItemCount() > 0;

	// skinning modifiers
	if( bHasSkinning )
	{
		// make the object into a derived object
		IDerivedObject* mDerivedObject = CreateDerivedObject();
		mDerivedObject->TransferReferences( mNewMaxTriObject );
		mDerivedObject->ReferenceObject( mNewMaxTriObject );

		// add the skinning modifier
		Modifier* mNewSkinModifier = (Modifier*)this->MaxInterface->CreateInstance( OSM_CLASS_ID, SKIN_CLASSID );

#if MAX_VERSION_MAJOR < 24
		ModContext* mSkinModContext = new ModContext( new Matrix3( 1 ), nullptr, nullptr );
#else
		ModContext* mSkinModContext = new ModContext( new Matrix3(), nullptr, nullptr );
#endif

		mDerivedObject->AddModifier( mNewSkinModifier, mSkinModContext );
		ISkinImportData* mSkinImportData = (ISkinImportData*)mNewSkinModifier->GetInterface( I_SKINIMPORTDATA );

		bool bHasInvalidBoneReference = false;

		std::map<INode*, int> boneToIdInUseMap;
		std::map<int, INode*> boneIdToBoneInUseMap;

		const uint numBonesPerVertex = sgBoneIds->GetTupleSize();
		for( uint vid = 0; vid < vertexCount; ++vid )
		{
			spRidData sgBoneId = sgBoneIds->GetTuple( vid );

			for( uint b = 0; b < numBonesPerVertex; ++b )
			{
				const int globalBoneIndex = sgBoneId[ b ];

				if( globalBoneIndex >= 0 )
				{
					const std::map<int, INode*>::const_iterator& boneIterator = boneIdToBoneInUseMap.find( globalBoneIndex );
					if( boneIterator != boneIdToBoneInUseMap.end() )
						continue;

					spSceneBone sgBone = sgBoneTable->GetBone( globalBoneIndex );

					const spString rBoneId = sgBone->GetNodeGUID();
					const char* cBoneId = rBoneId.c_str();
					const spString rBoneName = sgBone->GetName();
					const char* cBoneName = rBoneName.c_str();

					INode* mBone = this->MaxInterface->GetINodeByName( ConstCharPtrToLPCTSTR( cBoneName ) );
					if( !mBone )
					{
						bHasInvalidBoneReference = true;
						break;
					}

					boneToIdInUseMap.insert( std::pair<INode*, int>( mBone, globalBoneIndex ) );
					boneIdToBoneInUseMap.insert( std::pair<int, INode*>( globalBoneIndex, mBone ) );
				}
			}
		}

		if( bHasInvalidBoneReference )
		{
			boneToIdInUseMap.clear();
			boneIdToBoneInUseMap.clear();

			std::basic_string<TCHAR> tWarningMessage = tIndexedNodeName;
			tWarningMessage +=
			    _T(" - Mapping data indicates reuse of existing bone hierarchy but was unable to get a valid bone reference. Ignoring skinning...");

			this->LogToWindow( tWarningMessage, Warning );
		}
		else
		{
			if( bHasGlobalMeshMap )
			{
				INode* mOriginalMaxNode = mMappedMaxNode;

				Modifier* skinModifier = nullptr;
				Object* mMaxObject = mOriginalMaxNode->GetObjectRef();

				// check if the object has a skinning modifier
				// start by checking if it is a derived object
				if( mMaxObject != nullptr && mMaxObject->SuperClassID() == GEN_DERIVOB_CLASS_ID )
				{
					IDerivedObject* mMaxDerivedObject = dynamic_cast<IDerivedObject*>( mMaxObject );

					// derived object, look through the modifier list for a skinning modifier
					for( int modifierIndex = 0; modifierIndex < mMaxDerivedObject->NumModifiers(); ++modifierIndex )
					{
						Modifier* mModifier = mMaxDerivedObject->GetModifier( modifierIndex );
						if( mModifier != nullptr && mModifier->ClassID() == SKIN_CLASSID )
						{
							// found a skin, use it
							skinModifier = mModifier;
							break;
						}
					}
				}

				if( skinModifier )
				{
					// add all bones from the original mesh
					ISkin* mSkin = (ISkin*)skinModifier->GetInterface( I_SKIN );
					for( int boneIndex = 0; boneIndex < mSkin->GetNumBones(); ++boneIndex )
					{
						INode* mBone = mSkin->GetBone( boneIndex );
						if( boneToIdInUseMap.find( mBone ) != boneToIdInUseMap.end() )
						{
							mSkinImportData->AddBoneEx( mBone, FALSE );

							Matrix3 mTransformation;
							mSkin->GetBoneInitTM( mBone, mTransformation, false );
							mSkinImportData->SetBoneTm( mBone, mTransformation, mTransformation );
						}
					}
				}
			}
			else
			{
				// copy bone transformations for every bone
				for( std::map<std::string, GlobalMeshMap>::const_iterator& nodeIterator = this->GlobalGuidToMaxNodeMap.begin();
				     nodeIterator != this->GlobalGuidToMaxNodeMap.end();
				     nodeIterator++ )
				{
					INode* mOriginalMaxNode = this->MaxInterface->GetINodeByHandle( nodeIterator->second.GetMaxId() );
					if( mOriginalMaxNode )
					{
						// if the name does not match, try to get Max mesh by name (fallback)
						if( mOriginalMaxNode->GetName() != nodeIterator->second.GetName() )
						{
							mOriginalMaxNode = this->MaxInterface->GetINodeByName( nodeIterator->second.GetName().c_str() );
						}
					}

					// if null, possible root node
					if( !mOriginalMaxNode )
						continue;

					Modifier* mCurrentSkinModifier = nullptr;

					// skinning modifiers
					Object* mMaxObject = mOriginalMaxNode->GetObjectRef();

					// check if the object has a skinning modifier
					// start by checking if it is a derived object
					if( mMaxObject != nullptr && mMaxObject->SuperClassID() == GEN_DERIVOB_CLASS_ID )
					{
						IDerivedObject* mMaxDerivedObject = dynamic_cast<IDerivedObject*>( mMaxObject );

						// derived object, look through the modifier list for a skinning modifier
						for( int modifierIndex = 0; modifierIndex < mMaxDerivedObject->NumModifiers(); ++modifierIndex )
						{
							Modifier* mModifier = mMaxDerivedObject->GetModifier( modifierIndex );
							if( mModifier != nullptr && mModifier->ClassID() == SKIN_CLASSID )
							{
								// found a skin, use it
								mCurrentSkinModifier = mModifier;
								break;
							}
						}
					}

					if( mCurrentSkinModifier == nullptr )
						continue;

					// add all bones from the original mesh
					ISkin* mSkin = (ISkin*)mCurrentSkinModifier->GetInterface( I_SKIN );
					for( int boneIndex = 0; boneIndex < mSkin->GetNumBones(); ++boneIndex )
					{
						INode* mBone = mSkin->GetBone( boneIndex );

						std::map<INode*, int>::iterator& boneInUseIterator = boneToIdInUseMap.find( mBone );
						if( boneInUseIterator != boneToIdInUseMap.end() )
						{
							mSkinImportData->AddBoneEx( mBone, FALSE );

							Matrix3 mTransformation;
							mSkin->GetBoneInitTM( mBone, mTransformation, false );
							mSkinImportData->SetBoneTm( mBone, mTransformation, mTransformation );

							boneToIdInUseMap.erase( boneInUseIterator );
						}
					}
				}
			}

			// update the derived object. this is needed to create a context data
			ObjectState MObjectState = mDerivedObject->Eval( 0 );

			spRealArray sgBoneWeights = sgMeshData->GetBoneWeights();

			// set the bone weights per-vertex
			for( uint vid = 0; vid < vertexCount; ++vid )
			{
				Tab<INode*> mBonesArray;
				Tab<float> mWeightsArray;
				mBonesArray.SetCount( numBonesPerVertex );
				mWeightsArray.SetCount( numBonesPerVertex );

				// get the tuple data
				spRidData sgBoneId = sgBoneIds->GetTuple( vid );
				spRealData sgBoneWeight = sgBoneWeights->GetTuple( vid );

				uint boneCount = 0;
				for( uint boneIndex = 0; boneIndex < numBonesPerVertex; ++boneIndex )
				{
					const int globalBoneIndex = sgBoneId[ boneIndex ];

					if( globalBoneIndex >= 0 )
					{
						spSceneBone sgBone = sgBoneTable->GetBone( globalBoneIndex );

						const std::map<int, INode*>::const_iterator& boneIterator = boneIdToBoneInUseMap.find( globalBoneIndex );
						if( boneIterator == boneIdToBoneInUseMap.end() )
							continue;

						INode* mBone = boneIterator->second;

						mBonesArray[ boneCount ] = mBone;
						mWeightsArray[ boneCount ] = sgBoneWeight[ boneIndex ];
						++boneCount;
					}
				}

				// resize arrays to actual count of bones added
				mBonesArray.SetCount( boneCount );
				mWeightsArray.SetCount( boneCount );

				// set the vertex weights
				// TODO: set all in same call!
				const bool bWeightAdded = mSkinImportData->AddWeights( mNewMaxNode, vid, mBonesArray, mWeightsArray ) == TRUE;
				if( !bWeightAdded )
				{
					std::basic_string<TCHAR> tWarningMessage = tIndexedNodeName;
					tWarningMessage += _T(" - Could not add bone weights to the given node, ignoring weights.");

					this->LogToWindow( tWarningMessage, Warning );
					return false;
				}
			}
		}
	}
	// clear shading network info
	this->ClearShadingNetworkInfo();

	mNewMaxMesh.InvalidateGeomCache();
	mNewMaxMesh.InvalidateTopologyCache();

	// add custom attributes

	// max deviation
	spRealArray sgMaxDeviation = sgProcessedScene->GetCustomFieldMaxDeviation();
	if( !sgMaxDeviation.IsNull() )
	{
		const real maxDev = sgMaxDeviation->GetItem( 0 );
		mNewMaxNode->SetUserPropFloat( _T("MaxDeviation"), maxDev );
	}

	// scene radius
	const real sceneRadius = sgProcessedScene->GetRadius();
	mNewMaxNode->SetUserPropFloat( _T("SceneRadius"), sceneRadius );

	// scene meshes radius
	const real sceneMeshesRadius = GetSceneMeshesRadius( sgProcessedScene );
	mNewMaxNode->SetUserPropFloat( _T("SceneMeshesRadius"), sceneMeshesRadius );

	// processed meshes radius
	auto sgProcessedMeshesExtents = sgProcessedScene->GetCustomFieldProcessedMeshesExtents();
	if( !sgProcessedMeshesExtents.IsNull() )
	{
		const real processedMeshesRadius = sgProcessedMeshesExtents->GetBoundingSphereRadius();
		mNewMaxNode->SetUserPropFloat( _T("ProcessedMeshesRadius"), processedMeshesRadius );
	}

	// original node name
	mNewMaxNode->SetUserPropString( _T("OriginalNodeName"), tMaxOriginalNodeName.c_str() );

	// intended node name
	mNewMaxNode->SetUserPropString( _T("IntendedNodeName"), tFormattedMeshName.c_str() );

	// imported node name
	mNewMaxNode->SetUserPropString( _T("ImportedNodeName"), tIndexedNodeName );

	return true;
}

void SimplygonMax::WriteSGTexCoordsToMaxChannel_Quad( spRealArray sgTexCoords, MNMesh& mMaxMesh, int maxChannel, uint cornerCount, uint faceCount )
{
	// setup objects
	// mMaxMesh.setMapSupport( maxChannel );
	mMaxMesh.InitMap( maxChannel );

	UVWMapper mUVWMapper;
	mMaxMesh.ApplyMapper( mUVWMapper, maxChannel );

	// setup and copy the corners
	spRidArray sgVertexIds = sg->CreateRidArray();
	spRealArray sgPackedTexCoords = spRealArray::SafeCast( sgTexCoords->NewPackedCopy( sgVertexIds ) );

	MNMap* mMaxMeshMap = mMaxMesh.M( maxChannel );
	if( mMaxMeshMap != nullptr )
	{
		// setup data channels
		// mMaxMeshMap.setNumFaces( faceCount );
		mMaxMeshMap->setNumVerts( sgPackedTexCoords->GetTupleCount() );

		// copy vertex data
		for( uint vid = 0; vid < sgPackedTexCoords->GetTupleCount(); ++vid )
		{
			// get the tuple
			spRealData sgTexCoord = sgPackedTexCoords->GetTuple( vid );

			// remap
			if( this->TextureCoordinateRemapping == 0 )
			{
				mMaxMeshMap->v[ vid ].x = sgTexCoord[ 0 ];
				mMaxMeshMap->v[ vid ].y = sgTexCoord[ 1 ];
				mMaxMeshMap->v[ vid ].z = 0;
			}
			else if( this->TextureCoordinateRemapping == 1 )
			{
				mMaxMeshMap->v[ vid ].x = sgTexCoord[ 0 ];
				mMaxMeshMap->v[ vid ].z = sgTexCoord[ 1 ];
				mMaxMeshMap->v[ vid ].y = 0;
			}
			else if( this->TextureCoordinateRemapping == 2 )
			{
				mMaxMeshMap->v[ vid ].y = sgTexCoord[ 0 ];
				mMaxMeshMap->v[ vid ].z = sgTexCoord[ 1 ];
				mMaxMeshMap->v[ vid ].x = 0;
			}
		}

		uint sgIndex = 0;
		for( uint fid = 0; fid < faceCount; ++fid )
		{
			int deg = mMaxMeshMap->f[ fid ].deg;

			if( deg == 3 )
			{
				for( int c = 0; c < deg; ++c )
				{
					const rid vid = sgVertexIds->GetItem( sgIndex++ );
					mMaxMeshMap->f[ fid ].tv[ c ] = vid;
				}
			}
			if( deg == 4 )
			{
				int indices[ 6 ] = { sgVertexIds.GetItem( sgIndex + 0 ),
				                     sgVertexIds.GetItem( sgIndex + 1 ),
				                     sgVertexIds.GetItem( sgIndex + 2 ),
				                     sgVertexIds.GetItem( sgIndex + 3 ),
				                     sgVertexIds.GetItem( sgIndex + 4 ),
				                     sgVertexIds.GetItem( sgIndex + 5 ) };

				int quadIndices[ 4 ];
				int originalCornerIndices[ 4 ];
				ConvertToQuad( indices, quadIndices, sgIndex, originalCornerIndices );

				for( int c = 0; c < deg; ++c )
				{
					const rid vid = sgVertexIds->GetItem( originalCornerIndices[ c ] );
					mMaxMeshMap->f[ fid ].tv[ c ] = vid;
				}
				sgIndex += 6;
			}
		}
	}
	else
	{
		std::string sWarning = "Quad texcoords import - mappingchannel ";
		sWarning += std::to_string( maxChannel );
		sWarning += " wasnt able to initiate";
	}
}

void SimplygonMax::ConvertToQuad( int* triangleVertexIndices, int* quadVertexIndices )
{
	quadVertexIndices[ 0 ] = triangleVertexIndices[ 1 ];
	quadVertexIndices[ 1 ] = triangleVertexIndices[ 2 ];
	quadVertexIndices[ 2 ] = triangleVertexIndices[ 5 ];
	quadVertexIndices[ 3 ] = triangleVertexIndices[ 0 ];
}

void SimplygonMax::ConvertToQuad( int* triangleVertexIndices, int* quadVertexIndices, int originalQuadIndexStart, int* originalQuadIndices )
{
	quadVertexIndices[ 0 ] = triangleVertexIndices[ 1 ];
	quadVertexIndices[ 1 ] = triangleVertexIndices[ 2 ];
	quadVertexIndices[ 2 ] = triangleVertexIndices[ 5 ];
	quadVertexIndices[ 3 ] = triangleVertexIndices[ 0 ];

	originalQuadIndices[ 0 ] = 1 + originalQuadIndexStart;
	originalQuadIndices[ 1 ] = 2 + originalQuadIndexStart;
	originalQuadIndices[ 2 ] = 5 + originalQuadIndexStart;
	originalQuadIndices[ 3 ] = 0 + originalQuadIndexStart;
}

bool SimplygonMax::WritebackMapping_Quad( size_t lodIndex, uint faceCount, const uint cornerCount, MNMesh& newMaxMesh, spSceneMesh sgMesh )
{
	spGeometryData sgMeshData = sgMesh->GetGeometry();

	// mesh info
	const uint vertexCount = sgMeshData->GetVertexCount();

	// do a pre-pass to find out mappable texcoords
	std::set<int> mappingChannelsInUse;

	std::map<int, int> indexedTexCoordFieldMap;
	std::map<std::string, int> unNamedTexCoordFieldMap;

	// find texcoords
	for( uint texCoordIndex = 0; texCoordIndex < SG_NUM_SUPPORTED_TEXTURE_CHANNELS; ++texCoordIndex )
	{
		spRealArray sgTexCoords = sgMeshData->GetTexCoords( texCoordIndex );
		if( sgTexCoords.IsNull() )
			continue;

		const spString rTexCoordName = sgTexCoords->GetAlternativeName();
		if( rTexCoordName.IsNullOrEmpty() )
		{
			unNamedTexCoordFieldMap.insert( std::pair<std::string, int>( std::string( "TexCoords" ) + std::to_string( texCoordIndex ), texCoordIndex ) );
			continue;
		}

		const char* cTexCoordName = rTexCoordName.c_str();
		if( !cTexCoordName )
		{
			unNamedTexCoordFieldMap.insert( std::pair<std::string, int>( std::string( "TexCoords" ) + std::to_string( texCoordIndex ), texCoordIndex ) );
			continue;
		}

		if( IsNumber( cTexCoordName ) )
		{
			const int mappingChannelInUse = atoi( cTexCoordName );
			indexedTexCoordFieldMap.insert( std::pair<int, int>( mappingChannelInUse, texCoordIndex ) );
			mappingChannelsInUse.insert( texCoordIndex );
		}
		else
		{
			unNamedTexCoordFieldMap.insert( std::pair<std::string, int>( cTexCoordName, texCoordIndex ) );
		}
	}

	std::map<int, int> indexedVertexColorFieldMap;
	std::map<std::string, int> unNamedVertexColorFieldMap;

	// find vertex colors
	for( uint vertexColorIndex = 0; vertexColorIndex < SG_NUM_SUPPORTED_COLOR_CHANNELS; ++vertexColorIndex )
	{
		spRealArray sgVertexColors = sgMeshData->GetColors( vertexColorIndex );
		if( sgVertexColors.IsNull() )
			continue;

		const spString rVertexColorName = sgVertexColors->GetAlternativeName();
		if( rVertexColorName.IsNullOrEmpty() )
		{
			unNamedVertexColorFieldMap.insert( std::pair<std::string, int>( std::string( "Colors" ) + std::to_string( vertexColorIndex ), vertexColorIndex ) );
			continue;
		}

		const char* cVertexColorName = rVertexColorName.c_str();
		if( !cVertexColorName )
		{
			unNamedVertexColorFieldMap.insert( std::pair<std::string, int>( std::string( "Colors" ) + std::to_string( vertexColorIndex ), vertexColorIndex ) );
			continue;
		}

		if( IsNumber( cVertexColorName ) )
		{
			const int mappingChannelInUse = atoi( cVertexColorName );
			indexedVertexColorFieldMap.insert( std::pair<int, int>( mappingChannelInUse, vertexColorIndex ) );
			mappingChannelsInUse.insert( vertexColorIndex );
		}
		else
		{
			unNamedVertexColorFieldMap.insert( std::pair<std::string, int>( cVertexColorName, vertexColorIndex ) );
		}
	}

	// write texcoords with valid index
	for( std::map<int, int>::const_iterator& texIterator = indexedTexCoordFieldMap.begin(); texIterator != indexedTexCoordFieldMap.end(); texIterator++ )
	{
		const int maxChannel = texIterator->first;
		const int texCoordIndex = texIterator->second;

		if( maxChannel >= 1 && maxChannel < MAX_MESHMAPS )
		{
			spRealArray sgTexCoords = sgMeshData->GetTexCoords( texCoordIndex );
			this->WriteSGTexCoordsToMaxChannel_Quad( sgTexCoords, newMaxMesh, maxChannel, cornerCount, faceCount );

			const spString rTexCoordName = sgTexCoords->GetAlternativeName();
			if( rTexCoordName.IsNullOrEmpty() )
				continue;

			const char* cTexCoordName = rTexCoordName.c_str();
			if( !cTexCoordName )
				continue;

			this->ImportedUvNameToMaxIndex.insert( std::pair<std::string, int>( cTexCoordName, maxChannel ) );
			this->ImportedMaxIndexToUv.insert( std::pair<int, std::string>( maxChannel, cTexCoordName ) );
		}
	}

	// write vertex colors with valid index
	for( std::map<int, int>::const_iterator& vertexColorIterator = indexedVertexColorFieldMap.begin(); vertexColorIterator != indexedVertexColorFieldMap.end();
	     vertexColorIterator++ )
	{
		const int maxChannel = vertexColorIterator->first;
		const int vertexColorIndex = vertexColorIterator->second;

		if( maxChannel >= -2 && maxChannel < MAX_MESHMAPS )
		{
			spRealArray sgVertexColors = sgMeshData->GetColors( vertexColorIndex );
			this->WriteSGVertexColorsToMaxChannel_Quad( sgVertexColors, newMaxMesh, maxChannel, cornerCount, faceCount );
		}
	}

	// write unmapped texcoords
	uint targetTexCoordMaxChannel = 1;
	for( std::map<std::string, int>::const_iterator& texIterator = unNamedTexCoordFieldMap.begin(); texIterator != unNamedTexCoordFieldMap.end();
	     texIterator++ )
	{
		const std::string sTexCoordName = texIterator->first;
		const int texCoordIndex = texIterator->second;

		while( true )
		{
			const std::set<int>::const_iterator& texCoordMap = mappingChannelsInUse.find( targetTexCoordMaxChannel );
			if( texCoordMap == mappingChannelsInUse.end() )
			{
				break;
			}

			targetTexCoordMaxChannel++;
		}

		if( targetTexCoordMaxChannel >= 1 && targetTexCoordMaxChannel < MAX_MESHMAPS )
		{
			spRealArray sgTexCoords = sgMeshData->GetTexCoords( texCoordIndex );
			this->WriteSGTexCoordsToMaxChannel_Quad( sgTexCoords, newMaxMesh, targetTexCoordMaxChannel, cornerCount, faceCount );

			mappingChannelsInUse.insert( targetTexCoordMaxChannel );

			const char* cTexCoordName = texIterator->first.c_str();
			if( !cTexCoordName )
				continue;

			this->ImportedUvNameToMaxIndex.insert( std::pair<std::string, int>( cTexCoordName, targetTexCoordMaxChannel ) );
			this->ImportedMaxIndexToUv.insert( std::pair<int, std::string>( targetTexCoordMaxChannel, cTexCoordName ) );
		}
	}

	// write unmapped vertex colors
	int targetVertexColorMaxChannel = 0;
	for( std::map<std::string, int>::const_iterator& vertexColorIterator = unNamedVertexColorFieldMap.begin();
	     vertexColorIterator != unNamedVertexColorFieldMap.end();
	     vertexColorIterator++ )
	{
		const std::string sVertexColorName = vertexColorIterator->first;
		const int vertexColorIndex = vertexColorIterator->second;

		while( true )
		{
			const std::set<int>::const_iterator& vertexColorMap = mappingChannelsInUse.find( targetVertexColorMaxChannel );
			if( vertexColorMap == mappingChannelsInUse.end() )
			{
				break;
			}

			targetVertexColorMaxChannel++;
		}

		if( targetVertexColorMaxChannel >= -2 && targetVertexColorMaxChannel < MAX_MESHMAPS )
		{
			spRealArray sgVertexColors = sgMeshData->GetColors( vertexColorIndex );
			this->WriteSGVertexColorsToMaxChannel_Quad( sgVertexColors, newMaxMesh, targetVertexColorMaxChannel, cornerCount, faceCount );

			mappingChannelsInUse.insert( targetVertexColorMaxChannel );
		}
	}

	return true;
}

// writes Simplygon vertex-colors back to Max
void SimplygonMax::WriteSGVertexColorsToMaxChannel_Quad( spRealArray sgVertexColors, MNMesh& mMaxMesh, int maxChannel, uint cornerCount, uint faceCount )
{
	// setup objects
	// mMaxMesh.setMapSupport( maxChannel );
	mMaxMesh.InitMap( maxChannel );

	// setup and copy the corners
	spRidArray sgVertexIds = sg->CreateRidArray();
	spRealArray sgPackedVertexColors = spRealArray::SafeCast( sgVertexColors->NewPackedCopy( sgVertexIds ) );

	MNMap* mMaxMeshMap = mMaxMesh.M( maxChannel );
	if( mMaxMeshMap != nullptr )
	{
		// setup data channels
		// mMaxMeshMap.setNumFaces( faceCount );
		mMaxMeshMap->setNumVerts( sgPackedVertexColors->GetTupleCount() );

		// copy vertex data
		for( uint vid = 0; vid < sgPackedVertexColors->GetTupleCount(); ++vid )
		{
			// get the tuple
			spRealData sgColor = sgPackedVertexColors->GetTuple( vid );

			mMaxMeshMap->v[ vid ].x = sgColor[ 0 ];
			mMaxMeshMap->v[ vid ].y = sgColor[ 1 ];
			mMaxMeshMap->v[ vid ].z = sgColor[ 2 ];
		}

		uint sgIndex = 0;
		for( uint fid = 0; fid < faceCount; ++fid )
		{
			int deg = mMaxMeshMap->f[ fid ].deg;

			if( deg == 3 )
			{
				for( int c = 0; c < deg; ++c )
				{
					const rid cid = sgVertexIds->GetItem( sgIndex++ );
					mMaxMeshMap->f[ fid ].tv[ c ] = cid;
				}
			}
			if( deg == 4 )
			{
				int indices[ 6 ] = { sgVertexIds.GetItem( sgIndex + 0 ),
				                     sgVertexIds.GetItem( sgIndex + 1 ),
				                     sgVertexIds.GetItem( sgIndex + 2 ),
				                     sgVertexIds.GetItem( sgIndex + 3 ),
				                     sgVertexIds.GetItem( sgIndex + 4 ),
				                     sgVertexIds.GetItem( sgIndex + 5 ) };

				int quadIndices[ 4 ];
				int originalCornerIndices[ 4 ];
				ConvertToQuad( indices, quadIndices, sgIndex, originalCornerIndices );

				for( int c = 0; c < deg; ++c )
				{
					const rid cid = sgVertexIds->GetItem( originalCornerIndices[ c ] );
					mMaxMeshMap->f[ fid ].tv[ c ] = cid;
				}
				sgIndex += 6;
			}
		}
	}
	else
	{
		std::string sWarning = "Quad vertexcolors import - mappingchannel ";
		sWarning += std::to_string( maxChannel );
		sWarning += " wasnt able to initiate";
		LogToWindow( ConstCharPtrToLPCWSTRr( sWarning.c_str() ), ErrorType::Warning );
	}
}

// writes Simplygon tex-coords back to Max
void SimplygonMax::WriteSGTexCoordsToMaxChannel( spRealArray sgTexCoords, Mesh& mMaxMesh, int maxChannel, uint cornerCount, uint triangleCount )
{
	// setup objects
	mMaxMesh.setMapSupport( maxChannel );

	UVWMapper mUVWMapper;
	mMaxMesh.ApplyMapper( mUVWMapper, maxChannel );

	// setup and copy the corners
	spRidArray sgVertexIds = sg->CreateRidArray();
	spRealArray sgPackedTexCoords = spRealArray::SafeCast( sgTexCoords->NewPackedCopy( sgVertexIds ) );

	MeshMap& mMaxMeshMap = mMaxMesh.Map( maxChannel );

	// setup data channels
	mMaxMeshMap.setNumFaces( triangleCount );
	mMaxMeshMap.setNumVerts( sgPackedTexCoords->GetTupleCount() );

	// copy vertex data
	for( uint vid = 0; vid < sgPackedTexCoords->GetTupleCount(); ++vid )
	{
		// get the tuple
		spRealData sgTexCoord = sgPackedTexCoords->GetTuple( vid );

		// remap
		if( this->TextureCoordinateRemapping == 0 )
		{
			mMaxMeshMap.tv[ vid ].x = sgTexCoord[ 0 ];
			mMaxMeshMap.tv[ vid ].y = sgTexCoord[ 1 ];
			mMaxMeshMap.tv[ vid ].z = 0;
		}
		else if( this->TextureCoordinateRemapping == 1 )
		{
			mMaxMeshMap.tv[ vid ].x = sgTexCoord[ 0 ];
			mMaxMeshMap.tv[ vid ].z = sgTexCoord[ 1 ];
			mMaxMeshMap.tv[ vid ].y = 0;
		}
		else if( this->TextureCoordinateRemapping == 2 )
		{
			mMaxMeshMap.tv[ vid ].y = sgTexCoord[ 0 ];
			mMaxMeshMap.tv[ vid ].z = sgTexCoord[ 1 ];
			mMaxMeshMap.tv[ vid ].x = 0;
		}
	}

	// copy face data
	for( uint tid = 0; tid < triangleCount; ++tid )
	{
		for( int c = 0; c < 3; ++c )
		{
			const rid vid = sgVertexIds->GetItem( tid * 3 + c );
			mMaxMeshMap.tf[ tid ].t[ c ] = vid;
		}
	}
}

// writes Simplygon vertex-colors back to Max
void SimplygonMax::WriteSGVertexColorsToMaxChannel( spRealArray sgVertexColors, Mesh& mMaxMesh, int maxChannel, uint cornerCount, uint triangleCount )
{
	// setup objects
	mMaxMesh.setMapSupport( maxChannel );

	// setup and copy the corners
	spRidArray sgVertexIds = sg->CreateRidArray();
	spRealArray sgPackedVertexColors = spRealArray::SafeCast( sgVertexColors->NewPackedCopy( sgVertexIds ) );

	MeshMap& mMaxMeshMap = mMaxMesh.Map( maxChannel );

	// setup data channels
	mMaxMeshMap.setNumFaces( triangleCount );
	mMaxMeshMap.setNumVerts( sgPackedVertexColors->GetTupleCount() );

	// copy vertex data
	for( uint vid = 0; vid < sgPackedVertexColors->GetTupleCount(); ++vid )
	{
		// get the tuple
		spRealData sgColor = sgPackedVertexColors->GetTuple( vid );

		mMaxMeshMap.tv[ vid ].x = sgColor[ 0 ];
		mMaxMeshMap.tv[ vid ].y = sgColor[ 1 ];
		mMaxMeshMap.tv[ vid ].z = sgColor[ 2 ];
	}

	// copy face data
	for( uint tid = 0; tid < triangleCount; ++tid )
	{
		for( uint c = 0; c < 3; ++c )
		{
			mMaxMeshMap.tf[ tid ].t[ c ] = sgVertexIds->GetItem( tid * 3 + c );
		}
	}
}

// returns true if string is a number
bool IsNumber( const std::string& sPossibleNumber )
{
	// check length of incoming string
	const size_t length = sPossibleNumber.length();
	if( length > 0 )
	{
		// if there is a minus sign at the start of the string,
		// continue to verify that remaining characters are valid numbers.
		// if not negative, verify that all characters are valid numbers.
		const size_t startIndex = ( sPossibleNumber[ 0 ] == '-' && length > 1 ) ? 1 : 0;
		return sPossibleNumber.find_first_not_of( "0123456789", startIndex ) == std::string::npos;
	}

	return false;
}

// Writes Simplygon vertex-colors and tex-coords back to Max
bool SimplygonMax::WritebackMapping( size_t lodIndex, Mesh& newMaxMesh, spSceneMesh sgMesh )
{
	spGeometryData sgMeshData = sgMesh->GetGeometry();

	// mesh info
	const uint vertexCount = sgMeshData->GetVertexCount();
	const uint triangleCount = sgMeshData->GetTriangleCount();
	const uint cornerCount = triangleCount * 3;

	// do a pre-pass to find out mappable texcoords
	std::set<int> mappingChannelsInUse;

	std::map<int, int> indexedTexCoordFieldMap;
	std::map<std::string, int> unNamedTexCoordFieldMap;

	// find texcoords
	for( uint texCoordIndex = 0; texCoordIndex < SG_NUM_SUPPORTED_TEXTURE_CHANNELS; ++texCoordIndex )
	{
		spRealArray sgTexCoords = sgMeshData->GetTexCoords( texCoordIndex );
		if( sgTexCoords.IsNull() )
			continue;

		const spString rTexCoordName = sgTexCoords->GetAlternativeName();
		if( rTexCoordName.IsNullOrEmpty() )
		{
			unNamedTexCoordFieldMap.insert( std::pair<std::string, int>( std::string( "TexCoords" ) + std::to_string( texCoordIndex ), texCoordIndex ) );
			continue;
		}

		const char* cTexCoordName = rTexCoordName.c_str();
		if( !cTexCoordName )
		{
			unNamedTexCoordFieldMap.insert( std::pair<std::string, int>( std::string( "TexCoords" ) + std::to_string( texCoordIndex ), texCoordIndex ) );
			continue;
		}

		if( IsNumber( cTexCoordName ) )
		{
			const int mappingChannelInUse = atoi( cTexCoordName );
			indexedTexCoordFieldMap.insert( std::pair<int, int>( mappingChannelInUse, texCoordIndex ) );
			mappingChannelsInUse.insert( mappingChannelInUse );
		}
		else
		{
			unNamedTexCoordFieldMap.insert( std::pair<std::string, int>( cTexCoordName, texCoordIndex ) );
		}
	}

	std::map<int, int> indexedVertexColorFieldMap;
	std::map<std::string, int> unNamedVertexColorFieldMap;

	// find vertex colors
	for( uint vertexColorIndex = 0; vertexColorIndex < SG_NUM_SUPPORTED_COLOR_CHANNELS; ++vertexColorIndex )
	{
		spRealArray sgVertexColors = sgMeshData->GetColors( vertexColorIndex );
		if( sgVertexColors.IsNull() )
			continue;

		const spString rVertexColorName = sgVertexColors->GetAlternativeName();
		if( rVertexColorName.IsNullOrEmpty() )
		{
			unNamedVertexColorFieldMap.insert( std::pair<std::string, int>( std::string( "Colors" ) + std::to_string( vertexColorIndex ), vertexColorIndex ) );
			continue;
		}

		const char* cVertexColorName = rVertexColorName.c_str();
		if( !cVertexColorName )
		{
			unNamedVertexColorFieldMap.insert( std::pair<std::string, int>( std::string( "Colors" ) + std::to_string( vertexColorIndex ), vertexColorIndex ) );
			continue;
		}

		if( IsNumber( cVertexColorName ) )
		{
			const int mappingChannelInUse = atoi( cVertexColorName );
			indexedVertexColorFieldMap.insert( std::pair<int, int>( mappingChannelInUse, vertexColorIndex ) );
			mappingChannelsInUse.insert( mappingChannelInUse );
		}
		else
		{
			unNamedVertexColorFieldMap.insert( std::pair<std::string, int>( cVertexColorName, vertexColorIndex ) );
		}
	}

	// write texcoords with valid index
	for( std::map<int, int>::const_iterator& texIterator = indexedTexCoordFieldMap.begin(); texIterator != indexedTexCoordFieldMap.end(); texIterator++ )
	{
		const int maxChannel = texIterator->first;
		const int texCoordIndex = texIterator->second;

		if( maxChannel >= 1 && maxChannel < MAX_MESHMAPS )
		{
			spRealArray sgTexCoords = sgMeshData->GetTexCoords( texCoordIndex );
			this->WriteSGTexCoordsToMaxChannel( sgTexCoords, newMaxMesh, maxChannel, cornerCount, triangleCount );

			const spString rTexCoordName = sgTexCoords->GetAlternativeName();
			if( rTexCoordName.IsNullOrEmpty() )
				continue;

			const char* cTexCoordName = rTexCoordName.c_str();
			if( !cTexCoordName )
				continue;

			this->ImportedUvNameToMaxIndex.insert( std::pair<std::string, int>( cTexCoordName, maxChannel ) );
			this->ImportedMaxIndexToUv.insert( std::pair<int, std::string>( maxChannel, cTexCoordName ) );
		}
	}

	// write vertex colors with valid index
	for( std::map<int, int>::const_iterator& vertexColorIterator = indexedVertexColorFieldMap.begin(); vertexColorIterator != indexedVertexColorFieldMap.end();
	     vertexColorIterator++ )
	{
		const int maxChannel = vertexColorIterator->first;
		const int vertexColorIndex = vertexColorIterator->second;

		if( maxChannel >= -2 && maxChannel < MAX_MESHMAPS )
		{
			spRealArray sgVertexColors = sgMeshData->GetColors( vertexColorIndex );
			this->WriteSGVertexColorsToMaxChannel( sgVertexColors, newMaxMesh, maxChannel, cornerCount, triangleCount );
		}
	}

	// write unmapped texcoords
	uint targetTexCoordMaxChannel = 1;
	for( std::map<std::string, int>::const_iterator& texIterator = unNamedTexCoordFieldMap.begin(); texIterator != unNamedTexCoordFieldMap.end();
	     texIterator++ )
	{
		const std::string sTexCoordName = texIterator->first;
		const int texCoordIndex = texIterator->second;

		while( true )
		{
			const std::set<int>::const_iterator& texCoordMap = mappingChannelsInUse.find( targetTexCoordMaxChannel );
			if( texCoordMap == mappingChannelsInUse.end() )
			{
				break;
			}

			targetTexCoordMaxChannel++;
		}

		if( targetTexCoordMaxChannel >= 1 && targetTexCoordMaxChannel < MAX_MESHMAPS )
		{
			spRealArray sgTexCoords = sgMeshData->GetTexCoords( texCoordIndex );
			this->WriteSGTexCoordsToMaxChannel( sgTexCoords, newMaxMesh, targetTexCoordMaxChannel, cornerCount, triangleCount );

			mappingChannelsInUse.insert( targetTexCoordMaxChannel );

			const char* cTexCoordName = texIterator->first.c_str();
			if( !cTexCoordName )
				continue;

			this->ImportedUvNameToMaxIndex.insert( std::pair<std::string, int>( cTexCoordName, targetTexCoordMaxChannel ) );
			this->ImportedMaxIndexToUv.insert( std::pair<int, std::string>( targetTexCoordMaxChannel, cTexCoordName ) );
		}
	}

	// write unmapped vertex colors
	int targetVertexColorMaxChannel = 0;
	for( std::map<std::string, int>::const_iterator& vertexColorIterator = unNamedVertexColorFieldMap.begin();
	     vertexColorIterator != unNamedVertexColorFieldMap.end();
	     vertexColorIterator++ )
	{
		const std::string sVertexColorName = vertexColorIterator->first;
		const int vertexColorIndex = vertexColorIterator->second;

		while( true )
		{
			const std::set<int>::const_iterator& vertexColorMap = mappingChannelsInUse.find( targetVertexColorMaxChannel );
			if( vertexColorMap == mappingChannelsInUse.end() )
			{
				break;
			}

			targetVertexColorMaxChannel++;
		}

		if( targetVertexColorMaxChannel >= -2 && targetVertexColorMaxChannel < MAX_MESHMAPS )
		{
			spRealArray sgVertexColors = sgMeshData->GetColors( vertexColorIndex );
			this->WriteSGVertexColorsToMaxChannel( sgVertexColors, newMaxMesh, targetVertexColorMaxChannel, cornerCount, triangleCount );

			mappingChannelsInUse.insert( targetVertexColorMaxChannel );
		}
	}

	return true;
}

// creates a new Max material based on Simplygon's material
Mtl* SimplygonMax::CreateMaterial( spScene sgProcessedScene,
                                   spSceneMesh sgProcessedMesh,
                                   size_t lodIndex,
                                   std::basic_string<TCHAR> sgMeshName,
                                   std::basic_string<TCHAR> sgMaterialName,
                                   uint globalMaterialIndex )
{
	spGeometryData sgMeshData = sgProcessedMesh->GetGeometry();

	TCHAR tNewMaterialName[ MAX_PATH ] = { 0 };
	TSTR tsMaterialName = _T("");

	_stprintf_s( tNewMaterialName, MAX_PATH, _T("%s"), sgMaterialName.c_str() );
	tsMaterialName = tNewMaterialName;

	spRidArray sgMaterialIds = sgMeshData->GetMaterialIds();

	if( sgMaterialIds.IsNull() )
	{
#if MAX_VERSION_MAJOR < 23
		return SetupMaxStdMaterial( sgProcessedScene, sgMeshName, Simplygon::NullPtr, tsMaterialName, tNewMaterialName );
#else
		return SetupPhysicalMaterial( sgProcessedScene, sgMeshName, Simplygon::NullPtr, tsMaterialName, tNewMaterialName );
#endif
	}

	spMaterialTable sgMaterialTable = sgProcessedScene->GetMaterialTable();
	spMaterial sgMaterial = sgMaterialTable->GetMaterial( globalMaterialIndex );
	const spString rMaterialId = sgMaterial->GetMaterialGUID();
	const std::string sMaterialId = rMaterialId.c_str();

	// see if material already exists
	if( this->CachedMaterialInfos.size() > 0 )
	{
		for( uint materialInfoIndex = 0; materialInfoIndex < this->CachedMaterialInfos.size(); ++materialInfoIndex )
		{
			// if already in list return reference, else continue normally
			if( this->CachedMaterialInfos[ materialInfoIndex ].MaterialId == sMaterialId )
			{
				this->materialInfoHandler->Add( sgMeshName, this->CachedMaterialInfos[ materialInfoIndex ].MaterialName );
#if MAX_VERSION_MAJOR < 23
				return this->CachedMaterialInfos[ materialInfoIndex ].MaxMaterialReference;
#else
				return this->CachedMaterialInfos[ materialInfoIndex ].MaxPhysicalMaterialReference;
#endif
			}
		}
	}

	if( this->UseNewMaterialSystem )
	{
		ShadingNetworkProxyWriteBack* writebackMaterialProxy = GetProxyShadingNetworkWritebackMaterial();

		// found definition for proxy, map it back
		if( writebackMaterialProxy != nullptr )
		{
			std::map<std::basic_string<TCHAR>, int> tDXParamBitmapLookup;

			Mtl* mMaxMaterial = nullptr;

			// mtl reference was not setup during export to Simplygon
			std::map<std::basic_string<TCHAR>, Mtl*>::const_iterator& tShaderIterator = this->UsedShaderReferences.find( tNewMaterialName );
			if( tShaderIterator != UsedShaderReferences.end() )
			{
				mMaxMaterial = tShaderIterator->second;
			}
			else
			{
				// get unique material name
				std::basic_string<TCHAR> tNewUniqueMaterialName = GetUniqueMaterialName( tNewMaterialName );

				// create a DirectX shader material
				Mtl* mMaxMaterialInstance = (Mtl*)this->MaxInterface->CreateInstance( SClass_ID( MATERIAL_CLASS_ID ), Class_ID( 249140708, 1630788338 ) );
				this->UsedShaderReferences.insert( std::pair<std::basic_string<TCHAR>, Mtl*>( tNewMaterialName, mMaxMaterialInstance ) );

				// assign new name
				mMaxMaterialInstance->SetName( tNewUniqueMaterialName.c_str() );

				// get DirectX interface
				IDxMaterial3* mMaxDXMaterial = (IDxMaterial3*)mMaxMaterialInstance->GetInterface( IDXMATERIAL_INTERFACE );

				// get path to custom HLSL shader file
				IParameterManager* mParameterManager = mMaxDXMaterial->GetCurrentParameterManager();
				IFileResolutionManager* maxFileResolutionManager = IFileResolutionManager::GetInstance();
				MaxSDK::Util::Path mShaderFilePath( writebackMaterialProxy->GetEffectFilePath().c_str() );

				if( !maxFileResolutionManager->GetFullFilePath( mShaderFilePath, MaxSDK::AssetManagement::kBitmapAsset ) )
				{
					return nullptr;
				}

				// get shader effect parameter block
				IParamBlock2* effectFileParamBlock = mMaxMaterialInstance->GetParamBlock( kEffectFilePBlockIndex );

				// set DirectX material to use custom HLSL shader file
				const bool bEffectFileSet =
				    effectFileParamBlock->SetValue( kEffectFileParamID, this->MaxInterface->GetTime(), mShaderFilePath.GetString() ) == TRUE;
				if( !bEffectFileSet )
				{
					return nullptr;
				}

				int numBitmaps = 0;

				// map Simplygon properties back to shader
				for( int paramIndex = 0; paramIndex < mParameterManager->GetNumberOfParams(); ++paramIndex )
				{
					std::basic_string<TCHAR> tParamName = std::basic_string<TCHAR>( mParameterManager->GetParamName( paramIndex ) );
					const int paramType = mParameterManager->GetParamType( paramIndex );
					if( paramType == 1010 || paramType == 1009 || paramType == IParameterManager::kPType_Texture )
					{
						tDXParamBitmapLookup[ tParamName ] = paramIndex;
					}
				}

				// for each channel casted in material
				for( uint channelIndex = 0; channelIndex < sgMaterial->GetMaterialChannelCount(); ++channelIndex )
				{
					const spString rChannelName = sgMaterial->GetMaterialChannelFromIndex( channelIndex );
					const char* cChannelName = rChannelName.c_str();
					std::basic_string<TCHAR> sChannelName = ConstCharPtrToLPCTSTR( cChannelName );

					spShadingNode sgExitNode = sgMaterial->GetShadingNetwork( cChannelName );

					// if no shading node, skip
					if( sgExitNode.IsNull() )
						continue;

					// only exit node is mapped for now
					if( writebackMaterialProxy->SGChannelToShadingNode.find( sChannelName ) != writebackMaterialProxy->SGChannelToShadingNode.end() )
					{
						const int parameterIndex = tDXParamBitmapLookup[ writebackMaterialProxy->SGChannelToShadingNode[ sChannelName ] ];
						std::basic_string<TCHAR> tParameterName = mParameterManager->GetParamName( parameterIndex );

						spShadingTextureNode sgTextureNode = FindUpstreamTextureNode( sgExitNode );

						if( !sgTextureNode.IsNull() )
						{
							this->SetupMaxDXTexture( sgProcessedScene,
							                         sgMaterial,
							                         cChannelName,
							                         mMaxMaterialInstance,
							                         mMaxDXMaterial,
							                         tParameterName,
							                         sgTextureNode,
							                         writebackMaterialProxy->SGChannelToShadingNode[ sChannelName ],
							                         sgMeshName,
							                         tNewMaterialName );
						}
					}
				}

				this->materialInfoHandler->Add( std::basic_string<TCHAR>( sgMeshName ) );

				mMaxDXMaterial->ReloadDXEffect();

				return mMaxMaterialInstance;
			}

			if( mMaxMaterial )
			{
				this->materialInfoHandler->Add( std::basic_string<TCHAR>( sgMeshName ), std::basic_string<TCHAR>( mMaxMaterial->GetName() ) );
			}

			return mMaxMaterial;
		}
		else
		{
#if MAX_VERSION_MAJOR < 23
			return SetupMaxStdMaterial( sgProcessedScene, sgMeshName, sgMaterial, tsMaterialName, tNewMaterialName );
#else
			return SetupPhysicalMaterial( sgProcessedScene, sgMeshName, sgMaterial, tsMaterialName, tNewMaterialName );
#endif
		}
	}
	else
	{
#if MAX_VERSION_MAJOR < 23
		return SetupMaxStdMaterial( sgProcessedScene, sgMeshName, sgMaterial, tsMaterialName, tNewMaterialName );
#else
		return SetupPhysicalMaterial( sgProcessedScene, sgMeshName, sgMaterial, tsMaterialName, tNewMaterialName );
#endif
	}

	return nullptr;
}

// Set Max texture gamma
void SetBitmapTextureGamma( BitmapTex* mBitmapTex, float requestedGamma = 1.0f )
{
	IParamBlock2* mPB2 = mBitmapTex->GetParamBlock( 0 );

	// get the bitmap parameter
	const int bitmapIndex = mPB2->GetDesc()->NameToIndex( _T("bitmap") );
	const ParamID mBitmapId = mPB2->GetDesc()->IndextoID( bitmapIndex );
	PBBitmap* mPBBitmap = mPB2->GetBitmap( mBitmapId );

	// get current gamma
	const float currentTextureGamma = mBitmapTex->GetBitmap( 0 )->Gamma();

	if( currentTextureGamma != requestedGamma )
	{
		// tell the bitmap info struct to use a gamma value
		mPBBitmap->bi.SetCustomFlag( BMM_CUSTOM_GAMMA );

		// set a gamma value
		mPBBitmap->bi.SetCustomGamma( requestedGamma );
		mBitmapTex->ReloadBitmapAndUpdate();
	}
}

// Get Max texture gamma
float GetBitmapTextureGamma( BitmapTex* mBitmapTex )
{
	IParamBlock2* mPB2 = mBitmapTex->GetParamBlock( 0 );

	// get the bitmap parameter
	const int bitmapIndex = mPB2->GetDesc()->NameToIndex( _T("bitmap") );
	const ParamID mBitmapId = mPB2->GetDesc()->IndextoID( bitmapIndex );
	PBBitmap* mPBBitmap = mPB2->GetBitmap( mBitmapId );

	bool bCustGamma = false;
	if( mPBBitmap != nullptr )
	{
		const DWORD custFlags = mPBBitmap->bi.GetCustomFlags();
		bCustGamma = ( custFlags & BMM_CUSTOM_GAMMA ) != 0;
	}

	if( bCustGamma )
	{
		const float gamma = mPBBitmap->bi.GetCustomGamma();
		return gamma;
	}
	else
	{
		// get current gamma
		Bitmap* mBitmap = mBitmapTex->GetBitmap( 0 );

		float gamma = 1.0;
		if( mBitmap != nullptr )
		{
			gamma = mBitmap->Gamma();
		}
		return gamma;
	}
}

// Sets the gamma for the specific bitmap.
void SetBitmapGamma( PBBitmap* pbBitmap, float requestedGamma = 1.0f )
{
	// Get current gamma
	float currentTextureGamma = pbBitmap->bi.Gamma();

	if( currentTextureGamma != requestedGamma )
	{
		// Tell the bitmap info struct to use a gamma value
		pbBitmap->bi.SetCustomFlag( BMM_CUSTOM_GAMMA );

		// Set a gamma value
		pbBitmap->bi.SetCustomGamma( requestedGamma );
	}
}

// Gets the gamma for the specific bitmap.
float GetBitmapGamma( PBBitmap* pbBitmap )
{
	bool hasCustomGamma = false;
	if( pbBitmap != NULL )
	{
		DWORD customFlags = pbBitmap->bi.GetCustomFlags();
		hasCustomGamma = ( customFlags & BMM_CUSTOM_GAMMA ) != 0;
	}
	else
	{
		return 1.0f;
	}

	// If custom gamma is set, return this value
	if( hasCustomGamma )
	{
		float gamma = pbBitmap->bi.GetCustomGamma();
		return gamma;
	}
	else
	{
		// Get current gamma from bitmap,
		// if the bitmap is valid then return the value,
		// otherwise default to 1.0f
		Bitmap* bitMap = pbBitmap->bm;

		float gamma = 1.0f;
		if( bitMap != NULL )
		{
			gamma = bitMap->Gamma();
		}

		return gamma;
	}
}

// Import texture from Simplygon material channel
bool SimplygonMax::ImportMaterialTexture( spScene sgProcessedScene,
                                          spMaterial sgMaterial,
                                          const TCHAR* tNodeName,
                                          const TCHAR* tChannelName,
                                          int maxChannelId,
                                          BitmapTex** mBitmapTex,
                                          std::basic_string<TCHAR> tMeshNameOverride,
                                          std::basic_string<TCHAR> tMaterialNameOverride )
{
	const IPathConfigMgr* mPathManager = IPathConfigMgr::GetPathConfigMgr();
	const std::basic_string<TCHAR> tMaxBitmapDirectory = mPathManager->GetDir( APP_IMAGE_DIR );

	const char* cChannelName = LPCTSTRToConstCharPtr( tChannelName );

	// if channel exists
	if( sgMaterial->HasMaterialChannel( cChannelName ) )
	{
		// override
		std::basic_string<TCHAR> tTextureTargetDirectory = tMaxBitmapDirectory;
		if( this->TextureOutputDirectory.length() > 0 )
		{
			const bool bFolderCreated = CreateFolder( this->TextureOutputDirectory.c_str() );
			if( !bFolderCreated )
			{
				std::basic_string<TCHAR> tWarningMessage =
				    _T( "Warning! - Failed to set up the texture path override, please verify the input string and that Max has the required admin rights "
				        "for accessing the specified location. Textures will be copied to the default path." );
				this->LogMessageToScriptEditor( tWarningMessage );
			}
			else
			{
				tTextureTargetDirectory = this->TextureOutputDirectory;
			}
		}

		// get texture directory
		std::basic_string<TCHAR> tBakedTextureDirectory = this->workDirectoryHandler->GetBakedTexturesPath();

		// get texture table for this specific scene
		spTextureTable sgTextureTable = sgProcessedScene->GetTextureTable();

		// fetch channel parameters
		spShadingNode sgExitNode = sgMaterial->GetShadingNetwork( cChannelName );
		if( !sgExitNode.IsNull() )
		{
			std::map<std::basic_string<TCHAR>, spShadingTextureNode> sgShadingNodeNameToTextureNode;
			FindAllUpStreamTextureNodes( sgExitNode, sgShadingNodeNameToTextureNode );

			if( sgShadingNodeNameToTextureNode.size() > 0 )
			{
				// use texture
				spShadingTextureNode sgTextureNode = sgShadingNodeNameToTextureNode.begin()->second;

				// empty name check
				spString rTextureNameToFind = sgTextureNode->GetTextureName();
				if( rTextureNameToFind.IsNullOrEmpty() )
				{
					std::basic_string<TCHAR> tErrorMessage =
					    _T("Error (Simplygon): Found a ShadingTextureNode with invalid (NULL or empty) TextureName, unable to map texture on ");
					tErrorMessage += ConstCharPtrToLPCTSTR( rTextureNameToFind.c_str() );
					tErrorMessage += _T(") with invalid (NULL or empty) UV-set, unable to map texture on ");
					tErrorMessage += tChannelName;
					tErrorMessage += _T(" channel.\n");

					this->LogMessageToScriptEditor( tErrorMessage );
					return false;
				}

				// empty tex coord level check
				spString rTextureUvSet = sgTextureNode->GetTexCoordName();
				if( rTextureUvSet.IsNullOrEmpty() )
				{
					std::basic_string<TCHAR> tErrorMessage = _T("Error (Simplygon): Found a ShadingTextureNode (");
					tErrorMessage += ConstCharPtrToLPCTSTR( rTextureNameToFind.c_str() );
					tErrorMessage += _T(") with invalid (NULL or empty) UV-set, unable to map texture on ");
					tErrorMessage += tChannelName;
					tErrorMessage += _T(" channel.\n");

					this->LogMessageToScriptEditor( tErrorMessage );
					return false;
				}

				// use texture
				std::basic_string<TCHAR> tTextureNameToFind = ConstCharPtrToLPCTSTR( rTextureNameToFind );
				std::basic_string<TCHAR> tTextureUvSet = ConstCharPtrToLPCTSTR( rTextureUvSet );

				spTexture sgTexture = sgTextureTable->FindTexture( LPCTSTRToConstCharPtr( tTextureNameToFind.c_str() ) );

				// check for empty texture
				if( sgTexture.IsNull() )
				{
					std::basic_string<TCHAR> tErrorMessage = _T("Error (Simplygon): Could not resolve texture ");
					tErrorMessage += tTextureNameToFind;
					tErrorMessage += _T(" on ");
					tErrorMessage += tChannelName;
					tErrorMessage += _T( " channel.\n" );

					this->LogMessageToScriptEditor( tErrorMessage );
					return false;
				}

				if( sgTexture->GetFilePath().IsNullOrEmpty() && sgTexture->GetImageData().IsNull() )
				{
					std::basic_string<TCHAR> tErrorMessage = _T("Error (Simplygon): Invalid path / data (NULL or empty) for texture: ");
					tErrorMessage += tTextureNameToFind;
					tErrorMessage += _T(" on ");
					tErrorMessage += tChannelName;
					tErrorMessage += _T( ".\n" );

					this->LogMessageToScriptEditor( tErrorMessage );
					return false;
				}

				std::basic_string<TCHAR> tTextureName = ConstCharPtrToLPCTSTR( sgTexture->GetName() );
				std::basic_string<TCHAR> tTexturePath = sgTexture->GetImageData().IsNull() ? ConstCharPtrToLPCTSTR( sgTexture->GetFilePath() ) : _T("");
				std::basic_string<TCHAR> tSourceFilePath = Combine( tBakedTextureDirectory, tTexturePath );

				if( !sgTexture->GetImageData().IsNull() )
				{
					// Embedded data, should be written to file
					tSourceFilePath = Combine( tSourceFilePath, tTextureName );
					if( ExportTextureToFile( sg, sgTexture, LPCTSTRToConstCharPtr( tSourceFilePath.c_str() ) ) )
					{
						sgTexture->SetImageData( Simplygon::NullPtr );
						tSourceFilePath = ConstCharPtrToLPCWSTRr( sgTexture.GetFilePath().c_str() );
					}
				}

				std::basic_string<TCHAR> sFinalImportTexturePath = tSourceFilePath;
				if( this->copyTextures )
				{
					// the name of the imported texture is based on the name of the node
					std::basic_string<TCHAR> tImportTextureName = tTextureName + GetExtensionOfFile( tSourceFilePath );
					ReplaceInvalidCharacters( tImportTextureName, '_' );

					std::basic_string<TCHAR> tImportTexturePath = std::basic_string<TCHAR>( tTextureTargetDirectory.c_str() );
					tImportTexturePath = Combine( tImportTexturePath, tImportTextureName );

					sFinalImportTexturePath = tImportTexturePath;
					// make sure to get a unique name
					if( this->UseNonConflictingTextureNames )
					{
						sFinalImportTexturePath = GetNonConflictingNameInPath( sFinalImportTexturePath.c_str() );
					}

					uint numCopyRetries = 0;

					// copy the file
					while( CopyFile( tSourceFilePath.c_str(), sFinalImportTexturePath.c_str(), FALSE ) == FALSE )
					{
						const DWORD dwErrorCode = GetLastError();

						// if in use (mostly caused by multiple instances racing)
						// then try again
						if( dwErrorCode == ERROR_SHARING_VIOLATION && numCopyRetries < MaxNumCopyRetries )
						{
							Sleep( 100 );
							numCopyRetries++;
							continue;
						}

						TCHAR tErrorCode[ 64 ] = { 0 };
						_stprintf_s( tErrorCode, 64, _T("%u"), dwErrorCode );

						std::basic_string<TCHAR> tErrorMessage = _T("Error (Simplygon): - could not copy texture:\n ");
						tErrorMessage += tTexturePath;
						tErrorMessage += _T("\n ");
						tErrorMessage += sFinalImportTexturePath;
						tErrorMessage += _T("\n Code: ");
						tErrorMessage += tErrorCode;
						tErrorMessage += _T("\n");

						this->LogMessageToScriptEditor( tErrorMessage );

						return false;
					}
				}

				BitmapTex* mNewBitmapTex = NewDefaultBitmapTex();

				// check what kind of channel it is
				if( maxChannelId == ID_DI )
				{
					mNewBitmapTex->SetAlphaAsRGB( false );
					mNewBitmapTex->SetAlphaAsMono( false );
					mNewBitmapTex->SetAlphaSource( ALPHA_NONE );
				}

				else if( maxChannelId == ID_OP )
				{
					mNewBitmapTex->SetAlphaAsRGB( false );
					mNewBitmapTex->SetAlphaAsMono( false );
					mNewBitmapTex->SetAlphaSource( ALPHA_NONE );
				}

				mNewBitmapTex->SetMapName( sFinalImportTexturePath.c_str() );

				if( sgTextureNode->GetColorSpaceOverride() == Simplygon::EImageColorSpace::sRGB )
				{
					SetBitmapTextureGamma( mNewBitmapTex, 2.2f );
				}
				else
				{
					SetBitmapTextureGamma( mNewBitmapTex, 1.0f );
				}

				// post step
				if( maxChannelId == ID_BU )
				{
					SetBitmapTextureGamma( mNewBitmapTex, 1.0f );
				}

				int maxMappingChannel = 1;
				StdUVGen* mUVGen = mNewBitmapTex->GetUVGen();
				if( mUVGen )
				{
					spString rTexCoordName = sgTextureNode->GetTexCoordName();
					const char* cTexCoordName = rTexCoordName.c_str();

					std::map<std::string, int>::const_iterator& texCoordMap = this->ImportedUvNameToMaxIndex.find( cTexCoordName );
					if( texCoordMap != this->ImportedUvNameToMaxIndex.end() )
					{
						maxMappingChannel = texCoordMap->second;
						mUVGen->SetMapChannel( maxMappingChannel );
					}
				}

				*mBitmapTex = mNewBitmapTex;

				this->materialInfoHandler->Add( tMeshNameOverride, tMaterialNameOverride, tChannelName, sFinalImportTexturePath, maxMappingChannel );
			}
		}
	}

	return true;
}

PBBitmap* SimplygonMax::ImportMaterialTexture( spScene sgProcessedScene,
                                               spMaterial sgMaterial,
                                               const TCHAR* tNodeName,
                                               const TCHAR* tChannelName,
                                               std::basic_string<TCHAR> tMeshNameOverride,
                                               std::basic_string<TCHAR> tMaterialNameOverride )
{
	const IPathConfigMgr* mPathManager = IPathConfigMgr::GetPathConfigMgr();
	const std::basic_string<TCHAR> tMaxBitmapDirectory = mPathManager->GetDir( APP_IMAGE_DIR );

	const char* cChannelName = LPCTSTRToConstCharPtr( tChannelName );

	// if channel exists
	if( sgMaterial->HasMaterialChannel( cChannelName ) )
	{
		// override
		std::basic_string<TCHAR> tTextureTargetDirectory = tMaxBitmapDirectory;
		if( this->TextureOutputDirectory.length() > 0 )
		{
			const bool bFolderCreated = CreateFolder( this->TextureOutputDirectory.c_str() );
			if( !bFolderCreated )
			{
				std::basic_string<TCHAR> tWarningMessage =
				    _T( "Warning! - Failed to set up the texture path override, please verify the input string and that Max has the required admin rights "
				        "for accessing the specified location. Textures will be copied to the default path." );
				this->LogMessageToScriptEditor( tWarningMessage );
			}
			else
			{
				tTextureTargetDirectory = this->TextureOutputDirectory;
			}
		}

		// get texture directory
		std::basic_string<TCHAR> tBakedTextureDirectory = this->workDirectoryHandler->GetBakedTexturesPath();

		// get texture table for this specific scene
		spTextureTable sgTextureTable = sgProcessedScene->GetTextureTable();

		// fetch channel parameters
		spShadingNode sgExitNode = sgMaterial->GetShadingNetwork( cChannelName );
		if( !sgExitNode.IsNull() )
		{
			std::map<std::basic_string<TCHAR>, spShadingTextureNode> sgShadingNodeNameToTextureNode;
			FindAllUpStreamTextureNodes( sgExitNode, sgShadingNodeNameToTextureNode );

			if( sgShadingNodeNameToTextureNode.size() > 0 )
			{
				// use texture
				spShadingTextureNode sgTextureNode = sgShadingNodeNameToTextureNode.begin()->second;

				// empty name check
				spString rTextureNameToFind = sgTextureNode->GetTextureName();
				if( rTextureNameToFind.IsNullOrEmpty() )
				{
					std::basic_string<TCHAR> tErrorMessage =
					    _T("Error (Simplygon): Found a ShadingTextureNode with invalid (NULL or empty) TextureName, unable to map texture on ");
					tErrorMessage += ConstCharPtrToLPCTSTR( rTextureNameToFind.c_str() );
					tErrorMessage += _T(") with invalid (NULL or empty) UV-set, unable to map texture on ");
					tErrorMessage += tChannelName;
					tErrorMessage += _T(" channel.\n");

					this->LogMessageToScriptEditor( tErrorMessage );
					return false;
				}

				// empty tex coord level check
				spString rTextureUvSet = sgTextureNode->GetTexCoordName();
				if( rTextureUvSet.IsNullOrEmpty() )
				{
					std::basic_string<TCHAR> tErrorMessage = _T("Error (Simplygon): Found a ShadingTextureNode (");
					tErrorMessage += ConstCharPtrToLPCTSTR( rTextureNameToFind.c_str() );
					tErrorMessage += _T(") with invalid (NULL or empty) UV-set, unable to map texture on ");
					tErrorMessage += tChannelName;
					tErrorMessage += _T(" channel.\n");

					this->LogMessageToScriptEditor( tErrorMessage );
					return false;
				}

				// use texture
				std::basic_string<TCHAR> tTextureNameToFind = ConstCharPtrToLPCTSTR( rTextureNameToFind );
				std::basic_string<TCHAR> tTextureUvSet = ConstCharPtrToLPCTSTR( rTextureUvSet );

				spTexture sgTexture = sgTextureTable->FindTexture( LPCTSTRToConstCharPtr( tTextureNameToFind.c_str() ) );

				if( sgTexture.IsNull() )
				{
					std::basic_string<TCHAR> tErrorMessage = _T("Error (Simplygon): Could not resolve texture ");
					tErrorMessage += tTextureNameToFind;
					tErrorMessage += _T(" on ");
					tErrorMessage += tChannelName;
					tErrorMessage += _T( " channel.\n" );

					this->LogMessageToScriptEditor( tErrorMessage );
					return false;
				}

				std::basic_string<TCHAR> tTextureName = ConstCharPtrToLPCTSTR( sgTexture->GetName() );
				std::basic_string<TCHAR> tTexturePath = sgTexture->GetImageData().IsNull() ? ConstCharPtrToLPCTSTR( sgTexture->GetFilePath() ) : _T("");
				std::basic_string<TCHAR> tSourceFilePath = Combine( tBakedTextureDirectory, tTexturePath );

				if( !sgTexture->GetImageData().IsNull() )
				{
					// Embedded data, should be written to file
					tSourceFilePath = Combine( tSourceFilePath, tTextureName );
					if( ExportTextureToFile( sg, sgTexture, LPCTSTRToConstCharPtr( tSourceFilePath.c_str() ) ) )
					{
						sgTexture->SetImageData( Simplygon::NullPtr );
						tSourceFilePath = ConstCharPtrToLPCWSTRr( sgTexture.GetFilePath().c_str() );
					}
				}

				std::basic_string<TCHAR> sFinalImportTexturePath = tSourceFilePath;

				if( this->copyTextures )
				{
					// the name of the imported texture is based on the name of the node
					std::basic_string<TCHAR> tImportTextureName = tTextureName + GetExtensionOfFile( tTexturePath );
					ReplaceInvalidCharacters( tImportTextureName, '_' );

					std::basic_string<TCHAR> tImportTexturePath = std::basic_string<TCHAR>( tTextureTargetDirectory.c_str() );
					tImportTexturePath = Combine( tImportTexturePath, tImportTextureName );

					sFinalImportTexturePath = tImportTexturePath;

					// make sure to get a unique name
					if( this->UseNonConflictingTextureNames )
					{
						sFinalImportTexturePath = GetNonConflictingNameInPath( sFinalImportTexturePath.c_str() );
					}

					uint numCopyRetries = 0;

					// copy the file
					while( CopyFile( tSourceFilePath.c_str(), sFinalImportTexturePath.c_str(), FALSE ) == FALSE )
					{
						const DWORD dwErrorCode = GetLastError();

						// if in use (mostly caused by multiple instances racing)
						// then try again
						if( dwErrorCode == ERROR_SHARING_VIOLATION && numCopyRetries < MaxNumCopyRetries )
						{
							Sleep( 100 );
							numCopyRetries++;
							continue;
						}

						TCHAR tErrorCode[ 64 ] = { 0 };
						_stprintf_s( tErrorCode, 64, _T("%u"), dwErrorCode );

						std::basic_string<TCHAR> tErrorMessage = _T("Error - could not copy texture:\n ");
						tErrorMessage += tTexturePath;
						tErrorMessage += _T("\n ");
						tErrorMessage += sFinalImportTexturePath;
						tErrorMessage += _T("\n Code: ");
						tErrorMessage += tErrorCode;
						tErrorMessage += _T("\n");

						this->LogMessageToScriptEditor( tErrorMessage );

						return false;
					}
				}

				PBBitmap* newPBBitmap = SetupMaxTexture( sFinalImportTexturePath.c_str() );

				if( sgTextureNode->GetColorSpaceOverride() == Simplygon::EImageColorSpace::sRGB )
				{
					SetBitmapGamma( newPBBitmap, 2.2f );
				}
				else
				{
					SetBitmapGamma( newPBBitmap, 1.0f );
				}

				int maxChannel = 1;
				if( true )
				{
					spString rTexCoordName = sgTextureNode->GetTexCoordName();
					const char* cTexCoordName = rTexCoordName.c_str();

					std::map<std::string, int>::const_iterator& texCoordMap = this->ImportedUvNameToMaxIndex.find( cTexCoordName );
					if( texCoordMap != this->ImportedUvNameToMaxIndex.end() )
					{
						maxChannel = texCoordMap->second;
					}
				}

				this->materialInfoHandler->Add( tMeshNameOverride, tMaterialNameOverride, tChannelName, sFinalImportTexturePath, maxChannel );

				return newPBBitmap;
			}
		}
	}

	return nullptr;
}
// Progress window callback
INT_PTR CALLBACK SimplygonMax::AppDialogProc( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	SimplygonMax* simplygonMaxInstance = (SimplygonMax*)GetWindowLongPtr( hwndDlg, GWLP_USERDATA );

	switch( uMsg )
	{
		case WM_INITDIALOG:
		{
			simplygonMaxInstance = (SimplygonMax*)lParam;
			SetWindowLongPtr( hwndDlg, GWLP_USERDATA, lParam );

			SendDlgItemMessage( hwndDlg, IDC_PROGRESS_VALUE, PBM_SETRANGE32, 0, 100 );
			SendDlgItemMessage( hwndDlg, IDC_PROGRESS_VALUE, PBM_SETPOS, 0, 0 );

			ShowWindow( hwndDlg, SW_HIDE );
			SetTimer( hwndDlg, 0x1234, 100, nullptr );
			return (INT_PTR)TRUE;
		}

		case WM_COMMAND:
		{
			if( LOWORD( wParam ) == IDOK || LOWORD( wParam ) == IDCANCEL )
			{
				// ignore manual close messages
				return (INT_PTR)TRUE;
			}
			break;
		}

		case WM_TIMER:
		{
			if( wParam == 0x1234 )
			{
				if( simplygonMaxInstance->SpawnError )
				{
					simplygonMaxInstance->SpawnThreadExitValue = 0;
					::EndDialog( hwndDlg, -1 );
					return (INT_PTR)TRUE;
				}

				if( simplygonMaxInstance->SpawnThreadHandle == nullptr )
				{
					simplygonMaxInstance->SpawnThreadHandle =
					    ::CreateThread( nullptr, 0, &StaticProcessingThread, (void*)simplygonMaxInstance, 0, &simplygonMaxInstance->SpawnThreadID );
					if( simplygonMaxInstance->SpawnThreadHandle == nullptr )
					{
						// failed to spawn thread
						::KillTimer( hwndDlg, 0x1234 );
						::MessageBox( hwndDlg, _T("Failed to create Simplygon processing thread"), _T("Error"), MB_OK );
						::EndDialog( hwndDlg, -1 );
						return (INT_PTR)TRUE;
					}
				}
				else
				{
					DWORD dwStatus = 0;
					::GetExitCodeThread( simplygonMaxInstance->SpawnThreadHandle, &dwStatus );
					if( dwStatus != STILL_ACTIVE )
					{
						simplygonMaxInstance->SpawnThreadExitValue = dwStatus;
						EndDialog( hwndDlg, 0 );
						return (INT_PTR)TRUE;
					}

					// copy the log information
					simplygonMaxInstance->threadLock.Enter();

					const int log_prgs = simplygonMaxInstance->logProgress;
					TCHAR* log_msg = nullptr;
					if( _tcslen( simplygonMaxInstance->tLogMessage ) > 0 )
					{
						log_msg = _tcsdup( simplygonMaxInstance->tLogMessage );
						simplygonMaxInstance->tLogMessage[ 0 ] = _T( '\0' );
					}

					// set the gui values
					if( log_msg != nullptr )
					{
						SendDlgItemMessage( hwndDlg, IDC_EDIT_INFOBOX, EM_SETSEL, 0, -1 );
						SendDlgItemMessage( hwndDlg, IDC_EDIT_INFOBOX, EM_SETSEL, -1, -1 );
						SendDlgItemMessage( hwndDlg, IDC_EDIT_INFOBOX, EM_REPLACESEL, 0, (LPARAM)log_msg );
						free( log_msg );
					}

					if( log_prgs != SendDlgItemMessage( hwndDlg, IDC_PROGRESS_VALUE, PBM_GETPOS, 0, 0 ) )
					{
						SendDlgItemMessage( hwndDlg, IDC_PROGRESS_VALUE, PBM_SETPOS, log_prgs, 0 );
					}

					simplygonMaxInstance->threadLock.Leave();

					return (INT_PTR)TRUE;
				}
			}
		}
	}

	return (INT_PTR)FALSE;
}

// add log-string
void SimplygonMax::AddLogString( const TCHAR* tMessage )
{
	this->threadLock.Enter();
	_tcscpy( &tLogMessage[ _tcslen( tLogMessage ) ], tMessage );
	this->threadLock.Leave();
}

// add log-string to window
void SimplygonMax::LogToWindow( std::basic_string<TCHAR> tMessage, ErrorType errorType, bool bSleepBeforeResuming )
{
	tMessage += _T("\r\n");

	// if progress window is initialized, output message
	if( this->tLogMessage != nullptr )
	{
		this->AddLogString( tMessage.c_str() );
	}

	// if warning or error, output to console as well as log
	if( errorType != Info )
	{
		this->LogMessageToScriptEditor( ( errorType == Error ? _T("Error (Simplygon): ") : _T("Warning (Simplygon): ") ) + tMessage );
		if( this->MaxInterface )
		{
			this->MaxInterface->Log()->LogEntry( errorType == Error ? SYSLOG_ERROR : SYSLOG_WARN, NO_DIALOG, _M( "Simplygon Max Plugin" ), tMessage.c_str() );
		}

		//		Send log message to Simplygon UI.
		//		Disabled due to Safe Scene Script Execution in 3ds Max
		// 		Todo: Detect Script execution mode and enable when allowed.
		//		std::wstring logMethod = errorType == ErrorType::Error ? L"SendErrorToLog" : L"SendWarningToLog";
		//		TCHAR tExecuteSendLogToUIScript[ MAX_PATH ] = { 0 };
		//		_stprintf_s( tExecuteSendLogToUIScript, MAX_PATH, _T("ui = dotNetObject \"SimplygonUI.UIAccessor\"\nui.%s \"%s\""), logMethod.c_str(),
		//tMessage.c_str() ); 		const TSTR wExecuteSendLogToUIScript( tExecuteSendLogToUIScript );
		//
		// #if MAX_VERSION_MAJOR < 24
		//		ExecuteMAXScriptScript( wExecuteSendLogToUIScript.data(), TRUE );
		// #else
		//		ExecuteMAXScriptScript( wExecuteSendLogToUIScript.data(), MAXScript::ScriptSource::NotSpecified, TRUE );
		// #endif
	}

	// if info, only output to log
	else
	{
		if( this->MaxInterface )
		{
			this->MaxInterface->Log()->LogEntry( SYSLOG_INFO, NO_DIALOG, _M( "Simplygon Max Plugin" ), tMessage.c_str() );
		}
	}

	if( bSleepBeforeResuming )
	{
		Sleep( 5000 );
	}
}

bool SimplygonMax::UseSettingsPipelineForProcessing( const INT64 pipelineId )
{
	// make sure Simplygon is initialized,
	// this will only be done once
	if( sg == nullptr )
	{
		if( !SimplygonMaxInstance->Initialize() )
		{
			throw std::exception( "Failed to initialize Simplygon SDK." );
		}
	}

	// find pipeline object with the given id
	const std::map<INT64, spPipeline>::const_iterator& pipelineIterator = PipelineHelper::Instance()->nameToSettingsPipeline.find( pipelineId );
	if( pipelineIterator == PipelineHelper::Instance()->nameToSettingsPipeline.end() )
	{
		throw std::exception( "The pipeline id was not found." );
	}

	// save a reference to the specified pipeline
	this->sgPipeline = pipelineIterator->second;

	return true;
}

// general message callback
void SimplygonMax::Callback( std::basic_string<TCHAR> tId, bool bIsError, std::basic_string<TCHAR> tMessage, int progress )
{
	TCHAR tBuffer[ 16 ] = { 0 };
	_stprintf_s( tBuffer, 16, _T("%d"), progress );

	std::basic_string<TCHAR> tComposedMessage;

	if( tId.length() > 0 )
	{
		tComposedMessage = tMessage + _T("\t(") + tBuffer + _T("%)\t (") + tId + _T(")");
	}
	else if( progress > 0 )
	{
		tComposedMessage = tMessage + _T("\t(") + tBuffer + _T("%)");
	}
	else
	{
		tComposedMessage = tMessage;
	}

	const DWORD dwWaitResult = WaitForSingleObject( this->UILock, INFINITE );

	try
	{
		this->logProgress = progress;
		this->LogToWindow( tComposedMessage, bIsError ? Error : Info );
	}
	catch( ... )
	{
	}

	ReleaseMutex( this->UILock );
}

// processing thread which executes Simplygon
bool SimplygonMax::ProcessScene()
{
	bool bProcessingSucceeded = true;
	std::vector<std::string> errorMessages;
	std::vector<std::string> warningMessages;
	try
	{
		// fetch output texture path
		std::basic_string<TCHAR> tBakedTexturesPath = this->GetWorkDirectoryHandler()->GetBakedTexturesPath();
		std::basic_string<TCHAR> tWorkDirectory = this->GetWorkDirectoryHandler()->GetWorkDirectory();
		std::basic_string<TCHAR> tPipelineFilePath = Combine( this->GetWorkDirectoryHandler()->GetWorkDirectory(), _T("sgPipeline.json") );

		std::basic_string<TCHAR> tFinalExternalBatchPath = _T("");

		// if there is a environment path, use it
		std::basic_string<TCHAR> tEnvironmentPath = GetSimplygonEnvironmentVariable( _T( SIMPLYGON_10_PATH ) );
		if( tEnvironmentPath.size() > 0 )
		{
			tFinalExternalBatchPath = tEnvironmentPath;
		}
		else
		{
			std::string sErrorMessage = "Invalid environment path: ";
			sErrorMessage += SIMPLYGON_10_PATH;
			throw std::exception( sErrorMessage.c_str() );
		}

		// setup Simplygon processing module
		SimplygonProcessingModule* processingModule = new SimplygonProcessingModule();
		processingModule->SetTextureOutputDirectory( tBakedTexturesPath );
		processingModule->SetWorkDirectory( tWorkDirectory );
		processingModule->SetProgressObserver( SimplygonInitInstance );
		processingModule->SetErrorHandler( SimplygonInitInstance );
		processingModule->SetExternalBatchPath( tFinalExternalBatchPath );

		// check if the pipeline is valid before saving
		if( this->sgPipeline.IsNull() )
		{
			std::string sErrorMessage = "Invalid pipeline.";
			throw std::exception( sErrorMessage.c_str() );
		}

		// ugly solution, make sure to reset strings!
		const bool bSceneFromFile = this->inputSceneFile.length() > 0 && this->outputSceneFile.length() > 0;
		if( bSceneFromFile )
		{
			// original Simplygon scene from file
			std::basic_string<TCHAR> tInputSceneFile = CorrectPath( this->inputSceneFile );
			std::basic_string<TCHAR> tOutputSceneFile = CorrectPath( this->outputSceneFile );

			// start process with the given pipeline settings file
			std::vector<std::basic_string<TCHAR>> tOutputFileList = processingModule->RunPipelineOnFile(
			    tInputSceneFile, tOutputSceneFile, this->sgPipeline, EPipelineRunMode( this->PipelineRunMode ), errorMessages, warningMessages );

			this->GetMaterialInfoHandler()->AddProcessedSceneFiles( tOutputFileList );
		}
		else
		{
			// fetch original Simplygon scene
			const spScene sgOriginalScene = this->GetSceneHandler()->sgScene;

			// start process with the given pipeline settings file
			std::vector<spScene> sgProcessedScenes =
			    processingModule->RunPipeline( sgOriginalScene, this->sgPipeline, EPipelineRunMode( this->PipelineRunMode ), errorMessages, warningMessages );

			// fetch processed Simplygon scene
			this->SceneHandler->sgProcessedScenes = sgProcessedScenes;
		}
	}
	catch( std::exception ex )
	{
		bProcessingSucceeded = false;
	}

	// Write errors and warnings to log.
	if( errorMessages.size() > 0 )
	{
		for( const auto& error : errorMessages )
		{
			this->LogToWindow( ConstCharPtrToLPCTSTR( error.c_str() ), Error, true );
		}
	}
	if( warningMessages.size() > 0 )
	{
		for( const auto& warning : warningMessages )
		{
			this->LogToWindow( ConstCharPtrToLPCTSTR( warning.c_str() ), Warning, false );
		}
	}

	// if processing failed, cleanup and notify user.
	if( !bProcessingSucceeded )
	{
		this->CleanUp();

		return false;
	}

	return true;
}

// processing thread which lunches Simplygon
DWORD WINAPI SimplygonMax::StaticProcessingThread( LPVOID lpParameter )
{
	SimplygonMax* simplygonMaxInstance = (SimplygonMax*)lpParameter;

	// run processing
	const bool bResult = simplygonMaxInstance->ProcessScene();

	// call thread, return success value
	return bResult ? 1 : 0;
}

// starts the processing thread
bool SimplygonMax::RunSimplygonProcess()
{
	this->SpawnThreadHandle = nullptr;
	this->SpawnError = 0;
	this->SpawnThreadExitValue = 0;
	this->SpawnThreadID = 0;
	this->tLogMessage = new TCHAR[ 8192 ];
	this->tLogMessage[ 0 ] = _T( '\0' );
	this->logProgress = 0;

	bool bResult = false;

	if( this->ShowProgress )
	{
		// bring up a progress dialog, run the processing in a worker thread
		DialogBoxParam( hInstance, MAKEINTRESOURCE( IDD_DIALOG_APPDLG ), MaxInterface->GetMAXHWnd(), &AppDialogProc, (LPARAM)this );
		bResult = ( SpawnThreadExitValue != 0 );
	}
	else
	{
		// run the processing in this thread instead
		bResult = this->ProcessScene();
	}

	delete[] this->tLogMessage;
	this->tLogMessage = nullptr;

	return bResult;
}

// cleanup functions
void SimplygonMax::CleanUpGlobalMaterialMappingData()
{
	// remove materials
	for( size_t metaDataIndex = 0; metaDataIndex < this->GlobalExportedMaterialMap.size(); ++metaDataIndex )
	{
		if( this->GlobalExportedMaterialMap[ metaDataIndex ] != nullptr )
		{
			delete this->GlobalExportedMaterialMap[ metaDataIndex ];
			this->GlobalExportedMaterialMap[ metaDataIndex ] = nullptr;
		}
	}

	this->GlobalExportedMaterialMap.clear();
}

void SimplygonMax::RequiredCleanUp()
{
	this->SelectedMeshCount = 0;
	this->SelectedMeshNodes.clear();
	this->ImportedUvNameToMaxIndex.clear();
	this->ImportedMaxIndexToUv.clear();

	this->MaxBoneToSgBone.clear();
	this->SgBoneToMaxBone.clear();
	this->SgBoneIdToIndex.clear();

	this->MaxSgNodeMap.clear();
	this->SgMaxNodeMap.clear();
	this->ImportedTextures.clear();
	this->LoadedTexturePathToID.clear();
}

void SimplygonMax::CleanUp()
{
	// Note: If users are using exporting scene using distributed processing and importing the scnee.
	// We want to kepe the mapping around to hook back to source material ids.
	if( this->extractionType != ExtractionType::EXPORT_TO_FILE )
	{
		this->CleanUpGlobalMaterialMappingData();
	}

	this->RequiredCleanUp();

	this->ShadingTextureNodeToPath.clear();

	// remove handlers
	if( this->workDirectoryHandler )
	{
		delete this->workDirectoryHandler;
		this->workDirectoryHandler = nullptr;
	}
	if( this->SceneHandler )
	{
		delete this->SceneHandler;
		this->SceneHandler = nullptr;
	}

	// create proxy materials
	this->materialProxyTable.clear();
	this->materialProxyWritebackTable.clear();
	this->CachedMaterialInfos.clear();

	// if the we are in the hold, accept and close
	if( theHold.Holding() )
	{
		theHold.Accept( _T("Simplygon") );
	}
}

// Gets the material map for the given Max material
MaxMaterialMap* SimplygonMax::GetGlobalMaterialMap( Mtl* mMaxMaterial )
{
	std::basic_string<TCHAR> tMaterialName = mMaxMaterial->GetName();
	AnimHandle mMaxMaterialHandle = Animatable::GetHandleByAnim( mMaxMaterial );

	for( size_t materialIndex = 0; materialIndex < this->GlobalExportedMaterialMap.size(); ++materialIndex )
	{
		MaxMaterialMap* mMap = this->GlobalExportedMaterialMap[ materialIndex ];

		if( mMap->sgMaterialName == tMaterialName && mMap->mMaxMaterialHandle == mMaxMaterialHandle )
		{
			return mMap;
		}
	}

	return nullptr;
}

// Gets the material map for the given Max material
MaxMaterialMap* SimplygonMax::GetGlobalMaterialMapUnsafe( Mtl* mMaxMaterial )
{
	std::basic_string<TCHAR> tMaterialName = mMaxMaterial->GetName();
	for( size_t materialIndex = 0; materialIndex < this->GlobalExportedMaterialMap.size(); ++materialIndex )
	{
		MaxMaterialMap* mMap = this->GlobalExportedMaterialMap[ materialIndex ];

		if( mMap->sgMaterialName == tMaterialName && mMap->NumSubMaterials == mMaxMaterial->NumSubMtls() )
		{
			return mMap;
		}
	}

	return nullptr;
}

// Gets the material map for the given Simplygon material
MaxMaterialMap* SimplygonMax::GetGlobalMaterialMap( std::string sgMaterialId )
{
	for( size_t materialIndex = 0; materialIndex < this->GlobalExportedMaterialMap.size(); ++materialIndex )
	{
		if( this->GlobalExportedMaterialMap[ materialIndex ]->sgMaterialId == sgMaterialId )
		{
			return this->GlobalExportedMaterialMap[ materialIndex ];
		}
	}

	return nullptr;
}

// creates a Simplygon material based on the Max material
MaxMaterialMap* SimplygonMax::AddMaterial( Mtl* mMaxMaterial, spGeometryData sgMeshData )
{
	// if no material is applied, use default material
	if( mMaxMaterial == nullptr )
	{
		return nullptr;
	}

	std::basic_string<TCHAR> mMaterialName = mMaxMaterial->GetName();

	// look for the material
	MaxMaterialMap* materialMap = this->GetGlobalMaterialMap( mMaxMaterial );
	if( materialMap != nullptr )
	{
		return materialMap;
	}

	materialMap = new MaxMaterialMap();
	materialMap->SetupFromMaterial( mMaxMaterial );

	this->GlobalExportedMaterialMap.push_back( materialMap );

	// add the material(s) into the material table
	if( materialMap->NumSubMaterials == 0 )
	{
		std::pair<std::string, int> mMap = this->AddMaxMaterialToSgScene( mMaxMaterial );
		this->GlobalMaxToSgMaterialMap.insert( std::pair<Mtl*, int>( mMaxMaterial, mMap.second ) );
		this->GlobalSgToMaxMaterialMap.insert( std::pair<std::string, Mtl*>( mMap.first, mMaxMaterial ) );

		materialMap->sgMaterialId = mMap.first;

		materialMap->MaxToSGMapping.insert( std::pair<int, int>( 0, mMap.second ) );
		materialMap->SGToMaxMapping.insert( std::pair<int, int>( mMap.second, 0 ) );

		materialMap->NumActiveMaterials = 1;
	}
	else
	{
		// add sub-materials
		for( uint materialIndex = 0; materialIndex < materialMap->NumSubMaterials; ++materialIndex )
		{
			Mtl* mMaxSubMaterial = mMaxMaterial->GetSubMtl( materialIndex );

			// if empty, ignore
			if( mMaxSubMaterial == nullptr )
				continue;

			MaxMaterialMap* subMaterialMap = GetGlobalMaterialMap( mMaxSubMaterial );

			// if no mapping, create mapping
			if( subMaterialMap == nullptr )
			{
				std::basic_string<TCHAR> mSubMaterialName = mMaxSubMaterial->GetName();

				subMaterialMap = new MaxMaterialMap();
				subMaterialMap->SetupFromMaterial( mMaxSubMaterial );

				std::pair<std::string, int> mMap = this->AddMaxMaterialToSgScene( mMaxSubMaterial );
				this->GlobalMaxToSgMaterialMap.insert( std::pair<Mtl*, int>( mMaxSubMaterial, mMap.second ) );
				this->GlobalSgToMaxMaterialMap.insert( std::pair<std::string, Mtl*>( mMap.first, mMaxSubMaterial ) );

				subMaterialMap->sgMaterialId = mMap.first;

				materialMap->MaxToSGMapping.insert( std::pair<int, int>( materialIndex, mMap.second ) );
				subMaterialMap->MaxToSGMapping.insert( std::pair<int, int>( materialIndex, mMap.second ) );

				materialMap->SGToMaxMapping.insert( std::pair<int, int>( mMap.second, materialIndex ) );
				subMaterialMap->SGToMaxMapping.insert( std::pair<int, int>( mMap.second, materialIndex ) );

				subMaterialMap->NumActiveMaterials++;

				this->GlobalExportedMaterialMap.push_back( subMaterialMap );
			}

			// if mapping, reuse mapping
			else
			{
				std::basic_string<TCHAR> mSubMaterialName = mMaxSubMaterial->GetName();

				int sgMaterialIndex = 0;
				const std::map<Mtl*, int>::const_iterator& mMtlIterator = this->GlobalMaxToSgMaterialMap.find( mMaxSubMaterial );
				if( mMtlIterator != this->GlobalMaxToSgMaterialMap.end() )
				{
					sgMaterialIndex = mMtlIterator->second;
				}

				materialMap->MaxToSGMapping.insert( std::pair<int, int>( materialIndex, sgMaterialIndex ) );
				materialMap->SGToMaxMapping.insert( std::pair<int, int>( sgMaterialIndex, materialIndex ) );
			}

			materialMap->NumActiveMaterials++;
		}
	}

	return materialMap;
}

// returns resolved texture file path
static TSTR FindBitmapFile( const TCHAR* tFilePath )
{
	// initialize variables
	bool bFilePathFound = false;

	MaxSDK::Util::Path mMaxFilepath( tFilePath );

	// get a pointer to the file resolution manager
	IFileResolutionManager* mFileManager = IFileResolutionManager::GetInstance();

	DbgAssert( mFileManager ); // make sure you get the pointer

	// attempt to find the file
	bFilePathFound = mFileManager->GetFullFilePath( mMaxFilepath, MaxSDK::AssetManagement::kBitmapAsset );

	if( bFilePathFound )
		return mMaxFilepath.GetString();
	else
		return TSTR();
}

// returns full texture path
void GetImageFullFilePath( const TCHAR* tFilePath, TCHAR* tDestinationPath )
{
	// early out if dest is null
	if( tDestinationPath == nullptr )
		return;

	// make sure dest is reset
	tDestinationPath[ 0 ] = _T( '\0' );

	// early out if src is null
	if( tFilePath == nullptr )
		return;

	TCHAR tFullPath[ MAX_PATH ] = { 0 };
	TCHAR* tFileName = nullptr; // points to the filename part of FillPath

	// try the IFileResolutionManager first
	TSTR tFullFilePath = FindBitmapFile( tFilePath );
	if( GetFileAttributes( tFullFilePath ) != INVALID_FILE_ATTRIBUTES )
	{
		// found it
		_tcscpy( tDestinationPath, tFullFilePath );
		return;
	}

	// get full path name
	if( GetFullPathName( tFilePath, MAX_PATH, tFullPath, &tFileName ) == 0 )
		return; // conversion failed, return empty string

	// test the full path name first
	if( GetFileAttributes( tFullPath ) != INVALID_FILE_ATTRIBUTES )
	{
		_tcscpy( tDestinationPath, tFullPath );
		return;
	}

	// the file was not found, try the other registered paths instead
	const int numDirectories = TheManager->GetMapDirCount();
	for( int dirIndex = 0; dirIndex < numDirectories; ++dirIndex )
	{
		TCHAR tTestPath[ MAX_PATH ] = { 0 };
		_stprintf_s( tTestPath, MAX_PATH, _T("%s\\%s"), TheManager->GetMapDir( dirIndex ), tFileName );
		if( GetFileAttributes( tTestPath ) != INVALID_FILE_ATTRIBUTES )
		{
			_tcscpy( tDestinationPath, tTestPath );
			return;
		}
	}
}

// looks for if the texture has alpha (by looking at the header)
bool TextureHasAlpha( const char* tTextureFilePath )
{
	// create an image data importer instance
	spImageDataImporter sgImageImporter = sg->CreateImageDataImporter();

	// set import path
	sgImageImporter->SetImportFilePath( tTextureFilePath );

	// set to load header only
	sgImageImporter->SetImportOnlyHeader( true );

	// try to import image
	const bool bImageLoaded = sgImageImporter->RunImport();
	if( bImageLoaded )
	{
		const uint numChannels = sgImageImporter->GetNumberOfChannels();
		if( numChannels == 4 )
		{
			return true;
		}
	}

	return false;
}

// returns a shading color node based on the input color values
spShadingColorNode CreateColorShadingNetwork( float r, float g, float b, float a )
{
	spShadingColorNode sgColorNode = sg->CreateShadingColorNode();
	sgColorNode->SetColor( r, g, b, a );
	return sgColorNode;
}

// returns a material-color override for the given channel, if any
MaterialColorOverride* HasMaterialColorOverrideForChannel( std::vector<MaterialColorOverride>* materialColorOverrides,
                                                           std::basic_string<TCHAR> tMaxStdMaterialName,
                                                           std::basic_string<TCHAR> tChannelName )
{
	for( size_t c = 0; c < materialColorOverrides->size(); ++c )
	{
		std::basic_string<TCHAR> maxChannelName = materialColorOverrides->at( c ).MappingChannelName;

		if( tMaxStdMaterialName == materialColorOverrides->at( c ).MaterialName && maxChannelName == tChannelName )
		{
			return &( materialColorOverrides->at( c ) );
		}
	}

	return nullptr;
}

// returns a shading-color node override for the given channel, if any
spShadingColorNode AssignMaxColorToSgMaterialChannel( spMaterial sgMaterial,
                                                      const char* cChannelName,
                                                      StdMat2* mMaxStdMaterial,
                                                      Interface* mMaxInterface,
                                                      long maxChannelId,
                                                      std::vector<MaterialColorOverride> materialColorOverrides )
{
	const long maxChannel = mMaxStdMaterial->StdIDToChannel( maxChannelId );
#if MAX_VERSION_MAJOR >= 26
	std::basic_string<TCHAR> tMappedChannelName = mMaxStdMaterial->GetSubTexmapSlotName( maxChannel, true );
#else
	std::basic_string<TCHAR> tMappedChannelName = mMaxStdMaterial->GetSubTexmapSlotName( maxChannel );
#endif

	ReplaceInvalidCharacters( tMappedChannelName, _T( '_' ) );

	std::basic_string<TCHAR> tMaxMaterialName = mMaxStdMaterial->GetName();

	// assign color
	switch( maxChannelId )
	{
		case ID_AM:
		{
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, tMaxMaterialName, tMappedChannelName );

			if( colorOverride != nullptr )
			{
				// shading network
				return CreateColorShadingNetwork( colorOverride->GetR(), colorOverride->GetG(), colorOverride->GetB() );
			}

			// standard color
			const Color mChannelColor = mMaxStdMaterial->GetAmbient( mMaxInterface->GetTime() );

			// shading network
			return CreateColorShadingNetwork( mChannelColor.r, mChannelColor.g, mChannelColor.b );
		}

		case ID_DI:
		{
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, tMaxMaterialName, tMappedChannelName );

			if( colorOverride != nullptr )
			{
				// shading network
				return CreateColorShadingNetwork( colorOverride->GetR(), colorOverride->GetG(), colorOverride->GetB() );
			}

			// standard color
			const Color mChannelColor = mMaxStdMaterial->GetDiffuse( mMaxInterface->GetTime() );

			// shading network
			return CreateColorShadingNetwork( mChannelColor.r, mChannelColor.g, mChannelColor.b );
		}

		case ID_SP:
		{
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, tMaxMaterialName, tMappedChannelName );

			if( colorOverride != nullptr )
			{
				// shading network
				return CreateColorShadingNetwork( colorOverride->GetR(), colorOverride->GetG(), colorOverride->GetB() );
			}

			// standard color
			const Color mChannelColor = mMaxStdMaterial->GetSpecular( mMaxInterface->GetTime() );
			const float shininess = mMaxStdMaterial->GetShininess( mMaxInterface->GetTime() ) * 128.f;

			// shading network
			return CreateColorShadingNetwork( mChannelColor.r, mChannelColor.g, mChannelColor.b, shininess );
		}

		case ID_SH:
		{
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, tMaxMaterialName, tMappedChannelName );

			if( colorOverride != nullptr )
			{
				// shading network
				return CreateColorShadingNetwork( colorOverride->GetR(), colorOverride->GetG(), colorOverride->GetB() );
			}

			// shading network
			return CreateColorShadingNetwork();
		}

		case ID_SS:
		{
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, tMaxMaterialName, tMappedChannelName );

			if( colorOverride != nullptr )
			{
				// shading network
				return CreateColorShadingNetwork( colorOverride->GetR(), colorOverride->GetG(), colorOverride->GetB() );
			}

			// shading network
			return CreateColorShadingNetwork();
		}

		case ID_SI:
		{
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, tMaxMaterialName, tMappedChannelName );

			if( colorOverride != nullptr )
			{
				// shading network
				return CreateColorShadingNetwork( colorOverride->GetR(), colorOverride->GetG(), colorOverride->GetB() );
			}

			// shading network
			return Simplygon::NullPtr;
		}

		case ID_OP:
		{
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, tMaxMaterialName, tMappedChannelName );

			if( colorOverride != nullptr )
			{
				// shading network
				return CreateColorShadingNetwork( colorOverride->GetR(), colorOverride->GetG(), colorOverride->GetB() );
			}

			// shading network
			const float opacity = mMaxStdMaterial->GetOpacity( mMaxInterface->GetTime() );
			return CreateColorShadingNetwork( opacity, opacity, opacity, opacity );
		}

		case ID_FI:
		{
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, tMaxMaterialName, tMappedChannelName );

			if( colorOverride != nullptr )
			{
				// shading network
				return CreateColorShadingNetwork( colorOverride->GetR(), colorOverride->GetG(), colorOverride->GetB() );
			}

			// shading network
			return Simplygon::NullPtr;
		}

		case ID_BU:
		{
			// shading network
			return Simplygon::NullPtr;
		}

		case ID_RL:
		{
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, tMaxMaterialName, tMappedChannelName );

			if( colorOverride != nullptr )
			{
				// shading network
				return CreateColorShadingNetwork( colorOverride->GetR(), colorOverride->GetG(), colorOverride->GetB() );
			}

			// shading network
			return Simplygon::NullPtr;
		}

		case ID_RR:
		{
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, tMaxMaterialName, tMappedChannelName );

			if( colorOverride != nullptr )
			{
				// shading network
				return CreateColorShadingNetwork( colorOverride->GetR(), colorOverride->GetG(), colorOverride->GetB() );
			}

			// shading network
			return Simplygon::NullPtr;
		}

		case ID_DP:
		{
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, tMaxMaterialName, tMappedChannelName );

			if( colorOverride != nullptr )
			{
				// shading network
				return CreateColorShadingNetwork( colorOverride->GetR(), colorOverride->GetG(), colorOverride->GetB() );
			}

			// shading network
			return Simplygon::NullPtr;
		}

		default:
		{
			std::basic_string<TCHAR> tChannelName = ConstCharPtrToLPCTSTR( cChannelName );
			ReplaceInvalidCharacters( tChannelName, _T( '_' ) );

			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, tMaxMaterialName, tChannelName );

			if( colorOverride != nullptr )
			{
				// shading network
				return CreateColorShadingNetwork( colorOverride->GetR(), colorOverride->GetG(), colorOverride->GetB() );
			}
			// unsupported color, just set color components to one for now...
			return Simplygon::NullPtr;
		}
	}
}

// Assigns a color to the specified Max StdMaterial
void AssignSgColorToMaxMaterial( float r, float g, float b, float a, StdMat2* mMaxStdMaterial, Interface* mMaxInterface, long maxChannelId )
{
	// assign color
	switch( maxChannelId )
	{
		case ID_AM:
		{
			mMaxStdMaterial->SetAmbient( Color( r, g, b ), 0 );
		}
		break;

		case ID_DI:
		{
			mMaxStdMaterial->SetDiffuse( Color( r, g, b ), 0 );
		}
		break;

		case ID_SP:
		{
			mMaxStdMaterial->SetSpecular( Color( r, g, b ), 0 );
			mMaxStdMaterial->SetShininess( a / 128.f, 0 );
		}
		break;

		case ID_SS:
		{
			mMaxStdMaterial->SetSpecular( Color( r, g, b ), 0 );
			mMaxStdMaterial->SetShininess( a / 128.f, 0 );
		}
		break;

		case ID_SH:
		{
		}
		break;

		case ID_OP:
		{
			mMaxStdMaterial->SetOpacity( ( r + g + b ) / 3.0f, 0 );
		}
		break;

		default:
		{
		}
	}
}

// returns whether the Simplygon material channel has a shading network assigned
bool SimplygonMax::MaterialChannelHasShadingNetwork( spMaterial sgMaterial, const char* cChannelName )
{
	if( !sgMaterial->HasMaterialChannel( cChannelName ) )
		return false;

	if( !sgMaterial->GetShadingNetwork( cChannelName ) )
		return false;

	return true;
}

void SimplygonMax::LogMaterialNodeMessage( Texmap* mTexMap,
                                           std::basic_string<TCHAR> tMaterialName,
                                           std::basic_string<TCHAR> tChannelName,
                                           const bool partialFail,
                                           std::basic_string<TCHAR> tExtendedInformation )
{
	if( !mTexMap )
		return;

	Class_ID nodeClassId = mTexMap->ClassID();
	TSTR nodeClassName = _T("Unknown");
	mTexMap->GetClassName( nodeClassName );

	if( nodeClassId == Class_ID( BMTEX_CLASS_ID, 0 ) )
	{
		// LogToWindow( tMaterialName + _T(" (") + tChannelName + _T(") - Bitmap texture node is not supported."), Warning );
	}
	else if( nodeClassId == GNORMAL_CLASS_ID )
	{
		// LogToWindow( tMaterialName + _T(" (") + tChannelName + _T(") - Composite texture node is not supported."), Warning );
	}
	else
	{
		if( partialFail != true )
		{
			LogToWindow( tMaterialName + _T(" (") + tChannelName + _T(") - ") + std::basic_string<TCHAR>( nodeClassName ) +
			                 _T(" texture node is not supported."),
			             Warning );
		}
		else
		{
			LogToWindow( tMaterialName + _T(" (") + tChannelName + _T(") - ") + std::basic_string<TCHAR>( nodeClassName ) + _T(" ") + tExtendedInformation,
			             Warning );
		}
	}
}

std::basic_string<TCHAR> SimplygonMax::SetupMaxMappingChannel( const char* cMaterialName, const char* cChannelName, Texmap* mTexMap )
{
	// set mapping channel to 1
	std::basic_string<TCHAR> tMaxMappingChannel;
	tMaxMappingChannel = _T("1");

	// check for uv overrides
	if( this->MaterialChannelOverrides.size() > 0 )
	{
		for( size_t channelIndex = 0; channelIndex < this->MaterialChannelOverrides.size(); ++channelIndex )
		{
			const MaterialTextureMapChannelOverride& mappingChannelOverride = this->MaterialChannelOverrides[ channelIndex ];
			if( strcmp( cMaterialName, LPCTSTRToConstCharPtr( mappingChannelOverride.MaterialName.c_str() ) ) == 0 )
			{
				if( strcmp( cChannelName, LPCTSTRToConstCharPtr( mappingChannelOverride.MappingChannelName.c_str() ) ) == 0 )
				{
					TCHAR tMappingChannelBuffer[ MAX_PATH ] = { 0 };
					_stprintf_s( tMappingChannelBuffer, _T("%d"), mappingChannelOverride.MappingChannel );

					tMaxMappingChannel = tMappingChannelBuffer;
					break;
				}
			}
		}
	}
	// or explicit channels
	else if( mTexMap->GetUVWSource() == UVWSRC_EXPLICIT )
	{
		const int maxMappingChannel = mTexMap->GetMapChannel();
		TCHAR tMappingChannelBuffer[ MAX_PATH ] = { 0 };
		_stprintf_s( tMappingChannelBuffer, _T("%d"), maxMappingChannel );

		tMaxMappingChannel = tMappingChannelBuffer;
	}
	return tMaxMappingChannel;
}

bool HasActiveTransparency( BitmapTex* mBitmapTex )
{
	if( mBitmapTex != nullptr )
	{
		// PBR and STD materials listens to different flags in Max
#if MAX_VERSION_MAJOR >= 23
		return mBitmapTex->GetAlphaAsRGB( TRUE ) == TRUE;
#else
		return mBitmapTex->GetAlphaAsMono( TRUE ) == TRUE;
#endif
	}
	return false;
}

spShadingNode SimplygonMax::CreateSgMaterialPBRChannel(
    Texmap* mTexMap, long maxChannelId, const char* cMaterialName, const char* cChannelName, MaterialNodes::TextureSettingsOverride* textureSettingsOverride )
{
	std::basic_string<TCHAR> tMaterialName = ConstCharPtrToLPCTSTR( cMaterialName );
	std::basic_string<TCHAR> tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

	MaterialNodes::MaterialChannelData mChannelData = MaterialNodes::MaterialChannelData(
	    tMaterialName, tChannelName, maxChannelId, nullptr, Simplygon::NullPtr, &this->MaterialTextureOverrides, this->CurrentTime, true );

	spShadingNode sgShadingNode = CreateSgMaterial( mTexMap, mChannelData, textureSettingsOverride );

	if( sgShadingNode == NullPtr )
	{
		mChannelData.mWarningMessage += _T(", replacing with black color node.");
		SimplygonMaxInstance->LogToWindow( mChannelData.mWarningMessage, Warning );

		spShadingColorNode sgBlackNode = sg->CreateShadingColorNode();
		sgBlackNode->SetColor( 0.0f, 0.0f, 0.0f, 1.0f );

		sgShadingNode = sgBlackNode;
	}

	return sgShadingNode;
}

void SimplygonMax::CreateAndLinkTexture( MaterialNodes::TextureData& rTextureData )
{
	if( rTextureData.mBitmap )
	{
		spTexture sgTexture;

		// do lookup in case this texture is already in use
		std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
		    this->LoadedTexturePathToID.find( rTextureData.mTexturePathWithName );
		const bool bTextureInUse = ( tTextureIterator != this->LoadedTexturePathToID.end() );

		if( bTextureInUse )
		{
			sgTexture =
			    this->SceneHandler->sgScene->GetTextureTable()->FindTextureUsingFilePath( LPCTSTRToConstCharPtr( rTextureData.mTexturePathWithName.c_str() ) );
		}
		else
		{
			sgTexture = sg->CreateTexture();
			sgTexture->SetName( LPCTSTRToConstCharPtr( rTextureData.mTextureName.c_str() ) );
			sgTexture->SetFilePath( LPCTSTRToConstCharPtr( rTextureData.mTexturePathWithName.c_str() ) );

			this->SceneHandler->sgScene->GetTextureTable()->AddTexture( sgTexture );
		}

		// if the texture was not already in scene, copy it to local work folder
		if( !bTextureInUse )
		{
			const spString rTexturePathWithName = sgTexture->GetFilePath();
			this->LoadedTexturePathToID.insert( std::pair<std::basic_string<TCHAR>, std::basic_string<TCHAR>>(
			    rTextureData.mTexturePathWithName, ConstCharPtrToLPCTSTR( rTexturePathWithName.c_str() ) ) );
		}
	}
}

void SimplygonMax::ApplyChannelSpecificModifiers(
    long maxChannelId, StdMat2* mMaxStdMaterial, std::basic_string<TCHAR> tMaterialName, Color* outColor, float* outAlpha )
{
	std::basic_string<TCHAR> tChannelName;
	std::basic_string<TCHAR> tMaxMaterialName = mMaxStdMaterial->GetName();

	MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &MaterialColorOverrides, tMaxMaterialName, tChannelName );

	Color mBaseColor = Color( 1, 1, 1 );

	// copy material colors
	switch( maxChannelId )
	{
		case ID_AM:
		{
			*outColor = colorOverride ? Color( colorOverride->ColorValue ) : mMaxStdMaterial->GetAmbient( this->MaxInterface->GetTime() );
			*outAlpha = colorOverride ? colorOverride->ColorValue[ 3 ] : 1.0f;

			break;
		}
		case ID_DI:
		{
			*outColor = colorOverride ? Color( colorOverride->ColorValue ) : mMaxStdMaterial->GetDiffuse( this->MaxInterface->GetTime() );
			*outAlpha = colorOverride ? colorOverride->ColorValue[ 3 ] : 1.0f;

			break;
		}
		case ID_SP:
		{
			// shininess
			*outColor = colorOverride ? Color( colorOverride->ColorValue ) : mMaxStdMaterial->GetSpecular( this->MaxInterface->GetTime() );
			*outAlpha = colorOverride ? colorOverride->ColorValue[ 3 ] : mMaxStdMaterial->GetShininess( this->MaxInterface->GetTime() ) * 128.f;

			break;
		}
		case ID_SH:
		{
			*outColor = colorOverride ? Color( colorOverride->ColorValue ) : mBaseColor;
			*outAlpha = colorOverride ? colorOverride->ColorValue[ 3 ] : 1.0f;

			break;
		}
		case ID_SS:
		{
			*outColor = colorOverride ? Color( colorOverride->ColorValue ) : mBaseColor;
			*outAlpha = colorOverride ? colorOverride->ColorValue[ 3 ] : 1.0f;

			break;
		}
		case ID_SI:
		{
			*outColor = colorOverride ? Color( colorOverride->ColorValue ) : mMaxStdMaterial->GetSelfIllumColor( this->MaxInterface->GetTime() );
			*outAlpha = colorOverride ? colorOverride->ColorValue[ 3 ] : mMaxStdMaterial->GetSelfIllum( this->MaxInterface->GetTime() );

			break;
		}
		case ID_OP:
		{
			float opacity = mMaxStdMaterial->GetOpacity( this->MaxInterface->GetTime() );

			*outColor = colorOverride ? Color( colorOverride->ColorValue ) : Color( opacity, opacity, opacity );
			*outAlpha = colorOverride ? colorOverride->ColorValue[ 3 ] : opacity;

			break;
		}
		case ID_FI:
		{
			*outColor = colorOverride ? Color( colorOverride->ColorValue ) : mBaseColor;
			*outAlpha = colorOverride ? colorOverride->ColorValue[ 3 ] : 1.0f;

			break;
		}
		case ID_BU:
		{
			*outColor = mBaseColor;
			*outAlpha = 1.0f;
			break;
		}
		case ID_RL:
		{
			*outColor = colorOverride ? Color( colorOverride->ColorValue ) : mBaseColor;
			*outAlpha = colorOverride ? colorOverride->ColorValue[ 3 ] : 1.0f;

			break;
		}
		case ID_RR:
		{
			*outColor = colorOverride ? Color( colorOverride->ColorValue ) : mBaseColor;
			*outAlpha = colorOverride ? colorOverride->ColorValue[ 3 ] : 1.0f;

			break;
		}
		case ID_DP:
		{
			*outColor = colorOverride ? Color( colorOverride->ColorValue ) : mBaseColor;
			*outAlpha = colorOverride ? colorOverride->ColorValue[ 3 ] : 1.0f;

			break;
		}
		default:
		{
			*outColor = mBaseColor;
			*outAlpha = 1.0f;
			break;
		}
	}
}

spShadingNode SimplygonMax::CreateSgMaterial( Texmap* mTexMap,
                                              MaterialNodes::MaterialChannelData& mMaterialChannel,
                                              MaterialNodes::TextureSettingsOverride* textureSettingsOverride )
{
	spShadingNode sgNode = NullPtr;

	if( mTexMap && ( mTexMap->ClassID() == Class_ID( BMTEX_CLASS_ID, 0 ) || ( mTexMap->ClassID() == GNORMAL_CLASS_ID ) ||
	                 ( mTexMap->ClassID() == Class_ID( RGBMULT_CLASS_ID, 0 ) ) || ( mTexMap->ClassID() == Class_ID( TINT_CLASS_ID, 0 ) ) ||
	                 ( mTexMap->ClassID() == Class_ID( COMPOSITE_CLASS_ID, 0 ) ) || ( mTexMap->ClassID() == Class_ID( COLORCORRECTION_CLASS_ID, 0 ) ) ) )
	{
		// see if there are nodes in between slot and texture
		if( mTexMap->ClassID() == Class_ID( BMTEX_CLASS_ID, 0 ) )
		{
			// texture settingsoverride will only apply to the textures directly under it
			sgNode = MaterialNodes::RunBitmapNode( mTexMap, mMaterialChannel, textureSettingsOverride );
		}
		else if( mTexMap->ClassID() == GNORMAL_CLASS_ID )
		{
			// get first texmap of the normal node
			mTexMap = mTexMap->GetSubTexmap( 0 );

			if( mTexMap )
			{
				// check that the texmap is a bitmaptex,
				// if so, use it for this material channel
				Class_ID normalNodeClassId = mTexMap->ClassID();
				if( normalNodeClassId == Class_ID( BMTEX_CLASS_ID, 0 ) )
				{
					sgNode = MaterialNodes::RunBitmapNode( mTexMap, mMaterialChannel );
				}
				else
				{
					LogMaterialNodeMessage( mTexMap, mMaterialChannel.mMaterialName, mMaterialChannel.mChannelName );
				}
			}
		}
		else if( mTexMap->ClassID() == Class_ID( RGBMULT_CLASS_ID, 0 ) )
		{
			sgNode = MaterialNodes::RunMultiplyNode( mTexMap, mMaterialChannel );
		}
		else if( mTexMap->ClassID() == Class_ID( TINT_CLASS_ID, 0 ) )
		{
			sgNode = MaterialNodes::RunTintNode( mTexMap, mMaterialChannel );
		}
		else if( mTexMap->ClassID() == Class_ID( COMPOSITE_CLASS_ID, 0 ) )
		{
			sgNode = MaterialNodes::RunCompositeNode( mTexMap, mMaterialChannel );
		}
		else if( mTexMap->ClassID() == Class_ID( COLORCORRECTION_CLASS_ID, 0 ) )
		{
			sgNode = MaterialNodes::RunColorCorrectionNode( mTexMap, mMaterialChannel );
		}
		else
		{
			sgNode = MaterialNodes::RunBitmapNode( mTexMap, mMaterialChannel );
		}
	}
	else
	{
		// if there is a texmap of unsupported type, output warning
		if( mTexMap )
		{
			Class_ID nodeClassId = mTexMap->ClassID();
			TSTR nodeClassName = _T("Unknown");
			mTexMap->GetClassName( nodeClassName );

			mMaterialChannel.mWarningMessage = mMaterialChannel.mMaterialName + _T(" (") + mMaterialChannel.mChannelName + _T(") - ") +
			                                   std::basic_string<TCHAR>( nodeClassName ) + _T(" texture node is not supported");
		}

		if( mMaterialChannel.IsSTD() )
		{
			const char* materialName = LPCTSTRToConstCharPtr( mMaterialChannel.mMaterialName.c_str() );
			const char* materialChannel = LPCTSTRToConstCharPtr( mMaterialChannel.mChannelName.c_str() );

			// check for material overrides
			TCHAR tTexturePath[ MAX_PATH ] = { 0 };

			// disable sRGB if normal map
			bool bIsSRGB = mMaterialChannel.mMaxChannelId != ID_BU;
			const float gamma = 1.0f;

			std::basic_string<TCHAR> tTexturePathOverride = _T("");
			for( size_t channelIndex = 0; channelIndex < this->MaterialTextureOverrides.size(); ++channelIndex )
			{
				const MaterialTextureOverride& textureOverride = this->MaterialTextureOverrides[ channelIndex ];
				if( strcmp( materialName, LPCTSTRToConstCharPtr( textureOverride.MaterialName.c_str() ) ) == 0 )
				{
					if( strcmp( materialChannel, LPCTSTRToConstCharPtr( textureOverride.MappingChannelName.c_str() ) ) == 0 )
					{
						tTexturePathOverride = textureOverride.TextureFileName;
						bIsSRGB = textureOverride.IsSRGB;
						break;
					}
				}
			}

			// if there is a texture override,
			// continue building material channel
			if( tTexturePathOverride.length() > 0 )
			{
				// set mapping channel to 1
				std::basic_string<TCHAR> tMaxMappingChannel = _T("1");

				// check for uv overrides
				if( this->MaterialChannelOverrides.size() > 0 )
				{
					for( size_t channelIndex = 0; channelIndex < this->MaterialChannelOverrides.size(); ++channelIndex )
					{
						const MaterialTextureMapChannelOverride& mappingChannelOverride = this->MaterialChannelOverrides[ channelIndex ];
						if( strcmp( materialName, LPCTSTRToConstCharPtr( mappingChannelOverride.MaterialName.c_str() ) ) == 0 )
						{
							if( strcmp( materialChannel, LPCTSTRToConstCharPtr( mappingChannelOverride.MappingChannelName.c_str() ) ) == 0 )
							{
								TCHAR tMappingChannelBuffer[ MAX_PATH ] = { 0 };
								_stprintf_s( tMappingChannelBuffer, _T("%d"), mappingChannelOverride.MappingChannel );

								tMaxMappingChannel = tMappingChannelBuffer;
								break;
							}
						}
					}
				}
				// else, use standard channel
				else
				{
					const int maxMappingChannel = 1;
					TCHAR tMappingChannelBuffer[ MAX_PATH ] = { 0 };
					_stprintf_s( tMappingChannelBuffer, _T("%d"), maxMappingChannel );

					tMaxMappingChannel = tMappingChannelBuffer;
				}

				_stprintf_s( tTexturePath, _T("%s"), tTexturePathOverride.c_str() );

				sgNode = MaterialNodes::RunBitmapNode( mTexMap, mMaterialChannel );
			}
		}
	}

	return sgNode;
}

// creates Simplygon material channels based on the channels in the Max StdMaterial
void SimplygonMax::CreateSgMaterialSTDChannel( long maxChannelId, StdMat2* mMaxStdMaterial, spMaterial sgMaterial, bool* hasTextures )
{
	const spString rMaterialName = sgMaterial->GetName();
	const char* cMaterialName = rMaterialName.c_str();
	std::string sMaterialName = cMaterialName;
	std::basic_string<TCHAR> tMaterialName = ConstCharPtrToLPCTSTR( cMaterialName );

	const long maxChannel = mMaxStdMaterial->StdIDToChannel( maxChannelId );

#if MAX_VERSION_MAJOR >= 26
	std::basic_string<TCHAR> tChannelName = mMaxStdMaterial->GetSubTexmapSlotName( maxChannel, true );
#else
	std::basic_string<TCHAR> tChannelName = mMaxStdMaterial->GetSubTexmapSlotName( maxChannel );
#endif

	ReplaceInvalidCharacters( tChannelName, _T( '_' ) );

	const char* cChannelName = LPCTSTRToConstCharPtr( tChannelName.c_str() );

	// add material channel
	if( !sgMaterial->HasMaterialChannel( cChannelName ) )
	{
		sgMaterial->AddMaterialChannel( cChannelName );
	}

	float mAlpha;
	Color mColor;
	const float blendAmount = mMaxStdMaterial->GetTexmapAmt( maxChannel, this->MaxInterface->GetTime() );
	ApplyChannelSpecificModifiers( maxChannelId, mMaxStdMaterial, tMaterialName, &mColor, &mAlpha );

	spShadingColorNode sgBlendAmountNode = sg->CreateShadingColorNode();
	sgBlendAmountNode->SetColor( blendAmount, blendAmount, blendAmount, blendAmount );

	Texmap* mTexMap = mMaxStdMaterial->GetSubTexmap( maxChannel );
	if( mTexMap )
	{
		// float4 finalColor2 = float4( baseColor.rgb * ( 1 - mul2selectedAlpha ), 1.0f );
		// float4 superFinalColor2 = float4( ( map3_x_map4.rgb ) + finalColor2.rgb, 1.0f );
		// float4 result2 = clamp( float4( superFinalColor2 ), 0.0f, 1.0f );
		// result2.a = mul2selectedAlpha;

		MaterialNodes::MaterialChannelData mChannelData = MaterialNodes::MaterialChannelData(
		    tMaterialName, tChannelName, maxChannelId, mMaxStdMaterial, sgMaterial, &this->MaterialTextureOverrides, this->CurrentTime, false );

		spShadingNode sgShadingNode = CreateSgMaterial( mTexMap, mChannelData );

		if( sgShadingNode == NullPtr )
		{
			mChannelData.mWarningMessage += _T(", replacing with basecolor node.");
			SimplygonMaxInstance->LogToWindow( mChannelData.mWarningMessage, Warning );

			spShadingColorNode sgBaseColorNode = sg->CreateShadingColorNode();
			sgBaseColorNode->SetColor( mColor.r, mColor.g, mColor.b, 1.0f );

			sgShadingNode = sgBaseColorNode;
		}

		const char* cChannelName = LPCTSTRToConstCharPtr( tChannelName.c_str() );

		if( maxChannelId == ID_BU )
		{
			sgMaterial->SetUseTangentSpaceNormals( true );
		}

		if( maxChannelId == ID_OP )
		{
			bool bTextureHasAlpha = true;
			bool bHasActiveTransparency = false;
			int alphaSource = ALPHA_FILE;

			// bitmap check
			if( mTexMap->ClassID() == Class_ID( BMTEX_CLASS_ID, 0 ) )
			{
				BitmapTex* bitmapTex = (BitmapTex*)mTexMap;
				TCHAR tTexturePath[ MAX_PATH ] = { 0 };
				GetImageFullFilePath( bitmapTex->GetMapName(), tTexturePath );

				bTextureHasAlpha = TextureHasAlpha( LPCTSTRToConstCharPtr( tTexturePath ) );
				bHasActiveTransparency = HasActiveTransparency( bitmapTex );
				alphaSource = bitmapTex->GetAlphaSource();
			}

			if( bHasActiveTransparency == FALSE )
			{
				spShadingColorNode sgOneNode = sg->CreateShadingColorNode();
				sgOneNode->SetColor( 1, 1, 1, 1 );

				spShadingSwizzlingNode sgRedSwizzleNode = sg->CreateShadingSwizzlingNode();
				sgRedSwizzleNode->SetInput( 0, sgShadingNode );
				sgRedSwizzleNode->SetInput( 1, sgShadingNode );
				sgRedSwizzleNode->SetInput( 2, sgShadingNode );
				sgRedSwizzleNode->SetInput( 3, sgShadingNode );

				sgRedSwizzleNode->SetRedComponent( 0 );
				sgRedSwizzleNode->SetGreenComponent( 0 );
				sgRedSwizzleNode->SetBlueComponent( 0 );
				sgRedSwizzleNode->SetAlphaComponent( 0 );

				spShadingSwizzlingNode sgGreenSwizzleNode = sg->CreateShadingSwizzlingNode();
				sgGreenSwizzleNode->SetInput( 0, sgShadingNode );
				sgGreenSwizzleNode->SetInput( 1, sgShadingNode );
				sgGreenSwizzleNode->SetInput( 2, sgShadingNode );
				sgGreenSwizzleNode->SetInput( 3, sgShadingNode );

				sgGreenSwizzleNode->SetRedComponent( 1 );
				sgGreenSwizzleNode->SetGreenComponent( 1 );
				sgGreenSwizzleNode->SetBlueComponent( 1 );
				sgGreenSwizzleNode->SetAlphaComponent( 1 );

				spShadingSwizzlingNode sgBlueSwizzleNode = sg->CreateShadingSwizzlingNode();
				sgBlueSwizzleNode->SetInput( 0, sgShadingNode );
				sgBlueSwizzleNode->SetInput( 1, sgShadingNode );
				sgBlueSwizzleNode->SetInput( 2, sgShadingNode );
				sgBlueSwizzleNode->SetInput( 3, sgShadingNode );

				sgBlueSwizzleNode->SetRedComponent( 2 );
				sgBlueSwizzleNode->SetGreenComponent( 2 );
				sgBlueSwizzleNode->SetBlueComponent( 2 );
				sgBlueSwizzleNode->SetAlphaComponent( 2 );

				spShadingAddNode sgAddRGNode = sg->CreateShadingAddNode();
				sgAddRGNode->SetInput( 0, sgRedSwizzleNode );
				sgAddRGNode->SetInput( 1, sgGreenSwizzleNode );

				spShadingAddNode sgAddRGBNode = sg->CreateShadingAddNode();
				sgAddRGBNode->SetInput( 0, sgAddRGNode );
				sgAddRGBNode->SetInput( 1, sgBlueSwizzleNode );

				spShadingColorNode sg1Through3Node = sg->CreateShadingColorNode();
				sg1Through3Node->SetDefaultParameter( 0, 3, 3, 3, 3 );

				spShadingDivideNode sgDivideNode = sg->CreateShadingDivideNode();
				sgDivideNode->SetInput( 0, sgAddRGBNode );
				sgDivideNode->SetInput( 1, sg1Through3Node );

				spShadingSwizzlingNode sgAlphaSwizzleNode = sg->CreateShadingSwizzlingNode();
				sgAlphaSwizzleNode->SetInput( 0, sgShadingNode );
				sgAlphaSwizzleNode->SetInput( 1, sgShadingNode );
				sgAlphaSwizzleNode->SetInput( 2, sgShadingNode );
				sgAlphaSwizzleNode->SetInput( 3, sgShadingNode );

				sgAlphaSwizzleNode->SetRedComponent( 3 );
				sgAlphaSwizzleNode->SetGreenComponent( 3 );
				sgAlphaSwizzleNode->SetBlueComponent( 3 );
				sgAlphaSwizzleNode->SetAlphaComponent( 3 );

				spShadingSubtractNode sgOneMinusNode = sg->CreateShadingSubtractNode();
				sgOneMinusNode->SetInput( 0, sgOneNode );
				sgOneMinusNode->SetInput( 1, sgAlphaSwizzleNode );

				spShadingAddNode sgAddInvAlphaNode = sg->CreateShadingAddNode();
				sgAddInvAlphaNode->SetInput( 0, sgOneMinusNode );
				sgAddInvAlphaNode->SetInput( 1, sgDivideNode );

				spShadingSwizzlingNode sgFinalSwizzleNode = sg->CreateShadingSwizzlingNode();
				sgFinalSwizzleNode->SetInput( 0, sgShadingNode );
				sgFinalSwizzleNode->SetInput( 1, sgShadingNode );
				sgFinalSwizzleNode->SetInput( 2, sgShadingNode );
				sgFinalSwizzleNode->SetInput( 3, sgAddInvAlphaNode );

				sgFinalSwizzleNode->SetRedComponent( 0 );
				sgFinalSwizzleNode->SetGreenComponent( 1 );
				sgFinalSwizzleNode->SetBlueComponent( 2 );
				sgFinalSwizzleNode->SetAlphaComponent( 3 );

				sgShadingNode = sgFinalSwizzleNode;
			}

			spShadingSwizzlingNode sgSwizzleNode = sg->CreateShadingSwizzlingNode();
			sgSwizzleNode->SetInput( 0, sgShadingNode );
			sgSwizzleNode->SetInput( 1, sgShadingNode );
			sgSwizzleNode->SetInput( 2, sgShadingNode );
			sgSwizzleNode->SetInput( 3, sgShadingNode );
			if( bTextureHasAlpha )
			{
				sgSwizzleNode->SetRedComponent( 3 );
				sgSwizzleNode->SetGreenComponent( 3 );
				sgSwizzleNode->SetBlueComponent( 3 );
				sgSwizzleNode->SetAlphaComponent( 3 );
			}
			else
			{
				sgSwizzleNode->SetRedComponent( 0 );
				sgSwizzleNode->SetGreenComponent( 0 );
				sgSwizzleNode->SetBlueComponent( 0 );
				sgSwizzleNode->SetAlphaComponent( 0 );
			}

			sgMaterial->SetShadingNetwork( cChannelName, sgSwizzleNode );
		}
		else
		{
			spShadingColorNode sgOneNode = sg->CreateShadingColorNode();
			sgOneNode->SetColor( 1.0f, 1.0f, 1.0f, 1.0f );

			spShadingColorNode sgZeroNode = sg->CreateShadingColorNode();
			sgZeroNode->SetColor( 0.0f, 0.0f, 0.0f, 0.0f );

			spShadingColorNode sgDestinationNode = sg->CreateShadingColorNode();
			sgDestinationNode->SetColor( mColor.r, mColor.g, mColor.b, 1.0f );

			// float4 finalColor2 = float4( baseColor.rgb * ( 1 - mul2selectedAlpha ), 1.0f );
			spShadingSwizzlingNode sgAlphaNode = sg->CreateShadingSwizzlingNode();
			sgAlphaNode->SetInput( 0, sgShadingNode );
			sgAlphaNode->SetInput( 1, sgShadingNode );
			sgAlphaNode->SetInput( 2, sgShadingNode );
			sgAlphaNode->SetInput( 3, sgShadingNode );
			sgAlphaNode->SetRedComponent( 3 );
			sgAlphaNode->SetGreenComponent( 3 );
			sgAlphaNode->SetBlueComponent( 3 );
			sgAlphaNode->SetAlphaComponent( 3 );

			spShadingSubtractNode sgInvAlphaNode = sg->CreateShadingSubtractNode();
			sgInvAlphaNode->SetInput( 0, sgOneNode );
			sgInvAlphaNode->SetInput( 1, sgAlphaNode );

			spShadingMultiplyNode sgDestinationXInvAlphaNode = sg->CreateShadingMultiplyNode();
			sgDestinationXInvAlphaNode->SetInput( 0, sgDestinationNode );
			sgDestinationXInvAlphaNode->SetInput( 1, sgInvAlphaNode );

			spShadingAddNode sgAddNode = sg->CreateShadingAddNode();
			sgAddNode->SetInput( 0, sgShadingNode );
			sgAddNode->SetInput( 1, sgDestinationXInvAlphaNode );

			spShadingClampNode sgClampNode = sg->CreateShadingClampNode();
			sgClampNode->SetInput( 0, sgAddNode );
			sgClampNode->SetInput( 1, sgZeroNode );
			sgClampNode->SetInput( 2, sgOneNode );

			spShadingInterpolateNode sgBlendBaseNode = sg->CreateShadingInterpolateNode();
			sgBlendBaseNode->SetInput( 0, sgDestinationNode );
			sgBlendBaseNode->SetInput( 1, sgClampNode );
			sgBlendBaseNode->SetInput( 2, sgBlendAmountNode );

			spShadingSwizzlingNode sgAlphaOneSwizzleNode = sg->CreateShadingSwizzlingNode();
			sgAlphaOneSwizzleNode->SetInput( 0, sgBlendBaseNode );
			sgAlphaOneSwizzleNode->SetInput( 1, sgBlendBaseNode );
			sgAlphaOneSwizzleNode->SetInput( 2, sgBlendBaseNode );
			sgAlphaOneSwizzleNode->SetInput( 3, sgAlphaNode );
			sgAlphaOneSwizzleNode->SetRedComponent( 0 );
			sgAlphaOneSwizzleNode->SetGreenComponent( 1 );
			sgAlphaOneSwizzleNode->SetBlueComponent( 2 );
			sgAlphaOneSwizzleNode->SetAlphaComponent( 3 );

			sgMaterial->SetShadingNetwork( cChannelName, sgAlphaOneSwizzleNode );
		}
	}
	else
	{
		if( maxChannelId != ID_BU )
		{
			spShadingColorNode sgDestinationNode = sg->CreateShadingColorNode();
			sgDestinationNode->SetColor( mColor.r, mColor.g, mColor.b, 1.0f );

			sgMaterial->SetShadingNetwork( cChannelName, sgDestinationNode );
		}
	}
}

// creates a Simplygon material based on the given MaxMaterial (shading-network or not)
std::pair<std::string, int> SimplygonMax::AddMaxMaterialToSgScene( Mtl* mMaxMaterial )
{
	bool bTextureChannelsInUse[ NTEXMAPS ] = { false };

	// create sg material
	spMaterial sgMaterial = sg->CreateMaterial();
	sgMaterial->SetBlendMode( EMaterialBlendMode::Blend );

	std::basic_string<TCHAR> tMaterialName = mMaxMaterial->GetName();
	const char* cMaterialName = LPCTSTRToConstCharPtr( tMaterialName.c_str() );
	sgMaterial->SetName( cMaterialName );

	bool tryUsingNewMaterialSystem = this->UseNewMaterialSystem;

	if( tryUsingNewMaterialSystem )
	{
		// initialize nodes in the node table
		this->InitializeNodesInNodeTable();

		// setup a shading network for the material
		ShadingNetworkProxy* materialProxy = this->GetProxyShadingNetworkMaterial( std::basic_string<TCHAR>( tMaterialName ) );

		MtlBase* mMaxBaseMaterial = static_cast<MtlBase*>( mMaxMaterial );
		if( !mMaxBaseMaterial )
			return std::pair<std::string, int>( "", 0 );

		if( materialProxy == nullptr )
			return std::pair<std::string, int>( "", 0 );

		this->SetupMaterialWithCustomShadingNetwork( sgMaterial, materialProxy );
		IDxMaterial3* mMaxDXMaterial = (IDxMaterial3*)mMaxMaterial->GetInterface( IDXMATERIAL3_INTERFACE );

		if( mMaxDXMaterial != nullptr )
		{
			IParameterManager* mParameterManager = mMaxDXMaterial->GetCurrentParameterManager();

			// for each parameter in the material go through it and look for names from the node-list to to populate
			std::vector<std::basic_string<TCHAR>> tTextureNames;
			for( int parameterIndex = 0; parameterIndex < mParameterManager->GetNumberOfParams(); parameterIndex++ )
			{
				bool bIsAttribute = false;
				int parameterType = mParameterManager->GetParamType( parameterIndex );

				MaxSemantics mParameterSemantics = mParameterManager->GetParamSemantics( parameterIndex );
				std::basic_string<TCHAR> tParameterName = mParameterManager->GetParamName( parameterIndex );

				spShadingNode sgShadingNode = this->GetSpShadingNodeFromTable( tParameterName, materialProxy );
				AttributeData* parameterAttribute = this->GetNodeAttribute( tParameterName, materialProxy );

				// if no node found in shading network continue looking
				if( sgShadingNode.IsNull() && parameterAttribute == nullptr )
				{
					// push texture
					if( parameterType == 1010 || parameterType == 1009 || parameterType == IParameterManager::kPType_Texture )
					{
						tTextureNames.push_back( tParameterName );
					}
					continue;
				}

				// get up the attribute in node proxy object
				// only supported int, bool and float types
				// extend later to include more types
				if( parameterAttribute )
				{
					if( parameterType == IParameterManager::kPType_Int )
					{
						int val;
						mParameterManager->GetParamData( (void*)&val, parameterIndex );
						parameterAttribute->DataType = nat_Int;
						parameterAttribute->IntData = (float)val;
						parameterAttribute->Data = &val;
					}
					else if( parameterType == IParameterManager::kPType_Bool )
					{
						bool val;
						mParameterManager->GetParamData( (void*)&val, parameterIndex );
						parameterAttribute->DataType = nat_Bool;
						parameterAttribute->BoolData = val;
						parameterAttribute->Data = &val;
					}
					else if( parameterType == IParameterManager::kPType_Float )
					{
						float val;
						mParameterManager->GetParamData( (void*)&val, parameterIndex );
						parameterAttribute->DataType = nat_Float;
						parameterAttribute->FloatData = val;
						parameterAttribute->Data = &val;
					}
				}
				else if( !sgShadingNode.IsNull() )
				{
					// check if node is texture node do stuff
					if( parameterType == 1010 || parameterType == 1009 || parameterType == IParameterManager::kPType_Texture )
					{
						tTextureNames.push_back( tParameterName );
					}
					// check if node is color node do stuff
					else if( parameterType == IParameterManager::kPType_Color )
					{
						float color[ 4 ] = { 0 };
						bool bHasParam = mParameterManager->GetParamData( color, parameterIndex );

						if( !bHasParam )
							return std::pair<std::string, int>( "", 0 );

						// possible bug
						spShadingNode sgPossibleColorNode = this->GetSpShadingNodeFromTable( tParameterName, materialProxy );

						if( sgPossibleColorNode.IsNull() )
							return std::pair<std::string, int>( "", 0 );

						spShadingColorNode sgColorNode = spShadingColorNode::SafeCast( sgPossibleColorNode );

						if( !sgColorNode )
							return std::pair<std::string, int>( "", 0 );

						sgColorNode->SetDefaultParameter( 0, color[ 0 ], color[ 1 ], color[ 2 ], color[ 3 ] );
					}
				}
			}

			// now we have the textures setup nodes
			for( uint textureIndex = 0; textureIndex < tTextureNames.size(); ++textureIndex )
			{
				spShadingNode sgShadingNode = this->GetSpShadingNodeFromTable( tTextureNames[ textureIndex ], materialProxy );
				if( sgShadingNode.IsNull() )
				{
					continue;
				}

				int nodeId = INT_MAX;
				NodeProxy* nodeProxy = this->GetNodeFromTable( tTextureNames[ textureIndex ], materialProxy, &nodeId );
				spShadingTextureNode sgTextureNode = spShadingTextureNode::SafeCast( sgShadingNode );

				std::map<std::basic_string<TCHAR>, AttributeData*>::iterator tAttributeIterator;
				for( tAttributeIterator = nodeProxy->Attributes.begin(); tAttributeIterator != nodeProxy->Attributes.end(); tAttributeIterator++ )
				{
					if( tAttributeIterator->second->NodeId != nodeId )
						continue;

					if( tAttributeIterator->second->NodeAttrType == static_cast<int>( TileU ) && tAttributeIterator->second->FloatData != 1.0f )
					{
						sgTextureNode->SetTileU( tAttributeIterator->second->FloatData );
					}
					else if( tAttributeIterator->second->NodeAttrType == static_cast<int>( TileV ) && tAttributeIterator->second->FloatData != 1.0f )
					{
						sgTextureNode->SetTileV( tAttributeIterator->second->FloatData );
					}
					else if( tAttributeIterator->second->NodeAttrType == static_cast<int>( UVChannel ) && tAttributeIterator->second->IntData != 0 )
					{
						TCHAR tMappingChannel[ MAX_PATH ] = { 0 };
						_stprintf( tMappingChannel, _T("%d"), (int)tAttributeIterator->second->IntData );
						sgTextureNode->SetTexCoordName( LPCTSTRToConstCharPtr( tMappingChannel ) );
					}
					else if( tAttributeIterator->second->NodeAttrType == static_cast<int>( TileUV ) && tAttributeIterator->second->FloatData != 1.0f )
					{
						sgTextureNode->SetTileV( tAttributeIterator->second->FloatData );
						sgTextureNode->SetTileU( tAttributeIterator->second->FloatData );
					}
					else if( tAttributeIterator->second->NodeAttrType == static_cast<int>( OffsetU ) && tAttributeIterator->second->FloatData != 0.0f )
					{
						sgTextureNode->SetOffsetU( tAttributeIterator->second->FloatData );
					}
					else if( tAttributeIterator->second->NodeAttrType == static_cast<int>( OffsetV ) && tAttributeIterator->second->FloatData != 0.0f )
					{
						sgTextureNode->SetOffsetV( -tAttributeIterator->second->FloatData );
					}
					else if( tAttributeIterator->second->NodeAttrType == static_cast<int>( OffsetUV ) && tAttributeIterator->second->FloatData != 0.0f )
					{
						sgTextureNode->SetOffsetU( tAttributeIterator->second->FloatData );
						sgTextureNode->SetOffsetV( -tAttributeIterator->second->FloatData );
					}
				}

				if( nodeProxy->UVOverride != -1 )
				{
					TCHAR tMappingChannel[ MAX_PATH ] = { 0 };
					_stprintf( tMappingChannel, _T("%d"), nodeProxy->UVOverride );
					sgTextureNode->SetTexCoordName( LPCTSTRToConstCharPtr( tMappingChannel ) );
				}
				if( nodeProxy->uTilingOverride )
				{
					sgTextureNode->SetTileU( nodeProxy->uTiling );
				}
				if( nodeProxy->vTilingOverride )
				{
					sgTextureNode->SetTileV( nodeProxy->vTiling );
				}

				if( nodeProxy->uOffsetOverride )
				{
					sgTextureNode->SetOffsetU( nodeProxy->uOffset );
				}
				if( nodeProxy->vOffsetOverride )
				{
					sgTextureNode->SetOffsetV( -nodeProxy->vOffset );
				}

				// this->MapDxTextureToSGTexNode(maxDXMaterial, index, tex_node);
				PBBitmap* mPBBitmap = mMaxDXMaterial->GetEffectBitmap( textureIndex );
				int numEffectBitmaps = mMaxDXMaterial->GetNumberOfEffectBitmaps();

				if( mPBBitmap )
				{
					BitmapInfo mBitmapInfo = mPBBitmap->bi;

					IDxMaterial2::BitmapTypes mDXBitmapType = mMaxDXMaterial->GetBitmapUsage( textureIndex );
					const int mBitmapMappingChannel = mMaxDXMaterial->GetBitmapMappingChannel( textureIndex );

					TCHAR tFileDestination[ MAX_PATH ];
					const TCHAR* tFilePath = mBitmapInfo.Name();
					const TCHAR* tFileName = mBitmapInfo.Filename();

					// retrieve the file path
					GetImageFullFilePath( tFilePath, tFileDestination );

					if( _tcslen( tFilePath ) > 0 )
					{
						// create texture node
						std::basic_string<TCHAR> tTexturePathWithName = this->ImportTexture( std::basic_string<TCHAR>( tFilePath ) );
						std::basic_string<TCHAR> tTextureName = GetTitleOfFile( tTexturePathWithName );
						std::basic_string<TCHAR> tTextureExtension = GetExtensionOfFile( tTexturePathWithName );
						std::basic_string<TCHAR> tTextureNameWithExtension = tTextureName + tTextureExtension;

						sgTextureNode->SetTextureName( LPCTSTRToConstCharPtr( tTextureName.c_str() ) );

						char cMappingChannel[ MAX_PATH ];
						itoa( 1, cMappingChannel, 10 );
						if( !sgTextureNode->GetTexCoordName().IsNullOrEmpty() && !sgTextureNode->GetTexCoordName().c_str() == NULL )
						{
							const spString uvSet = sgTextureNode->GetTexCoordName();
						}
						else
						{
							sgTextureNode->SetTexCoordName( cMappingChannel );
						}

						bool bIsSRGB = false;

						const float gamma = mBitmapInfo.Gamma();
						if( gamma < 2.3f && gamma > 2.1f )
						{
							bIsSRGB = true;
						}

						if( nodeProxy->isSRGBOverride )
							sgTextureNode->SetColorSpaceOverride( nodeProxy->isSRGB ? Simplygon::EImageColorSpace::sRGB : Simplygon::EImageColorSpace::Linear );
						else
							sgTextureNode->SetColorSpaceOverride( bIsSRGB ? Simplygon::EImageColorSpace::sRGB : Simplygon::EImageColorSpace::Linear );

						// add extra mapping for post processing of data
						std::map<spShadingTextureNode, std::basic_string<TCHAR>>::const_iterator& shadingNodeIterator =
						    this->ShadingTextureNodeToPath.find( sgTextureNode );
						if( shadingNodeIterator == this->ShadingTextureNodeToPath.end() )
						{
							this->ShadingTextureNodeToPath.insert(
							    std::pair<spShadingTextureNode, std::basic_string<TCHAR>>( sgTextureNode, tTexturePathWithName ) );
						}
					}
				}

				// validate texture node
				if( sgTextureNode->GetTexCoordName().IsNullOrEmpty() && sgTextureNode->GetTextureName().IsNullOrEmpty() )
				{
					std::basic_string<TCHAR> tErrorMessage( _T("The texture was not found: ") );
					tErrorMessage +=
					    materialProxy->GetName() + std::basic_string<TCHAR>( _T(" - ") ) + tTextureNames[ textureIndex ] + std::basic_string<TCHAR>( _T("\n") );

					the_listener->edit_stream->puts( tErrorMessage.c_str() );
					the_listener->edit_stream->flush();

					this->MaxInterface->Log()->LogEntry( SYSLOG_INFO, NO_DIALOG, _M( "Simplygon shading networks" ), tErrorMessage.c_str() );
				}
			}

			// convert material
			for( uint channelIndex = 0; channelIndex < sgMaterial->GetMaterialChannelCount(); ++channelIndex )
			{
				const spString rChannelName = sgMaterial->GetMaterialChannelFromIndex( channelIndex );

				// is channel name valid?
				if( rChannelName.IsNullOrEmpty() )
					continue;

				else if( rChannelName.c_str() == nullptr )
					continue;

				const char* cChannelName = rChannelName.c_str();

				// fetch all shading texture nodes from material
				spShadingNode sgExitNode = sgMaterial->GetShadingNetwork( cChannelName );
				if( sgExitNode.IsNull() )
					continue;

				std::map<std::basic_string<TCHAR>, spShadingTextureNode> shadingTextureNodeList;
				FindAllUpStreamTextureNodes( sgExitNode, shadingTextureNodeList );

				for( std::pair<std::basic_string<TCHAR>, spShadingTextureNode> shadingTextureIterator : shadingTextureNodeList )
				{
					spShadingTextureNode sgTextureNode = shadingTextureIterator.second;

					if( sgTextureNode.IsNull() )
						continue;

					else if( sgTextureNode->GetTextureName().IsNullOrEmpty() )
						continue;

					spString rTextureName = sgTextureNode->GetTextureName();
					std::basic_string<TCHAR> tTexturePath = ConstCharPtrToLPCTSTR( rTextureName.c_str() );

					// if full path found, use it
					std::map<spShadingTextureNode, std::basic_string<TCHAR>>::const_iterator& sgTextureToPathIterator =
					    this->ShadingTextureNodeToPath.find( sgTextureNode );
					if( sgTextureToPathIterator != this->ShadingTextureNodeToPath.end() )
					{
						tTexturePath = sgTextureToPathIterator->second;
					}

					std::basic_string<TCHAR> tTextureName = GetTitleOfFile( tTexturePath );
					std::basic_string<TCHAR> tTextureExtension = GetExtensionOfFile( tTexturePath );
					std::basic_string<TCHAR> tTextureNameWithExtension = tTextureName + tTextureExtension;

					const spString sgsdkTexCoordName = sgTextureNode->GetTexCoordName();
					const bool sgsdkUseSRGB = sgTextureNode->GetColorSpaceOverride() == Simplygon::EImageColorSpace::sRGB;
					const uint sgsdkParamCount = sgTextureNode->GetParameterCount();

					// create texture and add it to scene
					spTexture sgTexture;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& textureIterator =
					    this->LoadedTexturePathToID.find( tTexturePath );
					const bool bTextureInUse = ( textureIterator != this->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = this->SceneHandler->sgScene->GetTextureTable()->FindTextureUsingFilePath( LPCTSTRToConstCharPtr( tTexturePath.c_str() ) );
					}
					else
					{
						sgTexture = sg->CreateTexture();
						sgTexture->SetName( LPCTSTRToConstCharPtr( tTextureName.c_str() ) );
						sgTexture->SetFilePath( LPCTSTRToConstCharPtr( tTexturePath.c_str() ) );

						this->SceneHandler->sgScene->GetTextureTable()->AddTexture( sgTexture );
					}

					// if the texture was not already in scene, copy it to local work folder
					if( !bTextureInUse )
					{
						const spString rTexturePathWithName = sgTexture->GetFilePath();
						this->LoadedTexturePathToID.insert( std::pair<std::basic_string<TCHAR>, std::basic_string<TCHAR>>(
						    tTexturePath, ConstCharPtrToLPCTSTR( rTexturePathWithName.c_str() ) ) );
					}
				}
			}
		}
		else
		{
			// this is not dx material
			// fall back to std material
			tryUsingNewMaterialSystem = false;
		}
	}

	// use old system as standard, or fallback
	if( !tryUsingNewMaterialSystem )
	{
		Class_ID mClassId = mMaxMaterial->ClassID();

		// find max std materials
		if( mClassId == Class_ID( DMTL_CLASS_ID, 0 ) )
		{
			// use opacity as standard for StdMat
			sgMaterial->SetOpacityType( EOpacityType::Opacity );

			StdMat2* mMaxStdMaterial = (StdMat2*)mMaxMaterial;

			// check for standard channels (see mapping)
			for( long maxChannelId = 0; maxChannelId < NTEXMAPS; ++maxChannelId )
			{
				this->CreateSgMaterialSTDChannel( maxChannelId, mMaxStdMaterial, sgMaterial, bTextureChannelsInUse );
			}
		}

#if MAX_VERSION_MAJOR >= 23
		// find max physical materials
		else if( mClassId == PHYSICAL_MATERIAL_CLASS_ID )
		{
			// use transparency as standard for PhysicalMaterial
			sgMaterial->SetOpacityType( EOpacityType::Transparency );

			PhysicalMaterial mPhysicalMaterial( this );
			mPhysicalMaterial.ReadPropertiesFromMaterial( mMaxMaterial );
			mPhysicalMaterial.ConvertToSimplygonMaterial( sgMaterial, this->CurrentTime );
		}
#endif
	}

	spMaterialTable sgMaterialTable = this->SceneHandler->sgScene->GetMaterialTable();
	const rid globalMaterialIndex = sgMaterialTable->GetMaterialsCount();
	sgMaterialTable->AddMaterial( sgMaterial );

	const spString rMaterialId = sgMaterial->GetMaterialGUID();
	const char* cMaterialId = rMaterialId.c_str();

	return std::pair<std::string, int>( cMaterialId, globalMaterialIndex );
}

#pragma region STANDIN_TEXTURE_GENERATION
#pragma pack( 1 )

struct BMPheader
{
	char magic[ 2 ];
	int file_size;
	int unused;
	int offset;
	int header_size;
	int sizeX;
	int sizeY;
	short planes;
	short BPP;
	int type;
	int data_size;
	int dpiX;
	int dpiY;
	int palette_colors;
	int important_colors;
};

#pragma pack()

// w must be a multiple of 4
BMPheader SetupBMPHeader( int width, int height )
{
	BMPheader header;
	header.magic[ 0 ] = 66;
	header.magic[ 1 ] = 77;
	header.file_size = width * height * 3 + 54;
	header.unused = 0;
	header.offset = 54;
	header.header_size = 40;
	header.sizeX = width;
	header.sizeY = height;
	header.planes = 1;
	header.BPP = 24;
	header.type = 0;
	header.data_size = width * height * 3; // 24 bpp
	header.dpiX = 2835;
	header.dpiY = 2835;
	header.palette_colors = 0;
	header.important_colors = 0;
	return header;
}

const int TEXTURE_WIDTH = 256;
const int TEXTURE_HEIGHT = 256;

bool WriteStandinTexture( const TCHAR* tOutputFilePath )
{
	assert( TEXTURE_WIDTH <= SHRT_MAX && TEXTURE_WIDTH > 0 );
	assert( TEXTURE_HEIGHT <= SHRT_MAX && TEXTURE_HEIGHT > 0 );

	FILE* textureFile = _tfopen( tOutputFilePath, _T("wb") );
	if( !textureFile )
		return false;

	BMPheader header = SetupBMPHeader( TEXTURE_WIDTH, TEXTURE_HEIGHT );
	fwrite( &header, sizeof( BMPheader ), 1, textureFile );
	unsigned char* textureData = new unsigned char[ TEXTURE_WIDTH * TEXTURE_HEIGHT * 3 ];

	for( int y = 0; y < TEXTURE_HEIGHT; ++y )
	{
		for( int x = 0; x < TEXTURE_WIDTH; ++x )
		{
			textureData[ ( x + y * TEXTURE_WIDTH ) * 3 + 0 ] = static_cast<unsigned char>( ( x * 0xff ) / TEXTURE_WIDTH );
			if( ( ( ( x >> 3 ) & 0x1 ) ^ ( ( y >> 3 ) & 0x1 ) ) )
			{
				textureData[ ( x + y * TEXTURE_WIDTH ) * 3 + 1 ] = static_cast<unsigned char>( 0 );
			}
			else
			{
				textureData[ ( x + y * TEXTURE_WIDTH ) * 3 + 1 ] = static_cast<unsigned char>( 0xff );
			}
			textureData[ ( x + y * TEXTURE_WIDTH ) * 3 + 2 ] = static_cast<unsigned char>( ( y * 0xff ) / TEXTURE_HEIGHT );
		}
	}

	fwrite( textureData, 1, TEXTURE_WIDTH * TEXTURE_HEIGHT * 3, textureFile );
	delete[] textureData;
	fclose( textureFile );
	return true;
}
#pragma endregion

// imports a texture to the current work-directory, reusing textures if possible
std::basic_string<TCHAR> SimplygonMax::ImportTexture( std::basic_string<TCHAR> tOriginalFilePath )
{
	// look for the texture in the list of imported textures
	for( uint i = 0; i < uint( this->ImportedTextures.size() ); ++i )
	{
		if( this->ImportedTextures[ i ].OriginalPath == tOriginalFilePath )
		{
			return this->ImportedTextures[ i ].ImportedPath;
		}
	}

	WorkDirectoryHandler* workDirectoryHandler = this->GetWorkDirectoryHandler();

	bool textureDirectoryOverrideInUse = false;
	std::basic_string<TCHAR> tTexturePathOverride = workDirectoryHandler->GetTextureOutputDirectoryOverride();
	if( tTexturePathOverride.length() > 0 )
	{
		bool folderCreated = CreateFolder( tTexturePathOverride );
		if( !folderCreated )
		{
			return _T("");
		}
		else
		{
			textureDirectoryOverrideInUse = true;
		}
	}

	// not found, we have to import it
	std::basic_string<TCHAR> tSourcePath = GetFullPathOfFile( tOriginalFilePath );

	ImportedTexture importedTexture;

	if( this->copyTextures )
	{
		// setup a name for the imported texture
		std::basic_string<TCHAR> tImportName =
		    GetNonConflictingNameInPath( _T(""), GetTitleOfFile( tSourcePath ).c_str(), GetExtensionOfFile( tSourcePath ).c_str() );

		bool bHasExportDirectory = false;

		// create folder if it does not exist
		std::basic_string<TCHAR> tExportDirectory = workDirectoryHandler->GetExportWorkDirectory();
		std::basic_string<TCHAR> tExportTexturesDirectory = workDirectoryHandler->GetExportTexturesPath();

		if( tExportDirectory.length() > 0 )
		{
			bHasExportDirectory = true;
			CreateFolder( tExportTexturesDirectory );
		}

		// fetch original textures folder
		std::basic_string<TCHAR> tExportOriginalTexturesDirectory = workDirectoryHandler->GetOriginalTexturesPath();

		std::basic_string<TCHAR> tImportPath = _T("");

		int indexer = 1;
		while( true )
		{
			// if import directory is set, use it as root
			if( bHasExportDirectory )
			{
				tImportPath = Combine( tExportTexturesDirectory, tImportName );
			}

			// otherwise, use original directory as root
			else
			{
				tImportPath = Combine( tExportOriginalTexturesDirectory, tImportName );
			}

			if( FileExists( tImportPath.c_str() ) )
			{
				tImportName =
				    GetTitleOfFile( tSourcePath ) + ConstCharPtrToLPCTSTR( std::to_string( indexer ).c_str() ) + GetExtensionOfFile( tSourcePath ).c_str();
				indexer++;
			}
			else
			{
				break;
			}
		}

		// if we have the texture file, copy it into our work directory
		bool bImported = false;
		if( FileExists( tSourcePath ) )
		{
			if( CopyFile( tSourcePath.c_str(), tImportPath.c_str(), FALSE ) )
			{
				// check file attributes for ReadOnly-flag, remove if possible
				const DWORD dwFileAttributes = GetFileAttributes( tImportPath.c_str() );
				const bool bIsReadOnly = dwFileAttributes & FILE_ATTRIBUTE_READONLY;
				if( bIsReadOnly )
				{
					const BOOL bFileAttributeSet = SetFileAttributes( tImportPath.c_str(), FILE_ATTRIBUTE_NORMAL );
					if( !bFileAttributeSet )
					{
						std::basic_string<TCHAR> tWarningMessage =
						    _T("Warning, could not restore file attributes, please make sure that the file has normal file ")
						    _T("attributes or that Max has the privileges to change them.\nFile: ");
						tWarningMessage += tImportPath.c_str();
						tWarningMessage += _T("\n\n");

						this->LogMessageToScriptEditor( tWarningMessage );
					}
				}

				bImported = true;
			}
		}

		// if the texture was not found, or could not be read, use a stand in texture
		if( !bImported )
		{
			std::basic_string<TCHAR> tWarningMessage;

			tWarningMessage += _T("Failed to import texture: ");
			tWarningMessage += tSourcePath.c_str();
			tWarningMessage += _T(", using a stand-in texture");

			this->LogToWindow( tWarningMessage, Warning );

			WriteStandinTexture( tImportPath.c_str() );
		}

		// add the imported texture info
		importedTexture.OriginalPath = tOriginalFilePath;
		importedTexture.ImportedPath = tImportPath;
	}
	else
	{
		importedTexture.OriginalPath = tOriginalFilePath;
		importedTexture.ImportedPath = tOriginalFilePath;
	}

	this->ImportedTextures.push_back( importedTexture );

	return importedTexture.ImportedPath;
}

// sets a node as input on the given input channel
bool SimplygonMax::SetInputNode( int nodeId, int inputChannel, int nodeToConnectId )
{
	this->nodeTable[ nodeId ]->SetNodeInput( inputChannel, nodeToConnectId );
	return true;
}

// takes in node id and vertex color channel to read vertex colors from
bool SimplygonMax::SetVertexColorChannel( int nodeId, int vertexColorChannel )
{
	this->nodeTable[ nodeId ]->SetVertexColorChannel( vertexColorChannel );
	return true;
}

// node id, channel id ( 0:R, 1:G, 2:B, 3:A), swizzle index ( 0:R, 1:G, 2:B, 3:A)
bool SimplygonMax::SetSwizzleChannel( int nodeId, int channel, int swizzleFromIndex )
{
	if( channel < 0 && channel > 3 )
		LogMessageToScriptEditor( std::basic_string<TCHAR>( _T("Valid Channels are 0:R, 1:G, 2:B, 3:A") ) );

	if( swizzleFromIndex < 0 && swizzleFromIndex > 3 )
		LogMessageToScriptEditor( std::basic_string<TCHAR>( _T("Valid Swizzle indices are 0:R, 1:G, 2:B, 3:A") ) );

	this->nodeTable[ nodeId ]->SetChannelSwizzle( channel, swizzleFromIndex );
	return true;
}

// node id, goemetry field name
bool SimplygonMax::SetGeometryFieldName( int nodeId, std::basic_string<TCHAR> tGeometryFieldName )
{
	if( tGeometryFieldName.empty() )
	{
		LogMessageToScriptEditor( std::basic_string<TCHAR>( _T("SetGeometryFieldName: geometryFieldName is empty.") ) );
		return false;
	}

	this->nodeTable[ nodeId ]->SetGeometryFieldName( tGeometryFieldName );
	return true;
}

// node id, goemetry field index
bool SimplygonMax::SetGeometryFieldIndex( int nodeId, int geometryFieldIndex )
{
	if( geometryFieldIndex < 0 )
	{
		LogMessageToScriptEditor( std::basic_string<TCHAR>( _T("SetGeometryFieldIndex: geometryFieldIndex is invalid.") ) );
		return false;
	}

	this->nodeTable[ nodeId ]->SetGeometryFieldIndex( geometryFieldIndex );
	return true;
}

// node id, goemetry field type
bool SimplygonMax::SetGeometryFieldType( int nodeId, int geometryFieldType )
{
	if( geometryFieldType < 0 )
	{
		LogMessageToScriptEditor( std::basic_string<TCHAR>( _T("SetGeometryFieldType: geometryFieldType is invalid.") ) );
		return false;
	}

	this->nodeTable[ nodeId ]->SetGeometryFieldType( geometryFieldType );
	return true;
}

// create texture node and return id
#define sg_createnode_implement( varname )                             \
	int SimplygonMax::Create##varname( std::basic_string<TCHAR> name ) \
	{                                                                  \
		NodeProxy* node = new NodeProxy( name, varname );              \
		this->nodeTable.push_back( node );                             \
		return (int)( this->nodeTable.size() - 1 );                    \
	}

sg_createnode_implement( ShadingTextureNode );
sg_createnode_implement( ShadingInterpolateNode );
sg_createnode_implement( ShadingVertexColorNode );
sg_createnode_implement( ShadingClampNode );
sg_createnode_implement( ShadingAddNode );
sg_createnode_implement( ShadingSubtractNode );
sg_createnode_implement( ShadingDivideNode );
sg_createnode_implement( ShadingMultiplyNode );
sg_createnode_implement( ShadingColorNode );
sg_createnode_implement( ShadingSwizzlingNode );
sg_createnode_implement( ShadingLayeredBlendNode );
sg_createnode_implement( ShadingPowNode );
sg_createnode_implement( ShadingStepNode );
sg_createnode_implement( ShadingNormalize3Node );
sg_createnode_implement( ShadingSqrtNode );
sg_createnode_implement( ShadingDot3Node );
sg_createnode_implement( ShadingCross3Node );
sg_createnode_implement( ShadingCosNode );
sg_createnode_implement( ShadingSinNode );
sg_createnode_implement( ShadingMaxNode );
sg_createnode_implement( ShadingMinNode );
sg_createnode_implement( ShadingEqualNode );
sg_createnode_implement( ShadingNotEqualNode );
sg_createnode_implement( ShadingGreaterThanNode );
sg_createnode_implement( ShadingLessThanNode );
sg_createnode_implement( ShadingGeometryFieldNode );

// connects a shading node to the given material channel
bool SimplygonMax::ConnectRootNodeToChannel( int nodeId, int materialIndex, std::basic_string<TCHAR> tChannelName )
{
	this->materialProxyTable[ materialIndex ]->ShadingNodeToSGChannel[ tChannelName ] = nodeId;
	return true;
}

// sets a shading node's default parameter
bool SimplygonMax::SetDefaultParameter( int nodeId, int parameterId, float r, float g, float b, float a )
{
	fColor* defaultParam = new fColor( r, g, b, a );
	this->nodeTable[ nodeId ]->Parameters[ parameterId ] = defaultParam;
	this->nodeTable[ nodeId ]->UseDefaultParameterInput[ parameterId ] = true;
	return true;
}

// Adds an attribute to the given texture node
// Note: these attributes will be preserved from the original shader into the node proxy
bool SimplygonMax::AddNodeAttribute( int nodeId, std::basic_string<TCHAR> tAttributeName, int attributeType )
{
	AttributeData* attribute = new AttributeData( nodeId, attributeType );
	this->nodeTable[ nodeId ]->Attributes[ tAttributeName ] = attribute;
	return true;
}

// overrides the mapping channel for the given shading texture node
bool SimplygonMax::SetUV( int nodeId, int mappingChannel )
{
	this->nodeTable[ nodeId ]->UVOverride = mappingChannel;
	return true;
}

// overrides color-space type for the given shading texture node
bool SimplygonMax::SetSRGB( int nodeId, bool isSRGB )
{
	this->nodeTable[ nodeId ]->isSRGB = isSRGB;
	this->nodeTable[ nodeId ]->isSRGBOverride = true;
	return true;
}

// overrides tangent space for the given material
bool SimplygonMax::SetUseTangentSpaceNormals( std::basic_string<TCHAR> tMaterialName, bool bTangentSpace )
{
	for( std::vector<ShadingNetworkProxy*>::iterator& proxyIterator = this->materialProxyTable.begin(); proxyIterator != this->materialProxyTable.end();
	     proxyIterator++ )
	{
		if( CompareStrings( ( *proxyIterator )->GetName(), tMaterialName ) )
		{
			( *proxyIterator )->SetUseTangentSpaceNormals( bTangentSpace );
			return true;
		}
	}

	return false;
}

// overrides the UV-tiling for the given shading texture node
bool SimplygonMax::SetUVTiling( int nodeId, float u, float v )
{
	this->SetUTiling( nodeId, u );
	this->SetVTiling( nodeId, v );
	return true;
}

// overrides the U-tiling for the given shading texture node
bool SimplygonMax::SetUTiling( int nodeId, float u )
{
	this->nodeTable[ nodeId ]->uTiling = u;
	this->nodeTable[ nodeId ]->uTilingOverride = true;
	return true;
}

// overrides the V-tiling for the given shading texture node
bool SimplygonMax::SetVTiling( int nodeId, float v )
{
	this->nodeTable[ nodeId ]->vTiling = v;
	this->nodeTable[ nodeId ]->vTilingOverride = true;
	return true;
}

// overrides the UV-offset for the given shading texture node
bool SimplygonMax::SetUVOffset( int nodeId, float u, float v )
{
	this->SetUOffset( nodeId, u );
	this->SetVOffset( nodeId, v );
	return true;
}

// overrides the U-offset for the given shading texture node
bool SimplygonMax::SetUOffset( int nodeId, float u )
{
	this->nodeTable[ nodeId ]->uOffset = u;
	this->nodeTable[ nodeId ]->uOffsetOverride = true;
	return true;
}

// overrides the V-offset for the given shading texture node
bool SimplygonMax::SetVOffset( int nodeId, float v )
{
	this->nodeTable[ nodeId ]->vOffset = v;
	this->nodeTable[ nodeId ]->vOffsetOverride = true;
	return true;
}

// logs a message to the Max Listener
void SimplygonMax::LogMessageToScriptEditor( std::basic_string<TCHAR> tMessage )
{
	the_listener->edit_stream->puts( tMessage.c_str() );
	the_listener->edit_stream->flush();
}

// validates shading-network
void SimplygonMax::ValidateMaterialShadingNetwork( spMaterial sgMaterial, ShadingNetworkProxy* materialProxy )
{
}

// Assign shading networks to Simplygon material
void SimplygonMax::SetupMaterialWithCustomShadingNetwork( spMaterial sgMaterial, ShadingNetworkProxy* materialProxy )
{
	sgMaterial->SetUseTangentSpaceNormals( materialProxy->GetUseTangentSpaceNormals() );

	const int materialId = GetMaterialId( materialProxy );

	for( std::map<std::basic_string<TCHAR>, int>::iterator tShadingNodeIterator = materialProxy->ShadingNodeToSGChannel.begin();
	     tShadingNodeIterator != materialProxy->ShadingNodeToSGChannel.end();
	     ++tShadingNodeIterator )
	{
		std::basic_string<TCHAR> tChannelName = tShadingNodeIterator->first;
		const char* cChannelName = LPCTSTRToConstCharPtr( tChannelName.c_str() );

		if( !sgMaterial->HasMaterialChannel( cChannelName ) )
		{
			sgMaterial->AddMaterialChannel( cChannelName );
		}

		sgMaterial->SetShadingNetwork( cChannelName, CreateSgNodeNetwork( tShadingNodeIterator->second, materialId ) );
	}
}

// create Simplygon shading-node network
spShadingNode SimplygonMax::CreateSgNodeNetwork( int nodeId, int materialId )
{
	this->nodeTable[ nodeId ]->MaterialId = materialId;

	spShadingNode sgExitNode = this->nodeTable[ nodeId ]->ShadingExitNode;

	spShadingVertexColorNode sgVertexColorNode = spShadingVertexColorNode::SafeCast( sgExitNode );
	if( !sgVertexColorNode.IsNull() )
	{
		sgVertexColorNode->SetVertexColorIndex( this->nodeTable[ nodeId ]->VertexColorChannel );
		char cVertexColorChannel[ MAX_PATH ];
		sprintf( cVertexColorChannel, "%d", this->nodeTable[ nodeId ]->VertexColorChannel );
		sgVertexColorNode->SetVertexColorSet( cVertexColorChannel );
	}

	spShadingSwizzlingNode sgSwizzleNode = spShadingSwizzlingNode::SafeCast( sgExitNode );
	if( !sgSwizzleNode.IsNull() )
	{
		sgSwizzleNode->SetRedComponent( this->nodeTable[ nodeId ]->ChannelSwizzleIndices[ 0 ] );
		sgSwizzleNode->SetGreenComponent( this->nodeTable[ nodeId ]->ChannelSwizzleIndices[ 1 ] );
		sgSwizzleNode->SetBlueComponent( this->nodeTable[ nodeId ]->ChannelSwizzleIndices[ 2 ] );
		sgSwizzleNode->SetAlphaComponent( this->nodeTable[ nodeId ]->ChannelSwizzleIndices[ 3 ] );
	}

	spShadingGeometryFieldNode sgGeometryFieldNode = spShadingGeometryFieldNode::SafeCast( sgExitNode );
	if( !sgGeometryFieldNode.IsNull() )
	{
		std::basic_string<TCHAR> tGeometryFieldName = this->nodeTable[ nodeId ]->GeometryFieldName;
		if( tGeometryFieldName.size() > 0 )
		{
			sgGeometryFieldNode->SetFieldName( LPCTSTRToConstCharPtr( tGeometryFieldName.c_str() ) );
		}

		const int geometryFieldIndex = this->nodeTable[ nodeId ]->GeometryFieldIndex;
		if( !( geometryFieldIndex < 0 ) )
		{
			sgGeometryFieldNode->SetFieldIndex( geometryFieldIndex );
		}

		const int geometryFieldType = this->nodeTable[ nodeId ]->GeometryFieldType;
		if( !( geometryFieldType < 0 ) )
		{
			sgGeometryFieldNode->SetFieldType( geometryFieldType );
		}
	}

	std::vector<int>::iterator childNodeIterator;

	int inputChannel = 0;
	for( childNodeIterator = this->nodeTable[ nodeId ]->ChildNodes.begin(); childNodeIterator != this->nodeTable[ nodeId ]->ChildNodes.end();
	     childNodeIterator++ )
	{
		const int childNodeId = ( *childNodeIterator );

		if( childNodeId < 0 )
		{
			inputChannel++;
			continue;
		}

		spShadingNode sgChildNode = CreateSgNodeNetwork( childNodeId, materialId );

		const spString rNodeName = sgExitNode->GetName();
		const spString rChildNodeName = sgChildNode->GetName();

		if( this->nodeTable[ nodeId ]->NodeType == ShadingMultiplyNode )
		{
			spShadingMultiplyNode sgMultiplyNode = spShadingMultiplyNode::SafeCast( sgExitNode );
			sgMultiplyNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingAddNode )
		{
			spShadingAddNode sgAddNode = spShadingAddNode::SafeCast( sgExitNode );
			sgAddNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingSubtractNode )
		{
			spShadingSubtractNode sgSubtractNode = spShadingSubtractNode::SafeCast( sgExitNode );
			sgSubtractNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingDivideNode )
		{
			spShadingDivideNode sgDivideNode = spShadingDivideNode::SafeCast( sgExitNode );
			sgDivideNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingInterpolateNode )
		{
			spShadingInterpolateNode sgInterpolateNode = spShadingInterpolateNode::SafeCast( sgExitNode );
			sgInterpolateNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingClampNode )
		{
			spShadingClampNode sgClampNode = spShadingClampNode::SafeCast( sgExitNode );
			sgClampNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingSwizzlingNode )
		{
			spShadingSwizzlingNode sgSwizzlingNode = spShadingSwizzlingNode::SafeCast( sgExitNode );
			sgSwizzlingNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingPowNode )
		{
			spShadingPowNode sgPowNode = spShadingPowNode::SafeCast( sgExitNode );
			sgPowNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingStepNode )
		{
			spShadingStepNode sgStepNode = spShadingStepNode::SafeCast( sgExitNode );
			sgStepNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingNormalize3Node )
		{
			spShadingNormalize3Node sgNormalizeNode = spShadingNormalize3Node::SafeCast( sgExitNode );
			sgNormalizeNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingSqrtNode )
		{
			spShadingSqrtNode sgSqrtNode = spShadingSqrtNode::SafeCast( sgExitNode );
			sgSqrtNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingDot3Node )
		{
			spShadingDot3Node sgDotNode = spShadingDot3Node::SafeCast( sgExitNode );
			sgDotNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingCross3Node )
		{
			spShadingCross3Node sgCrossNode = spShadingCross3Node::SafeCast( sgExitNode );
			sgCrossNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingCosNode )
		{
			spShadingCosNode sgCosNode = spShadingCosNode::SafeCast( sgExitNode );
			sgCosNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingSinNode )
		{
			spShadingSinNode sgSinNode = spShadingSinNode::SafeCast( sgExitNode );
			sgSinNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingMaxNode )
		{
			spShadingMaxNode sgMaxNode = spShadingMaxNode::SafeCast( sgExitNode );
			sgMaxNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingMinNode )
		{
			spShadingMinNode sgMinNode = spShadingMinNode::SafeCast( sgExitNode );
			sgMinNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingEqualNode )
		{
			spShadingEqualNode sgEqualNode = spShadingEqualNode::SafeCast( sgExitNode );
			sgEqualNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingNotEqualNode )
		{
			spShadingNotEqualNode sgNotEqualNode = spShadingNotEqualNode::SafeCast( sgExitNode );
			sgNotEqualNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingGreaterThanNode )
		{
			spShadingGreaterThanNode sgGreaterThanNode = spShadingGreaterThanNode::SafeCast( sgExitNode );
			sgGreaterThanNode->SetInput( inputChannel, sgChildNode );
		}
		else if( this->nodeTable[ nodeId ]->NodeType == ShadingLessThanNode )
		{
			spShadingLessThanNode sgLessThanNode = spShadingLessThanNode::SafeCast( sgExitNode );
			sgLessThanNode->SetInput( inputChannel, sgChildNode );
		}
		else
		{
			std::basic_string<TCHAR> tMessage( _T("Set Input command is not supported for this node type: ") );
			tMessage += ConstCharPtrToLPCTSTR( sgExitNode->GetClass() );
			tMessage += _T("\n");

			this->LogMessageToScriptEditor( tMessage );
		}

		inputChannel++;
	}

	return spShadingNode( sgExitNode );
}

// create shading-network proxy
int SimplygonMax::CreateProxyShadingNetworkMaterial( std::basic_string<TCHAR> tMaterialName, MaxMaterialType materialType )
{
	for( std::vector<ShadingNetworkProxy*>::const_iterator& proxyIterator = this->materialProxyTable.begin(); proxyIterator != this->materialProxyTable.end();
	     proxyIterator++ )
	{
		if( CompareStrings( ( *proxyIterator )->GetName(), tMaterialName ) )
		{
			return -1;
		}
	}

	ShadingNetworkProxy* proxyMaterial = new ShadingNetworkProxy( tMaterialName, materialType );
	this->materialProxyTable.push_back( proxyMaterial );

	return int( this->materialProxyTable.size() - 1 );
}

// create shading-network proxy
int SimplygonMax::CreateProxyShadingNetworkWritebackMaterial( std::basic_string<TCHAR> tEffectFilePath, MaxMaterialType materialType )
{
	for( std::vector<ShadingNetworkProxyWriteBack*>::const_iterator& proxyIterator = this->materialProxyWritebackTable.begin();
	     proxyIterator != this->materialProxyWritebackTable.end();
	     proxyIterator++ )
	{
		if( CompareStrings( ( *proxyIterator )->GetEffectFilePath(), tEffectFilePath ) )
		{
			return -1;
		}
	}

	ShadingNetworkProxyWriteBack* proxyMaterial = new ShadingNetworkProxyWriteBack( tEffectFilePath );

	this->materialProxyWritebackTable.push_back( proxyMaterial );

	return int( this->materialProxyWritebackTable.size() - 1 );
}

// gets material proxy id
int SimplygonMax::GetMaterialId( ShadingNetworkProxy* materialProxy )
{
	int proxyIndex = 0;
	for( std::vector<ShadingNetworkProxy*>::const_iterator& proxyIterator = materialProxyTable.begin(); proxyIterator != materialProxyTable.end();
	     proxyIterator++ )
	{
		if( CompareStrings( ( *proxyIterator )->GetName(), materialProxy->GetName() ) )
		{
			return proxyIndex;
		}

		proxyIndex++;
	}

	return -1;
}

// gets shading-network proxy by name
ShadingNetworkProxy* SimplygonMax::GetProxyShadingNetworkMaterial( std::basic_string<TCHAR> tMaterialName )
{
	for( std::vector<ShadingNetworkProxy*>::const_iterator& proxyIterator = materialProxyTable.begin(); proxyIterator != materialProxyTable.end();
	     proxyIterator++ )
	{
		if( CompareStrings( ( *proxyIterator )->GetName(), tMaterialName ) )
		{
			return *proxyIterator;
		}
	}

	return nullptr;
}

ShadingNetworkProxyWriteBack* SimplygonMax::GetProxyShadingNetworkWritebackMaterial()
{
	for( std::vector<ShadingNetworkProxyWriteBack*>::const_iterator& proxyIterator = materialProxyWritebackTable.begin();
	     proxyIterator != materialProxyWritebackTable.end();
	     proxyIterator++ )
	{
		if( ( *proxyIterator ) )
		{
			return *proxyIterator;
		}
	}

	return nullptr;
}

// creates and returns a shading node with the given type
spShadingNode SimplygonMax::CreateSGMaterialNode( NodeProxyType nodeType )
{
	if( nodeType == ShadingTextureNode )
		return spShadingNode( sg->CreateShadingTextureNode() );
	else if( nodeType == ShadingAddNode )
		return spShadingNode( sg->CreateShadingAddNode() );
	else if( nodeType == ShadingSubtractNode )
		return spShadingNode( sg->CreateShadingSubtractNode() );
	else if( nodeType == ShadingMultiplyNode )
		return spShadingNode( sg->CreateShadingMultiplyNode() );
	else if( nodeType == ShadingDivideNode )
		return spShadingNode( sg->CreateShadingDivideNode() );
	else if( nodeType == ShadingClampNode )
		return spShadingNode( sg->CreateShadingClampNode() );
	else if( nodeType == ShadingInterpolateNode )
		return spShadingNode( sg->CreateShadingInterpolateNode() );
	else if( nodeType == ShadingColorNode )
		return spShadingNode( sg->CreateShadingColorNode() );
	else if( nodeType == ShadingVertexColorNode )
		return spShadingNode( sg->CreateShadingVertexColorNode() );
	else if( nodeType == ShadingSwizzlingNode )
		return spShadingNode( sg->CreateShadingSwizzlingNode() );
	else if( nodeType == ShadingLayeredBlendNode )
		return spShadingNode( sg->CreateShadingLayeredBlendNode() );
	else if( nodeType == ShadingPowNode )
		return spShadingNode( sg->CreateShadingPowNode() );
	else if( nodeType == ShadingStepNode )
		return spShadingNode( sg->CreateShadingStepNode() );
	else if( nodeType == ShadingNormalize3Node )
		return spShadingNode( sg->CreateShadingNormalize3Node() );
	else if( nodeType == ShadingSqrtNode )
		return spShadingNode( sg->CreateShadingSqrtNode() );
	else if( nodeType == ShadingDot3Node )
		return spShadingNode( sg->CreateShadingDot3Node() );
	else if( nodeType == ShadingCross3Node )
		return spShadingNode( sg->CreateShadingCross3Node() );
	else if( nodeType == ShadingCosNode )
		return spShadingNode( sg->CreateShadingCosNode() );
	else if( nodeType == ShadingSinNode )
		return spShadingNode( sg->CreateShadingSinNode() );
	else if( nodeType == ShadingMaxNode )
		return spShadingNode( sg->CreateShadingMaxNode() );
	else if( nodeType == ShadingMinNode )
		return spShadingNode( sg->CreateShadingMinNode() );
	else if( nodeType == ShadingEqualNode )
		return spShadingNode( sg->CreateShadingEqualNode() );
	else if( nodeType == ShadingNotEqualNode )
		return spShadingNode( sg->CreateShadingNotEqualNode() );
	else if( nodeType == ShadingGreaterThanNode )
		return spShadingNode( sg->CreateShadingGreaterThanNode() );
	else if( nodeType == ShadingLessThanNode )
		return spShadingNode( sg->CreateShadingLessThanNode() );
	else if( nodeType == ShadingGeometryFieldNode )
		return spShadingNode( sg->CreateShadingGeometryFieldNode() );

	return Simplygon::NullPtr;
}

// gets shading texture nodes from table
void SimplygonMax::GetSpShadingNodesFromTable( NodeProxyType nodeType,
                                               std::basic_string<TCHAR> tChannel,
                                               ShadingNetworkProxy* materialProxy,
                                               std::map<int, NodeProxy*>& nodeProxies )
{
	std::map<std::basic_string<TCHAR>, int>::const_iterator& channelIterator = materialProxy->ShadingNodeToSGChannel.find( tChannel );
	if( channelIterator != materialProxy->ShadingNodeToSGChannel.end() )
	{
		const int nodeIndex = channelIterator->second;
		if( this->nodeTable.size() > nodeIndex )
		{
			NodeProxy* nodeProxy = this->nodeTable.at( nodeIndex );

			if( nodeProxy )
			{
				this->GetNodeProxyFromTable( nodeIndex, nodeType, nodeProxy, nodeProxies );
			}
		}
	}
}

void SimplygonMax::GetNodeProxyFromTable( int nodeIndex, NodeProxyType nodeType, NodeProxy* nodeProxy, std::map<int, NodeProxy*>& nodeProxies )
{
	if( nodeProxy->NodeType == nodeType )
	{
		nodeProxies.insert( std::pair<int, NodeProxy*>( nodeIndex, nodeProxy ) );
	}
	for( int i : nodeProxy->ChildNodes )
	{
		if( i >= 0 )
		{
			NodeProxy* cNodeProxy = this->nodeTable.at( i );
			this->GetNodeProxyFromTable( i, nodeType, cNodeProxy, nodeProxies );
		}
	}
}

// gets shading-node by name from table
spShadingNode SimplygonMax::GetSpShadingNodeFromTable( std::basic_string<TCHAR> tNodeName, ShadingNetworkProxy* materialProxy )
{
	const int materialId = this->GetMaterialId( materialProxy );

	for( std::vector<NodeProxy*>::const_iterator& proxyIterator = this->nodeTable.begin(); proxyIterator != this->nodeTable.end(); proxyIterator++ )
	{
		if( CompareStrings( ( *proxyIterator )->NodeName, tNodeName ) == true && ( *proxyIterator )->MaterialId == materialId )
		{
			return ( *proxyIterator )->ShadingExitNode;
		}
	}

	return Simplygon::NullPtr;
}

// gets node-proxy from table, by name
NodeProxy* SimplygonMax::GetNodeFromTable( std::basic_string<TCHAR> tNodeName, ShadingNetworkProxy* materialProxy, int* outIndex )
{
	const int materialId = GetMaterialId( materialProxy );
	for( uint i = 0; i < this->nodeTable.size(); ++i )
	{
		NodeProxy* nodeProxy = this->nodeTable[ i ];
		if( !nodeProxy )
			continue;

		if( CompareStrings( nodeProxy->NodeName, tNodeName ) == true && nodeProxy->MaterialId == materialId )
		{
			if( outIndex )
			{
				*outIndex = i;
			}
			return nodeProxy;
		}
	}

	return nullptr;
}

// Gets node-attribute by name
AttributeData* SimplygonMax::GetNodeAttribute( std::basic_string<TCHAR> tAttributeName, ShadingNetworkProxy* materialProxy )
{
	const int materialId = GetMaterialId( materialProxy );

	for( std::vector<NodeProxy*>::const_iterator& proxyiterator = this->nodeTable.begin(); proxyiterator != this->nodeTable.end(); proxyiterator++ )
	{
		if( ( *proxyiterator )->Attributes.find( tAttributeName ) != ( *proxyiterator )->Attributes.end() && ( *proxyiterator )->MaterialId == materialId )
		{
			return ( *proxyiterator )->Attributes[ tAttributeName ];
		}
	}

	return nullptr;
}

// connect Simplygon channel to material node
bool SimplygonMax::ConnectSgChannelToMaterialNode( std::basic_string<TCHAR> tSGChannelName, std::basic_string<TCHAR> tMaxMaterialParamName )
{
	// look for if channel is already mapped as not to override
	if( this->sgChannelToMaxMatParam.find( tSGChannelName ) == this->sgChannelToMaxMatParam.end() )
	{
		this->sgChannelToMaxMatParam[ tSGChannelName ] = tMaxMaterialParamName;
		return true;
	}

	return false;
}

// assigns texture information to Simplygon texture node
void SimplygonMax::SetupSGDXTexture( IDxMaterial3* mMaxDXMaterial, int bitmapIndex, spShadingTextureNode sgTextureNode )
{
	const PBBitmap* mPBBitmap = mMaxDXMaterial->GetEffectBitmap( bitmapIndex );
	const int numEffectFiles = mMaxDXMaterial->GetNumberOfEffectBitmaps();

	if( mPBBitmap )
	{
		BitmapInfo mBitmapInfo = mPBBitmap->bi;

		const IDxMaterial2::BitmapTypes mDXBitmapType = mMaxDXMaterial->GetBitmapUsage( bitmapIndex );
		const int mappingChannel = mMaxDXMaterial->GetBitmapMappingChannel( bitmapIndex );

		TCHAR tFileDest[ MAX_PATH ] = { 0 };
		const TCHAR* tFilePath = mBitmapInfo.Name();
		const TCHAR* tFileName = mBitmapInfo.Filename();

		// retrieve the file path
		GetImageFullFilePath( tFilePath, tFileDest );

		if( _tcslen( tFilePath ) > 0 )
		{
			std::basic_string<TCHAR> tTexturePathWithName = this->ImportTexture( std::basic_string<TCHAR>( tFilePath ) );
			std::basic_string<TCHAR> tTextureName = GetTitleOfFile( tTexturePathWithName );
			std::basic_string<TCHAR> tTextureExtension = GetExtensionOfFile( tTexturePathWithName );
			std::basic_string<TCHAR> tTextureNameWithExtension = tTextureName + tTextureExtension;

			bool bIsSRGB = false;

			const float gamma = mBitmapInfo.Gamma();
			if( gamma < 2.3f && gamma > 2.1f )
			{
				bIsSRGB = true;
			}

			sgTextureNode->SetTextureName( LPCTSTRToConstCharPtr( tTextureName.c_str() ) );

			char cMappingChannel[ MAX_PATH ] = { 0 };
			itoa( 1, cMappingChannel, 10 );
			if( !sgTextureNode->GetTexCoordName().IsNullOrEmpty() && !sgTextureNode->GetTexCoordName().c_str() == NULL )
			{
				const spString uvSet = sgTextureNode->GetTexCoordName();
			}
			else
			{
				sgTextureNode->SetTexCoordName( cMappingChannel );
			}
		}
	}
}

#pragma region ShaderParamBlocks

void ScanParamBlocks( IParamBlock2* pb )
{
	if( !pb )
		return;

	const int num_param = pb->NumParams();
	for( int j = 0; j < num_param; ++j )
	{
		const ParamID id = (ParamID)pb->IDtoIndex( (ParamID)j );

		if( id != -1 )
		{
			const ParamDef& param_def = pb->GetParamDef( id );

			size_t pos = std::basic_string<TCHAR>( param_def.int_name ).find( _T("mapChannel") );
			if( pos != std::basic_string<TCHAR>::npos )
			{
				PB2Value& pb2_val = pb->GetPB2Value( id, 0 );
				int mapChannel = pb2_val.i;
				continue;
			}
		}
	}
}

void SetParameterValue( PB2Value& object, bool value )
{
	object.i = value;
}
void SetParameterValue( PB2Value& object, int value )
{
	object.i = value;
}
void SetParameterValue( PB2Value& object, float value )
{
	object.f = value;
}
void SetParameterValue( PB2Value& object, float value[ 4 ] )
{
	if( object.p4 )
	{
		object.p4->x = value[ 0 ];
		object.p4->y = value[ 1 ];
		object.p4->z = value[ 2 ];
		object.p4->w = value[ 3 ];
	}
	// TODO: When we rewrite this code, add error handling for set paramenter value object.p4.
	// We assume that p4 is allocated but if it is not we will ignore updating the values since we cant new points.
}

void SetParameterValue( PB2Value& object, std::basic_string<TCHAR> value )
{
	object.s = value.c_str();
}

void SetParameterValue( PB2Value& object, PBBitmap* value )
{
	object.bm = value;
}

template <class T> void SetShaderParameter( Mtl* mMaxMaterial, std::basic_string<TCHAR> tShaderParameterName, T value )
{
	const int numReferences = mMaxMaterial->NumRefs();
	for( int i = 0; i < numReferences; i++ )
	{
		ReferenceTarget* referenceTarget = mMaxMaterial->GetReference( i );

		if( !referenceTarget )
		{
			continue;
		}

		MSTR mClassName;
		referenceTarget->GetClassName( mClassName );
		if( mClassName == L"ParamBlock2" )
		{
			IParamBlock2* mParamBlock = dynamic_cast<IParamBlock2*>( referenceTarget );
			if( !mParamBlock )
				continue;

			const int numParams = mParamBlock->NumParams();
			for( int j = 0; j < numParams; ++j )
			{
				const ParamID mParamId = mParamBlock->IndextoID( j );
				const ParamDef& mParamDef = mParamBlock->GetParamDef( mParamId );

				if( mParamDef.int_name == nullptr )
					continue;

				bool bIsEqual = ( std::basic_string<TCHAR>( mParamDef.int_name ).compare( tShaderParameterName ) == 0 );
				if( bIsEqual )
				{
					SetParameterValue( mParamBlock->GetPB2Value( mParamId, 0 ), value );
					break;
				}
			}
		}
	}
}
#pragma endregion

// assigns texture information to Max texture node
void SimplygonMax::SetupMaxDXTexture( spScene sgProcessedScene,
                                      spMaterial sgMaterial,
                                      const char* cChannelName,
                                      Mtl* mMaxmaterial,
                                      IDxMaterial3* mMaxDXMaterial,
                                      std::basic_string<TCHAR> tTextureParameterName,
                                      spShadingTextureNode sgTextureNode,
                                      std::basic_string<TCHAR> tNodeName,
                                      std::basic_string<TCHAR> tMeshNameOverride,
                                      std::basic_string<TCHAR> tMaterialNameOverride )
{
	IPathConfigMgr* mPathManager = IPathConfigMgr::GetPathConfigMgr();
	const std::basic_string<TCHAR> tMaxBitmapDirectory = mPathManager->GetDir( APP_IMAGE_DIR );

	spTextureTable sgTextureTable = sgProcessedScene->GetTextureTable();
	spTexture sgTexture = Simplygon::NullPtr;

	// if channel exists
	if( sgMaterial->HasMaterialChannel( cChannelName ) )
	{
		const spString rTextureName = sgTextureNode->GetTextureName();
		const char* cTextureName = rTextureName.c_str();
		sgTexture = sgTextureTable->FindTexture( cTextureName );
	}

	// Assuming texture has a name
	if( !sgTexture.IsNull() )
	{
		std::basic_string<TCHAR> tTextureName = ConstCharPtrToLPCTSTR( sgTexture->GetName() );
		std::basic_string<TCHAR> tTexturePath = ConstCharPtrToLPCTSTR( sgTexture->GetFilePath() );
		std::basic_string<TCHAR> tChannelName = ConstCharPtrToLPCTSTR( cChannelName );
		std::basic_string<TCHAR> tTargetFilePath = Combine( tMaxBitmapDirectory, tTexturePath );

		if( !sgTexture->GetImageData().IsNull() )
		{
			// Embedded data, should be written to file
			if( ExportTextureToFile( sg, sgTexture, LPCTSTRToConstCharPtr( tTexturePath.c_str() ) ) )
			{
				tTexturePath = ConstCharPtrToLPCTSTR( sgTexture->GetFilePath().c_str() );
				sgTexture->SetImageData( Simplygon::NullPtr );
			}
		}

		if( this->copyTextures )
		{
			// the name of the imported texture is based on the name of the node
			std::basic_string<TCHAR> tImportTextureName = tTextureName + GetExtensionOfFile( tTexturePath );
			ReplaceInvalidCharacters( tImportTextureName, _T( '_' ) );
			std::basic_string<TCHAR> tImportTexturePath = Combine( tMaxBitmapDirectory, tImportTextureName );

			if( this->TextureOutputDirectory.length() > 0 )
			{
				const bool bFolderCreated = CreateFolder( LPCTSTRToConstCharPtr( this->TextureOutputDirectory.c_str() ) );
				if( !bFolderCreated )
				{
					tTargetFilePath = Combine( tMaxBitmapDirectory, tImportTextureName );
				}
				else
				{
					tTargetFilePath = Combine( this->TextureOutputDirectory, tImportTextureName );
				}
			}
			else
			{
				tTargetFilePath = Combine( std::basic_string<TCHAR>( tMaxBitmapDirectory ), tImportTextureName );
			}

			// make sure to get a unique name
			if( this->UseNonConflictingTextureNames )
			{
				tTargetFilePath = GetNonConflictingNameInPath( tTargetFilePath.c_str() );
			}

			uint numCopyRetries = 0;

			// copy the file
			while( CopyFile( tTexturePath.c_str(), tTargetFilePath.c_str(), FALSE ) == FALSE )
			{
				const DWORD dwErrorCode = GetLastError();

				// if in use (mostly caused by multiple instances racing)
				// then try again
				if( dwErrorCode == ERROR_SHARING_VIOLATION && numCopyRetries < MaxNumCopyRetries )
				{
					Sleep( 100 );
					numCopyRetries++;
					continue;
				}

				TCHAR tErrorCode[ 64 ] = { 0 };
				_stprintf_s( tErrorCode, 64, _T("%u"), dwErrorCode );

				std::basic_string<TCHAR> tErrorMessage = _T("Error - could not copy texture:\n ");
				tErrorMessage += tTexturePath;
				tErrorMessage += _T("\n ");
				tErrorMessage += tTargetFilePath;
				tErrorMessage += _T("\n Code: ");
				tErrorMessage += tErrorCode;
				tErrorMessage += _T("\n");

				this->LogMessageToScriptEditor( tErrorMessage );

				return;
			}
		}

		const spString rTexCoordName = sgTextureNode->GetTexCoordName();
		const char* cTexCoordName = rTexCoordName.c_str();

		int maxChannel = 1;
		std::map<std::string, int>::const_iterator& texCoordMap = this->ImportedUvNameToMaxIndex.find( cTexCoordName );
		if( texCoordMap != this->ImportedUvNameToMaxIndex.end() )
		{
			maxChannel = texCoordMap->second;
		}

		this->materialInfoHandler->Add( tMeshNameOverride, tMaterialNameOverride, tChannelName, tTargetFilePath, maxChannel );

		BitmapInfo mBitmapInfo;
		mBitmapInfo.SetName( tTargetFilePath.c_str() );

		PBBitmap* mPBBitmap = new PBBitmap( mBitmapInfo );
		mPBBitmap->Load();

		IParameterManager* mParameterManager = mMaxDXMaterial->GetCurrentParameterManager();

		for( int paramIndex = 0; paramIndex < mParameterManager->GetNumberOfParams(); ++paramIndex )
		{
			std::basic_string<TCHAR> shaderParameterName = mParameterManager->GetParamName( paramIndex );
			int shaderParameterType = mParameterManager->GetParamType( shaderParameterName.c_str() );

			const bool bIsTexture = shaderParameterType == IParameterManager::kPType_Texture || shaderParameterType == 1010 || shaderParameterType == 1009;
			if( bIsTexture && tTextureParameterName == shaderParameterName )
			{
				// set the bitmap to the specified effect channel
				// mMaxDXMaterial->SetEffectBitmap( paramIndex, mPBBitmap );
				SetShaderParameter( mMaxmaterial, tTextureParameterName, mPBBitmap );

				// resolve mapping channel parameter name
				std::basic_string<TCHAR> tTextureMapChannelParameterName = tTextureParameterName + _T("mapChannel");

				// and try to set the map channel for the specific texture in the shader
				SetShaderParameter( mMaxmaterial, tTextureMapChannelParameterName, maxChannel );
				break;
			}
		}
	}
	else
	{
		this->LogMessageToScriptEditor( std::basic_string<TCHAR>( _T("No output texture found to setup.") ) );
	}
}

void SimplygonMax::ClearShadingNetworkInfo( bool bReset )
{
	if( this->ShadingNetworkClearInfo.GetClearFlag() )
	{
		if( CompareStrings( this->ShadingNetworkClearInfo.GetPartToClear(), CLEAR_MAT_PIPELINE[ 0 ] ) )
		{
			if( this->nodeTable.size() > 0 )
			{
				this->nodeTable.clear();
			}
			if( this->sgChannelToMaxMatParam.size() > 0 )
			{
				this->sgChannelToMaxMatParam.clear();
			}
			if( this->materialProxyTable.size() > 0 )
			{
				this->materialProxyTable.clear();
			}
		}
		else if( CompareStrings( this->ShadingNetworkClearInfo.GetPartToClear(), CLEAR_MAT_PIPELINE[ 1 ] ) )
		{
			if( this->nodeTable.size() > 0 )
			{
				this->nodeTable.clear();
			}
		}
		else if( CompareStrings( this->ShadingNetworkClearInfo.GetPartToClear(), CLEAR_MAT_PIPELINE[ 2 ] ) )
		{
			if( this->sgChannelToMaxMatParam.size() > 0 )
			{
				this->sgChannelToMaxMatParam.clear();
			}
		}
		else if( CompareStrings( this->ShadingNetworkClearInfo.GetPartToClear(), CLEAR_MAT_PIPELINE[ 3 ] ) )
		{
		}
	}

	if( bReset )
	{
		if( this->nodeTable.size() > 0 )
		{
			this->nodeTable.clear();
		}
		if( this->sgChannelToMaxMatParam.size() > 0 )
		{
			this->sgChannelToMaxMatParam.clear();
		}
		if( this->materialProxyTable.size() > 0 )
		{
			this->materialProxyTable.clear();
		}
	}
}

void SimplygonMax::SetShadingNetworkClearInfo( bool bSet, int flagIndex )
{
	this->ShadingNetworkClearInfo.SetClearFlag( bSet );
	this->ShadingNetworkClearInfo.SetPartToClear( flagIndex );
}

// gets all shading texture nodes in the specified shading-network
void FindAllUpStreamTextureNodes( spShadingNode sgShadingNode, std::map<std::basic_string<TCHAR>, spShadingTextureNode>& sgTextureNodeList )
{
	std::vector<spShadingTextureNode> sgTextureNodes;

	if( sgShadingNode.IsNull() )
		return;

	spShadingTextureNode sgTextureNode = spShadingTextureNode::SafeCast( sgShadingNode );
	if( !sgTextureNode.IsNull() )
	{
		std::basic_string<TCHAR> tNodeName;
		if( !sgTextureNode->GetName().IsNullOrEmpty() )
		{
			tNodeName = std::basic_string<TCHAR>( ConstCharPtrToLPCTSTR( sgTextureNode->GetName().c_str() ) );
		}
		else
		{
			const int numTextures = (int)sgTextureNodeList.size();

			std::basic_ostringstream<TCHAR, std::char_traits<TCHAR>> tNodeNameStream;
			tNodeNameStream << std::basic_string<TCHAR>( _T("TextureNode%d") ) << numTextures;

			tNodeName = tNodeNameStream.str();
		}

		sgTextureNodeList[ tNodeName ] = sgTextureNode;
		return;
	}

	if( !sgShadingNode.IsNull() )
	{
		spShadingFilterNode sgFilterNode = Simplygon::spShadingFilterNode::SafeCast( sgShadingNode );

		if( !sgFilterNode.IsNull() )
		{
			for( uint i = 0; i < sgFilterNode->GetParameterCount(); ++i )
			{
				if( sgFilterNode->GetParameterIsInputable( i ) )
				{
					if( !sgFilterNode->GetInput( i ).IsNull() )
					{
						FindAllUpStreamTextureNodes( sgFilterNode->GetInput( i ), sgTextureNodeList );
					}
				}
			}
		}
	}
}

void GlobalLogMaterialNodeMessage( Texmap* mTexMap,
                                   std::basic_string<TCHAR> tMaterialName,
                                   std::basic_string<TCHAR> tChannelName,
                                   const bool partialFail,
                                   std::basic_string<TCHAR> tExtendedInformation )
{
	if( SimplygonMaxInstance )
	{
		SimplygonMaxInstance->LogMaterialNodeMessage( mTexMap, tMaterialName, tChannelName, partialFail, tExtendedInformation );
	}
}

float GetSceneMeshesRadius( Simplygon::spScene sgScene )
{
	float result = 0.f;
	const rid ssId = sgScene->SelectNodes( "SceneMesh" );
	spExtents extents = sg->CreateExtents();

	if( sgScene->CalculateExtentsOfSelectionSetId( extents, ssId ) )
		result = extents->GetBoundingSphereRadius();

	sgScene->GetSelectionSetTable()->RemoveSelectionSet( ssId );

	return result;
}

// gets upstream shading texture node in the specified shading-network
spShadingTextureNode SimplygonMax::FindUpstreamTextureNode( spShadingNode sgShadingNode )
{
	if( sgShadingNode.IsNull() )
		return Simplygon::NullPtr;

	spShadingTextureNode sgTextureNode = Simplygon::spShadingTextureNode::SafeCast( sgShadingNode );
	if( !sgTextureNode.IsNull() )
	{
		return sgTextureNode;
	}

	spShadingFilterNode sgFilterNode = Simplygon::spShadingFilterNode::SafeCast( sgShadingNode );
	if( !sgFilterNode.IsNull() )
	{
		for( uint i = 0; i < sgFilterNode->GetParameterCount(); ++i )
		{
			if( sgFilterNode->GetParameterIsInputable( i ) )
			{
				if( !sgFilterNode->GetInput( i ).IsNull() )
				{
					spShadingTextureNode sgChildTextureNode = FindUpstreamTextureNode( sgFilterNode->GetInput( i ) );
					if( !sgChildTextureNode.IsNull() )
					{
						return sgChildTextureNode;
					}
				}
			}
		}
	}

	return Simplygon::NullPtr;
}

// gets upstream shading color node in the specified shading-network
spShadingColorNode SimplygonMax::FindUpstreamColorNode( spShadingNode sgShadingNode )
{
	if( sgShadingNode.IsNull() )
		return Simplygon::NullPtr;

	spShadingColorNode sgColorNode = Simplygon::spShadingColorNode::SafeCast( sgShadingNode );
	if( !sgColorNode.IsNull() )
	{
		return sgColorNode;
	}

	spShadingFilterNode sgFilterNode = Simplygon::spShadingFilterNode::SafeCast( sgShadingNode );
	if( !sgFilterNode.IsNull() )
	{
		for( uint i = 0; i < sgFilterNode->GetParameterCount(); ++i )
		{
			if( sgFilterNode->GetParameterIsInputable( i ) )
			{
				if( !sgFilterNode->GetInput( i ).IsNull() )
				{
					spShadingColorNode sgChildColorNode = FindUpstreamColorNode( sgFilterNode->GetInput( i ) );
					if( !sgChildColorNode.IsNull() )
					{
						return sgChildColorNode;
					}
				}
			}
		}
	}

	return Simplygon::NullPtr;
}

// initializes nodes in node table
void SimplygonMax::InitializeNodesInNodeTable()
{
	for( std::vector<NodeProxy*>::iterator proxyIterator = this->nodeTable.begin(); proxyIterator != this->nodeTable.end(); proxyIterator++ )
	{
		if( !( *proxyIterator )->IsInitialized() )
		{
			( *proxyIterator )->SetNode( CreateSGMaterialNode( ( *proxyIterator )->NodeType ) );
		}
	}
}

// creates a Max StdMaterial from a Simplygon material
Mtl* SimplygonMax::SetupMaxStdMaterial( spScene sgProcessedScene, std::basic_string<TCHAR> tMeshName, spMaterial sgMaterial, TSTR tNodeName, TCHAR* tLodName )
{
	bool bResult = true;
	BitmapTex* Textures[ NTEXMAPS ] = { nullptr };

	// create a new max material
	spString tMaterialName = sgMaterial->GetName();
	const char* cMaterialName = tMaterialName.c_str();

	// get unique material name
	std::basic_string<TCHAR> tStandardMaterialName = GetUniqueMaterialName( ConstCharPtrToLPCTSTR( cMaterialName ) );

	StdMat2* mMaxStdMaterial = NewDefaultStdMat();
	mMaxStdMaterial->SetName( tStandardMaterialName.c_str() );
	mMaxStdMaterial->SetMtlFlag( MTL_TEX_DISPLAY_ENABLED | MTL_HW_TEX_ENABLED | MTL_HW_MAT_ENABLED );

	// fetch texture or color information from shading network
	for( uint maxChannelId = 0; maxChannelId < NTEXMAPS; ++maxChannelId )
	{
		const long maxChannel = mMaxStdMaterial->StdIDToChannel( maxChannelId );

		// get standard Max material channel name
#if MAX_VERSION_MAJOR >= 26
		std::basic_string<TCHAR> tChannelName = mMaxStdMaterial->GetSubTexmapSlotName( maxChannel, true );
#else
		std::basic_string<TCHAR> tChannelName = mMaxStdMaterial->GetSubTexmapSlotName( maxChannel );
#endif
		ReplaceInvalidCharacters( tChannelName, _T( '_' ) );

		bResult = this->ImportMaterialTexture(
		    sgProcessedScene, sgMaterial, tNodeName, tChannelName.c_str(), maxChannelId, &Textures[ maxChannelId ], tMeshName, tLodName );
		if( !bResult )
		{
			std::basic_string<TCHAR> tErrorMessage = _T("SetupMaxStdMaterial: Failed to import a texture for ");
			tErrorMessage += tChannelName;
			tErrorMessage += _T(" channel.");

			this->LogToWindow( tErrorMessage, Warning );
		}
	}

	// fetch color and texture information from non-shading network
	mMaxStdMaterial->SetMtlFlag( MTL_TEX_DISPLAY_ENABLED );

	for( uint maxChannelId = 0; maxChannelId < NTEXMAPS; ++maxChannelId )
	{
		const long maxChannel = mMaxStdMaterial->StdIDToChannel( maxChannelId );

		BitmapTex* mBitmapTex = Textures[ maxChannelId ];

		if( mBitmapTex )
		{
			switch( maxChannelId )
			{
				// special case for bump map
				case ID_BU:
				{
					Texmap* mNormalMap = (Texmap*)::CreateInstance( TEXMAP_CLASS_ID, GNORMAL_CLASS_ID );
					if( mNormalMap )
					{
						mMaxStdMaterial->SetSubTexmap( maxChannel, mNormalMap );
						mMaxStdMaterial->EnableMap( maxChannel, TRUE );
						mMaxStdMaterial->SetTexmapAmt( maxChannel, 1, 0 );
						mNormalMap->SetSubTexmap( 0, mBitmapTex );
						this->MaxInterface->ActivateTexture( mBitmapTex, mMaxStdMaterial );
					}
				}
				break;

				// special case for opacity
				case ID_OP:
				{
					mMaxStdMaterial->SetSubTexmap( maxChannel, mBitmapTex );
					mMaxStdMaterial->EnableMap( maxChannel, TRUE );
					mMaxStdMaterial->SetActiveTexmap( mBitmapTex );
					this->MaxInterface->ActivateTexture( mBitmapTex, mMaxStdMaterial );
				}
				break;

				// other channels, use default
				default:
				{
					mMaxStdMaterial->SetSubTexmap( maxChannel, mBitmapTex );
					mMaxStdMaterial->EnableMap( maxChannel, TRUE );
					this->MaxInterface->ActivateTexture( mBitmapTex, mMaxStdMaterial );
				}
				break;

			} // end switch
		}     // end if

	} // end for

	if( /*UseNonConflictingTextureNames == false */ true )
	{
		MaterialInfo materialInfo( tLodName );

#if MAX_VERSION_MAJOR < 23
		materialInfo.MaxMaterialReference = mMaxStdMaterial;
#endif

		const spString rMaterialId = sgMaterial->GetMaterialGUID();
		materialInfo.MaterialId = rMaterialId.c_str();

		this->CachedMaterialInfos.push_back( materialInfo );
	}

	return mMaxStdMaterial;
}

// Specifies which texture path a Max bitmap should use.
PBBitmap* SetupMaxTexture( std::basic_string<TCHAR> tFilePath )
{
	BitmapInfo mBitmapInfo;
	mBitmapInfo.SetName( tFilePath.c_str() );

	PBBitmap* mPBBitmap = new PBBitmap( mBitmapInfo );
	mPBBitmap->Load();

	return mPBBitmap;
}

#if MAX_VERSION_MAJOR >= 23
Mtl* SimplygonMax::SetupPhysicalMaterial( spScene sgProcessedScene, std::basic_string<TCHAR> tMeshName, spMaterial sgMaterial, TSTR tNodeName, TCHAR* tLodName )
{
	const uint numMaterialChannels = sgMaterial->GetMaterialChannelCount();

	// create a new max material
	spString rPhysicalMaterialName = sgMaterial->GetName();
	const char* cPhysicalMaterialName = rPhysicalMaterialName.c_str();

	// get unique material name
	std::basic_string<TCHAR> tPhysicalMaterialName = GetUniqueMaterialName( ConstCharPtrToLPCTSTR( cPhysicalMaterialName ) );

	bool bLegacyPhysical = false;
	Mtl* mMaxPhysicalMaterial = NewPhysicalMaterial( nullptr, &bLegacyPhysical );
	mMaxPhysicalMaterial->SetName( tPhysicalMaterialName.c_str() );
	mMaxPhysicalMaterial->SetMtlFlag( MTL_TEX_DISPLAY_ENABLED | MTL_HW_TEX_ENABLED | MTL_HW_MAT_ENABLED );

	SetShaderParameter( mMaxPhysicalMaterial, _T("emission"), 0.f );
	SetShaderParameter( mMaxPhysicalMaterial, _T("emission_map_on"), false );

	SetShaderParameter( mMaxPhysicalMaterial, _T("emit_color"), Point4( 0.f, 0.f, 0.f, 1.f ) );
	SetShaderParameter( mMaxPhysicalMaterial, _T("emit_color_map_on"), false );

	// new to max 2023
#if MAX_VERSION_MAJOR >= 25
	SetShaderParameter( mMaxPhysicalMaterial, _T("sheen_color"), Point4( 1.f, 1.f, 1.f, 1.f ) );
	SetShaderParameter( mMaxPhysicalMaterial, _T("sheen_color_map_on"), false );
#endif

	for( uint channelIndex = 0; channelIndex < numMaterialChannels; ++channelIndex )
	{
		spString rChannelName = sgMaterial->GetMaterialChannelFromIndex( channelIndex );
		const char* cChannelName = rChannelName.c_str();
		std::basic_string<TCHAR> tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

		BitmapTex* mBitmapTex = nullptr;

		const bool bIsNormalChannel = ( tChannelName == _T("bump") || tChannelName == _T("coat_bump") );
		const bool bIsTransparencyChannel = ( tChannelName == _T("transparency") );

		int materialChannelID = -1;
		if( bIsNormalChannel )
		{
			materialChannelID = ID_BU;
		}
		else if( bIsTransparencyChannel )
		{
			materialChannelID = ID_OP;
		}

		const bool bResult =
		    this->ImportMaterialTexture( sgProcessedScene, sgMaterial, tNodeName, tChannelName.c_str(), materialChannelID, &mBitmapTex, tMeshName, tLodName );

		if( !bResult )
		{
			std::basic_string<TCHAR> tErrorMessage = _T("SetupPhysicalMaterial: Failed to import a texture for ");
			tErrorMessage += tChannelName;
			tErrorMessage += _T(" channel.");

			this->LogToWindow( tErrorMessage, Warning );
		}
		else if( !mBitmapTex )
		{
			// skip warning if there aren't any textures on this channel (most likely due to non-baked channels)
			continue;
		}

		if( tChannelName == _T("base_weight") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("base_weight"), 1.f );
			mMaxPhysicalMaterial->SetSubTexmap( 0, mBitmapTex );
		}
		else if( tChannelName == _T("base_color") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("base_color"), Point4( 1.f, 1.f, 1.f, 1.f ) );
			mMaxPhysicalMaterial->SetSubTexmap( 1, mBitmapTex );
		}
		else if( tChannelName == _T("reflectivity") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("reflectivity"), 1.f );
			mMaxPhysicalMaterial->SetSubTexmap( 2, mBitmapTex );
		}
		else if( tChannelName == _T("refl_color") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("refl_color"), Point4( 1.f, 1.f, 1.f, 1.f ) );
			mMaxPhysicalMaterial->SetSubTexmap( 3, mBitmapTex );
		}
		else if( tChannelName == _T("roughness") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("roughness"), 1.f );
			mMaxPhysicalMaterial->SetSubTexmap( 4, mBitmapTex );

			// not compatible with metalness
			SetShaderParameter( mMaxPhysicalMaterial, _T("metalness"), 0.f );
			SetShaderParameter( mMaxPhysicalMaterial, _T("metalness_map_on"), false );
		}
		else if( tChannelName == _T("metalness") )
		{
			const bool bHasRoughnessMap = mMaxPhysicalMaterial->GetSubTexmap( 4 ) != nullptr;

			// metalness is not compatible with roughness, disable rendering of metalness if roughness exists
			SetShaderParameter( mMaxPhysicalMaterial, _T("metalness"), bHasRoughnessMap ? 0.f : 1.f );
			SetShaderParameter( mMaxPhysicalMaterial, _T("metalness_map_on"), bHasRoughnessMap ? false : true );

			mMaxPhysicalMaterial->SetSubTexmap( 5, mBitmapTex );
		}
		else if( tChannelName == _T("diff_rough") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("diff_roughness"), 1.f );
			mMaxPhysicalMaterial->SetSubTexmap( 6, mBitmapTex );
		}
		else if( tChannelName == _T("anisotropy") )
		{
			// SetShaderParameter( mMaxPhysicalMaterial, _T("anisotropy"), 1.f );
			mMaxPhysicalMaterial->SetSubTexmap( 7, mBitmapTex );
		}
		else if( tChannelName == _T("aniso_angle") )
		{
			// SetShaderParameter( mMaxPhysicalMaterial, _T("aniso_angle"), 0.f );
			mMaxPhysicalMaterial->SetSubTexmap( 8, mBitmapTex );
		}
		else if( tChannelName == _T("transparency") )
		{
			// SetShaderParameter( mMaxPhysicalMaterial, _T("transparency"), 0.f );
			// SetShaderParameter( mMaxPhysicalMaterial, _T("transparency_map_on"), true );

			mMaxPhysicalMaterial->SetSubTexmap( 9, mBitmapTex );
		}
		else if( tChannelName == _T("trans_color") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("trans_color"), Point4( 1.f, 1.f, 1.f, 1.f ) );
			mMaxPhysicalMaterial->SetSubTexmap( 10, mBitmapTex );
		}
		else if( tChannelName == _T("trans_rough") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("trans_roughness"), 1.f );
			SetShaderParameter( mMaxPhysicalMaterial, _T("trans_roughness_lock"), false );

			mMaxPhysicalMaterial->SetSubTexmap( 11, mBitmapTex );
		}
		else if( tChannelName == _T("trans_ior") )
		{
			// SetShaderParameter( mMaxPhysicalMaterial, _T("trans_ior"), 50.f );
			mMaxPhysicalMaterial->SetSubTexmap( 12, mBitmapTex );
		}
		else if( tChannelName == _T("sss_scatter") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("scattering"), 1.0f );
			mMaxPhysicalMaterial->SetSubTexmap( 13, mBitmapTex );
		}
		else if( tChannelName == _T("sss_color") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("sss_color"), Point4( 1.f, 1.f, 1.f, 1.f ) );
			mMaxPhysicalMaterial->SetSubTexmap( 14, mBitmapTex );
		}
		else if( tChannelName == _T("sss_scatter_color") )
		{
			// Physical Material does not support maps for this property
		}
		else if( tChannelName == _T("sss_scale") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("sss_scale"), 1.f );
			SetShaderParameter( mMaxPhysicalMaterial, _T("sss_depth"), 1000.f );
			mMaxPhysicalMaterial->SetSubTexmap( 15, mBitmapTex );
		}
		else if( tChannelName == _T("emission") )
		{
			// disable emission rendering for now due to weird behavior in viewport ( works in offline renderer )
			SetShaderParameter( mMaxPhysicalMaterial, _T("emission"), 1.0f );

			mMaxPhysicalMaterial->SetSubTexmap( 16, mBitmapTex );
		}
		else if( tChannelName == _T("emit_color") )
		{
			// disable emission rendering for now due to weird behavior in viewport ( works in offline renderer )
			SetShaderParameter( mMaxPhysicalMaterial, _T("emission"), 1.f );

			SetShaderParameter( mMaxPhysicalMaterial, _T("emit_color"), Point4( 0.f, 0.f, 0.f, 1.f ) );

			mMaxPhysicalMaterial->SetSubTexmap( 17, mBitmapTex );
		}
		else if( tChannelName == _T("coat") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("coat"), 1.f );
			mMaxPhysicalMaterial->SetSubTexmap( 18, mBitmapTex );
		}
		else if( tChannelName == _T("coat_color") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("coat_color"), Point4( 1.f, 1.f, 1.f, 1.f ) );
			mMaxPhysicalMaterial->SetSubTexmap( 19, mBitmapTex );
		}
		else if( tChannelName == _T("coat_rough") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("coat_rough"), 0.f );
			mMaxPhysicalMaterial->SetSubTexmap( 20, mBitmapTex );
		}
		// new to max 2023
#if MAX_VERSION_MAJOR >= 25
		else if( tChannelName == _T("coat_anistropy") )
		{
			// SetShaderParameter( mMaxPhysicalMaterial, _T("coat_anistropy"), 1.0f );
			mMaxPhysicalMaterial->SetSubTexmap( 21, mBitmapTex );
		}
		else if( tChannelName == _T("coat_anisoangle") )
		{
			// SetShaderParameter( mMaxPhysicalMaterial, _T("coat_anisoangle"), 0.f );
			mMaxPhysicalMaterial->SetSubTexmap( 22, mBitmapTex );
		}
		else if( tChannelName == _T("sheen") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("sheen"), 1.0f );
			mMaxPhysicalMaterial->SetSubTexmap( 23, mBitmapTex );
		}
		else if( tChannelName == _T("sheen_color") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("sheen"), 1.0f );
			SetShaderParameter( mMaxPhysicalMaterial, _T("sheen_color_map_on"), true );
			mMaxPhysicalMaterial->SetSubTexmap( 24, mBitmapTex );
		}
		else if( tChannelName == _T("sheen_roughness") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("sheen_roughness"), 1.0f );
			mMaxPhysicalMaterial->SetSubTexmap( 25, mBitmapTex );
		}
		else if( tChannelName == _T("thin_film") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("thin_film"), 0.0f );
			mMaxPhysicalMaterial->SetSubTexmap( 26, mBitmapTex );
		}
		else if( tChannelName == _T("thin_film_ior") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("thin_film_ior"), 0.0f );
			mMaxPhysicalMaterial->SetSubTexmap( 27, mBitmapTex );
		}
#endif
		else if( tChannelName == _T("bump") )
		{
			// SetShaderParameter( mMaxPhysicalMaterial, _T("bump_map_amt"), 1.0f );
			Texmap* mNormalMap = (Texmap*)::CreateInstance( TEXMAP_CLASS_ID, GNORMAL_CLASS_ID );
			if( mNormalMap )
			{
				mMaxPhysicalMaterial->SetSubTexmap( 30, mNormalMap );
				mNormalMap->SetSubTexmap( 0, mBitmapTex );
			}
		}
		else if( tChannelName == _T("coat_bump") )
		{
			// SetShaderParameter( mMaxPhysicalMaterial, _T("clearcoat_bump_map_amt"), 0.3f );
			Texmap* mNormalMap = (Texmap*)::CreateInstance( TEXMAP_CLASS_ID, GNORMAL_CLASS_ID );
			if( mNormalMap )
			{
				mMaxPhysicalMaterial->SetSubTexmap( 31, mNormalMap );
				mNormalMap->SetSubTexmap( 0, mBitmapTex );
			}
		}
		else if( tChannelName == _T("displacement") )
		{
			SetShaderParameter( mMaxPhysicalMaterial, _T("displacement_map_amt"), 1.f );
			mMaxPhysicalMaterial->SetSubTexmap( 32, mBitmapTex );
		}
		else if( tChannelName == _T("cutout") )
		{
			mMaxPhysicalMaterial->SetSubTexmap( 33, mBitmapTex );
		}
	}

	mMaxPhysicalMaterial->SetMtlFlag( MTL_TEX_DISPLAY_ENABLED );

	if( /*UseNonConflictingTextureNames == false */ true )
	{
		MaterialInfo materialInfo( tLodName );
#if MAX_RELEASE < 23
		materialInfo.MaxMaterialReference = mMaxPhysicalMaterial;
#else
		materialInfo.MaxPhysicalMaterialReference = mMaxPhysicalMaterial;
#endif
		const spString rMaterialId = sgMaterial->GetMaterialGUID();
		materialInfo.MaterialId = rMaterialId.c_str();

		this->CachedMaterialInfos.push_back( materialInfo );
	}

	return mMaxPhysicalMaterial;
}
#endif

// gets the lod-switch camera distance
double SimplygonMax::GetLODSwitchCameraDistance( int pixelSize )
{
	double camera_distance = -1.0f;

	// check if any nodes are selected else print message and return
	if( GetCOREInterface()->GetSelNodeCount() < 1 )
	{
		LogMessageToScriptEditor( std::basic_string<TCHAR>( _T("Select an object to be able to get the switch distance for desired pixel size!") ) );
		return camera_distance;
	}

	ViewExp& active_viewport = (ViewExp&)GetCOREInterface()->GetActiveViewExp();
	GraphicsWindow* graphics_window = active_viewport.getGW();

	if( graphics_window )
	{
		// pick the first selected node
		INode* mNode = GetCOREInterface()->GetSelNode( 0 );

		if( !this->IsMesh( mNode ) )
		{
			LogMessageToScriptEditor( std::basic_string<TCHAR>( _T("Could not convert to tri object.") ) );
			return camera_distance;
		}

		ObjectState os = mNode->EvalWorldState( this->CurrentTime );
		if( os.obj == nullptr )
		{
			LogMessageToScriptEditor( std::basic_string<TCHAR>( _T("Object state invalid.") ) );
			return camera_distance;
		}

		// convert to trimesh, get the object
		TriObject* tobj = (TriObject*)SafeConvertToType( os.obj, this->CurrentTime, triObjectClassID );
		if( tobj == nullptr )
		{
			LogMessageToScriptEditor( std::basic_string<TCHAR>( _T("Could not convert to tri object.") ) );
			return camera_distance;
		}

		int unitType = GetUnitDisplayType();
		float scale = 1.0f;

#if MAX_VERSION_MAJOR < 24
		GetMasterUnitInfo( &unitType, &scale );
#else
		GetSystemUnitInfo( &unitType, &scale );
#endif

		Box3 bbox;
		tobj->GetWorldBoundBox( this->CurrentTime, mNode, &active_viewport, bbox );
		const Point3 center = bbox.Center();
		const double radius = center.LengthSquared();

		const int screenwidth = graphics_window->getWinSizeX();
		const int screenheight = graphics_window->getWinSizeY();
		const double fov_horizontal = active_viewport.GetFOV();
		const double fov_verticle = 2. * atan( tan( RAD2DEG( fov_horizontal ) / 2. ) * ( screenwidth / screenheight ) );

		const double screen_ratio = double( pixelSize ) / (double)screenheight;

		// normalized distance to the "screen" if the height of the screen is 1.
		const double normalized_distance = 1.0 / ( tan( DEG2RAD( fov_verticle / 2 ) ) );

		// the view-angle of the bounding sphere rendered onscreen.
		const double bsphere_angle = atan( screen_ratio / normalized_distance );

		// The distance in real world units from the camera to the center of the bounding sphere. Not to be confused with normalized distance
		camera_distance = radius / sin( bsphere_angle );
		camera_distance *= scale;
	}

	return camera_distance;
}

// gets the lod-switch pixel-size
double SimplygonMax::GetLODSwitchPixelSize( double distance )
{
	double pixelsize = 0.0f;

	if( GetCOREInterface()->GetSelNodeCount() < 1 )
	{
		LogMessageToScriptEditor( std::basic_string<TCHAR>( _T("Select an object to get switch distance for desired pixel size.") ) );
		return pixelsize;
	}

	ViewExp& active_viewport = (ViewExp&)GetCOREInterface()->GetActiveViewExp();
	GraphicsWindow* graphics_window = active_viewport.getGW();

	if( graphics_window )
	{
		// pick the first selected node
		INode* n = GetCOREInterface()->GetSelNode( 0 );

		if( !this->IsMesh( n ) )
		{
			LogMessageToScriptEditor( std::basic_string<TCHAR>( _T("Could not convert to tri object.") ) );
			return pixelsize;
		}

		ObjectState os = n->EvalWorldState( this->CurrentTime );
		if( os.obj == nullptr )
		{
			LogMessageToScriptEditor( std::basic_string<TCHAR>( _T("Object state invalid, aborting.") ) );
			return pixelsize;
		}

		// convert to trimesh, get the object
		TriObject* tobj = (TriObject*)SafeConvertToType( os.obj, this->CurrentTime, triObjectClassID );
		if( tobj == nullptr )
		{
			LogMessageToScriptEditor( std::basic_string<TCHAR>( _T("Could not convert to tri object.") ) );
			return pixelsize;
		}

		int unitType = GetUnitDisplayType();
		float scale = 1.0f;

#if MAX_VERSION_MAJOR < 24
		GetMasterUnitInfo( &unitType, &scale );
#else
		GetSystemUnitInfo( &unitType, &scale );
#endif

		distance /= scale;

		// get radius of bbox
		Box3 bbox;
		tobj->GetWorldBoundBox( this->CurrentTime, n, &active_viewport, bbox );
		Point3 center = bbox.Center();
		double radius = center.LengthSquared();

		int screenwidth = graphics_window->getWinSizeX();
		int screenheight = graphics_window->getWinSizeY();
		double fov_horizontal = active_viewport.GetFOV();
		double fov_verticle = 2 * atan( tan( RAD2DEG( fov_horizontal ) / 2 ) * ( screenwidth / screenheight ) );

		// the view-angle of the bounding sphere rendered on-screen.
		double bsphere_angle = asin( radius / distance );

		// this assumes the near clipping plane is at a distance of 1. Normalized screen height of the geometry
		double geom_view_height = tan( bsphere_angle );

		// the size of (half) the screen if the near clipping plane is at a distance of 1
		double screen_view_height = ( tan( DEG2RAD( fov_verticle / 2 ) ) );

		// the ratio of the geometry's screen size compared to the actual size of the screen
		double view_ratio = geom_view_height / screen_view_height;

		// multiply by the number of pixels on the screen
		pixelsize = int( view_ratio * screenheight );
	}

	return pixelsize;
}

// sets whether to enable reading of edge sets or not
void SimplygonMax::SetEnableEdgeSets( bool bEnable )
{
	this->EdgeSetsEnabled = bEnable;
}

// sets up Max vertex colors
void SimplygonMax::SetupVertexColorData(
    Mesh& mMaxMesh, int mappingChannel, UVWMapper& mUVWMapper, uint TriangleCount, uint VertexCount, spRealArray sgVertexColors )
{
	mMaxMesh.setMapSupport( mappingChannel );
	mMaxMesh.ApplyMapper( mUVWMapper, mappingChannel );

	MeshMap& mMaxMeshMap = mMaxMesh.Map( mappingChannel );
	mMaxMeshMap.SetFlag( MESHMAP_VERTCOLOR );

	// set up the data
	mMaxMeshMap.setNumFaces( TriangleCount );
	mMaxMeshMap.setNumVerts( VertexCount );

	spRidArray sgVertexIds = sg->CreateRidArray();
	spRealArray sgPackedVertexColors = spRealArray::SafeCast( sgVertexColors->NewPackedCopy( sgVertexIds ) );

	for( uint tid = 0; tid < TriangleCount; ++tid )
	{
		for( int c = 0; c < 3; ++c )
		{
			// point at the vertex
			const rid vid = sgVertexIds->GetItem( tid * 3 + c );

			mMaxMeshMap.tf[ tid ].t[ c ] = vid;

			spRealData sgColor = sgPackedVertexColors->GetTuple( vid );

			// copy baked color channel
			mMaxMeshMap.tv[ vid ].x = sgColor[ 0 ];
			mMaxMeshMap.tv[ vid ].y = sgColor[ 1 ];
			mMaxMeshMap.tv[ vid ].z = sgColor[ 2 ];
		}
	}
}

// gets the material-info handler
MaterialInfoHandler* SimplygonMax::GetMaterialInfoHandler()
{
	return this->materialInfoHandler;
}

// gets work-directory handler
WorkDirectoryHandler* SimplygonMax::GetWorkDirectoryHandler()
{
	return this->workDirectoryHandler;
}

// gets scene handler
Scene* SimplygonMax::GetSceneHandler()
{
	return this->SceneHandler;
}

void SimplygonMax::SetCopyTextures( bool bCopy )
{
	this->copyTextures = bCopy;
}

void SimplygonMax::SetLinkMeshes( bool bLink )
{
	this->mapMeshes = bLink;
}

void SimplygonMax::SetLinkMaterials( bool bLink )
{
	this->mapMaterials = bLink;
}

void SimplygonMax::ClearGlobalMapping()
{
	this->GlobalGuidToMaxNodeMap.clear();
	this->GlobalMaxToSgMaterialMap.clear();
	this->GlobalSgToMaxMaterialMap.clear();
	this->GlobalExportedMaterialMap.clear();
}

void SimplygonMax::SetMeshFormatString( std::basic_string<TCHAR> tMeshFormatString )
{
	this->meshFormatString = tMeshFormatString;
}

void SimplygonMax::SetInitialLODIndex( int initialIndex )
{
	this->initialLodIndex = initialIndex;
}
