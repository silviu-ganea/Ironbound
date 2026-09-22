## Current handoff state — 2026-09-22

The native Editor target now builds, and `/Game/Blueprints/SandboxCharacter_Mover` compiles with **zero errors and zero warnings**. The Blueprint cleanup removed the old attack trigger and sword-hit damage flow, and removed the obsolete `InitializeFocus` hookup. Death now calls `CombatExecution.CancelActiveExecutions`; body initialization, health/death presentation, weapon/equipment setup, GASP input, traversal, Mover, and the GASP AnimBP remain. No fight has been rewired or tested after this cleanup.

### Legacy behavior to reproduce through the new system

This is a record of the previous prototype path, not the current implementation:

- The old `AttemptAttack` flow selected attack index 0 from `DT_AttackMasterMoves`, resolved the opponent mesh, prepared an attack, then committed it by setting the montage/damage and invoking the `Attack` event. The old event played the montage only when `CombatExecution` allowed an attack.
- Sword collision was only allowed to apply one hit during that attack. It ignored self-contact, required an opponent fighter, applied `DamagePerHit` with team validation, and requested the victim's physical hit reaction. The victim's health/death flow then hid the bar, unpossessed if applicable, stopped Mover, and ragdolled the body.
- As described by the project owner, the earlier procedural parry observed an incoming attack, generated a sword/hand interception pose, and used CCDIK to bring the upper body into that pose. Its quality was rough, but the defender could keep attempting parries. Preserve this intended behavior when rebuilding; do not treat the now-empty deprecated `IronboundParryComponent` as its implementation.
- Pawn0 repeatedly chose the same overhead skill and pursued Pawn1; Pawn1 had no attack skill and only attempted parries. Team 0 was an observer. The goal was autonomous fighting until Pawn0 killed Pawn1.

### Rebuild boundary

Keep the Mover/GASP graph as the movement and animation owner. The AI/controller should submit an attack or parry intention; the technique/execution components should validate and execute it; Mover should continue to consume controller navigation through `NavMover` or player input through the existing input path. Weapon contact, damage, reaction, and death must be explicit combat-system responsibilities. Attack selection must move from the old `DT_AttackMasterMoves` shortcut to the new skill/repertoire/technique definitions, and parry observation/pose generation must be restored in the new threat/executor path. The old attack helper function names remain only as inert Blueprint stubs pending later cleanup; they are not the new gameplay path.

---

## Combat equipment and attack execution refactor — 2026-09-16

Current implementation details and measured results are in [COMBAT_ARCHITECTURE.md](COMBAT_ARCHITECTURE.md). This section supersedes older descriptions of attack setup and the completed stationary duel; those remain as historical findings.

- Restored the committed sword grip using authored weapon data: hand-relative (-7.5, 2, 0) cm, 180-degree pitch. Equipment setup now happens on equip, not every attack; prediction uses the same authored grip instead of live physics displacement.
- Added runtime equipment and combat-execution components. Mover owns rotation through orientation intent; execution owns approach/alignment/commit/recovery and validates actual-pose contact before committing.
- Corrected the trajectory solver's squared-versus-linear tolerance comparison and candidate bookkeeping. Isolated animation sampling now explicitly updates animation before reading the evaluated pose.
- Reconnected attack commitment, implemented the formerly empty strike-window notify, and restored the disconnected health-bar/death flow. Passive targets without controllers are supported by the death path.
- The current map is deliberately Pawn0 versus passive Pawn1 (teams 1 and 2); Team 0 remains the observer. Pawn1's disabled AI possession was preserved.
- PIE recording: Pawn0 closed from 2,000 cm, delivered ten 10-damage hits, Pawn1 died, and Pawn0 returned to idle. Observer remained at 1,000 health. Maximum committed yaw error 2.496 degrees on the first swing, approximately zero thereafter. Idle unequip/re-equip and observer attack/damage rejection passed.
- This validates the requested one-attacker scenario, not a completed tactical AI architecture. Contact prediction is still a coarse bone/capsule surface proxy; injury, stamina, dodging, body-shape prediction and victim-owned reaction policy remain future work. See the architecture document for precise limits and extension points.

