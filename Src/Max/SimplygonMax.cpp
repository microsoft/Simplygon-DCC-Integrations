// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"

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

using namespace Simplygon;

#if( VERSION_INT < 420 )
#ifndef GNORMAL_CLASS_ID
#define GNORMAL_CLASS_ID Class_ID( 0x243e22c6, 0X63F6A014 )
#endif
#endif

#define MaxNumCopyRetries 10u

extern HINSTANCE hInstance;

SimplygonMax* SimplygonMaxInstance = nullptr;
extern SimplygonInitClass* SimplygonInitInstance;

#define PHYSICAL_MATERIAL_CLASS_ID Class_ID( 1030429932L, 3735928833L )

PBBitmap* SetupMaxTexture( std::basic_string<TCHAR> tFilePath );
bool TextureHasAlpha( const char* tTextureFilePath );
float GetBitmapTextureGamma( BitmapTex* mBitmapTex );
void GetImageFullFilePath( const TCHAR* tPath, TCHAR* tDestinationPath );
spShadingColorNode CreateColorShadingNetwork( float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f );

spShadingTextureNode CreateTextureNode(
    BitmapTex* mBitmapTex, std::basic_string<TCHAR>& tMaxMappingChannel, std::basic_string<TCHAR>& tTextureName, TimeValue& time, const bool isSRGB )
{
	// create a basic shading network
	spShadingTextureNode sgTextureNode = sg->CreateShadingTextureNode();
	sgTextureNode->SetTextureName( LPCTSTRToConstCharPtr( tTextureName.c_str() ) );
	sgTextureNode->SetTexCoordName( LPCTSTRToConstCharPtr( tMaxMappingChannel.c_str() ) );
	sgTextureNode->SetUseSRGB( isSRGB );

	if( mBitmapTex != nullptr)
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
	return sgTextureNode;
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
		if( bIsEnabled && mTexMap && ( mTexMap->ClassID() == Class_ID( BMTEX_CLASS_ID, 0 ) ) )
		{
			return true;
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
		std::basic_string<TCHAR> tMaterialName = ConstCharPtrToLPCTSTR( rMaterialName.c_str() );

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

									// create a basic shading network
									spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMappingChannel, tTextureName, time, bSRGB );

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
										sgTextureNode->SetUseSRGB( nodeProxy->isSRGB );
									}

									// create texture and add it to scene
									spTexture sgTexture = nullptr;

									// do lookup in case this texture is already in use
									std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
									    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
									const bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

									if( bTextureInUse )
									{
										sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
				BitmapTex* mBitmapTex = (BitmapTex*)mBaseWeightMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );
				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// fetch mapping channel
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mBaseWeightMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					const bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
			const TCHAR* tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

			this->CreateMaterialChannel( sgMaterial, tChannelName );

			Point4* mBaseColor = this->GetColor4( _T("base_color") );
			Texmap* mBaseColorMap = this->GetMap( _T("base_color_map") );
			bool mBaseColorMapOn = this->GetBool( _T("base_color_map_on") );

			if( HasValidTexMap( mBaseColorMap, mBaseColorMapOn ) )
			{
				BitmapTex* mBitmapTex = (BitmapTex*)mBaseColorMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );
				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// fetch mapping channel
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mBaseColorMap );

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
					
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					const bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
				BitmapTex* mBitmapTex = (BitmapTex*)mReflectivityMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mReflectivityMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					const bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
				BitmapTex* mBitmapTex = (BitmapTex*)mReflColorMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mReflColorMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					const bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
				BitmapTex* mBitmapTex = (BitmapTex*)mRougnessMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mRougnessMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					spShadingNode sgExitNode = nullptr;
					if( mRoughnessInv )
					{
						spShadingColorNode sgNegativeNode = sg->CreateShadingColorNode();
						sgNegativeNode->SetColor( -1.0f, -1.0f, -1.0f, 1.0f );

						spShadingColorNode sgPositiveNode = sg->CreateShadingColorNode();
						sgPositiveNode->SetColor( 1.0f, 1.0f, 1.0f, 1.0f );

						spShadingMultiplyNode sgMultiplyNode = sg->CreateShadingMultiplyNode();
						sgMultiplyNode->SetInput( 0, sgNegativeNode );
						sgMultiplyNode->SetInput( 1, sgTextureNode );

						spShadingAddNode sgAddNode = sg->CreateShadingAddNode();
						sgAddNode->SetInput( 0, sgMultiplyNode );
						sgAddNode->SetInput( 1, sgPositiveNode );

						sgExitNode = (spShadingNode)sgAddNode;
					}
					else
					{
						sgExitNode = (spShadingNode)sgTextureNode;
					}

					sgMaterial->SetShadingNetwork( cChannelName, sgExitNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					const bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mRougnessMap && mRoughnessMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mRougnessMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = nullptr;
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
				BitmapTex* mBitmapTex = (BitmapTex*)mDiffRoughMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mDiffRoughMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					const bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
				BitmapTex* mBitmapTex = (BitmapTex*)mMetalnessMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mMetalnessMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					const bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
				BitmapTex* mBitmapTex = (BitmapTex*)mTransIORMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mTransIORMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					const bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mTransIORMap && mTransIORMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mTransIORMap, tMaterialName, tChannelName );
				}

				// fit IOR of 0 - 50 into 0 - 1
				float divisor = 50.0f;
				float mCorrectedIOR = clamp( mTransIOR / divisor, 0.0f, 1.0f );

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
				BitmapTex* mBitmapTex = (BitmapTex*)mTransparencyMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );
				const bool bUseAlphaAsTransparency = mBitmapTex->GetAlphaAsRGB( TRUE ) == TRUE;

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mTransparencyMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					spShadingSwizzlingNode sgSwizzleNode = sg->CreateShadingSwizzlingNode();
					sgSwizzleNode->SetInput( 0, sgTextureNode );
					sgSwizzleNode->SetInput( 1, sgTextureNode );
					sgSwizzleNode->SetInput( 2, sgTextureNode );
					sgSwizzleNode->SetInput( 3, sgTextureNode );

					const bool bTextureHasAlpha = TextureHasAlpha( LPCTSTRToConstCharPtr( tTexturePathWithName.c_str() ) );

					if( bTextureHasAlpha && bUseAlphaAsTransparency )
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

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					const bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
				BitmapTex* mBitmapTex = (BitmapTex*)mTransColorMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mTransColorMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					const bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
			float convertedDepth = clamp( mTransDepth / 1000.f, 0.0f, 1.0f );

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
				BitmapTex* mBitmapTex = (BitmapTex*)mTransRoughMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mTransRoughMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					const bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mTransRoughMap && mTransRoughMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mTransRoughMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = nullptr;
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
				BitmapTex* mBitmapTex = (BitmapTex*)mScatteringMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mScatteringMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					const bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
				BitmapTex* mBitmapTex = (BitmapTex*)mSSSColorMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mSSSColorMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					const bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
				BitmapTex* mBitmapTex = (BitmapTex*)mSSSScaleMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mSSSScaleMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					// not sure if combining is preferred?!
					spShadingColorNode sgScaleColorNode = sg->CreateShadingColorNode();

					// fit depth of 0 - 1000 into 0.0 - 1.0, truncate everything larger than 1000
					// combine depth and scale into same texture
					float correctedDepth = clamp( mSSSDepth / 1000.0f, 0.0f, 1.0f );
					float combinedScaleAndDepth = mSSSScale * correctedDepth;

					sgScaleColorNode->SetColor( combinedScaleAndDepth, combinedScaleAndDepth, combinedScaleAndDepth, 1.0f );

					spShadingMultiplyNode sgMultiplyNode = sg->CreateShadingMultiplyNode();
					sgMultiplyNode->SetInput( 0, sgScaleColorNode );
					sgMultiplyNode->SetInput( 1, sgTextureNode );

					spShadingNode sgExitNode = (spShadingNode)sgMultiplyNode;

					sgMaterial->SetShadingNetwork( cChannelName, sgExitNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					const bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mSSSScaleMap && mSSSScaleMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mSSSScaleMap, tMaterialName, tChannelName );
				}

				// fit depth of 0 - 1000 into 0.0 - 1.0, truncate everything larger than 1000
				// combine depth and scale into same texture
				float correctedDepth = clamp( mSSSDepth / 1000.0f, 0.0f, 1.0f );
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
				BitmapTex* mBitmapTex = (BitmapTex*)mEmissionMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );
				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// fetch mapping channel
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mEmissionMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					const bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
				BitmapTex* mBitmapTex = (BitmapTex*)mEmitColorMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );
				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// fetch mapping channel
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mEmitColorMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					const bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
			float correctedLuminance = clamp( mEmitLuminance / divisor, 0.0f, 1.0f );

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

			if( HasValidTexMap( mBumpMap, mBumpMapOn ) || ( mBumpMap && mBumpMapOn && mBumpMap->ClassID() == GNORMAL_CLASS_ID && mBumpMap->GetSubTexmap( 0 ) ) )
			{
				this->CreateMaterialChannel( sgMaterial, tChannelName );

				// retrieve the texture file path
				BitmapTex* mBitmapTex = (BitmapTex*)mBumpMap;

				if( mBumpMap->ClassID() == GNORMAL_CLASS_ID )
				{
					mBumpMap = mBumpMap->GetSubTexmap( 0 );
					mBitmapTex = (BitmapTex*)mBumpMap;
				}

				const bool bIsSRGB = false;

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mBumpMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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

			if( HasValidTexMap( mCoatBumpMap, mCoatBumpMapOn ) ||
			    ( mCoatBumpMap && mCoatBumpMapOn && mCoatBumpMap->ClassID() == GNORMAL_CLASS_ID && mCoatBumpMap->GetSubTexmap( 0 ) ) )
			{
				this->CreateMaterialChannel( sgMaterial, tChannelName );

				// retrieve the texture file path
				BitmapTex* mBitmapTex = (BitmapTex*)mCoatBumpMap;

				if( mCoatBumpMap->ClassID() == GNORMAL_CLASS_ID )
				{
					mCoatBumpMap = mCoatBumpMap->GetSubTexmap( 0 );
					mBitmapTex = (BitmapTex*)mCoatBumpMap;
				}

				const bool bIsSRGB = false;

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mCoatBumpMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
				this->CreateMaterialChannel( sgMaterial, tChannelName );

				// retrieve the texture file path
				BitmapTex* mBitmapTex = (BitmapTex*)mDisplacementMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mDisplacementMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					spShadingColorNode sgColorNode = sg->CreateShadingColorNode();
					sgColorNode->SetColor( mDisplacementMapAmt, mDisplacementMapAmt, mDisplacementMapAmt, 1.0f );

					spShadingMultiplyNode sgMultiplyNode = sg->CreateShadingMultiplyNode();
					sgMultiplyNode->SetInput( 0, sgColorNode );
					sgMultiplyNode->SetInput( 1, sgTextureNode );

					sgMaterial->SetShadingNetwork( cChannelName, sgMultiplyNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
				// retrieve the texture file path
				BitmapTex* mBitmapTex = (BitmapTex*)mCutoutMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mCutoutMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
				// retrieve the texture file path
				BitmapTex* mBitmapTex = (BitmapTex*)mCoatMap;

				bool bIsSRGB = IsSRGB( mBitmapTex );

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mCoatMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
				// retrieve the texture file path
				BitmapTex* mBitmapTex = (BitmapTex*)mCoatColorMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mCoatColorMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
				// retrieve the texture file path
				BitmapTex* mBitmapTex = (BitmapTex*)mCoatRoughMap;

				const bool bIsSRGB = IsSRGB( mBitmapTex );

				GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

				// if the texture path was found
				if( _tcslen( tTexturePath ) > 0 )
				{
					// set mapping channel to 1
					std::basic_string<TCHAR> tMaxMappingChannel = GetMappingChannelAsString( mCoatRoughMap );

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

					// create a basic shading network
					spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, time, bIsSRGB );

					spShadingNode sgExitNode = nullptr;
					if( mCoatRoughnessInv )
					{
						spShadingColorNode sgNegativeNode = sg->CreateShadingColorNode();
						sgNegativeNode->SetColor( -1.0f, -1.0f, -1.0f, 1.0f );

						spShadingColorNode sgPositiveNode = sg->CreateShadingColorNode();
						sgPositiveNode->SetColor( 1.0f, 1.0f, 1.0f, 1.0f );

						spShadingMultiplyNode sgMultiplyNode = sg->CreateShadingMultiplyNode();
						sgMultiplyNode->SetInput( 0, sgNegativeNode );
						sgMultiplyNode->SetInput( 1, sgTextureNode );

						spShadingAddNode sgAddNode = sg->CreateShadingAddNode();
						sgAddNode->SetInput( 0, sgMultiplyNode );
						sgAddNode->SetInput( 1, sgPositiveNode );

						sgExitNode = (spShadingNode)sgAddNode;
					}
					else
					{
						sgExitNode = (spShadingNode)sgTextureNode;
					}

					sgMaterial->SetShadingNetwork( cChannelName, sgExitNode );

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator =
					    MaxReference->LoadedTexturePathToID.find( tTexturePathWithName );
					bool bTextureInUse = ( tTextureIterator != MaxReference->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = MaxReference->GetSceneHandler()->sgScene->GetTextureTable()->FindTextureUsingPath(
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
			else
			{
				// if there is a texmap of unsupported type enabled, output warning
				if( mCoatRoughMap && mCoatRoughMapOn )
				{
					MaxReference->LogMaterialNodeMessage( mCoatRoughMap, tMaterialName, tChannelName );
				}

				spShadingColorNode sgColorNode = nullptr;

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
			float correctedCoatIOR = clamp( mCoatIOR / divisor, 0.0f, 1.0f );

			spShadingColorNode sgColorNode = CreateColorShadingNetwork( correctedCoatIOR, correctedCoatIOR, correctedCoatIOR, 1.0f );

			sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
		}
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

	this->extractionType = BATCH_PROCESSOR;
	this->TextureCoordinateRemapping = 0;
	this->LockSelectedVertices = false;

	this->sgPipeline = nullptr;

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

	this->sgPipeline = nullptr;
	this->inputSceneFile = _T("");
	this->outputSceneFile = _T("");

	this->SettingsObjectName = _T("");
	this->meshFormatString = _T("{MeshName}");
	this->initialLodIndex = 1;

	this->CanUndo = true;
	this->ShowProgress = true;
	this->RunDebugger = false;

	this->PipelineRunMode = 1;

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

	this->MaxBoneToSgBone.clear();
	this->SgBoneToMaxBone.clear();
	this->SgBoneIdToIndex.clear();

	this->MaxSgNodeMap.clear();
	this->SgMaxNodeMap.clear();
	this->GlobalExportedMaterialMap.clear();
	this->GlobalMaxToSgMaterialMap.clear();

	this->ShadingTextureNodeToPath.clear();

	this->SelectionSetEdgesMap.clear();

	this->DefaultPrefix = std::basic_string<TCHAR>( _T("_LOD") );

	this->LoadedTexturePathToID.clear();

	this->ImportedTextures.clear();

	this->ClearShadingNetworkInfo( true );

	this->TextureOutputDirectory = _T("");

	this->materialProxyTable.clear();
	this->materialProxyWritebackTable.clear();

	this->SetUseNewMaterialSystem( false );

	this->ImportedMaxIndexToUv.clear();
	this->ImportedUvNameToMaxIndex.clear();
	this->GlobalGuidToMaxNodeMap.clear();
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

	sg->SetGlobalBoolSetting( "UseCustomBillboardCloudUpVector", true );
	sg->SetGlobalFloatSetting( "BillboardCloudUpVectorX", 0.0f );
	sg->SetGlobalFloatSetting( "BillboardCloudUpVectorY", 0.0f );
	sg->SetGlobalFloatSetting( "BillboardCloudUpVectorZ", 1.0f );

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
	spSceneNode sgCreatedNode = nullptr;
	bool bPostAddCameraToSelectionSet = false;

	// is this node a mesh?
	const bool bIsMesh = this->IsMesh( mMaxNode );

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

	sgCreatedNode->SetIsFrozen( mMaxNode->IsFrozen() > 0 );

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

// Returns true if node (INode) is a mesh
bool SimplygonMax::IsMesh( INode* mMaxNode )
{
	Object* mMaxObject = mMaxNode->GetObjectRef();
	ObjectState mMaxObjectState = mMaxNode->EvalWorldState( this->CurrentTime );
	if( !mMaxObjectState.obj )
	{
		return false; // we have no object, skip it, go on
	}

	if( !mMaxObjectState.obj->CanConvertToType( triObjectClassID ) )
	{
		return false; // this is not a tri-mesh object, skip it, go on
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

	if( !mMaxObjectState.obj->CanConvertToType( mClassId ) && !dynamic_cast<MaxSDK::IPhysicalCamera*>( mMaxObjectState.obj ) )
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
	if( !mMaxObjectState.obj->CanConvertToType( mClassId ) && !bIsPhysical )
	{
		return spSceneNode();
	}

	GenCamera* mMaxGeneralCamera =
	    bIsPhysical ? dynamic_cast<GenCamera*>( mMaxObjectState.obj ) : (GenCamera*)mMaxObjectState.obj->ConvertToType( this->CurrentTime, mClassId );

	const int mCamType = mMaxGeneralCamera->Type();
	if( mCamType == FREE_CAMERA ) {}
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
	for( size_t meshIndex = 0; meshIndex < this->SelectedMeshCount; ++meshIndex )
	{
		if( !this->ExtractGeometry( meshIndex ) )
		{
			return false;
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
	sgBoneNode->SetIsFrozen( sgNode->GetIsFrozen() );
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
				spSelectionSet sgSelectionSetList = nullptr;
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
				// found a skin, use it
				meshNode->SkinModifiers = mModifier;
				break;
			}
		}
	}

	// if there is a skinning modifier, temporarily disable if enabled, and store current state
	BOOL bSkinModifierEnabled = FALSE;
	if( meshNode->SkinModifiers != nullptr )
	{
		bSkinModifierEnabled = meshNode->SkinModifiers->IsEnabled();
		meshNode->SkinModifiers->DisableMod();
	}

	ObjectState mMaxNodeObjectState = mMaxNode->EvalWorldState( this->CurrentTime );
	TriObject* mMaxTriObject = (TriObject*)mMaxNodeObjectState.obj->ConvertToType( this->CurrentTime, triObjectClassID );

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
			sgVertexLocks->SetItem( vid, mMaxMesh.vertSel[ vid ] > 0 );
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
		spRidArray sgParentMaterialIds = nullptr;

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
			for( int vid = 0; vid < vertexCount; ++vid )
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
				int boneIndex = 0;
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

	// loop through the map of selected sets
	this->LogToWindow( _T("Loop through selection sets...") );

	// selection sets
	this->AddToObjectSelectionSet( mMaxNode );
	this->AddEdgeCollapse( mMaxNode, sgMeshData );

	// re-enable skinning in the original geometry
	if( bSkinModifierEnabled )
	{
		meshNode->SkinModifiers->EnableMod();
		mMaxNode->EvalWorldState( this->CurrentTime );
	}

	return true;
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

		std::vector<spSceneMesh> sgProcessedMeshes;
		const spSceneNode sgRootNode = sgProcessedScene->GetRootNode();
		CollectSceneMeshes( sgRootNode, sgProcessedMeshes );

		// import meshes
		std::map<std::string, INode*> meshNodesThatNeedsParents;
		for( size_t meshIndex = 0; meshIndex < sgProcessedMeshes.size(); ++meshIndex )
		{
			spSceneMesh sgProcessedMesh = sgProcessedMeshes[ meshIndex ];

			const spString rNodeGuid = sgProcessedMesh->GetNodeGUID();

			const bool bWriteBackSucceeded = this->WritebackGeometry( sgProcessedScene, logicalLodIndex, sgProcessedMesh, meshNodesThatNeedsParents );
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
			tTempMaterialName = ( TSTR )( tMaterialNameStream.str().c_str() );
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
		for( uint mid = 0; mid < numMaterials; ++mid )
		{
			MtlBase* mMaxMaterial = static_cast<MtlBase*>( ( *mSceneMaterials )[ mid ] );
			if( mMaxMaterial && mMaxMaterial->IsMultiMtl() )
			{
				MultiMtl* mMaxMultiMaterial = (MultiMtl*)mMaxMaterial;

				for( uint smid = 0; smid < mMaxMultiMaterial->NumSubMtls(); ++smid )
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

// Write back data to max
bool SimplygonMax::WritebackGeometry( spScene sgProcessedScene,
                                      size_t logicalLodIndex,
                                      spSceneMesh sgProcessedMesh,
                                      std::map<std::string, INode*>& meshNodesThatNeedsParents )
{
	const spString rSgMeshId = sgProcessedMesh->GetNodeGUID();
	const char* cSgMeshId = rSgMeshId.c_str();

	// try to find mesh map via global lookup
	const std::map<std::string, GlobalMeshMap>::const_iterator& meshMap = this->GlobalGuidToMaxNodeMap.find( cSgMeshId );
	bool bHasMeshMap = ( this->mapMeshes || this->extractionType == BATCH_PROCESSOR ) ? meshMap != this->GlobalGuidToMaxNodeMap.end() : nullptr;

	// try to find original Max mesh from map handle
	INode* mMappedMaxNode = bHasMeshMap ? this->MaxInterface->GetINodeByHandle( meshMap->second.GetMaxId() ) : nullptr;
	if( mMappedMaxNode )
	{
		// if the name does not match, try to get Max mesh by name (fallback)
		if( mMappedMaxNode->GetName() != meshMap->second.GetName() )
		{
			mMappedMaxNode = bHasMeshMap ? this->MaxInterface->GetINodeByName( meshMap->second.GetName().c_str() ) : nullptr;
		}
	}

	bHasMeshMap = mMappedMaxNode != nullptr;

	// fetch the geometry from the mesh node
	spGeometryData sgMeshData = sgProcessedMesh->GetGeometry();
	spString rProcessedMeshName = sgProcessedMesh->GetName();
	const char* cProcessedMeshName = rProcessedMeshName.c_str();
	std::basic_string<TCHAR> tMaxOriginalNodeName = ConstCharPtrToLPCTSTR( cProcessedMeshName );

	if( sgMeshData->GetTriangleCount() == 0 )
	{
		std::basic_string<TCHAR> tWarningMessage = _T("Warning - Zero triangle mesh detected when importing node: ");
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
	if( bHasMeshMap )
	{
		INode* mOriginalMaxNode = mMappedMaxNode;
		tMaxOriginalNodeName = mOriginalMaxNode->GetName();
		mNewMaxNode->SetMtl( mOriginalMaxNode->GetMtl() );
		mOriginalMaxMaterial = mOriginalMaxNode->GetMtl();

		// only use material map if we want to map original materials
		if( mOriginalMaxMaterial )
		{
			globalMaterialMap = this->mapMaterials ? this->GetGlobalMaterialMap( mOriginalMaxMaterial ) : nullptr;
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
			mNewMaxMesh.setFaceMtlIndex( tid, mid >= 0 ? mid : 0 );
		}
		else if( !sgMaterialIds.IsNull() )
		{
			const int mid = sgMaterialIds->GetItem( tid );

			// if material id is valid, assign, otherwise 0
			mNewMaxMesh.setFaceMtlIndex( tid, mid >= 0 ? mid + 0 : 0 );
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
	std::basic_string<TCHAR> tMeshName = bHasMeshMap ? tMaxOriginalNodeName.c_str() : tMaxOriginalNodeName.c_str();

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

	if( !sgMaterialIds.IsNull() )
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

			if( mid >= sgMaterialTable->GetMaterialsCount() )
			{
				std::basic_string<TCHAR> tErrorMessage = _T("Error - Writeback of material(s) failed due to an out-of-range material id when importing node ");
				tErrorMessage += tMaxOriginalNodeName;
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

					// fetch global guid map
					globalMaterialMap = this->GetGlobalMaterialMap( sMaterialId );
					if( !globalMaterialMap )
						return false;

					// get mapped material from guid map
					Mtl* mGlobalMaxMaterial = GetExistingMappedMaterial( globalMaterialMap->sgMaterialId );
					if( !mGlobalMaxMaterial )
					{
						// if not there, fallback to name based fetch
						mGlobalMaxMaterial = GetExistingMaterial( globalMaterialMap->sgMaterialName );
						if( !mGlobalMaxMaterial )
						{
							return false;
						}
					}

					if( globalMaterialMap )
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

				globalMaterialMap = this->GetGlobalMaterialMap( sMaterialId );

				if( globalMaterialMap )
				{
					Mtl* mGlobalMaxMaterial = GetExistingMaterial( globalMaterialMap->sgMaterialName );
					mOriginalMaxMaterial = mGlobalMaxMaterial;
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

					boneToIdInUseMap.insert( std::pair<INode*, int>( mBone, globalBoneIndex ) );
					boneIdToBoneInUseMap.insert( std::pair<int, INode*>( globalBoneIndex, mBone ) );
				}
			}
		}

		if( bHasMeshMap )
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
				return false;
		}
	}

	// clear shading network info
	this->ClearShadingNetworkInfo();

	mNewMaxMesh.InvalidateGeomCache();
	mNewMaxMesh.InvalidateTopologyCache();

	// add custom attributes

	// max deviation
	spRealArray sgMaxDeviation = spRealArray::SafeCast( sgProcessedScene->GetCustomField( "MaxDeviation" ) );
	if( !sgMaxDeviation.IsNull() )
	{
		const real maxDev = sgMaxDeviation->GetItem( 0 );
		mNewMaxNode->SetUserPropFloat( _T("MaxDeviation"), maxDev );
	}

	// scene radius
	const real sceneRadius = sgProcessedScene->GetRadius();
	mNewMaxNode->SetUserPropFloat( _T("SceneRadius"), sceneRadius );

	// original node name
	mNewMaxNode->SetUserPropString( _T("OriginalNodeName"), tMaxOriginalNodeName.c_str() );

	// intended node name
	mNewMaxNode->SetUserPropString( _T("IntendedNodeName"), tFormattedMeshName.c_str() );

	// imported node name
	mNewMaxNode->SetUserPropString( _T("ImportedNodeName"), tIndexedNodeName );

	return true;
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
	uint targetVertexColorMaxChannel = 0;
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
		return SetupMaxStdMaterial( sgProcessedScene, sgMeshName, nullptr, tsMaterialName, tNewMaterialName );
#else
		return SetupPhysicalMaterial( sgProcessedScene, sgMeshName, nullptr, tsMaterialName, tNewMaterialName );
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
					return false;
				}

				// empty tex coord level check
				spString rTextureUvSet = sgTextureNode->GetTexCoordName();
				if( rTextureUvSet.IsNullOrEmpty() )
				{
					return false;
				}

				// use texture
				std::basic_string<TCHAR> tTextureNameToFind = ConstCharPtrToLPCTSTR( rTextureNameToFind );
				std::basic_string<TCHAR> tTextureUvSet = ConstCharPtrToLPCTSTR( rTextureUvSet );

				spTexture sgTexture = sgTextureTable->FindTexture( LPCTSTRToConstCharPtr( tTextureNameToFind.c_str() ) );

				if( sgTexture.IsNull() )
				{
					return false;
				}

				spString rFilePath = sgTexture->GetFilePath();

				if( rFilePath.c_str() == nullptr )
				{
					return false;
				}

				std::basic_string<TCHAR> tTextureName = ConstCharPtrToLPCTSTR( sgTexture->GetName() );
				std::basic_string<TCHAR> tTexturePath = ConstCharPtrToLPCTSTR( sgTexture->GetFilePath() );
				std::basic_string<TCHAR> tTargetFilePath = Combine( tMaxBitmapDirectory, tTexturePath );

				if( !sgTexture->GetImageData().IsNull() )
				{
					// Embedded data, should be written to file
					sgTexture->SetFilePath( LPCTSTRToConstCharPtr( tTexturePath.c_str() ) );
					if( sgTexture->ExportImageData() )
					{
						tTexturePath = ConstCharPtrToLPCTSTR( sgTexture->GetFilePath().c_str() );
						sgTexture->SetImageData( nullptr );
					}
				}

				if( this->copyTextures )
				{
					// the name of the imported texture is based on the name of the node
					std::basic_string<TCHAR> tImportTextureName = tTextureName + GetExtensionOfFile( tTexturePath );
					ReplaceInvalidCharacters( tImportTextureName, '_' );
					std::basic_string<TCHAR> tImportTexturePath = Combine( tMaxBitmapDirectory, tImportTextureName );

					if( this->TextureOutputDirectory.length() > 0 )
					{
						const bool bFolderCreated = CreateFolder( this->TextureOutputDirectory.c_str() );
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

				mNewBitmapTex->SetMapName( tTargetFilePath.c_str() );

				if( sgTextureNode->GetUseSRGB() )
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

				this->materialInfoHandler->Add( tMeshNameOverride, tMaterialNameOverride, tChannelName, tTargetFilePath, maxMappingChannel );
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
					return false;
				}

				// empty tex coord level check
				spString rTextureUvSet = sgTextureNode->GetTexCoordName();
				if( rTextureUvSet.IsNullOrEmpty() )
				{
					return false;
				}

				// use texture
				std::basic_string<TCHAR> tTextureNameToFind = ConstCharPtrToLPCTSTR( rTextureNameToFind );
				std::basic_string<TCHAR> tTextureUvSet = ConstCharPtrToLPCTSTR( rTextureUvSet );

				spTexture sgTexture = sgTextureTable->FindTexture( LPCTSTRToConstCharPtr( tTextureNameToFind.c_str() ) );

				if( sgTexture.IsNull() )
				{
					return false;
				}

				spString rFilePath = sgTexture->GetFilePath();

				if( rFilePath.c_str() == nullptr )
				{
					return false;
				}

				std::basic_string<TCHAR> tTextureName = ConstCharPtrToLPCTSTR( sgTexture->GetName() );
				std::basic_string<TCHAR> tTexturePath = ConstCharPtrToLPCTSTR( sgTexture->GetFilePath() );
				std::basic_string<TCHAR> tTargetFilePath = Combine( tMaxBitmapDirectory, tTexturePath );

				if( !sgTexture->GetImageData().IsNull() )
				{
					// Embedded data, should be written to file
					sgTexture->SetFilePath( LPCTSTRToConstCharPtr( tTexturePath.c_str() ) );
					if( sgTexture->ExportImageData() )
					{
						tTexturePath = ConstCharPtrToLPCTSTR( sgTexture->GetFilePath().c_str() );
						sgTexture->SetImageData( nullptr );
					}
				}

				if( this->copyTextures )
				{
					// the name of the imported texture is based on the name of the node
					std::basic_string<TCHAR> tImportTextureName = tTextureName + GetExtensionOfFile( tTexturePath );
					ReplaceInvalidCharacters( tImportTextureName, '_' );
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

						return false;
					}
				}

				PBBitmap* newPBBitmap = SetupMaxTexture( tTargetFilePath.c_str() );

				if( sgTextureNode->GetUseSRGB() )
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

				this->materialInfoHandler->Add( tMeshNameOverride, tMaterialNameOverride, tChannelName, tTargetFilePath, maxChannel );

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
	try
	{
		// fetch output texture path
		std::basic_string<TCHAR> tBakedTexturesPath = this->GetWorkDirectoryHandler()->GetBakedTexturesPath();
		std::basic_string<TCHAR> tWorkDirectory = this->GetWorkDirectoryHandler()->GetWorkDirectory();
		std::basic_string<TCHAR> tPipelineFilePath = Combine( this->GetWorkDirectoryHandler()->GetWorkDirectory(), _T("sgPipeline.json") );

		std::basic_string<TCHAR> tFinalExternalBatchPath = _T("");

		// if there is a environment path, use it
		std::basic_string<TCHAR> tEnvironmentPath = GetSimplygonEnvironmentVariable( _T( SIMPLYGON_9_PATH ) );
		if( tEnvironmentPath.size() > 0 )
		{
			tFinalExternalBatchPath = tEnvironmentPath;
		}
		else
		{
			std::string sErrorMessage = "Invalid environment path: ";
			sErrorMessage += SIMPLYGON_9_PATH;
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
			std::vector<std::basic_string<TCHAR>> tOutputFileList =
			    processingModule->RunPipelineOnFile( tInputSceneFile, tOutputSceneFile, this->sgPipeline, EPipelineRunMode( this->PipelineRunMode ) );

			this->GetMaterialInfoHandler()->AddProcessedSceneFiles( tOutputFileList );
		}
		else
		{
			// fetch original Simplygon scene
			const spScene sgOriginalScene = this->GetSceneHandler()->sgScene;

			// start process with the given pipeline settings file
			std::vector<spScene> sgProcessedScenes =
			    processingModule->RunPipeline( sgOriginalScene, this->sgPipeline, EPipelineRunMode( this->PipelineRunMode ) );

			// fetch processed Simplygon scene
			this->SceneHandler->sgProcessedScenes = sgProcessedScenes;
		}
	}
	catch( std::exception ex )
	{
		this->LogToWindow( ConstCharPtrToLPCTSTR( ex.what() ), Error, true );
		bProcessingSucceeded = false;
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

// cleanup function
void SimplygonMax::CleanUp()
{
	// Note: If users are using exporting scene using distributed processing and importing the scnee. We want to kepe the mapping around to hook back to soruce
	// materail ids.
	if( this->extractionType != ExtractionType::EXPORT_TO_FILE )
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

	this->SelectedMeshNodes.clear();
	this->ImportedUvNameToMaxIndex.clear();
	this->ImportedMaxIndexToUv.clear();
	this->LoadedTexturePathToID.clear();

	this->MaxBoneToSgBone.clear();
	this->SgBoneToMaxBone.clear();
	this->SgBoneIdToIndex.clear();

	this->MaxSgNodeMap.clear();
	this->SgMaxNodeMap.clear();
	this->ImportedTextures.clear();
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
                                                           StdMat2* mMaxStdMaterial,
                                                           std::basic_string<TCHAR> tChannelName )
{
	std::basic_string<TCHAR> maxMaterialName = mMaxStdMaterial->GetName();

	for( size_t c = 0; c < materialColorOverrides->size(); ++c )
	{
		std::basic_string<TCHAR> maxChannelName = materialColorOverrides->at( c ).MappingChannelName;

		if( maxMaterialName == materialColorOverrides->at( c ).MaterialName && maxChannelName == tChannelName )
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

	std::basic_string<TCHAR> tMappedChannelName = mMaxStdMaterial->GetSubTexmapSlotName( maxChannel );
	ReplaceInvalidCharacters( tMappedChannelName, _T( '_' ) );

	// assign color
	switch( maxChannelId )
	{
		case ID_AM:
		{
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, mMaxStdMaterial, tMappedChannelName );

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
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, mMaxStdMaterial, tMappedChannelName );

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
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, mMaxStdMaterial, tMappedChannelName );

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
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, mMaxStdMaterial, tMappedChannelName );

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
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, mMaxStdMaterial, tMappedChannelName );

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
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, mMaxStdMaterial, tMappedChannelName );

			if( colorOverride != nullptr )
			{
				// shading network
				return CreateColorShadingNetwork( colorOverride->GetR(), colorOverride->GetG(), colorOverride->GetB() );
			}

			// shading network
			return nullptr;
		}

		case ID_OP:
		{
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, mMaxStdMaterial, tMappedChannelName );

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
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, mMaxStdMaterial, tMappedChannelName );

			if( colorOverride != nullptr )
			{
				// shading network
				return CreateColorShadingNetwork( colorOverride->GetR(), colorOverride->GetG(), colorOverride->GetB() );
			}

			// shading network
			return nullptr;
		}

		case ID_BU:
		{
			// shading network
			return nullptr;
		}

		case ID_RL:
		{
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, mMaxStdMaterial, tMappedChannelName );

			if( colorOverride != nullptr )
			{
				// shading network
				return CreateColorShadingNetwork( colorOverride->GetR(), colorOverride->GetG(), colorOverride->GetB() );
			}

			// shading network
			return nullptr;
		}

		case ID_RR:
		{
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, mMaxStdMaterial, tMappedChannelName );

			if( colorOverride != nullptr )
			{
				// shading network
				return CreateColorShadingNetwork( colorOverride->GetR(), colorOverride->GetG(), colorOverride->GetB() );
			}

			// shading network
			return nullptr;
		}

		case ID_DP:
		{
			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, mMaxStdMaterial, tMappedChannelName );

			if( colorOverride != nullptr )
			{
				// shading network
				return CreateColorShadingNetwork( colorOverride->GetR(), colorOverride->GetG(), colorOverride->GetB() );
			}

			// shading network
			return nullptr;
		}

		default:
		{
			std::basic_string<TCHAR> tChannelName = ConstCharPtrToLPCTSTR( cChannelName );
			ReplaceInvalidCharacters( tChannelName, _T( '_' ) );

			MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &materialColorOverrides, mMaxStdMaterial, tChannelName );

			if( colorOverride != nullptr )
			{
				// shading network
				return CreateColorShadingNetwork( colorOverride->GetR(), colorOverride->GetG(), colorOverride->GetB() );
			}
			// unsupported color, just set color components to one for now...
			return nullptr;
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

void SimplygonMax::CreateShadingNetworkForStdMaterial( long maxChannelId,
                                                       StdMat2* mMaxStdMaterial,
                                                       spMaterial sgMaterial,
                                                       std::basic_string<TCHAR> tMaxMappingChannel,
                                                       std::basic_string<TCHAR> tMappedChannelName,
                                                       std::basic_string<TCHAR> tFilePath,
                                                       float blendAmount,
                                                       bool bIsSRGB,
                                                       BitmapTex* mBitmapTex,
                                                       bool* hasTextures )
{
	const char* cChannelName = LPCTSTRToConstCharPtr( tMappedChannelName.c_str() );

	// import texture
	std::basic_string<TCHAR> tTexturePathWithName = this->ImportTexture( std::basic_string<TCHAR>( tFilePath ) );
	std::basic_string<TCHAR> tTextureName = GetTitleOfFile( tTexturePathWithName );
	std::basic_string<TCHAR> tTextureExtension = GetExtensionOfFile( tTexturePathWithName );
	std::basic_string<TCHAR> tTextureNameWithExtension = tTextureName + tTextureExtension;

	Color mBaseColor( 1.0f, 1.0f, 1.0f );
	
	spShadingTextureNode sgTextureNode = CreateTextureNode( mBitmapTex, tMaxMappingChannel, tTextureName, this->CurrentTime, bIsSRGB );

	spShadingColorNode sgColorNode = sg->CreateShadingColorNode();
	sgColorNode->SetColor( mBaseColor.r, mBaseColor.g, mBaseColor.b, 1.0f );

	spShadingColorNode sgBlendAmountNode = sg->CreateShadingColorNode();
	sgBlendAmountNode->SetColor( blendAmount, blendAmount, blendAmount, 1.0f );

	spShadingNode sgExitNode = nullptr;

	MaterialColorOverride* colorOverride = HasMaterialColorOverrideForChannel( &MaterialColorOverrides, mMaxStdMaterial, tMappedChannelName );

	// copy material colors
	if( maxChannelId == ID_AM )
	{
		mBaseColor = colorOverride ? Color( colorOverride->ColorValue ) : mMaxStdMaterial->GetAmbient( this->MaxInterface->GetTime() );
		float alpha = colorOverride ? colorOverride->ColorValue[ 3 ] : 1.0f;

		sgColorNode->SetColor( mBaseColor.r, mBaseColor.g, mBaseColor.b, alpha );

		spShadingInterpolateNode sgInterpolationNode = sg->CreateShadingInterpolateNode();
		sgInterpolationNode->SetInput( 0, sgColorNode );
		sgInterpolationNode->SetInput( 1, sgTextureNode );
		sgInterpolationNode->SetInput( 2, sgBlendAmountNode );

		sgExitNode = (spShadingNode)sgInterpolationNode;
	}
	else if( maxChannelId == ID_DI )
	{
		mBaseColor = colorOverride ? Color( colorOverride->ColorValue ) : mMaxStdMaterial->GetDiffuse( this->MaxInterface->GetTime() );
		float alpha = colorOverride ? colorOverride->ColorValue[ 3 ] : 1.0f;

		sgColorNode->SetColor( mBaseColor.r, mBaseColor.g, mBaseColor.b, alpha );

		spShadingInterpolateNode sgInterpolationNode = sg->CreateShadingInterpolateNode();
		sgInterpolationNode->SetInput( 0, sgColorNode );
		sgInterpolationNode->SetInput( 1, sgTextureNode );
		sgInterpolationNode->SetInput( 2, sgBlendAmountNode );

		sgExitNode = (spShadingNode)sgInterpolationNode;
	}
	else if( maxChannelId == ID_SP )
	{
		mBaseColor = colorOverride ? Color( colorOverride->ColorValue ) : mMaxStdMaterial->GetSpecular( this->MaxInterface->GetTime() );
		float shininess = colorOverride ? colorOverride->ColorValue[ 3 ] : mMaxStdMaterial->GetShininess( this->MaxInterface->GetTime() ) * 128.f;

		sgColorNode->SetColor( mBaseColor.r, mBaseColor.g, mBaseColor.b, shininess );

		spShadingInterpolateNode sgInterpolationNode = sg->CreateShadingInterpolateNode();
		sgInterpolationNode->SetInput( 0, sgColorNode );
		sgInterpolationNode->SetInput( 1, sgTextureNode );
		sgInterpolationNode->SetInput( 2, sgBlendAmountNode );

		sgExitNode = (spShadingNode)sgInterpolationNode;
	}
	else if( maxChannelId == ID_SH )
	{
		mBaseColor = colorOverride ? Color( colorOverride->ColorValue ) : mBaseColor;
		float alpha = colorOverride ? colorOverride->ColorValue[ 3 ] : 1.0f;

		sgColorNode->SetColor( mBaseColor.r, mBaseColor.g, mBaseColor.b, alpha );

		spShadingInterpolateNode sgInterpolationNode = sg->CreateShadingInterpolateNode();
		sgInterpolationNode->SetInput( 0, sgColorNode );
		sgInterpolationNode->SetInput( 1, sgTextureNode );
		sgInterpolationNode->SetInput( 2, sgBlendAmountNode );

		sgExitNode = (spShadingNode)sgInterpolationNode;
	}
	else if( maxChannelId == ID_SS )
	{
		mBaseColor = colorOverride ? Color( colorOverride->ColorValue ) : mBaseColor;
		float alpha = colorOverride ? colorOverride->ColorValue[ 3 ] : 1.0f;

		sgColorNode->SetColor( mBaseColor.r, mBaseColor.g, mBaseColor.b, alpha );

		spShadingInterpolateNode sgInterpolationNode = sg->CreateShadingInterpolateNode();
		sgInterpolationNode->SetInput( 0, sgColorNode );
		sgInterpolationNode->SetInput( 1, sgTextureNode );
		sgInterpolationNode->SetInput( 2, sgBlendAmountNode );

		sgExitNode = (spShadingNode)sgInterpolationNode;
	}
	else if( maxChannelId == ID_SI )
	{
		mBaseColor = colorOverride ? Color( colorOverride->ColorValue ) : mMaxStdMaterial->GetSelfIllumColor( this->MaxInterface->GetTime() );
		float alpha = colorOverride ? colorOverride->ColorValue[ 3 ] : mMaxStdMaterial->GetSelfIllum( this->MaxInterface->GetTime() );

		sgColorNode->SetColor( mBaseColor.r, mBaseColor.g, mBaseColor.b, alpha );

		spShadingInterpolateNode sgInterpolationNode = sg->CreateShadingInterpolateNode();
		sgInterpolationNode->SetInput( 0, sgColorNode );
		sgInterpolationNode->SetInput( 1, sgTextureNode );
		sgInterpolationNode->SetInput( 2, sgBlendAmountNode );

		sgExitNode = (spShadingNode)sgInterpolationNode;
	}
	else if( maxChannelId == ID_OP )
	{
		float opacity = mMaxStdMaterial->GetOpacity( this->MaxInterface->GetTime() );

		mBaseColor = colorOverride ? Color( colorOverride->ColorValue ) : Color( opacity, opacity, opacity );
		float alpha = colorOverride ? colorOverride->ColorValue[ 3 ] : opacity;

		sgColorNode->SetColor( mBaseColor.r, mBaseColor.g, mBaseColor.b, alpha );
		spShadingInterpolateNode sgInterpolationNode = sg->CreateShadingInterpolateNode();
		sgInterpolationNode->SetInput( 0, sgColorNode );
		sgInterpolationNode->SetInput( 1, sgTextureNode );
		sgInterpolationNode->SetInput( 2, sgBlendAmountNode );

		spShadingSwizzlingNode sgSwizzleNode = sg->CreateShadingSwizzlingNode();
		sgSwizzleNode->SetInput( 0, sgInterpolationNode );
		sgSwizzleNode->SetInput( 1, sgInterpolationNode );
		sgSwizzleNode->SetInput( 2, sgInterpolationNode );
		sgSwizzleNode->SetInput( 3, sgInterpolationNode );

		bool bUseAlphaAsTransparency = false;
		Texmap* mTexMap = mMaxStdMaterial->GetSubTexmap( mMaxStdMaterial->StdIDToChannel( maxChannelId ) );
		if( mTexMap )
		{
			BitmapTex* mBitmapTex = (BitmapTex*)mTexMap;
			if( mBitmapTex )
			{
				bUseAlphaAsTransparency = mBitmapTex->GetAlphaAsRGB( TRUE ) == TRUE;
			}
		}

		const bool bTextureHasAlpha = TextureHasAlpha( LPCTSTRToConstCharPtr( tTexturePathWithName.c_str() ) );

		if( bTextureHasAlpha && bUseAlphaAsTransparency )
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

		sgExitNode = (spShadingNode)sgSwizzleNode;
	}
	else if( maxChannelId == ID_FI )
	{
		mBaseColor = colorOverride ? Color( colorOverride->ColorValue ) : mBaseColor;
		float alpha = colorOverride ? colorOverride->ColorValue[ 3 ] : 1.0f;

		sgColorNode->SetColor( mBaseColor.r, mBaseColor.g, mBaseColor.b, alpha );

		spShadingInterpolateNode sgInterpolationNode = sg->CreateShadingInterpolateNode();
		sgInterpolationNode->SetInput( 0, sgColorNode );
		sgInterpolationNode->SetInput( 1, sgTextureNode );
		sgInterpolationNode->SetInput( 2, sgBlendAmountNode );

		sgExitNode = (spShadingNode)sgInterpolationNode;
	}
	else if( maxChannelId == ID_BU )
	{
		// ignore for now
	}
	else if( maxChannelId == ID_RL )
	{
		mBaseColor = colorOverride ? Color( colorOverride->ColorValue ) : mBaseColor;
		float alpha = colorOverride ? colorOverride->ColorValue[ 3 ] : 1.0f;

		sgColorNode->SetColor( mBaseColor.r, mBaseColor.g, mBaseColor.b, alpha );

		spShadingInterpolateNode sgInterpolationNode = sg->CreateShadingInterpolateNode();
		sgInterpolationNode->SetInput( 0, sgColorNode );
		sgInterpolationNode->SetInput( 1, sgTextureNode );
		sgInterpolationNode->SetInput( 2, sgBlendAmountNode );

		sgExitNode = (spShadingNode)sgInterpolationNode;
	}
	else if( maxChannelId == ID_RR )
	{
		mBaseColor = colorOverride ? Color( colorOverride->ColorValue ) : mBaseColor;
		float alpha = colorOverride ? colorOverride->ColorValue[ 3 ] : 1.0f;

		sgColorNode->SetColor( mBaseColor.r, mBaseColor.g, mBaseColor.b, alpha );

		spShadingInterpolateNode sgInterpolationNode = sg->CreateShadingInterpolateNode();
		sgInterpolationNode->SetInput( 0, sgColorNode );
		sgInterpolationNode->SetInput( 1, sgTextureNode );
		sgInterpolationNode->SetInput( 2, sgBlendAmountNode );

		sgExitNode = (spShadingNode)sgInterpolationNode;
	}
	else if( maxChannelId == ID_DP )
	{
		mBaseColor = colorOverride ? Color( colorOverride->ColorValue ) : mBaseColor;
		float alpha = colorOverride ? colorOverride->ColorValue[ 3 ] : 1.0f;

		sgColorNode->SetColor( mBaseColor.r, mBaseColor.g, mBaseColor.b, alpha );

		spShadingInterpolateNode sgInterpolationNode = sg->CreateShadingInterpolateNode();
		sgInterpolationNode->SetInput( 0, sgColorNode );
		sgInterpolationNode->SetInput( 1, sgTextureNode );
		sgInterpolationNode->SetInput( 2, sgBlendAmountNode );

		sgExitNode = (spShadingNode)sgInterpolationNode;
	}
	else
	{
		sgColorNode->SetColor( mBaseColor.r, mBaseColor.g, mBaseColor.b, 1.0f );

		spShadingInterpolateNode sgInterpolationNode = sg->CreateShadingInterpolateNode();
		sgInterpolationNode->SetInput( 0, sgColorNode );
		sgInterpolationNode->SetInput( 1, sgTextureNode );
		sgInterpolationNode->SetInput( 2, sgBlendAmountNode );

		sgExitNode = (spShadingNode)sgInterpolationNode;
	}

	// export xml string from shading network material
	if( maxChannelId == ID_BU )
	{
		sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );
	}
	else
	{
		if( sgExitNode != nullptr )
		{
			sgMaterial->SetShadingNetwork( cChannelName, sgExitNode );
		}
	}

	// create texture and add it to scene
	spTexture sgTexture = nullptr;

	// do lookup in case this texture is already in use
	std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& tTextureIterator = this->LoadedTexturePathToID.find( tTexturePathWithName );
	const bool bTextureInUse = ( tTextureIterator != this->LoadedTexturePathToID.end() );

	if( bTextureInUse )
	{
		sgTexture = this->SceneHandler->sgScene->GetTextureTable()->FindTextureUsingPath( LPCTSTRToConstCharPtr( tTexturePathWithName.c_str() ) );
	}
	else
	{
		sgTexture = sg->CreateTexture();
		sgTexture->SetName( LPCTSTRToConstCharPtr( tTextureName.c_str() ) );
		sgTexture->SetFilePath( LPCTSTRToConstCharPtr( tTexturePathWithName.c_str() ) );

		this->SceneHandler->sgScene->GetTextureTable()->AddTexture( sgTexture );
	}

	// if the texture was not already in scene, copy it to local work folder
	if( !bTextureInUse )
	{
		const spString rTexturePathWithName = sgTexture->GetFilePath();
		this->LoadedTexturePathToID.insert(
		    std::pair<std::basic_string<TCHAR>, std::basic_string<TCHAR>>( tTexturePathWithName, ConstCharPtrToLPCTSTR( rTexturePathWithName.c_str() ) ) );
	}

	if( maxChannelId == ID_BU )
	{
		sgMaterial->SetUseTangentSpaceNormals( true );
	}

	hasTextures[ maxChannelId ] = true;
}

void SimplygonMax::LogMaterialNodeMessage( Texmap* mTexMap, std::basic_string<TCHAR> tMaterialName, std::basic_string<TCHAR> tChannelName )
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
		LogToWindow( tMaterialName + _T(" (") + tChannelName + _T(") - ") + std::basic_string<TCHAR>( nodeClassName ) + _T(" texture node is not supported."),
		             Warning );
	}
}

