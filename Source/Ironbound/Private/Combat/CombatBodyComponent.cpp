#include "Combat/CombatBodyComponent.h"

#include "Combat/CombatEquipmentComponent.h"
#include "Combat/WeaponDefinition.h"

#include "Combat/FighterComponent.h"

#include "Components/SkeletalMeshComponent.h"

#include "Components/StaticMeshComponent.h"

#include "PhysicsControlComponent.h"
#include "Components/ActorComponent.h"

#include "PhysicsEngine/PhysicsAsset.h"

#include "PhysicsEngine/SkeletalBodySetup.h"

#include "PhysicsEngine/BodyInstance.h"

#include "PhysicsEngine/PhysicsConstraintTemplate.h"

#include "PhysicsEngine/PhysicsConstraintComponent.h"



namespace

{

    FPhysicsControlData TrackingData(float Linear, float Angular, float VelocityMultiplier = 1.f)

    {

        FPhysicsControlData Data;

        Data.LinearStrength = Linear;

        Data.AngularStrength = Angular;

        Data.LinearDampingRatio = Data.AngularDampingRatio = 1.f;

        Data.LinearExtraDamping = Data.AngularExtraDamping = 0.f;

        Data.LinearTargetVelocityMultiplier = VelocityMultiplier;

        Data.AngularTargetVelocityMultiplier = VelocityMultiplier;

        Data.bUseSkeletalAnimation = true;

        Data.bUseAccelerationDriveMode = true;

        Data.bOnlyControlChildObject = true;

        return Data;

    }

}



UCombatBodyComponent::UCombatBodyComponent()

{

    PrimaryComponentTick.bCanEverTick = true;

    PrimaryComponentTick.TickGroup = TG_PrePhysics;



    PostPhysicsTick.bCanEverTick = true;

    PostPhysicsTick.TickGroup = TG_PostPhysics;

}

void UCombatBodyComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	USkeletalMeshComponent* Mesh = Owner ? Owner->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	UPhysicsControlComponent* Controls = Owner ? Owner->FindComponentByClass<UPhysicsControlComponent>() : nullptr;
	if (!InitializeBody(Mesh, Controls))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("CombatBody: automatic initialization failed for %s; check skeletal mesh, Physics Asset, and Physics Control component"),
			*GetNameSafe(Owner));
	}
}



void FCombatBodyPostPhysicsTick::ExecuteTick(

    float DeltaTime,

    ELevelTick TickType,

    ENamedThreads::Type CurrentThread,

    const FGraphEventRef& CompletionEvent)

{

    if (Target && TickType == LEVELTICK_All)

    {

        Target->MeasureTracking();

    }

}



void UCombatBodyComponent::RegisterComponentTickFunctions(bool bRegister)

{

    Super::RegisterComponentTickFunctions(bRegister);



    if (bRegister && GetOwner())

    {

        PostPhysicsTick.Target = this;

        PostPhysicsTick.RegisterTickFunction(GetOwner()->GetLevel());

    }

    else

    {

        PostPhysicsTick.UnRegisterTickFunction();

    }

}



bool UCombatBodyComponent::InitializeBody(

    USkeletalMeshComponent* Mesh,

    UPhysicsControlComponent* Controls)

