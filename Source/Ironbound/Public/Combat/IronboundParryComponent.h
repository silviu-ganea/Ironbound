#pragma once



#include "CoreMinimal.h"

#include "Components/ActorComponent.h"

#include "IronboundParryComponent.generated.h"



class USkeletalMeshComponent;

class UIronboundCombatFocusComponent;

class UIronboundCombatBodyComponent;



UENUM(BlueprintType)

enum class EIronboundParryState : uint8

{

    Observing,

    Reacting,

    Moving,

    Holding

};



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

    int32 AnatomyRejected = 0;

    int32 WristRejected = 0;

    int32 BodyRejected = 0;

    int32 TimingRejected = 0;

    int32 SpeedRejected = 0;

    int32 ValidCandidates = 0;



    float FirstBodyIntersectionTime = -1.f;

    float BestBodyClearance = -1.f;

    float BestTimeUntilContact = -1.f;

    float BestRequiredHandSpeed = -1.f;

    float BestRequiredBladeAngularSpeed = -1.f;

    float BestQuality = -1.f;

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



    FTransform RequiredWeaponTransform = FTransform::Identity;

    FTransform RequiredHandTransform = FTransform::Identity;

    FVector RequiredHandPosition = FVector::ZeroVector;



    float IncomingTime = 0.f;

    float TimeUntilContact = 0.f;

    float DefenderBladeFraction = 0.f;

    float IncomingBladeFraction = 0.f;

    float IntersectionAngleDegrees = 0.f;

    float RequiredHandSpeed = 0.f;

    float RequiredBladeAngularSpeed = 0.f;

    float BodyClearance = -1.f;

    float Quality = -TNumericLimits<float>::Max();



    bool bValid = false;

};



USTRUCT()
struct FArmExtensionMetrics
{
    GENERATED_BODY()

    float Distance = 0.f;
    float MaxReach = 0.f;
    float ExtensionRatio = 0.f;
    float MinReach = 0.f;
    float UpperArmLength = 0.f;
    float ForearmLength = 0.f;
    float GeometricFreedom = 0.f; // |sin(joint angle)|: 0 at folded/extended singularities
};




USTRUCT()

struct FCandidateRawMetrics

{

    GENERATED_BODY()



    float TacticalQuality;      // 0-1, derived from crossing angles, timing, etc.

    float WristDeviationRadians; // 0-PI biomechanical metric

    float GeometricFreedom;     // 0-1 biomechanical metric

    float ExtensionRatio;       // shoulder-to-hand distance / maximum two-link reach

    bool bGeometricallyValid;   // Passes reach constraints

};



USTRUCT()

struct FParryPolicy

{

    GENERATED_BODY()



    float TacticalWeight = 0.7f;      // Weight for tactical quality in combined score

    float WristSharpness = 1.5f;     // Wrist deviation curve steepness  

    float FreedomSharpness = 2.0f;   // Geometric freedom curve steepness

    float QualityBandWidth = 0.95f;  // Candidate quality selection band (95% of best)

    float ParrySkill = 0.5f;         // 0.0 = low skill, 1.0 = high skill

};



UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))

class IRONBOUND_API UIronboundParryComponent : public UActorComponent

{

    GENERATED_BODY()



public:

    UIronboundParryComponent();



    UFUNCTION(BlueprintPure, Category="Combat|Parry")

    EIronboundParryState GetParryState() const { return ParryState; }



    UFUNCTION(BlueprintPure, Category="Combat|Parry")

    bool HasActiveParryPose() const { return bHasActiveParryCandidate; }



    // These are the EXECUTED targets, not the final locked targets.  The

    // Control Rig therefore follows a stable motion plan instead of snapping.

    UFUNCTION(BlueprintPure, Category="Combat|Parry")

    FTransform GetActiveParryHandTransform() const

    {

        return bHasActiveParryCandidate ? ExecutedHandTransform : FTransform::Identity;

    }





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



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy")

    FName PelvisBone = TEXT("pelvis");



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy", meta=(ClampMin="0.0"))

    float ArmReachMargin = 1.f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Skill", meta=(ClampMin="0.0", ClampMax="1.0"))

    float ParrySkill = 0.5f;         // 0.0 = low skill, 1.0 = high skill



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Policy", meta=(ClampMin="0.0", ClampMax="1.0"))

    float QualityBandWidth = 0.95f;  // Candidate quality selection band (95% of best)



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Policy", meta=(ClampMin="0.1"))

    float WristSharpness = 1.5f;     // Wrist deviation curve steepness



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Policy", meta=(ClampMin="0.1"))

    float FreedomSharpness = 2.0f;   // Geometric freedom curve steepness



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Policy", meta=(ClampMin="0.0", ClampMax="1.0"))

    float TacticalWeight = 0.7f;     // Weight for tactical quality in combined score



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Execution", meta=(ClampMin="0.1"))

    float PositionArrivalTolerance = 2.f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Debug")

    bool bDrawParryDebug = true;



    // Weapon sampling parameters

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Weapon", meta=(ClampMin="0.0", ClampMax="1.0"))

    float MinParryBladeFraction = 0.30f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Weapon", meta=(ClampMin="0.0", ClampMax="1.0"))

