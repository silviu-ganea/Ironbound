# Combat ownership and verification

## Implemented foundation — 2026-09-16

The immediate test remains **Pawn0 attacking passive Pawn1**, with opposing teams and a Team 0 observer. Pawn1's existing level instance has AI possession disabled; this was preserved deliberately. The controller is reusable for other fighters, but simultaneous active opponents were not part of this verification.

### Ownership

| Responsibility | Owner | Contract |
| --- | --- | --- |
| Select an enemy and a move | `BP_FIghterAIController` | Submit intent; exclude self, dead fighters, same team, and Team 0. Do not rotate the pawn or manipulate equipment. |
| Approach, align, commit, recover, cancel | `UIronboundCombatExecutionComponent` | Own action state and combat facing. Commit only after arrival, low measured movement speed, facing tolerance, and contact validation at the actual actor transform. |
| Apply movement and rotation | Existing Mover input pipeline | `ProduceInput` passes locomotion orientation through `ResolveOrientationIntent`. Mover remains the writer of actor orientation. |
| Equip/unequip and intended trajectory cache | `UIronboundEquipmentComponent` | Break the constraint before changing simulation; place from authored grip, establish joint frames, then invalidate cached trajectories. Reject equipment changes while committed or recovering. |
| Weapon identity and geometry | `UIronboundWeaponDefinition` / `DA_Sword1a` | Mesh, mass, hand bone, weapon-to-hand transform, blade base and tip. No grip calibration from simulated world transforms. |
| Attack content | `DT_AttackMasterMoves` | Sequence, montage, strike interval, sample count, allowed bones, tolerance, damage, and bounded knockback. Commit uses this row's montage and damage. |
| Intended blade sampling and contact solve | `UIronboundTrajectoryLibrary` | Produce actor-local geometry from animation and authored equipment. Planning and actual-pose validation use the same contact metric. Prediction does not apply damage. |
| Strike interval | `UIronboundAttackWindow` via existing notify Blueprint | Notify opens/closes state on the fighter's execution component, never on the shared notify object. Completion, interruption, cancellation and death close the window. |
| Actual contact and health | Existing pawn collision/health Blueprint | Physical sword contact during the strike window drives damage. Health/team/death rules remain here for now. |

### Equipment and coordinate spaces

Recovered from a temporary copy of committed Blueprint `0972c1c`: sword attached to `palm_r_Socket`, local rotation 180 degrees pitch. Including the socket offset, weapon-local → `hand_r` is translation **(-7.5, 2, 0) cm**, quaternion **(0, 1, 0, 0)**, unit scale.

The same authored transform now drives both equipment placement and prediction:

`blade point in weapon → authored grip → sampled hand in mesh → mesh in actor → candidate/actual actor in world`.

The normalized sword's blade points along negative local Z. `DA_Sword1a` uses base `(0,0,0)` and tip `(0,0,-105.536964)` from the inspected mesh extent. These are explicit weapon data, not a runtime longest-axis guess. Inspect/calibrate blade endpoints when adding a different mesh.

Never call equipment initialization at attack start. Once equipped, the sword remains physically simulated and constrained. Strength, collisions and other physical effects can still cause intended/actual differences; this is part of Ironbound's design.

Use `EquipWeapon(NewDefinition)` for loadout changes and `UnequipWeapon()` for removal. Each successful change invalidates trajectories and increments `Revision`. The current pawn's `RefreshAttackData` clears and warms the native cache. Before a fight, equip and warm all selected moves. No persistent animation bake is required. Editing a loaded animation or definition in place requires explicit refresh/re-equip; changing object contents is not detected automatically.

### Execution lifecycle

`Idle → Approaching → Aligning → Committed → Recovery → Idle`.

- A geometrically feasible pose is a destination, not permission to swing.
- Navigation uses an explicit arrival radius without adding capsule overlap radius. Failed/partial paths do not authorize attacks.
- Aligning stops navigation and supplies the solved orientation to Mover. It waits for measured speed ≤ 5 cm/s and yaw error ≤ 3 degrees, then independently checks contact at the actual pose.
- Commitment freezes facing through the montage. Completion/interruption releases into recovery; death cancels immediately. A deadline prevents native execution from staying committed forever if playback fails.
- An abandoned approach request expires; no valid enemy means the controller stops submitting requests.
- `IsCommitted()` is the authoritative busy query, including recovery. The old pawn `IsAttacking` flag is montage presentation/legacy hit-path state, not action arbitration.

