// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using System.Collections.Generic;

#if SIMPLYGON_INTEGRATION_TESTING
using IntegrationTests.TestFramework;
#endif

namespace SimplygonUI
{
    public interface SimplygonIntegrationCallback
    {
        void OnProcess(List<SimplygonSettingsProperty> integrationSettings);
        List<KeyValuePair<string, ESimplygonMaterialCaster>> GetMaterialChannelsFromSelection();
        string GetIntegrationName();
        string GetIntegrationVersion();
        List<SimplygonSettingsProperty> GetIntegrationSettings();
        void SetTangentCalculatorTypeSetting(Simplygon.ETangentSpaceMethod tangentCalculatorType);
        void ResetTangentCalculatorTypeSetting();
        void UpdatePipelineSettings(SimplygonPipeline pipeline, List<SimplygonSettingsProperty> integrationSettings);
        void UpdateGlobalSettings(SimplygonPipeline pipeline, List<SimplygonSettingsProperty> integrationSettings);
        bool? IsColorManagementEnabled();

        void EnableShortcuts();
        void DisableShortcuts();

        void Log(Category category, string message);

#if SIMPLYGON_INTEGRATION_TESTING
        // Test specific interface methods
        void TestingRestorePristineState();
        void TestingLoadScene(string scenePath);
        void TestingTakeScreenshots(string screenshotsBaseName);
        void TestingRunScript(string script, ScriptFlavor scriptFlavor);
        TestSceneData TestingGatherSceneData(SceneStatsScope sceneStatsScope);
#endif
    }

    public interface SimplygonUIExternalAccess
    {
        void LoadPipelineFromFile(string fileName);
        void SavePipeline(string fileName, bool serializeUICompontents, bool showFileDialog);
        void SendErrorToLog(string errorMessage);
        void SendWarningToLog(string warningMessage);
    }
}
