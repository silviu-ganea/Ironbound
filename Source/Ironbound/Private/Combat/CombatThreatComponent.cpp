#include "Combat/CombatThreatComponent.h"

#include "Combat/CombatBodyComponent.h"
#include "Combat/CombatEquipmentComponent.h"
#include "Combat/CombatExecutionComponent.h"
#include "Combat/CombatFocusComponent.h"
#include "Combat/CombatBodyProbes.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimCompositeBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Ironbound.h"

UCombatThreatComponent::UCombatThreatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Same tick group the old parry observation used, so measurement ordering
	// relative to anim updates is preserved.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UCombatThreatComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Cache sibling components.
	if (!Focus)
	{
		Focus = GetOwner() ? GetOwner()->FindComponentByClass<UCombatFocusComponent>() : nullptr;
	}
	if (!Equipment)
	{
		Equipment = GetOwner() ? GetOwner()->FindComponentByClass<UCombatEquipmentComponent>() : nullptr;
	}

	// ============================================================
	// TEMPORARY single threat source (isolated).
	//
	// Threats are discovered from the focus target's committed execution.
	// This preserves current functionality with the smallest change; the
	// model, bookkeeping and API below are already attacker-independent, so
	// discovering threats from other attackers only extends the discovery
	// loop. Do NOT treat "one focus target = all threats" as architectural.
	// ============================================================
	TArray<AActor*> Attackers;
	TArray<FCombatCommittedStrike> Strikes;
	DiscoverThreatSources(Attackers, Strikes);

	// Forget bookkeeping for attackers no longer observed.
	TArray<TWeakObjectPtr<AActor>> StaleKeys;
	for (const TPair<TWeakObjectPtr<AActor>, FThreatObservation>& Pair : Observations)
	{
		if (!Pair.Key.IsValid())
		{
			StaleKeys.Add(Pair.Key);
			continue;
		}

		const bool bStillObserved = AttacksArrayContains(Attackers, Pair.Key.Get());
		if (!bStillObserved)
		{
			StaleKeys.Add(Pair.Key);
		}
	}
	for (const TWeakObjectPtr<AActor>& Key : StaleKeys)
	{
		Observations.Remove(Key);
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	// Recognition bookkeeping per attacker (objective; no reaction timing).
	for (const TWeakObjectPtr<AActor>& Attacker : Attackers)
	{
		if (!Attacker.IsValid())
		{
			continue;
		}

		FThreatObservation& Observation = Observations.FindOrAdd(Attacker);

		if (Observation.ObservationWorldTime <= 0.f)
		{
			Observation.ObservationWorldTime = Now;
			Observation.bRecognized = false;
			Observation.bLoggedDiagnostics = false;

			UE_LOG(
				LogIronboundCombat,
				Log,
				TEXT("Threat observed [%s] attacker %s"),
				*GetNameSafe(GetOwner()),
				*GetNameSafe(Attacker.Get()));
		}

		if (!Observation.bRecognized)
		{
			const float Elapsed = Now - Observation.ObservationWorldTime;
			if (Elapsed + KINDA_SMALL_NUMBER < PerceptionDelaySeconds)
			{
				continue;
			}

			Observation.bRecognized = true;
			UE_LOG(LogIronboundCombat, Log,
				TEXT("Threat recognized [%s] attacker %s"),
				*GetNameSafe(GetOwner()), *GetNameSafe(Attacker.Get()));
		}
	}
}

bool UCombatThreatComponent::AttacksArrayContains(
	const TArray<AActor*>& Attackers,
	AActor* Attacker) const
{
	return Attackers.Contains(Attacker);
}

void UCombatThreatComponent::DiscoverThreatSources(
	TArray<AActor*>& OutAttackers,
	TArray<FCombatCommittedStrike>& OutStrikes)
{
	OutAttackers.Reset();
	OutStrikes.Reset();

	AActor* Owner = GetOwner();
	if (!Owner || !Focus)
	{
		return;
	}

	// TEMPORARY: only the focus target contributes threats today.
	const AActor* Target = Focus->GetCombatTarget();
	if (!Target)
	{
		return;
	}

	UCombatExecutionComponent* TargetExecution =
		Target->FindComponentByClass<UCombatExecutionComponent>();
	if (!TargetExecution || !TargetExecution->HasCommittedBladePath())
	{
		return;
	}

	const FCombatCommittedStrike Strike = TargetExecution->GetCommittedStrike();
	if (!Strike.bValid)
	{
		return;
	}

	OutAttackers.Add(const_cast<AActor*>(Target));
	OutStrikes.Add(Strike);
}

