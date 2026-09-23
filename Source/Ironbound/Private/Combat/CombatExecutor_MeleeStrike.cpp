#include "Combat/CombatExecutor_MeleeStrike.h"

#include "Combat/CombatEquipmentComponent.h"
#include "Combat/CombatTarget.h"
#include "Combat/CombatTechniqueExecutionConfigs.h"
#include "Combat/CombatTechniqueRow.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Ironbound.h"

const UExecConfig_MeleeStrike* UCombatExecutor_MeleeStrike::StrikeConfig() const
{
	return Cast<UExecConfig_MeleeStrike>(Config);
}

bool UCombatExecutor_MeleeStrike::OnInitialize(
	const FCombatTechniqueRequest& InRequest)
{
	const UExecConfig_MeleeStrike* LocalConfig = StrikeConfig();
	if (!LocalConfig)
	{
		UE_LOG(
			LogIronboundCombat,
			Warning,
			TEXT("MeleeStrike [%s | %s]: row does not use a melee-strike execution config"),
			*GetNameSafe(GetFighter()),
			*InRequest.TechniqueId.ToString());

		return false;
	}

	if (!LocalConfig->SourceSequence || !LocalConfig->CombatTargets || !LocalConfig->Montage)
	{
		UE_LOG(
			LogIronboundCombat,
			Warning,
			TEXT("MeleeStrike [%s | %s]: execution config is incomplete (SourceSequence, CombatTargets and Montage are required)"),
			*GetNameSafe(GetFighter()),
			*InRequest.TechniqueId.ToString());

		return false;
	}

	AActor* Target = InRequest.Target;
	if (!IsValid(Target))
	{
		UE_LOG(
			LogIronboundCombat,
			Warning,
			TEXT("MeleeStrike [%s | %s]: request has no valid target"),
			*GetNameSafe(GetFighter()),
			*InRequest.TechniqueId.ToString());

		return false;
	}

	UCombatEquipmentComponent* Equipment = GetEquipment();
	if (!Equipment || !Equipment->bReady || !Equipment->Definition)
	{
		UE_LOG(
			LogIronboundCombat,
			Warning,
			TEXT("MeleeStrike [%s | %s]: no ready equipment"),
			*GetNameSafe(GetFighter()),
			*InRequest.TechniqueId.ToString());

		return false;
	}

	PlannedTarget = Target;

	FBladeTrajectory Trajectory;
	if (!BuildAttackTrajectory(Trajectory))
	{
		UE_LOG(
			LogIronboundCombat,
			Warning,
			TEXT("MeleeStrike [%s | %s]: could not derive blade trajectory"),
			*GetNameSafe(GetFighter()),
			*InRequest.TechniqueId.ToString());

		return false;
	}

	PlannedTrajectory = MoveTemp(Trajectory);

	if (!SolveAttackPlan())
	{
		UE_LOG(
			LogIronboundCombat,
			Log,
			TEXT("MeleeStrike [%s | %s]: no attack plan"),
			*GetNameSafe(GetFighter()),
			*InRequest.TechniqueId.ToString());

		return false;
	}

	RequestWorldTime = GetWorld()->GetTimeSeconds();
	CommitDeadline = 0.f;

	LastLocation = GetFighter()->GetActorLocation();
	bHasPreviousLocation = true;

	CachedRequirement.bHasRequirement = true;
	CachedRequirement.DesiredLocation = PlannedTransform.GetLocation();
	CachedRequirement.DesiredFacing = PlannedTransform.Rotator();
	CachedRequirement.ArrivalTolerance = LocalConfig->ArrivalTolerance;
	CachedRequirement.FacingTolerance = LocalConfig->FacingTolerance;
	CachedRequirement.bMayMoveDuringExecution = true;

	Phase = EStrikePhase::Waiting;

	UE_LOG(
		LogIronboundCombat,
		Log,
		TEXT("MeleeStrike [%s | %s] stance solved: region=%s bone=%s score=%.1f miss=%.1f sample=%d time=%.3f"),
		*GetNameSafe(GetFighter()),
		*InRequest.TechniqueId.ToString(),
		*PlannedTargetRegion.ToString(),
		*PlannedTargetBone.ToString(),
		PlannedTargetScore,
		CurrentPredictedDistance,
		PlannedContactSampleIndex,
		PlannedTrajectory.Segments.IsValidIndex(PlannedContactSampleIndex)
			? PlannedTrajectory.Segments[PlannedContactSampleIndex].TimeSeconds : -1.f);

	return true;
}

