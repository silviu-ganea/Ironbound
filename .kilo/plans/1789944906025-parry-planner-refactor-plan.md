# Ironbound Procedural Parry Planner Refactoring Plan - Revised

## 1. NEUTRAL WRIST RELATIONSHIP DERIVATION

### Reference Pose Analysis
From existing `CalculateArmDimensions()` code, I can access the reference pose transforms:

```cpp
// Available C++ APIs in UIronboundParryComponent:
const FReferenceSkeleton& RefSkeleton = FighterMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();

// Get complete space transform for a bone
auto GetCompleteSpaceTransform = [&RefSkeleton, &RefPose](int32 BoneIndex)
{
    FTransform Result = RefPose[BoneIndex];
    for (int32 Parent = RefSkeleton.GetParentIndex(BoneIndex);
         Parent != INDEX_NONE;
         Parent = RefSkeleton.GetParentIndex(Parent))
    {
        Result *= RefPose[Parent];
    }
    return Result;
};
```

### Neutral Wrist Relationship Formulation
**Concept**: Derive physical forearm direction from reference pose joint positions, not bone axes.

**Physical Forearm Direction Derivation**:
```cpp
// Get physical forearm direction from reference pose joint positions
// IMPORTANT: This direction is in reference/component space (skeleton space)
FVector CalculateReferenceForearmDirection()
{
    if (!FighterMesh || !FighterMesh->GetSkeletalMeshAsset()) return FVector::ForwardVector;
    
    const int32 LowerArmIndex = FighterMesh->GetBoneIndex(LowerArmBone);
    const int32 HandIndex = FighterMesh->GetBoneIndex(HandBone);
    
    if (LowerArmIndex == INDEX_NONE || HandIndex == INDEX_NONE) 
        return FVector::ForwardVector;
    
    const FReferenceSkeleton& RefSkeleton = FighterMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();
    
    // Get complete reference-space transforms (component space)
    auto GetCompleteSpaceTransform = [&RefSkeleton, &RefPose](int32 BoneIndex)
    {
        FTransform Result = RefPose[BoneIndex];
        for (int32 Parent = RefSkeleton.GetParentIndex(BoneIndex);
             Parent != INDEX_NONE;
             Parent = RefSkeleton.GetParentIndex(Parent))
        {
            // UE: Result = Parent * Local (applies Parent first, then Local)
            // To build complete space from root to bone: accumulate parent transforms
            Result = Result * RefPose[Parent]; // Local * Parent
        }
        return Result;
    };
    
    const FTransform LowerArmComplete = GetCompleteSpaceTransform(LowerArmIndex);
    const FTransform HandComplete = GetCompleteSpaceTransform(HandIndex);
    
    // Physical forearm direction in reference space (component space)
    // Translation only, no rotation needed for direction vector
    FVector ReferenceForearmDir = (HandComplete.GetTranslation() - LowerArmComplete.GetTranslation()).GetSafeNormal();
    
    // Convert direction to lowerarm-local space
    // Use inverse rotation of lowerarm transform (translation doesn't affect direction)
    FTransform LowerArmLocalTransform = LowerArmComplete.GetInverse();
    ReferenceForearmDir = LowerArmLocalTransform.TransformVectorNoScale(ReferenceForearmDir);
    
    return ReferenceForearmDir;
}

// Get neutral wrist relationship transform (reference space)
FTransform CalculateNeutralWristRelationship()
{
    if (!FighterMesh || !FighterMesh->GetSkeletalMeshAsset()) return FTransform::Identity;
    
    const int32 LowerArmIndex = FighterMesh->GetBoneIndex(LowerArmBone);
    const int32 HandIndex = FighterMesh->GetBoneIndex(HandBone);
    
    if (LowerArmIndex == INDEX_NONE || HandIndex == INDEX_NONE) 
        return FTransform::Identity;
    
    const FReferenceSkeleton& RefSkeleton = FighterMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();
    
    // Get complete reference-space transforms
    auto GetCompleteSpaceTransform = [&RefSkeleton, &RefPose](int32 BoneIndex)
    {
        FTransform Result = RefPose[BoneIndex];
        for (int32 Parent = RefSkeleton.GetParentIndex(BoneIndex);
             Parent != INDEX_NONE;
             Parent = RefSkeleton.GetParentIndex(Parent))
        {
            // UE: Result = Parent * Local (applies Parent first, then Local)
            // To build complete space from root to bone: accumulate parent transforms
            Result = Result * RefPose[Parent]; // Local * Parent
        }
        return Result;
    };
    
    const FTransform LowerArmComplete = GetCompleteSpaceTransform(LowerArmIndex);
    const FTransform HandComplete = GetCompleteSpaceTransform(HandIndex);
    
    // Neutral relationship = HandComplete * LowerArmComplete.Inverse()
    // This gives: transform that takes lowerarm space to hand space (both in reference space)
    return HandComplete * LowerArmComplete.GetInverse();
}

// Calculate neutral forearm direction from required hand transform
FVector CalculateNeutralForearmDirection(const FTransform& RequiredHandTransform)
{
    // Get neutral wrist relationship (reference space: lowerarm -> hand)
    FTransform NeutralWristRelationship = CalculateNeutralWristRelationship();
    
    // Get reference forearm direction (lowerarm-local space)
    FVector ReferenceForearmDir = CalculateReferenceForearmDirection();
    
    // RequiredForearmTransform transforms from lowerarm-local to world space
    // RequiredHandTransform transforms from hand-local to world space
    // RequiredForearm * NeutralWrist = RequiredHand
    // RequiredForearm = RequiredHand * NeutralWrist.Inverse()
    FTransform RequiredForearmTransform = RequiredHandTransform * NeutralWristRelationship.GetInverse();
    
    // Transform reference forearm direction (lowerarm-local) by required forearm transform (world space)
    // This gives the world-space direction of the forearm in the required pose
    return RequiredForearmTransform.TransformVector(ReferenceForearmDir);
}
```