    float MaxParryBladeFraction = 0.78f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="2", ClampMax="16"))

    int32 IncomingBladeContactSamples = 7;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="0.0", ClampMax="1.0"))

    float MinimumIncomingBladeFraction = 0.65f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Pose", meta=(ClampMin="0.0"))

    float PreferredVisibleHandTravel = 30.f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="0.0", ClampMax="1.0"))

    float MaximumIncomingBladeFraction = 0.95f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="0.0", ClampMax="1.0"))

    float PreferredIncomingBladeFraction = 0.8f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="2", ClampMax="12"))

    int32 DefenderBladeFractionSamples = 5;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="8", ClampMax="64"))

    int32 DefenseOrientationSamples = 24;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="2", ClampMax="12"))

    int32 DefenseCrossingAngleSamples = 6;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="0.0", ClampMax="90.0"))

    float MinimumIntersectionAngleDegrees = 70.f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="0.0", ClampMax="90.0"))

    float PreferredIntersectionAngleDegrees = 90.f;



    // Quality weights

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver")

    float CrossingQualityWeight = 5.0f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver")

    float BladeCenterQualityWeight = 1.0f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver")

    float DramaticPoseQualityWeight = 3.0f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver")

    float IncomingTipContactQualityWeight = 3.0f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver")

    float BodyClearanceQualityWeight = 1.5f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="1.0"))

    float BodyClearancePreferenceDistance = 75.f;



    // Timing parameters

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Timing", meta=(ClampMin="0.0"))

    // One 60 Hz trajectory sample. This is only a "do not start a parry once
    // the blade is effectively on you" guard; the real lead-time floor is
    // MinimumTimeToContact. At 0.08 s it also removed the ~5 samples nearest
    // impact, which are the ones that are actually inside arm reach.
    float BodySafetyMarginSeconds = 0.02f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Timing", meta=(ClampMin="0.0"))

    float PerceptionDelaySeconds = 0.f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Timing", meta=(ClampMin="0.0"))

    float ReactionDelaySeconds = 0.f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Timing", meta=(ClampMin="1.0"))

    float MaxParryHandSpeed = 500.f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Timing", meta=(ClampMin="1.0"))

    float MaxParryBladeAngularSpeed = 720.f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Timing", meta=(ClampMin="0.001"))

    float MinimumTimeToContact = 0.03f;



    // Execution parameters

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Execution", meta=(ClampMin="1.0"))

    float ParryMovementSpeed = 240.f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Execution", meta=(ClampMin="1.0"))

    float ParryRotationSpeedDegrees = 900.f;



    UPROPERTY(EditAnywhere, Category="Combat|Parry|Execution", meta=(ClampMin="0.0"))

    float LeadHoldSeconds = 0.06f;



    bool FindBestParryCandidate(FIronboundParryCandidate& OutCandidate) const;



    bool EvaluateCandidate(

        const FVector& ShoulderWorld,

        const FVector& IncomingBaseWorld,

        const FVector& IncomingTipWorld,

        const FVector& IncomingContactWorld,

        const FVector& IncomingDirection,

        float IncomingTime,

        float IncomingBladeFraction,

        const FVector& DefenseDirection,

        float DefenderBladeFraction,

        float BladeLength,

        const FTransform& CurrentWeaponTransform,

        const FVector& CurrentDefenseDirection,

        FIronboundParryCandidate& OutCandidate) const;



private:

    UPROPERTY()

    TObjectPtr<USkeletalMeshComponent> FighterMesh;



    UPROPERTY()

    TObjectPtr<UIronboundCombatFocusComponent> CombatFocus;



    UPROPERTY()

    TObjectPtr<UIronboundCombatBodyComponent> CombatBody;



    TWeakObjectPtr<AActor> ObservedAttacker;



    bool bObservedCommittedAttack = false;

    bool bAttackRecognized = false;

    bool bReactionReady = false;

    float ObservationWorldTime = 0.f;

    float RecognitionWorldTime = 0.f;



    mutable bool bDiagnosticsLoggedForObservedAttack = false;

    mutable FString LastParryEarlyExitReason;



    float UpperArmLength = 0.f;

    float ForearmLength = 0.f;

    float ArmLength = 0.f;



    mutable FIronboundParryDiagnostics LastDiagnostics;



    FIronboundParryCandidate ActiveParryCandidate;

    bool bHasActiveParryCandidate = false;

    EIronboundParryState ParryState = EIronboundParryState::Observing;



    FTransform ExecutedHandTransform = FTransform::Identity;



    void ResetParryAction();

    void InitializeExecutionPose();

    void UpdateExecutionPose(float DeltaTime);



    bool CalculateArmDimensions();

    void UpdateAttackTimingState();

    bool GetIncomingSourcePlaybackTime(float& OutSourceTime) const;

    float GetTimeUntilIncomingSample(float IncomingTime) const;



    // Mathematical comfort functions

    FArmExtensionMetrics CalculateArmExtensionMetrics(

        const FVector& Shoulder,

        const FVector& Hand,

        float UpperArmLengthParam,

        float ForearmLengthParam) const;



    float CalculateMinimumWristDeviationAnalytical(

        const FVector& Shoulder,

        const FVector& Hand,

        const FVector& NeutralForearmDir,

        float UpperArmLength,

        float ForearmLength) const;



    FVector CalculateReferenceForearmDirection() const;

    FTransform CalculateNeutralWristRelationship() const;

    FVector CalculateNeutralForearmDirection(const FTransform& RequiredHandTransform) const;





    float ApplyPolicyToRawMetrics(const FCandidateRawMetrics& Raw, const FParryPolicy& Policy) const;



    void SelectComfortableCandidate(

        const TArray<FIronboundParryCandidate>& Candidates,

        FIronboundParryCandidate& OutSelected,

        uint32 SelectionSeed = 0) const;



    void SelectComfortableCandidate(

        const TArray<FIronboundParryCandidate>& Candidates,

        FIronboundParryCandidate& OutSelected,

        uint32 SelectionSeed,

        const FParryPolicy& Policy) const;



    void DrawAnatomyDebug() const;

    void DrawParrySolutionDebug() const;

    void DrawDiagnosticsDebug() const;



    void LogDiagnosticsOnce() const;





};
