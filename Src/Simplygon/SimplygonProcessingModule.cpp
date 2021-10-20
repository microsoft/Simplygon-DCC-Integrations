// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "SimplygonProcessingModule.h"
#include "Common.h"
#include <Strsafe.h>
#include <corecrt_io.h>
#include <fcntl.h>
#include <cctype>

#include "SimplygonLoader.h"

using namespace Simplygon;
extern ISimplygon* sg;

HANDLE hRead = INVALID_HANDLE_VALUE;
HANDLE hWrite = INVALID_HANDLE_VALUE;

#define READ_BUFFER_SIZE 4096

bool IsPassthroughPipeline( Simplygon::spPipeline sgPipeline );

#pragma region PARSE_RESULT
enum ParseType
{
	PROGRESS_MESSAGE,
	ERROR_MESSAGE
};

class ParseResult
{
	private:
	std::string ErrorMessage;
	int Progress;

	public:
	ParseType Type;

	ParseResult( std::string errorMessage )
	{
		this->Type = ERROR_MESSAGE;
		this->Progress = 0;
		this->ErrorMessage = errorMessage;
	}

	ParseResult( int progress )
	{
		this->Type = PROGRESS_MESSAGE;
		this->Progress = progress;
	}

	int GetProgress() const { return this->Progress; }

	std::string GetErrorMessage() const { return this->ErrorMessage; }
};
#pragma endregion

SimplygonProcessingModule::SimplygonProcessingModule()
{
	this->progressObserver = nullptr;
	this->errorHandler = nullptr;
	this->batchType = EXTERNAL;
}

SimplygonProcessingModule::~SimplygonProcessingModule()
{
}

std::basic_string<TCHAR> GetErrorMessage( DWORD dwError )
{
	std::basic_string<TCHAR> tErrorString = _T("");
	LPVOID lpMsgBuf = nullptr;
	LPVOID lpDisplayBuf = nullptr;

	FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	               nullptr,
	               dwError,
	               MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
	               (LPTSTR)&lpMsgBuf,
	               0,
	               nullptr );

	const int len = lstrlen( (LPCTSTR)lpMsgBuf ) + 40;
	lpDisplayBuf = (LPVOID)LocalAlloc( LMEM_ZEROINIT, ( size_t )( len * sizeof( TCHAR ) ) );
	if( lpDisplayBuf != nullptr )
	{
		StringCchPrintf( (LPTSTR)lpDisplayBuf, LocalSize( lpDisplayBuf ) / sizeof( TCHAR ), TEXT( "Error code %d - %s" ), (int)dwError, (LPTSTR)lpMsgBuf );
		MessageBox( nullptr, (LPCTSTR)lpDisplayBuf, TEXT( "Error" ), MB_OK );

		tErrorString = (LPCTSTR)lpDisplayBuf;
	}

	LocalFree( lpMsgBuf );
	LocalFree( lpDisplayBuf );

	return tErrorString;
}

DWORD ReadFromPipe( char* cReadBuffer )
{
	DWORD dwNumBytesRead = 0;

	memset( cReadBuffer, '\0', sizeof( char ) * READ_BUFFER_SIZE );
	const bool bBytesRead = ReadFile( hRead, cReadBuffer, READ_BUFFER_SIZE, &dwNumBytesRead, nullptr ) == TRUE;

	return dwNumBytesRead;
}

DWORD ExecuteProcess( std::basic_string<TCHAR> tBatchPath,
                      std::basic_string<TCHAR> tSettingsPath,
                      std::basic_string<TCHAR> tScenePath,
                      std::basic_string<TCHAR> tOutputPath,
                      HANDLE* phProcess )
{
	// Set up members of the SECURITY_ATTRIBUTES structure.
	SECURITY_ATTRIBUTES securityAttributes;
	ZeroMemory( &securityAttributes, sizeof( SECURITY_ATTRIBUTES ) );
	securityAttributes.nLength = sizeof( SECURITY_ATTRIBUTES );
	securityAttributes.bInheritHandle = TRUE;
	securityAttributes.lpSecurityDescriptor = nullptr;

	// Set up members of the PROCESS_INFORMATION structure.
	PROCESS_INFORMATION processInformation;
	ZeroMemory( &processInformation, sizeof( PROCESS_INFORMATION ) );

	// Set up members of the STARTUPINFO structure.
	STARTUPINFO startupInfo;
	ZeroMemory( &startupInfo, sizeof( STARTUPINFO ) );
	startupInfo.cb = sizeof( STARTUPINFO );

	// create a command line string
	TCHAR* tArguments = new TCHAR[ MAX_PATH * 5 ];
	if( tSettingsPath.size() > 0 )
	{
		_stprintf_s( tArguments, MAX_PATH * 5, _T(" -Progress \"%s\" \"%s\" \"%s\""), tSettingsPath.c_str(), tScenePath.c_str(), tOutputPath.c_str() );
	}
	else
	{
		return ERROR_INVALID_PARAMETER;
	}

	// Create a pipe for the batch process's STDOUT.
	if( !CreatePipe( &hRead, &hWrite, &securityAttributes, 0 ) )
	{
		return ERROR_PIPE_NOT_CONNECTED;
	}

	// Ensure the read handle to the pipe for STDOUT is not inherited.
	if( !SetHandleInformation( hRead, HANDLE_FLAG_INHERIT, 0 ) )
	{
		return ERROR_PIPE_NOT_CONNECTED;
	}

	// Setup output and error handles
	startupInfo.hStdError = hWrite;
	startupInfo.hStdOutput = hWrite;
	startupInfo.dwFlags |= STARTF_USESTDHANDLES;

	DWORD dwPipeMode = PIPE_WAIT | PIPE_READMODE_BYTE;
	const bool bStateSuccess = SetNamedPipeHandleState( hRead, &dwPipeMode, nullptr, nullptr ) == TRUE;

	// Create the child process.
	const DWORD dwReturnCode = CreateProcess( tBatchPath.c_str(), // exe file path
	                                          tArguments,         // command line
	                                          nullptr,            // process security attributes
	                                          nullptr,            // primary thread security attributes
	                                          TRUE,               // handles are inherited
	                                          CREATE_NO_WINDOW,   // creation flags
	                                          nullptr,            // use parent's environment
	                                          nullptr,            // use parent's current directory
	                                          &startupInfo,       // STARTUPINFO pointer
	                                          &processInformation // receives PROCESS_INFORMATION
	);

	// Close write handle
	CloseHandle( hWrite );
	hWrite = INVALID_HANDLE_VALUE;

	delete[] tArguments;

	// function succeeded, return handle to process, release handles we will not use anymore
	*phProcess = processInformation.hProcess;
	CloseHandle( processInformation.hThread );

	return dwReturnCode;
}

std::vector<ParseResult> ParseMessage( const char* cReadBuffer )
{
	std::vector<ParseResult> sMessageVector;
	std::string sReadBuffer( cReadBuffer );

	while( true )
	{
		const size_t index = sReadBuffer.find( "\r\n" );
		if( index != std::string::npos )
		{
			std::string sLine = sReadBuffer.substr( 0, index );

			if( sLine.length() > 0 )
			{
				const unsigned char startSign = sLine[ 0 ];

				const bool bProgress = std::isdigit( startSign ) > 0;
				if( bProgress )
				{
					const int progress = atoi( sLine.c_str() );
					sMessageVector.push_back( ParseResult( progress ) );
				}
				else
				{
					sMessageVector.push_back( ParseResult( sReadBuffer ) );
					break;
				}
			}

			sReadBuffer = sReadBuffer.substr( index + 2, READ_BUFFER_SIZE );
		}
		else
		{
			break;
		}
	}

	return sMessageVector;
}

DWORD SimplygonProcessingModule::PostProgress()
{
	char cReadBuffer[ READ_BUFFER_SIZE ] = { 0 };

	// read message from pipe
	const DWORD dwNumBytesRead = ReadFromPipe( cReadBuffer );

	// parse message to either progress or error
	const std::vector<ParseResult>& sMessages = ParseMessage( cReadBuffer );

	// delegate messages to end-point
	for( size_t index = 0; index < sMessages.size(); ++index )
	{
		const ParseResult& pResult = sMessages[ index ];

		// id progress message, post progress to progress handler
		if( pResult.Type == PROGRESS_MESSAGE )
		{
			const int progress = pResult.GetProgress();
			if( this->progressObserver )
			{
				this->progressObserver->OnProgress( nullptr, (real)progress );
			}
		}

		// if error message, post error to error handler, if any,
		// otherwise, throw error.
		else if( pResult.Type == ERROR_MESSAGE )
		{
			if( this->errorHandler )
			{
				this->errorHandler->HandleError( nullptr, "", "", 0, pResult.GetErrorMessage().c_str() );
			}
			else
			{
				throw std::exception( pResult.GetErrorMessage().c_str() );
			}
		}
	}

	return dwNumBytesRead;
}

// waits for process to end, and returns exit value
// the process handle is also released by the function
DWORD SimplygonProcessingModule::WaitForProcess( HANDLE& hProcess )
{
	DWORD dwExitCode = 0;

	while( true )
	{
		// check if process has ended
		GetExitCodeProcess( hProcess, &dwExitCode );
		if( dwExitCode != STILL_ACTIVE )
		{
			break;
		}

		// check for messages while waiting
		PostProgress();

		// wait for it to signal
		// DWORD dwResult = WaitForSingleObject(hProcess, 10);
		Sleep( 1 );
	}

	// check for left-overs
	while( PostProgress() )
		;

	// clean-up handles
	CloseHandle( hRead );
	CloseHandle( hProcess );

	hRead = INVALID_HANDLE_VALUE;
	hProcess = INVALID_HANDLE_VALUE;

	return dwExitCode;
}

bool IsPassthroughPipeline( spPipeline sgPipeline )
{
	return sgPipeline->GetPipelineSettings()->GetIntermediateStep();
}

