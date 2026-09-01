		struct AnimationAssetLookup
		{
			UAnimSet* animSet = nullptr;
			UAnimSequence* sequence = nullptr;
		};

		AnimationAssetLookup FindAnimationAsset(
			const USkeletalMeshComponent* component, const char* sequenceName)
		{
			AnimationAssetLookup result{};
			if (!component || !sequenceName || !*sequenceName)
				return result;

			const int32_t animSetCount = std::min<int32_t>(
				component->AnimSets.size(), 256);
			for (int32_t setIndex = 0; setIndex < animSetCount; ++setIndex)
			{
				UAnimSet* animSet = component->AnimSets.at(setIndex);
				if (!animSet)
					continue;
				const int32_t sequenceCount = std::min<int32_t>(
					animSet->Sequences.size(), 8192);
				for (int32_t sequenceIndex = 0;
					sequenceIndex < sequenceCount; ++sequenceIndex)
				{
					UAnimSequence* sequence =
						animSet->Sequences.at(sequenceIndex);
					if (!sequence || !sequence->SequenceName.IsValid())
						continue;
					if (_stricmp(sequence->SequenceName.ToString().c_str(),
						sequenceName) == 0)
					{
						result.animSet = animSet;
						result.sequence = sequence;
						return result;
					}
				}
			}
			return result;
		}

		bool ResolveExistingAnimationName(
			const USkeletalMeshComponent* component,
			const char* requestedName, FName& resolvedName)
		{
			const AnimationAssetLookup lookup =
				FindAnimationAsset(component, requestedName);
			if (lookup.sequence
				&& lookup.sequence->SequenceName.IsValid())
			{
				memcpy_s(&resolvedName, sizeof(resolvedName),
					&lookup.sequence->SequenceName,
					sizeof(lookup.sequence->SequenceName));
				return true;
			}
			const FName fallback(requestedName ? requestedName : "");
			if (!fallback.IsValid())
				return false;
			memcpy_s(&resolvedName, sizeof(resolvedName),
				&fallback, sizeof(fallback));
			return true;
		}

		std::string DescribeAnimationAssets(
			const USkeletalMeshComponent* component, const char* sequenceName)
		{
			if (!component)
				return "component=<null>";
			const AnimationAssetLookup lookup =
				FindAnimationAsset(component, sequenceName);
			std::ostringstream stream;
			stream << "component=" << ObjectName(component)
				<< ", mesh=" << ObjectName(component->SkeletalMesh)
				<< ", tree=" << ObjectName(component->AnimTreeTemplate)
				<< ", root=" << ObjectName(component->Animations)
				<< ", sets=" << component->AnimSets.size()
				<< ", nodes=" << component->AnimTickArray.size()
				<< ", matchSet=" << ObjectName(lookup.animSet)
				<< ", matchSeq=" << ObjectName(lookup.sequence);
			return stream.str();
		}

		std::uint64_t PoseHash(const USkeletalMeshComponent* component)
		{
			if (!component)
				return 0;
			std::uint64_t hash = 1469598103934665603ull;
			const int32_t atomCount = std::min<int32_t>(
				component->LocalAtoms.size(), 4096);
			for (int32_t index = 0; index < atomCount; ++index)
			{
				const auto* bytes = reinterpret_cast<const std::uint8_t*>(
					&component->LocalAtoms.at(index));
				for (std::size_t byte = 0; byte < sizeof(FBoneAtom); ++byte)
				{
					hash ^= bytes[byte];
					hash *= 1099511628211ull;
				}
			}
			return hash;
		}

		template <typename Parameters>
		bool InvokeNativeFunction(UObject* object, UFunction* function,
			Parameters& parameters)
		{
			if (!object || !function
				|| (function->FunctionFlags & 0x400) == 0
				|| function->iNative == 0)
			{
				return false;
			}
			// Generated SDK wrappers temporarily clear FUNC_Native/iNative.
			// That turns intrinsic animation functions into silent no-ops in
			// this build. ProcessEvent must see their original native metadata.
			object->ProcessEvent(function, &parameters, nullptr);
			return true;
		}

		bool NativeCheckSequenceEvent(USequenceEvent* event,
			AActor* originator, AActor* instigator, bool testOnly,
			bool pushTop, TArray<int32_t>& activateIndices)
		{
			static UFunction* function = nullptr;
			if (!function)
			{
				function = UFunction::FindFunction(
					"Function Engine.SequenceEvent.CheckActivate");
			}
			USequenceEvent_execCheckActivate_Params parameters{};
			parameters.InOriginator = originator;
			parameters.InInstigator = instigator;
			parameters.bTest = testOnly;
			parameters.bPushTop = pushTop;
			memcpy_s(&parameters.ActivateIndices,
				sizeof(parameters.ActivateIndices),
				&activateIndices, sizeof(activateIndices));
			if (!InvokeNativeFunction(event, function, parameters))
				return false;
			memcpy_s(&activateIndices, sizeof(activateIndices),
				&parameters.ActivateIndices,
				sizeof(parameters.ActivateIndices));
			return parameters.ReturnValue;
		}

		bool NativeActivateOutputLink(USequenceOp* op,
			int32_t outputIndex)
		{
			static UFunction* function = nullptr;
			if (!function)
			{
				function = UFunction::FindFunction(
					"Function Engine.SequenceOp.ActivateOutputLink");
			}
			USequenceOp_execActivateOutputLink_Params parameters{};
			parameters.OutputIdx = outputIndex;
			if (!InvokeNativeFunction(op, function, parameters))
				return false;
			return parameters.ReturnValue;
		}

		bool NativeLoadChapter(
			ACheckPointManager* manager,
			EChapterNameList chapter)
		{
			static UFunction* function = nullptr;
			if (!function)
			{
				function = UFunction::FindFunction(
					"Function Engine.CheckPointManager.LoadChapter");
			}
			ACheckPointManager_execLoadChapter_Params parameters{};
			parameters.beLoadedCharpter =
				static_cast<std::uint8_t>(chapter);
			return InvokeNativeFunction(
				manager, function, parameters);
		}

		bool NativeGetLastLoadedChapter(
			ACheckPointManager* manager,
			std::uint8_t& chapter)
		{
			static UFunction* function = nullptr;
			if (!function)
			{
				function = UFunction::FindFunction(
					"Function Engine.CheckPointManager."
					"GetLastLoadedChapter");
			}
			ACheckPointManager_execGetLastLoadedChapter_Params
				parameters{};
			if (!InvokeNativeFunction(
					manager, function, parameters))
			{
				return false;
			}
			chapter = parameters.ReturnValue;
			return true;
		}

		bool NativeSetActorLocationNoCheck(
			AActor* actor, const FVector& location)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.Actor.SetLocationNoCheck");
			AActor_execSetLocationNoCheck_Params parameters{};
			memcpy_s(&parameters.NewLocation,
				sizeof(parameters.NewLocation),
				&location, sizeof(location));
			if (!InvokeNativeFunction(actor, function, parameters))
				return false;
			return parameters.ReturnValue;
		}

		bool NativeSetActorRotation(
			AActor* actor, const FRotator& rotation)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.Actor.SetRotation");
			AActor_execSetRotation_Params parameters{};
			memcpy_s(&parameters.NewRotation,
				sizeof(parameters.NewRotation),
				&rotation, sizeof(rotation));
			if (!InvokeNativeFunction(actor, function, parameters))
				return false;
			return parameters.ReturnValue;
		}

		bool NativeSetAnim(UAnimNodeSequence* sequence,
			const FName& sequenceName)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.AnimNodeSequence.SetAnim");
			UAnimNodeSequence_execSetAnim_Params parameters{};
			memcpy_s(&parameters.Sequence, sizeof(parameters.Sequence),
				&sequenceName, sizeof(sequenceName));
			return InvokeNativeFunction(sequence, function, parameters);
		}

		bool NativePlayAnim(UAnimNodeSequence* sequence, bool looping,
			float rate, float position)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.AnimNodeSequence.PlayAnim");
			UAnimNodeSequence_execPlayAnim_Params parameters{};
			parameters.bLoop = looping;
			parameters.InRate = rate;
			parameters.StartTime = position;
			return InvokeNativeFunction(sequence, function, parameters);
		}

		bool NativeSetPosition(UAnimNodeSequence* sequence, float position)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.AnimNodeSequence.SetPosition");
			UAnimNodeSequence_execSetPosition_Params parameters{};
			parameters.NewTime = position;
			parameters.bFireNotifies = false;
			return InvokeNativeFunction(sequence, function, parameters);
		}

		bool NativeSetActiveChild(UAliceGameAnimNode_BlendList* slot,
			int32_t childIndex, float blendTime)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function AliceGame.AliceGameAnimNode_BlendList.SetActiveChild");
			UAliceGameAnimNode_BlendList_execSetActiveChild_Params parameters{};
			parameters.ChildIndex = childIndex;
			parameters.BlendTime = blendTime;
			return InvokeNativeFunction(slot, function, parameters);
		}

		bool NativePlayConfigAnim(AAliceGamePawn* pawn,
			int32_t blendNodeIndex, int32_t configType,
			FAnimationParaConfig& config)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function AliceGame.AliceGamePawn.PlayConfigAnim");
			AAliceGamePawn_execPlayConfigAnim_Params parameters{};
			memcpy_s(&parameters.AnimConfig, sizeof(parameters.AnimConfig),
				&config, sizeof(config));
			parameters.BlendNodeIndex = blendNodeIndex;
			parameters.configtype = configType;
			const bool invoked = InvokeNativeFunction(
				pawn, function, parameters);
			if (invoked)
			{
				memcpy_s(&config, sizeof(config), &parameters.AnimConfig,
					sizeof(parameters.AnimConfig));
			}
			return invoked;
		}

		bool NativeStopConfigAnim(AAliceGamePawn* pawn, float blendOutTime,
			FAnimationParaConfig& config)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function AliceGame.AliceGamePawn.StopConfigAnim");
			AAliceGamePawn_execStopConfigAnim_Params parameters{};
			memcpy_s(&parameters.AnimConfig, sizeof(parameters.AnimConfig),
				&config, sizeof(config));
			parameters.BlendOutTime = blendOutTime;
			parameters.bForceStop = false;
			parameters.bForceAnimNotify = false;
			parameters.bForceAnimEnd = false;
			const bool invoked = InvokeNativeFunction(
				pawn, function, parameters);
			if (invoked)
			{
				memcpy_s(&config, sizeof(config), &parameters.AnimConfig,
					sizeof(parameters.AnimConfig));
			}
			return invoked;
		}

		bool NativeForceSkelUpdate(USkeletalMeshComponent* component)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.SkeletalMeshComponent.ForceSkelUpdate");
			USkeletalMeshComponent_execForceSkelUpdate_Params parameters{};
			return InvokeNativeFunction(component, function, parameters);
		}

		bool NativeSetParentAnimComponent(
			USkeletalMeshComponent* component,
			USkeletalMeshComponent* parent)
		{
			static UFunction* function = nullptr;
			if (!function)
			{
				function = UFunction::FindFunction(
					"Function Engine.SkeletalMeshComponent."
					"SetParentAnimComponent");
			}
			USkeletalMeshComponent_execSetParentAnimComponent_Params
				parameters{};
			parameters.NewParentAnimComp = parent;
			return InvokeNativeFunction(component, function, parameters);
		}

		bool NativeUpdateParentBoneMap(
			USkeletalMeshComponent* component)
		{
			static UFunction* function = nullptr;
			if (!function)
			{
				function = UFunction::FindFunction(
					"Function Engine.SkeletalMeshComponent."
					"UpdateParentBoneMap");
			}
			USkeletalMeshComponent_execUpdateParentBoneMap_Params
				parameters{};
			return InvokeNativeFunction(component, function, parameters);
		}

		bool NativeGetBoneName(USkeletalMeshComponent* component,
			int32_t boneIndex, FName& boneName)
		{
			static UFunction* function = nullptr;
			if (!function)
			{
				function = UFunction::FindFunction(
					"Function Engine.SkeletalMeshComponent."
					"GetBoneName");
			}
			USkeletalMeshComponent_execGetBoneName_Params parameters{};
			parameters.BoneIndex = boneIndex;
			if (!InvokeNativeFunction(component, function, parameters))
				return false;
			memcpy_s(&boneName, sizeof(boneName),
				&parameters.ReturnValue,
				sizeof(parameters.ReturnValue));
			return boneName.IsValid();
		}

		bool NativeGetBoneMatrix(USkeletalMeshComponent* component,
			int32_t boneIndex, FMatrix& matrix)
		{
			static UFunction* function = nullptr;
			if (!function)
			{
				function = UFunction::FindFunction(
					"Function Engine.SkeletalMeshComponent."
					"GetBoneMatrix");
			}
			USkeletalMeshComponent_execGetBoneMatrix_Params parameters{};
			parameters.BoneIndex = boneIndex;
			if (!InvokeNativeFunction(component, function, parameters))
				return false;
			matrix = parameters.ReturnValue;
			return true;
		}

		bool NativeInverseTransformVector(const FMatrix& matrix,
			const FVector& vector, FVector& transformed)
		{
			static UFunction* function = nullptr;
			if (!function)
			{
				function = UFunction::FindFunction(
					"Function Core.Object.InverseTransformVector");
			}
			UObject_execInverseTransformVector_Params parameters{};
			parameters.TM = matrix;
			parameters.A = vector;
			if (!InvokeNativeFunction(
				UObject::StaticClass(), function, parameters))
			{
				return false;
			}
			transformed = parameters.ReturnValue;
			return true;
		}

		bool NativeMatrixGetRotator(const FMatrix& matrix,
			FRotator& rotation)
		{
			static UFunction* function = nullptr;
			if (!function)
			{
				function = UFunction::FindFunction(
					"Function Core.Object.MatrixGetRotator");
			}
			UObject_execMatrixGetRotator_Params parameters{};
			parameters.TM = matrix;
			if (!InvokeNativeFunction(
				UObject::StaticClass(), function, parameters))
			{
				return false;
			}
			rotation = parameters.ReturnValue;
			return true;
		}

		bool NativeAttachComponentToBone(
			USkeletalMeshComponent* parent,
			UActorComponent* component, const FName& boneName,
			const FVector& relativeLocation,
			const FRotator& relativeRotation,
			const FVector& relativeScale)
		{
			static UFunction* function = nullptr;
			if (!function)
			{
				function = UFunction::FindFunction(
					"Function Engine.SkeletalMeshComponent."
					"AttachComponent");
			}
			USkeletalMeshComponent_execAttachComponent_Params parameters{};
			parameters.Component = component;
			memcpy_s(&parameters.BoneName,
				sizeof(parameters.BoneName),
				&boneName, sizeof(boneName));
			parameters.RelativeLocation = relativeLocation;
			parameters.RelativeRotation = relativeRotation;
			parameters.RelativeScale = relativeScale;
			return InvokeNativeFunction(parent, function, parameters);
		}

		FMatrix MultiplyMatrices(
			const FMatrix& left, const FMatrix& right)
		{
			const float a[4][4]{
				{ left.XPlane.X, left.XPlane.Y,
					left.XPlane.Z, left.XPlane.W },
				{ left.YPlane.X, left.YPlane.Y,
					left.YPlane.Z, left.YPlane.W },
				{ left.ZPlane.X, left.ZPlane.Y,
					left.ZPlane.Z, left.ZPlane.W },
				{ left.WPlane.X, left.WPlane.Y,
					left.WPlane.Z, left.WPlane.W }
			};
			const float b[4][4]{
				{ right.XPlane.X, right.XPlane.Y,
					right.XPlane.Z, right.XPlane.W },
				{ right.YPlane.X, right.YPlane.Y,
					right.YPlane.Z, right.YPlane.W },
				{ right.ZPlane.X, right.ZPlane.Y,
					right.ZPlane.Z, right.ZPlane.W },
				{ right.WPlane.X, right.WPlane.Y,
					right.WPlane.Z, right.WPlane.W }
			};
			float result[4][4]{};
			for (int row = 0; row < 4; ++row)
			{
				for (int column = 0; column < 4; ++column)
				{
					for (int index = 0; index < 4; ++index)
						result[row][column] +=
							a[row][index] * b[index][column];
				}
			}

			FMatrix matrix{};
			matrix.XPlane.X = result[0][0];
			matrix.XPlane.Y = result[0][1];
			matrix.XPlane.Z = result[0][2];
			matrix.XPlane.W = result[0][3];
			matrix.YPlane.X = result[1][0];
			matrix.YPlane.Y = result[1][1];
			matrix.YPlane.Z = result[1][2];
			matrix.YPlane.W = result[1][3];
			matrix.ZPlane.X = result[2][0];
			matrix.ZPlane.Y = result[2][1];
			matrix.ZPlane.Z = result[2][2];
			matrix.ZPlane.W = result[2][3];
			matrix.WPlane.X = result[3][0];
			matrix.WPlane.Y = result[3][1];
			matrix.WPlane.Z = result[3][2];
			matrix.WPlane.W = result[3][3];
			return matrix;
		}

		FMatrix RotationMatrixDegrees(
			float xDegrees, float yDegrees, float zDegrees)
		{
			constexpr float DegreesToRadians =
				3.14159265358979323846f / 180.0f;
			const float x = xDegrees * DegreesToRadians;
			const float y = yDegrees * DegreesToRadians;
			const float z = zDegrees * DegreesToRadians;
			const float sx = std::sin(x);
			const float cx = std::cos(x);
			const float sy = std::sin(y);
			const float cy = std::cos(y);
			const float sz = std::sin(z);
			const float cz = std::cos(z);

			// TransformPoint uses UE3's row-vector convention. These matrices
			// are therefore the transpose of the common column-vector form.
			FMatrix rotateX{};
			rotateX.XPlane.X = 1.0f;
			rotateX.YPlane.Y = cx;
			rotateX.YPlane.Z = sx;
			rotateX.ZPlane.Y = -sx;
			rotateX.ZPlane.Z = cx;
			rotateX.WPlane.W = 1.0f;

			FMatrix rotateY{};
			rotateY.XPlane.X = cy;
			rotateY.XPlane.Z = -sy;
			rotateY.YPlane.Y = 1.0f;
			rotateY.ZPlane.X = sy;
			rotateY.ZPlane.Z = cy;
			rotateY.WPlane.W = 1.0f;

			FMatrix rotateZ{};
			rotateZ.XPlane.X = cz;
			rotateZ.XPlane.Y = sz;
			rotateZ.YPlane.X = -sz;
			rotateZ.YPlane.Y = cz;
			rotateZ.ZPlane.Z = 1.0f;
			rotateZ.WPlane.W = 1.0f;

			return MultiplyMatrices(
				MultiplyMatrices(rotateX, rotateY), rotateZ);
		}

		bool InvertAffineMatrix(
			const FMatrix& matrix, FMatrix& inverse)
		{
			const float a00 = matrix.XPlane.X;
			const float a01 = matrix.XPlane.Y;
			const float a02 = matrix.XPlane.Z;
			const float a10 = matrix.YPlane.X;
			const float a11 = matrix.YPlane.Y;
			const float a12 = matrix.YPlane.Z;
			const float a20 = matrix.ZPlane.X;
			const float a21 = matrix.ZPlane.Y;
			const float a22 = matrix.ZPlane.Z;
			const float determinant =
				a00 * (a11 * a22 - a12 * a21)
				- a01 * (a10 * a22 - a12 * a20)
				+ a02 * (a10 * a21 - a11 * a20);
			if (!std::isfinite(determinant)
				|| std::abs(determinant) < 0.000001f)
			{
				return false;
			}

			const float reciprocal = 1.0f / determinant;
			const float i00 = (a11 * a22 - a12 * a21)
				* reciprocal;
			const float i01 = (a02 * a21 - a01 * a22)
				* reciprocal;
			const float i02 = (a01 * a12 - a02 * a11)
				* reciprocal;
			const float i10 = (a12 * a20 - a10 * a22)
				* reciprocal;
			const float i11 = (a00 * a22 - a02 * a20)
				* reciprocal;
			const float i12 = (a02 * a10 - a00 * a12)
				* reciprocal;
			const float i20 = (a10 * a21 - a11 * a20)
				* reciprocal;
			const float i21 = (a01 * a20 - a00 * a21)
				* reciprocal;
			const float i22 = (a00 * a11 - a01 * a10)
				* reciprocal;

			inverse = {};
			inverse.XPlane.X = i00;
			inverse.XPlane.Y = i01;
			inverse.XPlane.Z = i02;
			inverse.YPlane.X = i10;
			inverse.YPlane.Y = i11;
			inverse.YPlane.Z = i12;
			inverse.ZPlane.X = i20;
			inverse.ZPlane.Y = i21;
			inverse.ZPlane.Z = i22;
			const FVector translation(
				matrix.WPlane.X,
				matrix.WPlane.Y,
				matrix.WPlane.Z);
			inverse.WPlane.X = -(translation.X * i00
				+ translation.Y * i10
				+ translation.Z * i20);
			inverse.WPlane.Y = -(translation.X * i01
				+ translation.Y * i11
				+ translation.Z * i21);
			inverse.WPlane.Z = -(translation.X * i02
				+ translation.Y * i12
				+ translation.Z * i22);
			inverse.WPlane.W = 1.0f;
			return true;
		}

		bool NativeAttachComponentToSocket(
			USkeletalMeshComponent* parent,
			UActorComponent* component, const FName& socketName)
		{
			static UFunction* function = nullptr;
			if (!function)
			{
				function = UFunction::FindFunction(
					"Function Engine.SkeletalMeshComponent."
					"AttachComponentToSocket");
			}
			USkeletalMeshComponent_execAttachComponentToSocket_Params
				parameters{};
			parameters.Component = component;
			memcpy_s(&parameters.SocketName,
				sizeof(parameters.SocketName),
				&socketName, sizeof(socketName));
			return InvokeNativeFunction(parent, function, parameters);
		}

		bool NativeDetachComponent(USkeletalMeshComponent* parent,
			UActorComponent* component)
		{
			static UFunction* function = nullptr;
			if (!function)
			{
				function = UFunction::FindFunction(
					"Function Engine.SkeletalMeshComponent."
					"DetachComponent");
			}
			USkeletalMeshComponent_execDetachComponent_Params parameters{};
			parameters.Component = component;
			return InvokeNativeFunction(parent, function, parameters);
		}

		bool NativeAttachComponent(AActor* actor, UActorComponent* component)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.Actor.AttachComponent");
			AActor_execAttachComponent_Params parameters{};
			parameters.NewComponent = component;
			return InvokeNativeFunction(actor, function, parameters);
		}

		bool NativeDetachActorComponent(
			AActor* actor, UActorComponent* component)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.Actor.DetachComponent");
			AActor_execDetachComponent_Params parameters{};
			parameters.ExComponent = component;
			return InvokeNativeFunction(actor, function, parameters);
		}

		bool NativeSetOwner(AActor* actor, AActor* owner)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.Actor.SetOwner");
			AActor_execSetOwner_Params parameters{};
			parameters.NewOwner = owner;
			return InvokeNativeFunction(actor, function, parameters);
		}

		AActor* NativeSpawn(AActor* context, UClass* spawnClass,
			AActor* owner, const FName& tag, const FVector& location,
			const FRotator& rotation, AActor* actorTemplate,
			bool noCollisionFail)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.Actor.Spawn");
			AActor_execSpawn_Params parameters{};
			parameters.SpawnClass = spawnClass;
			parameters.SpawnOwner = owner;
			memcpy_s(&parameters.SpawnTag, sizeof(parameters.SpawnTag),
				&tag, sizeof(tag));
			memcpy_s(&parameters.SpawnLocation,
				sizeof(parameters.SpawnLocation),
				&location, sizeof(location));
			memcpy_s(&parameters.SpawnRotation,
				sizeof(parameters.SpawnRotation),
				&rotation, sizeof(rotation));
			parameters.ActorTemplate = actorTemplate;
			parameters.bNoCollisionFail = noCollisionFail;
			if (!InvokeNativeFunction(context, function, parameters))
				return nullptr;
			return parameters.ReturnValue;
		}

		bool NativeDestroyActor(AActor* actor)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.Actor.Destroy");
			AActor_execDestroy_Params parameters{};
			if (!InvokeNativeFunction(actor, function, parameters))
				return false;
			return parameters.ReturnValue;
		}

		bool NativeReattachComponent(AActor* actor,
			UActorComponent* component)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.Actor.ReattachComponent");
			AActor_execReattachComponent_Params parameters{};
			parameters.ComponentToReattach = component;
			return InvokeNativeFunction(actor, function, parameters);
		}

		bool NativeForceUpdateComponents(AActor* actor,
			bool collisionUpdate, bool transformOnly)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.Actor.ForceUpdateComponents");
			AActor_execForceUpdateComponents_Params parameters{};
			parameters.bCollisionUpdate = collisionUpdate;
			parameters.bTransformOnly = transformOnly;
			return InvokeNativeFunction(actor, function, parameters);
		}

		bool NativeForceComponentUpdate(UActorComponent* component,
			bool transformOnly)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.ActorComponent.ForceUpdate");
			UActorComponent_execForceUpdate_Params parameters{};
			parameters.bTransformOnly = transformOnly;
			return InvokeNativeFunction(component, function, parameters);
		}

		bool NativeSetActorHidden(AActor* actor, bool hidden)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.Actor.SetHidden");
			AActor_execSetHidden_Params parameters{};
			parameters.bNewHidden = hidden;
			return InvokeNativeFunction(actor, function, parameters);
		}

		bool NativeSetComponentHidden(UPrimitiveComponent* component,
			bool hidden)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.PrimitiveComponent.SetHidden");
			UPrimitiveComponent_execSetHidden_Params parameters{};
			parameters.NewHidden = hidden;
			return InvokeNativeFunction(component, function, parameters);
		}

		bool NativeResetHair(UHairComponent* component)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.HairComponent.Reset");
			UHairComponent_execReset_Params parameters{};
			return InvokeNativeFunction(component, function, parameters);
		}

		bool NativeDeleteClothSimulator(UClothComponent* component)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.ClothComponent.DeleteSimulator");
			UClothComponent_execDeleteSimulator_Params parameters{};
			return InvokeNativeFunction(component, function, parameters);
		}

		bool NativeSetSkeletalMesh(USkeletalMeshComponent* component,
			USkeletalMesh* mesh, bool keepSpaceBases)
		{
			static UFunction* function = nullptr;
			if (!function)
				function = UFunction::FindFunction(
					"Function Engine.SkeletalMeshComponent.SetSkeletalMesh");
			USkeletalMeshComponent_execSetSkeletalMesh_Params parameters{};
			parameters.NewMesh = mesh;
			parameters.bKeepSpaceBases = keepSpaceBases;
			parameters.InbAlwaysUseInstanceWeights = false;
			return InvokeNativeFunction(component, function, parameters);
		}

		bool NativeAttachWeaponToSocket(AAliceGameWeaponBase* weapon,
			const FName& socketName)
		{
			static UFunction* function = nullptr;
			if (!function)
			{
				function = UFunction::FindFunction(
					"Function AliceGame.AliceGameWeaponBase."
					"AttachWeaponToInstigatorMeshSocket");
			}
			AAliceGameWeaponBase_execAttachWeaponToInstigatorMeshSocket_Params
				parameters{};
			memcpy_s(&parameters.SocketName, sizeof(parameters.SocketName),
				&socketName, sizeof(socketName));
			return InvokeNativeFunction(weapon, function, parameters);
		}

		bool NativePlayWeaponSlotAnim(AAliceGameWeaponBase* weapon,
			const FName& sequenceName, float rate, bool looping,
			float blendInTime, float blendOutTime)
		{
			static UFunction* function = nullptr;
			if (!function)
			{
				function = UFunction::FindFunction(
					"Function AliceGame.AliceGameWeaponBase."
					"PlayWeaponSlotAnim");
			}
			AAliceGameWeaponBase_execPlayWeaponSlotAnim_Params parameters{};
			memcpy_s(&parameters.Sequence, sizeof(parameters.Sequence),
				&sequenceName, sizeof(sequenceName));
			parameters.fDesiredRate = rate;
			parameters.bLoop = looping;
			parameters.BlendInTime = blendInTime;
			parameters.BlendOutTime = blendOutTime;
			return InvokeNativeFunction(weapon, function, parameters);
		}

		bool NativeStopWeaponSlotAnim(AAliceGameWeaponBase* weapon,
			float blendOutTime)
		{
			static UFunction* function = nullptr;
			if (!function)
			{
				function = UFunction::FindFunction(
					"Function AliceGame.AliceGameWeaponBase."
					"StopWeaponSlotAnim");
			}
			AAliceGameWeaponBase_execStopWeaponSlotAnim_Params parameters{};
			parameters.BlendOutTime = blendOutTime;
			return InvokeNativeFunction(weapon, function, parameters);
		}