TArray<FCombatThreat> UCombatThreatComponent::GetIncomingThreats() const
{
	TArray<FCombatThreat> Threats;

	// Evaluate the current sources on demand so callers always see live state.
	// Const: discovery reads objective state only.
	UCombatThreatComponent* Mutable = const_cast<UCombatThreatComponent*>(this);

	TArray<AActor*> Attackers;
	TArray<FCombatCommittedStrike> Strikes;
	Mutable->DiscoverThreatSources(Attackers, Strikes);

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	for (int32 Index = 0; Index < Attackers.Num(); ++Index)
	{
		AActor* Attacker = Attackers[Index];

		const FThreatObservation* Observation = Observations.Find(Attacker);
		if (!Observation ||
			!Observation->bRecognized ||
			Now - Observation->ObservationWorldTime + KINDA_SMALL_NUMBER < PerceptionDelaySeconds)
		{
			continue;
		}

		FCombatThreat Threat;
		if (BuildThreatFromCommittedStrike(Attacker, Strikes[Index], Threat))
		{
			Threats.Add(Threat);
		}
	}

	return Threats;
}

bool UCombatThreatComponent::GetPrimaryIncomingThreat(FCombatThreat& OutThreat) const
{
	const TArray<FCombatThreat> Threats = GetIncomingThreats();

	// Most urgent = smallest positive time to impact; threats already inside
	// the body envelope (negative) sort last so they are not "primary" over
	// still-approachable contacts.
	OutThreat = FCombatThreat();

	bool bFound = false;
	float BestTime = TNumericLimits<float>::Max();

	for (const FCombatThreat& Threat : Threats)
	{
		const float Time = Threat.TimeToImpact > 0.f ? Threat.TimeToImpact : TNumericLimits<float>::Max();
		if (Time < BestTime)
		{
			BestTime = Time;
			OutThreat = Threat;
			bFound = true;
		}
	}

	return bFound;
}

FCombatThreatContext UCombatThreatComponent::BuildThreatContext() const
{
	FCombatThreatContext Context;

	FCombatThreat Threat;
	if (GetPrimaryIncomingThreat(Threat))
	{
		Context.bHasThreat = true;
		Context.Threat = Threat;
	}

	return Context;
}

bool UCombatThreatComponent::HasIncomingThreat() const
{
	FCombatThreat Unused;
	return GetPrimaryIncomingThreat(Unused);
}

bool UCombatThreatComponent::GetAttackerSourcePlaybackTime(
	AActor* Attacker,
	float& OutSourceTime) const
{
	OutSourceTime = 0.f;

	if (!Attacker)
	{
		return false;
	}

	// Same mapping the parry observation used: montage position -> source
	// animation position, through the montage's slot segments.
	const UCombatEquipmentComponent* AttackerEquipment =
		Attacker->FindComponentByClass<UCombatEquipmentComponent>();

	USkeletalMeshComponent* Mesh = AttackerEquipment ? AttackerEquipment->GetFighterMesh() : nullptr;
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;

	if (!Anim || !Montage)
	{
		return false;
	}

	const float MontagePosition = Anim->Montage_GetPosition(Montage);

	for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
	{
		for (const FAnimSegment& Segment : SlotTrack.AnimTrack.AnimSegments)
		{
			const float Start = Segment.StartPos;
			const float End = Start + Segment.GetLength();

			if (MontagePosition + KINDA_SMALL_NUMBER < Start ||
				MontagePosition - KINDA_SMALL_NUMBER > End)
			{
				continue;
			}

			OutSourceTime = Segment.ConvertTrackPosToAnimPos(MontagePosition);
			return true;
		}
	}

	return false;
}

