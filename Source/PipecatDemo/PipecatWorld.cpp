//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#include "PipecatWorld.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPipecatWorld, Log, All);

namespace
{
// How long a change takes to show, in seconds.
const float ChangeSeconds = 5.0f;

// The ground: a square of this size, in centimeters, made of quads. While the
// terrain changes, it's rebuilt this often, in seconds.
const float GroundSize = 60000.0f;
const int32 GroundQuads = 200;
const float GroundRebuildSeconds = 0.05f;

// Hills, and the mountains around the valley the player starts in, in
// centimeters.
const float HillHeight = 1500.0f;
const float MountainHeight = 12000.0f;
const float ValleyRadius = 5000.0f;
const float MountainsRadius = 20000.0f;

// The pond and the lake, ahead and to the right of where the player starts,
// and the river's course, from their left, past them, and through the pond and
// the lake, in centimeters. The river starts where the mountains are this
// high. The water fills them up to its level.
const FVector2D PondCenter(4000.0f, 2200.0f);
const float PondRadius = 2500.0f;
const float PondDepth = 700.0f;
const FVector2D LakeCenter(11000.0f, 6000.0f);
const float LakeRadius = 10000.0f;
const float LakeDepth = 1500.0f;
const FVector2D RiverCourse[] = {
	{-2000.0f, -30000.0f},
	{2500.0f, -6000.0f},
	{2300.0f, 0.0f},
	{5000.0f, 4500.0f},
	{12000.0f, 9000.0f},
	{20000.0f, 30000.0f},
};
const float RiverHalfWidth = 1300.0f;
const float RiverDepth = 450.0f;
const float RiverSourceHeight = 1000.0f;
// The ground around them is flat this far beyond their edge, relative to their
// size, so hills don't fill them. And it's muddy this far from about where the
// water reaches.
const float ShoreWidth = 0.5f;
const float Waterline = 0.88f;
const float MudWidth = 0.2f;
const float WaterLevel = -40.0f;
const float DryLevel = -1500.0f;

// The ground's colors: the template's gray grid, whose lines fade into grass,
// and patches of dirt, more of them where the grass is sparse.
const FLinearColor GridSurface(0.18f, 0.18f, 0.18f);
const FLinearColor GridLines(0.026f, 0.026f, 0.026f);
const FLinearColor GridSubLines(0.089f, 0.089f, 0.089f);
const FLinearColor GrassSurface(0.035f, 0.075f, 0.015f);
const float SparsePatchiness = 0.3f;
const float LushPatchiness = 0.15f;

// When the sun rises and sets, how high it gets at noon, in degrees, and how
// bright it is, in lux. It rises behind where the player starts looking, and
// sets ahead of it.
const float SunriseHour = 6.0f;
const float SunsetHour = 19.0f;
const float NoonElevation = 70.0f;
const float SunriseAzimuth = 200.0f;
const float SunLux = 10.0f;

// The darkest the camera adapts to, in EV100, with the sun high, and with the
// sun low or at night, so e.g. a forest's shade doesn't make everything look
// washed out, but a sunset behind a hill isn't black.
const float DayMinExposure = 2.0f;
const float NightMinExposure = -3.0f;

// What a plant's growth looks like during a change: it takes this much of the
// change to grow, after it starts.
const float GrowthFraction = 0.25f;

// The value of each level the bot can ask for.
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
const TMap<FString, float> TerrainLevels = {
	{TEXT("flat"), 0.0f},
	{TEXT("hilly"), 0.5f},
	{TEXT("mountains"), 1.0f},
};
const TMap<FString, float> GrassLevels = {
	{TEXT("none"), 0.0f},
	{TEXT("some"), 0.4f},
	{TEXT("lush"), 1.0f},
};
const TMap<FString, float> TreeLevels = {
	{TEXT("none"), 0.0f},
	{TEXT("few"), 0.12f},
	{TEXT("forest"), 1.0f},
};

// Reads `Field` from `Changes` into `Value`, if it's true (1) or false (0).
void ReadSwitch(const FJsonObject& Changes, const TCHAR* Field, float& Value)
{
	bool bOn = false;
	if (Changes.TryGetBoolField(Field, bOn))
	{
		Value = bOn ? 1.0f : 0.0f;
	}
}

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

// From 1 in the middle of a pond, lake or river to 0 at its edge, `Distance`
// being relative to its size.
float Bank(float Distance)
{
	return Distance >= 1.0f ? 0.0f : 0.5f * (1.0f + FMath::Cos(PI * Distance));
}

// From 1 within a pond, lake or river, and its shore, to 0 beyond, `Distance`
// being relative to its size.
float Flatness(float Distance)
{
	return Bank(FMath::Max(Distance - 1.0f, 0.0f) / ShoreWidth);
}

// The distance from the middle of the river, which meanders along its course,
// relative to its width.
float RiverDistance(const FVector2D& Position)
{
	FVector2D Meander(
		FMath::PerlinNoise2D(Position / 4000.0f), FMath::PerlinNoise2D(Position / 4000.0f + FVector2D(5.2f, 1.3f)));
	FVector Point(Position + 700.0f * Meander, 0.0f);
	float Distance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index + 1 < UE_ARRAY_COUNT(RiverCourse); ++Index)
	{
		Distance = FMath::Min(
			Distance,
			static_cast<float>(FMath::PointDistToSegment(
				Point, FVector(RiverCourse[Index], 0.0f), FVector(RiverCourse[Index + 1], 0.0f))));
	}
	return Distance / RiverHalfWidth;
}