{

    if (!Mesh || !Controls || !Mesh->GetPhysicsAsset())

    {

        return false;

    }



    FighterMesh = Mesh;

    PhysicsControls = Controls;



    AddTickPrerequisiteComponent(Controls);



    bReleased = false;

    bParryBraceActive = false;



    Controls->DestroyAllControlsAndBodyModifiers();



    UpperBodyBones.Reset();

    WeaponArmBones.Reset();

    WeaponWorldControls.Reset();

    WeaponParentControls.Reset();

    ReactionWorldControls.Reset();

    ReactionParentControls.Reset();



    for (const USkeletalBodySetup* Setup : Mesh->GetPhysicsAsset()->SkeletalBodySetups)

    {

        if (!Setup)

        {

            continue;

        }



        const FName Bone = Setup->BoneName;



        if (Bone != "spine_01" && !Mesh->BoneIsChildOf(Bone, "spine_01"))

        {

            continue;

        }



        const bool bWeaponArm =

            Bone == "clavicle_r" ||

            Mesh->BoneIsChildOf(Bone, "clavicle_r");



        (bWeaponArm ? WeaponArmBones : UpperBodyBones).Add(Bone);



        FPhysicsControlModifierData Modifier;

        Modifier.MovementType = EPhysicsMovementType::Simulated;

        Modifier.CollisionType =

            bWeaponArm

                ? ECollisionEnabled::NoCollision

                : ECollisionEnabled::QueryAndPhysics;

        Modifier.PhysicsBlendWeight = 1.f;



        Controls->CreateBodyModifier(

            Mesh,

            Bone,

            bWeaponArm ? "WeaponArm" : "ReactionBody",

            Modifier);

    }



    // IMPORTANT: retain the exact control names returned by Physics Control.

    WeaponWorldControls =

        Controls->CreateControlsFromSkeletalMesh(

            Mesh,

            WeaponArmBones,

            EPhysicsControlType::WorldSpace,

            TrackingData(WeaponTrackingStrength, WeaponTrackingStrength),

            "WeaponWorld");



    WeaponParentControls =

        Controls->CreateControlsFromSkeletalMesh(

            Mesh,

            WeaponArmBones,

            EPhysicsControlType::ParentSpace,

            TrackingData(0.f, WeaponTrackingStrength),

            "WeaponParent");



    ReactionWorldControls =

        Controls->CreateControlsFromSkeletalMesh(

            Mesh,

            UpperBodyBones,

            EPhysicsControlType::WorldSpace,

            TrackingData(60.f, 60.f),

            "ReactionWorld");



    ReactionParentControls =

        Controls->CreateControlsFromSkeletalMesh(

            Mesh,

            UpperBodyBones,

            EPhysicsControlType::ParentSpace,

            TrackingData(0.f, 40.f),

            "ReactionParent");



    return !WeaponArmBones.IsEmpty() &&

           !UpperBodyBones.IsEmpty() &&

           !WeaponWorldControls.IsEmpty() &&

           !WeaponParentControls.IsEmpty() &&

           !ReactionWorldControls.IsEmpty() &&

           !ReactionParentControls.IsEmpty();

}



void UCombatBodyComponent::UpdateJointLimits(bool bRestore)

{

    if (!FighterMesh || !PhysicsControls || !FighterMesh->GetPhysicsAsset())

    {

        return;

    }



    const UPhysicsAsset* Asset = FighterMesh->GetPhysicsAsset();



    for (int32 Index = 0; Index < Asset->ConstraintSetup.Num(); ++Index)

    {

        const UPhysicsConstraintTemplate* Template = Asset->ConstraintSetup[Index];

        FConstraintInstance* Joint = FighterMesh->GetConstraintInstanceByIndex(Index);



        if (!Template || !Joint)

        {

            continue;

        }



        const auto& Default = Template->DefaultInstance;



        if (!WeaponArmBones.Contains(Default.ConstraintBone1) &&

            !UpperBodyBones.Contains(Default.ConstraintBone1))

        {

            continue;

        }



        if (bRestore || !bFitJointLimitsToAnimation)

        {

            Joint->RestoreAngularLimitsToDefault(Default);

        }

        else

        {

            const FTransform Child = PhysicsControls->GetCachedBoneTransform(

                FighterMesh, Default.ConstraintBone1);

            const FTransform Parent = PhysicsControls->GetCachedBoneTransform(

                FighterMesh, Default.ConstraintBone2);



            Joint->WidenLimitsForDriveTarget(

                Child.GetRelativeTransform(Parent).GetRotation(), Default);

        }

    }

}



void UCombatBodyComponent::UpdateWeaponDrives()

