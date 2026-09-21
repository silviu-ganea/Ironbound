#include "Combat/IronboundCombatAnimInstance.h"
#include "Combat/IronboundCombatFocusComponent.h"
#include "Combat/IronboundCombatExecutionComponent.h"
#include "Combat/IronboundParryComponent.h"
#include "Components/SkeletalMeshComponent.h"

void UIronboundCombatAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    const AActor* Owner = GetOwningActor();

    const auto* Focus =
        Owner ? Owner->FindComponentByClass<UIronboundCombatFocusComponent>() : nullptr;
    const AActor* Target = Focus ? Focus->GetCombatTarget() : nullptr;
    const auto* Execution =
        Owner ? Owner->FindComponentByClass<UIronboundCombatExecutionComponent>() : nullptr;
    const auto* Parry =
        Owner ? Owner->FindComponentByClass<UIronboundParryComponent>() : nullptr;

    // C++ owns the selected hand transform only. CCDIK determines the actual
    // elbow/shoulder/torso solution; no elbow target is exported.
    ParryIKActive = Parry && Parry->HasActiveParryPose();
    ParryHandTarget =
        ParryIKActive ? Parry->GetActiveParryHandTransform() : FTransform::Identity;

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
