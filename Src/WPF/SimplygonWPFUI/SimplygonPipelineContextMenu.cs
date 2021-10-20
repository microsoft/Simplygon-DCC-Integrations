// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using System.Collections.Generic;
using System.Linq;
using System.Windows;
using System.Windows.Controls;

namespace SimplygonUI
{
    public interface ISimplygonPipelineContextMenuCallback
    {
        void OnNewPipelineSelected(SimplygonPipeline pipeline);
        void OnNewMaterialCasterSelected(SimplygonMaterialCaster materialCaster);
        void OnAutomaticMaterialCastersSelected();
        bool IsObjectsSelected();
        void OnDeletePipelineSelected(SimplygonPipeline pipeline);
        SimplygonPipeline GetParentPipeline();
    }

    public class SimplygonPipelineContextMenuItemData<T>
    {
        public string Name { get; protected set; }
        public T Target { get; protected set; }
        public bool Cascaded { get; protected set; }
        public SimplygonPipelineContextMenuItemData(string name, T target, bool cascaded = false)
        {
            Name = name;
            Target = target;
            Cascaded = cascaded;
        }
    }

    public class SimplygonPipelineContextMenu
    {
        bool isLODComponentDropdownEnabled;
        bool isCascadedLODComponentDropdownEnabled;
        bool isMaterialCasterComponentDropdownEnabled;
        bool AutomaticMaterialCastersFromSelection;
        ISimplygonPipelineContextMenuCallback callback;
        public MenuItem Root { get; private set; }

        public SimplygonPipelineContextMenu(ISimplygonPipelineContextMenuCallback callback, bool lodComponentDropdownEnabled = true, bool cascadedLODComponentDropdownEnabled = true, bool materialCasterComponentDropdownEnabled = true, bool automaticMaterialCastersFromSelection = false)
        {
            isLODComponentDropdownEnabled = lodComponentDropdownEnabled;
            isCascadedLODComponentDropdownEnabled = cascadedLODComponentDropdownEnabled;
            isMaterialCasterComponentDropdownEnabled = materialCasterComponentDropdownEnabled;
            AutomaticMaterialCastersFromSelection = automaticMaterialCastersFromSelection;
            this.callback = callback;
            Update();
        }

        public void Update()
        {
            Root = new MenuItem() { Header = "LOD Component" };
            if (isLODComponentDropdownEnabled)
            {
                MenuItem lodComponent = null;
                if (isLODComponentDropdownEnabled && !isCascadedLODComponentDropdownEnabled && !isMaterialCasterComponentDropdownEnabled)
                {
                    lodComponent = Root;
                }
                else
                {
                    lodComponent = new MenuItem() { Header = "LOD" };
                }

                foreach (var pipelineTemplate in SimplygonPipelineDatabase.PipelineTemplates)
                {
                    List<string> pathList = pipelineTemplate.MenuPath.Split('/').ToList();

                    if (pathList.Count > 0)
                    {
                        pathList.Reverse();
                        Stack<string> path = new Stack<string>(pathList);
                        BuildDropdownItem<SimplygonPipeline>(lodComponent, path, pipelineTemplate);
                    }
                }
                if (lodComponent != Root)
                {
                    Root.Items.Add(lodComponent);
                }
            }
            if (isCascadedLODComponentDropdownEnabled)
            {
                var cascadedLodComponent = new MenuItem() { Header = "Cascaded LOD" };
                foreach (var pipelineTemplate in SimplygonPipelineDatabase.PipelineTemplates)
                {
                    List<string> pathList = pathList = pipelineTemplate.MenuPath.Split('/').ToList();
                    
                    if (pathList.Count > 0)
                    {
                        pathList.Reverse();
                        Stack<string> path = new Stack<string>(pathList);
                        BuildDropdownItem<SimplygonPipeline>(cascadedLodComponent, path, pipelineTemplate, true);
                    }
                }
                Root.Items.Add(cascadedLodComponent);
            }

            if (isMaterialCasterComponentDropdownEnabled)
            {
                var materialCasterComponent = new MenuItem() { Header = "Material Caster" };

                if (!isLODComponentDropdownEnabled && !isCascadedLODComponentDropdownEnabled && isMaterialCasterComponentDropdownEnabled)
                {
                    materialCasterComponent = Root;
                }

                foreach (var materialCasterTemplate in SimplygonPipelineDatabase.MaterialCasterTemplates)
                {
                    List<string> pathList = materialCasterTemplate.MenuPath.Split('/').ToList();

                    if (pathList.Count > 0)
                    {
                        pathList.Reverse();
                        Stack<string> path = new Stack<string>(pathList);
                        BuildDropdownItem<SimplygonMaterialCaster>(materialCasterComponent, path, materialCasterTemplate);
                    }
                }
                var automaticMaterialCastersMenuItem = new MenuItem() { Header = "Automatic", IsEnabled = callback.IsObjectsSelected() };
                automaticMaterialCastersMenuItem.Tag = new SimplygonPipelineContextMenuItemData<SimplygonMaterialCaster>("Automatic", null, false);
                automaticMaterialCastersMenuItem.Click += ContextMenuItem_Click;

                materialCasterComponent.Items.Add(automaticMaterialCastersMenuItem);
                if (materialCasterComponent != Root)
                {
                    Root.Items.Add(materialCasterComponent);
                }
            }
        }

        private void ContextMenuItem_Click(object sender, RoutedEventArgs e)
        {
            var menuItem = sender as MenuItem;

            if (menuItem != null && menuItem.Tag != null)
            {
                if (menuItem.Tag is SimplygonPipelineContextMenuItemData<SimplygonPipeline>)
                {
                    SimplygonPipelineContextMenuItemData<SimplygonPipeline> itemData = menuItem.Tag as SimplygonPipelineContextMenuItemData<SimplygonPipeline>;

                    callback.OnNewPipelineSelected(itemData.Target);
                }
                else if (menuItem.Tag is SimplygonPipelineContextMenuItemData<SimplygonMaterialCaster>)
                {
                    SimplygonPipelineContextMenuItemData<SimplygonMaterialCaster> itemData = menuItem.Tag as SimplygonPipelineContextMenuItemData<SimplygonMaterialCaster>;

                    if (itemData.Name == "Automatic" && itemData.Target == null)
                    {
                        callback.OnAutomaticMaterialCastersSelected();
                    }
                    else
                    {
                        callback.OnNewMaterialCasterSelected(itemData.Target);
                    }
                }
            }
        }

        protected void BuildDropdownItem<T>(MenuItem parent, Stack<string> path, T target, bool cascaded = false)
        {
            string childName = path.Pop();
            MenuItem child = null;
            foreach (var item in parent.Items)
            {
                MenuItem menuItem = item as MenuItem;
                if (menuItem.Header.ToString() == childName)
                {
                    child = menuItem;
                    break;
                }
            }

            if (child == null)
            {
                if (path.Count == 0)
                {
                    child = new MenuItem() { Header = childName };
                    child.Tag = new SimplygonPipelineContextMenuItemData<T>(childName, target, cascaded);
                    child.Click += ContextMenuItem_Click;
                }
                else
                {
                    child = new MenuItem() { Header = childName };
                }

                parent.Items.Add(child);
            }

            if (path.Count > 0)
            {
                BuildDropdownItem(child, path, target, cascaded);
            }
        }
    }
}
