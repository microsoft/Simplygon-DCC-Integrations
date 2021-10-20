// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using Autodesk.Max;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows.Controls;
using System.Windows.Threading;

namespace SimplygonUI.MaxUI
{
    /// <summary>
    /// Interaction logic for SimplygonMaxUI.xaml
    /// </summary>
    public partial class SimplygonMaxUI : UserControl, SimplygonIntegrationCallback, SimplygonUIExternalAccess
    {
        private static readonly object singletonLock = new object();
        private static SimplygonUIExternalAccess instance = null;
        public static SimplygonUIExternalAccess Instance
        {
            get
            {
                lock (singletonLock)
                {
                    if (instance == null)
                    {
                        instance = new SimplygonMaxUI();
                    }
                    return instance;
                }
            }
        }

        protected GlobalDelegates.Delegate5 OnSelectionDelegate;

        const string MaxScript_GetMaterialNamesFromSelection =
"fn GetEnabledChannelNames selectedShaderChannels mat =                                                                                                 \n" +
"(                                                                                                                                                      \n" +
"	local supportedMaterialChannelSlots = #(\"ambientMapEnable\", \"diffuseMapEnable\", \"specularMapEnable\", \"opacityMapEnable\", \"bumpMapEnable\") \n" +
"	local supportedMaterialChannels = #(\"Ambient_Color\", \"Diffuse_Color\", \"Specular_Color\", \"Opacity\", \"Bump\")                                \n" +
"                                                                                                                                                       \n" +
"	for i = 1  to supportedMaterialChannelSlots.Count do                                                                                                \n" +
"	(                                                                                                                                                   \n" +
"		channelName = supportedMaterialChannels[i]                                                                                                      \n" +
"		bHasMap = getProperty mat supportedMaterialChannelSlots[i]                                                                                      \n" +
"		if bHasmap do                                                                                                                                   \n" +
"		(                                                                                                                                               \n" +
"			appendIfUnique  selectedShaderChannels  channelName                                                                                         \n" +
"		)                                                                                                                                               \n" +
"	)                                                                                                                                                   \n" +
")                                                                                                                                                      \n" +
"fn GetMaterialChannelNames =                                                                                                                           \n" +
"(                                                                                                                                                      \n" +
"	local supportedShaderTypes = #(\"Anisotropic\", \"Blinn\", \"Phong\", \"Metal\", \"Multi-Layer\", \"Oren-Nayar-Blinn\", \"Translucent\")            \n" +
"	selectedShaderChannels = #()                                                                                                                        \n" +
"                                                                                                                                                       \n" +
"	for obj in selection do                                                                                                                             \n" +
"	(                                                                                                                                                   \n" +
"		mat = obj.mat                                                                                                                                   \n" +
"		if mat == undefined do                                                                                                                          \n" +
"			continue                                                                                                                                    \n" +
"		                                                                                                                                                \n" +
"		props = getPropNames  mat                                                                                                                       \n" +
"		                                                                                                                                                \n" +
"		for i = 1 to props.count do                                                                                                                     \n" +
"		(                                                                                                                                               \n" +
"			prop = getProperty  mat props[i]                                                                                                            \n" +
"			if (props[i] as string) == \"shaderByName\" do                                                                                              \n" +
"			(                                                                                                                                           \n" +
"				shaderProp = getProperty  mat props[i]                                                                                                  \n" +
"				if finditem supportedShaderTypes shaderProp do                                                                                          \n" +
"				(                                                                                                                                       \n" +
"					GetEnabledChannelNames selectedShaderChannels mat                                                                                   \n" +
"				)                                                                                                                                       \n" +
"			)                                                                                                                                           \n" +
"		)                                                                                                                                               \n" +
"		                                                                                                                                                \n" +
"		subMatCount = getNumSubMtls mat                                                                                                                 \n" +
"		for i = 1  to subMatCount do                                                                                                                    \n" +
"		(                                                                                                                                               \n" +
"			subMat = getSubMtl mat i                                                                                                                    \n" +
"			if subMat == undefined do                                                                                                                   \n" +
"				continue                                                                                                                                \n" +
"					                                                                                                                                    \n" +
"			props = getPropNames subMat                                                                                                                 \n" +
"			                                                                                                                                            \n" +
"			for i = 1 to props.count do                                                                                                                 \n" +
"			(                                                                                                                                           \n" +
"				prop = getProperty subMat props[i]                                                                                                      \n" +
"				if (props[i] as string) == \"shaderByName\" do                                                                                          \n" +
"				(                                                                                                                                       \n" +
"					shaderProp = getProperty  subMat props[i]                                                                                           \n" +
"					if finditem supportedShaderTypes shaderProp do                                                                                      \n" +
"					(                                                                                                                                   \n" +
"						GetEnabledChannelNames selectedShaderChannels submat                                                                            \n" +
"					)                                                                                                                                   \n" +
"				)                                                                                                                                       \n" +
"			)                                                                                                                                           \n" +
"		)                                                                                                                                               \n" +
"	)                                                                                                                                                   \n" +
"	                                                                                                                                                    \n" +
"	selectedShaderChannels                                                                                                                              \n" +
")                                                                                                                                                      \n";

