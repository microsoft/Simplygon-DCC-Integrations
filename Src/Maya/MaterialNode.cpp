// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"

#include "MaterialNode.h"
#include "BakedMaterial.h"
#include "MeshNode.h"

#include "SimplygonCmd.h"
#include "WorkDirectoryHandler.h"
#include "HelperFunctions.h"
#include "Common.h"
#include <map>

inline MStatus ExecuteCommand( const MString& cmd, MStringArray& dest )
{
#ifdef _DEBUG
	if( !MGlobal::executeCommand( cmd, dest, true ) )
#else
	if( !MGlobal::executeCommand( cmd, dest, false ) )
#endif
	{
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MaterialNode::MaterialNode( SimplygonCmd* cmd, MaterialHandler* materialHandler )
{
	this->materialHandler = materialHandler;
	this->cmd = cmd;
	this->IsBasedOnSimplygonShadingNetwork = false;

	// setup a default material using nullptr object
	this->Name = "SimplygonDefaultMaterial";
	this->MaterialObject = MObject::kNullObj;
	this->shadingNetworkData = nullptr;

	this->sgMaterial = sg->CreateMaterial();
	this->sgMaterial->SetBlendMode( EMaterialBlendMode::Blend );

	// set default material
	this->AmbientValue.ColorValue[ 0 ] = 0.f;
	this->AmbientValue.ColorValue[ 1 ] = 0.f;
	this->AmbientValue.ColorValue[ 2 ] = 0.f;
	this->AmbientValue.ColorValue[ 3 ] = 1.f;
	this->ColorValue.ColorValue[ 0 ] = 0.8f;
	this->ColorValue.ColorValue[ 1 ] = 0.8f;
	this->ColorValue.ColorValue[ 2 ] = 0.8f;
	this->ColorValue.ColorValue[ 3 ] = 1.f;
	this->SpecularValue.ColorValue[ 0 ] = 0.f;
	this->SpecularValue.ColorValue[ 1 ] = 0.f;
	this->SpecularValue.ColorValue[ 2 ] = 0.f;
	this->SpecularValue.ColorValue[ 3 ] = 0.f;
	this->IncandescenceValue.ColorValue[ 0 ] = 0.f;
	this->IncandescenceValue.ColorValue[ 1 ] = 0.f;
	this->IncandescenceValue.ColorValue[ 2 ] = 0.f;
	this->IncandescenceValue.ColorValue[ 3 ] = 0.f;
	this->TransparencyValue.ColorValue[ 0 ] = 1.f;
	this->TransparencyValue.ColorValue[ 1 ] = 1.f;
	this->TransparencyValue.ColorValue[ 2 ] = 1.f;
	this->TransparencyValue.ColorValue[ 3 ] = 1.f;
	this->TranslucenceValue.ColorValue[ 0 ] = 0.f;
	this->TranslucenceValue.ColorValue[ 1 ] = 0.f;
	this->TranslucenceValue.ColorValue[ 2 ] = 0.f;
	this->TranslucenceValue.ColorValue[ 3 ] = 0.f;
	this->TranslucenceDepthValue.ColorValue[ 0 ] = 0.f;
	this->TranslucenceDepthValue.ColorValue[ 1 ] = 0.f;
	this->TranslucenceDepthValue.ColorValue[ 2 ] = 0.f;
	this->TranslucenceDepthValue.ColorValue[ 3 ] = 0.f;
	this->TranslucenceFocusValue.ColorValue[ 0 ] = 0.f;
	this->TranslucenceFocusValue.ColorValue[ 1 ] = 0.f;
	this->TranslucenceFocusValue.ColorValue[ 2 ] = 0.f;
	this->TranslucenceFocusValue.ColorValue[ 3 ] = 0.f;
}

MaterialNode::~MaterialNode()
{
	UserTextures.clear();

	if( shadingNetworkData != nullptr )
		delete shadingNetworkData;
}

///////////////////////////////////////////////////////////////////////////////////

MStatus MaterialNode::SetupFromName( MString mMaterialName )
{
	this->Name = mMaterialName;
	std::string sMaterialName = mMaterialName.asChar();

	// find the material from the name
	if( !GetMObjectOfNamedObject( Name, this->MaterialObject ) )
	{
		MGlobal::displayError( "MaterialNode::SetupFromName: failed to find a named object" );
		return MStatus::kFailure;
	}

	MStatus mStatus;
	MFnDependencyNode mShadingGroup( this->MaterialObject, &mStatus );
	MObject mShaderGroup = GetConnectedNamedPlug( mShadingGroup, "surfaceShader" );
	MFnDependencyNode mShaderGroupDependencyNode( mShaderGroup );
	std::basic_string<TCHAR> tMaterialName( mShaderGroupDependencyNode.name().asChar() );

	this->sgMaterial->SetName( tMaterialName.c_str() );

	this->cmd->s_GlobalMaterialDagPathToGuid.insert( std::pair<std::string, std::string>( sMaterialName, tMaterialName ) );
	this->cmd->s_GlobalMaterialGuidToDagPath.insert( std::pair<std::string, std::string>( tMaterialName, sMaterialName ) );

	IsBasedOnSimplygonShadingNetwork = false;

	// if material network has xml network defined
	if( this->materialHandler->HasMaterialWithXMLNetworks( tMaterialName ) )
	{
		// get simplygonShadingNetwrokMaterial  and setup relevant parts
		this->shadingNetworkData = this->materialHandler->GetMaterialWithShadingNetworks( tMaterialName );

		// create shading network data object and insert into the lookup
		const int numMaterialChannels = shadingNetworkData->sgMaterial->GetMaterialChannelCount();
		for( int i = 0; i < numMaterialChannels; i++ )
		{
			std::basic_string<TCHAR> tMaterialChannelName( shadingNetworkData->sgMaterial->GetMaterialChannelFromIndex( i ) );
			spShadingNode sgExitNode = shadingNetworkData->sgMaterial->GetShadingNetwork( tMaterialChannelName.c_str() );

			if( !sgExitNode.IsNull() )
			{
				shadingNetworkData->ChannelToShadingNetworkMap[ tMaterialChannelName ] = new ShadingPerChannelData();
				shadingNetworkData->ChannelToShadingNetworkMap[ tMaterialChannelName ]->sgExitNode = sgExitNode;
				// Extract Texture and Color nodes form the Simplygon Material
				// TODO:: keep a separate lookups for each channel or a global lookup
				this->materialHandler->FindAllUpStreamTextureNodes( sgExitNode,
				                                                    shadingNetworkData->ChannelToShadingNetworkMap[ tMaterialChannelName ]->TextureNodeLookup );
				this->materialHandler->FindAllUpStreamColorNodes( sgExitNode,
				                                                  shadingNetworkData->ChannelToShadingNetworkMap[ tMaterialChannelName ]->ColorNodeLookup );
				IsBasedOnSimplygonShadingNetwork = true;
			}
		}

		return InternalSetupConnectNetworkNodes();
	}
	else
		return this->InternalSetup();
}

spShadingNode MaterialNode::FindUpstreamNode( spShadingNode sgShadingNode, const char* cNodeName )
{
	if( sgShadingNode.IsNull() )
		return Simplygon::NullPtr;

	if( !sgShadingNode.IsNull() && strcmp( sgShadingNode->GetName(), cNodeName ) )
	{
		return sgShadingNode;
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
					spShadingNode sgUpstreamNode = FindUpstreamNode( sgFilterNode->GetInput( i ), cNodeName );
					if( !sgUpstreamNode.IsNull() && strcmp( sgUpstreamNode->GetName(), cNodeName ) )
						return sgUpstreamNode;
				}
			}
		}
	}
	return Simplygon::NullPtr;
}

inline MPlug GetConnectedUpstreamPlug( const MPlug& mPlug )
{
	if( mPlug.isNull() )
		return MPlug();

	MPlugArray mConnectedPlugs;
	mPlug.connectedTo( mConnectedPlugs, true, false );
	if( mConnectedPlugs.length() > 0 )
	{
		return mConnectedPlugs[ 0 ];
	}

	return MPlug();
}

inline MObject GetConnectedUpstreamNode( const MPlug& mPlug )
{
	if( mPlug.isNull() )
		return MObject();

	MObject mNode;
	MPlugArray mConnectedPlugs;
	mPlug.connectedTo( mConnectedPlugs, true, false );
	if( mConnectedPlugs.length() > 0 )
	{
		mNode = mConnectedPlugs[ 0 ].node();
	}
	return mNode;
}

inline MObject GetConnectedUpstreamNode( const MPlug& mPlug, int index )
{
	if( mPlug[ index ].isNull() )
		return MObject();

	MObject mNode;
	MPlugArray mConnectedPlugs;
	mPlug[ index ].connectedTo( mConnectedPlugs, true, false );
	if( mConnectedPlugs.length() > 0 )
	{
		mNode = mConnectedPlugs[ 0 ].node();
	}
	return mNode;
}

