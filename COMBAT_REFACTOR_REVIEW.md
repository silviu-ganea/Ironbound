# Experiment branch: build repair and C++ review

Review date: 2026-09-22. Compared `experiment` at `74389a5` with local `main` at `36630d5`.

Scope: repair native compilation/linking and review the refactor. Blueprint graphs, animation assets, DataTables, maps, and gameplay policy are not changed in this pass. A successful native build does **not** establish that the combat prototype runs after this refactor.

The local `main` commit is titled "Before big refactor (pawns not attacking eachother here)". It is useful for comparing the parry implementation, but is not evidence of a working end-to-end duel. Earlier verification in COMBAT_ARCHITECTURE.md predates this refactor.

## Build repairs

- Corrected `ESTrikePhase` to the declared `EStrikePhase`.
- Explicitly mapped technique kind to execution kind instead of assigning unrelated enum types.
- Let the owning execution component invoke the executor's protected cleanup hook.
- Renamed initialization parameters to avoid hiding the inherited request member (MSVC C4458).
- Made mutable parry diagnostic state consistent with the const solver methods, as in the original component.
- Made threat weapon references use `UStaticMeshComponent`, matching the actual simulated weapon representation.
- Gave the melee contact tolerance a distinct name so unity compilation cannot collide with the trajectory library's anonymous-namespace constant.
- Added direct includes and forward declarations needed when translation units compile independently.
- Implemented the declared but missing threat-component mesh accessor, preferring the configured equipment mesh and falling back to the owner's skeletal mesh.

## Architecture assessment

The ownership split is reasonable: Fighter owns identity/team/repertoire; TechniqueComponent validates availability; Focus holds the selected opponent; ThreatComponent measures incoming attacks; ExecutionComponent admits and owns executions; executors perform techniques; Equipment owns grip/weapon geometry; Body owns physical drives; Vitals owns health. Keep this structure instead of adding a parallel combat system. `DiscoverThreatSources` intentionally uses the current focus target as a temporary two-fighter bridge; the plurality of `GetIncomingThreats` is currently API/model shape, not a working multi-attacker scan, and the handover explicitly permits this temporary bridge.

The refactor is incomplete at the connections between these owners. These are review findings, not gameplay changes implemented by this build repair:

1. **P1 — No native intent/navigation owner.** There is no C++ AI controller or policy loop requesting techniques, satisfying `GetEngagementRequirement`, choosing focus, or requesting a defensive response. The previous execution component's navigation is gone. `UIronboundParryComponent` is now inert, and the legacy skillset's `bCanParry` is not consumed by native combat. Rewiring assets alone cannot meet the eventual goal of *all* AI policy in C++; that needs a native controller implementation. A parry-only fighter should prepare the reactive technique without being required to learn an offensive skill.

2. **P1 — Preparation has a short timeout.** `UCombatExecutor_MeleeStrike::HasStanceTimedOut` ends an execution 1.5 seconds after its request, even if its controller is still approaching. This short limit was present in the prior executor too; the current design still needs a deliberate preparation lifetime so ordinary travel does not cause repeated failed requests. `CommitDeadline` is likewise calculated at admission, so travel consumes the montage's playback allowance. Start the commitment deadline when the montage starts, and distinguish approach lifetime from alignment timeout. Check the return value of `Montage_Play`.

3. **P1 — Commitment trusts the solved stance.** `IsReadyToCommit` uses `CurrentPredictedDistance` returned for the *solved* stance. It does not re-evaluate scored contact at the fighter's actual transform at commitment. The previous C++ execution used the solved contact metric too, so this is a validation gap rather than a removed feature; arrival and facing tolerances can still admit a swing whose actual pose misses.

4. **P1 — Native weapon-hit routing is unfinished.** `UCombatEquipmentComponent` classifies body/blade contacts, but does not bind a native weapon hit handler. No native call site sends a body contact through `UCombatInteractionLibrary::Resolve` and `UCombatReactionComponent::ReceiveInteraction`. The execution record's one-hit flag has setters but no native damage gate. `ReceiveInteraction` applies health damage only; its resolved impulse is not delivered to Body. The existing Blueprint contact path must be consciously retained or replaced, with strike-window and one-hit gating, before removing it.

5. **P1 — Dead targets are not excluded by the new focus validator.** `UCombatFocusComponent::IsEnemy` checks this component's `bDead` and team IDs, but does not inspect the candidate's Vitals. `CanExecute` similarly checks target identity/team but not target death. Merely calling `MarkDead` on the victim does not stop another fighter retaining it as a target. The melee executor also checks pointer validity, not life state, while preparing.

