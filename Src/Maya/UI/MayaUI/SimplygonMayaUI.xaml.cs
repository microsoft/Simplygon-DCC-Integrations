// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using Autodesk.Maya.OpenMaya;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows;

namespace SimplygonUI.MayaUI
{
    /// <summary>
    /// Interaction logic for SimplygonMayaUI.xaml
    /// </summary>
    public partial class SimplygonMayaUI : Window, SimplygonIntegrationCallback
    {
        // supported material channels for Maya std materials
        readonly List<string> MaterialChannelNames = new List<string>() { "color", "transparency", "ambientColor", "incandescence", "normalCamera", "translucence", "translucenceDepth", "translucenceFocus", "specularColor", };

        // supported Maya std material types
        readonly List<MFn.Type> StandardMaterialTypes = new List<MFn.Type>() { MFn.Type.kAnisotropy, MFn.Type.kBlinn, MFn.Type.kPhong, MFn.Type.kLambert };

        // global MEL scripts
        const string MEL_GetMaterialNamesFromSelection =
        "global proc string[] GetMaterialNamesFromSelection()                               \n" +
        "{                                                                                  \n" +
        "   string $mShapes[] = `ls -sl -o -dag -s`;                                        \n" +
        "   string $mShaders[] = `listConnections -type \"shadingEngine\" $mShapes`;        \n" +
        "   string $mMaterials[] = ls(\"-mat\", listConnections($mShaders));                \n" +
        "   return (stringArrayRemoveDuplicates($mMaterials));                              \n" +
        "}                                                                                  \n";

        const string MEL_GetFilteredSelectionSets =
        "global proc string[] GetFilteredSelectionSets()													\n" +
        "{																									\n" +
        "    string $mFilteredSelectionSets[];																\n" +
        "    																								\n" +
        "    $sceneSets = `listSets -allSets`;																\n" +
        "    for($s in $sceneSets)																			\n" +
        "    {																								\n" +
        "        if(!`objExists $s`)																		\n" +
        "        continue;																					\n" +
        "																									\n" +
        "        $b = `setFilterScript $s`;																	\n" +
        "        if($b)																						\n" +
        "        {																							\n" +
        "            stringArrayInsertAtIndex(size($mFilteredSelectionSets), $mFilteredSelectionSets, $s);	\n" +
        "        }																							\n" +
        "    }																								\n" +
        "    																								\n" +
        "    return $mFilteredSelectionSets;																\n" +
        "}																									\n";

        bool GlobalMELScriptsInitialized { get; set; } = false;

        bool InitializeGlobalMELScripts()
        {
            // try to register global scripts for later use
            try
            {
                MGlobal.executeCommand(MEL_GetMaterialNamesFromSelection);
                MGlobal.executeCommand(MEL_GetFilteredSelectionSets);
                GlobalMELScriptsInitialized = true;
            }
            catch (Exception ex)
            {
                Log(Category.Error, "Could not register necessary scrips. \n\nDetails: " + ex);
            }

            return GlobalMELScriptsInitialized;
        }

        List<SimplygonSettingsProperty> integrationSettings;

        public SimplygonMayaUI()
        {
            integrationSettings = new List<SimplygonSettingsProperty>();

            InitializeComponent();
            MainUI.IntegrationParent = this;
            MainUI.SetIntegrationType(SimplygonIntegrationType.Maya);

            Application.Current.Resources.MergedDictionaries.Add(this.Resources);
        }

