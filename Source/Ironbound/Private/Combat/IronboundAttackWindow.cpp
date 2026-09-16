#include "Combat/IronboundAttackWindow.h"
#include "Combat/IronboundCombatExecutionComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

void UIronboundAttackWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	if (MeshComp && MeshComp->GetOwner())
		if (auto* Execution = MeshComp->GetOwner()->FindComponentByClass<UIronboundCombatExecutionComponent>())
			Execution->bStrikeWindowOpen = Execution->Phase == EIronboundAttackPhase::Committed;
}

void UIronboundAttackWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	if (MeshComp && MeshComp->GetOwner())
		if (auto* Execution = MeshComp->GetOwner()->FindComponentByClass<UIronboundCombatExecutionComponent>())
			Execution->bStrikeWindowOpen = false;
}
