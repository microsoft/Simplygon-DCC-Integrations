// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using Microsoft.Win32;
using Newtonsoft.Json.Linq;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.Versioning;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media.Imaging;

#if SIMPLYGON_INTEGRATION_TESTING
using SimplygonUI.Testing;
using IntegrationTests.TestFramework;
#endif

namespace SimplygonUI
{
    /// <summary>
    /// Interaction logic for SimplygonWPFUIMain.xaml
    /// </summary>
    [SupportedOSPlatform("windows6.1")]
    public partial class SimplygonWPFUIMain : UserControl, ISimplygonPipelineContextMenuCallback, INotifyPropertyChanged, SimplygonUIExternalAccess
    {
        public UILogger uiLogger { get { return UILogger.Instance; } }
        public static int LODIndex = 1;
        public event PropertyChangedEventHandler PropertyChanged;
        protected void OnPropertyChanged([CallerMemberName] string name = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        }

        private bool IsInEditMode { get; set; }
        public bool IsProcessButtonEnabled { get; private set; }
        public ObservableCollection<string> SelectionSets { get; protected set; }
        public SimplygonIntegrationCallback IntegrationParent { get; set; }

        private SimplygonPipelineContextMenu lodComponentContextMenu;
        private List<SimplygonSettingsProperty> integrationSettings;
        private string IntegrationName { get; set; }
        public ObservableCollection<SimplygonPipeline> Pipelines { get; protected set; }

        public SimplygonWPFUIMain()
        {
            Pipelines = new ObservableCollection<SimplygonPipeline>();

            InitializeComponent();
            this.DataContext = this;

            AddLODComponentButton.Visibility = Visibility.Visible;
            SelectedObjectsGroupBox.Visibility = Visibility.Collapsed;
            ProcessButton.Visibility = Visibility.Collapsed;
            IntegrationSettingsGroupBox.Visibility = Visibility.Collapsed;

            SelectionSets = new ObservableCollection<string>() { "" };
            integrationSettings = new List<SimplygonSettingsProperty>();

#if DEBUG
            SimplygonVersionInfo.Text = $"Simplygon v{SimplygonVersion.Build} (debug)";
#else
            SimplygonVersionInfo.Text = $"Simplygon v{SimplygonVersion.Build}";
#endif

            try
            {
                ToolTipService.ShowDurationProperty.OverrideMetadata(typeof(DependencyObject), new FrameworkPropertyMetadata(Int32.MaxValue));
            }
            catch (Exception)
            {
                //Just ignore the exception. The depencyobject is already set.
            }
        }

        public void Log(Category category, string message)
        {
            UILogger.Instance.Log(category, message);
        }

        public void SetIntegrationType(SimplygonIntegrationType integrationType)
        {
            SimplygonIntegration.Type = integrationType;
            SimplygonPipelineDatabase.Refresh();
        }

        public void SetSelectedObjects(List<string> selectedObjects)
        {
            SelectedObjectListBox.ItemsSource = selectedObjects;

            if (IsProcessButtonEnabled != selectedObjects.Count > 0)
            {
                IsProcessButtonEnabled = selectedObjects.Count > 0;
                ProcessImage.Source = IsProcessButtonEnabled ? new BitmapImage(new Uri(@"pack://application:,,,/Simplygon.WPFUI.Net8;component/simplygon_process_enabled.png")) :
                                                               new BitmapImage(new Uri(@"pack://application:,,,/Simplygon.WPFUI.Net8;component/simplygon_process_disabled.png"));
                ProcessButton.IsEnabled = IsProcessButtonEnabled;
            }
        }

        public void SetSelectionSets(List<string> selectionSets)
        {
            SelectionSets = new ObservableCollection<string>() { "" };
            foreach (string selectionSetName in selectionSets.OrderBy(i => i))
            {
                SelectionSets.Add(selectionSetName);
            }
            OnPropertyChanged("SelectionSets");
        }

        private void AddLODComponentButton_Click(object sender, RoutedEventArgs e)
        {
            lodComponentContextMenu = new SimplygonPipelineContextMenu(this, true, false, false);
            Button target = sender as Button;
            ContextMenu cm = new ContextMenu();

            cm.ItemsSource = lodComponentContextMenu.Root.Items;
            cm.PlacementTarget = target;
            cm.Placement = PlacementMode.Bottom;
            cm.Width = target.Width;
            cm.IsOpen = true;
        }

