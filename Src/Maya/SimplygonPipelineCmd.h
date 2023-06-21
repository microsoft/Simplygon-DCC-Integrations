// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <maya/MPxCommand.h>
#include <tchar.h>
#include "maya/MStatus.h"
#include "maya/MSyntax.h"
#include "maya/MArgList.h"

class SimplygonPipelineCmd : public MPxCommand
{
	public:
	SimplygonPipelineCmd();
	virtual ~SimplygonPipelineCmd();

	void Callback( std::basic_string<TCHAR> tId, bool error, std::basic_string<TCHAR> tMessage, int progress );

	MStatus doIt( const MArgList& mArgList ) override;
	MStatus redoIt() override;
	MStatus undoIt() override;
	bool isUndoable() const override;

	void LogWarningToWindow( std::basic_string<TCHAR> tMessage );
	void LogErrorToWindow( std::basic_string<TCHAR> tMessage );

	static void* creator();
	static MSyntax createSyntax();
	MStatus ParseArguments( const MArgList& mArgList );

	private:
	CRITICAL_SECTION cs;
};