#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatTechniqueExecutor.h"
#include "Combat/CombatTrajectoryLibrary.h"
#include "CombatExecutor_MeleeStrike.generated.h"

class UAnimMontage;
class UAnimInstance;
class UExecConfig_MeleeStrike;

/**
 * Deliberate melee strike execution: solve, wait for engagement, commit,
 * recover.
 *
 * Owns trajectory building, stance solving, engagement-requirement emission
 * (never navigation), montage playback in C++, committed-trajectory
 * publication and recovery. Created only after a technique request has been
 * validated and admitted.
 */
UCLASS()
class IRONBOUND_API UCombatExecutor_MeleeStrike : public UCombatTechniqueExecutor
{
	GENERATED_BODY()

public:
	virtual void GetEngagementRequirement(FCombatEngagementRequirement& OutRequirement) const override;
	virtual bool GetFacingIntent(FVector& OutIntent) const override;
	virtual bool IsAwaitingAlignment() const override;
	virtual float GetFacingDeltaDegrees() const override;
	virtual void OnExternalFinishRequest() override;

protected:
	virtual bool OnInitialize(const FCombatTechniqueRequest& Request) override;
	virtual void OnTick(float DeltaTime) override;
	virtual void OnFinish() override;

private:
	enum class EStrikePhase : uint8
	{
		Waiting,
		Committed,
		Recovering
	};

	const UExecConfig_MeleeStrike* StrikeConfig() const;

	bool BuildAttackTrajectory(FBladeTrajectory& OutTrajectory) const;
	bool SolveAttackPlan();
	bool IsAtStancePosition() const;
	bool IsReadyToCommit() const;
	bool HasStanceTimedOut(float Now) const;

	void UpdateMeasuredSpeed(float DeltaTime);
	void UpdateFacingError();
	void UpdateCommittedFacingError();

	void CommitStrike();
	void EnterRecovery();

	void HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	void DrawAttackDebug();

	ESTrikePhase Phase = EStrikePhase::Waiting;

	FCombatEngagementRequirement CachedRequirement;

	TWeakObjectPtr<AActor> PlannedTarget;

	FBladeTrajectory PlannedTrajectory;
	FTransform PlannedTransform = FTransform::Identity;
	FTransform CommittedTransform = FTransform::Identity;

	FName PlannedTargetRegion = NAME_None;
	FName PlannedTargetBone = NAME_None;
	float PlannedTargetScore = 0.f;
	float CurrentPredictedDistance = 0.f;

	FVector AttackFacingIntent = FVector::ZeroVector;

	FVector PreviousTip = FVector::ZeroVector;
	FVector LastLocation = FVector::ZeroVector;

	float MeasuredSpeed = 0.f;
	float RecoveryUntil = 0.f;
	float CommitDeadline = 0.f;
	float RequestWorldTime = 0.f;
	float MaxCommittedFacingError = 0.f;
	float FacingErrorDegrees = 0.f;

	bool bHasPreviousTip = false;
	bool bHasPreviousLocation = false;
	bool bAtStancePosition = false;
	bool bMontagePlaying = false;
};