        private void ProcessButton_Click(object sender, RoutedEventArgs e)
        {
            if (IsProcessButtonEnabled)
            {
                if (IntegrationParent != null)
                {
                    IntegrationParent.OnProcess(integrationSettings);
                }
            }
        }

        private void AddPipeline(SimplygonPipeline pipeline)
        {
            pipeline.UIMain = this;
            pipeline.MenuParent = this;

            pipeline.InitializeUI();
            Pipelines.Add(pipeline);
            PipelinesStackPanel.Children.Add(pipeline);

            PipelineHeader.Text = "Simplygon Pipeline";
            AddLODComponentButton.Visibility = Visibility.Visible;
            SelectedObjectsGroupBox.Visibility = Visibility.Visible;
            ProcessButton.Visibility = Visibility.Visible;

            pipeline.SetEditMode(IsInEditMode);

            if (Pipelines.Count == 1)
            {
                IntegrationSettingsTreeView.Items.Clear();

                IntegrationSettingsHeader.Text = $"{IntegrationParent.GetIntegrationName()} Settings";
                integrationSettings = IntegrationParent.GetIntegrationSettings();
                var root = new SimplygonTreeViewItem("");
                foreach (var item in integrationSettings)
                {
                    IntegrationSettingsTreeView.Items.Add(item);
                }
                IntegrationSettingsGroupBox.Visibility = Visibility.Visible;
            }

            if (pipeline.AutomaticMaterialCastersFromSelection)
            {
                pipeline.UpdateAutomaticMaterialCasters();
            }
        }

        public void OnNewPipelineSelected(SimplygonPipeline pipeline)
        {
            try
            {
                if (!string.IsNullOrEmpty(pipeline.FilePath))
                {
                    LoadPipelineFromFile(pipeline.FilePath);
                }
                else
                {
                    pipeline = pipeline.DeepCopy();
                    if (pipeline.PipelineType == ESimplygonPipeline.Passthrough)
                    {
                        foreach (var cascadedPipeline in pipeline.CascadedPipelines)
                        {
                            AddPipeline(cascadedPipeline);
                        }

                        if (pipeline.CascadedPipelines.Count > 0)
                        {
                            IntegrationParent.SetTangentCalculatorTypeSetting(pipeline.CascadedPipelines.First().GlobalSettings.DefaultTangentCalculatorType);
                        }
                    }
                    else
                    {
                        AddPipeline(pipeline);
                        IntegrationParent.SetTangentCalculatorTypeSetting(pipeline.GlobalSettings.DefaultTangentCalculatorType);
                    }
                }
            }
            catch (Exception)
            {
            }
        }

        public void OnNewMaterialCasterSelected(SimplygonMaterialCaster materialCaster)
        {

        }

        public void OnAutomaticMaterialCastersSelected()
        {

        }

        public bool IsObjectsSelected()
        {
            if (SelectedObjectListBox != null && SelectedObjectListBox.ItemsSource != null)
            {
                List<string> selectedObjects = SelectedObjectListBox.ItemsSource as List<string>;
                if (selectedObjects != null)
                {
                    return selectedObjects.Count > 0;
                }
            }
            return false;
        }

        public void OnDeletePipelineSelected(SimplygonPipeline pipeline)
        {
            Pipelines.Remove(pipeline);
            PipelinesStackPanel.Children.Remove(pipeline);

            if (Pipelines.Count == 0)
            {
                PipelineHeader.Text = "Simplygon Pipeline - Please add a LOD component to get started.";
                AddLODComponentButton.Visibility = Visibility.Visible;
                SelectedObjectsGroupBox.Visibility = Visibility.Collapsed;
                ProcessButton.Visibility = Visibility.Collapsed;
                IntegrationSettingsGroupBox.Visibility = Visibility.Collapsed;

                integrationSettings.Clear();
                IntegrationSettingsTreeView.Items.Clear();
            }
        }

        public SimplygonPipeline GetParentPipeline()
        {
            return null;
        }

        public void ApplyPipelineOverrides(SimplygonPipeline pipeline, bool isGenerated)
        {

        }

