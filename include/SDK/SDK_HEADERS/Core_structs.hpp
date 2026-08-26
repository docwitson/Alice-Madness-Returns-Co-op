/*
#############################################################################################
# Alice2 (ASDK) SDK 1.0.0.0
# Generated with the CodeRedGenerator v1.2.0
# ========================================================================================= #
# File: Core_structs.hpp
# ========================================================================================= #
# Credits: ItsBranK, TheFeckless
# Links: www.github.com/CodeRedModding/CodeRed-Generator
#############################################################################################
*/
#pragma once

#ifdef _MSC_VER
#pragma pack(push, 0x4)
#endif

/*
# ========================================================================================= #
# Structs
# ========================================================================================= #
*/

// ScriptStruct Core.Object.Rotator
// (Custom Override)
// ScriptStruct Core.Object.Rotator
// 0x000C
struct FRotator
{
    int32_t Pitch; // 0x0000 (0x0004) [65536 units = 360 degrees]
    int32_t Yaw;   // 0x0004 (0x0004)
    int32_t Roll;  // 0x0008 (0x0004)

    FRotator() : Pitch(0), Yaw(0), Roll(0) {}
    FRotator(int32_t pitch, int32_t yaw, int32_t roll) : Pitch(pitch), Yaw(yaw), Roll(roll) {}

    FRotator operator+(const FRotator& other) const { return FRotator(Pitch + other.Pitch, Yaw + other.Yaw, Roll + other.Roll); }
    FRotator operator-(const FRotator& other) const { return FRotator(Pitch - other.Pitch, Yaw - other.Yaw, Roll - other.Roll); }
    FRotator operator-() const { return FRotator(-Pitch, -Yaw, -Roll); }
    FRotator& operator+=(const FRotator& other) { Pitch += other.Pitch; Yaw += other.Yaw; Roll += other.Roll; return *this; }
    FRotator& operator-=(const FRotator& other) { Pitch -= other.Pitch; Yaw -= other.Yaw; Roll -= other.Roll; return *this; }
    bool operator==(const FRotator& other) const { return ((Pitch == other.Pitch) && (Yaw == other.Yaw) && (Roll == other.Roll)); }
    bool operator!=(const FRotator& other) const { return !(*this == other); }

    // Wraps an angle into [0, 65535] (UE3 FRotator::Clamp).
    static int32_t ClampAxis(int32_t angle) { return (angle & 0xFFFF); }
    // Wraps an angle into [-32768, 32767] (UE3 FRotator::NormalizeAxis).
    static int32_t NormalizeAxis(int32_t angle) { angle &= 0xFFFF; return ((angle > 32767) ? (angle - 65536) : angle); }

    FRotator GetClamped() const { return FRotator(ClampAxis(Pitch), ClampAxis(Yaw), ClampAxis(Roll)); }
    FRotator GetNormalized() const { return FRotator(NormalizeAxis(Pitch), NormalizeAxis(Yaw), NormalizeAxis(Roll)); }
    bool IsZero() const { return ((ClampAxis(Pitch) == 0) && (ClampAxis(Yaw) == 0) && (ClampAxis(Roll) == 0)); }
};

// ScriptStruct Core.Object.Vector
// (Custom Override)
// ScriptStruct Core.Object.Vector
// 0x000C
struct FVector
{
    float X; // 0x0000 (0x0004)
    float Y; // 0x0004 (0x0004)
    float Z; // 0x0008 (0x0004)

    FVector() : X(0.0f), Y(0.0f), Z(0.0f) {}
    FVector(float x, float y, float z) : X(x), Y(y), Z(z) {}

    FVector operator+(const FVector& other) const { return FVector(X + other.X, Y + other.Y, Z + other.Z); }
    FVector operator-(const FVector& other) const { return FVector(X - other.X, Y - other.Y, Z - other.Z); }
    FVector operator*(float scale) const { return FVector(X * scale, Y * scale, Z * scale); }
    FVector operator/(float scale) const { float r = (1.0f / scale); return FVector(X * r, Y * r, Z * r); }
    FVector operator-() const { return FVector(-X, -Y, -Z); }
    FVector& operator+=(const FVector& other) { X += other.X; Y += other.Y; Z += other.Z; return *this; }
    FVector& operator-=(const FVector& other) { X -= other.X; Y -= other.Y; Z -= other.Z; return *this; }
    FVector& operator*=(float scale) { X *= scale; Y *= scale; Z *= scale; return *this; }
    FVector& operator/=(float scale) { float r = (1.0f / scale); X *= r; Y *= r; Z *= r; return *this; }
    bool operator==(const FVector& other) const { return ((X == other.X) && (Y == other.Y) && (Z == other.Z)); }
    bool operator!=(const FVector& other) const { return !(*this == other); }
    float operator|(const FVector& other) const { return ((X * other.X) + (Y * other.Y) + (Z * other.Z)); } // Dot product.
    FVector operator^(const FVector& other) const { return FVector((Y * other.Z) - (Z * other.Y), (Z * other.X) - (X * other.Z), (X * other.Y) - (Y * other.X)); } // Cross product.

