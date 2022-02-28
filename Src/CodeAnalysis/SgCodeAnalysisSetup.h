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

/*
Use the set/reset to still warning about warning but not error out due.
i.e type casting, deprecations
*/
#define SG_WARNING_LEVEL_SET( warningNo ,level) \
__pragma(warning( level : warningNo ))

#define SG_WARNING_LEVEL_RESET( warningNo ) \
__pragma(warning( default : warningNo ))
