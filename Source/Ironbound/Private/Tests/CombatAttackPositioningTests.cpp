#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/CombatTrajectoryLibrary.h"

#include "Misc/AutomationTest.h"

namespace
{
	static FBladeSegment MakeTestBlade()
	{
		FBladeSegment Blade;
		Blade.Base = FVector::ZeroVector;
		Blade.Tip = FVector(100.f, 0.f, 0.f);
		return Blade;
	}

	static FCombatAttackOpportunity MakeOpportunity(
		float StandoffCm,
		int32 ContactSample,
		float TargetRadiusCm)
	{
		FCombatAttackOpportunity Opportunity;
		Opportunity.TechniqueId = TEXT("TestCut");
		Opportunity.Region = TEXT("Torso");
		Opportunity.Stance = FTransform(
			FRotator::ZeroRotator,
			FVector(-StandoffCm, 0.f, 0.f),
			FVector::OneVector);
		Opportunity.StandoffCm = StandoffCm;
		Opportunity.ContactSample = ContactSample;
		Opportunity.AimPointAlongBlade = -1.f;
		Opportunity.MissCm = UCombatTrajectoryLibrary::EvaluateBladeContactMiss(
			MakeTestBlade(), Opportunity.Stance, FVector::ZeroVector,
			TargetRadiusCm, Opportunity.AimPointAlongBlade,
			Opportunity.ContactFraction);
		Opportunity.bFeasible = ContactSample != INDEX_NONE &&
			UCombatTrajectoryLibrary::IsContactFeasible(
				Opportunity.MissCm,
				UCombatTrajectoryLibrary::MaxOpportunityContactMissCm,
				UCombatTrajectoryLibrary::DefaultContactPenetrationMarginCm);
		return Opportunity;
	}