    float SizeSquared() const { return ((X * X) + (Y * Y) + (Z * Z)); }
    float Size() const { return sqrtf(SizeSquared()); }
    float Size2D() const { return sqrtf((X * X) + (Y * Y)); }
    bool IsZero() const { return ((X == 0.0f) && (Y == 0.0f) && (Z == 0.0f)); }

    FVector GetNormalized(float tolerance = 0.00000001f) const
    {
        float squareSum = SizeSquared();
        if (squareSum > tolerance) { float scale = (1.0f / sqrtf(squareSum)); return FVector(X * scale, Y * scale, Z * scale); }
        return FVector();
    }

    bool Normalize(float tolerance = 0.00000001f)
    {
        float squareSum = SizeSquared();
        if (squareSum > tolerance) { float scale = (1.0f / sqrtf(squareSum)); X *= scale; Y *= scale; Z *= scale; return true; }
        return false;
    }

    static float Dist(const FVector& a, const FVector& b) { return (b - a).Size(); }
    static float DistSquared(const FVector& a, const FVector& b) { return (b - a).SizeSquared(); }

    static FVector FromRotator(const struct FRotator& rotator)
    {
        const float toRadians = (3.14159265358979f / 32768.0f);
        float pitch = (rotator.Pitch * toRadians);
        float yaw = (rotator.Yaw * toRadians);
        float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw);
        return FVector((cp * cy), (cp * sy), sp);
    }

    struct FRotator Rotation() const
    {
        const float toUnits = (32768.0f / 3.14159265358979f);
        FRotator rotator;
        rotator.Yaw = (int32_t)(atan2f(Y, X) * toUnits);
        rotator.Pitch = (int32_t)(atan2f(Z, Size2D()) * toUnits);
        rotator.Roll = 0;
        return rotator;
    }
};

inline FVector operator*(float scale, const FVector& v) { return (v * scale); }

// ScriptStruct Core.Object.Plane
// 0x0004 (0x000C - 0x0010)
struct FPlane : FVector
{
	float                                              W;                                             // 0x000C (0x0004) [0x0000000000000001] (CPF_Edit)    
};

// ScriptStruct Core.Object.Guid
// 0x0010
struct FGuid
{
	int32_t                                            A;                                             // 0x0000 (0x0004) [0x0000000000000000]               
	int32_t                                            B;                                             // 0x0004 (0x0004) [0x0000000000000000]               
	int32_t                                            C;                                             // 0x0008 (0x0004) [0x0000000000000000]               
	int32_t                                            D;                                             // 0x000C (0x0004) [0x0000000000000000]               
};

// ScriptStruct Core.Object.Vector2D
// (Custom Override)
// ScriptStruct Core.Object.Vector2D
// 0x0008
struct FVector2D
{
    float X; // 0x0000 (0x0004)
    float Y; // 0x0004 (0x0004)

    FVector2D() : X(0.0f), Y(0.0f) {}
    FVector2D(float x, float y) : X(x), Y(y) {}

    FVector2D operator+(const FVector2D& other) const { return FVector2D(X + other.X, Y + other.Y); }
    FVector2D operator-(const FVector2D& other) const { return FVector2D(X - other.X, Y - other.Y); }
    FVector2D operator*(float scale) const { return FVector2D(X * scale, Y * scale); }
    FVector2D operator/(float scale) const { float r = (1.0f / scale); return FVector2D(X * r, Y * r); }
    FVector2D operator-() const { return FVector2D(-X, -Y); }
    FVector2D& operator+=(const FVector2D& other) { X += other.X; Y += other.Y; return *this; }
    FVector2D& operator-=(const FVector2D& other) { X -= other.X; Y -= other.Y; return *this; }
    bool operator==(const FVector2D& other) const { return ((X == other.X) && (Y == other.Y)); }
    bool operator!=(const FVector2D& other) const { return !(*this == other); }
    float operator|(const FVector2D& other) const { return ((X * other.X) + (Y * other.Y)); } // Dot product.