// The distance from the middle of a pond or a lake whose shore isn't quite
// round, relative to its size.
float RoundDistance(const FVector2D& Position, const FVector2D& Center, float Radius, float Irregularity)
{
	float Shore = Radius * (1.0f + Irregularity * FMath::PerlinNoise2D(Position / 3000.0f + FVector2D(7.0f, 3.0f)));
	return FVector2D::Distance(Position, Center) / Shore;
}

// Peaks and ridges, from 0 to about 1.
float Ridges(const FVector2D& Position)
{
	float Height = 0.0f;
	float Amplitude = 0.6f;
	FVector2D Point = Position / 15000.0f;
	for (int32 Octave = 0; Octave < 4; ++Octave)
	{
		float Ridge = 1.0f - FMath::Abs(FMath::PerlinNoise2D(Point));
		Height += Amplitude * Ridge * Ridge;
		Amplitude *= 0.45f;
		Point = Point * 2.1f + FVector2D(13.7f, 7.3f);
	}
	return Height;
}

// For trying looks without the bot, e.g. Pipecat.SetEnvironment time_of_day=sunset fog=thick
FAutoConsoleCommandWithWorldAndArgs SetEnvironmentCommand(
	TEXT("Pipecat.SetEnvironment"),
	TEXT("Changes the environment, e.g. time_of_day=sunset fog=thick terrain=hilly trees=forest lake=true"),
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

	Ground = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Ground"));
	Ground->bUseComplexAsSimpleCollision = true;
	// The terrain changes while the player walks on it.
	Ground->bUseAsyncCooking = true;
	RootComponent = Ground;

	// A rippling surface over the whole ground, at the water's level, with the
	// Water plugin's material, which foams at the shore. It's below the ground
	// until there's water.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WaterMaterial(
		TEXT("/Water/Materials/WaterSurface/Water_Material_CustomMesh.Water_Material_CustomMesh"));
	Water = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Water"));
	Water->SetupAttachment(Ground);
	Water->SetStaticMesh(Plane.Object);
	Water->SetMaterial(0, WaterMaterial.Object);
	Water->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Water->SetRelativeScale3D(FVector(GroundSize / 100.0f, GroundSize / 100.0f, 1.0f));
	Water->SetVisibility(false);

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

	Exposure = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Exposure"));
	Exposure->SetupAttachment(Ground);
	Exposure->bUnbound = true;
	Exposure->Settings.bOverride_AutoExposureMinBrightness = true;
}

void APipecatWorld::BeginPlay()
{
	Super::BeginPlay();

	// The template's prototype grid, with dirt, which turns into grass. It's
	// created by setup.sh.
	UMaterialInterface* GridMaterial =
		LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Pipecat/MI_Ground.MI_Ground"));
	if (GridMaterial)
	{
		GroundMaterial = UMaterialInstanceDynamic::Create(GridMaterial, this);
		Ground->SetMaterial(0, GroundMaterial);
	}
	else
	{
		UE_LOG(LogPipecatWorld, Warning, TEXT("No ground material: run setup.sh"));
	}
	BuildGround(true);

	// Clouds come and go with the density and coverage of the engine's cloud
	// material.
	UMaterialInterface* CloudBase = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst"));
	if (CloudBase)
	{
		CloudMaterial = UMaterialInstanceDynamic::Create(CloudBase, this);
		Clouds->SetMaterial(CloudMaterial);
	}

	// Grass close to where the player starts, and trees everywhere but there.
	Plant(Grass, GrassMeshes, 180.0f, 30000.0f, 0.0f, 1.0f, 2.2f);
	Plant(Trees, TreeMeshes, 1000.0f, GroundSize, 1200.0f, 0.7f, 1.3f);

	Show(Current);
}

