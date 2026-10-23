//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#include "PipecatObjects.h"

#include "Components/InputComponent.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogPipecatObjects, Log, All);

namespace
{
// How far in front of the player objects go, and how far apart they stay.
const float Distance = 350.0f;
const float Spacing = 220.0f;

// For trying objects without the bot, e.g. Pipecat.Spawn door, or Pipecat.Spawn
// with nothing to remove them all.
FAutoConsoleCommandWithWorldAndArgs SpawnCommand(
	TEXT("Pipecat.Spawn"),
	TEXT("Puts an object in front of the player, e.g. Pipecat.Spawn door. Without one, removes them all."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
		for (TActorIterator<APipecatObjects> It(World); It; ++It)
		{
			if (Args.IsEmpty())
			{
				It->Clear();
			}
			for (const FString& Name : Args)
			{
				It->Spawn(Name);
			}
		}
	}));
} // namespace

APipecatObjects::APipecatObjects()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void APipecatObjects::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!Manager)
	{
		SetUpPlayer();
	}

	// The ground rises and sinks as the terrain changes, and the objects stay
	// on it. That's NPCs too, which don't fall without a controller.
	Spawned.RemoveAll([](const TObjectPtr<AActor>& Object) { return !IsValid(Object); });
	for (AActor* Object : Spawned)
	{
		PutOnGround(Object);
	}
}

void APipecatObjects::PutOnGround(AActor* Object)
{
	// The bottom of what's seen: not of trigger volumes around it.
	FBox Bounds(ForceInit);
	Object->ForEachComponent<UMeshComponent>(false, [&Bounds](const UMeshComponent* Mesh) {
		if (Mesh->IsVisible() && !Mesh->bHiddenInGame)
		{
			Bounds += Mesh->Bounds.GetBox();
		}
	});
	if (!Bounds.IsValid)
	{
		Bounds = Object->GetComponentsBoundingBox(false, true);
	}

	FCollisionQueryParams Params(TEXT("PipecatObjectGround"), true);
	Params.AddIgnoredActors(ToRawPtrTArrayUnsafe(Spawned));
	if (APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(Player);
	}
	FVector Location = Object->GetActorLocation();
	FHitResult Hit;
	if (Bounds.IsValid && GetWorld()->LineTraceSingleByChannel(Hit, FVector(Location.X, Location.Y, Location.Z + 20000.0f),
			FVector(Location.X, Location.Y, Location.Z - 20000.0f), ECC_WorldStatic, Params))
	{
		float Lift = Hit.ImpactPoint.Z - Bounds.Min.Z;
		if (FMath::Abs(Lift) > 1.0f)
		{
			Object->AddActorWorldOffset(FVector(0.0f, 0.0f, Lift), false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
}

bool APipecatObjects::SetUpPlayer()
{
	APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0);
	APawn* Player = Controller ? Controller->GetPawn() : nullptr;
	if (!Player || !Controller->InputComponent)
	{
		return false;
	}

	UClass* ManagerClass = InteractionManager.TryLoadClass<UActorComponent>();
	if (!ManagerClass)
	{
		UE_LOG(LogPipecatObjects, Warning, TEXT("Unable to load the interaction manager %s"),
			*InteractionManager.ToString());
		// Don't try again every frame.
		Manager = NewObject<UActorComponent>(this, UActorComponent::StaticClass());
		return false;
	}

	Manager = NewObject<UActorComponent>(Player, ManagerClass, TEXT("InteractionManager"));
	Player->AddInstanceComponent(Manager);
	Manager->RegisterComponent();

	// The Interaction System's demo character's keys.
	Controller->InputComponent->BindKey(EKeys::F, IE_Pressed, this, &APipecatObjects::Interact);
	Controller->InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &APipecatObjects::SelectUp);
	Controller->InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &APipecatObjects::SelectDown);

	UE_LOG(LogPipecatObjects, Log, TEXT("Gave the player the interaction manager: F to interact, the mouse wheel to choose"));
	return true;
}

void APipecatObjects::CallManager(const TCHAR* Function)
{
	UFunction* Found = Manager ? Manager->FindFunction(Function) : nullptr;
	if (!Found)
	{
		return;
	}
	// The manager's functions take no inputs, but may have outputs or locals.
	TArray<uint8> Params;
	Params.SetNumZeroed(Found->ParmsSize);
	Manager->ProcessEvent(Found, Params.GetData());
}

void APipecatObjects::Interact()
{
	CallManager(TEXT("Interact"));
}

void APipecatObjects::SelectUp()
{
	CallManager(TEXT("Select Index UP"));
}

void APipecatObjects::SelectDown()
{
	CallManager(TEXT("Select Index Down"));
}

void APipecatObjects::Spawn(const FString& Name)
{
	const FPipecatObjectKind* Kind = Kinds.FindByPredicate(
		[&Name](const FPipecatObjectKind& Candidate) { return Candidate.Name.Equals(Name, ESearchCase::IgnoreCase); });
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Kind || !Player)
	{
		UE_LOG(LogPipecatObjects, Warning, TEXT("Unable to add a %s"), *Name);
		return;
	}
	UObject* Asset = Kind->Asset.TryLoad();

	// The first free spot in front of the player, along an arc.
	FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
	FVector Center = Player->GetActorLocation();
	FVector Location = Center + Forward * Distance;
	for (int32 Try = 0; Try < 12; Try++)
	{
		float Angle = (Try % 2 ? 1.0f : -1.0f) * ((Try + 1) / 2) * 25.0f;
		float Reach = Distance + (Try / 7) * Spacing;
		FVector Candidate = Center + Forward.RotateAngleAxis(Angle, FVector::UpVector) * Reach;
		bool bFree = !Spawned.ContainsByPredicate([&Candidate](const TObjectPtr<AActor>& Object) {
			return IsValid(Object) && FVector::Dist2D(Object->GetActorLocation(), Candidate) < Spacing;
		});
		if (bFree)
		{
			Location = Candidate;
			break;
		}
	}
	// Facing the player. Tick puts it on the ground.
	FRotator Rotation((Center - Location).GetSafeNormal2D().Rotation());
	FTransform Transform(Rotation, Location, FVector(Kind->Scale));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Object = nullptr;
	if (UClass* Class = Cast<UClass>(Asset); Class && Class->IsChildOf<AActor>())
	{
		Object = GetWorld()->SpawnActor<AActor>(Class, Transform, Params);
	}
	else if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset))
	{
		AStaticMeshActor* MeshActor = GetWorld()->SpawnActor<AStaticMeshActor>(Transform.GetLocation(), Rotation, Params);
		if (MeshActor)
		{
			MeshActor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			MeshActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			MeshActor->SetActorScale3D(FVector(Kind->Scale));
		}
		Object = MeshActor;
	}
	if (!Object)
	{
		UE_LOG(LogPipecatObjects, Warning, TEXT("Unable to add a %s from %s"), *Name, *Kind->Asset.ToString());
		return;
	}
	Spawned.Add(Object);
	UE_LOG(LogPipecatObjects, Log, TEXT("Added a %s at %s"), *Name, *Location.ToCompactString());
}

void APipecatObjects::Clear()
{
	for (AActor* Object : Spawned)
	{
		if (IsValid(Object))
		{
			Object->Destroy();
		}
	}
	Spawned.Empty();
}