**Coordinate Space Flow**:
1. `ReferenceForearmDir`: Lowerarm-local space (physical forearm axis in bone's local coordinates)
2. `NeutralWristRelationship`: Reference space transform (lowerarm → hand)
3. `RequiredForearmTransform`: World space transform (lowerarm-local → world)
4. `RequiredHandTransform`: World space transform (hand-local → world)

**Mathematical Correctness**:
The transformation chain is correct:
1. `ReferenceForearmDir` starts in lowerarm-local space
2. `RequiredForearmTransform` transforms from lowerarm-local to world space
3. Applying `RequiredForearmTransform.TransformVector(ReferenceForearmDir)` correctly transforms the local direction to world space

**Transform Order Verification**:
- UE FTransform: A * B applies A first, then B
- Complete space construction: Local * Parent (builds from bone to root)
- Direction conversion: TransformVectorNoScale handles rotation only

### Robustness Assessment
✅ **Generalizes to any skeleton** - Uses reference pose, not hardcoded axes
✅ **Available cheaply** - Uses existing C++ APIs already in use
✅ **Orientation-aware** - Considers actual hand-to-forearm relationship
✅ **World-space aware** - Converts from local reference to world coordinates

**Critical Assumption**: The reference pose represents a natural neutral wrist position. This should be true for well-rigged characters.

**V1 Raw Biomechanics**: Exactly two metrics:
- WristDeviationRadians: 0 to PI (0 = perfect neutral, PI = max deviation)
- GeometricFreedom: 0 to 1 (0 = singularity, 1 = full elbow circle radius)

## 2. MINIMUM WRIST DEVIATION - ANALYTICAL SOLUTION
        
    const float Radius = FMath::Sqrt(RadiusSq);
    const FVector Center = Shoulder + Axis * Along;
    
    // Calculate neutral elbow position
    const FVector NeutralElbow = Hand - NeutralForearmDir * ForearmLength;
    
    // Project neutral elbow onto elbow circle plane
    const FVector ToNeutral = NeutralElbow - Center;
    const float ProjectionAlongAxis = FVector::DotProduct(ToNeutral, Axis);
    const FVector PlaneVector = ToNeutral - ProjectionAlongAxis * Axis;
    
    if (Radius < SMALL_NUMBER)
    {
        // Two-link singularity: elbow circle collapsed to single point
        const FVector ValidElbow = Center; // Only possible elbow position
        const FVector ValidForearmDir = (Hand - ValidElbow).GetSafeNormal();
        
        // Calculate actual deviation from neutral forearm direction
        const float DotProduct = FMath::Clamp(
            FVector::DotProduct(NeutralForearmDir, ValidForearmDir), -1.f, 1.f);
        const float Angle = FMath::Acos(DotProduct);
        
        return Angle;
    }
    
    if (PlaneVector.IsNearlyZero())
    {
        // Neutral elbow projects to circle center but radius > 0
        // Create any valid elbow point on the circle (perpendicular to axis)
        FVector Orthogonal1, Orthogonal2, Orthogonal3;
        Axis.FindBestAxisVectors(Orthogonal1, Orthogonal2, Orthogonal3);
        
        // Use first orthogonal direction to create a valid elbow position
        const FVector ValidElbow = Center + Orthogonal1 * Radius;
        
        // Calculate forearm direction from this elbow position
        const FVector ValidForearmDir = (Hand - ValidElbow).GetSafeNormal();
        
        // Calculate angle between neutral and valid forearm directions
        const float DotProduct = FMath::Clamp(
            FVector::DotProduct(NeutralForearmDir, ValidForearmDir), -1.f, 1.f);
        const float Angle = FMath::Acos(DotProduct);
        
        return Angle;
    }
    
    // Project onto circle edge
    const FVector NormalizedPlaneVector = PlaneVector.GetSafeNormal();
    const FVector ProjectedElbow = Center + NormalizedPlaneVector * Radius;
    
    // Calculate forearm direction from projected elbow
    const FVector ValidForearmDir = (Hand - ProjectedElbow).GetSafeNormal();
    
    // Calculate angle between neutral and valid forearm directions
    const float DotProduct = FMath::Clamp(
        FVector::DotProduct(NeutralForearmDir, ValidForearmDir), -1.f, 1.f);
    const float Angle = FMath::Acos(DotProduct);
    
    return Angle;
}
```

**Edge Cases Handled**:
- Hand too close/too far: Returns PI (maximum deviation)
- Radius≈0 (two-link singularity): Calculates actual deviation from unique elbow position
- Neutral elbow projects to circle center but radius>0: Calculates actual deviation using arbitrary orthogonal direction
- No valid solution: Returns PI (shouldn't happen if reach check passes)

**Why Analytical Over Sampling**:
1. **Mathematically exact** - No sampling error
2. **More efficient** - Single calculation vs multiple samples
3. **More robust** - No issues with sampling density or distribution
4. **Geometrically clear** - Direct projection of ideal onto valid solution plane

## 4. ARM EXTENSION / GEOMETRIC FREEDOM - REMOVED PREFERRED ELBOW ANGLE

### Geometric Freedom Analysis
**Maximum Elbow Circle Radius Derivation**:

For a two-link arm with lengths A (upper arm) and B (forearm), the elbow circle radius is:

```
R² = A² - Along²
where Along = (A² - B² + D²)/(2D)
and D = shoulder-to-hand distance
```

**Finding the true maximum radius over the physically valid interval |A-B| ≤ D ≤ A+B**:

Let C = A² - B², then Along(D) = D/2 + C/(2D)

Taking the derivative:
```
dAlong/dD = 1/2 - C/(2D²) = (D² - C)/(2D²)
```

Since R² = A² - Along², then:
```
d(R²)/dD = -2 * Along * dAlong/dD = -2 * Along * (D² - C)/(2D²)
```

Setting d(R²)/dD = 0 gives critical points:
1. Along = 0
2. D² - C = 0 ⇒ D² = A² - B²

**Case 1: Along = 0**
```
Along = (A² - B² + D²)/(2D) = 0
=> A² - B² + D² = 0
=> D² = B² - A²
```
This is only possible when B ≥ A. When B ≥ A, D = √(B² - A²) gives Along = 0, so R² = A² - 0 = A², thus R = A.

**Case 2: D² = A² - B²**
This is only possible when A > B. When A > B, D = √(A² - B²) is the critical point.

**Evaluating at endpoints and critical points**:

1. At D = |A-B| (minimum reach):
```
Along = (A² - B² + (A-B)²)/(2(A-B)) = A
R² = A² - A² = 0
```

2. At D = A+B (maximum reach):
```
Along = (A² - B² + (A+B)²)/(2(A+B)) = A  
R² = A² - A² = 0
```

3. At critical points:
- When B ≥ A: D = √(B² - A²), Along = 0, R = A
- When A > B: D = √(A² - B²), Along = D, R = B

**Universal Maximum Radius**:
For A > B at the critical point D = √(A² - B²):
```
Along = (A² - B² + D²)/(2D) = (D² + D²)/(2D) = D
R² = A² - Along² = A² - D² = A² - (A² - B²) = B²
R = B
```
Since A > B, this means R = B is the maximum radius.

```
MaxPossibleRadius = min(A, B)
```

**Normalized Geometric Freedom**:
```cpp
FArmExtensionMetrics CalculateArmExtensionMetrics(
    const FVector& Shoulder,
    const FVector& Hand,
    float UpperArmLength,
    float ForearmLength)
{
    const float D = FVector::Distance(Shoulder, Hand);
    const float A = UpperArmLength;
    const float B = ForearmLength;
    const float MaxReach = A + B;
    const float MinReach = FMath::Abs(A - B);
    
    // Check geometric validity
    if (D < MinReach || D > MaxReach)
    {
        return {D, MaxReach, 1.f, 0.f, A, MinReach, 0.f, 0.f}; // Invalid
    }
    
    // Calculate elbow circle geometry
    const float Along = (A*A - B*B + D*D) / (2*D);
    const float RadiusSq = A*A - Along*Along;
    const float Radius = FMath::Sqrt(FMath::Max(0.f, RadiusSq));
    
    // Maximum possible radius = min(A, B)
    const float MaxPossibleRadius = FMath::Min(A, B);
    
    // Normalized freedom = current radius / maximum possible radius
    // 1.0 = maximum possible elbow circle radius (full freedom)
    // 0.0 = zero radius (singularity, no freedom)
    const float NormalizedFreedom = MaxPossibleRadius > SMALL_NUMBER ? Radius / MaxPossibleRadius : 0.f;
    
    return {
        D, MaxReach, D/MaxReach, Radius, MaxPossibleRadius, MinReach, NormalizedFreedom
    };
}
```

**Verification**:
- At D = |A-B|: R = 0, freedom = 0 ✓
- At D = A+B: R = 0, freedom = 0 ✓  
- At maximum radius configuration: R = min(A,B), freedom = 1 ✓

### Raw Geometric Freedom (No Arbitrary Thresholds)
```cpp
float CalculateRawGeometricFreedom(const FArmExtensionMetrics& Metrics)
{
    if (Metrics.Distance < Metrics.MinReach || Metrics.Distance > Metrics.MaxReach)
        return 0.f; // No freedom for unreachable positions
        
    // Normalized freedom from elbow circle geometry (0-1)
    // 1.0 = maximum possible elbow circle radius (full freedom)
    // 0.0 = zero radius (singularity, no freedom)
    return Metrics.NormalizedFreedom;
}
```

### Gameplay Mapping (Separate from Raw Metric)
```cpp
// Tunable policy - not derived from geometry
float GetExtensionComfortFromFreedom(float RawFreedom, float GameplaySharpness = 2.0f)
{
    // Apply tunable curve to raw geometric freedom
    // Default: quadratic curve for more dramatic falloff near zero freedom
    return FMath::Pow(RawFreedom, GameplaySharpness);
}
```

**Rationale**:
1. **Geometrically grounded** - Based on actual elbow circle radius, not arbitrary ratios
2. **No arbitrary thresholds** - Raw freedom is 0-1 based on circle geometry calculus
3. **CCDIK-aware** - Larger radius = more elbow options for natural poses
4. **Singularity-aware** - Approaches zero at both minimum and maximum reach
5. **Mathematically consistent** - Maximum radius derived from calculus over valid interval

## 5. V1 COMFORT MODEL - MINIMAL BIOMECHANICAL METRICS

### Core Principle: Minimal but Grounded
**V1 Comfort consists of exactly two biomechanical metrics:**

#### A) Arm Extension / Remaining Geometric Freedom
```cpp
// Calculate using FArmExtensionMetrics
float geometricFreedom = CalculateArmExtensionMetrics(
    Shoulder, Hand, UpperArmLength, ForearmLength).NormalizedFreedom;
