#include "Combat/CombatReactionComponent.h"

#include "Combat/FighterVitalsComponent.h"
#include "GameFramework/Actor.h"

UCombatReactionComponent::UCombatReactionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCombatReactionComponent::BeginPlay()
{
	Super::BeginPlay();

	// Resolve once, so the first landed hit does not pay for the lookup.
	GetVitals();
}

UFighterVitalsComponent* UCombatReactionComponent::GetVitals()
{
	if (!Vitals)
	{
		if (const AActor* Owner = GetOwner())
		{
			Vitals = Owner->FindComponentByClass<UFighterVitalsComponent>();
		}
	}

	return Vitals;
}

bool UCombatReactionComponent::CanReceiveCombatHit()
{
	const UFighterVitalsComponent* Target = GetVitals();
	return Target != nullptr && !Target->IsDead();
}

bool UCombatReactionComponent::ReceiveCombatHit(float DamageAmount, int32 AttackerTeam)
{
	UFighterVitalsComponent* Target = GetVitals();
	if (!Target)
	{
		return false;
	}

	// Whether a hit is acceptable at all is a health rule, so it stays with the health. This
	// component is the entry point, not a second copy of the validation.
	return Target->ReceiveCombatHit(DamageAmount, AttackerTeam);
}

int32 UCombatReactionComponent::GetReactionTeam()
{
	const UFighterVitalsComponent* Target = GetVitals();
	return Target ? Target->GetOwnerFighterTeam() : 0;
}
