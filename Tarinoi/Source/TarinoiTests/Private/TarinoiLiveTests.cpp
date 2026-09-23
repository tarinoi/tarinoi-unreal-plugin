// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

// Play-throughs against real synced content, with the project's generated bindings. Unit tests
// can share the code's wrong assumptions about content shape; real content cannot. These skip
// (with a note) unless the project has synced the Tarinoi Starter Pack and regenerated bindings.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Bindings/TarinoiBindings.h"
#include "Data/TarinoiDatabase.h"
#include "TarinoiRuntime.h"
#include "TarinoiRuntimeHarness.h"
#include "TarinoiSettings.h"
#include "UObject/StrongObjectPtr.h"

namespace TarinoiLiveTests
{
	// Cards in the Starter Pack template.
	const TCHAR* EngineQuestion = TEXT("r_THWAR1tt-bkEFHkj1uT");
	const TCHAR* ExampleIntro = TEXT("9ThTonKdmgOnmcoLeHx4G");
}

BEGIN_DEFINE_SPEC(FTarinoiLiveSpec, "Tarinoi.Live.StarterPack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	TStrongObjectPtr<UTarinoiRuntime> Runtime;
	TStrongObjectPtr<UTarinoiRuntimeRecorder> Events;
	bool bReady = false;

	/** Moves on one step, whichever kind of stop the dialogue is at: a line, or a choice of one. */
	void Step()
	{
		if (Runtime->GetDialogueState() == ETarinoiDialogueState::PcChoice)
		{
			Runtime->SelectChoice(0);
		}
		else
		{
			Runtime->Advance();
		}
	}

	bool Locate(const TCHAR* CardId, FString& OutCollection)
	{
		TSharedPtr<FJsonObject> Card;
		return Runtime->GetDocumentStore()->LocateCard(CardId, OutCollection, Card);
	}
END_DEFINE_SPEC(FTarinoiLiveSpec)

void FTarinoiLiveSpec::Define()
{
	using namespace TarinoiLiveTests;

	BeforeEach([this]()
	{
		bReady = false;
		const UTarinoiSettings* Settings = GetDefault<UTarinoiSettings>();
		const FString ProjectId = Settings->GetProjectId();
		if (ProjectId.IsEmpty() || !FPaths::FileExists(FTarinoiDatabase::PathForProject(ProjectId)))
		{
			AddInfo(TEXT("Skipped: no synced project. Set the API path and sync to run the live tests."));
			return;
		}

		Runtime.Reset(NewObject<UTarinoiRuntime>());
		Events.Reset(NewObject<UTarinoiRuntimeRecorder>());
		Events->Listen(Runtime.Get());
		if (!Runtime->ConfigureWith(*Settings))
		{
			AddInfo(TEXT("Skipped: the synced project did not configure."));
			return;
		}

		Runtime->GetBindings()->BindGeneratedDefaults();
		FString Collection;
		if (!Runtime->GetBindings()->GetFunctions(TEXT("tarinoi")) || !Runtime->GetBindings()->GetVariables(TEXT("global")) || !Locate(EngineQuestion, Collection))
		{
			AddInfo(TEXT("Skipped: needs the Starter Pack synced and its bindings generated and compiled."));
			return;
		}
		bReady = true;
	});

	AfterEach([this]()
	{
		if (Runtime)
		{
			Runtime->Shutdown();
		}
		Runtime.Reset();
		Events.Reset();
	});

	It("remembers the engine the player names, and picks the matching line later", [this]()
	{
		if (!bReady)
		{
			return;
		}

		FString Collection;
		Locate(EngineQuestion, Collection);
		Runtime->StartDialogue(Collection, EngineQuestion);
		TestTrue("asks the question", Events->Lines.Num() > 0 && Events->Lines.Last().Line.Contains(TEXT("Which game engine")));
		Runtime->Advance();

		const TArray<FTarinoiDialogueChoice> Choices = Events->ChoiceSets.Num() > 0 ? Events->ChoiceSets.Last() : TArray<FTarinoiDialogueChoice>();
		const FTarinoiDialogueChoice* Unreal = Choices.FindByPredicate([](const FTarinoiDialogueChoice& Choice) { return Choice.Line.Contains(TEXT("Unreal")); });
		if (!TestNotNull("offers Unreal", Unreal))
		{
			return;
		}

		// SetText(Var.global.game_engine, Ls.global.engines.unreal) runs when this is chosen.
		Runtime->SelectChoice(Unreal->Index);
		const FTarinoiValue Engine = Runtime->EvalExpression(TEXT("Var.global.game_engine")).Resolve();
		const FTarinoiValue Expected = Runtime->EvalExpression(TEXT("Ls.global.engines.unreal"));
		TestFalse("the list option resolves", Expected.IsNone());
		TestEqualSensitive("stored the list value", Engine.ToString(), Expected.ToString());
		Runtime->AbortDialogue();

		// A non-player set gated on StringEquals: only the first passing line is shown.
		FString ExampleCollection;
		Locate(ExampleIntro, ExampleCollection);
		Events->Lines.Reset();
		Runtime->StartDialogue(ExampleCollection, ExampleIntro);
		Step();
		TestTrue(FString::Printf(TEXT("the Unreal line: %s"), Events->Lines.Num() ? *Events->Lines.Last().Line : TEXT("none")),
			Events->Lines.Num() > 0 && Events->Lines.Last().Line.Contains(TEXT("using Unreal")));
		TestEqual("no errors", Events->Errors.Num(), 0);
	});
}

#endif
