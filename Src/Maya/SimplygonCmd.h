// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _SimplygonCmd
#define _SimplygonCmd

#include "Scene.h"
#include "MaterialNode.h"
#include "CriticalSection.h"
#include "MaterialInfoHandler.h"
#include "Common.h"

#include <vector>
#include "SimplygonInit.h"

enum ExtractionType
{
	BATCH_PROCESSOR = 0,
	IMPORT_FROM_FILE,
	EXPORT_TO_FILE,
	PROCESS_FROM_FILE,
	NONE
};

class SimplygonCmd
    : public MPxCommand
    , SimplygonEventRelay
{
	public:
	static std::map<std::string, std::string> s_GlobalMaterialDagPathToGuid;
	static std::map<std::string, std::string> s_GlobalMaterialGuidToDagPath;

	static std::map<std::string, std::string> s_GlobalMeshDagPathToGuid;
	static std::map<std::string, std::string> s_GlobalMeshGuidToDagPath;

	public:
	SimplygonCmd();
	virtual ~SimplygonCmd();

	MStatus doIt( const MArgList& mArgList ) override;
	MStatus redoIt() override;
	MStatus undoIt() override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax createSyntax();

	void Cleanup();
	MStatus ParseArguments( const MArgList& mArgList );

	void SetCurrentProcess( const char* cName );
	void SetCurrentProgressRange( int s, int e );
	void SetCurrentProgress( int val );

	Scene* GetSceneHandler();
	MaterialHandler* GetMaterialHandler();
	static MaterialInfoHandler* GetMaterialInfoHandler();
	WorkDirectoryHandler* GetWorkDirectoryHandler();

	bool GetMergeIdenticallySetupMaterials();

	bool SkipBlendShapeWeightPostfix();
	bool UseCurrentPoseAsBindPose();
	bool UseOldSkinningMethod();
	bool DoNotGenerateMaterials();

	void LogToWindow( std::basic_string<TCHAR> tMessage, int progress = -1 );
	void LogWarningToWindow( std::basic_string<TCHAR> tMessage, int progress = -1 );
	void LogErrorToWindow( std::basic_string<TCHAR> tMessage, int progress = -1 );

	void ProgressCallback( int progress ) override;
	void ErrorCallback( const char* errorMessage ) override;

	Simplygon::spPipeline sgPipeline;
	bool UseSettingsPipelineForProcessing( const INT64 pipelineId );

	MStatus ExportToFile( std::basic_string<TCHAR> tExportFilePath );
	MStatus ImportFromFile( std::basic_string<TCHAR> tImportFilePath );
	void ClearGlobalMapping();

	bool mapMaterials;
	bool mapMeshes;
	bool copyTextures;
	bool clearGlobalMapping;

	bool useQuadExportImport;

	ExtractionType extractionType;

	MString inputSceneFile;
	MString outputSceneFile;

	MString meshFormatString;
	size_t initialLodIndex;

	MString blendshapeFormatString;

	std::map<std::string, std::set<std::string>> selectionSets;
	std::set<std::string> activeSelectionSets;

	private:
	CRITICAL_SECTION cs;
	Scene* sceneHandler;

	MaterialHandler* materialHandler;
	static MaterialInfoHandler* materialInfoHandler;
	WorkDirectoryHandler* workDirectoryHandler;

	bool noMaterialMerging;
	std::string OutputTextureDirectory;

	bool hasProgressWindow;
	bool showBatchWindow;

	void BeginProgress();
	void EndProgress();

	// command flags
	bool listSettings;
	bool creaseValues;
	bool skipBlendShapePostfix;
	bool useCurrentPoseAsBindPose;
	bool doNotGenerateMaterial;
	bool useOldSkinningMethod;

	bool runInternally;
	bool runSimplygonGrid;
	bool runIncredibuild;
	bool runFastbuild;

	std::vector<MString> vertexLockSets;
	std::vector<MString> vertexLockMaterials;

	MSelectionList ReductionList;
	MSelectionList InitialSelectionList;

	MStatus RegisterGlobalScripts();

	MStatus AddNodesToSelectionSet( std::string sgNodeType );

	MStatus PreExtract();
	MStatus ExtractScene();
	MStatus ExtractSceneMaterials();
	MStatus ProcessScene();

	MStatus RunPlugin( const MArgList& mArgList );

	MStatus RemoveLODMeshes();
	MStatus ImportScenes();
};

#endif
