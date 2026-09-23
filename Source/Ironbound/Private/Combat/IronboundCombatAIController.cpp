#include "Combat/IronboundCombatAIController.h"

#include "Combat/BattleManager.h"
#include "Combat/CombatExecutionComponent.h"
#include "Combat/CombatExecutionTypes.h"
#include "Combat/CombatEquipmentComponent.h"
#include "Combat/CombatTechniqueExecutionConfigs.h"
#include "Combat/CombatFocusComponent.h"
#include "Combat/CombatTechniqueComponent.h"
#include "Combat/CombatTechniqueRow.h"
#include "Combat/CombatTarget.h"
#include "Combat/CombatThreatComponent.h"
#include "Combat/CombatThreatTypes.h"
#include "Combat/FighterComponent.h"
#include "Combat/FighterVitalsComponent.h"
#include "Combat/WeaponDefinition.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DrawDebugHelpers.h"
#include "Navigation/PathFollowingComponent.h"
#include "TimerManager.h"
#include "Ironbound.h"

namespace
{
	FString MakeFriendlyActorName(const AActor* Actor)
	{
		FString Name = GetNameSafe(Actor);
		if (Name.IsEmpty()) return TEXT("unknown target");

		const int32 LastUnderscore = Name.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (LastUnderscore != INDEX_NONE)
		{
			const FString InstanceNumber = Name.RightChop(LastUnderscore + 1);
			if (InstanceNumber.IsNumeric() && Name.Left(LastUnderscore).EndsWith(TEXT("_C")))
			{
				return FString::Printf(TEXT("Pawn %s"), *InstanceNumber);
			}
		}

		Name.RemoveFromEnd(TEXT("_C"));
		Name.ReplaceInline(TEXT("_"), TEXT(" "));
		return Name;
	}

	FString MakeFriendlyTargetPart(FName Region, FName Bone)
	{
		const FName PartName = Bone.IsNone() ? Region : Bone;
		FString Part = PartName.ToString().ToLower();
		if (Part.IsEmpty()) return TEXT("body");

		FString Side;
		if (Part.EndsWith(TEXT("_l")))
		{
			Part = Part.LeftChop(2);
			Side = TEXT("left ");
		}
		else if (Part.EndsWith(TEXT("_r")))
		{
			Part = Part.LeftChop(2);
			Side = TEXT("right ");
		}
		if (Part.StartsWith(TEXT("spine_")))
		{
			Part = TEXT("torso");
			Side.Reset();
		}
		Part.ReplaceInline(TEXT("_"), TEXT(" "));
		return Side + Part;
	}
}

AIronboundCombatAIController::AIronboundCombatAIController()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AIronboundCombatAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	CachePawnComponents();
	CurrentTarget.Reset();
	PendingThreatAttacker.Reset();
	LastAnsweredThreatAttacker.Reset();
	PendingThreatTechnique = NAME_None;
	LastAnsweredThreatTechnique = NAME_None;
	PendingThreatPlanId = 0;
	LastAnsweredThreatPlanId = 0;
	RecentAttackRegions.Reset();
	bHasMoveGoal = false;
	bMoveRequestActive = false;
	NextStandingPlanWorldTime = 0.f;
	bHadActiveDeliberate = false;
	bPlanEverCommitted = false;
	bLoggedNoDeliberateTechnique = false;
	bClosingForOpportunity = false;
	bHasRejectedOpportunity = false;
	ActivePlanId = 0;
	PursuitIntentId = 0;
	ConsecutiveFailedPlans = 0;
	ConsecutivePathFailures = 0;
	ActiveOpportunity = FCombatAttackOpportunity();
	PlannedTarget.Reset();
	MovementMode = EIronboundAIMovementMode::Idle;
	LastIntent.Reset();
	NextDecisionWorldTime = 0.f;
	NextDeliberateAttemptWorldTime = 0.f;
	NextPathRequestWorldTime = 0.f;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DecisionTimer);
		World->GetTimerManager().SetTimer(
			DecisionTimer,
			this,
			&AIronboundCombatAIController::DecisionStep,
			FMath::Max(0.05f, MinimumDecisionIntervalSeconds),
			true);
	}

	PublishIntent(TEXT("Starting"));
}

void AIronboundCombatAIController::OnUnPossess()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DecisionTimer);
	}

	StopMovement();
	ClearAttackPlan(TEXT("Unpossessed"), false);
	CurrentTarget.Reset();
	PendingThreatAttacker.Reset();
	LastAnsweredThreatAttacker.Reset();
	PendingThreatTechnique = NAME_None;
	LastAnsweredThreatTechnique = NAME_None;
	PendingThreatPlanId = 0;
	LastAnsweredThreatPlanId = 0;
	RecentAttackRegions.Reset();
	LastIntent.Reset();

	Super::OnUnPossess();
}

void AIronboundCombatAIController::CachePawnComponents()
{
	APawn* ControlledPawn = GetPawn();
	Fighter = ControlledPawn ? ControlledPawn->FindComponentByClass<UFighterComponent>() : nullptr;
	Vitals = ControlledPawn ? ControlledPawn->FindComponentByClass<UFighterVitalsComponent>() : nullptr;
	Focus = ControlledPawn ? ControlledPawn->FindComponentByClass<UCombatFocusComponent>() : nullptr;
	Execution = ControlledPawn ? ControlledPawn->FindComponentByClass<UCombatExecutionComponent>() : nullptr;

	// Execution lazily creates these domain components; requesting them here
	// ensures AI and player paths see the same live registry/observer instances.
	Techniques = Execution ? Execution->GetTechniques() : nullptr;
	if (Execution)
	{
		Execution->GetThreats();
	}

	if (!Fighter || !Vitals || !Focus || !Execution || !Techniques)
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("Combat AI [%s]: pawn is missing a required combat component"),
			*GetNameSafe(ControlledPawn));
	}
}

bool AIronboundCombatAIController::IsLivingEnemy(const AActor* Candidate) const
{
	if (!IsValid(Candidate) || Candidate == GetPawn() || !Fighter || !Fighter->IsEnemy(Candidate))
	{
		return false;
	}

	const UFighterVitalsComponent* CandidateVitals =
		Candidate->FindComponentByClass<UFighterVitalsComponent>();
	return !CandidateVitals || CandidateVitals->IsAlive();
}

AActor* AIronboundCombatAIController::SelectEnemy()
{
	if (AActor* Existing = CurrentTarget.Get(); IsLivingEnemy(Existing))
	{
		return Existing;
	}

	if (Focus)
	{
		if (AActor* ExistingFocus = Focus->GetCombatTarget(); IsLivingEnemy(ExistingFocus))
		{
			CurrentTarget = ExistingFocus;
			return ExistingFocus;
		}
	}

	AActor* BestTarget = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();

	// Prefer the battle registry when one exists. The actor scan is a useful
	// placed-pawn prototype fallback and still uses FighterComponent for teams.
	TArray<AActor*> Candidates;
	if (ABattleManager* Battle = ABattleManager::FindBattleManager(this))
	{
		Candidates = Battle->GetAllFighters();
	}
	else if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (IsValid(*It) && (*It)->FindComponentByClass<UFighterComponent>())
			{
				Candidates.Add(*It);
			}
		}
	}

	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return nullptr;
	}

	for (AActor* Candidate : Candidates)
	{
		if (!IsLivingEnemy(Candidate))
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(
			ControlledPawn->GetActorLocation(), Candidate->GetActorLocation());
		if (DistanceSquared < BestDistanceSquared)
		{
			BestTarget = Candidate;
			BestDistanceSquared = DistanceSquared;
		}
	}

	CurrentTarget = BestTarget;
	return BestTarget;
}

