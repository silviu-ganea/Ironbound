#include "Combat/CombatExecutionComponent.h"

#include "Combat/CombatFocusComponent.h"
#include "Combat/CombatTechniqueComponent.h"
#include "Combat/CombatTechniqueExecutor.h"
#include "Combat/CombatTechniqueExecutionConfigs.h"
#include "Combat/CombatThreatComponent.h"
#include "Combat/FighterVitalsComponent.h"
#include "Combat/FighterComponent.h"
#include "Combat/CombatReactionComponent.h"
#include "Combat/CombatInteractionLibrary.h"
#include "Combat/CombatEquipmentComponent.h"
#include "Combat/CombatTechniqueRow.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "Ironbound.h"

namespace
{
	ECombatExecutionKind ExecutionKind(ECombatTechniqueKind Kind)
	{
		return Kind == ECombatTechniqueKind::Reactive
			? ECombatExecutionKind::Reactive : ECombatExecutionKind::Deliberate;
	}

	bool IsSameRequirement(
		const FCombatEngagementRequirement& A,
		const FCombatEngagementRequirement& B)
	{
		return A.bHasRequirement == B.bHasRequirement &&
			   A.DesiredLocation.Equals(B.DesiredLocation) &&
			   A.DesiredFacing.Equals(B.DesiredFacing) &&
			   FMath::IsNearlyEqual(A.ArrivalTolerance, B.ArrivalTolerance) &&
			   FMath::IsNearlyEqual(A.FacingTolerance, B.FacingTolerance) &&
			   A.bMayMoveDuringExecution == B.bMayMoveDuringExecution;
	}
}

UCombatExecutionComponent::UCombatExecutionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	/*
	 * Pre-physics: reactive executors drive animation hand targets that the
	 * AnimInstance consumes in the same frame. All measurements used by the
	 * executors (root speed, facing) are actor-level and group-independent.
	 */
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UCombatExecutionComponent::BeginPlay()
{
	Super::BeginPlay();

	// The pawn combat domain owns these two components; create them when the
	// pawn Blueprint has not added them explicitly.
	GetTechniques();
	GetThreats();

	if (UFighterVitalsComponent* Vitals = GetOwner()->FindComponentByClass<UFighterVitalsComponent>())
	{
		Vitals->OnDeath.AddDynamic(this, &UCombatExecutionComponent::HandleOwnerDeath);
	}
}

void UCombatExecutionComponent::HandleOwnerDeath()
{
	CancelAllExecutions();
}

// ============================================================================
// Submission boundary
// ============================================================================

