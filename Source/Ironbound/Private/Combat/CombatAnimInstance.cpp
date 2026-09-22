#include "Combat/CombatAnimInstance.h"

#include "Combat/CombatExecutionComponent.h"
#include "Combat/CombatFocusComponent.h"
#include "Components/SkeletalMeshComponent.h"

void UCombatAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    const AActor* Owner = GetOwningActor();

    const auto* Focus =
        Owner ? Owner->FindComponentByClass<UCombatFocusComponent>() : nullptr;
    const AActor* Target = Focus ? Focus->GetCombatTarget() : nullptr;
    const auto* Execution =
        Owner ? Owner->FindComponentByClass<UCombatExecutionComponent>() : nullptr;

    // C++ owns the selected hand transform only. CCDIK determines the actual
    // elbow/shoulder/torso solution; no elbow target is exported. The active
    // reactive execution (procedural parry) publishes the hand target through
    // the execution component; the anim instance never talks to the executor
    // directly.
    ParryIKActive = Execution && Execution->GetActiveHandTarget(ParryHandTarget);
    if (!ParryIKActive)
    {
        ParryHandTarget = FTransform::Identity;
    }

    if (USkeletalMeshComponent* AnimMesh = GetSkelMeshComponent())
    {
        const FTransform& MeshWorldTransform = AnimMesh->GetComponentTransform();
        ParryHandTargetComponentSpace =
            ParryIKActive
                ? ParryHandTarget.GetRelativeTransform(MeshWorldTransform)
                : FTransform::Identity;
    }
    else
    {
        ParryHandTargetComponentSpace = FTransform::Identity;
    }

    if (Target)
    {
        const auto* Mesh = Target->FindComponentByClass<USkeletalMeshComponent>();
        CombatLookLocation =
            Mesh && Mesh->DoesSocketExist("head")
                ? Mesh->GetSocketLocation("head")
                : Target->GetActorLocation();
    }

    CombatIsAligning = Execution && Execution->IsAligning();
    CombatFacingDelta =
        CombatIsAligning ? Execution->GetCombatFacingDelta() : 0.f;

    const float DesiredAlpha =
        Target && !(Execution && Execution->CanPlayAttack()) ? 1.f : 0.f;

    CombatLookAlpha =
        FMath::FInterpTo(CombatLookAlpha, DesiredAlpha, DeltaSeconds, 12.f);
}
