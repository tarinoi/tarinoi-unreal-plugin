// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "TarinoiEditorActions.h"

#include "Codegen/TarinoiBindingValidator.h"
#include "Codegen/TarinoiCodegen.h"
#include "Data/TarinoiDatabase.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "Interfaces/IProjectManager.h"
#include "Logging/MessageLog.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProjectDescriptor.h"
#include "Settings/ProjectPackagingSettings.h"
#include "Sync/TarinoiApiImporter.h"
#include "Sync/TarinoiCredentials.h"
#include "Sync/TarinoiSnapshot.h"
#include "Tarinoi.h"
#include "TarinoiSettings.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "TarinoiEditor"

namespace
{
	const FName MessageLogName(TEXT("Tarinoi"));

	TSharedPtr<FTarinoiApiImporter> ActiveImport;

	/**
	 * Adds one line to a section of Config/DefaultGame.ini, as text, unless it is there already.
	 * Saving a settings object instead would write out its whole section: a large diff for one entry.
	 */
	void AddGameIniLine(const FString& Section, const FString& Entry)
	{
		const FString Ini = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultGame.ini"));
		FString Text;
		FFileHelper::LoadFileToString(Text, *Ini);
		if (Text.Contains(Entry))
		{
			return;
		}

		const FString Header = TEXT("[") + Section + TEXT("]");
		const int32 At = Text.Find(Header, ESearchCase::IgnoreCase);
		if (At == INDEX_NONE)
		{
			Text += (Text.IsEmpty() || Text.EndsWith(TEXT("\n")) ? TEXT("") : TEXT("\n")) + FString(TEXT("\n")) + Header + TEXT("\n") + Entry + TEXT("\n");
		}
		else
		{
			const int32 LineEnd = Text.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At);
			Text.InsertAt(LineEnd == INDEX_NONE ? Text.Len() : LineEnd + 1, Entry + TEXT("\n"));
		}
		FFileHelper::SaveStringToFile(Text, *Ini, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	FString ProjectIdOrReport()
	{
		const FString ProjectId = UTarinoiSettings::Get()->GetProjectId();
		if (ProjectId.IsEmpty())
		{
			TarinoiEditorActions::Notify(TEXT("Set the API path in Project Settings > Plugins > Tarinoi first."), false);
		}
		return ProjectId;
	}
}

namespace TarinoiEditorActions
{
	void Notify(const FString& Message, bool bSuccess)
	{
		UE_LOG(LogTarinoi, Display, TEXT("%s"), *Message);

		FMessageLog Log(MessageLogName);
		if (bSuccess)
		{
			Log.Info(FText::FromString(Message));
		}
		else
		{
			Log.Error(FText::FromString(Message));
		}

		if (!IsRunningCommandlet() && FSlateApplication::IsInitialized())
		{
			FNotificationInfo Info(FText::FromString(Message));
			Info.ExpireDuration = bSuccess ? 5.0f : 10.0f;
			Info.bUseSuccessFailIcons = true;
			Info.Image = nullptr;
			if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
			{
				Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
			}
		}
	}

	bool IsSyncing()
	{
		return ActiveImport.IsValid();
	}

	void Sync(TFunction<void(const FTarinoiSyncResult&)> OnDone)
	{
		if (ActiveImport.IsValid())
		{
			Notify(TEXT("Tarinoi: a sync is already running."), false);
			return;
		}

		const UTarinoiSettings* Settings = UTarinoiSettings::Get();
		const FString ProjectId = ProjectIdOrReport();
		if (ProjectId.IsEmpty())
		{
			return;
		}

		const TSharedPtr<FTarinoiDatabase> Database = FTarinoiDatabase::Acquire(ProjectId);
		if (!Database.IsValid())
		{
			Notify(FString::Printf(TEXT("Tarinoi: could not open the local database for '%s'. See the log."), *ProjectId), false);
			return;
		}

		TSharedPtr<SNotificationItem> Progress;
		if (!IsRunningCommandlet() && FSlateApplication::IsInitialized())
		{
			FNotificationInfo Info(LOCTEXT("Syncing", "Tarinoi: syncing..."));
			Info.bFireAndForget = false;
			Progress = FSlateNotificationManager::Get().AddNotification(Info);
			if (Progress.IsValid())
			{
				Progress->SetCompletionState(SNotificationItem::CS_Pending);
			}
		}

		ActiveImport = MakeShared<FTarinoiApiImporter>();
		ActiveImport->Start(Settings->ApiPath, FTarinoiCredentials::Read(FTarinoiCredentials::ApiKeyName), Database,
			FTarinoiApiImporter::FOnProgress::CreateLambda([Progress](const FString& Message, float)
			{
				if (Progress.IsValid())
				{
					Progress->SetText(FText::FromString(TEXT("Tarinoi: ") + Message));
				}
			}),
			FTarinoiApiImporter::FOnComplete::CreateLambda([Progress, OnDone](const FTarinoiSyncResult& Result)
			{
				ActiveImport.Reset();
				if (Progress.IsValid())
				{
					Progress->ExpireAndFadeout();
				}

				if (Result.bSuccess)
				{
					Notify(TEXT("Tarinoi: sync complete: ") + Result.Stats.ToString(), true);
					for (const FString& Warning : Result.Stats.Warnings)
					{
						FMessageLog(MessageLogName).Warning(FText::FromString(Warning));
					}
					if (UTarinoiSettings::Get()->bCodegenOnSync)
					{
						RegenerateBindings();
					}
				}
				else
				{
					Notify(TEXT("Tarinoi: ") + Result.Error, false);
					FMessageLog(MessageLogName).Open(EMessageSeverity::Error);
				}

				if (OnDone)
				{
					OnDone(Result);
				}
			}));
	}

