//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#include "PipecatWorld.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPipecatWorld, Log, All);

namespace
{
// How long a change takes to show, in seconds.
const float ChangeSeconds = 5.0f;

// The ground: a square of this size, in centimeters, made of quads.
const float GroundSize = 40000.0f;
const int32 GroundQuads = 100;

// When the sun rises and sets, how high it gets at noon, in degrees, and how
// bright it is, in lux. It rises behind where the player starts looking, and
// sets ahead of it.
const float SunriseHour = 6.0f;
const float SunsetHour = 19.0f;
const float NoonElevation = 70.0f;
const float SunriseAzimuth = 200.0f;
const float SunLux = 10.0f;

// The hour each time of day asks for.
const TMap<FString, float> Hours = {
	{TEXT("dawn"), 6.25f},
	{TEXT("noon"), 12.5f},
	{TEXT("sunset"), 18.75f},
	{TEXT("night"), 23.5f},
};
const TMap<FString, float> FogLevels = {
	{TEXT("none"), 0.0f},
	{TEXT("light"), 0.35f},
	{TEXT("thick"), 1.0f},
};
const TMap<FString, float> CloudLevels = {
	{TEXT("clear"), 0.0f},
	{TEXT("some"), 0.5f},
	{TEXT("cloudy"), 1.0f},
};

// Reads `Field` from `Changes` into `Value`, if it's one of `Levels`.
void ReadLevel(const FJsonObject& Changes, const TCHAR* Field, const TMap<FString, float>& Levels, float& Value)
{
	FString Name;
	if (!Changes.TryGetStringField(Field, Name))
	{
		return;
	}
	if (const float* Level = Levels.Find(Name.ToLower()))
	{
		Value = *Level;
	}
	else
	{
		UE_LOG(LogPipecatWorld, Warning, TEXT("Unknown %s: %s"), Field, *Name);
	}
}

float SmoothStep(float T)
{
	return T * T * (3.0f - 2.0f * T);
}

// For trying looks without the bot, e.g. Pipecat.SetEnvironment time_of_day=sunset fog=thick
FAutoConsoleCommandWithWorldAndArgs SetEnvironmentCommand(
	TEXT("Pipecat.SetEnvironment"),
	TEXT("Changes the environment, e.g. time_of_day=sunset fog=thick clouds=cloudy"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World) {
		if (!World)
		{
			return;
		}
		FJsonObject Changes;
		for (const FString& Arg : Args)
		{
			FString Field, Value;
			if (Arg.Split(TEXT("="), &Field, &Value))
			{
				Changes.SetStringField(Field, Value);
			}
		}
		for (TActorIterator<APipecatWorld> It(World); It; ++It)
		{
			It->SetEnvironment(Changes);
		}
	}));
} // namespace

APipecatWorld::APipecatWorld()
{
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GridMaterial(
		TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"));

	Ground = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Ground"));
	Ground->bUseComplexAsSimpleCollision = true;
	Ground->SetMaterial(0, GridMaterial.Object);
	RootComponent = Ground;

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(Ground);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetAtmosphereSunLightIndex(0);

	// Faint and blue, so nights aren't black.
	Moon = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Moon"));
	Moon->SetupAttachment(Ground);
	Moon->SetMobility(EComponentMobility::Movable);
	Moon->SetAtmosphereSunLight(true);
	Moon->SetAtmosphereSunLightIndex(1);
	Moon->SetLightColor(FLinearColor(0.55f, 0.65f, 1.0f));
	Moon->SetCastShadows(false);

	Sky = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Sky"));
	Sky->SetupAttachment(Ground);

	// Lights the world with the sky as it changes.
	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Ground);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;

	Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("Clouds"));
	Clouds->SetupAttachment(Ground);

	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(Ground);
	Fog->SetVolumetricFog(true);
	Fog->SetFogHeightFalloff(0.2f);
}

void APipecatWorld::BeginPlay()
{
	Super::BeginPlay();

	BuildGround();

	// Clouds come and go with the density and coverage of the engine's cloud
	// material.
	UMaterialInterface* CloudBase = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst"));
	if (CloudBase)
	{
		CloudMaterial = UMaterialInstanceDynamic::Create(CloudBase, this);
		Clouds->SetMaterial(CloudMaterial);
	}

	Show(Current);
}

