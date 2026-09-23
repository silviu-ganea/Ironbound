#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/CombatTrajectoryLibrary.h"
#include "Misc/AutomationTest.h"

namespace
{
	static FBladeTrajectory MakeTrajectory(float BaseReach, float TipReach)
	{
		FBladeTrajectory Trajectory;
		Trajectory.bValid = true;
		FBladeSegment Before;
		Before.Base = FVector(BaseReach, -30.f, 0.f);
		Before.Tip = FVector(TipReach, -30.f, 0.f);
		Trajectory.Segments.Add(Before);
		FBladeSegment Contact;
		Contact.Base = FVector(BaseReach, 30.f, 0.f);
		Contact.Tip = FVector(TipReach, 30.f, 0.f);
		Trajectory.Segments.Add(Contact);
		Trajectory.ReachMax = TipReach;
		return Trajectory;
	}

	static FCombatAttackOpportunity EvaluateStance(
		const FBladeTrajectory& Trajectory, float Standoff, float TargetRadius)
	{
		FCombatAttackOpportunity Opportunity;
		Opportunity.TechniqueId = TEXT("TestCut");
		Opportunity.Region = TEXT("Torso");
		Opportunity.StandoffCm = Standoff;
		Opportunity.Stance = FTransform(
			FRotator::ZeroRotator, FVector(-Standoff, 0.f, 0.f),
			FVector::OneVector);
		Opportunity.MissCm = UCombatTrajectoryLibrary::EvaluateTrajectoryContactMiss(
			Trajectory, Opportunity.Stance, FVector::ZeroVector, TargetRadius,
			-1.f, 0.f, 1.f, Opportunity.ContactSample,
			Opportunity.ContactFraction);
		Opportunity.bFeasible = Opportunity.ContactSample != INDEX_NONE &&
			UCombatTrajectoryLibrary::IsContactFeasible(
				Opportunity.MissCm,
				UCombatTrajectoryLibrary::MaxOpportunityContactMissCm);
		return Opportunity;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatAttackFartherValidStanceTest,
	"Ironbound.Combat.AttackPositioning.FarthestValidDistalContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCombatAttackFartherValidStanceTest::RunTest(const FString& Parameters)
{
	TArray<FCombatAttackOpportunity> Candidates;
	const FBladeTrajectory Short = MakeTrajectory(40.f, 140.f);
	Candidates.Add(EvaluateStance(Short, 110.f, 10.f));
	Candidates.Add(EvaluateStance(Short, 140.f, 10.f));
	Candidates.Add(EvaluateStance(Short, 150.f, 10.f));
	TestTrue(TEXT("Sampled contact selects the farthest penetrating stance"),
		UCombatTrajectoryLibrary::SelectPreferredAttackOpportunityIndex(Candidates) == 1 &&
		Candidates[1].ContactSample == 0 && Candidates[1].bFeasible &&
		!Candidates[2].bFeasible);
	TestTrue(TEXT("Contact occurs between animation samples, where neither sampled tip touches"),
		Candidates[1].ContactFraction == 1.f &&
		FVector::Distance(Short.Segments[0].Tip, FVector(140.f, 0.f, 0.f)) > 10.f &&
		FVector::Distance(Short.Segments[1].Tip, FVector(140.f, 0.f, 0.f)) > 10.f);
	const FCombatAttackOpportunity CenteredHead = EvaluateStance(Short, 140.f, 7.f);
	const FCombatAttackOpportunity GlancingHead = EvaluateStance(Short, 132.f, 7.f);
	TestTrue(TEXT("The tip path must pass through the central head volume"),
		CenteredHead.bFeasible && !GlancingHead.bFeasible);

	Candidates.Reset();
	const FBladeTrajectory Long = MakeTrajectory(60.f, 180.f);
	Candidates.Add(EvaluateStance(Long, 140.f, 10.f));
	Candidates.Add(EvaluateStance(Long, 180.f, 10.f));
	Candidates.Add(EvaluateStance(Long, 190.f, 10.f));
	TestTrue(TEXT("A different sampled arm and blade reach changes the valid standoff"),
		UCombatTrajectoryLibrary::SelectPreferredAttackOpportunityIndex(Candidates) == 1 &&
		Candidates[1].bFeasible && !Candidates[2].bFeasible);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatAttackFartherOutsideVolumeTest,
	"Ironbound.Combat.AttackPositioning.RejectFartherOutsideTargetVolume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCombatAttackFartherOutsideVolumeTest::RunTest(const FString& Parameters)
{
	const FBladeTrajectory Trajectory = MakeTrajectory(40.f, 140.f);
	const FCombatAttackOpportunity Contact = EvaluateStance(Trajectory, 140.f, 10.f);
	const FCombatAttackOpportunity Outside = EvaluateStance(Trajectory, 150.f, 10.f);
	TestTrue(TEXT("The target-volume boundary lacks the required penetration"),
		Contact.bFeasible && !Outside.bFeasible && Outside.MissCm >= 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatAttackInnerBladeOnlyTest,
	"Ironbound.Combat.AttackPositioning.RejectBladeIntersectionWithoutTipPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCombatAttackInnerBladeOnlyTest::RunTest(const FString& Parameters)
{
	FBladeTrajectory Trajectory = MakeTrajectory(40.f, 140.f);
	for (FBladeSegment& Segment : Trajectory.Segments)
	{
		Segment.Base.Y = 0.f;
		Segment.Tip.Y = 0.f;
	}
	const FCombatAttackOpportunity Inner = EvaluateStance(Trajectory, 90.f, 4.f);
	float BladeFraction = -1.f;
	const float BladeMiss = UCombatTrajectoryLibrary::EvaluateBladeContactMiss(
		Trajectory.Segments[1], Inner.Stance, FVector::ZeroVector, 4.f,
		-1.f, BladeFraction);
	TestTrue(TEXT("A blade intersection must not count when the animated tip path misses"),
		!Inner.bFeasible && Inner.ContactFraction == 1.f &&
		UCombatTrajectoryLibrary::IsContactFeasible(
			BladeMiss, UCombatTrajectoryLibrary::MaxOpportunityContactMissCm) &&
		FMath::IsNearlyEqual(BladeFraction, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatAttackSideSwingCenterTest,
	"Ironbound.Combat.AttackPositioning.SideSwingTipCrossesTargetCenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCombatAttackSideSwingCenterTest::RunTest(const FString& Parameters)
{
	FBladeTrajectory Trajectory;
	Trajectory.bValid = true;
	FBladeSegment First;
	First.Base = FVector(0.f, 20.f, 20.f);
	First.Tip = FVector(0.f, 100.f, 20.f);
	Trajectory.Segments.Add(First);
	FBladeSegment Last;
	Last.Base = FVector(0.f, 20.f, -20.f);
	Last.Tip = FVector(0.f, 100.f, -20.f);
	Trajectory.Segments.Add(Last);
	const FVector Target(0.f, 100.f, 0.f);
	FTransform Stance;
	const bool bCentered = UCombatTrajectoryLibrary::PlaceTrajectoryContactAtTarget(
		First, Last, Target, 0.f, FVector::OneVector, 0.f, -1.f, Stance);
	int32 Sample = INDEX_NONE;
	float Fraction = -1.f;
	const float Miss = UCombatTrajectoryLibrary::EvaluateTrajectoryContactMiss(
		Trajectory, Stance, Target, 7.f, -1.f, 0.f, 1.f,
		Sample, Fraction);
	TestTrue(TEXT("A tip arc can cross the target center even when the actor faces 90 degrees away"),
		bCentered && FMath::IsNearlyEqual(Stance.GetLocation().Size2D(), 0.f) &&
		FMath::IsNearlyEqual(Stance.Rotator().Yaw, 0.f) &&
		FMath::Abs(FMath::FindDeltaAngleDegrees(
			(Target - Stance.GetLocation()).Rotation().Yaw,
			Stance.Rotator().Yaw)) > 45.f &&
		Sample == 0 && Fraction == 1.f &&
		UCombatTrajectoryLibrary::IsContactFeasible(
			Miss, UCombatTrajectoryLibrary::MaxOpportunityContactMissCm));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
