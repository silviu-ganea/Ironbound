#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/FighterTypes.h"
#include "FighterComponent.generated.h"

class UIronboundCombatFocusComponent;

/**
 * A single fighter: persistent identity plus the runtime state of one battle.
 *
 * This component is the only owner of fighter identity, attributes, skills and team
 * state. The physical fighter is a separate actor (for example SandboxCharacter_Mover),
 * which owns movement, animation, body physics and equipment but no second copy of the
 * fighter's identity.
 *
 * Persistent state (FighterData) is authored once and never mutated by combat.
 * Battle-scoped state (BattleAttributes, BattleSkills, BattleTeamId) is assigned per
 * battle. BattleTeamId is runtime state and is not part of FFighter.
 */
UCLASS(ClassGroup=(Ironbound), meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UFighterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFighterComponent();

	/** Persistent identity, attributes and learned skills. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fighter")
	FFighter FighterData;

	/** Mutable per-battle copy of FighterData.Attributes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fighter|Battle")
	FFighterAttributes BattleAttributes;

	/**
	 * The subset of the fighter's learned skills selected for the current battle.
	 *
	 * Not filled from FighterData.LearnedSkills: a battle loadout is a selection, so a skill
	 * the fighter has learned is not automatically one it brings into this fight. Populate it
	 * with SelectBattleSkill/SetBattleSkills.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fighter|Battle")
	TArray<FLearnedSkill> BattleSkills;

	/**
	 * Runtime battle team. Assigned by ABattleManager::RegisterFighter; 0 means the fighter
	 * is not on a side yet.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fighter|Battle")
	int32 BattleTeamId = 0;

	/**
	 * Copies persistent data into per-battle state. Call before registering the fighter for a
	 * battle; registration is what assigns the team.
	 */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Fighter")
	void InitializeFighter(const FFighter& InFighterData);

	/** Assigns the runtime battle team, keeping the legacy combat readers in step. */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Fighter")
	void SetBattleTeamId(int32 InTeamId);

	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	int32 GetBattleTeamId() const { return BattleTeamId; }

	/** True once the fighter has been put on a real side (Team 0 is not a side). */
	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	bool HasTeam() const { return BattleTeamId > 0; }

	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	FFighter GetFighterData() const { return FighterData; }

	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	FGuid GetFighterId() const { return FighterData.Id; }

	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	FText GetFighterName() const { return FighterData.Name; }

	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	FFighterAttributes GetBattleAttributes() const { return BattleAttributes; }

	UFUNCTION(BlueprintCallable, Category="Ironbound|Fighter")
	void SetBattleAttributes(const FFighterAttributes& InAttributes);

	/** Adds a skill to the persistent learned list, or updates it when the ID already exists. */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Fighter")
	bool LearnSkill(const FLearnedSkill& Skill);

	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	bool HasLearnedSkill(FName SkillId) const;

	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	bool FindLearnedSkill(FName SkillId, FLearnedSkill& OutSkill) const;

	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	int32 GetLearnedSkillCount() const { return FighterData.LearnedSkills.Num(); }

	/**
	 * Puts a learned skill into this battle's loadout. Fails for a skill the fighter never
	 * learned, so the loadout can never reference a skill the fighter does not know.
	 */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Fighter")
	bool SelectBattleSkill(FName SkillId);

	UFUNCTION(BlueprintCallable, Category="Ironbound|Fighter")
	bool RemoveBattleSkill(FName SkillId);

	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	bool IsBattleSkillSelected(FName SkillId) const;

	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	TArray<FLearnedSkill> GetBattleSkills() const { return BattleSkills; }

	UFUNCTION(BlueprintCallable, Category="Ironbound|Fighter")
	void ClearBattleSkills();

	/** Same valid team. Self and unteamed (Team 0) fighters are never allies. */
	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	bool IsAlly(const AActor* Other) const;

	/** Different valid teams. Self and unteamed (Team 0) fighters are never enemies. */
	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	bool IsEnemy(const AActor* Other) const;

	/**
	 * Reads any spawned fighter's team straight off its fighter component, without needing a
	 * battle manager. Returns 0 when the actor has no fighter component or no team.
	 */
	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	static int32 GetFighterTeamId(const AActor* FighterActor);
};