        const string MaxScript_GetPhysicalMaterialNamesFromSelection =
"fn GetAllPhysicalMaterialChannelNames selectedShaderChannels mat =																																\n" +
"(																																																\n" +
"	local supportedMaterialChannels = #(\"base_weight\", \"base_color\", \"reflectivity\", \"refl_color\", \"roughness\", \"diff_rough\", \"metalness\", \"transparency\", 						\n" +
"		\"trans_color\", \"trans_depth\", \"trans_rough\", \"sss_scatter\", \"sss_color\", \"sss_scatter_color\", \"sss_scale\", \"emission\", \"emit_color\", \"emit_luminance\",				\n" +
"		\"emit_kelvin\", \"bump\", \"coat_bump\", \"displacement\", \"cutout\", \"coating\", \"coat_color\", \"coat_roughness\", \"coat_ior\", \"coat_affect_color\", \"coat_affect_roughness\")\n" +
"																																																\n" +
"	for channelName in supportedMaterialChannels do																																				\n" +
"	(																																															\n" +
"		appendIfUnique selectedShaderChannels channelName																																		\n" +
"	)																																															\n" +
")																																																\n" +
"																																																\n" +
"fn GetPhysicalMaterialChannelNames =																																							\n" +
"(																																																\n" +
"	local supportedShaderTypes = #(\"PhysicalMaterial\")																																		\n" +
"	selectedShaderChannels = #()																																								\n" +
"																																																\n" +
"	for obj in selection do																																										\n" +
"	(                                                                                                                                                 											\n" +
"		mat = obj.mat                                                                                                                                 											\n" +
"		if mat == undefined do                                                                                                                        											\n" +
"			continue                                                                                                                                  											\n" +
"																																																\n" +
"		if classof mat == Physical_Material then																																				\n" +
"		(																																														\n" +
"			GetAllPhysicalMaterialChannelNames selectedShaderChannels mat                                                                                 											\n" +
"		)																																														\n" +
"																																																\n" +
"		subMatCount = getNumSubMtls mat																																							\n" +
"		for i = 1  to subMatCount do																																							\n" +
"		(																																														\n" +
"			subMat = getSubMtl mat i                                                                                                                  											\n" +
"			if subMat == undefined do                                                                                                                 											\n" +
"				continue   																																										\n" +
"																																																\n" +
"			if classof subMat == Physical_Material then																																			\n" +
"			(																																													\n" +
"				GetAllPhysicalMaterialChannelNames selectedShaderChannels subMat                                                                           										\n" +
"			)																																													\n" +
"		)                                                                                                                                             											\n" +
"	)                                                                                                                                                 											\n" +
"																																																\n" +
"	selectedShaderChannels                                                                                                                            											\n" +
")																																																\n";

        const string MaxScript_GetSelectionSets =
"fn GetSelectionSets =                                                          \n" +
"(                                                                              \n" +
"    selectionSetNames = #()                                                    \n" +
"	for i =1 to selectionSets.count do                                          \n" +
"	(                                                                           \n" +
"        appendIfUnique selectionSetNames  selectionSets[i].name                \n" +
"    )                                                                          \n" +
"    selectionSetNames                                                          \n" +
")                                                                              \n";

        IGlobal mGlobal { get; set; } = null;
        IInterface mCoreInterface { get; set; } = null;
        bool GlobalMaxScriptsInitialized { get; set; } = false;
        Dispatcher mainThreadDispatcher { get; set; } = null;

