// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FTarinoiEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FTarinoiEditorModule, TarinoiEditor)
