#include "Combat/CombatEquipmentComponent.h"

#include "Combat/CombatExecutionComponent.h"
#include "Combat/CombatRigDefinition.h"
#include "Combat/FighterComponent.h"
#include "Combat/WeaponDefinition.h"

#include "Animation/AnimSequenceBase.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Ironbound.h"

void UCombatEquipmentComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner || !Definition)
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("Equipment: %s has no owner or weapon definition"), *GetNameSafe(Owner));
		return;
	}

	USkeletalMeshComponent* Mesh = Owner->FindComponentByClass<USkeletalMeshComponent>();
	UPhysicsConstraintComponent* WeaponConstraint = Owner->FindComponentByClass<UPhysicsConstraintComponent>();
	UStaticMeshComponent* WeaponComponent = nullptr;
	TArray<UStaticMeshComponent*> StaticMeshes;
	Owner->GetComponents<UStaticMeshComponent>(StaticMeshes);
	for (UStaticMeshComponent* Candidate : StaticMeshes)
	{
		if (Candidate && (!Definition->Mesh || Candidate->GetStaticMesh() == Definition->Mesh))
		{
			WeaponComponent = Candidate;
			break;
		}
	}

	if (!InitializeEquipment(Mesh, WeaponComponent, WeaponConstraint))
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("Equipment: automatic initialization failed for %s; check mesh, weapon, constraint, and definition"),
			*GetNameSafe(Owner));
	}
}

bool UCombatEquipmentComponent::InitializeEquipment(
	USkeletalMeshComponent* InFighterMesh,
	UStaticMeshComponent* InWeapon,
	UPhysicsConstraintComponent* InConstraint)
{
	FighterMesh = InFighterMesh;
	Weapon = InWeapon;
	Constraint = InConstraint;
	if (ActiveGripId.IsNone() && Definition && !Definition->Grips.IsEmpty())
	{
		ActiveGripId = Definition->Grips[0].GripId;
	}

	return EquipWeapon(Definition);
}

const FWeaponGrip* UCombatEquipmentComponent::GetActiveGrip() const
{
	if (!Definition || Definition->Grips.IsEmpty())
	{
		return nullptr;
	}

	if (!ActiveGripId.IsNone())
	{
		if (const FWeaponGrip* Grip = Definition->FindGrip(ActiveGripId))
		{
			return Grip;
		}
	}

	return &Definition->Grips[0];
}

FName UCombatEquipmentComponent::GetHandBone() const
{
	if (const FWeaponGrip* Grip = GetActiveGrip())
	{
		const UFighterComponent* Fighter = GetOwner()
			? GetOwner()->FindComponentByClass<UFighterComponent>() : nullptr;
		if (Fighter && Fighter->RigDefinition)
		{
			const FFighterArmRigProfile& Arm = Grip->PrimaryHandSide == EWeaponHandSide::Left
				? Fighter->RigDefinition->LeftArm : Fighter->RigDefinition->RightArm;
			if (!Arm.HandBone.IsNone())
			{
				return Arm.HandBone;
			}
		}
	}
	return Definition ? Definition->HandBone : NAME_None;
}

FName UCombatEquipmentComponent::GetUpperArmBone() const
{
	if (const FWeaponGrip* Grip = GetActiveGrip())
	{
		const UFighterComponent* Fighter = GetOwner()
			? GetOwner()->FindComponentByClass<UFighterComponent>() : nullptr;
		if (Fighter && Fighter->RigDefinition)
		{
			return (Grip->PrimaryHandSide == EWeaponHandSide::Left
				? Fighter->RigDefinition->LeftArm : Fighter->RigDefinition->RightArm).UpperArmBone;
		}
	}
	return NAME_None;
}

FName UCombatEquipmentComponent::GetLowerArmBone() const
{
	if (const FWeaponGrip* Grip = GetActiveGrip())
	{
		const UFighterComponent* Fighter = GetOwner()
			? GetOwner()->FindComponentByClass<UFighterComponent>() : nullptr;
		if (Fighter && Fighter->RigDefinition)
		{
			return (Grip->PrimaryHandSide == EWeaponHandSide::Left
				? Fighter->RigDefinition->LeftArm : Fighter->RigDefinition->RightArm).LowerArmBone;
		}
	}
	return NAME_None;
}

FName UCombatEquipmentComponent::GetSupportHandBone() const
{
	if (const FWeaponGrip* Grip = GetActiveGrip(); Grip && Grip->bUsesSupportHand)
	{
		const UFighterComponent* Fighter = GetOwner()
			? GetOwner()->FindComponentByClass<UFighterComponent>() : nullptr;
		if (Fighter && Fighter->RigDefinition)
		{
			return (Grip->SupportHandSide == EWeaponHandSide::Left
				? Fighter->RigDefinition->LeftArm : Fighter->RigDefinition->RightArm).HandBone;
		}
	}
	return NAME_None;
}

FTransform UCombatEquipmentComponent::GetWeaponToHand() const
{
	if (const FWeaponGrip* Grip = GetActiveGrip())
	{
		return Grip->WeaponToHand;
	}
	return Definition ? Definition->WeaponToHand : FTransform::Identity;
}

bool UCombatEquipmentComponent::EquipWeapon(
	UWeaponDefinition* NewDefinition)
{
	if (!FighterMesh ||
		!Weapon ||
		!Constraint ||
		!NewDefinition ||
		!NewDefinition->Mesh)
	{
		return false;
	}

	const FWeaponGrip* NewGrip = ActiveGripId.IsNone()
		? (NewDefinition->Grips.IsEmpty() ? nullptr : &NewDefinition->Grips[0])
		: NewDefinition->FindGrip(ActiveGripId);
	FName HandBone = NewDefinition->HandBone;
	if (NewGrip)
	{
		const UFighterComponent* Fighter = GetOwner()
			? GetOwner()->FindComponentByClass<UFighterComponent>() : nullptr;
		if (Fighter && Fighter->RigDefinition)
		{
			HandBone = (NewGrip->PrimaryHandSide == EWeaponHandSide::Left
				? Fighter->RigDefinition->LeftArm : Fighter->RigDefinition->RightArm).HandBone;
		}
	}
	if (HandBone.IsNone() || !FighterMesh->DoesSocketExist(HandBone))
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("Equipment: grip hand bone '%s' is not present on %s"),
			*HandBone.ToString(), *GetNameSafe(FighterMesh->GetSkeletalMeshAsset()));
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
	if (NewGrip)
	{
		ActiveGripId = NewGrip->GripId;
	}

	Weapon->SetStaticMesh(Definition->Mesh);
	Weapon->SetVisibility(true);

	const FTransform HandWorld =
		FighterMesh->GetSocketTransform(HandBone);

	const FTransform WeaponWorld =
		GetWeaponToHand() * HandWorld;

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
		HandBone,
		Weapon,
		NAME_None);

	Constraint->SetDisableCollision(true);

	if (const FBodyInstance* HandBody =
		FighterMesh->GetBodyInstance(HandBone))
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
			GetHandBone(),
			GetWeaponToHand(),
			Definition->BladeBase,
			Definition->BladeTip,
			OutTrajectory))
	{
		return false;
	}

	Trajectories.Add(Key, OutTrajectory);
	return true;
}
