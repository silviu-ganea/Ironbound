#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IronboundParryComponent.generated.h"

class USkeletalMeshComponent;
class UIronboundCombatFocusComponent;

USTRUCT()
struct FIronboundParryCandidate
{
    GENERATED_BODY()

    FVector ContactPoint = FVector::ZeroVector;

    FVector IncomingBase = FVector::ZeroVector;
    FVector IncomingTip = FVector::ZeroVector;

    FVector DefenseBase = FVector::ZeroVector;
    FVector DefenseTip = FVector::ZeroVector;

    FVector RequiredHandPosition = FVector::ZeroVector;
    FVector RequiredElbowPosition = FVector::ZeroVector;

    float IncomingTime = 0.f;
    float DefenderBladeFraction = 0.f;
    float IntersectionAngleDegrees = 0.f;

    float MovementCost = TNumericLimits<float>::Max();

    bool bValid = false;
};

UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UIronboundParryComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UIronboundParryComponent();

protected:
    virtual void BeginPlay() override;

    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // ------------------------------------------------------------
    // Anatomy
    // ------------------------------------------------------------

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy")
    FName UpperArmBone = TEXT("upperarm_r");

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy")
    FName LowerArmBone = TEXT("lowerarm_r");

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy")
    FName HandBone = TEXT("hand_r");

    UPROPERTY(
        EditAnywhere,
        Category="Combat|Parry|Anatomy",
        meta=(ClampMin="0.0"))
    float ArmReachMargin = 1.0f;

    UPROPERTY(
        EditAnywhere,
        Category="Combat|Parry|Anatomy",
        meta=(ClampMin="4", ClampMax="64"))
    int32 ElbowCircleSamples = 16;

    UPROPERTY(
        EditAnywhere,
        Category="Combat|Parry|Anatomy",
        meta=(ClampMin="0.0"))
    float ElbowMovementCostWeight = 0.35f;

    /*
     * Candidate elbows are compared against the defender's current
     * anatomical elbow side.
     *
     * 0.0 = candidate may reach the center plane, but may not cross
     *       onto the opposite side.
     *
     * Higher values require the candidate elbow to remain more
     * strongly on the current anatomical side.
     */
    UPROPERTY(
        EditAnywhere,
        Category="Combat|Parry|Anatomy",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float MinimumElbowSideDot = 0.0f;

    // ------------------------------------------------------------
    // Defender blade contact region
    // ------------------------------------------------------------

    UPROPERTY(
        EditAnywhere,
        Category="Combat|Parry|Weapon",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float MinParryBladeFraction = 0.20f;

    UPROPERTY(
        EditAnywhere,
        Category="Combat|Parry|Weapon",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float MaxParryBladeFraction = 0.90f;

    // ------------------------------------------------------------
    // Solver sampling
    // ------------------------------------------------------------

    UPROPERTY(
        EditAnywhere,
        Category="Combat|Parry|Solver",
        meta=(ClampMin="2", ClampMax="32"))
    int32 IncomingBladeContactSamples = 5;

    UPROPERTY(
        EditAnywhere,
        Category="Combat|Parry|Solver",
        meta=(ClampMin="2", ClampMax="16"))
    int32 DefenderBladeFractionSamples = 4;

    UPROPERTY(
        EditAnywhere,
        Category="Combat|Parry|Solver",
        meta=(ClampMin="4", ClampMax="64"))
    int32 DefenseOrientationSamples = 12;

    UPROPERTY(
        EditAnywhere,
        Category="Combat|Parry|Solver",
        meta=(ClampMin="2", ClampMax="16"))
    int32 DefenseCrossingAngleSamples = 5;

    UPROPERTY(
        EditAnywhere,
        Category="Combat|Parry|Solver",
        meta=(ClampMin="0.0", ClampMax="90.0"))
    float MinimumIntersectionAngleDegrees = 30.f;

    UPROPERTY(
        EditAnywhere,
        Category="Combat|Parry|Solver",
        meta=(ClampMin="0.0"))
    float TranslationCostWeight = 1.f;

    UPROPERTY(
        EditAnywhere,
        Category="Combat|Parry|Solver",
        meta=(ClampMin="0.0"))
    float RotationCostWeight = 0.35f;

    // ------------------------------------------------------------
    // Debug
    // ------------------------------------------------------------

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Debug")
    bool bDrawParryDebug = true;

private:
    UPROPERTY()
    TObjectPtr<USkeletalMeshComponent> FighterMesh;

    UPROPERTY()
    TObjectPtr<UIronboundCombatFocusComponent> CombatFocus;

    float UpperArmLength = 0.f;
    float ForearmLength = 0.f;
    float ArmLength = 0.f;

    bool CalculateArmDimensions();

    void DrawAnatomyDebug() const;
    void DrawParrySolutionDebug() const;

    bool GetCurrentWeaponGeometry(
        FVector& OutCurrentBaseWorld,
        FVector& OutCurrentTipWorld,
        float& OutBladeLength,
        float& OutMinFraction,
        float& OutMaxFraction) const;

    bool FindBestElbowPosition(
        const FVector& ShoulderWorld,
        const FVector& RequiredHandWorld,
        const FVector& CurrentElbowWorld,
        FVector& OutElbowWorld,
        float& OutElbowMovementCost) const;

    bool FindBestParryCandidate(
        FIronboundParryCandidate& OutCandidate) const;

    bool EvaluateCandidate(
        const FVector& ShoulderWorld,
        const FVector& CurrentElbowWorld,
        const FVector& IncomingBaseWorld,
        const FVector& IncomingTipWorld,
        const FVector& IncomingContactWorld,
        const FVector& IncomingDirection,
        float IncomingTime,
        const FVector& DefenseDirection,
        float DefenderBladeFraction,
        float BladeLength,
        const FVector& CurrentDefenseBase,
        const FVector& CurrentDefenseDirection,
        FIronboundParryCandidate& OutCandidate) const;
};