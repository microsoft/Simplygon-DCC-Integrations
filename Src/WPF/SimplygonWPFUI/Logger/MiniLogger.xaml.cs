// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using System.Windows;
using System.Windows.Controls;

namespace SimplygonUI
{
    /// <summary>
    /// Interaction logic for MiniLogger.xaml
    /// </summary>
    public partial class MiniLogger : UserControl
    {
        public MiniLogger()
        {
            InitializeComponent();
            DataContext = this;
        }

        public static readonly DependencyProperty LogEntryProperty =
             DependencyProperty.Register("LogEntry", typeof(LogEntry),
             typeof(MiniLogger), new FrameworkPropertyMetadata(null));

        public LogEntry LogEntry
        {
            get { return (LogEntry)GetValue(LogEntryProperty); }
            set { SetValue(LogEntryProperty, value); }
        }
    }
}