---

\# IRONBOUND — Combat Prototype Roadmap



> \*\*Project:\*\* Ironbound  

> \*\*Engine:\*\* Unreal Engine 5.8.2  

> \*\*Repository:\*\* `silviu-ganea/Ironbound`  

> \*\*Main branch:\*\* `main`  

> \*\*Starting point:\*\* Epic Game Animation Sample Project (GASP)



This file is the project's \*\*living technical memory and roadmap\*\*.



The GitHub repository is the source of truth for the current implementation.  

This document should contain the information that is expensive to rediscover:



\- final design goals,

\- current development goals,

\- completed milestones,

\- important technical discoveries,

\- failed/abandoned approaches,

\- known traps,

\- and the next experiments.



\---



\# AI / DEVELOPER INSTRUCTIONS



\## Always keep this file updated



Whenever meaningful project state changes, update this document.



Update it when:



\- a goal is completed,

\- a new goal is added,

\- an experiment confirms or disproves a hypothesis,

\- an important bug is solved,

\- an architecture decision changes,

\- a workaround is discovered,

\- an approach is abandoned,

\- or an important "aha" moment occurs.



When a roadmap item is completed, change:



`\[ ]` → `\[x]`



Do not mark a feature complete until it has actually been tested.



\## Preserve useful discoveries



If solving something required substantial debugging, record the result here.



The purpose is to prevent future developers or AI agents from repeating the same failed investigation.



Do not silently delete useful historical lessons when implementation changes. Keep a short note explaining what was learned and why the old approach was abandoned.



\## Check the repository when possible



When GitHub access is available, inspect the actual repository instead of relying on this document for implementation details.



This file explains \*\*why, status, discoveries, and roadmap\*\*.



The repository explains \*\*what currently exists\*\*.



Note: Unreal `.uasset` / `.umap` files are binary and may still require Unreal inspection or screenshots to understand Blueprint graph logic.



\---



\# FINAL GAME VISION



Ironbound is intended to become a \*\*physics-driven spectator / management combat game\*\*.



The player manages fighters rather than directly controlling every attack and defensive action.



Core principle:



> \*\*Animation represents intended movement. Physics determines what the fighter can actually achieve.\*\*



A fighter should not perfectly reproduce an attack simply because an animation is playing.



Execution should depend on things such as:



\- strength,

\- skill,

\- balance,

\- fatigue,

\- injury,

\- weapon mass,

\- weapon inertia,

\- body position,

\- collisions,

\- and external forces.



Two fighters using the \*\*same attack animation\*\* should be capable of producing very different physical results.



\---



\# TARGET COMBAT MODEL



Long-term flow:



AI chooses action  

↓  

Target animation / pose  

↓  

Physics Control / active ragdoll tries to follow it  

↓  

Stats determine physical capability  

↓  

Weapon/body physics modify the motion  

↓  

Actual attack / block / failure emerges



Important:



Decision ≠ guaranteed execution  

Animation ≠ final motion



The fighter may make the correct decision and still physically fail.



That failure is intended gameplay.



\---



\# LONG-TERM GOALS



\## Fighter capability



Different fighters should physically perform the same intended action differently.



Examples:



\### Strength



Controls how much physical effort the fighter can produce.



Potential effects:



\- weapon acceleration,

\- lifting heavy weapons,

\- resisting impacts,

\- posture recovery.



\### Skill



Controls how accurately and efficiently the fighter follows intended motion.



Potential effects:



\- tracking accuracy,

\- wobble,

\- overshoot,

\- recovery,

\- weapon trajectory precision.



\### Fatigue / injury



Should degrade physical performance rather than merely changing animation speed.



\### Balance



Should influence resistance to knockback and ability to recover posture.



These mappings are still hypotheses and must be experimentally validated.



\---



\# PHYSICAL WEAPONS



Weapons should physically matter.



Relevant properties include:



\- mass,

\- center of mass,

\- length,

\- inertia,

\- collision shape.



A heavy weapon should actually be harder to accelerate, stop, redirect, and recover.



A weak fighter using a heavy weapon should not behave like a strong fighter using the same weapon.



