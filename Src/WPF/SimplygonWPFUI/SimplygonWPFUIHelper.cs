// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using System;
using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.IO;

namespace SimplygonUI
{
    [ValueConversion(typeof(bool), typeof(Visibility))]
    public class InvertableBooleanToVisibilityConverter : IValueConverter
    {
        enum InputParameterMode
        {
            NoChange, Inverted
        }

        public object Convert(object value, Type targetType,
                              object parameter, CultureInfo culture)
        {
            var bInputValue = (bool)value;
            var inputParameterType = (InputParameterMode)Enum.Parse(typeof(InputParameterMode), (string)parameter);

            if (inputParameterType == InputParameterMode.Inverted)
                return !bInputValue ? Visibility.Visible : Visibility.Collapsed;

            return bInputValue ? Visibility.Visible : Visibility.Collapsed;
        }

        public object ConvertBack(object value, Type targetType,
            object parameter, CultureInfo culture)
        {
            return null;
        }
    }

    [ValueConversion(typeof(double), typeof(double))]
    public class AdjustValue : IValueConverter
    {
        public object Convert(object value, Type targetType,
                              object parameter, CultureInfo culture)
        {
            var fValue = System.Convert.ToDouble(value, System.Globalization.CultureInfo.InvariantCulture);
            var fAdjustValue = System.Convert.ToDouble(parameter, System.Globalization.CultureInfo.InvariantCulture);

            return fValue + fAdjustValue;
        }

        public object ConvertBack(object value, Type targetType,
            object parameter, CultureInfo culture)
        {
            return null;
        }
    }

    [ValueConversion(typeof(string), typeof(string))]
    public class AddClampsToString : IValueConverter
    {
        public object Convert(object value, Type targetType,
                              object parameter, CultureInfo culture)
        {
            return "[" + value + "]";
        }

        public object ConvertBack(object value, Type targetType,
            object parameter, CultureInfo culture)
        {
            return null;
        }
    }

    public class DatabindingDebugConverter : IValueConverter
    {
        public object Convert(object value, Type targetType,
            object parameter, CultureInfo culture)
        {
            return value;
        }

        public object ConvertBack(object value, Type targetType,
            object parameter, CultureInfo culture)
        {
            return value;
        }
    }

    public class ProxyBinding : Freezable
    {
        protected override Freezable CreateInstanceCore()
        {
            return new ProxyBinding();
        }

        public object Data
        {
            get { return (object)GetValue(DataProperty); }
            set { SetValue(DataProperty, value); }
        }

        public static readonly DependencyProperty DataProperty = DependencyProperty.Register("Data", typeof(object), typeof(ProxyBinding), new PropertyMetadata(null));
    }

    public class IntMinMaxValue : DependencyObject
    {
        public static readonly DependencyProperty MinValueProperty = DependencyProperty.Register(nameof(Min), typeof(int), typeof(IntMinMaxValue));
        public static readonly DependencyProperty MaxValueProperty = DependencyProperty.Register(nameof(Max), typeof(int), typeof(IntMinMaxValue));

        public int Min
        {
            get { return (int)GetValue(MinValueProperty); }
            set { SetValue(MinValueProperty, value); }
        }
        public int Max
        {
            get { return (int)GetValue(MaxValueProperty); }
            set { SetValue(MaxValueProperty, value); }
        }
    }

    public class FloatMinMaxValue : DependencyObject
    {
        public static readonly DependencyProperty MinValueProperty = DependencyProperty.Register(nameof(Min), typeof(float), typeof(FloatMinMaxValue));
        public static readonly DependencyProperty MaxValueProperty = DependencyProperty.Register(nameof(Max), typeof(float), typeof(FloatMinMaxValue));

        public float Min
        {
            get { return (float)GetValue(MinValueProperty); }
            set { SetValue(MinValueProperty, value); }
        }
        public float Max
        {
            get { return (float)GetValue(MaxValueProperty); }
            set { SetValue(MaxValueProperty, value); }
        }
    }

