#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/CombatInteractionTypes.h"
#include "CombatReactionComponent.generated.h"

class UFighterVitalsComponent;

/**
 * Damage intake for one fighter.
 *
 * Attackers talk to this component; it owns no health of its own and forwards the hit to the
 * owner's UFighterVitalsComponent, which owns health, the team gate and death. Use it as the
 * single "this fighter was hit" entry point so hit intake stays off the owning actor's
 * Blueprint.
 */
UCLASS(ClassGroup=(Ironbound), meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UCombatReactionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatReactionComponent();

	/**
	 * Hit intake target. Leave empty to use the owner's CombatVitals component, which is what
	 * a fighter normally wants; set it only to route this fighter's hits somewhere else.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Reaction")
	TObjectPtr<UFighterVitalsComponent> Vitals;

	/** Resolves the intake target from the owner and caches it. Returns null if there is none. */
	UFUNCTION(BlueprintPure, Category="Ironbound|Combat|Reaction")
	UFighterVitalsComponent* GetVitals();

	/** True when this fighter is alive and has somewhere to take damage. */
	UFUNCTION(BlueprintPure, Category="Ironbound|Combat|Reaction")
	bool CanReceiveCombatHit();

	/**
	 * A hit landed on this fighter. Returns true when the damage was accepted.
	 *
	 * The decision itself - already dead, unteamed defender, friendly fire - belongs to the
	 * health owner (UFighterVitalsComponent::ReceiveCombatHit), so this only forwards.
	 */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat|Reaction")
	bool ReceiveCombatHit(float DamageAmount, int32 AttackerTeam);

	/**
	 * Typed interaction intake. This is the proper combat path: the caller
	 * builds the FCombatInteraction from the physical contact, resolves it
	 * through UCombatInteractionLibrary, and feeds the typed result here.
	 * ReceiveCombatHit remains as the transitional untyped seam.
	 */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat|Reaction")
	bool ReceiveInteraction(
		const FCombatInteraction& Interaction,
		const FCombatInteractionResult& Result);

	/** Team this fighter reacts for, read from the same authority combat uses. */
	UFUNCTION(BlueprintPure, Category="Ironbound|Combat|Reaction")
	int32 GetReactionTeam();

protected:
	virtual void BeginPlay() override;
};