    float SizeSquared() const { return ((X * X) + (Y * Y)); }
    float Size() const { return sqrtf(SizeSquared()); }
    bool IsZero() const { return ((X == 0.0f) && (Y == 0.0f)); }
};

// ScriptStruct Core.Object.Vector4
// 0x0010
struct FVector4
{
	float                                              X;                                             // 0x0000 (0x0004) [0x0000000000000001] (CPF_Edit)    
	float                                              Y;                                             // 0x0004 (0x0004) [0x0000000000000001] (CPF_Edit)    
	float                                              Z;                                             // 0x0008 (0x0004) [0x0000000000000001] (CPF_Edit)    
	float                                              W;                                             // 0x000C (0x0004) [0x0000000000000001] (CPF_Edit)    
};

// ScriptStruct Core.Object.LinearColor
// (Custom Override)
// ScriptStruct Core.Object.LinearColor
// 0x0010
struct FLinearColor
{
    float R; // 0x0000 (0x0004)
    float G; // 0x0004 (0x0004)
    float B; // 0x0008 (0x0004)
    float A; // 0x000C (0x0004)

    FLinearColor() : R(0.0f), G(0.0f), B(0.0f), A(0.0f) {}
    FLinearColor(float r, float g, float b, float a) : R(r), G(g), B(b), A(a) {}

    FLinearColor operator+(const FLinearColor& other) const { return FLinearColor(R + other.R, G + other.G, B + other.B, A + other.A); }
    FLinearColor operator-(const FLinearColor& other) const { return FLinearColor(R - other.R, G - other.G, B - other.B, A - other.A); }
    FLinearColor operator*(float scale) const { return FLinearColor(R * scale, G * scale, B * scale, A * scale); }
    FLinearColor operator*(const FLinearColor& other) const { return FLinearColor(R * other.R, G * other.G, B * other.B, A * other.A); }
    bool operator==(const FLinearColor& other) const { return ((R == other.R) && (G == other.G) && (B == other.B) && (A == other.A)); }
    bool operator!=(const FLinearColor& other) const { return !(*this == other); }
};

// ScriptStruct Core.Object.Color
// (Custom Override)
// ScriptStruct Core.Object.Color
// 0x0004
struct FColor
{
    uint8_t B; // 0x0000 (0x0001) [stored B, G, R, A like UE3]
    uint8_t G; // 0x0001 (0x0001)
    uint8_t R; // 0x0002 (0x0001)
    uint8_t A; // 0x0003 (0x0001)

    FColor() : B(0), G(0), R(0), A(255) {}
    FColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) : B(b), G(g), R(r), A(a) {}

    bool operator==(const FColor& other) const { return ((B == other.B) && (G == other.G) && (R == other.R) && (A == other.A)); }
    bool operator!=(const FColor& other) const { return !(*this == other); }

    uint32_t DWColor() const { return (((uint32_t)A << 24) | ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B); } // 0xAARRGGBB.
    struct FLinearColor ToLinear() const { const float s = (1.0f / 255.0f); return FLinearColor((R * s), (G * s), (B * s), (A * s)); }
};

// ScriptStruct Core.Object.InterpCurvePointVector2D
// 0x001D
struct FInterpCurvePointVector2D
{
	float                                              InVal;                                         // 0x0000 (0x0004) [0x0000000000000001] (CPF_Edit)    
	struct FVector2D                                   OutVal;                                        // 0x0004 (0x0008) [0x0000000000000001] (CPF_Edit)    
	struct FVector2D                                   ArriveTangent;                                 // 0x000C (0x0008) [0x0000000000000001] (CPF_Edit)    
	struct FVector2D                                   LeaveTangent;                                  // 0x0014 (0x0008) [0x0000000000000001] (CPF_Edit)    
	uint8_t                                            InterpMode;                                    // 0x001C (0x0001) [0x0000000000000001] (CPF_Edit)    
	uint8_t                                            MinStructAlignment[0x3];                         // 0x001D (0x0003) ADDED PADDING
};