    public class IntRangeValidationRule : ValidationRule
    {
        public IntMinMaxValue MinMax { get; set; }

        public override ValidationResult Validate(
          object value, System.Globalization.CultureInfo cultureInfo)
        {
            int intValue;

            if (!Int32.TryParse(value.ToString(), NumberStyles.Any, cultureInfo, out intValue)) return new ValidationResult(false, "Not valid");
            if (intValue < MinMax.Min) return new ValidationResult(false, "Less than min");
            if (intValue > MinMax.Max) return new ValidationResult(false, "Greater than max");
            return ValidationResult.ValidResult;
        }
    }

    public class FloatRangeValidationRule : ValidationRule
    {
        public FloatMinMaxValue MinMax { get; set; }

        public override ValidationResult Validate(
          object value, System.Globalization.CultureInfo cultureInfo)
        {
            float floatValue;

            if (!float.TryParse(value.ToString(), NumberStyles.Any, cultureInfo, out floatValue)) return new ValidationResult(false, "Not valid");
            if (floatValue < MinMax.Min) return new ValidationResult(false, "Less than min");
            if (floatValue > MinMax.Max) return new ValidationResult(false, "Greater than max");
            return ValidationResult.ValidResult;
        }
    }

    public class SimplygonTreeViewDataTemplateSelector : DataTemplateSelector
    {
        public override DataTemplate SelectTemplate(object item, DependencyObject container)
        {
            FrameworkElement element = container as FrameworkElement;

            if (element != null && item != null)
            {
                if (item is SimplygonSettings)
                {
                    return element.FindResource("SimplygonSettingsTemplate") as DataTemplate;
                }
                else if (item is SimplygonSettingsProperty)
                {
                    SimplygonSettingsProperty settingsPropertyItem = item as SimplygonSettingsProperty;
                    if (settingsPropertyItem.Type == "bool")
                    {
                        return element.FindResource("SimplygonSettingsPropertyBoolTemplate") as DataTemplate;
                    }
                    else if (settingsPropertyItem.Type == "int" || settingsPropertyItem.Type == "uint" || settingsPropertyItem.Type == "rid")
                    {
                        if (settingsPropertyItem.HasDependencyObject)
                        {
                            return element.FindResource("SimplygonSettingsPropertyIntWithDependencyObjectTemplate") as DataTemplate;
                        }
                        else
                        {
                            return element.FindResource("SimplygonSettingsPropertyIntTemplate") as DataTemplate;
                        }
                    }
                    else if (settingsPropertyItem.Type == "real")
                    {
                        if (settingsPropertyItem.HasDependencyObject)
                        {
                            return element.FindResource("SimplygonSettingsPropertyFloatWithDependencyObjectTemplate") as DataTemplate;
                        }
                        else
                        {
                            return element.FindResource("SimplygonSettingsPropertyFloatTemplate") as DataTemplate;
                        }
                    }
                    else if (settingsPropertyItem.Type == "enum")
                    {
                        return element.FindResource("SimplygonSettingsPropertyEnumTemplate") as DataTemplate;
                    }
                    else if (settingsPropertyItem.Type == "string")
                    {
                        if (settingsPropertyItem.TypeOverride == "SelectionSet")
                        {
                            return element.FindResource("SimplygonSettingsPropertySelectionSetTemplate") as DataTemplate;
                        }
                        else if (settingsPropertyItem.TypeOverride == "FolderBrowser")
                        {
                            return element.FindResource("SimplygonSettingsPropertyFolderBrowserTemplate") as DataTemplate;
                        }
                        return element.FindResource("SimplygonSettingsPropertyStringTemplate") as DataTemplate;
                    }
                    return element.FindResource("SimplygonSettingsPropertyTemplate") as DataTemplate;
                }
                else if (item is SimplygonTreeViewItem)
                {
                    return element.FindResource("SimplygonTreeViewItemTemplate") as DataTemplate;
                }

            }

            return null;
        }
    }

