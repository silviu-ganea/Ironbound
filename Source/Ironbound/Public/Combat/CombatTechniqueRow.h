#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "CombatTechniqueRow.generated.h"

class UCombatTechniqueExecutionConfig;

/**
 * How a technique is brought into play.
 *
 * Deliberate: the controller chooses to start it (attack, takedown...).
 * Reactive: the controller answers an observed incoming threat with it (parry).
 * Both kinds travel through the same origin-agnostic RequestTechnique API.
 */
UENUM(BlueprintType)
enum class ECombatTechniqueKind : uint8
{
	Deliberate,
	Reactive
};

/**
 * Which body regions a technique occupies while executing.
 *
 * Vocabulary only: this does not arbitrate body resources today, it just
 * records the scope so future body-region arbitration does not require
 * replacing the execution architecture.
 */
USTRUCT(BlueprintType)
struct IRONBOUND_API FCombatBodyScope
{
	GENERATED_BODY()

	/** Body regions this technique drives (Body.* gameplay tags). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat")
	FGameplayTagContainer BodyRegions;

	/** Whether general locomotion may continue while this technique executes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat")
	bool bAllowsLocomotion = true;
};

/**
 * Transitional damage data carried by a technique row.
 *
 * CompatibilityDamage reproduces the current fixed prototype damage and is
 * explicitly NOT the final physical damage architecture: the physical inputs
 * (contact point, normal, impulse, weapon velocity/mass, body region) are
 * preserved in FCombatInteraction for the future physical resolution.
 */
USTRUCT(BlueprintType)
struct IRONBOUND_API FCombatDamageProfile
{
	GENERATED_BODY()

	/** Fixed compatibility damage applied per resolved hit while the physical damage model does not exist. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat", meta=(ClampMin="0.0"))
	float CompatibilityDamage = 0.f;

	/** Scale applied to the physical impulse carried by the interaction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat", meta=(ClampMin="0.0"))
	float ImpulseScale = 1.f;
};

/**
 * A concrete combat action definition (for example Sword_Overhead_01,
 * Sword_Thrust_01, Sword_Parry).
 *
 * The row name IS the technique id. A technique references learned skills
 * (RequiredSkills), constrains the weapon family, describes the body scope,
 * and points at the executor-specific execution config DataAsset. Common
 * authored domain information lives here; executor-specific configuration
 * lives in that config, never in a growing heterogeneous row.
 */
USTRUCT(BlueprintType)
struct IRONBOUND_API FCombatTechniqueRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Technique")
	FText DisplayName;

	/** Category and trigger tags (Technique.Category.*, Technique.Trigger.*). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Technique")
	FGameplayTagContainer Tags;

	/** Whether controllers start this deliberately or reactively. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Technique")
	ECombatTechniqueKind Kind = ECombatTechniqueKind::Deliberate;

	/** Skill ids (not technique ids) that must be learned to use this technique. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Technique")
	TArray<FName> RequiredSkills;

	/** Weapon family required (Weapon.Family.*). Empty means any equipment. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Technique")
	FGameplayTag WeaponFamilyTag;

	/** Accepted family tags, when an action supports more than one weapon family. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Technique|Compatibility")
	FGameplayTagContainer CompatibleWeaponFamilies;

	/** Accepted handling classes such as OneHanded or TwoHanded. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Technique|Compatibility")
	FGameplayTagContainer CompatibleWeaponClasses;

	/** Minimum family/class proficiency resolved by the equipped weapon profile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Technique|Compatibility", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MinimumWeaponProficiency = 0.f;

	/** Deliberate techniques that fight an opponent require an explicit request target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Technique")
	bool bRequiresCombatTarget = false;

	/** Whether this reactive technique can intercept an incoming strike for its active duration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Technique|Defense")
	bool bBlocksIncomingStrike = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Technique")
	FCombatBodyScope BodyScope;

	/** Executor binding + executor-specific authored configuration (hard reference). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Technique")
	TObjectPtr<UCombatTechniqueExecutionConfig> ExecutionConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Technique")
	FCombatDamageProfile DamageProfile;
};
