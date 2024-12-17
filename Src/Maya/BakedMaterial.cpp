// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"
#include "SimplygonCmd.h"
#include "BakedMaterial.h"
#include "WorkDirectoryHandler.h"
#include "HelperFunctions.h"
#include "Common.h"
#include <WTypes.h>
#include <iomanip>
#include <sys/stat.h>

#define MaxNumCopyRetries 10u

MStatus StandardMaterial::ImportMaterialTextureFile( const char* cChannelName,
                                                     StandardMaterialChannel* materialChannel,
                                                     MString mMeshNameOverride,
                                                     MString mMaterialNameOverride )
{
	if( this->sgMaterial.IsNull() )
		return MStatus::kSuccess;

	else if( !this->sgMaterial->HasMaterialChannel( cChannelName ) )
		return MStatus::kSuccess;

	MStatus mStatus;

	// import the materials from the import folder into the
	// textures folder of the current project
	MString mTexturePath;

	bool bTextureDirectoryOverrideInUse = false;
	std::basic_string<TCHAR> tTexturePathOverride = this->cmd->GetWorkDirectoryHandler()->GetTextureOutputDirectoryOverride();
	if( tTexturePathOverride.length() > 0 )
	{
		const bool bFolderCreated = CreateFolder( tTexturePathOverride );
		if( !bFolderCreated )
		{
			MGlobal::displayWarning( "Failed to set up the texture path override, please verify the input string and that Maya has the required admin rights "
			                         "for accessing the specified location. Textures will be copied to the default path." );
		}
		else
		{
			bTextureDirectoryOverrideInUse = true;
			mTexturePath = MString( tTexturePathOverride.c_str() );
		}
	}

	if( !bTextureDirectoryOverrideInUse )
	{
		mStatus = GetMayaWorkspaceTextureFolder( mTexturePath );
		if( !mStatus )
		{
			MGlobal::displayError( "Failed to retrieve Maya's texture directory." );
			return mStatus;
		}
	}

	spShadingNode sgExitNode = this->sgMaterial->GetShadingNetwork( cChannelName );
	if( !sgExitNode.IsNull() )
	{
		// get texture directory
		std::basic_string<TCHAR> tBakedTextureDirectory = this->cmd->GetWorkDirectoryHandler()->GetBakedTexturesPath();

		// fetch channel parameters
		std::basic_string<TCHAR> tChannelName = ConstCharPtrToLPCTSTR( cChannelName );

		std::map<std::basic_string<TCHAR>, spShadingTextureNode> sgShadingNodeNameToTextureNode;
		this->cmd->GetMaterialHandler()->FindAllUpStreamTextureNodes( sgExitNode, sgShadingNodeNameToTextureNode );

		if( sgShadingNodeNameToTextureNode.size() > 0 )
		{
			// use texture
			spShadingTextureNode sgTextureNode = sgShadingNodeNameToTextureNode.begin()->second;

			// empty name check
			spString rTextureNameToFind = sgTextureNode->GetTextureName();
			if( rTextureNameToFind.IsNullOrEmpty() )
			{
				MString mErrorMessage = MString( "Found a ShadingTextureNode with invalid (NULL or empty) TextureName, unable to map texture on " ) +
				                        MString( cChannelName ) + MString( " channel." );
				MGlobal::displayError( mErrorMessage );
				return MStatus::kInvalidParameter;
			}

			std::basic_string<TCHAR> tTextureNameToFind = ConstCharPtrToLPCTSTR( rTextureNameToFind );

			// empty tex coord level check
			spString rTextureUvSet = sgTextureNode->GetTexCoordName();
			if( rTextureUvSet.IsNullOrEmpty() )
			{
				MString mErrorMessage = MString( "Found a ShadingTextureNode (" ) + MString( tTextureNameToFind.c_str() ) +
				                        MString( ") with invalid (NULL or empty) UV-set, unable to map texture on " ) + MString( cChannelName ) +
				                        MString( " channel." );

				MGlobal::displayError( mErrorMessage );
				return MStatus::kInvalidParameter;
			}

			std::basic_string<TCHAR> tTextureUvSet = ConstCharPtrToLPCTSTR( rTextureUvSet );

			spTexture sgTexture = this->sgTextureTable->FindTexture( tTextureNameToFind.c_str() );

			if( sgTexture.IsNull() )
			{
				MGlobal::displayError( "Could not resolve texture " + MString( tTextureNameToFind.c_str() ) + MString( " on " ) + MString( cChannelName ) +
				                       MString( " channel." ) );
				return MStatus::kFailure;
			}

			if( sgTexture->GetFilePath().IsNullOrEmpty() && sgTexture->GetImageData().IsNull() )
			{
				MGlobal::displayError( "Invalid path / data (NULL or empty) for texture: " + MString( tTextureNameToFind.c_str() ) + MString( " on " ) +
				                       MString( cChannelName ) + MString( "." ) );
				return MStatus::kFailure;
			}

			std::basic_string<TCHAR> tTextureName = ConstCharPtrToLPCTSTR( sgTexture->GetName() );
			std::basic_string<TCHAR> tTexturePath = sgTexture->GetImageData().IsNull() ? ConstCharPtrToLPCTSTR( sgTexture->GetFilePath() ) : "";
			std::basic_string<TCHAR> tSourceFilePath = Combine( tBakedTextureDirectory, tTexturePath );
			if( !sgTexture->GetImageData().IsNull() )
			{
				// Embedded data, should be written to file
				tSourceFilePath = Combine( tSourceFilePath, tTextureName );
				if( ExportTextureToFile( sg, sgTexture, LPCTSTRToConstCharPtr( tSourceFilePath.c_str() ) ) )
				{
					tSourceFilePath = sgTexture->GetFilePath().c_str();
					sgTexture->SetImageData( Simplygon::NullPtr );
				}
			}

			if( cmd->copyTextures )
			{
				// the name of the imported texture is based on the name of the node
				std::basic_string<TCHAR> tImportTextureName = tTextureName + GetExtensionOfFile( tSourceFilePath );

				ReplaceInvalidCharacters( tImportTextureName, '_' );

				std::basic_string<TCHAR> tImportTexturePath = std::basic_string<TCHAR>( mTexturePath.asChar() );
				tImportTexturePath = Combine( tImportTexturePath, tImportTextureName );

				std::string sFinalImportTexturePath = tImportTexturePath;

				// make sure to get a unique name
				sFinalImportTexturePath = GetNonConflictingNameInPath( tImportTexturePath.c_str() );

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

					// otherwise, output error message
					char tMessageBuffer[ MAX_PATH ] = { 0 };
					sprintf_s( tMessageBuffer, "%u", dwErrorCode );
					MGlobal::displayError( "Could not copy texture:\n " + MString( tSourceFilePath.c_str() ) + "\n " +
					                       MString( sFinalImportTexturePath.c_str() ) + "\n Code: " + MString( tMessageBuffer ) );
					return MStatus::kFailure;
				}

				materialChannel->TexturePath = sFinalImportTexturePath.c_str();
			}

			else
			{
				materialChannel->TexturePath = tSourceFilePath.c_str();
			}

			materialChannel->IsSRGB = sgTextureNode->GetColorSpaceOverride() == Simplygon::EImageColorSpace::sRGB;

			if( tTextureUvSet.length() > 0 )
			{
				materialChannel->UVSet = tTextureUvSet.c_str();
			}
		}
	}

	// add shading group here, not material name
	this->cmd->GetMaterialInfoHandler()->Add(
	    mMeshNameOverride.asChar(), ( mMaterialNameOverride + "SG" ).asChar(), cChannelName, materialChannel->TexturePath.asChar() );
	return MStatus::kSuccess;
}

