// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "TarinoiCommandlet.h"

#include "Containers/Ticker.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "Misc/FileHelper.h"
#include "Sync/TarinoiCredentials.h"
#include "Tarinoi.h"
#include "TarinoiEditorActions.h"
#include "TarinoiSettings.h"

UTarinoiCommandlet::UTarinoiCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UTarinoiCommandlet::Main(const FString& Params)
{
	FString ApiPath;
	if (FParse::Value(*Params, TEXT("ApiPath="), ApiPath))
	{
		UTarinoiSettings* Settings = GetMutableDefault<UTarinoiSettings>();
		Settings->ApiPath = ApiPath;
		Settings->TryUpdateDefaultConfigFile();
		UE_LOG(LogTarinoi, Display, TEXT("Tarinoi: project set to '%s'."), *Settings->GetProjectId());
	}

	FString TokenFile;
	if (FParse::Value(*Params, TEXT("TokenFile="), TokenFile))
	{
		FString Token;
		if (!FFileHelper::LoadFileToString(Token, *TokenFile) || Token.TrimStartAndEnd().IsEmpty())
		{
			UE_LOG(LogTarinoi, Error, TEXT("Tarinoi: could not read a token from '%s'."), *TokenFile);
			return 1;
		}
		FTarinoiCredentials::Write(FTarinoiCredentials::ApiKeyName, Token.TrimStartAndEnd());
		UE_LOG(LogTarinoi, Display, TEXT("Tarinoi: API token saved."));
	}

	if (FParse::Param(*Params, TEXT("Sync")))
	{
		bool bDone = false;
		bool bOk = false;
		TarinoiEditorActions::Sync([&bDone, &bOk](const FTarinoiSyncResult& Result)
		{
			bOk = Result.bSuccess;
			bDone = true;
		});

		// There is no engine loop here, so pump the HTTP module and tickers by hand.
		const double Deadline = FPlatformTime::Seconds() + 600.0;
		while (!bDone && TarinoiEditorActions::IsSyncing() && FPlatformTime::Seconds() < Deadline)
		{
			FHttpModule::Get().GetHttpManager().Tick(0.05f);
			FTSTicker::GetCoreTicker().Tick(0.05f);
			FPlatformProcess::Sleep(0.05f);
		}

		if (!bOk)
		{
			UE_LOG(LogTarinoi, Error, TEXT("Tarinoi: sync did not complete."));
			return 1;
		}
	}

	if (FParse::Param(*Params, TEXT("Generate")) && !TarinoiEditorActions::RegenerateBindings())
	{
		return 1;
	}

	if (FParse::Param(*Params, TEXT("Check")) && TarinoiEditorActions::CheckBindings() > 0)
	{
		return 1;
	}

	if (FParse::Param(*Params, TEXT("Export")) && !TarinoiEditorActions::ExportSnapshot())
	{
		return 1;
	}

	return 0;
}
