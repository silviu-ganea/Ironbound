#include "Combat/CombatExecutor_MeleeStrike.h"

#include "Combat/CombatEquipmentComponent.h"
#include "Combat/CombatTarget.h"
#include "Combat/CombatTechniqueExecutionConfigs.h"
#include "Combat/CombatTechniqueRow.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
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

	if (InRequest.PlanId > 0 && !InRequest.PlannedOpportunity.bFeasible)
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("[AI] EXECUTION PlanId=%d Result=Refused Reason=MissingSelectedOpportunity"), InRequest.PlanId);
		return false;
	}
	if (!(InRequest.PlannedOpportunity.bFeasible
		? AdoptPlannedOpportunity(InRequest.PlannedOpportunity) : SolveAttackPlan()))
	{
		UE_LOG(
			LogIronboundCombat,
			Log,
			TEXT("MeleeStrike [%s | %s]: selected attack opportunity invalid (plan %d)"),
			*GetNameSafe(GetFighter()),
			*InRequest.TechniqueId.ToString(), InRequest.PlanId);

		return false;
	}

	RequestWorldTime = GetWorld()->GetTimeSeconds();
	InvalidContactStartWorldTime = 0.f;
	NextAlignmentDiagnosticWorldTime = RequestWorldTime + 1.f;
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
	AttackFacingIntent = PlannedTransform.GetRotation().GetForwardVector();

	UE_LOG(
		LogIronboundCombat,
		Log,
		TEXT("MeleeStrike [%s | %s] stance selected: plan=%d region=%s bone=%s score=%.1f miss=%.1f stance=%s yaw=%.1f sample=%d time=%.3f bladeFraction=%.2f"),
		*GetNameSafe(GetFighter()),
		*InRequest.TechniqueId.ToString(),
		InRequest.PlanId,
		*PlannedTargetRegion.ToString(),
		*PlannedTargetBone.ToString(),
		PlannedTargetScore,
		CurrentPredictedDistance,
		*PlannedTransform.GetLocation().ToCompactString(),
		PlannedTransform.Rotator().Yaw,
		PlannedContactSampleIndex,
		PlannedTrajectory.Segments.IsValidIndex(PlannedContactSampleIndex)
			? PlannedTrajectory.Segments[PlannedContactSampleIndex].TimeSeconds : -1.f,
		PlannedAimPointAlongBlade);

	return true;
}

bool UCombatExecutor_MeleeStrike::AdoptPlannedOpportunity(const FCombatAttackOpportunity& Opportunity)
{
	const AActor* Target = PlannedTarget.Get();
	USkeletalMeshComponent* TargetMesh = Target ? Target->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	const UExecConfig_MeleeStrike* LocalConfig = StrikeConfig();
	if (!TargetMesh || Opportunity.TechniqueId != GetRequest().TechniqueId ||
		FVector::Dist2D(Target->GetActorLocation(), Opportunity.TargetLocationAtQuery) > 75.f ||
		Opportunity.AimPointAlongBlade < -1.f || Opportunity.AimPointAlongBlade > 1.f ||
		(LocalConfig->AimPointAlongBlade >= 0.f &&
			!FMath::IsNearlyEqual(LocalConfig->AimPointAlongBlade, Opportunity.AimPointAlongBlade, 0.01f)) ||
		Opportunity.Stance.GetLocation().ContainsNaN())
	{
		return false;
	}
	const FName RequiredRegion = GetRequest().TargetRegion.IsNone()
		? LocalConfig->DefaultTargetRegion : GetRequest().TargetRegion;
	if (!RequiredRegion.IsNone() && RequiredRegion != Opportunity.Region)
	{
		return false;
	}
	const FVector Toward = (Target->GetActorLocation() - Opportunity.Stance.GetLocation()).GetSafeNormal2D();
	if (Toward.IsNearlyZero() ||
		FMath::Abs(FMath::FindDeltaAngleDegrees(Toward.Rotation().Yaw,
			Opportunity.Stance.Rotator().Yaw)) > LocalConfig->MaxFacingDeviationFromTargetDegrees + 0.01f)
	{
		return false;
	}
	FName Region, Bone;
	int32 Sample = INDEX_NONE;
	float Score = 0.f;
	const float Miss = UCombatTrajectoryLibrary::EvaluateScoredContact(
		PlannedTrajectory, Opportunity.Stance, TargetMesh, LocalConfig->CombatTargets,
		Region, Bone, Sample, Score, Opportunity.Region, Opportunity.AimPointAlongBlade,
		LocalConfig->AimWindowStartFraction, LocalConfig->AimWindowEndFraction,
		Opportunity.Bone);
	const float OpportunityContactLimit = FMath::Min(
		LocalConfig->ContactToleranceCm, UCombatTrajectoryLibrary::MaxOpportunityContactMissCm);
	if (Bone != Opportunity.Bone || Sample == INDEX_NONE || Miss > OpportunityContactLimit)
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("[AI] EXECUTION PlanId=%d AlignmentValid=false Reason=OpportunityContactMarginExceeded region=%s bone=%s miss=%.1f limit=%.1f"),
			GetRequest().PlanId, *Region.ToString(), *Bone.ToString(), Miss, OpportunityContactLimit);
		return false;
	}
	PlannedTransform = Opportunity.Stance;
	PlannedTargetRegion = Region;
	PlannedTargetBone = Bone;
	PlannedContactSampleIndex = Sample;
	PlannedTargetScore = Score;
	PlannedAimPointAlongBlade = Opportunity.AimPointAlongBlade;
	CurrentPredictedDistance = Miss;
	UE_LOG(LogIronboundCombat, Log,
		TEXT("[AI] EXECUTION PlanId=%d AlignmentValid=true stance=%s region=%s bone=%s miss=%.1f"),
		GetRequest().PlanId, *PlannedTransform.GetLocation().ToCompactString(),
		*Region.ToString(), *Bone.ToString(), Miss);
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
	PlannedAimPointAlongBlade = StrikeConfig()->AimPointAlongBlade;
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
		   CurrentPredictedDistance <= GetCommitContactToleranceCm();
}