6. **P2 — Parry lifecycle is not equivalent to main.** The old system had observation, perception delay, reaction delay, a latched hand target, moving/holding state, and immediate reset when the incoming committed attack disappeared. Perception delay survives in ThreatComponent; reaction delay has no implemented owner. The new executor releases after the predicted interception time plus `LeadHoldSeconds`, not at the end of the observed attack. It does not immediately cancel when that incoming strike is cancelled. Repeated requests can admit another parry for the same swing. `PositionArrivalTolerance` is authored but no longer read to produce the old holding state. Preserve these distinctions when restoring the defensive loop; predicted contact is not proof of physical sword contact.

7. **P2 — Availability cache can become stale.** `GetAvailableTechniques` watches equipment and repertoire revisions but not learned-skill edits, technique-table replacement, or death/reinitialization. `LearnSkill` does not bump the repertoire revision. `RequestTechnique` rechecks live availability, so this does not grant an invalid execution, but controllers enumerating the cache can miss newly available techniques or retain stale candidates.

8. **P2 — Admission is not body-resource arbitration.** Separate deliberate/reactive slot limits can admit an attack and a parry which both drive the right hand. `BodyScope` is currently vocabulary only. In addition, the notify and committed-strike queries select a single deliberate execution even though the authored maximum can exceed one. Keep prototype limits at one and resolve conflicting weapon-arm actions before supporting concurrent techniques.

9. **P2 — A CoreRedirect points to a type that does not exist.** `EIronboundAttackPhase` is redirected to `ECombatStrikePhase`, but no such reflected enum is declared; the new lifecycle type is `ECombatExecutionState`, with different values and per-execution rather than pawn-wide semantics. Do not redirect it to that new type by guesswork. Preserve and inspect old assets during migration, then remove this phase redirect if no true enum replacement is introduced. The class, technique-config, and target-struct redirects match the source renames; body tick's struct redirect is appropriate for the private tick type.

10. **P2 — Weapon family proficiency is stored but does not gate availability.** `UFighterComponent::GetWeaponProficiency` is implemented, but `UCombatTechniqueComponent::IsAvailable` checks repertoire, learned required skills and weapon family only. A zero-proficiency fighter can therefore use a technique whose family proficiency is meant to matter. Decide whether the authored threshold belongs in the row before exposing proficiency as an eligibility promise.

11. **P2 — Execution result is not recorded.** Executors finish with a free-form reason string and the record is immediately removed; callers cannot distinguish Succeeded, Failed, Interrupted or Cancelled, or observe why a technique ended. This limits the requested controller feedback loop and makes telemetry/UI depend on logs. Add an outcome contract when callers actually need it; do not add a global result manager.

12. **P2 — `MaxDeliberateExecutions` is misleading above one.** The component admits multiple deliberate records when configured, but primary engagement, committed trajectory, notify strike window and `CanPlayAttack` all inspect only the first matching record. Keep it at one by default and document/enforce that policy until those aggregate queries have defined multi-record semantics.

13. **P2 — Threat recognition is delayed inside the observation component.** `PerceptionDelaySeconds` makes `GetIncomingThreats` withhold a discovered threat until the delay elapses. The intended split exposes objective threat information immediately and leaves reaction/decision latency to the controller. Keep this property at zero during migration, then move reaction timing with the policy that consumes the threat.

## Procedural parry parity

Source comparison confirms these survived, with owner/config renames:

- Reference-skeleton arm measurements, two-link reach and elbow-circle freedom.
- Analytical wrist-deviation calculation and neutral wrist/forearm relationships.
- Blade crossing, blade-fraction/orientation/roll sampling, and the hard crossing-angle constraint.
- Timing/speed rejection, body-envelope intersection safety margin, and body-clearance ranking.
- Tactical/wrist/freedom scoring and skill-weighted candidate selection.
- Constant-speed hand translation and quaternion rotation interpolation.
- World-space hand output and conversion to component space in `UCombatAnimInstance` for CCDIK.
- Physics Control parry brace and simulated grip angular drive, including release on death. Body implementation changes are principally class/team-source renames.
- Intended blade trajectory derivation: identical to main after class/type renames.

Do not reintroduce a second autonomous parry component alongside the executor. The missing behavior is the observation-to-request policy and execution lifetime, not another copy of the geometric solver. Existing per-instance settings on the retired parry component are not automatically transferred to the new config asset.