MStatus MaterialNode::InternalSetupConnectNetworkNodes()
{
	// setup the dependency node function set on the object
	MStatus mStatus;
	MFnDependencyNode mShadingGroup( this->MaterialObject, &mStatus );
	if( !mStatus )
	{
		MGlobal::displayError( "MaterialNode::InternalSetup: object is not a dependency graph node" );
		return MStatus::kFailure;
	}

	// find a shader node, if one exists
	MObject mMaterialShaderNode = GetConnectedNamedPlug( mShadingGroup, "surfaceShader" );

	// if no material was found, just return, we will use the default values
	if( mMaterialShaderNode == MObject::kNullObj )
	{
		return MStatus::kSuccess;
	}

	MFnDependencyNode mShaderNode( mMaterialShaderNode, &mStatus );
	if( !mStatus )
	{
		MGlobal::displayError( "MaterialNode::InternalSetup: object is not a dependency graph node" );
		return MStatus::kFailure;
	}

	std::basic_string<TCHAR> tShaderName = mShaderNode.name().asChar();

	// look for shader. if not found return error
	std::map<std::basic_string<TCHAR>, spShadingTextureNode>::iterator textureIterator;
	std::map<std::basic_string<TCHAR>, spShadingColorNode>::iterator colorIterator;
	std::map<std::basic_string<TCHAR>, ShadingPerChannelData*>::iterator channelIterator;

	for( channelIterator = shadingNetworkData->ChannelToShadingNetworkMap.begin(); channelIterator != shadingNetworkData->ChannelToShadingNetworkMap.end();
	     channelIterator++ )
	{
		for( textureIterator = channelIterator->second->TextureNodeLookup.begin(); textureIterator != channelIterator->second->TextureNodeLookup.end();
		     textureIterator++ )
		{
			MString mPlugName = MString( textureIterator->first.c_str() );
			MObject mPlugObject = GetConnectedNamedPlug( mShaderNode, mPlugName );

			if( mPlugObject != MObject::kNullObj )
			{
				if( mPlugObject.apiType() == MFn::kFileTexture )
				{
					MString mDefaultUVSetName( "map1" );
					if( textureIterator->second->GetTexCoordName().NonEmpty() )
					{
						mDefaultUVSetName = textureIterator->second->GetTexCoordName();
					}

					MFnDependencyNode mFileTextureNode( mPlugObject );
					// Required for BinSkim compat
					// TODO: Deprecated method, should be replaced!
					SG_DISABLE_SPECIFIC_BEGIN( 4996 )
					MPlug mTexFilePlug = mFileTextureNode.findPlug( "fileTextureName" );
					SG_DISABLE_SPECIFIC_END
					MString mFileName;

					// if the plug is not null extract the file name
					if( !mTexFilePlug.isNull() )
					{
						mTexFilePlug.getValue( mFileName );

						// Required for BinSkim compat
						// TODO: Deprecated method, should be replaced!
						SG_DISABLE_SPECIFIC_BEGIN( 4996 )
						MPlug mUVSetPlug = mFileTextureNode.findPlug( "uvCoord" );
						SG_DISABLE_SPECIFIC_END
						if( !mUVSetPlug.isNull() )
						{
							MObject mPlace2dTexture = ::GetConnectedUpstreamNode( mUVSetPlug );
							if( !mPlace2dTexture.isNull() )
							{
								MFnDependencyNode mPlace2dTextureNode( mPlace2dTexture );
								// Required for BinSkim compat
								// TODO: Deprecated method, should be replaced!
								SG_DISABLE_SPECIFIC_BEGIN( 4996 )
								mUVSetPlug = mPlace2dTextureNode.findPlug( "uvCoord" );
								SG_DISABLE_SPECIFIC_END

								MObject mUVChooser = ::GetConnectedUpstreamNode( mUVSetPlug );

								if( !mUVChooser.isNull() )
								{
									MFnDependencyNode uvChooserFn( mUVChooser );

									// Required for BinSkim compat
									// TODO: Deprecated method, should be replaced!
									SG_DISABLE_SPECIFIC_BEGIN( 4996 )
									// get the uvSets plug, which is an array of connected nodes
									MPlug mUVSetsPlug = uvChooserFn.findPlug( "uvSets" );
									SG_DISABLE_SPECIFIC_END
									if( !mUVSetsPlug.isNull() )
									{
										for( uint i = 0; i < mUVSetsPlug.numElements(); ++i )
										{
											MPlug mShapePlug = ::GetConnectedUpstreamPlug( mUVSetsPlug[ i ] );

											mDefaultUVSetName = mShapePlug.asString();
										}
									}
								}
								else
								{
									MFnDependencyNode mUVSetNode( mUVSetPlug );

									// Required for BinSkim compat
									// TODO: Deprecated method, should be replaced!
									SG_DISABLE_SPECIFIC_BEGIN( 4996 )
									// get the uvSets plug, which is an array of connected nodes
									MPlug mUVSetsPlug = mUVSetNode.findPlug( "uvSets" );
									SG_DISABLE_SPECIFIC_END
									if( !mUVSetsPlug.isNull() )
									{
										for( uint i = 0; i < mUVSetsPlug.numElements(); ++i )
										{
											MPlug mShapePlug = ::GetConnectedUpstreamPlug( mUVSetsPlug[ i ] );

											mDefaultUVSetName = mShapePlug.asString();
										}
									}
								}
							}
						}
					}

					std::basic_string<TCHAR> tTexturePathWithName = this->materialHandler->ImportTexture( mFileName ).c_str();
					std::basic_string<TCHAR> tTextureName = GetTitleOfFile( tTexturePathWithName );
					std::basic_string<TCHAR> tTextureExtension = GetExtensionOfFile( tTexturePathWithName );
					std::basic_string<TCHAR> tTextureNameWithExtension = tTextureName + tTextureExtension;

					// add extra mapping for post processing of data
					std::map<spShadingTextureNode, std::basic_string<TCHAR>>::iterator nodeToPathIterator =
					    this->ShadingTextureNodeToPath.find( textureIterator->second );
					if( nodeToPathIterator == this->ShadingTextureNodeToPath.end() )
					{
						this->ShadingTextureNodeToPath.insert(
						    std::pair<spShadingTextureNode, std::basic_string<TCHAR>>( textureIterator->second, tTexturePathWithName ) );
					}

					if( tTexturePathWithName.size() > 0 )
					{
						textureIterator->second->SetTextureName( tTextureName.c_str() );
						textureIterator->second->SetTexCoordName( mDefaultUVSetName.asChar() );
					}
				}
				else if( mPlugObject.apiType() == MFn::kBump )
				{
					MFnDependencyNode mBumpNode( mPlugObject );
					const bool bHasAttribute = mBumpNode.hasAttribute( "bumpInterp" );
					if( bHasAttribute )
					{
						MObject mBumpAttibute = mBumpNode.attribute( "bumpInterp" );
						if( !mBumpAttibute.isNull() )
						{
							if( mBumpAttibute.apiType() == MFn::kEnumAttribute )
							{
								int bumpType = 0;

								// Required for BinSkim compat
								// TODO: Deprecated method, should be replaced!
								SG_DISABLE_SPECIFIC_BEGIN( 4996 )
								MPlug mBumpInterpPlug = mBumpNode.findPlug( "bumpInterp" );
								SG_DISABLE_SPECIFIC_END
								if( !mBumpInterpPlug.isNull() )
								{
									const MStatus mHasIndex = mBumpInterpPlug.getValue( bumpType );
								}

								MFnEnumAttribute mBumpInterp( mBumpAttibute );
								MString mFieldName = mBumpInterp.fieldName( bumpType );
								const char* cDefault = mFieldName.asChar();

								this->sgMaterial->SetUseTangentSpaceNormals( bumpType == 1 ? true : false );
							}
						}
					}

					MPlugArray mBumpPlugs;
					mBumpNode.getConnections( mBumpPlugs );

					for( uint i = 0; i < mBumpPlugs.length(); ++i )
					{
						const MPlug& mBumpPlug = mBumpPlugs[ i ];
						const char* mBumpPlugName = mBumpPlug.name().asChar();

						if( !mBumpPlug.isNull() )
						{
							MObject mBumpObject = ::GetConnectedUpstreamNode( mBumpPlug );
							if( !mBumpObject.isNull() )
							{
								MString mDefaultUVSetName( "map1" );
								if( textureIterator->second->GetTexCoordName() != nullptr )
								{
									mDefaultUVSetName = textureIterator->second->GetTexCoordName();
								}

								MFnDependencyNode mFileTextureNode( mBumpObject );
								// Required for BinSkim compat
								// TODO: Deprecated method, should be replaced!
								SG_DISABLE_SPECIFIC_BEGIN( 4996 )
								MPlug mTexFilePlug = mFileTextureNode.findPlug( "fileTextureName" );
								SG_DISABLE_SPECIFIC_END

								MString mFileName;

								// if the plug is not null extract the file name
								if( !mTexFilePlug.isNull() )
								{
									mTexFilePlug.getValue( mFileName );

									// Required for BinSkim compat
									// TODO: Deprecated method, should be replaced!
									SG_DISABLE_SPECIFIC_BEGIN( 4996 )
									MPlug mUVSetPlug = mFileTextureNode.findPlug( "uvCoord" );
									SG_DISABLE_SPECIFIC_END
									if( !mUVSetPlug.isNull() )
									{
										MObject mPlace2dTexture = ::GetConnectedUpstreamNode( mUVSetPlug );
										if( !mPlace2dTexture.isNull() )
										{
											MFnDependencyNode mPlace2dTextureNode( mPlace2dTexture );
											// Required for BinSkim compat
											// TODO: Deprecated method, should be replaced!
											SG_DISABLE_SPECIFIC_BEGIN( 4996 )
											mUVSetPlug = mPlace2dTextureNode.findPlug( "uvCoord" );
											SG_DISABLE_SPECIFIC_END

											MObject mUVChooser = ::GetConnectedUpstreamNode( mUVSetPlug );

											if( !mUVChooser.isNull() )
											{
												MFnDependencyNode uvChooserFn( mUVChooser );

												// Required for BinSkim compat
												// TODO: Deprecated method, should be replaced!
												SG_DISABLE_SPECIFIC_BEGIN( 4996 )
												// get the uvSets plug, which is an array of connected nodes
												MPlug mUVSetsPlug = uvChooserFn.findPlug( "uvSets" );
												SG_DISABLE_SPECIFIC_END
												if( !mUVSetsPlug.isNull() )
												{
													for( uint j = 0; j < mUVSetsPlug.numElements(); ++j )
													{
														MPlug mShapePlug = ::GetConnectedUpstreamPlug( mUVSetsPlug[ j ] );

														mDefaultUVSetName = mShapePlug.asString();
													}
												}
											}
											else
											{
												MFnDependencyNode mUVSetNode( mUVSetPlug );

												// Required for BinSkim compat
												// TODO: Deprecated method, should be replaced!
												SG_DISABLE_SPECIFIC_BEGIN( 4996 )
												// get the uvSets plug, which is an array of connected nodes
												MPlug mUVSetsPlug = mUVSetNode.findPlug( "uvSets" );
												SG_DISABLE_SPECIFIC_END
												if( !mUVSetsPlug.isNull() )
												{
													for( uint j = 0; j < mUVSetsPlug.numElements(); ++j )
													{
														MPlug mShapePlug = ::GetConnectedUpstreamPlug( mUVSetsPlug[ j ] );

														mDefaultUVSetName = mShapePlug.asString();
													}
												}
											}
										}
									}

									// now  extract uv
								}

								std::basic_string<TCHAR> tTexturePathWithName = this->materialHandler->ImportTexture( mFileName ).c_str();
								std::basic_string<TCHAR> tTextureName = GetTitleOfFile( tTexturePathWithName );
								std::basic_string<TCHAR> tTextureExtension = GetExtensionOfFile( tTexturePathWithName );
								std::basic_string<TCHAR> tTextureNameWithExtension = tTextureName + tTextureExtension;

								// add extra mapping for post processing of data
								std::map<spShadingTextureNode, std::basic_string<TCHAR>>::iterator nodeToPathIterator =
								    this->ShadingTextureNodeToPath.find( textureIterator->second );
								if( nodeToPathIterator == this->ShadingTextureNodeToPath.end() )
								{
									this->ShadingTextureNodeToPath.insert(
									    std::pair<spShadingTextureNode, std::basic_string<TCHAR>>( textureIterator->second, tTexturePathWithName ) );
								}

								if( tTexturePathWithName.size() > 0 )
								{
									textureIterator->second->SetTextureName( tTextureName.c_str() );
									textureIterator->second->SetTexCoordName( mDefaultUVSetName.asChar() );
								}
							}
						}
					}
				}
			}
		}
	}

	// go through the color nodes lookup
	for( channelIterator = shadingNetworkData->ChannelToShadingNetworkMap.begin(); channelIterator != shadingNetworkData->ChannelToShadingNetworkMap.end();
	     channelIterator++ )
	{
		for( colorIterator = channelIterator->second->ColorNodeLookup.begin(); colorIterator != channelIterator->second->ColorNodeLookup.end();
		     colorIterator++ )
		{
			MString mColorPlugName = MString( colorIterator->first.c_str() );

			// if shader has color attribute
			if( mShaderNode.hasAttribute( mColorPlugName ) )
			{
				// Required for BinSkim compat
				// TODO: Deprecated method, should be replaced!
				SG_DISABLE_SPECIFIC_BEGIN( 4996 )
				MPlug mColorPlug = mShaderNode.findPlug( mColorPlugName );
				SG_DISABLE_SPECIFIC_END
				if( !mColorPlug.isNull() )
				{
					// get attribute object
					MObject mAttributeObject = mShaderNode.attribute( colorIterator->first.c_str() );
					if( mAttributeObject != MObject::kNullObj )
					{
						// check if type is float attribute
						if( mAttributeObject.apiType() == MFn::kAttribute3Float )
						{
							MFloatVector mColor;

							// extract color
							getFloat3PlugValue( mColorPlug, mColor );
							MFnNumericData mNumData( mAttributeObject );

							// get node and set color value
							colorIterator->second->SetColor( mColor[ 0 ], mColor[ 1 ], mColor[ 2 ], 1.0f );
						}
					}
				}
			}
		}
	}

	return MStatus::kSuccess;
}

