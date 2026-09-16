#include "Combat/IronboundCombatAnimInstance.h"
#include "Combat/IronboundCombatFocusComponent.h"
#include "Combat/IronboundCombatExecutionComponent.h"
#include "Components/SkeletalMeshComponent.h"

void UIronboundCombatAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	const AActor* Owner = GetOwningActor();
	const auto* Focus = Owner ? Owner->FindComponentByClass<UIronboundCombatFocusComponent>() : nullptr;
	const AActor* Target = Focus ? Focus->GetCombatTarget() : nullptr;
	const auto* Execution = Owner ? Owner->FindComponentByClass<UIronboundCombatExecutionComponent>() : nullptr;
	if (Target)
	{
		const auto* Mesh = Target->FindComponentByClass<USkeletalMeshComponent>();
		CombatLookLocation = Mesh && Mesh->DoesSocketExist("head") ? Mesh->GetSocketLocation("head") : Target->GetActorLocation();
	}
	// The authored attack owns the neck/head. Ease gaze back in after playback, without altering the weapon arm.
	const float DesiredAlpha = Target && !(Execution && Execution->CanPlayAttack()) ? 1.f : 0.f;
	CombatLookAlpha = FMath::FInterpTo(CombatLookAlpha, DesiredAlpha, DeltaSeconds, 12.f);
}
