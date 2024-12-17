using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace System.Runtime.Versioning
{
    /// <summary>
    /// This is a stub implementation of the SupportedOSPlatform attribute, 
    /// which exists in later .NET versions but not in .NET 4
    /// It exists for us to avoid having to #ifdef the attribute usage 
    /// in code that needs to compile on multiple .NET versions
    /// </summary>
    sealed public class SupportedOSPlatformAttribute : Attribute
    {
        public SupportedOSPlatformAttribute(string platformName)
        {
        }
    }
}
