# UE 5.8 sword mocap-to-gameplay guide

Ironbound's proven monocular sword workflow, recorded 2026-09-15. Project engine: UE 5.8.2.

This guide records the successful session procedure supplied by the project owner. The documentation update does not process footage, change animation assets, replace Montage segments, or test gameplay.

## 1. Ingest the original recording

1. Open **Live Link Hub > Capture Manager > Mono Video Ingest**.
2. Ingest the original sword recording.
3. Create/use a **MetaHuman Performance** asset with **Monocular Footage**, selecting the ingested footage.
4. Isolate each attack with **Start Frame to Process** and **End Frame to Process**. Do not crop the source videos into separate attack files. Keep the source recording reusable and record the chosen frame range for each attack.

If the camera footage is horizontally mirrored, leave it as captured for solving, export, and retargeting. Correct handedness on a duplicate of the retargeted UEFN animation in step 4.

## 2. Solve the selected attack and export

Use these proven Performance settings:

| Setting | Value |
| --- | --- |
| Facial Tracking | OFF |
| Body Tracking | ON |
| Auto Body Height | ON |
| Enable Foot-Locking | ON |
| Start Frame to Process / End Frame to Process | Selected attack's range |

Process the selected range. Preview the solve, including preparation, strike, and recovery, before exporting.

Use these export settings:

| Setting | Value |
| --- | --- |
| Export range | Processing Range |
| Auto Save Anim Sequence | ON |
| Face | OFF |
| Body | ON |
| Export Skeleton | Performer Skeleton |
| Unsolved Frame Behavior | Last Valid Frame |
| Curve Interpolation | Linear |
| Remove Redundant Curve Keys | ON |

Keep the Performer Skeleton export as the source for retargeting. Do not select the UEFN skeleton at this export step.

## 3. Retarget to the gameplay mannequin

1. Select the exported Performer Skeleton animation and use UE 5.8 **Retarget Animation Assets**.
2. Set the target mesh to **SKM_UEFN_Mannequin**.
3. Use **Auto Generate Retargeter**. Successful templates in this session were **UE5 Mannequin** for the source and **Fortnite Humanoid** for the target.
4. Preview and export the retargeted animation. The concrete batch/attack sequence is **AS_Mocap_SwordBatch02_Attack01_UEFN**.
5. Save a clean, unmodified retargeted master before applying any modifiers.

These template choices are the proven pairing for this capture, not a reason to restart the abandoned Mixamo retargeting pipeline.

## 4. Correct mirrored footage after retargeting

1. Duplicate the clean UEFN animation. Keep the original master unmodified.
2. Open the duplicate and use **Animation Data Modifiers > MirrorModifier**.
3. Assign **MDT_UEFN_Mannequin**.
4. Apply the modifier and save.
5. Preview the resulting handedness and complete sword arc. Use this final mirrored retargeted sequence for gameplay.

**Validated lesson:** clean retargeting followed by **MirrorModifier** produced the correct frontal swing. **ReOrientRootBoneModifier** and manual pelvis rotation were unnecessary/wrong remedies for the perceived stance yaw in this case. Do not stack those changes onto the clean result. If experiments have contaminated a working copy, start again from the unmodified master and apply the mirror step to a fresh duplicate.

### Concrete assets and local verification

Local files inspected on 2026-09-15:

| Asset | Location / role |
| --- | --- |
| SK_Mocap_SwordBatch02_Attack01 | `Content/TestSwordAnimations/MocapAnimations/`; batch attack skeletal asset |
| AS_Mocap_SwordBatch02_Attack01 | Same folder; source animation asset present locally |
| AS_Mocap_SwordBatch02_Attack01_UEFN | Same folder; retargeted attack sequence |
| Final mirrored version | Use the saved mirrored retargeted result; see naming caveat below |
| SKM_UEFN_Mannequin | `Content/Characters/UEFN_Mannequin/Meshes/`; target skeletal mesh |
| MDT_UEFN_Mannequin | `Content/Characters/UEFN_Mannequin/Rigs/`; mirror data table |
| AM_Mocap_SwordSwing_01 | `Content/TestSwordAnimations/MocapAnimations/`; existing gameplay Montage |

**Naming caveat:** no separately named mirrored Batch02 Attack01 sequence was found in the inspected local files. The saved `AS_Mocap_SwordBatch02_Attack01_UEFN` binary contains references to `MirrorModifier` and `MDT_UEFN_Mannequin`, and the Montage references that sequence. Those references alone do not establish that the modifier was applied successfully or that a clean master was preserved. Confirm the final mirrored asset and master in Unreal; do not invent a verified mirrored suffix or assume an unmodified master exists. For future processing, follow the duplicate-before-modifying procedure above.

## 5. Put the final sequence into the existing attack Montage

The existing attack Montage is **AM_Mocap_SwordSwing_01**, using **DefaultGroup.SwordAttackSlot**.

