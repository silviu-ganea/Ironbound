#include "Combat/IronboundEquipmentComponent.h"

#include "Combat/IronboundCombatExecutionComponent.h"
#include "Animation/AnimSequenceBase.h"
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

bool UIronboundEquipmentComponent::EquipWeapon(UIronboundWeaponDefinition* NewDefinition)
{
    if (!FighterMesh || !Weapon || !Constraint || !NewDefinition ||
        !NewDefinition->Mesh || !FighterMesh->DoesSocketExist(NewDefinition->HandBone))
    {
        return false;
    }

    if (auto* Execution =
        GetOwner()->FindComponentByClass<UIronboundCombatExecutionComponent>())
    {
        if (Execution->IsCommitted()) return false;
        Execution->CancelAttack();
    }

    Constraint->BreakConstraint();
    Weapon->SetSimulatePhysics(false);
    Weapon->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

    Definition = NewDefinition;
    Weapon->SetStaticMesh(Definition->Mesh);
    Weapon->SetVisibility(true);

    const FTransform HandWorld =
        FighterMesh->GetSocketTransform(Definition->HandBone);
    const FTransform WeaponWorld =
        Definition->WeaponToHand * HandWorld;

    Weapon->SetWorldTransform(
        WeaponWorld, false, nullptr, ETeleportType::TeleportPhysics);

    Weapon->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Weapon->SetMassOverrideInKg(NAME_None, Definition->MassKg, true);

    // Fast sword-on-sword contacts need CCD. Without it two thin blades can
    // cross between Chaos steps without generating a contact.
    Weapon->SetUseCCD(true);

    // Per-instance self-ignore: do not let our own sword get caught in our
    // torso/legs/head. Enemy bodies and enemy weapons are unaffected.
    Weapon->IgnoreActorWhenMoving(GetOwner(), true);

    Weapon->SetSimulatePhysics(true);
    Weapon->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Weapon->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);

    Constraint->SetWorldTransform(
        HandWorld, false, nullptr, ETeleportType::TeleportPhysics);

    // Make the grip rotationally rigid. The old Blueprint profile had locked
    // angular limits configured as SOFT limits (50/5) with no angular drive.
    Constraint->SetAngularDriveMode(EAngularDriveMode::SLERP);
    // UE 5.8 exposes the angular motion setters on FConstraintInstance,
    // not directly on UPhysicsConstraintComponent.
    Constraint->ConstraintInstance.SetAngularSwing1Motion(EAngularConstraintMotion::ACM_Locked);
    Constraint->ConstraintInstance.SetAngularSwing2Motion(EAngularConstraintMotion::ACM_Locked);
    Constraint->ConstraintInstance.SetAngularTwistMotion(EAngularConstraintMotion::ACM_Locked);

    Constraint->ConstraintInstance.ProfileInstance.ConeLimit.bSoftConstraint = false;
    Constraint->ConstraintInstance.ProfileInstance.TwistLimit.bSoftConstraint = false;

    Constraint->SetAngularOrientationTarget(FRotator::ZeroRotator);
    Constraint->SetAngularDriveParams(
        Definition->GripAngularStiffness,
        Definition->GripAngularDamping,
        0.f);
    Constraint->SetAngularOrientationDrive(true, true);
    Constraint->SetAngularVelocityDrive(true, true);
    Constraint->SetAngularVelocityTarget(FVector::ZeroVector);

    Constraint->SetConstrainedComponents(
        FighterMesh, Definition->HandBone, Weapon, NAME_None);
    Constraint->SetDisableCollision(true);

    if (const FBodyInstance* HandBody =
        FighterMesh->GetBodyInstance(Definition->HandBone))
    {
        Constraint->SetConstraintReferenceFrame(
            EConstraintFrame::Frame1,
            HandWorld.GetRelativeTransform(HandBody->GetUnrealWorldTransform()));
    }

    Constraint->SetConstraintReferenceFrame(
        EConstraintFrame::Frame2,
        HandWorld.GetRelativeTransform(WeaponWorld));

    Weapon->WakeAllRigidBodies();

    bReady = true;
    InvalidateTrajectories();

    UE_LOG(
        LogIronboundCombat, Log,
        TEXT("Equipment: %s equipped %s revision %d | rigid grip %.0f/%.0f | CCD ON | self collision ignored"),
        *GetNameSafe(GetOwner()), *GetNameSafe(Definition), Revision,
        Definition->GripAngularStiffness, Definition->GripAngularDamping);

    return true;
}

bool UIronboundEquipmentComponent::UnequipWeapon()
{
    if (auto* Execution =
        GetOwner()->FindComponentByClass<UIronboundCombatExecutionComponent>())
    {
        if (Execution->IsCommitted()) return false;
        Execution->CancelAttack();
    }

    if (Constraint) Constraint->BreakConstraint();

    if (Weapon)
    {
        Weapon->IgnoreActorWhenMoving(GetOwner(), false);
        Weapon->SetUseCCD(false);
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

bool UIronboundEquipmentComponent::GetTrajectory(
    UAnimSequenceBase* Sequence,
    FBladeTrajectory& OutTrajectory)
{
    OutTrajectory = FBladeTrajectory();

    if (!bReady || !Definition || !Sequence || !FighterMesh || !Weapon)
        return false;

    const FString Key = FString::Printf(
        TEXT("%s|%s|%s"),
        *Sequence->GetPathName(),
        *GetPathNameSafe(FighterMesh->GetSkeletalMeshAsset()),
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