\---



\# PHYSICAL BLOCKING



Long-term blocking should not depend on thousands of paired attack/block animations.



Desired concept:



Detect incoming weapon  

↓  

Predict trajectory  

↓  

Choose interception point  

↓  

Attempt defensive pose  

↓  

Physics Control moves fighter  

↓  

Weapons physically collide  

↓  

Physics determines result



Possible outcomes:



\- successful block,

\- weapon deflection,

\- weak guard collapsing,

\- missed interception,

\- defender losing balance,

\- attacker overpowering the block.



\---



\# CURRENT PROTOTYPE



Current test setup:



\- player-controlled fighter,

\- opponent/test victim,

\- both using swords,

\- GASP movement/camera foundation,

\- player can trigger a sword attack,

\- right arm uses Physics Control,

\- sword is physically simulated and constrained,

\- opponent can physically react to hits,

\- live debug slider controls attack angular strength.



Main character Blueprint:



`SandboxCharacter\_Mover`



\---



\# COMPLETED MILESTONES



\## Project setup



\- \[x] Start from Epic GASP.

\- \[x] Establish working two-fighter prototype.

\- \[x] Rename project to \*\*Ironbound\*\*.

\- \[x] Put project under Git.

\- \[x] Configure Unreal `.gitignore`.

\- \[x] Remove generated/local files from source control.

\- \[x] Upload tracked project successfully to GitHub.

\- \[x] Confirm local `main` matches `origin/main`.

\- \[x] Connect ChatGPT GitHub access to private Ironbound repository.

\- \[x] Create persistent technical roadmap.



\---



\## Combat stance — tested / completed (2026-09-14)

- [x] Validated in Play: stationary guard, normal keyboard/GASP movement, automatic return to guard, and the existing attack montage.
- `SandboxCharacter_Mover_ABP` caches the post-procedural GASP locomotion pose (`Procedural_FullBody`) as `BaseLocomotionPose`.
- Copy `CombatStance` from `SandboxCharacter_Mover` into the AnimBP; calculate `IsCombatStationary` from `Speed2D < 5` using GASP's existing speed value.
- `Sword_ForwardHigh` + stationary selects the full-body `A_Sword_Guard_ForwardHigh` pose. Moving selects `BaseLocomotionPose` with **NO upper-body guard overlay**; stopping while the stance remains active automatically restores the full-body guard. `None` uses normal GASP locomotion.
- Keep `SwordAttackSlot` after stance selection. The existing attack montage still works and its physical execution looks cleaner / less tangled.
- Deliberately abandoned upper-body-only moving guard for now: normal W movement runs/jogs and looks unnatural with the sword held in guard. Revisit only for dedicated combat footwork or AI shifting.
- Retain this session's `BP_FIghterAIController` and reusable character `Attack` event as the AI intent foundation. AI behavior was deferred while cleaner strikes were explored; following the completed mocap animation milestone below, the next active goal is the limited two-fighter AI duel.

## Health, teams, overhead health bar, and death — tested / completed (2026-09-15)

- [x] Added `MaxHealth` (default 100), `CurrentHealth`, `IsDead`, `DamagePerHit` (default 25), and instance-editable `FighterTeam` to `SandboxCharacter_Mover`.
- [x] Added `ReceiveCombatHit(DamageAmount, AttackerTeam)`. Dead fighters and fighters on the attacker's team reject damage; health is clamped to zero.
- [x] Added `WBP_FighterHealthBar` and a screen-space `HealthBar` widget component above every fighter. Runtime damage updates the progress bar and death hides it.
- [x] Added `Die`: mark dead, hide the health bar, unpossess the controller, disable the Mover component tick, disable capsule collision, switch the skeletal mesh to the `Ragdoll` collision profile, simulate all bodies, and wake the ragdoll.
- [x] Gated sword damage to an active attack montage and one successful damage event per attack. Incidental/resting sword contacts no longer drain health repeatedly.
- [x] Replaced the legacy placed combat-opponent instance so it inherits the new widget-component defaults correctly. `Combat_Test_Opponent` is Team 2; the default/spawned fighter is Team 1.
- Runtime validation: friendly damage left health unchanged; enemy damage changed 100 → 75 and the visible bar to 75%; lethal damage produced health 0, hid the bar, removed the controller, disabled Mover/capsule, and produced a simulated pelvis ragdoll.

