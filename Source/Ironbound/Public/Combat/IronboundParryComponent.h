#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IronboundParryComponent.generated.h"

class USkeletalMeshComponent;
class UIronboundCombatFocusComponent;

USTRUCT()
struct FIronboundParryDiagnostics
{
    GENERATED_BODY()

    int32 IncomingSegments = 0;
    int32 ExpiredSegments = 0;
    int32 ContactPoints = 0;
    int32 BroadReachRejected = 0;
    int32 GeneratedCandidates = 0;
    int32 AngleRejected = 0;
    int32 ElbowRejected = 0;
    int32 TimingRejected = 0;
    int32 SpeedRejected = 0;
    int32 ValidCandidates = 0;

    float BestTimeUntilContact = -1.f;
    float BestRequiredHandSpeed = -1.f;
    float BestRequiredBladeAngularSpeed = -1.f;
};

USTRUCT()
struct FIronboundParryCandidate
{
    GENERATED_BODY()

    FVector ContactPoint = FVector::ZeroVector;
    FVector IncomingBase = FVector::ZeroVector;
    FVector IncomingTip = FVector::ZeroVector;
    FVector DefenseBase = FVector::ZeroVector;
    FVector DefenseTip = FVector::ZeroVector;

    // Complete desired weapon pose. Blade direction alone leaves weapon roll
    // underdetermined, so the solver preserves the current weapon roll as the
    // minimum-rotation solution that aligns the current blade axis to the
    // candidate blade axis.
    FTransform RequiredWeaponTransform = FTransform::Identity;

    // Actual hand/socket transform implied by RequiredWeaponTransform and the
    // weapon definition's WeaponToHand attachment transform.
    FTransform RequiredHandTransform = FTransform::Identity;
    FVector RequiredHandPosition = FVector::ZeroVector;
    FVector RequiredElbowPosition = FVector::ZeroVector;

    float IncomingTime = 0.f;
    float TimeUntilContact = 0.f;
    float DefenderBladeFraction = 0.f;
    float IntersectionAngleDegrees = 0.f;
    float RequiredHandSpeed = 0.f;
    float RequiredBladeAngularSpeed = 0.f;

    // Pure pose displacement cost, retained for diagnostics.
    float MovementCost = TNumericLimits<float>::Max();

    // Final solver ranking. This includes pose displacement plus how much of
    // the defender's available movement budget the parry consumes.
    float SelectionCost = TNumericLimits<float>::Max();

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

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy")
    FName UpperArmBone = TEXT("upperarm_r");

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy")
    FName LowerArmBone = TEXT("lowerarm_r");

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy")
    FName HandBone = TEXT("hand_r");

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy", meta=(ClampMin="0.0"))
    float ArmReachMargin = 1.0f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy", meta=(ClampMin="4", ClampMax="64"))
    int32 ElbowCircleSamples = 16;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy", meta=(ClampMin="0.0"))
    float ElbowMovementCostWeight = 0.35f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MinimumElbowSideDot = 0.0f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Weapon", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MinParryBladeFraction = 0.20f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Weapon", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MaxParryBladeFraction = 0.90f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="2", ClampMax="32"))
    int32 IncomingBladeContactSamples = 5;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="2", ClampMax="16"))
    int32 DefenderBladeFractionSamples = 4;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="4", ClampMax="64"))
    int32 DefenseOrientationSamples = 12;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="2", ClampMax="16"))
    int32 DefenseCrossingAngleSamples = 5;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="0.0", ClampMax="90.0"))
    float MinimumIntersectionAngleDegrees = 30.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="0.0"))
    float TranslationCostWeight = 1.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="0.0"))
    float RotationCostWeight = 0.35f;

    // Timing limits. Incoming trajectory TimeSeconds is source-animation time.
    // Runtime timing is read from the attacker's active montage and converted
    // from montage-track time back into source-animation segment time.
    UPROPERTY(EditAnywhere, Category="Combat|Parry|Timing", meta=(ClampMin="0.0"))
    float ReactionDelaySeconds = 0.15f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Timing", meta=(ClampMin="1.0"))
    float MaxParryHandSpeed = 500.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Timing", meta=(ClampMin="1.0"))
    float MaxParryBladeAngularSpeed = 720.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Timing", meta=(ClampMin="0.001"))
    float MinimumTimeToContact = 0.03f;

    /*
     * Ranking only: candidates above the hard speed limits are still rejected
     * exactly as before. Among the survivors, prefer parries that consume less
     * of the defender's available hand/blade speed budget. Squaring the
     * utilization makes near-limit, last-moment parries increasingly costly
     * without introducing another arbitrary pass/fail timestamp.
     */
    UPROPERTY(EditAnywhere, Category="Combat|Parry|Timing", meta=(ClampMin="0.0"))
    float TimingPressureCostWeight = 100.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Debug")
    bool bDrawParryDebug = true;

private:
    UPROPERTY()
    TObjectPtr<USkeletalMeshComponent> FighterMesh;

    UPROPERTY()
    TObjectPtr<UIronboundCombatFocusComponent> CombatFocus;

    TWeakObjectPtr<AActor> ObservedAttacker;
    bool bObservedCommittedAttack = false;

    // Recognition/reaction state:
    // observing Commit is not enough. The defender first waits until the
    // attacker's source playback reaches the trajectory's automatically
    // derived ActiveStartTime (recognizable blade motion), then waits the
    // real ReactionDelaySeconds before solving the remaining trajectory.
    bool bAttackRecognized = false;
    bool bReactionReady = false;
    float RecognitionWorldTime = 0.f;

    mutable bool bDiagnosticsLoggedForObservedAttack = false;
    mutable FString LastParryEarlyExitReason;

    float UpperArmLength = 0.f;
    float ForearmLength = 0.f;
    float ArmLength = 0.f;

    mutable FIronboundParryDiagnostics LastDiagnostics;

    bool CalculateArmDimensions();
    void UpdateAttackTimingState();
    bool GetIncomingSourcePlaybackTime(float& OutSourceTime) const;
    float GetTimeUntilIncomingSample(float IncomingTime) const;
    void DrawDiagnosticsDebug() const;
    void LogDiagnosticsOnce() const;

    void DrawAnatomyDebug() const;
    void DrawParrySolutionDebug() const;

    bool GetCurrentWeaponGeometry(
        FTransform& OutCurrentWeaponTransform,
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

    bool FindBestParryCandidate(FIronboundParryCandidate& OutCandidate) const;

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
        const FTransform& CurrentWeaponTransform,
        const FVector& CurrentDefenseBase,
        const FVector& CurrentDefenseDirection,
        FIronboundParryCandidate& OutCandidate) const;
};
