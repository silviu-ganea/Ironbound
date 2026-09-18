#include "Combat/IronboundCombatExecutionComponent.h"
#include "Combat/IronboundEquipmentComponent.h"
#include "Combat/IronboundCombatFocusComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "GameFramework/Pawn.h"
#include "DrawDebugHelpers.h"
#include "Ironbound.h"


UIronboundCombatExecutionComponent::UIronboundCombatExecutionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}


// ============================================================================
// Orientation
// ============================================================================

FVector UIronboundCombatExecutionComponent::ResolveOrientationIntent(
	FVector LocomotionIntent) const
{
	if (!AttackFacingIntent.IsNearlyZero())
	{
		return AttackFacingIntent;
	}

	if (HasTarget())
	{
		return GetTargetFacingIntent();
	}

	return LocomotionIntent;
}


FVector UIronboundCombatExecutionComponent::GetTargetFacingIntent() const
{
	const AActor* Target = GetCombatFocus()->GetCombatTarget();

	const FVector OwnerLocation = GetOwner()->GetActorLocation();
	const FVector TargetLocation = Target->GetActorLocation();

	const FVector TargetFacingIntent =
		(TargetLocation - OwnerLocation).GetSafeNormal2D();

	return TargetFacingIntent;
}


bool UIronboundCombatExecutionComponent::HasTarget() const
{
	const UIronboundCombatFocusComponent* Focus = GetCombatFocus();

	return Focus && Focus->GetCombatTarget();
}


bool UIronboundCombatExecutionComponent::IsAligningOrCommitted() const
{
	return Phase == EIronboundAttackPhase::Aligning ||
		   Phase == EIronboundAttackPhase::Committed ||
		   Phase == EIronboundAttackPhase::Recovery;
}


float UIronboundCombatExecutionComponent::GetCombatFacingDelta() const
{
	float FacingDelta = 0.f;

	if (IsAligning() && !AttackFacingIntent.IsNearlyZero())
	{
		const float CurrentYaw = GetOwner()->GetActorRotation().Yaw;
		const float AttackYaw = AttackFacingIntent.Rotation().Yaw;

		FacingDelta =
			FMath::FindDeltaAngleDegrees(CurrentYaw, AttackYaw);
	}

	return FacingDelta;
}


// ============================================================================
// State
// ============================================================================

bool UIronboundCombatExecutionComponent::CanPlayAttack() const
{
	return Phase == EIronboundAttackPhase::Committed;
}


bool UIronboundCombatExecutionComponent::IsCommitted() const
{
	return Phase == EIronboundAttackPhase::Committed ||
		   Phase == EIronboundAttackPhase::Recovery;
}


bool UIronboundCombatExecutionComponent::IsAligning() const
{
	return Phase == EIronboundAttackPhase::Aligning;
}


// ============================================================================
// Attack preparation
// ============================================================================

bool UIronboundCombatExecutionComponent::PrepareAttack(
	USkeletalMeshComponent* TargetMesh,
	UAnimSequenceBase* Sequence,
	float StartTime,
	float EndTime,
	int32 NumSamples,
	const TArray<FName>& AllowedBones,
	float AcceptanceRadius)
{
	LastRequestTime = GetWorld()->GetTimeSeconds();

	if (IsCommitted())
	{
		return false;
	}

	if (!IsValidAttackRequest(TargetMesh))
	{
		CancelAttack();
		return false;
	}

	FBladeTrajectory Trajectory;

	if (!BuildAttackTrajectory(
		Sequence,
		StartTime,
		EndTime,
		NumSamples,
		Trajectory))
	{
		CancelAttack();
		return false;
	}

	if (!SolveAttackPlan(
		TargetMesh,
		Trajectory,
		AllowedBones,
		AcceptanceRadius))
	{
		CancelAttack();
		return false;
	}

	if (!IsAtAttackPosition())
	{
		ApproachAttackPosition();
		return false;
	}

	AlignAttack(TargetMesh, Trajectory, AllowedBones);

	if (!IsReadyToCommit(AcceptanceRadius))
	{
		return false;
	}

	CommitAttack(Sequence, Trajectory);

	return true;
}


bool UIronboundCombatExecutionComponent::IsValidAttackRequest(
	USkeletalMeshComponent* TargetMesh) const
{
	const UIronboundCombatFocusComponent* Focus = GetCombatFocus();

	return GetAIController() &&
		   TargetMesh &&
		   Focus &&
		   Focus->IsEnemy(TargetMesh->GetOwner()) &&
		   GetEquipment();
}


bool UIronboundCombatExecutionComponent::BuildAttackTrajectory(
	UAnimSequenceBase* Sequence,
	float StartTime,
	float EndTime,
	int32 NumSamples,
	FBladeTrajectory& OutTrajectory) const
{
	return GetEquipment()->GetTrajectory(
		Sequence,
		StartTime,
		EndTime,
		NumSamples,
		OutTrajectory);
}


