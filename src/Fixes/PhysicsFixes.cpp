#include "Common.hpp"
#include "Features.hpp"

#include <emmintrin.h>

static SafetyHookInline RangeAttackPawnCollisionCheck{};

static void __fastcall RangeAttackPawnCollisionCheck_Hook(int thisPtr, float DeltaTime)
{
	DeltaTime = TARGET_FRAME_TIME;
	RangeAttackPawnCollisionCheck.unsafe_fastcall<void>(thisPtr, DeltaTime);
}

void ApplyFixHighFPSProjectileCollisionCheck()
{
	if (!FixHighFPSProjectileCollisionCheck) return;

	RangeAttackPawnCollisionCheck = HookHelper::CreateHook((void*)GetAddress(Addr::RangeAttackPawnCollisionCheck), &RangeAttackPawnCollisionCheck_Hook);
}

// ---- Ragdoll ----

safetyhook::InlineHook GetUnrealWorldTM;
safetyhook::InlineHook ApexBoneWrite;
safetyhook::InlineHook ApexClothWrite;
static safetyhook::MidHook sceneFixedTimestep{};
static safetyhook::MidHook ragdollInterpInvalidate{};

static uintptr_t SceneSetTimingSkip = 0;

constexpr int PhysMaxSubSteps = 8;
constexpr float InterpSnapDistSq = 250.0f * 250.0f;
constexpr double InterpStaleFactor = 4.0;
constexpr double InterpIntervalPad = 1.12;
constexpr size_t InterpMaxBodies = 1024;
constexpr size_t InterpMaxChunks = 4096;
constexpr size_t InterpMaxCloth = 128;

struct BodyPoseState
{
	FVector prevPos, currPos;
	FQuat prevQuat, currQuat;
	double lastChange = 0.0;
	double lastSeen = 0.0;
	double interval = TARGET_FRAME_TIME;
	bool primed = false;
};
static std::unordered_map<uintptr_t, BodyPoseState> bodyPoses;
static std::unordered_map<uintptr_t, BodyPoseState> chunkPoses;

struct ClothPoseState
{
	std::vector<float> prev, curr, scratch[2];
	int flip = 0;
	double lastChange = 0.0;
	double lastSeen = 0.0;
	double interval = TARGET_FRAME_TIME;
	bool primed = false;
};
static std::unordered_map<uintptr_t, ClothPoseState> clothPoses;

static void* lastConfiguredScene = nullptr;
static double physClock = 0.0;

static inline FQuat QuatFromAxes(const FVector& x, const FVector& y, const FVector& z)
{
	FQuat q;
	float trace = x.X + y.Y + z.Z;
	if (trace > 0.0f)
	{
		float s = sqrtf(trace + 1.0f) * 2.0f;
		q.W = 0.25f * s;
		q.X = (y.Z - z.Y) / s;
		q.Y = (z.X - x.Z) / s;
		q.Z = (x.Y - y.X) / s;
	}
	else if (x.X > y.Y && x.X > z.Z)
	{
		float s = sqrtf(1.0f + x.X - y.Y - z.Z) * 2.0f;
		q.W = (y.Z - z.Y) / s;
		q.X = 0.25f * s;
		q.Y = (y.X + x.Y) / s;
		q.Z = (z.X + x.Z) / s;
	}
	else if (y.Y > z.Z)
	{
		float s = sqrtf(1.0f + y.Y - x.X - z.Z) * 2.0f;
		q.W = (z.X - x.Z) / s;
		q.X = (y.X + x.Y) / s;
		q.Y = 0.25f * s;
		q.Z = (z.Y + y.Z) / s;
	}
	else
	{
		float s = sqrtf(1.0f + z.Z - x.X - y.Y) * 2.0f;
		q.W = (x.Y - y.X) / s;
		q.X = (z.X + x.Z) / s;
		q.Y = (z.Y + y.Z) / s;
		q.Z = 0.25f * s;
	}

	return q;
}

static inline void QuatToAxes(const FQuat& q, FVector& x, FVector& y, FVector& z)
{
	x = FVector(1.0f - 2.0f * (q.Y * q.Y + q.Z * q.Z), 2.0f * (q.X * q.Y + q.W * q.Z), 2.0f * (q.X * q.Z - q.W * q.Y));
	y = FVector(2.0f * (q.X * q.Y - q.W * q.Z), 1.0f - 2.0f * (q.X * q.X + q.Z * q.Z), 2.0f * (q.Y * q.Z + q.W * q.X));
	z = FVector(2.0f * (q.X * q.Z + q.W * q.Y), 2.0f * (q.Y * q.Z - q.W * q.X), 1.0f - 2.0f * (q.X * q.X + q.Y * q.Y));
}

static inline FQuat QuatNlerp(const FQuat& a, const FQuat& b, float t)
{
	float dot = a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;
	float s = dot < 0.0f ? -t : t;
	FQuat q(a.X * (1.0f - t) + b.X * s, a.Y * (1.0f - t) + b.Y * s, a.Z * (1.0f - t) + b.Z * s, a.W * (1.0f - t) + b.W * s);

	if (!q.Normalize())
	{
		q = FQuat();
	}

	return q;
}

static inline float StepPoseState(BodyPoseState& st, const FVector& pos, const FQuat& quat, double now)
{
	bool stale = st.primed && (now - st.lastSeen) > TARGET_FRAME_TIME * InterpStaleFactor;
	st.lastSeen = now;

	if (!st.primed || stale)
	{
		st.prevPos = pos;
		st.currPos = pos;
		st.prevQuat = quat;
		st.currQuat = quat;
		st.lastChange = now;
		st.interval = TARGET_FRAME_TIME;
		st.primed = true;
		return 1.0f;
	}

	if (st.currPos != pos || st.currQuat != quat) // bit-identical until a substep actually ran
	{
		if (FVector::DistSquared(st.currPos, pos) > InterpSnapDistSq)
		{
			// teleport or huge fast mover: snap, don't sweep across the gap
			st.prevPos = pos;
			st.prevQuat = quat;
		}
		else
		{
			st.prevPos = st.currPos;
			st.prevQuat = st.currQuat;
		}

		st.currPos = pos;
		st.currQuat = quat;

		double dt = now - st.lastChange;
		if (dt < TARGET_FRAME_TIME * 0.5) dt = TARGET_FRAME_TIME * 0.5;
		if (dt > TARGET_FRAME_TIME * 4.0) dt = TARGET_FRAME_TIME * 4.0;
		st.interval = dt * InterpIntervalPad;
		st.lastChange = now;
	}

	float alpha = static_cast<float>((now - st.lastChange) / st.interval);
	if (alpha < 0.0f) alpha = 0.0f;
	if (alpha > 1.0f) alpha = 1.0f;
	return alpha;
}

static void OnSceneSetTiming(safetyhook::Context& ctx)
{
	void* nxScene = reinterpret_cast<void*>(ctx.eax);

	physClock += *reinterpret_cast<float*>(ctx.ebx + 0xC); // [ebx+0xC] = frame DeltaSeconds

	AAlicePlayerController* pc = g_State.AlicePlayerController;
	AAlicePawn* pawn = pc ? (AAlicePawn*)pc->Pawn : nullptr;

	if (pawn && pawn->bInRollingMode)
	{
		lastConfiguredScene = nullptr;
		return;
	}

	if (nxScene == lastConfiguredScene)
	{
		ctx.eip = SceneSetTimingSkip; // keep the fixed-step accumulator, skip the per-frame setTiming
		return;
	}

	*reinterpret_cast<float*>(ctx.ebx + 0x10) = TARGET_FRAME_TIME; // maxTimestep, the game wanted dt / NumSubSteps
	ctx.edi = PhysMaxSubSteps; // maxIter
	lastConfiguredScene = nxScene;
}

static FMatrix* __fastcall GetUnrealWorldTM_Hook(URB_BodyInstance* thisPtr, int, FMatrix* outM)
{
	GetUnrealWorldTM.unsafe_thiscall<FMatrix*>(thisPtr, outM);

	uintptr_t key = thisPtr->BodyData.Dummy; // NxActor*
	if (!key)
		return outM;

	FVector pos(outM->WPlane.X, outM->WPlane.Y, outM->WPlane.Z);
	FQuat quat = QuatFromAxes(outM->XPlane, outM->YPlane, outM->ZPlane);

	if (bodyPoses.size() > InterpMaxBodies)
	{
		bodyPoses.clear();
	}

	BodyPoseState& st = bodyPoses[key];
	float alpha = StepPoseState(st, pos, quat, physClock);

	FVector p = st.prevPos + (st.currPos - st.prevPos) * alpha;
	outM->WPlane.X = p.X;
	outM->WPlane.Y = p.Y;
	outM->WPlane.Z = p.Z;

	FVector ax, ay, az;
	QuatToAxes(QuatNlerp(st.prevQuat, st.currQuat, alpha), ax, ay, az);
	static_cast<FVector&>(outM->XPlane) = ax;
	static_cast<FVector&>(outM->YPlane) = ay;
	static_cast<FVector&>(outM->ZPlane) = az;

	return outM;
}