FName AIronboundCombatAIController::FindAvailableTechnique(
	ECombatTechniqueKind Kind,
	FName Preferred) const
{
	if (!Techniques)
	{
		return NAME_None;
	}

	if (!Preferred.IsNone())
	{
		const FCombatTechniqueRow* Row = Techniques->FindRow(Preferred);
		if (Row && Row->Kind == Kind && Techniques->IsAvailable(Preferred))
		{
			return Preferred;
		}
	}

	const UFighterComponent* CurrentFighter = Fighter;
	if (!CurrentFighter)
	{
		return NAME_None;
	}

	// Preserve repertoire order: designers can control preference by ordering
	// selected techniques on the fighter's battle loadout.
	for (const FName TechniqueId : CurrentFighter->GetBattleRepertoire())
	{
		const FCombatTechniqueRow* Row = Techniques->FindRow(TechniqueId);
		if (Row && Row->Kind == Kind && Techniques->IsAvailable(TechniqueId))
		{
			return TechniqueId;
		}
	}

	return NAME_None;
}

float AIronboundCombatAIController::GetEffectiveDecisionInterval() const
{
	const float Agility = Fighter ? FMath::Max(0.25f, Fighter->GetBattleAttributes().Agility) : 1.f;
	return FMath::Clamp(
		DecisionIntervalSeconds / Agility,
		FMath::Max(0.05f, MinimumDecisionIntervalSeconds),
		FMath::Max(MinimumDecisionIntervalSeconds, MaximumDecisionIntervalSeconds));
}

float AIronboundCombatAIController::GetEffectiveReactionDelay() const
{
	const float Agility = Fighter ? Fighter->GetBattleAttributes().Agility : 1.f;
	const float AgilityAboveBaseline = FMath::Max(0.f, Agility - 1.f);
	return FMath::Max(
		MinimumReactionDelaySeconds,
		BaseReactionDelaySeconds - AgilityAboveBaseline * AgilityReactionBonusSeconds);
}

FVector AIronboundCombatAIController::ResolveCombatMovementFacing(FVector TravelIntent) const
{
	if (MovementMode == EIronboundAIMovementMode::Pursuit ||
		MovementMode == EIronboundAIMovementMode::Idle)
	{
		return TravelIntent;
	}
	if (MovementMode == EIronboundAIMovementMode::AttackAlignment && ActiveOpportunity.bFeasible)
	{
		return ActiveOpportunity.Stance.GetRotation().GetForwardVector();
	}
	if (MovementMode == EIronboundAIMovementMode::GuardManeuver && GetPawn())
	{
		const AActor* FacingTarget = PlannedTarget.IsValid() ? PlannedTarget.Get() : CurrentTarget.Get();
		if (!FacingTarget) return TravelIntent;
		FVector Facing = (FacingTarget->GetActorLocation() - GetPawn()->GetActorLocation()).GetSafeNormal2D();
		if (const USkeletalMeshComponent* Mesh = GetPawn()->FindComponentByClass<USkeletalMeshComponent>())
		{
			Facing = FRotator(0.f, Facing.Rotation().Yaw - Mesh->GetRelativeRotation().Yaw, 0.f).Vector();
		}
		return Facing;
	}
	return Execution ? Execution->ResolveOrientationIntent(TravelIntent) : TravelIntent;
}

void AIronboundCombatAIController::SetMovementMode(EIronboundAIMovementMode NewMode, const TCHAR* Reason)
{
	if (MovementMode == NewMode) return;
	// GASP reads Mover's crouch state for its crouch locomotion. Use it only
	// during footwork; restore standing geometry before strike alignment.
	if (APawn* ControlledPawn = GetPawn())
	{
		if (UCharacterMoverComponent* Mover = ControlledPawn->FindComponentByClass<UCharacterMoverComponent>())
		{
			if (NewMode == EIronboundAIMovementMode::GuardManeuver)
			{
				if (!Mover->IsCrouching() && Mover->CanCrouch()) Mover->Crouch();
			}
			else if (Mover->IsCrouching())
			{
				Mover->UnCrouch();
				NextStandingPlanWorldTime = GetWorld()
					? GetWorld()->GetTimeSeconds() + 0.25f : 0.f;
			}
		}
	}
	if (NewMode == EIronboundAIMovementMode::Pursuit)
	{
		PursuitIntentId = NextPlanId++;
		UE_LOG(LogIronboundCombat, Log,
			TEXT("[AI] DECISION %d Trigger=%s Target=%s Intent=Pursuit"),
			PursuitIntentId, Reason, *GetNameSafe(CurrentTarget.Get()));
	}
	MovementMode = NewMode;
	const TCHAR* ModeName = TEXT("Idle");
	switch (NewMode)
	{
	case EIronboundAIMovementMode::Pursuit: ModeName = TEXT("PursuitFacingTravel"); break;
	case EIronboundAIMovementMode::GuardManeuver: ModeName = TEXT("GuardTargetFacingStrafe"); break;
	case EIronboundAIMovementMode::AttackAlignment: ModeName = TEXT("AttackAlignmentFacingStance"); break;
	case EIronboundAIMovementMode::CommittedAttack: ModeName = TEXT("CommittedExecutionOwnsMovement"); break;
	default: break;
	}
	UE_LOG(LogIronboundCombat, Log, TEXT("[AI] MOVEMENT PlanId=%d Mode=%s Reason=%s"),
		NewMode == EIronboundAIMovementMode::Pursuit ? PursuitIntentId : ActivePlanId, ModeName, Reason);
	if (NewMode == EIronboundAIMovementMode::GuardManeuver &&
		(PlannedTarget.IsValid() || CurrentTarget.IsValid()))
	{
		SetFocus(PlannedTarget.IsValid() ? PlannedTarget.Get() : CurrentTarget.Get());
	}
	else if (NewMode == EIronboundAIMovementMode::Pursuit || NewMode == EIronboundAIMovementMode::Idle)
	{
		ClearFocus(EAIFocusPriority::Gameplay);
	}
}

void AIronboundCombatAIController::DrawRejectedOpportunity() const
{
	UWorld* World = GetWorld();
	if (!bHasRejectedOpportunity || !World ||
		RejectedTrajectory.Segments.IsEmpty() ||
		RejectedOpportunity.ContactSample == INDEX_NONE)
	{
		return;
	}
	const FTransform& Stance = RejectedOpportunity.Stance;
	DrawDebugSphere(World, Stance.GetLocation(), 10.f, 10, FColor::Orange, false, 0.25f);
	for (int32 Index = 1; Index < RejectedTrajectory.Segments.Num(); ++Index)
	{
		DrawDebugLine(World,
			Stance.TransformPosition(RejectedTrajectory.Segments[Index - 1].Tip),
			Stance.TransformPosition(RejectedTrajectory.Segments[Index].Tip),
			FColor::Orange, false, 0.25f, 0, 1.5f);
	}
	DrawDebugString(World, Stance.GetLocation() + FVector(0.f, 0.f, 95.f),
		FString::Printf(TEXT("No hit: %.1f / %.1f cm (closest candidate)"),
			RejectedOpportunity.MissCm, RejectedContactToleranceCm),
		nullptr, FColor::Orange, 0.25f, false);
}