bool OverridePipelineParameters( spPipeline sgPipeline, const char* cTextureOutputPath, const char* cBatchpath, uint& lodIndex )
{
	const uint localLodIndex = lodIndex;

	// check children first
	for( uint c = 0; c < sgPipeline->GetCascadedPipelineCount(); ++c )
	{
		spPipeline sgCascadedPipeline = sgPipeline->GetCascadedPipelineByIndex( c );
		if( !sgCascadedPipeline.IsNull() )
		{
			if( !OverridePipelineParameters( sgCascadedPipeline, cTextureOutputPath, cBatchpath, ++lodIndex ) )
			{
				return false;
			}
		}
	}

	std::string sTextureOutputPath = CombineA( std::string( cTextureOutputPath ), std::string( "LOD" ) + std::to_string( localLodIndex ) );
	sgPipeline->GetPipelineSettings()->SetTextureOutputPath( sTextureOutputPath.c_str() );

	// update batch processor path
	sgPipeline->GetPipelineSettings()->SetSimplygonBatchPath( cBatchpath );

	spPipelineSettings sgPipelineSettings = sgPipeline->GetPipelineSettings();
	if( !sgPipelineSettings.IsNull() )
	{
		// override TexCoordName if it is not set (required for processing)
		std::string sPipelineType = sgPipeline->GetClass();
		if( std::string( "IReductionPipeline" ) == sPipelineType )
		{
			spString rTexCoordName = sgPipeline->GetStringParameter( "ReductionProcessor/MappingImageSettings/TexCoordName" );
			if( rTexCoordName.IsNullOrEmpty() )
			{
				return sgPipeline->SetStringParameter( "ReductionProcessor/MappingImageSettings/TexCoordName", "MaterialLOD" );
			}
		}
		else if( std::string( "IAggregationPipeline" ) == sPipelineType )
		{
			spString rTexCoordName = sgPipeline->GetStringParameter( "AggregationProcessor/MappingImageSettings/TexCoordName" );
			if( rTexCoordName.IsNullOrEmpty() )
			{
				return sgPipeline->SetStringParameter( "AggregationProcessor/MappingImageSettings/TexCoordName", "MaterialLOD" );
			}
		}
		else if( std::string( "IRemeshingLegacyPipeline" ) == sPipelineType )
		{
			spString rTexCoordName = sgPipeline->GetStringParameter( "RemeshingLegacyProcessor/MappingImageSettings/TexCoordName" );
			if( rTexCoordName.IsNullOrEmpty() )
			{
				return sgPipeline->SetStringParameter( "RemeshingLegacyProcessor/MappingImageSettings/TexCoordName", "MaterialLOD" );
			}
		}
		else if( std::string( "IRemeshingPipeline" ) == sPipelineType )
		{
			spString rTexCoordName = sgPipeline->GetStringParameter( "RemeshingProcessor/MappingImageSettings/TexCoordName" );
			if( rTexCoordName.IsNullOrEmpty() )
			{
				return sgPipeline->SetStringParameter( "RemeshingProcessor/MappingImageSettings/TexCoordName", "MaterialLOD" );
			}
		}
		else if( std::string( "IBillboardCloudVegetationPipeline" ) == sPipelineType || std::string( "IBillboardCloudPipeline" ) == sPipelineType ||
		         std::string( "IFlipbookPipeline" ) == sPipelineType || std::string( "IImpostorFromSingleViewPipeline" ) == sPipelineType )
		{
			spString rTexCoordName = sgPipeline->GetStringParameter( "ImpostorProcessor/MappingImageSettings/TexCoordName" );
			if( rTexCoordName.IsNullOrEmpty() )
			{
				return sgPipeline->SetStringParameter( "ImpostorProcessor/MappingImageSettings/TexCoordName", "MaterialLOD" );
			}
		}
	}

	return true;
}

EErrorCodes SimplygonProcessingModule::RunPipelineFromFile( std::basic_string<TCHAR> tPipelineFilePath,
                                                            std::basic_string<TCHAR> tSceneInputFilePath,
                                                            std::basic_string<TCHAR> tSceneOutputFilePath )
{
	Simplygon::spPipelineSerializer sgSerializer = sg->CreatePipelineSerializer();
	if( sgSerializer.IsNull() )
	{
		std::string sErrorMessage = "Failed to create pipeline serializer.";
		throw std::exception( sErrorMessage.c_str() );
	}

	const char* cPipelineFilePath = LPCTSTRToConstCharPtr( tPipelineFilePath.c_str() );
	Simplygon::spPipeline sgPipeline = sgSerializer->LoadPipelineFromFile( cPipelineFilePath );
	if( sgPipeline.IsNull() )
	{
		std::string sErrorMessage = "Invalid pipeline definition file - ";
		sErrorMessage += cPipelineFilePath;
		throw std::exception( sErrorMessage.c_str() );
	}

	// override texture output path
	const char* cTextureOutputPath = LPCTSTRToConstCharPtr( this->tTextureOutputPath.c_str() );
	const char* cExternalBatchPath = LPCTSTRToConstCharPtr( this->tExternalBatchPath.c_str() );

	uint iStartLodIndex = 1;
	const bool bResult = OverridePipelineParameters( sgPipeline, cTextureOutputPath, cExternalBatchPath, iStartLodIndex );
	if( !bResult )
	{
		std::string sErrorMessage = "Failed to override required pipeline parameters.";
		throw std::exception( sErrorMessage.c_str() );
	}

	const char* cSceneInputFile = LPCTSTRToConstCharPtr( tSceneInputFilePath.c_str() );
	const char* cSceneOutputFile = LPCTSTRToConstCharPtr( tSceneOutputFilePath.c_str() );

	sgPipeline->AddObserver( this->progressObserver );
	sgPipeline->RunSceneFromFile( cSceneInputFile, cSceneOutputFile, Simplygon::EPipelineRunMode::RunInThisProcess );

	return Simplygon::EErrorCodes::NoError;
}

EErrorCodes SimplygonProcessingModule::RunPipelineExternallyFromFile( std::basic_string<TCHAR> tPipelineFilePath,
                                                                      std::basic_string<TCHAR> tSceneInputFilePath,
                                                                      std::basic_string<TCHAR> tSceneOutputFilePath )
{
	Simplygon::spPipelineSerializer sgSerializer = sg->CreatePipelineSerializer();
	if( sgSerializer.IsNull() )
	{
		std::string sErrorMessage = "Failed to create pipeline serializer.";
		throw std::exception( sErrorMessage.c_str() );
	}

	const char* cPipelineFilePath = LPCTSTRToConstCharPtr( tPipelineFilePath.c_str() );
	Simplygon::spPipeline sgPipeline = sgSerializer->LoadPipelineFromFile( cPipelineFilePath );
	if( sgPipeline.IsNull() )
	{
		std::string sErrorMessage = "Invalid pipeline definition file - ";
		sErrorMessage += cPipelineFilePath;
		throw std::exception( sErrorMessage.c_str() );
	}

	const char* cTextureOutputPath = LPCTSTRToConstCharPtr( this->tTextureOutputPath.c_str() );
	const char* cExternalBatchPath = LPCTSTRToConstCharPtr( this->tExternalBatchPath.c_str() );

	uint iStartLodIndex = 1;
	const bool bResult = OverridePipelineParameters( sgPipeline, cTextureOutputPath, cExternalBatchPath, iStartLodIndex );
	if( !bResult )
	{
		std::string sErrorMessage = "Failed to override required pipeline parameters.";
		throw std::exception( sErrorMessage.c_str() );
	}

	sgSerializer->SavePipelineToFile( cPipelineFilePath, sgPipeline );

	HANDLE pHandle = nullptr;
	const DWORD dwResult = ExecuteProcess( this->tExternalBatchPath, tPipelineFilePath, tSceneInputFilePath, tSceneOutputFilePath, &pHandle );
	if( dwResult == 0 )
	{
		const DWORD dwErrorCode = GetLastError();
		std::basic_string<TCHAR> sErrorMessage = _T("ExecuteProcess (Batch Processor): ");
		sErrorMessage += GetErrorMessage( dwErrorCode );
		throw std::exception( LPCTSTRToConstCharPtr( sErrorMessage.c_str() ) );
	}

	const DWORD dwReturnCode = WaitForProcess( pHandle );
	if( dwReturnCode == 0 )
	{
		// save out cascaded scenes, if any
		if( sgPipeline->GetCascadedPipelineCount() > 0 )
		{
			std::basic_string<TCHAR> tOutputDirectory = GetDirectoryOfFile( tSceneOutputFilePath.c_str() );
			std::basic_string<TCHAR> tOutputFileName = GetTitleOfFile( tSceneOutputFilePath.c_str() );
			std::basic_string<TCHAR> tOutputFileNameExtension = GetExtensionOfFile( tSceneOutputFilePath.c_str() );

			// check children first
			for( uint c = 0; c < sgPipeline->GetCascadedPipelineCount(); ++c )
			{
				spPipeline sgCascadedPipeline = sgPipeline->GetCascadedPipelineByIndex( c );
				if( !sgCascadedPipeline.IsNull() )
				{
					spScene sgCascadedScene = sgCascadedPipeline->GetProcessedScene();
					if( sgCascadedScene.IsNull() )
						continue;

					std::basic_string<TCHAR> tOutputFilePath = Combine( tOutputDirectory, tOutputFileName );
					tOutputFilePath += _T("_LOD");
					tOutputFilePath += ( c + 1 );
					tOutputFilePath += tOutputFileNameExtension;

					const char* cOutputFilePath = LPCTSTRToConstCharPtr( tOutputFilePath.c_str() );

					const bool bSceneSaved = sgCascadedScene->SaveToFile( cOutputFilePath );
					if( !bSceneSaved )
					{
						std::string sErrorMessage = "Could not serialize cascaded scene to the specified file path - ";
						sErrorMessage += cOutputFilePath;

						throw std::exception( sErrorMessage.c_str() );
					}
				}
			}
		}
		return EErrorCodes::NoError;
	}
	return EErrorCodes::FailedToRunPipeline;
}

std::vector<Simplygon::spScene>
SimplygonProcessingModule::RunProcess( const spScene sgInputScene, std::basic_string<TCHAR> tPipeLineFilePath, PBatchType batchType )
{
	switch( batchType )
	{
		case INTERNAL: return RunInternalProcess( sgInputScene, tPipeLineFilePath );
		case EXTERNAL: return RunExternalProcess( sgInputScene, tPipeLineFilePath );
		default: throw std::exception( "Invalid batch type - " + batchType );
	}
}