static void OnRagdollTransition(safetyhook::Context& ctx)
{
	const uintptr_t actorAddr = (Addresses::GetBuild() == GameBuild::Current) ? ctx.esi : ctx.edi;

	auto* dropActor = reinterpret_cast<AAliceGameDropActor*>(actorAddr);
	if (!dropActor) return;

	USkeletalMeshComponent* skelComp = dropActor->SkelComp;
	if (!skelComp) return;

	UPhysicsAssetInstance* inst = skelComp->PhysicsAssetInstance;
	if (!inst) return;

	const TArray<URB_BodyInstance*>& bodies = inst->Bodies;
	const int32_t numBodies = bodies.size();
	if (!bodies.data() || numBodies <= 0 || numBodies > 512) return;

	for (int32_t i = 0; i < numBodies; i++)
	{
		URB_BodyInstance* body = bodies[i];
		if (body && body->BodyData.Dummy)
		{
			bodyPoses.erase(body->BodyData.Dummy);
		}
	}
}

static void __fastcall ApexBoneWrite_Hook(uint32_t* thisPtr, int, int* data, uint32_t firstBone, uint32_t numBones)
{
	ApexBoneWrite.unsafe_thiscall<void>(thisPtr, data, firstBone, numBones);

	float* poses = reinterpret_cast<float*>(thisPtr[1]);
	uint32_t maxBones = thisPtr[2];
	if (!poses || numBones > maxBones || firstBone > maxBones - numBones)
		return;

	if (chunkPoses.size() > InterpMaxChunks)
	{
		chunkPoses.clear();
	}

	for (uint32_t i = 0; i < numBones; i++)
	{
		float* f = poses + (firstBone + i) * 12;

		FVector pos(f[3], f[7], f[11]);

		FVector ax(f[0], f[4], f[8]);
		FVector ay(f[1], f[5], f[9]);
		FVector az(f[2], f[6], f[10]);
		float sx = ax.Size(), sy = ay.Size(), sz = az.Size();
		if (sx < 1e-4f || sy < 1e-4f || sz < 1e-4f)
			continue;

		ax /= sx;
		ay /= sy;
		az /= sz;

		float det = ax | (ay ^ az);
		bool rotOk = det > 0.5f;

		FQuat quat = rotOk ? QuatFromAxes(ax, ay, az) : FQuat();

		BodyPoseState& st = chunkPoses[reinterpret_cast<uintptr_t>(f)];
		float alpha = StepPoseState(st, pos, quat, physClock);

		FVector p = st.prevPos + (st.currPos - st.prevPos) * alpha;
		f[3] = p.X;
		f[7] = p.Y;
		f[11] = p.Z;

		if (!rotOk)
			continue;

		QuatToAxes(QuatNlerp(st.prevQuat, st.currQuat, alpha), ax, ay, az);

		// rebuild the 3x3 with each column's original scale restored
		f[0] = ax.X * sx;
		f[4] = ax.Y * sx;
		f[8] = ax.Z * sx;
		f[1] = ay.X * sy;
		f[5] = ay.Y * sy;
		f[9] = ay.Z * sy;
		f[2] = az.X * sz;
		f[6] = az.Y * sz;
		f[10] = az.Z * sz;
	}
}

static void __fastcall ApexClothWrite_Hook(uint32_t* thisPtr, int, uint8_t* data, uint32_t firstVertex, uint32_t numVerts)
{
	float* srcPos = *reinterpret_cast<float**>(data);
	uint32_t srcStride = *reinterpret_cast<uint32_t*>(data + 4);
	uint32_t maxVerts = thisPtr[8];

	if (!srcPos || srcStride < 12 || firstVertex != 0 || !numVerts || numVerts > maxVerts || maxVerts > 4096)
	{
		ApexClothWrite.unsafe_thiscall<void>(thisPtr, data, firstVertex, numVerts);
		return;
	}

	if (clothPoses.size() > InterpMaxCloth)
	{
		clothPoses.clear();
	}

	ClothPoseState& st = clothPoses[reinterpret_cast<uintptr_t>(thisPtr)];
	double now = physClock;

	if (st.curr.size() != numVerts * 3)
	{
		st.prev.assign(numVerts * 3, 0.0f);
		st.curr.assign(numVerts * 3, 0.0f);
		st.scratch[0].assign(numVerts * 3, 0.0f);
		st.scratch[1].assign(numVerts * 3, 0.0f);
		st.primed = false;
	}

	const uint8_t* src = reinterpret_cast<const uint8_t*>(srcPos);

	bool changed = false;
	for (uint32_t i = 0; i < numVerts; i++)
	{
		const float* v = reinterpret_cast<const float*>(src + i * srcStride);
		const float* c = &st.curr[i * 3];

		if (c[0] != v[0] || c[1] != v[1] || c[2] != v[2]) // bit-identical until a sim step ran
		{
			changed = true;
			break;
		}
	}

	bool stale = st.primed && (now - st.lastSeen) > TARGET_FRAME_TIME * InterpStaleFactor;
	st.lastSeen = now;

	if (!st.primed || stale)
	{
		for (uint32_t i = 0; i < numVerts; i++)
		{
			memcpy(&st.curr[i * 3], src + i * srcStride, 12);
		}

		st.prev = st.curr;
		st.lastChange = now;
		st.interval = TARGET_FRAME_TIME;
		st.primed = true;

		ApexClothWrite.unsafe_thiscall<void>(thisPtr, data, firstVertex, numVerts);
		return;
	}

	if (changed)
	{
		const float* v0 = reinterpret_cast<const float*>(src);
		float dx = st.curr[0] - v0[0], dy = st.curr[1] - v0[1], dz = st.curr[2] - v0[2];
		bool snap = dx * dx + dy * dy + dz * dz > 25.0f;

		if (!snap)
		{
			st.prev = st.curr;
		}

		for (uint32_t i = 0; i < numVerts; i++)
		{
			memcpy(&st.curr[i * 3], src + i * srcStride, 12);
		}

		if (snap)
		{
			st.prev = st.curr;
		}

		double dt = now - st.lastChange;
		if (dt < TARGET_FRAME_TIME * 0.5) dt = TARGET_FRAME_TIME * 0.5;
		if (dt > TARGET_FRAME_TIME * 4.0) dt = TARGET_FRAME_TIME * 4.0;
		st.interval = dt * InterpIntervalPad;
		st.lastChange = now;
	}

	float alpha = static_cast<float>((now - st.lastChange) / st.interval);
	if (alpha < 0.0f) alpha = 0.0f;
	if (alpha > 1.0f) alpha = 1.0f;

	std::vector<float>& out = st.scratch[st.flip];
	st.flip ^= 1;

	for (uint32_t i = 0; i < numVerts * 3; i++)
	{
		out[i] = st.prev[i] + (st.curr[i] - st.prev[i]) * alpha;
	}

	*reinterpret_cast<float**>(data) = out.data();
	*reinterpret_cast<uint32_t*>(data + 4) = 12;

	ApexClothWrite.unsafe_thiscall<void>(thisPtr, data, firstVertex, numVerts);

	*reinterpret_cast<float**>(data) = srcPos;
	*reinterpret_cast<uint32_t*>(data + 4) = srcStride;
}

void ApplyFixHighFPSPhysX()
{
	if (!FixHighFPSPhysX) return;

	SceneSetTimingSkip = GetAddress(Addr::PhysSceneSetTimingSkip);
	sceneFixedTimestep = safetyhook::create_mid(GetAddress(Addr::PhysSceneSetTiming), OnSceneSetTiming);
	GetUnrealWorldTM = HookHelper::CreateHook((void*)GetAddress(Addr::GetUnrealWorldTM), &GetUnrealWorldTM_Hook);
	ragdollInterpInvalidate = safetyhook::create_mid(GetAddress(Addr::RagdollTransition), OnRagdollTransition);
	ApexBoneWrite = HookHelper::CreateHook((void*)GetAddress(Addr::ApexBoneBufferWrite), &ApexBoneWrite_Hook);
	ApexClothWrite = HookHelper::CreateHook((void*)GetAddress(Addr::ApexClothVertexWrite), &ApexClothWrite_Hook);
}

// ---- Hair, Cloth ----

struct ParticleSimLayout
{
	int numParticles;
	int particles;
	int numColliders;
	int colliders;
	int particleStride;
	int lengthScale;
	int force;
	int windAmp;
	int radialCenter;
	int radialStrength;
	int strands;
	int numStrands;
	int drivenNormal;
};

