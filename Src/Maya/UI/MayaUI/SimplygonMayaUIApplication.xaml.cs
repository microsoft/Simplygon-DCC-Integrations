// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using System.Windows;

namespace SimplygonUI.MayaUI
{
    /// <summary>
    /// Interaction logic for SimplygonMayaUIApplication.xaml
    /// </summary>
    public partial class SimplygonMayaUIApplication : Application
    {
        public SimplygonMayaUIApplication()
        {
            InitializeComponent();
        }
        protected override void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);
            ShutdownMode = System.Windows.ShutdownMode.OnExplicitShutdown;
        }
    }
}
