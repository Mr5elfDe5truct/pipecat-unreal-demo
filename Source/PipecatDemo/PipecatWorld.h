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
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UVolumetricCloudComponent;

// The world around the player, which the bot changes when the player asks for
// it. It starts as an empty grid under a clear midday sky, and every change
// animates over a few seconds.
UCLASS()
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
	 */
	void SetEnvironment(const FJsonObject& Changes);

	virtual void Tick(float DeltaSeconds) override;

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
	};

	void BuildGround();
	void Show(const FLook& Look);

	//
	// Components
	//

	UPROPERTY(VisibleAnywhere, Category = "Pipecat")
	TObjectPtr<UProceduralMeshComponent> Ground;

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

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;

	//
	// Transitions
	//

	// What's shown now, and the change in progress, from one look to another.
	FLook Current;
	FLook From;
	FLook To;
	float Elapsed = 0.0f;
	bool bChanging = false;
};