MStatus MaterialNode::InternalSetup()
{
	// setup the dependency node function set on the object
	MStatus mStatus;
	MFnDependencyNode mShadingGroupDependencyNode( this->MaterialObject, &mStatus );
	if( !mStatus )
	{
		MGlobal::displayError( "MaterialNode::InternalSetup: object is not a dependency graph node" );
		return MStatus::kFailure;
	}

	// find a shader node, if one exists
	MObject mShaderNode = GetConnectedNamedPlug( mShadingGroupDependencyNode, "surfaceShader" );

	// if no material was found, just return, we will use the default values
	if( mShaderNode == MObject::kNullObj )
	{
		return MStatus::kSuccess;
	}

	MFnDependencyNode mShaderDependencyNode( mShaderNode );
	this->ShadingNodeName = mShaderDependencyNode.name();

	// get texture overrides
	std::vector<MaterialTextureOverride>& materialTextureOverrides = this->materialHandler->GetMaterialTextureOverrides();

	// look for possible textures to override
	for( uint i = 0; i < (uint)materialTextureOverrides.size(); ++i )
	{
		const MaterialTextureOverride& materialTextureOverride = materialTextureOverrides[ i ];

		// if material found
		if( materialTextureOverride.MaterialName == this->ShadingNodeName )
		{
			MString mType = materialTextureOverride.TextureType;
			mType = mType.toLowerCase();

			MaterialTextures* materialTextures = nullptr;

			// got one, look for the texture type
			if( mType == MAYA_MATERIAL_CHANNEL_AMBIENTCOLOR )
				materialTextures = &AmbientTextures;
			else if( mType == MAYA_MATERIAL_CHANNEL_COLOR )
				materialTextures = &ColorTextures;
			else if( mType == MAYA_MATERIAL_CHANNEL_SPECULARCOLOR )
				materialTextures = &SpecularColorTextures;
			else if( mType == MAYA_MATERIAL_CHANNEL_NORMALCAMERA )
				materialTextures = &NormalCameraTextures;
			else if( mType == MAYA_MATERIAL_CHANNEL_INCANDESCENCE )
				materialTextures = &IncandescenceTextures;
			else if( mType == MAYA_MATERIAL_CHANNEL_TRANSPARENCY )
				materialTextures = &TransparencyTextures;
			else if( mType == MAYA_MATERIAL_CHANNEL_TRANSLUECENCE )
				materialTextures = &TranslucenceTextures;
			else if( mType == MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_DEPTH )
				materialTextures = &TranslucenceDepthTextures;
			else if( mType == MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_FOCUS )
				materialTextures = &TranslucenceFocusTextures;
			else
			{
				// loop through the user textures
				bool bTextureExists = false;
				for( uint j = 0; j < UserTextures.size(); ++j )
				{
					MString mMappingChannelName = UserTextures[ j ].MappingChannelName;
					mMappingChannelName.toLowerCase();

					if( mType == mMappingChannelName)
					{
						bTextureExists = true;
					}
				}
				if( !bTextureExists )
				{
					MaterialTextures tmpMaterialTextures;
					tmpMaterialTextures.MappingChannelName = materialTextureOverride.TextureType;
					UserTextures.push_back( tmpMaterialTextures );
					materialTextures = &UserTextures[ UserTextures.size() - 1 ];
				}
			}

			// if the type was found, use it
			if( materialTextures != nullptr )
			{
				// make sure the layer exists in the material
				if( materialTextures->TextureLayers.size() <= (uint)materialTextureOverride.TextureLayer )
				{
					materialTextures->TextureLayers.resize( (size_t)materialTextureOverride.TextureLayer + 1 );
				}

				MaterialTextureLayer& textureLayer = materialTextures->TextureLayers[ materialTextureOverride.TextureLayer ];

				// import the texture into this material
				textureLayer.HasTangentSpaceNormals = materialTextureOverride.HasTangentSpaceNormals;
				textureLayer.BlendType = materialTextureOverride.BlendType;

				// look for a texture node that has this name
				MObject mObject;
				GetMObjectOfNamedObject( materialTextureOverride.TextureName, mObject );
				if( !mObject.isNull() )
				{
					this->GetFileTexture( mObject, textureLayer );
				}
				else
				{
					// this is not a texture node, but a texture file name
					// import this filename directly
					textureLayer.OriginalTextureFileName = materialTextureOverride.TextureName;
					textureLayer.TextureFileName = MString( this->materialHandler->ImportTexture( materialTextureOverride.TextureName ).c_str() );
				}
			}
			else
			{
				MGlobal::displayWarning( MString( "Invalid texture type: \"" ) + materialTextureOverride.TextureType +
				                         "\" in material texture override, ignored." );
			}
		}
	}

	// get the color plug, check if it has a texture or color value
	real colorFactor[ 3 ] = { 0.8f, 0.8f, 0.8f };
	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	MPlug mMaterialPlug = mShaderDependencyNode.findPlug( MAYA_MATERIAL_CHANNEL_COLOR );
	SG_DISABLE_SPECIFIC_END

	if( !mMaterialPlug.isNull() )
	{
		// get the texture if one exists
		if( this->GetFileTexture( ::GetConnectedUpstreamNode( mMaterialPlug ), this->ColorTextures ) )
		{
			// we have a texture, reset the diffuse color to white
			colorFactor[ 0 ] = 1.f;
			colorFactor[ 1 ] = 1.f;
			colorFactor[ 2 ] = 1.f;
		}
		else
		{
			// get the color value instead of the diffuse texture
			MFloatVector mColor;
			if( getFloat3PlugValue( mMaterialPlug, mColor ) )
			{
				colorFactor[ 0 ] = mColor[ 0 ];
				colorFactor[ 1 ] = mColor[ 1 ];
				colorFactor[ 2 ] = mColor[ 2 ];
			}
		}
	}

	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	// get the diffuse plug, multiply the value by the diffuse color of the material
	mMaterialPlug = mShaderDependencyNode.findPlug( "diffuse" );
	SG_DISABLE_SPECIFIC_END
	if( !mMaterialPlug.isNull() )
	{
		float diffuseFactor = 0.8f;
		if( mMaterialPlug.getValue( diffuseFactor ) )
		{
			colorFactor[ 0 ] *= diffuseFactor;
			colorFactor[ 1 ] *= diffuseFactor;
			colorFactor[ 2 ] *= diffuseFactor;
		}
	}

	// set the diffuse value
	this->SetMaterialColor( this->ColorValue, colorFactor[ 0 ], colorFactor[ 1 ], colorFactor[ 2 ] );

	// get the ambient color plug
	real ambientFactor[ 3 ] = { 0.f, 0.f, 0.f };
	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	mMaterialPlug = mShaderDependencyNode.findPlug( MAYA_MATERIAL_CHANNEL_AMBIENTCOLOR );
	SG_DISABLE_SPECIFIC_END
	if( !mMaterialPlug.isNull() )
	{
		// get the texture if one exists
		if( this->GetFileTexture( ::GetConnectedUpstreamNode( mMaterialPlug ), this->AmbientTextures ) )
		{
			// we have a texture, reset the diffuse color to white
			ambientFactor[ 0 ] = 1.f;
			ambientFactor[ 1 ] = 1.f;
			ambientFactor[ 2 ] = 1.f;
		}
		else
		{
			// get the color value instead of the texture
			MFloatVector mColor;
			if( getFloat3PlugValue( mMaterialPlug, mColor ) )
			{
				ambientFactor[ 0 ] = mColor[ 0 ];
				ambientFactor[ 1 ] = mColor[ 1 ];
				ambientFactor[ 2 ] = mColor[ 2 ];
			}
		}
	}

	// set the ambient value
	this->SetMaterialColor( this->AmbientValue, ambientFactor[ 0 ], ambientFactor[ 1 ], ambientFactor[ 2 ] );

	// get the specularColor plug
	real specularColorFactor[ 3 ] = { 0.f, 0.f, 0.f };
	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	mMaterialPlug = mShaderDependencyNode.findPlug( MAYA_MATERIAL_CHANNEL_SPECULARCOLOR );
	SG_DISABLE_SPECIFIC_END
	if( !mMaterialPlug.isNull() )
	{
		// get the texture if one exists
		if( this->GetFileTexture( ::GetConnectedUpstreamNode( mMaterialPlug ), this->SpecularColorTextures ) )
		{
			// we have a texture, reset the diffuse color to white
			specularColorFactor[ 0 ] = 1.f;
			specularColorFactor[ 1 ] = 1.f;
			specularColorFactor[ 2 ] = 1.f;
		}
		else
		{
			// get the color value instead of the texture
			MFloatVector mColor;
			if( getFloat3PlugValue( mMaterialPlug, mColor ) )
			{
				specularColorFactor[ 0 ] = mColor[ 0 ];
				specularColorFactor[ 1 ] = mColor[ 1 ];
				specularColorFactor[ 2 ] = mColor[ 2 ];
			}
		}
	}

	// get the transparency plug
	real transparencyFactor[ 3 ] = { 0.f, 0.f, 0.f };
	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	mMaterialPlug = mShaderDependencyNode.findPlug( MAYA_MATERIAL_CHANNEL_TRANSPARENCY );
	SG_DISABLE_SPECIFIC_END
	if( !mMaterialPlug.isNull() )
	{
		if( this->GetFileTexture( ::GetConnectedUpstreamNode( mMaterialPlug ), this->TransparencyTextures ) )
		{
			transparencyFactor[ 0 ] = 1.f;
			transparencyFactor[ 1 ] = 1.f;
			transparencyFactor[ 2 ] = 1.f;
		}
		else
		{
			MFloatVector mColor;
			if( getFloat3PlugValue( mMaterialPlug, mColor ) )
			{
				transparencyFactor[ 0 ] = 1 - mColor[ 0 ];
				transparencyFactor[ 1 ] = 1 - mColor[ 1 ];
				transparencyFactor[ 2 ] = 1 - mColor[ 2 ];
			}
		}
	}

	this->SetMaterialColor( this->TransparencyValue, transparencyFactor[ 0 ], transparencyFactor[ 1 ], transparencyFactor[ 2 ] );

	// get the translucence plug
	real translucenceFactor = 0.f;
	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	mMaterialPlug = mShaderDependencyNode.findPlug( MAYA_MATERIAL_CHANNEL_TRANSLUECENCE );
	SG_DISABLE_SPECIFIC_END
	if( !mMaterialPlug.isNull() )
	{
		if( this->GetFileTexture( ::GetConnectedUpstreamNode( mMaterialPlug ), this->TranslucenceTextures ) )
		{
			translucenceFactor = 1.f;
		}
		else
		{
			mMaterialPlug.getValue( translucenceFactor );
		}
	}

	this->SetMaterialColor( this->TranslucenceValue, translucenceFactor, translucenceFactor, translucenceFactor, translucenceFactor );

	// get the translucence depth plug
	real translucenceDepth = 0.5f;
	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	mMaterialPlug = mShaderDependencyNode.findPlug( MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_DEPTH );
	SG_DISABLE_SPECIFIC_END
	if( !mMaterialPlug.isNull() )
	{
		if( this->GetFileTexture( ::GetConnectedUpstreamNode( mMaterialPlug ), this->TranslucenceDepthTextures ) )
		{
			translucenceDepth = 1.f;
		}
		else
		{
			mMaterialPlug.getValue( translucenceDepth );
		}
	}

	this->SetMaterialColor( this->TranslucenceDepthValue, translucenceDepth, translucenceDepth, translucenceDepth, translucenceDepth );

	// get the translucence focus plug
	real translucenceFocus = 0.5f;
	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	mMaterialPlug = mShaderDependencyNode.findPlug( MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_FOCUS );
	SG_DISABLE_SPECIFIC_END
	if( !mMaterialPlug.isNull() )
	{
		if( this->GetFileTexture( ::GetConnectedUpstreamNode( mMaterialPlug ), this->TranslucenceFocusTextures ) )
		{
			translucenceFocus = 1.f;
		}
		else
		{
			mMaterialPlug.getValue( translucenceFocus );
		}
	}

	this->SetMaterialColor( this->TranslucenceFocusValue, translucenceFocus, translucenceFocus, translucenceFocus, translucenceFocus );

	// Specular shininess material variation handling
	///////////////////////////////////////////////////////////
	float shininess = 0.f;
	MString mShaderTypeName = mShaderDependencyNode.typeName();

	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	MPlug mCosinePowerPlug = mShaderDependencyNode.findPlug( "cosinePower", &mStatus );
	SG_DISABLE_SPECIFIC_END
	if( !mCosinePowerPlug.isNull() )
	{
		mCosinePowerPlug.getValue( shininess );
	}

	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	MPlug mEccentricityPlug = mShaderDependencyNode.findPlug( "eccentricity", &mStatus );
	MPlug mSpecularRollOffPlug = mShaderDependencyNode.findPlug( "specularRollOff", &mStatus );
	SG_DISABLE_SPECIFIC_END
	if( !mEccentricityPlug.isNull() && !mSpecularRollOffPlug.isNull() )
	{
		float specularRollOff = 0.f;
		float eccentricity = 0.f;

		mEccentricityPlug.getValue( eccentricity );
		mSpecularRollOffPlug.getValue( specularRollOff );

		shininess = 10.f + 118.f * ( 1.f - eccentricity ) * specularRollOff;
	}

	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	MPlug mRoughnessPlug = mShaderDependencyNode.findPlug( "roughness", &mStatus );
	MPlug mHighlightSizePlug = mShaderDependencyNode.findPlug( "highlightSize", &mStatus );
	SG_DISABLE_SPECIFIC_END
	if( !mRoughnessPlug.isNull() && !mHighlightSizePlug.isNull() )
	{
		float highlightSize = 0.0f;
		float roughness = 0.0f;
		mRoughnessPlug.getValue( roughness );
		mHighlightSizePlug.getValue( highlightSize );

		shininess = 10.f + 118.f * ( 1.f - roughness ) * highlightSize;
	}

	///////////////////////////////////////////////////////////////
	this->SetMaterialColor( this->SpecularValue, specularColorFactor[ 0 ], specularColorFactor[ 1 ], specularColorFactor[ 2 ], shininess );

	// get the incandescence plug
	real incandescenceFactor[ 3 ] = { 0.f, 0.f, 0.f };
	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	mMaterialPlug = mShaderDependencyNode.findPlug( MAYA_MATERIAL_CHANNEL_INCANDESCENCE );
	SG_DISABLE_SPECIFIC_END
	if( !mMaterialPlug.isNull() )
	{
		// get the texture if one exists
		if( this->GetFileTexture( ::GetConnectedUpstreamNode( mMaterialPlug ), this->IncandescenceTextures ) )
		{
			// we have a texture, reset the diffuse color to white
			incandescenceFactor[ 0 ] = 1;
			incandescenceFactor[ 1 ] = 1;
			incandescenceFactor[ 2 ] = 1;
		}
		else
		{
			// get the color value instead of the texture
			MFloatVector mColor;
			if( getFloat3PlugValue( mMaterialPlug, mColor ) )
			{
				incandescenceFactor[ 0 ] = mColor[ 0 ];
				incandescenceFactor[ 1 ] = mColor[ 1 ];
				incandescenceFactor[ 2 ] = mColor[ 2 ];
			}
		}
	}

	// set the diffuse value
	this->SetMaterialColor( this->IncandescenceValue, incandescenceFactor[ 0 ], incandescenceFactor[ 1 ], incandescenceFactor[ 2 ] );

	// get the bump map plug, check if it has a texture
	if( this->NormalCameraTextures.TextureLayers.size() == 0 )
	{
		// Required for BinSkim compat
		// TODO: Deprecated method, should be replaced!
		SG_DISABLE_SPECIFIC_BEGIN( 4996 )
		MPlug mNormalCameraPlug = mShaderDependencyNode.findPlug( MAYA_MATERIAL_CHANNEL_NORMALCAMERA );
		SG_DISABLE_SPECIFIC_END
		if( !mNormalCameraPlug.isNull() )
		{
			// look for a bump map node
			MObject mNormalsNode = GetConnectedUpstreamNode( mNormalCameraPlug );
			if( !mNormalsNode.isNull() )
			{
				MFnDependencyNode mNormalsDependencyNode( mNormalsNode );

				// Required for BinSkim compat
				// TODO: Deprecated method, should be replaced!
				SG_DISABLE_SPECIFIC_BEGIN( 4996 )
				// get the bump map input plugs
				MPlug mBumpValue = mNormalsDependencyNode.findPlug( "bumpValue" );
				MPlug mBumpInterp = mNormalsDependencyNode.findPlug( "bumpInterp" );
				SG_DISABLE_SPECIFIC_END
				if( !mBumpValue.isNull() && !mBumpInterp.isNull() )
				{
					// get the bump type
					int bumpType;
					mBumpInterp.getValue( bumpType );

					// we only support tangent and object space normals, not bump values
					if( bumpType == 1 || bumpType == 2 )
					{
						this->NormalCameraTextures.TextureLayers.resize( 1 );
						MaterialTextureLayer& textureLayer = this->NormalCameraTextures.TextureLayers[ 0 ];

						if( bumpType == 1 )
							textureLayer.HasTangentSpaceNormals = true;
						else
							textureLayer.HasTangentSpaceNormals = false;

						this->GetFileTexture( ::GetConnectedUpstreamNode( mBumpValue ), textureLayer );
					}
					else
					{
						MGlobal::displayWarning( "Detected an unsupported normal / bump type on the 'normalCamera' material channel in material '" +
						                         this->ShadingNodeName +
						                         "'. If the setting is incorrect, update the normal / bump type to either 'Tangent Space Normals' or "
						                         "'Object Space Normals'. If the texture is not of any of these types, please consider replacing the texture "
						                         "with a normal map. This texture will be ignored until the issues have been corrected." );
					}
				}
			}
		}
	}

	return MStatus::kSuccess;
}

bool MaterialNode::SetMaterialColor( MaterialColor& materialColor, real r, real g, real b, real a )
{
	materialColor.ColorValue[ 0 ] = r;
	materialColor.ColorValue[ 1 ] = g;
	materialColor.ColorValue[ 2 ] = b;
	materialColor.ColorValue[ 3 ] = a;
	return true;
}

bool MaterialNode::GetFileTexture( MObject mNode, MaterialTextureLayer& materialTextureLayer )
{
	if( mNode.isNull() )
		return false;

	// if already set, (through an override), just skip
	if( materialTextureLayer.TextureFileName != "" )
		return true;

	MFnDependencyNode mDependencyNode( mNode );

	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	// get the full texture path
	MPlug mFileTextureName = mDependencyNode.findPlug( "fileTextureName" );
	SG_DISABLE_SPECIFIC_END
	if( mFileTextureName.isNull() )
		return false;

	// get the filename and import the file
	MString mFileName;
	mFileTextureName.getValue( mFileName );
	materialTextureLayer.OriginalTextureFileName = mFileName;
	materialTextureLayer.TextureFileName = MString( this->materialHandler->ImportTexture( mFileName ).c_str() );

	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	// now, look for a place2dTexture, and a uvChooser with sets of shapes
	MPlug mUVCoordPlug = mDependencyNode.findPlug( "uvCoord" );
	SG_DISABLE_SPECIFIC_END
	if( !mUVCoordPlug.isNull() )
	{
		MObject mPlace2dTexture = ::GetConnectedUpstreamNode( mUVCoordPlug );
		if( !mPlace2dTexture.isNull() )
		{
			MFnDependencyNode mPlace2dTextureDependencyNode( mPlace2dTexture );
			// Required for BinSkim compat
			// TODO: Deprecated method, should be replaced!
			SG_DISABLE_SPECIFIC_BEGIN( 4996 )
			mUVCoordPlug = mPlace2dTextureDependencyNode.findPlug( "uvCoord" );
			SG_DISABLE_SPECIFIC_END
			MObject mUVChooser = ::GetConnectedUpstreamNode( mUVCoordPlug );
			if( !mUVChooser.isNull() )
			{
				MFnDependencyNode mUVChooserDependencyNode( mUVChooser );

				// Required for BinSkim compat
				// TODO: Deprecated method, should be replaced!
				SG_DISABLE_SPECIFIC_BEGIN( 4996 )
				// get the uvSets plug, which is an array of connected nodes
				MPlug mUVSetsPlug = mUVChooserDependencyNode.findPlug( "uvSets" );
				SG_DISABLE_SPECIFIC_END
				if( !mUVSetsPlug.isNull() )
				{
					for( uint i = 0; i < mUVSetsPlug.numElements(); ++i )
					{
						MPlug mShapePlug = ::GetConnectedUpstreamPlug( mUVSetsPlug[ i ] );

						TextureShapeUVLinkage link;

						link.Node = mShapePlug.node();
						link.UVSet = mShapePlug.asString();

						materialTextureLayer.TextureUVLinkage.push_back( link );
					}
				}
			}
		}
	}

	return true;
}

void MaterialNode::PopulateLayeredTextureProperties( const MFnDependencyNode& mDependencyNode,
                                                     const MPlug& mMultiLayeredChildPlug,
                                                     TextureProperties* textureLayer )
{
	MPlug mFilePlug = ::GetConnectedUpstreamPlug( mMultiLayeredChildPlug );
	if( mFilePlug.isNull() )
	{
		return;
	}

	MFnDependencyNode mFileDependencyNode( mFilePlug.node() );
	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	MPlug mFileTexturePlug = mFileDependencyNode.findPlug( "fileTextureName" );
	SG_DISABLE_SPECIFIC_END
	if( mFileTexturePlug.isNull() )
	{
		return;
	}

	this->PopulateTextureProperties( mFileDependencyNode, mFileTexturePlug, textureLayer );
}