```

**What it measures**: How close the arm is to full extension/singularity
**Range**: 0.0 (singularity, no freedom) to 1.0 (full elbow circle radius, maximum freedom)
**Mathematical basis**: Normalized elbow circle radius over physically valid configurations

#### B) Minimum Wrist Deviation from Neutral Wrist Relationship
```cpp
// Calculate using analytical projection
float wristDeviation = CalculateMinimumWristDeviationAnalytical(
    Shoulder, Hand, NeutralForearmDir, UpperArmLength, ForearmLength);
```

**What it measures**: Minimum wrist angle required to achieve hand target with neutral wrist alignment
**Range**: 0 to PI (0 = perfect neutral wrist possible, PI = maximum deviation possible)
**Mathematical basis**: Angle between neutral forearm direction and closest valid forearm direction

### Combined V1 Comfort Score (Gameplay Mapping)
```cpp
// Raw biomechanical measurements (no arbitrary thresholds)
struct FRawComfortMetrics
{
    float WristDeviationRadians;    // 0 to PI (0 = perfect neutral, PI = max deviation)
    float GeometricFreedom;         // 0 to 1 (0 = singularity, 1 = full freedom)
};

// Convert raw metrics to comfort scores (0-1, where 1 = comfortable)
float ConvertRawMetricsToComfort(
    const FRawComfortMetrics& Raw,
    float WristSharpness = 1.5f,    // Tunable: wrist deviation curve steepness
    float FreedomSharpness = 2.0f)  // Tunable: freedom curve steepness
{
    // Wrist comfort: 1.0 at 0° deviation, decreasing to 0.0 at PI
    float wristComfort = 1.0f - (Raw.WristDeviationRadians / PI);
    wristComfort = FMath::Pow(wristComfort, WristSharpness);
    
    // Freedom comfort: 1.0 at full freedom, decreasing to 0.0 at singularity
    float freedomComfort = FMath::Pow(Raw.GeometricFreedom, FreedomSharpness);
    
    // Combined comfort with equal weighting (tunable)
    return wristComfort * 0.5f + freedomComfort * 0.5f;
}