        private void SettingsButton_Click(object sender, RoutedEventArgs e)
        {
            Button target = sender as Button;
            ContextMenu cm = new ContextMenu();
            var editPipelineCM = new MenuItem() { Header = "Edit pipeline", ToolTip = "Customize the pipeline by hiding UI elements and changing default values." };
            var savePipelineCM = new MenuItem() { Header = "Save pipeline", ToolTip = @"If you save the pipeline to the default location (%USERPROFILE%\Documents\Simplygon\10\Pipelines) it will be available as a LOD component automatically." };
            var loadPipelineCM = new MenuItem() { Header = "Load pipeline", ToolTip = @"Load pipeline not in the default location (%USERPROFILE%\Documents\Simplygon\10\Pipelines)." };
            var exportPipelineCM = new MenuItem() { Header = "Export pipeline", ToolTip = "Export pipeline and remove all UI metadata from the JSON file. Recommended if you plan to use the pipeline without using the Simplygon UI." };
            editPipelineCM.IsCheckable = true;
            editPipelineCM.IsEnabled = Pipelines.Count > 0;
            editPipelineCM.IsChecked = IsInEditMode;
            editPipelineCM.Click += EditPipelineContextMenuItem_Click;
            savePipelineCM.IsEnabled = Pipelines.Count > 0;
            savePipelineCM.Click += SavePipelineContextMenuItem_Click;
            loadPipelineCM.Click += LoadPipelineContextMenuItem_Click;
            exportPipelineCM.IsEnabled = Pipelines.Count > 0;
            exportPipelineCM.Click += ExportPipelineContextMenuItem_Click;

            cm.Items.Add(editPipelineCM);
            cm.Items.Add(savePipelineCM);
            cm.Items.Add(loadPipelineCM);
            cm.Items.Add(new Separator());
#if LEGACYSETTINGSSUPPORT
            var importSPLCM = new MenuItem() { Header = "Import legacy settings", ToolTip = "Import legacy .SPL and .ini settings." };
            importSPLCM.Click += ImportSPLContextMenuItem_Click;
            cm.Items.Add(importSPLCM);
            var batchImportToolTip = @"Batch import and update .JSON, .SPL and .ini settings to the default location (%USERPROFILE%\Documents\Simplygon\10\Pipelines).";
#else
            var batchImportToolTip = @"Batch import and update .JSON settings to the default location (%USERPROFILE%\Documents\Simplygon\10\Pipelines).";
#endif
            var batchImportLegacyCM = new MenuItem() { Header = "Batch import settings", ToolTip = batchImportToolTip };
            batchImportLegacyCM.Click += BatchImportContextMenuItem_Click;
            cm.Items.Add(batchImportLegacyCM);
            cm.Items.Add(exportPipelineCM);
            cm.Items.Add(new Separator());

            var installLicenseCM = new MenuItem() { Header = "Install license" };
            installLicenseCM.Click += InstallLicenseCM_Click;
            var documentationCM = new MenuItem() { Header = "Visit documentation" };
            documentationCM.Click += DocumentationCM_Click;

            cm.Items.Add(installLicenseCM);
            cm.Items.Add(documentationCM);


            cm.PlacementTarget = target;
            cm.Placement = PlacementMode.Bottom;
            cm.Width = target.Width;
            cm.IsOpen = true;
        }

        private void DocumentationCM_Click(object sender, RoutedEventArgs e)
        {
            System.Diagnostics.Process.Start($@"https://documentation.simplygon.com/SimplygonSDK_{SimplygonVersion.Build}");
        }

        private void InstallLicenseCM_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                string simplygonPath = Environment.GetEnvironmentVariable("SIMPLYGON_10_PATH");

                if (!string.IsNullOrEmpty(simplygonPath))
                {
                    simplygonPath = Environment.ExpandEnvironmentVariables(simplygonPath);

                    System.Diagnostics.Process.Start($@"{simplygonPath}/SimplygonLicenseApplication.exe");
                }
            }
            catch (Exception)
            {
            }

        }

        private void EditPipelineContextMenuItem_Click(object sender, RoutedEventArgs e)
        {
            IsInEditMode = !IsInEditMode;
            foreach (var pipeline in Pipelines)
            {
                pipeline.SetEditMode(IsInEditMode);
            }
        }

        private void SavePipelineContextMenuItem_Click(object sender, RoutedEventArgs e)
        {
            SavePipeline(null, true);
        }

