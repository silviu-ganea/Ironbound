#include "Combat/CombatEquipmentComponent.h"

#include "Combat/CombatExecutionComponent.h"
#include "Combat/WeaponDefinition.h"

#include "Animation/AnimSequenceBase.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Ironbound.h"

bool UCombatEquipmentComponent::InitializeEquipment(
	USkeletalMeshComponent* InFighterMesh,
	UStaticMeshComponent* InWeapon,
	UPhysicsConstraintComponent* InConstraint)
{
	FighterMesh = InFighterMesh;
	Weapon = InWeapon;
	Constraint = InConstraint;

	return EquipWeapon(Definition);
}

bool UCombatEquipmentComponent::EquipWeapon(
	UWeaponDefinition* NewDefinition)
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
		GetOwner()->FindComponentByClass<UCombatExecutionComponent>())
	{
		if (Execution->IsCommitted())
		{
			return false;
		}

		Execution->CancelActiveExecutions();
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

bool UCombatEquipmentComponent::UnequipWeapon()
{
	if (auto* Execution =
		GetOwner()->FindComponentByClass<UCombatExecutionComponent>())
	{
		if (Execution->IsCommitted())
		{
			return false;
		}

		Execution->CancelActiveExecutions();
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

void UCombatEquipmentComponent::InvalidateTrajectories()
{
	Trajectories.Reset();
	++Revision;
}

bool UCombatEquipmentComponent::IsBodyContact(
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

	const UCombatEquipmentComponent* OtherEquipment =
		OtherActor->FindComponentByClass<UCombatEquipmentComponent>();

	return OtherEquipment &&
		   OtherEquipment->GetFighterMesh() == OtherComp;
}

bool UCombatEquipmentComponent::IsBladeContact(
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp) const
{
	if (!OtherActor ||
		!OtherComp ||
		OtherActor == GetOwner())
	{
		return false;
	}

	const UCombatEquipmentComponent* OtherEquipment =
		OtherActor->FindComponentByClass<UCombatEquipmentComponent>();

	return OtherEquipment &&
		   OtherEquipment->bReady &&
		   OtherEquipment->GetWeapon() == OtherComp;
}

void UCombatEquipmentComponent::NotifyBladeContact(
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

bool UCombatEquipmentComponent::GetTrajectory(
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

	if (!UCombatTrajectoryLibrary::BuildBladeTrajectoryWithGrip(
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
