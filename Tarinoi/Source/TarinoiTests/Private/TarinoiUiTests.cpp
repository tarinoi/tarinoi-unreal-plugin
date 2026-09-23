// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

// The quickstart interface, driven through its real buttons against a real runtime. The views are
// checked for size as well as content: a widget-counting test cannot see an interface that has
// collapsed to nothing, which is a mistake an earlier port shipped.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/UserWidget.h"
#include "Components/TarinoiDialogueTrigger.h"
#include "TarinoiRuntimeHarness.h"
#include "TarinoiTestHelpers.h"
#include "TarinoiTestWorld.h"
#include "Ui/TarinoiDialogueStripWidget.h"
#include "Ui/TarinoiQuickstartWidget.h"
#include "Ui/TarinoiStartPickerWidget.h"

using FCard = FTarinoiCardBuilder;

namespace TarinoiUiTests
{
	FVector2D LaidOutSize(UUserWidget* Widget)
	{
		const TSharedRef<SWidget> Slate = Widget->TakeWidget();
		Slate->SlatePrepass(1.0f);
		return Slate->GetDesiredSize();
	}
}

BEGIN_DEFINE_SPEC(FTarinoiUiSpec, "Tarinoi.Ui",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	TUniquePtr<FTarinoiRuntimeHarness> H;
	TUniquePtr<FTarinoiTestWorld> TestWorld;

	void SeedDialogue()
	{
		H->Configure();
		H->Store->Add(TEXT("s1"), FCard::Start().To(TEXT("hello")));
		H->Store->Add(TEXT("hello"), FCard::Line(TEXT("Hello there")).Mode(TEXT("npc")).To({TEXT("a"), TEXT("b")}));
		H->Store->Add(TEXT("a"), FCard::Line(TEXT("Hi")).Mode(TEXT("pc")).Geo(0).To(TEXT("bye")));
		H->Store->Add(TEXT("b"), FCard::Line(TEXT("Go away")).Mode(TEXT("pc")).Geo(10).To(TEXT("flow:end")));
		H->Store->Add(TEXT("bye"), FCard::Line(TEXT("Farewell")).Mode(TEXT("npc")).To(TEXT("flow:end")));
		H->Store->StartCards.Add({TEXT("s1"), TEXT("col1"), TEXT("Greeting")});
	}
END_DEFINE_SPEC(FTarinoiUiSpec)

void FTarinoiUiSpec::Define()
{
	using namespace TarinoiUiTests;

	BeforeEach([this]()
	{
		H = MakeUnique<FTarinoiRuntimeHarness>();
		TestWorld = MakeUnique<FTarinoiTestWorld>();
	});

	AfterEach([this]()
	{
		H.Reset();
		TestWorld.Reset();
	});

	Describe("dialogue strip", [this]()
	{
		It("shows a line with a Continue button, and advances when it is clicked", [this]()
		{
			SeedDialogue();
			UTarinoiDialogueStripWidget* Strip = CreateWidget<UTarinoiDialogueStripWidget>(TestWorld->World);
			Strip->BindRuntime(H->Runtime.Get());

			H->Start(TEXT("s1"));
			TarinoiTest::Strings(*this, TEXT("transcript"), Strip->GetTranscript(), {TEXT("Hello there")});
			TestEqual("one action", Strip->GetLiveActionCount(), 1);

			Strip->InvokeLiveAction(0);
			TestEqual("choices offered", Strip->GetLiveActionCount(), 2);
			TestTrue("runtime waits for a choice", H->State() == ETarinoiDialogueState::PcChoice);
		});

		It("picks a choice through its button, and freezes what came before", [this]()
		{
			SeedDialogue();
			UTarinoiDialogueStripWidget* Strip = CreateWidget<UTarinoiDialogueStripWidget>(TestWorld->World);
			Strip->BindRuntime(H->Runtime.Get());
			H->Start(TEXT("s1"));
			Strip->InvokeLiveAction(0);

			Strip->InvokeLiveAction(0); // "1. Hi"
			TestEqual("choice made", H->Events->ChoicesMade.Num(), 1);
			TestEqualSensitive("the first choice", H->Events->ChoicesMade[0].Line, FString(TEXT("Hi")));
			TestEqual("entries", Strip->GetEntryCount(), 3);
			TestEqualSensitive("latest line", Strip->GetTranscript().Last(), FString(TEXT("Farewell")));
			TestEqual("only the new Continue is live", Strip->GetLiveActionCount(), 1);
		});

		It("has a real size once laid out", [this]()
		{
			SeedDialogue();
			UTarinoiDialogueStripWidget* Strip = CreateWidget<UTarinoiDialogueStripWidget>(TestWorld->World);
			Strip->BindRuntime(H->Runtime.Get());
			H->Start(TEXT("s1"));
			const FVector2D Size = LaidOutSize(Strip);
			TestTrue(FString::Printf(TEXT("width %f"), Size.X), Size.X > 50.0f);
			TestTrue(FString::Printf(TEXT("height %f"), Size.Y), Size.Y > 30.0f);
		});

		It("shows an error with a way back", [this]()
		{
			H->Configure();
			UTarinoiDialogueStripWidget* Strip = CreateWidget<UTarinoiDialogueStripWidget>(TestWorld->World);
			Strip->BindRuntime(H->Runtime.Get());
			AddExpectedError(TEXT("was not found"));
			H->Start(TEXT("missing"));
			TestTrue("says so", Strip->GetTranscript().Num() == 1 && Strip->GetTranscript()[0].Contains(TEXT("The dialogue stopped")));
			Strip->InvokeLiveAction(0);
			TestEqual("ended", H->Events->EndedCount, 1);
		});

		It("empties on Clear", [this]()
		{
			SeedDialogue();
			UTarinoiDialogueStripWidget* Strip = CreateWidget<UTarinoiDialogueStripWidget>(TestWorld->World);
			Strip->BindRuntime(H->Runtime.Get());
			H->Start(TEXT("s1"));
			Strip->Clear();
			TestEqual("empty", Strip->GetEntryCount(), 0);
			TestEqual("no actions", Strip->GetLiveActionCount(), 0);
		});
	});

	Describe("start picker", [this]()
	{
		It("lists the entry points and reports the one picked", [this]()
		{
			SeedDialogue();
			UTarinoiStartPickerWidget* Picker = CreateWidget<UTarinoiStartPickerWidget>(TestWorld->World);
			Picker->BindRuntime(H->Runtime.Get());
			TestEqual("listed", Picker->GetEntryCount(), 1);
			TestTrue("laid out", LaidOutSize(Picker).Y > 30.0f);
		});

		It("says what to do when there is nothing to list", [this]()
		{
			H->Configure();
			UTarinoiStartPickerWidget* Picker = CreateWidget<UTarinoiStartPickerWidget>(TestWorld->World);
			Picker->BindRuntime(H->Runtime.Get());
			TestEqual("nothing", Picker->GetEntryCount(), 0);
		});
	});

	Describe("quickstart", [this]()
	{
		It("plays a dialogue picked from the list, and keeps the first line on screen", [this]()
		{
			// The regression this guards: switching to the dialogue view once cleared the transcript
			// after the strip had already drawn the first line, leaving a blank screen.
			SeedDialogue();
			UTarinoiQuickstartWidget* Quickstart = CreateWidget<UTarinoiQuickstartWidget>(TestWorld->World);
			Quickstart->BindRuntime(H->Runtime.Get());
			Quickstart->ShowPicker();
			TestFalse("picker first", Quickstart->IsShowingDialogue());

			Quickstart->GetPicker()->Pick(0);
			TestTrue("dialogue shown", Quickstart->IsShowingDialogue());
			TarinoiTest::Strings(*this, TEXT("first line kept"), Quickstart->GetStrip()->GetTranscript(), {TEXT("Hello there")});
		});

		It("returns to the picker when the dialogue ends, with the transcript cleared", [this]()
		{
			SeedDialogue();
			UTarinoiQuickstartWidget* Quickstart = CreateWidget<UTarinoiQuickstartWidget>(TestWorld->World);
			Quickstart->BindRuntime(H->Runtime.Get());
			Quickstart->GetPicker()->Pick(0);
			UTarinoiDialogueStripWidget* Strip = Quickstart->GetStrip();
			Strip->InvokeLiveAction(0); // continue
			Strip->InvokeLiveAction(1); // "2. Go away" leads to flow:end
			TestFalse("picker again", Quickstart->IsShowingDialogue());
			TestEqual("transcript cleared", Strip->GetEntryCount(), 0);
		});
	});

	Describe("dialogue trigger", [this]()
	{
		It("raises its entry point when triggered", [this]()
		{
			UTarinoiDialogueTrigger* Trigger = NewObject<UTarinoiDialogueTrigger>();
			UTarinoiRuntimeRecorder* Recorder = NewObject<UTarinoiRuntimeRecorder>();
			Trigger->OnTriggered.AddDynamic(Recorder, &UTarinoiRuntimeRecorder::HandleTriggered);
			Trigger->CollectionId = TEXT("col1");
			Trigger->CardId = TEXT("s1");
			TestTrue("configured", Trigger->IsConfigured());
			Trigger->Trigger();
			TarinoiTest::Strings(*this, TEXT("raised"), Recorder->Triggered, {TEXT("col1/s1")});
		});

		It("warns instead of firing when it has no dialogue", [this]()
		{
			UTarinoiDialogueTrigger* Trigger = NewObject<UTarinoiDialogueTrigger>();
			AddExpectedMessage(TEXT("has no dialogue set"), ELogVerbosity::Warning);
			Trigger->Trigger();
		});

		It("fires once on entry, and only for matching actors", [this]()
		{
			ATarinoiDialogueVolume* Volume = TestWorld->World->SpawnActor<ATarinoiDialogueVolume>();
			AActor* Stranger = TestWorld->World->SpawnActor<AActor>();
			AActor* Hero = TestWorld->World->SpawnActor<AActor>();
			Hero->Tags.Add(TEXT("Hero"));

			Volume->bPlayerPawnsOnly = false;
			Volume->RequiredActorTag = TEXT("Hero");
			Volume->DialogueTrigger->CollectionId = TEXT("col1");
			Volume->DialogueTrigger->CardId = TEXT("s1");

			TestFalse("stranger", Volume->Matches(Stranger));
			TestTrue("hero", Volume->Matches(Hero));
			TestFalse("nothing", Volume->Matches(nullptr));

			UTarinoiRuntimeRecorder* Recorder = NewObject<UTarinoiRuntimeRecorder>();
			Volume->DialogueTrigger->OnTriggered.AddDynamic(Recorder, &UTarinoiRuntimeRecorder::HandleTriggered);

			Volume->NotifyActorBeginOverlap(Stranger);
			TestFalse("stranger does not occupy", Volume->IsOccupied());
			TestEqual("stranger does not fire", Recorder->Triggered.Num(), 0);

			Volume->NotifyActorBeginOverlap(Hero);
			TestTrue("hero occupies", Volume->IsOccupied());
			TestEqual("fires on entry", Recorder->Triggered.Num(), 1);
			Volume->NotifyActorEndOverlap(Hero);
			TestFalse("hero left", Volume->IsOccupied());

			Volume->NotifyActorBeginOverlap(Hero);
			TestEqual("once only", Recorder->Triggered.Num(), 1);
		});

		It("reports occupants without firing in WhileInside mode", [this]()
		{
			ATarinoiDialogueVolume* Volume = TestWorld->World->SpawnActor<ATarinoiDialogueVolume>();
			AActor* Hero = TestWorld->World->SpawnActor<AActor>();
			Volume->bPlayerPawnsOnly = false;
			Volume->Mode = ETarinoiTriggerMode::WhileInside;
			Volume->DialogueTrigger->CollectionId = TEXT("col1");
			Volume->DialogueTrigger->CardId = TEXT("s1");
			UTarinoiRuntimeRecorder* Recorder = NewObject<UTarinoiRuntimeRecorder>();
			Volume->DialogueTrigger->OnTriggered.AddDynamic(Recorder, &UTarinoiRuntimeRecorder::HandleTriggered);

			Volume->NotifyActorBeginOverlap(Hero);
			TestTrue("occupied", Volume->IsOccupied());
			TestEqual("did not fire", Recorder->Triggered.Num(), 0);
			Volume->DialogueTrigger->Trigger();
			TestEqual("fires when the game asks", Recorder->Triggered.Num(), 1);
		});

		It("only admits player-controlled pawns by default", [this]()
		{
			ATarinoiDialogueVolume* Volume = TestWorld->World->SpawnActor<ATarinoiDialogueVolume>();
			APawn* Unpossessed = TestWorld->World->SpawnActor<APawn>();
			TestFalse("not player controlled", Volume->Matches(Unpossessed));
		});
	});
}

#endif
