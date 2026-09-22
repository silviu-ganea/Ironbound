#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FighterVitalsComponent.generated.h"

class UFighterComponent;
class UIronboundCombatFocusComponent;

/** Fired whenever health changes, including the initialization reset and the killing blow. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FFighterVitalsHealthChangedSignature,
	float, NewHealth,
	float, MaxHealth);

/** Fired once, when health reaches zero. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFighterVitalsDeathSignature);

/**
 * Sole owner of one fighter's health, damage and alive/dead state.
 *
 * The owning actor keeps movement, animation, body physics and presentation. It must not
 * mirror CurrentHealth/MaxHealth/alive state or reproduce the damage arithmetic: read this
 * component and listen to its delegates instead.
 *
 * Death is reported, never performed: OnDeath drives the owner's own death presentation
 * (ragdoll, widget teardown, attack cancellation), which stays wherever it is authored.
 */
UCLASS(ClassGroup=(Ironbound), meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UFighterVitalsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFighterVitalsComponent();

	/** Health at full strength. Authored per fighter; set with SetMaxHealth at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Vitals", meta=(ClampMin="0.0"))
	float MaxHealth = 100.f;

	/** Remaining health. Written only by this component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Vitals")
	float CurrentHealth = 100.f;

	/** Alive state. Set the moment health reaches zero; cleared by InitializeVitals. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Vitals")
	bool bDead = false;

	/**
	 * When true a hit is refused unless the owner is on a real team and the attacker is on a
	 * different one. Turn off for scripted, environmental or debug damage.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Vitals")
	bool bRequireEnemyTeamForDamage = true;

	/** Health changed. Carries the new value so listeners do not have to re-read the component. */
	UPROPERTY(BlueprintAssignable, Category="Combat|Vitals")
	FFighterVitalsHealthChangedSignature OnHealthChanged;

	/** Health reached zero. The owner's death presentation hooks onto this. */
	UPROPERTY(BlueprintAssignable, Category="Combat|Vitals")
	FFighterVitalsDeathSignature OnDeath;


	// Queries

	UFUNCTION(BlueprintPure, Category="Ironbound|Combat|Vitals")
	float GetCurrentHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintPure, Category="Ironbound|Combat|Vitals")
	float GetMaxHealth() const { return MaxHealth; }

	/** Remaining health as 0-1, for UI. Zero when MaxHealth is zero. */
	UFUNCTION(BlueprintPure, Category="Ironbound|Combat|Vitals")
	float GetHealthNormalized() const;

	UFUNCTION(BlueprintPure, Category="Ironbound|Combat|Vitals")
	bool IsDead() const { return bDead; }

	UFUNCTION(BlueprintPure, Category="Ironbound|Combat|Vitals")
	bool IsAlive() const { return !bDead; }

	/**
	 * Runtime battle team of the owner: FighterComponent once a battle has registered the
	 * fighter, otherwise the live combat team the existing systems read from CombatFocus.
	 * Returns 0 for an unteamed observer.
	 */
	UFUNCTION(BlueprintPure, Category="Ironbound|Combat|Vitals")
	int32 GetOwnerFighterTeam() const;


	// Commands

	/** Full reset: CurrentHealth = MaxHealth and alive again. Broadcasts OnHealthChanged. */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat|Vitals")
	void InitializeVitals();

	/** Sets MaxHealth and performs a full reset. */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat|Vitals")
	void SetMaxHealth(float InMaxHealth);

	/**
	 * Combat entry point. Refuses the hit when the owner is already dead, when the damage is
	 * not positive, or when the owner is unteamed / on the attacker's team. Returns true when
	 * the damage was accepted, false when it was ignored.
	 */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat|Vitals")
	bool ReceiveCombatHit(float DamageAmount, int32 AttackerTeam);

	/** Raw damage: subtracts and reports, with no team or source validation. */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat|Vitals")
	bool ApplyDamage(float DamageAmount);

	/** Immediate death without damage arithmetic. */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat|Vitals")
	void Kill();

	/** Restores health, clamped to MaxHealth. A dead fighter is not revived by healing. */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat|Vitals")
	void Heal(float HealAmount);


private:
	void EnterDeath();
};