	static FString DescribeSelection(
		const TArray<FCombatAttackOpportunity>& Candidates,
		int32 SelectedIndex,
		const FString& Reason)
	{
		if (!Candidates.IsValidIndex(SelectedIndex))
		{
			return FString::Printf(
				TEXT("Production preferred-stance result: no candidate selected. Reason: %s"),
				*Reason);
		}

		const FCombatAttackOpportunity& Selected = Candidates[SelectedIndex];
		return FString::Printf(
			TEXT("Production preferred-stance result: region=%s standoff=%.1f cm "
				"contactSample=%d bladeFraction=%.3f signedMiss=%.1f cm feasible=%s "
				"reason=%s"),
			*Selected.Region.ToString(), Selected.StandoffCm,
			Selected.ContactSample, Selected.ContactFraction, Selected.MissCm,
			Selected.bFeasible ? TEXT("true") : TEXT("false"), *Reason);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCombatAttackFartherValidStanceTest,
	"Ironbound.Combat.AttackPositioning.FarthestValidDistalContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatAttackFartherValidStanceTest::RunTest(const FString& Parameters)
{
	TArray<FCombatAttackOpportunity> Candidates;
	Candidates.Add(MakeOpportunity(90.f, 4, 10.f));
	Candidates.Add(MakeOpportunity(100.f, 7, 10.f));
	Candidates[0].MovementCostCm = 10.f;
	Candidates[1].MovementCostCm = 35.f;

	FString Reason;
	const int32 SelectedIndex =
		UCombatTrajectoryLibrary::SelectPreferredAttackOpportunityIndex(
			Candidates, &Reason);
	AddInfo(DescribeSelection(Candidates, SelectedIndex, Reason));

	TestTrue(TEXT("Both stances have distal-blade penetration contact"),
		Candidates[0].bFeasible && Candidates[1].bFeasible);
	TestTrue(FString::Printf(
		TEXT("Prefer the farther valid stance at %.1f cm over %.1f cm; selected index=%d (%s)."),
		Candidates[1].StandoffCm, Candidates[0].StandoffCm,
		SelectedIndex, *Reason),
		SelectedIndex == 1);
	if (Candidates.IsValidIndex(SelectedIndex))
	{
		TestTrue(TEXT("Selected contact is on the distal 10 percent of the blade"),
			Candidates[SelectedIndex].ContactFraction >=
				UCombatTrajectoryLibrary::OuterBladeContactStartFraction);
		TestTrue(TEXT("Selected signed miss meets the penetration margin"),
			Candidates[SelectedIndex].MissCm <=
				-UCombatTrajectoryLibrary::DefaultContactPenetrationMarginCm);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCombatAttackFartherOutsideVolumeTest,
	"Ironbound.Combat.AttackPositioning.RejectFartherOutsideTargetVolume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatAttackFartherOutsideVolumeTest::RunTest(const FString& Parameters)
{
	TArray<FCombatAttackOpportunity> Candidates;
	Candidates.Add(MakeOpportunity(100.f, 3, 10.f));
	Candidates.Add(MakeOpportunity(111.f, 8, 10.f));

	FString Reason;
	const int32 SelectedIndex =
		UCombatTrajectoryLibrary::SelectPreferredAttackOpportunityIndex(
			Candidates, &Reason);
	AddInfo(DescribeSelection(Candidates, SelectedIndex, Reason));

	TestTrue(FString::Printf(
		TEXT("Farther candidate miss=%.1f cm must fail the penetration margin."),
		Candidates[1].MissCm),
		Candidates[1].MissCm > 0.f && !Candidates[1].bFeasible);
	TestTrue(TEXT("The closer comparison stance remains feasible"),
		Candidates[0].bFeasible);
	TestTrue(FString::Printf(
		TEXT("Select the closer valid stance at %.1f cm, not the outside-volume stance at %.1f cm; selected index=%d (%s)."),
		Candidates[0].StandoffCm, Candidates[1].StandoffCm,
		SelectedIndex, *Reason),
		SelectedIndex == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCombatAttackInnerBladeOnlyTest,
	"Ironbound.Combat.AttackPositioning.RejectInnerBladeOnlyContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatAttackInnerBladeOnlyTest::RunTest(const FString& Parameters)
{
	FCombatAttackOpportunity InnerOnly = MakeOpportunity(80.f, 2, 4.f);
	float InnerFraction = -1.f;
	const float InnerFractionMiss = UCombatTrajectoryLibrary::EvaluateBladeContactMiss(
		MakeTestBlade(), InnerOnly.Stance, FVector::ZeroVector, 4.f,
		0.8f, InnerFraction);

	TArray<FCombatAttackOpportunity> Candidates;
	Candidates.Add(InnerOnly);
	Candidates.Add(MakeOpportunity(100.f, 9, 4.f));

	FString Reason;
	const int32 SelectedIndex =
		UCombatTrajectoryLibrary::SelectPreferredAttackOpportunityIndex(
			Candidates, &Reason);
	AddInfo(FString::Printf(
		TEXT("Inner-only candidate: region=%s standoff=%.1f cm sample=%d "
			"outerBladeFraction=%.3f signedMiss=%.1f cm; explicit inner fraction=%.3f "
			"signedMiss=%.1f cm feasibleAtInnerFraction=%s."),
		*InnerOnly.Region.ToString(), InnerOnly.StandoffCm,
		InnerOnly.ContactSample, InnerOnly.ContactFraction, InnerOnly.MissCm,
		InnerFraction, InnerFractionMiss,
		UCombatTrajectoryLibrary::IsContactFeasible(
			InnerFractionMiss,
			UCombatTrajectoryLibrary::MaxOpportunityContactMissCm,
			UCombatTrajectoryLibrary::DefaultContactPenetrationMarginCm)
			? TEXT("true") : TEXT("false")));
	AddInfo(DescribeSelection(Candidates, SelectedIndex, Reason));

	TestTrue(TEXT("The inner point would penetrate the target volume"),
		UCombatTrajectoryLibrary::IsContactFeasible(
			InnerFractionMiss,
			UCombatTrajectoryLibrary::MaxOpportunityContactMissCm,
			UCombatTrajectoryLibrary::DefaultContactPenetrationMarginCm));
	TestTrue(TEXT("Explicit blade-fraction contact remains at the requested fraction"),
		FMath::IsNearlyEqual(InnerFraction, 0.8f));
	TestTrue(FString::Printf(
		TEXT("The outer-blade evaluation must reject inner-only contact (fraction=%.3f, miss=%.1f cm)."),
		InnerOnly.ContactFraction, InnerOnly.MissCm),
		!InnerOnly.bFeasible);
	TestTrue(FString::Printf(
		TEXT("Select the valid distal-contact stance at %.1f cm; selected index=%d (%s)."),
		Candidates[1].StandoffCm, SelectedIndex, *Reason),
		SelectedIndex == 1 && Candidates[1].bFeasible);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
