#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CombatTarget.generated.h"

/**
 * One anatomical target region that the combat solver may try to hit.
 *
 * Score represents how desirable the target is to the combat planner.
 * DamageMultiplier changes damage after the actual hit region is resolved.
 * ContactRadiusCm approximates this region's hit volume around its listed bones.
 */
USTRUCT(BlueprintType)
struct IRONBOUND_API FCombatTargetRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Skeletal bones that belong to this target region. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Target")
	TArray<FName> Bones;

	/** Relative desirability of successfully hitting this region. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Target",
		meta=(ClampMin="0.0"))
	float Score = 1.0f;

	/** Damage scale for a confirmed hit in this region. Zero uses the legacy region fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Target|Damage",
		meta=(ClampMin="0.0"))
	float DamageMultiplier = 0.0f;

	/** Radius in cm around each listed bone used by trajectory and capsule-hit region queries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Target|Geometry",
		meta=(ClampMin="0.0"))
	float ContactRadiusCm = 0.0f;
};

/** Compatibility values used until older DT_CombatTargets rows are authored with the new fields. */
namespace CombatTargetRules
{
	inline float GetContactRadiusCm(FName Region, const FCombatTargetRow& Row)
	{
		if (Row.ContactRadiusCm > 0.f)
		{
			return Row.ContactRadiusCm;
		}

		if (Region == TEXT("Head")) return 14.f;
		if (Region == TEXT("Torso")) return 26.f;
		if (Region == TEXT("Hands")) return 8.f;
		return 10.f;
	}

	inline float GetDamageMultiplier(FName Region, const FCombatTargetRow& Row)
	{
		if (Row.DamageMultiplier > 0.f)
		{
			return Row.DamageMultiplier;
		}

		if (Region == TEXT("Head")) return 1.75f;
		if (Region == TEXT("Hands")) return 0.55f;
		return 1.f;
	}
}
