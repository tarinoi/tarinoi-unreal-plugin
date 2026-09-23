// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Data/TarinoiDocumentRow.h"
#include "Dom/JsonObject.h"
#include "UObject/Object.h"

#include "TarinoiDocumentStore.generated.h"

class FTarinoiDatabase;

/**
 * Supplies entity payloads that are already cached in memory. The runtime implements this; the
 * document store depends on the interface rather than the runtime, which keeps the data layer
 * testable on its own.
 */
class ITarinoiEntityCache
{
public:
	virtual ~ITarinoiEntityCache() = default;

	/** The entity payload for an identifier, or null if unknown. */
	virtual TSharedPtr<FJsonObject> GetEntityPayload(const FString& Identifier) const = 0;
};

/**
 * Reads dialogue content for the runtime, and the seam for replacing where it comes from.
 *
 * Subclass this to serve content from somewhere else (a pre-baked cache, your own asset format,
 * a test double) and select your class in Project Settings > Plugins > Tarinoi > Document Store
 * Class. The runtime creates one instance and calls Setup once the database is open.
 *
 * The methods are synchronous. The built-in store answers from a local file well inside a frame,
 * and the engine's SQLite build only allows the one game-thread connection anyway, so there is
 * nothing to gain from making traversal asynchronous. A store that needs slow I/O should do it
 * ahead of time, at Setup or on sync, and answer from memory.
 *
 * Implementations must not throw or assert on missing content. Return null or an empty array;
 * the runtime treats absence as a recoverable authoring problem.
 */
UCLASS(Abstract)
class TARINOI_API UTarinoiDocumentStore : public UObject
{
	GENERATED_BODY()

public:
	/** Called once the database is open, before any content is requested. */
	virtual void Setup(const TSharedPtr<FTarinoiDatabase>& InDatabase, const ITarinoiEntityCache* InEntities);

	/**
	 * A document's parsed payload, or null. Pass a collection id to disambiguate when the same
	 * document id appears in more than one collection.
	 */
	virtual TSharedPtr<FJsonObject> GetDocument(const FString& DocumentId, const FString& CollectionId = FString())
		PURE_VIRTUAL(UTarinoiDocumentStore::GetDocument, return nullptr;);

	/** A dialogue card's parsed payload, or null. */
	virtual TSharedPtr<FJsonObject> LoadCard(const FString& CollectionId, const FString& CardId)
		PURE_VIRTUAL(UTarinoiDocumentStore::LoadCard, return nullptr;);

	/**
	 * Finds a card by document id alone. A jump's data.target is a bare document id and may point
	 * at another board, so the collection has to be looked up. Returns false when not found.
	 */
	virtual bool LocateCard(const FString& CardId, FString& OutCollectionId, TSharedPtr<FJsonObject>& OutCard)
		PURE_VIRTUAL(UTarinoiDocumentStore::LocateCard, return false;);

	/** An entity's payload by identifier, or null. */
	virtual TSharedPtr<FJsonObject> GetEntity(const FString& Identifier);

	/** Every dialogue entry point currently visible. */
	virtual TArray<FTarinoiStartCardRow> QueryStartCards()
		PURE_VIRTUAL(UTarinoiDocumentStore::QueryStartCards, return {};);

protected:
	TSharedPtr<FTarinoiDatabase> Database;
	const ITarinoiEntityCache* Entities = nullptr;
};

/**
 * The default document store: reads straight from the local SQLite database, with the layer
 * merge applied in SQL.
 */
UCLASS()
class TARINOI_API UTarinoiSqliteDocumentStore : public UTarinoiDocumentStore
{
	GENERATED_BODY()

public:
	virtual TSharedPtr<FJsonObject> GetDocument(const FString& DocumentId, const FString& CollectionId = FString()) override;
	virtual TSharedPtr<FJsonObject> LoadCard(const FString& CollectionId, const FString& CardId) override;
	virtual bool LocateCard(const FString& CardId, FString& OutCollectionId, TSharedPtr<FJsonObject>& OutCard) override;
	virtual TArray<FTarinoiStartCardRow> QueryStartCards() override;

protected:
	bool IsUsable() const;

	/**
	 * Parses a stored payload. Malformed JSON is logged and treated as missing, so one corrupt
	 * document cannot stop a dialogue.
	 */
	static TSharedPtr<FJsonObject> ParsePayload(const FString& Payload, const FString& DocumentId);
};
