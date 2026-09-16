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
	/** Weapon-local to hand-bone-local transform, including the grip socket offset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	FTransform WeaponToHand;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	FVector BladeBase = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	FVector BladeTip = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon", meta=(ClampMin="0.01"))
	float MassKg = 2.5f;
};

/** Owns equip/unequip and the intended-trajectory cache, independently of AI decisions. */
UCLASS(ClassGroup=(Ironbound), meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UIronboundEquipmentComponent : public UActorComponent
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
	bool InitializeEquipment(USkeletalMeshComponent* InFighterMesh, UStaticMeshComponent* InWeapon,
		UPhysicsConstraintComponent* InConstraint);
	/** Menu/loadout entry point. Rejects replacement during a committed action. */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Equipment")
	bool EquipWeapon(UIronboundWeaponDefinition* NewDefinition);
	UFUNCTION(BlueprintCallable, Category="Ironbound|Equipment")
	bool UnequipWeapon();
	UFUNCTION(BlueprintCallable, Category="Ironbound|Equipment")
	void InvalidateTrajectories();
	UFUNCTION(BlueprintCallable, Category="Ironbound|Equipment")
	bool GetTrajectory(UAnimSequenceBase* Sequence, float StartTime, float EndTime, int32 NumSamples,
		FBladeTrajectory& OutTrajectory);
	USkeletalMeshComponent* GetFighterMesh() const { return FighterMesh; }
	UStaticMeshComponent* GetWeapon() const { return Weapon; }

private:
	UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> FighterMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Weapon;
	UPROPERTY(Transient) TObjectPtr<UPhysicsConstraintComponent> Constraint;
	UPROPERTY(Transient) TMap<FString, FBladeTrajectory> Trajectories;
};
