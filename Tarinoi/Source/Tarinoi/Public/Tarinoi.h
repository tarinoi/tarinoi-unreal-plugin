// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/** Everything Tarinoi writes to the log goes through this category. */
TARINOI_API DECLARE_LOG_CATEGORY_EXTERN(LogTarinoi, Log, All);

class FTarinoiModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