void MaterialNode::PopulateTextureProperties( const MFnDependencyNode& mDependencyNode, const MPlug& mFileTexturePlug, TextureProperties* textureLayer )
{
	// get the filename and import the file
	MString mFileName;
	mFileTexturePlug.getValue( mFileName );

	textureLayer->OriginalTextureFileName = mFileName;
	textureLayer->TextureFileName = MString( this->materialHandler->ImportTexture( mFileName ).c_str() );

	MStatus mStatus;

	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	MPlug mRepeatUPlug = mDependencyNode.findPlug( "repeatU", &mStatus );
	if( mStatus == MStatus::kSuccess )
	{
		const float repeatU = mRepeatUPlug.asFloat();
		textureLayer->RepeatUV[ 0 ] = repeatU;
	}

	MPlug mRepeatVPlug = mDependencyNode.findPlug( "repeatV", &mStatus );
	if( mStatus == MStatus::kSuccess )
	{
		const float repeatV = mRepeatVPlug.asFloat();
		textureLayer->RepeatUV[ 1 ] = repeatV;
	}

	MPlug mOffsetUPlug = mDependencyNode.findPlug( "offsetU", &mStatus );
	if( mStatus == MStatus::kSuccess )
	{
		const float offsetU = mOffsetUPlug.asFloat();
		textureLayer->OffsetUV[ 0 ] = offsetU;
	}

	MPlug mOffsetVPlug = mDependencyNode.findPlug( "offsetV", &mStatus );
	if( mStatus == MStatus::kSuccess )
	{
		const float offsetV = mOffsetVPlug.asFloat();
		textureLayer->OffsetUV[ 1 ] = offsetV;
	}

	MPlug mColorGainPlug = mDependencyNode.findPlug( "colorGain", &mStatus );
	SG_DISABLE_SPECIFIC_END

	if( mStatus == MStatus::kSuccess )
	{
		// int numChildren = mColorGainPlug.numChildren(&mStatus);
		for( uint k = 0; k < 3; ++k )
		{
			MPlug mColorGainComponentPlug = mColorGainPlug.child( k, &mStatus );
			if( mStatus == MStatus::kSuccess )
			{
				// Required for BinSkim compat
				// TODO: Deprecated method, should be replaced!
				SG_DISABLE_SPECIFIC_BEGIN( 4996 )
				const float colorGain = mColorGainComponentPlug.asFloat( MDGContext::fsNormal, &mStatus );
				SG_DISABLE_SPECIFIC_END
				if( mStatus == MStatus::kSuccess )
				{
					textureLayer->ColorGain[ k ] = colorGain;
				}
			}
		}
	}

	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	MPlug mSRGBPlug = mDependencyNode.findPlug( "colorSpace", &mStatus );
	if( mStatus == MStatus::kSuccess )
	{
		MString mColorSpaceType = mSRGBPlug.asString( MDGContext::fsNormal, &mStatus );
		textureLayer->SRGB = ( mColorSpaceType == "sRGB" );
	}

	// now, look for a place2dTexture, and a uvChooser with sets of shapes
	MPlug mUVCoordPlug = mDependencyNode.findPlug( "uvCoord" );
	SG_DISABLE_SPECIFIC_END
	if( mUVCoordPlug.isNull() )
	{
		return;
	}

	// MString mUVSetName = mUVCoordPlug.name();
	MObject mPlace2dTexture = ::GetConnectedUpstreamNode( mUVCoordPlug );
	if( mPlace2dTexture.isNull() )
	{
		return;
	}

	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	MFnDependencyNode mPlace2dTextureDependencyNode( mPlace2dTexture );
	mUVCoordPlug = mPlace2dTextureDependencyNode.findPlug( "uvCoord" );
	MObject mUVChooser = ::GetConnectedUpstreamNode( mUVCoordPlug );
	if( mUVChooser.isNull() )
	{
		return;
	}

	// get the uvSets plug, which is an array of connected nodes
	MFnDependencyNode mUVChooserDependencyNode( mUVChooser );
	MPlug mUVSetsPlug = mUVChooserDependencyNode.findPlug( "uvSets" );
	MString mUVSetName = mUVSetsPlug.name();
	if( mUVSetsPlug.isNull() )
	{
		return;
	}
	SG_DISABLE_SPECIFIC_END

	for( uint k = 0; k < mUVSetsPlug.numElements(); ++k )
	{
		MPlug mShapePlug = ::GetConnectedUpstreamPlug( mUVSetsPlug[ k ] );
		MString mShapePlugName = mShapePlug.name();

		TextureShapeUVLinkage link;
		link.Node = mShapePlug.node();
		link.UVSet = mShapePlug.asString();

		textureLayer->TextureUVLinkage.push_back( link );
	}
}

bool MaterialNode::GetFileTexture( MObject mNode, MaterialTextures& materialTextures )
{
	if( mNode.isNull() )
		return false;

	std::vector<MaterialTextureLayer>& textureLayers = materialTextures.TextureLayers;

	std::map<int, MaterialTextureLayer> mOverriddenTextureLayers;

	MFnDependencyNode mDependencyNode( mNode );

	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	// get the full texture path
	MPlug mFileTexturePlug = mDependencyNode.findPlug( "fileTextureName" );
	if( mFileTexturePlug.isNull() )
	{
		// Get the possible color array
		MPlug mMultiLayeredPlug = mDependencyNode.findPlug( "inputs" );
		SG_DISABLE_SPECIFIC_END

		if( mMultiLayeredPlug.isNull() )
			return false;

		const uint numMaterials = mMultiLayeredPlug.numElements();

		// Do not shrink dest_vector if a larger entry has been overridden
		if( numMaterials > textureLayers.size() )
		{
			textureLayers.resize( numMaterials );
		}

		for( uint materialIndex = 0; materialIndex < numMaterials; ++materialIndex )
		{
			MaterialTextureLayer* currentTextureLayer = &textureLayers[ materialIndex ];
			if( currentTextureLayer->TextureFileName != "" )
			{
				// This entry is overridden, store it and replace to it at the end
				mOverriddenTextureLayers.insert( std::pair<int, MaterialTextureLayer>( materialIndex, *currentTextureLayer ) );
			}

			MString mMultiLayeredName = mMultiLayeredPlug[ materialIndex ].name();
			const uint numChildren = mMultiLayeredPlug[ materialIndex ].numChildren();

			MString mVisiblePlugName = mMultiLayeredPlug[ materialIndex ].name() + ".isVisible";
			bool bVisible = true;
			for( uint layerIndex = 0; layerIndex < numChildren; ++layerIndex )
			{
				if( mVisiblePlugName == mMultiLayeredPlug[ materialIndex ].child( layerIndex ).name() )
				{
					mMultiLayeredPlug[ materialIndex ].child( layerIndex ).getValue( bVisible );
				}
			}

			if( !bVisible )
				continue;

			for( uint layerIndex = 0; layerIndex < numChildren; ++layerIndex )
			{
				MPlug mMultiLayeredChildPlug = mMultiLayeredPlug[ materialIndex ].child( layerIndex );

				if( mMultiLayeredChildPlug.isNull() )
				{
					continue;
				}

				MString mMultiLayeredChildName = mMultiLayeredChildPlug.name();
				if( mMultiLayeredChildName == mMultiLayeredName + ".blendMode" )
				{
					int blendType;

					// MAYA HAS THE SETTINGS FOR BLEND:
					// 0  : NONE
					// 1  : OVER
					// 2  : IN
					// 3  : OUT
					// 4  : ADD
					// 5  : SUBTRACT
					// 6  : MULTIPLY
					// 7  : DIFFERENCE
					// 8  : LIGHTEN
					// 9  : DARKEN
					// 10 : SATURATE
					// 11 : DESATURATE
					// 12 : ILLUMINATE

					const MStatus mHasBlendType = mMultiLayeredChildPlug.getValue( blendType );
					if( mHasBlendType == MStatus::kSuccess )
					{
						currentTextureLayer->BlendType = blendType;
					}
				}

				else if( mMultiLayeredChildName == mMultiLayeredName + ".alpha" )
				{
					float layerAlpha = 1.f;

					const MStatus mHasLayerAlphaFactor = mMultiLayeredChildPlug.getValue( layerAlpha );
					if( mHasLayerAlphaFactor == MStatus::kSuccess )
					{
						currentTextureLayer->LayerAlpha = layerAlpha;
					}

					MPlug mLayerAlphaFilePlug = ::GetConnectedUpstreamPlug( mMultiLayeredChildPlug );
					if( !mLayerAlphaFilePlug.isNull() )
					{
						if( !currentTextureLayer->AlphaTexture )
						{
							currentTextureLayer->AlphaTexture = new TextureProperties();
						}

						PopulateLayeredTextureProperties( mDependencyNode, mMultiLayeredChildPlug, currentTextureLayer->AlphaTexture );
					}
				}

				else if( mMultiLayeredChildName == mMultiLayeredName + ".color" )
				{
					PopulateLayeredTextureProperties( mDependencyNode, mMultiLayeredChildPlug, currentTextureLayer );
				}
			}
		}
	} // END if( mFileTextureName.isNull() )
	else
	{
		if( textureLayers.size() > 0 && textureLayers[ 0 ].TextureFileName != "" )
			return true;

		// The material is single layered
		if( textureLayers.size() < 1 )
			textureLayers.resize( 1 );

		MaterialTextureLayer* currentTextureLayer = &textureLayers[ 0 ];

		// get the filename and import the file
		PopulateTextureProperties( mDependencyNode, mFileTexturePlug, currentTextureLayer );
	}

	// Overwrite with overridden materials
	std::map<int, MaterialTextureLayer>::const_iterator& textureLayerOverrideIterator = mOverriddenTextureLayers.begin();
	while( textureLayerOverrideIterator != mOverriddenTextureLayers.end() )
	{
		const int layerIndex = textureLayerOverrideIterator->first;
		MaterialTextureLayer* currentTextureLayer = &textureLayers[ layerIndex ];

		int blendMode = MaterialNode::MAYA_BLEND_NONE;

		// If the overridden blend type is invalid, then use the current one from Maya
		if( textureLayerOverrideIterator->second.BlendType == -1 )
		{
			blendMode = currentTextureLayer->BlendType;
			if( blendMode < 0 )
			{
				blendMode = MaterialNode::MAYA_BLEND_NONE;
			}
		}

		*currentTextureLayer = textureLayerOverrideIterator->second;

		if( textureLayerOverrideIterator->second.BlendType == -1 )
		{
			currentTextureLayer->BlendType = blendMode;
		}

		++textureLayerOverrideIterator;
	}

	return true;
}

void MaterialNode::CreateSgMaterialChannel(
    const char* cMaterialChannelName, MeshNode* meshNode, const MaterialTextures& materialTextures, bool& hasTextures, bool& isSRGB )
{
	if( !sgMaterial->HasMaterialChannel( cMaterialChannelName ) )
	{
		sgMaterial->AddMaterialChannel( cMaterialChannelName );
	}

	this->SetMaterialTextureForMeshNode( cMaterialChannelName, meshNode, materialTextures, hasTextures, isSRGB );
}

std::string MaterialNode::GetSimplygonMaterialWithShadingNetwork( MString mMaterialName, MeshNode* meshNode )
{
	// use sgsdk material to save out shading network xml
	std::map<std::basic_string<TCHAR>, ShadingPerChannelData*>::iterator shadingNetworkIterator;

	if( this->shadingNetworkData != nullptr && !shadingNetworkData->ChannelToShadingNetworkMap.empty() &&
	    shadingNetworkData->ChannelToShadingNetworkMap.size() > 0 )
	{
		for( shadingNetworkIterator = shadingNetworkData->ChannelToShadingNetworkMap.begin();
		     shadingNetworkIterator != shadingNetworkData->ChannelToShadingNetworkMap.end();
		     shadingNetworkIterator++ )
		{
			const char* cChannelName = shadingNetworkIterator->first.c_str();

			if( !sgMaterial->HasMaterialChannel( cChannelName ) )
				sgMaterial->AddMaterialChannel( cChannelName );

			sgMaterial->SetShadingNetwork( cChannelName, shadingNetworkIterator->second->sgExitNode );

			// fetch all shading texture nodes from sgsdkMaterial
			spShadingNode sgExitNode = sgMaterial->GetShadingNetwork( cChannelName );
			std::map<std::basic_string<TCHAR>, spShadingTextureNode> tTexturePathToShadingNodeMap;
			this->materialHandler->FindAllUpStreamTextureNodes( sgExitNode, tTexturePathToShadingNodeMap );

			for( std::pair<std::basic_string<TCHAR>, spShadingTextureNode> tTexturePathToShadingNodeIterator : tTexturePathToShadingNodeMap )
			{
				spShadingTextureNode sgsdkTextureNode = tTexturePathToShadingNodeIterator.second;

				spString rTextureName = sgsdkTextureNode->GetTextureName();
				if( rTextureName.IsNullOrEmpty() )
				{
					std::string sErrorMessage = "Could not resolve a valid texture for a texture node on ";
					sErrorMessage += mMaterialName.asChar();
					sErrorMessage += "::";
					sErrorMessage += cChannelName;
					sErrorMessage += "!";

					this->cmd->LogWarningToWindow( sErrorMessage );
					continue;
				}

				std::basic_string<TCHAR> tTexturePathWithName = ConstCharPtrToLPCTSTR( rTextureName );

				// if full path found, use it
				const std::map<spShadingTextureNode, std::basic_string<TCHAR>>::const_iterator& tShadingNodeToPathIterator =
				    this->ShadingTextureNodeToPath.find( sgsdkTextureNode );
				if( tShadingNodeToPathIterator != this->ShadingTextureNodeToPath.end() )
				{
					tTexturePathWithName = tShadingNodeToPathIterator->second;
				}

				std::basic_string<TCHAR> tTextureName = GetTitleOfFile( tTexturePathWithName );
				std::basic_string<TCHAR> tTextureExtension = GetExtensionOfFile( tTexturePathWithName );
				std::basic_string<TCHAR> tTextureNameWithExtension = tTextureName + tTextureExtension;

				spString sgsdkTexCoordName = sgsdkTextureNode->GetTexCoordName();
				const bool sgsdkUseSRGB = sgsdkTextureNode->GetUseSRGB();
				const uint sgsdkParamCount = sgsdkTextureNode->GetParameterCount();

				// create texture and add it to scene
				spTexture sgTexture;

				sgTexture = sg->CreateTexture();
				sgTexture->SetName( tTextureName.c_str() );
				sgTexture->SetFilePath( tTexturePathWithName.c_str() );

				const int textureId = this->materialHandler->GetTextureTable()->AddTexture( sgTexture );
			}
		}
	}

	std::string globalMaterialIndex = sgMaterial->GetMaterialGUID();

	return globalMaterialIndex;
}

std::basic_string<TCHAR> CreateColorShadingNetwork( float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f )
{
	spShadingColorNode sgColorNode = sg->CreateShadingColorNode();
	sgColorNode->SetColor( r, g, b, a );

	spMaterial sgMaterial = sg->CreateMaterial();
	sgMaterial->SetShadingNetwork( "Diffuse", sgColorNode );

	spString rShadingNetwork = sgMaterial->SaveShadingNetworkToXML( "Diffuse" );
	std::basic_string<TCHAR> tShadingNetwork = ConstCharPtrToLPCTSTR( rShadingNetwork );

	return tShadingNetwork;
}

MaterialColorOverride* MaterialNode::GetMaterialColorOverrideForChannel( std::string sMaterialName, std::string sMaterialChannelName )
{
	std::vector<MaterialColorOverride>& materialColorOverrides = this->materialHandler->GetMaterialColorOverrides();
	MString mMaterialName = MString( sMaterialName.c_str() ).toLowerCase();

	for( uint i = 0; i < materialColorOverrides.size(); ++i )
	{
		if( MString( materialColorOverrides[ i ].MaterialName ).toLowerCase() != mMaterialName )
			continue;

		MString mColorType = materialColorOverrides[ i ].ColorType;

		if( mColorType.toLowerCase() == MString( sMaterialChannelName.c_str() ).toLowerCase() )
		{
			return &materialColorOverrides[ i ];
		}
	}

	return nullptr;
}

