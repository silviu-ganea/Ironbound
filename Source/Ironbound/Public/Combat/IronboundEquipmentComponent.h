#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "Combat/IronboundTrajectoryLibrary.h"
#include "IronboundEquipmentComponent.generated.h"

class UPhysicsConstraintComponent;


/** Authored weapon geometry; never inferred from a running physics simulation. */
UCLASS(BlueprintType)
class IRONBOUND_API UIronboundWeaponDefinition : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	TObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	FName HandBone = TEXT("hand_r");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	FTransform WeaponToHand;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	FVector BladeBase = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	FVector BladeTip = FVector::ZeroVector;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category="Weapon",
		meta=(ClampMin="0.01"))
	float MassKg = 2.5f;
};


/** Owns equip/unequip and intended-trajectory cache. */
UCLASS(
	ClassGroup=(Ironbound),
	meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UIronboundEquipmentComponent :
	public UActorComponent
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Equipment")
	TObjectPtr<UIronboundWeaponDefinition> Definition;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Equipment")
	int32 Revision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Equipment")
	bool bReady = false;


	UFUNCTION(BlueprintCallable, Category="Ironbound|Equipment")
	bool InitializeEquipment(
		USkeletalMeshComponent* InFighterMesh,
		UStaticMeshComponent* InWeapon,
		UPhysicsConstraintComponent* InConstraint);

	UFUNCTION(BlueprintCallable, Category="Ironbound|Equipment")
	bool EquipWeapon(
		UIronboundWeaponDefinition* NewDefinition);

	UFUNCTION(BlueprintCallable, Category="Ironbound|Equipment")
	bool UnequipWeapon();

	UFUNCTION(BlueprintCallable, Category="Ironbound|Equipment")
	void InvalidateTrajectories();

	/**
	 * Returns the automatically derived trajectory for this animation.
	 * No authored windows or sample count are required.
	 */
	bool GetTrajectory(
		UAnimSequenceBase* Sequence,
		FBladeTrajectory& OutTrajectory);

	USkeletalMeshComponent* GetFighterMesh() const
	{
		return FighterMesh;
	}

	UStaticMeshComponent* GetWeapon() const
	{
		return Weapon;
	}

private:

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> FighterMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Weapon;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicsConstraintComponent> Constraint;

	TMap<FString, FBladeTrajectory> Trajectories;
};