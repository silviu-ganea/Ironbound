#include "Combat/CombatAttackWindow.h"
#include "Combat/CombatExecutionComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

void UCombatAttackWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	if (MeshComp && MeshComp->GetOwner())
		if (auto* Execution = MeshComp->GetOwner()->FindComponentByClass<UCombatExecutionComponent>())
			Execution->SetStrikeWindowOpen(true);
}

void UCombatAttackWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	if (MeshComp && MeshComp->GetOwner())
		if (auto* Execution = MeshComp->GetOwner()->FindComponentByClass<UCombatExecutionComponent>())
			Execution->SetStrikeWindowOpen(false);
}