1. Open this existing Montage.
2. Replace only its old animation segment with the final mirrored retargeted sequence.
3. Preserve the **Montage asset identity, slot, sections, and notifies** so existing pawn references continue to use the same Montage.
4. Check segment start/end, play rate, duration, section boundaries, and notify timing against the new motion. A preserved notify can still land at the wrong phase if clip duration changes; flag any needed timing adjustment for a separate gameplay/asset task rather than silently deleting or rebuilding the Montage structure.
5. Save and validate through the existing attack trigger in Play: preparation, frontal strike, recovery, guard return, and physical sword behavior.

### What is verified about wiring

- The main character Blueprint is `SandboxCharacter_Mover` with a reusable `Attack` event, and `SwordAttackSlot` sits after stance selection in `SandboxCharacter_Mover_ABP`. Its older attack-test entry was `AM_TestSword_CleanSwing1` with temporary Key 1 input.
- Read-only inspection of the local `Content/Blueprints/SandboxCharacter_Mover.uasset` found an `AM_Mocap_SwordSwing_01` reference and Play Montage-related names.
- The local Montage binary contains `SwordAttackSlot` and an `AS_Mocap_SwordBatch02_Attack01_UEFN` reference. The full `DefaultGroup.SwordAttackSlot` designation is the project owner's confirmed setup.
- Binary name/reference inspection does **not** prove graph connections, the active input path, callback wiring, or runtime behavior. Exact current Blueprint execution wiring must be checked in Unreal before describing node-by-node connections. This update makes no such claim.

Preserving the Montage is the intended integration method; do not create a replacement Montage or rewire the pawn as part of this animation swap.

## Gameplay orientation lesson

**Actor/capsule forward is AI and gameplay forward.** The sword's diagonal guard direction does not redefine where the fighter faces. Judge the attack over its whole arc: it should cover target space in front of the actor, even when the starting guard points diagonally.

Use an actor-forward target placement to assess the swing. Do not rotate the pelvis or root just to make a guard pose's sword direction resemble a forward arrow. Future target-facing logic or **Motion Warping** can handle small alignment corrections; these are future options, not verified existing wiring or a substitute for a correctly retargeted and mirrored clip.

## End-to-end checklist

- [ ] Ingest original recording through Live Link Hub Capture Manager > Mono Video Ingest.
- [ ] Use a MetaHuman Performance asset with Monocular Footage.
- [ ] Record Start Frame to Process / End Frame to Process for this attack; retain original video.
- [ ] Set Facial Tracking OFF, Body Tracking ON, Auto Body Height ON, Enable Foot-Locking ON.
- [ ] Process and preview the selected range, leaving mirrored camera footage as captured.
- [ ] Export Processing Range with all seven export options in step 2 verified.
- [ ] Retarget the Performer Skeleton animation to SKM_UEFN_Mannequin with Auto Generate Retargeter; use the proven source/target templates.
- [ ] Save an unmodified retargeted master, then duplicate it.
- [ ] Apply MirrorModifier with MDT_UEFN_Mannequin to the duplicate; save and check the frontal swing.
- [ ] Confirm the actual final mirrored sequence name and preserve the clean master.
- [ ] Replace only the old segment in AM_Mocap_SwordSwing_01; retain DefaultGroup.SwordAttackSlot, sections, and notifies.
- [ ] Review timing and test the existing attack path in Play, including recovery and actor-forward coverage.
- [ ] Record observed results; do not mark runtime validation complete based only on a sequence preview.

## Troubleshooting and lessons learned

| Symptom / temptation | Proven lesson or next check |
| --- | --- |
| Long recording contains several attacks | Select each attack with processing start/end frames; do not crop source videos. |
| Footage has reversed handedness | Solve/export/retarget as captured, then mirror a duplicate on the UEFN skeleton. |
| Guard appears diagonally oriented | Check actor/capsule forward and the complete arc before assuming a yaw defect. |
| Considering root reorientation or manual pelvis rotation | Those were wrong/unnecessary for this case; restore clean retarget + MirrorModifier. |
| Unclear whether an animation is already mirrored | Inspect modifier/application state and preview in Unreal; names and binary references alone are insufficient. Keep a clean master. |
| Frozen/stalled motion in part of the solve | Check for unsolved frames: Last Valid Frame can hold the last valid pose. Review footage/range and solve quality before accepting export. |
| New sequence previews correctly but gameplay differs | Verify the existing Montage segment, slot path, timing, and physical setup in Unreal. Physics Control and sword constraint issues can distort otherwise good animation. |
| Considering recreating the Montage | Preserve AM_Mocap_SwordSwing_01 and its structure; replace only the sequence segment to retain existing references. |
| Small target alignment error remains | Assess actor-facing and target placement. Target-facing/Motion Warping are future alignment work, not part of this documentation change. |