bool UCombatThreatComponent::BuildThreatFromCommittedStrike(
	AActor* Attacker,
	const FCombatCommittedStrike& Strike,
	FCombatThreat& OutThreat) const
{
	OutThreat = FCombatThreat();

	if (!Attacker || !Strike.bValid)
	{
		return false;
	}

	float CurrentSourceTime = 0.f;
	if (!GetAttackerSourcePlaybackTime(Attacker, CurrentSourceTime))
	{
		return false;
	}

	// Body-envelope intersection along the committed trajectory.
	bool bAlreadyIntersected = false;
	const float BodyIntersectionTime = CombatBodyProbes::FindBodyIntersectionTime(
		GetFighterMesh(),
		Strike.Trajectory,
		Strike.Transform,
		CurrentSourceTime,
		bAlreadyIntersected);

	if (bAlreadyIntersected)
	{
		// The blade has already entered this fighter's body envelope; the
		// attack can no longer be intercepted ahead of contact. This is
		// still an objective threat state.
		OutThreat.Attacker = Attacker;
		OutThreat.TechniqueId = Strike.TechniqueId;
		OutThreat.AttackerPlanId = Strike.PlanId;
		OutThreat.SourceTrajectory = Strike.Trajectory;
		OutThreat.AttackerTransform = Strike.Transform;
		OutThreat.CurrentSourceTime = CurrentSourceTime;
		OutThreat.FirstBodyIntersectionTime =
			BodyIntersectionTime < TNumericLimits<float>::Max() ? BodyIntersectionTime : -1.f;
		OutThreat.TimeToImpact = -1.f;
		OutThreat.bHasPredictedContact = false;

		return true;
	}

	if (BodyIntersectionTime >= TNumericLimits<float>::Max())
	{
		return false;
	}

	// Predicted contact: the body probe closest to the blade at the
	// intersection sample.
	int32 ProbeCount = 0;
	const CombatBodyProbes::FBodyProbe* Probes = CombatBodyProbes::GetBodyProbes(ProbeCount);

	USkeletalMeshComponent* DefenderMesh = GetFighterMesh();
	if (!DefenderMesh)
	{
		return false;
	}

	bool bFoundContact = false;
	FVector BestContact = FVector::ZeroVector;
	float BestContactDistanceSq = TNumericLimits<float>::Max();
	FVector BestDirection = FVector::ZeroVector;

	for (const FBladeSegment& Segment : Strike.Trajectory.Segments)
	{
		if (!FMath::IsNearlyEqual(Segment.TimeSeconds, BodyIntersectionTime, 0.0001f))
		{
			continue;
		}

		const FVector BladeBase = Strike.Transform.TransformPosition(Segment.Base);
		const FVector BladeTip = Strike.Transform.TransformPosition(Segment.Tip);
		const FVector BladeDirection = (BladeTip - BladeBase).GetSafeNormal();

		for (int32 ProbeIndex = 0; ProbeIndex < ProbeCount; ++ProbeIndex)
		{
			const CombatBodyProbes::FBodyProbe& Probe = Probes[ProbeIndex];

			if (DefenderMesh->GetBoneIndex(Probe.Bone) == INDEX_NONE)
			{
				continue;
			}

			const FVector BodyPoint = DefenderMesh->GetSocketLocation(Probe.Bone);
			const FVector ClosestPoint = FMath::ClosestPointOnSegment(
				BodyPoint,
				BladeBase,
				BladeTip);

			const float DistanceSq = FVector::DistSquared(BodyPoint, ClosestPoint);
			if (DistanceSq < BestContactDistanceSq)
			{
				BestContactDistanceSq = DistanceSq;
				BestContact = ClosestPoint;
				BestDirection = BladeDirection;
				bFoundContact = true;
			}
		}

		break;
	}

	if (!bFoundContact)
	{
		return false;
	}

	OutThreat.Attacker = Attacker;
	OutThreat.Weapon = GetAttackerWeapon(Attacker);
	OutThreat.TechniqueId = Strike.TechniqueId;
	OutThreat.AttackerPlanId = Strike.PlanId;
	OutThreat.SourceTrajectory = Strike.Trajectory;
	OutThreat.AttackerTransform = Strike.Transform;
	OutThreat.CurrentSourceTime = CurrentSourceTime;
	OutThreat.ContactPoint = BestContact;
	OutThreat.Direction = BestDirection;
	OutThreat.TimeToImpact = BodyIntersectionTime - CurrentSourceTime;
	OutThreat.bHasPredictedContact = true;
	OutThreat.FirstBodyIntersectionTime = BodyIntersectionTime;

	return true;
}

UStaticMeshComponent* UCombatThreatComponent::GetAttackerWeapon(AActor* Attacker) const
{
	const UCombatEquipmentComponent* AttackerEquipment =
		Attacker ? Attacker->FindComponentByClass<UCombatEquipmentComponent>() : nullptr;

	return AttackerEquipment ? AttackerEquipment->GetWeapon() : nullptr;
}

USkeletalMeshComponent* UCombatThreatComponent::GetFighterMesh() const
{
	const AActor* Owner = GetOwner();
	const UCombatEquipmentComponent* OwnerEquipment = Owner
		? Owner->FindComponentByClass<UCombatEquipmentComponent>() : nullptr;
	return OwnerEquipment && OwnerEquipment->GetFighterMesh()
		? OwnerEquipment->GetFighterMesh()
		: (Owner ? Owner->FindComponentByClass<USkeletalMeshComponent>() : nullptr);
}
