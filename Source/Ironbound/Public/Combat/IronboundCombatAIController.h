#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Combat/CombatTechniqueRow.h"
#include "TimerManager.h"
#include "IronboundCombatAIController.generated.h"

class AActor;
class UCombatExecutionComponent;
class UCombatFocusComponent;
class UCombatTechniqueComponent;
class UFighterComponent;
class UFighterVitalsComponent;

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
};