void APipecatWorld::SetEnvironment(const FJsonObject& Changes)
{
	FLook Next = bChanging ? To : Current;
	float Hour = FMath::Fmod(Next.Hour, 24.0f);
	ReadLevel(Changes, TEXT("time_of_day"), Hours, Hour);
	ReadLevel(Changes, TEXT("fog"), FogLevels, Next.Fog);
	ReadLevel(Changes, TEXT("clouds"), CloudLevels, Next.Clouds);
	ReadLevel(Changes, TEXT("terrain"), TerrainLevels, Next.Terrain);
	ReadLevel(Changes, TEXT("grass"), GrassLevels, Next.Grass);
	ReadLevel(Changes, TEXT("trees"), TreeLevels, Next.Trees);
	ReadSwitch(Changes, TEXT("pond"), Next.Pond);
	ReadSwitch(Changes, TEXT("lake"), Next.Lake);
	ReadSwitch(Changes, TEXT("river"), Next.River);

	// Plants finish the change in progress, if any, so the new one starts from
	// them.
	if (bChanging)
	{
		GrowPlants(Grass, &FLook::Grass, 1.0f, false);
		GrowPlants(Trees, &FLook::Trees, 1.0f, false);
		Current.Grass = To.Grass;
		Current.Trees = To.Trees;
	}
	From = Current;
	StartChange(Grass);
	StartChange(Trees);

	// Time only goes forward, so e.g. from sunset to dawn it goes through the
	// night.
	Next.Hour = Hour;
	while (Next.Hour < From.Hour)
	{
		Next.Hour += 24.0f;
	}
	To = Next;
	Elapsed = 0.0f;
	bChanging = true;
	UE_LOG(
		LogPipecatWorld,
		Log,
		TEXT("Changing to hour %.1f, fog %.2f, clouds %.2f, terrain %.2f, grass %.2f, trees %.2f, pond %.0f, lake %.0f, river %.0f"),
		To.Hour,
		To.Fog,
		To.Clouds,
		To.Terrain,
		To.Grass,
		To.Trees,
		To.Pond,
		To.Lake,
		To.River);
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
	bool bDone = Elapsed >= ChangeSeconds;
	Current.Hour = FMath::Lerp(From.Hour, To.Hour, T);
	Current.Fog = FMath::Lerp(From.Fog, To.Fog, T);
	Current.Clouds = FMath::Lerp(From.Clouds, To.Clouds, T);
	Current.Terrain = FMath::Lerp(From.Terrain, To.Terrain, T);
	Current.Grass = FMath::Lerp(From.Grass, To.Grass, T);
	Current.Trees = FMath::Lerp(From.Trees, To.Trees, T);
	Current.Pond = FMath::Lerp(From.Pond, To.Pond, T);
	Current.Lake = FMath::Lerp(From.Lake, To.Lake, T);
	Current.River = FMath::Lerp(From.River, To.River, T);

	// The ground changes a few times a second, and the player and the plants
	// move with it.
	bool bGroundMoved = false;
	if (From.Terrain != To.Terrain || From.Pond != To.Pond || From.Lake != To.Lake || From.River != To.River)
	{
		SinceGroundBuilt += DeltaSeconds;
		if (SinceGroundBuilt >= GroundRebuildSeconds || bDone)
		{
			SinceGroundBuilt = 0.0f;
			BuildGround(false);
			LiftPlayer();
			bGroundMoved = true;
		}
	}
	// Plants grow over the whole change, from the player outwards.
	float Progress = FMath::Min(Elapsed / ChangeSeconds, 1.0f);
	GrowPlants(Grass, &FLook::Grass, Progress, bGroundMoved);
	GrowPlants(Trees, &FLook::Trees, Progress, bGroundMoved);

	if (bDone)
	{
		Current.Hour = FMath::Fmod(Current.Hour, 24.0f);
		bChanging = false;
	}
	Show(Current);
}

//
// The ground
//

