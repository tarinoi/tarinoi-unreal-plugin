// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "TarinoiSettings.generated.h"

class UTarinoiDocumentStore;

/** How much Tarinoi writes to the log. */
UENUM()
enum class ETarinoiLogLevel : uint8
{
	Verbose,
	Log,
	Warning,
	Error,
	Off,
};

/**
 * Project-wide Tarinoi configuration, under Project Settings > Plugins > Tarinoi and stored in
 * Config/DefaultTarinoi.ini, a file of its own so a project can choose whether to commit it.
 *
 * The API token is deliberately not here. It lives outside the project directory (see
 * FTarinoiCredentials), so it can never be committed or packaged into a build.
 */
UCLASS(Config = Tarinoi, DefaultConfig, meta = (DisplayName = "Tarinoi"))
class TARINOI_API UTarinoiSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTarinoiSettings();

	/** Your project's documents endpoint, ending in /documents. Shown next to the token under Integrations in Tarinoi. */
	UPROPERTY(Config, EditAnywhere, Category = "API", meta = (DisplayName = "API Path"))
	FString ApiPath;

	/** Re-sync periodically during Play In Editor, so authored changes show up without restarting. */
	UPROPERTY(Config, EditAnywhere, Category = "API")
	bool bPollEnabled = false;

	/** Seconds between polls. */
	UPROPERTY(Config, EditAnywhere, Category = "API", meta = (ClampMin = "1", EditCondition = "bPollEnabled"))
	int32 PollInterval = 10;

	/** The game module generated bindings are written into. Empty means the project's primary module. */
	UPROPERTY(Config, EditAnywhere, Category = "Codegen")
	FString CodegenModule;

	/** Folder for generated binding classes, relative to the module's source folder. Regenerated on every run. */
	UPROPERTY(Config, EditAnywhere, Category = "Codegen")
	FString CodegenOutputPath = TEXT("Tarinoi/Generated");

	/**
	 * Folder for your own binding implementations, relative to the module's source folder. The core
	 * functions reference implementation is written here once, for you to edit, and never overwritten.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Codegen")
	FString CodegenImplPath = TEXT("Tarinoi");

	/** Regenerate bindings after every successful sync from the editor. */
	UPROPERTY(Config, EditAnywhere, Category = "Codegen")
	bool bCodegenOnSync = false;

	/** Show only committed content, hiding uncommitted edits: what a player would see. */
	UPROPERTY(Config, EditAnywhere, Category = "Behaviour")
	bool bCommittedOnly = false;

	UPROPERTY(Config, EditAnywhere, Category = "Behaviour")
	ETarinoiLogLevel LogLevel = ETarinoiLogLevel::Log;

	/** Play from the snapshot bundled in Content/Tarinoi and never contact the API. Turn on for shipping builds. */
	UPROPERTY(Config, EditAnywhere, Category = "Behaviour")
	bool bOfflineMode = false;

	/** Where dialogue content is read from. Leave empty for the built-in SQLite store. */
	UPROPERTY(Config, EditAnywhere, Category = "Behaviour", meta = (AllowAbstract = "false"))
	TSoftClassPtr<UTarinoiDocumentStore> DocumentStoreClass;

	static const UTarinoiSettings* Get() { return GetDefault<UTarinoiSettings>(); }

	/** The project id derived from ApiPath, or "" when it isn't set or has the wrong shape. */
	FString GetProjectId() const { return ProjectIdFromApiPath(ApiPath); }

	/**
	 * Extracts the project id from a documents endpoint: the last path segment before a trailing
	 * /documents. Tolerates a trailing slash and a missing /documents.
	 */
	static FString ProjectIdFromApiPath(const FString& Path);

	/** Applies LogLevel to the LogTarinoi category. */
	void ApplyLogLevel() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
};
