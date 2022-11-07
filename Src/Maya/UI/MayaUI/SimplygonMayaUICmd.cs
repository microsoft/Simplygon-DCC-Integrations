// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using Autodesk.Maya;
using Autodesk.Maya.OpenMaya;
using System;
using System.Collections.Generic;

[assembly: MPxCommandClass(typeof(SimplygonUI.MayaUI.SimplygonUICmd), "SimplygonUI")]

namespace SimplygonUI.MayaUI
{
    public class SimplygonUICmd : MPxCommand, IMPxCommand
    {
        public static SimplygonMayaUI ui = null;
        public static MForeignWindowWrapper fww;

        List<MDagPath> selectedDagPaths = new List<MDagPath>();
        public override void doIt(MArgList argl)
        {
            int batchMode = 0;
            MGlobal.executeCommand("about -batch", out batchMode);

            if (batchMode == 1)
            {
                return;
            }

            if(argl.length > 0)
            {
                ExecuteScriptCommands(argl);
                return;
            }

            if (ui != null)
            {
                ui.Close();
                fww = null;
            }
            ui = new SimplygonMayaUI();
            ui.Show();

            if (fww == null)
            {
                IntPtr mWindowHandle = new System.Windows.Interop.WindowInteropHelper(ui).Handle;
                fww = new MForeignWindowWrapper(mWindowHandle);
            }

            MGlobal.executeCommand(@"if (`workspaceControl -ex ""Simplygon""`) deleteUI ""Simplygon""");
            MGlobal.executeCommand(@"workspaceControl -visible true -tabToControl ""ChannelBoxLayerEditor"" 1 -iw 450 Simplygon");
            MGlobal.executeCommand(@"control -e -p Simplygon SimplygonUI");

            MEventMessage.Event["SelectionChanged"] += SelectionChanged;
            MEventMessage.Event["SetModified"] += SetModified;
            MEventMessage.Event["NameChanged"] += NameChanged;
            MEventMessage.Event["NewSceneOpened"] += SceneChanged;
            MEventMessage.Event["SceneOpened"] += SceneChanged;
            MEventMessage.Event["SceneImported"] += SceneChanged;

            UpdateSelection();
            UpdateSelectionSets();
        }

        private void SelectionChanged(object sender, MBasicFunctionArgs arg)
        {
            UpdateSelection();
        }

        private void UpdateSelection()
        {
            try
            {
                MSelectionList selectionList = new MSelectionList();
                MGlobal.getActiveSelectionList(selectionList);

                List<string> selectedObjects = new List<string>();
                lock (selectedDagPaths)
                {
                    selectedDagPaths.Clear();
                    for (uint i = 0; i < selectionList.length; ++i)
                    {
                        try
                        {
                            MDagPath dagPath = new MDagPath();
                            selectionList.getDagPath((uint)i, dagPath);
                            if (dagPath.isValid && !string.IsNullOrEmpty(dagPath.fullPathName))
                            {
                                selectedDagPaths.Add(dagPath);
                                selectedObjects.Add(dagPath.fullPathName);
                            }
                        }
                        catch (Exception)
                        {
                            //Ignore this selected object
                        }
                    }
                    ui.SetSelectedObjects(selectedObjects);
                }
            }
            catch (Exception)
            {
            }
        }

        private void SetModified(object sender, MBasicFunctionArgs arg)
        {
            UpdateSelectionSets();
        }

        private void NameChanged(object sender, MBasicFunctionArgs arg)
        {
            UpdateSelectionSets();
        }

        private void UpdateSelectionSets()
        {
            lock (selectedDagPaths)
            {
                ui.UpdateSelectionSets();
            }
        }

        private void SceneChanged(object sender, MBasicFunctionArgs arg)
        {
            UpdateSelection();
            UpdateSelectionSets();
        }

        private void ExecuteScriptCommands(MArgList argl)
        {
            var scriptCommand = argl.asString(0);

            if (!string.IsNullOrEmpty(scriptCommand) && ui != null)
            {
                scriptCommand = scriptCommand.ToLower();
                if (scriptCommand == "-loadpipelinefromfile")
                {
                    if (argl.length > 1)
                    {
                        string filepath = argl.asString(1);
                        ui.MainUI.LoadPipelineFromFile(filepath);
                    }
                    else
                    {
                        MGlobal.displayInfo(@"
Missing file path
Usage:
SimplygonUI -LoadPipelineFromFile ""<file path>""
""");
                    }
                }
                else if (scriptCommand == "-savepipelinetofile")
                {
                    if (argl.length > 1)
                    {
                        string filepath = argl.asString(1);
                        ui.MainUI.SavePipeline(filepath, true);
                    }
                    else
                    {
                        MGlobal.displayInfo(@"
Missing file path
Usage:
SimplygonUI -SavePipelineToFile ""<file path>""
""");
                    }
                }
                else
                {
                    MGlobal.displayInfo(@"
Unknown script command: {scriptCommand}
Usage:
SimplygonUI -SavePipelineToFile ""<file path>""
SimplygonUI -LoadPipelineFromFile ""<file path>""
""");
                }
            }
        }
    }
}