        IFPValue ExecuteMaxScript(string script)
        {
            // ExecuteMaxScript can register and call external scripts / methods (in Max or Simplygon plug-in),
            // make sure that everything in this function is executed on main thread!
            var mMaxReturnValue = mainThreadDispatcher.Invoke(new Func<IFPValue>(() =>
            {
                IFPValue mMaxLocalReturnValue = mGlobal.FPValue.Create();

#if SIMPLYGONMAX2017UI || SIMPLYGONMAX2018UI
                mGlobal.ExecuteMAXScriptScript(script, false, mMaxLocalReturnValue);
#elif SIMPLYGONMAX2019UI || SIMPLYGONMAX2020UI || SIMPLYGONMAX2021UI
                mGlobal.ExecuteMAXScriptScript(script, false, mMaxLocalReturnValue, true);
#else
                mGlobal.ExecuteMAXScriptScript(script, Autodesk.Max.MAXScript.ScriptSource.NotSpecified, false, mMaxLocalReturnValue, true);
#endif
                return mMaxLocalReturnValue;
            }));

            return mMaxReturnValue;
        }

        bool InitializeGlobalMaxScripts()
        {
            // try to register global scripts for later use
            try
            {
                IFPValue maxRetVal = mGlobal.FPValue.Create();

#if SIMPLYGONMAX2017UI || SIMPLYGONMAX2018UI
                mGlobal.ExecuteMAXScriptScript(MaxScript_GetMaterialNamesFromSelection, false, maxRetVal);
                mGlobal.ExecuteMAXScriptScript(MaxScript_GetSelectionSets, false, maxRetVal);

#elif SIMPLYGONMAX2019UI || SIMPLYGONMAX2020UI
                mGlobal.ExecuteMAXScriptScript(MaxScript_GetMaterialNamesFromSelection, false, maxRetVal, true);
                mGlobal.ExecuteMAXScriptScript(MaxScript_GetSelectionSets, false, maxRetVal, true);
#elif SIMPLYGONMAX2021UI
                mGlobal.ExecuteMAXScriptScript(MaxScript_GetPhysicalMaterialNamesFromSelection, false, maxRetVal, true);
                mGlobal.ExecuteMAXScriptScript(MaxScript_GetSelectionSets, false, maxRetVal, true);
#else
                mGlobal.ExecuteMAXScriptScript(MaxScript_GetPhysicalMaterialNamesFromSelection, Autodesk.Max.MAXScript.ScriptSource.NotSpecified, false, maxRetVal, true);
                mGlobal.ExecuteMAXScriptScript(MaxScript_GetSelectionSets, Autodesk.Max.MAXScript.ScriptSource.NotSpecified, false, maxRetVal, true);
#endif

                GlobalMaxScriptsInitialized = true;
            }
            catch (Exception ex)
            {
                Log(Category.Error, "Could not register necessary scrips. \n\nDetails: " + ex);
            }

            return GlobalMaxScriptsInitialized;
        }

        List<SimplygonSettingsProperty> integrationSettings;

        private SimplygonMaxUI()
        {
            mainThreadDispatcher = System.Windows.Threading.Dispatcher.CurrentDispatcher;
            integrationSettings = new List<SimplygonSettingsProperty>();

            InitializeComponent();
            MainUI.IntegrationParent = this;
            MainUI.Resources.MergedDictionaries.Add(this.Resources);
#if SIMPLYGONMAX2021UI || SIMPLYGONMAX2022UI
            MainUI.SetIntegrationType(SimplygonIntegrationType.Max2021);
#else
            MainUI.SetIntegrationType(SimplygonIntegrationType.Max);
#endif

            mGlobal = Autodesk.Max.GlobalInterface.Instance;
            mCoreInterface = mGlobal.COREInterface;

            OnSelectionDelegate = new GlobalDelegates.Delegate5(this.OnSelection);
            mGlobal.RegisterNotification(OnSelectionDelegate, null, SystemNotificationCode.SelectionsetChanged);
            mGlobal.RegisterNotification(OnSelectionDelegate, null, SystemNotificationCode.NamedSelSetCreated);
            mGlobal.RegisterNotification(OnSelectionDelegate, null, SystemNotificationCode.NamedSelSetDeleted);
            mGlobal.RegisterNotification(OnSelectionDelegate, null, SystemNotificationCode.NamedSelSetRenamed);
        }

        public virtual void OnSelection(IntPtr param, INotifyInfo info)
        {
            // OnSelection is a callback triggered by various actions inside 3ds Max,
            // make sure that everything in this function is executed on main thread!
            mainThreadDispatcher.Invoke(new Action(() =>
            {
                try
                {
                    List<string> selectedObjects = new List<string>();
                    for (int i = 0; i < mCoreInterface.SelNodeCount; i++)
                    {
                        selectedObjects.Add(mCoreInterface.GetSelNode(i).Name);
                    }

                    SetSelectedObjects(selectedObjects);
                    UpdateSelectionSets();
                }
                catch (Exception ex)
                {
                    Log(Category.Warning, "Could not select processed geometries.\n\n Details: " + ex);
                }
            }));
        }