        public void OnProcess(List<SimplygonSettingsProperty> integrationSettings)
        {
            string tempDir = Environment.GetEnvironmentVariable("SIMPLYGON_9_TEMP");
            try
            {
                if (string.IsNullOrEmpty(tempDir))
                {
                    Log(Category.Error, "SIMPLYGON_9_TEMP environment variable is not set.");
                    return;
                }
                tempDir = Environment.ExpandEnvironmentVariables(tempDir);
                tempDir = Path.Combine(tempDir, Guid.NewGuid().ToString());
                Directory.CreateDirectory(tempDir);

                string tempPipeline = Path.Combine(tempDir, "pipeline.json").Replace("\\", "/");
                MainUI.SavePipeline(tempPipeline);

                string processCommand = $@"Simplygon -sf ""{tempPipeline}""";

                SimplygonUI.MayaUI.Settings.RunMode runModeSetting = integrationSettings.Where(i => i.GetType() == typeof(SimplygonUI.MayaUI.Settings.RunMode)).Select(i => i as SimplygonUI.MayaUI.Settings.RunMode).FirstOrDefault();
                SimplygonUI.MayaUI.Settings.InitialLodIndex initialLODIndexSetting = integrationSettings.Where(i => i.GetType() == typeof(SimplygonUI.MayaUI.Settings.InitialLodIndex)).Select(i => i as SimplygonUI.MayaUI.Settings.InitialLodIndex).FirstOrDefault();
                SimplygonUI.MayaUI.Settings.MeshNameFormat meshNameFormatSetting = integrationSettings.Where(i => i.GetType() == typeof(SimplygonUI.MayaUI.Settings.MeshNameFormat)).Select(i => i as SimplygonUI.MayaUI.Settings.MeshNameFormat).FirstOrDefault();
                SimplygonUI.MayaUI.Settings.SelectProcessedMeshes selectProcessedMeshesSetting = integrationSettings.Where(i => i.GetType() == typeof(SimplygonUI.MayaUI.Settings.SelectProcessedMeshes)).Select(i => i as SimplygonUI.MayaUI.Settings.SelectProcessedMeshes).FirstOrDefault();
                SimplygonUI.MayaUI.Settings.TextureOutputDirectory textureOutputDirectorySetting = integrationSettings.Where(i => i.GetType() == typeof(SimplygonUI.MayaUI.Settings.TextureOutputDirectory)).Select(i => i as SimplygonUI.MayaUI.Settings.TextureOutputDirectory).FirstOrDefault();

                if (runModeSetting != null)
                {
                    switch (runModeSetting.Value)
                    {
                        case Simplygon.EPipelineRunMode.RunInThisProcess:
                            processCommand += " -RunInternally";
                            break;
                        case Simplygon.EPipelineRunMode.RunDistributedUsingSimplygonGrid:
                            processCommand += " -RunSimplygonGrid";
                            break;
                        case Simplygon.EPipelineRunMode.RunDistributedUsingIncredibuild:
                            processCommand += " -RunIncredibuild";
                            break;
                        case Simplygon.EPipelineRunMode.RunDistributedUsingFastbuild:
                            processCommand += " -RunFastbuild";
                            break;
                        default:
                            break;
                    }
                }

                if (initialLODIndexSetting != null)
                {
                    processCommand += $" -InitialLodIndex {initialLODIndexSetting.Value}";
                }

                if (meshNameFormatSetting != null && !string.IsNullOrEmpty(meshNameFormatSetting.Value))
                {
                    processCommand += $@" -MeshNameFormat ""{meshNameFormatSetting.Value}""";
                }

                if (textureOutputDirectorySetting != null && !string.IsNullOrEmpty(textureOutputDirectorySetting.Value))
                {
                    string targetTextureDirectory = textureOutputDirectorySetting.Value.Replace("\\", "/");
                    processCommand += $@" -TextureOutputDirectory ""{targetTextureDirectory}""";
                }

                MGlobal.executeCommand(processCommand);

                if (selectProcessedMeshesSetting != null && selectProcessedMeshesSetting.Value)
                {
                    string queryCommand = $@"SimplygonQuery -SelectProcessedMeshes";
                    MGlobal.executeCommand(queryCommand);
                }
            }
            catch (Exception ex)
            {
                if (ex.ToString().Contains("(kFailure)"))
                {
                    Log(Category.Error, "A script returned an unexpected result, please see the script window for more details. \n Details: " + ex.Message);
                }
                else
                {
                    Log(Category.Error, ex.Message);
                }
            }
            finally
            {
                try
                {
                    Directory.Delete(tempDir, true);
                }
                catch (Exception ex)
                {
                    Log(Category.Warning, "Could not delete the following temporary folder (" + tempDir != null ? tempDir : "null" + ")" + ex.Message);
                }
            }
        }

