// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using Newtonsoft.Json.Linq;
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
using System.Windows.Shapes;

namespace SimplygonUI
{
    /// <summary>
    /// Interaction logic for SimplygonPipeline.xaml
    /// </summary>
    public partial class SimplygonPipeline : UserControl, ISimplygonPipelineContextMenuCallback
    {
        public SimplygonWPFUIMain UIMain { get; set; }
        public ISimplygonPipelineContextMenuCallback MenuParent { get; set; }
        private SimplygonPipelineContextMenu lodComponentContextMenu;
        public SimplygonPipeline()
        {
            InitializeUI();
        }

        public void InitializeUI()
        {
            InitializeComponent();
            SetLODColor();
            PipelineHeaderTextBlock.Text = PipelineName;

            PipelineTreeView.Items.Clear();
            foreach (var item in Settings)
            {
                PipelineTreeView.Items.Add(item);
            }

            MaterialCasterTreeView.Items.Clear();
            foreach (var materialCaster in MaterialCasters)
            {
                MaterialCasterTreeView.Items.Add(materialCaster);
            }

            foreach (var cascadedPipeline in CascadedPipelines)
            {
                PipelinesStackPanel.Children.Add(cascadedPipeline);
                cascadedPipeline.UIMain = this.UIMain;
                cascadedPipeline.MenuParent = this;

                cascadedPipeline.InitializeUI();
            }

            if (PipelineName == "OcclusionMeshPipeline")
            {
                MaterialCasterGroupBox.Visibility = Visibility.Collapsed;
            }
        }

        private void AddButton_Click(object sender, RoutedEventArgs e)
        {
            lodComponentContextMenu = new SimplygonPipelineContextMenu(this, false, true, false);
            Button target = sender as Button;
            ContextMenu cm = new ContextMenu();
            cm.ItemsSource = lodComponentContextMenu.Root.Items;
            cm.PlacementTarget = target;
            cm.Placement = PlacementMode.Bottom;
            cm.Width = target.Width;
            cm.IsOpen = true;
        }

        private void AddMaterialCasterButton_Click(object sender, RoutedEventArgs e)
        {
            lodComponentContextMenu = new SimplygonPipelineContextMenu(this, false, false, true, true);// Pipeline.GetAutomaticMaterialCastersFromSelection());
            Button target = sender as Button;
            ContextMenu cm = new ContextMenu();
            cm.ItemsSource = lodComponentContextMenu.Root.Items;
            cm.PlacementTarget = target;
            cm.Placement = PlacementMode.Bottom;
            cm.Width = target.Width;
            cm.IsOpen = true;
        }

        private void DeleteButton_Click(object sender, RoutedEventArgs e)
        {
            MenuParent.OnDeletePipelineSelected(this);
        }

        private void DeleteMaterialCasterButton_Click(object sender, RoutedEventArgs e)
        {
            var button = sender as Button;

            if (button.Tag is SimplygonMaterialCaster)
            {
                RemoveMaterialCaster(button.Tag as SimplygonMaterialCaster);
                MaterialCasterTreeView.Items.Remove(button.Tag as SimplygonMaterialCaster);
            }
        }

        private void DeleteAllMaterialCasterButton_Click(object sender, RoutedEventArgs e)
        {
            Button target = sender as Button;
            ContextMenu cm = new ContextMenu();
            cm.PlacementTarget = target;
            cm.Placement = PlacementMode.Bottom;
            cm.Width = target.Width;
            cm.IsOpen = true;

            var deleteAllMenuItem = new MenuItem() { Header = "Remove all", ToolTip = "Remove all added material casters" };
            deleteAllMenuItem.IsEnabled = MaterialCasterTreeView.Items.Count > 0;
            deleteAllMenuItem.Click += (s, se) => 
            {
                MaterialCasters.Clear();
                MaterialCasterTreeView.Items.Clear();
            };
            cm.Items.Add(deleteAllMenuItem);

        }

        public void OnNewPipelineSelected(SimplygonPipeline pipeline)
        {
            if (!string.IsNullOrEmpty(pipeline.FilePath))
            {
                var newPipeline = new SimplygonPipeline(pipeline.FilePath, JObject.Parse(System.IO.File.ReadAllText(pipeline.FilePath)));

                if (newPipeline != null)
                {
                    if (newPipeline.PipelineType == ESimplygonPipeline.Passthrough)
                    {
                        foreach (var cascadedPipeline in newPipeline.CascadedPipelines)
                        {
                            cascadedPipeline.InitializeUI();
                            AddCascadedPipeline(cascadedPipeline);
                        }

                    }
                    else
                    {
                        newPipeline.InitializeUI();
                        AddCascadedPipeline(newPipeline);
                    }
                }
            }
            else
            {
                var newPipeline = pipeline.DeepCopy();
                newPipeline.InitializeUI();
                AddCascadedPipeline(newPipeline);

                if (newPipeline.AutomaticMaterialCastersFromSelection)
                {
                    newPipeline.UpdateAutomaticMaterialCasters();
                }
            }
        }