// ScriptStruct Core.Object.InterpCurveVector2D
// 0x000D
struct FInterpCurveVector2D
{
	class TArray<struct FInterpCurvePointVector2D>     Points;                                        // 0x0000 (0x000C) [0x0000000000400001] (CPF_Edit | CPF_NeedCtorLink)
	uint8_t                                            InterpMethod;                                  // 0x000C (0x0001) [0x0000000000000000]               
	uint8_t                                            MinStructAlignment[0x3];                         // 0x000D (0x0003) ADDED PADDING
};

// ScriptStruct Core.Object.InterpCurvePointFloat
// 0x0011
struct FInterpCurvePointFloat
{
	float                                              InVal;                                         // 0x0000 (0x0004) [0x0000000000000001] (CPF_Edit)    
	float                                              OutVal;                                        // 0x0004 (0x0004) [0x0000000000000001] (CPF_Edit)    
	float                                              ArriveTangent;                                 // 0x0008 (0x0004) [0x0000000000000001] (CPF_Edit)    
	float                                              LeaveTangent;                                  // 0x000C (0x0004) [0x0000000000000001] (CPF_Edit)    
	uint8_t                                            InterpMode;                                    // 0x0010 (0x0001) [0x0000000000000001] (CPF_Edit)    
	uint8_t                                            MinStructAlignment[0x3];                         // 0x0011 (0x0003) ADDED PADDING
};

// ScriptStruct Core.Object.InterpCurveFloat
// 0x000D
struct FInterpCurveFloat
{
	class TArray<struct FInterpCurvePointFloat>        Points;                                        // 0x0000 (0x000C) [0x0000000000400001] (CPF_Edit | CPF_NeedCtorLink)
	uint8_t                                            InterpMethod;                                  // 0x000C (0x0001) [0x0000000000000000]               
	uint8_t                                            MinStructAlignment[0x3];                         // 0x000D (0x0003) ADDED PADDING
};

// ScriptStruct Core.Object.Cylinder
// 0x0008
struct FCylinder
{
	float                                              Radius;                                        // 0x0000 (0x0004) [0x0000000000000000]               
	float                                              Height;                                        // 0x0004 (0x0004) [0x0000000000000000]               
};

// ScriptStruct Core.Object.InterpCurvePointVector
// 0x0029
struct FInterpCurvePointVector
{
	float                                              InVal;                                         // 0x0000 (0x0004) [0x0000000000000001] (CPF_Edit)    
	struct FVector                                     OutVal;                                        // 0x0004 (0x000C) [0x0000000000000001] (CPF_Edit)    
	struct FVector                                     ArriveTangent;                                 // 0x0010 (0x000C) [0x0000000000000001] (CPF_Edit)    
	struct FVector                                     LeaveTangent;                                  // 0x001C (0x000C) [0x0000000000000001] (CPF_Edit)    
	uint8_t                                            InterpMode;                                    // 0x0028 (0x0001) [0x0000000000000001] (CPF_Edit)    
	uint8_t                                            MinStructAlignment[0x3];                         // 0x0029 (0x0003) ADDED PADDING
};

// ScriptStruct Core.Object.InterpCurveVector
// 0x000D
struct FInterpCurveVector
{
	class TArray<struct FInterpCurvePointVector>       Points;                                        // 0x0000 (0x000C) [0x0000000000400001] (CPF_Edit | CPF_NeedCtorLink)
	uint8_t                                            InterpMethod;                                  // 0x000C (0x0001) [0x0000000000000000]               
	uint8_t                                            MinStructAlignment[0x3];                         // 0x000D (0x0003) ADDED PADDING
};

// ScriptStruct Core.Object.Quat
// (Custom Override)
// ScriptStruct Core.Object.Quat
// 0x0010
struct FQuat
{
    float X; // 0x0000 (0x0004)
    float Y; // 0x0004 (0x0004)
    float Z; // 0x0008 (0x0004)
    float W; // 0x000C (0x0004)

    FQuat() : X(0.0f), Y(0.0f), Z(0.0f), W(1.0f) {} // Identity.
    FQuat(float x, float y, float z, float w) : X(x), Y(y), Z(z), W(w) {}

    // Quaternion composition, matching UE3's FQuat::operator* component order.
    FQuat operator*(const FQuat& q) const
    {
        return FQuat(
            ((W * q.X) + (X * q.W) + (Y * q.Z) - (Z * q.Y)),
            ((W * q.Y) - (X * q.Z) + (Y * q.W) + (Z * q.X)),
            ((W * q.Z) + (X * q.Y) - (Y * q.X) + (Z * q.W)),
            ((W * q.W) - (X * q.X) - (Y * q.Y) - (Z * q.Z)));
    }

