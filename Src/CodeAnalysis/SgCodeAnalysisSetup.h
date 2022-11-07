// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <CodeAnalysis/Warnings.h>

/*
Internal. Temporary
*/
#define SG_DISABLE_CA_WARNINGS \
__pragma(warning(push)) \
//__pragma(warning(disable : ALL_CODE_ANALYSIS_WARNINGS)) \
//__pragma(warning(default : 26451 26495)) //26451 Arithmetic overflow //6386 buffer overrun

#define SG_ENABLE_CA_WARNINGS \
__pragma(warning(pop)) 


/*
Third party
*/
#define SG_THIRDPARTY_BEGIN \
__pragma(warning(push)) \
__pragma(warning(disable : ALL_CODE_ANALYSIS_WARNINGS))


#define SG_THIRDPARTY_END \
__pragma(warning(pop))


/*
Disable specific warning
*/
#define SG_DISABLE_SPECIFIC_BEGIN(x) \
__pragma(warning(push)) \
__pragma(warning(disable : x))


#define SG_DISABLE_SPECIFIC_END \
__pragma(warning(pop))
