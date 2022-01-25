// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"
#include "Common.h"

std::vector<std::basic_string<TCHAR>> SimplygonProcessAdditionalSearchPaths;

using namespace std;

Matrix3 GetConversionMatrix()
{
	const Point3 row0( 1, 0, 0 );
	const Point3 row1( 0, 1, 0 );
	const Point3 row2( 0, 0, 1 );
	const Point3 row3( 0, 0, 0 );

	Matrix3 m( row0, row1, row2, row3 );

	return m;
}

// max matrix to sg matrix
Matrix3 ConvertMatrixII( Matrix3 maxMatrix )
{
	maxMatrix.Invert();

	const Point4 ir0 = maxMatrix.GetColumn( 0 );
	const Point4 ir1 = maxMatrix.GetColumn( 1 );
	const Point4 ir2 = maxMatrix.GetColumn( 2 );

	Matrix3 InvertedTransposedMatrix;
	InvertedTransposedMatrix.SetRow( 0, Point3( ir0.x, ir0.y, ir0.z ) );
	InvertedTransposedMatrix.SetRow( 1, Point3( ir1.x, ir1.y, ir1.z ) );
	InvertedTransposedMatrix.SetRow( 2, Point3( ir2.x, ir2.y, ir2.z ) );
	InvertedTransposedMatrix.SetRow( 3, Point3( 0, 0, 0 ) );

	return InvertedTransposedMatrix;
}

// max matrix to sg matrix
Matrix3 GetIdentityMatrix()
{
	Matrix3 identityMatrix;
	identityMatrix.IdentityMatrix();

	return identityMatrix;
}

int StringToint( std::string str )
{
	int v = 0;
	try
	{
		v = atoi( str.c_str() );
	}
	catch( std::exception ex )
	{
		throw std::exception( "StringToInt: failed when trying to convert to int." );
	}

	return v;
}

double StringTodouble( std::string str )
{
	double v = 0;
	try
	{
		v = atof( str.c_str() );
	}
	catch( std::exception ex )
	{
		throw std::exception( "StringToInt: failed when trying to convert to double." );
	}

	return v;
}

float StringTofloat( std::string str )
{
	return (float)StringTodouble( str );
}

std::string StringTostring( std::string str )
{
	return str;
}

bool StringTobool( std::string str )
{
	if( str == "1" )
		return true;
	else if( str == "true" )
		return true;
	else if( str == "True" )
		return true;
	else if( str == "TRUE" )
		return true;

	return false;
}

bool StringToNULL( std::basic_string<TCHAR> str )
{
	if( str == _T("null") )
		return true;
	else if( str == _T("Null") )
		return true;
	else if( str == _T("NULL") )
		return true;

	return false;
}

float _log2( float n )
{
	return log( n ) / log( 2.f );
}


