// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Sync/TarinoiSyncTypes.h"

/** Where codegen writes for the current project. */
struct TARINOIEDITOR_API FTarinoiCodegenTarget
{
	FString ModuleName;
	FString ModuleDir;
	FString OutputDir;
	FString ImplDir;
	FString ApiMacro;

	bool IsValid() const { return !ModuleName.IsEmpty(); }
};

/**
 * What the Tools > Tarinoi menu does, shared with the commandlet. Every action reports through
 * the log and the Tarinoi message log, and returns rather than asserting on a problem.
 */
namespace TarinoiEditorActions
{
	/** Starts a sync from the project settings. OnDone (optional) receives the result. */
	TARINOIEDITOR_API void Sync(TFunction<void(const FTarinoiSyncResult&)> OnDone = nullptr);

	TARINOIEDITOR_API bool IsSyncing();

	/** Writes the generated bindings and, once, the core functions scaffold. */
	TARINOIEDITOR_API bool RegenerateBindings();

	/** Compares the synced content with the compiled bindings. Returns the number of breaking issues. */
	TARINOIEDITOR_API int32 CheckBindings();

	/**
	 * Copies the synced content to Content/Tarinoi/<project>.db for offline mode, strips the API
	 * path and cursor from the copy, and makes sure packaging stages the folder.
	 */
	TARINOIEDITOR_API bool ExportSnapshot();

	/** Deletes the local content database for the project. The next sync refetches everything. */
	TARINOIEDITOR_API bool ClearLocalContent();

	/** The module and folders codegen targets, from the settings and the project descriptor. */
	TARINOIEDITOR_API FTarinoiCodegenTarget ResolveCodegenTarget();

	/** Shows a toast in the editor, or only logs when there is no UI. */
	TARINOIEDITOR_API void Notify(const FString& Message, bool bSuccess);
}