bool UCombatExecutionComponent::RequestTechnique(
	const FCombatTechniqueRequest& Request)
{
	const FCombatTechniqueRow* Row = nullptr;
	FText Reason;

	if (!ValidateRequest(Request, Row, Reason))
	{
		UE_LOG(
			LogIronboundCombat,
			Log,
			TEXT("Technique request refused [%s | %s]: %s"),
			*GetNameSafe(GetOwner()),
			*Request.TechniqueId.ToString(),
			*Reason.ToString());

		return false;
	}

	if (!HasAdmissionSlot(ExecutionKind(Row->Kind)))
	{
		UE_LOG(
			LogIronboundCombat,
			Log,
			TEXT("Technique request refused [%s | %s]: no admission slot"),
			*GetNameSafe(GetOwner()),
			*Request.TechniqueId.ToString());

		return false;
	}

	const UCombatTechniqueExecutionConfig* Config = Row->ExecutionConfig;
	if (!Config)
	{
		UE_LOG(
			LogIronboundCombat,
			Warning,
			TEXT("Technique request refused [%s | %s]: row has no execution config"),
			*GetNameSafe(GetOwner()),
			*Request.TechniqueId.ToString());

		return false;
	}

	TSubclassOf<UCombatTechniqueExecutor> ExecutorClass = Config->ExecutorClass;
	if (!*ExecutorClass)
	{
		UE_LOG(
			LogIronboundCombat,
			Warning,
			TEXT("Technique request refused [%s | %s]: execution config has no executor class"),
			*GetNameSafe(GetOwner()),
			*Request.TechniqueId.ToString());

		return false;
	}

	FCombatExecutionRecord& Record = Executions.AddDefaulted_GetRef();
	Record.RecordId = NextRecordId++;
	Record.PlanId = Request.PlanId;
	Record.TechniqueId = Request.TechniqueId;
	Record.Kind = ExecutionKind(Row->Kind);
	Record.BodyScope = Row->BodyScope;
	Record.Target = Request.Target;
	Record.State = ECombatExecutionState::Preparing;
	Record.Executor = NewObject<UCombatTechniqueExecutor>(this, ExecutorClass);

	if (!Record.Executor->InitializeExecution(
			this,
			Record.RecordId,
			*Row,
			Config,
			Request))
	{
		Executions.RemoveAt(Executions.Num() - 1);

		UE_LOG(
			LogIronboundCombat,
			Log,
			TEXT("Technique request refused [%s | %s]: executor rejected the execution"),
			*GetNameSafe(GetOwner()),
			*Request.TechniqueId.ToString());

		return false;
	}

	UE_LOG(
		LogIronboundCombat,
		Log,
		TEXT("Execution admitted [%s | %s | %s] record %d"),
		*GetNameSafe(GetOwner()),
		*Request.TechniqueId.ToString(),
		Row->Kind == ECombatTechniqueKind::Reactive ? TEXT("Reactive") : TEXT("Deliberate"),
		Record.RecordId);

	BroadcastRequirement();

	return true;
}

bool UCombatExecutionComponent::CanExecuteTechnique(
	const FCombatTechniqueRequest& Request) const
{
	const FCombatTechniqueRow* Row = nullptr;
	FText Reason;
	return ValidateRequest(Request, Row, Reason) &&
		   HasAdmissionSlot(ExecutionKind(Row->Kind));
}

bool UCombatExecutionComponent::ValidateRequest(
	const FCombatTechniqueRequest& Request,
	const FCombatTechniqueRow*& OutRow,
	FText& OutReason) const
{
	OutRow = nullptr;

	if (Request.TechniqueId.IsNone())
	{
		OutReason = FText::FromString(TEXT("no technique id"));
		return false;
	}

	UCombatTechniqueComponent* Techniques =
		GetOwner() ? GetOwner()->FindComponentByClass<UCombatTechniqueComponent>() : nullptr;

	if (!Techniques || !Techniques->IsConfigured())
	{
		OutReason = FText::FromString(TEXT("technique registry unavailable"));
		return false;
	}

	const FCombatTechniqueRow* Row = Techniques->FindRow(Request.TechniqueId);
	if (!Row)
	{
		OutReason = FText::FromString(TEXT("unknown technique id"));
		return false;
	}

	const UFighterVitalsComponent* Vitals =
		GetOwner()->FindComponentByClass<UFighterVitalsComponent>();

	if (!Vitals || Vitals->IsDead())
	{
		OutReason = FText::FromString(TEXT("fighter is dead"));
		return false;
	}

	if (!Techniques->IsAvailable(Request.TechniqueId))
	{
		OutReason = FText::FromString(TEXT("technique not available (repertoire/skills/equipment)"));
		return false;
	}

	if (!Techniques->CanExecute(Request.TechniqueId, Request))
	{
		OutReason = FText::FromString(TEXT("transient execution validation failed"));
		return false;
	}

	OutRow = Row;
	return true;
}

// ============================================================================
// Objective execution state
// ============================================================================

const FCombatExecutionRecord* UCombatExecutionComponent::FindPrimaryDeliberateRecord() const
{
	for (const FCombatExecutionRecord& Record : Executions)
	{
		if (Record.Kind == ECombatExecutionKind::Deliberate)
		{
			return &Record;
		}
	}

	return nullptr;
}

