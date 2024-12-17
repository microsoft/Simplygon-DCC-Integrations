// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using Microsoft.Win32;
using System;
using System.Collections;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;

namespace SimplygonUI
{
    /// <summary>
    /// Interaction logic for Logger.xaml
    /// </summary>
    public partial class Logger : UserControl, INotifyPropertyChanged
    {
        protected readonly uint MaxNumEntries = 100u;

        public event PropertyChangedEventHandler PropertyChanged;

        public Logger()
        {
            InitializeComponent();

            LogFilterComboBox.ItemsSource = Enum.GetValues(typeof(LogFilter)).Cast<LogFilter>();
            LogSortComboBox.ItemsSource = Enum.GetValues(typeof(LogSortMode)).Cast<LogSortMode>();
        }

        protected void OnPropertyChanged([CallerMemberName] string name = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        }

        public static readonly DependencyProperty ItemSourceProperty =
             DependencyProperty.Register("ItemsSource", typeof(IEnumerable),
             typeof(Logger), new FrameworkPropertyMetadata(null));

        public IEnumerable ItemsSource
        {
            get { return (IEnumerable)GetValue(ItemSourceProperty); }
            set { SetValue(ItemSourceProperty, value); }
        }

        private void ClearMenuItem_Click(object sender, RoutedEventArgs e)
        {
            (DataContext as UILogger)?.Clear();
        }

        private void LogFilterComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            LogFilter? logFilter = e.AddedItems[0] as LogFilter?;
            if (logFilter.HasValue)
            {
                var logger = this.DataContext as UILogger;
                if (logger != null)
                {
                    logger.FilterMode = logFilter.Value;
                }
            }
        }

        private void LogSortComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            LogSortMode? sortMode = e.AddedItems[0] as LogSortMode?;
            if (sortMode.HasValue)
            {
                var logger = this.DataContext as UILogger;
                if (logger != null)
                {
                    logger.SortMode = sortMode.Value;
                }
            }
        }


        private void ExportLogToFileCM_Click(object sender, RoutedEventArgs e)
        {
            var logEntries = (ItemsSource as ObservableCollection<LogEntry>);

            if (logEntries == null || logEntries.Count == 0)
            {
                MessageBox.Show("There are currently no entries in the log, aborting.", "Export log", MessageBoxButton.OK, MessageBoxImage.Information);
                return;
            }

            SaveFileDialog logExportDialog = new SaveFileDialog()
            {
                Filter = "Log(*.log)|*.log",
                Title = "Export log to file",
                FileName = "SimplygonUI.log",
                InitialDirectory = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments)
            };

            if (logExportDialog.ShowDialog() == true)
            {
                string logExportPath = logExportDialog.FileName;

                try
                {
                    (DataContext as UILogger)?.ExportEntriesToFile(logExportPath);
                }
                catch (Exception ex)
                {
                    MessageBox.Show("Failed to export the log to '" + logExportPath + "'.\n\nError message: " + ex, "Export log", MessageBoxButton.OK, MessageBoxImage.Error);
                }
            }
        }

        private void SettingsButton_Click(object sender, RoutedEventArgs e)
        {
            var logEntries = (ItemsSource as ObservableCollection<LogEntry>);
            bool bHasLogEntries = logEntries != null ? logEntries.Count > 0 : false;

            Button target = sender as Button;
            ContextMenu cm = new ContextMenu();

            var copyAllMenuItem = new MenuItem() { Header = "Copy all log entries", ToolTip = "Copies all log entries into the clipboard." };
            copyAllMenuItem.IsEnabled = bHasLogEntries;
            copyAllMenuItem.Click += CopyAllMenuItem_Click;
            cm.Items.Add(copyAllMenuItem);

            var clearAllMenuItem = new MenuItem() { Header = "Clear all log entries", ToolTip = "Clears all log entries in the Log window." };
            clearAllMenuItem.IsEnabled = bHasLogEntries;
            clearAllMenuItem.Click += ClearMenuItem_Click;
            cm.Items.Add(clearAllMenuItem);

            var exportToFileMenuItem = new MenuItem() { Header = "Export log to file", ToolTip = "Exports the content in the Log window to file." };
            exportToFileMenuItem.IsEnabled = bHasLogEntries;
            exportToFileMenuItem.Click += ExportLogToFileCM_Click;
            cm.Items.Add(exportToFileMenuItem);

            cm.PlacementTarget = target;
            cm.Placement = PlacementMode.Bottom;
            cm.Width = target.Width;
            cm.IsOpen = true;
        }

        private void CopyMenuItem_Click(object sender, RoutedEventArgs e)
        {
            var menuItem = sender as MenuItem;
            if (menuItem != null)
            {
                var logEntry = menuItem.DataContext as LogEntry;
                if (logEntry != null)
                {
                    var clipBoardLogString = logEntry.ExportToString();
                    try
                    {
                        Clipboard.SetText(clipBoardLogString);
                    }
                    catch (Exception ex)
                    {
                        MessageBox.Show("Could not copy log entry to clipboard.\n\nError: " + ex, "Copy log entry", MessageBoxButton.OK, MessageBoxImage.Error);
                    }
                }
            }
        }

        private void CopyAllMenuItem_Click(object sender, RoutedEventArgs e)
        {
            var logger = DataContext as UILogger;
            if (logger != null)
            {
                var clipBoardLogString = logger.ExportEntriesToString();
                try
                {
                    Clipboard.SetText(clipBoardLogString);
                }
                catch (Exception ex)
                {
                    MessageBox.Show("Could not copy log entries to clipboard.\n\nError: " + ex, "Copy log entries", MessageBoxButton.OK, MessageBoxImage.Error);
                }
            }
        }
    }
}
