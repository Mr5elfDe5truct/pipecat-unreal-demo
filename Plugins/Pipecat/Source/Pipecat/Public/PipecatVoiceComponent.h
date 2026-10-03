//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "PipecatVoiceComponent.generated.h"

class UAudioComponent;
class USoundWaveProcedural;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPipecatEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPipecatTextEvent, const FString&, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPipecatTranscriptEvent, const FString&, Text, bool, bFinal);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPipecatMessageEvent, const FString&, DataJson);

/**
 * Talks to a Pipecat bot with voice: starts the bot, sends it the microphone,
 * and plays its voice from the owner's position in the world.
 *
 * Events run on the game thread.
 */
UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent))
class PIPECAT_API UPipecatVoiceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPipecatVoiceComponent();

	/** The bot's start endpoint, e.g. http://localhost:7860/start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pipecat")
	FString StartUrl = TEXT("http://localhost:7860/start");

	/** Sent as a bearer token to the start endpoint, e.g. a Pipecat Cloud public API key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pipecat")
	FString ApiKey;

	/** Starts the bot and connects when play begins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pipecat")
	bool bConnectOnBeginPlay = true;

	/** Sends the microphone to the bot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pipecat")
	bool bUseMicrophone = true;

	/** How far away the bot's voice can be heard, in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pipecat")
	float VoiceFalloffDistance = 4000.0f;

	/** Starts the bot and connects to it, without waiting. */
	UFUNCTION(BlueprintCallable, Category = "Pipecat")
	void Connect();

	/** Disconnects from the bot. */
	UFUNCTION(BlueprintCallable, Category = "Pipecat")
	void Disconnect();

	/** Sends text to the bot, as if the user said it. */
	UFUNCTION(BlueprintCallable, Category = "Pipecat")
	void SendText(const FString& Text);

	/** Whether the bot is connected and ready. */
	UFUNCTION(BlueprintPure, Category = "Pipecat")
	bool IsReady() const;

	/** How loud the bot's voice is right now, from 0 to 1. */
	UFUNCTION(BlueprintPure, Category = "Pipecat")
	float GetBotVoiceLevel() const;

	/** How loud the microphone is right now, from 0 to 1. */
	UFUNCTION(BlueprintPure, Category = "Pipecat")
	float GetMicrophoneLevel() const;

	/** The bot is ready to talk. */
	UPROPERTY(BlueprintAssignable, Category = "Pipecat")
	FPipecatEvent OnBotReady;

	/** The connection ended. */
	UPROPERTY(BlueprintAssignable, Category = "Pipecat")
	FPipecatEvent OnDisconnected;

	/** Something went wrong. */
	UPROPERTY(BlueprintAssignable, Category = "Pipecat")
	FPipecatTextEvent OnError;

	UPROPERTY(BlueprintAssignable, Category = "Pipecat")
	FPipecatEvent OnUserStartedSpeaking;

	UPROPERTY(BlueprintAssignable, Category = "Pipecat")
	FPipecatEvent OnUserStoppedSpeaking;

	/** What the user said. Partial transcripts come before the final one. */
	UPROPERTY(BlueprintAssignable, Category = "Pipecat")
	FPipecatTranscriptEvent OnUserTranscript;

	UPROPERTY(BlueprintAssignable, Category = "Pipecat")
	FPipecatEvent OnBotStartedSpeaking;

	UPROPERTY(BlueprintAssignable, Category = "Pipecat")
	FPipecatEvent OnBotStoppedSpeaking;

	/** The user interrupted the bot. */
	UPROPERTY(BlueprintAssignable, Category = "Pipecat")
	FPipecatEvent OnBotInterrupted;

	/** Text the bot says, a sentence at a time, as it starts saying it. */
	UPROPERTY(BlueprintAssignable, Category = "Pipecat")
	FPipecatTextEvent OnBotOutput;

	/** A message the bot sent the game, with its data as JSON. */
	UPROPERTY(BlueprintAssignable, Category = "Pipecat")
	FPipecatMessageEvent OnServerMessage;

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY()
	TObjectPtr<UAudioComponent> VoiceAudio;

	UPROPERTY()
	TObjectPtr<USoundWaveProcedural> VoiceWave;

	TSharedPtr<class FPipecatSession> Session;
};
