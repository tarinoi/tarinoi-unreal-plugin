// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "TarinoiCommandlet.generated.h"

/**
 * Tarinoi's editor actions without the editor, for build machines and scripts:
 *
 *   UnrealEditor-Cmd MyGame.uproject -run=Tarinoi [-ApiPath=URL] [-TokenFile=PATH] [-Sync] [-Generate] [-Check] [-Export]
 *
 * -ApiPath sets the documents endpoint in the project settings. -TokenFile stores the token read
 * from a file (a token on the command line would show up in process lists). The actions run in
 * the order listed. Exits non-zero if any fails, or if -Check finds a breaking difference.
 */
UCLASS()
class UTarinoiCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UTarinoiCommandlet();

	virtual int32 Main(const FString& Params) override;
};
