#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/CombatTrajectoryLibrary.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatAttackOpportunityOuterReachTest,
	"Ironbound.Combat.AttackOpportunity.OuterReachRetention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatAttackOpportunityOuterReachTest::RunTest(const FString& Parameters)
{
	FBladeTrajectory Trajectory;
	Trajectory.bValid = true;
	FBladeSegment Segment;
	Segment.Base = FVector(40.f, 0.f, 0.f);
	Segment.Tip = FVector(140.f, 0.f, 0.f);
	Trajectory.Segments.Add(Segment);
	FCombatAttackOpportunity Closer;
	Closer.Region = TEXT("Torso");
	Closer.StandoffCm = 136.f;
	Closer.Stance = FTransform(FRotator::ZeroRotator,
		FVector(-136.f, 0.f, 0.f), FVector::OneVector);
	Closer.MissCm = UCombatTrajectoryLibrary::EvaluateTrajectoryContactMiss(
		Trajectory, Closer.Stance, FVector::ZeroVector, 10.f, -1.f,
		0.f, 1.f, Closer.ContactSample, Closer.ContactFraction);
	Closer.bFeasible = UCombatTrajectoryLibrary::IsContactFeasible(
		Closer.MissCm, UCombatTrajectoryLibrary::MaxOpportunityContactMissCm);
	FCombatAttackOpportunity Farther = Closer;
	Farther.StandoffCm = 140.f;
	Farther.Stance.SetLocation(FVector(-140.f, 0.f, 0.f));
	Farther.MissCm = UCombatTrajectoryLibrary::EvaluateTrajectoryContactMiss(
		Trajectory, Farther.Stance, FVector::ZeroVector, 10.f, -1.f,
		0.f, 1.f, Farther.ContactSample, Farther.ContactFraction);
	Farther.bFeasible = UCombatTrajectoryLibrary::IsContactFeasible(
		Farther.MissCm, UCombatTrajectoryLibrary::MaxOpportunityContactMissCm);
	TestTrue(TEXT("The farther trajectory-derived stance remains selectable"),
		Closer.bFeasible && Farther.bFeasible &&
		UCombatTrajectoryLibrary::ShouldRetainFarthestNearbyOpportunity(
			&Farther, &Closer));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
