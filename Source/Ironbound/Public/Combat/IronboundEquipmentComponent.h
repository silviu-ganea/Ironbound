#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "Combat/IronboundTrajectoryLibrary.h"
#include "IronboundEquipmentComponent.generated.h"

class UPhysicsConstraintComponent;

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

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon", meta=(ClampMin="0.01"))
    float MassKg = 2.5f;

    // Very stiff grip for the prototype. The sword should behave as if firmly
    // held by hand_r instead of rotating inside a soft angular constraint.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon|Grip", meta=(ClampMin="0.0"))
    float GripAngularStiffness = 2500.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon|Grip", meta=(ClampMin="0.0"))
    float GripAngularDamping = 80.f;
};

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
    bool InitializeEquipment(
        USkeletalMeshComponent* InFighterMesh,
        UStaticMeshComponent* InWeapon,
        UPhysicsConstraintComponent* InConstraint);

    UFUNCTION(BlueprintCallable, Category="Ironbound|Equipment")
    bool EquipWeapon(UIronboundWeaponDefinition* NewDefinition);

    UFUNCTION(BlueprintCallable, Category="Ironbound|Equipment")
    bool UnequipWeapon();

    UFUNCTION(BlueprintCallable, Category="Ironbound|Equipment")
    void InvalidateTrajectories();

    bool GetTrajectory(UAnimSequenceBase* Sequence, FBladeTrajectory& OutTrajectory);

    USkeletalMeshComponent* GetFighterMesh() const { return FighterMesh; }
    UStaticMeshComponent* GetWeapon() const { return Weapon; }

private:
    UPROPERTY(Transient)
    TObjectPtr<USkeletalMeshComponent> FighterMesh;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> Weapon;

    UPROPERTY(Transient)
    TObjectPtr<UPhysicsConstraintComponent> Constraint;

    TMap<FString, FBladeTrajectory> Trajectories;
};