        private void UpdateSelectionSets()
        {
            // try to initialize global scripts, if not already done
            if (!GlobalMaxScriptsInitialized && !InitializeGlobalMaxScripts())
                return;

            try
            {
                IFPValue maxRetVal = ExecuteMaxScript($@"GetSelectionSets()");

                var mSelectionSetTab = maxRetVal.STab;
                if (mSelectionSetTab != null && mSelectionSetTab.Count > 0)
                {
                    // set capacity in case this is done many times
                    List<string> selectionSets = new List<string>(mSelectionSetTab.Count);

                    for (int c = 0; c < mSelectionSetTab.Count; ++c)
                    {
                        var setName = mSelectionSetTab[c];
                        selectionSets.Add(setName);
                    }

                    SetSelectionSets(selectionSets);
                }
                else
                {
                    SetSelectionSets(new List<string>());
                }
            }
            catch (Exception ex)
            {
                Log(Category.Warning, "Could not read selection sets.\n\n Details: " + ex);
            }
        }

        private void SelectProcessedGeometries()
        {
            try
            {
                mCoreInterface.ClearNodeSelection(false);

                IFPValue maxRetVal = ExecuteMaxScript($@"sgsdk_GetProcessedMeshes()");

                ITab<string> processedMeshNameTab = maxRetVal.STab;

                if (processedMeshNameTab != null && processedMeshNameTab.Count > 0)
                {
#if SIMPLYGONMAX2017UI
                    var mNodeTab = mGlobal.NodeTab.Create();
#elif SIMPLYGONMAX2018UI
                    var mNodeTab = mGlobal.NodeTab.Create();
#elif SIMPLYGONMAX2019UI
                    var mNodeTab = mGlobal.NodeTab.Create();
#else
                    var mNodeTab = mGlobal.INodeTab.Create();
#endif
                    for (int i = 0; i < processedMeshNameTab.Count; ++i)
                    {
                        var mMeshName = processedMeshNameTab[i];
                        var mNode = mCoreInterface.GetINodeByName(mMeshName);
                        if (mNode != null)
                        {
                            mNodeTab.AppendNode(mNode, false, 0);
                        }
                    }

                    mCoreInterface.SelectNodeTab(mNodeTab, true, false);
                }
            }

            catch (Exception ex)
            {
                Log(Category.Warning, "Could not select processed geometries.\n\n Details: " + ex);
            }
        }

        private void ResetMaxStates()
        {
            try
            {
                IFPValue maxRetVal = ExecuteMaxScript($@"sgsdk_Reset()");
            }

            catch (Exception ex)
            {
                Log(Category.Warning, "Could not reset Simplygon plug-in states. \n\nDetails: " + ex);
            }
        }

