// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using System;

namespace SimplygonUI.MayaUI.Settings
{
    [System.Runtime.Serialization.DataContract]
    public class RunMode : SimplygonSettingsProperty
    {
        Simplygon.EPipelineRunMode value;
        [System.Runtime.Serialization.DataMember]
        public Simplygon.EPipelineRunMode Value { get { return value; } set { this.value = value; OnPropertyChanged(); } }

        [Newtonsoft.Json.JsonIgnore]
        public Array EnumValues { get { return Enum.GetValues(typeof(Simplygon.EPipelineRunMode)); } }

        public RunMode() : base("RunMode")
        {
            Name = "RunMode";
            Type = "enum";
            HelpText = "Set how the Simplygon pipeline will be executed.";
            Visible = true;

            Reset();
        }

        public override void Reset()
        {
            Value = Simplygon.EPipelineRunMode.RunInNewProcess;
        }
    }

    [System.Runtime.Serialization.DataContract]
    public class MeshNameFormat : SimplygonSettingsProperty
    {
        string value;
        [System.Runtime.Serialization.DataMember]
        public string Value { get { return value; } set { this.value = value; OnPropertyChanged(); } }

        public MeshNameFormat() : base("MeshNameFormat")
        {
            Name = "MeshNameFormat";
            Type = "string";
            HelpText = "Specifies the format string for generated meshes. {MeshName} and {LODIndex} are reserved keywords and will be replaced with the corresponding values during import. Use the InitialLODIndex slider to change the value of LODIndex. Ex: {MeshName}_LOD{LODIndex}";
            Visible = true;

            Reset();
        }

        public override void Reset()
        {
            Value = "{MeshName}_LOD{LODIndex}";
        }
    }

    [System.Runtime.Serialization.DataContract]
    public class TextureOutputDirectory : SimplygonSettingsProperty, SimplygonSettingsPropertyFolderBrowser
    {
        string value;
        [System.Runtime.Serialization.DataMember]
        public string Value { get { return value; } set { this.value = value; OnPropertyChanged(); } }

        public TextureOutputDirectory() : base("TextureOutputDirectory")
        {
            Name = "TextureOutputDirectory";
            Type = "string";
            HelpText = "If set: overrides the default texture output directory for baked textures.";
            Visible = true;
            TypeOverride = "FolderBrowser";

            Reset();
        }

        public override void Reset()
        {
            Value = "";
        }

        public void SetPath(string path)
        {
            Value = path;
        }

        public string GetPath()
        {
            return Value;
        }
    }

    [System.Runtime.Serialization.DataContract]
    public class InitialLodIndex : SimplygonSettingsProperty
    {
        int value;
        [System.Runtime.Serialization.DataMember]
        public int Value { get { return value; } set { this.value = value; OnPropertyChanged(); } }

        [System.Runtime.Serialization.DataMember]
        public float MinValue { get; set; }
        [System.Runtime.Serialization.DataMember]
        public float MaxValue { get; set; }
        [System.Runtime.Serialization.DataMember]
        public float TicksFrequencyValue { get; set; }

        public InitialLodIndex() : base("InitialLodIndex")
        {
            Name = "InitialLodIndex";
            Type = "int";
            HelpText = "Specifies the value of LODIndex when {LODIndex} is present in MeshNameFormat";
            Visible = true;
            MinValue = 0;
            MaxValue = 100;
            Value = 1;
            TicksFrequencyValue = 1;
        }

        public override void Reset()
        {
            Value = 1;
        }
    }

    [System.Runtime.Serialization.DataContract]
    public class SelectProcessedMeshes : SimplygonSettingsProperty
    {
        bool value;
        [System.Runtime.Serialization.DataMember]
        public bool Value { get { return value; } set { this.value = value; OnPropertyChanged(); } }

        public SelectProcessedMeshes() : base("SelectProcessedMeshes")
        {
            Name = "SelectProcessedMeshes";
            Type = "bool";
            HelpText = "Enabled: Simplygon will automatically select (optimized) meshes after the optimization has completed. Disabled: The original selection (that was made prior to the optimization) will be preserved.";
            Visible = true;

            Reset();
        }

        public override void Reset()
        {
            Value = false;
        }
    }

    public class TangentCalculatorType : SimplygonSettingsProperty
    {
        Simplygon.ETangentSpaceMethod value;
        [System.Runtime.Serialization.DataMember]
        public Simplygon.ETangentSpaceMethod Value { get { return value; } set { this.value = value; OnPropertyChanged(); } }

        [Newtonsoft.Json.JsonIgnore]
        public Array EnumValues { get { return Enum.GetValues(typeof(Simplygon.ETangentSpaceMethod)); } }

        public TangentCalculatorType() : base("TangentCalculatorType")
        {
            Name = "TangentCalculatorType";
            Type = "enum";
            HelpText = "Set how the tangent space is computed.";
            Visible = true;

            Reset();
        }

        public override void Reset()
        {
            Value = Simplygon.ETangentSpaceMethod.MikkTSpace;
        }
    }

    [System.Runtime.Serialization.DataContract]
    public class UseCurrentPoseAsBindPose : SimplygonSettingsProperty
    {
        bool value;
        [System.Runtime.Serialization.DataMember]
        public bool Value { get { return value; } set { this.value = value; OnPropertyChanged(); } }

        public UseCurrentPoseAsBindPose() : base("UseCurrentPoseAsBindPose")
        {
            Name = "UseCurrentPoseAsBindPose";
            Type = "bool";
            HelpText = "Enabled: Specifies that the skinning data will be extracted from the current pose rather than the bind pose. Please do not use this flag when in bind pose as synchronization issues might appear!";
#if SIMPLYGONMAYA2024UI || SIMPLYGONMAYA2025UI
            Visible = false;
#else
            Visible = true;
#endif

            Reset();
        }

        public override void Reset()
        {
#if SIMPLYGONMAYA2024UI || SIMPLYGONMAYA2025UI
            Value = true;
#else
            Value = false;
#endif
        }
    }

    
}
