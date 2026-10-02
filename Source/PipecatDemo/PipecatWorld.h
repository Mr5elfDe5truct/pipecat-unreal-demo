//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "PipecatWorld.generated.h"

class FJsonObject;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UVolumetricCloudComponent;

// The world around the player, which the bot changes when the player asks for
// it. It starts as an empty, flat grid under a clear midday sky, and every
// change animates over a few seconds.
UCLASS(Config = Game)
class APipecatWorld : public AActor
{
	GENERATED_BODY()

public:
	APipecatWorld();

	/**
	 * Changes the environment to what `Changes` asks for, e.g.
	 * {"time_of_day": "sunset", "fog": "thick"}. What it leaves out stays as
	 * it is.
	 *
	 * - time_of_day: dawn, noon, sunset or night.
	 * - fog: none, light or thick.
	 * - clouds: clear, some or cloudy.
	 * - terrain: flat, hilly or mountains.
	 * - grass: none, some or lush.
	 * - trees: none, few or forest.
	 * - pond, lake and river: true or false. They can be there together, and
	 *   the river flows through the lake.
	 */
	void SetEnvironment(const FJsonObject& Changes);

	virtual void Tick(float DeltaSeconds) override;

	/** The meshes of the grass, set in the game's config. */
	UPROPERTY(Config)
	TArray<FSoftObjectPath> GrassMeshes;

	/** The meshes of the trees, set in the game's config. */
	UPROPERTY(Config)
	TArray<FSoftObjectPath> TreeMeshes;

protected:
	virtual void BeginPlay() override;

private:
	// What the environment looks like, as values that change smoothly.
	struct FLook
	{
		// Hour of the day, from 0 to 24, or more while the day turns over.
		float Hour = 12.5f;
		// From 0 (none) to 1 (thick).
		float Fog = 0.0f;
		// From 0 (clear) to 1 (cloudy).
		float Clouds = 0.0f;
		// From 0 (flat), through hills, to 1 (mountains).
		float Terrain = 0.0f;
		// From 0 (none) to 1 (lush).
		float Grass = 0.0f;
		// From 0 (none) to 1 (forest).
		float Trees = 0.0f;
		// From 0 (none) to 1 (full): the ground sinks first, and then fills
		// with water.
		float Pond = 0.0f;
		float Lake = 0.0f;
		float River = 0.0f;
	};

	// The terrain at a point: the heights of the hills and of the mountains,
	// in centimeters, and how far it is from the middle of the pond, the lake
	// and the river, relative to their size: 0 in the middle, and 1 at their
	// edge.
	struct FTerrain
	{
		float Hills = 0.0f;
		float Mountains = 0.0f;
		float Pond = 0.0f;
		float Lake = 0.0f;
		float River = 0.0f;
	};

	// A plant on the ground, which grows out of it or shrinks into it.
	struct FPlant
	{
		// Its mesh, and its instance of the mesh.
		int32 Mesh = 0;
		int32 Instance = 0;
		FVector2D Position;
		// The terrain where it is.
		FTerrain Terrain;
		float Yaw = 0.0f;
		float Size = 1.0f;
		// It's there when the level of its kind is above this.
		float Rank = 0.0f;
		// How grown it is, from 0 to 1.
		float Growth = 0.0f;
		// When, during a change, it starts growing or shrinking, from 0 to 1.
		// Plants near the player go first.
		float Start = 0.0f;
	};

	// Plants of one kind, e.g. trees, scattered over the ground.
	struct FPlants
	{
		TArray<FPlant> All;
		// One per mesh.
		TArray<TObjectPtr<UInstancedStaticMeshComponent>> Meshes;
	};

	//
	// The ground
	//

	// The terrain at a point, and the ground's and the water's heights for a
	// look.
	static FTerrain TerrainAt(const FVector2D& Position);
	static float GroundHeight(const FTerrain& Terrain, const FLook& Look);
	static float WaterHeight(const FLook& Look);
	// How muddy the ground is, from 0 to 1, on the shores of the water.
	static float Mud(const FTerrain& Terrain, const FLook& Look);
	// How much of the river there is at a point, from 0 to 1: it doesn't cut
	// through mountains, it starts at their foot.
	static float RiverFade(const FTerrain& Terrain, const FLook& Look);
	void BuildGround(bool bCreate);
	void LiftPlayer();

	//
	// Plants
	//

	void Plant(
		FPlants& Plants,
		const TArray<FSoftObjectPath>& MeshPaths,
		float Spacing,
		float Area,
		float Clearing,
		float MinSize,
		float MaxSize);
	void StartChange(FPlants& Plants);
	// Grows and shrinks plants, whose level is `Level` of the look, as far as
	// `Progress` into the change.
	void GrowPlants(FPlants& Plants, float FLook::* Level, float Progress, bool bGroundMoved);
	// Whether a plant is there for a look: above its kind's level, and above
	// the water.
	static bool IsThere(const FPlant& Plant, const FLook& Look, float FLook::* Level);

	//
	// The sky
	//

	void Show(const FLook& Look);

	//
	// Components
	//

	UPROPERTY(VisibleAnywhere, Category = "Pipecat")
	TObjectPtr<UProceduralMeshComponent> Ground;

	UPROPERTY(VisibleAnywhere, Category = "Pipecat")
	TObjectPtr<UStaticMeshComponent> Water;

	UPROPERTY(VisibleAnywhere, Category = "Pipecat")
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY(VisibleAnywhere, Category = "Pipecat")
	TObjectPtr<UDirectionalLightComponent> Moon;

	UPROPERTY(VisibleAnywhere, Category = "Pipecat")
	TObjectPtr<USkyAtmosphereComponent> Sky;

	UPROPERTY(VisibleAnywhere, Category = "Pipecat")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, Category = "Pipecat")
	TObjectPtr<UVolumetricCloudComponent> Clouds;

	UPROPERTY(VisibleAnywhere, Category = "Pipecat")
	TObjectPtr<UExponentialHeightFogComponent> Fog;

	UPROPERTY(VisibleAnywhere, Category = "Pipecat")
	TObjectPtr<UPostProcessComponent> Exposure;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GroundMaterial;

	// The terrain at each of the ground's vertices.
	TArray<FTerrain> GroundTerrain;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;

	// The plants' meshes, kept alive here.
	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> PlantMeshes;

	FPlants Grass;
	FPlants Trees;

	//
	// Changes
	//

	// What's shown now, and the change in progress, from one look to another.
	FLook Current;
	FLook From;
	FLook To;
	float Elapsed = 0.0f;
	bool bChanging = false;
	// How long ago the ground was last rebuilt, while the terrain changes.
	float SinceGroundBuilt = 0.0f;
};
