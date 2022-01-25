// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using System.Collections.Generic;

namespace SimplygonUI
{
    public interface SimplygonIntegrationCallback
    {
        void OnProcess(List<SimplygonSettingsProperty> integrationSettings);
        List<KeyValuePair<string, ESimplygonMaterialCaster>> GetMaterialChannelsFromSelection();
        string GetIntegrationName();
        List<SimplygonSettingsProperty> GetIntegrationSettings();
        void SetTangentCalculatorTypeSetting(Simplygon.ETangentSpaceMethod tangentCalculatorType);
        void ResetTangentCalculatorTypeSetting();
        void UpdatePipelineSettings(SimplygonPipeline pipeline, List<SimplygonSettingsProperty> integrationSettings);
        void UpdateGlobalSettings(SimplygonPipeline pipeline, List<SimplygonSettingsProperty> integrationSettings);
        bool? IsColorManagementEnabled();

        void EnableShortcuts();
        void DisableShortcuts();

        void Log(Category category, string message);
    }

    public interface SimplygonUIExternalAccess
    {
        void LoadPipelineFromFile(string fileName);
        void SavePipeline(string fileName, bool serializeUICompontents, bool showFileDialog);
    }
}
