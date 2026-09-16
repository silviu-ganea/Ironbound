#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "IronboundCombatAnimInstance.generated.h"

/** Game-thread snapshot consumed by the AnimGraph's head-only look-at control. */
UCLASS(Transient, Blueprintable)
class IRONBOUND_API UIronboundCombatAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	UPROPERTY(Transient, BlueprintReadOnly, Category="Combat") FVector CombatLookLocation = FVector::ZeroVector;
	UPROPERTY(Transient, BlueprintReadOnly, Category="Combat") float CombatLookAlpha = 0.f;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};