bool SimplygonProcessingModule::RunProcess( std::basic_string<TCHAR> tInputSceneFile,
                                            std::basic_string<TCHAR> tOutputSceneFile,
                                            std::basic_string<TCHAR> tPipeLineFilePath,
                                            PBatchType batchType )
{
	switch( batchType )
	{
		case INTERNAL: return RunInternalProcess( tInputSceneFile, tOutputSceneFile, tPipeLineFilePath );
		case EXTERNAL: return RunExternalProcess( tInputSceneFile, tOutputSceneFile, tPipeLineFilePath );
		default: throw std::exception( "Invalid batch type - " + batchType );
	}
}

void GetCascadedScenes( spPipeline sgPipeline, std::vector<spScene>& sgScenes )
{
	// for cascaded pipelines at this level
	for( uint c = 0; c < sgPipeline->GetCascadedPipelineCount(); ++c )
	{
		spPipeline sgCascadedPipeline = sgPipeline->GetCascadedPipelineByIndex( c );
		if( !sgCascadedPipeline.IsNull() )
		{
			// get the processed scene
			spScene sgCascadedScene = sgCascadedPipeline->GetProcessedScene();
			if( sgCascadedScene.IsNull() )
			{
				std::string sErrorMessage = "Could not process the given scene, the cascaded output scene is null.";
				throw std::exception( sErrorMessage.c_str() );
			}

			// add scene to list
			if( !IsPassthroughPipeline( sgCascadedPipeline ) )
			{
				sgScenes.push_back( sgCascadedScene );
			}

			// go through all next level pipelines
			GetCascadedScenes( sgCascadedPipeline, sgScenes );
		}
	}
}

void ExportScenesToFile( spPipeline sgPipeline,
                         std::basic_string<TCHAR> tOutputSceneFile,
                         uint& lodIndex,
                         std::vector<std::basic_string<TCHAR>>& tOutputFileList,
                         std::basic_string<TCHAR> tPrefix = _T("LOD") )
{
	const uint localLodIndex = lodIndex;

	if( !IsPassthroughPipeline( sgPipeline ) )
	{
		// fetch the processed scene for the given pipeline
		spScene sgScene = sgPipeline->GetProcessedScene();
		if( sgScene.IsNull() )
		{
			std::string sErrorMessage = "Could not export the given scene, the output scene is null.";
			throw std::exception( sErrorMessage.c_str() );
		}
		else
		{
			// save the scene to file
			std::basic_string<TCHAR> tOutputDirectory = GetDirectoryOfFile( tOutputSceneFile.c_str() );
			std::basic_string<TCHAR> tOutputFileName = GetTitleOfFile( tOutputSceneFile.c_str() );
			std::basic_string<TCHAR> tOutputFileNameExtension = GetExtensionOfFile( tOutputSceneFile.c_str() );

			std::basic_string<TCHAR> tOutputFilePath = Combine( tOutputDirectory, tOutputFileName );
			if( tPrefix.size() > 0 )
			{
				tOutputFilePath += _T("_");
				tOutputFilePath += tPrefix;
				tOutputFilePath += ConstCharPtrToLPCTSTR( std::to_string( localLodIndex ).c_str() );
			}
			tOutputFilePath += tOutputFileNameExtension;

			const char* cOutputFilePath = LPCTSTRToConstCharPtr( tOutputFilePath.c_str() );

			// relink textures
			spTextureTable sgTextureTable = sgScene->GetTextureTable();
			if( !sgTextureTable.IsNull() )
			{
				for( uint t = 0; t < sgTextureTable->GetTexturesCount(); ++t )
				{
					spTexture sgTexture = sgTextureTable->GetTexture( t );
					if( !sgTexture.IsNull() )
					{
						spString sgFilePath = sgTexture->GetFilePath();
						if( !sgFilePath.IsNullOrEmpty() )
						{
							std::basic_string<TCHAR> tNewTextureDirectory = Combine( tOutputDirectory, _T("Textures") );
							if( tPrefix.size() > 0 )
							{
								tNewTextureDirectory = Combine( tNewTextureDirectory, tPrefix );
								tNewTextureDirectory += ConstCharPtrToLPCTSTR( std::to_string( localLodIndex ).c_str() );
							}

							const bool bCreated = CreateFolder( tNewTextureDirectory );

							std::basic_string<TCHAR> tSgFilePath = ConstCharPtrToLPCTSTR( sgFilePath.c_str() );
							std::basic_string<TCHAR> tFileName = GetNameOfFile( tSgFilePath.c_str() );
							std::basic_string<TCHAR> tNewFilepath = Combine( tNewTextureDirectory, tFileName.c_str() );

							uint indexCounter = 1;
							while( FileExists( tNewFilepath ) )
							{
								std::basic_string<TCHAR> tTmpFileName = GetTitleOfFile( tSgFilePath.c_str() );
								std::basic_string<TCHAR> tTmpExtension = GetExtensionOfFile( tSgFilePath.c_str() );

								std::basic_string<TCHAR> sNewFilePath = tTmpFileName + std::basic_string<TCHAR>( _T("_") );
								sNewFilePath += ConstCharPtrToLPCTSTR( std::to_string( indexCounter++ ).c_str() );
								sNewFilePath += tTmpExtension;

								tNewFilepath = Combine( tNewTextureDirectory, sNewFilePath.c_str() );
							}

							std::basic_string<TCHAR> tOldFilePath = ConstCharPtrToLPCTSTR( sgFilePath.c_str() );

							const bool bFileMoved = CopyFile( tOldFilePath.c_str(), tNewFilepath.c_str(), FALSE ) == TRUE;
							if( bFileMoved )
							{
								sgTexture->SetFilePath( LPCTSTRToConstCharPtr( tNewFilepath.c_str() ) );
							}

							continue;
						}
					}
				}
			}

			const bool bSaved = sgScene->SaveToFile( cOutputFilePath );
			if( !bSaved )
			{
				std::string sErrorMessage = "Could not export the given scene.";
				throw std::exception( sErrorMessage.c_str() );
			}

			tOutputFileList.push_back( tOutputFilePath );
			lodIndex++;
		}
	}

	// export cascaded pipeline, if any
	for( uint cIndex = 0; cIndex < sgPipeline->GetCascadedPipelineCount(); ++cIndex )
	{
		spPipeline sgCascadedPipeline = sgPipeline->GetCascadedPipelineByIndex( cIndex );
		if( !sgCascadedPipeline.IsNull() )
		{
			ExportScenesToFile( sgCascadedPipeline, tOutputSceneFile, lodIndex, tOutputFileList, tPrefix );
		}
	}
}

std::vector<Simplygon::spScene>
SimplygonProcessingModule::RunPipeline( const spScene sgInputScene, const Simplygon::spPipeline sgPipeline, Simplygon::EPipelineRunMode runMode )
{
	// early out if pipeline is null
	if( sgPipeline.IsNull() )
	{
		std::string sErrorMessage = "Invalid pipeline definition file";
		throw std::exception( sErrorMessage.c_str() );
	}

	const char* cTextureOutputPath = LPCTSTRToConstCharPtr( this->tTextureOutputPath.c_str() );
	const char* cExternalBatchPath = LPCTSTRToConstCharPtr( this->tExternalBatchPath.c_str() );

	// override some parameters in pipeline(s)
	uint iStartLodIndex = 1;
	const bool bResult = OverridePipelineParameters( sgPipeline, cTextureOutputPath, cExternalBatchPath, iStartLodIndex );
	if( !bResult )
	{
		std::string sErrorMessage = "Failed to override required pipeline parameters.";
		throw std::exception( sErrorMessage.c_str() );
	}

	// add progress observer for the pipeline
	const int observerId = sgPipeline->AddObserver( this->progressObserver );

	// run the pipeline internally, or in new process
	try
	{
		sgPipeline->RunScene( sgInputScene, runMode );
	}
	catch( std::exception ex )
	{
		// if error, remove progress observer
		sgPipeline->RemoveObserver( observerId );

		// and report error
		std::string sErrorMessage = "Could not process the given scene - ";
		sErrorMessage += ex.what();
		throw std::exception( sErrorMessage.c_str() );
	}

	// if process was successful, remove progress observer
	sgPipeline->RemoveObserver( observerId );

	// fetch the topmost  processed scene
	spScene sgProcessedScene = sgPipeline->GetProcessedScene();
	if( sgProcessedScene.IsNull() )
	{
		std::string sErrorMessage = "Could not process the given scene, the output scene is null.";
		throw std::exception( sErrorMessage.c_str() );
	}

	// fetch processed scene
	std::vector<Simplygon::spScene> sgProcessedScenes;

	if( !IsPassthroughPipeline( sgPipeline ) )
	{
		sgProcessedScenes.push_back( sgProcessedScene );
	}

	// fetch cascaded scenes
	GetCascadedScenes( sgPipeline, sgProcessedScenes );

	return sgProcessedScenes;
}

void GetNumberOfCascadedPipelines( spPipeline sgPipeline, uint& numPipelines )
{
	for( uint cIndex = 0; cIndex < sgPipeline->GetCascadedPipelineCount(); ++cIndex )
	{
		spPipeline sgCascadedPipeline = sgPipeline->GetCascadedPipelineByIndex( cIndex );
		if( !sgCascadedPipeline.IsNull() )
		{
			GetNumberOfCascadedPipelines( sgCascadedPipeline, ++numPipelines );
		}
	}
}

uint GetNumberOfPipelines( spPipeline sgPipeline )
{
	if( sgPipeline.IsNull() )
		return 0;

	uint numPipelines = 1;
	GetNumberOfCascadedPipelines( sgPipeline, numPipelines );

	return numPipelines;
}