void MaterialNode::HandleMaterialOverride()
{
	std::vector<MaterialColorOverride>& materialColorOverrides = this->materialHandler->GetMaterialColorOverrides();

	for( uint i = 0; i < materialColorOverrides.size(); ++i )
	{
		if( MString( this->ShadingNodeName ).toLowerCase() != MString( materialColorOverrides[ i ].MaterialName ).toLowerCase() )
			continue;

		MString mColorType = materialColorOverrides[ i ].ColorType;

		if( mColorType.toLowerCase() == MAYA_MATERIAL_CHANNEL_AMBIENTCOLOR )
		{
			this->AmbientValue.ColorValue[ 0 ] = materialColorOverrides[ i ].ColorValue[ 0 ];
			this->AmbientValue.ColorValue[ 1 ] = materialColorOverrides[ i ].ColorValue[ 1 ];
			this->AmbientValue.ColorValue[ 2 ] = materialColorOverrides[ i ].ColorValue[ 2 ];
			this->AmbientValue.ColorValue[ 3 ] = materialColorOverrides[ i ].ColorValue[ 3 ];
		}
		else if( mColorType.toLowerCase() == MAYA_MATERIAL_CHANNEL_SPECULARCOLOR )
		{
			this->SpecularValue.ColorValue[ 0 ] = materialColorOverrides[ i ].ColorValue[ 0 ];
			this->SpecularValue.ColorValue[ 1 ] = materialColorOverrides[ i ].ColorValue[ 1 ];
			this->SpecularValue.ColorValue[ 2 ] = materialColorOverrides[ i ].ColorValue[ 2 ];
			this->SpecularValue.ColorValue[ 3 ] = materialColorOverrides[ i ].ColorValue[ 3 ];
		}
		else if( mColorType.toLowerCase() == MAYA_MATERIAL_CHANNEL_COLOR )
		{
			this->ColorValue.ColorValue[ 0 ] = materialColorOverrides[ i ].ColorValue[ 0 ];
			this->ColorValue.ColorValue[ 1 ] = materialColorOverrides[ i ].ColorValue[ 1 ];
			this->ColorValue.ColorValue[ 2 ] = materialColorOverrides[ i ].ColorValue[ 2 ];
			this->ColorValue.ColorValue[ 3 ] = materialColorOverrides[ i ].ColorValue[ 3 ];
		}
		else if( mColorType.toLowerCase() == MAYA_MATERIAL_CHANNEL_INCANDESCENCE )
		{
			this->IncandescenceValue.ColorValue[ 0 ] = materialColorOverrides[ i ].ColorValue[ 0 ];
			this->IncandescenceValue.ColorValue[ 1 ] = materialColorOverrides[ i ].ColorValue[ 1 ];
			this->IncandescenceValue.ColorValue[ 2 ] = materialColorOverrides[ i ].ColorValue[ 2 ];
			this->IncandescenceValue.ColorValue[ 3 ] = materialColorOverrides[ i ].ColorValue[ 3 ];
		}
		else if( mColorType.toLowerCase() == MAYA_MATERIAL_CHANNEL_TRANSPARENCY )
		{
			this->TransparencyValue.ColorValue[ 0 ] = materialColorOverrides[ i ].ColorValue[ 0 ];
			this->TransparencyValue.ColorValue[ 1 ] = materialColorOverrides[ i ].ColorValue[ 1 ];
			this->TransparencyValue.ColorValue[ 2 ] = materialColorOverrides[ i ].ColorValue[ 2 ];
			this->TransparencyValue.ColorValue[ 3 ] = materialColorOverrides[ i ].ColorValue[ 3 ];
		}
		else if( mColorType.toLowerCase() == MAYA_MATERIAL_CHANNEL_TRANSLUECENCE )
		{
			this->TranslucenceValue.ColorValue[ 0 ] = materialColorOverrides[ i ].ColorValue[ 0 ];
			this->TranslucenceValue.ColorValue[ 1 ] = materialColorOverrides[ i ].ColorValue[ 1 ];
			this->TranslucenceValue.ColorValue[ 2 ] = materialColorOverrides[ i ].ColorValue[ 2 ];
			this->TranslucenceValue.ColorValue[ 3 ] = materialColorOverrides[ i ].ColorValue[ 3 ];
		}
		else if( mColorType.toLowerCase() == MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_DEPTH )
		{
			this->TranslucenceDepthValue.ColorValue[ 0 ] = materialColorOverrides[ i ].ColorValue[ 0 ];
			this->TranslucenceDepthValue.ColorValue[ 1 ] = materialColorOverrides[ i ].ColorValue[ 1 ];
			this->TranslucenceDepthValue.ColorValue[ 2 ] = materialColorOverrides[ i ].ColorValue[ 2 ];
			this->TranslucenceDepthValue.ColorValue[ 3 ] = materialColorOverrides[ i ].ColorValue[ 3 ];
		}
		else if( mColorType.toLowerCase() == MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_FOCUS )
		{
			this->TranslucenceFocusValue.ColorValue[ 0 ] = materialColorOverrides[ i ].ColorValue[ 0 ];
			this->TranslucenceFocusValue.ColorValue[ 1 ] = materialColorOverrides[ i ].ColorValue[ 1 ];
			this->TranslucenceFocusValue.ColorValue[ 2 ] = materialColorOverrides[ i ].ColorValue[ 2 ];
			this->TranslucenceFocusValue.ColorValue[ 3 ] = materialColorOverrides[ i ].ColorValue[ 3 ];
		}
	}
}

bool MaterialNode::MaterialChannelHasShadingNetwork( const char* cChannelName )
{
	if( !this->sgMaterial->HasMaterialChannel( cChannelName ) )
		return false;

	if( this->sgMaterial->GetShadingNetwork( cChannelName ).IsNull() )
		return false;

	return true;
}

void MaterialNode::CreateAndAssignColorNode( const char* cChannelName, const float colors[ 4 ] )
{
	this->CreateAndAssignColorNode( cChannelName, colors[ 0 ], colors[ 1 ], colors[ 2 ], colors[ 3 ] );
}

void MaterialNode::CreateAndAssignColorNode( const char* cChannelName, float r, float g, float b, float a )
{
	if( !sgMaterial->HasMaterialChannel( cChannelName ) )
	{
		sgMaterial->AddMaterialChannel( cChannelName );
	}

	spShadingColorNode sgColorNode = sg->CreateShadingColorNode();
	sgColorNode->SetColor( r, g, b, a );
	sgMaterial->SetShadingNetwork( cChannelName, sgColorNode );
}

void MaterialNode::CreateAndAssignColorNode( const char* cChannelName, float v )
{
	this->CreateAndAssignColorNode( cChannelName, v, v, v, v );
}

std::string MaterialNode::GetSimplygonMaterialForShape( MeshNode* meshNode )
{
	const uint numTextureChannels = 9;
	bool textureInUse[ numTextureChannels ];
	SetArray<bool>( textureInUse, numTextureChannels, false );

	bool sRGBInUse[ numTextureChannels ];
	SetArray<bool>( sRGBInUse, numTextureChannels, false );

	const uint userTextureInUseSize = (uint)this->UserTextures.size();
	bool* userTexturesInUse = nullptr;
	bool* userSRGBInUse = nullptr;
	if( UserTextures.size() > 0 )
	{
		userTexturesInUse = new bool[ userTextureInUseSize ];
		SetArray<bool>( userTexturesInUse, userTextureInUseSize, false );
		userSRGBInUse = new bool[ userTextureInUseSize ];
		SetArray<bool>( userSRGBInUse, userTextureInUseSize, false );
	}

	this->HandleMaterialOverride();

	// material channels and textures
	if( this->AmbientTextures.TextureLayers.size() > 0 )
	{
		this->CreateSgMaterialChannel( MAYA_MATERIAL_CHANNEL_AMBIENTCOLOR, meshNode, this->AmbientTextures, textureInUse[ 0 ], sRGBInUse[ 0 ] );
	}

	if( this->ColorTextures.TextureLayers.size() > 0 )
	{
		this->CreateSgMaterialChannel( MAYA_MATERIAL_CHANNEL_COLOR, meshNode, this->ColorTextures, textureInUse[ 1 ], sRGBInUse[ 1 ] );
	}

	if( this->SpecularColorTextures.TextureLayers.size() > 0 )
	{
		this->CreateSgMaterialChannel( MAYA_MATERIAL_CHANNEL_SPECULARCOLOR, meshNode, this->SpecularColorTextures, textureInUse[ 2 ], sRGBInUse[ 2 ] );
	}

	if( this->TransparencyTextures.TextureLayers.size() > 0 )
	{
		this->CreateSgMaterialChannel( MAYA_MATERIAL_CHANNEL_TRANSPARENCY, meshNode, this->TransparencyTextures, textureInUse[ 3 ], sRGBInUse[ 3 ] );
	}

	if( this->TranslucenceTextures.TextureLayers.size() > 0 )
	{
		this->CreateSgMaterialChannel( MAYA_MATERIAL_CHANNEL_TRANSLUECENCE, meshNode, this->TranslucenceTextures, textureInUse[ 4 ], sRGBInUse[ 4 ] );
	}

	if( this->TranslucenceDepthTextures.TextureLayers.size() > 0 )
	{
		this->CreateSgMaterialChannel(
		    MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_DEPTH, meshNode, this->TranslucenceDepthTextures, textureInUse[ 5 ], sRGBInUse[ 5 ] );
	}

	if( this->TranslucenceFocusTextures.TextureLayers.size() > 0 )
	{
		this->CreateSgMaterialChannel(
		    MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_FOCUS, meshNode, this->TranslucenceFocusTextures, textureInUse[ 6 ], sRGBInUse[ 6 ] );
	}

	if( this->IncandescenceTextures.TextureLayers.size() > 0 )
	{
		this->CreateSgMaterialChannel( MAYA_MATERIAL_CHANNEL_INCANDESCENCE, meshNode, this->IncandescenceTextures, textureInUse[ 7 ], sRGBInUse[ 7 ] );
	}

	if( this->NormalCameraTextures.TextureLayers.size() > 0 )
	{
		this->CreateSgMaterialChannel( MAYA_MATERIAL_CHANNEL_NORMALCAMERA, meshNode, this->NormalCameraTextures, textureInUse[ 8 ], sRGBInUse[ 8 ] );
	}

	// user material channels and textures (custom channels)
	for( uint textureIndex = 0; textureIndex < this->UserTextures.size(); ++textureIndex )
	{
		if( this->UserTextures[ textureIndex ].TextureLayers.size() > 0 )
		{
			if( userSRGBInUse != nullptr )
			{
				this->CreateSgMaterialChannel( this->UserTextures[ textureIndex ].MappingChannelName.asChar(),
				                               meshNode,
				                               this->UserTextures[ textureIndex ],
				                               userTexturesInUse[ textureIndex ],
				                               userSRGBInUse[ textureIndex ] );
			}
		}
	}

	const std::vector<MaterialColorOverride>& colorOverrides = this->materialHandler->GetMaterialColorOverrides();
	for( int i = 0; i < colorOverrides.size(); ++i )
	{
		// if user defined channels have colors...
		const MaterialColorOverride& colorOverride = colorOverrides[ i ];
		const char* cChannelName = colorOverride.ColorType.asChar();
		const char* cMaterialName = colorOverride.MaterialName.asChar();

		if( MString( sgMaterial->GetName() ).toLowerCase() != MString( cMaterialName ).toLowerCase() )
			continue;

		if( !MaterialChannelHasShadingNetwork( cChannelName ) )
		{
			this->CreateAndAssignColorNode( cChannelName, colorOverride.ColorValue );
		}
	}

	// material channels and colors
	if( !MaterialChannelHasShadingNetwork( MAYA_MATERIAL_CHANNEL_AMBIENTCOLOR ) )
	{
		this->CreateAndAssignColorNode( MAYA_MATERIAL_CHANNEL_AMBIENTCOLOR, this->AmbientValue.ColorValue );
	}

	if( !MaterialChannelHasShadingNetwork( MAYA_MATERIAL_CHANNEL_COLOR ) )
	{
		this->CreateAndAssignColorNode( MAYA_MATERIAL_CHANNEL_COLOR, this->ColorValue.ColorValue );
	}

	if( !MaterialChannelHasShadingNetwork( MAYA_MATERIAL_CHANNEL_SPECULARCOLOR ) )
	{
		this->CreateAndAssignColorNode( MAYA_MATERIAL_CHANNEL_SPECULARCOLOR, this->SpecularValue.ColorValue );
	}

	if( !MaterialChannelHasShadingNetwork( MAYA_MATERIAL_CHANNEL_TRANSPARENCY ) )
	{
		this->CreateAndAssignColorNode( MAYA_MATERIAL_CHANNEL_TRANSPARENCY, this->TransparencyValue.ColorValue );
	}

	if( !MaterialChannelHasShadingNetwork( MAYA_MATERIAL_CHANNEL_TRANSLUECENCE ) )
	{
		this->CreateAndAssignColorNode( MAYA_MATERIAL_CHANNEL_TRANSLUECENCE, this->TranslucenceValue.ColorValue );
	}

	if( !MaterialChannelHasShadingNetwork( MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_DEPTH ) )
	{
		this->CreateAndAssignColorNode( MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_DEPTH, this->TranslucenceDepthValue.ColorValue );
	}

	if( !MaterialChannelHasShadingNetwork( MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_FOCUS ) )
	{
		this->CreateAndAssignColorNode( MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_FOCUS, this->TranslucenceFocusValue.ColorValue );
	}

	if( !MaterialChannelHasShadingNetwork( MAYA_MATERIAL_CHANNEL_INCANDESCENCE ) )
	{
		this->CreateAndAssignColorNode( MAYA_MATERIAL_CHANNEL_INCANDESCENCE, this->IncandescenceValue.ColorValue );
	}

	// handle user defined channels and color
	for( uint textureIndex = 0; textureIndex < this->UserTextures.size(); ++textureIndex )
	{
		const char* cChannelName = this->UserTextures[ textureIndex ].MappingChannelName.asChar();
		if( !MaterialChannelHasShadingNetwork( cChannelName ) )
		{
			this->CreateAndAssignColorNode( cChannelName, 1.f );
		}
	}

	delete[] userTexturesInUse;
	delete[] userSRGBInUse;

	return std::string( sgMaterial->GetMaterialGUID() );
}