void APipecatWorld::BuildGround()
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;

	const int32 Side = GroundQuads + 1;
	const float Step = GroundSize / GroundQuads;
	for (int32 Y = 0; Y < Side; ++Y)
	{
		for (int32 X = 0; X < Side; ++X)
		{
			FVector Position(X * Step - GroundSize / 2, Y * Step - GroundSize / 2, 0.0f);
			Vertices.Add(Position);
			Normals.Add(FVector::UpVector);
			UVs.Add(FVector2D(Position.X, Position.Y) / 100.0f);
			Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
		}
	}
	for (int32 Y = 0; Y < GroundQuads; ++Y)
	{
		for (int32 X = 0; X < GroundQuads; ++X)
		{
			int32 Corner = Y * Side + X;
			Triangles.Append({Corner, Corner + Side, Corner + 1});
			Triangles.Append({Corner + 1, Corner + Side, Corner + Side + 1});
		}
	}

	Ground->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, {}, Tangents, true);
}

void APipecatWorld::SetEnvironment(const FJsonObject& Changes)
{
	FLook Next = bChanging ? To : Current;
	float Hour = FMath::Fmod(Next.Hour, 24.0f);
	ReadLevel(Changes, TEXT("time_of_day"), Hours, Hour);
	ReadLevel(Changes, TEXT("fog"), FogLevels, Next.Fog);
	ReadLevel(Changes, TEXT("clouds"), CloudLevels, Next.Clouds);

	// Time only goes forward, so e.g. from sunset to dawn it goes through the
	// night.
	From = Current;
	Next.Hour = Hour;
	while (Next.Hour < From.Hour)
	{
		Next.Hour += 24.0f;
	}
	To = Next;
	Elapsed = 0.0f;
	bChanging = true;
	UE_LOG(LogPipecatWorld, Log, TEXT("Changing to hour %.1f, fog %.2f, clouds %.2f"), To.Hour, To.Fog, To.Clouds);
}

void APipecatWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bChanging)
	{
		return;
	}

	Elapsed += DeltaSeconds;
	float T = SmoothStep(FMath::Min(Elapsed / ChangeSeconds, 1.0f));
	Current.Hour = FMath::Lerp(From.Hour, To.Hour, T);
	Current.Fog = FMath::Lerp(From.Fog, To.Fog, T);
	Current.Clouds = FMath::Lerp(From.Clouds, To.Clouds, T);
	if (Elapsed >= ChangeSeconds)
	{
		Current.Hour = FMath::Fmod(Current.Hour, 24.0f);
		bChanging = false;
	}
	Show(Current);
}

void APipecatWorld::Show(const FLook& Look)
{
	// The sun goes from east to west, and below the horizon at night.
	float Hour = FMath::Fmod(Look.Hour, 24.0f);
	float DayFraction = (Hour - SunriseHour) / (SunsetHour - SunriseHour);
	if (Hour < SunriseHour)
	{
		DayFraction = (Hour + 24.0f - SunriseHour) / (SunsetHour - SunriseHour);
	}
	float Elevation = NoonElevation * FMath::Sin(DayFraction * PI);
	float Azimuth = SunriseAzimuth + 180.0f * DayFraction;
	// Lights point where the light goes, away from the sun, which fades out
	// below the horizon.
	Sun->SetWorldRotation(FRotator(-Elevation, Azimuth + 180.0f, 0.0f));
	Sun->SetIntensity(SunLux * FMath::Clamp((Elevation + 4.0f) / 4.0f, 0.0f, 1.0f));

	// The moon rises as the sun sets.
	float Night = FMath::Clamp(-Elevation / 15.0f, 0.0f, 1.0f);
	Moon->SetWorldRotation(FRotator(-45.0f, Azimuth, 0.0f));
	Moon->SetIntensity(0.3f * Night);
	Moon->SetVisibility(Night > 0.0f);

	Fog->SetFogDensity(FMath::Lerp(0.002f, 0.08f, Look.Fog));
	Fog->SetVolumetricFogExtinctionScale(FMath::Lerp(1.0f, 4.0f, Look.Fog));

	if (CloudMaterial)
	{
		// From no clouds, through the engine's default ones, to a cloudy sky.
		float Some = FMath::Min(Look.Clouds * 2.0f, 1.0f);
		float Cloudy = FMath::Max(Look.Clouds * 2.0f - 1.0f, 0.0f);
		CloudMaterial->SetScalarParameterValue(TEXT("Cloud_GlobalDensity"), 0.008f * (Some + Cloudy));
		CloudMaterial->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), FMath::Lerp(-0.2f, 0.05f, Cloudy));
	}
}