float UCombatExecutor_MeleeStrike::GetCommitContactToleranceCm() const
{
	const UExecConfig_MeleeStrike* LocalConfig = StrikeConfig();
	if (!LocalConfig)
	{
		return 0.f;
	}
	if (GetRequest().PlanId <= 0)
	{
		return LocalConfig->ContactToleranceCm;
	}

	// The opportunity must start as a tight match when it is selected. Once
	// navigation and stance alignment finish, an animated target's hand may
	// have moved. Permit that bounded pose drift here; the actual weapon sweep
	// remains the authority for hit and damage.
	return FMath::Max(
		FMath::Min(LocalConfig->ContactToleranceCm, UCombatTrajectoryLibrary::MaxOpportunityContactMissCm),
		LocalConfig->MaxCommitTargetPoseDriftCm);
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
			UE_LOG(LogIronboundCombat, Warning,
				TEXT("MeleeStrike [%s | %s] stance timed out: plan=%d region=%s bone=%s arrived=%d miss=%.1fcm yaw=%.1fdeg speed=%.1fcm/s"),
				*GetNameSafe(Fighter), *GetRequest().TechniqueId.ToString(), GetRequest().PlanId,
				*PlannedTargetRegion.ToString(), *PlannedTargetBone.ToString(),
				bAtStancePosition ? 1 : 0, CurrentPredictedDistance,
				FacingErrorDegrees, MeasuredSpeed);
			FinishExecution(TEXT("stance timed out"));
			return;
		}

		// Keep the admitted stance stable while navigation satisfies it.
		// A later planner can explicitly replace this intent for moving targets;
		// running a global search every tick makes the goal orbit the defender.

		bAtStancePosition = IsAtStancePosition();
		UpdateFacingError();

		if (bAtStancePosition)
		{
			// Once positioned, only facing and settling remain; movement
			// during execution is no longer part of the engagement.
			CachedRequirement.bMayMoveDuringExecution = false;

			AttackFacingIntent = PlannedTransform.GetRotation().GetForwardVector();
			FName ActualRegion, ActualBone;
			int32 ActualSample = INDEX_NONE;
			float ActualScore = 0.f;
			CurrentPredictedDistance = UCombatTrajectoryLibrary::EvaluateScoredContact(
				PlannedTrajectory, Fighter->GetActorTransform(),
				PlannedTarget->FindComponentByClass<USkeletalMeshComponent>(),
				StrikeConfig()->CombatTargets, ActualRegion, ActualBone,
				ActualSample, ActualScore, PlannedTargetRegion,
				PlannedAimPointAlongBlade,
				StrikeConfig()->AimWindowStartFraction,
				StrikeConfig()->AimWindowEndFraction, PlannedTargetBone);
			if (FMath::Abs(FacingErrorDegrees) <= StrikeConfig()->FacingTolerance &&
				MeasuredSpeed <= StrikeConfig()->SettledSpeed &&
				CurrentPredictedDistance > GetCommitContactToleranceCm())
			{
				if (InvalidContactStartWorldTime <= 0.f) InvalidContactStartWorldTime = Now;
				if (Now - InvalidContactStartWorldTime > 0.75f)
				{
					UE_LOG(LogIronboundCombat, Warning,
					TEXT("[AI] EXECUTION PlanId=%d Result=ContactNoLongerValid Reason=TargetPoseDriftExceeded miss=%.1fcm limit=%.1fcm"),
					GetRequest().PlanId, CurrentPredictedDistance, GetCommitContactToleranceCm());
					FinishExecution(TEXT("contact no longer valid"));
					return;
				}
			}
			else InvalidContactStartWorldTime = 0.f;

			if (IsReadyToCommit())
			{
				CommitStrike();
			}
		}
		else
		{
			CachedRequirement.bMayMoveDuringExecution = true;
			AttackFacingIntent = PlannedTransform.GetRotation().GetForwardVector();
		}

		CachedRequirement.DesiredLocation = PlannedTransform.GetLocation();
		CachedRequirement.DesiredFacing = PlannedTransform.Rotator();
		DrawPlannedAttackDebug();
		if (Now >= NextAlignmentDiagnosticWorldTime)
		{
			const float PositionError = FVector::Dist(Fighter->GetActorLocation(), PlannedTransform.GetLocation());
			const float TargetDrift = PlannedTarget.IsValid()
				? FVector::Dist2D(PlannedTarget->GetActorLocation(), GetRequest().PlannedOpportunity.TargetLocationAtQuery)
				: -1.f;
			UE_LOG(LogIronboundCombat, Log,
				TEXT("[AI] ALIGNMENT PlanId=%d arrived=%d positionError=%.1f/%.1fcm yawError=%.1f/%.1fdeg speed=%.1f/%.1fcmps miss=%.1f/%.1fcm measured=%d targetDrift=%.1fcm actorYaw=%.1f desiredYaw=%.1f"),
				GetRequest().PlanId, bAtStancePosition ? 1 : 0,
				PositionError, StrikeConfig()->ArrivalTolerance,
				FacingErrorDegrees, StrikeConfig()->FacingTolerance,
				MeasuredSpeed, StrikeConfig()->SettledSpeed,
				CurrentPredictedDistance, GetCommitContactToleranceCm(),
				bAtStancePosition ? 1 : 0, TargetDrift,
				Fighter->GetActorRotation().Yaw, PlannedTransform.Rotator().Yaw);
			NextAlignmentDiagnosticWorldTime = Now + 1.f;
		}

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
	const UCombatEquipmentComponent* Equipment = GetEquipment();
	const UStaticMeshComponent* Weapon = Equipment ? Equipment->GetWeapon() : nullptr;
	const FTransform WeaponTransform = Weapon ? Weapon->GetComponentTransform() : FTransform::Identity;
	const UWeaponDefinition* WeaponDefinition = Equipment ? Equipment->Definition : nullptr;
	const FVector ActualBladeBase = WeaponDefinition
		? WeaponTransform.TransformPosition(WeaponDefinition->BladeBase) : FVector::ZeroVector;
	const FVector ActualBladeTip = WeaponDefinition
		? WeaponTransform.TransformPosition(WeaponDefinition->BladeTip) : FVector::ZeroVector;
	const FBladeSegment* PlannedContactSegment = PlannedTrajectory.Segments.IsValidIndex(PlannedContactSampleIndex)
		? &PlannedTrajectory.Segments[PlannedContactSampleIndex] : nullptr;
	const FVector PlannedBladeBase = PlannedContactSegment
		? CommittedTransform.TransformPosition(PlannedContactSegment->Base) : FVector::ZeroVector;
	const FVector PlannedBladeTip = PlannedContactSegment
		? CommittedTransform.TransformPosition(PlannedContactSegment->Tip) : FVector::ZeroVector;

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
		UE_LOG(LogIronboundCombat, Error,
			TEXT("[COMBAT] MONTAGE_FAILED PlanId=%d montage=%s playedLength=%.3f"),
			GetRequest().PlanId, *GetNameSafe(LocalConfig->Montage), PlayedLength);
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
		TEXT("MeleeStrike [%s | %s] committed: plan=%d region=%s bone=%s score=%.1f miss=%.1f yaw-error=%.1fdeg sample=%d sampleTime=%.3f bladeFraction=%.2f activeTime=%.3f..%.3f plannedRoot=%s plannedYaw=%.1f committedRoot=%s committedYaw=%.1f plannedBladeBase=%s plannedBladeTip=%s actualWeapon=%s weaponLoc=%s weaponRot=%s actualBladeBase=%s actualBladeTip=%s montage=%s sourceSequence=%s playedLength=%.3f montagePosition=%.3f"),
		*GetNameSafe(Fighter),
		*GetRequest().TechniqueId.ToString(),
		GetRequest().PlanId,
		*PlannedTargetRegion.ToString(),
		*PlannedTargetBone.ToString(),
		PlannedTargetScore,
		CurrentPredictedDistance,
		FMath::Abs(FacingErrorDegrees),
		PlannedContactSampleIndex,
		PlannedContactSegment ? PlannedContactSegment->TimeSeconds : -1.f,
		PlannedAimPointAlongBlade,
		PlannedTrajectory.ActiveStartTime,
		PlannedTrajectory.ActiveEndTime,
		*PlannedTransform.GetLocation().ToCompactString(), PlannedTransform.Rotator().Yaw,
		*CommittedTransform.GetLocation().ToCompactString(), CommittedTransform.Rotator().Yaw,
		*PlannedBladeBase.ToCompactString(), *PlannedBladeTip.ToCompactString(),
		*GetNameSafe(Weapon), *WeaponTransform.GetLocation().ToCompactString(),
		*WeaponTransform.Rotator().ToCompactString(), *ActualBladeBase.ToCompactString(),
		*ActualBladeTip.ToCompactString(), *GetNameSafe(LocalConfig->Montage),
		*GetNameSafe(LocalConfig->SourceSequence), PlayedLength,
		AnimInstance->Montage_GetPosition(LocalConfig->Montage));
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

