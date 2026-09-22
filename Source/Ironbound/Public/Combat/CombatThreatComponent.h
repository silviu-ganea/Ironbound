#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/CombatThreatTypes.h"
#include "CombatThreatComponent.generated.h"

class UCombatExecutionComponent;
class UCombatEquipmentComponent;
class UCombatFocusComponent;
class UStaticMeshComponent;
class USkeletalMeshComponent;

/**
 * Objective combat observation only: measures and exposes incoming-attack
 * threats. It never decides a response, never requests a technique, and holds
 * no policy. Both AI controllers and player input consume the same queries.
 *
 * THREAT vs FOCUS: a threat is any incoming attack that may affect this
 * fighter, from any attacker. CombatFocus is the target this fighter is
 * currently focused on. They are different concepts and different components.
 *
 * MIGRATION NOTE (temporary, isolated): threat discovery currently reads the
 * focus target's committed execution. The data model (FCombatThreat) and the
 * per-attacker bookkeeping below are attacker-independent; discovering threats
 * from other attackers later only extends ThreatDiscovery, it does not change
 * this component's API or the threat model.
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UCombatThreatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatThreatComponent();

	// ===== Observation API (objective state; no decisions) =====

	/** All currently recognized incoming threats, one per attacker at most. */
	UFUNCTION(BlueprintPure, Category="Combat|Threats")
	TArray<FCombatThreat> GetIncomingThreats() const;

	/** Most urgent recognized threat, if any. */
	UFUNCTION(BlueprintPure, Category="Combat|Threats")
	bool GetPrimaryIncomingThreat(FCombatThreat& OutThreat) const;

	/** Payload form of the primary threat, for attaching to a technique request. */
	UFUNCTION(BlueprintPure, Category="Combat|Threats")
	FCombatThreatContext BuildThreatContext() const;

	UFUNCTION(BlueprintPure, Category="Combat|Threats")
	bool HasIncomingThreat() const;

	// ===== Observation latency (objective) =====

	/**
	 * Seconds between first observing a committed attack and recognizing it.
	 * Perception stays with measurement; reaction latency is a controller
	 * trait and deliberately lives elsewhere.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Threats", meta=(ClampMin="0.0"))
	float PerceptionDelaySeconds = 0.f;

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Per-attacker recognition bookkeeping. Attacker-independent by construction. */
	struct FThreatObservation
	{
		float ObservationWorldTime = 0.f;
		bool bRecognized = false;
		bool bLoggedDiagnostics = false;
	};

	/** Maps a world time into the attacker's source-animation time (montage -> sequence). */
	bool GetAttackerSourcePlaybackTime(
		AActor* Attacker,
		float& OutSourceTime) const;

	/** Predicts the body-envelope contact for a committed trajectory. */
	bool BuildThreatFromCommittedStrike(
		AActor* Attacker,
		const FCombatCommittedStrike& Strike,
		FCombatThreat& OutThreat) const;

	void DiscoverThreatSources(TArray<AActor*>& OutAttackers, TArray<FCombatCommittedStrike>& OutStrikes);

	bool AttacksArrayContains(const TArray<AActor*>& Attackers, AActor* Attacker) const;
	USkeletalMeshComponent* GetFighterMesh() const;
	UStaticMeshComponent* GetAttackerWeapon(AActor* Attacker) const;

	UPROPERTY(Transient)
	TObjectPtr<UCombatFocusComponent> Focus;

	UPROPERTY(Transient)
	TObjectPtr<UCombatEquipmentComponent> Equipment;

	TMap<TWeakObjectPtr<AActor>, FThreatObservation> Observations;
};
