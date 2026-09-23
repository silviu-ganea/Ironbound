#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Combat/CombatTechniqueRow.h"
#include "Combat/CombatTrajectoryLibrary.h"
#include "TimerManager.h"
#include "IronboundCombatAIController.generated.h"

class AActor;
class UCombatExecutionComponent;
class UCombatFocusComponent;
class UCombatTechniqueComponent;
class UFighterComponent;
class UFighterVitalsComponent;

UENUM(BlueprintType)
enum class EIronboundAIMovementMode : uint8
{
	Idle, Pursuit, GuardManeuver, AttackAlignment, CommittedAttack
};

/**
 * Small, data-driven duel policy for the combat prototype.
 *
 * The controller observes fighter/combat state and submits technique and
 * navigation requests. Movement, technique execution, body physics and damage
 * remain owned by their respective pawn systems. The same pawn can instead be
 * possessed by a player controller, which uses the same RequestTechnique API.
 */
UCLASS(Blueprintable)
class IRONBOUND_API AIronboundCombatAIController : public AAIController
{
	GENERATED_BODY()

public:
	AIronboundCombatAIController();

	/** Base cadence; effective cadence is adjusted by the fighter's Agility. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Decision", meta=(ClampMin="0.05"))
	float DecisionIntervalSeconds = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Decision", meta=(ClampMin="0.05"))
	float MinimumDecisionIntervalSeconds = 0.10f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Decision", meta=(ClampMin="0.05"))
	float MaximumDecisionIntervalSeconds = 0.50f;

	/** How often an unavailable or refused deliberate technique may be retried. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Decision", meta=(ClampMin="0.05"))
	float DeliberateRetryIntervalSeconds = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Movement", meta=(ClampMin="1.0"))
	float MovementGoalUpdateDistanceCm = 60.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Movement", meta=(ClampMin="0.0"))
	float FailedPathRetryDelaySeconds = 0.75f;

	/** Chance to choose a nearby improvement when both current and nearby attacks are useful. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Decision", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RepositionPreference = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Decision", meta=(ClampMin="50.0"))
	float PlanningRangeCm = 450.f;

	/** If no attack is reachable from the planning boundary, advance to this distance before reassessing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Decision", meta=(ClampMin="50.0"))
	float CloseApproachDistanceCm = 220.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Decision", meta=(ClampMin="10.0"))
	float MaxNearbyMoveCm = 250.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Decision", meta=(ClampMin="0.0"))
	float MeaningfulQualityGain = 4.f;

	/** A feasible attack this much farther from the target is chosen before the preference roll. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Decision", meta=(ClampMin="0.0"))
	float MinimumRangeGainForRepositionCm = 5.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Decision", meta=(ClampMin="10.0"))
	float TargetDisplacementToleranceCm = 10.f;

	UFUNCTION(BlueprintPure, Category="Ironbound AI|Movement")
	EIronboundAIMovementMode GetCombatMovementMode() const { return MovementMode; }

	/** Blueprint Mover can use this before applying its rotation mode. */
	UFUNCTION(BlueprintPure, Category="Ironbound AI|Movement")
	FVector ResolveCombatMovementFacing(FVector TravelIntent) const;

	/** Base latency before answering a recognized threat; Agility reduces it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Reaction", meta=(ClampMin="0.0"))
	float BaseReactionDelaySeconds = 0.18f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Reaction", meta=(ClampMin="0.0"))
	float MinimumReactionDelaySeconds = 0.04f;

	/** Per Agility point above 1, subtract this many seconds from reaction delay. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Reaction", meta=(ClampMin="0.0"))
	float AgilityReactionBonusSeconds = 0.06f;

	/** Optional preferred technique ids; empty means choose an available row of that kind. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Techniques")
	FName PreferredDeliberateTechnique;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Techniques")
	FName PreferredReactiveTechnique;

	/** Optional policy restriction for deliberate strikes; empty lets geometry choose a useful contact. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Techniques")
	FName PreferredAttackTargetRegion;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Debug")
	bool bShowIntentDebug = true;

	/** One persistent on-screen line per controller, refreshed without stacking messages. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ironbound AI|Debug", meta=(ClampMin="0.1"))
	float IntentDebugRefreshSeconds = 1.0f;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	void DecisionStep();
	void CachePawnComponents();
	AActor* SelectEnemy();
	bool IsLivingEnemy(const AActor* Candidate) const;
	FName FindAvailableTechnique(ECombatTechniqueKind Kind, FName Preferred) const;
	void UpdateMovementRequest();
	void ClearAttackPlan(const TCHAR* Reason, bool bCancelExecution);
	void ChooseAttackPlan(AActor* Target, float Now);
	void SetMovementMode(EIronboundAIMovementMode NewMode, const TCHAR* Reason);
	void DrawRejectedOpportunity() const;
	void SuspendFailedClosePursuit();
	void PublishIntent(const TCHAR* Intent, const AActor* Target = nullptr);
	float GetEffectiveDecisionInterval() const;
	float GetEffectiveReactionDelay() const;

	UPROPERTY(Transient)
	TObjectPtr<UFighterComponent> Fighter;

	UPROPERTY(Transient)
	TObjectPtr<UFighterVitalsComponent> Vitals;

	UPROPERTY(Transient)
	TObjectPtr<UCombatFocusComponent> Focus;

	UPROPERTY(Transient)
	TObjectPtr<UCombatExecutionComponent> Execution;

	UPROPERTY(Transient)
	TObjectPtr<UCombatTechniqueComponent> Techniques;

	FTimerHandle DecisionTimer;
	TWeakObjectPtr<AActor> CurrentTarget;
	TWeakObjectPtr<AActor> PendingThreatAttacker;
	TWeakObjectPtr<AActor> LastAnsweredThreatAttacker;
	FName PendingThreatTechnique;
	FName LastAnsweredThreatTechnique;
	FVector LastMoveGoal = FVector::ZeroVector;
	FString LastIntent;
	float NextDecisionWorldTime = 0.f;
	float NextDeliberateAttemptWorldTime = 0.f;
	float PendingThreatStartWorldTime = 0.f;
	float NextPathRequestWorldTime = 0.f;
	bool bHasMoveGoal = false;
	bool bMoveRequestActive = false;
	float NextStandingPlanWorldTime = 0.f;
	bool bHadActiveDeliberate = false;
	bool bPlanEverCommitted = false;
	bool bLoggedNoDeliberateTechnique = false;
	bool bClosingForOpportunity = false;
	bool bHasRejectedOpportunity = false;
	FBladeTrajectory RejectedTrajectory;
	FCombatAttackOpportunity RejectedOpportunity;
	float RejectedContactToleranceCm = 0.f;
	int32 NextPlanId = 1;
	int32 ActivePlanId = 0;
	int32 PursuitIntentId = 0;
	int32 ConsecutivePathFailures = 0;
	int32 ConsecutiveFailedPlans = 0;
	FVector FailedPlanTargetLocation = FVector::ZeroVector;
	FVector FailedPlanAttackerLocation = FVector::ZeroVector;
	EIronboundAIMovementMode MovementMode = EIronboundAIMovementMode::Idle;
	FCombatAttackOpportunity ActiveOpportunity;
	TWeakObjectPtr<AActor> PlannedTarget;
};