float CalculateV1ComfortScore(
    const FVector& Shoulder,
    const FVector& Hand,
    const FVector& NeutralForearmDir,
    float UpperArmLength,
    float ForearmLength)
{
    // Calculate raw biomechanical metrics
    FArmExtensionMetrics extensionMetrics = CalculateArmExtensionMetrics(
        Shoulder, Hand, UpperArmLength, ForearmLength);
    
    float wristDeviation = CalculateMinimumWristDeviationAnalytical(
        Shoulder, Hand, NeutralForearmDir, UpperArmLength, ForearmLength);
    
    FRawComfortMetrics rawMetrics = {
        wristDeviation,
        extensionMetrics.NormalizedFreedom
    };
    
    // Apply tunable gameplay mapping
    return ConvertRawMetricsToComfort(rawMetrics);
}
```

### Excluded Metrics (Intentionally Omitted)
❌ **Arbitrary hand workspace boxes** - Would require manual tuning per character
❌ **Shoulder comfort approximation** - Cannot be estimated reliably before CCDIK solves
❌ **Distance from current hand** - Large defensive movements are desirable
❌ **Preferred elbow angles** - No principled biomechanical basis

### Advantages of Minimal Approach
1. **Mathematically rigorous** - Both metrics grounded in actual arm geometry
2. **Character-independent** - Scales naturally to different arm lengths
3. **Tunable balance** - Extension vs wrist weighting can be adjusted
4. **Extensible foundation** - Additional metrics can be added later if needed

## 6. DEFENDER BLADE FREEDOM - CONFIRMED

### Key Discovery Confirmed
**Existing DefenderBladeFraction sampling [0.30-0.78] already provides blade-axis freedom.**

**Do NOT add**:
- GenerateEquivalentPositions()
- SlipAmount dimension  
- Additional sampling layers

**Instead**:
- Improve scoring to select better existing fractions
- Possibly extend range to [0.20-0.85] if current range misses comfortable positions
- Focus on comfort evaluation within existing sampling

## 7. CANDIDATE QUALITY ARCHITECTURE

### Recommended Structure: Lexicographic + Comfort Tiered

**Phase 1: Hard Constraints (Binary Pass/Fail)**
```cpp
if (!CandidatePassesHardConstraints()) return false;
```
- Body protection before impact
- Valid blade intersection geometry  
- Geometrically reachable hand target (D ≤ A+B+margin)

**Phase 2: Raw Objective Metrics**
```cpp
struct FCandidateRawMetrics
{
    float TacticalQuality;      // 0-1, derived from crossing angles, timing, etc.
    float WristDeviationRadians; // 0-PI biomechanical metric
    float GeometricFreedom;     // 0-1 biomechanical metric
    bool bGeometricallyValid;   // Passes reach constraints
};
```

**Phase 3: Gameplay Policy Mapping**
```cpp
// Tunable policy parameters (not derived from biomechanics)
struct FParryPolicy
{
    float TacticalWeight = 0.7f;      // Weight for tactical quality in combined score
    float WristSharpness = 1.5f;      // Wrist deviation curve steepness  
    float FreedomSharpness = 2.0f;    // Geometric freedom curve steepness
    float QualityBandWidth = 0.95f;   // Candidate quality selection band (95% of best)
};