std::vector<std::basic_string<TCHAR>> SimplygonProcessingModule::RunPipelineOnFile( std::basic_string<TCHAR> tInputSceneFile,
                                                                                    std::basic_string<TCHAR> tOutputSceneFile,
                                                                                    spPipeline sgPipeline,
                                                                                    Simplygon::EPipelineRunMode runMode )
{
	// early out if pipeline is null
	if( sgPipeline.IsNull() )
	{
		std::string sErrorMessage = "Invalid pipeline definition file.";
		throw std::exception( sErrorMessage.c_str() );
	}

	const char* cTextureOutputPath = LPCTSTRToConstCharPtr( this->tTextureOutputPath.c_str() );
	const char* cExternalBatchPath = LPCTSTRToConstCharPtr( this->tExternalBatchPath.c_str() );

	// override some parameters in pipeline(s)
	uint iStartLodIndex = 1;
	const bool bResult = OverridePipelineParameters( sgPipeline, cTextureOutputPath, cExternalBatchPath, iStartLodIndex );
	if( !bResult )
	{
		std::string sErrorMessage = "Failed to override required pipeline parameters.";
		throw std::exception( sErrorMessage.c_str() );
	}

	const char* cSceneInputFile = LPCTSTRToConstCharPtr( tInputSceneFile.c_str() );
	const char* cSceneOutputFile = LPCTSTRToConstCharPtr( tOutputSceneFile.c_str() );

	// add progress observer for the pipeline
	const int observerId = sgPipeline->AddObserver( this->progressObserver );

	// run the pipeline internally, or in new process
	try
	{
		sgPipeline->RunSceneFromFile( cSceneInputFile, nullptr, runMode );
	}
	catch( std::exception ex )
	{
		// if error, remove progress observer
		sgPipeline->RemoveObserver( observerId );

		// and report error
		std::string sErrorMessage = "Could not process the given scene - ";
		sErrorMessage += ex.what();
		throw std::exception( sErrorMessage.c_str() );
	}

	// if process was successful, remove progress observer
	sgPipeline->RemoveObserver( observerId );

	// fetch the topmost  processed scene
	spScene sgProcessedScene = sgPipeline->GetProcessedScene();
	if( sgProcessedScene.IsNull() )
	{
		std::string sErrorMessage = "Could not process the given scene, the output scene is null.";
		throw std::exception( sErrorMessage.c_str() );
	}

	std::vector<std::basic_string<TCHAR>> tOutputFileList;

	// save processed scenes to disk and save output paths for later use (only use prefix if cascaded/lod-chain)
	uint iStartExportLodIndex = 1;
	ExportScenesToFile( sgPipeline, tOutputSceneFile, iStartExportLodIndex, tOutputFileList, GetNumberOfPipelines( sgPipeline ) > 0 ? _T("LOD") : _T("") );

	return tOutputFileList;
}

std::vector<Simplygon::spScene> SimplygonProcessingModule::RunInternalProcess( const spScene sgInputScene, std::basic_string<TCHAR> tPipeLineFilePath )
{
	std::basic_string<TCHAR> tInputFilePath = Combine( this->tWorkDirectory.c_str(), _T("sgInputScene.sb") );

	const char* cInputFilePath = LPCTSTRToConstCharPtr( tInputFilePath.c_str() );
	const char* cPipeLineFilePath = LPCTSTRToConstCharPtr( tPipeLineFilePath.c_str() );

	const bool bSceneSaved = sgInputScene->SaveToFile( cInputFilePath );
	if( !bSceneSaved )
	{
		std::string sErrorMessage = "Could not serialize scene to the specified file path - ";
		sErrorMessage += cInputFilePath;

		throw std::exception( sErrorMessage.c_str() );
	}

	Simplygon::spPipelineSerializer sgSerializer = sg->CreatePipelineSerializer();
	if( sgSerializer.IsNull() )
	{
		std::string sErrorMessage = "Failed to create pipeline serializer.";
		throw std::exception( sErrorMessage.c_str() );
	}

	spPipeline sgPipeline = sgSerializer->LoadPipelineFromFile( cPipeLineFilePath );
	if( sgPipeline.IsNull() )
	{
		std::string sErrorMessage = "Invalid pipeline definition file - ";
		sErrorMessage += cPipeLineFilePath;
		throw std::exception( sErrorMessage.c_str() );
	}

	// modify reduction settings
	const bool bMaterialBake = false;
	if( bMaterialBake )
	{
		spReductionPipeline sgReductionPipeline = spReductionPipeline::SafeCast( sgPipeline );

		// Set the Image Mapping Settings.
		spMappingImageSettings sgMappingImageSettings = sgReductionPipeline->GetMappingImageSettings();
		sgMappingImageSettings->SetGenerateMappingImage( true );
		sgMappingImageSettings->SetGenerateTexCoords( true );
		sgMappingImageSettings->GetParameterizerSettings()->SetMaxStretch( 0.25f );
		sgMappingImageSettings->GetOutputMaterialSettings( 0 )->SetGutterSpace( 2 );
		sgMappingImageSettings->GetOutputMaterialSettings( 0 )->SetTextureWidth( 1024 );
		sgMappingImageSettings->GetOutputMaterialSettings( 0 )->SetTextureHeight( 1024 );
		sgMappingImageSettings->GetOutputMaterialSettings( 0 )->SetMultisamplingLevel( 2 );
		sgMappingImageSettings->SetTexCoordLevel( 255 );

		// add color caster
		spColorCaster sgColorCaster = sg->CreateColorCaster();
		spColorCasterSettings sgColorCasterSettings = sgColorCaster->GetColorCasterSettings();
		sgColorCasterSettings->SetOutputPixelFormat( EPixelFormat::R8G8B8 );
		sgColorCasterSettings->SetDilation( 10 );

		sgReductionPipeline->AddMaterialCaster( sgColorCaster, 0 );

		// add normal caster
		spNormalCaster sgNormalCaster = sg->CreateNormalCaster();
		spNormalCasterSettings sgNormalCasterSettings = sgNormalCaster->GetNormalCasterSettings();
		sgNormalCasterSettings->SetOutputPixelFormat( EPixelFormat::R8G8B8 );
		sgNormalCasterSettings->SetDilation( 10 );
		sgNormalCasterSettings->SetFlipBackfacingNormals( true );
		sgNormalCasterSettings->SetGenerateTangentSpaceNormals( true );

		sgReductionPipeline->AddMaterialCaster( sgNormalCaster, 0 );
	}

	std::basic_string<TCHAR> tOutputFilePath = Combine( this->tWorkDirectory.c_str(), _T("sgOutputScene.sb") );
	const char* cOutputFilePath = LPCTSTRToConstCharPtr( tOutputFilePath.c_str() );

	const EErrorCodes returnCode = RunPipelineFromFile( tPipeLineFilePath, tInputFilePath, tOutputFilePath );
	if( returnCode != Simplygon::EErrorCodes::NoError )
	{
		throw std::exception( "Failed with error code = " + (int)returnCode );
	}

	spScene sgLodScene = sg->CreateScene();
	const bool bSceneLoaded = sgLodScene->LoadFromFile( cOutputFilePath );
	if( sgLodScene.IsNull() || !bSceneLoaded )
	{
		std::string sErrorMessage = "Could not load a scene from the given file path - ";
		sErrorMessage += cOutputFilePath;
		throw std::exception( sErrorMessage.c_str() );
	}

	return std::vector<Simplygon::spScene>() = { sgLodScene };
}

std::vector<Simplygon::spScene> SimplygonProcessingModule::RunExternalProcess( const spScene sgInputScene, std::basic_string<TCHAR> tPipeLineFilePath )
{
	std::basic_string<TCHAR> tInputFilePath = Combine( this->tWorkDirectory.c_str(), _T("sgInputScene.sb") );

	const char* cInputFilePath = LPCTSTRToConstCharPtr( tInputFilePath.c_str() );
	const char* cPipeLineFilePath = LPCTSTRToConstCharPtr( tPipeLineFilePath.c_str() );

	const bool bSceneSaved = sgInputScene->SaveToFile( cInputFilePath );
	if( !bSceneSaved )
	{
		std::string sErrorMessage = "Could not serialize scene to the specified file path - ";
		sErrorMessage += cInputFilePath;

		throw std::exception( sErrorMessage.c_str() );
	}

	std::basic_string<TCHAR> tOutputFilePath = Combine( this->tWorkDirectory, _T("sgOutputScene.sb") );
	const char* cOutputFilePath = LPCTSTRToConstCharPtr( tOutputFilePath.c_str() );

	const EErrorCodes returnCode = RunPipelineExternallyFromFile( tPipeLineFilePath, tInputFilePath, tOutputFilePath );

	if( returnCode == Simplygon::EErrorCodes::NoError )
	{
		std::string sErrorMessage = Simplygon::GetError( returnCode );
		throw std::exception( sErrorMessage.c_str() );
	}

	spScene sgLodScene = sg->CreateScene();
	const bool bSceneLoaded = sgLodScene->LoadFromFile( cOutputFilePath );

	if( sgLodScene.IsNull() || !bSceneLoaded )
	{
		std::string sErrorMessage = "Could not load a scene from the given file path - ";
		sErrorMessage += cOutputFilePath;
		throw std::exception( sErrorMessage.c_str() );
	}

	return std::vector<Simplygon::spScene>() = { sgLodScene };
}

