#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "WeaponDefinition.generated.h"

/** One authored hand/grip relationship for this weapon. */
USTRUCT(BlueprintType)
struct IRONBOUND_API FWeaponGrip
{
	GENERATED_BODY()

	/** Stable grip identifier (for example "RightHand", "LeftHand"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon Grip")
	FName GripId;

	/** Weapon-local to hand-space transform for this grip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon Grip")
	FTransform WeaponToHand = FTransform::Identity;
};

/** One authored contact surface of the weapon (strike edge, parry flat, etc.). */
USTRUCT(BlueprintType)
struct IRONBOUND_API FWeaponSurface
{
	GENERATED_BODY()

	/** Stable surface identifier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon Surface")
	FName SurfaceId;

	/** Role of this surface (Weapon.Surface.Strike / Weapon.Surface.Parry ...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon Surface")
	FGameplayTag Role;

	/** Surface start, in weapon-local space. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon Surface")
	FVector Base = FVector::ZeroVector;

	/** Surface end, in weapon-local space. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon Surface")
	FVector Tip = FVector::ZeroVector;
};

/**
 * Physical description of a weapon tool.
 *
 * A weapon definition describes the object only: mesh, mass, blade geometry,
 * weapon family, grips and contact surfaces. It never grants combat
 * knowledge; technique availability combines the battle repertoire, learned
 * skills, weapon proficiency and current equipment instead.
 */
UCLASS(BlueprintType)
class IRONBOUND_API UWeaponDefinition : public UDataAsset
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

	/** Weapon family this tool belongs to (Weapon.Family.Sword, ...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon|Family")
	FGameplayTag FamilyTag;

	/** Authored grips. The first entry mirrors the legacy WeaponToHand when authored. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon|Grips")
	TArray<FWeaponGrip> Grips;

	/** Authored contact surfaces (strike edge, parry band, ...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon|Surfaces")
	TArray<FWeaponSurface> Surfaces;

	/** Returns the authored grip with the given id, or null when absent. */
	const FWeaponGrip* FindGrip(FName GripId) const;

	/** Returns the first authored surface with the given role tag, or null. */
	const FWeaponSurface* FindSurfaceByRole(const FGameplayTag& Role) const;
};