void UCombatExecutor_MeleeStrike::DrawPlannedAttackDebug()
{
	const AActor* Fighter = GetFighter();
	UWorld* World = GetWorld();
	if (!Fighter || !World || !StrikeConfig()->bDrawDebugBlade ||
		PlannedTrajectory.Segments.IsEmpty())
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	if (Now + KINDA_SMALL_NUMBER < NextDebugDrawWorldTime)
	{
		return;
	}
	NextDebugDrawWorldTime = Now + 0.1f;
	constexpr float DebugLifetimeSeconds = 1.f;

	const FVector Stance = PlannedTransform.GetLocation();
	DrawDebugSphere(World, Stance, 12.f, 12, FColor::Orange, false, DebugLifetimeSeconds, 0, 1.5f);
	DrawDebugDirectionalArrow(World, Stance, Stance +
		PlannedTransform.GetRotation().GetForwardVector() * 65.f, 15.f,
		FColor::Orange, false, DebugLifetimeSeconds, 0, 2.f);
	DrawDebugLine(World, Fighter->GetActorLocation(), Stance,
		FColor::White, false, DebugLifetimeSeconds, 0, 0.8f);
	for (int32 Index = 1; Index < PlannedTrajectory.Segments.Num(); ++Index)
	{
		DrawDebugLine(World,
			PlannedTransform.TransformPosition(PlannedTrajectory.Segments[Index - 1].Tip),
			PlannedTransform.TransformPosition(PlannedTrajectory.Segments[Index].Tip),
			FColor::Cyan, false, DebugLifetimeSeconds, 0, 2.f);
	}
	if (PlannedTrajectory.Segments.IsValidIndex(PlannedContactSampleIndex))
	{
		const FBladeSegment& Contact = PlannedTrajectory.Segments[PlannedContactSampleIndex];
		const FVector Base = PlannedTransform.TransformPosition(Contact.Base);
		const FVector Tip = PlannedTransform.TransformPosition(Contact.Tip);
		DrawDebugLine(World, Base, Tip, FColor::Yellow, false, DebugLifetimeSeconds, 0, 3.f);
		if (PlannedAimPointAlongBlade >= 0.f)
		{
			DrawDebugSphere(World,
				FMath::Lerp(Base, Tip, FMath::Clamp(PlannedAimPointAlongBlade, 0.f, 1.f)),
				6.f, 10, FColor::Yellow, false, DebugLifetimeSeconds, 0, 2.f);
		}
	}
	if (const AActor* Target = PlannedTarget.Get())
	{
		if (const USkeletalMeshComponent* Mesh = Target->FindComponentByClass<USkeletalMeshComponent>())
		{
			DrawDebugSphere(World, Mesh->GetBoneLocation(PlannedTargetBone), 8.f, 12,
				FColor::Green, false, DebugLifetimeSeconds, 0, 1.5f);
		}
	}
	DrawDebugString(World, Stance + FVector(0.f, 0.f, 85.f),
		FString::Printf(TEXT("Plan %d: %s / %s tip miss %.1f cm, range %.1f cm"), GetRequest().PlanId,
			*PlannedTargetRegion.ToString(), *PlannedTargetBone.ToString(),
			CurrentPredictedDistance,
			FVector::Dist2D(Stance, PlannedTarget.IsValid()
				? PlannedTarget->GetActorLocation() : Stance)),
		nullptr, FColor::Cyan, DebugLifetimeSeconds, false);
}

void UCombatExecutor_MeleeStrike::DrawAttackDebug()
{
	AActor* Fighter = GetFighter();
	UWorld* World = GetWorld();

	if (!Fighter || !World || !StrikeConfig()->bDrawDebugBlade)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	if (Now + KINDA_SMALL_NUMBER < NextDebugDrawWorldTime)
	{
		return;
	}
	NextDebugDrawWorldTime = Now + 0.1f;

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
			1.f,
			0,
			1.f);
	}
}