bool SimplygonProcessingModule::RunInternalProcess( std::basic_string<TCHAR> tInputSceneFile,
                                                    std::basic_string<TCHAR> tOutputSceneFile,
                                                    std::basic_string<TCHAR> tPipeLineFilePath )
{
	std::basic_string<TCHAR> tInputFilePath = Combine( this->tWorkDirectory.c_str(), _T("sgInputScene.sb") );

	const char* cInputFilePath = LPCTSTRToConstCharPtr( tInputFilePath.c_str() );
	const char* cPipeLineFilePath = LPCTSTRToConstCharPtr( tPipeLineFilePath.c_str() );

	const bool bSceneSaved = CopyFile( tInputSceneFile.c_str(), tInputFilePath.c_str(), FALSE ) == TRUE;
	if( !bSceneSaved )
	{
		std::string sErrorMessage = "Could not serialize scene to the specified file path - ";
		sErrorMessage += cInputFilePath;

		throw std::exception( sErrorMessage.c_str() );
	}

	Simplygon::spPipelineSerializer sgSerializer = sg->CreatePipelineSerializer();
	if( sgSerializer.IsNull() )
	{
		std::string sErrorMessage = "Failed to create pipeline serializer.";
		throw std::exception( sErrorMessage.c_str() );
	}

	spPipeline sgPipeline = sgSerializer->LoadPipelineFromFile( cPipeLineFilePath );
	if( sgPipeline.IsNull() )
	{
		std::string sErrorMessage = "Invalid pipeline definition file - ";
		sErrorMessage += cPipeLineFilePath;
		throw std::exception( sErrorMessage.c_str() );
	}

	// modify reduction settings
	const bool bMaterialBake = false;
	if( bMaterialBake )
	{
		spReductionPipeline sgReductionPipeline = spReductionPipeline::SafeCast( sgPipeline );

		// Set the Image Mapping Settings.
		spMappingImageSettings sgMappingImageSettings = sgReductionPipeline->GetMappingImageSettings();
		sgMappingImageSettings->SetGenerateMappingImage( true );
		sgMappingImageSettings->SetGenerateTexCoords( true );
		sgMappingImageSettings->GetParameterizerSettings()->SetMaxStretch( 0.25f );
		sgMappingImageSettings->GetOutputMaterialSettings( 0 )->SetGutterSpace( 2 );
		sgMappingImageSettings->GetOutputMaterialSettings( 0 )->SetTextureWidth( 1024 );
		sgMappingImageSettings->GetOutputMaterialSettings( 0 )->SetTextureHeight( 1024 );
		sgMappingImageSettings->GetOutputMaterialSettings( 0 )->SetMultisamplingLevel( 2 );
		sgMappingImageSettings->SetTexCoordLevel( 255 );

		// add color caster
		spColorCaster sgColorCaster = sg->CreateColorCaster();
		spColorCasterSettings sgColorCasterSettings = sgColorCaster->GetColorCasterSettings();
		sgColorCasterSettings->SetOutputPixelFormat( EPixelFormat::R8G8B8 );
		sgColorCasterSettings->SetDilation( 10 );

		sgReductionPipeline->AddMaterialCaster( sgColorCaster, 0 );

		// add normal caster
		spNormalCaster sgNormalCaster = sg->CreateNormalCaster();
		spNormalCasterSettings sgNormalCasterSettings = sgNormalCaster->GetNormalCasterSettings();
		sgNormalCasterSettings->SetOutputPixelFormat( EPixelFormat::R8G8B8 );
		sgNormalCasterSettings->SetDilation( 10 );
		sgNormalCasterSettings->SetFlipBackfacingNormals( true );
		sgNormalCasterSettings->SetGenerateTangentSpaceNormals( true );

		sgReductionPipeline->AddMaterialCaster( sgNormalCaster, 0 );
	}

	std::basic_string<TCHAR> tOutputFilePath = tOutputSceneFile;
	std::basic_string<TCHAR> tOutputFileDirectory = GetDirectoryOfFile( tOutputFilePath.c_str() );
	const char* cOutputFilePath = LPCTSTRToConstCharPtr( tOutputFilePath.c_str() );

	const bool bTargetFolderCreated = CreateFolder( tOutputFileDirectory );
	if( !bTargetFolderCreated )
	{
		throw std::exception( ( std::string( "RunProcess - Could not create output folder: " ) + cOutputFilePath ).c_str() );
	}

	const EErrorCodes returnCode = RunPipelineFromFile( tPipeLineFilePath, tInputFilePath, tOutputFilePath );
	if( returnCode != Simplygon::EErrorCodes::NoError )
	{
		throw std::exception( "Failed with error code = " + (int)returnCode );
	}

	const bool bProcessedSceneExists = FileExists( tOutputFilePath );
	if( !bProcessedSceneExists )
	{
		std::string sErrorMessage = "Could not find the processed scene file from the given file path - ";
		sErrorMessage += cOutputFilePath;
		throw std::exception( sErrorMessage.c_str() );
	}
	else
	{
		// copy all textures and relink texture nodes
		spScene sgScene = sg->CreateScene();
		const bool bSceneLoaded = sgScene->LoadFromFile( LPCTSTRToConstCharPtr( tOutputFilePath.c_str() ) );
		if( bSceneLoaded )
		{
			spTextureTable sgTextureTable = sgScene->GetTextureTable();
			if( sgTextureTable.NonNull() )
			{
				for( uint t = 0; t < sgTextureTable->GetTexturesCount(); ++t )
				{
					spTexture sgTexture = sgTextureTable->GetTexture( t );
					spString sgTextureFilePath = sgTexture->GetFilePath();
					if( sgTextureFilePath.NonEmpty() )
					{
						std::basic_string<TCHAR> tSourceFilePath = ConstCharPtrToLPCTSTR( sgTextureFilePath.c_str() );
						std::basic_string<TCHAR> tFileName = GetNameOfFile( tSourceFilePath );

						std::basic_string<TCHAR> tTargetFilePath = GetDirectoryOfFile( tOutputFilePath.c_str() );
						std::basic_string<TCHAR> tTargetFileTexturePath = Combine( tTargetFilePath, _T("Textures") );
						std::basic_string<TCHAR> tFinalTargetFilePath = Combine( tTargetFileTexturePath, tFileName );

						const bool bCreated = CreateFolder( tTargetFileTexturePath );

						const bool bFileMoved = CopyFile( tSourceFilePath.c_str(), tFinalTargetFilePath.c_str(), FALSE ) == TRUE;
						if( bFileMoved )
						{
							sgTexture->SetFilePath( LPCTSTRToConstCharPtr( tFinalTargetFilePath.c_str() ) );
						}
					}
				}
			}

			const bool bSaved = sgScene->SaveToFile( LPCTSTRToConstCharPtr( tOutputFilePath.c_str() ) );
		}
	}

	return bProcessedSceneExists;
}

bool SimplygonProcessingModule::RunExternalProcess( std::basic_string<TCHAR> tInputSceneFile,
                                                    std::basic_string<TCHAR> tOutputSceneFile,
                                                    std::basic_string<TCHAR> tPipeLineFilePath )
{
	std::basic_string<TCHAR> tInputFilePath = Combine( this->tWorkDirectory.c_str(), _T("sgInputScene.sb") );

	const char* cInputFilePath = LPCTSTRToConstCharPtr( tInputFilePath.c_str() );
	const char* cPipeLineFilePath = LPCTSTRToConstCharPtr( tPipeLineFilePath.c_str() );

	const bool bSceneSaved = CopyFile( tInputSceneFile.c_str(), tInputFilePath.c_str(), FALSE ) == TRUE;
	if( !bSceneSaved )
	{
		std::string sErrorMessage = "Could not serialize scene to the specified file path - ";
		sErrorMessage += cInputFilePath;

		throw std::exception( sErrorMessage.c_str() );
	}

	std::basic_string<TCHAR> tOutputFilePath = tOutputSceneFile;
	const char* cOutputFilePath = LPCTSTRToConstCharPtr( tOutputFilePath.c_str() );

	const EErrorCodes returnCode = RunPipelineExternallyFromFile( tPipeLineFilePath, tInputFilePath, tOutputFilePath );

	if( returnCode == Simplygon::EErrorCodes::NoError )
	{
		std::string sErrorMessage = Simplygon::GetError( returnCode );
		throw std::exception( sErrorMessage.c_str() );
	}

	const bool bProcessedSceneExists = FileExists( tOutputFilePath );
	if( !bProcessedSceneExists )
	{
		std::string sErrorMessage = "Could not find the processed scene file from the given file path - ";
		sErrorMessage += cOutputFilePath;
		throw std::exception( sErrorMessage.c_str() );
	}
	else
	{
		// copy all textures and relink texture nodes
		spScene sgScene = sg->CreateScene();
		const bool bSceneLoaded = sgScene->LoadFromFile( LPCTSTRToConstCharPtr( tOutputFilePath.c_str() ) );
		if( bSceneLoaded )
		{
			spTextureTable sgTextureTable = sgScene->GetTextureTable();
			if( sgTextureTable.NonNull() )
			{
				for( uint t = 0; t < sgTextureTable->GetTexturesCount(); ++t )
				{
					spTexture sgTexture = sgTextureTable->GetTexture( t );
					spString sgTextureFilePath = sgTexture->GetFilePath();
					if( sgTextureFilePath.NonEmpty() )
					{
						std::basic_string<TCHAR> tSourceFilePath = ConstCharPtrToLPCTSTR( sgTextureFilePath.c_str() );
						std::basic_string<TCHAR> tFileName = GetNameOfFile( tSourceFilePath );

						std::basic_string<TCHAR> tTargetFilePath = GetDirectoryOfFile( tOutputFilePath.c_str() );
						std::basic_string<TCHAR> tTargetFileTexturePath = Combine( tTargetFilePath, _T("Textures") );
						std::basic_string<TCHAR> tFinalTargetFilePath = Combine( tTargetFileTexturePath, tFileName );

						const bool bCreated = CreateFolder( tTargetFileTexturePath );

						const bool bFileMoved = MoveFileEx( tSourceFilePath.c_str(),
						                                    tFinalTargetFilePath.c_str(),
						                                    MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) == TRUE;
						if( bFileMoved )
						{
							sgTexture->SetFilePath( LPCTSTRToConstCharPtr( tFinalTargetFilePath.c_str() ) );
						}
					}
				}
			}

			const bool bSaved = sgScene->SaveToFile( LPCTSTRToConstCharPtr( tOutputFilePath.c_str() ) );
		}
	}

	return bProcessedSceneExists;
}

