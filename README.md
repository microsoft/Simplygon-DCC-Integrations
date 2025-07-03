# Simplygon - DCC integrations

This project include source code for the Simplygon integrations for Autodesk 3ds Max and Maya.  
To be able to use these integrations you need to have Simplygon installed.  
You can [download Simplygon here](https://simplygon.com/Downloads).  
You can find more info about these integrations in the [Simplygon documentation](https://documentation.simplygon.com/).

## How to build

You can only build the integrations using Windows with the following Visual studio versions.

| DCC tool      | Build tool               |
| ------------- |---------------------------|
| 3ds Max 2017  | Visual studio 2015 (v140) |
| 3ds Max 2018  | Visual studio 2015 (v140) |
| 3ds Max 2019  | Visual studio 2015 (v140) |
| 3ds Max 2020  | Visual studio 2017 (v141) |
| 3ds Max 2021  | Visual studio 2017 (v141) |
| 3ds Max 2022  | Visual studio 2017 (v141) |
| 3ds Max 2023  | Visual studio 2017 (v141) |
| 3ds Max 2024  | Visual studio 2019 (v142) |
| Maya 2017     | Visual studio 2015 (v140) |
| Maya 2018     | Visual studio 2015 (v140) |
| Maya 2019     | Visual studio 2015 (v140) |
| Maya 2020     | Visual studio 2017 (v141) |
| Maya 2022     | Visual studio 2019 (v142) |
| Maya 2023     | Visual studio 2019 (v142) |
| Maya 2024     | Visual studio 2019 (v142) |
| Maya 2025     | Visual studio 2022 (v143) |

All Visual studio projects are available in a Visual studio solution for each DCC tool.  
[Max.sln](Src/Max.sln)  
[Maya.sln](Src/Maya.sln)

### SimplygonMax <_Version_>

This is the main plugin and acts as a bridge between 3ds Max and Simplygon.  
This is a C++ project and assumes 3ds Max and [3ds Max SDK](https://www.autodesk.com/developer-network/platform-technologies/3ds-max) are installed in _C:\Program Files\Autodesk_.

### SimplygonMax <_Version_> UI

This is the UI plugin and enables the user to create Simplygon pipelines which then can be sent to the main plugin for processing by Simplygon.  
This is a C#/WPF project and the [WPF-DarkScheme](https://github.com/ADN-DevTech/Maya-Net-Wpf-DarkScheme) can be used to skin the UI.

### SimplygonMax <_Version_> UIBootstrap

This is a C# bootstrap wrapper for the UI plugin so the UI plugin can be package into a single dll file.

### SimplygonMaya <_Version_>

This is the main plugin and acts as a bridge between Maya and Simplygon.  
This is a C++ project and assumes Maya is installed in _C:\Program Files\Autodesk_.

### SimplygonMaya <_Version_> UI

This is the UI plugin and enables the user to create Simplygon pipelines which then can be sent to the main plugin for processing by Simplygon.  
This is a C#/WPF project and the [WPF-DarkScheme](https://github.com/ADN-DevTech/Maya-Net-Wpf-DarkScheme) can be used to skin the UI.

## Support

Please use the [Simplygon zendesk portal](https://simplygon.zendesk.com/hc/en-us/requests/new) when you want to submit a support request.  


## Data Collection

The software may collect information about you and your use of the software and send it to Microsoft. Microsoft may use this information to provide services and improve our products and services. You may [turn off the telemetry as described in the repository](telemetry.md). There are also some features in the software that may enable you and Microsoft to collect data from users of your applications. If you use these features, you must comply with applicable law, including providing appropriate notices to users of your applications together with a copy of Microsoft’s privacy statement. Our privacy statement is located at https://go.microsoft.com/fwlink/?LinkID=824704. You can learn more about data collection and use in the help documentation and our privacy statement. Your use of the software operates as your consent to these practices.

## Trademarks

This project may contain trademarks or logos for projects, products, or services. Authorized use of Microsoft trademarks or logos is subject to and must follow [Microsoft’s Trademark & Brand Guidelines](https://www.microsoft.com/en-us/legal/intellectualproperty/trademarks/usage/general). Use of Microsoft trademarks or logos in modified versions of this project must not cause confusion or imply Microsoft sponsorship. Any use of third-party trademarks or logos are subject to those third-party’s policies.