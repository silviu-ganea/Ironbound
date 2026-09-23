#include "Combat/IronboundCombatAIController.h"

#include "Combat/BattleManager.h"
#include "Combat/CombatExecutionComponent.h"
#include "Combat/CombatExecutionTypes.h"
#include "Combat/CombatEquipmentComponent.h"
#include "Combat/CombatTechniqueExecutionConfigs.h"
#include "Combat/CombatFocusComponent.h"
#include "Combat/CombatTechniqueComponent.h"
#include "Combat/CombatTechniqueRow.h"
#include "Combat/CombatThreatComponent.h"
#include "Combat/CombatThreatTypes.h"
#include "Combat/FighterComponent.h"
#include "Combat/FighterVitalsComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Navigation/PathFollowingComponent.h"
#include "TimerManager.h"
#include "Ironbound.h"

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
	bHasMoveGoal = false;
	bMoveRequestActive = false;
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
	if (MovementMode == EIronboundAIMovementMode::GuardManeuver && PlannedTarget.IsValid() && GetPawn())
	{
		FVector Facing = (PlannedTarget->GetActorLocation() - GetPawn()->GetActorLocation()).GetSafeNormal2D();
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
	if (NewMode == EIronboundAIMovementMode::GuardManeuver && PlannedTarget.IsValid())
	{
		SetFocus(PlannedTarget.Get());
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
	FCombatAttackOpportunity Current, Nearby;
	UCombatTrajectoryLibrary::FindAttackOpportunities(AttackerMesh, TargetMesh, Trajectory,
		Config->CombatTargets, TechniqueId, RequiredRegion, Config->AimPointAlongBlade,
		Config->AimWindowStartFraction, Config->AimWindowEndFraction,
		Config->MaxFacingDeviationFromTargetDegrees, Config->ContactToleranceCm,
		MaxNearbyMoveCm, Current, Nearby);
	UE_LOG(LogIronboundCombat, Log,
		TEXT("[AI] OPPORTUNITIES PlanId=%d Current feasible=%d region=%s bone=%s quality=%.1f miss=%.1fcm; Nearby feasible=%d region=%s bone=%s quality=%.1f miss=%.1fcm move=%.1fcm stance=%s"),
		DecisionId, Current.bFeasible ? 1 : 0, *Current.Region.ToString(), *Current.Bone.ToString(),
		Current.Quality, Current.MissCm, Nearby.bFeasible ? 1 : 0,
		*Nearby.Region.ToString(), *Nearby.Bone.ToString(), Nearby.Quality, Nearby.MissCm,
		Nearby.MovementCostCm, *Nearby.Stance.GetLocation().ToCompactString());
	const float Improvement = Nearby.Quality - Nearby.MovementCostCm * 0.08f - Current.Quality;
	const bool bNearbyUseful = Nearby.bFeasible &&
		(!Current.bFeasible || Improvement >= MeaningfulQualityGain);
	const float Roll = Current.bFeasible && bNearbyUseful ? FMath::FRand() : -1.f;
	const bool bReposition = bNearbyUseful && (!Current.bFeasible || Roll < RepositionPreference);
	UE_LOG(LogIronboundCombat, Log,
		TEXT("[AI] POLICY PlanId=%d RepositionPreference=%.2f Roll=%.3f ImprovementAfterMove=%.1f"),
		DecisionId, RepositionPreference, Roll, Improvement);
	if (!Current.bFeasible && !Nearby.bFeasible)
	{
		RejectedTrajectory = Trajectory;
		RejectedOpportunity = Nearby.ContactSample != INDEX_NONE ? Nearby : Current;
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
	bHasRejectedOpportunity = false;
	bClosingForOpportunity = false;
	ActiveOpportunity = bReposition ? Nearby : Current;
	ActivePlanId = DecisionId;
	PlannedTarget = Target;
	UE_LOG(LogIronboundCombat, Log,
		TEXT("[AI] CHOICE PlanId=%d Intent=%s Reason=%s region=%s bone=%s quality=%.1f move=%.1fcm destination=%s yaw=%.1f"),
		ActivePlanId, bReposition ? TEXT("RepositionForAttack") : TEXT("AttackFromCurrentPosition"),
		bReposition ? (Current.bFeasible ? TEXT("UsefulImprovementWonPolicyRoll") : TEXT("CurrentCannotHitNearbyCan"))
			: (bNearbyUseful ? TEXT("CurrentAttackWonPolicyRoll") : TEXT("CurrentAttackGoodRepositionNotWorthCost")),
		*ActiveOpportunity.Region.ToString(), *ActiveOpportunity.Bone.ToString(),
		ActiveOpportunity.Quality, ActiveOpportunity.MovementCostCm,
		*ActiveOpportunity.Stance.GetLocation().ToCompactString(), ActiveOpportunity.Stance.Rotator().Yaw);
	FCombatTechniqueRequest Request;
	Request.TechniqueId = TechniqueId;
	Request.Target = Target;
	Request.TargetRegion = RequiredRegion;
	Request.PlannedOpportunity = ActiveOpportunity;
	Request.PlanId = ActivePlanId;
	if (!Execution->RequestTechnique(Request))
	{
		ClearAttackPlan(TEXT("ExecutionRefusedSelectedOpportunity"), false);
		return;
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
		Focus->SetCombatTarget(nullptr);
		StopMovement();
		PublishIntent(TEXT("Searching"));
		return;
	}

	if (Focus->GetCombatTarget() != Target)
	{
		Focus->SetCombatTarget(Target);
	}

	// Defense is considered before the next deliberate action, but this policy
	// never interrupts an already committed pawn execution.
	UCombatThreatComponent* Threats = Execution->GetThreats();
	FCombatThreat Threat;
	if (Threats && Threats->GetPrimaryIncomingThreat(Threat) &&
		Threat.bHasPredictedContact && IsValid(Threat.Attacker))
	{
		const bool bSameThreat = PendingThreatAttacker.Get() == Threat.Attacker &&
			PendingThreatTechnique == Threat.TechniqueId;
		if (!bSameThreat)
		{
			PendingThreatAttacker = Threat.Attacker;
			PendingThreatTechnique = Threat.TechniqueId;
			PendingThreatStartWorldTime = Now;
		}

		const FName ParryTechnique = FindAvailableTechnique(
			ECombatTechniqueKind::Reactive,
			PreferredReactiveTechnique);
		const bool bAlreadyAnswered = LastAnsweredThreatAttacker.Get() == Threat.Attacker &&
			LastAnsweredThreatTechnique == Threat.TechniqueId;
		if (!bAlreadyAnswered && !ParryTechnique.IsNone() &&
			Now - PendingThreatStartWorldTime >= GetEffectiveReactionDelay() &&
			!Execution->IsExecutingReactive() &&
			Execution->HasAdmissionSlot(ECombatExecutionKind::Reactive))
		{
			FCombatTechniqueRequest Request;
			Request.TechniqueId = ParryTechnique;
			Request.Target = Threat.Attacker;
			Request.ThreatContext.bHasThreat = true;
			Request.ThreatContext.Threat = Threat;

			if (Execution->CanExecuteTechnique(Request) && Execution->RequestTechnique(Request))
			{
				LastAnsweredThreatAttacker = Threat.Attacker;
				LastAnsweredThreatTechnique = Threat.TechniqueId;
				const FCombatTechniqueRow* Row = Techniques->FindRow(ParryTechnique);
				PublishIntent(Row ? *FString::Printf(TEXT("Parry: %s"), *Row->DisplayName.ToString()) : TEXT("Parry"), Target);
			}
			else
			{
				PublishIntent(TEXT("Threat observed; parry unavailable"), Target);
			}
		}
		else if (!Execution->IsExecutingReactive())
		{
			PublishIntent(TEXT("Threat observed; reacting"), Target);
		}
	}
	else
	{
		PendingThreatAttacker.Reset();
		LastAnsweredThreatAttacker.Reset();
		PendingThreatTechnique = NAME_None;
		LastAnsweredThreatTechnique = NAME_None;
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
		ClearAttackPlan(TEXT("TargetDisplacedBeyondPlanTolerance"), true);
	}
	if (ActivePlanId != 0 && bHasActiveDeliberate && Execution->IsCommitted() && !bPlanEverCommitted)
	{
		bPlanEverCommitted = true;
		SetMovementMode(EIronboundAIMovementMode::CommittedAttack, TEXT("StrikeCommitted"));
		UE_LOG(LogIronboundCombat, Log, TEXT("[AI] EXECUTION PlanId=%d Result=Committed"), ActivePlanId);
	}
	if (ActivePlanId != 0 && bHadActiveDeliberate && !bHasActiveDeliberate)
	{
		const bool bRecovered = bPlanEverCommitted;
		ClearAttackPlan(bRecovered ? TEXT("AttackRecovered") : TEXT("ExecutionTerminatedBeforeCommit"), false);
		NextDeliberateAttemptWorldTime = bRecovered ? Now : Now + DeliberateRetryIntervalSeconds;
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
		Requirement.bHasRequirement && Requirement.bMayMoveDuringExecution)
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
	const FString PawnName = GetNameSafe(GetPawn());
	const FString TargetName = GetNameSafe(Target);
	const FString Message = Target
		? FString::Printf(TEXT("AI %s: %s -> %s"), *PawnName, Intent, *TargetName)
		: FString::Printf(TEXT("AI %s: %s"), *PawnName, Intent);

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