APipecatWorld::FTerrain APipecatWorld::TerrainAt(const FVector2D& Position)
{
	float Rolling = FMath::PerlinNoise2D(Position / 12000.0f) +
					0.35f * FMath::PerlinNoise2D(Position / 4000.0f + FVector2D(31.0f, 17.0f));
	// Mountains rise around the valley the player starts in.
	float Around = FMath::SmoothStep(ValleyRadius, MountainsRadius, static_cast<float>(Position.Size()));
	FTerrain Terrain;
	Terrain.Hills = HillHeight * Rolling;
	Terrain.Mountains = MountainHeight * Around * Ridges(Position);
	Terrain.Pond = RoundDistance(Position, PondCenter, PondRadius, 0.35f);
	Terrain.Lake = RoundDistance(Position, LakeCenter, LakeRadius, 0.15f);
	Terrain.River = RiverDistance(Position);
	return Terrain;
}

float APipecatWorld::GroundHeight(const FTerrain& Terrain, const FLook& Look)
{
	// Hills come first, and then mountains.
	float Hills = FMath::Clamp(Look.Terrain * 2.0f, 0.0f, 1.0f);
	float Mountains = FMath::Clamp(Look.Terrain * 2.0f - 1.0f, 0.0f, 1.0f);
	float Relief = Hills * Terrain.Hills + Mountains * Terrain.Mountains;
	// Ponds, lakes and rivers flatten the ground and sink into it first, and
	// then fill with water.
	float Pond = FMath::Clamp(Look.Pond * 2.0f, 0.0f, 1.0f);
	float Lake = FMath::Clamp(Look.Lake * 2.0f, 0.0f, 1.0f);
	float River = FMath::Clamp(Look.River * 2.0f, 0.0f, 1.0f) * RiverFade(Terrain, Look);
	float Flat = FMath::Max3(
		Pond * Flatness(Terrain.Pond),
		Lake * Flatness(Terrain.Lake),
		River * Flatness(Terrain.River));
	return Relief * (1.0f - Flat) - PondDepth * Pond * Bank(Terrain.Pond) - LakeDepth * Lake * Bank(Terrain.Lake) -
		   RiverDepth * River * Bank(Terrain.River);
}

float APipecatWorld::WaterHeight(const FLook& Look)
{
	float Full = FMath::Clamp(FMath::Max3(Look.Pond, Look.Lake, Look.River) * 2.0f - 1.0f, 0.0f, 1.0f);
	return FMath::Lerp(DryLevel, WaterLevel, Full);
}

float APipecatWorld::Mud(const FTerrain& Terrain, const FLook& Look)
{
	auto Shore = [](float Distance) { return Bank(FMath::Abs(Distance - Waterline) / MudWidth); };
	return FMath::Max3(
		Look.Pond * Shore(Terrain.Pond),
		Look.Lake * Shore(Terrain.Lake),
		Look.River * RiverFade(Terrain, Look) * Shore(Terrain.River));
}

float APipecatWorld::RiverFade(const FTerrain& Terrain, const FLook& Look)
{
	float Mountains = FMath::Clamp(Look.Terrain * 2.0f - 1.0f, 0.0f, 1.0f) * Terrain.Mountains;
	return 1.0f - FMath::Clamp(Mountains / RiverSourceHeight, 0.0f, 1.0f);
}