const FCombatExecutionRecord* UCombatExecutionComponent::FindCommittedStrikeRecord() const
{
	for (const FCombatExecutionRecord& Record : Executions)
	{
		if (Record.Kind == ECombatExecutionKind::Deliberate &&
			Record.State == ECombatExecutionState::Committed &&
			Record.bCommittedTrajectoryValid)
		{
			return &Record;
		}
	}

	return nullptr;
}

bool UCombatExecutionComponent::IsCommitted() const
{
	for (const FCombatExecutionRecord& Record : Executions)
	{
		if (Record.Kind == ECombatExecutionKind::Deliberate &&
			(Record.State == ECombatExecutionState::Committed ||
			 Record.State == ECombatExecutionState::Recovering))
		{
			return true;
		}
	}

	return false;
}

bool UCombatExecutionComponent::CanPlayAttack() const
{
	for (const FCombatExecutionRecord& Record : Executions)
	{
		if (Record.Kind == ECombatExecutionKind::Deliberate &&
			Record.State == ECombatExecutionState::Committed)
		{
			return true;
		}
	}

	return false;
}

bool UCombatExecutionComponent::IsStrikeWindowOpen() const
{
	for (const FCombatExecutionRecord& Record : Executions)
	{
		if (Record.Kind == ECombatExecutionKind::Deliberate &&
			Record.bStrikeWindowOpen)
		{
			return true;
		}
	}

	return false;
}

bool UCombatExecutionComponent::HasCommittedBladePath() const
{
	return FindCommittedStrikeRecord() != nullptr;
}

FCombatCommittedStrike UCombatExecutionComponent::GetCommittedStrike() const
{
	FCombatCommittedStrike Strike;

	if (const FCombatExecutionRecord* Record = FindCommittedStrikeRecord())
	{
		Strike.bValid = true;
		Strike.TechniqueId = Record->TechniqueId;
		Strike.Attacker = GetOwner();
		Strike.Trajectory = Record->CommittedTrajectory;
		Strike.Transform = Record->CommittedTransform;
	}

	return Strike;
}

bool UCombatExecutionComponent::IsAligning() const
{
	for (const FCombatExecutionRecord& Record : Executions)
	{
		if (Record.Kind == ECombatExecutionKind::Deliberate &&
			Record.State == ECombatExecutionState::Preparing &&
			Record.Executor &&
			Record.Executor->IsAwaitingAlignment())
		{
			return true;
		}
	}

	return false;
}

float UCombatExecutionComponent::GetCombatFacingDelta() const
{
	for (const FCombatExecutionRecord& Record : Executions)
	{
		if (Record.Kind == ECombatExecutionKind::Deliberate &&
			Record.State == ECombatExecutionState::Preparing &&
			Record.Executor &&
			Record.Executor->IsAwaitingAlignment())
		{
			return Record.Executor->GetFacingDeltaDegrees();
		}
	}

	return 0.f;
}

FVector UCombatExecutionComponent::ResolveOrientationIntent(
	FVector LocomotionIntent) const
{
	for (const FCombatExecutionRecord& Record : Executions)
	{
		if (Record.Kind == ECombatExecutionKind::Deliberate &&
			Record.Executor &&
			Record.Executor->GetFacingIntent(LocomotionIntent))
		{
			FVector Intent;
			Record.Executor->GetFacingIntent(Intent);

			if (!Intent.IsNearlyZero())
			{
				return Intent;
			}
		}
	}

	if (const UCombatFocusComponent* Focus =
			GetOwner() ? GetOwner()->FindComponentByClass<UCombatFocusComponent>() : nullptr)
	{
		if (const AActor* Target = Focus->GetCombatTarget())
		{
			FVector Intent =
				(Target->GetActorLocation() - GetOwner()->GetActorLocation())
					.GetSafeNormal2D();
			if (const USkeletalMeshComponent* Mesh =
					GetOwner()->FindComponentByClass<USkeletalMeshComponent>())
			{
				const float RootYaw =
					FMath::UnwindDegrees(Intent.Rotation().Yaw - Mesh->GetRelativeRotation().Yaw);
				Intent = FRotator(0.f, RootYaw, 0.f).Vector();
			}

			if (!Intent.IsNearlyZero())
			{
				return Intent;
			}
		}
	}

	return LocomotionIntent;
}