bool UCombatExecutor_MeleeStrike::BuildAttackTrajectory(
	FBladeTrajectory& OutTrajectory) const
{
	UCombatEquipmentComponent* Equipment = GetEquipment();
	if (!Equipment)
	{
		return false;
	}

	return Equipment->GetTrajectory(StrikeConfig()->SourceSequence, OutTrajectory);
}

bool UCombatExecutor_MeleeStrike::SolveAttackPlan()
{
	AActor* Fighter = GetFighter();
	USkeletalMeshComponent* FighterMesh = GetFighterMesh();
	AActor* Target = PlannedTarget.Get();

	USkeletalMeshComponent* TargetMesh =
		Target ? Target->FindComponentByClass<USkeletalMeshComponent>() : nullptr;

	if (!Fighter || !FighterMesh || !TargetMesh)
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("MeleeStrike plan inputs: fighter=%s fighterMesh=%s target=%s targetMesh=%s"),
			*GetNameSafe(Fighter), *GetNameSafe(FighterMesh),
			*GetNameSafe(Target), *GetNameSafe(TargetMesh));
		return false;
	}

	FTransform Stance;
	int32 ContactSampleIndex = INDEX_NONE;
	const FName RequestedRegion = GetRequest().TargetRegion.IsNone()
		? StrikeConfig()->DefaultTargetRegion
		: GetRequest().TargetRegion;
	if (!RequestedRegion.IsNone() && !StrikeConfig()->CombatTargets->FindRow<FCombatTargetRow>(RequestedRegion, TEXT("MeleeStrike target region"), false))
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("MeleeStrike [%s]: target region '%s' is missing from CombatTargets"),
			*GetNameSafe(Fighter), *RequestedRegion.ToString());
		return false;
	}

	if (!UCombatTrajectoryLibrary::SolveAttackAlignmentWithFacingLimit(
			FighterMesh,
			TargetMesh,
			PlannedTrajectory,
			StrikeConfig()->CombatTargets,
			StrikeConfig()->MaxFacingDeviationFromTargetDegrees,
			Stance,
			PlannedTargetRegion,
			PlannedTargetBone,
			ContactSampleIndex,
			CurrentPredictedDistance,
			PlannedTargetScore,
			RequestedRegion,
			StrikeConfig()->AimPointAlongBlade,
			StrikeConfig()->AimWindowStartFraction,
			StrikeConfig()->AimWindowEndFraction))
	{
		return false;
	}

	PlannedTransform = Stance;
	PlannedContactSampleIndex = ContactSampleIndex;
	return true;
}

bool UCombatExecutor_MeleeStrike::IsAtStancePosition() const
{
	const AActor* Fighter = GetFighter();
	if (!Fighter)
	{
		return false;
	}

	return Fighter->GetActorLocation().Equals(
		PlannedTransform.GetLocation(),
		StrikeConfig()->ArrivalTolerance);
}

bool UCombatExecutor_MeleeStrike::IsReadyToCommit() const
{
	const UExecConfig_MeleeStrike* LocalConfig = StrikeConfig();

	return FMath::Abs(FacingErrorDegrees) <= LocalConfig->FacingTolerance &&
		   MeasuredSpeed <= LocalConfig->SettledSpeed &&
		   CurrentPredictedDistance <= LocalConfig->ContactToleranceCm;
}

bool UCombatExecutor_MeleeStrike::HasStanceTimedOut(float Now) const
{
	return Now - RequestWorldTime > StrikeConfig()->PreparationTimeoutSeconds ||
		   !PlannedTarget.IsValid();
}

void UCombatExecutor_MeleeStrike::UpdateMeasuredSpeed(float DeltaTime)
{
	const AActor* Fighter = GetFighter();
	if (!Fighter)
	{
		return;
	}

	if (bHasPreviousLocation && DeltaTime > SMALL_NUMBER)
	{
		MeasuredSpeed =
			FVector::Dist(LastLocation, Fighter->GetActorLocation()) / DeltaTime;
	}

	LastLocation = Fighter->GetActorLocation();
	bHasPreviousLocation = true;
}