#pragma region INTERNAL
spScene SimplygonProcessingModule::RunReduction( const spScene sgInputScene, bool bBakeMaterials /*= false*/ )
{
	if( false )
	{
		spWavefrontExporter objExporter = sg->CreateWavefrontExporter();
		objExporter->SetExportFilePath( "d:/_max_test.obj" );
		objExporter->SetScene( sgInputScene ); // scene now contains the remeshed geometry and the material table we modified above
		objExporter->RunExport();
	}

	// Create the reduction-processor, and set which scene to reduce
	spReductionProcessor sgReductionProcessor = sg->CreateReductionProcessor();

	// Create a copy of the original scene on which we will run the reduction
	spScene sgLodScene = sgInputScene->NewCopy();

	sgReductionProcessor->SetScene( sgLodScene );

	///////////////////////////////////////////////////////////////////////////////////////////////
	// SETTINGS - Most of these are set to the same value by default, but are set anyway for clarity

	// The reduction settings object contains settings pertaining to the actual decimation
	spReductionSettings sgReductionSettings = sgReductionProcessor->GetReductionSettings();
	// reductionSettings->SetKeepSymmetry(true); //Try, when possible to reduce symmetrically
	sgReductionSettings->SetUseAutomaticSymmetryDetection( false ); // Auto-detect the symmetry plane, if one exists. Can, if required, be set manually instead.
	sgReductionSettings->SetUseHighQualityNormalCalculation(
	    false ); // Drastically increases the quality of the LODs normals, at the cost of extra processing time.
	sgReductionSettings->SetReductionHeuristics(
	    EReductionHeuristics::Consistent ); // Choose between "fast" and "consistent" processing. Fast will look as good, but may cause inconsistent
	                                        // triangle counts when comparing MaxDeviation targets to the corresponding percentage targets.

	spVertexWeightSettings sgVertexWeightSettings = sgReductionProcessor->GetVertexWeightSettings();
	sgVertexWeightSettings->SetUseVertexWeightsInReducer( true );

	sgReductionSettings->SetMergeGeometries( false );
	sgReductionSettings->SetProcessSelectionSetName( "ObjectSelectionSet" );

	// The reducer uses importance weights for all features to decide where and how to reduce.
	// These are advanced settings and should only be changed if you have some specific reduction requirement
	/*reductionSettings->SetShadingImportance(2.f); //This would make the shading twice as important to the reducer as the other features.*/

	// The actual reduction triangle target are controlled by these settings
	sgReductionSettings->SetReductionTargets( EStopCondition::Any, true, false, false, false ); // Selects which targets should be considered when reducing
	sgReductionSettings->SetReductionTargetTriangleRatio( 1.0f );                               // Targets at 50% of the original triangle count
	sgReductionSettings->SetReductionTargetTriangleCount( 10 );                                 // Targets when only 10 triangle remains

	sgReductionSettings->SetReductionTargetMaxDeviation(
	    REAL_MAX );                                            // Targets when an error of the specified size has been reached. As set here it never happens.
	sgReductionSettings->SetReductionTargetOnScreenSize( 50 ); // Targets when the LOD is optimized for the selected on screen pixel size

	// The repair settings object contains settings to fix the geometries
	spRepairSettings sgRepairSettings = sgReductionProcessor->GetRepairSettings();
	sgRepairSettings->SetTJuncDist( 0.0f ); // Removes t-junctions with distance 0.0f
	sgRepairSettings->SetWeldDist( 0.0f );  // Welds overlapping vertices

	// The normal calculation settings deal with the normal-specific reduction settings
	spNormalCalculationSettings sgNormalSettings = sgReductionProcessor->GetNormalCalculationSettings();
	sgNormalSettings->SetReplaceNormals( false ); // If true, this will turn off normal handling in the reducer and recalculate them all afterwards instead.
	                                              // If false, the reducer will try to preserve the original normals as well as possible

	// The bone settings object contains settings for bone optimization.
	spBoneSettings sgBoneSettings = sgReductionProcessor->GetBoneSettings();
	sgBoneSettings->SetBoneReductionTargets( EStopCondition::Any, true, false, false, false );
	sgBoneSettings->SetBoneReductionTargetBoneRatio( 1.0f );
	sgBoneSettings->SetLockBoneSelectionSetName( "BoneLockSelectionSet" );
	sgBoneSettings->SetRemoveBoneSelectionSetName( "BoneRemoveSelectionSet" );

	/*normalSettings->SetHardEdgeAngle( 60.f ); //If the normals are recalculated, this sets the hard-edge angle.*/

	if( bBakeMaterials )
	{
		// Set the Image Mapping Settings.
		spMappingImageSettings sgMappingImageSettings = sgReductionProcessor->GetMappingImageSettings();
		// Without this we cannot fetch data from the original geometry, and thus not
		// generate diffuse, specular, normal maps and custom channel later.
		sgMappingImageSettings->SetGenerateMappingImage( true );
		// Set to generate new texture coordinates.
		sgMappingImageSettings->SetGenerateTexCoords( true );
		// The higher the number, the fewer texture-borders.
		sgMappingImageSettings->GetParameterizerSettings()->SetMaxStretch( 0.25f );
		// Buffer space for when texture is mip-mapped, so color values dont blend over.
		sgMappingImageSettings->GetOutputMaterialSettings( 0 )->SetGutterSpace( 2 );

		sgMappingImageSettings->GetOutputMaterialSettings( 0 )->SetTextureWidth( 1024 );
		sgMappingImageSettings->GetOutputMaterialSettings( 0 )->SetTextureHeight( 1024 );
		sgMappingImageSettings->GetOutputMaterialSettings( 0 )->SetMultisamplingLevel( 2 );

		sgMappingImageSettings->SetTexCoordLevel( 255 );
	}

	// END SETTINGS
	///////////////////////////////////////////////////////////////////////////////////////////////

	// Add progress observer
	if( this->progressObserver != nullptr )
	{
		sgReductionProcessor->AddObserver( progressObserver );
	}

	// Run the actual processing. After this, the set geometry will have been reduced according to the settings
	sgReductionProcessor->RunProcessing();

	if( bBakeMaterials )
	{
		// Mapping image is needed later on for texture casting.
		spMappingImage sgMappingImage = sgReductionProcessor->GetMappingImage();

		// Create new material table.
		spMaterialTable sgOutputMaterialTable = sg->CreateMaterialTable();
		spTextureTable sgOutputTextureTable = sg->CreateTextureTable();

		//	Create new material for the table.
		spMaterial sgOutputMaterial = sg->CreateMaterial();
		sgOutputMaterial->SetName( "SimplygonMaterial" );
		sgOutputMaterial->AddMaterialChannel( SG_MATERIAL_CHANNEL_DIFFUSE );
		sgOutputMaterial->AddMaterialChannel( SG_MATERIAL_CHANNEL_SPECULAR );
		sgOutputMaterial->AddMaterialChannel( SG_MATERIAL_CHANNEL_NORMALS );

		// Add the new material to the table
		sgOutputMaterialTable->AddMaterial( sgOutputMaterial );

		std::basic_string<TCHAR> tDiffuseTextureOutputName = Combine( tTextureOutputPath, _T("Diffuse.png") );
		std::basic_string<TCHAR> tSpecularTextureOutputName = Combine( tTextureOutputPath, _T("Specular.png") );
		std::basic_string<TCHAR> tNormalTextureOutputName = Combine( tTextureOutputPath, _T("Normals.png") );

		const char* cDiffuseTextureOutputName = LPCTSTRToConstCharPtr( tDiffuseTextureOutputName.c_str() );
		const char* cSpecularTextureOutputName = LPCTSTRToConstCharPtr( tSpecularTextureOutputName.c_str() );
		const char* cNormalTextureOutputName = LPCTSTRToConstCharPtr( tNormalTextureOutputName.c_str() );

		// DIFFUSE
		// Create a color caster to cast the diffuse texture data
		spColorCaster sgColorCaster = sg->CreateColorCaster();
		if( this->progressObserver != nullptr )
		{
			sgColorCaster->AddObserver( progressObserver );
		}

		sgColorCaster->GetColorCasterSettings()->SetMaterialChannel( SG_MATERIAL_CHANNEL_DIFFUSE ); // Select the diffuse channel from the original material
		sgColorCaster->SetSourceMaterials( sgLodScene->GetMaterialTable() );
		sgColorCaster->SetSourceTextures(
		    sgLodScene->GetTextureTable() ); // If we are casting materials defined by shading networks, a source texture table also needs to be set
		sgColorCaster->SetMappingImage(
		    sgMappingImage ); // The mapping image we got from the reduction process, reduced to half-width/height, just for testing purposes
		sgColorCaster->GetColorCasterSettings()->SetOutputPixelFormat( EPixelFormat::R8G8B8 ); // RGB
		sgColorCaster->GetColorCasterSettings()->SetDilation(
		    10 ); // To avoid mip-map artifacts, the empty pixels on the map needs to be filled to a degree as well.
		sgColorCaster->SetOutputFilePath( cDiffuseTextureOutputName ); // Where the texture map will be saved to file.
		sgColorCaster->RunProcessing();

		// set the material properties
		// Set material to point to created texture filename.
		AddSimplygonTexture( sgOutputMaterial, sgOutputTextureTable, ConstCharPtrToLPCTSTR( SG_MATERIAL_CHANNEL_DIFFUSE ), tDiffuseTextureOutputName );

		// SPECULAR
		// Modify the color caster to cast specular texture data
		sgColorCaster->GetColorCasterSettings()->SetMaterialChannel( SG_MATERIAL_CHANNEL_SPECULAR ); // Select the specular channel from the original material
		sgColorCaster->SetOutputFilePath( cSpecularTextureOutputName );                              // Where the texture map will be saved to file.
		sgColorCaster->RunProcessing();

		// set the material properties
		// Set material to point to created texture filename.
		AddSimplygonTexture( sgOutputMaterial, sgOutputTextureTable, ConstCharPtrToLPCTSTR( SG_MATERIAL_CHANNEL_SPECULAR ), tSpecularTextureOutputName );

		// NORMAL MAP
		// cast the normal map texture data
		spNormalCaster sgNormalCaster = sg->CreateNormalCaster();
		if( this->progressObserver != nullptr )
		{
			sgNormalCaster->AddObserver( progressObserver );
		}

		sgNormalCaster->SetSourceMaterials( sgLodScene->GetMaterialTable() );
		sgNormalCaster->SetSourceTextures(
		    sgLodScene->GetTextureTable() ); // If we are casting materials defined by shading networks, a source texture table also needs to be set
		sgNormalCaster->SetMappingImage( sgMappingImage );
		sgNormalCaster->GetNormalCasterSettings()->SetOutputPixelFormat( EPixelFormat::R8G8B8 ); // RGB
		sgNormalCaster->GetNormalCasterSettings()->SetDilation( 10 );
		sgNormalCaster->SetOutputFilePath( cNormalTextureOutputName );
		sgNormalCaster->GetNormalCasterSettings()->SetFlipBackfacingNormals( true );
		sgNormalCaster->GetNormalCasterSettings()->SetGenerateTangentSpaceNormals( true );
		sgNormalCaster->RunProcessing();

		// Set material to point to created texture filename.
		AddSimplygonTexture( sgOutputMaterial, sgOutputTextureTable, ConstCharPtrToLPCTSTR( SG_MATERIAL_CHANNEL_NORMALS ), tNormalTextureOutputName );

		// Overwrite the scene's material table with the casted materials
		sgLodScene->GetMaterialTable()->Clear();
		sgLodScene->GetMaterialTable()->Copy( sgOutputMaterialTable );
		sgLodScene->GetTextureTable()->Clear();
		sgLodScene->GetTextureTable()->Copy( sgOutputTextureTable );
	}

	if( false )
	{
		spWavefrontExporter objExporter = sg->CreateWavefrontExporter();
		objExporter->SetExportFilePath( "d:/_max_test_processed.obj" );
		objExporter->SetScene( sgLodScene ); // scene now contains the remeshed geometry and the material table we modified above
		objExporter->RunExport();
	}

	return sgLodScene;
}

