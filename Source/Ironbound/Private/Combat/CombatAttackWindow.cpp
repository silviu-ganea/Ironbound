#include "Combat/CombatAttackWindow.h"
#include "Combat/CombatEquipmentComponent.h"
#include "Combat/CombatExecutionComponent.h"
#include "Animation/AnimNotifyQueue.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Ironbound.h"

namespace
{
	void LogAttackWindow(
		const TCHAR* EventName,
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		float TotalDuration,
		const FAnimNotifyEventReference& EventReference)
	{
		AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
		const FAnimNotifyEvent* Notify = EventReference.GetNotify();
		UCombatEquipmentComponent* Equipment = Owner
			? Owner->FindComponentByClass<UCombatEquipmentComponent>() : nullptr;
		UStaticMeshComponent* Weapon = Equipment ? Equipment->GetWeapon() : nullptr;
		const FTransform WeaponTransform = Weapon
			? Weapon->GetComponentTransform() : FTransform::Identity;
		const UWeaponDefinition* Definition = Equipment ? Equipment->Definition : nullptr;
		const FVector BladeBase = Definition
			? WeaponTransform.TransformPosition(Definition->BladeBase) : FVector::ZeroVector;
		const FVector BladeTip = Definition
			? WeaponTransform.TransformPosition(Definition->BladeTip) : FVector::ZeroVector;

		UE_LOG(LogIronboundCombat, Log,
			TEXT("[COMBAT] WINDOW_NOTIFY Event=%s owner=%s source=%s sourceTime=%.3f sourceEnd=%.3f totalDuration=%.3f weapon=%s weaponLoc=%s weaponRot=%s actualBladeBase=%s actualBladeTip=%s"),
			EventName, *GetNameSafe(Owner),
			Animation ? *Animation->GetPathName() : TEXT("None"),
			Notify ? Notify->GetTriggerTime() : -1.f,
			Notify ? Notify->GetEndTriggerTime() : -1.f,
			TotalDuration, *GetNameSafe(Weapon),
			*WeaponTransform.GetLocation().ToCompactString(),
			*WeaponTransform.Rotator().ToCompactString(),
			*BladeBase.ToCompactString(), *BladeTip.ToCompactString());
	}
}

void UCombatAttackWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	LogAttackWindow(TEXT("Begin"), MeshComp, Animation, TotalDuration, EventReference);
	if (MeshComp && MeshComp->GetOwner())
		if (auto* Execution = MeshComp->GetOwner()->FindComponentByClass<UCombatExecutionComponent>())
			Execution->SetStrikeWindowOpen(true);
}

void UCombatAttackWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	const FAnimNotifyEvent* Notify = EventReference.GetNotify();
	LogAttackWindow(TEXT("End"), MeshComp, Animation,
		Notify ? Notify->GetDuration() : -1.f, EventReference);
	if (MeshComp && MeshComp->GetOwner())
		if (auto* Execution = MeshComp->GetOwner()->FindComponentByClass<UCombatExecutionComponent>())
			Execution->SetStrikeWindowOpen(false);
}
