//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "PipecatDemoSubsystem.generated.h"

class APipecatObjects;
class APipecatWorld;

// Builds the world, and adds the Pipecat companion to it, when play begins.
// The companion's bot owns the world, and the game builds what it sends.
UCLASS(Config = Game)
class UPipecatDemoSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The bot's start endpoint. -PipecatStartUrl=... overrides it. */
	UPROPERTY(Config)
	FString StartUrl;

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;

private:
	UFUNCTION()
	void HandleServerMessage(const FString& DataJson);

	TWeakObjectPtr<APipecatWorld> Environment;
	TWeakObjectPtr<APipecatObjects> Objects;
};