spScene SimplygonProcessingModule::RunRemeshing( const spScene sgInputScene, bool bBakeMaterials /*= false*/ )
{
	const uint onScreenSize = 300;
	const uint mergeDistance = 0;
	const uint textureSize = 1024;

	spScene sgLodScene = sgInputScene->NewCopy();

	spMaterialTable sgMaterialTable = sgLodScene->GetMaterialTable();
	spTextureTable sgTextureTable = sgLodScene->GetTextureTable();

	// Create a remeshing processor
	spRemeshingLegacyProcessor sgRemeshingLegacyProcessor = sg->CreateRemeshingLegacyProcessor();

	///////////////////////////////////////////////////////////////////////////////////////////////
	// SETTINGS
	sgRemeshingLegacyProcessor->SetScene( sgLodScene ); // Defines the scene on which to run the remesher

	// Geometry related settings:
	spRemeshingLegacySettings sgRemeshingLegacySettings = sgRemeshingLegacyProcessor->GetRemeshingLegacySettings();
	// remeshingSettings->SetProcessSelectionSetID(-1); //Can be used to remesh only a specific selection set defined in the scene
	sgRemeshingLegacySettings->SetOnScreenSize( onScreenSize );   // The most important setting, defines the "resolution" of the remeshing, i.e. tri-count
	sgRemeshingLegacySettings->SetMergeDistance( mergeDistance ); // Defines how large gaps to fill in, in pixels. Relative to the setting above.

	sgRemeshingLegacySettings->SetSurfaceTransferMode( ESurfaceTransferMode::Accurate ); // This toggles between the two available surface mapping modes
	// remeshingSettings->SetTransferColors(false); //Sets if the remesher should transfer the old vertex colors to the new geometry, if availible
	// remeshingSettings->SetTransferNormals(false) //Sets if the remesher should transfer the old normals to the new geometry (generally a bad idea since the
	// geometries don't match exactly)
	sgRemeshingLegacySettings->SetHardEdgeAngle(
	    80.f ); // Sets the normal hard edge angle, used for normal recalc if TransferNormals is off. Here, slightly lower than 90 degrees.
	// remeshingSettings->SetUseCuttingPlanes( false ); //Defines whether to use cutting planes or not. Planes are defined in the scene object.
	// remeshingSettings->SetCuttingPlaneSelectionSetID( -1 ); //Sets a selection set for cutting planes, if you don't want to use all of the planes in the
	// scene.
	sgRemeshingLegacySettings->SetUseEmptySpaceOverride(
	    false ); // Overrides what the remesher considers to be "outside", so you can do interiors. Set coord with SetEmptySpaceOverride.
	// remeshingSettings->SetMaxTriangleSize( 10 ); //Can be used to limit the triangle size of the output mesh, producing more triangles. Can be useful for
	// exotic use cases. remeshingSettings->SetMaxTriangleSize(10); Parameterization and material casting related settings

	spMappingImageSettings sgMappingImageSettings = sgRemeshingLegacyProcessor->GetMappingImageSettings();
	// mappingSettings->SetUseFullRetexturing( true ); //Kind of irrelevant for the remesher, since it always is a "full retexturing"
	sgMappingImageSettings->SetGenerateMappingImage(
	    true ); // Without this we cannot fetch data from the original geometry, and thus not generate diffuse and normal-maps later on.
	sgMappingImageSettings->SetGenerateTexCoords( true ); // Set to generate new texture coordinates.
	sgMappingImageSettings->SetGenerateTangents( true );  // Set to generate new tangents and bitangents.
	sgMappingImageSettings->GetParameterizerSettings()->SetMaxStretch(
	    0.5f ); // The higher the number, the fewer texture-borders. Also introduces more stretch, obviously.
	sgMappingImageSettings->GetOutputMaterialSettings( 0 )->SetGutterSpace(
	    1 ); // Buffer space for when texture is mip-mapped, so color values don't blend over. Greatly influences packing efficiency
	sgMappingImageSettings->SetTexCoordLevel( 0 ); // Sets the output texcoord level.
	sgMappingImageSettings->GetOutputMaterialSettings( 0 )->SetTextureWidth( textureSize );
	sgMappingImageSettings->GetOutputMaterialSettings( 0 )->SetTextureHeight( textureSize );
	sgMappingImageSettings->GetOutputMaterialSettings( 0 )->SetMultisamplingLevel( 2 );
	sgMappingImageSettings->SetMaximumLayers(
	    3 ); // IMPORTANT! This setting defines how many transparent layers the remesher will project onto the outermost surface of the remeshed geom,
	// and hence, how many layers will be in the generated mapping image
	sgMappingImageSettings->SetTexCoordLevel( 255 );
	// END SETTINGS
	///////////////////////////////////////////////////////////////////////////////////////////////

	if( this->progressObserver != nullptr )
	{
		sgRemeshingLegacyProcessor->AddObserver( progressObserver );
	}

	// Run the remeshing
	sgRemeshingLegacyProcessor->RunProcessing();

	///////////////////////////////////////////////////////////////////////////////////////////////
	// CASTING
	// Now, we need to retrieve the generated mapping image and use it to cast the old materials into a new one, for each channel.
	spMappingImage sgMappingImage = sgRemeshingLegacyProcessor->GetMappingImage();

	// Now, for each channel, we want to cast the input materials into a single output material, with one texture per channel.
	// Create new material for the table.
	spMaterial sgLodMaterial = sg->CreateMaterial();
	sgLodMaterial->SetName( "SimplygonMaterial" );

	// Make a new tex table
	spTextureTable sgLodTextureTable = sg->CreateTextureTable();

	if( bBakeMaterials )
	{
		// Create new material table.
		spMaterialTable sgOutputMaterialTable = sg->CreateMaterialTable();
		spTextureTable sgOutputTextureTable = sg->CreateTextureTable();

		//	Create new material for the table.
		spMaterial sgOutputMaterial = sg->CreateMaterial();
		sgOutputMaterial->SetName( "SimplygonMaterial" );
		sgOutputMaterial->AddMaterialChannel( SG_MATERIAL_CHANNEL_DIFFUSE );
		sgOutputMaterial->AddMaterialChannel( SG_MATERIAL_CHANNEL_SPECULAR );
		sgOutputMaterial->AddMaterialChannel( SG_MATERIAL_CHANNEL_NORMALS );

		// Add the new material to the table
		sgOutputMaterialTable->AddMaterial( sgOutputMaterial );

		std::basic_string<TCHAR> tDiffuseTextureOutputName = Combine( tTextureOutputPath, _T("Diffuse.png") );
		std::basic_string<TCHAR> tSpecularTextureOutputName = Combine( tTextureOutputPath, _T("Specular.png") );
		std::basic_string<TCHAR> tNormalTextureOutputName = Combine( tTextureOutputPath, _T("Normals.png") );

		const char* cDiffuseTextureOutputName = LPCTSTRToConstCharPtr( tDiffuseTextureOutputName.c_str() );
		const char* cSpecularTextureOutputName = LPCTSTRToConstCharPtr( tSpecularTextureOutputName.c_str() );
		const char* cNormalTextureOutputName = LPCTSTRToConstCharPtr( tNormalTextureOutputName.c_str() );

		// DIFFUSE
		// Create a color caster to cast the diffuse texture data
		spColorCaster sgColorCaster = sg->CreateColorCaster();
		if( this->progressObserver != nullptr )
		{
			sgColorCaster->AddObserver( progressObserver );
		}

		sgColorCaster->GetColorCasterSettings()->SetMaterialChannel( SG_MATERIAL_CHANNEL_DIFFUSE ); // Select the diffuse channel from the original material
		sgColorCaster->SetSourceMaterials( sgLodScene->GetMaterialTable() );
		sgColorCaster->SetSourceTextures(
		    sgLodScene->GetTextureTable() ); // If we are casting materials defined by shading networks, a source texture table also needs to be set
		sgColorCaster->SetMappingImage(
		    sgMappingImage ); // The mapping image we got from the reduction process, reduced to half-width/height, just for testing purposes
		sgColorCaster->GetColorCasterSettings()->SetOutputPixelFormat( EPixelFormat::R8G8B8 ); // RGB
		sgColorCaster->GetColorCasterSettings()->SetDilation(
		    10 ); // To avoid mip-map artifacts, the empty pixels on the map needs to be filled to a degree as well.
		sgColorCaster->SetOutputFilePath( cDiffuseTextureOutputName ); // Where the texture map will be saved to file.
		sgColorCaster->RunProcessing();

		// set the material properties
		// Set material to point to created texture filename.
		AddSimplygonTexture( sgOutputMaterial, sgOutputTextureTable, ConstCharPtrToLPCTSTR( SG_MATERIAL_CHANNEL_DIFFUSE ), tDiffuseTextureOutputName );

		// SPECULAR
		// Modify the color caster to cast specular texture data
		sgColorCaster->GetColorCasterSettings()->SetMaterialChannel( SG_MATERIAL_CHANNEL_SPECULAR ); // Select the specular channel from the original material
		sgColorCaster->SetOutputFilePath( cSpecularTextureOutputName );                              // Where the texture map will be saved to file.
		sgColorCaster->RunProcessing();

		// set the material properties
		// Set material to point to created texture filename.
		AddSimplygonTexture( sgOutputMaterial, sgOutputTextureTable, ConstCharPtrToLPCTSTR( SG_MATERIAL_CHANNEL_SPECULAR ), tSpecularTextureOutputName );

		// NORMAL MAP
		// cast the normal map texture data
		spNormalCaster sgNormalCaster = sg->CreateNormalCaster();
		if( this->progressObserver != nullptr )
		{
			sgNormalCaster->AddObserver( progressObserver );
		}

		sgNormalCaster->SetSourceMaterials( sgLodScene->GetMaterialTable() );
		sgNormalCaster->SetSourceTextures(
		    sgLodScene->GetTextureTable() ); // If we are casting materials defined by shading networks, a source texture table also needs to be set
		sgNormalCaster->SetMappingImage( sgMappingImage );
		sgNormalCaster->GetNormalCasterSettings()->SetOutputPixelFormat(
		    EPixelFormat::R8G8B8 ); // RGB, 3 channels! (But really the x, y and z values for the normal)
		sgNormalCaster->GetNormalCasterSettings()->SetDilation( 10 );
		sgNormalCaster->SetOutputFilePath( cNormalTextureOutputName );
		sgNormalCaster->GetNormalCasterSettings()->SetFlipBackfacingNormals( true );
		sgNormalCaster->GetNormalCasterSettings()->SetGenerateTangentSpaceNormals( true );
		sgNormalCaster->RunProcessing();

		// Set material to point to created texture filename.
		AddSimplygonTexture( sgOutputMaterial, sgOutputTextureTable, ConstCharPtrToLPCTSTR( SG_MATERIAL_CHANNEL_NORMALS ), tNormalTextureOutputName );

		// Overwrite the scene's material table with the casted materials
		sgLodScene->GetMaterialTable()->Clear();
		sgLodScene->GetMaterialTable()->Copy( sgOutputMaterialTable );
		sgLodScene->GetTextureTable()->Clear();
		sgLodScene->GetTextureTable()->Copy( sgOutputTextureTable );
	}

	// Now, we can clear the original material table in the scene, and replace its contents with our new lodMaterial
	sgMaterialTable->Clear();
	sgMaterialTable->AddMaterial( sgLodMaterial ); // This will be added at matId 0, which will match the remeshed geometry

	// Also, replace the texture list from the original with the new one
	sgTextureTable->Copy( sgLodTextureTable );
	// END CASTING
	///////////////////////////////////////////////////////////////////////////////////////////////

	return sgLodScene;
}