The controller's current 0.1-second timer is a small intent source. Circling, dodge selection, stamina budgets and injury-dependent choices can replace that source without taking over equipment or directly setting rotation. These future actions will need explicit priorities, cancellation rules and costs; they are not implemented by this change.

## Diagnosed failures and corrections

1. **Grip regression:** the new initialization wrote a relative rotation after physics could detach the weapon. Reinitializing the joint every attack preserved a displaced grip. Restored authored grip data and an explicit equip lifecycle.
2. **Facing conflict:** `SetActorRotation` fought Mover orientation state. Replaced with Mover input ownership and alignment gating.
3. **Misleading debug:** old cyan was predicted geometry at the current root, not measured physics. Red now shows intended strike geometry; blue records the physical tip during the strike window. Cyan during planning remains current-pose prediction.
4. **Solver units/ranking:** squared distances were compared with linear tolerances, and candidate bookkeeping could return another stance. Reworked candidate evaluation with linear distances and consistent pose/result bookkeeping; current distance is a valid probe again.
5. **Disconnected execution:** `AttemptAttack` had no wired attack call; the strike notify Blueprint and window setter were empty. Reconnected commit and implemented a native per-fighter notify while preserving montage/notify asset identity.
6. **Disconnected death/UI:** damage changed health without refreshing the bar or invoking death. Restored both and guarded unpossession for passive targets without controllers.

## Verification evidence

- Native code compiled and patched successfully through Live Coding; changed Blueprints compiled without warnings or errors.
- Pawn0 approached from 2,000 cm initial separation, aligned, then repeatedly attacked Pawn1.
- 788 state samples over 39.96 seconds: ten health reductions of 10 took Pawn1 from 100 to 0. Pawn1 became dead; Pawn0 returned to idle and stopped attacking.
- Maximum committed facing error was **2.496 degrees** on the first swing, then approximately zero. Root facing did not turn away before/during subsequent swings.
- Measured attacker's hand-relative grip stayed near the authored translation and rotation during repeated attacks. This is sampled evidence, not a bound on every physics substep.
- Observer stayed at 1,000 health and never attacked. Explicit observer `Attack` and `ReceiveCombatHit` calls were rejected in PIE.
- Idle unequip/re-equip succeeded, changed revision twice, and restored ready equipment.
- Final regression after wiring the native busy query and guarding controller unpossession: Pawn1 reached 0 health/dead, Pawn0 returned to Idle with IsAttacking false, and the Team 0 observer remained at 1,000 health. No new Blueprint runtime error was logged; the sole logged unpossession error belonged to the earlier run before the guard.
- The every-second-tick recorder can observe health after a notify has already closed: two health transitions appear in samples with a closed window. Do not claim these samples alone prove precise collision/notify ordering. The actual damage gate reads the native window on the contact execution path.
- Local diagnostic summary: `Saved/CodexCombatBaseline/verification.json`; baseline working Blueprint backups are in the same ignored folder. Temporary historical assets were removed.

## Remaining boundaries and future work

- Contact prediction currently uses bone positions shifted toward the attacker by capsule radius. This is a **coarse surface proxy**, not per-bone PhysicsAsset geometry. Replace with body-shape distance/sweep queries when adding anatomical targeting, moving-target prediction or obstacles. Navigation is checked separately from the geometric solve.
- Sampling evaluates an isolated sequence. It does not predict montage blending, root-motion displacement, procedural pose changes, future target motion, or physical tracking error. The current verified montage is a single segment at rate 1; arbitrary montage segments/rates and root-motion attacks need explicit mappings and validation.
- Weapon hit handling still includes prototype one-hit-per-attack and attacker-owned victim reaction logic. Move reaction policy into a victim health/injury component before expanding damage types, armor or persistent injuries. A failed physical strike must remain possible despite a feasible plan.
- AI still scans actors and uses a simple timer. Use a fighter registry/perception and a behavior planner as encounter size and tactics grow. A behavior tree/StateTree/utility policy should call the execution interface rather than duplicate it.
- Stamina, injuries, balance and physical effort limits are future capability/state systems. Do not implement them as exceptions inside trajectory geometry. Feed them into action eligibility/cost and Physics Control capability separately.
- Save persistent loadout identifiers in fighter/menu data; runtime components own spawned equipment and physics only. Networking, arbitrary character scale/rig combinations, packaged builds and simultaneous active duelists have not been validated here.
- Verification used the running editor's Live Coding build. A normal Editor/packaged build should be part of the next clean-build check before distributing the project.
