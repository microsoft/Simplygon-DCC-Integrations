// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Data;
using System.Windows.Media;

namespace SimplygonUI
{
    public enum Category
    {
        Information = 0,
        Warning,
        Error
    }

    public class LogEntry
    {
        public Category Category { get; private set; }
        public string Message { get; private set; }

        public SolidColorBrush CategoryBrush
        {
            get
            {
                if (Category == Category.Warning) return new SolidColorBrush(Colors.Orange);
                else if (Category == Category.Error) return new SolidColorBrush(Colors.DarkRed);
                else { return new SolidColorBrush(Colors.DeepSkyBlue); }
            }
        }

        public LogEntry(Category category, string message)
        {
            Category = category;
            Message = message;
        }

        public string ExportToString()
        {
            return Category + ": " + Message + "\n"; ;
        }
    }

    public enum LogFilter
    {
        All,
        Information,
        Warning,
        Error
    }

    public enum LogSortMode
    {
        FirstToLast,
        LastToFirst
    }

    public class UILogger : INotifyPropertyChanged, IDisposable
    {
        protected readonly uint MaxNumEntries = 100u;
        public event PropertyChangedEventHandler PropertyChanged;

        public ObservableCollection<LogEntry> LogEntries { get; private set; } = new ObservableCollection<LogEntry>();
        public LogEntry LatestLogEntry { get { return LogEntries.Count > 0 ? SortMode == LogSortMode.FirstToLast ? LogEntries.Last() : LogEntries.First() : null; } }

        private LogSortMode _sortMode = LogSortMode.FirstToLast;
        public LogSortMode SortMode
        {
            get { return _sortMode; }
            set { if (_sortMode != value) { _sortMode = value; Reverse(); } }
        }

        private LogFilter _filterMode = LogFilter.All;
        public LogFilter FilterMode
        {
            get { return _filterMode; }
            set
            {
                if (_filterMode != value)
                {
                    _filterMode = value; CollectionViewSource.GetDefaultView(LogEntries).Refresh();
                }
            }
        }

        public UILogger()
        {
            (CollectionViewSource.GetDefaultView(LogEntries) as CollectionView).Filter = ItemFilter;

            //Log(Category.Information, "This is information!");
            //Log(Category.Warning, "This is warning!");
            //Log(Category.Error, "This is error!");

            LogEntries.CollectionChanged += Entries_CollectionChanged;
        }

        private void Entries_CollectionChanged(object sender, System.Collections.Specialized.NotifyCollectionChangedEventArgs e)
        {
            OnPropertyChanged("LatestLogEntry");
        }

        public void Log(Category category, string message)
        {
            if (SortMode == LogSortMode.FirstToLast)
            {
                if (LogEntries.Count == MaxNumEntries)
                {
                    LogEntries.Remove(LogEntries.First());
                }
                LogEntries.Add(new LogEntry(category, message));
            }
            else
            {
                if (LogEntries.Count == MaxNumEntries)
                {
                    LogEntries.Remove(LogEntries.Last());
                }
                LogEntries.Insert(0, new LogEntry(category, message));
            }
        }

        private bool ItemFilter(object item)
        {
            var logEntry = item as LogEntry;
            if (FilterMode == LogFilter.All)
                return true;
            else if (logEntry.Category.ToString() == FilterMode.ToString())
                return true;

            return false;
        }

        public void Clear()
        {
            LogEntries.Clear();
        }

        public void Reverse()
        {
            LogEntries.CollectionChanged -= Entries_CollectionChanged;

            LogEntries = new ObservableCollection<LogEntry>(LogEntries.Reverse());
            LogEntries.CollectionChanged += Entries_CollectionChanged;

            (CollectionViewSource.GetDefaultView(LogEntries) as CollectionView).Filter = ItemFilter;

            OnPropertyChanged("LogEntries");
        }

        protected void OnPropertyChanged([CallerMemberName] string name = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        }

        public string ExportEntriesToString()
        {
            string logExport = string.Empty;
            foreach (var entry in LogEntries)
            {
                logExport += entry.ExportToString() + "\n";
            }

            return logExport;
        }

        public void ExportEntriesToFile(string logExportPath)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(logExportPath));

            var logString = string.Empty;
            logString += "SortMode: " + SortMode.ToString() + "\n";
            logString += "FilterMode: " + FilterMode.ToString() + "\n\n";

            logString += ExportEntriesToString();

            System.IO.File.WriteAllText(logExportPath, logString);
        }

        public void Dispose()
        {
            LogEntries.CollectionChanged -= Entries_CollectionChanged;
        }
    }
}