TArray<FName> UCombatExecutionComponent::GetActiveTechniqueIds() const
{
	TArray<FName> Ids;
	Ids.Reserve(Executions.Num());

	for (const FCombatExecutionRecord& Record : Executions)
	{
		Ids.Add(Record.TechniqueId);
	}

	return Ids;
}

bool UCombatExecutionComponent::IsExecutingReactive() const
{
	return Executions.ContainsByPredicate(
		[](const FCombatExecutionRecord& Record)
		{ return Record.Kind == ECombatExecutionKind::Reactive; });
}

bool UCombatExecutionComponent::GetActiveHandTarget(FTransform& OutHandTarget) const
{
	for (const FCombatExecutionRecord& Record : Executions)
	{
		if (Record.Kind == ECombatExecutionKind::Reactive &&
			Record.Executor &&
			Record.Executor->GetHandTarget(OutHandTarget))
		{
			return true;
		}
	}

	OutHandTarget = FTransform::Identity;
	return false;
}

bool UCombatExecutionComponent::HasAdmissionSlot(ECombatExecutionKind Kind) const
{
	int32 Count = 0;
	for (const FCombatExecutionRecord& Record : Executions)
	{
		if (Record.Kind == Kind)
		{
			++Count;
		}
	}

	const int32 Max =
		Kind == ECombatExecutionKind::Deliberate
			? MaxDeliberateExecutions
			: MaxReactiveExecutions;

	return Count < Max;
}

// ============================================================================
// Movement requirement seam
// ============================================================================

FCombatEngagementRequirement UCombatExecutionComponent::GetEngagementRequirement() const
{
	return CachedRequirement;
}

void UCombatExecutionComponent::BroadcastRequirement()
{
	FCombatEngagementRequirement Requirement;

	const FCombatExecutionRecord* Deliberate = FindPrimaryDeliberateRecord();
	if (Deliberate && Deliberate->Executor)
	{
		Deliberate->Executor->GetEngagementRequirement(Requirement);
	}

	if (!Requirement.bHasRequirement)
	{
		for (const FCombatExecutionRecord& Record : Executions)
		{
			if (Record.Kind == ECombatExecutionKind::Reactive && Record.Executor)
			{
				Record.Executor->GetEngagementRequirement(Requirement);
				if (Requirement.bHasRequirement)
				{
					break;
				}
			}
		}
	}

	if (!IsSameRequirement(Requirement, CachedRequirement))
	{
		CachedRequirement = Requirement;
		OnEngagementRequirementChanged.Broadcast(CachedRequirement);
	}
}

// ============================================================================
// Lifecycle helpers
// ============================================================================

FCombatExecutionRecord* UCombatExecutionComponent::FindRecord(int32 RecordId)
{
	return Executions.FindByPredicate(
		[RecordId](const FCombatExecutionRecord& Record)
		{ return Record.RecordId == RecordId; });
}

const FCombatExecutionRecord* UCombatExecutionComponent::FindRecord(int32 RecordId) const
{
	return Executions.FindByPredicate(
		[RecordId](const FCombatExecutionRecord& Record)
		{ return Record.RecordId == RecordId; });
}

void UCombatExecutionComponent::SetRecordState(int32 RecordId, ECombatExecutionState NewState)
{
	if (FCombatExecutionRecord* Record = FindRecord(RecordId))
	{
		Record->State = NewState;
		BroadcastRequirement();
	}
}

