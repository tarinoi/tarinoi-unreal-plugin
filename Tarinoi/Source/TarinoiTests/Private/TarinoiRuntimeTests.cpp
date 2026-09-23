// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

// Every traversal rule the runtime implements, one named test each. These rules are the product
// behaviour: in-app playback in Tarinoi follows the same ones, and what an author verified there
// has to hold in the game.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Bindings/TarinoiBindings.h"
#include "TarinoiRuntimeHarness.h"
#include "Misc/Paths.h"
#include "Sync/TarinoiCredentials.h"
#include "TarinoiTestHelpers.h"

using FCard = FTarinoiCardBuilder;

BEGIN_DEFINE_SPEC(FTarinoiRuntimeSpec, "Tarinoi.Runtime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	TUniquePtr<FTarinoiRuntimeHarness> H;

	void SeedShownOnceHub(const TCHAR* LoopTarget = TEXT("hub"))
	{
		H->Store->Add(TEXT("hub"), FCard::Blank().To({TEXT("a"), TEXT("b"), TEXT("c")}));
		H->Store->Add(TEXT("a"), FCard::Line(TEXT("Once only")).Mode(TEXT("pc")).Geo(0).ShownOnce().To(LoopTarget));
		H->Store->Add(TEXT("b"), FCard::Line(TEXT("Again")).Mode(TEXT("pc")).Geo(10).To(TEXT("hub")));
		H->Store->Add(TEXT("c"), FCard::Line(TEXT("And again")).Mode(TEXT("pc")).Geo(20).To(TEXT("hub")));
	}
END_DEFINE_SPEC(FTarinoiRuntimeSpec)

void FTarinoiRuntimeSpec::Define()
{
	BeforeEach([this]() { H = MakeUnique<FTarinoiRuntimeHarness>(); });
	AfterEach([this]() { H.Reset(); });

	Describe("basic playback", [this]()
	{
		It("raises an NPC line with its speaker and text", [this]()
		{
			H->SeedEntity(TEXT("narrator"), false, TEXT("The Narrator")).Configure();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Hello there")).Entity(TEXT("narrator")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TestEqual("one line", H->Events->Lines.Num(), 1);
			TestEqualSensitive("text", H->LastLine().Line, FString(TEXT("Hello there")));
			TestEqualSensitive("speaker", H->LastLine().EntityLabel, FString(TEXT("The Narrator")));
			TestEqualSensitive("card", H->LastLine().CardId, FString(TEXT("c1")));
			TestTrue("state", H->State() == ETarinoiDialogueState::NpcLine);
		});

		It("falls back to the entity identifier for the speaker label", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Hi")).Entity(TEXT("unknown_entity")).Mode(TEXT("npc")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TestEqualSensitive("label", H->LastLine().EntityLabel, FString(TEXT("unknown_entity")));
		});

		It("reports a missing card as an error", [this]()
		{
			H->Configure();
			AddExpectedError(TEXT("was not found"));
			H->Start(TEXT("nope"));
			TestEqual("errors", H->Events->Errors.Num(), 1);
			TestTrue("names the card", H->Events->Errors.Num() == 1 && H->Events->Errors[0].Contains(TEXT("nope")));
		});

		It("reports starting before configuring", [this]()
		{
			AddExpectedError(TEXT("not configured"));
			H->Start(TEXT("c1"));
			TestEqual("no lines", H->Events->Lines.Num(), 0);
		});
	});

	Describe("transparent cards", [this]()
	{
		for (const TCHAR* BaseRef : {TEXT("start"), TEXT("blank")})
		{
			It(FString::Printf(TEXT("traverses a %s card without showing it"), BaseRef), [this, BaseRef]()
			{
				H->Configure();
				H->Store->Add(TEXT("c1"), FCard::Of(BaseRef).To(TEXT("c2")));
				H->Store->Add(TEXT("c2"), FCard::Line(TEXT("Arrived")).Mode(TEXT("npc")).To(TEXT("flow:end")));
				H->Start(TEXT("c1"));
				TestEqual("only the line surfaces", H->Events->Lines.Num(), 1);
				TestEqualSensitive("text", H->LastLine().Line, FString(TEXT("Arrived")));
			});
		}

		It("traverses an unrecognised card type with a warning", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Of(TEXT("some_future_type")).To(TEXT("c2")));
			H->Store->Add(TEXT("c2"), FCard::Line(TEXT("Arrived")).Mode(TEXT("npc")).To(TEXT("flow:end")));
			AddExpectedMessage(TEXT("unrecognised type"), ELogVerbosity::Warning);
			H->Start(TEXT("c1"));
			TestEqualSensitive("text", H->LastLine().Line, FString(TEXT("Arrived")));
		});

		It("follows a jump's target link to another collection", [this]()
		{
			// A jump's destination is data.target: a bare document id, possibly on another board.
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Of(TEXT("jump")).Jump(TEXT("far")));
			H->Store->Add(TEXT("far"), FCard::Line(TEXT("Elsewhere")).Mode(TEXT("npc")).To(TEXT("flow:end")), TEXT("col2"));
			H->Start(TEXT("c1"));
			TestEqualSensitive("text", H->LastLine().Line, FString(TEXT("Elsewhere")));
			TestEqualSensitive("collection", H->LastLine().CollectionId, FString(TEXT("col2")));
		});

		It("reports a jump to a card that does not exist", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Of(TEXT("jump")).Jump(TEXT("nowhere")));
			AddExpectedError(TEXT("does not exist: 'nowhere'"));
			H->Start(TEXT("c1"));
			TestEqual("errors", H->Events->Errors.Num(), 1);
		});

		It("reports a jump with no target", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Of(TEXT("jump")));
			AddExpectedError(TEXT("does not say where to jump"));
			H->Start(TEXT("c1"));
			TestEqual("errors", H->Events->Errors.Num(), 1);
		});
	});

	Describe("ending", [this]()
	{
		It("finishes at flow:end", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Bye")).Mode(TEXT("npc")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			H->Advance();
			TestEqual("ended", H->Events->EndedCount, 1);
			TestTrue("idle", H->State() == ETarinoiDialogueState::Idle);
		});

		It("ends with an error at a card with no connections", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Dead end")).Mode(TEXT("npc")));
			H->Start(TEXT("c1"));
			AddExpectedError(TEXT("leads nowhere"));
			H->Advance();
			TestEqual("ended", H->Events->EndedCount, 1);
		});

		It("ends at once on abort", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Hi")).Mode(TEXT("npc")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			H->Runtime->AbortDialogue();
			TestEqual("ended", H->Events->EndedCount, 1);
			TestTrue("idle", H->State() == ETarinoiDialogueState::Idle);
		});
	});

	Describe("default connections", [this]()
	{
		It("follows a single default target silently", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("One")).Mode(TEXT("npc")).To(TEXT("c2")));
			H->Store->Add(TEXT("c2"), FCard::Line(TEXT("Two")).Mode(TEXT("npc")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			H->Advance();
			TestEqualSensitive("text", H->LastLine().Line, FString(TEXT("Two")));
			TestEqual("not a choice", H->Events->ChoiceSets.Num(), 0);
		});

		It("collapses duplicate default targets", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("One")).Mode(TEXT("npc")).Connect({TEXT("default>>c2"), TEXT("default>>c2")}));
			H->Store->Add(TEXT("c2"), FCard::Line(TEXT("Two")).Mode(TEXT("npc")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			H->Advance();
			TestEqualSensitive("text", H->LastLine().Line, FString(TEXT("Two")));
			TestEqual("not a choice", H->Events->ChoiceSets.Num(), 0);
		});

		It("offers several default targets as choices", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Blank().To({TEXT("a"), TEXT("b")}));
			H->Store->Add(TEXT("a"), FCard::Line(TEXT("Option A")).Mode(TEXT("pc")).Geo(10));
			H->Store->Add(TEXT("b"), FCard::Line(TEXT("Option B")).Mode(TEXT("pc")).Geo(20));
			H->Start(TEXT("c1"));
			TestEqual("one set", H->Events->ChoiceSets.Num(), 1);
			TestEqual("two choices", H->Choices().Num(), 2);
			TestTrue("state", H->State() == ETarinoiDialogueState::PcChoice);
		});

		It("does not offer a non-line card as a choice", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Blank().To({TEXT("a"), TEXT("b")}));
			H->Store->Add(TEXT("a"), FCard::Line(TEXT("Real option")).Mode(TEXT("npc")).Geo(10).To(TEXT("flow:end")));
			H->Store->Add(TEXT("b"), FCard::Blank().Geo(20));
			AddExpectedMessage(TEXT("is not a line"), ELogVerbosity::Warning);
			H->Start(TEXT("c1"));
			// Only one option survived, so it is followed rather than offered.
			TestEqual("no choice", H->Events->ChoiceSets.Num(), 0);
			TestEqualSensitive("text", H->LastLine().Line, FString(TEXT("Real option")));
		});
	});

	Describe("choice ordering by geo.y", [this]()
	{
		It("presents choices top to bottom", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Blank().To({TEXT("bottom"), TEXT("top"), TEXT("middle")}));
			H->Store->Add(TEXT("top"), FCard::Line(TEXT("Top")).Mode(TEXT("pc")).Geo(-50));
			H->Store->Add(TEXT("middle"), FCard::Line(TEXT("Middle")).Mode(TEXT("pc")).Geo(0));
			H->Store->Add(TEXT("bottom"), FCard::Line(TEXT("Bottom")).Mode(TEXT("pc")).Geo(120.5));
			H->Start(TEXT("c1"));
			TarinoiTest::Strings(*this, TEXT("order"), H->ChoiceLines(), {TEXT("Top"), TEXT("Middle"), TEXT("Bottom")});
		});

		It("sorts negative positions before zero", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Blank().To({TEXT("zero"), TEXT("negative")}));
			H->Store->Add(TEXT("zero"), FCard::Line(TEXT("Zero")).Mode(TEXT("pc")).Geo(0));
			H->Store->Add(TEXT("negative"), FCard::Line(TEXT("Negative")).Mode(TEXT("pc")).Geo(-1000));
			H->Start(TEXT("c1"));
			TarinoiTest::Strings(*this, TEXT("order"), H->ChoiceLines(), {TEXT("Negative"), TEXT("Zero")});
		});

		It("sorts cards without geometry last", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Blank().To({TEXT("nogeo"), TEXT("positioned")}));
			H->Store->Add(TEXT("nogeo"), FCard::Line(TEXT("No geometry")).Mode(TEXT("pc")));
			H->Store->Add(TEXT("positioned"), FCard::Line(TEXT("Positioned")).Mode(TEXT("pc")).Geo(500));
			H->Start(TEXT("c1"));
			TarinoiTest::Strings(*this, TEXT("order"), H->ChoiceLines(), {TEXT("Positioned"), TEXT("No geometry")});
		});

		It("keeps equal positions in their original order", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Blank().To({TEXT("first"), TEXT("second")}));
			H->Store->Add(TEXT("first"), FCard::Line(TEXT("First")).Mode(TEXT("pc")).Geo(10));
			H->Store->Add(TEXT("second"), FCard::Line(TEXT("Second")).Mode(TEXT("pc")).Geo(10));
			H->Start(TEXT("c1"));
			TarinoiTest::Strings(*this, TEXT("order"), H->ChoiceLines(), {TEXT("First"), TEXT("Second")});
		});

		It("numbers choices by their sorted position", [this]()
		{
			// SelectChoice indexes positionally, so a stale index would pick the wrong line.
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Blank().To({TEXT("bottom"), TEXT("top")}));
			H->Store->Add(TEXT("top"), FCard::Line(TEXT("Top")).Mode(TEXT("pc")).Geo(0).To(TEXT("flow:end")));
			H->Store->Add(TEXT("bottom"), FCard::Line(TEXT("Bottom")).Mode(TEXT("pc")).Geo(100).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TestEqual("first index", H->Choices()[0].Index, 0);
			TestEqual("second index", H->Choices()[1].Index, 1);
			H->Select(0);
			TestEqualSensitive("index 0 is the topmost", H->Events->ChoicesMade[0].Line, FString(TEXT("Top")));
		});
	});

	Describe("conditions", [this]()
	{
		It("does not offer a choice whose condition fails", [this]()
		{
			H->Configure();
			H->BindSpy();
			H->Store->Add(TEXT("c1"), FCard::Blank().To({TEXT("a"), TEXT("b"), TEXT("c")}));
			H->Store->Add(TEXT("a"), FCard::Line(TEXT("Allowed")).Mode(TEXT("pc")).Geo(0).Condition(TEXT("Fn.g.True()")));
			H->Store->Add(TEXT("b"), FCard::Line(TEXT("Blocked")).Mode(TEXT("pc")).Geo(10).Condition(TEXT("Fn.g.False()")));
			H->Store->Add(TEXT("c"), FCard::Line(TEXT("Also allowed")).Mode(TEXT("pc")).Geo(20));
			H->Start(TEXT("c1"));
			TarinoiTest::Strings(*this, TEXT("offered"), H->ChoiceLines(), {TEXT("Allowed"), TEXT("Also allowed")});
		});

		It("follows the only surviving option without asking", [this]()
		{
			H->Configure();
			H->BindSpy();
			H->Store->Add(TEXT("c1"), FCard::Blank().To({TEXT("a"), TEXT("b")}));
			H->Store->Add(TEXT("a"), FCard::Line(TEXT("Survivor")).Mode(TEXT("npc")).Geo(0).To(TEXT("flow:end")));
			H->Store->Add(TEXT("b"), FCard::Line(TEXT("Blocked")).Mode(TEXT("npc")).Geo(10).Condition(TEXT("Fn.g.False()")));
			H->Start(TEXT("c1"));
			TestEqual("no choice", H->Events->ChoiceSets.Num(), 0);
			TestEqualSensitive("text", H->LastLine().Line, FString(TEXT("Survivor")));
		});

		It("ends with an error when every choice is blocked", [this]()
		{
			H->Configure();
			H->BindSpy();
			H->Store->Add(TEXT("c1"), FCard::Blank().To({TEXT("a"), TEXT("b")}));
			H->Store->Add(TEXT("a"), FCard::Line(TEXT("A")).Mode(TEXT("pc")).Geo(0).Condition(TEXT("Fn.g.False()")));
			H->Store->Add(TEXT("b"), FCard::Line(TEXT("B")).Mode(TEXT("pc")).Geo(10).Condition(TEXT("Fn.g.False()")));
			AddExpectedError(TEXT("all 2 possible continuation"));
			H->Start(TEXT("c1"));
			TestEqual("ended", H->Events->EndedCount, 1);
		});

		It("steps over a card whose entry condition fails, rather than stopping", [this]()
		{
			H->Configure();
			H->BindSpy();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Skipped")).Mode(TEXT("npc")).Condition(TEXT("Fn.g.False()")).To(TEXT("c2")));
			H->Store->Add(TEXT("c2"), FCard::Line(TEXT("Reached")).Mode(TEXT("npc")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TestEqual("one line", H->Events->Lines.Num(), 1);
			TestEqualSensitive("text", H->LastLine().Line, FString(TEXT("Reached")));
		});

		It("treats an unfilled template in a condition as met", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Shown")).Mode(TEXT("npc")).Condition(TEXT("Fn.g.Check($placeholder)")).To(TEXT("flow:end")));
			AddExpectedMessage(TEXT("unfilled template"), ELogVerbosity::Warning);
			H->Start(TEXT("c1"));
			TestEqualSensitive("text", H->LastLine().Line, FString(TEXT("Shown")));
		});

		It("treats an explicit JSON null input_pin as no condition", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Shown")).Mode(TEXT("npc")).Condition(nullptr).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TestEqualSensitive("text", H->LastLine().Line, FString(TEXT("Shown")));
		});
	});

	Describe("player and non-player lines", [this]()
	{
		It("offers a player line reached on its own as a choice of one", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("I speak")).Mode(TEXT("pc")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TestEqual("no lines", H->Events->Lines.Num(), 0);
			TestEqual("one choice", H->Choices().Num(), 1);
			TestEqualSensitive("text", H->Choices()[0].Line, FString(TEXT("I speak")));
			TestTrue("state", H->State() == ETarinoiDialogueState::PcChoice);
		});

		It("lets line_mode override the speaking entity", [this]()
		{
			H->SeedEntity(TEXT("hero"), true).Configure();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Spoken at me")).Mode(TEXT("npc")).Entity(TEXT("hero")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TestEqual("npc mode wins", H->Events->Lines.Num(), 1);
		});

		It("lets the entity decide when the mode is inherited", [this]()
		{
			H->SeedEntity(TEXT("hero"), true).SeedEntity(TEXT("narrator"), false).Configure();
			H->Store->Add(TEXT("hero_line"), FCard::Line(TEXT("Mine")).Mode(TEXT("inherit")).Entity(TEXT("hero")).To(TEXT("flow:end")));
			H->Store->Add(TEXT("npc_line"), FCard::Line(TEXT("Theirs")).Mode(TEXT("inherit")).Entity(TEXT("narrator")).To(TEXT("flow:end")));
			H->Start(TEXT("hero_line"));
			TestEqual("player entity yields a choice", H->Choices().Num(), 1);
			H->Start(TEXT("npc_line"));
			TestEqual("non-player entity yields a line", H->Events->Lines.Num(), 1);
		});

		It("treats an unknown entity as non-player", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Hi")).Mode(TEXT("inherit")).Entity(TEXT("missing")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TestEqual("a line", H->Events->Lines.Num(), 1);
		});

		It("shows only the first of several NPC lines whose gates pass", [this]()
		{
			// Not a menu: the author picks the line by condition, and the topmost match is heard.
			H->Configure();
			H->BindSpy();
			H->Store->Add(TEXT("c1"), FCard::Blank().To({TEXT("a"), TEXT("b"), TEXT("c")}));
			H->Store->Add(TEXT("a"), FCard::Line(TEXT("You told me Godot")).Mode(TEXT("npc")).Geo(0).Condition(TEXT("Fn.g.True()")).To(TEXT("flow:end")));
			H->Store->Add(TEXT("b"), FCard::Line(TEXT("You have not visited")).Mode(TEXT("npc")).Geo(10).Condition(TEXT("Fn.g.True()")).To(TEXT("flow:end")));
			H->Store->Add(TEXT("c"), FCard::Line(TEXT("Fallback")).Mode(TEXT("npc")).Geo(20).To(TEXT("flow:end")));
			AddExpectedMessage(TEXT("sharing the same condition"), ELogVerbosity::Warning);
			H->Start(TEXT("c1"));
			TestEqual("no choice UI", H->Events->ChoiceSets.Num(), 0);
			TestEqual("one line", H->Events->Lines.Num(), 1);
			TestEqualSensitive("text", H->LastLine().Line, FString(TEXT("You told me Godot")));
			TestTrue("state", H->State() == ETarinoiDialogueState::NpcLine);
		});

		It("skips a failing NPC line to the next match", [this]()
		{
			H->Configure();
			H->BindSpy();
			H->Store->Add(TEXT("c1"), FCard::Blank().To({TEXT("a"), TEXT("b")}));
			H->Store->Add(TEXT("a"), FCard::Line(TEXT("Gated")).Mode(TEXT("npc")).Geo(0).Condition(TEXT("Fn.g.False()")).To(TEXT("flow:end")));
			H->Store->Add(TEXT("b"), FCard::Line(TEXT("Fallback")).Mode(TEXT("npc")).Geo(10).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TestEqualSensitive("text", H->LastLine().Line, FString(TEXT("Fallback")));
		});

		It("decides an inherited-mode set by its speaker", [this]()
		{
			H->SeedEntity(TEXT("narrator"), false).Configure();
			H->Store->Add(TEXT("c1"), FCard::Blank().To({TEXT("a"), TEXT("b")}));
			H->Store->Add(TEXT("a"), FCard::Line(TEXT("First")).Mode(TEXT("inherit")).Entity(TEXT("narrator")).Geo(0).To(TEXT("flow:end")));
			H->Store->Add(TEXT("b"), FCard::Line(TEXT("Second")).Mode(TEXT("inherit")).Entity(TEXT("narrator")).Geo(10).To(TEXT("flow:end")));
			AddExpectedMessage(TEXT("sharing the same condition"), ELogVerbosity::Warning);
			H->Start(TEXT("c1"));
			TestEqual("no choice", H->Events->ChoiceSets.Num(), 0);
			TestEqualSensitive("text", H->LastLine().Line, FString(TEXT("First")));
		});

		It("offers the player lines of a mixed set and drops the NPC lines", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Blank().To({TEXT("a"), TEXT("b"), TEXT("c")}));
			H->Store->Add(TEXT("a"), FCard::Line(TEXT("Say this")).Mode(TEXT("pc")).Geo(0));
			H->Store->Add(TEXT("b"), FCard::Line(TEXT("Narration")).Mode(TEXT("npc")).Geo(10));
			H->Store->Add(TEXT("c"), FCard::Line(TEXT("Or this")).Mode(TEXT("pc")).Geo(20));
			AddExpectedMessage(TEXT("both player and non-player"), ELogVerbosity::Warning);
			H->Start(TEXT("c1"));
			TarinoiTest::Strings(*this, TEXT("offered"), H->ChoiceLines(), {TEXT("Say this"), TEXT("Or this")});
			TestEqual("contiguous indices", H->Choices()[1].Index, 1);
		});
	});

	Describe("card functions", [this]()
	{
		It("runs an NPC line's functions when it is shown", [this]()
		{
			H->Configure();
			UTarinoiSpyFunctions* Spy = H->BindSpy();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Hi")).Mode(TEXT("npc")).Data(TEXT("effect"), TEXT("Fn.g.Effect()")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TarinoiTest::Strings(*this, TEXT("calls"), Spy->Calls, {TEXT("Effect")});
		});

		It("never runs functions on options the player does not take", [this]()
		{
			H->Configure();
			UTarinoiSpyFunctions* Spy = H->BindSpy();
			H->Store->Add(TEXT("c1"), FCard::Blank().To({TEXT("taken"), TEXT("ignored")}));
			H->Store->Add(TEXT("taken"), FCard::Line(TEXT("Taken")).Mode(TEXT("pc")).Geo(0).Data(TEXT("effect"), TEXT("Fn.g.First()")).To(TEXT("flow:end")));
			H->Store->Add(TEXT("ignored"), FCard::Line(TEXT("Ignored")).Mode(TEXT("pc")).Geo(10).Data(TEXT("effect"), TEXT("Fn.g.Second()")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TestEqual("offering has no side effects", Spy->Calls.Num(), 0);
			H->Select(0);
			TarinoiTest::Strings(*this, TEXT("calls"), Spy->Calls, {TEXT("First")});
		});

		It("runs functions in the order props declares", [this]()
		{
			H->Configure();
			UTarinoiSpyFunctions* Spy = H->BindSpy();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Hi")).Mode(TEXT("npc"))
				.Data(TEXT("second_prop"), TEXT("Fn.g.Second()")).Data(TEXT("first_prop"), TEXT("Fn.g.First()"))
				.Props({TEXT("first_prop"), TEXT("second_prop")}).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TarinoiTest::Strings(*this, TEXT("calls"), Spy->Calls, {TEXT("First"), TEXT("Second")});
		});

		It("runs functions props does not list last", [this]()
		{
			H->Configure();
			UTarinoiSpyFunctions* Spy = H->BindSpy();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Hi")).Mode(TEXT("npc"))
				.Data(TEXT("undeclared"), TEXT("Fn.g.Second()")).Data(TEXT("declared"), TEXT("Fn.g.First()"))
				.Props({TEXT("declared")}).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TarinoiTest::Strings(*this, TEXT("calls"), Spy->Calls, {TEXT("First"), TEXT("Second")});
		});

		It("skips a function with an unfilled template", [this]()
		{
			H->Configure();
			UTarinoiSpyFunctions* Spy = H->BindSpy();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Hi")).Mode(TEXT("npc")).Data(TEXT("effect"), TEXT("Fn.g.Effect($arg)")).To(TEXT("flow:end")));
			AddExpectedMessage(TEXT("unfilled template"), ELogVerbosity::Warning);
			H->Start(TEXT("c1"));
			TestEqual("not called", Spy->Calls.Num(), 0);
		});

		It("reports an unbound function and still shows the line", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Hi")).Mode(TEXT("npc")).Data(TEXT("effect"), TEXT("Fn.nobody.Missing()")).To(TEXT("flow:end")));
			AddExpectedError(TEXT("which is not bound"));
			H->Start(TEXT("c1"));
			TestEqualSensitive("line kept", H->LastLine().Line, FString(TEXT("Hi")));
		});

		It("leaves non-function data alone, and exposes it on the line", [this]()
		{
			H->Configure();
			H->BindSpy();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Hi")).Mode(TEXT("npc")).Data(TEXT("mood"), TEXT("happy")).Data(TEXT("count"), 3).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TestEqualSensitive("mood", H->LastLine().Data.FindRef(TEXT("mood")), FString(TEXT("happy")));
			TestEqualSensitive("count", H->LastLine().Data.FindRef(TEXT("count")), FString(TEXT("3")));
		});
	});

	Describe("named pins and output selectors", [this]()
	{
		It("routes by the selector's pin", [this]()
		{
			H->Configure();
			H->BindSpy()->PinToReturn = TEXT("success");
			H->Store->Add(TEXT("c1"), FCard::Blank().Selector(TEXT("Fn.g.PickPin()")).Connect({TEXT("success>>good"), TEXT("failure>>bad")}));
			H->Store->Add(TEXT("good"), FCard::Line(TEXT("Succeeded")).Mode(TEXT("npc")).To(TEXT("flow:end")));
			H->Store->Add(TEXT("bad"), FCard::Line(TEXT("Failed")).Mode(TEXT("npc")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TestEqualSensitive("text", H->LastLine().Line, FString(TEXT("Succeeded")));
		});

		It("stalls rather than guessing when the selector names an unknown pin", [this]()
		{
			H->Configure();
			H->BindSpy()->PinToReturn = TEXT("nonexistent");
			H->Store->Add(TEXT("c1"), FCard::Blank().Selector(TEXT("Fn.g.PickPin()")).Connect({TEXT("success>>good"), TEXT("failure>>bad")}));
			AddExpectedError(TEXT("has no pin 'nonexistent'"));
			H->Start(TEXT("c1"));
			TestEqual("errors", H->Events->Errors.Num(), 1);
			TestEqual("stalling is not ending", H->Events->EndedCount, 0);
			TestEqual("no lines", H->Events->Lines.Num(), 0);
		});

		It("falls back to picking by hand when the selector is not bound", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Blank().Selector(TEXT("Fn.nobody.Missing()")).Connect({TEXT("a>>x"), TEXT("b>>y")}));
			AddExpectedError(TEXT("not bound"));
			H->Start(TEXT("c1"));
			TestTrue("awaiting pin", H->State() == ETarinoiDialogueState::AwaitingPin);
			if (TestEqual("asked once", H->Events->PinRequests.Num(), 1))
			{
				TarinoiTest::Strings(*this, TEXT("pins"), H->Events->PinRequests[0], {TEXT("a"), TEXT("b")});
			}
		});

		It("falls back to picking by hand when the selector has an unfilled template", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Blank().Selector(TEXT("Fn.g.Pick($arg)")).Connect({TEXT("a>>x"), TEXT("b>>y")}));
			AddExpectedMessage(TEXT("unfilled template"), ELogVerbosity::Warning);
			H->Start(TEXT("c1"));
			TestTrue("awaiting pin", H->State() == ETarinoiDialogueState::AwaitingPin);
		});

		It("asks for the pin when there is no selector", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Blank().Connect({TEXT("a>>x"), TEXT("b>>y")}));
			H->Start(TEXT("c1"));
			TestTrue("awaiting pin", H->State() == ETarinoiDialogueState::AwaitingPin);
		});

		It("follows a pin picked by hand", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Blank().Connect({TEXT("a>>x"), TEXT("b>>y")}));
			H->Store->Add(TEXT("y"), FCard::Line(TEXT("Down path b")).Mode(TEXT("npc")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			H->SelectPin(TEXT("b"));
			TestEqualSensitive("text", H->LastLine().Line, FString(TEXT("Down path b")));
		});

		It("reports picking a pin that does not exist", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Blank().Connect({TEXT("a>>x")}));
			H->Start(TEXT("c1"));
			AddExpectedError(TEXT("no pin named 'zzz'"));
			H->SelectPin(TEXT("zzz"));
			TestEqual("errors", H->Events->Errors.Num(), 1);
		});

		It("keeps the first target of a duplicated pin", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Blank().Connect({TEXT("a>>first"), TEXT("a>>second")}));
			H->Store->Add(TEXT("first"), FCard::Line(TEXT("First")).Mode(TEXT("npc")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			H->SelectPin(TEXT("a"));
			TestEqualSensitive("text", H->LastLine().Line, FString(TEXT("First")));
		});
	});

	Describe("system lines", [this]()
	{
		It("shows a posted system line before the routed card", [this]()
		{
			H->Configure();
			UTarinoiSystemLineFunctions* Functions = NewObject<UTarinoiSystemLineFunctions>();
			Functions->Runtime = H->Runtime.Get();
			H->Runtime->GetBindings()->BindFunctions(TEXT("g"), Functions);
			H->Store->Add(TEXT("c1"), FCard::Blank().Selector(TEXT("Fn.g.CheckSkill()")).Connect({TEXT("success>>good")}));
			H->Store->Add(TEXT("good"), FCard::Line(TEXT("You made it")).Mode(TEXT("npc")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TestEqual("one line", H->Events->Lines.Num(), 1);
			TestTrue("system", H->LastLine().bIsSystem);
			TestEqualSensitive("text", H->LastLine().Line, FString(TEXT("You rolled well.")));
			TestEqualSensitive("mode", H->LastLine().LineMode, FString(TEXT("system")));
			H->Advance();
			TestEqualSensitive("routed", H->LastLine().Line, FString(TEXT("You made it")));
			TestFalse("not system", H->LastLine().bIsSystem);
		});

		It("shows only the first posted system line", [this]()
		{
			H->Configure();
			UTarinoiSystemLineFunctions* Functions = NewObject<UTarinoiSystemLineFunctions>();
			Functions->Runtime = H->Runtime.Get();
			Functions->ExtraLines = {TEXT("Second"), TEXT("Third")};
			H->Runtime->GetBindings()->BindFunctions(TEXT("g"), Functions);
			H->Store->Add(TEXT("c1"), FCard::Blank().Selector(TEXT("Fn.g.CheckSkill()")).Connect({TEXT("success>>good")}));
			H->Store->Add(TEXT("good"), FCard::Line(TEXT("Arrived")).Mode(TEXT("npc")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TestEqualSensitive("first", H->LastLine().Line, FString(TEXT("You rolled well.")));
			H->Advance();
			TestEqualSensitive("extras discarded", H->LastLine().Line, FString(TEXT("Arrived")));
		});
	});

	Describe("loop detection", [this]()
	{
		It("breaks a loop that never reaches the player", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("a"), FCard::Blank().To(TEXT("b")));
			H->Store->Add(TEXT("b"), FCard::Blank().To(TEXT("a")));
			AddExpectedError(TEXT("loops back"));
			H->Start(TEXT("a"));
			TestEqual("errors", H->Events->Errors.Num(), 1);
		});

		It("allows returning to a card across turns", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("a"), FCard::Line(TEXT("Again?")).Mode(TEXT("npc")).To(TEXT("b")));
			H->Store->Add(TEXT("b"), FCard::Line(TEXT("Yes")).Mode(TEXT("npc")).To(TEXT("a")));
			H->Start(TEXT("a"));
			H->Advance();
			H->Advance();
			TestEqual("three lines", H->Events->Lines.Num(), 3);
			TestEqualSensitive("back at the start", H->LastLine().Line, FString(TEXT("Again?")));
			TestEqual("no errors", H->Events->Errors.Num(), 0);
		});
	});

	Describe("input guards", [this]()
	{
		It("ignores Advance when nothing is shown", [this]()
		{
			H->Configure();
			AddExpectedMessage(TEXT("nothing to advance past"), ELogVerbosity::Warning);
			H->Advance();
			TestEqual("no lines", H->Events->Lines.Num(), 0);
		});

		It("ignores SelectChoice when no choices are open", [this]()
		{
			H->Configure();
			AddExpectedMessage(TEXT("no choices are open"), ELogVerbosity::Warning);
			H->Select(0);
			TestEqual("nothing chosen", H->Events->ChoicesMade.Num(), 0);
		});

		for (const int32 Index : {-1, 99})
		{
			It(FString::Printf(TEXT("ignores an out-of-range choice (%d)"), Index), [this, Index]()
			{
				H->Configure();
				H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Only option")).Mode(TEXT("pc")).To(TEXT("flow:end")));
				H->Start(TEXT("c1"));
				AddExpectedMessage(TEXT("out of range"), ELogVerbosity::Warning);
				H->Select(Index);
				TestEqual("nothing chosen", H->Events->ChoicesMade.Num(), 0);
				TestTrue("choice stays open", H->State() == ETarinoiDialogueState::PcChoice);
			});
		}

		It("ignores SelectPin when not waiting for one", [this]()
		{
			H->Configure();
			AddExpectedMessage(TEXT("not waiting for a pin"), ELogVerbosity::Warning);
			H->SelectPin(TEXT("a"));
		});
	});

	Describe("visited-choice history", [this]()
	{
		It("does not mark choices visited without a history store", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Option")).Mode(TEXT("pc")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TestFalse("not visited", H->Choices()[0].bVisited);
		});

		It("marks previously chosen options visited", [this]()
		{
			H->Configure();
			H->Runtime->SetHistoryStore(NewObject<UTarinoiInMemoryHistoryStore>());
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Option")).Mode(TEXT("pc")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			TestFalse("not seen yet", H->Choices()[0].bVisited);
			H->Select(0);
			H->Start(TEXT("c1"));
			TestTrue("seen on a second visit", H->Choices()[0].bVisited);
		});

		It("saves visited choices when the dialogue ends", [this]()
		{
			UTarinoiInMemoryHistoryStore* History = NewObject<UTarinoiInMemoryHistoryStore>();
			H->Configure();
			H->Runtime->SetHistoryStore(History);
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Option")).Mode(TEXT("pc")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			H->Select(0);
			TestTrue("saved", ITarinoiHistoryStore::Execute_GetVisited(History, TEXT("c1")).Contains(TEXT("c1")));
		});

		It("saves visited choices on abort too", [this]()
		{
			UTarinoiInMemoryHistoryStore* History = NewObject<UTarinoiInMemoryHistoryStore>();
			H->Configure();
			H->Runtime->SetHistoryStore(History);
			H->Store->Add(TEXT("c1"), FCard::Line(TEXT("Option")).Mode(TEXT("pc")).To(TEXT("c2")));
			H->Store->Add(TEXT("c2"), FCard::Line(TEXT("Next")).Mode(TEXT("npc")).To(TEXT("flow:end")));
			H->Start(TEXT("c1"));
			H->Select(0);
			H->Runtime->AbortDialogue();
			TestTrue("saved", ITarinoiHistoryStore::Execute_GetVisited(History, TEXT("c1")).Contains(TEXT("c1")));
		});
	});

	Describe("shown_once", [this]()
	{
		It("drops a shown_once option once the player takes it", [this]()
		{
			H->Configure();
			SeedShownOnceHub();
			H->Start(TEXT("hub"));
			TarinoiTest::Strings(*this, TEXT("first time"), H->ChoiceLines(), {TEXT("Once only"), TEXT("Again"), TEXT("And again")});
			H->Select(0);
			TarinoiTest::Strings(*this, TEXT("spent option filtered"), H->ChoiceLines(), {TEXT("Again"), TEXT("And again")});
		});

		It("remembers shown_once across dialogues through the history store", [this]()
		{
			H->Configure();
			H->Runtime->SetHistoryStore(NewObject<UTarinoiInMemoryHistoryStore>());
			SeedShownOnceHub(TEXT("flow:end"));
			H->Start(TEXT("hub"));
			H->Select(0);
			H->Start(TEXT("hub"));
			TarinoiTest::Strings(*this, TEXT("still filtered"), H->ChoiceLines(), {TEXT("Again"), TEXT("And again")});
		});

		It("forgets shown_once between dialogues without a history store", [this]()
		{
			H->Configure();
			SeedShownOnceHub(TEXT("flow:end"));
			H->Start(TEXT("hub"));
			H->Select(0);
			H->Start(TEXT("hub"));
			TarinoiTest::Strings(*this, TEXT("offered again"), H->ChoiceLines(), {TEXT("Once only"), TEXT("Again"), TEXT("And again")});
		});

		It("counts an NPC line as seen from the moment it is displayed", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("start"), FCard::Blank().To(TEXT("n1")));
			H->Store->Add(TEXT("n1"), FCard::Line(TEXT("Greeting")).Mode(TEXT("npc")).Geo(0).ShownOnce().To(TEXT("hub")));
			H->Store->Add(TEXT("hub"), FCard::Blank().To({TEXT("n1"), TEXT("n2"), TEXT("n3")}));
			H->Store->Add(TEXT("n2"), FCard::Line(TEXT("Small talk")).Mode(TEXT("npc")).Geo(10).To(TEXT("flow:end")));
			H->Store->Add(TEXT("n3"), FCard::Line(TEXT("More small talk")).Mode(TEXT("npc")).Geo(20).To(TEXT("flow:end")));
			H->Start(TEXT("start"));
			TestEqualSensitive("greeting", H->LastLine().Line, FString(TEXT("Greeting")));
			AddExpectedMessage(TEXT("sharing the same condition"), ELogVerbosity::Warning);
			H->Advance();
			TestEqual("no choice", H->Events->ChoiceSets.Num(), 0);
			TestEqualSensitive("spent greeting skipped", H->LastLine().Line, FString(TEXT("Small talk")));
		});

		It("ends the dialogue at a spent shown_once card reached on its own", [this]()
		{
			H->Configure();
			H->Runtime->SetHistoryStore(NewObject<UTarinoiInMemoryHistoryStore>());
			H->Store->Add(TEXT("s1"), FCard::Blank().To(TEXT("a")));
			H->Store->Add(TEXT("a"), FCard::Line(TEXT("Only way through")).Mode(TEXT("npc")).ShownOnce().To(TEXT("flow:end")));
			H->Start(TEXT("s1"));
			TestEqualSensitive("shown the first time", H->LastLine().Line, FString(TEXT("Only way through")));
			H->Advance();
			H->Events->Lines.Reset();
			AddExpectedError(TEXT("nothing else continues from here"));
			H->Start(TEXT("s1"));
			TestEqual("not shown again", H->Events->Lines.Num(), 0);
			TestEqual("ended twice", H->Events->EndedCount, 2);
		});

		It("ends the dialogue when every candidate is a spent shown_once card", [this]()
		{
			H->Configure();
			H->Store->Add(TEXT("hub"), FCard::Blank().To({TEXT("a"), TEXT("b")}));
			H->Store->Add(TEXT("a"), FCard::Line(TEXT("First")).Mode(TEXT("pc")).Geo(0).ShownOnce().To(TEXT("hub")));
			H->Store->Add(TEXT("b"), FCard::Line(TEXT("Second")).Mode(TEXT("pc")).Geo(10).ShownOnce().To(TEXT("hub")));
			H->Start(TEXT("hub"));
			H->Select(0);
			AddExpectedError(TEXT("already seen"));
			H->Select(0);
			TestEqual("ends rather than hangs", H->Events->EndedCount, 1);
		});
	});

	Describe("start cards and caches", [this]()
	{
		It("groups start cards by collection label", [this]()
		{
			H->SeedCollection(TEXT("col1"), TEXT("Zebra chapter")).SeedCollection(TEXT("col2"), TEXT("Alpha chapter")).Configure();
			H->Store->StartCards.Add({TEXT("s1"), TEXT("col1"), TEXT("In Zebra")});
			H->Store->StartCards.Add({TEXT("s2"), TEXT("col2"), TEXT("In Alpha")});
			const TArray<FTarinoiStartCard> Cards = H->Runtime->GetStartCards();
			if (TestEqual("two", Cards.Num(), 2))
			{
				TestEqualSensitive("alpha first", Cards[0].CollectionLabel, FString(TEXT("Alpha chapter")));
				TestEqualSensitive("zebra second", Cards[1].CollectionLabel, FString(TEXT("Zebra chapter")));
				TestTrue("label", Cards[0].Label.Contains(TEXT("In Alpha")));
				TestTrue("the card id disambiguates", Cards[0].Label.Contains(TEXT("s2")));
			}
		});

		It("gives an unlabelled start card a default label", [this]()
		{
			H->Configure();
			H->Store->StartCards.Add({TEXT("s1"), TEXT("col1"), FString()});
			TestTrue("default", H->Runtime->GetStartCards()[0].Label.StartsWith(TEXT("Start")));
		});

		It("loads entities from the database into the cache", [this]()
		{
			H->SeedEntity(TEXT("narrator"), false, TEXT("The Narrator")).Configure();
			TestEqualSensitive("label", TarinoiJson::Str(H->Runtime->GetEntityPayload(TEXT("narrator")), TEXT("label")), FString(TEXT("The Narrator")));
			TestFalse("unknown", H->Runtime->GetEntityPayload(TEXT("nobody")).IsValid());
		});

		It("evaluates expressions against the bindings", [this]()
		{
			H->Configure();
			H->BindSpy();
			TestTrue("true", H->Runtime->EvalExpression(TEXT("Fn.g.True()")).IsTruthy());
		});

		It("resolves list references against manifests shaped like real synced content", [this]()
		{
			// Real synced manifests carry the Ls.* name only in the identifier column, never in the
			// payload. A port that only read the payload silently lost every list, so skill-check
			// thresholds resolved to nothing and, downstream, to zero.
			H->SeedCollectionByIdentifier(TEXT("lists-col"), TEXT("global"), TEXT("Global lists"))
				.SeedListSpec(TEXT("lists-col"), TEXT("thresholds"), {{TEXT("easy"), 3}, {TEXT("heroic"), 9}})
				.Configure();
			TestEqual("heroic", H->Runtime->EvalExpression(TEXT("Ls.global.thresholds.heroic")).ToNumber(), 9.0);
			TestEqual("easy", H->Runtime->EvalExpression(TEXT("Ls.global.thresholds.easy")).ToNumber(), 3.0);
		});
	});

	Describe("configuration and sync", [this]()
	{
		It("refuses to configure without a project", [this]()
		{
			H->Settings->ApiPath = FString();
			AddExpectedError(TEXT("cannot work out which project"));
			TestFalse("configured", H->Runtime->ConfigureWith(*H->Settings));
		});

		It("refuses to configure offline without a snapshot", [this]()
		{
			H->Settings->bOfflineMode = true;
			AddExpectedError(TEXT("no snapshot"));
			TestFalse("configured", H->Runtime->ConfigureWith(*H->Settings));
		});

		Describe("through a scripted server", [this]()
		{
			BeforeEach([this]()
			{
				FTarinoiCredentials::SetFilePathOverride(FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("Tarinoi"), H->ProjectId, TEXT("credentials")));
				FTarinoiCredentials::Write(FTarinoiCredentials::ApiKeyName, TEXT("test-token"));
			});

			AfterEach([this]()
			{
				IFileManager::Get().Delete(*FTarinoiCredentials::FilePath());
				FTarinoiCredentials::SetFilePathOverride(FString());
			});

			It("raises the sync events and refreshes the caches", [this]()
			{
				H->Configure();
				const TSharedRef<FTarinoiDeferredTransport> Server = MakeShared<FTarinoiDeferredTransport>();
				H->Runtime->SetHttpTransport(Server);

				H->Runtime->Sync();
				TestEqual("started", H->Events->SyncStartedCount, 1);
				TestTrue("syncing", H->Runtime->IsSyncing());

				Server->Complete(200, FString::Printf(
					TEXT("{\"document_id\":\"hero\",\"collection_id\":\"col1\",\"layer_id\":\"%s\",\"document_type\":\"entity\",")
					TEXT("\"identifier\":\"hero\",\"update_key\":1,\"data_version\":\"2.0.0\",\"payload\":{\"label\":\"Hero\"}}"),
					TarinoiLayerFilter::MainLayer));

				TestFalse("finished", H->Runtime->IsSyncing());
				if (TestEqual("completed", H->Events->SyncsCompleted.Num(), 1))
				{
					TestEqual("upserted", H->Events->SyncsCompleted[0].DocumentsUpserted, 1);
				}
				TestEqualSensitive("entity cache refreshed", TarinoiJson::Str(H->Runtime->GetEntityPayload(TEXT("hero")), TEXT("label")), FString(TEXT("Hero")));
				TestEqualSensitive("sent the stored token", Server->Headers.FindRef(TEXT("Authorization")), FString(TEXT("Bearer test-token")));
			});

			It("ignores a second sync while one is running", [this]()
			{
				H->Configure();
				const TSharedRef<FTarinoiDeferredTransport> Server = MakeShared<FTarinoiDeferredTransport>();
				H->Runtime->SetHttpTransport(Server);

				H->Runtime->Sync();
				H->Runtime->Sync();
				TestEqual("one request", Server->Requests, 1);
				TestEqual("one start", H->Events->SyncStartedCount, 1);
				Server->Complete(200, FString());
				TestEqual("one completion", H->Events->SyncsCompleted.Num(), 1);
			});

			It("reports a failed sync", [this]()
			{
				H->Configure();
				const TSharedRef<FTarinoiDeferredTransport> Server = MakeShared<FTarinoiDeferredTransport>();
				H->Runtime->SetHttpTransport(Server);
				AddExpectedError(TEXT("credentials rejected"));
				H->Runtime->Sync();
				Server->Complete(401, FString());
				if (TestEqual("failed", H->Events->SyncsFailed.Num(), 1))
				{
					TestTrue("actionable", H->Events->SyncsFailed[0].Contains(TEXT("API token")));
				}
			});
		});
	});
}

#endif