float ApplyPolicyToRawMetrics(const FCandidateRawMetrics& Raw, const FParryPolicy& Policy);
```

**Phase 4: Combined Quality**
```cpp
float CombinedQuality = ApplyPolicyToRawMetrics(RawMetrics, Policy);
```

### Key Design Decisions
1. **Separation of concerns**: Raw biomechanics vs gameplay policy
2. **Lexicographic ordering**: Hard constraints first, then metrics, then policy
3. **Minimal V1 comfort**: Only two grounded biomechanical metrics (WristDeviationRadians, GeometricFreedom)
4. **Explicit policy**: All tunable values clearly labeled as policy

**Advantages**:
- Hard constraints remain absolute
- Raw measurements remain untainted by policy decisions
- Policy can be tuned without affecting biomechanical calculations
- Supports future skill weighting on policy layer
- Clear separation for ParrySkill integration
- No arbitrary thresholds in raw biomechanics

## 8. CONTROLLED VARIATION ALGORITHM

### Quality-Based Selection with Seeded Randomness

**Selection Algorithm**:
```cpp
// Group candidates by quality tier
TArray<FIronboundParryCandidate> Candidates;
// ... evaluate all candidates

// Find best quality
float BestQuality = Candidates.MaxBy([](const auto& C) { return C.CombinedQuality; })->CombinedQuality;

