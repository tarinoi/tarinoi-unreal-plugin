// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"

struct FTarinoiHttpResponse
{
	/** The HTTP status, or 0 when the request never got a response (DNS, TLS, timeout, ...). */
	int32 Status = 0;
	FString Body;

	/** Why the request failed without a response. Empty otherwise. */
	FString TransportError;
};

/**
 * How the importer talks to the network. The default goes through the engine's HTTP module;
 * tests substitute a scripted server so pagination, error mapping and every upsert rule can be
 * exercised with no network and no API key.
 *
 * OnComplete must be called exactly once, on the game thread. It may be called before Get returns.
 */
class TARINOI_API ITarinoiHttpTransport
{
public:
	virtual ~ITarinoiHttpTransport() = default;

	virtual void Get(const FString& Url, const TMap<FString, FString>& Headers,
		TFunction<void(const FTarinoiHttpResponse&)> OnComplete) = 0;

	/** Abandons any request in flight. Its OnComplete is not called. */
	virtual void Cancel() {}

	/** A transport over FHttpModule. */
	static TSharedRef<ITarinoiHttpTransport> CreateDefault();
};