        public void OnNewMaterialCasterSelected(SimplygonMaterialCaster materialCaster)
        {
            AddMaterialCaster(materialCaster.DeepCopy(PipelineType));
        }

        public void OnAutomaticMaterialCastersSelected()
        {
            UpdateAutomaticMaterialCasters();
        }

        public bool IsObjectsSelected()
        {
            return UIMain.IsObjectsSelected();
        }

        public void OnDeletePipelineSelected(SimplygonPipeline pipeline)
        {
            RemoveCascadedPipeline(pipeline);
        }

        public SimplygonPipeline GetParentPipeline()
        {
            return this;
        }

        private void TextBox_GotFocus(object sender, RoutedEventArgs e)
        {
            if (UIMain != null && UIMain.IntegrationParent != null)
            {
                UIMain.IntegrationParent.DisableShortcuts();
            }
        }

        private void TextBox_LostFocus(object sender, RoutedEventArgs e)
        {
            if (UIMain != null && UIMain.IntegrationParent != null)
            {
                UIMain.IntegrationParent.EnableShortcuts();
            }
        }

        private void TextBox_KeyEnterUpdate(object sender, KeyEventArgs e)
        {
            if (sender is TextBox)
            {
                if (e.Key == Key.Enter)
                {
                    BindingExpression binding = BindingOperations.GetBindingExpression((TextBox)sender, TextBox.TextProperty);
                    if (binding != null)
                    {
                        binding.UpdateSource();
                    }

                    if (UIMain != null && UIMain.IntegrationParent != null)
                    {
                        UIMain.IntegrationParent.EnableShortcuts();
                    }

                    Keyboard.ClearFocus();
                }
            }
        }

        private void ResetSettingsPropertyValue(object sender, RoutedEventArgs e)
        {
            SimplygonSettingsProperty settingsProperty = (sender as MenuItem).DataContext as SimplygonSettingsProperty;

            if (settingsProperty != null)
            {
                settingsProperty.Reset();
                return;
            }

            SimplygonSettings settings = (sender as MenuItem).DataContext as SimplygonSettings;

            if (settings != null)
            {
                settings.Reset();
                return;
            }

            SimplygonMaterialCaster materialCaster = (sender as MenuItem).DataContext as SimplygonMaterialCaster;

            if (materialCaster != null)
            {
                materialCaster.Reset();
                return;
            }
        }

        private void SetLODColor()
        {
            List<string> lodColors = new List<string>()
            {
                "#27a9e0", // Lod 1
                "#37dd37", // Lod 2
                "#ff0063", // Lod 3
                "#dd9b37", // Lod 4
                "#0075ad", // Lod 5
                "#dd37dd", // Lod 6
                "#699157", // Lod 7
                "#3b47bc", // Lod 8
                "#b9df1f", // Lod 9
                "#9f40bf", // Lod 10
            };

            LODBar.Background = new BrushConverter().ConvertFrom(lodColors[(SimplygonWPFUIMain.LODIndex++) % 10]) as SolidColorBrush;
        }

        public void UpdateAutomaticMaterialCasters()
        {
            if (UIMain != null && UIMain.IntegrationParent != null)
            {
                List<KeyValuePair<string, ESimplygonMaterialCaster>> materialChannels = UIMain.IntegrationParent.GetMaterialChannelsFromSelection();

                foreach (var materialChannel in materialChannels)
                {
                    var materialCaster = AddMaterialCaster(materialChannel.Value, materialChannel.Key, false);
                }

                List<SimplygonMaterialCaster> materialCastersToRemove = new List<SimplygonMaterialCaster>();
                foreach (var materialCaster in MaterialCasters)
                {
                    bool exist = materialChannels.Any(i => i.Key == materialCaster.MaterialChannelName);

                    if (!exist)
                    {
                        materialCastersToRemove.Add(materialCaster);
                    }
                }

                foreach (var materialCaster in materialCastersToRemove)
                {
                    RemoveMaterialCaster(materialCaster);
                }

                ApplyDynamicMaterialCasterSettings();
            }
        }

        void ApplyDynamicMaterialCasterSettings()
        {
            // if color management for an integration is disabled,
            // disable sRGB in automatic caster generation.
            var bColorManagement = UIMain.IntegrationParent.IsColorManagementEnabled();
            if (bColorManagement.HasValue)
            {
                if (bColorManagement.Value == false)
                {
                    foreach (var materialCaster in MaterialCasters)
                    {
                        if (materialCaster.ColorCasterSettings != null)
                        {
                            materialCaster.ColorCasterSettings.OutputSRGB = false;
                        }
                        if (materialCaster.OpacityCasterSettings != null)
                        {
                            materialCaster.OpacityCasterSettings.OutputSRGB = false;
                        }
                    }
                }
            }
        }
    }
}
