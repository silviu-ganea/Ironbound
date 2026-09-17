#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "IronboundCombatAnimInstance.generated.h"

/** Game-thread combat snapshot consumed by the AnimGraph. */
UCLASS(Transient, Blueprintable)
class IRONBOUND_API UIronboundCombatAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient, BlueprintReadOnly, Category="Combat")
	FVector CombatLookLocation = FVector::ZeroVector;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Combat")
	float CombatLookAlpha = 0.f;

	/** True while CombatExecution is establishing the trajectory solver's attack facing. */
	UPROPERTY(Transient, BlueprintReadOnly, Category="Combat|Alignment")
	bool CombatIsAligning = false;

	/**
	 * Signed yaw difference, in degrees, between current actor facing and desired attack facing.
	 * This is intended for the existing GASP turn-in-place system.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category="Combat|Alignment")
	float CombatFacingDelta = 0.f;

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};