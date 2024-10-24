// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.IO;
using System.Threading;
using Newtonsoft.Json.Linq;
using System.Runtime.Versioning;

namespace SimplygonUI
{
    /// <summary>
    /// Interaction logic for SimplygonImportSettingsDialog.xaml
    /// </summary>
    [SupportedOSPlatform("windows6.1")]
    public partial class SimplygonImportSettingsDialog : Window
    {
        public SimplygonWPFUIMain MainUI { get; set; }

        private List<string> filesToImport = new List<string>();
        private List<string> foldersToImport = new List<string>();

        private string selectedDestinationFolder;
        private bool selectedKeepFolderStructure;
        private bool selectedIncludeUIMetadata;

        private int importedFileCount;
        private int failedImportedFileCount;

        bool isClosing = false;

        public SimplygonImportSettingsDialog()
        {
            InitializeComponent();

            DestinationFolderTextBox.Text = SimplygonUtils.GetSimplygonPipelineDirectory();
            DestinationFolderTextBox.ToolTip = DestinationFolderTextBox.Text;
        }

        private void AddSourceButton_Click(object sender, RoutedEventArgs e)
        {
            Button target = sender as Button;

            AddSourceContextMenu.PlacementTarget = target;
            AddSourceContextMenu.Placement = PlacementMode.Bottom;
            AddSourceContextMenu.Width = target.Width;
            AddSourceContextMenu.IsOpen = true;
        }

        private void AddSourceFiles_Click(object sender, RoutedEventArgs e)
        {
            OpenFileDialog dialog = new OpenFileDialog()
            {
#if LEGACYSETTINGSSUPPORT
                Filter = "Simplygon Settings Files(*.json;*.spl;*.ini)|*.json;*.spl;*.ini",
#else
                Filter = "Simplygon Settings Files(*.json)|*.json",
#endif
                Title = "Import Simplygon settings",
                Multiselect = true
            };

            if (dialog.ShowDialog() == true)
            {
                if (dialog.FileNames.Length > 0)
                {
                    foreach (var fileName in dialog.FileNames)
                    {
                        if (!filesToImport.Contains(fileName))
                        {
                            filesToImport.Add(fileName);
                            FileNamesListBox.Items.Add(fileName);

                            SourceStackPanel.IsEnabled = true;
                            ImportButton.IsEnabled = true;
                        }
                    }
                }
            }
        }

        private void AddSourceFolder_Click(object sender, RoutedEventArgs e)
        {
            var dialog = new System.Windows.Forms.FolderBrowserDialog
            {
                RootFolder = Environment.SpecialFolder.MyComputer, //Without RootFolder the dialog is broken in Win11
                Description = "Select folder",
                ShowNewFolderButton = true,
            };

            if (dialog.ShowDialog() == System.Windows.Forms.DialogResult.OK)
            {
                if (!foldersToImport.Contains(dialog.SelectedPath))
                {
                    foldersToImport.Add(dialog.SelectedPath);
                    FileNamesListBox.Items.Add(dialog.SelectedPath);

                    SourceStackPanel.IsEnabled = true;
                    KeepFolderStructureCheckBox.IsEnabled = true;
                    ImportButton.IsEnabled = true;
                }
            }
        }

        private void DestinationFolder_Click(object sender, RoutedEventArgs e)
        {
            var dialog = new System.Windows.Forms.FolderBrowserDialog
            {
                RootFolder = Environment.SpecialFolder.MyComputer, //Without RootFolder the dialog is broken in Win11
                Description = "Select folder",
                ShowNewFolderButton = true,
                SelectedPath = DestinationFolderTextBox.Text
            };

            if (dialog.ShowDialog() == System.Windows.Forms.DialogResult.OK)
            {
                DestinationFolderTextBox.Text = dialog.SelectedPath;
                DestinationFolderTextBox.ToolTip = dialog.SelectedPath;
            }
        }

        private void Import_Click(object sender, RoutedEventArgs e)
        {
            importedFileCount = 0;
            failedImportedFileCount = 0;

            selectedDestinationFolder = DestinationFolderTextBox.Text;
            selectedKeepFolderStructure = KeepFolderStructureCheckBox.IsChecked == true;
            selectedIncludeUIMetadata = includeUIMetadataCheckBox.IsChecked == true;

            SourceStackPanel.IsEnabled = false;
            ProgressBarGrid.Visibility = Visibility.Visible;
            ProgressBar.IsIndeterminate = true;
            ImportButton.IsEnabled = false;
            AddSourceButton.IsEnabled = false;

            bool overwriteFile = OverwriteFilesCheckBox.IsChecked == true;

            Task.Run(() =>
            {
                List<KeyValuePair<string, string>> allFilesToImport = new List<KeyValuePair<string, string>>();

                foreach (string fileName in filesToImport)
                {
                    allFilesToImport.Add(new KeyValuePair<string, string>(fileName, selectedDestinationFolder));
                }

                foreach (string folderName in foldersToImport)
                {
                    EnumerateFilesInFolder(folderName, string.Empty, ref allFilesToImport);
                }

                this.Dispatcher.Invoke(new Action(() =>
                {
                    ProgressBar.Value = 0;
                    ProgressBar.Maximum = allFilesToImport.Count;
                    ProgressBar.IsIndeterminate = false;
                    ProgressBarStatus.Visibility = Visibility.Hidden;
                }));

                var thread = new Thread(() =>
                {
                    foreach (var file in allFilesToImport)
                    {
                        if (isClosing)
                        {
                            return;
                        }

                        ImportFile(file.Key, file.Value, overwriteFile);

                        this.Dispatcher.Invoke(new Action(() =>
                        {
                            ProgressBar.Value = ProgressBar.Value + 1;
                        }));
                    }

                    this.Dispatcher.Invoke(new Action(() =>
                    {
                        ProgressBarStatus.Visibility = Visibility.Visible;
                        ProgressBarStatus.Text = $"Imported files {importedFileCount}";
                        ProgressBar.Visibility = Visibility.Hidden;
                        CancelButton.Visibility = Visibility.Hidden;
                        CloseButton.Visibility = Visibility.Visible;
                    }));
                });
                thread.SetApartmentState(ApartmentState.STA);
                thread.Start();
            });
        }

