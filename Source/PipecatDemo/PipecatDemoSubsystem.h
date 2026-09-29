//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "PipecatDemoSubsystem.generated.h"

// Adds the Pipecat companion to the level when play begins, so the level
// itself doesn't change.
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
};
