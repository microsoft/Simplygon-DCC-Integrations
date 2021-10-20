// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <maya/MPxCommand.h>
#include <tchar.h>
#include "maya/MStatus.h"
#include "maya/MSyntax.h"
#include "maya/MArgList.h"

class SimplygonQueryCmd : public MPxCommand
{
	public:
	SimplygonQueryCmd();
	virtual ~SimplygonQueryCmd();

	MStatus redoIt() override { return MStatus::kSuccess; }
	MStatus undoIt() override { return MStatus::kSuccess; }
	bool isUndoable() const override { return false; }
	MStatus doIt( const MArgList& mArgList ) override;

	static void* creator();
	static MSyntax createSyntax();

	// TODO: Add them to a separate Utility class. Link it with Maya viewport
	// LOD Switch distance and pixel size calculator utility methods
	double GetLodSwitchDistance( double radius, int pixelsize );
	double GetPixelSize( double radius, double distance );
	double GetLodSwitchDistanceAtFOV( double fov );
	void SetScreenSize( int screenSize );
	int GetScreenSize();

	private:
	int ScreenSize;
};