void APipecatWorld::BuildGround(bool bCreate)
{
	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	// The mud, in red, for the ground's material.
	TArray<FColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	const int32 Side = GroundQuads + 1;
	const float Step = GroundSize / GroundQuads;
	Vertices.Reserve(Side * Side);
	Normals.Reserve(Side * Side);
	UVs.Reserve(Side * Side);
	Colors.Reserve(Side * Side);
	Tangents.Reserve(Side * Side);

	if (bCreate)
	{
		GroundTerrain.SetNumUninitialized(Side * Side);
		for (int32 Y = 0; Y < Side; ++Y)
		{
			for (int32 X = 0; X < Side; ++X)
			{
				GroundTerrain[Y * Side + X] = TerrainAt(FVector2D(X * Step - GroundSize / 2, Y * Step - GroundSize / 2));
			}
		}
	}
	TArray<float> Heights;
	Heights.SetNumUninitialized(Side * Side);
	for (int32 Vertex = 0; Vertex < Side * Side; ++Vertex)
	{
		Heights[Vertex] = GroundHeight(GroundTerrain[Vertex], Current);
	}

	for (int32 Y = 0; Y < Side; ++Y)
	{
		for (int32 X = 0; X < Side; ++X)
		{
			FVector2D Position(X * Step - GroundSize / 2, Y * Step - GroundSize / 2);
			Vertices.Add(FVector(Position, Heights[Y * Side + X]));
			UVs.Add(Position / 100.0f);
			Colors.Add(FColor(FMath::RoundToInt(255.0f * Mud(GroundTerrain[Y * Side + X], Current)), 0, 0));

			// From the slope to the neighbors.
			float Left = Heights[Y * Side + FMath::Max(X - 1, 0)];
			float Right = Heights[Y * Side + FMath::Min(X + 1, GroundQuads)];
			float Back = Heights[FMath::Max(Y - 1, 0) * Side + X];
			float Front = Heights[FMath::Min(Y + 1, GroundQuads) * Side + X];
			FVector Normal = FVector(Left - Right, Back - Front, 2.0f * Step).GetSafeNormal();
			Normals.Add(Normal);
			Tangents.Add(FProcMeshTangent(FVector(2.0f * Step, 0.0f, Right - Left).GetSafeNormal(), false));
		}
	}

	if (!bCreate)
	{
		Ground->UpdateMeshSection(0, Vertices, Normals, UVs, Colors, Tangents);
		return;
	}

	TArray<int32> Triangles;
	Triangles.Reserve(GroundQuads * GroundQuads * 6);
	for (int32 Y = 0; Y < GroundQuads; ++Y)
	{
		for (int32 X = 0; X < GroundQuads; ++X)
		{
			int32 Corner = Y * Side + X;
			Triangles.Append({Corner, Corner + Side, Corner + 1});
			Triangles.Append({Corner + 1, Corner + Side, Corner + Side + 1});
		}
	}
	Ground->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, true);
}