#if LEGACYSETTINGSSUPPORT
        private void ImportSPLContextMenuItem_Click(object sender, RoutedEventArgs e)
        {
            OpenFileDialog dialog = new OpenFileDialog()
            {
                Filter = "Simplygon Settings Files(*.spl;*.ini)|*.spl;*.ini",
                Title = "Import legacy Simplygon settings"
            };

            if (dialog.ShowDialog() == true)
            {
                try
                {
                    SimplygonPipeline pipeline = null;
                    if (System.IO.Path.GetExtension(dialog.FileName).ToLower() == ".spl")
                    {
                        pipeline = SimplygonUI.SPL.Importer.Import(dialog.FileName);
                    }
                    else if (System.IO.Path.GetExtension(dialog.FileName).ToLower() == ".ini")
                    {
                        pipeline = SimplygonUI.INI.Importer.Import(dialog.FileName);
                    }
                    if (pipeline != null)
                    {
                        Pipelines.Clear();
                        PipelinesStackPanel.Children.Clear();
                        if (pipeline.PipelineType == ESimplygonPipeline.Passthrough)
                        {
                            foreach (var cascadedPipeline in pipeline.CascadedPipelines)
                            {
                                AddPipeline(cascadedPipeline);
                            }

                            if (pipeline.CascadedPipelines.Count > 0)
                            {
                                IntegrationParent.SetTangentCalculatorTypeSetting(pipeline.CascadedPipelines.First().GlobalSettings.DefaultTangentCalculatorType);
                            }
                        }
                        else
                        {
                            AddPipeline(pipeline);
                            IntegrationParent.SetTangentCalculatorTypeSetting(pipeline.GlobalSettings.DefaultTangentCalculatorType);
                        }
                    }
                    else
                    {
                        Log(Category.Error, $"Failed to import {dialog.FileName}");
                    }

                }
                catch (Exception ex)
                {
                    Log(Category.Error, $"Failed to import {dialog.FileName}: {ex.Message}");
                }
            }
        }
