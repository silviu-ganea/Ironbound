#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatExecutionTypes.h"
#include "Combat/CombatTechniqueRow.h"
#include "CombatTechniqueExecutor.generated.h"

class AActor;
class UAnimSequenceBase;
class UCombatExecutionComponent;
class UCombatTechniqueExecutionConfig;
class UCombatBodyComponent;
class UCombatEquipmentComponent;
class UCombatFocusComponent;
class UCombatTechniqueComponent;
class UCombatThreatComponent;
class UFighterComponent;
class UFighterVitalsComponent;
class USkeletalMeshComponent;

/**
 * One active technique execution.
 *
 * Executors are created per execution by UCombatExecutionComponent after
 * authoritative validation and admission, and are destroyed at terminal.
 * They never decide that a technique should happen, never wait indefinitely,
 * never navigate, and never inspect who requested the technique.
 *
 * An executor emits movement desires through FCombatEngagementRequirement
 * and reports lifecycle through the owning execution component's record API.
 */
UCLASS(Abstract)
class IRONBOUND_API UCombatTechniqueExecutor : public UObject
{
	GENERATED_BODY()
	friend class UCombatExecutionComponent;

public:
	// ===== Lifecycle (driven by the execution component) =====

	/**
	 * Initializes the executor for one admitted execution.
	 *
	 * Returns false to reject the execution before its record exists; the
	 * request is then reported as refused. Returning true hands ownership of
	 * the lifecycle to the executor until it finishes.
	 */
	bool InitializeExecution(
		UCombatExecutionComponent* InOwner,
		int32 InRecordId,
		const FCombatTechniqueRow& InRow,
		const UCombatTechniqueExecutionConfig* InConfig,
		const FCombatTechniqueRequest& InRequest);

	/** Per-frame execution update, called by the execution component. */
	void TickExecution(float DeltaTime);

	/** Forces a terminal finish (death, equipment change, cancel). */
	void RequestFinish(const TCHAR* Reason);

	/**
	 * External finish request (legacy montage-end seam).
	 *
	 * Default: terminal finish immediately. Executors with a meaningful
	 * recovery phase override this to move into their recovery instead.
	 */
	virtual void OnExternalFinishRequest();

	/** Row snapshot of the executing technique. */
	const FCombatTechniqueRow& GetRow() const { return Row; }

	/** Executor-specific authored configuration (typed by the concrete executor). */
	const UCombatTechniqueExecutionConfig* GetConfig() const { return Config; }

	/** The request this execution was admitted from. */
	const FCombatTechniqueRequest& GetRequest() const { return Request; }

	int32 GetRecordId() const { return RecordId; }

	// ===== Contracts executors may fulfil (queried by the execution component) =====

	/** Movement/facing desire emitted by this executor. Empty means "movement is free". */
	virtual void GetEngagementRequirement(FCombatEngagementRequirement& OutRequirement) const;

	/** Facing intent while executing (world-space direction), for orientation relays. False when none. */
	virtual bool GetFacingIntent(FVector& OutIntent) const;

	/** True while the executor has reached its position but has not committed (presentation alignment). */
	virtual bool IsAwaitingAlignment() const;

	/** Yaw error the executor currently fights against, degrees (0 when none). */
	virtual float GetFacingDeltaDegrees() const;

	/** Hand target produced by this executor for animation (parry CCDIK). False when none. */
	virtual bool GetHandTarget(FTransform& OutHandTarget) const;

protected:
	// ===== Strategy hooks =====

	/** Called once after members are set. Return false to reject the execution. */
	virtual bool OnInitialize(const FCombatTechniqueRequest& InRequest);

	/** Called every frame while the record lives. */
	virtual void OnTick(float DeltaTime);

	/** Called once before the record is destroyed, for every finish path. */
	virtual void OnFinish();

	// ===== Record helpers (route through the owning execution component) =====

	void SetRecordState(ECombatExecutionState NewState);
	void MarkRecordCommitted(const FBladeTrajectory& Trajectory, const FTransform& Transform);
	void SetRecordStrikeWindow(bool bOpen);
	void MarkRecordContactResolved();

	/** Ends this execution: OnFinish runs, then the record is destroyed. */
	void FinishExecution(const TCHAR* Reason);

	// ===== Component access =====

	UCombatExecutionComponent* GetExecutionComponent() const { return OwnerComponent.Get(); }
	AActor* GetFighter() const;
	USkeletalMeshComponent* GetFighterMesh() const;
	UCombatTechniqueComponent* GetTechniques() const;
	UCombatThreatComponent* GetThreats() const;
	UCombatEquipmentComponent* GetEquipment() const;
	UCombatFocusComponent* GetFocus() const;
	UCombatBodyComponent* GetBody() const;
	UFighterComponent* GetFighterComponent() const;
	UFighterVitalsComponent* GetVitals() const;

	/** Proficiency the fighter has learned for a skill id, with the authored fallback. */
	float GetSkillProficiency(FName SkillId, float FallbackProficiency) const;

	UPROPERTY()
	TObjectPtr<const UCombatTechniqueExecutionConfig> Config = nullptr;

	FCombatTechniqueRow Row;
	FCombatTechniqueRequest Request;

private:
	TWeakObjectPtr<UCombatExecutionComponent> OwnerComponent;
	int32 RecordId = INDEX_NONE;
};
