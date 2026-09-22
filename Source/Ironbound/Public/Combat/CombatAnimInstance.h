#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "CombatAnimInstance.generated.h"

/** Game-thread combat snapshot consumed by the AnimGraph. */
UCLASS(Transient, Blueprintable)
class IRONBOUND_API UCombatAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    UPROPERTY(Transient, BlueprintReadOnly, Category="Combat")
    FVector CombatLookLocation = FVector::ZeroVector;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Combat")
    float CombatLookAlpha = 0.f;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Combat|Alignment")
    bool CombatIsAligning = false;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Combat|Alignment")
    float CombatFacingDelta = 0.f;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Combat|Parry")
    bool ParryIKActive = false;

    /** World-space hand/socket target produced by the parry solver. */
    UPROPERTY(Transient, BlueprintReadOnly, Category="Combat|Parry")
    FTransform ParryHandTarget = FTransform::Identity;

    /** Hand target converted into skeletal-mesh component space for CCDIK/AnimGraph use. */
    UPROPERTY(Transient, BlueprintReadOnly, Category="Combat|Parry")
    FTransform ParryHandTargetComponentSpace = FTransform::Identity;

    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};
