//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#include "PipecatDemoSubsystem.h"

#include "PipecatCompanion.h"
#include "PipecatVoiceComponent.h"
#include "PipecatWorld.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogPipecatDemo, Log, All);

bool UPipecatDemoSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPipecatDemoSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);

	FString Url = StartUrl;
	FParse::Value(FCommandLine::Get(), TEXT("PipecatStartUrl="), Url);

	// E.g. a Pipecat Cloud public API key.
	FString ApiKey = FPlatformMisc::GetEnvironmentVariable(TEXT("PIPECAT_API_KEY"));
	FParse::Value(FCommandLine::Get(), TEXT("PipecatApiKey="), ApiKey);

	// The world the bot changes, which starts as an empty grid.
	APipecatWorld* Environment = World.SpawnActor<APipecatWorld>();

	APipecatCompanion* Companion = World.SpawnActorDeferred<APipecatCompanion>(
		APipecatCompanion::StaticClass(), FTransform(FVector(0.0f, 0.0f, 300.0f)));
	if (!Companion)
	{
		UE_LOG(LogPipecatDemo, Error, TEXT("Unable to add the Pipecat companion"));
		return;
	}
	UE_LOG(LogPipecatDemo, Log, TEXT("Adding the Pipecat companion, with the bot at %s"), *Url);
	if (!Url.IsEmpty())
	{
		Companion->Voice->StartUrl = Url;
	}
	Companion->Voice->ApiKey = ApiKey;
	// For trying it without talking, e.g. -PipecatNoMicrophone -PipecatSay="Hi!".
	Companion->Voice->bUseMicrophone = !FParse::Param(FCommandLine::Get(), TEXT("PipecatNoMicrophone"));
	FParse::Value(FCommandLine::Get(), TEXT("PipecatSay="), Companion->FirstMessage);

	// The bot changes the world with set_environment, e.g. with
	// {"time_of_day": "sunset"}. It keeps track of the world itself.
	TWeakObjectPtr<APipecatWorld> WeakEnvironment = Environment;
	Companion->Voice->RegisterFunction(
		TEXT("set_environment"),
		[WeakEnvironment](const FString& ArgumentsJson) {
			TSharedPtr<FJsonObject> Changes;
			if (WeakEnvironment.IsValid() &&
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ArgumentsJson), Changes) && Changes)
			{
				WeakEnvironment->SetEnvironment(*Changes);
			}
		});

	Companion->FinishSpawning(FTransform(FVector(0.0f, 0.0f, 300.0f)));
}
