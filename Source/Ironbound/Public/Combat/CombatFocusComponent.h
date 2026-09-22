#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CombatFocusComponent.generated.h"

class UFighterComponent;

/**
 * Target STATE: "the target this fighter is currently focused on".
 *
 * Target SELECTION is controller policy (AI decision or player input) and
 * lives with the controller; this component only stores and validates the
 * selection. Auto-acquisition is deliberately NOT here anymore: controllers
 * call SetCombatTarget. CombatFocus is also not the threat system - threats
 * are measured by UCombatThreatComponent regardless of what this fighter is
 * focused on.
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UCombatFocusComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UCombatFocusComponent();

	/**
	 * Runtime battle team, read through the fighter component (single team
	 * source; no duplicated team state here).
	 */
	UFUNCTION(BlueprintPure, Category="Combat|Focus") int32 GetTeamId() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat") bool bDead = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat") TObjectPtr<AActor> CombatTarget;

	UFUNCTION(BlueprintCallable, Category="Combat|Focus") bool SetCombatTarget(AActor* Target);
	UFUNCTION(BlueprintPure, Category="Combat|Focus") AActor* GetCombatTarget() const;
	UFUNCTION(BlueprintCallable, Category="Combat|Focus") void MarkDead();
	bool IsEnemy(const AActor* Candidate) const;

	/**
	 * Deprecated bridge: team now lives on the fighter component. Forwards to
	 * UFighterComponent::SetBattleTeamId so old initialization still reaches
	 * the new authority. Remove with the pawn BP rewiring.
	 */
	UFUNCTION(BlueprintCallable, Category="Deprecated", meta=(DeprecatedFunction, DeprecationMessage="Team now lives on the Fighter component; register the fighter with the BattleManager instead."))
	void InitializeFocus(int32 FighterTeam);

protected:
	UFighterComponent* GetFighter() const;

	virtual void BeginPlay() override;
};
