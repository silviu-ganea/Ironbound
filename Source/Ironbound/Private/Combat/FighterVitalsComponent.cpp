#include "Combat/FighterVitalsComponent.h"

#include "Combat/FighterComponent.h"
#include "Combat/IronboundCombatFocusComponent.h"
#include "GameFramework/Actor.h"

UFighterVitalsComponent::UFighterVitalsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

float UFighterVitalsComponent::GetHealthNormalized() const
{
	if (MaxHealth <= 0.f)
	{
		return 0.f;
	}

	return FMath::Clamp(CurrentHealth / MaxHealth, 0.f, 1.f);
}

int32 UFighterVitalsComponent::GetOwnerFighterTeam() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return 0;
	}

	// FighterComponent is the team authority once a battle has registered the fighter.
	if (const UFighterComponent* Fighter = Owner->FindComponentByClass<UFighterComponent>())
	{
		if (Fighter->HasTeam())
		{
			return Fighter->GetBattleTeamId();
		}
	}

	// Compatibility bridge. Until the battle-manager registration path has populated
	// FighterComponent, the live side is still the one CombatFocus was initialized with -
	// the same value the Blueprint hit validation used to read off the actor.
	if (const UIronboundCombatFocusComponent* Focus = Owner->FindComponentByClass<UIronboundCombatFocusComponent>())
	{
		return Focus->Team;
	}

	return 0;
}

void UFighterVitalsComponent::InitializeVitals()
{
	// A fresh fighter is alive at full strength. This is the only place the alive state is
	// cleared, so a respawn or a new battle must come through here.
	bDead = false;
	CurrentHealth = MaxHealth;

	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
}

void UFighterVitalsComponent::SetMaxHealth(float InMaxHealth)
{
	MaxHealth = FMath::Max(0.f, InMaxHealth);
	InitializeVitals();
}

bool UFighterVitalsComponent::ReceiveCombatHit(float DamageAmount, int32 AttackerTeam)
{
	if (bDead || DamageAmount <= 0.f)
	{
		return false;
	}

	if (bRequireEnemyTeamForDamage)
	{
		const int32 DefenderTeam = GetOwnerFighterTeam();

		// Team 0 is not a side: an unteamed fighter is an observer and takes no combat damage.
		// Same team means friendly fire, which is also ignored.
		if (DefenderTeam <= 0 || DefenderTeam == AttackerTeam)
		{
			return false;
		}
	}

	return ApplyDamage(DamageAmount);
}

bool UFighterVitalsComponent::ApplyDamage(float DamageAmount)
{
	if (bDead || DamageAmount <= 0.f)
	{
		return false;
	}

	CurrentHealth = FMath::Max(0.f, CurrentHealth - DamageAmount);

	// Health is reported before death, so UI sees the empty bar before any ragdoll.
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);

	if (CurrentHealth <= 0.f)
	{
		EnterDeath();
	}

	return true;
}

void UFighterVitalsComponent::Kill()
{
	if (bDead)
	{
		return;
	}

	CurrentHealth = 0.f;
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
	EnterDeath();
}

void UFighterVitalsComponent::Heal(float HealAmount)
{
	if (bDead || HealAmount <= 0.f)
	{
		return;
	}

	if (CurrentHealth >= MaxHealth)
	{
		return;
	}

	CurrentHealth = FMath::Min(MaxHealth, CurrentHealth + HealAmount);
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
}

void UFighterVitalsComponent::EnterDeath()
{
	if (bDead)
	{
		return;
	}

	bDead = true;

	// Death presentation belongs to the owner: this only announces it.
	OnDeath.Broadcast();
}