    bool operator==(const FQuat& other) const { return ((X == other.X) && (Y == other.Y) && (Z == other.Z) && (W == other.W)); }
    bool operator!=(const FQuat& other) const { return !(*this == other); }

    float SizeSquared() const { return ((X * X) + (Y * Y) + (Z * Z) + (W * W)); }
    FQuat Inverse() const { return FQuat(-X, -Y, -Z, W); }

    bool Normalize(float tolerance = 0.00000001f)
    {
        float squareSum = SizeSquared();
        if (squareSum > tolerance) { float scale = (1.0f / sqrtf(squareSum)); X *= scale; Y *= scale; Z *= scale; W *= scale; return true; }
        return false;
    }

    FVector RotateVector(const FVector& v) const
    {
        FVector q(X, Y, Z);
        FVector t = ((q ^ v) * 2.0f);
        return ((v + (t * W)) + (q ^ t));
    }
};

// ScriptStruct Core.Object.Matrix
// 0x0040
struct FMatrix
{
	struct FPlane                                      XPlane;                                        // 0x0000 (0x0010) [0x0000000000000001] (CPF_Edit)    
	struct FPlane                                      YPlane;                                        // 0x0010 (0x0010) [0x0000000000000001] (CPF_Edit)    
	struct FPlane                                      ZPlane;                                        // 0x0020 (0x0010) [0x0000000000000001] (CPF_Edit)    
	struct FPlane                                      WPlane;                                        // 0x0030 (0x0010) [0x0000000000000001] (CPF_Edit)    
};

// ScriptStruct Core.Object.AlignedBoxSphereBounds
// 0x001C
struct FAlignedBoxSphereBounds
{
	struct FVector                                     Origin;                                        // 0x0000 (0x000C) [0x0000000000000000]               
	float                                              SphereRadius;                                  // 0x000C (0x0004) [0x0000000000000000]               
	struct FVector                                     BoxExtent;                                     // 0x0010 (0x000C) [0x0000000000000000]               
};

// ScriptStruct Core.Object.TwoVectors
// 0x0018
struct FTwoVectors
{
	struct FVector                                     v1;                                            // 0x0000 (0x000C) [0x0000000000000001] (CPF_Edit)    
	struct FVector                                     v2;                                            // 0x000C (0x000C) [0x0000000000000001] (CPF_Edit)    
};

// ScriptStruct Core.Object.TAlphaBlend
// 0x0015
struct FTAlphaBlend
{
	float                                              AlphaIn;                                       // 0x0000 (0x0004) [0x0000000000000002] (CPF_Const)   
	float                                              AlphaOut;                                      // 0x0004 (0x0004) [0x0000000000000002] (CPF_Const)   
	float                                              AlphaTarget;                                   // 0x0008 (0x0004) [0x0000000000000001] (CPF_Edit)    
	float                                              BlendTime;                                     // 0x000C (0x0004) [0x0000000000000001] (CPF_Edit)    
	float                                              BlendTimeToGo;                                 // 0x0010 (0x0004) [0x0000000000000002] (CPF_Const)   
	uint8_t                                            BlendType;                                     // 0x0014 (0x0001) [0x0000000000000001] (CPF_Edit)    
	uint8_t                                            MinStructAlignment[0x3];                         // 0x0015 (0x0003) ADDED PADDING
};

// ScriptStruct Core.Object.BoneAtom
// 0x0020
struct FBoneAtom
{
	struct FQuat                                       Rotation;                                      // 0x0000 (0x0010) [0x0000000000000000]               
	struct FVector                                     Translation;                                   // 0x0010 (0x000C) [0x0000000000000000]               
	float                                              Scale;                                         // 0x001C (0x0004) [0x0000000000000000]               
};

// ScriptStruct Core.Object.OctreeElementId
// 0x0008
struct FOctreeElementId
{
	struct FPointer                                    Node;                                          // 0x0000 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            ElementIndex;                                  // 0x0004 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
};

// ScriptStruct Core.Object.RenderCommandFence
// 0x0004
struct FRenderCommandFence
{
	int32_t                                            NumPendingFences;                              // 0x0000 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
};