	FTarinoiCodegenTarget ResolveCodegenTarget()
	{
		FTarinoiCodegenTarget Target;
		const UTarinoiSettings* Settings = UTarinoiSettings::Get();

		Target.ModuleName = Settings->CodegenModule;
		if (Target.ModuleName.IsEmpty())
		{
			// The project's primary game module: the first runtime module the .uproject lists.
			if (const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject())
			{
				for (const FModuleDescriptor& Module : Project->Modules)
				{
					if (Module.Type == EHostType::Runtime)
					{
						Target.ModuleName = Module.Name.ToString();
						break;
					}
				}
			}
		}

		if (Target.ModuleName.IsEmpty())
		{
			return Target;
		}

		Target.ModuleDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::GameSourceDir(), Target.ModuleName));
		Target.OutputDir = FPaths::Combine(Target.ModuleDir, Settings->CodegenOutputPath);
		Target.ImplDir = FPaths::Combine(Target.ModuleDir, Settings->CodegenImplPath);
		Target.ApiMacro = Target.ModuleName.ToUpper() + TEXT("_API");
		return Target;
	}

	bool RegenerateBindings()
	{
		FMessageLog Log(MessageLogName);
		const FString ProjectId = ProjectIdOrReport();
		if (ProjectId.IsEmpty())
		{
			return false;
		}

		const FTarinoiCodegenTarget Target = ResolveCodegenTarget();
		if (!Target.IsValid() || !FPaths::DirectoryExists(Target.ModuleDir))
		{
			Notify(TEXT("Tarinoi: this project has no C++ module to generate bindings into. Add C++ to the project (Tools > New C++ Class), or set Codegen Module in the Tarinoi settings."), false);
			return false;
		}

		const TSharedPtr<FTarinoiDatabase> Database = FTarinoiDatabase::Acquire(ProjectId);
		if (!Database.IsValid())
		{
			return false;
		}
		Database->bCommittedOnly = UTarinoiSettings::Get()->bCommittedOnly;

		const FTarinoiCodegenModel Model = TarinoiCodegen::Load(*Database);
		if (Model.IsEmpty())
		{
			Notify(TEXT("Tarinoi: nothing to generate. Sync first, and check that your project declares functions or variables."), false);
			return false;
		}

		FTarinoiCodegenOptions Options;
		Options.ProjectId = ProjectId;
		Options.ApiMacro = Target.ApiMacro;

		// Reported before writing: once the files change, the compiled classes no longer tell us
		// what the previous generation looked like.
		const TArray<FTarinoiBindingIssue> Issues = TarinoiBindingValidator::Validate(Model,
			FPaths::Combine(Target.ImplDir, TarinoiCoreFunctions::ClassName() + TEXT(".h")));
		for (const FTarinoiBindingIssue& Issue : Issues)
		{
			if (Issue.bBreaking)
			{
				Log.Warning(FText::FromString(Issue.Message));
			}
		}

		if (!TarinoiCodegen::Write(TarinoiCodegen::Render(Model, Options), Target.OutputDir))
		{
			Notify(TEXT("Tarinoi: could not write the generated bindings. See the log."), false);
			return false;
		}
		TarinoiCoreFunctions::Scaffold(Model, Target.ImplDir, Target.OutputDir, Options);

		// The generated classes include Tarinoi headers, so the module has to depend on it.
		FString BuildFile;
		FFileHelper::LoadFileToString(BuildFile, *FPaths::Combine(Target.ModuleDir, Target.ModuleName + TEXT(".Build.cs")));
		if (!BuildFile.Contains(TEXT("\"Tarinoi\"")))
		{
			Log.Warning(FText::FromString(FString::Printf(TEXT("%s.Build.cs does not list \"Tarinoi\" as a dependency, which the generated bindings need. Add it to PublicDependencyModuleNames."),
				*Target.ModuleName)));
		}

		Notify(FString::Printf(TEXT("Tarinoi: wrote bindings to %s. Build the project to compile them: close the editor and build from your IDE, since Live Coding cannot add new classes."),
			*Target.OutputDir), true);
		return true;
	}

	int32 CheckBindings()
	{
		FMessageLog Log(MessageLogName);
		const FString ProjectId = ProjectIdOrReport();
		const TSharedPtr<FTarinoiDatabase> Database = ProjectId.IsEmpty() ? nullptr : FTarinoiDatabase::Acquire(ProjectId);
		if (!Database.IsValid())
		{
			return 0;
		}
		Database->bCommittedOnly = UTarinoiSettings::Get()->bCommittedOnly;

		const FTarinoiCodegenTarget Target = ResolveCodegenTarget();
		const TArray<FTarinoiBindingIssue> Issues = TarinoiBindingValidator::Validate(TarinoiCodegen::Load(*Database),
			FPaths::Combine(Target.ImplDir, TarinoiCoreFunctions::ClassName() + TEXT(".h")));

		int32 Breaking = 0;
		for (const FTarinoiBindingIssue& Issue : Issues)
		{
			UE_LOG(LogTarinoi, Warning, TEXT("%s"), *Issue.Message);
			if (Issue.bBreaking)
			{
				++Breaking;
				Log.Error(FText::FromString(Issue.Message));
			}
			else
			{
				Log.Warning(FText::FromString(Issue.Message));
			}
		}

		if (Issues.Num() == 0)
		{
			Notify(TEXT("Tarinoi: the bindings match the synced content."), true);
		}
		else
		{
			Notify(FString::Printf(TEXT("Tarinoi: %d difference(s) from the synced content, %d breaking. Regenerate Bindings to update. See the Tarinoi message log."),
				Issues.Num(), Breaking), Breaking == 0);
			Log.Open();
		}
		return Breaking;
	}

	bool ExportSnapshot()
	{
		const FString ProjectId = ProjectIdOrReport();
		if (ProjectId.IsEmpty())
		{
			return false;
		}

		const FString Source = FTarinoiDatabase::PathForProject(ProjectId);
		if (!FPaths::FileExists(Source))
		{
			Notify(FString::Printf(TEXT("Tarinoi: there is no local content for '%s' to export. Sync first."), *ProjectId), false);
			return false;
		}

		// Committed transactions are already in the file (the connection uses a rollback journal),
		// so copying it while the shared connection is open is safe.
		const FString Target = TarinoiSnapshot::SourcePath(ProjectId);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Target), true);
		if (const TSharedPtr<FTarinoiDatabase> Stale = FTarinoiDatabase::AcquireIfOpen(Target))
		{
			Stale->Close();
		}
		if (IFileManager::Get().Copy(*Target, *Source, true, true) != COPY_OK)
		{
			Notify(FString::Printf(TEXT("Tarinoi: could not write the snapshot to %s."), *Target), false);
			return false;
		}

		// A shipped build has no business carrying the endpoint it was authored against.
		if (const TSharedPtr<FTarinoiDatabase> Snapshot = FTarinoiDatabase::AcquireAtPath(Target))
		{
			Snapshot->DeleteMeta({FTarinoiDatabase::ApiPathKey, FTarinoiDatabase::ApiSyncCursorKey});
			Snapshot->Close();
		}

		// Content/Tarinoi holds no assets, so packaging skips it unless told to stage it.
		UProjectPackagingSettings* Packaging = GetMutableDefault<UProjectPackagingSettings>();
		const bool bStaged = Packaging->DirectoriesToAlwaysStageAsUFS.ContainsByPredicate([](const FDirectoryPath& Path)
		{
			return Path.Path.Equals(TarinoiSnapshot::ContentFolder, ESearchCase::IgnoreCase);
		});
		if (!bStaged)
		{
			FDirectoryPath Folder;
			Folder.Path = TarinoiSnapshot::ContentFolder;
			Packaging->DirectoriesToAlwaysStageAsUFS.Add(Folder);
		}
		AddGameIniLine(TEXT("/Script/UnrealEd.ProjectPackagingSettings"),
			FString::Printf(TEXT("+DirectoriesToAlwaysStageAsUFS=(Path=\"%s\")"), TarinoiSnapshot::ContentFolder));

		// The settings live in a config file of their own, which packaging stages but warns about
		// unless the project says it is meant to ship.
		AddGameIniLine(TEXT("Staging"), FString::Printf(TEXT("+AllowedConfigFiles=%s/Config/DefaultTarinoi.ini"), FApp::GetProjectName()));

		Notify(FString::Printf(TEXT("Tarinoi: exported a %lld KB snapshot to %s. Turn on Offline Mode in the Tarinoi settings to play from it."),
			IFileManager::Get().FileSize(*Target) / 1024, *FPaths::ConvertRelativePathToFull(Target)), true);
		return true;
	}

	bool ClearLocalContent()
	{
		const FString ProjectId = ProjectIdOrReport();
		if (ProjectId.IsEmpty())
		{
			return false;
		}

		const FString Path = FTarinoiDatabase::PathForProject(ProjectId);
		if (!FPaths::FileExists(Path))
		{
			Notify(TEXT("Tarinoi: there is no local content to clear."), true);
			return true;
		}

		if (const TSharedPtr<FTarinoiDatabase> Open = FTarinoiDatabase::AcquireIfOpen(Path))
		{
			Open->Close();
		}

		for (const TCHAR* Suffix : {TEXT(""), TEXT("-journal"), TEXT("-wal"), TEXT("-shm")})
		{
			IFileManager::Get().Delete(*(Path + Suffix), false, true, true);
		}

		Notify(FString::Printf(TEXT("Tarinoi: cleared local content for '%s'. The next sync fetches everything again."), *ProjectId), true);
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