bool UIronboundCombatExecutionComponent::SolveAttackPlan(
	USkeletalMeshComponent* TargetMesh,
	const FBladeTrajectory& Trajectory,
	const TArray<FName>& AllowedBones,
	float AcceptanceRadius)
{
	FName Bone;
	int32 Sample;
	float PlannedDistance;
	FTransform Candidate;

	const bool bFoundAttackPlan =
		UIronboundTrajectoryLibrary::SolveAttackAlignment(
			GetEquipment()->GetFighterMesh(),
			TargetMesh,
			Trajectory,
			AllowedBones,
			AcceptanceRadius,
			Candidate,
			Bone,
			Sample,
			PlannedDistance);

	if (bFoundAttackPlan)
	{
		PlannedTarget = TargetMesh->GetOwner();
		PlannedTransform = Candidate;
	}

	return bFoundAttackPlan;
}


// ============================================================================
// Approach
// ============================================================================

bool UIronboundCombatExecutionComponent::IsAtAttackPosition() const
{
	const float DistanceToAttackPosition =
		FVector::Dist2D(
			GetOwner()->GetActorLocation(),
			PlannedTransform.GetLocation());

	return DistanceToAttackPosition <= ArrivalTolerance;
}


void UIronboundCombatExecutionComponent::ApproachAttackPosition()
{
	Phase = EIronboundAttackPhase::Approaching;

	const EPathFollowingRequestResult::Type Result =
		GetAIController()->MoveToLocation(
			PlannedTransform.GetLocation(),
			ArrivalTolerance * 0.5f,
			false,
			true,
			true,
			true,
			nullptr,
			false);

	if (Result == EPathFollowingRequestResult::Failed)
	{
		CancelAttack();
	}
}


// ============================================================================
// Alignment
// ============================================================================

void UIronboundCombatExecutionComponent::AlignAttack(
	USkeletalMeshComponent* TargetMesh,
	const FBladeTrajectory& Trajectory,
	const TArray<FName>& AllowedBones)
{
	GetAIController()->StopMovement();

	Phase = EIronboundAttackPhase::Aligning;

	// This becomes our persistent combat facing.
	// It is NOT cleared when the attack/recovery finishes.
	AttackFacingIntent =
		PlannedTransform.GetRotation().GetForwardVector();

	FacingErrorDegrees =
		FMath::Abs(
			FMath::FindDeltaAngleDegrees(
				GetOwner()->GetActorRotation().Yaw,
				PlannedTransform.Rotator().Yaw));

	FName Bone;
	int32 Sample;

	CurrentPredictedDistance =
		UIronboundTrajectoryLibrary::EvaluateContact(
			Trajectory,
			GetOwner()->GetActorTransform(),
			TargetMesh,
			AllowedBones,
			Bone,
			Sample);
}


bool UIronboundCombatExecutionComponent::IsReadyToCommit(
	float AcceptanceRadius) const
{
	return FacingErrorDegrees <= FacingTolerance &&
		   MeasuredSpeed <= SettledSpeed &&
		   CurrentPredictedDistance <= AcceptanceRadius;
}


// ============================================================================
// Commitment
// ============================================================================

void UIronboundCombatExecutionComponent::CommitAttack(
	UAnimSequenceBase* Sequence,
	const FBladeTrajectory& Trajectory)
{
	Phase = EIronboundAttackPhase::Committed;
	bStrikeWindowOpen = false;

	CommittedTrajectory = Trajectory;
	CommittedTransform = GetOwner()->GetActorTransform();

	MaxCommittedFacingError = FacingErrorDegrees;

	// Montage completion/interruption is primary.
	// Deadline prevents failed playback from locking the AI forever.
	CommitDeadline =
		LastRequestTime +
		Sequence->GetPlayLength() +
		2.f;

	bHasPreviousTip = false;

	UE_LOG(
		LogIronboundCombat,
		Log,
		TEXT(
			"Attack committed: %s yaw=%.2f desired=%.2f "
			"error=%.2f actual-pose-contact=%.2f"),
		*GetNameSafe(GetOwner()),
		GetOwner()->GetActorRotation().Yaw,
		PlannedTransform.Rotator().Yaw,
		FacingErrorDegrees,
		CurrentPredictedDistance);
}


// ============================================================================
// Finish / Cancel
// ============================================================================

void UIronboundCombatExecutionComponent::FinishAttack()
{
	if (Phase != EIronboundAttackPhase::Committed)
	{
		return;
	}

	UE_LOG(
		LogIronboundCombat,
		Log,
		TEXT(
			"Attack finished: %s maximum facing error %.3f degrees"),
		*GetNameSafe(GetOwner()),
		MaxCommittedFacingError);

	Phase = EIronboundAttackPhase::Recovery;
	bStrikeWindowOpen = false;

	RecoveryUntil =
		GetWorld()->GetTimeSeconds() + RecoverySeconds;

	bHasPreviousTip = false;
}


