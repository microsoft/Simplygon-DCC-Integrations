// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef __CRITICALSECTION_H__
#define __CRITICALSECTION_H__

#include <Windows.h>

#pragma warning( push )
#pragma warning( disable : 26135 )
class CriticalSection
{
	private:
	CRITICAL_SECTION cs;

	public:
	CriticalSection() { ::InitializeCriticalSection( &cs ); }
	~CriticalSection() { ::DeleteCriticalSection( &cs ); }

	void Enter() { ::EnterCriticalSection( &cs ); }
	void Leave() { ::LeaveCriticalSection( &cs ); }
};
#pragma warning( pop )

#endif