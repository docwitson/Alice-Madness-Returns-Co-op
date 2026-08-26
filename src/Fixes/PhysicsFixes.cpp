#include "Common.hpp"
#include "Features.hpp"

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

// ---- Hair ----

safetyhook::InlineHook HairSimulator;
static safetyhook::MidHook hairDeltaTimeOverride{};
static safetyhook::MidHook hairDeltaTimeRestore{};
static safetyhook::MidHook hairGravityAttenuate{};
static safetyhook::MidHook hairWindAttenuate{};
static safetyhook::MidHook hairShapeMatchDecompound{};
static safetyhook::MidHook hairInnerPhaseFix{};

static float frameTimeScale = 0.0f;
static float savedHairDeltaTime = 0.0f;

static void __fastcall HairSimulator_Hook(void* thisPtr, int, float delta)
{
	frameTimeScale = TARGET_FRAME_TIME / delta;
	savedHairDeltaTime = delta;

	HairSimulator.unsafe_thiscall<void>(thisPtr, delta);
}

static void OnHairDeltaTimeOverride(safetyhook::Context& ctx)
{
	float* dt = reinterpret_cast<float*>(ctx.ebx + 0x8);
	*dt *= frameTimeScale;
}

static void OnHairDeltaTimeRestore(safetyhook::Context& ctx)
{
	float* dt = reinterpret_cast<float*>(ctx.ebx + 0x8);
	*dt = savedHairDeltaTime;
}

static void OnHairGravityAttenuate(safetyhook::Context& ctx)
{
	float k = 1.0f / frameTimeScale;
	ctx.xmm2.f32[0] *= k;
	ctx.xmm2.f32[1] *= k;
	ctx.xmm2.f32[2] *= k;
}

static void OnHairWindAttenuate(safetyhook::Context& ctx)
{
	float k = 1.0f / frameTimeScale;
	ctx.xmm6.f32[0] *= k;
	ctx.xmm6.f32[1] *= k;
	ctx.xmm6.f32[2] *= k;
}

static void OnHairShapeMatchDecompound(safetyhook::Context& ctx)
{
	float f30 = ctx.xmm3.f32[0];
	float scale = frameTimeScale;
	float remain = 1.0f - f30;
	if (remain < 0.0f) remain = 0.0f;
	float fNew = 1.0f - std::pow(remain, 1.0f / scale);
	ctx.xmm3.f32[0] = fNew;
}

static void OnHairInnerPhaseFix(safetyhook::Context& ctx)
{
	float* innerDt = reinterpret_cast<float*>(ctx.ebx + 0x8);
	*innerDt = savedHairDeltaTime;
}

void ApplyFixHighFPSHairPhysics()
{
	if (!FixHighFPSHairPhysics) return;

	DWORD addr_DampingScaler = GetAddress(Addr::HairSimulator_DampingScaler);
	DWORD addr_DeltaTimeOverride = GetAddress(Addr::HairSimulator_DeltaTimeOverride);

	HairSimulator = HookHelper::CreateHook((void*)GetAddress(Addr::HairSimulator), &HairSimulator_Hook);
	hairDeltaTimeOverride = safetyhook::create_mid(addr_DeltaTimeOverride, OnHairDeltaTimeOverride);
	hairDeltaTimeRestore = safetyhook::create_mid(addr_DeltaTimeOverride + 0x8, OnHairDeltaTimeRestore);
	hairGravityAttenuate = safetyhook::create_mid(addr_DampingScaler, OnHairGravityAttenuate);
	hairWindAttenuate = safetyhook::create_mid(addr_DampingScaler + 0x6E, OnHairWindAttenuate);
	hairShapeMatchDecompound = safetyhook::create_mid(addr_DampingScaler + 0x179, OnHairShapeMatchDecompound);
	hairInnerPhaseFix = safetyhook::create_mid(addr_DampingScaler + 0x4B2, OnHairInnerPhaseFix);
}

// ---- Cloth ----

safetyhook::InlineHook ClothSimulator;

constexpr int CLOTH_MAX_INSTANCES = 32;
constexpr int CLOTH_MAX_PARTICLES = 80;
constexpr int CLOTH_MAX_FLOATS = CLOTH_MAX_PARTICLES * 3;

struct ClothInstanceState
{
	uint32_t lastUsed = 0;
	int numFloats = 0;
	float accumulator = 0.0f;
	bool primed = false;
	float trueP1[CLOTH_MAX_FLOATS];
	float relPrev[CLOTH_MAX_FLOATS];
	float relCurr[CLOTH_MAX_FLOATS];
	float applied[CLOTH_MAX_FLOATS];
};

static uintptr_t clothKeys[CLOTH_MAX_INSTANCES] = {};
static ClothInstanceState clothes[CLOTH_MAX_INSTANCES];
static uint32_t clothCounter = 0;

