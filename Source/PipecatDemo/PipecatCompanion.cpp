//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#include "PipecatCompanion.h"

#include "PipecatVoiceComponent.h"
#include "SPipecatSubtitles.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPipecatCompanion, Log, All);

namespace
{
const FLinearColor IdleColor(0.15f, 0.45f, 1.0f);
const FLinearColor ListeningColor(0.15f, 1.0f, 0.45f);
const FLinearColor SpeakingColor(0.65f, 0.3f, 1.0f);
} // namespace

APipecatCompanion::APipecatCompanion()
{
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	Orb = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Orb"));
	Orb->SetStaticMesh(Sphere.Object);
	Orb->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Orb->SetCastShadow(false);
	Orb->SetWorldScale3D(FVector(0.3f));
	RootComponent = Orb;

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Orb);
	Glow->SetCastShadows(false);
	Glow->SetAttenuationRadius(700.0f);

	Voice = CreateDefaultSubobject<UPipecatVoiceComponent>(TEXT("Voice"));
}

void APipecatCompanion::BeginPlay()
{
	UMaterialInterface* Emissive = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
	if (Emissive)
	{
		OrbMaterial = UMaterialInstanceDynamic::Create(Emissive, this);
		Orb->SetMaterial(0, OrbMaterial);
	}

	Voice->OnBotReady.AddDynamic(this, &APipecatCompanion::HandleBotReady);
	Voice->OnDisconnected.AddDynamic(this, &APipecatCompanion::HandleDisconnected);
	Voice->OnError.AddDynamic(this, &APipecatCompanion::HandleError);
	Voice->OnUserStartedSpeaking.AddDynamic(this, &APipecatCompanion::HandleUserStartedSpeaking);
	Voice->OnUserStoppedSpeaking.AddDynamic(this, &APipecatCompanion::HandleUserStoppedSpeaking);
	Voice->OnUserTranscript.AddDynamic(this, &APipecatCompanion::HandleUserTranscript);
	Voice->OnBotStartedSpeaking.AddDynamic(this, &APipecatCompanion::HandleBotStartedSpeaking);
	Voice->OnBotStoppedSpeaking.AddDynamic(this, &APipecatCompanion::HandleBotStoppedSpeaking);
	Voice->OnBotInterrupted.AddDynamic(this, &APipecatCompanion::HandleBotInterrupted);
	Voice->OnBotOutput.AddDynamic(this, &APipecatCompanion::HandleBotOutput);

	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Subtitles = SNew(SPipecatSubtitles);
		Viewport->AddViewportWidgetContent(Subtitles.ToSharedRef(), 10);
		Subtitles->SetStatus(TEXT("Starting the bot..."));
	}

	// Starts the voice component, which connects.
	Super::BeginPlay();
}

void APipecatCompanion::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Subtitles)
	{
		if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
		{
			Viewport->RemoveViewportWidgetContent(Subtitles.ToSharedRef());
		}
		Subtitles.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void APipecatCompanion::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;
	FollowPlayer(DeltaSeconds);
	UpdateGlow(DeltaSeconds);
}

void APipecatCompanion::FollowPlayer(float DeltaSeconds)
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player)
	{
		return;
	}

	// Floats over the player's right shoulder.
	FVector Target = Player->GetActorLocation() + Player->GetActorRightVector() * 110.0f
		+ FVector(0.0f, 0.0f, 80.0f + 10.0f * FMath::Sin(Time * 2.0f));
	FVector Location = GetActorLocation();
	if (FVector::Dist(Location, Target) > 3000.0f)
	{
		SetActorLocation(Target);
	}
	else
	{
		SetActorLocation(FMath::VInterpTo(Location, Target, DeltaSeconds, 3.0f));
	}
}

void APipecatCompanion::UpdateGlow(float DeltaSeconds)
{
	float Level = bUserSpeaking ? Voice->GetMicrophoneLevel() : Voice->GetBotVoiceLevel();
	SmoothedLevel = FMath::FInterpTo(SmoothedLevel, Level, DeltaSeconds, 14.0f);

	FLinearColor Color = bUserSpeaking ? ListeningColor : (bBotSpeaking ? SpeakingColor : IdleColor);
	float Breathing = 0.5f + 0.5f * FMath::Sin(Time * 1.5f);
	float Brightness = Voice->IsReady() ? 3.0f + Breathing + 40.0f * SmoothedLevel : 0.5f + Breathing;

	if (OrbMaterial)
	{
		OrbMaterial->SetVectorParameterValue(TEXT("Color"), Color * Brightness);
	}
	Glow->SetLightColor(Color);
	Glow->SetIntensity(1500.0f + 25000.0f * SmoothedLevel);
	Orb->SetWorldScale3D(FVector(0.3f * (1.0f + 0.6f * SmoothedLevel)));
}

void APipecatCompanion::HandleBotReady()
{
	UE_LOG(LogPipecatCompanion, Log, TEXT("Bot ready"));
	if (!FirstMessage.IsEmpty())
	{
		Voice->SendText(FirstMessage);
	}
	if (Subtitles)
	{
		Subtitles->SetStatus(TEXT("Say something! (Use headphones, so I don't hear myself.)"));
	}
}

void APipecatCompanion::HandleDisconnected()
{
	UE_LOG(LogPipecatCompanion, Log, TEXT("Disconnected"));
	if (Subtitles)
	{
		Subtitles->SetStatus(TEXT("Disconnected"));
	}
}

void APipecatCompanion::HandleError(const FString& Error)
{
	UE_LOG(LogPipecatCompanion, Warning, TEXT("Error: %s"), *Error);
	if (Subtitles)
	{
		Subtitles->SetStatus(FString::Printf(TEXT("Error: %s"), *Error));
	}
}

void APipecatCompanion::HandleUserStartedSpeaking()
{
	bUserSpeaking = true;
	BotText.Empty();
	if (Subtitles)
	{
		Subtitles->SetStatus(TEXT("Listening..."));
		Subtitles->SetBotText(BotText);
	}
}

void APipecatCompanion::HandleUserStoppedSpeaking()
{
	bUserSpeaking = false;
	if (Subtitles)
	{
		Subtitles->SetStatus(TEXT("Thinking..."));
	}
}

void APipecatCompanion::HandleUserTranscript(const FString& Text, bool bFinal)
{
	if (bFinal)
	{
		UE_LOG(LogPipecatCompanion, Log, TEXT("User: %s"), *Text);
	}
	if (Subtitles)
	{
		Subtitles->SetUserText(Text);
	}
}

void APipecatCompanion::HandleBotStartedSpeaking()
{
	UE_LOG(LogPipecatCompanion, Log, TEXT("Bot started speaking"));
	bBotSpeaking = true;
	if (Subtitles)
	{
		Subtitles->SetStatus(TEXT(""));
	}
}

void APipecatCompanion::HandleBotStoppedSpeaking()
{
	UE_LOG(LogPipecatCompanion, Log, TEXT("Bot stopped speaking"));
	bBotSpeaking = false;
}

void APipecatCompanion::HandleBotInterrupted()
{
	bBotSpeaking = false;
	BotText.Empty();
	if (Subtitles)
	{
		Subtitles->SetStatus(TEXT("Oh, sorry, go ahead."));
		Subtitles->SetBotText(BotText);
	}
}

void APipecatCompanion::HandleBotOutput(const FString& Text)
{
	UE_LOG(LogPipecatCompanion, Log, TEXT("Bot: %s"), *Text);
	BotText = BotText.IsEmpty() ? Text : BotText + TEXT(" ") + Text;
	if (Subtitles)
	{
		Subtitles->SetBotText(BotText);
	}
}
