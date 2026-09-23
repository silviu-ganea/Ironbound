#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatTechniqueRow.h"
#include "Combat/CombatThreatTypes.h"
#include "Combat/CombatTrajectoryLibrary.h"
#include "CombatExecutionTypes.generated.h"

class UCombatTechniqueExecutor;

/**
 * Execution kind matches the technique row kind: what admission slot this
 * execution occupies. It is policy vocabulary, not an origin marker.
 */
UENUM(BlueprintType)
enum class ECombatExecutionKind : uint8
{
	Deliberate,
	Reactive
};

/**
 * Lifecycle state of one active execution record.
 *
 * Records are destroyed at terminal: there is no terminal state here and no
 * permanent executor.
 */
UENUM(BlueprintType)
enum class ECombatExecutionState : uint8
{
	/** Prepared/admitted, not yet committed. */
	Preparing,
	/** Physical execution committed (trajectory observable). */
	Committed,
	/** Execution finished its active part, recovering. */
	Recovering
};

/**
 * One active execution: created by admission, owning its executor, destroyed
 * at terminal. A collection of these replaces any singular active-executor
 * model, and BodyScope is carried so future body-region arbitration can be
 * added without replacing the execution architecture.
 */
USTRUCT()
struct IRONBOUND_API FCombatExecutionRecord
{
	GENERATED_BODY()

	/** Monotonic id handed out by the execution component. */
	UPROPERTY()
	int32 RecordId = INDEX_NONE;

	UPROPERTY()
	int32 PlanId = 0;

	UPROPERTY()
	FName TechniqueId;

	UPROPERTY()
	ECombatExecutionKind Kind = ECombatExecutionKind::Deliberate;

	UPROPERTY()
	ECombatExecutionState State = ECombatExecutionState::Preparing;

	UPROPERTY()
	FCombatBodyScope BodyScope;

	UPROPERTY()
	TObjectPtr<AActor> Target = nullptr;

	UPROPERTY()
	TObjectPtr<UCombatTechniqueExecutor> Executor = nullptr;

	/** Strike window of THIS execution (opened/closed by the attack-window notify). */
	UPROPERTY()
	bool bStrikeWindowOpen = false;

	/** Execution-scoped one-hit dedup for body contact. */
	UPROPERTY()
	bool bContactResolved = false;

	/** Valid once committed: blade trajectory in attacker root-local space. */
	UPROPERTY()
	FBladeTrajectory CommittedTrajectory;

	UPROPERTY()
	FTransform CommittedTransform = FTransform::Identity;

	UPROPERTY()
	bool bCommittedTrajectoryValid = false;
};

/**
 * Movement/facing requirement emitted by an executor.
 *
 * Executors never navigate. The owning controller decides: an AI controller
 * satisfies the requirement via navigation/Mover; a player continues moving
 * freely and the technique simply waits until the conditions are satisfied.
 */
USTRUCT(BlueprintType)
struct IRONBOUND_API FCombatEngagementRequirement
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Combat|Engagement")
	bool bHasRequirement = false;

	/** Desired root position, world space. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Engagement")
	FVector DesiredLocation = FVector::ZeroVector;

	/** Desired root facing (yaw is the meaningful part). */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Engagement")
	FRotator DesiredFacing = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category="Combat|Engagement")
	float ArrivalTolerance = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Combat|Engagement")
	float FacingTolerance = 0.f;

	/** Whether the fighter may keep moving toward the desired location during execution. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Engagement")
	bool bMayMoveDuringExecution = true;
};

/**
 * A technique submission. Origin-agnostic: nothing in this struct knows or
 * cares whether it came from AI decision-making or player input.
 */
USTRUCT(BlueprintType)
struct IRONBOUND_API FCombatTechniqueRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="Combat|Request")
	FName TechniqueId;

	/** Opponent this technique is aimed at (when the technique requires a target). */
	UPROPERTY(BlueprintReadWrite, Category="Combat|Request")
	TObjectPtr<AActor> Target;

	/** Optional anatomical region row name (for example Head); empty uses the technique's default. */
	UPROPERTY(BlueprintReadWrite, Category="Combat|Request")
	FName TargetRegion = NAME_None;

	/** Optional decision-maker-selected opportunity. Execution validates it without choosing another stance. */
	UPROPERTY(BlueprintReadWrite, Category="Combat|Request")
	FCombatAttackOpportunity PlannedOpportunity;

	/** Correlation id supplied by a decision-maker; zero for unscripted/player requests. */
	UPROPERTY(BlueprintReadWrite, Category="Combat|Request")
	int32 PlanId = 0;

	/** Threat payload for reactive techniques; advisory for deliberate ones. */
	UPROPERTY(BlueprintReadWrite, Category="Combat|Request")
	FCombatThreatContext ThreatContext;
};

/**
 * Objective snapshot of this fighter's committed strike, consumed by threat
 * observation (and debugging) on the receiving side.
 */
USTRUCT(BlueprintType)
struct IRONBOUND_API FCombatCommittedStrike
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Combat|Attack")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category="Combat|Attack")
	FName TechniqueId;

	UPROPERTY(BlueprintReadOnly, Category="Combat|Attack")
	TObjectPtr<AActor> Attacker = nullptr;

	UPROPERTY(BlueprintReadOnly, Category="Combat|Attack")
	FBladeTrajectory Trajectory;

	UPROPERTY(BlueprintReadOnly, Category="Combat|Attack")
	FTransform Transform = FTransform::Identity;
};