void APipecatWorld::LiftPlayer()
{
	// Out of the ground, if it rose around them.
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player)
	{
		return;
	}
	FVector Location = Player->GetActorLocation();
	float Feet = GroundHeight(TerrainAt(FVector2D(Location)), Current) + Player->GetSimpleCollisionHalfHeight();
	if (Location.Z < Feet)
	{
		Location.Z = Feet + 5.0f;
		Player->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

//
// Plants
//

void APipecatWorld::Plant(
	FPlants& Plants,
	const TArray<FSoftObjectPath>& MeshPaths,
	float Spacing,
	float Area,
	float Clearing,
	float MinSize,
	float MaxSize)
{
	for (const FSoftObjectPath& Path : MeshPaths)
	{
		UStaticMesh* Mesh = Cast<UStaticMesh>(Path.TryLoad());
		if (!Mesh)
		{
			UE_LOG(LogPipecatWorld, Warning, TEXT("Unable to load %s"), *Path.ToString());
			continue;
		}
		UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(this);
		Instances->SetStaticMesh(Mesh);
		Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Instances->SetupAttachment(Ground);
		Instances->RegisterComponent();
		Plants.Meshes.Add(Instances);
		PlantMeshes.Add(Instances);
	}
	if (Plants.Meshes.IsEmpty())
	{
		return;
	}

	// On a jittered grid, so they look scattered but don't overlap much. All
	// of them are there, but not grown.
	FRandomStream Random(Plants.Meshes.Num() * 7919 + FMath::RoundToInt(Spacing));
	const int32 Count = FMath::FloorToInt(Area / Spacing);
	TArray<TArray<FTransform>> Transforms;
	Transforms.SetNum(Plants.Meshes.Num());
	for (int32 Y = 0; Y < Count; ++Y)
	{
		for (int32 X = 0; X < Count; ++X)
		{
			FPlant Plant;
			Plant.Mesh = Random.RandRange(0, Plants.Meshes.Num() - 1);
			Plant.Position = FVector2D(
				(X + Random.FRand()) * Spacing - Area / 2,
				(Y + Random.FRand()) * Spacing - Area / 2);
			if (Plant.Position.Size() < Clearing)
			{
				continue;
			}
			Plant.Terrain = TerrainAt(Plant.Position);
			Plant.Yaw = Random.FRandRange(0.0f, 360.0f);
			Plant.Size = Random.FRandRange(MinSize, MaxSize);
			Plant.Rank = Random.FRand();
			Plant.Instance = Transforms[Plant.Mesh].Num();
			Transforms[Plant.Mesh].Add(FTransform(FRotator(0.0f, Plant.Yaw, 0.0f), FVector(Plant.Position, 0.0f), FVector::ZeroVector));
			Plants.All.Add(Plant);
		}
	}
	for (int32 Mesh = 0; Mesh < Plants.Meshes.Num(); ++Mesh)
	{
		Plants.Meshes[Mesh]->AddInstances(Transforms[Mesh], false);
	}
}

void APipecatWorld::StartChange(FPlants& Plants)
{
	// From the player outwards, with some randomness.
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	FVector2D Center = Player ? FVector2D(Player->GetActorLocation()) : FVector2D::ZeroVector;
	const float Farthest = GroundSize * 0.75f;
	for (FPlant& Plant : Plants.All)
	{
		float Distance = FVector2D::Distance(Plant.Position, Center) / Farthest;
		Plant.Start = FMath::Clamp(Distance * (1.0f - GrowthFraction) + 0.05f * (Plant.Rank - 0.5f), 0.0f, 1.0f - GrowthFraction);
	}
}

bool APipecatWorld::IsThere(const FPlant& Plant, const FLook& Look, float FLook::* Level)
{
	return Plant.Rank < Look.*Level && GroundHeight(Plant.Terrain, Look) > WaterHeight(Look) + 30.0f;
}

void APipecatWorld::GrowPlants(FPlants& Plants, float FLook::* Level, float Progress, bool bGroundMoved)
{
	bool bChanged =
		From.*Level != To.*Level || From.Pond != To.Pond || From.Lake != To.Lake || From.River != To.River;
	if (Plants.Meshes.IsEmpty() || (!bChanged && !bGroundMoved))
	{
		return;
	}

	TArray<bool> Dirty;
	Dirty.Init(false, Plants.Meshes.Num());
	for (FPlant& Plant : Plants.All)
	{
		bool bThere = IsThere(Plant, To, Level);
		float Growth = bThere ? 1.0f : 0.0f;
		if (IsThere(Plant, From, Level) != bThere)
		{
			float Grown = FMath::Clamp((Progress - Plant.Start) / GrowthFraction, 0.0f, 1.0f);
			Growth = bThere ? SmoothStep(Grown) : 1.0f - SmoothStep(Grown);
		}
		if (Growth == Plant.Growth && !(bGroundMoved && Growth > 0.0f))
		{
			continue;
		}
		Plant.Growth = Growth;

		FVector Location(Plant.Position, GroundHeight(Plant.Terrain, Current));
		FTransform Transform(FRotator(0.0f, Plant.Yaw, 0.0f), Location, FVector(Plant.Size * Growth));
		Plants.Meshes[Plant.Mesh]->UpdateInstanceTransform(Plant.Instance, Transform, true, false, true);
		Dirty[Plant.Mesh] = true;
	}
	for (int32 Mesh = 0; Mesh < Plants.Meshes.Num(); ++Mesh)
	{
		if (Dirty[Mesh])
		{
			Plants.Meshes[Mesh]->MarkRenderStateDirty();
		}
	}
}

//
// The sky
//

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
	float HighSun = FMath::Clamp(Elevation / 20.0f, 0.0f, 1.0f);
	Exposure->Settings.AutoExposureMinBrightness = FMath::Lerp(NightMinExposure, DayMinExposure, HighSun);

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

	float WaterZ = WaterHeight(Look);
	Water->SetRelativeLocation(FVector(0.0f, 0.0f, WaterZ));
	Water->SetVisibility(WaterZ > DryLevel);

	// The grid turns green, uneven and patchy as the grass comes.
	if (GroundMaterial)
	{
		float Green = FMath::Min(Look.Grass * 2.5f, 1.0f);
		GroundMaterial->SetVectorParameterValue(TEXT("SurfaceColor"), FMath::Lerp(GridSurface, GrassSurface, Green));
		GroundMaterial->SetVectorParameterValue(TEXT("GridColor"), FMath::Lerp(GridLines, GrassSurface, Green));
		GroundMaterial->SetVectorParameterValue(TEXT("SubGridColor"), FMath::Lerp(GridSubLines, GrassSurface, Green));
		GroundMaterial->SetScalarParameterValue(TEXT("Detail"), Green);
		GroundMaterial->SetScalarParameterValue(
			TEXT("Patchiness"), Green * FMath::Lerp(SparsePatchiness, LushPatchiness, Look.Grass));
	}
}
