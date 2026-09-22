#include "Combat/CombatTechniqueExecutor.h"

#include "Combat/CombatBodyComponent.h"
#include "Combat/CombatEquipmentComponent.h"
#include "Combat/CombatExecutionComponent.h"
#include "Combat/CombatFocusComponent.h"
#include "Combat/CombatTechniqueComponent.h"
#include "Combat/CombatThreatComponent.h"
#include "Combat/FighterComponent.h"
#include "Combat/FighterVitalsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

bool UCombatTechniqueExecutor::InitializeExecution(
	UCombatExecutionComponent* InOwner,
	int32 InRecordId,
	const FCombatTechniqueRow& InRow,
	const UCombatTechniqueExecutionConfig* InConfig,
	const FCombatTechniqueRequest& InRequest)
{
	OwnerComponent = InOwner;
	RecordId = InRecordId;
	Row = InRow;
	Config = InConfig;
	Request = InRequest;

	return OnInitialize(InRequest);
}

void UCombatTechniqueExecutor::TickExecution(float DeltaTime)
{
	OnTick(DeltaTime);
}

void UCombatTechniqueExecutor::RequestFinish(const TCHAR* Reason)
{
	FinishExecution(Reason);
}

void UCombatTechniqueExecutor::OnExternalFinishRequest()
{
	FinishExecution(TEXT("external finish"));
}

void UCombatTechniqueExecutor::GetEngagementRequirement(
	FCombatEngagementRequirement& OutRequirement) const
{
	OutRequirement = FCombatEngagementRequirement();
}

bool UCombatTechniqueExecutor::GetFacingIntent(FVector& OutIntent) const
{
	OutIntent = FVector::ZeroVector;
	return false;
}

bool UCombatTechniqueExecutor::IsAwaitingAlignment() const
{
	return false;
}

float UCombatTechniqueExecutor::GetFacingDeltaDegrees() const
{
	return 0.f;
}

bool UCombatTechniqueExecutor::GetHandTarget(FTransform& OutHandTarget) const
{
	OutHandTarget = FTransform::Identity;
	return false;
}

bool UCombatTechniqueExecutor::OnInitialize(const FCombatTechniqueRequest& Request)
{
	return true;
}

void UCombatTechniqueExecutor::OnTick(float DeltaTime)
{
}

void UCombatTechniqueExecutor::OnFinish()
{
}

void UCombatTechniqueExecutor::SetRecordState(ECombatExecutionState NewState)
{
	if (UCombatExecutionComponent* Component = OwnerComponent.Get())
	{
		Component->SetRecordState(RecordId, NewState);
	}
}

void UCombatTechniqueExecutor::MarkRecordCommitted(
	const FBladeTrajectory& Trajectory,
	const FTransform& Transform)
{
	if (UCombatExecutionComponent* Component = OwnerComponent.Get())
	{
		Component->MarkRecordCommitted(RecordId, Trajectory, Transform);
	}
}

void UCombatTechniqueExecutor::SetRecordStrikeWindow(bool bOpen)
{
	if (UCombatExecutionComponent* Component = OwnerComponent.Get())
	{
		Component->SetRecordStrikeWindow(RecordId, bOpen);
	}
}

void UCombatTechniqueExecutor::MarkRecordContactResolved()
{
	if (UCombatExecutionComponent* Component = OwnerComponent.Get())
	{
		Component->MarkRecordContactResolved(RecordId);
	}
}

void UCombatTechniqueExecutor::FinishExecution(const TCHAR* Reason)
{
	if (UCombatExecutionComponent* Component = OwnerComponent.Get())
	{
		Component->FinishRecord(RecordId, Reason);
	}
}

AActor* UCombatTechniqueExecutor::GetFighter() const
{
	return OwnerComponent.IsValid() ? OwnerComponent->GetOwner() : nullptr;
}

USkeletalMeshComponent* UCombatTechniqueExecutor::GetFighterMesh() const
{
	const AActor* Fighter = GetFighter();
	return Fighter ? Fighter->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

UCombatTechniqueComponent* UCombatTechniqueExecutor::GetTechniques() const
{
	return OwnerComponent.IsValid() ? OwnerComponent->GetTechniques() : nullptr;
}

UCombatThreatComponent* UCombatTechniqueExecutor::GetThreats() const
{
	return OwnerComponent.IsValid() ? OwnerComponent->GetThreats() : nullptr;
}

UCombatEquipmentComponent* UCombatTechniqueExecutor::GetEquipment() const
{
	const AActor* Fighter = GetFighter();
	return Fighter ? Fighter->FindComponentByClass<UCombatEquipmentComponent>() : nullptr;
}

UCombatFocusComponent* UCombatTechniqueExecutor::GetFocus() const
{
	const AActor* Fighter = GetFighter();
	return Fighter ? Fighter->FindComponentByClass<UCombatFocusComponent>() : nullptr;
}

UCombatBodyComponent* UCombatTechniqueExecutor::GetBody() const
{
	const AActor* Fighter = GetFighter();
	return Fighter ? Fighter->FindComponentByClass<UCombatBodyComponent>() : nullptr;
}

UFighterComponent* UCombatTechniqueExecutor::GetFighterComponent() const
{
	const AActor* Fighter = GetFighter();
	return Fighter ? Fighter->FindComponentByClass<UFighterComponent>() : nullptr;
}

UFighterVitalsComponent* UCombatTechniqueExecutor::GetVitals() const
{
	const AActor* Fighter = GetFighter();
	return Fighter ? Fighter->FindComponentByClass<UFighterVitalsComponent>() : nullptr;
}

float UCombatTechniqueExecutor::GetSkillProficiency(
	FName SkillId,
	float FallbackProficiency) const
{
	const UFighterComponent* Fighter = GetFighterComponent();
	FLearnedSkill Skill;
	return Fighter && Fighter->FindLearnedSkill(SkillId, Skill)
		? FMath::Clamp(Skill.Proficiency, 0.f, 1.f)
		: FallbackProficiency;
}