StandardMaterial::StandardMaterial( SimplygonCmd* cmd, spTextureTable sgTextureTable )
{
	this->cmd = cmd;
	this->sgTextureTable = sgTextureTable;

	this->AmbientChannel = new StandardMaterialChannel( true );
	this->ColorChannel = new StandardMaterialChannel( true );
	this->SpecularChannel = new StandardMaterialChannel( true );
	this->TransparencyChannel = new StandardMaterialChannel( false );
	this->TranslucenceChannel = new StandardMaterialChannel( true );
	this->TranslucenceDepthChannel = new StandardMaterialChannel( true );
	this->TranslucenceFocusChannel = new StandardMaterialChannel( true );
	this->IncandescenceChannel = new StandardMaterialChannel( true );
	this->NormalCameraChannel = new StandardMaterialChannel( false );
	this->ReflectedColorChannel = new StandardMaterialChannel( true );
	this->ReflectivityChannel = new StandardMaterialChannel( true );
}

StandardMaterial::~StandardMaterial()
{
	delete this->AmbientChannel;
	delete this->ColorChannel;
	delete this->SpecularChannel;
	delete this->TransparencyChannel;
	delete this->TranslucenceChannel;
	delete this->TranslucenceDepthChannel;
	delete this->TranslucenceFocusChannel;
	delete this->IncandescenceChannel;
	delete this->NormalCameraChannel;
	delete this->ReflectedColorChannel;
	delete this->ReflectivityChannel;
}