spScene SimplygonProcessingModule::RunAggregation( const spScene sgInputScene, bool bBakeMaterials )
{
	throw std::exception( "RunAggregation is not implemented!" );

	return nullptr;
}

spScene SimplygonProcessingModule::RunReductionTest_1( const spScene sgInputScene )
{
	// Create the reduction-processor, and set which scene to reduce
	spReductionProcessor sgReductionProcessor = sg->CreateReductionProcessor();

	// Create a copy of the original scene on which we will run the reduction
	spScene sgLodScene = sgInputScene->NewCopy();

	sgReductionProcessor->SetScene( sgLodScene );

	///////////////////////////////////////////////////////////////////////////////////////////////
	// SETTINGS - Most of these are set to the same value by default, but are set anyway for clarity

	// The reduction settings object contains settings pertaining to the actual decimation
	spReductionSettings sgReductionSettings = sgReductionProcessor->GetReductionSettings();
	sgReductionSettings->SetAllowDegenerateTexCoords( true );
	sgReductionSettings->SetCreateGeomorphGeometry( false );
	sgReductionSettings->SetDataCreationPreferences( EDataCreationPreferences::PreferOriginalData );
	sgReductionSettings->SetEdgeSetImportance( 1.0f );
	sgReductionSettings->SetGenerateGeomorphData( false );
	sgReductionSettings->SetGeometryImportance( 8.0f );
	sgReductionSettings->SetGroupImportance( 1.0f );
	sgReductionSettings->SetInwardMoveMultiplier( 1.0f );
	sgReductionSettings->SetKeepSymmetry( false );
	sgReductionSettings->SetMaterialImportance( 2.82843f );
	sgReductionSettings->SetReductionTargetMaxDeviation( 1.0f );
	sgReductionSettings->SetMaxEdgeLength( 2147483647.f );
	sgReductionSettings->SetReductionTargetOnScreenSize( 300 );
	sgReductionSettings->SetOutwardMoveMultiplier( 1.0f );

	sgReductionSettings->SetReductionHeuristics( EReductionHeuristics::Fast );
	sgReductionSettings->SetReductionTargets( EStopCondition::All, true, false, false, false );
	sgReductionSettings->SetShadingImportance( 1.0f );
	sgReductionSettings->SetSkinningImportance( 1.0f );
	sgReductionSettings->SetSymmetryAxis( ESymmetryAxis::Y );
	sgReductionSettings->SetSymmetryDetectionTolerance( 0.0004f );
	sgReductionSettings->SetSymmetryOffset( 0.0f );
	sgReductionSettings->SetTextureImportance( 2.82843f );
	sgReductionSettings->SetReductionTargetTriangleCount( 1000 );
	sgReductionSettings->SetReductionTargetTriangleRatio( 0.25f );
	sgReductionSettings->SetUseAutomaticSymmetryDetection( false );
	sgReductionSettings->SetUseHighQualityNormalCalculation( false );
	sgReductionSettings->SetUseSymmetryQuadRetriangulator( true );
	sgReductionSettings->SetVertexColorImportance( 1.0f );

	// Vertex weight settings
	spVertexWeightSettings sgVertexWeightSettings = sgReductionProcessor->GetVertexWeightSettings();
	sgVertexWeightSettings->SetUseVertexWeightsInReducer( false );

	// The repair settings object contains settings to fix the geometries
	spRepairSettings sgRepairSettings = sgReductionProcessor->GetRepairSettings();
	sgRepairSettings->SetProgressivePasses( 3 );
	sgRepairSettings->SetTJuncDist( 0.0f );
	sgRepairSettings->SetUseTJunctionRemover( false );
	sgRepairSettings->SetUseWelding( true );
	sgRepairSettings->SetWeldDist( 0.0f );
	sgRepairSettings->SetWeldOnlyBorderVertices( false );
	sgRepairSettings->SetWeldOnlyBetweenSceneNodes( false );

	// The normal calculation settings deal with the normal-specific reduction settings
	spNormalCalculationSettings sgNormalSettings = sgReductionProcessor->GetNormalCalculationSettings();
	sgNormalSettings->SetSnapNormalsToFlatSurfaces( false );
	sgNormalSettings->SetHardEdgeAngle( 180.f );
	sgNormalSettings->SetRepairInvalidNormals( false );
	sgNormalSettings->SetReplaceNormals( false );
	sgNormalSettings->SetReplaceTangents( false );
	sgNormalSettings->SetScaleByAngle( true );
	sgNormalSettings->SetScaleByArea( true );

	spVisibilitySettings sgVisibilitySettings = sgReductionProcessor->GetVisibilitySettings();
	if( false )
	{
		// visibilitySettings->SetCameraSelectionSetName("");
		sgVisibilitySettings->SetConservativeMode( false );
		sgVisibilitySettings->SetCullOccludedGeometry( true );
		sgVisibilitySettings->SetFillNonVisibleAreaThreshold( 0.0f );
		sgVisibilitySettings->SetForceVisibilityCalculation( false );
		// visibilitySettings->SetOccluderSelectionSetName("");
		sgVisibilitySettings->SetRemoveTrianglesNotOccludingOtherTriangles( false );
		sgVisibilitySettings->SetUseBackfaceCulling( true );
		sgVisibilitySettings->SetUseVisibilityWeightsInReducer( true );
		sgVisibilitySettings->SetUseVisibilityWeightsInTexcoordGenerator( true );
		sgVisibilitySettings->SetVisibilityWeightsPower( 1.0f );
	}

	// END SETTINGS
	///////////////////////////////////////////////////////////////////////////////////////////////

	// Add progress observer
	if( this->progressObserver != nullptr )
	{
		sgReductionProcessor->AddObserver( progressObserver );
	}

	// Run the actual processing.
	// After this, the set geometry will have been reduced according to the settings
	sgReductionProcessor->RunProcessing();

	return sgLodScene;
}
#pragma endregion

void SimplygonProcessingModule::SetProgressObserver( Observer* progressObserver )
{
	this->progressObserver = progressObserver;
}

void SimplygonProcessingModule::SetErrorHandler( ErrorHandler* errorHandler )
{
	this->errorHandler = errorHandler;
}
void SimplygonProcessingModule::SetTextureOutputDirectory( std::basic_string<TCHAR> tTexturesPath )
{
	this->tTextureOutputPath = tTexturesPath;
}
void SimplygonProcessingModule::SetWorkDirectory( std::basic_string<TCHAR> tWorkDirectoryPath )
{
	this->tWorkDirectory = tWorkDirectoryPath;
}
void SimplygonProcessingModule::SetExternalBatchPath( std::basic_string<TCHAR> tBatchPath )
{
	this->tExternalBatchPath = tBatchPath;
}

void SimplygonProcessingModule::AddSimplygonTexture( spMaterial sgMaterial,
                                                     spTextureTable sgTextureTable,
                                                     std::basic_string<TCHAR> tChannelName,
                                                     std::basic_string<TCHAR> tTexturePath,
                                                     std::basic_string<TCHAR> tNamePrefix )
{
	TCHAR tTextureName[ MAX_PATH ] = { 0 };
	_stprintf_s( tTextureName, MAX_PATH, _T("%s%s"), tNamePrefix.c_str(), tChannelName.c_str() );

	const char* cTexturePath = LPCTSTRToConstCharPtr( tTexturePath.c_str() );
	const char* cChannelName = LPCTSTRToConstCharPtr( tChannelName.c_str() );
	const char* cTextureName = LPCTSTRToConstCharPtr( tTextureName );

	spTexture sgTexture = sg->CreateTexture();
	sgTexture->SetFilePath( cTexturePath );
	sgTexture->SetName( cTextureName );

	sgTextureTable->AddTexture( sgTexture );

	spShadingTextureNode sgTextureNode = sg->CreateShadingTextureNode();
	sgTextureNode->SetTextureName( cTextureName );
	sgTextureNode->SetTexCoordLevel( 0 );

	sgMaterial->SetShadingNetwork( cChannelName, sgTextureNode );
}
