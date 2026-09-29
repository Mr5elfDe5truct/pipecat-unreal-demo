//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "PipecatCompanion.generated.h"

class SPipecatSubtitles;
class UMaterialInstanceDynamic;
class UPipecatVoiceComponent;
class UPointLightComponent;
class UStaticMeshComponent;

// A glowing orb that follows the player around. It's a Pipecat bot: you talk
// to it, and its voice comes from where it is.
UCLASS()
class APipecatCompanion : public AActor
{
	GENERATED_BODY()

public:
	APipecatCompanion();

	UPROPERTY(VisibleAnywhere, Category = "Pipecat")
	TObjectPtr<UStaticMeshComponent> Orb;

	UPROPERTY(VisibleAnywhere, Category = "Pipecat")
	TObjectPtr<UPointLightComponent> Glow;

	UPROPERTY(VisibleAnywhere, Category = "Pipecat")
	TObjectPtr<UPipecatVoiceComponent> Voice;

	/** Sent to the bot as soon as it's ready, e.g. to start the conversation. */
	UPROPERTY(EditAnywhere, Category = "Pipecat")
	FString FirstMessage;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleBotReady();

	UFUNCTION()
	void HandleDisconnected();

	UFUNCTION()
	void HandleError(const FString& Error);

	UFUNCTION()
	void HandleUserStartedSpeaking();

	UFUNCTION()
	void HandleUserStoppedSpeaking();

	UFUNCTION()
	void HandleUserTranscript(const FString& Text, bool bFinal);

	UFUNCTION()
	void HandleBotStartedSpeaking();

	UFUNCTION()
	void HandleBotStoppedSpeaking();

	UFUNCTION()
	void HandleBotInterrupted();

	UFUNCTION()
	void HandleBotOutput(const FString& Text);

	void FollowPlayer(float DeltaSeconds);
	void UpdateGlow(float DeltaSeconds);

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> OrbMaterial;

	TSharedPtr<SPipecatSubtitles> Subtitles;
	FString BotText;
	bool bUserSpeaking = false;
	bool bBotSpeaking = false;
	float SmoothedLevel = 0.0f;
	float Time = 0.0f;
};