{

    if (!PhysicsControls || bReleased)

    {

        return;

    }



    if (bParryBraceActive)

    {

        // During a parry the IK pose remains the intentional arm-pose authority.

        // Use only the world-space arm controls here so the arm is not being

        // driven simultaneously through two competing spaces.

        PhysicsControls->SetControlDatas(

            WeaponWorldControls,

            TrackingData(

                ParryWorldLinearStrength,

                ParryWorldAngularStrength,

                ParryVelocityMultiplier));



        PhysicsControls->SetControlDatas(

            WeaponParentControls,

            TrackingData(

                0.f,

                0.f,

                ParryVelocityMultiplier));

    }

    else

    {

        PhysicsControls->SetControlDatas(

            WeaponWorldControls,

            TrackingData(WeaponTrackingStrength, WeaponTrackingStrength));



        PhysicsControls->SetControlDatas(

            WeaponParentControls,

            TrackingData(0.f, WeaponTrackingStrength));

    }

}



void UCombatBodyComponent::SetWeaponTrackingStrength(float Strength)

{

    WeaponTrackingStrength = FMath::Clamp(Strength, 5.f, 80.f);



    if (!bParryBraceActive)

    {

        UpdateWeaponDrives();

    }

}



void UCombatBodyComponent::SetParryGripDrive(bool bEnabled)

{

    const auto* Equipment =

        GetOwner()

            ? GetOwner()->FindComponentByClass<UCombatEquipmentComponent>()

            : nullptr;



    UPhysicsConstraintComponent* Grip =

        Equipment ? Equipment->GetConstraint() : nullptr;



    if (!Grip)

    {

        UE_LOG(

            LogTemp,

            Warning,

            TEXT("ParryGrip [%s]: no weapon grip constraint"),

            *GetNameSafe(GetOwner()));

        return;

    }



    if (bEnabled)

    {

        /*

         * The sword stays a fully simulated rigid body.

         *

         * Linear grip DOFs remain locked: the sword should not translate

         * through the fighter's hand.

         *

         * Angular DOFs are made free so the SLERP angular drive becomes the

         * thing that resists rotation. This gives us a finite spring/damper

         * instead of a rigid weld.

         */

        Grip->SetLinearXLimit(ELinearConstraintMotion::LCM_Locked, 0.f);

        Grip->SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0.f);

        Grip->SetLinearZLimit(ELinearConstraintMotion::LCM_Locked, 0.f);



        Grip->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Free, 0.f);

        Grip->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Free, 0.f);

        Grip->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Free, 0.f);



        Grip->SetAngularDriveMode(EAngularDriveMode::SLERP);



        // Zero is the authored grip-relative orientation because the constraint

        // frames were established at equip time from HandWorld.

        Grip->SetAngularOrientationTarget(FRotator::ZeroRotator);

        Grip->SetAngularVelocityTarget(FVector::ZeroVector);



        Grip->SetOrientationDriveSLERP(true);

        Grip->SetAngularVelocityDriveSLERP(true);



        Grip->SetAngularDriveParams(

            ParryGripAngularStiffness,

            ParryGripAngularDamping,

            ParryGripMaxTorque);



        if (Equipment->GetWeapon())

        {

            Equipment->GetWeapon()->WakeAllRigidBodies();

        }



        UE_LOG(

            LogTemp,

            Log,

            TEXT("ParryGrip [%s]: ENABLE stiffness=%.0f damping=%.0f maxTorque=%.0f"),

            *GetNameSafe(GetOwner()),

            ParryGripAngularStiffness,

            ParryGripAngularDamping,

            ParryGripMaxTorque);

    }

    else

    {

        Grip->SetOrientationDriveSLERP(false);

        Grip->SetAngularVelocityDriveSLERP(false);

        Grip->SetAngularDriveParams(0.f, 0.f, 0.f);



        /*

         * Restore the normal equipped-weapon relationship.

         *

         * The existing project uses the constraint as the physical grip outside

         * the parry window. Returning the angular DOFs to Locked reproduces the

         * normal rigid grip rather than leaving the sword freely rotating after

         * the first parry.

         */

        Grip->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Locked, 0.f);

        Grip->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Locked, 0.f);

        Grip->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Locked, 0.f);



        UE_LOG(

            LogTemp,

            Log,

            TEXT("ParryGrip [%s]: DISABLE -> normal locked grip"),

            *GetNameSafe(GetOwner()));

    }

}