// ScriptStruct Core.Object.RawDistribution
// 0x0018
struct FRawDistribution
{
	uint8_t                                            Type;                                          // 0x0000 (0x0001) [0x0000000000000000]               
	uint8_t                                            Op;                                            // 0x0001 (0x0001) [0x0000000000000000]               
	uint8_t                                            LookupTableNumElements;                        // 0x0002 (0x0001) [0x0000000000000000]               
	uint8_t                                            LookupTableChunkSize;                          // 0x0003 (0x0001) [0x0000000000000000]               
	class TArray<float>                                LookupTable;                                   // 0x0004 (0x000C) [0x0000000000400000] (CPF_NeedCtorLink)
	float                                              LookupTableTimeScale;                          // 0x0010 (0x0004) [0x0000000000000000]               
	float                                              LookupTableStartTime;                          // 0x0014 (0x0004) [0x0000000000000000]               
};

// ScriptStruct Core.Object.InterpCurvePointLinearColor
// 0x0035
struct FInterpCurvePointLinearColor
{
	float                                              InVal;                                         // 0x0000 (0x0004) [0x0000000000000001] (CPF_Edit)    
	struct FLinearColor                                OutVal;                                        // 0x0004 (0x0010) [0x0000000000000001] (CPF_Edit)    
	struct FLinearColor                                ArriveTangent;                                 // 0x0014 (0x0010) [0x0000000000000001] (CPF_Edit)    
	struct FLinearColor                                LeaveTangent;                                  // 0x0024 (0x0010) [0x0000000000000001] (CPF_Edit)    
	uint8_t                                            InterpMode;                                    // 0x0034 (0x0001) [0x0000000000000001] (CPF_Edit)    
	uint8_t                                            MinStructAlignment[0x3];                         // 0x0035 (0x0003) ADDED PADDING
};

// ScriptStruct Core.Object.InterpCurveLinearColor
// 0x000D
struct FInterpCurveLinearColor
{
	class TArray<struct FInterpCurvePointLinearColor>  Points;                                        // 0x0000 (0x000C) [0x0000000000400001] (CPF_Edit | CPF_NeedCtorLink)
	uint8_t                                            InterpMethod;                                  // 0x000C (0x0001) [0x0000000000000000]               
	uint8_t                                            MinStructAlignment[0x3];                         // 0x000D (0x0003) ADDED PADDING
};

// ScriptStruct Core.Object.InterpCurvePointQuat
// 0x0041
struct FInterpCurvePointQuat
{
	float                                              InVal;                                         // 0x0000 (0x0004) [0x0000000000000001] (CPF_Edit)    
	uint8_t                                            UnknownData00[0xC];                              // 0x0004 (0x000C) MISSED OFFSET
	struct FQuat                                       OutVal;                                        // 0x0010 (0x0010) [0x0000000000000001] (CPF_Edit)    
	struct FQuat                                       ArriveTangent;                                 // 0x0020 (0x0010) [0x0000000000000001] (CPF_Edit)    
	struct FQuat                                       LeaveTangent;                                  // 0x0030 (0x0010) [0x0000000000000001] (CPF_Edit)    
	uint8_t                                            InterpMode;                                    // 0x0040 (0x0001) [0x0000000000000001] (CPF_Edit)    
	uint8_t                                            MinStructAlignment[0xF];                         // 0x0041 (0x000F) ADDED PADDING
};

// ScriptStruct Core.Object.InterpCurveQuat
// 0x000D
struct FInterpCurveQuat
{
	class TArray<struct FInterpCurvePointQuat>         Points;                                        // 0x0000 (0x000C) [0x0000000000400001] (CPF_Edit | CPF_NeedCtorLink)
	uint8_t                                            InterpMethod;                                  // 0x000C (0x0001) [0x0000000000000000]               
	uint8_t                                            MinStructAlignment[0x3];                         // 0x000D (0x0003) ADDED PADDING
};

// ScriptStruct Core.Object.InterpCurvePointTwoVectors
// 0x004D
struct FInterpCurvePointTwoVectors
{
	float                                              InVal;                                         // 0x0000 (0x0004) [0x0000000000000001] (CPF_Edit)    
	struct FTwoVectors                                 OutVal;                                        // 0x0004 (0x0018) [0x0000000000000001] (CPF_Edit)    
	struct FTwoVectors                                 ArriveTangent;                                 // 0x001C (0x0018) [0x0000000000000001] (CPF_Edit)    
	struct FTwoVectors                                 LeaveTangent;                                  // 0x0034 (0x0018) [0x0000000000000001] (CPF_Edit)    
	uint8_t                                            InterpMode;                                    // 0x004C (0x0001) [0x0000000000000001] (CPF_Edit)    
	uint8_t                                            MinStructAlignment[0x3];                         // 0x004D (0x0003) ADDED PADDING
};

