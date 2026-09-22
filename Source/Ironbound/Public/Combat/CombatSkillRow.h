#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatTechniqueRow.h"
#include "CombatSkillRow.generated.h"

/**
 * Shared learnable/executable skill catalog row.
 *
 * The same stable id is used for fighter knowledge, battle preparation, AI
 * choice and execution. Executor-specific configs keep animation, procedural
 * and future paired-action behavior distinct without splitting the catalog.
 */
USTRUCT(BlueprintType)
struct IRONBOUND_API FCombatSkillRow : public FCombatTechniqueRow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skill")
	FText Description;
};
