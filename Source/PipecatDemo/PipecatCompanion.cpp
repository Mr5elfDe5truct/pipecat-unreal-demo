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
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPipecatCompanion, Log, All);

namespace
{
const FLinearColor IdleColor(0.15f, 0.45f, 1.0f);
const FLinearColor ListeningColor(0.15f, 1.0f, 0.45f);
const FLinearColor SpeakingColor(0.65f, 0.3f, 1.0f);
// The player's, while they speak.
const FLinearColor PlayerColor(1.0f, 0.12f, 0.08f);
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
	// Created by setup.sh.
	UMaterialInterface* GlowMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Pipecat/M_Glow.M_Glow"));
	if (GlowMaterial)
	{
		PlayerGlow = UMaterialInstanceDynamic::Create(GlowMaterial, this);
		PlayerGlow->SetVectorParameterValue(TEXT("Color"), PlayerColor);
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

	// The player glows with their voice, over whatever they look like.
	ACharacter* Player = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (PlayerGlow && Player)
	{
		float Target = bUserSpeaking ? 1.5f + 10.0f * SmoothedLevel : 0.0f;
		PlayerGlowLevel = FMath::FInterpTo(PlayerGlowLevel, Target, DeltaSeconds, 10.0f);
		PlayerGlow->SetScalarParameterValue(TEXT("Glow"), PlayerGlowLevel);
		if (Player->GetMesh()->GetOverlayMaterial() != PlayerGlow)
		{
			Player->GetMesh()->SetOverlayMaterial(PlayerGlow);
		}
	}
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
	// A new exchange, once the bot answered the last one. Until then, the
	// user is still taking their turn.
	if (!BotText.IsEmpty())
	{
		UserText.Empty();
		BotText.Empty();
	}
	if (Subtitles)
	{
		Subtitles->SetStatus(TEXT(""));
		Subtitles->SetUserText(UserText);
		Subtitles->SetBotText(BotText);
	}
}

void APipecatCompanion::HandleUserStoppedSpeaking()
{
	bUserSpeaking = false;
}

void APipecatCompanion::HandleUserTranscript(const FString& Text, bool bFinal)
{
	FString Piece = Text.TrimStartAndEnd();
	if (Piece.IsEmpty())
	{
		return;
	}
	if (bFinal)
	{
		UE_LOG(LogPipecatCompanion, Log, TEXT("User: %s"), *Piece);
	}
	// The user's turn comes in pieces: the finished ones, and the one they're
	// saying now.
	FString Turn = UserText.IsEmpty() ? Piece : UserText + TEXT(" ") + Piece;
	if (bFinal)
	{
		UserText = Turn;
	}
	if (Subtitles)
	{
		Subtitles->SetUserText(Turn);
	}
}

void APipecatCompanion::HandleBotStartedSpeaking()
{
	UE_LOG(LogPipecatCompanion, Log, TEXT("Bot started speaking"));
	bBotSpeaking = true;
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
