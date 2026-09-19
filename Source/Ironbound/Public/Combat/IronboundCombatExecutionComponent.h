#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/IronboundTrajectoryLibrary.h"
#include "IronboundCombatExecutionComponent.generated.h"

class AAIController;
class UDataTable;
class USkeletalMeshComponent;
class UAnimSequenceBase;
class UIronboundEquipmentComponent;
class UIronboundCombatFocusComponent;


UENUM(BlueprintType)
enum class EIronboundAttackPhase : uint8
{
	Idle,
	Approaching,
	Aligning,
	Committed,
	Recovery
};


/**
 * Movement/action arbitration.
 * AI selects target and move; this component executes the request.
 */
UCLASS(
	ClassGroup=(Ironbound),
	meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UIronboundCombatExecutionComponent :
	public UActorComponent
{
	GENERATED_BODY()

public:

	UIronboundCombatExecutionComponent();


	// State

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	EIronboundAttackPhase Phase =
		EIronboundAttackPhase::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	FTransform PlannedTransform;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	float CurrentPredictedDistance = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	float FacingErrorDegrees = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	bool bStrikeWindowOpen = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	float MaxCommittedFacingError = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	FName PlannedTargetRegion = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	FName PlannedTargetBone = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	float PlannedTargetScore = 0.f;


	// Settings

	/**
	 * Global anatomical target definitions used by the attack planner.
	 * Assign DT_CombatTargets to this on the pawn Blueprint.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category="Combat|Targets")
	TObjectPtr<UDataTable> CombatTargets;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category="Combat",
		meta=(ClampMin="0.1"))
	float ArrivalTolerance = 8.f;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category="Combat",
		meta=(ClampMin="0.1"))
	float FacingTolerance = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat")
	float SettledSpeed = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat")
	float RecoverySeconds = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat")
	bool bDrawActualBlade = true;


	// Attack

	/**
	 * The caller now supplies only target + source animation.
	 *
	 * Trajectory window, sampling, target bones, target desirability,
	 * engagement distance and contact tolerance are owned by the combat
	 * systems rather than by the move row.
	 */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat")
	bool PrepareAttack(
		USkeletalMeshComponent* TargetMesh,
		UAnimSequenceBase* Sequence);

	UFUNCTION(BlueprintPure, Category="Ironbound|Combat")
	FVector ResolveOrientationIntent(
		FVector LocomotionIntent) const;

	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat")
	void FinishAttack();

	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat")
	void CancelAttack();

	UFUNCTION(BlueprintPure, Category="Ironbound|Combat")
	bool CanPlayAttack() const;

	UFUNCTION(BlueprintPure, Category="Ironbound|Combat")
	bool IsCommitted() const;

	UFUNCTION(BlueprintPure, Category="Ironbound|Combat")
	bool IsAligning() const;

	UFUNCTION(BlueprintPure, Category="Ironbound|Combat")
	float GetCombatFacingDelta() const;

	/** True while this fighter has a committed blade trajectory available. */
	UFUNCTION(BlueprintPure, Category="Combat|Attack")
	bool HasCommittedBladePath() const
	{
		return Phase == EIronboundAttackPhase::Committed
			&& CommittedTrajectory.bValid;
	}

	/** Committed blade trajectory in attacker root-local space. */
	const FBladeTrajectory& GetCommittedTrajectory() const
	{
		return CommittedTrajectory;
	}

	/** Attacker root transform captured when the attack was committed. */
	const FTransform& GetCommittedTransform() const
	{
		return CommittedTransform;
	}

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction*
			ThisTickFunction) override;


private:

	bool IsAligningOrCommitted() const;
	bool HasTarget() const;
	bool HasAttackTimedOut(float Now) const;
	bool IsAtAttackPosition() const;
	bool IsReadyToCommit() const;

	FVector GetTargetFacingIntent() const;

	bool IsValidAttackRequest(
		USkeletalMeshComponent* TargetMesh) const;

	bool BuildAttackTrajectory(
		UAnimSequenceBase* Sequence,
		FBladeTrajectory& OutTrajectory) const;

	bool SolveAttackPlan(
		USkeletalMeshComponent* TargetMesh,
		const FBladeTrajectory& Trajectory);

	void ApproachAttackPosition();

	void AlignAttack(
		USkeletalMeshComponent* TargetMesh,
		const FBladeTrajectory& Trajectory);

	void CommitAttack(
		UAnimSequenceBase* Sequence,
		const FBladeTrajectory& Trajectory);

	void UpdateMeasuredSpeed(float DeltaTime);
	void UpdateCommittedFacingError();

	void DrawAttackDebug();
	void DrawIntendedBladeTrajectory();
	void DrawActualBladeTrajectory();

	AAIController* GetAIController() const;

	UIronboundEquipmentComponent*
	GetEquipment() const;

	UIronboundCombatFocusComponent*
	GetCombatFocus() const;


	TWeakObjectPtr<AActor> PlannedTarget;

	FBladeTrajectory CommittedTrajectory;
	FTransform CommittedTransform;

	FVector AttackFacingIntent =
		FVector::ZeroVector;

	FVector PreviousTip =
		FVector::ZeroVector;

	FVector LastLocation =
		FVector::ZeroVector;

	float MeasuredSpeed = 0.f;
	float RecoveryUntil = 0.f;
	float CommitDeadline = 0.f;
	float LastRequestTime = 0.f;

	bool bHasPreviousTip = false;
};