void AIronboundCombatAIController::SuspendFailedClosePursuit()
{
	if (!bClosingForOpportunity || ConsecutivePathFailures < 3 || !GetPawn()) return;
	ConsecutiveFailedPlans = 3;
	FailedPlanTargetLocation = CurrentTarget.IsValid()
		? CurrentTarget->GetActorLocation() : FVector::ZeroVector;
	FailedPlanAttackerLocation = GetPawn()->GetActorLocation();
	bClosingForOpportunity = false;
	UE_LOG(LogIronboundCombat, Warning,
		TEXT("[AI] PLANNING SUSPENDED Reason=CloseApproachNavigationFailed failures=%d"),
		ConsecutivePathFailures);
	StopMovement();
	bHasMoveGoal = false;
	bMoveRequestActive = false;
	SetMovementMode(EIronboundAIMovementMode::Idle, TEXT("CloseApproachNavigationFailed"));
}

void AIronboundCombatAIController::ClearAttackPlan(const TCHAR* Reason, bool bCancelExecution)
{
	if (ActivePlanId != 0)
	{
		UE_LOG(LogIronboundCombat, Log,
			TEXT("[AI] PLAN TERMINAL PlanId=%d Reason=%s Committed=%d"),
			ActivePlanId, Reason, bPlanEverCommitted ? 1 : 0);
		if (FCString::Strcmp(Reason, TEXT("AttackRecovered")) == 0)
		{
			ConsecutiveFailedPlans = 0;
			ConsecutivePathFailures = 0;
		}
		else if (!bPlanEverCommitted &&
			FCString::Strcmp(Reason, TEXT("TargetChanged")) != 0 &&
			FCString::Strcmp(Reason, TEXT("TargetUnavailable")) != 0 &&
			FCString::Strcmp(Reason, TEXT("OwnerDied")) != 0 &&
			FCString::Strcmp(Reason, TEXT("ExecutionTerminatedBeforeCommit")) != 0 &&
			FCString::Strcmp(Reason, TEXT("Unpossessed")) != 0)
		{
			++ConsecutiveFailedPlans;
			FailedPlanTargetLocation = PlannedTarget.IsValid()
				? PlannedTarget->GetActorLocation() : FVector::ZeroVector;
			FailedPlanAttackerLocation = GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector;
			if (ConsecutiveFailedPlans >= 3)
			{
				UE_LOG(LogIronboundCombat, Warning,
					TEXT("[AI] PLANNING SUSPENDED failedPlans=%d Reason=RepeatedFailure WaitingForTargetDisplacement"),
					ConsecutiveFailedPlans);
			}
		}
	}
	if (bCancelExecution && Execution && !Execution->IsCommitted())
	{
		Execution->CancelPlannedExecution(ActivePlanId, Reason);
	}
	StopMovement();
	bHasMoveGoal = false;
	bMoveRequestActive = false;
	bHadActiveDeliberate = false;
	bPlanEverCommitted = false;
	ActivePlanId = 0;
	ActiveOpportunity = FCombatAttackOpportunity();
	PlannedTarget.Reset();
	bHasRejectedOpportunity = false;
	bClosingForOpportunity = false;
	SetMovementMode(EIronboundAIMovementMode::Idle, Reason);
}