#endif

        private void BatchImportContextMenuItem_Click(object sender, RoutedEventArgs e)
        {
            SimplygonImportSettingsDialog importDialog = new SimplygonImportSettingsDialog();
            importDialog.MainUI = this;
            importDialog.Owner = Window.GetWindow(this);
            if (this.Resources != null)
            {
                importDialog.Resources.MergedDictionaries.Add(this.Resources);
            }
            importDialog.ShowDialog();

            SimplygonPipelineDatabase.Refresh();

            return;
        }

        public void LoadPipelineFromFile(string fileName)
        {
            try
            {
                SimplygonPipeline pipeline = null;

#if LEGACYSETTINGSSUPPORT
                if (System.IO.Path.GetExtension(fileName).ToLower() == ".spl")
                {
                    pipeline = SimplygonUI.SPL.Importer.Import(fileName);
                }
                else if (System.IO.Path.GetExtension(fileName).ToLower() == ".ini")
                {
                    pipeline = SimplygonUI.INI.Importer.Import(fileName);
                }
                else
                {
                    dynamic jsonData = JObject.Parse(System.IO.File.ReadAllText(fileName));
                    pipeline = new SimplygonPipeline(fileName, jsonData);
                }
#else
                dynamic jsonData = JObject.Parse(System.IO.File.ReadAllText(fileName));
                pipeline = new SimplygonPipeline(fileName, jsonData);
#endif

                if (pipeline != null)
                {
                    Pipelines.Clear();
                    PipelinesStackPanel.Children.Clear();
                    if (pipeline.PipelineType == ESimplygonPipeline.Passthrough)
                    {
                        foreach (var cascadedPipeline in pipeline.CascadedPipelines)
                        {
                            AddPipeline(cascadedPipeline);
                        }

                        if (pipeline.CascadedPipelines.Count > 0)
                        {
                            IntegrationParent.SetTangentCalculatorTypeSetting(pipeline.CascadedPipelines.First().GlobalSettings.DefaultTangentCalculatorType);
                        }
                    }
                    else
                    {
                        AddPipeline(pipeline);
                        IntegrationParent.SetTangentCalculatorTypeSetting(pipeline.GlobalSettings.DefaultTangentCalculatorType);
                    }
                    Log(Category.Information, $"Pipeline '{fileName}' loaded.");
                }
            }
            catch (Exception ex)
            {
                Log(Category.Error, $"Could not load pipeline '{fileName}' due to an error. Details: {ex.Message}");
            }
        }

        private void LoadPipelineContextMenuItem_Click(object sender, RoutedEventArgs e)
        {
            OpenFileDialog dialog = new OpenFileDialog()
            {
                Filter = "Simplygon Pipeline Files(*.json)|*.json",
                Title = "Load Simplygon pipeline"
            };

            if (dialog.ShowDialog() == true)
            {
                try
                {
                    LoadPipelineFromFile(dialog.FileName);
                }
                catch (Exception)
                {
                }
            }
        }

        private void ExportPipelineContextMenuItem_Click(object sender, RoutedEventArgs e)
        {
            SavePipeline(null, false, true);
        }

        public void SavePipeline(string filePath, bool serializeUICompontents = false, bool showFileDialog = false)
        {
            try
            {
                SimplygonPipeline outputPipeline = null;
                if (Pipelines.Count > 1)
                {
                    outputPipeline = new SimplygonPipeline(ESimplygonPipeline.Passthrough, string.Empty, false);

                    foreach (var pipeline in Pipelines)
                    {
                        outputPipeline.CascadedPipelines.Add(pipeline);
                    }

                    outputPipeline.FilePath = Pipelines.First().FilePath;
                }
                else if (Pipelines.Count == 1)
                {
                    outputPipeline = Pipelines[0];
                }

                if (string.IsNullOrEmpty(filePath) && !showFileDialog)
                {
                    filePath = outputPipeline.FilePath;
                }


                IntegrationParent.UpdateGlobalSettings(outputPipeline, integrationSettings);
                IntegrationParent.UpdatePipelineSettings(outputPipeline, integrationSettings);

                if (string.IsNullOrEmpty(filePath))
                {
                    SaveFileDialog dialog = new SaveFileDialog()
                    {
                        Filter = "Pipeline Files(*.json)|*.json",
                        Title = "Save pipeline",
                        FileName = "MyPipeline.json",
                        InitialDirectory = SimplygonPipelineDatabase.PipelineDirectory
                    };

                    if (dialog.ShowDialog() == true)
                    {
                        string pipelinePath = dialog.FileName;

                        Directory.CreateDirectory(Path.GetDirectoryName(pipelinePath));
                        File.WriteAllText(dialog.FileName, outputPipeline.SaveJson(serializeUICompontents).ToString());
                        SimplygonPipelineDatabase.Refresh();
                        Log(Category.Information, $"Pipeline saved to {dialog.FileName}.");
                    }
                }
                else
                {

                    Directory.CreateDirectory(Path.GetDirectoryName(filePath));
                    File.WriteAllText(filePath, outputPipeline.SaveJson(serializeUICompontents).ToString());
                    SimplygonPipelineDatabase.Refresh();
                    Log(Category.Information, $"Pipeline saved to {filePath}.");
                }
            }
            catch (Exception ex)
            {
                Log(Category.Error, "Could not save the pipeline to the following path (" + filePath + ") due to an error.\n\n Details: " + ex);
            }
        }

        private bool HasCascadedQuadPipeline(List<SimplygonPipeline> pipelines)
        {
            if (pipelines != null)
            {
                foreach (var pipeline in pipelines)
                {
                    if (pipeline.PipelineName == "QuadReductionPipeline")
                    {
                        return true;
                    }
                    else if (HasCascadedQuadPipeline(pipeline.CascadedPipelines))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        public bool HasQuadPipeline()
        {
            foreach (var pipeline in Pipelines)
            {
                if (pipeline.PipelineName == "QuadReductionPipeline")
                {
                    return true;
                }
                else if (HasCascadedQuadPipeline(pipeline.CascadedPipelines))
                {
                    return true;
                }
            }

            return false;
        }

        private void ScrollViewer_PreviewMouseWheel(object sender, MouseWheelEventArgs e)
        {
            ScrollViewer scv = (ScrollViewer)sender;
            scv.ScrollToVerticalOffset(scv.VerticalOffset - e.Delta);
            e.Handled = true;
        }

        private void TextBox_GotFocus(object sender, RoutedEventArgs e)
        {
            if (IntegrationParent != null)
            {
                IntegrationParent.DisableShortcuts();
            }
        }

        private void TextBox_LostFocus(object sender, RoutedEventArgs e)
        {
            if (IntegrationParent != null)
            {
                IntegrationParent.EnableShortcuts();
            }
        }

        private void btnChoosePath_Click(object sender, RoutedEventArgs e)
        {
            var folderBrowserButton = sender as Button;
            if (folderBrowserButton != null)
            {
                var folderBrowserSetting = folderBrowserButton.DataContext as SimplygonSettingsPropertyFolderBrowser;
                if (folderBrowserSetting != null)
                {
                    string existinFolderPath = folderBrowserSetting.GetPath();

                    var folderPicker = new System.Windows.Forms.FolderBrowserDialog
                    {
                        RootFolder = Environment.SpecialFolder.MyComputer, //Without RootFolder the dialog is broken in Win11
                        Description = "Select directory",
                        ShowNewFolderButton = true,
                        SelectedPath = existinFolderPath.Length > 0 ? existinFolderPath : null
                    };

                    if (folderPicker.ShowDialog() == System.Windows.Forms.DialogResult.OK)
                    {
                        folderBrowserSetting.SetPath(folderPicker.SelectedPath);
                    }
                }
            }
        }

        private void TextBox_GotFocus(object sender, MouseEventArgs e)
        {

        }

        private void TextBoxWithNoResize_GotFocus(object sender, RoutedEventArgs e)
        {
            if (IntegrationParent != null)
            {
                IntegrationParent.DisableShortcuts();
            }
        }

        private void TextBoxWithNoResize_LostFocus(object sender, RoutedEventArgs e)
        {
            if (IntegrationParent != null)
            {
                IntegrationParent.EnableShortcuts();
            }
        }

        void GoToLogTab()
        {
            // 0 = Processing, 1 = Log
            tabControl.SelectedIndex = 1;
        }

        private void StatusBar_MouseLeftButtonUp(object sender, MouseButtonEventArgs e)
        {
            GoToLogTab();
        }

        private void StatusBar_GoToLogWindow_Click(object sender, RoutedEventArgs e)
        {
            GoToLogTab();
        }

        private void StatusBar_ClearLogEntries_Click(object sender, RoutedEventArgs e)
        {
            UILogger.Instance.Clear();
        }

        public void SendErrorToLog(string errorMessage)
        {
            this.Dispatcher.Invoke(new Action(() =>
            {
                Log(Category.Error, errorMessage);
            }));
        }
        public void SendWarningToLog(string warningMessage)
        {
            this.Dispatcher.Invoke(new Action(() =>
            {
                Log(Category.Warning, warningMessage);
            }));
        }

#if SIMPLYGON_INTEGRATION_TESTING
        // Testing
        private TestSequenceWatcher tsw { get; set; }
        public WPFDriver wpfDriver { get; set; }

        public void StartTestDriver()
        {
            tsw = new TestSequenceWatcher(IntegrationParent.GetIntegrationName(), IntegrationParent.GetIntegrationVersion());
            tsw.TestSequenceDetected += StartTestSequence;
        }

        private void StartTestSequence(object sender, TestSequence testSequence)
        {
            // Initiate wpfDriver if it's not initialized
            if (wpfDriver == null)
            {
                var integration = $"{IntegrationParent.GetIntegrationName()}{IntegrationParent.GetIntegrationVersion()}";
                // Special Max mappings
                if (IntegrationParent.GetIntegrationName() == "3ds Max")
                {
                    // Max provides no interface to get the "year" version (e.g. Max 2022) of the product
                    // through their SDK APIs (AFAI(Jeppe)K), so the commonly used approach to calculate this
                    // is to take the integration version (e.g. 24 for Max 2022) and remove 2 from it.
                    var parsedVersion = int.Parse(IntegrationParent.GetIntegrationVersion().Split('.')[0]);
                    integration = $"Max20{parsedVersion - 2}";
                }
                wpfDriver = new WPFDriver(this, integration);
            }
            wpfDriver.AddTestSequenceToQueue(testSequence);
        }
#endif
    }
}
