#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CombatTarget.generated.h"

/**
 * One anatomical target region that the combat solver may try to hit.
 *
 * Score represents how desirable the target is to the combat planner.
 * It does NOT represent damage.
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
};