void UCombatBodyComponent::BeginParryBrace()

{

    if (bReleased || bParryBraceActive)

    {

        return;

    }



    bParryBraceActive = true;



    // First establish the compliant but very stiff simulated grip.

    SetParryGripDrive(true);



    // Then strengthen the physical arm that carries that grip.

    UpdateWeaponDrives();



    UE_LOG(

        LogTemp,

        Log,

        TEXT("ParryBrace [%s]: BEGIN world=(%.0f,%.0f) velocity=%.2f grip=(%.0f,%.0f,%.0f) controls=(%d,%d)"),

        *GetNameSafe(GetOwner()),

        ParryWorldLinearStrength,

        ParryWorldAngularStrength,

        ParryVelocityMultiplier,

        ParryGripAngularStiffness,

        ParryGripAngularDamping,

        ParryGripMaxTorque,

        WeaponWorldControls.Num(),

        WeaponParentControls.Num());

}



void UCombatBodyComponent::EndParryBrace()

{

    if (!bParryBraceActive)

    {

        return;

    }



    // Restore the ordinary weapon grip before weakening the arm drives.

    SetParryGripDrive(false);



    bParryBraceActive = false;



    if (!bReleased)

    {

        UpdateWeaponDrives();

    }



    UE_LOG(

        LogTemp,

        Log,

        TEXT("ParryBrace [%s]: END restore=%.0f"),

        *GetNameSafe(GetOwner()),

        WeaponTrackingStrength);

}



void UCombatBodyComponent::UpdateDrives()

{

    if (!PhysicsControls || bReleased)

    {

        return;

    }



    const float Recovery = 1.f - ReactionWeight;



    PhysicsControls->SetControlDatas(

        ReactionWorldControls,

        TrackingData(

            FMath::Lerp(0.f, 60.f, Recovery),

            FMath::Lerp(0.f, 60.f, Recovery)));



    PhysicsControls->SetControlDatas(

        ReactionParentControls,

        TrackingData(

            0.f,

            FMath::Lerp(0.f, 40.f, Recovery)));

}



void UCombatBodyComponent::ApplyHitReaction(

    UPrimitiveComponent* Weapon,

    const FHitResult& Hit)

{

    if (!FighterMesh || !Weapon)

    {

        return;

    }



    const auto* Focus = GetOwner()
        ? GetOwner()->FindComponentByClass<UFighterComponent>()
        : nullptr;

    const auto* Attacker = Weapon->GetOwner()
        ? Weapon->GetOwner()->FindComponentByClass<UFighterComponent>()
        : nullptr;



    if (!Focus || !Attacker || Focus->GetBattleTeamId() <= 0 || Attacker->GetBattleTeamId() <= 0 ||

        Focus->GetBattleTeamId() == Attacker->GetBattleTeamId())

    {

        return;

    }



    FName Bone = Hit.BoneName;

    if (!bReleased && !UpperBodyBones.Contains(Bone) && !WeaponArmBones.Contains(Bone))

    {

        Bone = "spine_03";

    }



    FBodyInstance* Body = FighterMesh->GetBodyInstance(Bone);

    if (!Body)

    {

        return;

    }



    FVector Direction = Weapon->GetPhysicsLinearVelocityAtPoint(Hit.ImpactPoint);

    const float Speed = Direction.Size();



    UE_LOG(

        LogTemp,

        Warning,

        TEXT("SWORD HIT SPEED = %.1f cm/s | Reaction speed = %.1f"),

        Speed,

        FMath::Clamp(Speed * 2.0f, 60.f, MaxReactionSpeed));



    if (!Direction.Normalize())

    {

        Direction = (GetOwner()->GetActorLocation() - Weapon->GetComponentLocation()).GetSafeNormal();

    }



    if (!bReleased)

    {

        ReactionTimeRemaining = FMath::Max(RecoveryDuration, 0.01f);

        ReactionWeight = 1.f;

        ++ReactionCount;

        UpdateDrives();

    }



    const FVector Impulse =

        Direction * FMath::Clamp(Speed * 2.0f, 60.f, MaxReactionSpeed) * Body->GetBodyMass();



    FighterMesh->AddImpulseAtLocation(Impulse, Hit.ImpactPoint, Bone);

}



