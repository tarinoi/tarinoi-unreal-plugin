// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "ISettingsModule.h"
#include "MessageLogModule.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "STarinoiTokenDialog.h"
#include "TarinoiEditorActions.h"
#include "TarinoiSettings.h"
#include "TarinoiSettingsCustomization.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "TarinoiEditor"

class FTarinoiEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FMessageLogModule& MessageLog = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
		FMessageLogInitializationOptions Options;
		Options.bShowFilters = true;
		MessageLog.RegisterLogListing("Tarinoi", LOCTEXT("MessageLog", "Tarinoi"), Options);

		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditor.RegisterCustomClassLayout(UTarinoiSettings::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FTarinoiSettingsCustomization::MakeInstance));

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FTarinoiEditorModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);

		if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
		{
			FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").UnregisterCustomClassLayout(UTarinoiSettings::StaticClass()->GetFName());
		}

		if (FModuleManager::Get().IsModuleLoaded("MessageLog"))
		{
			FModuleManager::GetModuleChecked<FMessageLogModule>("MessageLog").UnregisterLogListing("Tarinoi");
		}
	}

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		UToolMenu* Tools = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		FToolMenuSection& Section = Tools->AddSection("Tarinoi", LOCTEXT("Section", "Tarinoi"));
		Section.AddSubMenu("TarinoiMenu", LOCTEXT("Menu", "Tarinoi"), LOCTEXT("MenuTip", "Sync Tarinoi content and generate bindings."),
			FNewToolMenuDelegate::CreateRaw(this, &FTarinoiEditorModule::FillMenu));
	}

	void FillMenu(UToolMenu* Menu)
	{
		FToolMenuSection& Content = Menu->AddSection("Content", LOCTEXT("Content", "Content"));
		Content.AddMenuEntry("Sync", LOCTEXT("Sync", "Sync"), LOCTEXT("SyncTip", "Fetch new content from your Tarinoi project."),
			FSlateIcon(), FUIAction(
				FExecuteAction::CreateLambda([]() { TarinoiEditorActions::Sync(); }),
				FCanExecuteAction::CreateLambda([]() { return !TarinoiEditorActions::IsSyncing(); })));
		Content.AddMenuEntry("Export", LOCTEXT("Export", "Export Snapshot"),
			LOCTEXT("ExportTip", "Copy the synced content into Content/Tarinoi, for offline mode and packaged builds."),
			FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() { TarinoiEditorActions::ExportSnapshot(); })));
		Content.AddMenuEntry("Clear", LOCTEXT("Clear", "Clear Local Content"),
			LOCTEXT("ClearTip", "Delete the local copy of the content. The next sync fetches everything again."),
			FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() { TarinoiEditorActions::ClearLocalContent(); })));

		FToolMenuSection& Bindings = Menu->AddSection("Bindings", LOCTEXT("Bindings", "Bindings"));
		Bindings.AddMenuEntry("Regenerate", LOCTEXT("Regenerate", "Regenerate Bindings"),
			LOCTEXT("RegenerateTip", "Write C++ binding classes for the synced functions and variables into your game module."),
			FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() { TarinoiEditorActions::RegenerateBindings(); })));
		Bindings.AddMenuEntry("Check", LOCTEXT("Check", "Check Bindings"),
			LOCTEXT("CheckTip", "Compare the compiled bindings with the synced content."),
			FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() { TarinoiEditorActions::CheckBindings(); })));

		FToolMenuSection& Setup = Menu->AddSection("Setup", LOCTEXT("Setup", "Setup"));
		Setup.AddMenuEntry("Token", LOCTEXT("Token", "Set API Token..."), LOCTEXT("TokenTip", "Store the API token for this project, outside the project folder."),
			FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() { STarinoiTokenDialog::Open(); })));
		Setup.AddMenuEntry("Settings", LOCTEXT("Settings", "Settings..."), LOCTEXT("SettingsTip", "Open Project Settings > Plugins > Tarinoi."),
			FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]()
			{
				FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Plugins", "Tarinoi");
			})));
	}
};

IMPLEMENT_MODULE(FTarinoiEditorModule, TarinoiEditor)

#undef LOCTEXT_NAMESPACE