// ScriptStruct Core.Object.InterpCurveTwoVectors
// 0x000D
struct FInterpCurveTwoVectors
{
	class TArray<struct FInterpCurvePointTwoVectors>   Points;                                        // 0x0000 (0x000C) [0x0000000000400001] (CPF_Edit | CPF_NeedCtorLink)
	uint8_t                                            InterpMethod;                                  // 0x000C (0x0001) [0x0000000000000000]               
	uint8_t                                            MinStructAlignment[0x3];                         // 0x000D (0x0003) ADDED PADDING
};

// ScriptStruct Core.Object.BoxSphereBounds
// 0x001C
struct FBoxSphereBounds
{
	struct FVector                                     Origin;                                        // 0x0000 (0x000C) [0x0000000000000000]               
	struct FVector                                     BoxExtent;                                     // 0x000C (0x000C) [0x0000000000000000]               
	float                                              SphereRadius;                                  // 0x0018 (0x0004) [0x0000000000000000]               
};

// ScriptStruct Core.Object.Box
// 0x0019
struct FBox
{
	struct FVector                                     Min;                                           // 0x0000 (0x000C) [0x0000000000000001] (CPF_Edit)    
	struct FVector                                     Max;                                           // 0x000C (0x000C) [0x0000000000000001] (CPF_Edit)    
	uint8_t                                            IsValid;                                       // 0x0018 (0x0001) [0x0000000000000000]               
	uint8_t                                            MinStructAlignment[0x3];                         // 0x0019 (0x0003) ADDED PADDING
};

// ScriptStruct Core.Object.TPOV
// 0x001C
struct FTPOV
{
	struct FVector                                     Location;                                      // 0x0000 (0x000C) [0x0000000000000001] (CPF_Edit)    
	struct FRotator                                    Rotation;                                      // 0x000C (0x000C) [0x0000000000000001] (CPF_Edit)    
	float                                              FOV;                                           // 0x0018 (0x0004) [0x0000000000000001] (CPF_Edit)    
};

// ScriptStruct Core.Object.SHVector
// 0x0030
struct FSHVector
{
	float                                              V[9];                                          // 0x0000 (0x0024) [0x0000000000000001] (CPF_Edit)    
	float                                              Padding[3];                                    // 0x0024 (0x000C) [0x0000000000000000]               
};

// ScriptStruct Core.Object.SHVectorRGB
// 0x0090
struct FSHVectorRGB
{
	struct FSHVector                                   R;                                             // 0x0000 (0x0030) [0x0000000000000001] (CPF_Edit)    
	struct FSHVector                                   G;                                             // 0x0030 (0x0030) [0x0000000000000001] (CPF_Edit)    
	struct FSHVector                                   B;                                             // 0x0060 (0x0030) [0x0000000000000001] (CPF_Edit)    
};

// ScriptStruct Core.Object.IntPoint
// 0x0008
struct FIntPoint
{
	int32_t                                            X;                                             // 0x0000 (0x0004) [0x0000000000000001] (CPF_Edit)    
	int32_t                                            Y;                                             // 0x0004 (0x0004) [0x0000000000000001] (CPF_Edit)    
};

// ScriptStruct Core.Object.Array_Mirror
// 0x000C
struct FArray_Mirror
{
	struct FPointer                                    Data;                                          // 0x0000 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            ArrayNum;                                      // 0x0004 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            ArrayMax;                                      // 0x0008 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
};

// ScriptStruct Core.Object.IndirectArray_Mirror
// 0x000C
struct FIndirectArray_Mirror
{
	struct FPointer                                    Data;                                          // 0x0000 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            ArrayNum;                                      // 0x0004 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            ArrayMax;                                      // 0x0008 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
};

// ScriptStruct Core.Object.FColorVertexBuffer_Mirror
// 0x0014
struct FFColorVertexBuffer_Mirror
{
	struct FPointer                                    VfTable;                                       // 0x0000 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	struct FPointer                                    VertexData;                                    // 0x0004 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            Data;                                          // 0x0008 (0x0004) [0x0000000000000002] (CPF_Const)   
	int32_t                                            Stride;                                        // 0x000C (0x0004) [0x0000000000000002] (CPF_Const)   
	int32_t                                            NumVertices;                                   // 0x0010 (0x0004) [0x0000000000000002] (CPF_Const)   
};