        public void UpdatePipelineSettings(SimplygonPipeline pipeline, List<SimplygonSettingsProperty> integrationSettings)
        {
            if (pipeline.PipelineSettings != null)
            {
                pipeline.PipelineSettings.SimplygonBatchPath = null;
                pipeline.PipelineSettings.EmbedReferences = false;
            }

            foreach (var cascadedPipeline in pipeline.CascadedPipelines)
            {
                UpdatePipelineSettings(cascadedPipeline, integrationSettings);
            }
        }
        public void UpdateGlobalSettings(SimplygonPipeline pipeline, List<SimplygonSettingsProperty> integrationSettings)
        {
            var tangentSpaceMethodSetting = integrationSettings.Where(i => i.GetType() == typeof(SimplygonUI.MayaUI.Settings.TangentCalculatorType)).FirstOrDefault();

            if (pipeline.GlobalSettings != null)
            {
                if (tangentSpaceMethodSetting != null)
                {
                    pipeline.GlobalSettings.DefaultTangentCalculatorType = ((SimplygonUI.MayaUI.Settings.TangentCalculatorType)tangentSpaceMethodSetting).Value; ;
                }
            }

            foreach (var cascadedPipeline in pipeline.CascadedPipelines)
            {
                UpdateGlobalSettings(cascadedPipeline, integrationSettings);
            }
        }

        public string GetIntegrationName()
        {
            return "Maya";
        }

        public bool? IsColorManagementEnabled()
        {
            int bColorManagementEnabled = 1;
            MGlobal.executeCommand(@"colorManagementPrefs -q -cmEnabled", out bColorManagementEnabled);

            return bColorManagementEnabled == 1 ? true : false;
        }

        public List<KeyValuePair<string, ESimplygonMaterialCaster>> GetMaterialChannelsFromSelection()
        {
            // try to initialize global scripts, if not already done
            if (!GlobalMELScriptsInitialized && !InitializeGlobalMELScripts())
                return new List<KeyValuePair<string, ESimplygonMaterialCaster>>();

            Dictionary<string, ESimplygonMaterialCaster> materialChannels = new Dictionary<string, ESimplygonMaterialCaster>();

            try
            {
                // fetch all materials in the scene selection
                MStringArray materialList = new MStringArray();

                MGlobal.executeCommand(@"GetMaterialNamesFromSelection()", materialList);

                // loop materials to see if there are any supported material types and material channels
                bool hasSupportedMaterial = false;
                for (int i = 0; i < materialList.length; ++i)
                {
                    // get reference to material
                    MSelectionList selectionLists = new MSelectionList();
                    selectionLists.add(materialList[i]);
                    MObject mObject = new MObject();
                    selectionLists.getDependNode(0, mObject);

                    if (!mObject.isNull)
                    {
                        // skip unsupported types
                        MFn.Type materialType = mObject.apiType;
                        if (!StandardMaterialTypes.Contains(materialType))
                            continue;

                        hasSupportedMaterial = true;

                        MFnDependencyNode dependencyNode = new MFnDependencyNode(mObject);

                        // loop all supported material channels and see if they exist in the current material
                        foreach (var materialChannelName in MaterialChannelNames)
                        {
                            if (materialChannels.ContainsKey(materialChannelName))
                                continue;

                            try
                            {
                                MPlug materialChannelPlug = dependencyNode.findPlug(materialChannelName);

                                if (!materialChannelPlug.isNull)
                                {
                                    // assign the correct caster type for the specific material channel
                                    if (materialChannelName == "normalCamera")
                                    {
                                        materialChannels.Add(materialChannelName, ESimplygonMaterialCaster.NormalCaster);
                                    }
                                    else if (materialChannelName == "transparency")
                                    {
                                        materialChannels.Add(materialChannelName, ESimplygonMaterialCaster.OpacityCaster);
                                    }
                                    else
                                    {
                                        materialChannels.Add(materialChannelName, ESimplygonMaterialCaster.ColorCaster);
                                    }
                                }
                            }
                            catch (Exception)
                            {
                                // Maya .NET replaced MStatus with exceptions so we have to ignore this exceptions as it
                                // simply states that the material channel does not exist for the specific material.
                            }
                        }
                    }
                }

                if (!hasSupportedMaterial)
                {
                    Log(Category.Warning, "Failed to generate material casters, could not find any supported materials in the given selection.");
                }

                else if (materialChannels.Count() == 0)
                {
                    Log(Category.Warning, "Failed to generate material casters, could not find any supported material channels for the selected materials.");
                }
            }
            catch (Exception ex)
            {
                Log(Category.Error, "Failed to generate material casters: " + ex.Message);
            }

            return materialChannels.ToList();
        }