void UCombatExecutor_MeleeStrike::UpdateFacingError()
{
	const AActor* Fighter = GetFighter();
	if (!Fighter)
	{
		return;
	}

	const float RequiredYaw = PlannedTransform.Rotator().Yaw;
	const float CurrentYaw = Fighter->GetActorRotation().Yaw;

	FacingErrorDegrees =
		FMath::FindDeltaAngleDegrees(CurrentYaw, RequiredYaw);
}

void UCombatExecutor_MeleeStrike::UpdateCommittedFacingError()
{
	const AActor* Fighter = GetFighter();
	if (!Fighter)
	{
		return;
	}

	const float RequiredYaw = PlannedTransform.Rotator().Yaw;
	const float CurrentYaw = Fighter->GetActorRotation().Yaw;

	const float FacingError =
		FMath::Abs(FMath::FindDeltaAngleDegrees(CurrentYaw, RequiredYaw));

	MaxCommittedFacingError =
		FMath::Max(MaxCommittedFacingError, FacingError);
}

void UCombatExecutor_MeleeStrike::OnTick(float DeltaTime)
{
	AActor* Fighter = GetFighter();
	UWorld* World = GetWorld();
	if (!Fighter || !World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	switch (Phase)
	{
	case EStrikePhase::Waiting:
	{
		UpdateMeasuredSpeed(DeltaTime);

		if (HasStanceTimedOut(Now))
		{
			FinishExecution(TEXT("stance timed out"));
			return;
		}

		// Track a moving target: re-solve while waiting, exactly like the old
		// per-call re-solve did.
		if (!SolveAttackPlan())
		{
			FinishExecution(TEXT("attack plan lost"));
			return;
		}

		bAtStancePosition = IsAtStancePosition();

		if (bAtStancePosition)
		{
			// Once positioned, only facing and settling remain; movement
			// during execution is no longer part of the engagement.
			CachedRequirement.bMayMoveDuringExecution = false;

			UpdateFacingError();
			AttackFacingIntent = PlannedTransform.GetRotation().GetForwardVector();

			if (IsReadyToCommit())
			{
				CommitStrike();
			}
		}
		else
		{
			CachedRequirement.bMayMoveDuringExecution = true;
			FacingErrorDegrees = 0.f;
		}

		CachedRequirement.DesiredLocation = PlannedTransform.GetLocation();
		CachedRequirement.DesiredFacing = PlannedTransform.Rotator();

		break;
	}

	case EStrikePhase::Committed:
	{
		UpdateCommittedFacingError();

		if (bAtStancePosition)
		{
			AttackFacingIntent = PlannedTransform.GetRotation().GetForwardVector();
		}

		if (Now > CommitDeadline)
		{
			FinishExecution(TEXT("commit expired"));
			return;
		}

		DrawAttackDebug();

		break;
	}

	case EStrikePhase::Recovering:
	{
		if (Now >= RecoveryUntil)
		{
			FinishExecution(TEXT("recovered"));
			return;
		}

		break;
	}
	}
}

void UCombatExecutor_MeleeStrike::CommitStrike()
{
	AActor* Fighter = GetFighter();
	USkeletalMeshComponent* FighterMesh = GetFighterMesh();
	const UExecConfig_MeleeStrike* LocalConfig = StrikeConfig();

	if (!Fighter || !FighterMesh || !LocalConfig)
	{
		return;
	}

	CommittedTransform = Fighter->GetActorTransform();
	MaxCommittedFacingError = FMath::Abs(FacingErrorDegrees);

	MarkRecordCommitted(PlannedTrajectory, CommittedTransform);
	Phase = EStrikePhase::Committed;

	UAnimInstance* AnimInstance = FighterMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		UE_LOG(
			LogIronboundCombat,
			Error,
			TEXT("MeleeStrike [%s | %s]: no anim instance to play the strike montage"),
			*GetNameSafe(Fighter),
			*GetRequest().TechniqueId.ToString());

		FinishExecution(TEXT("no anim instance"));
		return;
	}

	FOnMontageEnded MontageEnded;
	MontageEnded.BindUObject(this, &UCombatExecutor_MeleeStrike::HandleMontageEnded);

	const float PlayedLength = AnimInstance->Montage_Play(LocalConfig->Montage);
	if (PlayedLength <= 0.f)
	{
		FinishExecution(TEXT("montage failed to start"));
		return;
	}

	CommitDeadline = GetWorld()->GetTimeSeconds() + PlannedTrajectory.ActiveEndTime +
		LocalConfig->CommitTimeoutExtraSeconds;
	AnimInstance->Montage_SetEndDelegate(MontageEnded, LocalConfig->Montage);
	bMontagePlaying = true;

	UE_LOG(
		LogIronboundCombat,
		Log,
		TEXT("MeleeStrike [%s | %s] committed: region=%s bone=%s score=%.1f miss=%.1f yaw-error=%.1fdeg"),
		*GetNameSafe(Fighter),
		*GetRequest().TechniqueId.ToString(),
		*PlannedTargetRegion.ToString(),
		*PlannedTargetBone.ToString(),
		PlannedTargetScore,
		CurrentPredictedDistance,
		FMath::Abs(FacingErrorDegrees));
}