void AIronboundCombatAIController::ChooseAttackPlan(AActor* Target, float Now)
{
	const FName TechniqueId = FindAvailableTechnique(ECombatTechniqueKind::Deliberate,
		PreferredDeliberateTechnique);
	NextDeliberateAttemptWorldTime = Now + FMath::Max(0.05f, DeliberateRetryIntervalSeconds);
	if (TechniqueId.IsNone() && bLoggedNoDeliberateTechnique) return;
	const int32 DecisionId = NextPlanId++;
	auto RecordPlanningFailure = [&]()
	{
		++ConsecutiveFailedPlans;
		FailedPlanTargetLocation = Target->GetActorLocation();
		FailedPlanAttackerLocation = GetPawn()->GetActorLocation();
		if (ConsecutiveFailedPlans >= 3)
		{
			UE_LOG(LogIronboundCombat, Warning,
				TEXT("[AI] PLANNING SUSPENDED failedPlans=%d Reason=RepeatedUnviableOpportunity WaitingForTargetDisplacement"),
				ConsecutiveFailedPlans);
		}
	};
	if (TechniqueId.IsNone())
	{
		if (!bLoggedNoDeliberateTechnique)
		{
			UE_LOG(LogIronboundCombat, Log,
				TEXT("[AI] DECISION %d Target=%s Result=NoAvailableDeliberateTechnique"),
				DecisionId, *GetNameSafe(Target));
			bLoggedNoDeliberateTechnique = true;
		}
		return;
	}
	bLoggedNoDeliberateTechnique = false;
	UE_LOG(LogIronboundCombat, Log,
		TEXT("[AI] DECISION %d Trigger=ReadyAfterRecoveryOrInvalidation Target=%s Technique=%s"),
		DecisionId, *GetNameSafe(Target), *TechniqueId.ToString());
	const FCombatTechniqueRow* Row = Techniques->FindRow(TechniqueId);
	const UExecConfig_MeleeStrike* Config = Row ? Cast<UExecConfig_MeleeStrike>(Row->ExecutionConfig) : nullptr;
	UCombatEquipmentComponent* Equipment = GetPawn()->FindComponentByClass<UCombatEquipmentComponent>();
	USkeletalMeshComponent* AttackerMesh = GetPawn()->FindComponentByClass<USkeletalMeshComponent>();
	USkeletalMeshComponent* TargetMesh = Target->FindComponentByClass<USkeletalMeshComponent>();
	FBladeTrajectory Trajectory;
	if (!Config || !Equipment || !AttackerMesh || !TargetMesh ||
		!Equipment->GetTrajectory(Config->SourceSequence, Trajectory))
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("[AI] CHOICE PlanId=%d Result=PlanningInputsUnavailable"), DecisionId);
		return;
	}
	const FName RequiredRegion = PreferredAttackTargetRegion.IsNone()
		? Config->DefaultTargetRegion : PreferredAttackTargetRegion;
	// Planning follows the technique's authored contact policy: -1 tracks
	// the sword tip through the sampled swing.
	const float PlanningAimPoint = Config->AimPointAlongBlade;
	TArray<FCombatAttackOpportunity> Opportunities;
	UCombatTrajectoryLibrary::FindAttackOpportunities(AttackerMesh, TargetMesh, Trajectory,
		Config->CombatTargets, TechniqueId, RequiredRegion, PlanningAimPoint,
		Config->AimWindowStartFraction, Config->AimWindowEndFraction,
		Config->MaxFacingDeviationFromTargetDegrees, Config->ContactToleranceCm,
		MaxNearbyMoveCm, Opportunities);

	TArray<FCombatAttackOpportunity> FeasibleOpportunities;
	FCombatAttackOpportunity ClosestRejected;
	for (const FCombatAttackOpportunity& Opportunity : Opportunities)
	{
		if (Opportunity.bFeasible)
		{
			FeasibleOpportunities.Add(Opportunity);
		}
		else if (Opportunity.ContactSample != INDEX_NONE &&
			Opportunity.MissCm < ClosestRejected.MissCm)
		{
			ClosestRejected = Opportunity;
		}
	}

	const float WeaponProficiency = Fighter && Equipment && Equipment->Definition
		? Fighter->GetEffectiveWeaponProficiency(Equipment->Definition)
		: 0.25f;
	float LearnedSkillProficiency = 0.5f;
	if (Row && !Row->RequiredSkills.IsEmpty())
	{
		LearnedSkillProficiency = 0.f;
		for (const FName SkillId : Row->RequiredSkills)
		{
			LearnedSkillProficiency += Techniques->GetSkillProficiency(SkillId, 0.25f);
		}
		LearnedSkillProficiency /= static_cast<float>(Row->RequiredSkills.Num());
	}
	const float DecisionSkill = FMath::Clamp(
		0.55f * WeaponProficiency + 0.45f * LearnedSkillProficiency, 0.f, 1.f);

	struct FScoredOpportunity
	{
		FCombatAttackOpportunity Opportunity;
		float Score = -TNumericLimits<float>::Max();
		float ExpectedDamage = 0.f;
	};
	TArray<FScoredOpportunity> ScoredOpportunities;
	const float MaximumMovement = FMath::Max(MaxNearbyMoveCm, 1.f);
	for (const FCombatAttackOpportunity& Opportunity : FeasibleOpportunities)
	{
		const FCombatTargetRow* TargetRow = Config->CombatTargets
			? Config->CombatTargets->FindRow<FCombatTargetRow>(
				Opportunity.Region, TEXT("AI attack target selection"), false)
			: nullptr;
		if (!TargetRow)
		{
			continue;
		}

		const float ExpectedDamage = FMath::Max(0.f,
			Row->DamageProfile.CompatibilityDamage *
			CombatTargetRules::GetDamageMultiplier(Opportunity.Region, *TargetRow));
		const float Desirability = FMath::Clamp(TargetRow->Score / 100.f, 0.f, 1.f);
		const float DamageValue = FMath::Clamp(ExpectedDamage / 20.f, 0.f, 1.f);
		const float ContactQuality = FMath::Clamp(
			(UCombatTrajectoryLibrary::MaxOpportunityContactMissCm - Opportunity.MissCm) /
			(UCombatTrajectoryLibrary::MaxOpportunityContactMissCm +
			CombatTargetRules::GetContactRadiusCm(Opportunity.Region, *TargetRow)),
			0.f, 1.f);
		const float RangeQuality = FMath::Clamp(
			Opportunity.StandoffCm / FMath::Max(Trajectory.ReachMax + 60.f, 1.f), 0.f, 1.f);
		const float MovementPenalty = 0.08f * FMath::Clamp(
			Opportunity.MovementCostCm / MaximumMovement, 0.f, 1.f);
		const float TacticalScore =
			0.25f * Desirability +
			0.30f * DamageValue +
			0.20f * ContactQuality +
			0.25f * RangeQuality -
			MovementPenalty;

		// Keep an already viable in-place strike unless repositioning materially
		// improves its contact or tactical value. This hysteresis stops small
		// pose changes from pulling the controller into repeated lateral footwork.
		if (Opportunity.MovementCostCm > 1.f)
		{
			const FScoredOpportunity* CurrentSameRegion = ScoredOpportunities.FindByPredicate(
				[&Opportunity](const FScoredOpportunity& Candidate)
				{
					return Candidate.Opportunity.Region == Opportunity.Region &&
						Candidate.Opportunity.MovementCostCm <= 1.f;
				});
			if (CurrentSameRegion &&
				TacticalScore < CurrentSameRegion->Score + MeaningfulQualityGain)
			{
				continue;
			}
		}

		FScoredOpportunity& Scored = ScoredOpportunities.AddDefaulted_GetRef();
		Scored.Opportunity = Opportunity;
		Scored.Score = TacticalScore;
		Scored.ExpectedDamage = ExpectedDamage;
	}

	if (ScoredOpportunities.IsEmpty())
	{
		RejectedTrajectory = Trajectory;
		RejectedOpportunity = ClosestRejected;
		RejectedContactToleranceCm = Config->ContactToleranceCm;
		bHasRejectedOpportunity = RejectedOpportunity.ContactSample != INDEX_NONE;
		const float Distance = FVector::Dist2D(GetPawn()->GetActorLocation(), Target->GetActorLocation());
		if (Distance > CloseApproachDistanceCm)
		{
			UE_LOG(LogIronboundCombat, Log,
				TEXT("[AI] CHOICE PlanId=%d Intent=CloseForOpportunity Reason=NoViableContactAtPlanningBoundary distance=%.1fcm closeAt=%.1fcm nearestMiss=%.1f/%.1fcm"),
				DecisionId, Distance, CloseApproachDistanceCm,
				RejectedOpportunity.MissCm, Config->ContactToleranceCm);
			bClosingForOpportunity = true;
			ConsecutivePathFailures = 0;
			SetMovementMode(EIronboundAIMovementMode::Pursuit,
				TEXT("NoNearbyContactAtPlanningBoundary"));
			return;
		}
		UE_LOG(LogIronboundCombat, Log,
			TEXT("[AI] CHOICE PlanId=%d Result=NoViableOpportunity Reason=CurrentAndNearbyContactInvalid"), DecisionId);
		RecordPlanningFailure();
		return;
	}

	// Cycle through feasible anatomy: an otherwise reachable region is not
	// discarded because its authored score is lower, and the fallback permits
	// repetition when the recent set contains every feasible region.
	float BestScore = -TNumericLimits<float>::Max();
	for (const FScoredOpportunity& Candidate : ScoredOpportunities)
	{
		BestScore = FMath::Max(BestScore, Candidate.Score);
	}
	TArray<int32> SelectionPool;
	TArray<int32> DiversePool;
	for (int32 Index = 0; Index < ScoredOpportunities.Num(); ++Index)
	{
		const FScoredOpportunity& Candidate = ScoredOpportunities[Index];
		SelectionPool.Add(Index);
		if (!RecentAttackRegions.Contains(Candidate.Opportunity.Region))
		{
			DiversePool.Add(Index);
		}
	}
	if (!DiversePool.IsEmpty())
	{
		SelectionPool = MoveTemp(DiversePool);
	}
	else
	{
		// All currently feasible regions were used recently. Start a new cycle
		// so a two-region duel alternates instead of repeating its top target.
		RecentAttackRegions.Reset();
		SelectionPool.Reset();
		for (int32 Index = 0; Index < ScoredOpportunities.Num(); ++Index)
		{
			SelectionPool.Add(Index);
		}
	}

	const float Temperature = FMath::Lerp(
		FMath::Clamp(RepositionPreference, 0.02f, 1.f), 0.025f, DecisionSkill);
	float TotalWeight = 0.f;
	TArray<float> Weights;
	for (const int32 Index : SelectionPool)
	{
		const float Weight = FMath::Exp(FMath::Clamp(
			(ScoredOpportunities[Index].Score - BestScore) / Temperature, -12.f, 0.f));
		Weights.Add(Weight);
		TotalWeight += Weight;
	}
	uint32 Seed = HashCombine(GetTypeHash(GetPawn()), GetTypeHash(DecisionId));
	FRandomStream Random( static_cast<int32>(Seed) );
	float Pick = Random.FRand() * TotalWeight;
	int32 SelectedIndex = SelectionPool.Last();
	for (int32 PoolIndex = 0; PoolIndex < SelectionPool.Num(); ++PoolIndex)
	{
		Pick -= Weights[PoolIndex];
		if (Pick <= 0.f)
		{
			SelectedIndex = SelectionPool[PoolIndex];
			break;
		}
	}

	const FScoredOpportunity& Chosen = ScoredOpportunities[SelectedIndex];
	ActiveOpportunity = Chosen.Opportunity;
	const bool bReposition = ActiveOpportunity.MovementCostCm > 1.f;
	UE_LOG(LogIronboundCombat, Log,
		TEXT("[AI] OPPORTUNITY_SET PlanId=%d count=%d selectedRegion=%s bone=%s score=%.3f expectedDamage=%.2f missToSurface=%.1fcm standoff=%.1fcm movement=%.1fcm skill=%.2f temperature=%.3f"),
		DecisionId, ScoredOpportunities.Num(), *ActiveOpportunity.Region.ToString(),
		*ActiveOpportunity.Bone.ToString(), Chosen.Score, Chosen.ExpectedDamage,
		ActiveOpportunity.MissCm, ActiveOpportunity.StandoffCm,
		ActiveOpportunity.MovementCostCm, DecisionSkill, Temperature);
	for (const FScoredOpportunity& Candidate : ScoredOpportunities)
	{
		UE_LOG(LogIronboundCombat, VeryVerbose,
			TEXT("[AI] OPPORTUNITY PlanId=%d region=%s bone=%s score=%.3f damage=%.2f miss=%.1fcm move=%.1fcm range=%.1fcm"),
			DecisionId, *Candidate.Opportunity.Region.ToString(),
			*Candidate.Opportunity.Bone.ToString(), Candidate.Score,
			Candidate.ExpectedDamage, Candidate.Opportunity.MissCm,
			Candidate.Opportunity.MovementCostCm, Candidate.Opportunity.StandoffCm);
	}
	bHasRejectedOpportunity = false;
	bClosingForOpportunity = false;
	ActivePlanId = DecisionId;
	PlannedTarget = Target;
	UE_LOG(LogIronboundCombat, Log,
		TEXT("[AI] CHOICE PlanId=%d Intent=%s Reason=%s region=%s bone=%s quality=%.1f range=%.1fcm surfaceMiss=%.1fcm move=%.1fcm destination=%s yaw=%.1f"),
		ActivePlanId, bReposition ? TEXT("RepositionForAttack") : TEXT("AttackFromCurrentPosition"),
		bReposition ? TEXT("FeasibleOpportunityImprovesPolicyScoreBeyondHysteresis") : TEXT("BestFeasibleOpportunityAtCurrentPosition"),
		*ActiveOpportunity.Region.ToString(), *ActiveOpportunity.Bone.ToString(),
		ActiveOpportunity.Quality, ActiveOpportunity.StandoffCm, ActiveOpportunity.MissCm,
		ActiveOpportunity.MovementCostCm,
		*ActiveOpportunity.Stance.GetLocation().ToCompactString(), ActiveOpportunity.Stance.Rotator().Yaw);
	FCombatTechniqueRequest Request;
	Request.TechniqueId = TechniqueId;
	Request.Target = Target;
	Request.TargetRegion = ActiveOpportunity.Region;
	Request.PlannedOpportunity = ActiveOpportunity;
	Request.PlanId = ActivePlanId;
	if (!Execution->RequestTechnique(Request))
	{
		ClearAttackPlan(TEXT("ExecutionRefusedSelectedOpportunity"), false);
		return;
	}
	RecentAttackRegions.Add(ActiveOpportunity.Region);
	while (RecentAttackRegions.Num() > 2)
	{
		RecentAttackRegions.RemoveAt(0);
	}
	bHadActiveDeliberate = true;
	SetMovementMode(bReposition ? EIronboundAIMovementMode::GuardManeuver :
		EIronboundAIMovementMode::AttackAlignment, TEXT("SelectedOpportunityAdmitted"));
}

