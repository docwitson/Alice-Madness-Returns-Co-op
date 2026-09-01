		bool SendBytes(const void* bytes, int size)
		{
			return sendto(g_socket, static_cast<const char*>(bytes), size, 0,
				reinterpret_cast<const sockaddr*>(&g_serverEndpoint), sizeof(g_serverEndpoint)) == size;
		}

		std::size_t SaveFileIndex(SaveSyncFileKind kind)
		{
			return kind == SaveSyncFileKind::Checkpoint ? 1u : 0u;
		}

		const wchar_t* SaveFileName(SaveSyncFileKind kind)
		{
			return kind == SaveSyncFileKind::Checkpoint
				? L"Alice2Checkpoint.sav" : L"PersistentData_PC.PSD";
		}

		void RememberOriginalClientSavePath(const wchar_t* originalPath)
		{
			if (!originalPath || !*originalPath)
				return;
			const std::filesystem::path path(originalPath);
			const std::wstring name = path.filename().wstring();
			SaveSyncFileKind kind{};
			if (_wcsicmp(name.c_str(),
					SaveFileName(SaveSyncFileKind::PersistentData)) == 0)
				kind = SaveSyncFileKind::PersistentData;
			else if (_wcsicmp(name.c_str(),
					SaveFileName(SaveSyncFileKind::Checkpoint)) == 0)
				kind = SaveSyncFileKind::Checkpoint;
			else
				return;
			std::error_code error;
			const std::filesystem::path absolute =
				std::filesystem::absolute(path, error);
			const std::filesystem::path resolved = error ? path : absolute;
			std::lock_guard<std::mutex> lock(g_observedSavePathMutex);
			g_originalClientSavePaths[SaveFileIndex(kind)] = resolved;

			// An existing selected profile may only touch one of its files while
			// AliceCoop is running.  Discover the sibling only when it already
			// exists in the exact same profile directory; never guess a different
			// profile, because the eventual commit is intentionally destructive.
			const SaveSyncFileKind siblingKind =
				kind == SaveSyncFileKind::Checkpoint
					? SaveSyncFileKind::PersistentData
					: SaveSyncFileKind::Checkpoint;
			const std::size_t siblingIndex = SaveFileIndex(siblingKind);
			if (g_originalClientSavePaths[siblingIndex].empty())
			{
				const std::filesystem::path sibling =
					resolved.parent_path() / SaveFileName(siblingKind);
				std::error_code siblingError;
				if (std::filesystem::is_regular_file(sibling, siblingError)
					&& !siblingError)
				{
					g_originalClientSavePaths[siblingIndex] = sibling;
				}
			}
		}

		void RefreshActiveProfileSaveTargets()
		{
			UAliceGameEngine* aliceEngine = g_State.AliceEngine;
			if (!aliceEngine)
				return;
			const int32_t index = aliceEngine->CurrentPlayerDataIndex;
			if (index < 0 || index >= aliceEngine->PlayerList.size())
				return;
			const FAliceGamePlayerProfileData& profile =
				aliceEngine->PlayerList.at(index);
			const wchar_t* rawName = profile.PlayerName.c_str();
			if (!rawName || !*rawName)
				return;
			const std::wstring profileName(rawName);
			if (profileName == L"." || profileName == L".."
				|| profileName.find_first_of(L"\\/:*?\"<>|")
					!= std::wstring::npos)
			{
				return;
			}

			wchar_t userProfile[1024]{};
			const DWORD count = GetEnvironmentVariableW(
				L"USERPROFILE", userProfile,
				static_cast<DWORD>(std::size(userProfile)));
			if (count == 0 || count >= std::size(userProfile))
				return;
			const std::filesystem::path root =
				std::filesystem::path(userProfile) / L"Documents"
				/ L"My Games" / L"Alice Madness Returns"
				/ L"AliceGame" / L"CheckPoint";
			const std::filesystem::path directory = root / profileName;
			const std::array<std::filesystem::path, SaveSyncFileCount> paths{
				directory / SaveFileName(SaveSyncFileKind::PersistentData),
				directory / SaveFileName(SaveSyncFileKind::Checkpoint) };
			const std::string displayName = NarrowAscii(profileName);
			bool changed = false;
			{
				std::lock_guard<std::mutex> lock(g_observedSavePathMutex);
				changed = g_activeSaveProfileName != displayName;
				g_activeSaveProfileName = displayName;
				g_activeSaveProfileMetadata = {
					profile.CompletPercent,
					profile.PlayedHours,
					profile.PlayedMin };
				if (g_config.role == Role::Client)
					g_originalClientSavePaths = paths;
				else if (g_config.role == Role::Host)
					g_observedSavePaths = paths;
			}
			if (changed)
			{
				Log("SAVESYNC active selected profile=" + displayName
					+ ", role="
					+ (g_config.role == Role::Host ? "host." : "client."));
			}
		}

		bool HaveOriginalClientSaveTargets()
		{
			std::lock_guard<std::mutex> lock(g_observedSavePathMutex);
			return !g_originalClientSavePaths[0].empty()
				&& !g_originalClientSavePaths[1].empty()
				&& g_originalClientSavePaths[0].parent_path()
					== g_originalClientSavePaths[1].parent_path();
		}

		void ApplyPendingClientProfileMetadata()
		{
			if (!g_pendingClientProfileMetadataApply.load(
					std::memory_order_acquire))
				return;
			UAliceGameEngine* aliceEngine = g_State.AliceEngine;
			if (!aliceEngine)
				return;
			std::array<float, 3> metadata{};
			std::string profileName;
			{
				std::lock_guard<std::mutex> lock(g_observedSavePathMutex);
				metadata = g_pendingClientProfileMetadata;
				profileName = g_pendingClientProfileName;
			}
			if (profileName.empty())
				return;
			FAliceGamePlayerProfileData* target = nullptr;
			for (int32_t index = 0; index < aliceEngine->PlayerList.size(); ++index)
			{
				FAliceGamePlayerProfileData& candidate =
					aliceEngine->PlayerList.at(index);
				const wchar_t* rawName = candidate.PlayerName.c_str();
				if (rawName
					&& _stricmp(NarrowAscii(std::wstring(rawName)).c_str(),
						profileName.c_str()) == 0)
				{
					target = &candidate;
					break;
				}
			}
			if (!target)
				return;
			target->CompletPercent = metadata[0];
			target->PlayedHours = metadata[1];
			target->PlayedMin = metadata[2];
			g_pendingClientProfileMetadataApply.store(
				false, std::memory_order_release);
			aliceEngine->SavePlayerList();
			Log("SAVESYNC selected profile-list metadata updated for "
				+ profileName + '.');
		}

		void SetSaveSyncStatus(
			const std::string& status, bool inProgress, int progress)
		{
			{
				std::lock_guard<std::mutex> lock(g_saveSyncStatusMutex);
				g_saveSyncStatus = status;
			}
			g_saveSyncProgress.store(
				std::clamp(progress, 0, 100), std::memory_order_release);
			g_saveSyncInProgress.store(
				inProgress, std::memory_order_release);
			Log("SAVESYNC " + status + " progress="
				+ std::to_string(std::clamp(progress, 0, 100)) + "%.");
		}

		bool ComputeSha256(
			const std::vector<std::uint8_t>& bytes,
			std::array<std::uint8_t, 32>& digest)
		{
			BCRYPT_ALG_HANDLE algorithm = nullptr;
			BCRYPT_HASH_HANDLE hash = nullptr;
			DWORD objectLength = 0;
			DWORD resultLength = 0;
			bool ok = false;
			if (BCryptOpenAlgorithmProvider(
					&algorithm, BCRYPT_SHA256_ALGORITHM,
					nullptr, 0) < 0)
				return false;
			if (BCryptGetProperty(
					algorithm, BCRYPT_OBJECT_LENGTH,
					reinterpret_cast<PUCHAR>(&objectLength),
					sizeof(objectLength), &resultLength, 0) >= 0)
			{
				std::vector<std::uint8_t> object(objectLength);
				if (BCryptCreateHash(
						algorithm, &hash, object.data(),
						static_cast<ULONG>(object.size()),
						nullptr, 0, 0) >= 0
					&& (bytes.empty() || BCryptHashData(
						hash, const_cast<PUCHAR>(bytes.data()),
						static_cast<ULONG>(bytes.size()), 0) >= 0)
					&& BCryptFinishHash(
						hash, digest.data(),
						static_cast<ULONG>(digest.size()), 0) >= 0)
				{
					ok = true;
				}
			}
			if (hash)
				BCryptDestroyHash(hash);
			BCryptCloseAlgorithmProvider(algorithm, 0);
			return ok;
		}

		bool ReadSaveFile(
			const std::filesystem::path& path,
			std::vector<std::uint8_t>& bytes)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file)
				return false;
			const std::streamoff size = file.tellg();
			if (size <= 0 || size > 4 * 1024 * 1024)
				return false;
			bytes.resize(static_cast<std::size_t>(size));
			file.seekg(0, std::ios::beg);
			return static_cast<bool>(file.read(
				reinterpret_cast<char*>(bytes.data()), size));
		}

		bool DiscoverHostSavePaths(
			std::array<std::filesystem::path,
				SaveSyncFileCount>& paths)
		{
			{
				std::lock_guard<std::mutex> lock(g_observedSavePathMutex);
				paths = g_observedSavePaths;
			}
			std::error_code error;
			if (!paths[0].empty() && paths[1].empty())
				paths[1] = paths[0].parent_path()
					/ SaveFileName(SaveSyncFileKind::Checkpoint);
			if (!paths[1].empty() && paths[0].empty())
				paths[0] = paths[1].parent_path()
					/ SaveFileName(SaveSyncFileKind::PersistentData);
			if (std::filesystem::is_regular_file(paths[0], error)
				&& std::filesystem::is_regular_file(paths[1], error))
			{
				return true;
			}

			wchar_t userProfile[1024]{};
			const DWORD count = GetEnvironmentVariableW(
				L"USERPROFILE", userProfile,
				static_cast<DWORD>(std::size(userProfile)));
			if (count == 0 || count >= std::size(userProfile))
				return false;
			const std::filesystem::path root =
				std::filesystem::path(userProfile) / L"Documents"
				/ L"My Games" / L"Alice Madness Returns"
				/ L"AliceGame" / L"CheckPoint";
			if (!std::filesystem::is_directory(root, error))
				return false;

			struct Candidate
			{
				std::array<std::filesystem::path,
					SaveSyncFileCount> files{};
				std::filesystem::file_time_type newest{};
			};
			std::unordered_map<std::wstring, Candidate> candidates;
			for (std::filesystem::recursive_directory_iterator iterator(
					root, std::filesystem::directory_options::skip_permission_denied,
					error), end; iterator != end && !error;
				iterator.increment(error))
			{
				if (!iterator->is_regular_file(error))
					continue;
				const std::wstring name = iterator->path().filename().wstring();
				SaveSyncFileKind kind{};
				if (_wcsicmp(name.c_str(),
						SaveFileName(SaveSyncFileKind::PersistentData)) == 0)
					kind = SaveSyncFileKind::PersistentData;
				else if (_wcsicmp(name.c_str(),
						SaveFileName(SaveSyncFileKind::Checkpoint)) == 0)
					kind = SaveSyncFileKind::Checkpoint;
				else
					continue;
				Candidate& candidate = candidates[
					iterator->path().parent_path().wstring()];
				candidate.files[SaveFileIndex(kind)] = iterator->path();
				const auto modified = iterator->last_write_time(error);
				if (!error && modified > candidate.newest)
					candidate.newest = modified;
			}
			const Candidate* best = nullptr;
			for (const auto& [directory, candidate] : candidates)
			{
				if (candidate.files[0].empty() || candidate.files[1].empty())
					continue;
				if (!best || candidate.newest > best->newest)
					best = &candidate;
			}
			if (!best)
				return false;
			paths = best->files;
			return true;
		}

		bool WriteSaveStage(
			const std::filesystem::path& path,
			const std::vector<std::uint8_t>& bytes)
		{
			HANDLE file = CreateFileW(
				path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
			if (file == INVALID_HANDLE_VALUE)
				return false;
			std::size_t offset = 0;
			bool ok = true;
			while (offset < bytes.size())
			{
				DWORD written = 0;
				const std::size_t remaining = bytes.size() - offset;
				const DWORD amount = static_cast<DWORD>((std::min)(
					remaining, static_cast<std::size_t>(64 * 1024)));
				if (!WriteFile(file, bytes.data() + offset,
						amount, &written, nullptr)
					|| written != amount)
				{
					ok = false;
					break;
				}
				offset += written;
			}
			if (ok)
				ok = FlushFileBuffers(file) != FALSE;
			CloseHandle(file);
			return ok;
		}

		bool EnsureClientProfileBootstrap(
			const std::filesystem::path& targetDirectory,
			std::string& errorText)
		{
			const std::filesystem::path target =
				targetDirectory / L"GameConfig_PC.CFG";
			std::error_code filesystemError;
			if (std::filesystem::is_regular_file(target, filesystemError)
				&& !filesystemError
				&& std::filesystem::file_size(target, filesystemError) > 0)
			{
				return true;
			}

			// GameConfig is profile-local controller/bootstrap state. It must not
			// come from the host: that would also import the host's input and video
			// preferences. A genuinely empty Alice profile does not have this file
			// yet, so initialize it from another profile owned by this same client.
			const std::filesystem::path checkpointRoot =
				targetDirectory.parent_path();
			std::filesystem::path best;
			std::filesystem::file_time_type bestTime{};
			filesystemError.clear();
			for (std::filesystem::directory_iterator iterator(
					checkpointRoot,
					std::filesystem::directory_options::skip_permission_denied,
					filesystemError), end;
				iterator != end && !filesystemError;
				iterator.increment(filesystemError))
			{
				if (!iterator->is_directory(filesystemError)
					|| iterator->path() == targetDirectory)
				{
					continue;
				}
				const std::filesystem::path candidate =
					iterator->path() / L"GameConfig_PC.CFG";
				filesystemError.clear();
				if (!std::filesystem::is_regular_file(
						candidate, filesystemError)
					|| filesystemError)
				{
					continue;
				}
				const std::uintmax_t byteCount =
					std::filesystem::file_size(candidate, filesystemError);
				if (filesystemError || byteCount == 0
					|| byteCount > 256 * 1024)
				{
					continue;
				}
				const auto modified =
					std::filesystem::last_write_time(candidate, filesystemError);
				if (!filesystemError
					&& (best.empty() || modified > bestTime))
				{
					best = candidate;
					bestTime = modified;
				}
			}
			if (best.empty())
			{
				errorText =
					"EMPTY PROFILE: START A NEW GAME ONCE, THEN SYNC";
				return false;
			}

			const std::filesystem::path stage =
				target.wstring() + L".alicecoop-bootstrap.tmp";
			DeleteFileW(stage.c_str());
			if (!CopyFileW(best.c_str(), stage.c_str(), FALSE)
				|| !MoveFileExW(stage.c_str(), target.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			{
				DeleteFileW(stage.c_str());
				errorText = "CLIENT PROFILE BOOTSTRAP FAILED";
				return false;
			}
			Log("SAVESYNC initialized empty client profile GameConfig from local profile "
				+ NarrowAscii(best.parent_path().filename().wstring()) + '.');
			return true;
		}

		bool CommitClientSaveTransfer(
			const ClientSaveTransfer& transfer, std::string& errorText)
		{
			const std::array<std::filesystem::path, SaveSyncFileCount>& targets =
				transfer.targetPaths;
			if (targets[0].empty() || targets[1].empty()
				|| targets[0].parent_path() != targets[1].parent_path())
			{
				errorText = "SELECT OR CREATE THE CLIENT PROFILE FIRST";
				return false;
			}
			const std::filesystem::path directory = targets[0].parent_path();
			std::error_code filesystemError;
			std::filesystem::create_directories(directory, filesystemError);
			if (filesystemError
				|| !EnsureClientProfileBootstrap(directory, errorText))
			{
				if (errorText.empty())
					errorText = "COULD NOT CREATE CLIENT PROFILE DIRECTORY";
				return false;
			}
			std::array<std::filesystem::path, SaveSyncFileCount> stages{};
			std::array<std::filesystem::path, SaveSyncFileCount> backups{};
			std::array<bool, SaveSyncFileCount> existed{};
			for (std::size_t index = 0; index < SaveSyncFileCount; ++index)
			{
				stages[index] = targets[index].wstring() + L".alicecoop-receive.tmp";
				backups[index] = targets[index].wstring() + L".alicecoop-presync.bak";
				DeleteFileW(stages[index].c_str());
				if (!WriteSaveStage(stages[index], transfer.files[index].bytes))
				{
					errorText = "FAILED TO STAGE RECEIVED SAVE";
					for (const auto& stage : stages)
						DeleteFileW(stage.c_str());
					return false;
				}
				std::vector<std::uint8_t> verifyBytes;
				std::array<std::uint8_t, 32> verifyHash{};
				if (!ReadSaveFile(stages[index], verifyBytes)
					|| !ComputeSha256(verifyBytes, verifyHash)
					|| verifyHash != transfer.files[index].hash)
				{
					errorText = "STAGED SAVE FAILED SHA-256 CHECK";
					for (const auto& stage : stages)
						DeleteFileW(stage.c_str());
					return false;
				}
				existed[index] = GetFileAttributesW(targets[index].c_str())
					!= INVALID_FILE_ATTRIBUTES;
				if (existed[index]
					&& !CopyFileW(targets[index].c_str(),
						backups[index].c_str(), FALSE))
				{
					errorText = "COULD NOT CREATE PRE-SYNC BACKUP";
					for (const auto& stage : stages)
						DeleteFileW(stage.c_str());
					return false;
				}
			}

			std::size_t committed = 0;
			for (; committed < SaveSyncFileCount; ++committed)
			{
				if (!MoveFileExW(stages[committed].c_str(),
						targets[committed].c_str(),
						MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				{
					errorText = "ATOMIC SAVE REPLACEMENT FAILED";
					break;
				}
			}
			if (committed != SaveSyncFileCount)
			{
				for (std::size_t index = 0; index < committed; ++index)
				{
					if (existed[index])
						CopyFileW(backups[index].c_str(),
							targets[index].c_str(), FALSE);
					else
						DeleteFileW(targets[index].c_str());
				}
				for (const auto& stage : stages)
					DeleteFileW(stage.c_str());
				return false;
			}
			return true;
		}

		void SendHello()
		{
			HelloPayload payload{};
			payload.role = g_config.role;
			payload.processId = GetCurrentProcessId();
			const char* label = g_config.role == Role::Host ? "HOST" : "CLIENT";
			strncpy_s(payload.label, label, _TRUNCATE);
			const auto packet = MakePacket(PacketType::Hello, 0, 0, g_sequence++, payload);
			SendBytes(&packet, sizeof(packet));
		}

		void SendClientCommand(const ClientCommandPayload& command)
		{
			const auto packet = MakePacket(PacketType::ClientCommand,
				g_sessionId.load(), g_playerId.load(), g_sequence++, command);
			SendBytes(&packet, sizeof(packet));
		}

		void SendHostSnapshot(const HostSnapshotPayload& snapshot)
		{
			const auto packet = MakePacket(PacketType::HostSnapshot,
				g_sessionId.load(), g_playerId.load(), g_sequence++, snapshot);
			SendBytes(&packet, sizeof(packet));
		}

		void SendAnimationGraph(const AnimationGraphPayload& graph)
		{
			const auto packet = MakePacket(PacketType::AnimationGraph,
				g_sessionId.load(), g_playerId.load(), g_sequence++, graph);
			SendBytes(&packet, sizeof(packet));
		}

		void SendProjectileEvent(const ProjectileEventPayload& event)
		{
			const auto packet = MakePacket(PacketType::ProjectileEvent,
				g_sessionId.load(), g_playerId.load(), g_sequence++, event);
			SendBytes(&packet, sizeof(packet));
		}

		void SendSharedWorldEvent(const SharedWorldEventPayload& event)
		{
			const auto packet = MakePacket(PacketType::SharedWorldEvent,
				g_sessionId.load(), g_playerId.load(), g_sequence++, event);
			SendBytes(&packet, sizeof(packet));
		}

		void SendSaveSyncRequest(std::uint32_t transferId)
		{
			SaveSyncRequestPayload payload{};
			payload.transferId = transferId;
			payload.flags = 1; // explicit destructive confirmation was accepted
			const auto packet = MakePacket(PacketType::SaveSyncRequest,
				g_sessionId.load(), g_playerId.load(), g_sequence++, payload);
			SendBytes(&packet, sizeof(packet));
		}

		void SendSaveSyncManifest(const HostSaveTransfer& transfer)
		{
			SaveSyncManifestPayload payload{};
			payload.transferId = transfer.transferId;
			for (std::size_t index = 0; index < SaveSyncFileCount; ++index)
			{
				const SaveTransferFile& file = transfer.files[index];
				payload.files[index].kind = file.kind;
				payload.files[index].byteCount =
					static_cast<std::uint32_t>(file.bytes.size());
				payload.files[index].chunkCount = static_cast<std::uint16_t>(
					SaveChunkCount(file.bytes.size()));
				std::copy(file.hash.begin(), file.hash.end(),
					payload.files[index].sha256);
			}
			payload.completionPercent = transfer.completionPercent;
			payload.playedHours = transfer.playedHours;
			payload.playedMinutes = transfer.playedMinutes;
			const auto packet = MakePacket(PacketType::SaveSyncManifest,
				g_sessionId.load(), g_playerId.load(), g_sequence++, payload);
			SendBytes(&packet, sizeof(packet));
		}

		void SendSaveSyncChunk(
			const HostSaveTransfer& transfer,
			std::size_t fileIndex, std::size_t chunkIndex)
		{
			const SaveTransferFile& file = transfer.files[fileIndex];
			const std::size_t offset = chunkIndex * SaveSyncChunkBytes;
			if (offset >= file.bytes.size())
				return;
			SaveSyncChunkPayload payload{};
			payload.transferId = transfer.transferId;
			payload.kind = file.kind;
			payload.chunkIndex = static_cast<std::uint16_t>(chunkIndex);
			payload.chunkCount = static_cast<std::uint16_t>(
				SaveChunkCount(file.bytes.size()));
			payload.dataSize = static_cast<std::uint16_t>(
				SaveChunkDataSize(file.bytes.size(), chunkIndex));
			std::copy_n(file.bytes.data() + offset,
				payload.dataSize, payload.data);
			payload.dataChecksum = SaveChunkChecksum(
				payload.data, payload.dataSize);
			const auto packet = MakePacket(PacketType::SaveSyncChunk,
				g_sessionId.load(), g_playerId.load(), g_sequence++, payload);
			SendBytes(&packet, sizeof(packet));
		}

		void SendSaveSyncAck(
			std::uint32_t transferId, SaveSyncFileKind kind,
			SaveSyncAckStatus status, std::uint16_t chunkIndex,
			std::uint32_t detail = 0)
		{
			SaveSyncAckPayload payload{};
			payload.transferId = transferId;
			payload.kind = kind;
			payload.status = status;
			payload.chunkIndex = chunkIndex;
			payload.detail = detail;
			const auto packet = MakePacket(PacketType::SaveSyncAck,
				g_sessionId.load(), g_playerId.load(), g_sequence++, payload);
			SendBytes(&packet, sizeof(packet));
		}

		bool PrepareHostSaveTransfer(std::uint32_t transferId)
		{
			std::array<std::filesystem::path, SaveSyncFileCount> paths{};
			if (!DiscoverHostSavePaths(paths))
			{
				SetSaveSyncStatus(
					"HOST SAVE FILES NOT FOUND", false, 0);
				SendSaveSyncAck(transferId,
					SaveSyncFileKind::PersistentData,
					SaveSyncAckStatus::TransferFailed, 0, 1);
				return false;
			}
			HostSaveTransfer transfer{};
			transfer.active = true;
			transfer.transferId = transferId;
			transfer.startedAt = Clock::now();
			transfer.nextManifestAt = Clock::now();
			{
				std::lock_guard<std::mutex> lock(g_observedSavePathMutex);
				transfer.completionPercent = g_activeSaveProfileMetadata[0];
				transfer.playedHours = g_activeSaveProfileMetadata[1];
				transfer.playedMinutes = g_activeSaveProfileMetadata[2];
			}
			const std::array<SaveSyncFileKind, SaveSyncFileCount> kinds{
				SaveSyncFileKind::PersistentData,
				SaveSyncFileKind::Checkpoint };
			for (std::size_t index = 0; index < SaveSyncFileCount; ++index)
			{
				transfer.files[index].kind = kinds[index];
				transfer.files[index].path = paths[index];
				if (!ReadSaveFile(paths[index], transfer.files[index].bytes)
					|| !ComputeSha256(
						transfer.files[index].bytes,
						transfer.files[index].hash))
				{
					SetSaveSyncStatus(
						"HOST SAVE READ FAILED", false, 0);
					SendSaveSyncAck(transferId, kinds[index],
						SaveSyncAckStatus::TransferFailed, 0, 2);
					return false;
				}
				const std::size_t chunkCount =
					SaveChunkCount(transfer.files[index].bytes.size());
				if (chunkCount == 0
					|| chunkCount > (std::numeric_limits<std::uint16_t>::max)())
				{
					SendSaveSyncAck(transferId, kinds[index],
						SaveSyncAckStatus::TransferFailed, 0, 3);
					return false;
				}
				transfer.acked[index].assign(chunkCount, 0);
			}
			g_hostSaveTransfer = std::move(transfer);
			SetSaveSyncStatus("SENDING VERIFIED HOST SAVE", true, 0);
			Log("SAVESYNC host prepared transfer="
				+ std::to_string(transferId) + ", persistent="
				+ std::to_string(g_hostSaveTransfer.files[0].bytes.size())
				+ " bytes, checkpoint="
				+ std::to_string(g_hostSaveTransfer.files[1].bytes.size())
				+ " bytes.");
			return true;
		}

		void HandleSaveSyncManifest(const SaveSyncManifestPayload& manifest)
		{
			if (g_config.role != Role::Client || manifest.transferId == 0
				|| manifest.transferId
					!= g_requestedSaveTransferId.load(std::memory_order_acquire))
				return;
			ClientSaveTransfer transfer{};
			transfer.active = true;
			transfer.transferId = manifest.transferId;
			transfer.startedAt = Clock::now();
			if (!std::isfinite(manifest.completionPercent)
				|| !std::isfinite(manifest.playedHours)
				|| !std::isfinite(manifest.playedMinutes)
				|| manifest.completionPercent < 0.0f
				|| manifest.completionPercent > 100.0f
				|| manifest.playedHours < 0.0f
				|| manifest.playedHours > 1000000.0f
				|| manifest.playedMinutes < 0.0f
				|| manifest.playedMinutes > 1000000.0f)
			{
				return;
			}
			transfer.completionPercent = manifest.completionPercent;
			transfer.playedHours = manifest.playedHours;
			transfer.playedMinutes = manifest.playedMinutes;
			{
				std::lock_guard<std::mutex> lock(g_observedSavePathMutex);
				transfer.targetPaths = g_requestedClientSavePaths;
				transfer.targetProfileName = g_requestedClientProfileName;
			}
			if (transfer.targetPaths[0].empty()
				|| transfer.targetPaths[1].empty()
				|| transfer.targetProfileName.empty())
			{
				return;
			}
			std::array<bool, SaveSyncFileCount> seen{};
			for (const SaveSyncFileManifest& incoming : manifest.files)
			{
				if (incoming.kind != SaveSyncFileKind::PersistentData
					&& incoming.kind != SaveSyncFileKind::Checkpoint)
					return;
				const std::size_t index = SaveFileIndex(incoming.kind);
				if (seen[index] || incoming.byteCount == 0
					|| incoming.byteCount > 4 * 1024 * 1024
					|| incoming.chunkCount == 0
					|| incoming.chunkCount != SaveChunkCount(incoming.byteCount))
					return;
				seen[index] = true;
				SaveTransferFile& file = transfer.files[index];
				file.kind = incoming.kind;
				file.bytes.resize(incoming.byteCount);
				file.chunks.assign(incoming.chunkCount, 0);
				std::copy_n(incoming.sha256, file.hash.size(), file.hash.begin());
				transfer.totalChunks += incoming.chunkCount;
			}
			if (!seen[0] || !seen[1])
				return;
			g_clientSaveTransfer = std::move(transfer);
			SetSaveSyncStatus("RECEIVING HOST SAVE", true, 0);
		}

		void HandleSaveSyncChunk(const SaveSyncChunkPayload& chunk)
		{
			if (g_config.role != Role::Client
				|| !g_clientSaveTransfer.active
				|| chunk.transferId != g_clientSaveTransfer.transferId
				|| (chunk.kind != SaveSyncFileKind::PersistentData
					&& chunk.kind != SaveSyncFileKind::Checkpoint))
				return;
			const std::size_t fileIndex = SaveFileIndex(chunk.kind);
			SaveTransferFile& file = g_clientSaveTransfer.files[fileIndex];
			if (chunk.chunkCount != file.chunks.size()
				|| chunk.chunkIndex >= file.chunks.size()
				|| chunk.dataSize == 0
				|| chunk.dataSize > SaveSyncChunkBytes)
				return;
			const std::size_t offset =
				static_cast<std::size_t>(chunk.chunkIndex)
					* SaveSyncChunkBytes;
			const std::size_t expected = SaveChunkDataSize(
				file.bytes.size(), chunk.chunkIndex);
			if (chunk.dataSize != expected
				|| chunk.dataChecksum != SaveChunkChecksum(
					chunk.data, chunk.dataSize))
				return;
			if (!file.chunks[chunk.chunkIndex])
			{
				std::copy_n(chunk.data, chunk.dataSize,
					file.bytes.data() + offset);
				file.chunks[chunk.chunkIndex] = 1;
				++g_clientSaveTransfer.receivedChunks;
			}
			SendSaveSyncAck(chunk.transferId, chunk.kind,
				SaveSyncAckStatus::ChunkReceived, chunk.chunkIndex);
			const int progress = static_cast<int>(
				g_clientSaveTransfer.receivedChunks * 100
					/ (std::max<std::size_t>)(
						1, g_clientSaveTransfer.totalChunks));
			g_saveSyncProgress.store(progress, std::memory_order_release);
			if (g_clientSaveTransfer.receivedChunks
				!= g_clientSaveTransfer.totalChunks)
				return;

			for (const SaveTransferFile& completed :
				g_clientSaveTransfer.files)
			{
				std::array<std::uint8_t, 32> digest{};
				if (!ComputeSha256(completed.bytes, digest)
					|| digest != completed.hash)
				{
					SendSaveSyncAck(chunk.transferId, completed.kind,
						SaveSyncAckStatus::TransferFailed, 0, 4);
					SetSaveSyncStatus(
						"TRANSFER FAILED SHA-256 CHECK", false, progress);
					g_clientSaveTransfer = {};
					g_requestedSaveTransferId.store(0);
					return;
				}
			}
			SetSaveSyncStatus("VERIFIED; APPLYING BOTH SAVE FILES", true, 100);
			std::string errorText;
			const bool committed = CommitClientSaveTransfer(
				g_clientSaveTransfer, errorText);
			SendSaveSyncAck(chunk.transferId,
				SaveSyncFileKind::PersistentData,
				committed ? SaveSyncAckStatus::TransferVerified
					: SaveSyncAckStatus::TransferFailed,
				0, committed ? 0u : 5u);
			if (committed)
			{
				{
					std::lock_guard<std::mutex> lock(g_observedSavePathMutex);
					g_pendingClientProfileMetadata = {
						g_clientSaveTransfer.completionPercent,
						g_clientSaveTransfer.playedHours,
						g_clientSaveTransfer.playedMinutes };
					g_pendingClientProfileName =
						g_clientSaveTransfer.targetProfileName;
				}
				g_pendingClientProfileMetadataApply.store(
					true, std::memory_order_release);
				SetSaveSyncStatus(
					"VERIFIED - RESTART CLIENT TO LOAD PROFILE", false, 100);
			}
			else
				SetSaveSyncStatus(errorText, false, 100);
			g_clientSaveTransfer = {};
			g_requestedSaveTransferId.store(0);
		}

		void HandleSaveSyncAck(const SaveSyncAckPayload& ack)
		{
			if (g_config.role == Role::Client)
			{
				if (ack.status == SaveSyncAckStatus::TransferFailed
					&& ack.transferId
						== g_requestedSaveTransferId.load())
				{
					SetSaveSyncStatus(
						"HOST COULD NOT PROVIDE SAVE", false, 0);
					g_requestedSaveTransferId.store(0);
					g_clientSaveTransfer = {};
				}
				return;
			}
			if (!g_hostSaveTransfer.active
				|| ack.transferId != g_hostSaveTransfer.transferId)
				return;
			if (ack.status == SaveSyncAckStatus::ChunkReceived)
			{
				const std::size_t fileIndex = SaveFileIndex(ack.kind);
				if (fileIndex < SaveSyncFileCount
					&& ack.chunkIndex < g_hostSaveTransfer.acked[fileIndex].size())
					g_hostSaveTransfer.acked[fileIndex][ack.chunkIndex] = 1;
			}
			else if (ack.status == SaveSyncAckStatus::TransferVerified)
			{
				SetSaveSyncStatus("CLIENT SAVE VERIFIED", false, 100);
				g_hostSaveTransfer = {};
			}
			else if (ack.status == SaveSyncAckStatus::TransferFailed)
			{
				SetSaveSyncStatus("CLIENT REJECTED SAVE TRANSFER", false, 0);
				g_hostSaveTransfer = {};
			}
		}

		void PumpSaveSyncNetwork(Clock::time_point now)
		{
			if (!g_connected)
				return;
			if (g_config.role == Role::Client)
			{
				const std::uint32_t requested =
					g_requestedSaveTransferId.load(std::memory_order_acquire);
				if (requested != 0 && !g_clientSaveTransfer.active)
				{
					if (g_clientSaveTransfer.transferId != requested)
					{
						g_clientSaveTransfer = {};
						g_clientSaveTransfer.transferId = requested;
						g_clientSaveTransfer.startedAt = now;
						g_clientSaveTransfer.nextRequestAt = now;
					}
					if (now >= g_clientSaveTransfer.nextRequestAt)
					{
						SendSaveSyncRequest(requested);
						g_clientSaveTransfer.nextRequestAt =
							now + std::chrono::milliseconds(500);
					}
					if (now - g_clientSaveTransfer.startedAt
						> std::chrono::seconds(20))
					{
						SetSaveSyncStatus("HOST SAVE REQUEST TIMED OUT", false, 0);
						g_requestedSaveTransferId.store(0);
						g_clientSaveTransfer = {};
					}
				}
				return;
			}

			if (!g_hostSaveTransfer.active)
				return;
			if (now - g_hostSaveTransfer.startedAt > std::chrono::seconds(90))
			{
				SetSaveSyncStatus("SAVE TRANSFER TIMED OUT", false, 0);
				g_hostSaveTransfer = {};
				return;
			}
			if (now >= g_hostSaveTransfer.nextManifestAt)
			{
				SendSaveSyncManifest(g_hostSaveTransfer);
				g_hostSaveTransfer.nextManifestAt =
					now + std::chrono::milliseconds(600);
			}
			const std::size_t attempts =
				g_hostSaveTransfer.acked[0].size()
					+ g_hostSaveTransfer.acked[1].size();
			for (int sent = 0; sent < 2; ++sent)
			{
				bool found = false;
				for (std::size_t attempt = 0; attempt < attempts; ++attempt)
				{
					if (g_hostSaveTransfer.nextChunk
						>= g_hostSaveTransfer.acked[
							g_hostSaveTransfer.nextFile].size())
					{
						g_hostSaveTransfer.nextChunk = 0;
						g_hostSaveTransfer.nextFile =
							(g_hostSaveTransfer.nextFile + 1)
								% SaveSyncFileCount;
					}
					const std::size_t fileIndex = g_hostSaveTransfer.nextFile;
					const std::size_t chunkIndex = g_hostSaveTransfer.nextChunk++;
					if (!g_hostSaveTransfer.acked[fileIndex][chunkIndex])
					{
						SendSaveSyncChunk(
							g_hostSaveTransfer, fileIndex, chunkIndex);
						found = true;
						break;
					}
				}
				if (!found)
					break;
			}
			std::size_t acknowledged = 0;
			for (const auto& file : g_hostSaveTransfer.acked)
				acknowledged += std::count(file.begin(), file.end(), 1);
			g_saveSyncProgress.store(static_cast<int>(
				acknowledged * 100 / (std::max<std::size_t>)(1, attempts)));
		}

		void SendPing()
		{
			Header packet{};
			packet.type = PacketType::Ping;
			packet.sessionId = g_sessionId.load();
			packet.playerId = g_playerId.load();
			packet.sequence = g_sequence++;
			SendBytes(&packet, sizeof(packet));
		}

		void HandleDatagram(const std::uint8_t* bytes, std::size_t size)
		{
			if (size < sizeof(Header))
				return;
			const auto* header = reinterpret_cast<const Header*>(bytes);
			if (!IsValidHeader(*header, size))
				return;
			g_lastRelayPacketMs = ElapsedMilliseconds();

			if (header->type == PacketType::Welcome && header->payloadSize == sizeof(WelcomePayload))
			{
				const auto* welcome = reinterpret_cast<const WelcomePayload*>(bytes + sizeof(Header));
				if (welcome->accepted)
				{
					const bool wasConnected = g_connected.exchange(true);
					g_playerId = welcome->assignedPlayerId;
					g_sessionId = header->sessionId;
					if (!wasConnected)
						Log("Connected to relay. session=" + std::to_string(header->sessionId)
							+ ", player=" + std::to_string(welcome->assignedPlayerId) + '.');
				}
				else
				{
					g_connected = false;
					Log(std::string("Relay rejected connection: ") + welcome->message);
				}
			}
			else if (g_config.role == Role::Host
				&& header->type == PacketType::ClientCommand
				&& header->payloadSize == sizeof(ClientCommandPayload)
				&& header->playerId != g_playerId.load())
			{
				ClientCommandSnapshot snapshot{};
				snapshot.command = *reinterpret_cast<const ClientCommandPayload*>(
					bytes + sizeof(Header));
				snapshot.receivedAt = Clock::now();
				std::lock_guard lock(g_stateMutex);
				g_inboundClientCommand = snapshot;
			}
			else if (g_config.role == Role::Client
				&& header->type == PacketType::HostSnapshot
				&& header->payloadSize == sizeof(HostSnapshotPayload)
				&& header->playerId != g_playerId.load())
			{
				HostWorldSnapshot snapshot{};
				snapshot.snapshot = *reinterpret_cast<const HostSnapshotPayload*>(
					bytes + sizeof(Header));
				snapshot.receivedAt = Clock::now();
				std::lock_guard lock(g_stateMutex);
				g_inboundHostSnapshot = snapshot;
			}
			else if (header->type == PacketType::AnimationGraph
				&& header->payloadSize == sizeof(AnimationGraphPayload)
				&& header->playerId != g_playerId.load())
			{
				const auto& graph = *reinterpret_cast<const AnimationGraphPayload*>(
					bytes + sizeof(Header));
				if (graph.sequenceCount > MaxAnimationSequences
					|| graph.blendCount > MaxAnimationBlends)
				{
					return;
				}

				std::lock_guard lock(g_stateMutex);
				if (!g_inboundAnimationGraph
					|| g_inboundAnimationGraph->graph.actorId
						!= graph.actorId
					|| IsNewerSequence(graph.frameNumber,
						g_inboundAnimationGraph->graph.frameNumber))
				{
					ReceivedAnimationGraph received{};
					received.graph = graph;
					received.receivedAt = Clock::now();
					g_inboundAnimationGraph = received;
				}
			}
			else if (header->type == PacketType::ProjectileEvent
				&& header->payloadSize == sizeof(ProjectileEventPayload)
				&& header->playerId != g_playerId.load())
			{
				const auto& event = *reinterpret_cast<
					const ProjectileEventPayload*>(
						bytes + sizeof(Header));
				if (event.eventSerial == 0 || event.projectileId == 0
					|| event.actorId != header->playerId)
				{
					return;
				}
				ReceivedProjectileEvent received{};
				received.event = event;
				received.receivedAt = Clock::now();
				std::lock_guard lock(g_stateMutex);
				if (g_inboundProjectileEvents.size() >= 256)
					g_inboundProjectileEvents.pop_front();
				g_inboundProjectileEvents.push_back(received);
			}
			else if (header->type == PacketType::SharedWorldEvent
				&& header->payloadSize
					== sizeof(SharedWorldEventPayload)
				&& header->playerId != g_playerId.load())
			{
				const auto& event = *reinterpret_cast<const
					SharedWorldEventPayload*>(
						bytes + sizeof(Header));
				const bool validDirection =
					(g_config.role == Role::Host
						&& event.kind == SharedWorldEventKind::
							EnemyDamageRequest)
					|| (g_config.role == Role::Client
						&& event.kind == SharedWorldEventKind::
							EnemyAuthoritativeState)
					|| event.kind == SharedWorldEventKind::
						VentStateApplied
					|| event.kind == SharedWorldEventKind::
						InteractionUsed
					|| event.kind == SharedWorldEventKind::
						InteractionMatineeStarted
					|| event.kind == SharedWorldEventKind::
						InteractionInLondon
					|| event.kind == SharedWorldEventKind::
						InteractionTriggerUsed
					|| event.kind == SharedWorldEventKind::
						InteractionContextActionActivated
					|| event.kind == SharedWorldEventKind::
						InteractionContextActorStarted
					|| event.kind == SharedWorldEventKind::
						BreakableDestroyed;
				if (!validDirection
					|| event.eventSerial == 0
					|| event.entityKey == 0
					|| event.actorId != header->playerId)
				{
					return;
				}
				ReceivedSharedWorldEvent received{};
				received.event = event;
				received.receivedAt = Clock::now();
				std::lock_guard lock(g_stateMutex);
				if (g_inboundSharedWorldEvents.size() >= 256)
					g_inboundSharedWorldEvents.pop_front();
				g_inboundSharedWorldEvents.push_back(received);
			}
			else if (g_config.role == Role::Host
				&& header->type == PacketType::SaveSyncRequest
				&& header->payloadSize == sizeof(SaveSyncRequestPayload)
				&& header->playerId != g_playerId.load())
			{
				const auto& request = *reinterpret_cast<const
					SaveSyncRequestPayload*>(bytes + sizeof(Header));
				if (request.transferId != 0 && (request.flags & 1u) != 0
					&& (!g_hostSaveTransfer.active
						|| g_hostSaveTransfer.transferId != request.transferId))
				{
					PrepareHostSaveTransfer(request.transferId);
				}
			}
			else if (g_config.role == Role::Client
				&& header->type == PacketType::SaveSyncManifest
				&& header->payloadSize == sizeof(SaveSyncManifestPayload)
				&& header->playerId != g_playerId.load())
			{
				const auto& manifest = *reinterpret_cast<const
					SaveSyncManifestPayload*>(bytes + sizeof(Header));
				if (!g_clientSaveTransfer.active)
					HandleSaveSyncManifest(manifest);
			}
			else if (g_config.role == Role::Client
				&& header->type == PacketType::SaveSyncChunk
				&& header->payloadSize == sizeof(SaveSyncChunkPayload)
				&& header->playerId != g_playerId.load())
			{
				HandleSaveSyncChunk(*reinterpret_cast<const
					SaveSyncChunkPayload*>(bytes + sizeof(Header)));
			}
			else if (header->type == PacketType::SaveSyncAck
				&& header->payloadSize == sizeof(SaveSyncAckPayload)
				&& header->playerId != g_playerId.load())
			{
				HandleSaveSyncAck(*reinterpret_cast<const
					SaveSyncAckPayload*>(bytes + sizeof(Header)));
			}
			else if (header->type == PacketType::PeerLeft && header->payloadSize == sizeof(PeerLeftPayload))
			{
				if (g_saveSyncInProgress.load())
					SetSaveSyncStatus("PEER DISCONNECTED DURING SAVE SYNC", false, 0);
				g_requestedSaveTransferId.store(0);
				g_clientSaveTransfer = {};
				g_hostSaveTransfer = {};
				{
					std::lock_guard lock(g_stateMutex);
					g_inboundClientCommand.reset();
					g_inboundHostSnapshot.reset();
					g_inboundAnimationGraph.reset();
					g_inboundProjectileEvents.clear();
					g_inboundSharedWorldEvents.clear();
					g_outboundSharedWorldEvents.clear();
				}
				g_peerTeardownRequested = true;
				Log("Peer disconnected from relay.");
			}
		}

		void NetworkLoop()
		{
			auto nextHello = Clock::now();
			auto nextUpdate = Clock::now();
			auto nextPing = Clock::now();
			std::array<std::uint8_t, MaxDatagramSize> buffer{};

			while (g_running)
			{
				const auto now = Clock::now();
				if (g_connected && g_lastRelayPacketMs.load() != 0
					&& ElapsedMilliseconds() - g_lastRelayPacketMs.load() > 3500)
				{
					g_connected = false;
					g_playerId = 0;
					g_sessionId = 0;
					if (g_saveSyncInProgress.load())
						SetSaveSyncStatus(
							"RELAY LOST DURING SAVE SYNC", false, 0);
					g_requestedSaveTransferId.store(0);
					g_clientSaveTransfer = {};
					g_hostSaveTransfer = {};
					{
						std::lock_guard lock(g_stateMutex);
						g_inboundClientCommand.reset();
						g_inboundHostSnapshot.reset();
						g_inboundAnimationGraph.reset();
						g_inboundProjectileEvents.clear();
						g_inboundSharedWorldEvents.clear();
						g_outboundProjectileEvents.clear();
						g_outboundSharedWorldEvents.clear();
					}
					g_peerTeardownRequested = true;
					nextHello = now;
					Log("Relay heartbeat timed out; reconnecting.");
				}

				if (!g_connected && now >= nextHello)
				{
					SendHello();
					nextHello = now + std::chrono::seconds(1);
				}

				if (g_connected && now >= nextPing)
				{
					SendPing();
					nextPing = now + std::chrono::seconds(1);
				}

				PumpSaveSyncNetwork(now);

				if (g_connected && now >= nextUpdate)
				{
					std::optional<ClientCommandPayload> command;
					std::optional<HostSnapshotPayload> snapshot;
					std::optional<AnimationGraphPayload> animationGraph;
					std::deque<ProjectileEventPayload> projectileEvents;
					std::deque<SharedWorldEventPayload>
						sharedWorldEvents;
					{
						std::lock_guard lock(g_stateMutex);
						if (g_config.role == Role::Client)
							command = g_outboundClientCommand;
						else if (g_config.role == Role::Host)
							snapshot = g_outboundHostSnapshot;
						animationGraph = g_outboundAnimationGraph;
						projectileEvents.swap(
							g_outboundProjectileEvents);
						sharedWorldEvents.swap(
							g_outboundSharedWorldEvents);
					}
					if (command)
						SendClientCommand(*command);
					else if (snapshot)
						SendHostSnapshot(*snapshot);
					if (animationGraph)
						SendAnimationGraph(*animationGraph);
					for (const auto& event : projectileEvents)
						SendProjectileEvent(event);
					for (const auto& event : sharedWorldEvents)
						SendSharedWorldEvent(event);
					nextUpdate = now + std::chrono::milliseconds(1000 / g_config.sendRateHz);
				}

				for (;;)
				{
					sockaddr_in source{};
					int sourceLength = sizeof(source);
					const int received = recvfrom(g_socket, reinterpret_cast<char*>(buffer.data()),
						static_cast<int>(buffer.size()), 0, reinterpret_cast<sockaddr*>(&source), &sourceLength);
					if (received == SOCKET_ERROR)
					{
						const int error = WSAGetLastError();
						if (error != WSAEWOULDBLOCK)
							Log("recvfrom failed with WSA error " + std::to_string(error) + '.');
						break;
					}
					HandleDatagram(buffer.data(), static_cast<std::size_t>(received));
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		}