## Cleaner sword animation — completed (2026-09-15)

- [x] Produce a cleaner sword attack animation; the successful final approach was **Unreal MetaHuman mocap**, with a result confirmed successful by the project owner.
- Exploration included animation/rig solutions and **Cascadeur**. Cascadeur was tried but was not the final approach; preserve this exploration as history.
- Successful overhead-swing animation sequence: `/Game/TestSwordAnimations/MocapAnimations/AS_Mocap_SwordSwing_01_RH_UEFN1`.
- This completes the cleaner-animation milestone. The next active goal is two AI-controlled fighter pawns using only this overhead-swing skill and fighting autonomously until one dies.
- Animation creation is confirmed complete; integration into the AI attack flow and validation of the autonomous duel remain pending.

---

\# CURRENT ATTACK TEST

Current successful overhead-swing animation sequence:

`/Game/TestSwordAnimations/MocapAnimations/AS_Mocap_SwordSwing_01_RH_UEFN1`

Created successfully using Unreal MetaHuman mocap. Use this sequence for the next single-skill AI duel goal.

## Previous attack test setup — retained technical history

The following animation, montage, slot and input describe the previously documented test setup. Replacement of the montage's animation with the new mocap sequence has not been confirmed in this documentation update.



Attack animation:



`A\_AwesomeSwordAnimationsV5\_AttackCombo2\_Stage1\_Complete\_IP`



Montage:



`AM\_TestSword\_CleanSwing1`



Slot:



`DefaultGroup.SwordAttackSlot`



Temporary input:



`Key 1 → play attack`



\---



\# PHYSICS CONTROL



Right-arm Physics Control currently starts at:



`clavicle\_r`



Important current ideas:



Angular Strength  

→ how aggressively the arm follows target animation



Max Torque  

→ currently unlimited at 0  

→ promising candidate for physical effort / strength



The runtime function:



`SetAttackAngularStrength(NewStrength)`



updates the existing right-arm Physics Control live.



Temporary tests:



Key 5 → Angular Strength 15  

Key 6 → Angular Strength 500



5 and 6 are keyboard keys, not strength values.



\---



\# MAJOR VALIDATED RESULT



\## CORE MECHANIC VALIDATED



The same attack animation was tested at different Physics Control angular strengths.



\### Low strength ≈ 15



Observed:



\- arm strongly lagged,

\- sword struggled to rise,

\- fighter barely completed the attack,

\- attack looked physically weak.



\### High strength ≈ 500



Observed:



\- arm tracked target animation much more strongly,

\- sword produced an effective swing,

\- attack reached the opponent,

\- physical contact produced visible reaction.



\### Conclusion



> \*\*Same intended animation + different physical control strength = different physical execution.\*\*



This validates one of Ironbound's central design assumptions.



The game does not necessarily need separate canned "weak" and "strong" attack animations.



\- \[x] Demonstrate weak execution using the same animation.

\- \[x] Demonstrate strong execution using the same animation.

\- \[x] Demonstrate meaningful physical weapon contact.



\---



\# LIVE DEBUG UI



Widget:



`WBP\_AttackStrengthTest`



Current slider:



`Attack Angular Strength`



Range:



`0–500`



It updates Physics Control during PIE.



\- \[x] Runtime Angular Strength setter.

\- \[x] Live slider.

\- \[x] Numeric display.

\- \[x] Verify physical result changes live.



\---



\# IMPORTANT DISCOVERIES / AHA MOMENTS



\## AHA 1 — The source attack animation is not the main problem



The clean attack was previewed with a correctly oriented sword and no physics interference.



Wrist motion and sword trajectory looked correct.



Therefore, major runtime distortion should first be investigated in:



\- Physics Control,

\- constraints,

\- simulated bodies,

\- collision,

\- torque/strength,

\- weapon physics.



Do not immediately return to animation retargeting.



\---



\## AHA 2 — Physics Control must actually be initialized



