// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using System;
using UiViewModels.Actions;

namespace SimplygonUI.MaxUI
{
    public class SimplygonPlugin : UiViewModels.Actions.CuiDockableContentAdapter
    {
        public override bool IsMainContent { get { return true; } }
        public override string MenuText { get { return ActionText; } }
        public override string ButtonText { get { return ActionText; } }
        public override bool IsVisible { get { return true; } }

        protected override void OnLoadingConfiguration(CuiDockableContentConfigEventArgs args)
        {

            base.OnLoadingConfiguration(args);
        }

        public override string ActionText
        {
            get { return "SimplygonUI"; }
        }

        public override string Category
        {
            get { return "Simplygon"; }
        }

        public override Type ContentType
        {
            get { return typeof(SimplygonMaxUI); }
        }

        public override object CreateDockableContent()
        {
            return SimplygonMaxUI.Instance;
        }

        public override UiViewModels.Actions.DockStates.Dock DockingModes
        {
            get { return (UiViewModels.Actions.DockStates.Dock.Left | UiViewModels.Actions.DockStates.Dock.Right | UiViewModels.Actions.DockStates.Dock.Floating); }
        }

        public override string WindowTitle
        {
            get { return "Simplygon"; }
        }

        public override bool IsChecked
        {
            get { return true; }
        }

        public override bool NeedsKeyboardFocus
        {
            get
            {
                return true;
            }
        }
#if SIMPLYGONMAX2018UI || SIMPLYGONMAX2019UI || SIMPLYGONMAX2020UI || SIMPLYGONMAX2021UI || SIMPLYGONMAX2022UI || SIMPLYGONMAX2023UI || SIMPLYGONMAX2024UI
        public override string ObjectName
        {
            get
            {
                return ActionText;
            }
        }
#endif

    }
}

namespace SimplygonUI
{
    using Autodesk.Max;
    using SimplygonUI.MaxUI;

    public class UIAccessor
    {
        private static SimplygonUIExternalAccess UI { get { return SimplygonMaxUI.Instance; } }

        public UIAccessor() { }

        public static bool LoadPipelineFromFile(string pipelineFilePath)
        {
            bool bPipelineLoaded = false;
            try
            {
                UI?.LoadPipelineFromFile(pipelineFilePath);
                bPipelineLoaded = true;
            }
            catch (Exception ex)
            {
                GlobalInterface.Instance.TheListener.EditStream.Puts("[Error] UIAccessor.LoadPipelineFromFile: " + ex.Message + "\n");
            }

            return bPipelineLoaded;
        }

        public static bool SavePipelineToFile(string pipelineFilePath)
        {
            bool bPipelineSaved = false;
            try
            {
                UI?.SavePipeline(pipelineFilePath, true, false);
                bPipelineSaved = true;
            }
            catch (Exception ex)
            {
                GlobalInterface.Instance.TheListener.EditStream.Puts("[Error] UIAccessor.SavePipelineToFile: " + ex.Message + "\n");
            }

            return bPipelineSaved;
        }

        public static void SendErrorToLog(string errorMessage)
        {
            try
            {
                UI?.SendErrorToLog(errorMessage);
            }
            catch (Exception ex)
            {
                GlobalInterface.Instance.TheListener.EditStream.Puts("[Error] UIAccessor.SendErrorToLog: " + ex.Message + "\n");
            }
        }

        public static void SendWarningToLog(string warningMessage)
        {
            try
            {
                UI?.SendWarningToLog(warningMessage);
            }
            catch (Exception ex)
            {
                GlobalInterface.Instance.TheListener.EditStream.Puts("[Error] UIAccessor.SendWarningToLog: " + ex.Message + "\n");
            }
        }

        public static void AssemblyLoad() { }
        public static void AssemblyMain() { }
    }
}