        private void Cancel_Click(object sender, RoutedEventArgs e)
        {
            isClosing = true;
            Close();
        }

        private void Close_Click(object sender, RoutedEventArgs e)
        {
            Close();
        }

        private void ImportFile(string fileName, string destinationFolder, bool overwriteFile)
        {
            try
            {
                SimplygonPipeline pipeline = null;

                if (System.IO.Path.GetExtension(fileName).ToLower() == ".spl")
                {
#if LEGACYSETTINGSSUPPORT
                    pipeline = SimplygonUI.SPL.Importer.Import(fileName);
#endif
                }
                else if (System.IO.Path.GetExtension(fileName).ToLower() == ".ini")
                {
#if LEGACYSETTINGSSUPPORT
                    pipeline = SimplygonUI.INI.Importer.Import(fileName);
#endif
                }
                else
                {
                    dynamic jsonData = JObject.Parse(System.IO.File.ReadAllText(fileName));
                    this.Dispatcher.Invoke(new Action(() =>
                    {
                        pipeline = new SimplygonPipeline(fileName, jsonData);
                    }));
                }

                if (pipeline != null)
                {
                    Directory.CreateDirectory(destinationFolder);

                    string newFilePath = Path.Combine(destinationFolder, Path.GetFileNameWithoutExtension(fileName) + ".json");

                    if (!overwriteFile)
                    {
                        newFilePath = SimplygonUtils.GetUniqueFilePath(newFilePath);
                    }

                    File.WriteAllText(newFilePath, pipeline.SaveJson(selectedIncludeUIMetadata).ToString());
                    this.Dispatcher.Invoke(new Action(() =>
                    {
                        MainUI.Log(Category.Information, $"Imported {fileName}");
                    }));
                    importedFileCount++;
                }
                else
                {
                    failedImportedFileCount++;
                    this.Dispatcher.Invoke(new Action(() =>
                    {
                        MainUI.Log(Category.Error, $"Failed to import {fileName}");
                    }));
                }
            }
            catch (Exception ex)
            {
                failedImportedFileCount++;
                this.Dispatcher.Invoke(new Action(() =>
                {
                    MainUI.Log(Category.Error, $"Failed to import {fileName}: {ex.Message}");
                }));
            }
        }

        private void EnumerateFilesInFolder(string folderName, string relativeFolderName, ref List<KeyValuePair<string, string>> allFilesToImport)
        {
            this.Dispatcher.Invoke(new Action(() =>
            {
                ProgressBarStatus.Text = $"Searching in {folderName}";
            }));

            if (isClosing)
            {
                return;
            }

            string destinationFolder = selectedDestinationFolder;

            if(selectedKeepFolderStructure)
            {
                destinationFolder = Path.Combine(selectedDestinationFolder, relativeFolderName);
            }

            try
            {
                var iniFiles = Directory.EnumerateFiles(folderName, "*.ini");
                var splFiles = Directory.EnumerateFiles(folderName, "*.spl");
                var jsonFiles = Directory.EnumerateFiles(folderName, "*.json");
                var files = new List<string>();
                files.AddRange(iniFiles);
                files.AddRange(splFiles);
                files.AddRange(jsonFiles);
                foreach (var file in files)
                {
                    allFilesToImport.Add(new KeyValuePair<string, string>(file, destinationFolder));
                }
            }
            catch (Exception ex)
            {
                this.Dispatcher.Invoke(new Action(() =>
                {
                    MainUI.Log(Category.Warning, ex.Message);
                }));
            }

            try
            {
                var directories = Directory.EnumerateDirectories(folderName);
                foreach (var directory in directories)
                {
                    EnumerateFilesInFolder(directory, Path.Combine(relativeFolderName, Path.GetFileName(directory)), ref allFilesToImport);
                }
            }
            catch (Exception ex)
            {
                this.Dispatcher.Invoke(new Action(() =>
                {
                    MainUI.Log(Category.Warning, ex.Message);
                }));
            }
        }
    }
}
