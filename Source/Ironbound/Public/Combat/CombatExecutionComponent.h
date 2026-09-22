#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/CombatExecutionTypes.h"
#include "CombatExecutionComponent.generated.h"

class UCombatTechniqueComponent;
class UCombatTechniqueExecutor;
class UCombatThreatComponent;
class UFighterVitalsComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCombatEngagementRequirementChangedSignature,
	const FCombatEngagementRequirement&, Requirement);

/**
 * Request admission + collection of active execution records.
 *
 * This is the pawn's combat execution domain. It is origin-agnostic: AI
 * controllers, player input and future StateTree planners all submit through
 * RequestTechnique, and the component cannot distinguish nor care about the
 * request origin. RequestTechnique performs the AUTHORITATIVE validation
 * (availability, transient CanExecute, admission policy) - a previous
 * controller-side CanExecute result is never trusted, because combat state
 * may have changed between query and request.
 *
 * Executions live in a record collection (one record per execution, each
 * owning its executor). Admission limits are policy, not structure.
 *
 * This component owns no navigation and holds no AI policy: executors emit
 * FCombatEngagementRequirement and the owning controller decides how (or
 * whether) to satisfy it.
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UCombatExecutionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatExecutionComponent();

	// =====================================================================
	// Submission boundary (the ONLY way to start a technique execution)
	// =====================================================================

	/**
	 * Requests a technique execution.
	 *
	 * Authoritative validation runs here regardless of what the controller
	 * queried beforehand: row lookup, stable availability, transient
	 * CanExecute against the live request, and admission policy. On success a
	 * record + executor are created and the requirement is emitted.
	 *
	 * Returns true when the execution was admitted.
	 */
	UFUNCTION(BlueprintCallable, Category="Combat|Techniques")
	bool RequestTechnique(const FCombatTechniqueRequest& Request);

	/**
	 * Advisory query mirroring RequestTechnique's validation (including the
	 * admission slots). Controllers use this to decide; RequestTechnique
	 * still revalidates everything.
	 */
	UFUNCTION(BlueprintCallable, Category="Combat|Techniques")
	bool CanExecuteTechnique(const FCombatTechniqueRequest& Request) const;

	// =====================================================================
	// Objective execution state (observation; consumed by both controller kinds)
	// =====================================================================

	/** True while a deliberate execution is committed or recovering. */
	UFUNCTION(BlueprintPure, Category="Combat|Attack")
	bool IsCommitted() const;

	/**
	 * Old semantic preserved: true while the committed deliberate execution is
	 * in its active committed phase (not yet in recovery). The AnimBP look
	 * gate and legacy BP still consume this.
	 */
	UFUNCTION(BlueprintPure, Category="Combat|Attack")
	bool CanPlayAttack() const;

	/** True while the committed deliberate execution is inside its strike window. */
	UFUNCTION(BlueprintPure, Category="Combat|Attack")
	bool IsStrikeWindowOpen() const;

	/** True while this fighter has a committed blade trajectory available. */
	UFUNCTION(BlueprintPure, Category="Combat|Attack")
	bool HasCommittedBladePath() const;

	/** Objective snapshot of the committed strike (threat observation consumes this). */
	UFUNCTION(BlueprintPure, Category="Combat|Attack")
	FCombatCommittedStrike GetCommittedStrike() const;

	/** True while a deliberate execution is positioned but not yet committed. */
	UFUNCTION(BlueprintPure, Category="Combat|Attack")
	bool IsAligning() const;

	/** Yaw error against the execution's facing intent while aligning. */
	UFUNCTION(BlueprintPure, Category="Combat|Attack")
	float GetCombatFacingDelta() const;

	/** Facing intent for locomotion: execution intent first, then focus target. */
	UFUNCTION(BlueprintCallable, Category="Combat|Attack")
	FVector ResolveOrientationIntent(
		FVector LocomotionIntent) const;

	/** Technique ids currently executing. */
	UFUNCTION(BlueprintPure, Category="Combat|Techniques")
	TArray<FName> GetActiveTechniqueIds() const;

	/** True while any reactive execution is active. */
	UFUNCTION(BlueprintPure, Category="Combat|Techniques")
	bool IsExecutingReactive() const;

	/** True when a reactive execution exposes a hand target for animation. */
	UFUNCTION(BlueprintPure, Category="Combat|Techniques")
	bool GetActiveHandTarget(FTransform& OutHandTarget) const;

	/** True when the request would currently be admitted (admission policy only). */
	UFUNCTION(BlueprintPure, Category="Combat|Techniques")
	bool HasAdmissionSlot(ECombatExecutionKind Kind) const;

	// =====================================================================
	// Movement requirement seam (executors emit; controllers consume)
	// =====================================================================

	/** Current engagement requirement emitted by the primary execution. */
	UFUNCTION(BlueprintPure, Category="Combat|Engagement")
	FCombatEngagementRequirement GetEngagementRequirement() const;

	/** Broadcast whenever the engagement requirement changes. */
	UPROPERTY(BlueprintAssignable, Category="Combat|Engagement")
	FCombatEngagementRequirementChangedSignature OnEngagementRequirementChanged;

	// =====================================================================
	// Admission policy (policy, not structure)
	// =====================================================================

	/** Maximum simultaneous deliberate executions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Admission", meta=(ClampMin="1"))
	int32 MaxDeliberateExecutions = 1;

	/** Maximum simultaneous reactive executions. Reactive may coexist with deliberate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Admission", meta=(ClampMin="1"))
	int32 MaxReactiveExecutions = 1;

	/** Radius of the compatibility strike sweep, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Contact", meta=(ClampMin="0.1"))
	float StrikeSweepRadiusCm = 4.f;

	// =====================================================================
	// Lifecycle helpers (legacy BP seams + system coordination)
	// =====================================================================

	/**
	 * Deprecated seam: the old BP montage-end path calls this. Forwards to
	 * finishing the committed deliberate execution. Remove with the BP
	 * rewiring; executors own their montage lifecycle after that.
	 */
	UFUNCTION(BlueprintCallable, Category="Deprecated", meta=(DeprecatedFunction, DeprecationMessage="Execution components finish committed executions themselves; controllers request techniques via RequestTechnique."))
	void FinishAttack();

	/**
	 * Deprecated seam: the old BP cancel path calls this. Forwards to
	 * CancelActiveExecutions. Remove with the BP rewiring.
	 */
	UFUNCTION(BlueprintCallable, Category="Deprecated", meta=(DeprecatedFunction, DeprecationMessage="Use CancelActiveExecutions."))
	void CancelAttack();

	/** Cancels every active execution that is not committed (equipment changes, death). */
	UFUNCTION(BlueprintCallable, Category="Combat|Techniques")
	void CancelActiveExecutions();

	/** Immediate cancellation of everything, including committed executions. */
	UFUNCTION(BlueprintCallable, Category="Combat|Techniques")
	void CancelAllExecutions();

	virtual void BeginPlay() override;

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// ===== Record API used by executors and the attack-window notify =====

	void SetRecordState(int32 RecordId, ECombatExecutionState NewState);
	void MarkRecordCommitted(int32 RecordId, const FBladeTrajectory& Trajectory, const FTransform& Transform);
	void SetRecordStrikeWindow(int32 RecordId, bool bOpen);
	void MarkRecordContactResolved(int32 RecordId);
	void FinishRecord(int32 RecordId, const TCHAR* Reason);

	/** Strike-window seam driven by the UCombatAttackWindow notify. */
	void SetStrikeWindowOpen(bool bOpen);

	// ===== Sibling combat-domain components (lazy creation keeps the pawn BP minimal) =====

	UCombatTechniqueComponent* GetTechniques();
	UCombatThreatComponent* GetThreats();

private:
	UFUNCTION()
	void HandleOwnerDeath();

	bool ValidateRequest(
		const FCombatTechniqueRequest& Request,
		const FCombatTechniqueRow*& OutRow,
		FText& OutReason) const;

	FCombatExecutionRecord* FindRecord(int32 RecordId);
	const FCombatExecutionRecord* FindRecord(int32 RecordId) const;
	const FCombatExecutionRecord* FindPrimaryDeliberateRecord() const;
	const FCombatExecutionRecord* FindCommittedStrikeRecord() const;
	void ResolveStrikeContacts();
	void BroadcastRequirement();

	UPROPERTY()
	TArray<FCombatExecutionRecord> Executions;

	int32 NextRecordId = 1;

	FCombatEngagementRequirement CachedRequirement;
};
