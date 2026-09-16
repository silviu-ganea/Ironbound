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

FVector UIronboundCombatExecutionComponent::ResolveOrientationIntent(FVector LocomotionIntent) const
{
	if (Phase == EIronboundAttackPhase::Aligning || Phase == EIronboundAttackPhase::Committed) return DesiredFacing;
	const auto* Focus = GetOwner()->FindComponentByClass<UIronboundCombatFocusComponent>();
	if (const AActor* Target = Focus ? Focus->GetCombatTarget() : nullptr)
		return (Target->GetActorLocation() - GetOwner()->GetActorLocation()).GetSafeNormal2D();
	return LocomotionIntent;
}

bool UIronboundCombatExecutionComponent::PrepareAttack(USkeletalMeshComponent* TargetMesh,
	UAnimSequenceBase* Sequence, float StartTime, float EndTime, int32 NumSamples,
	const TArray<FName>& AllowedBones, float AcceptanceRadius)
{
	LastRequestTime = GetWorld()->GetTimeSeconds();
	if (IsCommitted()) return false;
	auto* Pawn = Cast<APawn>(GetOwner());
	auto* AI = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
	auto* Equipment = GetOwner()->FindComponentByClass<UIronboundEquipmentComponent>();
	const auto* Focus = GetOwner()->FindComponentByClass<UIronboundCombatFocusComponent>();
	if (!AI || !TargetMesh || !Focus || !Focus->IsEnemy(TargetMesh->GetOwner()) || !Equipment)
	{
		CancelAttack();
		return false;
	}
	FBladeTrajectory Trajectory;
	if (!Equipment->GetTrajectory(Sequence, StartTime, EndTime, NumSamples, Trajectory))
	{
		CancelAttack();
		return false;
	}
	FName Bone;
	int32 Sample;
	float PlannedDistance;
	FTransform Candidate;
	if (!UIronboundTrajectoryLibrary::SolveAttackAlignment(Equipment->GetFighterMesh(), TargetMesh,
		Trajectory, AllowedBones, AcceptanceRadius, Candidate, Bone, Sample, PlannedDistance))
	{
		CancelAttack(); // An unreachable body region must not turn into permission to swing.
		return false;
	}
	PlannedTarget = TargetMesh->GetOwner();
	PlannedTransform = Candidate;
	const float DistanceToStance = FVector::Dist2D(GetOwner()->GetActorLocation(), Candidate.GetLocation());
	if (DistanceToStance > ArrivalTolerance)
	{
		Phase = EIronboundAttackPhase::Approaching;
		const auto Result = AI->MoveToLocation(Candidate.GetLocation(), ArrivalTolerance * 0.5f,
			false, true, true, true, nullptr, false);
		if (Result == EPathFollowingRequestResult::Failed) CancelAttack();
		return false;
	}
	AI->StopMovement();
	Phase = EIronboundAttackPhase::Aligning;
	DesiredFacing = Candidate.GetRotation().GetForwardVector();
	FacingErrorDegrees = FMath::Abs(FMath::FindDeltaAngleDegrees(GetOwner()->GetActorRotation().Yaw,
		Candidate.Rotator().Yaw));
	CurrentPredictedDistance = UIronboundTrajectoryLibrary::EvaluateContact(Trajectory,
		GetOwner()->GetActorTransform(), TargetMesh, AllowedBones, Bone, Sample);
	if (FacingErrorDegrees > FacingTolerance || MeasuredSpeed > SettledSpeed ||
		CurrentPredictedDistance > AcceptanceRadius) return false;
	Phase = EIronboundAttackPhase::Committed;
	bStrikeWindowOpen = false;
	CommittedTrajectory = Trajectory;
	CommittedTransform = GetOwner()->GetActorTransform();
	MaxCommittedFacingError = FacingErrorDegrees;
	// Montage completion/interruption is primary; deadline prevents a failed playback locking AI forever.
	CommitDeadline = LastRequestTime + Sequence->GetPlayLength() + 2.f;
	bHasPreviousTip = false;
	UE_LOG(LogIronboundCombat, Log, TEXT("Attack committed: %s yaw=%.2f desired=%.2f error=%.2f actual-pose-contact=%.2f"),
		*GetNameSafe(GetOwner()), GetOwner()->GetActorRotation().Yaw, Candidate.Rotator().Yaw,
		FacingErrorDegrees, CurrentPredictedDistance);
	return true;
}

void UIronboundCombatExecutionComponent::FinishAttack()
{
	if (Phase != EIronboundAttackPhase::Committed) return;
	UE_LOG(LogIronboundCombat, Log, TEXT("Attack finished: %s maximum facing error %.3f degrees"),
		*GetNameSafe(GetOwner()), MaxCommittedFacingError);
	Phase = EIronboundAttackPhase::Recovery;
	bStrikeWindowOpen = false;
	RecoveryUntil = GetWorld()->GetTimeSeconds() + RecoverySeconds;
	bHasPreviousTip = false;
}

void UIronboundCombatExecutionComponent::CancelAttack()
{
	if (auto* Pawn = Cast<APawn>(GetOwner()))
		if (auto* AI = Cast<AAIController>(Pawn->GetController())) AI->StopMovement();
	Phase = EIronboundAttackPhase::Idle;
	bStrikeWindowOpen = false;
	PlannedTarget.Reset();
	bHasPreviousTip = false;
}

void UIronboundCombatExecutionComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const FVector Location = GetOwner()->GetActorLocation();
	MeasuredSpeed = DeltaTime > SMALL_NUMBER ? FVector::Dist2D(Location, LastLocation) / DeltaTime : 0.f;
	LastLocation = Location;
	const float Now = GetWorld()->GetTimeSeconds();
	if ((Phase == EIronboundAttackPhase::Recovery && Now >= RecoveryUntil) ||
		(Phase == EIronboundAttackPhase::Committed && Now >= CommitDeadline) ||
		((Phase == EIronboundAttackPhase::Approaching || Phase == EIronboundAttackPhase::Aligning) &&
			(Now - LastRequestTime > 1.5f || !PlannedTarget.IsValid()))) CancelAttack();
	if (Phase != EIronboundAttackPhase::Committed) return;
	MaxCommittedFacingError = FMath::Max(MaxCommittedFacingError,
		float(FMath::Abs(FMath::FindDeltaAngleDegrees(GetOwner()->GetActorRotation().Yaw, PlannedTransform.Rotator().Yaw))));
	if (!bDrawActualBlade) return;
	// Compare the SAME strike interval: red = intended at commitment; blue = measured physical blade.
	for (int32 I = 1; I < CommittedTrajectory.Segments.Num(); ++I)
	{
		DrawDebugLine(GetWorld(), CommittedTransform.TransformPosition(CommittedTrajectory.Segments[I-1].Tip),
			CommittedTransform.TransformPosition(CommittedTrajectory.Segments[I].Tip), FColor::Red, false, 0.f, 0, 2.f);
	}
	if (!bStrikeWindowOpen) { bHasPreviousTip = false; return; }
	const auto* Equipment = GetOwner()->FindComponentByClass<UIronboundEquipmentComponent>();
	if (!Equipment || !Equipment->bReady || !Equipment->Definition || !Equipment->GetWeapon()) return;
	const FVector Tip = Equipment->GetWeapon()->GetComponentTransform().TransformPosition(Equipment->Definition->BladeTip);
	if (bHasPreviousTip) DrawDebugLine(GetWorld(), PreviousTip, Tip, FColor::Blue, false, 2.f, 0, 2.f);
	PreviousTip = Tip;
	bHasPreviousTip = true;
}