void UCombatExecutionComponent::MarkRecordCommitted(
	int32 RecordId,
	const FBladeTrajectory& Trajectory,
	const FTransform& Transform)
{
	if (FCombatExecutionRecord* Record = FindRecord(RecordId))
	{
		Record->State = ECombatExecutionState::Committed;
		Record->CommittedTrajectory = Trajectory;
		Record->CommittedTransform = Transform;
		Record->bCommittedTrajectoryValid = Trajectory.bValid;
		Record->bStrikeWindowOpen = false;
		BroadcastRequirement();
	}
}

void UCombatExecutionComponent::SetRecordStrikeWindow(int32 RecordId, bool bOpen)
{
	if (FCombatExecutionRecord* Record = FindRecord(RecordId))
	{
		Record->bStrikeWindowOpen = bOpen;
	}
}

void UCombatExecutionComponent::MarkRecordContactResolved(int32 RecordId)
{
	if (FCombatExecutionRecord* Record = FindRecord(RecordId))
	{
		Record->bContactResolved = true;
	}
}

void UCombatExecutionComponent::SetStrikeWindowOpen(bool bOpen)
{
	/*
	 * The attack-window notify carries no record identity: it applies to the
	 * committed deliberate execution. Admission policy keeps at most one, so
	 * the first match is the whole set.
	 */
	for (FCombatExecutionRecord& Record : Executions)
	{
		if (Record.Kind == ECombatExecutionKind::Deliberate &&
			Record.State == ECombatExecutionState::Committed)
		{
			Record.bStrikeWindowOpen = bOpen;
			return;
		}
	}

	if (bOpen)
	{
		UE_LOG(
			LogIronboundCombat,
			Warning,
			TEXT("Strike window opened [%s] with no committed deliberate execution"),
			*GetNameSafe(GetOwner()));
	}
}

void UCombatExecutionComponent::FinishRecord(int32 RecordId, const TCHAR* Reason)
{
	const int32 Index = Executions.IndexOfByPredicate(
		[RecordId](const FCombatExecutionRecord& Record)
		{ return Record.RecordId == RecordId; });

	if (Index == INDEX_NONE)
	{
		return;
	}

	FCombatExecutionRecord Record = MoveTemp(Executions[Index]);
	Executions.RemoveAt(Index);

	if (Record.Executor)
	{
		Record.Executor->OnFinish();
	}

	UE_LOG(
		LogIronboundCombat,
		Log,
		TEXT("Execution terminal [%s | %s] record %d plan %d: %s"),
		*GetNameSafe(GetOwner()),
		*Record.TechniqueId.ToString(),
		Record.RecordId,
		Record.PlanId,
		Reason);

	BroadcastRequirement();
}

void UCombatExecutionComponent::FinishAttack()
{
	/*
	 * Deprecated seam. The old BP montage-end path asked the component to move
	 * a committed attack into recovery; that responsibility now lives in the
	 * executor (montage-end binding). Forward to the executor's external
	 * finish hook so the old wiring still produces the old semantics.
	 */
	for (FCombatExecutionRecord& Record : Executions)
	{
		if (Record.Kind == ECombatExecutionKind::Deliberate &&
			Record.State == ECombatExecutionState::Committed &&
			Record.Executor)
		{
			Record.Executor->OnExternalFinishRequest();
			return;
		}
	}
}

void UCombatExecutionComponent::CancelAttack()
{
	CancelActiveExecutions();
}

void UCombatExecutionComponent::CancelActiveExecutions()
{
	for (int32 Index = Executions.Num() - 1; Index >= 0; --Index)
	{
		FCombatExecutionRecord& Record = Executions[Index];

		if (Record.State == ECombatExecutionState::Committed || !Record.Executor)
		{
			continue;
		}

		const int32 RecordId = Record.RecordId;
		Record.Executor->RequestFinish(TEXT("cancelled"));
		if (FindRecord(RecordId))
		{
			FinishRecord(RecordId, TEXT("cancelled"));
		}
	}
}