// creates Simplygon material channels based on the channels in the Max StdMaterial
void SimplygonMax::CreateSgMaterialChannel( long maxChannelId, StdMat2* mMaxStdMaterial, spMaterial sgMaterial, bool* hasTextures )
{
	const spString rMaterialName = sgMaterial->GetName();
	const char* cMaterialName = rMaterialName.c_str();
	std::string sMaterialName = cMaterialName;
	std::basic_string<TCHAR> tMaterialName = ConstCharPtrToLPCTSTR( cMaterialName );

	const long maxChannel = mMaxStdMaterial->StdIDToChannel( maxChannelId );

	std::basic_string<TCHAR> tChannelName = mMaxStdMaterial->GetSubTexmapSlotName( maxChannel );
	ReplaceInvalidCharacters( tChannelName, _T( '_' ) );

	// add material channel
	const char* cChannelName = LPCTSTRToConstCharPtr( tChannelName.c_str() );
	if( !sgMaterial->HasMaterialChannel( cChannelName ) )
	{
		sgMaterial->AddMaterialChannel( cChannelName );
	}

	Texmap* mTexMap = mMaxStdMaterial->GetSubTexmap( maxChannel );
	const float blendAmount = mMaxStdMaterial->GetTexmapAmt( maxChannel, this->MaxInterface->GetTime() );

	if( mTexMap && ( mTexMap->ClassID() == Class_ID( BMTEX_CLASS_ID, 0 ) || ( mTexMap->ClassID() == GNORMAL_CLASS_ID && mTexMap->GetSubTexmap( 0 ) ) ) )
	{
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
			mBitmapTex = nullptr;

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
					LogMaterialNodeMessage( mTexMap, tMaterialName, tChannelName );
				}
			}
		}
		else
		{
			LogMaterialNodeMessage( mTexMap, tMaterialName, tChannelName );
		}

		// if we have an actual texture node,
		// fetch gama, texture path, mapping channel,
		// and create representing shading network.
		if( mBitmapTex )
		{
			bool bIsSRGB = false;

			// change sRGB based on gamma
			const float gamma = GetBitmapTextureGamma( mBitmapTex );
			if( gamma <= 2.21000000f && gamma >= 2.19000000f )
			{
				bIsSRGB = true;
			}

			// retrieve the texture file path
			TCHAR tTexturePath[ MAX_PATH ] = { 0 };
			GetImageFullFilePath( mBitmapTex->GetMapName(), tTexturePath );

			// if the texture path was found
			if( _tcslen( tTexturePath ) > 0 )
			{
				// if normal map, disable sRGB
				if( maxChannelId == ID_BU )
				{
					bIsSRGB = false;
				}

				// set mapping channel to 1
				std::basic_string<TCHAR> tMaxMappingChannel = _T("1");

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

				std::basic_string<TCHAR> tTexturePathOverride = _T("");
				for( size_t channelIndex = 0; channelIndex < this->MaterialTextureOverrides.size(); ++channelIndex )
				{
					const MaterialTextureOverride& textureOverride = this->MaterialTextureOverrides[ channelIndex ];
					if( strcmp( cMaterialName, LPCTSTRToConstCharPtr( textureOverride.MaterialName.c_str() ) ) == 0 )
					{
						if( strcmp( cChannelName, LPCTSTRToConstCharPtr( textureOverride.MappingChannelName.c_str() ) ) == 0 )
						{
							tTexturePathOverride = textureOverride.TextureFileName;
							break;
						}
					}
				}

				if( tTexturePathOverride.length() > 0 )
				{
					_stprintf_s( tTexturePath, _T("%s"), tTexturePathOverride.c_str() );
				}

				CreateShadingNetworkForStdMaterial( maxChannelId,
				                                    mMaxStdMaterial,
				                                    sgMaterial,
				                                    tMaxMappingChannel,
				                                    tChannelName,
				                                    tTexturePath,
				                                    blendAmount,
				                                    bIsSRGB,
				                                    mBitmapTex,
				                                    hasTextures );
			}
		}
	}
	else
	{
		// if there is a texmap of unsupported type, output warning
		if( mTexMap )
		{
			LogMaterialNodeMessage( mTexMap, tMaterialName, tChannelName );
		}

		// check for material overrides
		TCHAR tTexturePath[ MAX_PATH ] = { 0 };

		// disable sRGB if normal map
		bool bIsSRGB = maxChannelId != ID_BU;
		const float gamma = 1.0f;

		std::basic_string<TCHAR> tTexturePathOverride = _T("");
		for( size_t channelIndex = 0; channelIndex < this->MaterialTextureOverrides.size(); ++channelIndex )
		{
			const MaterialTextureOverride& textureOverride = this->MaterialTextureOverrides[ channelIndex ];
			if( strcmp( cMaterialName, LPCTSTRToConstCharPtr( textureOverride.MaterialName.c_str() ) ) == 0 )
			{
				if( strcmp( cChannelName, LPCTSTRToConstCharPtr( textureOverride.MappingChannelName.c_str() ) ) == 0 )
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

			// else, use standard channel
			else
			{
				const int maxMappingChannel = 1;
				TCHAR tMappingChannelBuffer[ MAX_PATH ] = { 0 };
				_stprintf_s( tMappingChannelBuffer, _T("%d"), maxMappingChannel );

				tMaxMappingChannel = tMappingChannelBuffer;
			}

			_stprintf_s( tTexturePath, _T("%s"), tTexturePathOverride.c_str() );

			CreateShadingNetworkForStdMaterial(
			    maxChannelId, mMaxStdMaterial, sgMaterial, tMaxMappingChannel, tChannelName, tTexturePath, blendAmount, bIsSRGB, nullptr, hasTextures );
		}
	}

	if( !hasTextures[ maxChannelId ] )
	{
		spShadingColorNode sgColorNode =
		    AssignMaxColorToSgMaterialChannel( sgMaterial, cChannelName, mMaxStdMaterial, this->MaxInterface, maxChannelId, MaterialColorOverrides );

		if( !sgColorNode.IsNull() && !MaterialChannelHasShadingNetwork( sgMaterial, cChannelName ) )
		{
			sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
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
							sgTextureNode->SetUseSRGB( nodeProxy->isSRGB );
						else
							sgTextureNode->SetUseSRGB( bIsSRGB );

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
					const bool sgsdkUseSRGB = sgTextureNode->GetUseSRGB();
					const uint sgsdkParamCount = sgTextureNode->GetParameterCount();

					// create texture and add it to scene
					spTexture sgTexture = nullptr;

					// do lookup in case this texture is already in use
					std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>>::const_iterator& textureIterator =
					    this->LoadedTexturePathToID.find( tTexturePath );
					const bool bTextureInUse = ( textureIterator != this->LoadedTexturePathToID.end() );

					if( bTextureInUse )
					{
						sgTexture = this->SceneHandler->sgScene->GetTextureTable()->FindTextureUsingPath( LPCTSTRToConstCharPtr( tTexturePath.c_str() ) );
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
				this->CreateSgMaterialChannel( maxChannelId, mMaxStdMaterial, sgMaterial, bTextureChannelsInUse );
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
			textureData[ ( x + y * TEXTURE_WIDTH ) * 3 + 0 ] = ( x * 0xff ) / TEXTURE_WIDTH;
			if( ( ( ( x >> 3 ) & 0x1 ) ^ ( ( y >> 3 ) & 0x1 ) ) )
			{
				textureData[ ( x + y * TEXTURE_WIDTH ) * 3 + 1 ] = 0;
			}
			else
			{
				textureData[ ( x + y * TEXTURE_WIDTH ) * 3 + 1 ] = 0xff;
			}
			textureData[ ( x + y * TEXTURE_WIDTH ) * 3 + 2 ] = ( y * 0xff ) / TEXTURE_HEIGHT;
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

			tWarningMessage += _T("Warning: Failed to import texture: ");
			tWarningMessage += tSourcePath.c_str();
			tWarningMessage += _T(", using a stand-in texture");

			this->LogMessageToScriptEditor( tWarningMessage );

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

	return nullptr;
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

	return nullptr;
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
	object.p4 = new Point4( value );
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
	spTexture sgTexture = nullptr;

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
			sgTexture->SetFilePath( LPCTSTRToConstCharPtr( tTexturePath.c_str() ) );
			if( sgTexture->ExportImageData() )
			{
				tTexturePath = ConstCharPtrToLPCTSTR( sgTexture->GetFilePath().c_str() );
				sgTexture->SetImageData( nullptr );
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

// gets upstream shading texture node in the specified shading-network
spShadingTextureNode SimplygonMax::FindUpstreamTextureNode( spShadingNode sgShadingNode )
{
	if( sgShadingNode.IsNull() )
		return nullptr;

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

	return nullptr;
}

// gets upstream shading color node in the specified shading-network
spShadingColorNode SimplygonMax::FindUpstreamColorNode( spShadingNode sgShadingNode )
{
	if( sgShadingNode.IsNull() )
		return nullptr;

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

	return nullptr;
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
		std::basic_string<TCHAR> tChannelName = mMaxStdMaterial->GetSubTexmapSlotName( maxChannel );
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

	for( uint channelIndex = 0; channelIndex < numMaterialChannels; ++channelIndex )
	{
		spString rChannelName = sgMaterial->GetMaterialChannelFromIndex( channelIndex );
		const char* cChannelName = rChannelName.c_str();
		std::basic_string<TCHAR> tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

		BitmapTex* mBitmapTex = nullptr;

		const bool bIsNormalChannel = ( tChannelName == _T("bump") || tChannelName == _T("coat_bump") );

		const bool bResult = this->ImportMaterialTexture(
		    sgProcessedScene, sgMaterial, tNodeName, tChannelName.c_str(), bIsNormalChannel ? ID_BU : -1, &mBitmapTex, tMeshName, tLodName );

		if( !bResult || !mBitmapTex )
		{
			std::basic_string<TCHAR> tErrorMessage = _T("SetupPhysicalMaterial: Failed to import a texture for ");
			tErrorMessage += tChannelName;
			tErrorMessage += _T(" channel.");

			this->LogToWindow( tErrorMessage, Warning );
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
			// disable emission rendering for now due to weird behavior
			SetShaderParameter( mMaxPhysicalMaterial, _T("emission"), 0.f );
			SetShaderParameter( mMaxPhysicalMaterial, _T("emission_map_on"), false );

			SetShaderParameter( mMaxPhysicalMaterial, _T("emit_color"), Point4( 0.f, 0.f, 0.f, 1.f ) );
			SetShaderParameter( mMaxPhysicalMaterial, _T("emit_color_map_on"), false );

			mMaxPhysicalMaterial->SetSubTexmap( 16, mBitmapTex );
		}
		else if( tChannelName == _T("emit_color") )
		{
			// disable emission rendering for now due to weird behavior
			SetShaderParameter( mMaxPhysicalMaterial, _T("emission"), 0.f );
			SetShaderParameter( mMaxPhysicalMaterial, _T("emission_map_on"), false );

			SetShaderParameter( mMaxPhysicalMaterial, _T("emit_color"), Point4( 0.f, 0.f, 0.f, 1.f ) );
			SetShaderParameter( mMaxPhysicalMaterial, _T("emit_color_map_on"), false );

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

		// convert to trimesh, get the object
		TriObject* tobj = (TriObject*)os.obj->ConvertToType( this->CurrentTime, triObjectClassID );

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
		const float fov_horizontal = active_viewport.GetFOV();
		const float fov_verticle = 2 * atan( tan( RAD2DEG( fov_horizontal ) / 2 ) * ( screenwidth / screenheight ) );

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

		// convert to trimesh, get the object
		TriObject* tobj = (TriObject*)os.obj->ConvertToType( this->CurrentTime, triObjectClassID );

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
		float fov_horizontal = active_viewport.GetFOV();
		float fov_verticle = 2 * atan( tan( RAD2DEG( fov_horizontal ) / 2 ) * ( screenwidth / screenheight ) );

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
