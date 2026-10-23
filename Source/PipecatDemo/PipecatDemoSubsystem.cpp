//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#include "PipecatDemoSubsystem.h"

#include "PipecatCompanion.h"
#include "PipecatObjects.h"
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
	Environment = World.SpawnActor<APipecatWorld>();
	// The objects the bot puts in it, which the player can use.
	Objects = World.SpawnActor<APipecatObjects>();

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

	// The bot owns the world, and sends its settings when the game connects
	// and whenever they change.
	Companion->Voice->OnServerMessage.AddDynamic(this, &UPipecatDemoSubsystem::HandleServerMessage);

	Companion->FinishSpawning(FTransform(FVector(0.0f, 0.0f, 300.0f)));
}

void UPipecatDemoSubsystem::HandleServerMessage(const FString& DataJson)
{
	// E.g. {"type": "environment", "environment": {"time_of_day": "sunset", ...}}.
	TSharedPtr<FJsonObject> Message;
	FString Type;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(DataJson), Message) || !Message ||
		!Message->TryGetStringField(TEXT("type"), Type))
	{
		return;
	}
	const TSharedPtr<FJsonObject>* Settings = nullptr;
	if (Type == TEXT("environment") && Environment.IsValid() &&
		Message->TryGetObjectField(TEXT("environment"), Settings))
	{
		Environment->SetEnvironment(**Settings);
	}
	// E.g. {"type": "spawn", "object": "door"}, and {"type": "clear"}.
	FString Name;
	if (Type == TEXT("spawn") && Objects.IsValid() && Message->TryGetStringField(TEXT("object"), Name))
	{
		Objects->Spawn(Name);
	}
	if (Type == TEXT("clear") && Objects.IsValid())
	{
		Objects->Clear();
	}
}