void AIronboundCombatAIController::DecisionStep()
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}
	DrawRejectedOpportunity();

	if (!Fighter || !Vitals || !Focus || !Execution || !Techniques)
	{
		CachePawnComponents();
		if (!Fighter || !Vitals || !Focus || !Execution || !Techniques)
		{
			PublishIntent(TEXT("Missing combat setup"));
			return;
		}
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (Now + KINDA_SMALL_NUMBER < NextDecisionWorldTime)
	{
		UpdateMovementRequest();
		return;
	}
	NextDecisionWorldTime = Now + GetEffectiveDecisionInterval();

	if (Vitals->IsDead())
	{
		ClearAttackPlan(TEXT("OwnerDied"), false);
		StopMovement();
		PublishIntent(TEXT("Dead"));
		return;
	}

	AActor* Target = SelectEnemy();
	if (!Target)
	{
		ClearAttackPlan(TEXT("TargetUnavailable"), true);
		CurrentTarget.Reset();
		PendingThreatAttacker.Reset();
		LastAnsweredThreatAttacker.Reset();
		PendingThreatTechnique = NAME_None;
		LastAnsweredThreatTechnique = NAME_None;
		PendingThreatPlanId = 0;
		LastAnsweredThreatPlanId = 0;
		PendingThreatStartWorldTime = 0.f;
		RecentAttackRegions.Reset();
		Focus->SetCombatTarget(nullptr);
		StopMovement();
		PublishIntent(TEXT("Searching"));
		return;
	}

	if (Focus->GetCombatTarget() != Target)
	{
		Focus->SetCombatTarget(Target);
		const FString PawnName = MakeFriendlyActorName(GetPawn());
		const FString TargetName = MakeFriendlyActorName(Target);
		const FString AcquisitionMessage = FString::Printf(
			TEXT("AI %s: I found a target (%s)."), *PawnName, *TargetName);
		UE_LOG(LogIronboundCombat, Log, TEXT("%s"), *AcquisitionMessage);
		if (bShowIntentDebug && GEngine)
		{
			const uint64 DebugKey = 0x1B100000ULL + static_cast<uint64>(GetUniqueID() & 0x00FFFFFFu);
			GEngine->AddOnScreenDebugMessage(
				DebugKey,
				FMath::Max(1.5f, IntentDebugRefreshSeconds),
				FColor::Green,
				AcquisitionMessage);
		}
	}

	// Defense is considered before the next deliberate action, but this policy
	// never interrupts an already committed pawn execution.
	UCombatThreatComponent* Threats = Execution->GetThreats();
	FCombatThreat Threat;
	if (Threats && Threats->GetPrimaryIncomingThreat(Threat))
	{
		if (!Threat.bHasPredictedContact || !IsValid(Threat.Attacker))
		{
			if (PendingThreatAttacker.Get() != Threat.Attacker ||
				PendingThreatPlanId != Threat.AttackerPlanId)
			{
				UE_LOG(LogIronboundCombat, Log,
					TEXT("[AI][PARRY] REFUSED stage=PredictedContact reason=%s attacker=%s plan=%d firstIntersection=%.3fs currentSource=%.3fs"),
					Threat.bHasPredictedContact ? TEXT("attacker invalid") : TEXT("blade already intersected defender envelope"),
					*GetNameSafe(Threat.Attacker), Threat.AttackerPlanId,
					Threat.FirstBodyIntersectionTime, Threat.CurrentSourceTime);
			}
			PendingThreatAttacker = Threat.Attacker;
			PendingThreatTechnique = Threat.TechniqueId;
			PendingThreatPlanId = Threat.AttackerPlanId;
			PendingThreatStartWorldTime = Now;
		}
		else
		{
			const bool bSameThreat = PendingThreatAttacker.Get() == Threat.Attacker &&
				PendingThreatTechnique == Threat.TechniqueId &&
				PendingThreatPlanId == Threat.AttackerPlanId;
			if (!bSameThreat)
			{
				PendingThreatAttacker = Threat.Attacker;
				PendingThreatTechnique = Threat.TechniqueId;
				PendingThreatPlanId = Threat.AttackerPlanId;
				PendingThreatStartWorldTime = Now;
				UE_LOG(LogIronboundCombat, Log,
					TEXT("[AI][PARRY] REACTION_STARTED attacker=%s plan=%d technique=%s tti=%.3fs"),
					*GetNameSafe(Threat.Attacker), Threat.AttackerPlanId,
					*Threat.TechniqueId.ToString(), Threat.TimeToImpact);
			}

		const FName ParryTechnique = FindAvailableTechnique(
			ECombatTechniqueKind::Reactive,
			PreferredReactiveTechnique);
		const bool bAlreadyAnswered = LastAnsweredThreatAttacker.Get() == Threat.Attacker &&
			LastAnsweredThreatTechnique == Threat.TechniqueId &&
			LastAnsweredThreatPlanId == Threat.AttackerPlanId;
		if (ParryTechnique.IsNone())
		{
			FString Cause = TEXT("no reactive technique in battle repertoire");
			const TArray<FName> Repertoire = Fighter->GetBattleRepertoire();
			for (const FName CandidateId : Repertoire)
			{
				const FCombatTechniqueRow* CandidateRow = Techniques->FindRow(CandidateId);
				if (!CandidateRow || CandidateRow->Kind != ECombatTechniqueKind::Reactive)
				{
					continue;
				}
				Cause = Techniques->GetAvailabilityFailureReason(CandidateId);
				break;
			}
			if (!bSameThreat)
			{
				UE_LOG(LogIronboundCombat, Warning,
					TEXT("[AI][PARRY] REFUSED stage=TechniqueAvailability reason=%s repertoireCount=%d attacker=%s plan=%d"),
					*Cause, Repertoire.Num(), *GetNameSafe(Threat.Attacker), Threat.AttackerPlanId);
			}
			PublishIntent(TEXT("Threat observed; no reactive technique"), Target);
		}
		else if (bAlreadyAnswered)
		{
			UE_LOG(LogIronboundCombat, VeryVerbose,
				TEXT("[AI][PARRY] SKIP reason=PlanAlreadyAnswered attacker=%s plan=%d"),
				*GetNameSafe(Threat.Attacker), Threat.AttackerPlanId);
		}
		else if (Execution->IsExecutingReactive())
		{
			UE_LOG(LogIronboundCombat, VeryVerbose,
				TEXT("[AI][PARRY] WAIT reason=ReactiveExecutionAlreadyActive attacker=%s plan=%d"),
				*GetNameSafe(Threat.Attacker), Threat.AttackerPlanId);
		}
		else
		{
			FCombatTechniqueRequest Request;
			Request.TechniqueId = ParryTechnique;
			Request.Target = Threat.Attacker;
			Request.ThreatContext.bHasThreat = true;
			Request.ThreatContext.Threat = Threat;

			const float ConfiguredDelay = GetEffectiveReactionDelay();
			const bool bUrgentThreat = Threat.TimeToImpact <= ConfiguredDelay + 0.12f;
			const float RequiredDelay = bUrgentThreat ? 0.f : ConfiguredDelay;
			const float Elapsed = Now - PendingThreatStartWorldTime;
			if (Elapsed + KINDA_SMALL_NUMBER < RequiredDelay)
			{
				UE_LOG(LogIronboundCombat, VeryVerbose,
					TEXT("[AI][PARRY] WAIT stage=ReactionTiming attacker=%s plan=%d elapsed=%.3fs required=%.3fs tti=%.3fs urgent=%d"),
					*GetNameSafe(Threat.Attacker), Threat.AttackerPlanId,
					Elapsed, RequiredDelay, Threat.TimeToImpact, bUrgentThreat ? 1 : 0);
			}
			else if (!Execution->HasAdmissionSlot(ECombatExecutionKind::Reactive))
			{
				UE_LOG(LogIronboundCombat, Warning,
					TEXT("[AI][PARRY] REFUSED stage=ExecutionAdmission reason=no reactive admission slot attacker=%s plan=%d"),
					*GetNameSafe(Threat.Attacker), Threat.AttackerPlanId);
			}
			else if (!Execution->CanExecuteTechnique(Request))
			{
				UE_LOG(LogIronboundCombat, Warning,
					TEXT("[AI][PARRY] REFUSED stage=ExecutionAdmission reason=%s technique=%s attacker=%s plan=%d"),
					*Execution->GetTechniqueRejectionReason(Request),
					*ParryTechnique.ToString(), *GetNameSafe(Threat.Attacker),
					Threat.AttackerPlanId);
			}
			else if (Execution->RequestTechnique(Request))
			{
				LastAnsweredThreatAttacker = Threat.Attacker;
				LastAnsweredThreatTechnique = Threat.TechniqueId;
				LastAnsweredThreatPlanId = Threat.AttackerPlanId;
				UE_LOG(LogIronboundCombat, Log,
					TEXT("[AI][PARRY] REQUEST_ACCEPTED technique=%s attacker=%s plan=%d sourceTechnique=%s tti=%.3fs"),
					*ParryTechnique.ToString(), *GetNameSafe(Threat.Attacker),
					Threat.AttackerPlanId, *Threat.TechniqueId.ToString(), Threat.TimeToImpact);
				const FCombatTechniqueRow* ParryRow = Techniques->FindRow(ParryTechnique);
				PublishIntent(ParryRow ? *FString::Printf(TEXT("Parry: %s"), *ParryRow->DisplayName.ToString()) : TEXT("Parry"), Target);
			}
			else
			{
				UE_LOG(LogIronboundCombat, Warning,
					TEXT("[AI][PARRY] REFUSED stage=ReactiveExecutorInitialization reason=executor rejected request technique=%s attacker=%s plan=%d"),
					*ParryTechnique.ToString(), *GetNameSafe(Threat.Attacker), Threat.AttackerPlanId);
			}
		}
		}
	}
	else
	{
		PendingThreatAttacker.Reset();
		LastAnsweredThreatAttacker.Reset();
		PendingThreatTechnique = NAME_None;
		LastAnsweredThreatTechnique = NAME_None;
		PendingThreatPlanId = 0;
		LastAnsweredThreatPlanId = 0;
		PendingThreatStartWorldTime = 0.f;
	}

	const bool bHasActiveDeliberate = !Execution->HasAdmissionSlot(ECombatExecutionKind::Deliberate);
	if (ActivePlanId != 0 && PlannedTarget.Get() != Target && !Execution->IsCommitted())
	{
		ClearAttackPlan(TEXT("TargetChanged"), true);
	}
	if (ActivePlanId != 0 && bHasActiveDeliberate && !Execution->IsCommitted() &&
		FVector::Dist2D(Target->GetActorLocation(), ActiveOpportunity.TargetLocationAtQuery) >
			TargetDisplacementToleranceCm)
	{
		ClearAttackPlan(TEXT("TargetMovedDuringPreparation"), true);
		ConsecutiveFailedPlans = 0;
		NextDeliberateAttemptWorldTime = Now + 0.1f;
	}
	if (ActivePlanId != 0 && bHasActiveDeliberate && Execution->IsCommitted() && !bPlanEverCommitted)
	{
		bPlanEverCommitted = true;
		SetMovementMode(EIronboundAIMovementMode::CommittedAttack, TEXT("StrikeCommitted"));
		UE_LOG(LogIronboundCombat, Log, TEXT("[AI] EXECUTION PlanId=%d Result=Committed"), ActivePlanId);
	}
	if (ActivePlanId != 0 && bHadActiveDeliberate && !bHasActiveDeliberate)
	{
		const int32 FinishedPlanId = ActivePlanId;
		const bool bRecovered = bPlanEverCommitted;
		const bool bTerminatedBeforeCommit = !bRecovered;
		ClearAttackPlan(bRecovered ? TEXT("AttackRecovered") : TEXT("ExecutionTerminatedBeforeCommit"), false);
		if (bTerminatedBeforeCommit)
		{
			// A selected stance can go stale as the target's animated pose changes
			// while navigation is in progress. Retry from the current pose instead
			// of suspending planning because neither actor root moved.
			ConsecutiveFailedPlans = 0;
			NextDeliberateAttemptWorldTime = Now + 0.1f;
			UE_LOG(LogIronboundCombat, Log,
				TEXT("[AI] PLANNING PlanId=%d Result=RetryAfterPreCommitInvalidation"), FinishedPlanId);
		}
		else
		{
			NextDeliberateAttemptWorldTime = Now;
		}
	}
	if (!bHasActiveDeliberate && ActivePlanId == 0)
	{
		const float Distance = FVector::Dist2D(ControlledPawn->GetActorLocation(), Target->GetActorLocation());
		const float DecisionRange = bClosingForOpportunity
			? FMath::Min(CloseApproachDistanceCm, PlanningRangeCm) : PlanningRangeCm;
		if (Distance > DecisionRange)
		{
			SetMovementMode(EIronboundAIMovementMode::Pursuit,
				bClosingForOpportunity ? TEXT("ClosingForOpportunity") : TEXT("TargetOutsidePlanningRange"));
		}
		else if (Now >= NextDeliberateAttemptWorldTime)
		{
			// A crouched attacker has a lower actor origin and a different
			// mesh pose. Sample the sword arc only after returning to the
			// standing geometry used by the strike montage.
			if (MovementMode == EIronboundAIMovementMode::GuardManeuver &&
				!FindAvailableTechnique(ECombatTechniqueKind::Deliberate,
					PreferredDeliberateTechnique).IsNone())
			{
				SetMovementMode(EIronboundAIMovementMode::Idle, TEXT("StandForStrikePlanning"));
				UpdateMovementRequest();
				return;
			}
			if (const UCharacterMoverComponent* Mover =
				ControlledPawn->FindComponentByClass<UCharacterMoverComponent>())
			{
				if (Mover->IsCrouching() || Now < NextStandingPlanWorldTime)
				{
					UpdateMovementRequest();
					return;
				}
			}
			if (ConsecutiveFailedPlans >= 3)
			{
				if (FVector::Dist2D(Target->GetActorLocation(), FailedPlanTargetLocation) <=
					TargetDisplacementToleranceCm &&
					FVector::Dist2D(ControlledPawn->GetActorLocation(), FailedPlanAttackerLocation) <=
					TargetDisplacementToleranceCm)
				{
					UpdateMovementRequest();
					return;
				}
				UE_LOG(LogIronboundCombat, Log,
					TEXT("[AI] PLANNING RESUMED Reason=FighterOrTargetDisplacedAfterRepeatedFailure"));
				ConsecutiveFailedPlans = 0;
				ConsecutivePathFailures = 0;
			}
			if (MovementMode == EIronboundAIMovementMode::Pursuit)
			{
				StopMovement();
				bHasMoveGoal = false;
				bMoveRequestActive = false;
				SetMovementMode(EIronboundAIMovementMode::Idle, TEXT("EnteredPlanningRange"));
			}
			bClosingForOpportunity = false;
			ChooseAttackPlan(Target, Now);
		}
		if (ActivePlanId == 0 && !bClosingForOpportunity &&
			MovementMode == EIronboundAIMovementMode::Idle)
		{
			SetMovementMode(EIronboundAIMovementMode::GuardManeuver,
				TEXT("HoldingCombatRange"));
		}
	}

	UpdateMovementRequest();

	if (Execution->IsAligning())
	{
		SetMovementMode(EIronboundAIMovementMode::AttackAlignment, TEXT("ArrivedAtSelectedStance"));
		PublishIntent(TEXT("Aligning attack"), Target);
	}
	else if (!Execution->HasAdmissionSlot(ECombatExecutionKind::Deliberate))
	{
		PublishIntent(Execution->IsCommitted() ? TEXT("Executing attack") : TEXT("Preparing attack"), Target);
	}
	else if (Execution->IsExecutingReactive())
	{
		PublishIntent(TEXT("Holding parry"), Target);
	}
	else if (bClosingForOpportunity)
	{
		PublishIntent(TEXT("Closing for attack opportunity"), Target);
	}
	else if (FindAvailableTechnique(ECombatTechniqueKind::Deliberate, PreferredDeliberateTechnique).IsNone())
	{
		PublishIntent(TEXT("Engaged; no attack selected"), Target);
	}
	else if (CurrentTarget.IsValid())
	{
		PublishIntent(TEXT("Engaging"), Target);
	}
}