MStatus StandardMaterial::ExtractMapping( MDagPath mShape )
{
	MStatus mStatus;
	MString mCommand;

	MStringArray mUvToTextureMapping;
	mCommand = "GetLink( ";
	mCommand += CreateQuotedText( mShape.fullPathName() );
	mCommand += ");";

	mStatus = ExecuteCommand( mCommand, mUvToTextureMapping );
	if( !mStatus )
	{
		MGlobal::displayError( "Failed to get texture to uv mapping for node: " + mShape.fullPathName() );
		return mStatus;
	}

	for( uint i = 0; i < mUvToTextureMapping.length(); ++i )
	{
		std::string sMap = mUvToTextureMapping[ i ].asChar();
		const size_t splitIndex = sMap.find( "<>" );
		if( splitIndex == std::string::npos )
			continue;

		std::string sUVSet = sMap.substr( 0, splitIndex );
		std::string sTextureNode = sMap.substr( splitIndex + 2, 255 );

		this->UvToTextureNodeMap.push_back( std::pair<std::string, std::string>( sUVSet, sTextureNode ) );
	}

	return MStatus::kSuccess;
}

MStatus StandardMaterial::ImportMapping( MDagPath mShape )
{
	MStatus mStatus = MStatus::kSuccess;

	for( uint i = 0; i < this->UvToTextureNodeMap.size(); ++i )
	{
		MString mUVSet = this->UvToTextureNodeMap[ i ].first.c_str();
		MString mTextureNode = this->UvToTextureNodeMap[ i ].second.c_str();

		MStringArray mUvToTextureMapping;
		MString mCommand = "CreateLink( ";
		mCommand += CreateQuotedText( mShape.fullPathName() ) + ", ";
		mCommand += CreateQuotedText( mUVSet ) + ", ";
		mCommand += CreateQuotedText( mTextureNode );
		mCommand += ");";

		mStatus = ExecuteCommand( mCommand, mUvToTextureMapping );
		if( !mStatus )
		{
			MGlobal::displayError( "Failed to get texture to uv mapping for node: " + mShape.fullPathName() );
			return mStatus;
		}
	}

	return mStatus;
}