// ScriptStruct Core.Object.RenderCommandFence_Mirror
// 0x0004
struct FRenderCommandFence_Mirror
{
	int32_t                                            NumPendingFences;                              // 0x0000 (0x0004) [0x0000000000003002] (CPF_Const | CPF_Native | CPF_Transient)
};

// ScriptStruct Core.Object.UntypedBulkData_Mirror
// 0x0034
struct FUntypedBulkData_Mirror
{
	struct FPointer                                    VfTable;                                       // 0x0000 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            BulkDataFlags;                                 // 0x0004 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            ElementCount;                                  // 0x0008 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            BulkDataOffsetInFile;                          // 0x000C (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            BulkDataSizeOnDisk;                            // 0x0010 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            SavedBulkDataFlags;                            // 0x0014 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            SavedElementCount;                             // 0x0018 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            SavedBulkDataOffsetInFile;                     // 0x001C (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            SavedBulkDataSizeOnDisk;                       // 0x0020 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	struct FPointer                                    BulkData;                                      // 0x0024 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            LockStatus;                                    // 0x0028 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	struct FPointer                                    AttachedAr;                                    // 0x002C (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            bShouldFreeOnEmpty;                            // 0x0030 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
};

// ScriptStruct Core.Object.BitArray_Mirror
// 0x001C
struct FBitArray_Mirror
{
	struct FPointer                                    IndirectData;                                  // 0x0000 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            InlineData[4];                                 // 0x0004 (0x0010) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            NumBits;                                       // 0x0014 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            MaxBits;                                       // 0x0018 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
};

// ScriptStruct Core.Object.SparseArray_Mirror
// 0x0030
struct FSparseArray_Mirror
{
	class TArray<int32_t>                              Elements;                                      // 0x0000 (0x000C) [0x0000000000001002] (CPF_Const | CPF_Native)
	struct FBitArray_Mirror                            AllocationFlags;                               // 0x000C (0x001C) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            FirstFreeIndex;                                // 0x0028 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            NumFreeIndices;                                // 0x002C (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
};

// ScriptStruct Core.Object.Set_Mirror
// 0x003C
struct FSet_Mirror
{
	struct FSparseArray_Mirror                         Elements;                                      // 0x0000 (0x0030) [0x0000000000001002] (CPF_Const | CPF_Native)
	struct FPointer                                    Hash;                                          // 0x0030 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            InlineHash;                                    // 0x0034 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            HashSize;                                      // 0x0038 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
};

// ScriptStruct Core.Object.MultiMap_Mirror
// 0x003C
struct FMultiMap_Mirror
{
	struct FSet_Mirror                                 Pairs;                                         // 0x0000 (0x003C) [0x0000000000001002] (CPF_Const | CPF_Native)
};

// ScriptStruct Core.Object.Map_Mirror
// 0x003C
struct FMap_Mirror
{
	struct FSet_Mirror                                 Pairs;                                         // 0x0000 (0x003C) [0x0000000000001002] (CPF_Const | CPF_Native)
};

// ScriptStruct Core.Object.ThreadSafeCounter
// 0x0004
struct FThreadSafeCounter
{
	int32_t                                            Value;                                         // 0x0000 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
};

// ScriptStruct Core.Object.Double
// 0x0008
struct FDouble
{
	int32_t                                            A;                                             // 0x0000 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
	int32_t                                            B;                                             // 0x0004 (0x0004) [0x0000000000001002] (CPF_Const | CPF_Native)
};

// ScriptStruct Core.DistributionFloat.RawDistributionFloat
// 0x0004 (0x0018 - 0x001C)
struct FRawDistributionFloat : FRawDistribution
{
	class UDistributionFloat*                          Distribution;                                  // 0x0018 (0x0004) [0x0000000006080009] (CPF_Edit | CPF_ExportObject | CPF_Component | CPF_NoClear | CPF_EditInline)
};

// ScriptStruct Core.DistributionVector.RawDistributionVector
// 0x0004 (0x0018 - 0x001C)
struct FRawDistributionVector : FRawDistribution
{
	class UDistributionVector*                         Distribution;                                  // 0x0018 (0x0004) [0x0000000006080009] (CPF_Edit | CPF_ExportObject | CPF_Component | CPF_NoClear | CPF_EditInline)
};

/*
# ========================================================================================= #
#
# ========================================================================================= #
*/

#ifdef _MSC_VER
#pragma pack(pop)
#endif