void AIronboundCombatAIController::UpdateMovementRequest()
{
	if (!Execution || !GetPawn())
	{
		return;
	}
	const FCombatEngagementRequirement Requirement = Execution->GetEngagementRequirement();
	if (ActivePlanId != 0 && !Execution->IsCommitted() &&
		MovementMode == EIronboundAIMovementMode::AttackAlignment &&
		Requirement.bHasRequirement && Requirement.bMayMoveDuringExecution &&
		FVector::DistSquared2D(GetPawn()->GetActorLocation(), Requirement.DesiredLocation) >
			FMath::Square(FMath::Max(1.f, Requirement.ArrivalTolerance)))
	{
		SetMovementMode(EIronboundAIMovementMode::GuardManeuver,
			TEXT("LeftStanceBeforeCommit"));
	}
	const bool bPursuit = MovementMode == EIronboundAIMovementMode::Pursuit && CurrentTarget.IsValid();
	const bool bGuardMove = ActivePlanId != 0 &&
		MovementMode == EIronboundAIMovementMode::GuardManeuver &&
		Requirement.bHasRequirement && Requirement.bMayMoveDuringExecution;
	if (!bPursuit && !bGuardMove)
	{
		if (GetMoveStatus() != EPathFollowingStatus::Idle)
		{
			StopMovement();
		}
		bHasMoveGoal = false;
		bMoveRequestActive = false;
		return;
	}
	const FVector Goal = bPursuit ? CurrentTarget->GetActorLocation() : Requirement.DesiredLocation;
	const float AcceptanceRadius = bPursuit
		? (bClosingForOpportunity ? FMath::Min(CloseApproachDistanceCm, PlanningRangeCm)
			: PlanningRangeCm) * 0.75f :
		FMath::Max(1.f, Requirement.ArrivalTolerance);
	if (FVector::DistSquared2D(GetPawn()->GetActorLocation(), Goal) <=
		FMath::Square(AcceptanceRadius))
	{
		// Crouch lowers the Mover actor origin by about 26 cm. The stance is
		// reached in the ground plane; stand before the executor checks its
		// full 3D transform and strike geometry.
		if (bGuardMove)
		{
			SetMovementMode(EIronboundAIMovementMode::AttackAlignment,
				TEXT("ReachedStanceGroundPosition"));
		}
		if (GetMoveStatus() != EPathFollowingStatus::Idle)
		{
			StopMovement();
		}
		LastMoveGoal = Goal;
		bHasMoveGoal = true;
		bMoveRequestActive = false;
		return;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (bMoveRequestActive && GetMoveStatus() == EPathFollowingStatus::Idle)
	{
		bMoveRequestActive = false;
		++ConsecutivePathFailures;
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("[AI] NAVIGATION PlanId=%d Result=PathEndedBeforeArrival failures=%d goal=%s"),
			bPursuit ? PursuitIntentId : ActivePlanId, ConsecutivePathFailures, *Goal.ToCompactString());
		if (bGuardMove)
		{
			ClearAttackPlan(TEXT("StanceNavigationFailed"), true);
			NextDeliberateAttemptWorldTime = Now + DeliberateRetryIntervalSeconds *
				FMath::Min(3, ConsecutivePathFailures + 1);
			return;
		}
		if (bPursuit)
		{
			SuspendFailedClosePursuit();
			if (MovementMode != EIronboundAIMovementMode::Pursuit) return;
		}
	}
	const bool bGoalChanged = !bHasMoveGoal ||
		FVector::DistSquared(LastMoveGoal, Goal) >
		FMath::Square(MovementGoalUpdateDistanceCm);
	const bool bRetryReady = GetMoveStatus() == EPathFollowingStatus::Idle &&
		Now >= NextPathRequestWorldTime;
	if (bGoalChanged || bRetryReady)
	{
		LastMoveGoal = Goal;
		bHasMoveGoal = true;
		NextPathRequestWorldTime = Now + FMath::Max(0.f, FailedPathRetryDelaySeconds);
		const EPathFollowingRequestResult::Type MoveResult = MoveToLocation(
			Goal,
			AcceptanceRadius,
			false,  // Do not inflate the executor's authored root arrival tolerance.
			true,   // Use Navigation System paths; NavMover feeds the path into Mover.
			true,
			bGuardMove, // Guard keeps target focus; pursuit follows travel direction.
			nullptr,
			false); // Partial paths do not satisfy a combat stance requirement.
		if (MoveResult == EPathFollowingRequestResult::Failed)
		{
			++ConsecutivePathFailures;
			UE_LOG(LogIronboundCombat, Warning,
				TEXT("[AI] NAVIGATION PlanId=%d Result=RequestFailed mode=%s goal=%s failures=%d"),
				bPursuit ? PursuitIntentId : ActivePlanId, bGuardMove ? TEXT("GuardManeuver") : TEXT("Pursuit"),
				*Goal.ToCompactString(), ConsecutivePathFailures);
			if (bGuardMove)
			{
				ClearAttackPlan(TEXT("StanceUnreachable"), true);
				NextDeliberateAttemptWorldTime = Now + DeliberateRetryIntervalSeconds *
					FMath::Min(3, ConsecutivePathFailures + 1);
			}
			else if (bPursuit)
			{
				SuspendFailedClosePursuit();
			}
		}
		else
		{
			bMoveRequestActive = MoveResult == EPathFollowingRequestResult::RequestSuccessful;
			if (bGoalChanged)
			{
			UE_LOG(LogIronboundCombat, Log,
				TEXT("[AI] NAVIGATION PlanId=%d Mode=%s goal=%s arrival=%.1fcm result=%d"),
				bPursuit ? PursuitIntentId : ActivePlanId, bGuardMove ? TEXT("GuardTargetFacingStrafe") : TEXT("PursuitFacingTravel"),
				*Goal.ToCompactString(), AcceptanceRadius, int32(MoveResult));
			}
		}
	}
}

