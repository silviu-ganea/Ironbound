#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/FighterTypes.h"
#include "FighterComponent.generated.h"

class UDataTable;

/**
 * A single fighter: persistent identity plus the runtime state of one battle.
 *
 * This component is the only owner of fighter identity, attributes, skills,
 * techniques repertoire, weapon proficiency and team state. The physical
 * fighter is a separate actor (for example SandboxCharacter_Mover), which owns
 * movement, animation, body physics and equipment but no second copy of the
 * fighter's identity.
 *
 * Persistent state (FighterData) is authored once and never mutated by combat.
 * Battle-scoped state (BattleAttributes, BattleRepertoire, BattleTeamId) is
 * assigned per battle. BattleTeamId is runtime state and is not part of
 * FFighter.
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UFighterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFighterComponent();

	/** Persistent identity, attributes, learned skills and weapon proficiencies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fighter")
	FFighter FighterData;

	/**
	 * Mutable per-battle copy of FighterData.Attributes.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fighter|Battle")
	FFighterAttributes BattleAttributes;

	/**
	 * BATTLE REPERTOIRE: the TECHNIQUES selected/prepared for this battle.
	 *
	 * Entries are technique ids (row names of DT_CombatTechniques), never
	 * skill ids. A technique references its RequiredSkills; availability is
	 * computed by the technique component from this repertoire, the learned
	 * skills, weapon proficiency and current equipment.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fighter|Battle")
	TArray<FName> BattleRepertoire;

	/**
	 * Optional reference to DT_CombatTechniques used by SelectBattleTechnique
	 * to validate a technique's RequiredSkills against the learned skills.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fighter|Battle")
	TObjectPtr<UDataTable> BattleTechniquesTable;

	/**
	 * The subset of the fighter's learned skills selected for the current battle.
	 *
	 * Deprecated legacy loadout (skill ids of the retired attack-row era).
	 * Kept temporarily for existing Blueprint wiring; use BattleRepertoire.
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

	/** Assigns the runtime battle team. This component is the single team authority. */
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

	// ===== Skills (knowledge) =====

	/** Adds a skill to the persistent learned list, or updates it when the ID already exists. */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Fighter")
	bool LearnSkill(const FLearnedSkill& Skill);

	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	bool HasLearnedSkill(FName SkillId) const;

	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	bool FindLearnedSkill(FName SkillId, FLearnedSkill& OutSkill) const;

	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	int32 GetLearnedSkillCount() const { return FighterData.LearnedSkills.Num(); }

	// ===== Weapon proficiency (belonging to the fighter, not the weapon) =====

	UFUNCTION(BlueprintCallable, Category="Ironbound|Fighter")
	bool SetWeaponProficiency(const FFighterWeaponProficiency& Proficiency);

	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	float GetWeaponProficiency(const FGameplayTag& FamilyTag, float FallbackProficiency = 0.f) const;

	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	TArray<FFighterWeaponProficiency> GetWeaponProficiencies() const { return FighterData.WeaponProficiencies; }

	// ===== Battle repertoire (techniques) =====

	/**
	 * Prepares a technique for this battle. Validates the technique's
	 * RequiredSkills against the learned skills when the technique table is
	 * available; the technique id must reference DT_CombatTechniques rows, not
	 * skill rows.
	 */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Fighter")
	bool SelectBattleTechnique(FName TechniqueId);

	UFUNCTION(BlueprintCallable, Category="Ironbound|Fighter")
	bool RemoveBattleTechnique(FName TechniqueId);

	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	bool IsBattleTechniqueSelected(FName TechniqueId) const;

	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	TArray<FName> GetBattleRepertoire() const { return BattleRepertoire; }

	UFUNCTION(BlueprintCallable, Category="Ironbound|Fighter")
	void ClearBattleRepertoire();

	/** Bumped whenever the repertoire changes; availability caches watch this. */
	UFUNCTION(BlueprintPure, Category="Ironbound|Fighter")
	int32 GetRepertoireRevision() const { return RepertoireRevision; }

	// ===== Deprecated legacy loadout (skill ids) =====

	UFUNCTION(BlueprintCallable, Category="Deprecated", meta=(DeprecatedFunction, DeprecationMessage="Battle repertoire holds technique ids now; use SelectBattleTechnique."))
	bool SelectBattleSkill(FName SkillId);

	UFUNCTION(BlueprintCallable, Category="Deprecated", meta=(DeprecatedFunction, DeprecationMessage="Use RemoveBattleTechnique."))
	bool RemoveBattleSkill(FName SkillId);

	UFUNCTION(BlueprintPure, Category="Deprecated", meta=(DeprecatedFunction, DeprecationMessage="Use IsBattleTechniqueSelected."))
	bool IsBattleSkillSelected(FName SkillId) const;

	UFUNCTION(BlueprintPure, Category="Deprecated", meta=(DeprecatedFunction, DeprecationMessage="Use GetBattleRepertoire."))
	TArray<FLearnedSkill> GetBattleSkills() const { return BattleSkills; }

	UFUNCTION(BlueprintCallable, Category="Deprecated", meta=(DeprecatedFunction, DeprecationMessage="Use ClearBattleRepertoire."))
	void ClearBattleSkills();

	// ===== Team relations =====

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

private:
	int32 RepertoireRevision = 0;
};
