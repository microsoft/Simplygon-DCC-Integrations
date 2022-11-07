// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef __HELPERFUNCTIONS_H__
#define __HELPERFUNCTIONS_H__

#include <WTypes.h>
#include <string>
#include <vector>

typedef unsigned int uint;

enum ColorSpaceType
{
	NotDefined = 0,
	Linear = 1,
	SRGB = 2
};

std::basic_string<TCHAR> ToLower( std::basic_string<TCHAR> tString );
std::string ToUpper( std::string string );

bool CompareStrings( std::basic_string<TCHAR> tString1, std::basic_string<TCHAR> tString2 );

bool IsSubstringPartOfString( std::basic_string<TCHAR> tSourceString, std::basic_string<TCHAR> tPartString );
std::basic_string<TCHAR> TrimSpaces( std::basic_string<TCHAR> tSourceString );
int GetSettingsStringIndex( std::vector<std::basic_string<TCHAR>>& tSettingsStrings, const TCHAR* tName, std::basic_string<TCHAR>* tResult );

std::basic_string<TCHAR> CreateGuid();
std::basic_string<TCHAR> EmptyGuid();

bool GuidCompare( std::string sString1, std::string sString2 );

std::basic_string<TCHAR> RemoveInvalidCharacters( const std::basic_string<TCHAR>& tSourceString );
void RemoveInvalidCharacters( std::basic_string<TCHAR>& tSourceString );

void ReplaceInvalidCharacters( std::basic_string<TCHAR>& tSourceString, TCHAR tNewChar );
bool CharacterFilter( TCHAR tChar );

template <typename T, typename Y> void SetArray( T* targetArray, Y* values )
{
	for( uint i = 0; i < targetArray->num_dims; ++i )
	{
		targetArray->V[ i ] = values[ i ];
	}
}

template <typename T, typename Y> void SetArray( T* targetArray, ... )
{
	va_list dynamicArgumentList;
	va_start( dynamicArgumentList, targetArray );

	for( uint i = 0; i < targetArray->num_dims; ++i )
	{
		targetArray->V[ i ] = va_arg( dynamicArgumentList, Y );
	}

	va_end( dynamicArgumentList );
}

template <typename T> static void SetArray( T* targetArray, uint numElements, T value )
{
	for( uint e = 0; e < numElements; ++e )
	{
		targetArray[ e ] = value;
	}
}

template <typename T, typename Y> void FillArray( T* targetArray, Y value )
{
	for( uint i = 0; i < targetArray->num_dims; ++i )
	{
		targetArray->V[ i ] = value;
	}
}

template <typename T, typename Y> void SetTuple( uint index, T* data, std::vector<Y>& list, uint indexSpan )
{
	int startPosition = index * indexSpan;

	for( uint element = 0; element < indexSpan; ++element )
	{
		list[ startPosition + element ] = data[ element ];
	}
}

template <typename T> void ClearArray( T* targetArray, T value, uint length )
{
	for( uint element = 0; element < length; ++element )
	{
		targetArray[ element ] = value;
	}
}


std::basic_string<wchar_t> AppendInt( const std::basic_string<wchar_t>& str, int value );
std::basic_string<char> AppendInt( const std::basic_string<char>& str, int value );

#endif