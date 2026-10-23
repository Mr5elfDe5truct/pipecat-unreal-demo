//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "PipecatObjects.generated.h"

// An object the bot can put in the world, set in the game's config.
USTRUCT()
struct FPipecatObjectKind
{
	GENERATED_BODY()

	// What the bot calls it, e.g. "door".
	UPROPERTY(Config)
	FString Name;

	// An actor's class, e.g. a Blueprint's "/Game/Path/BP_Door.BP_Door_C", or
	// a static mesh.
	UPROPERTY(Config)
	FSoftObjectPath Asset;

	UPROPERTY(Config)
	float Scale = 1.0f;
};

// The objects the bot puts in the world, in front of the player, and the
// Interaction System's manager on the player, so they can use them: F to
// interact, and the mouse wheel to pick what to do.
UCLASS(Config = Game)
class APipecatObjects : public AActor
{
	GENERATED_BODY()

public:
	APipecatObjects();

	/** What the bot can put in the world. */
	UPROPERTY(Config)
	TArray<FPipecatObjectKind> Kinds;

	/** The Interaction System's manager, which the player gets. */
	UPROPERTY(Config)
	FSoftClassPath InteractionManager;

	/** Puts an object of a kind in front of the player. */
	void Spawn(const FString& Name);

	/** Removes every object the bot put in the world. */
	void Clear();

	virtual void Tick(float DeltaSeconds) override;

private:
	// Gives the player the interaction manager, and the keys that use it, once
	// there's a player.
	bool SetUpPlayer();
	// Moves an object up or down, so it stands on the ground.
	void PutOnGround(AActor* Object);
	void CallManager(const TCHAR* Function);
	void Interact();
	void SelectUp();
	void SelectDown();

	UPROPERTY()
	TObjectPtr<UActorComponent> Manager;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> Spawned;
};