constexpr ParticleSimLayout HAIR_LAYOUT = { 0xAC, 0xC0, 0xB8, 0xC8, 0x50, 0x9C, 0x40, 0x50, -1, -1, 0xC4, 0xB0, -1 };
constexpr ParticleSimLayout CLOTH_LAYOUT = { 0xB8, 0xC4, 0xC0, 0xCC, 0x70, -1, 0x40, 0x60, 0x50, 0xA0, -1, -1, 0x40 };
constexpr float SIM_STEP_DT = 0.033f;
constexpr int SIM_MAX_INSTANCES = 64;
constexpr int SIM_MAX_PARTICLES = 4096; // Alice's hair is 2565
constexpr int SIM_MAX_COLLIDERS = 64;
constexpr float SIM_CUT_SPEED = 12000.0f;
constexpr float SIM_CUT_MIN_DIST = 32.0f;
constexpr float SIM_CUT_TURN_COS = 0.5f;
constexpr uint64_t SIM_INPUT_STALE_MS = 250;
constexpr uint64_t SIM_SLOT_BUSY_MS = 1000;
constexpr uint64_t SIM_PURGE_AFTER_MS = 30000;
constexpr uint64_t SIM_PURGE_PERIOD_MS = 5000;

struct ParticleSimState
{
	uintptr_t key = 0;
    const uint8_t* particleAllocation = nullptr;
    const uint8_t* colliderAllocation = nullptr;
	uint64_t lastSeen = 0;
	uint64_t lastCall = 0;
	const ParticleSimLayout* layout = nullptr;
	int numParticles = 0;
	int numColliders = 0;
	float accumulator = 0.0f;
	bool primed = false;
	bool hasPrevInputs = false;
	bool theirs = false;
	float prevMatrix[16] = {};
	float prevLengthScale = 1.0f;
	float prevForceWind[6] = {};

	float tickMatrix[16] = {};
	float blastLocal[3] = {};
	float blastStrength = 0.0f;
	bool hasTickMatrix = false;
	bool blastLocalValid = false;
	bool blastAttached = false;
	bool blastStrengthPending = false;
	bool blastStrengthValid = false;

	// One block per instance, sized to fit: trueP1 | offPrev | offCurr | applied | prevColliders | liveColliders | restPrev | restLive
	std::unique_ptr<float[]> buffer;
	size_t capacity = 0;
	float* trueP1 = nullptr;
	float* offPrev = nullptr;
	float* offCurr = nullptr;
	float* applied = nullptr;
	float* prevColliders = nullptr;
	float* liveColliders = nullptr;
	float* restPrev = nullptr;
	float* restLive = nullptr;

	// hair only
	std::unique_ptr<int32_t[]> rootOf;
	std::unique_ptr<uint64_t[]> strandRecords;
	int numStrands = 0;
	bool rootsBuilt = false;
};

static ParticleSimState simStates[SIM_MAX_INSTANCES];

void InvalidateHighFPSParticleSimulation(const void* simulator)
{
    if (!simulator) return;
    for (ParticleSimState& state : simStates)
    {
        if (state.key == reinterpret_cast<uintptr_t>(simulator))
        {
            // A native Reset/DeleteSimulator may reuse all allocation addresses.
            // Never blend the new bind pose with pre-hide particle history.
            state = ParticleSimState{};
            return;
        }
    }
}

static uint64_t SimActivityClock(uint64_t now)
{
	static uint64_t lastNow = 0, clock = 0;

	uint64_t elapsed = lastNow ? now - lastNow : 0;
	clock += elapsed < 100 ? elapsed : 100;
	lastNow = now;
	return clock;
}

static void PurgeSimStates(uint64_t clock)
{
	static uint64_t lastPurge = 0;
	if (clock - lastPurge < SIM_PURGE_PERIOD_MS)
		return;

	lastPurge = clock;
	for (ParticleSimState& s : simStates)
	{
		if (s.key && clock - s.lastSeen > SIM_PURGE_AFTER_MS)
		{
			s = ParticleSimState{};
		}
	}
}

static ParticleSimState* AcquireSimState(uintptr_t key, uint64_t now, uint64_t clock, const ParticleSimLayout* layout, int numParticles, int numColliders, const uint8_t* particles, const uint8_t* colliders)
{
	ParticleSimState* state = nullptr;
	ParticleSimState* victim = nullptr;

	for (ParticleSimState& s : simStates)
	{
		if (s.key == key)
		{
			state = &s;
			break;
		}

		// a free slot first, the least recently stepped one otherwise
		if (!victim || (victim->key && (!s.key || s.lastSeen < victim->lastSeen)))
		{
			victim = &s;
		}
	}

	if (!state)
	{
		// Every slot was stepped within the last second: leave this instance to the vanilla path instead of evicting one that is mid-interpolation
		if (victim->key && clock - victim->lastSeen < SIM_SLOT_BUSY_MS)
			return nullptr;

		*victim = ParticleSimState{};
		state = victim;
		state->key = key;
	}

	// New instance, or the game built a different sim at an address it had just freed
	if (state->layout != layout || state->numParticles != numParticles || state->numColliders != numColliders
        || state->particleAllocation != particles || state->colliderAllocation != colliders)
	{
		size_t n = static_cast<size_t>(numParticles) * 4;
		size_t m = static_cast<size_t>(numColliders) * 3;
		size_t rest = layout->drivenNormal >= 0 ? n * 2 : 0;
		size_t needed = n * 4 + m * 2 + rest * 2;

		if (state->capacity < needed)
		{
			float* block = new (std::nothrow) float[needed]();
			if (!block)
			{
				*state = ParticleSimState{};
				return nullptr;
			}

			state->buffer.reset(block);
			state->capacity = needed;
		}

		float* base = state->buffer.get();
		state->trueP1 = base;
		state->offPrev = base + n;
		state->offCurr = base + n * 2;
		state->applied = base + n * 3;
		state->prevColliders = base + n * 4;
		state->liveColliders = base + n * 4 + m;
		state->restPrev = rest ? base + n * 4 + m * 2 : nullptr;
		state->restLive = rest ? base + n * 4 + m * 2 + rest : nullptr;
		state->rootOf.reset(layout->strands >= 0 ? new (std::nothrow) int32_t[numParticles] : nullptr);
		state->strandRecords.reset(layout->strands >= 0 ? new (std::nothrow) uint64_t[numParticles]() : nullptr);
		state->rootsBuilt = false;
		if (layout->strands >= 0 && !state->rootOf)
		{
			*state = ParticleSimState{};
			return nullptr;
		}

        state->particleAllocation = particles;
        state->colliderAllocation = colliders;
		state->layout = layout;
		state->numParticles = numParticles;
		state->numColliders = numColliders;
		state->accumulator = 0.0f;
		state->primed = false;
		state->hasPrevInputs = false;
		state->theirs = false;
		state->hasTickMatrix = false;
		state->blastLocalValid = state->blastAttached = state->blastStrengthPending = state->blastStrengthValid = false;
	}

	if (now - state->lastCall > SIM_INPUT_STALE_MS)
	{
		state->hasPrevInputs = false;
	}

	state->lastCall = now;
	state->lastSeen = clock;
	return state;
}

// Inverse of the rotation / scale part, so that local = world * inverse (row vectors, like the game)
static void SimInverseBasis(const float* m, float* inv)
{
	float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
	float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);

	if (!(fabsf(det) > 1e-12f))
	{
		static const float identity[9] = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };
		memcpy(inv, identity, sizeof(identity));
		return;
	}

	float r = 1.0f / det;
	inv[0] = (e * i - f * h) * r; inv[1] = (c * h - b * i) * r; inv[2] = (b * f - c * e) * r;
	inv[3] = (f * g - d * i) * r; inv[4] = (a * i - c * g) * r; inv[5] = (c * d - a * f) * r;
	inv[6] = (d * h - e * g) * r; inv[7] = (b * g - a * h) * r; inv[8] = (a * e - b * d) * r;
}

static inline void SimToBodyFrame(const float* world, const float* inv, float* local)
{
	for (int j = 0; j < 3; j++)
	{
		local[j] = world[0] * inv[j] + world[1] * inv[3 + j] + world[2] * inv[6 + j];
	}
}

static inline void SimLocalPoint(const float* world, const float* matrix, const float* inv, float* local)
{
	float offset[3] = { world[0] - matrix[12], world[1] - matrix[13], world[2] - matrix[14] };
	SimToBodyFrame(offset, inv, local);
}

static inline void SimWorldPoint(const float* local, const float* matrix, float* world)
{
	for (int j = 0; j < 3; j++)
	{
		world[j] = local[0] * matrix[j] + local[1] * matrix[4 + j] + local[2] * matrix[8 + j] + matrix[12 + j];
	}
}

static bool UpdateSimRoots(const ParticleSimLayout& layout, const uint8_t* sim, ParticleSimState& state)
{
	int numStrands = *(const int*)(sim + layout.numStrands);
	const uint8_t* strands = *(const uint8_t* const*)(sim + layout.strands);
	int numParticles = state.numParticles;
	if (!strands || numStrands < 0) numStrands = 0;
	if (numStrands > numParticles) numStrands = numParticles;

	uint64_t* kept = state.strandRecords.get();
	bool changed = !state.rootsBuilt || numStrands != state.numStrands;
	for (int c = 0; c < numStrands; c++)
	{
		uint64_t record;
		memcpy(&record, strands + c * 0x20 + 0x10, sizeof(record));
		if (record != kept[c])
		{
			kept[c] = record;
			changed = true;
		}
	}

	if (!changed)
		return false;

	int32_t* rootOf = state.rootOf.get();

	for (int i = 0; i < numParticles; i++)
	{
		rootOf[i] = i;
	}

	for (int c = 0; c < numStrands; c++)
	{
		int first = *(const int*)(strands + c * 0x20 + 0x10);
		int count = *(const int*)(strands + c * 0x20 + 0x14);
		if (first < 0 || count <= 0 || first >= numParticles) continue;
		if (count > numParticles - first) count = numParticles - first;

		for (int q = first; q < first + count; q++)
		{
			rootOf[q] = first;
		}
	}

	state.numStrands = numStrands;
	state.rootsBuilt = true;
	return true;
}