        public List<SimplygonSettingsProperty> GetIntegrationSettings()
        {
            integrationSettings = new List<SimplygonSettingsProperty>();

            integrationSettings.Add(new SimplygonUI.MayaUI.Settings.MeshNameFormat());
            integrationSettings.Add(new SimplygonUI.MayaUI.Settings.InitialLodIndex());
            integrationSettings.Add(new SimplygonUI.MayaUI.Settings.RunMode());
            integrationSettings.Add(new SimplygonUI.MayaUI.Settings.TangentCalculatorType());
            integrationSettings.Add(new SimplygonUI.MayaUI.Settings.TextureOutputDirectory());
            integrationSettings.Add(new SimplygonUI.MayaUI.Settings.SelectProcessedMeshes());

            return integrationSettings;
        }
        public void SetTangentCalculatorTypeSetting(Simplygon.ETangentSpaceMethod tangentCalculatorType)
        {
            var tangentCalculatorTypeSetting = integrationSettings.Where(i => i.GetType() == typeof(SimplygonUI.MayaUI.Settings.TangentCalculatorType)).FirstOrDefault();

            if (tangentCalculatorTypeSetting != null)
            {
                ((SimplygonUI.MayaUI.Settings.TangentCalculatorType)tangentCalculatorTypeSetting).Value = tangentCalculatorType;
            }
        }
        public void ResetTangentCalculatorTypeSetting()
        {
            var tangentCalculatorTypeSetting = integrationSettings.Where(i => i.GetType() == typeof(SimplygonUI.MayaUI.Settings.TangentCalculatorType)).FirstOrDefault();

            if (tangentCalculatorTypeSetting != null)
            {
                ((SimplygonUI.MayaUI.Settings.TangentCalculatorType)tangentCalculatorTypeSetting).Reset();
            }
        }

        public void UpdateSelectionSets()
        {
            // try to initialize global scripts, if not already done
            if (!GlobalMELScriptsInitialized && !InitializeGlobalMELScripts())
                return;

            MStringArray setList = new MStringArray();
            MGlobal.executeCommand(@"GetFilteredSelectionSets()", setList);

            // set capacity in case this is done many times
            List<string> selectionSets = new List<string>((int)setList.length);
            for (int i = 0; i < setList.length; ++i)
            {
                string setName = setList[i];
                selectionSets.Add(setName);
            }

            SetSelectionSets(selectionSets);
        }

        public void SetSelectedObjects(List<string> selectedObjects)
        {
            MainUI.SetSelectedObjects(selectedObjects);
        }

        public void SetSelectionSets(List<string> selectionSets)
        {
            MainUI.SetSelectionSets(selectionSets);
        }

        public void EnableShortcuts()
        {

        }

        public void DisableShortcuts()
        {

        }

        public void Log(Category category, string message)
        {
            MainUI.Log(category, message);
        }
    }
}