int MaterialHandler::FindUVSetIndex( MObject mShapeObject, std::vector<MString>& mUVSets, const TextureProperties& textureLayer )
{
	MFnDependencyNode mDependencyNode( mShapeObject );

	MString mShapeName = mDependencyNode.name();
	MString mTextureName = textureLayer.OriginalTextureFileName;

	// look for an override of the uv set for this shape/texture combo
	for( uint uvOverrideIndex = 0; uvOverrideIndex < TextureShapeUVLinkageOverrides.size(); ++uvOverrideIndex )
	{
		// if names
		if( mShapeName == TextureShapeUVLinkageOverrides[ uvOverrideIndex ].Node &&
		    IsSamePath( mTextureName.asChar(), TextureShapeUVLinkageOverrides[ uvOverrideIndex ].TextureName.asChar() ) )
		{
			MString mUVSetName = TextureShapeUVLinkageOverrides[ uvOverrideIndex ].UVSet;

			for( uint uvIndex = 0; uvIndex < mUVSets.size(); ++uvIndex )
			{
				if( mUVSetName == mUVSets[ uvIndex ] )
				{
					return uvIndex;
				}
			}
		}
	}

	for( uint uvLinkageIndex = 0; uvLinkageIndex < textureLayer.TextureUVLinkage.size(); ++uvLinkageIndex )
	{
		const TextureShapeUVLinkage& uvLink = textureLayer.TextureUVLinkage[ uvLinkageIndex ];
		if( uvLink.Node == mShapeObject )
		{
			// we have a node, find the UV set
			for( uint uvIndex = 0; uvIndex < mUVSets.size(); ++uvIndex )
			{
				if( uvLink.UVSet == mUVSets[ uvIndex ] )
				{
					return uvIndex;
				}
			}
		}
	}

	// none found, return default 0
	return 0;
}

// DESC: Looks for if the texture has alpha (by looking at the header)
bool TextureHasAlpha( const char* cTexFilePath )
{
	// create an image data importer instance
	spImageDataImporter sgImporter = sg->CreateImageDataImporter();

	// set import path
	sgImporter->SetImportFilePath( cTexFilePath );

	// set to load header only
	sgImporter->SetImportOnlyHeader( true );

	// try to import image
	const bool bImageLoaded = sgImporter->RunImport();
	if( bImageLoaded )
	{
		const uint numChannels = sgImporter->GetNumberOfChannels();
		if( numChannels == 4 )
		{
			return true;
		}
	}

	return false;
}

spShadingNode AddExitNodeToLayeredBlendNode( spShadingLayeredBlendNode sgLayeredNode, int layer, spShadingNode sgExitNode )
{
	sgLayeredNode->SetInput( layer, sgExitNode );

	return (spShadingNode)sgLayeredNode;
}

spShadingNode GenerateSgTextureNodeFromLayer( const TextureProperties* textureLayer, std::basic_string<TCHAR> tMayaUVSet, bool bIsSRGB )
{
	std::basic_string<TCHAR> tTexturePathWithName = textureLayer->TextureFileName.asChar();
	std::basic_string<TCHAR> tTextureName = GetTitleOfFile( tTexturePathWithName );
	std::basic_string<TCHAR> tTextureExtension = GetExtensionOfFile( tTexturePathWithName );
	std::basic_string<TCHAR> tTextureNameWithExtension = tTextureName + tTextureExtension;

	const float repeatU = textureLayer->RepeatUV[ 0 ];
	const float repeatV = textureLayer->RepeatUV[ 1 ];

	const float offsetU = textureLayer->OffsetUV[ 0 ];
	const float offsetV = textureLayer->OffsetUV[ 1 ];

	spShadingTextureNode c = sg->CreateShadingTextureNode();
	c->SetTextureName( tTextureName.c_str() );
	c->SetTexCoordName( tMayaUVSet.c_str() );
	c->SetTileU( repeatU );
	c->SetTileV( repeatV );
	c->SetOffsetU( offsetU );
	c->SetOffsetV( offsetV );
	c->SetUseSRGB( bIsSRGB );

	return c;
}

void MaterialNode::AddTextureToSimplygonScene( std::basic_string<TCHAR> tTextureFileName )
{
	std::basic_string<TCHAR> tTexturePathWithName = tTextureFileName;
	std::basic_string<TCHAR> tTextureName = GetTitleOfFile( tTexturePathWithName );
	std::basic_string<TCHAR> tTextureExtension = GetExtensionOfFile( tTexturePathWithName );
	std::basic_string<TCHAR> tTextureNameWithExtension = tTextureName + tTextureExtension;

	spTexture sgTexture;

	sgTexture = sg->CreateTexture();
	sgTexture->SetName( tTextureName.c_str() );
	sgTexture->SetFilePath( tTexturePathWithName.c_str() );

	const int textureIndex = this->materialHandler->GetTextureTable()->AddTexture( sgTexture );
}

spShadingNode OverrideAlphaForShadingNode( spShadingNode sgFinalBaseOutput, spShadingNode sgLayeredAlpha )
{
	// combine RGB from base texture and R from alpha texture
	spShadingSwizzlingNode sgFinalCompositeOutput = sg->CreateShadingSwizzlingNode();
	sgFinalCompositeOutput->SetInput( 0, sgFinalBaseOutput );
	sgFinalCompositeOutput->SetInput( 1, sgFinalBaseOutput );
	sgFinalCompositeOutput->SetInput( 2, sgFinalBaseOutput );
	sgFinalCompositeOutput->SetInput( 3, sgLayeredAlpha );

	sgFinalCompositeOutput->SetRedComponent( 0 );
	sgFinalCompositeOutput->SetGreenComponent( 1 );
	sgFinalCompositeOutput->SetBlueComponent( 2 );
	sgFinalCompositeOutput->SetAlphaComponent( 0 );

	return sgFinalCompositeOutput;
}

void MaterialNode::SetMaterialTextureForMeshNode(
    std::string sMaterialChannel, MeshNode* meshNode, const MaterialTextures& materialTextures, bool& bHasTextures, bool& bsRGB )
{
	static uint counter = 0;

	const std::vector<MaterialTextureLayer>& textureLayers = materialTextures.TextureLayers;

	// get the tri mesh node shape MObject handle
	MDagPath mShapeDagPath;
	if( meshNode )
	{
		mShapeDagPath = meshNode->GetOriginalNode();
		mShapeDagPath.extendToShape();
	}
	else
	{
		return;
	}

	std::string sWorkDirectory = this->cmd->GetWorkDirectoryHandler()->GetOriginalTexturesPath();

	bool nNeedBaseLayer = false;
	if( textureLayers.size() > 0 )
	{
		const MaterialTextureLayer& textureLayer = textureLayers.back();
		nNeedBaseLayer = textureLayer.BlendType != MAYA_BLEND_NONE;
	}

	const uint layerCount = nNeedBaseLayer ? (uint)textureLayers.size() + 1 : (uint)textureLayers.size();
	spShadingLayeredBlendNode sgBlendNode = sg->CreateShadingLayeredBlendNode();
	sgBlendNode->SetInputCount( layerCount );

	if( nNeedBaseLayer )
	{
		spShadingColorNode c = sg->CreateShadingColorNode();
		c->SetColor( 0.f, 0.f, 0.f, 1.f );

		spShadingNode exitNode = AddExitNodeToLayeredBlendNode( sgBlendNode, 0, (spShadingNode)c );
		sgBlendNode->SetPerInputBlendType( 0, ETextureBlendType::Replace );
	}

	uint upLayerIndex = nNeedBaseLayer ? 1 : 0;
	for( int layerIndex = (int)textureLayers.size() - 1; layerIndex >= 0; --layerIndex )
	{
		const MaterialTextureLayer& textureLayer = textureLayers[ layerIndex ];

		// uv string for shading network
		const int mayUVSetIndex = this->materialHandler->FindUVSetIndex( mShapeDagPath.node(), meshNode->GetUVSets(), textureLayer );
		std::basic_string<TCHAR> tMayaUVSet = meshNode->GetUVSets()[ mayUVSetIndex ].asChar();

		// get mapping channel overrides
		const std::vector<MaterialTextureMapChannelOverride>& MaterialTextureMapChannelOverrides = this->materialHandler->GetMaterialChannelOverrides();

		// set texture channel overrides for this material
		for( uint i = 0; i < MaterialTextureMapChannelOverrides.size(); ++i )
		{
			if( MaterialTextureMapChannelOverrides[ i ].Layer == layerIndex )
			{
				// if material exists
				MString mMaterialName = MaterialTextureMapChannelOverrides[ i ].MaterialName;
				mMaterialName.toLowerCase();
				MString mShadingNodeName = ShadingNodeName;
				mShadingNodeName.toLowerCase();

				if( mMaterialName == mShadingNodeName )
				{
					// convert once for every name in list
					MString mCurrentChannelName = MaterialTextureMapChannelOverrides[ i ].MappingChannelName;
					mCurrentChannelName.toLowerCase();

					if( mCurrentChannelName == MString( sMaterialChannel.c_str() ).toLowerCase() )
					{
						tMayaUVSet = MaterialTextureMapChannelOverrides[ i ].NamedMappingChannel.asChar();
					}
				}
			} // end if
		}

		// sRGB string for shading network
		bool bIsSRGB = MColorManagementUtilities::isColorManagementEnabled() ? textureLayer.SRGB : false;
		if( CompareStrings( sMaterialChannel, MAYA_MATERIAL_CHANNEL_NORMALCAMERA ) )
		{
			bIsSRGB = false;
		}

		// if empty name, no texture at all
		if( textureLayer.TextureFileName.length() == 0 )
		{
			// Ignore for now!
		}
		else
		{
			// import texture
			std::basic_string<TCHAR> tTexturePathWithName = textureLayer.TextureFileName.asChar();
			std::basic_string<TCHAR> tTextureName = GetTitleOfFile( tTexturePathWithName );
			std::basic_string<TCHAR> tTextureExtension = GetExtensionOfFile( tTexturePathWithName );
			std::basic_string<TCHAR> tTextureNameWithExtension = tTextureName + tTextureExtension;

			const float repeatU = textureLayer.RepeatUV[ 0 ];
			const float repeatV = textureLayer.RepeatUV[ 1 ];

			const float offsetU = textureLayer.OffsetUV[ 0 ];
			const float offsetV = textureLayer.OffsetUV[ 1 ];

			const MaterialColorOverride* colorOverride = GetMaterialColorOverrideForChannel( ShadingNodeName.asChar(), sMaterialChannel );

			// setup shading network for layered alpha channel
			spShadingNode sgLayeredAlpha;
			const TextureProperties* layeredAlphaTexture = textureLayer.AlphaTexture;
			if( layeredAlphaTexture )
			{
				spShadingColorNode sgLayeredAlphaColor = sg->CreateShadingColorNode();
				sgLayeredAlphaColor->SetColor(
				    layeredAlphaTexture->ColorGain[ 0 ], layeredAlphaTexture->ColorGain[ 1 ], layeredAlphaTexture->ColorGain[ 2 ], 1.f );

				// uv string for alpha
				const int mAlphaUVSetIndex = this->materialHandler->FindUVSetIndex( mShapeDagPath.node(), meshNode->GetUVSets(), *layeredAlphaTexture );
				std::basic_string<TCHAR> mAlphaUVSet = meshNode->GetUVSets()[ mAlphaUVSetIndex ].asChar();

				spShadingNode sgLayeredAlphaTexture = GenerateSgTextureNodeFromLayer( textureLayer.AlphaTexture, mAlphaUVSet, false );

				spShadingMultiplyNode sgFinalLayeredAlphaOutput = sg->CreateShadingMultiplyNode();
				sgFinalLayeredAlphaOutput->SetInput( 0, sgLayeredAlphaColor );
				sgFinalLayeredAlphaOutput->SetInput( 1, sgLayeredAlphaTexture );

				sgLayeredAlpha = sgFinalLayeredAlphaOutput;
			}

			// special case for diffuse (diffuseMult * colorGain * texture)
			if( CompareStrings( sMaterialChannel, MAYA_MATERIAL_CHANNEL_COLOR ) )
			{
				spShadingNode sgBaseTexture = GenerateSgTextureNodeFromLayer( &textureLayer, tMayaUVSet, bIsSRGB );
				spShadingColorNode sgBaseColor = sg->CreateShadingColorNode();

				if( colorOverride != nullptr )
				{
					sgBaseColor->SetColor( colorOverride->ColorValue[ 0 ] * textureLayer.ColorGain[ 0 ],
					                       colorOverride->ColorValue[ 1 ] * textureLayer.ColorGain[ 1 ],
					                       colorOverride->ColorValue[ 2 ] * textureLayer.ColorGain[ 2 ],
					                       colorOverride->ColorValue[ 3 ] );
				}
				else
				{
					sgBaseColor->SetColor( this->ColorValue.ColorValue[ 0 ] * textureLayer.ColorGain[ 0 ],
					                       this->ColorValue.ColorValue[ 1 ] * textureLayer.ColorGain[ 1 ],
					                       this->ColorValue.ColorValue[ 2 ] * textureLayer.ColorGain[ 2 ],
					                       this->ColorValue.ColorValue[ 3 ] );
				}

				spShadingMultiplyNode sgFinalBaseOutput = sg->CreateShadingMultiplyNode();
				sgFinalBaseOutput->SetInput( 0, sgBaseColor );
				sgFinalBaseOutput->SetInput( 1, sgBaseTexture );

				if( layeredAlphaTexture )
				{
					// combine RGB from base texture and R from alpha texture
					spShadingNode sgFinalCompositeOutput = OverrideAlphaForShadingNode( sgFinalBaseOutput, sgLayeredAlpha );
					spShadingNode sgExitNode = AddExitNodeToLayeredBlendNode( sgBlendNode, (int)upLayerIndex, (spShadingNode)sgFinalCompositeOutput );
				}
				else
				{
					// combine RGB from base texture and alpha factor
					spShadingColorNode sgAlphaColor = sg->CreateShadingColorNode();
					sgAlphaColor->SetColor( 1.f, 1.f, 1.f, textureLayer.BlendType == MAYA_BLEND_NONE ? 1.f : textureLayer.LayerAlpha );

					spShadingMultiplyNode sgFinalCompositeOutput = sg->CreateShadingMultiplyNode();
					sgFinalCompositeOutput->SetInput( 0, sgFinalBaseOutput );
					sgFinalCompositeOutput->SetInput( 1, sgAlphaColor );

					spShadingNode sgExitNode = AddExitNodeToLayeredBlendNode( sgBlendNode, (int)upLayerIndex, (spShadingNode)sgFinalCompositeOutput );
				}
			}

			// special case for specular
			else if( CompareStrings( sMaterialChannel, MAYA_MATERIAL_CHANNEL_SPECULARCOLOR ) )
			{
				spShadingNode sgBaseTexture = GenerateSgTextureNodeFromLayer( &textureLayer, tMayaUVSet, bIsSRGB );
				spShadingColorNode sgBaseColor = sg->CreateShadingColorNode();

				if( colorOverride != nullptr )
				{
					sgBaseColor->SetColor( colorOverride->ColorValue[ 0 ] * textureLayer.ColorGain[ 0 ],
					                       colorOverride->ColorValue[ 1 ] * textureLayer.ColorGain[ 1 ],
					                       colorOverride->ColorValue[ 2 ] * textureLayer.ColorGain[ 2 ],
					                       colorOverride->ColorValue[ 3 ] );
				}
				else
				{
					float shininess = this->SpecularValue.ColorValue[ 3 ] / 128.f;
					if( shininess > 1.f )
					{
						shininess = 1.f;
					}

					sgBaseColor->SetColor( 1.f * textureLayer.ColorGain[ 0 ], 1.f * textureLayer.ColorGain[ 1 ], 1.f * textureLayer.ColorGain[ 2 ], shininess );
				}

				spShadingMultiplyNode sgFinalBaseOutput = sg->CreateShadingMultiplyNode();
				sgFinalBaseOutput->SetInput( 0, sgBaseColor );
				sgFinalBaseOutput->SetInput( 1, sgBaseTexture );

				if( layeredAlphaTexture )
				{
					// combine RGB from base texture and R from alpha texture
					spShadingNode sgFinalCompositeOutput = OverrideAlphaForShadingNode( sgFinalBaseOutput, sgLayeredAlpha );
					spShadingNode sgExitNode = AddExitNodeToLayeredBlendNode( sgBlendNode, (int)upLayerIndex, (spShadingNode)sgFinalCompositeOutput );
				}
				else
				{
					spShadingNode sgExitNode = AddExitNodeToLayeredBlendNode( sgBlendNode, upLayerIndex, (spShadingNode)sgFinalBaseOutput );
				}
			}

			// special case for normals
			else if( CompareStrings( sMaterialChannel, MAYA_MATERIAL_CHANNEL_NORMALCAMERA ) )
			{
				spShadingNode sgBaseTexture = GenerateSgTextureNodeFromLayer( &textureLayer, tMayaUVSet, bIsSRGB );

				if( layeredAlphaTexture )
				{
					// combine RGB from base texture and R from alpha texture
					spShadingNode sgFinalCompositeOutput = OverrideAlphaForShadingNode( sgBaseTexture, sgLayeredAlpha );
					spShadingNode sgExitNode = AddExitNodeToLayeredBlendNode( sgBlendNode, (int)upLayerIndex, (spShadingNode)sgFinalCompositeOutput );
				}
				else
				{
					spShadingNode sgExitNode = AddExitNodeToLayeredBlendNode( sgBlendNode, upLayerIndex, (spShadingNode)sgBaseTexture );
				}
			}

			// special case for opacity
			else if( CompareStrings( sMaterialChannel, MAYA_MATERIAL_CHANNEL_TRANSPARENCY ) )
			{
				const bool bHasAlpha = TextureHasAlpha( textureLayer.OriginalTextureFileName.asChar() );

				spShadingNode sgBaseTexture = GenerateSgTextureNodeFromLayer( &textureLayer, tMayaUVSet, bIsSRGB );

				spShadingSwizzlingNode sgBaseTextureSwizzle = sg->CreateShadingSwizzlingNode();
				sgBaseTextureSwizzle->SetInput( 0, sgBaseTexture );
				sgBaseTextureSwizzle->SetInput( 1, sgBaseTexture );
				sgBaseTextureSwizzle->SetInput( 2, sgBaseTexture );
				sgBaseTextureSwizzle->SetInput( 3, sgBaseTexture );

				if( bHasAlpha )
				{
					sgBaseTextureSwizzle->SetRedComponent( 3 );
					sgBaseTextureSwizzle->SetGreenComponent( 3 );
					sgBaseTextureSwizzle->SetBlueComponent( 3 );
					sgBaseTextureSwizzle->SetAlphaComponent( 3 );
				}
				else
				{
					sgBaseTextureSwizzle->SetRedComponent( 0 );
					sgBaseTextureSwizzle->SetGreenComponent( 0 );
					sgBaseTextureSwizzle->SetBlueComponent( 0 );
					sgBaseTextureSwizzle->SetAlphaComponent( 0 );
				}

				spShadingColorNode sgBaseColor = sg->CreateShadingColorNode();
				sgBaseColor->SetColor( textureLayer.ColorGain[ 0 ], textureLayer.ColorGain[ 1 ], textureLayer.ColorGain[ 2 ], 1.f );

				spShadingMultiplyNode sgFinalBaseOutput = sg->CreateShadingMultiplyNode();
				sgFinalBaseOutput->SetInput( 0, sgBaseTextureSwizzle );
				sgFinalBaseOutput->SetInput( 1, sgBaseColor );

				if( layeredAlphaTexture )
				{
					// combine RGB from base texture and R from alpha texture
					spShadingNode sgFinalCompositeOutput = OverrideAlphaForShadingNode( sgFinalBaseOutput, sgLayeredAlpha );
					spShadingNode sgExitNode = AddExitNodeToLayeredBlendNode( sgBlendNode, (int)upLayerIndex, (spShadingNode)sgFinalCompositeOutput );
				}
				else
				{
					spShadingNode sgExitNode = AddExitNodeToLayeredBlendNode( sgBlendNode, upLayerIndex, (spShadingNode)sgFinalBaseOutput );
				}
			}

			// if other channel (colorGain * texture)
			else
			{
				spShadingNode sgBaseTexture = GenerateSgTextureNodeFromLayer( &textureLayer, tMayaUVSet, bIsSRGB );

				spShadingColorNode sgBlackColor = sg->CreateShadingColorNode();
				sgBlackColor->SetColor( 0.f, 0.f, 0.f, 1.f );

				spShadingColorNode sgBaseColor = sg->CreateShadingColorNode();
				if( colorOverride != nullptr )
				{
					sgBaseColor->SetColor( colorOverride->ColorValue[ 0 ] * textureLayer.ColorGain[ 0 ],
					                       colorOverride->ColorValue[ 1 ] * textureLayer.ColorGain[ 1 ],
					                       colorOverride->ColorValue[ 2 ] * textureLayer.ColorGain[ 2 ],
					                       colorOverride->ColorValue[ 3 ] );
				}
				else
				{
					sgBaseColor->SetColor( textureLayer.ColorGain[ 0 ], textureLayer.ColorGain[ 1 ], textureLayer.ColorGain[ 2 ], 1.f );
				}

				spShadingInterpolateNode sgFinalBaseOutput = sg->CreateShadingInterpolateNode();
				sgFinalBaseOutput->SetInput( 0, sgBlackColor );
				sgFinalBaseOutput->SetInput( 1, sgBaseTexture );
				sgFinalBaseOutput->SetInput( 2, sgBaseColor );

				if( layeredAlphaTexture )
				{
					// combine RGB from base texture and R from alpha texture
					spShadingNode sgFinalCompositeOutput = OverrideAlphaForShadingNode( sgFinalBaseOutput, sgLayeredAlpha );
					spShadingNode sgExitNode = AddExitNodeToLayeredBlendNode( sgBlendNode, (int)upLayerIndex, (spShadingNode)sgFinalCompositeOutput );
				}
				else
				{
					spShadingNode sgExitNode = AddExitNodeToLayeredBlendNode( sgBlendNode, upLayerIndex, (spShadingNode)sgFinalBaseOutput );
				}
			}

			// add textures to Simplygon scene
			AddTextureToSimplygonScene( tTexturePathWithName );

			if( layeredAlphaTexture )
			{
				AddTextureToSimplygonScene( layeredAlphaTexture->TextureFileName.asChar() );
			}

			bHasTextures = true;
			bsRGB = bIsSRGB;

			switch( textureLayer.BlendType )
			{
				case MAYA_BLEND_ADD: sgBlendNode->SetPerInputBlendType( upLayerIndex, ETextureBlendType::AddWAlpha ); break;
				case MAYA_BLEND_MULTIPLY: sgBlendNode->SetPerInputBlendType( upLayerIndex, ETextureBlendType::MultiplyWAlpha ); break;
				case MAYA_BLEND_SUBTRACT: sgBlendNode->SetPerInputBlendType( upLayerIndex, ETextureBlendType::SubtractWAlpha ); break;
				case MAYA_BLEND_OVER: sgBlendNode->SetPerInputBlendType( upLayerIndex, ETextureBlendType::Over ); break;
				case MAYA_BLEND_IN: sgBlendNode->SetPerInputBlendType( upLayerIndex, ETextureBlendType::In ); break;
				case MAYA_BLEND_OUT: sgBlendNode->SetPerInputBlendType( upLayerIndex, ETextureBlendType::Out ); break;
				default: sgBlendNode->SetPerInputBlendType( upLayerIndex, ETextureBlendType::Replace ); break;
			}
		}

		if( CompareStrings( sMaterialChannel, MAYA_MATERIAL_CHANNEL_NORMALCAMERA ) )
		{
			sgMaterial->SetUseTangentSpaceNormals( textureLayer.HasTangentSpaceNormals );
		}

		upLayerIndex++;
	}

	if( !this->sgMaterial->HasMaterialChannel( sMaterialChannel.c_str() ) )
	{
		this->sgMaterial->AddMaterialChannel( sMaterialChannel.c_str() );
	}

	this->sgMaterial->SetShadingNetwork( sMaterialChannel.c_str(), sgBlendNode );
}