    public class SimplygonIntegrationSettingsTreeViewDataTemplateSelector : DataTemplateSelector
    {
        public override DataTemplate SelectTemplate(object item, DependencyObject container)
        {
            FrameworkElement element = container as FrameworkElement;

            if (element != null && item != null)
            {
                if (item is SimplygonSettingsProperty)
                {
                    SimplygonSettingsProperty settingsPropertyItem = item as SimplygonSettingsProperty;
                    if (settingsPropertyItem.Type == "bool")
                    {
                        return element.FindResource("SimplygonSettingsPropertyBoolTemplate") as DataTemplate;
                    }
                    else if (settingsPropertyItem.Type == "int" || settingsPropertyItem.Type == "uint" || settingsPropertyItem.Type == "rid")
                    {
                        if (settingsPropertyItem.HasDependencyObject)
                        {
                            return element.FindResource("SimplygonSettingsPropertyIntWithDependencyObjectTemplate") as DataTemplate;
                        }
                        else
                        {
                            return element.FindResource("SimplygonSettingsPropertyIntTemplate") as DataTemplate;
                        }
                    }
                    else if (settingsPropertyItem.Type == "real")
                    {
                        if (settingsPropertyItem.HasDependencyObject)
                        {
                            return element.FindResource("SimplygonSettingsPropertyFloatWithDependencyObjectTemplate") as DataTemplate;
                        }
                        else
                        {
                            return element.FindResource("SimplygonSettingsPropertyFloatTemplate") as DataTemplate;
                        }
                    }
                    else if (settingsPropertyItem.Type == "enum")
                    {
                        return element.FindResource("SimplygonSettingsPropertyEnumTemplate") as DataTemplate;
                    }
                    else if (settingsPropertyItem.Type == "string")
                    {
                        if (settingsPropertyItem.TypeOverride == "SelectionSet")
                        {
                            return element.FindResource("SimplygonSettingsPropertySelectionSetTemplate") as DataTemplate;
                        }
                        else if (settingsPropertyItem.TypeOverride == "FolderBrowser")
                        {
                            return element.FindResource("SimplygonSettingsPropertyFolderBrowserTemplate") as DataTemplate;
                        }
                        return element.FindResource("SimplygonSettingsPropertyStringTemplate") as DataTemplate;
                    }
                    return element.FindResource("SimplygonSettingsPropertyTemplate") as DataTemplate;
                }
                else if (item is SimplygonTreeViewItem)
                {
                    return element.FindResource("SimplygonTreeViewItemTemplate") as DataTemplate;
                }

            }

            return null;
        }
    }

    public class SimplygonUtils
    {
        static public string GetSimplygonPipelineDirectory()
        {
            string simplygon9SharedDirectory = Environment.GetEnvironmentVariable("SIMPLYGON_9_SHARED");
            if (string.IsNullOrWhiteSpace(simplygon9SharedDirectory))
            {
                simplygon9SharedDirectory = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "Simplygon", "9");
            }

            simplygon9SharedDirectory = Environment.ExpandEnvironmentVariables(simplygon9SharedDirectory);
            string pipelineDirectory = System.IO.Path.Combine(simplygon9SharedDirectory, "Pipelines");
            if (!System.IO.Directory.Exists(pipelineDirectory))
            {
                System.IO.Directory.CreateDirectory(pipelineDirectory);
            }

            return pipelineDirectory;
        }

        static public string GetUniqueFilePath(string filePath)
        {
            if( !File.Exists(filePath) )
            {
                return filePath;
            }

            string directory = Path.GetDirectoryName(filePath);
            string fileNameWithoutExt = Path.GetFileNameWithoutExtension(filePath);
            string ext = Path.GetExtension(filePath);

            for (int i = 1; i < 1000; ++i)
            {
                string newFilePath = Path.Combine(directory, fileNameWithoutExt + $"({i}){Path.GetExtension(filePath)}");

                if (!File.Exists(newFilePath))
                {
                    return newFilePath;
                }
            }

            return filePath;
        }
    }
}