void UCombatBodyComponent::ReleaseForDeath()

{

    // If death occurs while parrying, remove the grip drive first.

    if (bParryBraceActive)

    {

        SetParryGripDrive(false);

    }



    bReleased = true;

    bParryBraceActive = false;

    ReactionWeight = 0.f;

    ReactionTimeRemaining = 0.f;



    UpdateJointLimits(true);



    if (PhysicsControls)

    {

        PhysicsControls->DestroyAllControlsAndBodyModifiers();

    }

}



void UCombatBodyComponent::TickComponent(

    float DeltaTime,

    ELevelTick TickType,

    FActorComponentTickFunction* TickFunction)

{

    Super::TickComponent(DeltaTime, TickType, TickFunction);



    if (!PhysicsControls || !FighterMesh || bReleased)

    {

        return;

    }



    UpdateJointLimits(false);



    if (ReactionTimeRemaining > 0.f)

    {

        ReactionTimeRemaining = FMath::Max(0.f, ReactionTimeRemaining - DeltaTime);



        const float T = FMath::Clamp(

            ReactionTimeRemaining / FMath::Max(RecoveryDuration, 0.01f), 0.f, 1.f);



        ReactionWeight = FMath::SmoothStep(0.f, 0.8f, T);

        UpdateDrives();

    }

}



void UCombatBodyComponent::MeasureTracking()

{

    if (!PhysicsControls || !FighterMesh || bReleased)

    {

        return;

    }



    const auto* Equipment = GetOwner()->FindComponentByClass<UCombatEquipmentComponent>();

    if (!Equipment || !Equipment->bReady || !Equipment->Definition || !Equipment->GetWeapon())

    {

        return;

    }



    const FTransform AnimatedHand = PhysicsControls->GetCachedBoneTransform(

        FighterMesh, Equipment->GetHandBone());

    const FTransform ActualHand = FighterMesh->GetSocketTransform(Equipment->GetHandBone());



    HandTrackingError = FVector::Distance(AnimatedHand.GetLocation(), ActualHand.GetLocation());

    HandAngularTrackingError = FMath::RadiansToDegrees(

        AnimatedHand.GetRotation().AngularDistance(ActualHand.GetRotation()));



    const FVector IntendedTip =

        (Equipment->GetWeaponToHand() * AnimatedHand).TransformPosition(

            Equipment->Definition->BladeTip);

    const FVector ActualTip = Equipment->GetWeapon()->GetComponentTransform().TransformPosition(

        Equipment->Definition->BladeTip);



    WeaponTipTrackingError = FVector::Distance(IntendedTip, ActualTip);

    GripTipError = FVector::Distance(

        (Equipment->GetWeaponToHand() * ActualHand).TransformPosition(

            Equipment->Definition->BladeTip),

        ActualTip);

}



bool UCombatBodyComponent::BindExistingBody(
    USkeletalMeshComponent* Mesh,
    UPhysicsControlComponent* Controls)
{
    if (!Mesh || !Controls || !Mesh->GetPhysicsAsset())
    {
        return false;
    }

    FighterMesh = Mesh;
    PhysicsControls = Controls;

    AddTickPrerequisiteComponent(Controls);

    bReleased = false;
    bParryBraceActive = false;

    // The existing Blueprint setup owns creation of the RightArm controls.
    // CombatBody only references those existing named sets; it must not destroy
    // or recreate the pawn's Physics Control setup.
    WeaponWorldControls.Reset();
    WeaponParentControls.Reset();

    // Keep the set names here. UpdateWeaponDrives() applies data to the named
    // sets through SetControlData rather than requiring us to copy private
    // control-name arrays out of PhysicsControlComponent.
    UE_LOG(
        LogTemp,
        Log,
        TEXT("CombatBody [%s]: bound to existing RightArm Physics Control sets"),
        *GetNameSafe(GetOwner()));

    return true;
}
