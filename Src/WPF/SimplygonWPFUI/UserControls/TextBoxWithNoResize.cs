// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using System.Windows;
using System.Windows.Controls;

namespace SimplygonUI.UserControls
{
    public class TextBoxWithNoResizeBase : TextBox
    {
        protected override Size MeasureOverride(Size constraint)
        {
            return new Size(0, constraint.Height);
        }
    }
}