// Largest axis length of the transform: what the game itself uses as the scale of the body
static inline float SimScale(const float* m)
{
	float best = 0.0f;

	for (int r = 0; r < 3; r++)
	{
		float length = m[r * 4] * m[r * 4] + m[r * 4 + 1] * m[r * 4 + 1] + m[r * 4 + 2] * m[r * 4 + 2];
		if (length > best) best = length;
	}

	return sqrtf(best);
}

static inline void SimAnchor(const uint8_t* particle, const float* matrix, float* out)
{
	const float* local = reinterpret_cast<const float*>(particle + 0x20);

	for (int j = 0; j < 3; j++)
	{
		out[j] = local[0] * matrix[j] + local[1] * matrix[4 + j] + local[2] * matrix[8 + j] + matrix[12 + j];
	}
}

static bool SimInputsCut(const float* prev, const float* live, float delta)
{
	float dx = live[12] - prev[12], dy = live[13] - prev[13], dz = live[14] - prev[14];
	float limit = SIM_CUT_SPEED * delta;
	if (limit < SIM_CUT_MIN_DIST) limit = SIM_CUT_MIN_DIST;

	if (!(dx * dx + dy * dy + dz * dz <= limit * limit))
		return true;

	for (int r = 0; r < 3; r++)
	{
		const float* a = prev + r * 4;
		const float* b = live + r * 4;
		float ab = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
		float aa = a[0] * a[0] + a[1] * a[1] + a[2] * a[2];
		float bb = b[0] * b[0] + b[1] * b[1] + b[2] * b[2];

		// written so that NaN counts as a cut
		if (!(ab > 0.0f) || !(ab * ab >= SIM_CUT_TURN_COS * SIM_CUT_TURN_COS * aa * bb))
			return true;
	}

	return false;
}

static void InterpolateSimMatrix(const float* a, const float* b, float s, float* out)
{
	float t = 1.0f - s;
	float lenA[3]{}, lenB[3]{}, ra[9]{}, rb[9]{};
	bool rigid = true;

	for (int r = 0; r < 3; r++)
	{
		const float* pa = a + r * 4;
		const float* pb = b + r * 4;
		lenA[r] = sqrtf(pa[0] * pa[0] + pa[1] * pa[1] + pa[2] * pa[2]);
		lenB[r] = sqrtf(pb[0] * pb[0] + pb[1] * pb[1] + pb[2] * pb[2]);

		if (!(lenA[r] > 1e-12f) || !(lenB[r] > 1e-12f))
		{
			rigid = false;
			break;
		}

		for (int j = 0; j < 3; j++)
		{
			ra[r * 3 + j] = pa[j] / lenA[r];
			rb[r * 3 + j] = pb[j] / lenB[r];
		}
	}

	float sine = 0.0f, cosine = 1.0f, axis[3] = {};

	if (rigid)
	{
		// the turn that takes a to b (row vectors: rb = ra * d), as sin(angle) * axis and cos(angle)
		float d[9]{};
		for (int i = 0; i < 3; i++)
		{
			for (int j = 0; j < 3; j++)
			{
				d[i * 3 + j] = ra[i] * rb[j] + ra[3 + i] * rb[3 + j] + ra[6 + i] * rb[6 + j];
			}
		}

		axis[0] = (d[5] - d[7]) * 0.5f;
		axis[1] = (d[6] - d[2]) * 0.5f;
		axis[2] = (d[1] - d[3]) * 0.5f;
		sine = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
		cosine = (d[0] + d[4] + d[8] - 1.0f) * 0.5f;
	}

	for (int j = 0; j < 3; j++)
	{
		out[12 + j] = a[12 + j] * t + b[12 + j] * s;
	}

	if (!rigid || !(sine > 1e-6f) || !(cosine > 0.0f))
	{
		for (int r = 0; r < 3; r++)
		{
			const float* pa = a + r * 4;
			const float* pb = b + r * 4;
			float v[3] = { pa[0] * t + pb[0] * s, pa[1] * t + pb[1] * s, pa[2] * t + pb[2] * s };

			float la = sqrtf(pa[0] * pa[0] + pa[1] * pa[1] + pa[2] * pa[2]);
			float lb = sqrtf(pb[0] * pb[0] + pb[1] * pb[1] + pb[2] * pb[2]);
			float lv = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
			float k = lv > 1e-12f ? (la * t + lb * s) / lv : 1.0f;

			out[r * 4] = v[0] * k;
			out[r * 4 + 1] = v[1] * k;
			out[r * 4 + 2] = v[2] * k;
		}

		return;
	}

	float angle = atan2f(sine, cosine) * s;
	float c = cosf(angle), sn = sinf(angle), k = 1.0f - c;
	float x = axis[0] / sine, y = axis[1] / sine, z = axis[2] / sine;

	// the same turn by s * angle, row-vector form
	float p[9] = {
		c + k * x * x,      k * x * y + sn * z, k * x * z - sn * y,
		k * x * y - sn * z, c + k * y * y,      k * y * z + sn * x,
		k * x * z + sn * y, k * y * z - sn * x, c + k * z * z };

	for (int r = 0; r < 3; r++)
	{
		float length = lenA[r] * t + lenB[r] * s;

		for (int j = 0; j < 3; j++)
		{
			out[r * 4 + j] = (ra[r * 3] * p[j] + ra[r * 3 + 1] * p[3 + j] + ra[r * 3 + 2] * p[6 + j]) * length;
		}
	}
}

static bool SimParticlesRewritten(const uint8_t* particles, int numParticles, int stride, const float* applied)
{
	int i = 0;
	for (; i + 4 <= numParticles; i += 4)
	{
		const uint8_t* p = particles + i * stride + 0x10;
		const float* a = applied + i * 4;
		__m128i e0 = _mm_cmpeq_epi32(_mm_loadu_si128((const __m128i*)p), _mm_loadu_si128((const __m128i*)a));
		__m128i e1 = _mm_cmpeq_epi32(_mm_loadu_si128((const __m128i*)(p + stride)), _mm_loadu_si128((const __m128i*)(a + 4)));
		__m128i e2 = _mm_cmpeq_epi32(_mm_loadu_si128((const __m128i*)(p + 2 * stride)), _mm_loadu_si128((const __m128i*)(a + 8)));
		__m128i e3 = _mm_cmpeq_epi32(_mm_loadu_si128((const __m128i*)(p + 3 * stride)), _mm_loadu_si128((const __m128i*)(a + 12)));
		__m128i all = _mm_and_si128(_mm_and_si128(e0, e1), _mm_and_si128(e2, e3));
		if ((_mm_movemask_epi8(all) & 0x0FFF) != 0x0FFF) return true;
	}

	for (; i < numParticles; i++)
	{
		__m128i found = _mm_loadu_si128((const __m128i*)(particles + i * stride + 0x10));
		__m128i drawn = _mm_loadu_si128((const __m128i*)(applied + i * 4));
		if ((_mm_movemask_epi8(_mm_cmpeq_epi32(found, drawn)) & 0x0FFF) != 0x0FFF) return true;
	}

	return false;
}

static inline void SimStoreXyz(float* out, __m128 v)
{
	_mm_storel_epi64((__m128i*)out, _mm_castps_si128(v));
	_mm_store_ss(out + 2, _mm_movehl_ps(v, v));
}

static bool CheckAndRestoreSimParticles(uint8_t* particles, int numParticles, int stride, const float* applied, const float* trueP1)
{
	constexpr int BLOCK = 8;
	for (int first = 0; first < numParticles; first += BLOCK)
	{
		const int last = first + BLOCK < numParticles ? first + BLOCK : numParticles;
		__m128i same = _mm_set1_epi32(-1);

		for (int i = first; i < last; i++)
		{
			const uint8_t* p = particles + i * stride + 0x10;
			_mm_prefetch((const char*)p + 8 * stride, _MM_HINT_T0);
			same = _mm_and_si128(same, _mm_cmpeq_epi32(_mm_loadu_si128((const __m128i*)p), _mm_loadu_si128((const __m128i*)(applied + i * 4))));
		}

		if ((_mm_movemask_epi8(same) & 0x0FFF) != 0x0FFF) return false;

		for (int i = first; i < last; i++)
		{
			SimStoreXyz((float*)(particles + i * stride + 0x10), _mm_loadu_ps(trueP1 + i * 4));
		}
	}

	return true;
}