void UCombatExecutionComponent::CancelAllExecutions()
{
	for (int32 Index = Executions.Num() - 1; Index >= 0; --Index)
	{
		FCombatExecutionRecord& Record = Executions[Index];

		if (!Record.Executor)
		{
			continue;
		}

		const int32 RecordId = Record.RecordId;
		Record.Executor->RequestFinish(TEXT("cancelled"));
		if (FindRecord(RecordId))
		{
			FinishRecord(RecordId, TEXT("cancelled"));
		}
	}
}

void UCombatExecutionComponent::CancelPlannedExecution(int32 PlanId, const TCHAR* Reason)
{
	if (PlanId <= 0) return;
	for (const FCombatExecutionRecord& Record : Executions)
	{
		if (Record.PlanId != PlanId || Record.Kind != ECombatExecutionKind::Deliberate ||
			Record.State != ECombatExecutionState::Preparing || !Record.Executor)
		{
			continue;
		}
		const int32 RecordId = Record.RecordId;
		Record.Executor->RequestFinish(Reason);
		if (FindRecord(RecordId)) FinishRecord(RecordId, Reason);
		return;
	}
}

// ============================================================================
// Tick
// ============================================================================

void UCombatExecutionComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Reverse iteration: executors may finish (and remove) their own record.
	for (int32 Index = Executions.Num() - 1; Index >= 0; --Index)
	{
		if (UCombatTechniqueExecutor* Executor = Executions[Index].Executor)
		{
			Executor->TickExecution(DeltaTime);
		}
	}

	ResolveStrikeContacts();

	BroadcastRequirement();
}

void UCombatExecutionComponent::ResolveStrikeContacts()
{
	AActor* Attacker = GetOwner();
	UWorld* World = GetWorld();
	UCombatTechniqueComponent* Techniques = GetTechniques();
	if (!Attacker || !World || !Techniques)
	{
		return;
	}

	for (FCombatExecutionRecord& Record : Executions)
	{
		if (Record.Kind != ECombatExecutionKind::Deliberate ||
			Record.State != ECombatExecutionState::Committed ||
			!Record.bStrikeWindowOpen ||
			Record.bContactResolved ||
			!Record.bCommittedTrajectoryValid ||
			!Record.Target ||
			!Record.CommittedTrajectory.bValid)
		{
			continue;
		}

		AActor* Defender = Record.Target;
		UCombatReactionComponent* Reaction =
			Defender->FindComponentByClass<UCombatReactionComponent>();
		if (!Reaction || !Reaction->CanReceiveCombatHit())
		{
			continue;
		}

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IronboundStrikeContact), true, Attacker);
		QueryParams.AddIgnoredActor(Attacker);
		FHitResult Contact;
		bool bContactFound = false;
		const FCollisionShape SweepShape = FCollisionShape::MakeSphere(FMath::Max(0.1f, StrikeSweepRadiusCm));

		for (const FBladeSegment& Segment : Record.CommittedTrajectory.Segments)
		{
			const FVector Start = Record.CommittedTransform.TransformPosition(Segment.Base);
			const FVector End = Record.CommittedTransform.TransformPosition(Segment.Tip);
			if (World->SweepSingleByChannel(
				Contact,
				Start,
				End,
				FQuat::Identity,
				ECC_Pawn,
				SweepShape,
				QueryParams) &&
				Contact.GetActor() == Defender)
			{
				bContactFound = true;
				break;
			}
		}

		if (!bContactFound)
		{
			continue;
		}

		Record.bContactResolved = true;
		const FCombatTechniqueRow* AttackRow = Techniques->FindRow(Record.TechniqueId);
		FCombatInteraction Interaction;
		Interaction.Source = Attacker;
		Interaction.Receiver = Defender;
		Interaction.SourceTechniqueId = Record.TechniqueId;
		Interaction.ContactPoint = Contact.ImpactPoint;
		Interaction.ContactNormal = Contact.ImpactNormal;
		Interaction.BodyRegion = Contact.BoneName;
		Interaction.ReceiverComponent = Contact.GetComponent();
		Interaction.Impulse = Contact.ImpactNormal * Record.CommittedTrajectory.PeakTipSpeed;
		Interaction.WeaponTipSpeed = Record.CommittedTrajectory.PeakTipSpeed;
		if (const UCombatEquipmentComponent* Equipment =
				Attacker->FindComponentByClass<UCombatEquipmentComponent>())
		{
			Interaction.SourceComponent = Equipment->GetWeapon();
		}

		bool bBlocked = false;
		if (UCombatExecutionComponent* DefenderExecution =
				Defender->FindComponentByClass<UCombatExecutionComponent>())
		{
			for (const FName ActiveId : DefenderExecution->GetActiveTechniqueIds())
			{
				const FCombatTechniqueRow* DefensiveRow =
					DefenderExecution->GetTechniques()->FindRow(ActiveId);
				if (DefensiveRow && DefensiveRow->Kind == ECombatTechniqueKind::Reactive &&
					DefensiveRow->bBlocksIncomingStrike)
				{
					Interaction.ReceiverTechniqueId = ActiveId;
					bBlocked = true;
					break;
				}
			}
		}

		if (bBlocked)
		{
			UE_LOG(LogIronboundCombat, Log,
				TEXT("Strike blocked [%s | %s] by [%s | %s]"),
				*GetNameSafe(Attacker), *Record.TechniqueId.ToString(),
				*GetNameSafe(Defender), *Interaction.ReceiverTechniqueId.ToString());
			continue;
		}

		if (AttackRow)
		{
			const FCombatInteractionResult Result =
				UCombatInteractionLibrary::Resolve(Interaction, Techniques->TechniquesTable);
			if (Reaction->ReceiveInteraction(Interaction, Result))
			{
				UE_LOG(LogIronboundCombat, Log,
					TEXT("Strike hit [%s | %s -> %s] damage=%.1f"),
					*GetNameSafe(Attacker), *Record.TechniqueId.ToString(),
					*GetNameSafe(Defender), Result.Damage);
			}
		}
	}
}

