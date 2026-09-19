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
		Owner
			? Owner->FindComponentByClass<UIronboundCombatFocusComponent>()
			: nullptr;

	const AActor* Target =
		Focus
			? Focus->GetCombatTarget()
			: nullptr;

	const auto* Execution =
		Owner
			? Owner->FindComponentByClass<UIronboundCombatExecutionComponent>()
			: nullptr;

	const auto* Parry =
		Owner
			? Owner->FindComponentByClass<UIronboundParryComponent>()
			: nullptr;

	// Snapshot cached procedural-parry state for the AnimGraph.
	// The AnimInstance does not run the solver.
	ParryIKActive =
		Parry && Parry->HasActiveParryPose();

	ParryHandTarget =
		ParryIKActive
			? Parry->GetActiveParryHandTransform()
			: FTransform::Identity;

	ParryElbowTarget =
		ParryIKActive
			? Parry->GetActiveParryElbowPosition()
			: FVector::ZeroVector;

	// Control Rig operates in the skeletal mesh component/global space.
	// Keep the original world-space values above for diagnostics and expose
	// component-space equivalents specifically for the rig.
	if (USkeletalMeshComponent* AnimMesh = GetSkelMeshComponent())
	{
		const FTransform& MeshWorldTransform = AnimMesh->GetComponentTransform();

		ParryHandTargetComponentSpace =
			ParryIKActive
				? ParryHandTarget.GetRelativeTransform(MeshWorldTransform)
				: FTransform::Identity;

		ParryElbowTargetComponentSpace =
			ParryIKActive
				? MeshWorldTransform.InverseTransformPosition(ParryElbowTarget)
				: FVector::ZeroVector;
	}
	else
	{
		ParryHandTargetComponentSpace = FTransform::Identity;
		ParryElbowTargetComponentSpace = FVector::ZeroVector;
	}

	if (Target)
	{
		const auto* Mesh =
			Target->FindComponentByClass<USkeletalMeshComponent>();

		CombatLookLocation =
			Mesh && Mesh->DoesSocketExist("head")
				? Mesh->GetSocketLocation("head")
				: Target->GetActorLocation();
	}

	// Snapshot attack-alignment information for the AnimGraph.
	CombatIsAligning =
		Execution && Execution->IsAligning();

	CombatFacingDelta =
		CombatIsAligning
			? Execution->GetCombatFacingDelta()
			: 0.f;

	// The authored attack owns the neck/head.
	// Ease gaze back in after playback without altering the weapon arm.
	const float DesiredAlpha =
		Target && !(Execution && Execution->CanPlayAttack())
			? 1.f
			: 0.f;

	CombatLookAlpha =
		FMath::FInterpTo(
			CombatLookAlpha,
			DesiredAlpha,
			DeltaSeconds,
			12.f);
}
