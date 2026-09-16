#include "Combat/IronboundCombatFocusComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

UIronboundCombatFocusComponent::UIronboundCombatFocusComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.2f;
}
void UIronboundCombatFocusComponent::InitializeFocus(int32 FighterTeam)
{
	Team = FighterTeam;
	bDead = false;
	CombatTarget = nullptr;
}
bool UIronboundCombatFocusComponent::IsEnemy(const AActor* Candidate) const
{
	if (bDead || Team <= 0 || !IsValid(Candidate) || Candidate == GetOwner()) return false;
	const auto* Other = Candidate->FindComponentByClass<UIronboundCombatFocusComponent>();
	return Other && !Other->bDead && Other->Team > 0 && Other->Team != Team;
}
bool UIronboundCombatFocusComponent::SetCombatTarget(AActor* Target)
{
	if (Target && !IsEnemy(Target)) return false;
	CombatTarget = Target;
	return true;
}
AActor* UIronboundCombatFocusComponent::GetCombatTarget() const
{
	return IsEnemy(CombatTarget) ? CombatTarget.Get() : nullptr;
}
void UIronboundCombatFocusComponent::MarkDead()
{
	bDead = true;
	CombatTarget = nullptr;
}
void UIronboundCombatFocusComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (GetCombatTarget()) return; // Sticky selection: no nearest-enemy thrashing or retaliatory retargeting.
	CombatTarget = nullptr;
	if (!bAutoAcquireTarget || bDead || Team <= 0) return;
	double BestDistance = TNumericLimits<double>::Max();
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		if (!IsEnemy(*It)) continue;
		const double Distance = FVector::DistSquared(GetOwner()->GetActorLocation(), It->GetActorLocation());
		if (Distance < BestDistance) { BestDistance = Distance; CombatTarget = *It; }
	}
}