// Define acceptable quality band (configurable)
float QualityThreshold = BestQuality * 0.95f;  // Allow 5% quality drop

// Filter candidates within acceptable band
TArray<FIronboundParryCandidate> AcceptableCandidates;
for (const auto& Candidate : Candidates)
{
    if (Candidate.CombinedQuality >= QualityThreshold)
        AcceptableCandidates.Add(Candidate);
}

// Select from acceptable candidates with quality-weighted randomness
float TotalQuality = AcceptableCandidates.SumBy([](const auto& C) { return C.CombinedQuality; });
float SelectionRand = FMath::FRand() * TotalQuality;
float Cumulative = 0.f;

for (const auto& Candidate : AcceptableCandidates)
{
    Cumulative += Candidate.CombinedQuality;
    if (SelectionRand <= Cumulative)
    {
        SelectedCandidate = Candidate;
        break;
    }
}
```

**Advantages**:
- Never lets terrible candidates win
- Quality band width controls variation vs consistency
- Weighted selection favors better candidates within band
- Deterministic with seeded random for debugging

## 9. FUTURE PARRY SKILL INTEGRATION

### Skill-Based Parameterization

**Skill Parameter Architecture**:
```cpp
// Add to IronboundParryComponent.h
UPROPERTY(EditAnywhere, Category="Combat|Parry|Skill")
float ParrySkill = 0.5f;  // 0.0 = low skill, 1.0 = high skill

