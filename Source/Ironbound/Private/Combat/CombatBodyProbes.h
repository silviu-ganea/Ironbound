#pragma once

// Internal shared body-envelope geometry for threat measurement and parry
// solving. Not a reflected type. Both the threat component (hard timing gate +
// observation) and the procedural parry executor (candidate clearance
// ranking) must measure against the same body definition.

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "Combat/CombatTrajectoryLibrary.h"

namespace CombatBodyProbes
{
	struct FBodyProbe
	{
		FName Bone;
		float Radius;
	};

	// Conservative gameplay envelope used for both the hard timing gate and
	// candidate clearance ranking. This is intentionally the same geometry in
	// both paths so the solver cannot rank against a different body definition
	// than the one used to invalidate a parry.
	inline const FBodyProbe* GetBodyProbes(int32& OutCount)
	{
		static const FBodyProbe Probes[] =
		{
			{ TEXT("pelvis"), 34.f },
			{ TEXT("spine_01"), 30.f },
			{ TEXT("spine_02"), 31.f },
			{ TEXT("spine_03"), 30.f },
			{ TEXT("clavicle_l"), 12.f },
			{ TEXT("upperarm_l"), 14.f },
			{ TEXT("lowerarm_l"), 12.f },
			{ TEXT("hand_l"), 9.f },
			{ TEXT("clavicle_r"), 12.f },
			{ TEXT("upperarm_r"), 14.f },
			{ TEXT("lowerarm_r"), 12.f },
			{ TEXT("hand_r"), 9.f },
			{ TEXT("neck_01"), 24.f },
			{ TEXT("head"), 28.f }
		};

		OutCount = UE_ARRAY_COUNT(Probes);
		return Probes;
	}

	/** First trajectory sample whose blade enters the body envelope. Max float when none. */
	inline float FindBodyIntersectionTime(
		USkeletalMeshComponent* Mesh,
		const FBladeTrajectory& Trajectory,
		const FTransform& AttackerTransform,
		float CurrentSourceTime,
		bool& bOutAlreadyIntersected)
	{
		bOutAlreadyIntersected = false;
		if (!Mesh)
		{
			return TNumericLimits<float>::Max();
		}

		int32 ProbeCount = 0;
		const FBodyProbe* Probes = GetBodyProbes(ProbeCount);

		for (const FBladeSegment& Segment : Trajectory.Segments)
		{
			const FVector BladeBase = AttackerTransform.TransformPosition(Segment.Base);
			const FVector BladeTip = AttackerTransform.TransformPosition(Segment.Tip);

			for (int32 ProbeIndex = 0; ProbeIndex < ProbeCount; ++ProbeIndex)
			{
				const FBodyProbe& Probe = Probes[ProbeIndex];

				if (Mesh->GetBoneIndex(Probe.Bone) == INDEX_NONE)
				{
					continue;
				}

				const FVector BodyPoint = Mesh->GetSocketLocation(Probe.Bone);
				const FVector ClosestPoint = FMath::ClosestPointOnSegment(
					BodyPoint,
					BladeBase,
					BladeTip);

				if (FVector::DistSquared(BodyPoint, ClosestPoint) <=
					FMath::Square(Probe.Radius))
				{
					if (Segment.TimeSeconds <=
						CurrentSourceTime + KINDA_SMALL_NUMBER)
					{
						bOutAlreadyIntersected = true;
					}

					// Trajectory samples are ordered by source time. Returning
					// the first hit preserves the first body intersection,
					// including one that happened before reaction became ready.
					return Segment.TimeSeconds;
				}
			}
		}

		return TNumericLimits<float>::Max();
	}

	/** Minimum clearance between the body envelope and a blade segment. */
	inline float FindMinimumBodyClearance(
		USkeletalMeshComponent* Mesh,
		const FVector& BladeBase,
		const FVector& BladeTip)
	{
		if (!Mesh)
		{
			return -TNumericLimits<float>::Max();
		}

		float MinimumClearance = TNumericLimits<float>::Max();
		bool bFoundProbe = false;

		int32 ProbeCount = 0;
		const FBodyProbe* Probes = GetBodyProbes(ProbeCount);

		for (int32 ProbeIndex = 0; ProbeIndex < ProbeCount; ++ProbeIndex)
		{
			const FBodyProbe& Probe = Probes[ProbeIndex];

			if (Mesh->GetBoneIndex(Probe.Bone) == INDEX_NONE)
			{
				continue;
			}

			bFoundProbe = true;

			const FVector BodyPoint = Mesh->GetSocketLocation(Probe.Bone);
			const FVector ClosestPoint = FMath::ClosestPointOnSegment(
				BodyPoint,
				BladeBase,
				BladeTip);

			const float Clearance = FMath::Sqrt(
				FVector::DistSquared(BodyPoint, ClosestPoint)) -
				Probe.Radius;

			MinimumClearance = FMath::Min(MinimumClearance, Clearance);
		}

		return bFoundProbe
			? MinimumClearance
			: -TNumericLimits<float>::Max();
	}
}