        public void OnProcess(List<SimplygonSettingsProperty> integrationSettings)
        {
            string tempDir = Environment.GetEnvironmentVariable("SIMPLYGON_9_TEMP");
            try
            {
                if (string.IsNullOrEmpty(tempDir))
                {
                    Log(Category.Error, "Could not read SIMPLYGON_9_TEMP environment variable, aborting!");
                }

                tempDir = Environment.ExpandEnvironmentVariables(tempDir);
                tempDir = Path.Combine(tempDir, Guid.NewGuid().ToString());
                Directory.CreateDirectory(tempDir);

                string tempPipeline = Path.Combine(tempDir, "pipeline.json");

                MainUI.SavePipeline(tempPipeline);

                SimplygonUI.MaxUI.Settings.SelectProcessedMeshes selectProcessedMeshesSetting = integrationSettings.Where(i => i.GetType() == typeof(SimplygonUI.MaxUI.Settings.SelectProcessedMeshes)).Select(i => i as SimplygonUI.MaxUI.Settings.SelectProcessedMeshes).FirstOrDefault();
                SimplygonUI.MaxUI.Settings.ResetStates resetStatesSetting = integrationSettings.Where(i => i.GetType() == typeof(SimplygonUI.MaxUI.Settings.ResetStates)).Select(i => i as SimplygonUI.MaxUI.Settings.ResetStates).FirstOrDefault();
                SimplygonUI.MaxUI.Settings.RunMode runModeSetting = integrationSettings.Where(i => i.GetType() == typeof(SimplygonUI.MaxUI.Settings.RunMode)).Select(i => i as SimplygonUI.MaxUI.Settings.RunMode).FirstOrDefault();
                SimplygonUI.MaxUI.Settings.InitialLodIndex initialLODIndexSetting = integrationSettings.Where(i => i.GetType() == typeof(SimplygonUI.MaxUI.Settings.InitialLodIndex)).Select(i => i as SimplygonUI.MaxUI.Settings.InitialLodIndex).FirstOrDefault();
                SimplygonUI.MaxUI.Settings.MeshNameFormat meshNameFormatSetting = integrationSettings.Where(i => i.GetType() == typeof(SimplygonUI.MaxUI.Settings.MeshNameFormat)).Select(i => i as SimplygonUI.MaxUI.Settings.MeshNameFormat).FirstOrDefault();
                SimplygonUI.MaxUI.Settings.TextureOutputDirectory textureOutputDirectorySetting = integrationSettings.Where(i => i.GetType() == typeof(SimplygonUI.MaxUI.Settings.TextureOutputDirectory)).Select(i => i as SimplygonUI.MaxUI.Settings.TextureOutputDirectory).FirstOrDefault();

                IFPValue maxRetVal = null;

                // reset Max plug-in states, make sure this is done before applying any cusom settings from UI,
                // such as sgsdk_MeshNameFormat and various override functions, or they will be overwritten. 
                // This reset is required as Max is state-based and will otherwise keep previous states set 
                // from script.

                if (resetStatesSetting != null && resetStatesSetting.Value)
                {
                    ResetMaxStates();
                }

                if (runModeSetting != null)
                {
                    maxRetVal = ExecuteMaxScript($@"sgsdk_SetPipelineRunMode {(int)runModeSetting.Value}");
                }

                if (initialLODIndexSetting != null)
                {
                    maxRetVal = ExecuteMaxScript($@"sgsdk_SetInitialLODIndex {initialLODIndexSetting.Value}");
                }

                if (meshNameFormatSetting != null && !string.IsNullOrEmpty(meshNameFormatSetting.Value))
                {
                    maxRetVal = ExecuteMaxScript($@"sgsdk_SetMeshNameFormat ""{meshNameFormatSetting.Value}"" ");
                }

                if (textureOutputDirectorySetting != null && !string.IsNullOrEmpty(textureOutputDirectorySetting.Value))
                {
                    string scriptifiedTexturePath = Path.GetFullPath(textureOutputDirectorySetting.Value).Replace("\\", "\\\\");
                    maxRetVal = ExecuteMaxScript($@"sgsdk_SetTextureOutputDirectory ""{scriptifiedTexturePath}"" ");
                }

                string scriptifiedPipelinePath = Path.GetFullPath(tempPipeline).Replace("\\", "\\\\");
                maxRetVal = ExecuteMaxScript($@"sgsdk_RunPipelineOnSelection ""{scriptifiedPipelinePath}"" ");

                if (selectProcessedMeshesSetting != null && selectProcessedMeshesSetting.Value)
                {
                    SelectProcessedGeometries();
                }

            }
            catch (Exception ex)
            {
                Log(Category.Error, "Could not execute necessary scrips. \n\nDetails: " + ex);
            }
            finally
            {
                try
                {
                    Directory.Delete(tempDir, true);
                }
                catch (Exception)
                {
                }
            }
        }
        public string GetIntegrationName()
        {
            return "3ds Max";
        }

        public bool? IsColorManagementEnabled()
        {
            return null;
        }

        public void UpdatePipelineSettings(SimplygonPipeline pipeline, List<SimplygonSettingsProperty> integrationSettings)
        {
            if (pipeline.PipelineSettings != null)
            {
                pipeline.PipelineSettings.EmbedReferences = false;
            }

            foreach (var cascadedPipeline in pipeline.CascadedPipelines)
            {
                UpdatePipelineSettings(cascadedPipeline, integrationSettings);
            }
        }
        public void UpdateGlobalSettings(SimplygonPipeline pipeline, List<SimplygonSettingsProperty> integrationSettings)
        {
            var tangentSpaceMethodSetting = integrationSettings.Where(i => i.GetType() == typeof(SimplygonUI.MaxUI.Settings.TangentCalculatorType)).FirstOrDefault();

            if (pipeline.GlobalSettings != null)
            {
                if (tangentSpaceMethodSetting != null)
                {
                    pipeline.GlobalSettings.DefaultTangentCalculatorType = ((SimplygonUI.MaxUI.Settings.TangentCalculatorType)tangentSpaceMethodSetting).Value; ;
                }
            }

            foreach (var cascadedPipeline in pipeline.CascadedPipelines)
            {
                UpdateGlobalSettings(cascadedPipeline, integrationSettings);
            }
        }