static uint32_t __fastcall ClothSimulator_Hook(void* thisPtr, int, float delta)
{
	uint8_t* cloth = (uint8_t*)thisPtr;
	int numParticles = *(int*)(cloth + 0xB8);
	uint8_t* particles = *(uint8_t**)(cloth + 0xC4);

	if (!particles || numParticles <= 0)
		return ClothSimulator.unsafe_thiscall<uint32_t>(thisPtr, delta);

	bool bypass = numParticles > CLOTH_MAX_PARTICLES || delta > (1.0f / 59.0f);

	// Dollmaker strings
	if (!bypass && *(int*)(cloth + 0xC0) == 0)
	{
		int constraints = *(int*)(cloth + 0xBC);
		bypass = (numParticles == 15 && constraints == 27) || (numParticles == 20 && constraints == 37);
	}

	uintptr_t key = (uintptr_t)thisPtr;

	if (bypass)
	{
		for (int i = 0; i < CLOTH_MAX_INSTANCES; i++)
		{
			if (clothKeys[i] != key)
				continue;

			ClothInstanceState& state = clothes[i];

			if (state.primed && state.numFloats == numParticles * 3)
			{
				for (int p = 0; p < numParticles; p++)
				{
					float* p1 = (float*)(particles + p * 0x70 + 0x10);

					for (int j = 0; j < 3; j++)
					{
						int k = p * 3 + j;
						if (p1[j] == state.applied[k])
						{
							p1[j] = state.trueP1[k];
						}
					}
				}
			}

			state.primed = false;
			break;
		}

		return ClothSimulator.unsafe_thiscall<uint32_t>(thisPtr, delta);
	}

	int slot = -1;
	for (int i = 0; i < CLOTH_MAX_INSTANCES; i++)
	{
		if (clothKeys[i] == key)
		{
			slot = i;
			break;
		}
	}

	if (slot == -1)
	{
		slot = 0;
		for (int i = 1; i < CLOTH_MAX_INSTANCES; i++)
		{
			if (clothes[i].lastUsed < clothes[slot].lastUsed)
			{
				slot = i;
			}
		}

		clothKeys[slot] = key;
		clothes[slot].numFloats = 0;
	}

	ClothInstanceState& state = clothes[slot];
	state.lastUsed = ++clothCounter;

	int numFloats = numParticles * 3;
	if (state.numFloats != numFloats)
	{
		state.numFloats = numFloats;
		state.primed = false;
	}

	// Restore the simulation-true p1.
	// If the engine rewrote p1 since we set it (teleport reset, instance respawn), adopt its value and resync instead
	if (state.primed)
	{
		for (int i = 0; i < numParticles; i++)
		{
			float* p1 = (float*)(particles + i * 0x70 + 0x10);
			float* p3 = (float*)(particles + i * 0x70 + 0x30);

			for (int j = 0; j < 3; j++)
			{
				int k = i * 3 + j;
				if (p1[j] == state.applied[k])
				{
					p1[j] = state.trueP1[k];
				}
				else
				{
					state.trueP1[k] = p1[j];
					state.relCurr[k] = p1[j] - p3[j];
					state.relPrev[k] = state.relCurr[k];
				}
			}
		}
	}

	state.accumulator += delta;

	if (!state.primed)
		state.accumulator = TARGET_FRAME_TIME; // first sight of this instance: tick now

	uint32_t result = 0;
	if (state.accumulator >= TARGET_FRAME_TIME)
	{
		memcpy(state.relPrev, state.relCurr, numFloats * sizeof(float));
		result = ClothSimulator.unsafe_thiscall<uint32_t>(thisPtr, TARGET_FRAME_TIME);
		state.accumulator -= TARGET_FRAME_TIME;

		// Capture the post-tick anchor-relative state
		for (int i = 0; i < numParticles; i++)
		{
			float* p1 = (float*)(particles + i * 0x70 + 0x10);
			float* p3 = (float*)(particles + i * 0x70 + 0x30);

			for (int j = 0; j < 3; j++)
			{
				state.relCurr[i * 3 + j] = p1[j] - p3[j];
			}
		}

		if (!state.primed)
		{
			memcpy(state.relPrev, state.relCurr, numFloats * sizeof(float));
			state.primed = true;
		}
	}

	// Write the render state: live anchor + interpolated relative motion
	float alpha = state.accumulator / TARGET_FRAME_TIME;
	const float* matrix = (const float*)cloth;

	for (int i = 0; i < numParticles; i++)
	{
		uint8_t* particle = particles + i * 0x70;
		float* p1 = (float*)(particle + 0x10);
		const float* localAnchor = (const float*)(particle + 0x20);

		for (int j = 0; j < 3; j++)
		{
			// anchor = p2.x * M0 + p2.y * M1 + p2.z * M2 + M3
			float anchor = localAnchor[0] * matrix[j] + localAnchor[1] * matrix[4 + j] + localAnchor[2] * matrix[8 + j] + matrix[12 + j];

			int k = i * 3 + j;
			state.trueP1[k] = p1[j];
			p1[j] = anchor + state.relPrev[k] + (state.relCurr[k] - state.relPrev[k]) * alpha;
			state.applied[k] = p1[j];
		}
	}

	return result;
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
