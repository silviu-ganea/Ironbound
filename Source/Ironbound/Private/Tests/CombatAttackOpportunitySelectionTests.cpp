#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/CombatTrajectoryLibrary.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCombatAttackOpportunityOuterReachTest,
	"Ironbound.Combat.AttackOpportunity.OuterReachRetention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatAttackOpportunityOuterReachTest::RunTest(const FString& Parameters)
{
	FCombatAttackOpportunity Closer;
	Closer.bFeasible = true;
	Closer.TechniqueId = TEXT("TestTechnique");
	Closer.Region = TEXT("Torso");
	Closer.ContactSample = 1;
	Closer.MissCm = 1.f;
	Closer.StandoffCm = 100.f;

	FCombatAttackOpportunity Farther;
	Farther.bFeasible = true;
	Farther.TechniqueId = Closer.TechniqueId;
	Farther.Region = Closer.Region;
	Farther.ContactSample = 1;
	Farther.MissCm = 10.f;
	Farther.StandoffCm = 145.f;

	const float AcceptedMissToleranceCm =
		UCombatTrajectoryLibrary::MaxOpportunityContactMissCm;
	const float MissGapCm = Farther.MissCm - Closer.MissCm;
	const bool bFartherWithinAcceptedTolerance =
		Farther.MissCm <= AcceptedMissToleranceCm;
	const bool bRetainedFarther =
		UCombatTrajectoryLibrary::ShouldRetainFarthestNearbyOpportunity(
			&Farther, &Closer);
	const TCHAR* ActualRetention = bRetainedFarther
		? TEXT("farther opportunity retained")
		: TEXT("farther opportunity rejected; no alternative candidate identity is returned");
	const TCHAR* FartherToleranceStatus = bFartherWithinAcceptedTolerance
		? TEXT("within")
		: TEXT("outside");

	AddInfo(FString::Printf(
		TEXT("Region: %s | accepted contact-miss tolerance: %.1f cm"),
		*Closer.Region.ToString(), AcceptedMissToleranceCm));
	AddInfo(FString::Printf(
		TEXT("Closer candidate: standoff=%.1f cm, miss=%.1f cm, feasible=%s. "
			"Farther candidate: standoff=%.1f cm, miss=%.1f cm, feasible=%s. "
			"Miss gap: %.1f cm."),
		Closer.StandoffCm, Closer.MissCm,
		Closer.bFeasible ? TEXT("true") : TEXT("false"),
		Farther.StandoffCm, Farther.MissCm,
		Farther.bFeasible ? TEXT("true") : TEXT("false"),
		MissGapCm));
	AddInfo(FString::Printf(
		TEXT("Expected: keep the farther opportunity available for practical outer reach "
			"instead of crowding (standoff %.1f cm vs %.1f cm). Its feasibility is %s "
			"and its %.1f cm miss is %s the %.1f cm accepted tolerance."),
		Farther.StandoffCm, Closer.StandoffCm,
		Farther.bFeasible ? TEXT("true") : TEXT("false"),
		Farther.MissCm, FartherToleranceStatus, AcceptedMissToleranceCm));
	AddInfo(FString::Printf(
		TEXT("Actual production selection-rule result: %s."),
		ActualRetention));

	TestTrue(
		FString::Printf(
			TEXT("Farther miss=%.1f cm must be within accepted tolerance=%.1f cm; "
				"actual production result: %s. Region=%s."),
			Farther.MissCm, AcceptedMissToleranceCm, ActualRetention,
			*Closer.Region.ToString()),
		bFartherWithinAcceptedTolerance);

	// Fighters should use their weapon's practical outer reach instead of crowding the opponent.
	TestTrue(
		FString::Printf(
			TEXT("Expected farther opportunity retained at %.1f cm standoff "
				"(miss %.1f cm); actual production result: %s. "
				"Closer miss=%.1f cm, miss gap=%.1f cm, accepted tolerance=%.1f cm, "
				"region=%s."),
			Farther.StandoffCm, Farther.MissCm, ActualRetention,
			Closer.MissCm, MissGapCm, AcceptedMissToleranceCm,
			*Closer.Region.ToString()),
		bRetainedFarther);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
