#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/CombatExecutionTypes.h"
#include "CombatTechniqueComponent.generated.h"

class UCombatEquipmentComponent;
class UDataTable;
class UFighterComponent;
class UFighterVitalsComponent;
struct FCombatTechniqueRow;

/**
 * Stable technique availability + transient execution validation.
 *
 * This is the pawn's authoritative availability registry: it combines the
 * battle repertoire (technique ids), the fighter's learned skills (technique
 * RequiredSkills), weapon proficiency and the current equipment state.
 *
 * Availability is stable state and only recomputes when its inputs change
 * (equipment Revision, repertoire revision). Transient CanExecute is re-run
 * at request time by the execution component, which performs the
 * authoritative validation.
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UCombatTechniqueComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatTechniqueComponent();

	/**
	 * Technique table (DT_CombatTechniques). The pawn Blueprint assigns this
	 * during the manual data setup until a native default owner exists.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Techniques")
	TObjectPtr<UDataTable> TechniquesTable;

	/** True when the table is assigned, so availability queries are meaningful. */
	UFUNCTION(BlueprintPure, Category="Combat|Techniques")
	bool IsConfigured() const { return TechniquesTable != nullptr; }

	/** Row lookup; null when the table is missing or the id is unknown. (C++ seam) */
	const FCombatTechniqueRow* FindRow(FName TechniqueId) const;

	/** Stable availability: repertoire + learned skills + weapon family/equipment. */
	UFUNCTION(BlueprintPure, Category="Combat|Techniques")
	bool IsAvailable(FName TechniqueId) const;
	FString GetAvailabilityFailureReason(FName TechniqueId) const;

	/** All currently available technique ids. */
	UFUNCTION(BlueprintPure, Category="Combat|Techniques")
	TArray<FName> GetAvailableTechniques() const;

	/**
	 * Transient validation against a live request context (target validity,
	 * threat payload presence). Advisory when controllers call it; the
	 * execution component re-runs it authoritatively.
	 */
	UFUNCTION(BlueprintPure, Category="Combat|Techniques")
	bool CanExecute(FName TechniqueId, const FCombatTechniqueRequest& Context) const;
	FString GetExecutionFailureReason(FName TechniqueId, const FCombatTechniqueRequest& Context) const;

	/** Proficiency the fighter has learned for a skill id; fallback when unlearned. */
	UFUNCTION(BlueprintPure, Category="Combat|Techniques")
	float GetSkillProficiency(FName SkillId, float FallbackProficiency = 0.f) const;

	/** Forces the availability cache to rebuild (equipment/grip changes normally trigger this). */
	UFUNCTION(BlueprintCallable, Category="Combat|Techniques")
	void RefreshAvailability();

	virtual void BeginPlay() override;

private:
	void RebuildCacheIfNeeded() const;
	UCombatEquipmentComponent* GetEquipment() const;
	UFighterComponent* GetFighter() const;
	UFighterVitalsComponent* GetVitals() const;

	mutable TArray<FName> AvailableCache;
	mutable int32 CachedEquipmentRevision = -1;
	mutable int32 CachedRepertoireRevision = -1;
	mutable bool bCacheBuilt = false;
};