`Create Controls and Body Modifiers from Limb Bones` originally had no proper execution path.



Custom event:



`InitArmPhysicsControl`



was added and called from BeginPlay.



Without this, runtime Physics Control settings did not have properly created controls to modify.



\---



\## AHA 3 — Sword simulation and constraint state are coupled



Known working configuration:



Sword Simulate Physics = ON  

PhysicsConstraint = active



Broken test:



Sword Simulate Physics = OFF  

PhysicsConstraint = still active



Result:



severe character distortion / "pretzel"



Do not disable sword physics while leaving the current constraint active.



This is a physics/constraint problem, not an animation problem.



\---



\## AHA 4 — Duplicate widget instances caused misleading UI behavior



Symptoms:



\- overlapping values,

\- two slider thumbs.



Cause:



multiple `WBP\_AttackStrengthTest` instances



The widget creation path now checks whether one already exists before creating another.



\---



\## AHA 5 — Runtime Physics Control tuning works



Physics Control parameters can be changed during PIE using runtime setters.



This makes live stat experiments practical.



\---



\# ABANDONED / AVOIDED PATHS



\## Mixamo retargeting



Earlier path:



`RTG\_Mixamo\_To\_GASP`



was abandoned.



The current sword attack works directly with:



`SK\_UEFN\_Mannequin`



Do not restart the old retargeting investigation unless new evidence requires it.



\---



## Cleaner animation / rig exploration — historical outcome (2026-09-15)

Animation/rig solutions, including Cascadeur, were explored in pursuit of a cleaner sword swing. Cascadeur was tried but ultimately not used for the successful result. Unreal MetaHuman mocap was the successful final approach, producing `/Game/TestSwordAnimations/MocapAnimations/AS_Mocap_SwordSwing_01_RH_UEFN1`. No specific Cascadeur failure cause was recorded; do not infer that the tool is unusable.

---

\# HIT REACTION STATUS



Current sword hit logic is prototype-only.



On sword collision with another `SandboxCharacter\_Mover`, the victim currently enables physics below:



`pelvis`



This produces physical reaction.



Important observation:



The opponent's head visibly reacted to a strong sword impact even without explicitly feeding `Normal Impulse` into an `Add Impulse` node.



Current hit/ragdoll logic is \*\*not final combat/damage architecture\*\*.



\---



\# CURRENT ROADMAP

## Two AI fighters, one overhead-swing skill — tested / completed (2026-09-15)

- [x] `L_CombatPrototype` contains two AI-controlled `SandboxCharacter_Mover` duelists using `BP_FIghterAIController`.
- [x] `Pawn0 - Team Red` is Team 1 and `Pawn1 - Team Blue` is Team 2. They begin face-to-face at the proven 150 cm strike-test spacing.
- [x] Both autonomously repeat the existing reusable `Attack` event. A randomized first-attack offset prevents deterministic simultaneous lethal trades.
- [x] Death immediately clears attack authority before ragdoll, preventing a dead fighter's sword from applying a post-mortem hit.
- [x] The locally controlled player pawn is Team 0: it is an observer, hides its fighter health widget, rejects combat damage, and is never used by the duel controller.
- [x] The overhead widget now shows a compact name/team line above the health bar at approximately one head-height above the fighter.
- Runtime validation: both custom AI controllers attacked and exchanged damage; one fighter died and ragdolled while the other survived (tested with both Red and Blue winning on separate runs); the player observer remained at 100 health.

Reliable notify-driven strike windows, navigation/engagement movement, and recovery/self-snag checks remain future combat work.
---

## Completed immediate goal — cleaner sword animation

- [x] Complete cleaner-animation exploration and obtain the successful Unreal MetaHuman mocap overhead swing (see completed milestone and exploration history above).




\## Phase 1 — Clean combat prototype level



Completed:



\- \[x] Duplicate the current known-working GASP level.

\- \[x] Create a dedicated combat-development level: `L\_CombatPrototype`.

\- \[x] Remove demo clutter gradually.

\- \[x] Preserve hidden GASP dependencies.

\- \[x] Test after every cleanup batch.