template <bool Roots>
static void DrawSimParticles(uint8_t* particles, int numParticles, int stride, const int32_t* rootOf, const float* matrix, float alpha, const float* offPrev, const float* offCurr, float* applied)
{
	const __m128 row0 = _mm_loadu_ps(matrix), row1 = _mm_loadu_ps(matrix + 4), row2 = _mm_loadu_ps(matrix + 8), row3 = _mm_loadu_ps(matrix + 12);
	const __m128 blend = _mm_set1_ps(alpha);
	const __m128 size = _mm_set1_ps(SimScale(matrix));

	int32_t anchorIndex = -1;
	__m128 base = _mm_setzero_ps();

	for (int i = 0; i < numParticles; i++)
	{
		int32_t index = Roots ? rootOf[i] : i;
		if (index != anchorIndex)
		{
			anchorIndex = index;
			__m128 local = _mm_loadu_ps((const float*)(particles + index * stride + 0x20));
			base = _mm_add_ps(_mm_add_ps(_mm_add_ps(
				_mm_mul_ps(_mm_shuffle_ps(local, local, 0x00), row0),
				_mm_mul_ps(_mm_shuffle_ps(local, local, 0x55), row1)),
				_mm_mul_ps(_mm_shuffle_ps(local, local, 0xAA), row2)), row3);
		}

		float* p1 = (float*)(particles + i * stride + 0x10);
		int k = i * 4;

		_mm_prefetch((const char*)p1 + 8 * stride, _MM_HINT_T0);

		__m128 prev = _mm_loadu_ps(offPrev + k);
		__m128 drawn = _mm_add_ps(base, _mm_mul_ps(_mm_add_ps(prev, _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(offCurr + k), prev), blend)), size));
		SimStoreXyz(p1, drawn);
		_mm_storeu_ps(applied + k, drawn);
	}
}

static bool CheckAndDrawSimParticles(uint8_t* particles, int numParticles, int stride, const int32_t* rootOf, const float* matrix, float alpha, const float* offPrev, const float* offCurr, float* applied, int* drawn)
{
	constexpr int BLOCK = 8;
	const __m128 row0 = _mm_loadu_ps(matrix), row1 = _mm_loadu_ps(matrix + 4), row2 = _mm_loadu_ps(matrix + 8), row3 = _mm_loadu_ps(matrix + 12);
	const __m128 blend = _mm_set1_ps(alpha);
	const __m128 size = _mm_set1_ps(SimScale(matrix));

	int32_t anchorIndex = -1;
	__m128 base = _mm_setzero_ps();

	for (int first = 0; first < numParticles; first += BLOCK)
	{
		const int last = first + BLOCK < numParticles ? first + BLOCK : numParticles;
		__m128i same = _mm_set1_epi32(-1);

		for (int i = first; i < last; i++)
		{
			const uint8_t* p = particles + i * stride + 0x10;
			_mm_prefetch((const char*)p + 8 * stride, _MM_HINT_T0);
			same = _mm_and_si128(same, _mm_cmpeq_epi32(_mm_loadu_si128((const __m128i*)p), _mm_loadu_si128((const __m128i*)(applied + i * 4))));
		}

		if ((_mm_movemask_epi8(same) & 0x0FFF) != 0x0FFF)
		{
			*drawn = first;
			return false;
		}

		for (int i = first; i < last; i++)
		{
			int32_t index = rootOf[i];
			if (index != anchorIndex)
			{
				anchorIndex = index;
				__m128 local = _mm_loadu_ps((const float*)(particles + index * stride + 0x20));
				base = _mm_add_ps(_mm_add_ps(_mm_add_ps(
					_mm_mul_ps(_mm_shuffle_ps(local, local, 0x00), row0),
					_mm_mul_ps(_mm_shuffle_ps(local, local, 0x55), row1)),
					_mm_mul_ps(_mm_shuffle_ps(local, local, 0xAA), row2)), row3);
			}

			int k = i * 4;
			__m128 prev = _mm_loadu_ps(offPrev + k);
			__m128 out = _mm_add_ps(base, _mm_mul_ps(_mm_add_ps(prev, _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(offCurr + k), prev), blend)), size));
			SimStoreXyz((float*)(particles + i * stride + 0x10), out);
			_mm_storeu_ps(applied + k, out);
		}
	}

	*drawn = numParticles;
	return true;
}

template <bool Roots>
static void CaptureAndDrawSimParticles(uint8_t* particles, int numParticles, int stride, const int32_t* rootOf, const float* matrix, float unscale, float alpha, float* trueP1, const float* offPrev, float* offCurr, float* applied)
{
	const __m128 row0 = _mm_loadu_ps(matrix), row1 = _mm_loadu_ps(matrix + 4), row2 = _mm_loadu_ps(matrix + 8), row3 = _mm_loadu_ps(matrix + 12);
	const __m128 blend = _mm_set1_ps(alpha);
	const __m128 size = _mm_set1_ps(SimScale(matrix));
	const __m128 scale = _mm_set1_ps(unscale);

	int32_t anchorIndex = -1;
	__m128 anchor = _mm_setzero_ps(), base = _mm_setzero_ps();

	for (int i = 0; i < numParticles; i++)
	{
		int32_t index = Roots ? rootOf[i] : i;
		if (index != anchorIndex)
		{
			anchorIndex = index;
			anchor = _mm_loadu_ps((const float*)(particles + index * stride + 0x30));
			__m128 local = _mm_loadu_ps((const float*)(particles + index * stride + 0x20));
			base = _mm_add_ps(_mm_add_ps(_mm_add_ps(
				_mm_mul_ps(_mm_shuffle_ps(local, local, 0x00), row0),
				_mm_mul_ps(_mm_shuffle_ps(local, local, 0x55), row1)),
				_mm_mul_ps(_mm_shuffle_ps(local, local, 0xAA), row2)), row3);
		}

		float* p1 = (float*)(particles + i * stride + 0x10);
		int k = i * 4;
		_mm_prefetch((const char*)p1 + 8 * stride, _MM_HINT_T0);

		__m128 current = _mm_loadu_ps(p1);
		__m128 off = _mm_mul_ps(_mm_sub_ps(current, anchor), scale);
		_mm_storeu_ps(trueP1 + k, current);
		_mm_storeu_ps(offCurr + k, off);

		__m128 prev = _mm_loadu_ps(offPrev + k);
		__m128 drawn = _mm_add_ps(base, _mm_mul_ps(_mm_add_ps(prev, _mm_mul_ps(_mm_sub_ps(off, prev), blend)), size));
		SimStoreXyz(p1, drawn);
		_mm_storeu_ps(applied + k, drawn);
	}
}

static void AdoptSimParticles(ParticleSimState* state, const uint8_t* particles, int numParticles, int stride, const int32_t* rootOf, const float* frame)
{
	float scale = SimScale(frame);
	float unscale = scale > 1e-6f ? 1.0f / scale : 1.0f;

	for (int i = 0; i < numParticles; i++)
	{
		const float* p1 = (const float*)(particles + i * stride + 0x10);
		int k = i * 4;

		float anchor[3];
		SimAnchor(particles + rootOf[i] * stride, frame, anchor);

		for (int j = 0; j < 3; j++) state->offCurr[k + j] = (p1[j] - anchor[j]) * unscale;
		memcpy(state->offPrev + k, state->offCurr + k, sizeof(float) * 3);
	}
}

static uint32_t StepTheirs(safetyhook::InlineHook& original, void* thisPtr, float delta, ParticleSimState* state, const uint8_t* particles, int numParticles, int stride)
{
	uint32_t result = 0;
	if (delta > 0.0f) result = original.unsafe_thiscall<uint32_t>(thisPtr, delta < SIM_STEP_DT ? delta : SIM_STEP_DT);

	for (int i = 0; i < numParticles; i++)
	{
		memcpy(state->applied + i * 4, particles + i * stride + 0x10, sizeof(float) * 3);
	}

	state->theirs = true;
	state->primed = false;
	return result;
}