void AIronboundCombatAIController::PublishIntent(const TCHAR* Intent, const AActor* Target)
{
	const FString PawnName = MakeFriendlyActorName(GetPawn());
	const FString TargetName = MakeFriendlyActorName(Target);
	const FString RawIntent = Intent ? FString(Intent) : FString(TEXT("thinking"));
	const FString TargetPart = ActiveOpportunity.bFeasible && PlannedTarget.Get() == Target
		? MakeFriendlyTargetPart(ActiveOpportunity.Region, ActiveOpportunity.Bone)
		: FString(TEXT("body"));
	const FString TargetPartPhrase = FString::Printf(TEXT("%s's %s"), *TargetName, *TargetPart);

	FString TechniqueName = TEXT("sword");
	if (Execution && Techniques)
	{
		for (const FName TechniqueId : Execution->GetActiveTechniqueIds())
		{
			if (const FCombatTechniqueRow* Row = Techniques->FindRow(TechniqueId);
				Row && !Row->DisplayName.IsEmpty())
			{
				TechniqueName = Row->DisplayName.ToString();
				break;
			}
		}
	}

	FString Sentence;
	if (RawIntent.StartsWith(TEXT("Parry: "), ESearchCase::IgnoreCase))
	{
		Sentence = FString::Printf(TEXT("I am parrying %s with %s."), *TargetName, *RawIntent.RightChop(7));
	}
	else if (RawIntent == TEXT("Starting")) Sentence = TEXT("I am ready to fight.");
	else if (RawIntent == TEXT("Missing combat setup")) Sentence = TEXT("I cannot fight because my combat setup is incomplete.");
	else if (RawIntent == TEXT("Dead")) Sentence = TEXT("I am down and cannot fight.");
	else if (RawIntent == TEXT("Searching")) Sentence = TEXT("I am looking for an enemy.");
	else if (RawIntent == TEXT("Threat observed; parry unavailable")) Sentence = FString::Printf(TEXT("I see an incoming attack from %s, but I cannot parry it."), *TargetName);
	else if (RawIntent == TEXT("Threat observed; reacting")) Sentence = FString::Printf(TEXT("I see an incoming attack from %s and am reacting."), *TargetName);
	else if (RawIntent == TEXT("Aligning attack")) Sentence = FString::Printf(TEXT("I am setting my stance to strike %s."), *TargetPartPhrase);
	else if (RawIntent == TEXT("Preparing attack")) Sentence = FString::Printf(TEXT("I am planning to use %s against %s."), *TechniqueName, *TargetPartPhrase);
	else if (RawIntent == TEXT("Executing attack")) Sentence = FString::Printf(TEXT("I am striking %s with %s."), *TargetPartPhrase, *TechniqueName);
	else if (RawIntent == TEXT("Holding parry")) Sentence = FString::Printf(TEXT("I am holding my guard against %s."), *TargetName);
	else if (RawIntent == TEXT("Closing for attack opportunity")) Sentence = FString::Printf(TEXT("I am moving into a viable attack position against %s."), *TargetName);
	else if (RawIntent == TEXT("Engaged; no attack selected")) Sentence = FString::Printf(TEXT("I found %s, but no deliberate attack is available."), *TargetName);
	else if (RawIntent == TEXT("Engaging")) Sentence = FString::Printf(TEXT("I found %s and am choosing an attack."), *TargetName);
	else if (RawIntent == TEXT("Parry")) Sentence = FString::Printf(TEXT("I am parrying %s."), *TargetName);
	else
	{
		FString ReadableIntent = RawIntent;
		ReadableIntent.ReplaceInline(TEXT("_"), TEXT(" "));
		ReadableIntent.ToLowerInline();
		Sentence = FString::Printf(TEXT("I am %s."), *ReadableIntent);
	}
	const FString Message = FString::Printf(TEXT("AI %s: %s"), *PawnName, *Sentence);

	if (LastIntent != Message)
	{
		LastIntent = Message;
		UE_LOG(LogIronboundCombat, Log, TEXT("%s"), *Message);
	}

	if (bShowIntentDebug && GEngine)
	{
		const uint64 DebugKey = 0x1B000000ULL + static_cast<uint64>(GetUniqueID() & 0x00FFFFFFu);
		GEngine->AddOnScreenDebugMessage(
			DebugKey,
			FMath::Max(0.1f, IntentDebugRefreshSeconds),
			FColor::Cyan,
			Message);
	}
}