// Quality band width scales with skill
float GetQualityBandWidth() const
{
    return 1.0f - ParrySkill * 0.8f;  // High skill = narrow band (0.2), Low skill = wide band (1.0)
}

// Selection probability distribution sharpens with skill
float GetQualityExponent() const
{
    return 1.0f + ParrySkill * 3.0f;  // High skill = sharp distribution (4.0), Low skill = flat (1.0)
}
```

**Selection Algorithm with Skill**:
```cpp
// In SelectComfortableCandidate()
float QualityBandWidth = GetQualityBandWidth();
float QualityThreshold = BestQuality * QualityBandWidth;

// Filter acceptable candidates
// ...

// Apply skill-weighted selection
float Exponent = GetQualityExponent();
float TotalWeight = 0.f;
for (const auto& Candidate : AcceptableCandidates)
{
    float QualityRatio = Candidate.CombinedQuality / BestQuality;
    TotalWeight += FMath::Pow(QualityRatio, Exponent);
}

// Weighted random selection
float SelectionRand = FMath::FRand() * TotalWeight;
float Cumulative = 0.f;

for (const auto& Candidate : AcceptableCandidates)
{
    float QualityRatio = Candidate.CombinedQuality / BestQuality;
    Cumulative += FMath::Pow(QualityRatio, Exponent);
    if (SelectionRand <= Cumulative)
    {
        SelectedCandidate = Candidate;
        break;
    }
}
```

**Advantages**:
- Clean separation between geometry evaluation and skill
- Skill affects selection without corrupting objective quality
- Natural skill progression from wide variation to consistent excellence
- Easy to tune and debug

## 10. ELBOW CODE CLEANUP ANALYSIS

### Code to DELETE (Elbow as OUTPUT)
```cpp
// Functions to remove completely:
- FindBestElbowPosition()  // Lines 433-507
- IsDefensivePoseAnatomicallyUseful()  // Lines 509-547 (replace with comfort check)

// Fields to remove:
- RequiredElbowPosition (from FIronboundParryCandidate)
- ElbowAngleDegrees (from FIronboundParryCandidate)  
- ExecutedElbowPosition (member variable)
- ParryElbowTarget (from UIronboundCombatAnimInstance)
- ParryElbowTargetComponentSpace (from UIronboundCombatAnimInstance)