static uint32_t StepAtFixedRate(safetyhook::InlineHook& original, const ParticleSimLayout& layout, void* thisPtr, float delta)
{
	uint8_t* sim = (uint8_t*)thisPtr;
	int numParticles = *(int*)(sim + layout.numParticles);
	int numColliders = *(int*)(sim + layout.numColliders);
	uint8_t* particles = *(uint8_t**)(sim + layout.particles);
	uint8_t* colliders = *(uint8_t**)(sim + layout.colliders);
	const int stride = layout.particleStride;

	if (!particles || numParticles <= 0 || numParticles > SIM_MAX_PARTICLES)
		return original.unsafe_thiscall<uint32_t>(thisPtr, delta);

	uint64_t now = GetTickCount64();
	uint64_t clock = SimActivityClock(now);
	PurgeSimStates(clock);

	// Too many colliders to track: they stay live, the transform is still resampled
	int trackedColliders = (colliders && numColliders > 0 && numColliders <= SIM_MAX_COLLIDERS) ? numColliders : 0;

	ParticleSimState* state = AcquireSimState((uintptr_t)thisPtr, now, clock, &layout, numParticles, trackedColliders, particles, colliders);
	if (!state)
		return original.unsafe_thiscall<uint32_t>(thisPtr, delta);

	float* matrix = (float*)sim;
	float* center = layout.radialCenter >= 0 ? (float*)(sim + layout.radialCenter) : nullptr;
	int32_t* rootOf = state->rootOf.get();

	uint32_t result = 0;
	bool tick = !state->primed;
	const float span = delta < SIM_STEP_DT ? delta : TARGET_FRAME_TIME;
	float lastAlpha = state->accumulator / TARGET_FRAME_TIME;
	if (lastAlpha < 0.0f) lastAlpha = 0.0f;
	if (lastAlpha > 1.0f) lastAlpha = 1.0f;

	if (state->primed && delta > 0.0f)
	{
		state->accumulator += span;
		tick = state->accumulator >= TARGET_FRAME_TIME;
	}

	bool rootsChanged = rootOf && UpdateSimRoots(layout, sim, *state);
	if (state->primed && rootsChanged)
	{
		AdoptSimParticles(state, particles, numParticles, stride, rootOf, state->hasPrevInputs ? state->prevMatrix : matrix);
	}

	const bool restoreNow = state->primed && tick;
	const bool checkFirst = restoreNow || ((state->primed || state->theirs) && (rootsChanged || state->theirs || !rootOf));
	const bool rewritten = restoreNow ? !CheckAndRestoreSimParticles(particles, numParticles, stride, state->applied, state->trueP1) : checkFirst && SimParticlesRewritten(particles, numParticles, stride, state->applied);
	if (rewritten)
		return StepTheirs(original, thisPtr, delta, state, particles, numParticles, stride);

	state->theirs = false;

	for (int c = 0; c < trackedColliders; c++)
	{
		memcpy(state->liveColliders + c * 3, colliders + c * 0x30, sizeof(float) * 3);
	}

	// cloth: the rest pose as the callers posed it for this frame
	if (state->restLive)
	{
		for (int i = 0; i < numParticles; i++)
		{
			memcpy(state->restLive + i * 6, particles + i * stride + 0x20, sizeof(float) * 3);
			memcpy(state->restLive + i * 6 + 3, particles + i * stride + layout.drivenNormal, sizeof(float) * 3);
		}
	}

	float liveCenter[3] = {};
	float liveStrength = 0.0f;

	if (center)
	{
		memcpy(liveCenter, center, sizeof(liveCenter));
		liveStrength = *(float*)(sim + layout.radialStrength);

		if (state->blastStrengthPending)
		{
			state->blastStrength = liveStrength;
			state->blastStrengthValid = true;
			state->blastStrengthPending = false;
		}

		if (state->hasPrevInputs)
		{
			float local[3], inv[9];
			SimInverseBasis(state->prevMatrix, inv);
			SimLocalPoint(liveCenter, state->prevMatrix, inv, local);

			if (state->blastLocalValid)
			{
				float dx = local[0] - state->blastLocal[0], dy = local[1] - state->blastLocal[1], dz = local[2] - state->blastLocal[2];
				float reach = fabsf(local[0]) + fabsf(local[1]) + fabsf(local[2]);
				state->blastAttached = dx * dx + dy * dy + dz * dz <= (0.5f + 0.001f * reach) * (0.5f + 0.001f * reach);
			}

			memcpy(state->blastLocal, local, sizeof(local));
			state->blastLocalValid = true;
		}
	}

	if (tick)
	{
		float liveMatrix[16];
		float liveLengthScale = 1.0f;
		float liveForceWind[6];
		memcpy(liveMatrix, matrix, sizeof(liveMatrix));

		float s = 1.0f;
		if (state->primed)
		{
			s = 1.0f - (state->accumulator - TARGET_FRAME_TIME) / span;
			if (s < 0.0f) s = 0.0f;
			if (s > 1.0f) s = 1.0f;
		}

		const bool resample = state->primed && s < 1.0f && state->hasPrevInputs && !SimInputsCut(state->prevMatrix, liveMatrix, span);

		if (resample)
		{
			InterpolateSimMatrix(state->prevMatrix, liveMatrix, s, matrix);

			if (layout.lengthScale >= 0)
			{
				float* lengthScale = (float*)(sim + layout.lengthScale);
				liveLengthScale = *lengthScale;
				*lengthScale = state->prevLengthScale + (liveLengthScale - state->prevLengthScale) * s;
			}

			float* force = (float*)(sim + layout.force);
			float* windAmp = (float*)(sim + layout.windAmp);
			memcpy(liveForceWind, force, sizeof(float) * 3);
			memcpy(liveForceWind + 3, windAmp, sizeof(float) * 3);
			for (int j = 0; j < 3; j++)
			{
				force[j] = state->prevForceWind[j] + (liveForceWind[j] - state->prevForceWind[j]) * s;
				windAmp[j] = state->prevForceWind[3 + j] + (liveForceWind[3 + j] - state->prevForceWind[3 + j]) * s;
			}

			// Colliders hang on bones of the same body: interpolated in the body's frame they follow its turn exactly
			float invPrev[9], invLive[9];
			SimInverseBasis(state->prevMatrix, invPrev);
			SimInverseBasis(liveMatrix, invLive);

			for (int c = 0; c < trackedColliders; c++)
			{
				float a[3], b[3];
				SimLocalPoint(state->prevColliders + c * 3, state->prevMatrix, invPrev, a);
				SimLocalPoint(state->liveColliders + c * 3, liveMatrix, invLive, b);

				float local[3] = { a[0] + (b[0] - a[0]) * s, a[1] + (b[1] - a[1]) * s, a[2] + (b[2] - a[2]) * s };
				SimWorldPoint(local, matrix, (float*)(colliders + c * 0x30));
			}

			if (state->restLive)
			{
				for (int i = 0; i < numParticles; i++)
				{
					const float* a = state->restPrev + i * 6;
					const float* b = state->restLive + i * 6;
					float* rest = (float*)(particles + i * stride + 0x20);
					float* normal = (float*)(particles + i * stride + layout.drivenNormal);

					for (int j = 0; j < 3; j++)
					{
						rest[j] = a[j] + (b[j] - a[j]) * s;
						normal[j] = a[3 + j] + (b[3 + j] - a[3 + j]) * s;
					}
				}
			}
		}

		const bool lagBlast = center && state->blastAttached && state->hasTickMatrix;
		if (lagBlast)
		{
			SimWorldPoint(state->blastLocal, state->tickMatrix, center);
			if (liveStrength != 0.0f && state->blastStrengthValid) *(float*)(sim + layout.radialStrength) = state->blastStrength;
		}

		if (state->primed)
		{
			std::swap(state->offPrev, state->offCurr);
		}

		result = original.unsafe_thiscall<uint32_t>(thisPtr, state->primed || !(delta > 0.0f && delta < SIM_STEP_DT) ? SIM_STEP_DT : delta);

		memcpy(state->tickMatrix, matrix, sizeof(state->tickMatrix));
		state->hasTickMatrix = true;
		state->blastStrengthPending = center != nullptr;

		// Everything back the way the callers left it: they and the bone pass that follows read it
		if (resample)
		{
			memcpy(matrix, liveMatrix, sizeof(liveMatrix));
			if (layout.lengthScale >= 0) *(float*)(sim + layout.lengthScale) = liveLengthScale;
			memcpy(sim + layout.force, liveForceWind, sizeof(float) * 3);
			memcpy(sim + layout.windAmp, liveForceWind + 3, sizeof(float) * 3);

			for (int c = 0; c < trackedColliders; c++)
			{
				memcpy(colliders + c * 0x30, state->liveColliders + c * 3, sizeof(float) * 3);
			}

			if (state->restLive)
			{
				for (int i = 0; i < numParticles; i++)
				{
					memcpy(particles + i * stride + 0x20, state->restLive + i * 6, sizeof(float) * 3);
					memcpy(particles + i * stride + layout.drivenNormal, state->restLive + i * 6 + 3, sizeof(float) * 3);
				}
			}
		}

		if (lagBlast)
		{
			memcpy(center, liveCenter, sizeof(liveCenter));
			*(float*)(sim + layout.radialStrength) = liveStrength;
		}

		float scale = SimScale(state->tickMatrix);
		float unscale = scale > 1e-6f ? 1.0f / scale : 1.0f;
		float alpha = state->primed ? (state->accumulator - TARGET_FRAME_TIME) / TARGET_FRAME_TIME : 0.0f;
		if (alpha < 0.0f) alpha = 0.0f;
		if (alpha > 1.0f) alpha = 1.0f;
		const float* offPrev = state->primed ? state->offPrev : state->offCurr;
		if (rootOf) CaptureAndDrawSimParticles<true>(particles, numParticles, stride, rootOf, matrix, unscale, alpha, state->trueP1, offPrev, state->offCurr, state->applied);
		else CaptureAndDrawSimParticles<false>(particles, numParticles, stride, rootOf, matrix, unscale, alpha, state->trueP1, offPrev, state->offCurr, state->applied);

		if (state->primed)
		{
			state->accumulator -= TARGET_FRAME_TIME;
		}
		else
		{
			memcpy(state->offPrev, state->offCurr, numParticles * 4 * sizeof(float));
			state->accumulator = 0.0f;
			state->primed = true;
		}
	}

	float lastMatrix[16];
	memcpy(lastMatrix, state->prevMatrix, sizeof(lastMatrix));
	memcpy(state->prevMatrix, matrix, sizeof(state->prevMatrix));
	if (layout.lengthScale >= 0) state->prevLengthScale = *(float*)(sim + layout.lengthScale);
	memcpy(state->prevForceWind, sim + layout.force, sizeof(float) * 3);
	memcpy(state->prevForceWind + 3, sim + layout.windAmp, sizeof(float) * 3);
	std::swap(state->prevColliders, state->liveColliders);
	if (state->restLive) std::swap(state->restPrev, state->restLive);
	state->hasPrevInputs = true;

	if (tick)
		return result;

	float alpha = state->accumulator / TARGET_FRAME_TIME;
	if (alpha < 0.0f) alpha = 0.0f;
	if (alpha > 1.0f) alpha = 1.0f;

	if (checkFirst)
	{
		if (rootOf) DrawSimParticles<true>(particles, numParticles, stride, rootOf, matrix, alpha, state->offPrev, state->offCurr, state->applied);
		else DrawSimParticles<false>(particles, numParticles, stride, rootOf, matrix, alpha, state->offPrev, state->offCurr, state->applied);
		return result;
	}

	int drawn = 0;
	if (CheckAndDrawSimParticles(particles, numParticles, stride, rootOf, matrix, alpha, state->offPrev, state->offCurr, state->applied, &drawn))
		return result;

	DrawSimParticles<true>(particles, drawn, stride, rootOf, lastMatrix, lastAlpha, state->offPrev, state->offCurr, state->applied);
	return StepTheirs(original, thisPtr, delta, state, particles, numParticles, stride);

	return result;
}

