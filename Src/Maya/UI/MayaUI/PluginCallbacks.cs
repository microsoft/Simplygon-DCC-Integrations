// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using System;
using System.Diagnostics;
using Autodesk.Maya.OpenMaya;

namespace SimplygonUI.MayaUI
{
    class PluginCallbacks
    {

        public static bool InitializePlugin()
        {
            RegisterCallbacks();
            return true;
        }

        public static bool UninitializePlugin()
        {
            DeregisterCallbacks();
            return true;
        }

        public static void RegisterCallbacks()
        {
            MSceneMessage.BeforePluginLoad += prePluginLoadCallback;
            MSceneMessage.AfterPluginLoad += postPluginLoadCallback;
            MSceneMessage.BeforePluginUnload += prePluginUnloadCallback;
            MSceneMessage.AfterPluginUnload += postPluginUnloadCallback;
        }

        public static void DeregisterCallbacks()
        {
            MSceneMessage.BeforePluginLoad -= prePluginLoadCallback;
            MSceneMessage.AfterPluginLoad -= postPluginLoadCallback;
            MSceneMessage.BeforePluginUnload -= prePluginUnloadCallback;
            MSceneMessage.AfterPluginUnload -= postPluginUnloadCallback;
        }

        private static bool IsSimplygonUIPlugIn(MStringArrayFunctionArgs args)
        {
            if (args == null)
            {
                return false;
            }
            else if (args.strs == null)
            {
                return false;
            }

            return args.strs.Contains("SimplygonMaya" + MGlobal.mayaVersion + "UI");
        }

        private static void prePluginLoadCallback(object sender, MStringArrayFunctionArgs args)
        {
            if (IsSimplygonUIPlugIn(args))
            {
                PrintInfo("PRE plugin load callback with " + args.strs.length + " items:", args.strs);
            }
        }

        private static void postPluginLoadCallback(object sender, MStringArrayFunctionArgs args)
        {
            if (IsSimplygonUIPlugIn(args))
            {
                PrintInfo("POST plugin load callback with " + args.strs.length + " items:", args.strs);

                if (MGlobal.apiVersion != 20200000)
                {
                    // This call will crash in Maya 2020.0
                    MGlobal.executeCommand(@"SimplygonUI");
                }
            }
        }

        private static void prePluginUnloadCallback(object sender, MStringArrayFunctionArgs args)
        {
            if (IsSimplygonUIPlugIn(args))
            {
                PrintInfo("PRE plugin unload callback with " + args.strs.length + " items:", args.strs);
                MGlobal.executeCommand(@"if (`workspaceControl -ex ""Simplygon""`) deleteUI ""Simplygon""");
            }
        }

        private static void postPluginUnloadCallback(object sender, MStringArrayFunctionArgs args)
        {
            if (IsSimplygonUIPlugIn(args))
            {
                PrintInfo("POST plugin unload callback with " + args.strs.length + " items:", args.strs);
            }
        }

        [Conditional("DEBUGPRINT")]
        private static void PrintInfo(string headerMessage, MStringArray str)
        {
            MGlobal.displayInfo(headerMessage);
            for (int i = 0; i < str.length; i++)
            {
                MGlobal.displayInfo("\tCallback item " + i + " is : " + str[i]);
            }
        }
    }
}
