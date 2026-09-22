#include "Combat/CombatFocusComponent.h"

#include "Combat/FighterComponent.h"
#include "GameFramework/Pawn.h"

UCombatFocusComponent::UCombatFocusComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCombatFocusComponent::BeginPlay()
{
	Super::BeginPlay();
}

UFighterComponent* UCombatFocusComponent::GetFighter() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UFighterComponent>() : nullptr;
}

int32 UCombatFocusComponent::GetTeamId() const
{
	const UFighterComponent* Fighter = GetFighter();
	return Fighter ? Fighter->GetBattleTeamId() : 0;
}

void UCombatFocusComponent::InitializeFocus(int32 FighterTeam)
{
	// Deprecated bridge: route legacy team initialization to the single team
	// authority instead of duplicating the value here.
	if (UFighterComponent* Fighter = GetFighter())
	{
		Fighter->SetBattleTeamId(FighterTeam);
	}
}

bool UCombatFocusComponent::IsEnemy(const AActor* Candidate) const
{
	if (bDead || !IsValid(Candidate) || Candidate == GetOwner()) return false;

	const UFighterComponent* Fighter = GetFighter();
	if (!Fighter || Fighter->GetBattleTeamId() <= 0) return false;

	const UFighterComponent* Other = Candidate->FindComponentByClass<UFighterComponent>();
	return Other && Other->GetBattleTeamId() > 0 && Other->GetBattleTeamId() != Fighter->GetBattleTeamId();
}

bool UCombatFocusComponent::SetCombatTarget(AActor* Target)
{
	if (Target && !IsEnemy(Target)) return false;
	CombatTarget = Target;
	return true;
}

AActor* UCombatFocusComponent::GetCombatTarget() const
{
	return IsEnemy(CombatTarget) ? CombatTarget.Get() : nullptr;
}

void UCombatFocusComponent::MarkDead()
{
	bDead = true;
	CombatTarget = nullptr;
}