        public List<SimplygonSettingsProperty> GetIntegrationSettings()
        {
            integrationSettings = new List<SimplygonSettingsProperty>();

            integrationSettings.Add(new SimplygonUI.MaxUI.Settings.MeshNameFormat());
            integrationSettings.Add(new SimplygonUI.MaxUI.Settings.InitialLodIndex());
            integrationSettings.Add(new SimplygonUI.MaxUI.Settings.RunMode());
            integrationSettings.Add(new SimplygonUI.MaxUI.Settings.TangentCalculatorType());
            integrationSettings.Add(new SimplygonUI.MaxUI.Settings.TextureOutputDirectory());
            integrationSettings.Add(new SimplygonUI.MaxUI.Settings.SelectProcessedMeshes());
            integrationSettings.Add(new SimplygonUI.MaxUI.Settings.ResetStates());

            return integrationSettings;
        }

        public void SetTangentCalculatorTypeSetting(Simplygon.ETangentSpaceMethod tangentCalculatorType)
        {
            var tangentCalculatorTypeSetting = integrationSettings.Where(i => i.GetType() == typeof(SimplygonUI.MaxUI.Settings.TangentCalculatorType)).FirstOrDefault();

            if (tangentCalculatorTypeSetting != null)
            {
                ((SimplygonUI.MaxUI.Settings.TangentCalculatorType)tangentCalculatorTypeSetting).Value = tangentCalculatorType;
            }
        }
        public void ResetTangentCalculatorTypeSetting()
        {
            var tangentCalculatorTypeSetting = integrationSettings.Where(i => i.GetType() == typeof(SimplygonUI.MaxUI.Settings.TangentCalculatorType)).FirstOrDefault();

            if (tangentCalculatorTypeSetting != null)
            {
                ((SimplygonUI.MaxUI.Settings.TangentCalculatorType)tangentCalculatorTypeSetting).Reset();
            }
        }

        public List<KeyValuePair<string, ESimplygonMaterialCaster>> GetMaterialChannelsFromSelection()
        {
            // try to initialize global scripts, if not already done
            if (!GlobalMaxScriptsInitialized && !InitializeGlobalMaxScripts())
                return new List<KeyValuePair<string, ESimplygonMaterialCaster>>();

            IFPValue maxRetVal = null;

#if SIMPLYGONMAX2021UI || SIMPLYGONMAX2022UI
            maxRetVal = ExecuteMaxScript($@"GetPhysicalMaterialChannelNames()");
#else

            maxRetVal = ExecuteMaxScript($@"GetMaterialChannelNames()");
#endif

            Dictionary<string, ESimplygonMaterialCaster> materialChannels = new Dictionary<string, ESimplygonMaterialCaster>();

            var mMaterialChannelTab = maxRetVal.STab;

            if (mMaterialChannelTab != null && mMaterialChannelTab.Count > 0)
            {
                for (int c = 0; c < mMaterialChannelTab.Count; ++c)
                {
                    var materialChannelName = mMaterialChannelTab[c];

                    if (materialChannelName == "Bump" || materialChannelName == "bump" || materialChannelName == "coat_bump")
                    {
                        materialChannels.Add(materialChannelName, ESimplygonMaterialCaster.NormalCaster);
                    }
                    else if (materialChannelName == "Opacity" || materialChannelName == "Opacity_Color" || materialChannelName == "transparency")
                    {
                        materialChannels.Add(materialChannelName, ESimplygonMaterialCaster.OpacityCaster);
                    }
                    else
                    {
                        materialChannels.Add(materialChannelName, ESimplygonMaterialCaster.ColorCaster);
                    }
                }
            }

            if (materialChannels.Count() == 0)
            {
                Log(Category.Warning, "Failed to generate material casters, could not find any supported material channels for the selected materials.");
            }

            return materialChannels.ToList();
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
            mGlobal.EnableAccelerators();
        }

        public void DisableShortcuts()
        {
            mGlobal.DisableAccelerators();
        }

        public void LoadPipelineFromFile(string fileName)
        {
            MainUI.LoadPipelineFromFile(fileName);
        }

        public void Log(Category category, string message)
        {
            MainUI.Log(category, message);
        }
    }
}