void UCombatExecutor_MeleeStrike::HandleMontageEnded(
	UAnimMontage* Montage,
	bool bInterrupted)
{
	if (Phase != EStrikePhase::Committed)
	{
		return;
	}

	EnterRecovery();
}

void UCombatExecutor_MeleeStrike::EnterRecovery()
{
	Phase = EStrikePhase::Recovering;

	SetRecordState(ECombatExecutionState::Recovering);
	SetRecordStrikeWindow(false);

	UWorld* World = GetWorld();
	RecoveryUntil =
		World ? World->GetTimeSeconds() + StrikeConfig()->RecoverySeconds : 0.f;

	if (bMontagePlaying)
	{
		if (USkeletalMeshComponent* FighterMesh = GetFighterMesh())
		{
			if (UAnimInstance* AnimInstance = FighterMesh->GetAnimInstance())
			{
				AnimInstance->Montage_Stop(0.2f);
			}
		}

		bMontagePlaying = false;
	}
}

void UCombatExecutor_MeleeStrike::OnFinish()
{
	if (bMontagePlaying)
	{
		if (USkeletalMeshComponent* FighterMesh = GetFighterMesh())
		{
			if (UAnimInstance* AnimInstance = FighterMesh->GetAnimInstance())
			{
				AnimInstance->Montage_Stop(0.1f);
			}
		}

		bMontagePlaying = false;
	}

	// Old semantics preserved: a cancelled alignment must not silently stand
	// at the stance yaw facing nothing.
	AttackFacingIntent = FVector::ZeroVector;
}

void UCombatExecutor_MeleeStrike::OnExternalFinishRequest()
{
	// Legacy montage-end seam: committed -> recovery, same as old FinishAttack.
	if (Phase == EStrikePhase::Committed)
	{
		EnterRecovery();
		return;
	}

	FinishExecution(TEXT("external finish"));
}

void UCombatExecutor_MeleeStrike::GetEngagementRequirement(
	FCombatEngagementRequirement& OutRequirement) const
{
	if (Phase == EStrikePhase::Waiting)
	{
		OutRequirement = CachedRequirement;
		return;
	}

	OutRequirement = FCombatEngagementRequirement();
}

bool UCombatExecutor_MeleeStrike::GetFacingIntent(FVector& OutIntent) const
{
	OutIntent = AttackFacingIntent;
	return !AttackFacingIntent.IsNearlyZero();
}

bool UCombatExecutor_MeleeStrike::IsAwaitingAlignment() const
{
	return Phase == EStrikePhase::Waiting && bAtStancePosition;
}

float UCombatExecutor_MeleeStrike::GetFacingDeltaDegrees() const
{
	return IsAwaitingAlignment() ? FacingErrorDegrees : 0.f;
}

void UCombatExecutor_MeleeStrike::DrawAttackDebug()
{
	AActor* Fighter = GetFighter();
	UWorld* World = GetWorld();

	if (!Fighter || !World || !StrikeConfig()->bDrawDebugBlade)
	{
		return;
	}

	/*
	 * Committed trajectory in current pose vs actual weapon blade.
	 */
	for (int32 Index = 1;
		 Index <= PlannedContactSampleIndex && Index < PlannedTrajectory.Segments.Num();
		 ++Index)
	{
		const FBladeSegment& Previous = PlannedTrajectory.Segments[Index - 1];
		const FBladeSegment& Current = PlannedTrajectory.Segments[Index];

		DrawDebugLine(
			World,
			CommittedTransform.TransformPosition(Previous.Tip),
			CommittedTransform.TransformPosition(Current.Tip),
			FColor::Red,
			false,
			0.f,
			0,
			1.f);
	}
}
