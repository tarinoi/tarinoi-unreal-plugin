// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

/** One entry point, as the Start Card dropdown lists it. */
struct FTarinoiStartOption
{
	FString CollectionId;
	FString CardId;
	FString Label;
};

/**
 * Adds a Start Card dropdown to the dialogue trigger's details: the synced entry points, by
 * collection and label, filling in both ids when one is picked. Typing the ids still works.
 */
class FTarinoiTriggerCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance() { return MakeShared<FTarinoiTriggerCustomization>(); }

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	/** The synced entry points, sorted by collection label then card label. Empty if nothing is synced. */
	static TArray<TSharedPtr<FTarinoiStartOption>> LoadStartOptions();
};
