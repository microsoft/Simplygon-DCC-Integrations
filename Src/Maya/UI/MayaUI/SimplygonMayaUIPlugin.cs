// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using Autodesk.Maya.OpenMaya;
using System.Runtime.Versioning;
using System.Windows;

[assembly: ExtensionPlugin(typeof(SimplygonUI.MayaUI.SimplygonMayaUIPlugin))]

namespace SimplygonUI.MayaUI
{
    [SupportedOSPlatform("windows6.1")]
    public class SimplygonMayaUIPlugin : IExtensionPlugin
    {
        public static Application app = null;
        public bool InitializePlugin()
        {
            if (Application.Current == null && app == null)
                app = new SimplygonMayaUIApplication();

            if (Application.ResourceAssembly == null)
                Application.ResourceAssembly = typeof(SimplygonMayaUIPlugin).Assembly;

            bool returnValue = PluginCallbacks.InitializePlugin();

            // Fallback method to load UI in Maya 2020.0
            if (MGlobal.apiVersion == 20200000)
            {
                SimplygonUICmd uiCmd = new SimplygonUICmd();
                uiCmd.doIt(null);
            }

            return returnValue;
        }

        public bool UninitializePlugin()
        {
            return PluginCallbacks.UninitializePlugin();
        }

        public string GetMayaDotNetSdkBuildVersion()
        {
            return "";
        }

    }
}
