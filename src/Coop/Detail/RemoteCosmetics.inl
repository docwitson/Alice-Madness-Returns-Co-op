		bool AttachRigidPresentationCosmetic(
			USkeletalMeshComponent* body,
			USkeletalMeshComponent* cosmetic,
			bool headAnchor, const char* label)
		{
			if (!body || !cosmetic || !cosmetic->SkeletalMesh)
				return false;

			FName anchorName;
			const int32_t anchorIndex = headAnchor
				? FindPresentationAnchorBone(
					body, true, anchorName)
				: FindPresentationAnchorBone(
					body, false, anchorName);
			FMatrix boneMatrix{};
			if (anchorIndex < 0
				|| !NativeGetBoneMatrix(
					body, anchorIndex, boneMatrix))
			{
				Log(std::string("COSMETICSTAGE rigid attach ")
					+ label + " failed: anchor unavailable.");
				return false;
			}

			FMatrix inverseBoneMatrix{};
			if (!InvertAffineMatrix(
				boneMatrix, inverseBoneMatrix))
			{
				Log(std::string("COSMETICSTAGE rigid attach ")
					+ label
					+ " failed: bone transform is singular.");
				return false;
			}

			// Alice's cloth and fallback scalp meshes are authored in the
			// character's root coordinate system. Preserve the body's complete
			// basis instead of applying bone-local Euler corrections.
			FMatrix desiredWorld = body->LocalToWorld;
			if (headAnchor)
			{
				// Bind the authored scalp centre to the head bone. Once this
				// world-space bind pose is converted to a bone-relative
				// transform below, UE3's native attachment updates both its
				// position and orientation with every head animation.
				FMatrix orientationOnly = desiredWorld;
				orientationOnly.WPlane.X = 0.0f;
				orientationOnly.WPlane.Y = 0.0f;
				orientationOnly.WPlane.Z = 0.0f;
				const FVector scalpFromComponent =
					TransformPoint(
						cosmetic->SkeletalMesh->Bounds.Origin,
						orientationOnly);
				desiredWorld.WPlane.X =
					boneMatrix.WPlane.X - scalpFromComponent.X;
				desiredWorld.WPlane.Y =
					boneMatrix.WPlane.Y - scalpFromComponent.Y;
				desiredWorld.WPlane.Z =
					boneMatrix.WPlane.Z - scalpFromComponent.Z;

				// The head bone origin sits below the visual crown centre.
				// Keep the constraint itself on Bip01-Head and add this
				// root-space bind offset, which will subsequently rotate and
				// translate with the head through the native attachment.
				constexpr float ScalpUpOffset = 17.0f;
				desiredWorld.WPlane.X +=
					desiredWorld.ZPlane.X * ScalpUpOffset;
				desiredWorld.WPlane.Y +=
					desiredWorld.ZPlane.Y * ScalpUpOffset;
				desiredWorld.WPlane.Z +=
					desiredWorld.ZPlane.Z * ScalpUpOffset;
			}

			// UE3 uses row-vector matrices. ComponentWorld *
			// inverse(BoneWorld) produces the attachment transform in
			// bone space. Euler subtraction cannot represent the same
			// composition and was rotating the dress into the ground.
			const FMatrix relativeMatrix = MultiplyMatrices(
				desiredWorld, inverseBoneMatrix);
			const FVector relativeLocation(
				relativeMatrix.WPlane.X,
				relativeMatrix.WPlane.Y,
				relativeMatrix.WPlane.Z);
			FRotator relativeRotation;
			if (!NativeMatrixGetRotator(
				relativeMatrix, relativeRotation))
			{
				Log(std::string("COSMETICSTAGE rigid attach ")
					+ label
					+ " failed: relative rotation unavailable.");
				return false;
			}
			const FVector relativeScale(
				FVector(relativeMatrix.XPlane.X,
					relativeMatrix.XPlane.Y,
					relativeMatrix.XPlane.Z).Size(),
				FVector(relativeMatrix.YPlane.X,
					relativeMatrix.YPlane.Y,
					relativeMatrix.YPlane.Z).Size(),
				FVector(relativeMatrix.ZPlane.X,
					relativeMatrix.ZPlane.Y,
					relativeMatrix.ZPlane.Z).Size());
			const FMatrix recomposedMatrix = MultiplyMatrices(
				relativeMatrix, boneMatrix);
			const FVector originalOrigin(
				desiredWorld.WPlane.X,
				desiredWorld.WPlane.Y,
				desiredWorld.WPlane.Z);
			const FVector recomposedOrigin(
				recomposedMatrix.WPlane.X,
				recomposedMatrix.WPlane.Y,
				recomposedMatrix.WPlane.Z);
			const float originError = FVector::Dist(
				originalOrigin, recomposedOrigin);
			NativeSetParentAnimComponent(cosmetic, nullptr);
			cosmetic->bTransformFromAnimParent = false;
			NativeDetachComponent(body, cosmetic);
			const bool attached = NativeAttachComponentToBone(
				body, cosmetic, anchorName,
				relativeLocation, relativeRotation,
				relativeScale);
			cosmetic->bAlwaysUpdateMeshObject = true;
			cosmetic->bForceMeshObjectUpdate = true;
			cosmetic->bUpdateSkelWhenNotRendered = true;
			cosmetic->bTickAnimNodesWhenNotRendered = true;
			cosmetic->Bounds = body->Bounds;

			std::ostringstream stream;
			stream << "COSMETICSTAGE rigid attach " << label
				<< ": result=" << (attached ? "yes" : "no")
				<< ", bone=" << anchorName.ToString()
				<< ", index=" << anchorIndex
				<< ", relative=("
				<< relativeLocation.X << ','
				<< relativeLocation.Y << ','
				<< relativeLocation.Z << ";rot="
				<< relativeRotation.Pitch << ','
				<< relativeRotation.Yaw << ','
				<< relativeRotation.Roll << ";scale="
				<< relativeScale.X << ','
				<< relativeScale.Y << ','
				<< relativeScale.Z
				<< ";matrixError=" << originError << ")"
				<< ", worldBasis="
				<< (headAnchor
					? "body-scalp-center-head-plus-up17"
					: "body-root-bound-to-pelvis")
				<< ", attachedParent="
				<< ObjectName(cosmetic->AttachedToSkelComponent)
				<< '.';
			Log(stream.str());
			return attached;
		}

		void ReattachProxyCosmetics(AAlicePawn* remote)
		{
			if (!remote)
				return;

			if (remote->HairComponent)
			{
				// The native strand renderer always resolves the local
				// playable Alice. Resetting this second component can therefore
				// add another head of strands to player one. Keep it dormant;
				// SpawnRemoteHairProxy creates the ordinary skeletal fallback.
				remote->HairComponent->OverrideMesh = remote->Mesh;
				remote->HairComponent->bOwnerNoSee = false;
				remote->HairComponent->bOnlyOwnerSee = false;
				remote->HairComponent->bJustAttached = false;
				remote->HairComponent->bPendingReset = false;
				remote->HairComponent->HiddenGame = true;
				NativeSetComponentHidden(
					remote->HairComponent, true);
			}

			auto reattachCloth = [remote](UClothComponent* cloth)
			{
				if (!cloth || !cloth->SkeletalMesh || !remote->Mesh)
					return;

				const char* label =
					cloth == remote->SkirtComponent
						? "skirt"
						: (cloth == remote->BowComponent
							? "bow"
							: (cloth == remote->RibbonComponent
								? "ribbon" : "ear"));
				if (cloth->Owner != remote || !cloth->bAttached)
					NativeAttachComponent(remote, cloth);
				// Alice's custom cloth simulator resolves the playable pawn
				// globally and therefore skins a second Alice against the
				// first one's bones. A presentation proxy must use ordinary
				// UE3 master-pose skinning instead. This deliberately trades
				// secondary cloth physics for a stable dress on the right body.
				NativeDeleteClothSimulator(cloth);
				cloth->bEnableClothSimulation = false;
				cloth->bClothFrozen = true;
				cloth->bPendingReset = false;
				cloth->bJustAttached = false;
				NativeReattachComponent(remote, cloth);
				cloth->bOwnerNoSee = false;
				cloth->bOnlyOwnerSee = false;
				cloth->bPauseAnims = false;
				cloth->bNoSkeletonUpdate = false;
				cloth->bUpdateSkelWhenNotRendered = true;
				cloth->bTickAnimNodesWhenNotRendered = true;
				NativeForceSkelUpdate(cloth);
				NativeForceComponentUpdate(cloth, false);
				AttachRigidPresentationCosmetic(
					remote->Mesh, cloth, false, label);
				cloth->Bounds = remote->Mesh->Bounds;
			};

			reattachCloth(remote->SkirtComponent);
			reattachCloth(remote->BowComponent);
			reattachCloth(remote->RibbonComponent);
			reattachCloth(remote->EarComponent);
			NativeForceUpdateComponents(remote, false, false);
		}

		bool CaptureNativeHairBasisCorrection(AAlicePawn* source)
		{
			g_hasRemoteHairBasisCorrection = false;
			if (!source || !source->Mesh || !source->HairComponent)
				return false;

			FMatrix hairOrientation =
				source->HairComponent->LocalToWorld;
			FMatrix bodyOrientation =
				source->Mesh->LocalToWorld;
			hairOrientation.WPlane.X = 0.0f;
			hairOrientation.WPlane.Y = 0.0f;
			hairOrientation.WPlane.Z = 0.0f;
			bodyOrientation.WPlane.X = 0.0f;
			bodyOrientation.WPlane.Y = 0.0f;
			bodyOrientation.WPlane.Z = 0.0f;
			FMatrix inverseBody;
			if (!InvertAffineMatrix(
				bodyOrientation, inverseBody))
			{
				Log("COSMETICSTAGE native hair basis unavailable: "
					"body orientation is singular.");
				return false;
			}

			g_remoteHairBasisCorrection = MultiplyMatrices(
				hairOrientation, inverseBody);
			g_remoteHairBasisCorrection.WPlane.X = 0.0f;
			g_remoteHairBasisCorrection.WPlane.Y = 0.0f;
			g_remoteHairBasisCorrection.WPlane.Z = 0.0f;
			g_hasRemoteHairBasisCorrection = true;
			Log("COSMETICSTAGE native hair basis captured: hair={"
				+ DescribeSpatialFrame(hairOrientation)
				+ "}, body={" + DescribeSpatialFrame(bodyOrientation)
				+ "}, correction={"
				+ DescribeSpatialFrame(
					g_remoteHairBasisCorrection) + "}.");
			return true;
		}

		bool PrepareIndependentHairTemplate(UHair* hairTemplate)
		{
			if (!hairTemplate)
				return false;
			if (g_remoteHairBindTemplate != hairTemplate)
			{
				g_remoteHairBindTemplate = hairTemplate;
				g_remoteHairRootCentroid =
					FVector(0.0f, 0.0f, 0.0f);
				g_remoteHairRootCount = 0;
				for (int32_t index = 0;
					index < hairTemplate->Strands.size(); ++index)
				{
					const FStrand& strand =
						hairTemplate->Strands.at(index);
					if (strand.NodeCount <= 0
						|| strand.StartNodeIndex < 0
						|| strand.StartNodeIndex
							>= hairTemplate->Nodes.size())
					{
						continue;
					}
					g_remoteHairRootCentroid +=
						hairTemplate->Nodes.at(
							strand.StartNodeIndex).Position;
					++g_remoteHairRootCount;
				}
				if (g_remoteHairRootCount > 0)
				{
					g_remoteHairRootCentroid =
						g_remoteHairRootCentroid
						/ static_cast<float>(
							g_remoteHairRootCount);
				}

				AAlicePawn* localPawn = GetLocalPawn();
				UHair* localHairTemplate =
					localPawn && localPawn->HairComponent
						? localPawn->HairComponent->Template
						: nullptr;
				const bool independentTemplate =
					hairTemplate != localHairTemplate;
				bool nodesRotated = false;
				FVector firstNodeBefore(0.0f, 0.0f, 0.0f);
				FVector firstNodeAfter(0.0f, 0.0f, 0.0f);
				if (hairTemplate->Nodes.size() > 0)
					firstNodeBefore =
						hairTemplate->Nodes.at(0).Position;
				if (independentTemplate
					&& g_hasRemoteHairBasisCorrection
					&& g_remoteHairRotatedTemplate
						!= hairTemplate)
				{
					for (int32_t index = 0;
						index < hairTemplate->Nodes.size();
						++index)
					{
						FNode& node =
							hairTemplate->Nodes.at(index);
						const FVector relative =
							node.Position
							- g_remoteHairRootCentroid;
						node.Position =
							TransformPoint(relative,
								g_remoteHairBasisCorrection)
							+ g_remoteHairRootCentroid;
					}
					g_remoteHairRotatedTemplate =
						hairTemplate;
					nodesRotated = true;
				}
				if (independentTemplate)
				{
					if (g_remoteHairTuningTemplate != hairTemplate
						|| g_remoteHairBaseNodes.size()
							!= static_cast<std::size_t>(
								hairTemplate->Nodes.size()))
					{
						g_remoteHairTuningTemplate = hairTemplate;
						g_remoteHairBaseNodes.clear();
						g_remoteHairBaseNodes.reserve(
							hairTemplate->Nodes.size());
						for (int32_t index = 0;
							index < hairTemplate->Nodes.size();
							++index)
						{
							g_remoteHairBaseNodes.push_back(
								hairTemplate->Nodes.at(
									index).Position);
						}
					}
					const bool tuneTemplate =
						g_hairRotationCandidate
							== HairRotationCandidate::TemplateNodes;
					const FMatrix manualRotation =
						RotationMatrixDegrees(
							g_config.hairRotationX,
							g_config.hairRotationY,
							g_config.hairRotationZ);
					for (int32_t index = 0;
						index < hairTemplate->Nodes.size();
						++index)
					{
						const FVector base =
							g_remoteHairBaseNodes[
								static_cast<std::size_t>(index)];
						const FVector relative =
							base - g_remoteHairRootCentroid;
						hairTemplate->Nodes.at(index).Position =
							tuneTemplate
								? (TransformPoint(
									relative, manualRotation)
									+ g_remoteHairRootCentroid)
								: base;
					}
				}
				if (hairTemplate->Nodes.size() > 0)
					firstNodeAfter =
						hairTemplate->Nodes.at(0).Position;

				const FBox& box =
					hairTemplate->FixedRelativeBoundingBox;
				std::ostringstream stream;
				stream << "COSMETICSTAGE hair bind data: roots="
					<< g_remoteHairRootCount
					<< ", centroid=("
					<< g_remoteHairRootCentroid.X << ','
					<< g_remoteHairRootCentroid.Y << ','
					<< g_remoteHairRootCentroid.Z << ')'
					<< ", fixedBox=("
					<< box.Min.X << ',' << box.Min.Y << ','
					<< box.Min.Z << ")-("
					<< box.Max.X << ',' << box.Max.Y << ','
					<< box.Max.Z << "), correction="
					<< (g_hasRemoteHairBasisCorrection
						? "native-hair-to-body" : "identity")
					<< ", manualRotationXYZ=("
					<< g_config.hairRotationX << ','
					<< g_config.hairRotationY << ','
					<< g_config.hairRotationZ << ')'
					<< ", runtimeCandidate="
					<< static_cast<int>(
						g_hairRotationCandidate)
					<< ", independentTemplate="
					<< (independentTemplate ? "yes" : "no")
					<< ", nodesRotated="
					<< (nodesRotated ? "yes" : "no")
					<< ", firstNode=("
					<< firstNodeBefore.X << ','
					<< firstNodeBefore.Y << ','
					<< firstNodeBefore.Z << ")->("
					<< firstNodeAfter.X << ','
					<< firstNodeAfter.Y << ','
					<< firstNodeAfter.Z << ").";
				Log(stream.str());
			}
			return g_remoteHairRootCount > 0;
		}

		bool AlignIndependentHairActorToScalp(
			ASkeletalMeshActor* actor, UHairComponent* hair,
			USkeletalMeshComponent* scalp)
		{
			if (!actor || !hair || !hair->Template
				|| !scalp || !scalp->SkeletalMesh)
				return false;
			if (!PrepareIndependentHairTemplate(hair->Template))
				return false;
			const FMatrix scalpFrame = scalp->LocalToWorld;
			const FMatrix manualRotation = RotationMatrixDegrees(
				g_config.hairRotationX,
				g_config.hairRotationY,
				g_config.hairRotationZ);
			const bool tuneComponentWorld =
				g_hairRotationCandidate
					== HairRotationCandidate::ComponentWorld
				|| g_hairRotationCandidate
					== HairRotationCandidate::ComponentAndActor;
			const bool tuneActor =
				g_hairRotationCandidate
					== HairRotationCandidate::ActorRotation
				|| g_hairRotationCandidate
					== HairRotationCandidate::ComponentAndActor;

			// Keep the known-good head bind as the base. The live calibrator
			// can insert the same local-space rotation at several candidate
			// layers so we can identify which one the custom renderer consumes.
			FMatrix hairWorld = tuneComponentWorld
				? MultiplyMatrices(manualRotation, scalpFrame)
				: scalpFrame;
			const FVector scalpCentre = TransformPoint(
				scalp->SkeletalMesh->Bounds.Origin,
				scalpFrame);
			FMatrix orientationOnly = hairWorld;
			orientationOnly.WPlane.X = 0.0f;
			orientationOnly.WPlane.Y = 0.0f;
			orientationOnly.WPlane.Z = 0.0f;
			const FVector rootOffset = TransformPoint(
				g_remoteHairRootCentroid, orientationOnly);
			hairWorld.WPlane.X =
				scalpCentre.X - rootOffset.X;
			hairWorld.WPlane.Y =
				scalpCentre.Y - rootOffset.Y;
			hairWorld.WPlane.Z =
				scalpCentre.Z - rootOffset.Z;
			FMatrix scalpOrientation = scalpFrame;
			scalpOrientation.WPlane.X = 0.0f;
			scalpOrientation.WPlane.Y = 0.0f;
			scalpOrientation.WPlane.Z = 0.0f;
			const FVector tuningOffset = TransformPoint(
				FVector(g_config.hairOffsetX,
					g_config.hairOffsetY,
					g_config.hairOffsetZ),
				scalpOrientation);
			hairWorld.WPlane.X += tuningOffset.X;
			hairWorld.WPlane.Y += tuningOffset.Y;
			hairWorld.WPlane.Z += tuningOffset.Z;
			const FMatrix actorFrame = tuneActor
				? MultiplyMatrices(manualRotation, scalpFrame)
				: scalpFrame;
			FRotator actorRotation;
			if (!NativeMatrixGetRotator(actorFrame, actorRotation))
				return false;
			FRotator relativeRotation(0, 0, 0);
			if (g_hairRotationCandidate
				== HairRotationCandidate::ComponentRelative
				&& !NativeMatrixGetRotator(
					manualRotation, relativeRotation))
			{
				return false;
			}

			const FVector hairLocation(
				hairWorld.WPlane.X,
				hairWorld.WPlane.Y,
				hairWorld.WPlane.Z);
			// Direct actor location plus per-frame LocalToWorld is the only
			// transform path this custom UE3 hair renderer actually consumes.
			// The selected candidate determines where the tuning rotation is
			// injected without disturbing the translation bind.
			actor->Location = hairLocation;
			actor->Rotation = actorRotation;
			// The custom hair renderer does not reliably consume scale from
			// the manually supplied LocalToWorld matrix. Mirror the proxy
			// actor scale explicitly so shrink affects strands as well.
			if (g_remotePawn)
			{
				actor->SetDrawScale(g_remotePawn->DrawScale);
				actor->DrawScale = g_remotePawn->DrawScale;
				actor->SetDrawScale3D(g_remotePawn->DrawScale3D);
				actor->DrawScale3D = g_remotePawn->DrawScale3D;
				hair->LengthScale =
					g_remoteHairBaseLengthScale
					* g_remotePawn->DrawScale;
				hair->StrandWidth =
					g_remoteHairBaseStrandWidth
					* g_remotePawn->DrawScale;
			}
			hair->Rotation = relativeRotation;
			hair->LocalToWorld = hairWorld;
			if (g_loggedConfigAnimationStages.insert(
				"independent-hair-node-transform").second)
			{
				std::ostringstream stream;
				stream << "COSMETICSTAGE independent hair node "
					"transform active: frameRotation=("
					<< actorRotation.Pitch << ','
					<< actorRotation.Yaw << ','
					<< actorRotation.Roll << ')'
					<< ", appliedBasis={"
					<< DescribeSpatialFrame(hairWorld) << "}.";
				Log(stream.str());
			}
			return true;
		}

		const char* HairRotationCandidateName(
			HairRotationCandidate candidate)
		{
			switch (candidate)
			{
			case HairRotationCandidate::ComponentWorld:
				return "ComponentWorld";
			case HairRotationCandidate::ActorRotation:
				return "ActorRotation";
			case HairRotationCandidate::ComponentAndActor:
				return "Component+Actor";
			case HairRotationCandidate::ComponentRelative:
				return "ComponentRelative";
			case HairRotationCandidate::TemplateNodes:
				return "TemplateNodes+Reset";
			default:
				return "Unknown";
			}
		}

		const wchar_t* HairRotationCandidateWideName(
			HairRotationCandidate candidate)
		{
			switch (candidate)
			{
			case HairRotationCandidate::ComponentWorld:
				return L"ComponentWorld";
			case HairRotationCandidate::ActorRotation:
				return L"ActorRotation";
			case HairRotationCandidate::ComponentAndActor:
				return L"Component+Actor";
			case HairRotationCandidate::ComponentRelative:
				return L"ComponentRelative";
			case HairRotationCandidate::TemplateNodes:
				return L"TemplateNodes+Reset";
			default:
				return L"Unknown";
			}
		}

		void ApplyRuntimeHairTemplateRotation()
		{
			UHair* hairTemplate = g_remoteHairTuningTemplate;
			if (!hairTemplate
				|| g_remoteHairBaseNodes.size()
					!= static_cast<std::size_t>(
						hairTemplate->Nodes.size()))
			{
				return;
			}

			const bool enabled =
				g_hairRotationCandidate
					== HairRotationCandidate::TemplateNodes;
			const FMatrix rotation = RotationMatrixDegrees(
				g_config.hairRotationX,
				g_config.hairRotationY,
				g_config.hairRotationZ);
			for (int32_t index = 0;
				index < hairTemplate->Nodes.size(); ++index)
			{
				const FVector base =
					g_remoteHairBaseNodes[
						static_cast<std::size_t>(index)];
				const FVector relative =
					base - g_remoteHairRootCentroid;
				hairTemplate->Nodes.at(index).Position =
					enabled
						? (TransformPoint(relative, rotation)
							+ g_remoteHairRootCentroid)
						: base;
			}

			bool reset = false;
			if (g_remoteIndependentHair)
			{
				g_remoteIndependentHair->bJustAttached = true;
				g_remoteIndependentHair->bPendingReset = true;
				reset = NativeResetHair(g_remoteIndependentHair);
				NativeForceComponentUpdate(
					g_remoteIndependentHair, false);
			}
			Log(std::string("HAIRTUNE template ")
				+ (enabled ? "rotation applied" : "restored")
				+ ", reset=" + (reset ? "yes" : "no") + '.');
		}

		bool HairTuningKeyPressed(int virtualKey,
			std::size_t keyIndex)
		{
			const bool down =
				(GetAsyncKeyState(virtualKey) & 0x8000) != 0;
			const bool pressed =
				down && !g_hairTuningKeyDown[keyIndex];
			g_hairTuningKeyDown[keyIndex] = down;
			return pressed;
		}

		void HandleHairRotationTuningInput()
		{
			if (!g_config.hairTuningEnabled)
				return;
			bool candidateChanged = false;
			bool rotationChanged = false;
			bool positionChanged = false;
			bool overlayChanged = false;
			if (HairTuningKeyPressed(VK_OEM_4, 6))
			{
				const auto next = static_cast<std::uint8_t>(
					g_hairRotationCandidate) + 1;
				g_hairRotationCandidate =
					static_cast<HairRotationCandidate>(
						next % static_cast<std::uint8_t>(
							HairRotationCandidate::Count));
				candidateChanged = true;
			}

			constexpr float StepDegrees = 5.0f;
			if (HairTuningKeyPressed('O', 0))
			{
				g_config.hairRotationX -= StepDegrees;
				rotationChanged = true;
			}
			if (HairTuningKeyPressed('P', 1))
			{
				g_config.hairRotationX += StepDegrees;
				rotationChanged = true;
			}
			if (HairTuningKeyPressed('L', 2))
			{
				g_config.hairRotationY -= StepDegrees;
				rotationChanged = true;
			}
			if (HairTuningKeyPressed(VK_OEM_1, 3))
			{
				g_config.hairRotationY += StepDegrees;
				rotationChanged = true;
			}
			if (HairTuningKeyPressed(VK_OEM_COMMA, 4))
			{
				g_config.hairRotationZ -= StepDegrees;
				rotationChanged = true;
			}
			if (HairTuningKeyPressed(VK_OEM_PERIOD, 5))
			{
				g_config.hairRotationZ += StepDegrees;
				rotationChanged = true;
			}
			constexpr float PositionStep = 0.2f;
			if (HairTuningKeyPressed('U', 7))
			{
				g_config.hairOffsetX -= PositionStep;
				positionChanged = true;
			}
			if (HairTuningKeyPressed('I', 8))
			{
				g_config.hairOffsetX += PositionStep;
				positionChanged = true;
			}
			if (HairTuningKeyPressed('J', 9))
			{
				g_config.hairOffsetY -= PositionStep;
				positionChanged = true;
			}
			if (HairTuningKeyPressed('K', 10))
			{
				g_config.hairOffsetY += PositionStep;
				positionChanged = true;
			}
			if (HairTuningKeyPressed('N', 11))
			{
				g_config.hairOffsetZ -= PositionStep;
				positionChanged = true;
			}
			if (HairTuningKeyPressed('M', 12))
			{
				g_config.hairOffsetZ += PositionStep;
				positionChanged = true;
			}
			if (HairTuningKeyPressed(VK_OEM_6, 13))
			{
				g_tuningOverlayVisible =
					!g_tuningOverlayVisible;
				overlayChanged = true;
			}

			if (!candidateChanged && !rotationChanged
				&& !positionChanged && !overlayChanged)
				return;
			if (candidateChanged || rotationChanged)
				ApplyRuntimeHairTemplateRotation();
			std::ostringstream stream;
			stream << "HAIRTUNE candidate="
				<< HairRotationCandidateName(
					g_hairRotationCandidate)
				<< ", XYZ=(" << g_config.hairRotationX << ','
				<< g_config.hairRotationY << ','
				<< g_config.hairRotationZ << "), offset=("
				<< g_config.hairOffsetX << ','
				<< g_config.hairOffsetY << ','
				<< g_config.hairOffsetZ << "), overlay="
				<< (g_tuningOverlayVisible ? "visible" : "hidden")
				<< '.';
			Log(stream.str());
		}

		void DestroyRemoteHairProxy()
		{
			ASkeletalMeshActor* proxy = g_remoteHairProxy;
			if (!proxy)
				return;
			if (!g_remoteWorld || g_remoteWorld != g_currentWorld)
			{
				g_remoteHairProxy = nullptr;
				g_remoteIndependentHair = nullptr;
				g_remoteHairProxyBaseDrawScale = 1.0f;
				g_remoteHairSkeletalBaseScale = 1.0f;
				g_remoteIndependentHairBaseScale = 1.0f;
				g_remoteHairNeedsReset = false;
				g_remoteHairBaseLengthScale = 1.0f;
				g_remoteHairBaseStrandWidth = 1.0f;
				g_remoteHairBindTemplate = nullptr;
				g_remoteHairRootCount = 0;
				g_hasRemoteHairBasisCorrection = false;
				g_remoteHairRotatedTemplate = nullptr;
				g_remoteHairTuningTemplate = nullptr;
				g_remoteHairBaseNodes.clear();
				return;
			}
			const std::string proxyName = ObjectName(proxy);
			if (g_remoteIndependentHair)
			{
				g_remoteIndependentHair->OverrideMesh = nullptr;
				g_remoteIndependentHair->bJustAttached = false;
				g_remoteIndependentHair->bPendingReset = false;
				g_remoteIndependentHair->HiddenGame = true;
				NativeSetComponentHidden(
					g_remoteIndependentHair, true);
			}
			if (proxy->SkeletalMeshComponent)
			{
				if (g_remotePawn && g_remotePawn->HairComponent
					&& g_remotePawn->HairComponent->OverrideMesh
						== proxy->SkeletalMeshComponent)
				{
					g_remotePawn->HairComponent->OverrideMesh = nullptr;
					g_remotePawn->HairComponent->HiddenGame = true;
					NativeSetComponentHidden(
						g_remotePawn->HairComponent, true);
				}
				NativeSetComponentHidden(
					proxy->SkeletalMeshComponent, true);
				if (g_remotePawn && g_remotePawn->Mesh
					&& proxy->SkeletalMeshComponent
						->ParentAnimComponent == g_remotePawn->Mesh)
				{
					NativeSetParentAnimComponent(
						proxy->SkeletalMeshComponent, nullptr);
				}
				if (g_remotePawn && g_remotePawn->Mesh
					&& proxy->SkeletalMeshComponent
						->AttachedToSkelComponent
							== g_remotePawn->Mesh)
				{
					NativeDetachComponent(
						g_remotePawn->Mesh,
						proxy->SkeletalMeshComponent);
				}
			}
			NativeSetActorHidden(proxy, true);
			proxy->bNoDelete = false;
			proxy->bStatic = false;
			const bool destroyed = NativeDestroyActor(proxy);
			Log("COSMETICSTAGE hair proxy destroy actor="
				+ proxyName + ", result="
				+ (destroyed ? std::string("destroyed.")
					: std::string("hidden-only.")));
			g_remoteHairProxy = nullptr;
			g_remoteIndependentHair = nullptr;
			g_remoteHairProxyBaseDrawScale = 1.0f;
			g_remoteHairSkeletalBaseScale = 1.0f;
			g_remoteIndependentHairBaseScale = 1.0f;
			g_remoteHairNeedsReset = false;
			g_remoteHairBaseLengthScale = 1.0f;
			g_remoteHairBaseStrandWidth = 1.0f;
			g_remoteHairBindTemplate = nullptr;
			g_remoteHairRootCount = 0;
			g_hasRemoteHairBasisCorrection = false;
		}

		void SpawnRemoteHairProxy(AAlicePawn* remote)
		{
			DestroyRemoteHairProxy();
			if (!remote || !remote->Mesh || !remote->HairComponent
				|| !remote->HairComponent->Template
				|| !remote->HairComponent->Template->SkeletalMesh)
			{
				Log("COSMETICSTAGE skeletal hair proxy unavailable.");
				return;
			}

			USkeletalMesh* hairMesh =
				remote->HairComponent->Template->SkeletalMesh;
			UHairComponent* sourceHair = remote->HairComponent;
			CaptureNativeHairBasisCorrection(GetLocalPawn());
			const bool templatePreparedBeforeSpawn =
				PrepareIndependentHairTemplate(
					sourceHair->Template);
			if (!g_loggedHairMeshCandidates)
			{
				g_loggedHairMeshCandidates = true;
				int loggedCandidates = 0;
				TArray<UObject*>* objects = UObject::GObjObjects();
				const int32_t objectCount =
					objects ? objects->size() : 0;
				for (int32_t index = 0;
					index < objectCount
						&& loggedCandidates < 64;
					++index)
				{
					UObject* object = objects->at(index);
					if (!object
						|| !object->IsA(
							USkeletalMesh::StaticClass()))
					{
						continue;
					}
					const std::string fullName =
						ObjectName(object);
					if (!ContainsCaseInsensitive(
						fullName, "hair"))
					{
						continue;
					}
					auto* candidate =
						reinterpret_cast<USkeletalMesh*>(
							object);
					std::ostringstream stream;
					stream
						<< "COSMETICSTAGE hair mesh candidate: "
						<< fullName
						<< ", lods="
						<< candidate->LODModels.ArrayNum
						<< ", materials="
						<< candidate->Materials.size()
						<< ", bones="
						<< candidate->RefSkeleton.size()
						<< ", boundsRadius="
						<< candidate->Bounds.SphereRadius
						<< '.';
					Log(stream.str());
					++loggedCandidates;
				}
				Log("COSMETICSTAGE loaded hair mesh candidates="
					+ std::to_string(loggedCandidates) + '.');
			}
			// SkeletalMeshHairActor owns a distinct HairComponent instance.
			// This mirrors the way a separate Unity GameObject would own its
			// own cloth/hair simulation component instead of sharing the
			// playable character's component state.
			UClass* proxyClass =
				ASkeletalMeshHairActor::StaticClass();
			ASkeletalMeshHairActor* hairActorDefault = nullptr;
			if (TArray<UObject*>* objects = UObject::GObjObjects())
			{
				for (int32_t index = 0;
					index < objects->size(); ++index)
				{
					UObject* object = objects->at(index);
					if (object && object->Class == proxyClass
						&& object->IsDefaultObject())
					{
						hairActorDefault =
							reinterpret_cast<
								ASkeletalMeshHairActor*>(object);
						break;
					}
				}
			}

			// This map actor's class default is authored static/no-delete, so
			// UE3 rejects an ordinary runtime Spawn before PreBeginPlay. Relax
			// only the default template for the duration of Spawn and restore
			// it immediately afterwards. The spawned copy remains movable.
			bool defaultWasStatic = false;
			bool defaultWasNoDelete = false;
			bool defaultWasMovable = false;
			UHair* defaultActorHair = nullptr;
			UHair* defaultComponentHair = nullptr;
			if (hairActorDefault)
			{
				defaultWasStatic = hairActorDefault->bStatic;
				defaultWasNoDelete =
					hairActorDefault->bNoDelete;
				defaultWasMovable = hairActorDefault->bMovable;
				hairActorDefault->bStatic = false;
				hairActorDefault->bNoDelete = false;
				hairActorDefault->bDeleteMe = false;
				hairActorDefault->bPendingDelete = false;
				hairActorDefault->bMovable = true;
				defaultActorHair = hairActorDefault->Hair;
				hairActorDefault->Hair = sourceHair->Template;
				if (hairActorDefault->HairComponent)
				{
					defaultComponentHair =
						hairActorDefault->HairComponent->Template;
					hairActorDefault->HairComponent->Template =
						sourceHair->Template;
				}
				Log("COSMETICSTAGE hair actor default prepared: "
					+ ObjectName(hairActorDefault)
					+ ", static="
					+ std::to_string(defaultWasStatic)
					+ ", noDelete="
					+ std::to_string(defaultWasNoDelete)
					+ ", movable="
					+ std::to_string(defaultWasMovable)
					+ ", skeletal="
					+ ObjectName(
						hairActorDefault->SkeletalMeshComponent)
					+ ", hairComponent="
					+ ObjectName(
						hairActorDefault->HairComponent)
					+ '.');
			}
			AActor* spawned = NativeSpawn(remote, proxyClass, remote,
				FName(), remote->Location, remote->Rotation,
				hairActorDefault, true);
			if (hairActorDefault)
			{
				hairActorDefault->bStatic = defaultWasStatic;
				hairActorDefault->bNoDelete =
					defaultWasNoDelete;
				hairActorDefault->bMovable =
					defaultWasMovable;
				hairActorDefault->Hair = defaultActorHair;
				if (hairActorDefault->HairComponent)
				{
					hairActorDefault->HairComponent->Template =
						defaultComponentHair;
				}
			}
			if (!spawned
				|| !spawned->IsA(
					ASkeletalMeshHairActor::StaticClass()))
			{
				if (spawned)
				{
					spawned->bNoDelete = false;
					NativeDestroyActor(spawned);
				}
				Log("COSMETICSTAGE independent hair actor spawn failed; "
					"using the rigid scalp fallback.");
				proxyClass =
					ASkeletalMeshActorSpawnable::StaticClass();
				spawned = NativeSpawn(remote, proxyClass, remote,
					FName(), remote->Location, remote->Rotation,
					nullptr, true);
				if (!spawned
					|| !spawned->IsA(
						ASkeletalMeshActorSpawnable::StaticClass()))
				{
					if (spawned)
					{
						spawned->bNoDelete = false;
						NativeDestroyActor(spawned);
					}
					Log("COSMETICSTAGE skeletal hair proxy "
						"fallback spawn failed.");
					return;
				}
			}

			auto* proxy =
				reinterpret_cast<ASkeletalMeshActor*>(
					spawned);
			ASkeletalMeshHairActor* hairActor =
				spawned->IsA(ASkeletalMeshHairActor::StaticClass())
					? reinterpret_cast<ASkeletalMeshHairActor*>(
						spawned)
					: nullptr;
			const bool spawnedWithPreparedTemplate =
				hairActor && hairActor->HairComponent
				&& hairActor->HairComponent->Template
					== sourceHair->Template;
			std::uintptr_t simulatorAtSpawn =
				hairActor && hairActor->HairComponent
					? hairActor->HairComponent->Simulator.Dummy
					: 0;
			proxy->bNoDelete = false;
			proxy->bStatic = false;
			proxy->bMovable = true;
			proxy->bCanBeDamaged = false;
			proxy->bCollideActors = false;
			proxy->bCollideWorld = false;
			proxy->bBlockActors = false;
			NativeSetOwner(proxy, remote);
			if (!proxy->SkeletalMeshComponent)
			{
				NativeSetActorHidden(proxy, true);
				NativeDestroyActor(proxy);
				Log("COSMETICSTAGE skeletal hair proxy has no component.");
				return;
			}

			USkeletalMeshComponent* component =
				proxy->SkeletalMeshComponent;
			NativeSetSkeletalMesh(component, hairMesh, false);
			component->LightEnvironment = remote->LightEnvironment;
			component->bOwnerNoSee = false;
			component->bOnlyOwnerSee = false;
			// The skeletal scalp is only a head-space transform anchor now.
			// Keep its pose ticking but do not render the cap mesh.
			component->HiddenGame = true;
			component->bPauseAnims = false;
			component->bNoSkeletonUpdate = false;
			component->bUpdateSkelWhenNotRendered = true;
			component->bTickAnimNodesWhenNotRendered = true;
			NativeSetComponentHidden(component, true);
			NativeSetActorHidden(proxy, false);
			NativeForceSkelUpdate(component);
			NativeForceComponentUpdate(component, false);
			// Establish a stable root-space bind pose first, then constrain the
			// scalp rigidly to Alice's animated head through the same native
			// component attachment path used by presentation weapons.
			component->LocalToWorld = remote->Mesh->LocalToWorld;
			component->Bounds = remote->Mesh->Bounds;
			component->bAlwaysUpdateMeshObject = true;
			component->bForceMeshObjectUpdate = true;
			const bool hairBound = AttachRigidPresentationCosmetic(
				remote->Mesh, component, true, "hair-skeletal");
			NativeForceComponentUpdate(component, false);
			NativeForceUpdateComponents(proxy, false, false);

			g_remoteHairProxy = proxy;
			g_remoteHairProxyBaseDrawScale =
				proxy->DrawScale > 0.001f
					? proxy->DrawScale : 1.0f;
			g_remoteHairSkeletalBaseScale =
				component->Scale > 0.001f
					? component->Scale : 1.0f;

			UHairComponent* strandHair =
				hairActor ? hairActor->HairComponent : nullptr;
			bool strandReset = false;
			bool strandAligned = false;
			if (hairActor && strandHair)
			{
				// Hair is immutable cooked strand data; the actor and simulator
				// are independent, while Template and Material can safely
				// reference the same assets as the playable Alice.
				hairActor->Hair = sourceHair->Template;
				hairActor->HairAttachBoneName =
					FName("Bip01-Head");
				strandHair->Template = sourceHair->Template;
				strandHair->PhysicsAsset =
					sourceHair->PhysicsAsset;
				strandHair->Force = sourceHair->Force;
				strandHair->PerturbAmplitude =
					sourceHair->PerturbAmplitude;
				strandHair->PerturbTemporalPeriod =
					sourceHair->PerturbTemporalPeriod;
				strandHair->PerturbSpatialPeriod =
					sourceHair->PerturbSpatialPeriod;
				strandHair->PerturbPhaseShift =
					sourceHair->PerturbPhaseShift;
				strandHair->Damping = sourceHair->Damping;
				strandHair->Iteration = sourceHair->Iteration;
				strandHair->LengthScale =
					sourceHair->LengthScale;
				strandHair->Material = sourceHair->Material;
				strandHair->TessellationStep =
					sourceHair->TessellationStep;
				strandHair->StrandWidth =
					sourceHair->StrandWidth;
				g_remoteHairBaseLengthScale =
					sourceHair->LengthScale;
				g_remoteHairBaseStrandWidth =
					sourceHair->StrandWidth;
				strandHair->SortOffset =
					sourceHair->SortOffset;
				strandHair->bOwnerNoSee = false;
				strandHair->bOnlyOwnerSee = false;
				// Match the playable Alice: the dedicated HairComponent uses
				// its Template data directly. Feeding the scalp component here
				// applies its authored basis a second time inside the native
				// renderer.
				strandHair->OverrideMesh = nullptr;
				strandHair->Bounds = remote->Mesh->Bounds;
				strandHair->HiddenGame = false;
				strandHair->bJustAttached = true;
				strandHair->bPendingReset = true;
				strandAligned =
					AlignIndependentHairActorToScalp(
						hairActor, strandHair, component);
				// Keep the component on its owning actor so Reset/Tick consume
				// their state normally. The actor itself now follows the scalp.
				NativeReattachComponent(
					hairActor, strandHair);
				AlignIndependentHairActorToScalp(
					hairActor, strandHair, component);
				NativeSetComponentHidden(strandHair, false);
				strandReset = NativeResetHair(strandHair);
				// Reset may refresh component state; retain the native
				// no-override rendering path used by the playable Alice.
				strandHair->OverrideMesh = nullptr;
				NativeForceComponentUpdate(
					strandHair, false);
				NativeForceUpdateComponents(
					hairActor, false, false);
				g_remoteIndependentHair = strandHair;
				g_remoteIndependentHairBaseScale =
					strandHair->Scale > 0.001f
						? strandHair->Scale : 1.0f;
			}

			// The component embedded in the cloned Alice is still tied to
			// Alice's native owner logic. Keep only that copy dormant.
			sourceHair->OverrideMesh = remote->Mesh;
			sourceHair->bJustAttached = false;
			sourceHair->bPendingReset = false;
			sourceHair->HiddenGame = true;
			NativeSetComponentHidden(sourceHair, true);

			std::ostringstream stream;
			stream << "COSMETICSTAGE skeletal hair proxy spawned: actor="
				<< ObjectName(proxy)
				<< ", mesh=" << ObjectName(hairMesh)
				<< ", lods=" << hairMesh->LODModels.ArrayNum
				<< ", materials=" << hairMesh->Materials.size()
				<< ", sourceBones=" << remote->Mesh->SpaceBases.size()
				<< ", parentBones=" << component->ParentBoneMap.size()
				<< ", attachedParent="
				<< ObjectName(component->AttachedToSkelComponent)
				<< ", headBound="
				<< (hairBound ? "yes" : "no")
				<< ", actorClass="
				<< ObjectName(proxy->Class)
				<< ", independentHair="
				<< ObjectName(strandHair)
				<< ", strandTemplate="
				<< ObjectName(
					strandHair ? strandHair->Template : nullptr)
				<< ", strandMaterial="
				<< ObjectName(
					strandHair ? strandHair->Material : nullptr)
				<< ", strandReset="
				<< (strandReset ? "yes" : "no")
				<< ", templatePreparedBeforeSpawn="
				<< (templatePreparedBeforeSpawn ? "yes" : "no")
				<< ", spawnedWithPreparedTemplate="
				<< (spawnedWithPreparedTemplate ? "yes" : "no")
				<< ", simulatorAtSpawn="
				<< simulatorAtSpawn
				<< ", strandActorAligned="
				<< (strandAligned ? "yes" : "no")
				<< ", simulator="
				<< (strandHair
					&& strandHair->Simulator.Dummy
						? "yes" : "no")
				<< ", strandOverride="
				<< ObjectName(strandHair
					? strandHair->OverrideMesh : nullptr)
				<< '.';
			Log(stream.str());
			for (int32_t index = 0;
				index < hairMesh->Materials.size(); ++index)
			{
				Log("COSMETICSTAGE scalp material["
					+ std::to_string(index) + "]="
					+ ObjectName(hairMesh->Materials.at(index))
					+ ", componentMaterial="
					+ ObjectName(index < component->Materials.size()
						? component->Materials.at(index) : nullptr)
					+ '.');
			}
		}

		void ForceRemoteCosmeticMasterPose(AAlicePawn* remote)
		{
			if (!remote || !remote->Mesh)
				return;

			auto maintainRigidCosmetic =
				[remote](USkeletalMeshComponent* component,
					const char* label)
			{
				if (!component
					|| component->AttachedToSkelComponent
						!= remote->Mesh)
				{
					return;
				}

				// Do not overwrite LocalToWorld here. The native pelvis
				// attachment carries the root-space bind offset and must be
				// allowed to inherit the animated pelvis hierarchy.
				component->Bounds = remote->Mesh->Bounds;
				component->bAlwaysUpdateMeshObject = true;
				component->bForceMeshObjectUpdate = true;
				component->bRecentlyRendered = true;

				const std::string key =
					std::string("cosmetic-rigid:") + label;
				if (g_loggedConfigAnimationStages
					.insert(key).second)
				{
					std::ostringstream stream;
					stream << "COSMETICSTAGE rigid active "
						<< label << ": parent="
						<< ObjectName(
							component->AttachedToSkelComponent)
						<< ", boundsRadius="
						<< component->Bounds.SphereRadius
						<< '.';
					Log(stream.str());
				}
			};

			const std::array<UClothComponent*, 4> clothComponents{
				remote->SkirtComponent,
				remote->BowComponent,
				remote->RibbonComponent,
				remote->EarComponent
			};
			for (UClothComponent* cloth : clothComponents)
			{
				if (!cloth || !cloth->SkeletalMesh
					|| cloth->AttachedToSkelComponent
						!= remote->Mesh)
				{
					continue;
				}
				maintainRigidCosmetic(
					cloth, cloth == remote->SkirtComponent
					? "skirt"
					: (cloth == remote->BowComponent
						? "bow"
						: (cloth == remote->RibbonComponent
							? "ribbon" : "ear")));
			}
			if (g_remoteHairProxy
				&& g_remoteHairProxy->SkeletalMeshComponent)
			{
				USkeletalMeshComponent* hair =
					g_remoteHairProxy->SkeletalMeshComponent;
				if (hair->AttachedToSkelComponent
					!= remote->Mesh)
				{
					const bool rebound =
						AttachRigidPresentationCosmetic(
							remote->Mesh, hair, true,
							"hair-skeletal-repair");
					if (g_loggedConfigAnimationStages.insert(
						"cosmetic-head-constraint-repair:"
						"hair-skeletal").second)
					{
						Log(std::string(
							"COSMETICSTAGE head constraint repair ")
							+ (rebound ? "succeeded." : "failed."));
					}
				}
				hair->Bounds = remote->Mesh->Bounds;
				hair->bAlwaysUpdateMeshObject = true;
				hair->bForceMeshObjectUpdate = true;
				hair->bRecentlyRendered = true;
				if (g_remoteIndependentHair)
				{
					AlignIndependentHairActorToScalp(
						g_remoteHairProxy,
						g_remoteIndependentHair, hair);
					g_remoteIndependentHair->Bounds =
						remote->Mesh->Bounds;
				}
				if (g_remoteIndependentHair
					&& g_remoteIndependentHair->OverrideMesh)
				{
					g_remoteIndependentHair->OverrideMesh = nullptr;
					if (g_loggedConfigAnimationStages.insert(
						"independent-hair-native-template-repair")
						.second)
					{
						Log("COSMETICSTAGE independent hair restored "
							"to native Template rendering.");
					}
				}
				if (g_loggedConfigAnimationStages.insert(
					"cosmetic-head-constraint:hair-skeletal")
					.second)
				{
					Log("COSMETICSTAGE head constraint active "
						"hair-skeletal: parent="
						+ ObjectName(
							hair->AttachedToSkelComponent)
						+ ", boundsRadius="
						+ std::to_string(
							hair->Bounds.SphereRadius)
						+ '.');
				}
			}

			if (g_remoteHidden)
			{
				// Dodge and local cinematics hide the presentation-only remote
				// Alice without mutating the replicated actor state.
				// Native component updates can re-expose rigid attachments one
				// frame after their body disappears, leaving skirt/scalp at the
				// motion root. Reassert component-local hiding post-pose.
				auto hide = [](UPrimitiveComponent* primitive,
					bool nativeVisibility)
				{
					if (!primitive)
						return;
					if (nativeVisibility)
					{
						primitive->HiddenGame = true;
						NativeSetComponentHidden(
							primitive, true);
					}
					primitive->SetScale(0.001f);
					primitive->Scale = 0.001f;
					primitive->SetScale3D(
						FVector(0.001f, 0.001f, 0.001f));
					primitive->Scale3D =
						FVector(0.001f, 0.001f, 0.001f);
				};
				hide(remote->Mesh, true);
				hide(remote->UpperBodyComponent, true);
				hide(remote->HairComponent, true);
				hide(remote->SkirtComponent, true);
				hide(remote->BowComponent, true);
				hide(remote->RibbonComponent, true);
				hide(remote->EarComponent, true);
				if (remote->Weapon)
				{
					hide(remote->Weapon->Mesh, true);
					remote->Weapon->bHidden = true;
					NativeSetActorHidden(remote->Weapon, true);
				}
				if (remote->DummyWeapon)
					hide(remote->DummyWeapon->Mesh, true);

				// Never use HiddenGame or scale on the independent UHair
				// component. Even a single frame invalidates its simulator.
				// Owner-only visibility is applied above and preserves it.
				if (g_remoteIndependentHair)
				{
					g_remoteIndependentHair->bOwnerNoSee = false;
					g_remoteIndependentHair->bOnlyOwnerSee = true;
				}
			}
			else
			{
				// Alice's single-player dodge path can leave translucency or
				// scale state on every Alice-shaped primitive in the world.
				// Clear that state only on our presentation tree. Visibility
				// itself was restored immediately before this function by
				// ApplyRemoteComponentVisibility, so dormant anchor meshes
				// remain dormant.
				auto restore = [](UPrimitiveComponent* primitive,
					float scale, const FVector& scale3D)
				{
					if (!primitive)
						return;
					primitive->bForceTranslucency = false;
					primitive->ForceTranslucencyAlpha = 1.0f;
					primitive->ForceTranslucencyTargetAlpha = 1.0f;
					primitive->ForceTranslucencyBlendTime = 0.0f;
					primitive->ForceTranslucencyBlendSpeed = 0.0f;
					primitive->SetScale(scale);
					primitive->Scale = scale;
					primitive->SetScale3D(scale3D);
					primitive->Scale3D = scale3D;
				};
				remote->bForceTranslucency = false;
				remote->ForceTranslucencyAlpha = 1.0f;
				const FVector unitScale(1.0f, 1.0f, 1.0f);
				const FVector actorScale =
					g_remotePresentation.valid
						? FVector(
							g_remotePresentation.state.drawScale3D[0],
							g_remotePresentation.state.drawScale3D[1],
							g_remotePresentation.state.drawScale3D[2])
						: unitScale;
				remote->SetDrawScale3D(actorScale);
				remote->DrawScale3D = actorScale;
				const FVector meshScale3D =
					g_remotePresentation.valid
						? FVector(
							g_remotePresentation.state.meshScale3D[0],
							g_remotePresentation.state.meshScale3D[1],
							g_remotePresentation.state.meshScale3D[2])
						: unitScale;
				restore(remote->Mesh,
					g_remotePresentation.valid
						&& std::isfinite(
							g_remotePresentation.state.meshScale)
						&& g_remotePresentation.state.meshScale > 0.01f
					? g_remotePresentation.state.meshScale : 1.0f,
					meshScale3D);
				restore(remote->UpperBodyComponent, 1.0f, unitScale);
				restore(remote->HairComponent, 1.0f, unitScale);
				restore(remote->SkirtComponent, 1.0f, unitScale);
				restore(remote->BowComponent, 1.0f, unitScale);
				restore(remote->RibbonComponent, 1.0f, unitScale);
				restore(remote->EarComponent, 1.0f, unitScale);
				if (g_remoteHairProxy)
				{
					float hairScaleFactor = 1.0f;
					if (g_remotePresentation.valid
						&& std::isfinite(
							g_remotePresentation.state.drawScale)
						&& g_remotePresentation.state.drawScale
							> 0.01f)
					{
						const float baseScale =
							g_remoteBaseDrawScale > 0.01f
								? g_remoteBaseDrawScale : 1.0f;
						hairScaleFactor = std::clamp(
							g_remotePresentation.state.drawScale
								/ baseScale,
							0.1f, 2.0f);
					}
					const float hairActorScale =
						g_remoteHairProxyBaseDrawScale
						* hairScaleFactor;
					g_remoteHairProxy->SetDrawScale(
						hairActorScale);
					g_remoteHairProxy->DrawScale =
						hairActorScale;
					restore(
						g_remoteHairProxy->SkeletalMeshComponent,
						g_remoteHairSkeletalBaseScale, unitScale);
				}
				restore(g_remoteIndependentHair,
					g_remoteIndependentHairBaseScale, unitScale);
				if (g_remoteHairProxy)
				{
					g_remoteHairProxy->SetDrawScale3D(unitScale);
					g_remoteHairProxy->DrawScale3D = unitScale;
				}
				if (g_remoteIndependentHair)
				{
					// HiddenGame/scale changes invalidate Alice's custom hair
					// simulator. Owner-only visibility keeps it ticking while
					// excluding it from the local cinematic view.
					g_remoteIndependentHair->bOwnerNoSee = false;
					g_remoteIndependentHair->bOnlyOwnerSee = false;
					g_remoteIndependentHair->HiddenGame = false;
					NativeSetComponentHidden(
						g_remoteIndependentHair, false);
				}
				g_remoteHairNeedsReset = false;
				if (remote->Weapon)
				{
					remote->Weapon->bForceTranslucency = false;
					remote->Weapon->ForceTranslucencyAlpha = 1.0f;
					if (remote->Weapon
						== g_remotePresentationWeapon)
					{
						remote->Weapon->SetDrawScale(
							g_remoteWeaponBaseDrawScale);
						remote->Weapon->DrawScale =
							g_remoteWeaponBaseDrawScale;
						remote->Weapon->SetDrawScale3D(unitScale);
						remote->Weapon->DrawScale3D = unitScale;
					}
					restore(remote->Weapon->Mesh, 1.0f, unitScale);
				}
				if (remote->DummyWeapon)
					restore(remote->DummyWeapon->Mesh, 1.0f,
						unitScale);
			}
		}