MObject MaterialNode::GetConnectedNamedPlug( const MFnDependencyNode& mDependencyNode, MString mPlugName )
{
	MObject mNode = MObject::kNullObj;
	// Required for BinSkim compat
	// TODO: Deprecated method, should be replaced!
	SG_DISABLE_SPECIFIC_BEGIN( 4996 )
	MPlug mNodePlug = mDependencyNode.findPlug( mPlugName );
	SG_DISABLE_SPECIFIC_END
	if( !mNodePlug.isNull() )
	{
		// find the shader node that is connected to the object set
		MPlugArray connectedPlugs;
		mNodePlug.connectedTo( connectedPlugs, true, false );
		if( connectedPlugs.length() > 0 )
		{
			// the shader node was found, use this as the
			mNode = connectedPlugs[ 0 ].node();
		}
	}
	return mNode;
}

/*void MaterialNode::SetLinkageOnModifiedNode( MeshNode *mesh_node )
{
MDagPath original = mesh_node->GetOriginalNode();
original.extendToShape();
MObject originalNode = original.node();

MDagPath modified = mesh_node->GetModifiedNode();
modified.extendToShape();

this->CopyTextureLinkage( modified , originalNode , this->AmbientTexture );
this->CopyTextureLinkage( modified , originalNode , this->DiffuseTexture );
this->CopyTextureLinkage( modified , originalNode , this->SpecularTexture );
this->CopyTextureLinkage( modified , originalNode , this->NormalsTexture );
}

void MaterialNode::CopyTextureLinkage( MDagPath destnode , MObject srcnode , MaterialTexture &tex_desc )
{
MFnDependencyNode textureFn( tex_desc.TextureNode );

for( uint i=0; i<tex_desc.TextureUVLinkage.size(); ++i )
{
TextureShapeUVLinkage &link = tex_desc.TextureUVLinkage[i];
if( link.Node == srcnode )
{
// found one, setup the linkage
MString cmd = "setUVSetLinkOnSelectedObjects(\"" + link.UVSet + "\" , \"" + textureFn.name() + "\");";
ExecuteSelectedObjectCommand( cmd , destnode , MObject::kNullObj );
}
}
}
*/

MaterialHandler::MaterialHandler( SimplygonCmd* _cmd )
{
	this->cmd = _cmd;
}

MaterialHandler::~MaterialHandler()
{
	// delete allocated materials
	for( uint nodeIndex = 0; nodeIndex < MaterialNodes.size(); ++nodeIndex )
	{
		delete MaterialNodes[ nodeIndex ];
	}

	this->MaterialNodes.clear();
	this->MaterialIdToMaterialNode.clear();
	this->MaterialColorOverrides.clear();
	this->MaterialTextureOverrides.clear();
	this->MaterialTextureMapChannelOverrides.clear();
	this->TextureShapeUVLinkageOverrides.clear();
	this->ChannelToShadingNetworkDataMap.clear();

	for( std::map<std::string, StandardMaterial*>::iterator& it = this->MaterialIdToStandardMaterial.begin(); it != this->MaterialIdToStandardMaterial.end();
	     it++ )
	{
		delete it->second;
	}

	this->MaterialIdToStandardMaterial.clear();
	this->MaterialIdToMaterialIndex.clear();
}

void MaterialHandler::Setup( spMaterialTable sgMaterialTable, spTextureTable sgTextureTable )
{
	this->sgMaterialTable = sgMaterialTable;
	this->sgTextureTable = sgTextureTable;
}

MaterialNode* MaterialHandler::GetMaterial( const MString& mNodeName )
{
	// check if the material already exists in the list
	for( uint nodeIndex = 0; nodeIndex < this->MaterialNodes.size(); ++nodeIndex )
	{
		if( this->MaterialNodes[ nodeIndex ]->GetShadingGroupName() == mNodeName )
		{
			return this->MaterialNodes[ nodeIndex ];
		}
	}

	return nullptr;
}

Simplygon::spTextureTable MaterialHandler::GetTextureTable()
{
	return sgTextureTable;
}

Simplygon::spMaterialTable MaterialHandler::GetMaterialTable()
{
	return this->sgMaterialTable;
}

MaterialNode* MaterialHandler::AddMaterial( MString mMaterialName )
{
	// check if the material already exists in the list
	MaterialNode* currentMaterial = GetMaterial( mMaterialName );
	if( currentMaterial )
	{
		return currentMaterial;
	}

	// if the material is new, add it, and set it up from the scene material
	currentMaterial = new MaterialNode( this->cmd, this );

	if( !currentMaterial->SetupFromName( mMaterialName ) )
	{
		MGlobal::displayError( MString( "Simplygon: Failed to setup material " ) + mMaterialName );
		delete currentMaterial;
		return nullptr;
	}

	const uint mID = (uint)this->MaterialNodes.size();

	this->MaterialNodes.push_back( currentMaterial );

	std::string sgGuidMatID = currentMaterial->sgMaterial->GetMaterialGUID();
	currentMaterial->map_sgguid_to_sg.insert( std::pair<std::string, int>( sgGuidMatID, mID ) );

	return currentMaterial;
}

std::string MaterialHandler::GetSimplygonMaterialForShape( MString mMaterialName, MeshNode* meshNode )
{
	MaterialNode* currentMaterial = GetMaterial( mMaterialName );

	if( !currentMaterial )
	{
		MGlobal::displayError( MString( "Simplygon: Failed to find material " ) + mMaterialName );
		return "";
	}

	for( std::map<std::string, MaterialNode*>::const_iterator& materialMap = this->MaterialIdToMaterialNode.begin();
	     materialMap != this->MaterialIdToMaterialNode.end();
	     materialMap++ )
	{
		if( materialMap->second->Name == mMaterialName )
		{
			return materialMap->first;
		}
	}

	std::string sgGlobalMaterialID = "";

	if( currentMaterial->IsBasedOnSimplygonShadingNetwork )
	{
		sgGlobalMaterialID = currentMaterial->GetSimplygonMaterialWithShadingNetwork( mMaterialName, meshNode );
	}
	else
	{
		// setup a simplygon material, check if it already exists in the material table
		sgGlobalMaterialID = currentMaterial->GetSimplygonMaterialForShape( meshNode );
	}

	const int globalMaterialIndex = this->sgMaterialTable->AddMaterial( currentMaterial->sgMaterial );

	this->MaterialIdToMaterialNode.insert( std::pair<std::string, MaterialNode*>( sgGlobalMaterialID, currentMaterial ) );
	this->MaterialIdToMaterialIndex.insert( std::pair<std::string, int>( sgGlobalMaterialID, globalMaterialIndex ) );

	return sgGlobalMaterialID;
}

MaterialNode* MaterialHandler::GetMaterialFromSimplygonMaterialId( std::string sSgMaterialId )
{
	const std::map<std::string, MaterialNode*>::const_iterator& materialNodeMap = this->MaterialIdToMaterialNode.find( sSgMaterialId );
	if( materialNodeMap == this->MaterialIdToMaterialNode.end() )
	{
		return nullptr;
	}

	return materialNodeMap->second;
}

