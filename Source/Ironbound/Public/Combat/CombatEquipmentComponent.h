#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/CombatTrajectoryLibrary.h"
#include "CombatEquipmentComponent.generated.h"

class UPhysicsConstraintComponent;
class UPrimitiveComponent;
class UWeaponDefinition;

/** Owns equip/unequip, weapon contact classification, and intended-trajectory cache. */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UCombatEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Equipment")
	TObjectPtr<UWeaponDefinition> Definition;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Equipment")
	int32 Revision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Equipment")
	bool bReady = false;

	UFUNCTION(BlueprintCallable, Category="Combat|Equipment")
	bool InitializeEquipment(
		USkeletalMeshComponent* InFighterMesh,
		UStaticMeshComponent* InWeapon,
		UPhysicsConstraintComponent* InConstraint);

	UFUNCTION(BlueprintCallable, Category="Combat|Equipment")
	bool EquipWeapon(UWeaponDefinition* NewDefinition);

	UFUNCTION(BlueprintCallable, Category="Combat|Equipment")
	bool UnequipWeapon();

	UFUNCTION(BlueprintCallable, Category="Combat|Equipment")
	void InvalidateTrajectories();

	/** True only when OtherComp is the opponent's skeletal body mesh. */
	UFUNCTION(BlueprintPure, Category="Combat|Equipment|Contact")
	bool IsBodyContact(UPrimitiveComponent* OtherComp) const;

	/** True only when OtherComp is the equipped weapon component of OtherActor. */
	UFUNCTION(BlueprintPure, Category="Combat|Equipment|Contact")
	bool IsBladeContact(AActor* OtherActor, UPrimitiveComponent* OtherComp) const;

	/** Records a sword-on-sword contact. This intentionally does not apply damage. */
	UFUNCTION(BlueprintCallable, Category="Combat|Equipment|Contact")
	void NotifyBladeContact(AActor* OtherActor, const FHitResult& Hit);

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

	UPhysicsConstraintComponent* GetConstraint() const
	{
		return Constraint;
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