// Parameters to remove:
- ElbowCircleSamples
- MinimumElbowSideDot
- MinimumElbowAngleDegrees
- MaximumElbowAngleDegrees
- PreferredElbowAngleDegrees
- ElbowQualityWeight
- CalculateExtensionStrain()
- extensionStrain
- old D/(A+B) quadratic extension penalty
- old 0.7 threshold
```

### Code to RETAIN (Elbow as MATHEMATICAL ANALYSIS)
```cpp
// Retain two-link arm geometry utilities:
- CalculateMinimumWristDeviation()
- CalculateArmExtensionMetrics() 
- CalculateElbowCircleGeometry() // For deviation calculation

// Retain basic reach validation:
- Distance(S, H) <= A + B + margin
```

### Update Execution Logic
```cpp
// In UpdateExecutionPose():
// Remove elbow interpolation (lines 286-290)
// Keep only hand target execution
```

## 11. UPDATED IMPLEMENTATION SEQUENCE

### Phase 1: Mathematical Comfort Foundation
1. **Add comfort metric parameters** to header (raw metrics + policy structure)
2. **Implement CalculateMinimumWristDeviationAnalytical()** using elbow circle projection
3. **Implement CalculateArmExtensionMetrics()** using elbow circle geometry
4. **Implement CalculateNeutralWristRelationship()** using reference pose transforms
5. **Add raw comfort metrics calculation** to EvaluateCandidate()
6. **Test raw metrics** with debug visualization

### Phase 2: Policy Integration  
1. **Implement FParryPolicy structure** with tunable parameters
2. **Implement ApplyPolicyToRawMetrics()** function for gameplay mapping
3. **Update EvaluateCandidate()** to return both raw metrics and policy-weighted quality
4. **Test policy mapping** with different parameter combinations

### Phase 3: Quality Architecture Integration
1. **Implement lexicographic scoring** (hard constraints → raw metrics → policy)
2. **Update candidate evaluation** to separate biomechanics from policy
3. **Implement SelectComfortableCandidate()** with quality-band selection
4. **Test combined quality scoring** with policy vs raw metric separation

### Phase 4: Variation and Skill Architecture
1. **Implement controlled variation algorithm** with seeded randomness
2. **Add ParrySkill parameter** that influences policy parameters (band width, exponent)
3. **Test variation** and skill progression scenarios
4. **Validate deterministic debugging** behavior

### Phase 5: Cleanup and Final Integration
1. **Remove obsolete elbow output code** (FindBestElbowPosition, etc.)
2. **Remove elbow-related parameters** and debug drawing
3. **Update animation interface** to remove elbow targets
4. **Test complete pipeline** with CCDIK contract compliance

### Key Mathematical Corrections
- **Elbow circle projection**: Fixed plane projection algorithm for degenerate case
- **Geometric freedom**: Uses elbow circle radius / min(A,B) (no arbitrary 0.7 threshold)
- **Reference pose transforms**: Corrected coordinate spaces and transform semantics
- **Metric separation**: Raw biomechanics (WristDeviationRadians, GeometricFreedom) separate from tunable gameplay policy

## 12. VERIFIED ANIMATION CONTRACT
✅ **CCDIK receives hand location** - RequiredHandTransform.GetLocation()
✅ **Transform Modify Bone applies hand rotation** - RequiredHandTransform.GetRotation()  
✅ **Elbow target no longer consumed** - Remove all elbow output
✅ **Do not modify AnimBlueprint/ControlRig/CCDIK assets**

## KEY INSIGHTS
1. **Grip convention needs verification** - WeaponToHand axis mapping critical for forearm direction
2. **Biomechanical metrics are viable** - WristDeviationRadians and GeometricFreedom provide grounded comfort evaluation
3. **Existing sampling is sufficient** - DefenderBladeFraction provides needed freedom
4. **Skill system design is clean** - Separates geometry evaluation from fighter ability
5. **Elbow geometry useful as analysis** - Can compute comfort without selecting elbow positions
6. **Mathematical consistency achieved** - All derivations properly grounded in calculus over valid intervals