## Manual asset migration boundaries

These are source-level contracts to use during the user's Blueprint rewiring, not claims about the current asset graph:

- Assign `FCombatTechniqueRow` data and concrete execution configs; the `ExecutorClass` property must be populated. Assign the technique registry and prepare technique IDs in `BattleRepertoire`. Legacy `AttackMoves` and `bCanParry` do not populate it.
- Keep one team authority (`UFighterComponent`) and one health authority (`UFighterVitalsComponent`). Team 0 remains the observer convention.
- Retain equipment/body initialization using the real mesh, physical sword and constraint. Retain Mover's call to `ResolveOrientationIntent`; the execution code does not itself rotate the pawn.
- Preserve the montage slot and the `UCombatAttackWindow` notify path. After adopting executor-owned montage playback, remove duplicate Blueprint playback/end control.
- Use `UCombatAnimInstance` parry hand output for the existing CCDIK/hand-rotation graph. The C++ executor supplies the target, not a baked parry animation or a forced elbow target.
- Explicitly decide where intent/navigation, physical contact routing and death cleanup live before deleting their old Blueprint implementations.

## Configuration and migration code

- `Config/DefaultEngine.ini` differs from `main` only by the appended rename redirects, so there is no unrelated collision-profile/channel churn in this branch diff.
- `Config/DefaultGameplayTags.ini` adds only the intended Technique, Weapon and Body vocabulary. `Weapon.Surface.*` and `Technique.Trigger.*` are currently authored vocabulary; runtime code does not use them to choose surfaces/triggers.
- `GameplayTags` in `Ironbound.Build.cs` is required by the new tag properties. `AIModule` and `PhysicsControl` are already needed by the project. No new plugin module dependency appears in the current build rules.
- The melee config's montage/source-animation references are local to that executor; the shared technique row/config base does not impose montage playback on future executors. `UWeaponDefinition`, however, currently describes one weapon instance, one legacy hand bone/grip transform, one weapon component and one constraint per fighter. Dual wield, shield slots and multiple grips need a later equipment-slot expansion; do not read the authored `Grips`/`Surfaces` arrays as if current executors already use them.
- `UIronboundParryComponent` is an intentional inert reflected shell so old actor Blueprint component references can resolve during manual migration. It no longer observes attacks or drives a parry. Keep it until the pawn Blueprints are inspected, rewired and resaved; it is not a working compatibility implementation.
- `UIronboundPawnSkillsetComponent` is an intentional reflected legacy shell retaining `AttackMoves`, `bCanParry` and callable accessors. The flag has no effect on the new executor, and attack rows do not populate `BattleRepertoire`. Keep it during migration; remove it only after those assets stop referencing it.
- Redirects are migration aids, not proof assets have migrated. In particular, no old-name component wrapper should be recreated. The enum redirect listed above is a mismatch, not a useful compatibility path.
- MCP inspection was unavailable, so the presence of serialized references, live technique-table rows, the current combat-map assignments and actual collision callbacks remain unknown. Leave source config and reflected shells in place until those asset references can be checked.

## Verification limits

Unreal asset inspection was unavailable: the exposed inspector returned "No valid session token found", and the project's configured local MCP endpoint at `127.0.0.1:8000/mcp` refused the connection. No Unreal Editor process was running when checked. No Blueprints were loaded, compiled, saved or rewired through tools, and no PIE duel was run.

## Build verification

Ran the requested Development editor build with Unity disabled using the same command on three consecutive runs:

```powershell
& 'X:\Tools\UE_5.8\Engine\Build\BatchFiles\Build.bat' IronboundEditor Win64 Development '-Project=X:\Projects\UnrealEngineProjects\Ironbound\Ironbound.uproject' -WaitMutex -DisableUnity
```

All three completed with `Result: Succeeded`. Each scheduled 221 actions; the third completed in 308.41 seconds. It compiled and linked `UnrealEditor-Ironbound.dll` successfully, as well as the requested editor target. `git diff --check` passed (Git only reported the repository's LF-to-CRLF normalization warnings).

UBT logged `Creating makefile for IronboundEditor (no existing makefile)` on each run and rebuilt much of the editor/plugin dependency graph, while the Ironbound module compiled and linked successfully each time. The target makefile was written under `Intermediate/Build/Win64/x64/IronboundEditor/Development/Makefile.bin`; this appears to be a full-target cache/invalidation issue, not a native source failure. The repeated full-build result does not substitute for opening the project in Unreal Editor, compiling affected Blueprints, or running the combat level.