void UIronboundCombatExecutionComponent::CancelAttack()
{
	if (AAIController* AI = GetAIController())
	{
		AI->StopMovement();
	}

	Phase = EIronboundAttackPhase::Idle;
	bStrikeWindowOpen = false;
	PlannedTarget.Reset();
	bHasPreviousTip = false;

	// Deliberately DO NOT clear AttackFacingIntent here.
	//
	// Ending an attack must not implicitly change the fighter's facing.
	// A later combat decision may deliberately choose a new facing.
}


// ============================================================================
// Tick
// ============================================================================

void UIronboundCombatExecutionComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(
		DeltaTime,
		TickType,
		ThisTickFunction);

	UpdateMeasuredSpeed(DeltaTime);

	const float Now = GetWorld()->GetTimeSeconds();

	if (HasAttackTimedOut(Now))
	{
		CancelAttack();
	}

	if (Phase == EIronboundAttackPhase::Committed)
	{
		UpdateCommittedFacingError();
		DrawAttackDebug();
	}
}


void UIronboundCombatExecutionComponent::UpdateMeasuredSpeed(
	float DeltaTime)
{
	const FVector CurrentLocation =
		GetOwner()->GetActorLocation();

	MeasuredSpeed =
		DeltaTime > SMALL_NUMBER
			? FVector::Dist2D(
				CurrentLocation,
				LastLocation) / DeltaTime
			: 0.f;

	LastLocation = CurrentLocation;
}


bool UIronboundCombatExecutionComponent::HasAttackTimedOut(
	float Now) const
{
	const bool bRecoveryFinished =
		Phase == EIronboundAttackPhase::Recovery &&
		Now >= RecoveryUntil;

	const bool bCommitExpired =
		Phase == EIronboundAttackPhase::Committed &&
		Now >= CommitDeadline;

	const bool bPreparationExpired =
		(Phase == EIronboundAttackPhase::Approaching ||
		 Phase == EIronboundAttackPhase::Aligning) &&
		(Now - LastRequestTime > 1.5f ||
		 !PlannedTarget.IsValid());

	return bRecoveryFinished ||
		   bCommitExpired ||
		   bPreparationExpired;
}


void UIronboundCombatExecutionComponent::UpdateCommittedFacingError()
{
	const float CurrentFacingError =
		FMath::Abs(
			FMath::FindDeltaAngleDegrees(
				GetOwner()->GetActorRotation().Yaw,
				PlannedTransform.Rotator().Yaw));

	MaxCommittedFacingError =
		FMath::Max(
			MaxCommittedFacingError,
			CurrentFacingError);
}


// ============================================================================
// Debug
// ============================================================================

void UIronboundCombatExecutionComponent::DrawAttackDebug()
{
	if (!bDrawActualBlade)
	{
		return;
	}

	DrawIntendedBladeTrajectory();

	if (bStrikeWindowOpen)
	{
		DrawActualBladeTrajectory();
	}
	else
	{
		bHasPreviousTip = false;
	}
}


void UIronboundCombatExecutionComponent::DrawIntendedBladeTrajectory()
{
	for (int32 I = 1;
		 I < CommittedTrajectory.Segments.Num();
		 ++I)
	{
		const FVector Start =
			CommittedTransform.TransformPosition(
				CommittedTrajectory.Segments[I - 1].Tip);

		const FVector End =
			CommittedTransform.TransformPosition(
				CommittedTrajectory.Segments[I].Tip);

		DrawDebugLine(
			GetWorld(),
			Start,
			End,
			FColor::Red,
			false,
			0.f,
			0,
			2.f);
	}
}


void UIronboundCombatExecutionComponent::DrawActualBladeTrajectory()
{
	const UIronboundEquipmentComponent* Equipment =
		GetEquipment();

	if (!Equipment ||
		!Equipment->bReady ||
		!Equipment->Definition ||
		!Equipment->GetWeapon())
	{
		return;
	}

	const FVector Tip =
		Equipment->GetWeapon()
			->GetComponentTransform()
			.TransformPosition(
				Equipment->Definition->BladeTip);

	if (bHasPreviousTip)
	{
		DrawDebugLine(
			GetWorld(),
			PreviousTip,
			Tip,
			FColor::Blue,
			false,
			2.f,
			0,
			2.f);
	}

	PreviousTip = Tip;
	bHasPreviousTip = true;
}


// ============================================================================
// Component access
// ============================================================================

AAIController* UIronboundCombatExecutionComponent::GetAIController() const
{
	const APawn* Pawn =
		Cast<APawn>(GetOwner());

	return Pawn
		? Cast<AAIController>(Pawn->GetController())
		: nullptr;
}


UIronboundEquipmentComponent*
UIronboundCombatExecutionComponent::GetEquipment() const
{
	return GetOwner()
		->FindComponentByClass<UIronboundEquipmentComponent>();
}


UIronboundCombatFocusComponent*
UIronboundCombatExecutionComponent::GetCombatFocus() const
{
	return GetOwner()
		->FindComponentByClass<UIronboundCombatFocusComponent>();
}