MStatus StandardMaterial::CreatePhong( MDagPath mShape, MString mMeshName, MString mMaterialNameOverride, bool isMeshLOD )
{
	MStatus mStatus;

	// import the textures
	mStatus = this->ImportMaterialTextureFile( MAYA_MATERIAL_CHANNEL_AMBIENTCOLOR, this->AmbientChannel, mMeshName, mMaterialNameOverride );
	if( !mStatus )
	{
		MGlobal::displayError( "Failed to import texture: " + this->AmbientChannel->TexturePath );
		return mStatus;
	}
	mStatus = this->ImportMaterialTextureFile( MAYA_MATERIAL_CHANNEL_COLOR, this->ColorChannel, mMeshName, mMaterialNameOverride );
	if( !mStatus )
	{
		MGlobal::displayError( "Failed to import texture: " + this->ColorChannel->TexturePath );
		return mStatus;
	}
	mStatus = this->ImportMaterialTextureFile( MAYA_MATERIAL_CHANNEL_SPECULARCOLOR, this->SpecularChannel, mMeshName, mMaterialNameOverride );
	if( !mStatus )
	{
		MGlobal::displayError( "Failed to import texture: " + this->SpecularChannel->TexturePath );
		return mStatus;
	}
	mStatus = this->ImportMaterialTextureFile( MAYA_MATERIAL_CHANNEL_NORMALCAMERA, this->NormalCameraChannel, mMeshName, mMaterialNameOverride );
	if( !mStatus )
	{
		MGlobal::displayError( "Failed to import texture: " + this->NormalCameraChannel->TexturePath );
		return mStatus;
	}
	mStatus = this->ImportMaterialTextureFile( MAYA_MATERIAL_CHANNEL_TRANSPARENCY, this->TransparencyChannel, mMeshName, mMaterialNameOverride );
	if( !mStatus )
	{
		MGlobal::displayError( "Failed to import texture: " + this->TransparencyChannel->TexturePath );
		return mStatus;
	}
	mStatus = this->ImportMaterialTextureFile( MAYA_MATERIAL_CHANNEL_TRANSLUECENCE, this->TranslucenceChannel, mMeshName, mMaterialNameOverride );
	if( !mStatus )
	{
		MGlobal::displayError( "Failed to import texture: " + this->TranslucenceChannel->TexturePath );
		return mStatus;
	}
	mStatus = this->ImportMaterialTextureFile( MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_DEPTH, this->TranslucenceDepthChannel, mMeshName, mMaterialNameOverride );
	if( !mStatus )
	{
		MGlobal::displayError( "Failed to import texture: " + this->TranslucenceDepthChannel->TexturePath );
		return mStatus;
	}
	mStatus = this->ImportMaterialTextureFile( MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_FOCUS, this->TranslucenceFocusChannel, mMeshName, mMaterialNameOverride );
	if( !mStatus )
	{
		MGlobal::displayError( "Failed to import texture: " + this->TranslucenceFocusChannel->TexturePath );
		return mStatus;
	}
	mStatus = this->ImportMaterialTextureFile( MAYA_MATERIAL_CHANNEL_INCANDESCENCE, this->IncandescenceChannel, mMeshName, mMaterialNameOverride );
	if( !mStatus )
	{
		MGlobal::displayError( "Failed to import texture: " + this->IncandescenceChannel->TexturePath );
		return mStatus;
	}
	mStatus = this->ImportMaterialTextureFile( MAYA_MATERIAL_CHANNEL_REFLECTEDCOLOR, this->ReflectedColorChannel, mMeshName, mMaterialNameOverride );
	if( !mStatus )
	{
		MGlobal::displayError( "Failed to import texture: " + this->ReflectedColorChannel->TexturePath );
		return mStatus;
	}
	mStatus = this->ImportMaterialTextureFile( MAYA_MATERIAL_CHANNEL_REFLECTIVITY, this->ReflectivityChannel, mMeshName, mMaterialNameOverride );
	if( !mStatus )
	{
		MGlobal::displayError( "Failed to import texture: " + this->ReflectivityChannel->TexturePath );
		return mStatus;
	}

	if( !this->sgMaterial.IsNull() )
	{
		for( uint c = 0; c < this->sgMaterial->GetMaterialChannelCount(); ++c )
		{
			spString rChannelName = this->sgMaterial->GetMaterialChannelFromIndex( c );
			std::string sChannelName = rChannelName;
			if( sChannelName.length() == 0 )
				continue;

			const char* channelName = sChannelName.c_str();
			if( strcmp( channelName, MAYA_MATERIAL_CHANNEL_AMBIENTCOLOR ) == 0 )
				continue;
			else if( strcmp( channelName, MAYA_MATERIAL_CHANNEL_COLOR ) == 0 )
				continue;
			else if( strcmp( channelName, MAYA_MATERIAL_CHANNEL_SPECULARCOLOR ) == 0 )
				continue;
			else if( strcmp( channelName, MAYA_MATERIAL_CHANNEL_NORMALCAMERA ) == 0 )
				continue;
			else if( strcmp( channelName, MAYA_MATERIAL_CHANNEL_TRANSPARENCY ) == 0 )
				continue;
			else if( strcmp( channelName, MAYA_MATERIAL_CHANNEL_TRANSLUECENCE ) == 0 )
				continue;
			else if( strcmp( channelName, MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_DEPTH ) == 0 )
				continue;
			else if( strcmp( channelName, MAYA_MATERIAL_CHANNEL_TRANSLUECENCE_FOCUS ) == 0 )
				continue;
			else if( strcmp( channelName, MAYA_MATERIAL_CHANNEL_INCANDESCENCE ) == 0 )
				continue;
			else if( strcmp( channelName, MAYA_MATERIAL_CHANNEL_REFLECTEDCOLOR ) == 0 )
				continue;
			else if( strcmp( channelName, MAYA_MATERIAL_CHANNEL_REFLECTIVITY ) == 0 )
				continue;

			StandardMaterialChannel standardMaterialChannel( true );

			mStatus = this->ImportMaterialTextureFile( channelName, &standardMaterialChannel, mMeshName, mMaterialNameOverride );
			if( !mStatus )
			{
				MGlobal::displayError( "Failed to import texture on " + MString( channelName ) + " channel." );
				return mStatus;
			}
		}
	}

	this->ShaderName = mMaterialNameOverride;
	this->ShaderGroupName = mMaterialNameOverride + "SG";

	if( this->cmd->DoNotGenerateMaterials() )
		return MStatus::kSuccess;

	const float baseCosinePower = 15.f;

	// create the shader and shading group
	MStringArray mShaderArray;
	MString mCommand = "SimplygonMaya_createPhongMaterial( ";
	mCommand += CreateQuotedText( mShape.fullPathName() ) + ", ";
	mCommand += CreateQuotedText( this->NodeName ) + ", ";
	mCommand += CreateQuotedText( this->AmbientChannel->TexturePath ) + ", ";
	mCommand += CreateQuotedText( this->ColorChannel->TexturePath ) + ", ";
	mCommand += CreateQuotedText( this->SpecularChannel->TexturePath ) + ", ";
	mCommand += CreateQuotedText( this->NormalCameraChannel->TexturePath ) + ", ";
	mCommand += CreateQuotedText( this->TransparencyChannel->TexturePath ) + ", ";
	mCommand += CreateQuotedText( this->TranslucenceChannel->TexturePath ) + ", ";
	mCommand += CreateQuotedText( this->TranslucenceDepthChannel->TexturePath ) + ", ";
	mCommand += CreateQuotedText( this->TranslucenceFocusChannel->TexturePath ) + ", ";
	mCommand += CreateQuotedText( this->IncandescenceChannel->TexturePath ) + ", ";
	mCommand += CreateQuotedText( this->ReflectedColorChannel->TexturePath ) + ", ";
	mCommand += CreateQuotedText( this->ReflectivityChannel->TexturePath ) + ", ";
	mCommand += baseCosinePower;
	mCommand += ", ";
	mCommand += CreateQuotedText( this->AmbientChannel->UVSet ) + ", ";
	mCommand += CreateQuotedText( this->ColorChannel->UVSet ) + ", ";
	mCommand += CreateQuotedText( this->SpecularChannel->UVSet ) + ", ";
	mCommand += CreateQuotedText( this->NormalCameraChannel->UVSet ) + ", ";
	mCommand += CreateQuotedText( this->TransparencyChannel->UVSet ) + ", ";
	mCommand += CreateQuotedText( this->TranslucenceChannel->UVSet ) + ", ";
	mCommand += CreateQuotedText( this->TranslucenceDepthChannel->UVSet ) + ", ";
	mCommand += CreateQuotedText( this->TranslucenceFocusChannel->UVSet ) + ", ";
	mCommand += CreateQuotedText( this->IncandescenceChannel->UVSet ) + ", ";
	mCommand += CreateQuotedText( this->ReflectedColorChannel->UVSet ) + ", ";
	mCommand += CreateQuotedText( this->ReflectivityChannel->UVSet ) + ", ";

	mCommand += this->AmbientChannel->IsSRGB;
	mCommand += ", ";
	mCommand += this->ColorChannel->IsSRGB;
	mCommand += ", ";
	mCommand += this->SpecularChannel->IsSRGB;
	mCommand += ", ";
	mCommand += this->TransparencyChannel->IsSRGB;
	mCommand += ", ";
	mCommand += this->TranslucenceChannel->IsSRGB;
	mCommand += ", ";
	mCommand += this->TranslucenceDepthChannel->IsSRGB;
	mCommand += ", ";
	mCommand += this->TranslucenceFocusChannel->IsSRGB;
	mCommand += ", ";
	mCommand += this->IncandescenceChannel->IsSRGB;
	mCommand += ", ";
	mCommand += this->ReflectedColorChannel->IsSRGB;
	mCommand += ", ";
	mCommand += this->ReflectivityChannel->IsSRGB;
	mCommand += ");";

	mStatus = ExecuteCommand( mCommand, mShaderArray );
	if( !mStatus )
	{
		MGlobal::displayError( "Failed creating baked material for node: " + this->NodeName );
		return mStatus;
	}

	this->ShaderName = mShaderArray[ 0 ];
	this->ShaderGroupName = mShaderArray[ 1 ];

	mStatus = GetMObjectOfNamedObject( this->ShaderName, this->ShaderObject );
	if( !mStatus )
	{
		MGlobal::displayError( "Failed retrieving shader node: " + this->ShaderName );
		return mStatus;
	}
	mStatus = GetMObjectOfNamedObject( this->ShaderGroupName, this->ShaderGroupObject );
	if( !mStatus )
	{
		MGlobal::displayError( "Failed retrieving shader group node: " + this->ShaderGroupName );
		return mStatus;
	}

	// done, apply to object
	return MStatus::kSuccess;
}
