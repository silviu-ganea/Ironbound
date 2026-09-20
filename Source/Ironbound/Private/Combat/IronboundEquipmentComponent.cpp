#include "Combat/IronboundEquipmentComponent.h"

#include "Combat/IronboundCombatExecutionComponent.h"

#include "Animation/AnimSequenceBase.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Ironbound.h"

bool UIronboundEquipmentComponent::InitializeEquipment(
	USkeletalMeshComponent* InFighterMesh,
	UStaticMeshComponent* InWeapon,
	UPhysicsConstraintComponent* InConstraint)
{
	FighterMesh = InFighterMesh;
	Weapon = InWeapon;
	Constraint = InConstraint;

	return EquipWeapon(Definition);
}

bool UIronboundEquipmentComponent::EquipWeapon(
	UIronboundWeaponDefinition* NewDefinition)
{
	if (!FighterMesh ||
		!Weapon ||
		!Constraint ||
		!NewDefinition ||
		!NewDefinition->Mesh ||
		!FighterMesh->DoesSocketExist(NewDefinition->HandBone))
	{
		return false;
	}

	if (auto* Execution =
		GetOwner()->FindComponentByClass<UIronboundCombatExecutionComponent>())
	{
		if (Execution->IsCommitted())
		{
			return false;
		}

		Execution->CancelAttack();
	}

	Constraint->BreakConstraint();

	Weapon->SetSimulatePhysics(false);

	Weapon->DetachFromComponent(
		FDetachmentTransformRules::KeepWorldTransform);

	Definition = NewDefinition;

	Weapon->SetStaticMesh(Definition->Mesh);
	Weapon->SetVisibility(true);

	const FTransform HandWorld =
		FighterMesh->GetSocketTransform(Definition->HandBone);

	const FTransform WeaponWorld =
		Definition->WeaponToHand * HandWorld;

	Weapon->SetWorldTransform(
		WeaponWorld,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);

	Weapon->SetCollisionEnabled(
		ECollisionEnabled::QueryAndPhysics);

	Weapon->SetMassOverrideInKg(
		NAME_None,
		Definition->MassKg,
		true);

	Weapon->SetSimulatePhysics(true);

	Weapon->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Weapon->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);

	Constraint->SetWorldTransform(
		HandWorld,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);

	Constraint->SetConstrainedComponents(
		FighterMesh,
		Definition->HandBone,
		Weapon,
		NAME_None);

	Constraint->SetDisableCollision(true);

	if (const FBodyInstance* HandBody =
		FighterMesh->GetBodyInstance(Definition->HandBone))
	{
		Constraint->SetConstraintReferenceFrame(
			EConstraintFrame::Frame1,
			HandWorld.GetRelativeTransform(
				HandBody->GetUnrealWorldTransform()));
	}

	Constraint->SetConstraintReferenceFrame(
		EConstraintFrame::Frame2,
		HandWorld.GetRelativeTransform(WeaponWorld));

	Weapon->WakeAllRigidBodies();

	bReady = true;

	InvalidateTrajectories();

	UE_LOG(
		LogIronboundCombat,
		Log,
		TEXT("Equipment: %s equipped %s revision %d"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(Definition),
		Revision);

	return true;
}

bool UIronboundEquipmentComponent::UnequipWeapon()
{
	if (auto* Execution =
		GetOwner()->FindComponentByClass<UIronboundCombatExecutionComponent>())
	{
		if (Execution->IsCommitted())
		{
			return false;
		}

		Execution->CancelAttack();
	}

	if (Constraint)
	{
		Constraint->BreakConstraint();
	}

	if (Weapon)
	{
		Weapon->SetSimulatePhysics(false);
		Weapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Weapon->SetVisibility(false);
	}

	bReady = false;
	Definition = nullptr;

	InvalidateTrajectories();

	return true;
}

void UIronboundEquipmentComponent::InvalidateTrajectories()
{
	Trajectories.Reset();
	++Revision;
}

bool UIronboundEquipmentComponent::IsBodyContact(
	UPrimitiveComponent* OtherComp) const
{
	if (!OtherComp)
	{
		return false;
	}

	AActor* OtherActor = OtherComp->GetOwner();
	if (!OtherActor || OtherActor == GetOwner())
	{
		return false;
	}

	const UIronboundEquipmentComponent* OtherEquipment =
		OtherActor->FindComponentByClass<UIronboundEquipmentComponent>();

	return OtherEquipment &&
		   OtherEquipment->GetFighterMesh() == OtherComp;
}

bool UIronboundEquipmentComponent::IsBladeContact(
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp) const
{
	if (!OtherActor ||
		!OtherComp ||
		OtherActor == GetOwner())
	{
		return false;
	}

	const UIronboundEquipmentComponent* OtherEquipment =
		OtherActor->FindComponentByClass<UIronboundEquipmentComponent>();

	return OtherEquipment &&
		   OtherEquipment->bReady &&
		   OtherEquipment->GetWeapon() == OtherComp;
}

void UIronboundEquipmentComponent::NotifyBladeContact(
	AActor* OtherActor,
	const FHitResult& Hit)
{
	if (!OtherActor)
	{
		return;
	}

	UE_LOG(
		LogIronboundCombat,
		Log,
		TEXT("PARRY BLADE CONTACT: %s blade hit %s blade at %s"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(OtherActor),
		*Hit.ImpactPoint.ToCompactString());
}

bool UIronboundEquipmentComponent::GetTrajectory(
	UAnimSequenceBase* Sequence,
	FBladeTrajectory& OutTrajectory)
{
	OutTrajectory = FBladeTrajectory();

	if (!bReady ||
		!Definition ||
		!Sequence ||
		!FighterMesh ||
		!Weapon)
	{
		return false;
	}

	const FString Key =
		FString::Printf(
			TEXT("%s|%s|%s"),
			*Sequence->GetPathName(),
			*GetPathNameSafe(
				FighterMesh->GetSkeletalMeshAsset()),
			*FighterMesh->GetRelativeTransform().ToString());

	if (const FBladeTrajectory* Cached = Trajectories.Find(Key))
	{
		OutTrajectory = *Cached;
		return OutTrajectory.bValid;
	}

	if (!UIronboundTrajectoryLibrary::BuildBladeTrajectoryWithGrip(
			FighterMesh,
			Weapon,
			Sequence,
			Definition->HandBone,
			Definition->WeaponToHand,
			Definition->BladeBase,
			Definition->BladeTip,
			OutTrajectory))
	{
		return false;
	}

	Trajectories.Add(Key, OutTrajectory);
	return true;
}
