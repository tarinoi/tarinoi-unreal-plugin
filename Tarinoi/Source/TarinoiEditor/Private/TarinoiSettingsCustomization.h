// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

/**
 * Adds an API Token row to Project Settings > Plugins > Tarinoi: whether a token is saved, and a
 * button to set one. The token itself is not a setting (it never goes in a config file), so it
 * gets a row of its own rather than a property.
 */
class FTarinoiSettingsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance() { return MakeShared<FTarinoiSettingsCustomization>(); }

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