void MaterialHandler::AddMaterialColorOverride( MString MaterialName, MString ColorType, float r, float g, float b, float a )
{
	MaterialColorOverride t;
	t.MaterialName = MaterialName;
	t.ColorType = ColorType;
	t.ColorValue[ 0 ] = r;
	t.ColorValue[ 1 ] = g;
	t.ColorValue[ 2 ] = b;
	t.ColorValue[ 3 ] = a;
	this->MaterialColorOverrides.push_back( t );
}

void MaterialHandler::AddMaterialTextureOverride( MString MaterialName, MString TextureType, MString TextureName, int Layer, int BlendType, bool tangent_space )
{
	MaterialTextureOverride t;
	t.MaterialName = MaterialName;
	t.TextureType = TextureType;
	t.TextureName = TextureName;
	t.TextureLayer = Layer;
	t.HasTangentSpaceNormals = tangent_space;
	t.BlendType = BlendType;
	this->MaterialTextureOverrides.push_back( t );
}

void MaterialHandler::AddMaterialTextureChannelOverride( MString MaterialName, MString TextureType, int Layer, int Channel )
{
	MaterialTextureMapChannelOverride t;
	t.MaterialName = MaterialName;
	t.MappingChannelName = TextureType;
	t.MappingChannel = Channel;
	t.Layer = Layer;
	this->MaterialTextureMapChannelOverrides.push_back( t );
}

void MaterialHandler::AddMaterialTextureNamedChannelOverride( MString MaterialName, MString TextureType, int Layer, MString Channel )
{
	MaterialTextureMapChannelOverride t;
	t.MaterialName = MaterialName;
	t.MappingChannelName = TextureType;
	t.MappingChannel = -1;
	t.NamedMappingChannel = Channel;
	t.Layer = Layer;
	this->MaterialTextureMapChannelOverrides.push_back( t );
}

void MaterialHandler::AddTextureShapeUVLinkageOverride( MString Node, MString UVSet, MString TextureName )
{
	TextureShapeUVLinkageOverride t;
	t.Node = Node;
	t.UVSet = UVSet;
	t.TextureName = TextureName;
	this->TextureShapeUVLinkageOverrides.push_back( t );
}

void MaterialHandler::FindAllUpStreamTextureNodes( spShadingNode sgShadingNode,
                                                   std::map<std::basic_string<TCHAR>, spShadingTextureNode>& sgShadingNodeNameToTextureNode )
{
	std::vector<spShadingTextureNode> sgTextureNodes;

	if( sgShadingNode.IsNull() )
		return;

	spShadingTextureNode sgTextureNode = Simplygon::spShadingTextureNode::SafeCast( sgShadingNode );
	if( !sgTextureNode.IsNull() )
	{
		std::basic_string<TCHAR> tNodeName;
		if( !sgTextureNode->GetName().IsNullOrEmpty() )
		{
			tNodeName = std::basic_string<TCHAR>( sgTextureNode->GetName() );
		}
		else
		{
			const int nodeCount = (int)sgShadingNodeNameToTextureNode.size();
			tNodeName = string_format( std::basic_string<TCHAR>( "TextureNode_%d" ), nodeCount );
		}

		sgShadingNodeNameToTextureNode[ tNodeName ] = sgTextureNode;
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
						this->FindAllUpStreamTextureNodes( sgFilterNode->GetInput( i ), sgShadingNodeNameToTextureNode );
					}
				}
			}
		}
	}
}

void MaterialHandler::FindAllUpStreamColorNodes( spShadingNode sgShadingNode, std::map<std::basic_string<TCHAR>, spShadingColorNode>& sgNodeNameToColorNode )
{
	if( sgShadingNode.IsNull() )
		return;

	spShadingColorNode sgColorNode = Simplygon::spShadingColorNode::SafeCast( sgShadingNode );
	if( !sgColorNode.IsNull() )
	{
		std::basic_string<TCHAR> tNodeName;
		if( sgColorNode->GetName().NonEmpty() )
		{
			tNodeName = std::basic_string<TCHAR>( sgColorNode->GetName() );
		}
		else
		{
			const int nodeCount = (int)sgNodeNameToColorNode.size();
			tNodeName = string_format( std::basic_string<TCHAR>( "ColorNode_%d" ), nodeCount );
		}

		sgNodeNameToColorNode[ tNodeName ] = sgColorNode;
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
						this->FindAllUpStreamColorNodes( sgFilterNode->GetInput( i ), sgNodeNameToColorNode );
					}
				}
			}
		}
	}
}

void MaterialHandler::FindAllUpStreamVertexColorNodes( spShadingNode sgShadingNode,
                                                       std::map<std::basic_string<TCHAR>, spShadingVertexColorNode>& sgNodeNameToVertexColorNode )
{
	if( sgShadingNode.IsNull() )
		return;

	spShadingVertexColorNode sgVertexColorNode = Simplygon::spShadingVertexColorNode::SafeCast( sgShadingNode );
	if( !sgVertexColorNode.IsNull() )
	{
		std::basic_string<TCHAR> tNodeName;
		if( sgVertexColorNode->GetName().NonEmpty() )
		{
			tNodeName = std::basic_string<TCHAR>( sgVertexColorNode->GetName() );
		}
		else
		{
			const int itemCount = (int)sgNodeNameToVertexColorNode.size();
			tNodeName = string_format( std::basic_string<TCHAR>( "VertexColorNode_%d" ), itemCount );
		}

		sgNodeNameToVertexColorNode[ tNodeName ] = sgVertexColorNode;
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
						this->FindAllUpStreamVertexColorNodes( sgFilterNode->GetInput( i ), sgNodeNameToVertexColorNode );
					}
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//
// Texture import section

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
	BMPheader head = { 0 };
	head.magic[ 0 ] = 66;
	head.magic[ 1 ] = 77;
	head.file_size = width * height * 3 + 54;
	head.unused = 0;
	head.offset = 54;
	head.header_size = 40;
	head.sizeX = width;
	head.sizeY = height;
	head.planes = 1;
	head.BPP = 24;
	head.type = 0;
	head.data_size = width * height * 3; // 24 bpp
	head.dpiX = 2835;
	head.dpiY = 2835;
	head.palette_colors = 0;
	head.important_colors = 0;
	return head;
}

const int STANDIN_TEXTURE_WIDTH = 256;
const int STANDIN_TEXTURE_HEIGHT = 256;

bool WriteStandinTexture( const char* cTargetFilePath )
{
	assert( STANDIN_TEXTURE_WIDTH <= SHRT_MAX && STANDIN_TEXTURE_WIDTH > 0 );
	assert( STANDIN_TEXTURE_HEIGHT <= SHRT_MAX && STANDIN_TEXTURE_HEIGHT > 0 );

	FILE* textureFile = fopen( cTargetFilePath, "wb" );
	if( !textureFile )
		return false;

	const BMPheader textureHeader = SetupBMPHeader( STANDIN_TEXTURE_WIDTH, STANDIN_TEXTURE_HEIGHT );
	fwrite( &textureHeader, sizeof( BMPheader ), 1, textureFile );
	unsigned char* textureData = new unsigned char[ STANDIN_TEXTURE_WIDTH * STANDIN_TEXTURE_HEIGHT * 3 ];

	for( int y = 0; y < STANDIN_TEXTURE_HEIGHT; ++y )
	{
		for( int x = 0; x < STANDIN_TEXTURE_WIDTH; ++x )
		{
			textureData[ ( x + y * STANDIN_TEXTURE_WIDTH ) * 3 + 0 ] = ( x * 0xff ) / STANDIN_TEXTURE_WIDTH;
			if( ( ( ( x >> 3 ) & 0x1 ) ^ ( ( y >> 3 ) & 0x1 ) ) )
			{
				textureData[ ( x + y * STANDIN_TEXTURE_WIDTH ) * 3 + 1 ] = 0;
			}
			else
			{
				textureData[ ( x + y * STANDIN_TEXTURE_WIDTH ) * 3 + 1 ] = 0xff;
			}
			textureData[ ( x + y * STANDIN_TEXTURE_WIDTH ) * 3 + 2 ] = ( y * 0xff ) / STANDIN_TEXTURE_HEIGHT;
		}
	}

	fwrite( textureData, 1, STANDIN_TEXTURE_WIDTH * STANDIN_TEXTURE_HEIGHT * 3, textureFile );
	delete[] textureData;
	fclose( textureFile );

	return true;
}

std::string MaterialHandler::ImportTexture( const MString& mFilePath )
{
	// look for the texture in the list of imported textures
	for( uint textureIndex = 0; textureIndex < this->ImportedTextures.size(); ++textureIndex )
	{
		if( this->ImportedTextures[ textureIndex ].OriginalPath == mFilePath )
		{
			return this->ImportedTextures[ textureIndex ].ImportedPath;
		}
	}

	WorkDirectoryHandler* workDirectoryHandler = this->cmd->GetWorkDirectoryHandler();

	bool textureDirectoryOverrideInUse = false;
	std::basic_string<TCHAR> tTexturePathOverride = workDirectoryHandler->GetTextureOutputDirectoryOverride();
	if( tTexturePathOverride.length() > 0 )
	{
		const bool folderCreated = CreateFolder( tTexturePathOverride );
		if( !folderCreated )
		{
			MGlobal::displayWarning( "Failed to set up the texture path override, please verify the input string and that Maya has the required admin rights "
			                         "for accessing the specified location. Textures will be copied to the default path." );
		}
		else
		{
			textureDirectoryOverrideInUse = true;
		}
	}

	// not found, we have to import it
	std::string sSourcePath = GetFullPathOfFile( std::string( mFilePath.asChar() ) );

	ImportedTexture importedTexture;

	if( cmd->copyTextures )
	{
		// setup a name for the imported texture
		std::string sImportName = GetNonConflictingNameInPath( "", GetTitleOfFile( sSourcePath ).c_str(), GetExtensionOfFile( sSourcePath ).c_str() );

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

		std::string sImportPath = "";

		int indexer = 1;
		while( true )
		{
			// if import directory is set, use it as root
			if( bHasExportDirectory )
			{
				sImportPath = Combine( tExportTexturesDirectory, sImportName );
			}

			// otherwise, use original directory as root
			else
			{
				sImportPath = Combine( tExportOriginalTexturesDirectory, sImportName );
			}

			if( FileExists( sImportPath ) )
			{
				sImportName = GetTitleOfFile( sSourcePath ) + std::to_string( indexer ) + GetExtensionOfFile( sSourcePath ).c_str();
				indexer++;
			}
			else
			{
				break;
			}
		}

		// if we have the texture file, copy it into our work directory
		bool bImported = false;
		if( FileExists( sSourcePath ) )
		{
			if( CopyFile( sSourcePath.c_str(), sImportPath.c_str(), FALSE ) )
			{
				// check file attributes for ReadOnly-flag, remove if possible
				const DWORD dwFileAttributes = GetFileAttributes( sImportPath.c_str() );
				const bool bIsReadOnly = dwFileAttributes & FILE_ATTRIBUTE_READONLY;
				if( bIsReadOnly )
				{
					const BOOL bFileAttributeSet = SetFileAttributes( sImportPath.c_str(), FILE_ATTRIBUTE_NORMAL );
					if( !bFileAttributeSet )
					{
						std::string sWarningMessage = _T("Warning, could not restore file attributes, please make sure that the file has normal file ")
						                              _T("attributes or that Maya has the privileges to change them.\nFile: ");
						sWarningMessage += sImportPath.c_str();
						sWarningMessage += _T("\n\n");

						this->cmd->LogWarningToWindow( sWarningMessage );
					}
				}

				bImported = true;
			}
		}

		// if the texture was not found, or could not be read, use a stand in texture
		if( !bImported )
		{
			MString mWarningMessage;

			mWarningMessage += "Warning: Failed to import texture: ";
			mWarningMessage += sSourcePath.c_str();
			mWarningMessage += ", using a stand-in texture";

			MGlobal::displayWarning( mWarningMessage );

			WriteStandinTexture( sImportPath.c_str() );
		}

		// add the imported texture info
		importedTexture.OriginalPath = mFilePath;
		importedTexture.ImportedPath = sImportPath;
	}
	else
	{
		// add the imported texture info
		importedTexture.OriginalPath = mFilePath;
		importedTexture.ImportedPath = mFilePath.asChar();
	}

	this->ImportedTextures.push_back( importedTexture );

	return importedTexture.ImportedPath;
}

void MaterialHandler::AddMaterialWithShadingNetworks( std::basic_string<TCHAR> tMaterialName )
{
	if( !ChannelToShadingNetworkDataMap.empty() )
	{
		if( ChannelToShadingNetworkDataMap.find( tMaterialName ) == ChannelToShadingNetworkDataMap.end() )
		{
			// material is currently not in the lookup. Setup a new material and add it to the lookup
			spMaterial sgMaterial = sg->CreateMaterial();
			sgMaterial->SetName( tMaterialName.c_str() );
			sgMaterial->SetBlendMode( EMaterialBlendMode::Blend );

			ChannelToShadingNetworkDataMap[ tMaterialName ] = new ShadingNetworkData();
			ChannelToShadingNetworkDataMap[ tMaterialName ]->sgMaterial = sgMaterial;
		}
	}
	else
	{
		spMaterial sgMaterial = sg->CreateMaterial();
		sgMaterial->SetName( tMaterialName.c_str() );
		sgMaterial->SetBlendMode( EMaterialBlendMode::Blend );

		ChannelToShadingNetworkDataMap[ tMaterialName ] = new ShadingNetworkData();
		ChannelToShadingNetworkDataMap[ tMaterialName ]->sgMaterial = sgMaterial;
	}
}

ShadingNetworkData* MaterialHandler::GetMaterialWithShadingNetworks( std::basic_string<TCHAR> tMaterialName )
{
	std::map<std::basic_string<TCHAR>, ShadingNetworkData*>::iterator channelIterator = ChannelToShadingNetworkDataMap.find( tMaterialName );
	if( channelIterator != ChannelToShadingNetworkDataMap.end() )
	{
		return channelIterator->second;
	}

	return nullptr;
}

void MaterialHandler::SetupMaterialChannelNetworkFromXML( std::basic_string<TCHAR> tMaterialName,
                                                          std::basic_string<TCHAR> tChannelName,
                                                          std::basic_string<TCHAR> tXMLString )
{
	ShadingNetworkData* shadingNetworkData = GetMaterialWithShadingNetworks( tMaterialName );
	if( shadingNetworkData == nullptr )
		return;

	if( shadingNetworkData->sgMaterial.NonNull() )
	{
		shadingNetworkData->sgMaterial->LoadShadingNetworkFromXML( tChannelName.c_str(), tXMLString.c_str() );
	}
	else
	{
		shadingNetworkData->sgMaterial = sg->CreateMaterial();
		shadingNetworkData->sgMaterial->LoadShadingNetworkFromXML( tChannelName.c_str(), tXMLString.c_str() );
	}

	shadingNetworkData->sgMaterial->SetBlendMode( EMaterialBlendMode::Blend );
}

bool MaterialHandler::HasMaterialWithXMLNetworks( std::basic_string<TCHAR> tMaterialName )
{
	if( GetMaterialWithShadingNetworks( tMaterialName ) )
	{
		return true;
	}

	return false;
}
