#pragma once

#include "CoreMinimal.h"
#include "FighterTypes.generated.h"

/**
 * Persistent, battle-independent fighter attributes.
 *
 * Copied into UFighterComponent::BattleAttributes when a battle starts. The persistent
 * copy is never written back, so a battle cannot corrupt what the fighter owns outside it.
 */
USTRUCT(BlueprintType)
struct IRONBOUND_API FFighterAttributes
{
	GENERATED_BODY()

	/** Physical force the fighter can produce. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fighter|Attributes", meta=(ClampMin="0.0"))
	float Strength = 1.f;

	/** How accurately and efficiently the fighter follows intended motion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fighter|Attributes", meta=(ClampMin="0.0"))
	float Agility = 1.f;

	/** How long the fighter can sustain physical effort. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fighter|Attributes", meta=(ClampMin="0.0"))
	float Endurance = 1.f;
};

/**
 * A skill this fighter has learned.
 *
 * SkillId is a stable identifier, never a display name: it matches the row name in
 * DT_AttackMasterMoves (for example "Attack_001"). Renaming a move's DisplayName or
 * moving its montage therefore cannot break a saved loadout.
 */
USTRUCT(BlueprintType)
struct IRONBOUND_API FLearnedSkill
{
	GENERATED_BODY()

	/** Stable skill identifier; the row name of the move in the attack master table. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fighter|Skills")
	FName SkillId;

	/** Mastery of the skill. 0 means learned but unpractised. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fighter|Skills", meta=(ClampMin="0.0"))
	float Proficiency = 0.f;
};

/**
 * Persistent fighter identity: who this fighter is, independent of any battle.
 *
 * Deliberately contains no team: a fighter's side is runtime battle state and lives on
 * UFighterComponent/ABattleManager instead.
 */
USTRUCT(BlueprintType)
struct IRONBOUND_API FFighter
{
	GENERATED_BODY()

	/** Stable identity, preserved across battles and saves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fighter")
	FGuid Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fighter")
	FText Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fighter")
	FFighterAttributes Attributes;

	/** Everything the fighter has learned. Only a subset of this is taken into battle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fighter")
	TArray<FLearnedSkill> LearnedSkills;
};

/**
 * One side in a battle.
 *
 * Runtime only: rebuilt by ABattleManager from the spawned fighter actors, and
 * deliberately not part of FFighter.
 */
USTRUCT(BlueprintType)
struct IRONBOUND_API FBattleTeam
{
	GENERATED_BODY()

	/** Team identifier. 0 is reserved for unteamed observers and is never a valid side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle")
	int32 TeamId = 0;

	/** Spawned fighter actors currently on this team, added uniquely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle")
	TArray<TObjectPtr<AActor>> Fighters;
};