// ============================================================================
// Sibling combat-domain components
// ============================================================================

UCombatTechniqueComponent* UCombatExecutionComponent::GetTechniques()
{
	if (UCombatTechniqueComponent* Techniques =
			GetOwner()->FindComponentByClass<UCombatTechniqueComponent>())
	{
		if (!Techniques->TechniquesTable)
		{
			if (const UFighterComponent* Fighter = GetOwner()->FindComponentByClass<UFighterComponent>())
			{
				Techniques->TechniquesTable = Fighter->CombatSkillsTable;
			}
		}
		return Techniques;
	}

	UCombatTechniqueComponent* Techniques =
		NewObject<UCombatTechniqueComponent>(GetOwner());
	if (const UFighterComponent* Fighter = GetOwner()->FindComponentByClass<UFighterComponent>())
	{
		Techniques->TechniquesTable = Fighter->CombatSkillsTable;
	}

	Techniques->RegisterComponent();

	UE_LOG(
		LogIronboundCombat,
		Log,
		TEXT("Combat domain [%s]: created technique registry component"),
		*GetNameSafe(GetOwner()));

	return Techniques;
}

UCombatThreatComponent* UCombatExecutionComponent::GetThreats()
{
	if (UCombatThreatComponent* Threats =
			GetOwner()->FindComponentByClass<UCombatThreatComponent>())
	{
		return Threats;
	}

	UCombatThreatComponent* Threats =
		NewObject<UCombatThreatComponent>(GetOwner());

	Threats->RegisterComponent();

	UE_LOG(
		LogIronboundCombat,
		Log,
		TEXT("Combat domain [%s]: created threat observation component"),
		*GetNameSafe(GetOwner()));

	return Threats;
}
