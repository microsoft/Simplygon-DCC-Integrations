// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"

#include "maya/MArgDatabase.h"
#include "maya/MGlobal.h"

#include "DataCollection.h"
#include "SimplygonInit.h"

DataCollection::~DataCollection()
{
}

void DataCollection::SetSceneHandler( Scene* handler )
{
	this->sceneHandler = handler;
}

void DataCollection::SetMaterialHandler( MaterialHandler* handler )
{
	this->materialHandler = handler;
}

DataCollection::DataCollection()
{
	this->sceneHandler = nullptr;
	this->materialHandler = nullptr;
	this->SceneRadius = 0.0;
}

DataCollection* DataCollection::GetInstance()
{
	if( DataCollection::instance == nullptr )
	{
		DataCollection::instance = new DataCollection();
		return DataCollection::instance;
	}

	return DataCollection::instance;
}
