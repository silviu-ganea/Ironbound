#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "IronboundPawnSkillsetComponent.generated.h"

/**
 * DEPRECATED legacy loadout (AttackMoves into DT_AttackMasterMoves).
 *
 * Fighter knowledge now lives on UFighterComponent (LearnedSkills /
 * WeaponProficiencies); battle technique selection lives in
 * UFighterComponent::BattleRepertoire (technique ids into DT_CombatTechniques).
 *
 * Kept only so assets holding an instance (pawn SCS component, level
 * instances) keep loading before the manual Blueprint cleanup. It has no
 * behavior. After the pawn BP drops this component, delete this class
 * entirely.
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent, DeprecatedProperty))
class IRONBOUND_API UIronboundPawnSkillsetComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIronboundPawnSkillsetComponent();

	/** Deprecated: old attack-row loadout; superseded by the battle repertoire. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Loadout")
	TArray<FDataTableRowHandle> AttackMoves;

	/** Deprecated: always dead; parry is now requested like any technique. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Loadout")
	bool bCanParry = false;

	UFUNCTION(BlueprintPure, Category="Deprecated")
	bool HasAttacks() const;

	UFUNCTION(BlueprintPure, Category="Deprecated")
	int32 GetAttackCount() const;

	UFUNCTION(BlueprintCallable, Category="Deprecated")
	bool GetAttack(int32 Index, FDataTableRowHandle& OutAttack) const;
};