// ---- Hair ----

safetyhook::InlineHook HairSimulator;

static void __fastcall HairSimulator_Hook(void* thisPtr, int, float delta)
{
	StepAtFixedRate(HairSimulator, HAIR_LAYOUT, thisPtr, delta);
}

void ApplyFixHighFPSHairPhysics()
{
	if (!FixHighFPSHairPhysics) return;

	HairSimulator = HookHelper::CreateHook((void*)GetAddress(Addr::HairSimulator), &HairSimulator_Hook);
}

// ---- Cloth ----

safetyhook::InlineHook ClothSimulator;

constexpr int CLOTH_MAX_PARTICLES = 80;

static uint32_t __fastcall ClothSimulator_Hook(void* thisPtr, int, float delta)
{
	uint8_t* cloth = (uint8_t*)thisPtr;
	int numParticles = *(int*)(cloth + 0xB8);

	bool bypass = numParticles > CLOTH_MAX_PARTICLES;

	// Dollmaker strings
	if (!bypass && *(int*)(cloth + 0xC0) == 0)
	{
		int constraints = *(int*)(cloth + 0xBC);
		bypass = (numParticles == 15 && constraints == 27) || (numParticles == 20 && constraints == 37);
	}

	if (bypass)
		return ClothSimulator.unsafe_thiscall<uint32_t>(thisPtr, delta);

	return StepAtFixedRate(ClothSimulator, CLOTH_LAYOUT, thisPtr, delta);
}

void ApplyFixHighFPSClothPhysics()
{
	if (!FixHighFPSClothPhysics) return;

	ClothSimulator = HookHelper::CreateHook((void*)GetAddress(Addr::ClothSimulator), &ClothSimulator_Hook);
}

// ---- Walking ----

static safetyhook::MidHook WalkFloorStick{};
static safetyhook::MidHook WalkVelocityRecompute{};
static safetyhook::MidHook WalkFloorAccepted{};
static safetyhook::MidHook WalkFallGate{};
static safetyhook::MidHook WalkSetBaseGuard{};
static safetyhook::MidHook FallIntegratedVel{};
static safetyhook::MidHook FallVelRecompute{};
static safetyhook::MidHook FallLandVelGate{};

static uintptr_t WalkVelSkipTarget = 0;
static uintptr_t WalkFallGateResume = 0;
static uintptr_t WalkSetBaseSkip = 0;
static uintptr_t FallLandVelTake = 0;

constexpr float FloorGraceVoid = 0.05f;
constexpr float FloorGraceContact = 0.15f;
constexpr float FallRestoreCap = -80.0f;

constexpr float PhantomFallMaxSeconds = 0.5f;
constexpr float PhantomFallMaxDrop = 60.0f;
constexpr uint64_t PhantomNeedsFireMs = 1500;
constexpr uint64_t GateSuppressMs = 600;
constexpr uint64_t GateSuppressCapMs = 4000;

constexpr float BudgetStaleSeconds = 0.1f;

constexpr float DesignFrameCutoff = TARGET_FRAME_TIME * 0.9f;
constexpr float WindowNoiseFloor = 0.3f;
constexpr float WindowObstructedRatio = 0.55f;
constexpr float ObstructedExitCos = 0.57f;
constexpr float ObstructedFreeSpeed = 200.0f;
constexpr float ObstructedGoneRatio = 0.98f;
constexpr float GoodMoveMinExp = 1.0f;
constexpr uint64_t GoodMoveGraceMs = 600;

struct FloorState
{
	float badTime = 0.0f;
	float voidTime = 0.0f;
	FVector lastGood;
	uint64_t suppressUntil = 0;
	uint64_t suppressArmedAt = 0;
	float budgetStale = 0.0f;
	FVector frameLoc;
	bool frameLocValid = false;
	float winTime = 0.0f;
	float winDx = 0.0f, winDy = 0.0f, winExp = 0.0f;
	bool obstructed = false;
	float holdX = 0.0f, holdY = 0.0f;
	uintptr_t frameBase = 0;
	FVector obstructedDir;
	uint64_t lastBudgetPinnedAt = 0;
	uint64_t lastGoodMoveAt = 0;

	FloorState() { lastGood.Z = 1.0f; }
};
static std::unordered_map<uintptr_t, FloorState> floorStates;

static bool fallEpisode = false;
static uint64_t fallEpisodeStart = 0;
static uintptr_t fallPawn = 0;
static FVector fallIntVel;
static float fallBeginZ = 0.0f;
static uint64_t lastGateFireAt = 0;

static void OnWalkFloorStick(safetyhook::Context& ctx)
{
	float dt = *reinterpret_cast<float*>(ctx.ebx + 0x8);
	float s = TARGET_FRAME_TIME / dt;
	float* v = reinterpret_cast<float*>(ctx.ebp - 0x278);
	*v *= s; // the nudge is per-frame dt^2, rescale once to the 30fps design frame
}

static void OnWalkVelocityRecompute(safetyhook::Context& ctx)
{
	float dt = *reinterpret_cast<float*>(ctx.ebx + 0x8);
	AAlicePawn* pawn = reinterpret_cast<AAlicePawn*>(ctx.edi);
	FloorState& fs = floorStates[ctx.edi];

	if (pawn->bInGiantMode)
	{
		fs.frameLocValid = false;
		fs.obstructed = false;
		return;
	}

	if (dt >= DesignFrameCutoff)
	{
		fs.frameLocValid = false;
		return;
	}

	FVector ref = pawn->Location;
	if (pawn->Base)
	{
		ref.X -= pawn->Base->Location.X;
		ref.Y -= pawn->Base->Location.Y;
	}
	if (reinterpret_cast<uintptr_t>(pawn->Base) != fs.frameBase)
	{
		fs.frameBase = reinterpret_cast<uintptr_t>(pawn->Base);
		fs.frameLocValid = false;
	}

	float dx = ref.X - fs.frameLoc.X;
	float dy = ref.Y - fs.frameLoc.Y;
	bool first = !fs.frameLocValid;
	fs.frameLoc = ref;
	fs.frameLocValid = true;

	float expected = sqrtf(pawn->Velocity.X * pawn->Velocity.X + pawn->Velocity.Y * pawn->Velocity.Y) * dt;

	float vbx = pawn->Velocity.X, vby = pawn->Velocity.Y;

	if (first)
	{
		fs.winTime = 0.0f;
		fs.winDx = 0.0f; fs.winDy = 0.0f; fs.winExp = 0.0f;
		fs.obstructed = false;
	}
	else
	{
		fs.winTime += dt;
		fs.winDx += dx;
		fs.winDy += dy;
		fs.winExp += expected;

		// Classify at the design timescale, where contact jitter divides away
		if (fs.winTime >= TARGET_FRAME_TIME)
		{
			float winMoved = sqrtf(fs.winDx * fs.winDx + fs.winDy * fs.winDy);
			float winSpeed = winMoved / fs.winTime;
			if (fs.winExp >= GoodMoveMinExp && winMoved >= fs.winExp * WindowObstructedRatio)
			{
				fs.lastGoodMoveAt = GetTickCount64();
			}

			if (GetTickCount64() - fs.lastBudgetPinnedAt < 250 && GetTickCount64() - fs.lastGoodMoveAt < GoodMoveGraceMs)
			{
				fs.obstructed = false;
			}
			else if (!fs.obstructed)
			{
				if (fs.winExp >= WindowNoiseFloor && winMoved < fs.winExp * WindowObstructedRatio)
				{
					fs.obstructed = true;
					fs.holdX = fs.winDx / fs.winTime;
					fs.holdY = fs.winDy / fs.winTime;
					float sp = sqrtf(vbx * vbx + vby * vby);

					if (sp > 1.0f)
					{
						fs.obstructedDir.X = vbx / sp;
						fs.obstructedDir.Y = vby / sp;
					}
				}
			}
			else if (winSpeed >= ObstructedFreeSpeed || (fs.winExp >= 0.15f && winMoved >= fs.winExp * ObstructedGoneRatio))
			{
				fs.obstructed = false; // genuinely moving again, or the obstruction is gone
			}
			else
			{
				fs.holdX = fs.winDx / fs.winTime;
				fs.holdY = fs.winDy / fs.winTime;
			}

			fs.winTime = 0.0f;
			fs.winDx = 0.0f; fs.winDy = 0.0f; fs.winExp = 0.0f;
		}
	}

	// steering away from the obstruction releases the hold instantly
	if (fs.obstructed)
	{
		float ax = pawn->Acceleration.X, ay = pawn->Acceleration.Y;
		float a2 = ax * ax + ay * ay;
		if (a2 > 1.0f)
		{
			float inv = 1.0f / sqrtf(a2);
			if (ax * inv * fs.obstructedDir.X + ay * inv * fs.obstructedDir.Y < ObstructedExitCos)
			{
				fs.obstructed = false;
			}
		}
	}

	if (fs.obstructed)
	{
		pawn->Velocity.X = fs.holdX;
		pawn->Velocity.Y = fs.holdY;
	}

	ctx.eip = WalkVelSkipTarget; // "Velocity.Z = 0"
}

