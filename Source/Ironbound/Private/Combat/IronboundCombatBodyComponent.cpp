#include "Combat/IronboundCombatBodyComponent.h"
#include "Combat/IronboundEquipmentComponent.h"
#include "Combat/IronboundCombatFocusComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsControlComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"

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

UIronboundCombatBodyComponent::UIronboundCombatBodyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	PostPhysicsTick.bCanEverTick = true;
	PostPhysicsTick.TickGroup = TG_PostPhysics;
}

void FIronboundBodyPostPhysicsTick::ExecuteTick(
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

void UIronboundCombatBodyComponent::RegisterComponentTickFunctions(bool bRegister)
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

bool UIronboundCombatBodyComponent::InitializeBody(
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

void UIronboundCombatBodyComponent::UpdateJointLimits(bool bRestore)
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

void UIronboundCombatBodyComponent::UpdateWeaponDrives()
{
	if (!PhysicsControls || bReleased)
	{
		return;
	}

	if (bParryBraceActive)
	{
		// The parry solver selects the sword contact and derives the hand/elbow
		// pose. Control Rig/IK is the only intentional arm-pose authority. The
		// constrained weapon follows hand_r, while this single world-space
		// Physics Control only makes the simulated arm track that authored pose.
		// The parent-space control is disabled here; driving the same weapon arm
		// through both spaces was a second wrist authority and could twist the
		// hand into the fighter.
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

void UIronboundCombatBodyComponent::SetWeaponTrackingStrength(float Strength)
{
	WeaponTrackingStrength = FMath::Clamp(Strength, 5.f, 80.f);

	if (!bParryBraceActive)
	{
		UpdateWeaponDrives();
	}
}

void UIronboundCombatBodyComponent::BeginParryBrace()
{
	if (bReleased || bParryBraceActive)
	{
		return;
	}

	bParryBraceActive = true;
	UpdateWeaponDrives();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("ParryBrace [%s]: BEGIN parentAngular=%.0f velocity=%.2f controls=(%d,%d)"),
		*GetNameSafe(GetOwner()),
		ParryParentAngularStrength,
		ParryVelocityMultiplier,
		WeaponWorldControls.Num(),
		WeaponParentControls.Num());
}

void UIronboundCombatBodyComponent::EndParryBrace()
{
	if (!bParryBraceActive)
	{
		return;
	}

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

void UIronboundCombatBodyComponent::UpdateDrives()
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

void UIronboundCombatBodyComponent::ApplyHitReaction(
	UPrimitiveComponent* Weapon,
	const FHitResult& Hit)
{
	if (!FighterMesh || !Weapon)
	{
		return;
	}

	const auto* Focus = GetOwner()->FindComponentByClass<UIronboundCombatFocusComponent>();
	const auto* Attacker = Weapon->GetOwner()
		? Weapon->GetOwner()->FindComponentByClass<UIronboundCombatFocusComponent>()
		: nullptr;

	if (!Focus || !Attacker || Focus->Team <= 0 || Attacker->Team <= 0 ||
		Focus->Team == Attacker->Team)
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

void UIronboundCombatBodyComponent::ReleaseForDeath()
{
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

void UIronboundCombatBodyComponent::TickComponent(
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

void UIronboundCombatBodyComponent::MeasureTracking()
{
	if (!PhysicsControls || !FighterMesh || bReleased)
	{
		return;
	}

	const auto* Equipment = GetOwner()->FindComponentByClass<UIronboundEquipmentComponent>();
	if (!Equipment || !Equipment->bReady || !Equipment->Definition || !Equipment->GetWeapon())
	{
		return;
	}

	const FTransform AnimatedHand = PhysicsControls->GetCachedBoneTransform(
		FighterMesh, Equipment->Definition->HandBone);
	const FTransform ActualHand = FighterMesh->GetSocketTransform(Equipment->Definition->HandBone);

	HandTrackingError = FVector::Distance(AnimatedHand.GetLocation(), ActualHand.GetLocation());
	HandAngularTrackingError = FMath::RadiansToDegrees(
		AnimatedHand.GetRotation().AngularDistance(ActualHand.GetRotation()));

	const FVector IntendedTip =
		(Equipment->Definition->WeaponToHand * AnimatedHand).TransformPosition(
			Equipment->Definition->BladeTip);
	const FVector ActualTip = Equipment->GetWeapon()->GetComponentTransform().TransformPosition(
		Equipment->Definition->BladeTip);

	WeaponTipTrackingError = FVector::Distance(IntendedTip, ActualTip);
	GripTipError = FVector::Distance(
		(Equipment->Definition->WeaponToHand * ActualHand).TransformPosition(
			Equipment->Definition->BladeTip),
		ActualTip);
}
