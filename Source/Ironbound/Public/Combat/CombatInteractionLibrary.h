#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Combat/CombatInteractionTypes.h"
#include "CombatInteractionLibrary.generated.h"

class UDataTable;
class USkeletalMeshComponent;

/**
 * Stateless interaction resolution seam.
 *
 * Resolve turns a typed interaction into a typed result. Today that means the
 * transitional CompatibilityDamage taken from the source technique row
 * (falling back to the fixed prototype damage of 10 when the row is unknown);
 * tomorrow the physical damage model replaces the body of this function
 * without changing the call contract. It owns no state and does no routing.
 */
UCLASS()
class IRONBOUND_API UCombatInteractionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Fixed damage reproduced for regression compatibility while no technique row is available. */
	static constexpr float DefaultCompatibilityDamage = 10.f;

	/**
	 * Resolves the interaction.
	 *
	 * TechniquesTable is the DT_CombatTechniques table used to read the source
	 * technique's damage profile. Passing null falls back to the default
	 * compatibility damage.
	 */
	UFUNCTION(BlueprintCallable, Category="Combat|Interaction")
	static FCombatInteractionResult Resolve(
		const FCombatInteraction& Interaction,
		const UDataTable* TechniquesTable);

	/** Native hit path that also applies the actual anatomical target row. */
	static FCombatInteractionResult ResolveWithTargets(
		const FCombatInteraction& Interaction,
		const UDataTable* TechniquesTable,
		const UDataTable* CombatTargets);

	/** Map a physics hit bone directly, or use nearest configured target bone for capsule hits. */
	static FName ResolveBodyRegion(
		const UDataTable* CombatTargets,
		USkeletalMeshComponent* ReceiverMesh,
		FName HitBone,
		const FVector& ContactPoint,
		FName& OutResolvedBone,
		bool& bOutUsedFallback);
};