static void ClearStaleStepUpBudget(AAlicePawn* pawn, FloorState& fs, float tick)
{
	// physWalking can skip the per-substep fStepUpAccumZ reset, and the residue pins stepUp against near-vertical hits indefinitely
	// Drop residue that outlives a few frames
	if (pawn->fStepUpAccumZ > pawn->fStepUpBugZ)
	{
		uint64_t now = GetTickCount64();
		fs.lastBudgetPinnedAt = now;
		fs.budgetStale += tick;

		// Residue during locomotion is stale, a pinned budget while she grinds in place is the anti-climb lock on jump geometry and stays
		if (fs.budgetStale >= BudgetStaleSeconds && now - fs.lastGoodMoveAt < GoodMoveGraceMs)
		{
			pawn->fStepUpAccumZ = 0.0f;
			fs.budgetStale = 0.0f;
		}
	}
	else
	{
		fs.budgetStale = 0.0f;
	}
}

static void OnWalkSetBase(safetyhook::Context& ctx)
{
	if (*reinterpret_cast<uintptr_t*>(ctx.ebp - 0xDC) == 0) // [ebp-0DCh] = Hit.Actor
	{
		ctx.eip = WalkSetBaseSkip;
	}
}

static void OnWalkFloorAccepted(safetyhook::Context& ctx)
{
	AAlicePawn* pawn = reinterpret_cast<AAlicePawn*>(ctx.edi);

	if (floorStates.size() > 64) floorStates.clear();

	FloorState& fs = floorStates[ctx.edi];
	fs.badTime = 0.0f;
	fs.voidTime = 0.0f;
	fs.lastGood = pawn->Floor;

	ClearStaleStepUpBudget(pawn, fs, *reinterpret_cast<float*>(ctx.ebp - 0x38)); // [ebp-38h] = timeTick

	if (fallEpisode)
	{
		fallEpisode = false;
		uint64_t now = GetTickCount64();
		float dur = (now - fallEpisodeStart) / 1000.0f;
		float dz = pawn->Location.Z - fallBeginZ;

		pawn->fStepUpAccumZ = 0.0f;
		fs.budgetStale = 0.0f;
		fs.frameLocValid = false;
		fs.obstructed = false;
		fs.winTime = 0.0f;
		fs.winDx = 0.0f; fs.winDy = 0.0f; fs.winExp = 0.0f;

		if (now - lastGateFireAt < PhantomNeedsFireMs && dur < PhantomFallMaxSeconds && dz > -PhantomFallMaxDrop)
		{
			fs.suppressUntil = now + GateSuppressMs;
			fs.suppressArmedAt = now;
		}
	}
}

static void OnWalkFallGate(safetyhook::Context& ctx)
{
	AAlicePawn* pawn = reinterpret_cast<AAlicePawn*>(ctx.edi);
	float tick = *reinterpret_cast<float*>(ctx.ebp - 0x38); // [ebp-38h] = timeTick
	float hitTime = *reinterpret_cast<float*>(ctx.ebp - 0xD8); // [ebp-0D8h] = floor-probe Hit.Time
	bool voidBelow = hitTime >= 1.0f;

	if (floorStates.size() > 64) floorStates.clear();

	FloorState& fs = floorStates[ctx.edi];

	ClearStaleStepUpBudget(pawn, fs, tick);

	fs.badTime += tick;
	if (voidBelow)
	{
		fs.voidTime += tick;
	}
	else
	{
		fs.voidTime = 0.0f;
	}

	bool voidFire = fs.voidTime >= FloorGraceVoid;
	bool contactFire = fs.badTime >= FloorGraceContact;

	uint64_t now = GetTickCount64();

	if (now < fs.suppressUntil && !voidFire)
	{
		if (now - fs.suppressArmedAt < GateSuppressCapMs)
		{
			fs.suppressUntil = now + GateSuppressMs;
		}

		pawn->Floor = fs.lastGood;
		ctx.eip = WalkFallGateResume;
		return;
	}

	if (!voidFire && !contactFire)
	{
		pawn->Floor = fs.lastGood;
		ctx.eip = WalkFallGateResume;
		return;
	}

	lastGateFireAt = now;
	fs.badTime = 0.0f;
	fs.voidTime = 0.0f;
}

static void OnFallIntegrated(safetyhook::Context& ctx)
{
	AAlicePawn* pawn = reinterpret_cast<AAlicePawn*>(ctx.esi);

	fallPawn = ctx.esi;
	fallIntVel = pawn->Velocity;

	if (!fallEpisode)
	{
		fallEpisode = true;
		fallEpisodeStart = GetTickCount64();
		fallBeginZ = pawn->Location.Z;
	}
}

static void OnFallVelRecompute(safetyhook::Context& ctx)
{
	if (ctx.esi != fallPawn) return;

	AAlicePawn* pawn = reinterpret_cast<AAlicePawn*>(ctx.esi);
	float dz = pawn->Location.Z - *reinterpret_cast<float*>(ctx.ebp - 0x9C); // [ebp-9Ch] = OldLocation.Z
	float tick = *reinterpret_cast<float*>(ctx.ebp - 0x24); // [ebp-24h] = timeTick
	float intZ = fallIntVel.Z;

	if (tick < 0.012f && fabsf(dz) < 0.05f && intZ < 0.0f && pawn->Velocity.Z > 0.5f * intZ)
	{
		pawn->Velocity.Z = intZ < FallRestoreCap ? FallRestoreCap : intZ;
	}
}

static void OnFallLandVelGate(safetyhook::Context& ctx)
{
	if (ctx.xmm4.f32[0] > 1e-7f)
	{
		ctx.eip = FallLandVelTake;
	}
}

void ApplyFixHighFPSWalkingPhysics()
{
	if (!FixHighFPSWalkingPhysics) return;

	WalkVelSkipTarget = GetAddress(Addr::WalkVelocityRecomputeSkip);
	WalkFallGateResume = GetAddress(Addr::WalkFallGateResume);
	WalkSetBaseSkip = GetAddress(Addr::WalkSetBaseSkip);
	FallLandVelTake = GetAddress(Addr::FallLandVelTake);
	WalkFloorStick = safetyhook::create_mid(GetAddress(Addr::WalkFloorStick), OnWalkFloorStick);
	WalkVelocityRecompute = safetyhook::create_mid(GetAddress(Addr::WalkVelocityRecompute), OnWalkVelocityRecompute);
	WalkFloorAccepted = safetyhook::create_mid(GetAddress(Addr::WalkFloorAccepted), OnWalkFloorAccepted);
	WalkFallGate = safetyhook::create_mid(GetAddress(Addr::WalkFallGate), OnWalkFallGate);
	WalkSetBaseGuard = safetyhook::create_mid(GetAddress(Addr::WalkSetBaseGuard), OnWalkSetBase);
	FallIntegratedVel = safetyhook::create_mid(GetAddress(Addr::FallIntegrated), OnFallIntegrated);
	FallVelRecompute = safetyhook::create_mid(GetAddress(Addr::FallVelRecompute), OnFallVelRecompute);
	FallLandVelGate = safetyhook::create_mid(GetAddress(Addr::FallLandVelGate), OnFallLandVelGate);
}