Cleanup completed and Play-tested successfully. Removed Traversal Gym, teleporters, TargetDummy, floor buttons, demo widgets, arrows/signage and related clutter. Intentionally kept the Landscape/hill for future slope, balance and footing tests. The level retains the working combat setup and core environment actors.

Always verify:



\- movement,

\- camera,

\- two fighters,

\- Key 1 attack,

\- sword physics,

\- Physics Control,

\- hit reaction,

\- strength slider.



Do not mass-delete GASP Content assets yet.



\---



\## Phase 2 — Separate tracking from physical strength



Pending experiment after the active single-skill AI duel goal:



`Angular Strength vs Max Torque`



Current hypothesis:



Angular Strength → tracking / skill  

Max Torque → physical effort / strength



This has NOT been proven yet.



Tasks:



\- \[ ] Add runtime Max Torque setter.

\- \[ ] Add live Max Torque debug control.

\- \[ ] Test high/low Angular Strength against high/low Max Torque.

\- \[ ] Determine what each parameter actually represents best.

\- \[ ] Record results here.



\---



\## Phase 3 — Weapon mass and inertia



\- \[ ] Test light vs heavy weapons.

\- \[ ] Test weak fighter + heavy weapon.

\- \[ ] Test strong fighter + heavy weapon.

\- \[ ] Investigate center of mass.

\- \[ ] Investigate inertia.

\- \[ ] Test interaction with Max Torque.

\- \[ ] Investigate `Use Acceleration Drive Mode = false`.



Do not change drive mode casually; expect retuning.



\---



\## Phase 4 — Whole-body physical fighter



\- \[ ] Add torso contribution.

\- \[ ] Add left-arm contribution.

\- \[ ] Develop posture control.

\- \[ ] Develop balance.

\- \[ ] Develop impact disturbance.

\- \[ ] Develop recovery from disturbance.

\- \[ ] Decide when partial/full ragdoll should occur.



\---



\## Phase 5 — Physical defense



\- \[ ] Reliable weapon-vs-weapon collision.

\- \[ ] Incoming weapon trajectory estimation.

\- \[ ] Interception prediction.

\- \[ ] Defensive target generation.

\- \[ ] Physical block attempt.

\- \[ ] Strength/skill influence block success.



\---



\## Phase 6 — Combat AI



Eventually AI should control:



\- positioning,

\- distance,

\- attack selection,

\- attack timing,

\- defense timing,

\- trajectory prediction,

\- interception,

\- fatigue awareness,

\- recovery,

\- risk.



The AI chooses intent.



Physics determines execution.



\---



\# DEVELOPMENT RULES



Prefer small experiments that answer one question at a time.



Avoid prematurely building:



\- huge attack libraries,

\- huge block libraries,

\- complex final AI,

\- final stat systems,

\- final damage systems,

\- large-scale GASP asset cleanup.



Before debugging something difficult, check this document first.



If an experiment reveals something useful, update this document immediately.



Even a result such as:



`This approach does not work because...`



is valuable project knowledge.



\---



\# CURRENT NEXT ACTION



Create two AI-controlled fighter pawns that use only the overhead-swing skill from `/Game/TestSwordAnimations/MocapAnimations/AS_Mocap_SwordSwing_01_RH_UEFN1` and autonomously fight each other until one dies. Preserve the validated guard / locomotion / montage flow and reuse the existing AI controller / Attack event foundation where applicable.

The health/death/team foundation and a basic montage-active, one-hit-per-attack damage gate are now complete. Precise notify-driven strike windows and recovery / self-snag checks remain relevant to the upcoming duel. Phase 2 torque-vs-tracking experiments remain pending.

The autonomous two-fighter duel is implemented and runtime-tested. The next combat work should focus on notify-driven strike windows, engagement movement, and recovery/self-snag checks.



\---



\# CORE IDENTITY OF IRONBOUND



The target is not:



> sword animations with ragdoll noise added on top.



The target is:



> a fighter whose physical capability determines whether its intended action actually succeeds.



In short:



AI decides what the fighter wants to do.  

Animation describes intended motion.  

Stats determine physical capability.  

Physics determines what actually happens.


