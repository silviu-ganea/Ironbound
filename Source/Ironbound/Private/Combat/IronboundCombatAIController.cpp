#include "Combat/IronboundCombatAIController.h"

#include "Combat/BattleManager.h"
#include "Combat/CombatExecutionComponent.h"
#include "Combat/CombatExecutionTypes.h"
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

void AIronboundCombatAIController::DecisionStep()
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

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
		StopMovement();
		PublishIntent(TEXT("Dead"));
		return;
	}

	AActor* Target = SelectEnemy();
	if (!Target)
	{
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

	const bool bHasActiveDeliberate =
		!Execution->HasAdmissionSlot(ECombatExecutionKind::Deliberate);
	if (!bHasActiveDeliberate && Now >= NextDeliberateAttemptWorldTime)
	{
		const FName AttackTechnique = FindAvailableTechnique(
			ECombatTechniqueKind::Deliberate,
			PreferredDeliberateTechnique);
		if (!AttackTechnique.IsNone())
		{
			FCombatTechniqueRequest Request;
			Request.TechniqueId = AttackTechnique;
			Request.Target = Target;

			NextDeliberateAttemptWorldTime = Now + FMath::Max(0.05f, DeliberateRetryIntervalSeconds);
			if (Execution->CanExecuteTechnique(Request) && Execution->RequestTechnique(Request))
			{
				const FCombatTechniqueRow* Row = Techniques->FindRow(AttackTechnique);
				PublishIntent(Row ? *FString::Printf(TEXT("Attack: %s"), *Row->DisplayName.ToString()) : TEXT("Attack"), Target);
			}
		}
	}

	UpdateMovementRequest();

	if (Execution->IsAligning())
	{
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
	if (!Execution)
	{
		return;
	}

	FCombatEngagementRequirement Requirement = Execution->GetEngagementRequirement();
	if (!Requirement.bHasRequirement || !Requirement.bMayMoveDuringExecution)
	{
		if (GetMoveStatus() != EPathFollowingStatus::Idle)
		{
			StopMovement();
		}
		bHasMoveGoal = false;
		return;
	}

	const float AcceptanceRadius = FMath::Max(1.f, Requirement.ArrivalTolerance);
	if (FVector::DistSquared2D(GetPawn()->GetActorLocation(), Requirement.DesiredLocation) <=
		FMath::Square(AcceptanceRadius))
	{
		if (GetMoveStatus() != EPathFollowingStatus::Idle)
		{
			StopMovement();
		}
		LastMoveGoal = Requirement.DesiredLocation;
		bHasMoveGoal = true;
		return;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const bool bGoalChanged = !bHasMoveGoal ||
		FVector::DistSquared(LastMoveGoal, Requirement.DesiredLocation) >
		FMath::Square(MovementGoalUpdateDistanceCm);
	const bool bRetryReady = GetMoveStatus() == EPathFollowingStatus::Idle &&
		Now >= NextPathRequestWorldTime;
	if (bGoalChanged || bRetryReady)
	{
		LastMoveGoal = Requirement.DesiredLocation;
		bHasMoveGoal = true;
		NextPathRequestWorldTime = Now + FMath::Max(0.f, FailedPathRetryDelaySeconds);
		const EPathFollowingRequestResult::Type MoveResult = MoveToLocation(
			Requirement.DesiredLocation,
			AcceptanceRadius,
			false,  // Do not inflate the executor's authored root arrival tolerance.
			true,   // Use Navigation System paths; NavMover feeds the path into Mover.
			true,
			true,   // Strafe is handled by movement orientation intent, not actor rotation writes.
			nullptr,
			false); // Partial paths do not satisfy a combat stance requirement.
		if (MoveResult == EPathFollowingRequestResult::Failed)
		{
			UE_LOG(LogIronboundCombat, Warning,
				TEXT("Combat AI [%s]: navigation could not reach the requested combat stance"),
				*GetNameSafe(GetPawn()));
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
