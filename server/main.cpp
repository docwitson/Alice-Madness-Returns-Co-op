#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>

#include "../src/Coop/Protocol.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace
{
	using namespace AliceCoopProtocol;
	using Clock = std::chrono::steady_clock;

	std::atomic_bool g_running = true;
	std::mutex g_logMutex;
	std::ofstream g_logFile;

	struct ClientSlot
	{
		bool occupied = false;
		Role role = Role::Unknown;
		std::uint32_t playerId = 0;
		std::uint32_t processId = 0;
		sockaddr_in endpoint{};
		Clock::time_point lastSeen{};
		std::uint64_t receivedStates = 0;
		std::uint64_t receivedCommands = 0;
		std::uint64_t receivedSnapshots = 0;
		std::uint64_t receivedProjectiles = 0;
		std::uint64_t receivedWorldEvents = 0;
		std::uint64_t receivedSavePackets = 0;
		std::string label;
		std::string mapName;
	};

	std::string Timestamp()
	{
		SYSTEMTIME time{};
		GetLocalTime(&time);
		std::ostringstream stream;
		stream << std::setfill('0')
			<< std::setw(2) << time.wHour << ':'
			<< std::setw(2) << time.wMinute << ':'
			<< std::setw(2) << time.wSecond << '.'
			<< std::setw(3) << time.wMilliseconds;
		return stream.str();
	}

	void Log(const std::string& message)
	{
		std::lock_guard lock(g_logMutex);
		const std::string line = '[' + Timestamp() + "] " + message;
		std::cout << line << std::endl;
		if (g_logFile)
		{
			g_logFile << line << std::endl;
			g_logFile.flush();
		}
	}

	BOOL WINAPI ConsoleHandler(DWORD signal)
	{
		if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_BREAK_EVENT)
		{
			g_running = false;
			return TRUE;
		}
		return FALSE;
	}

	std::string EndpointToString(const sockaddr_in& endpoint)
	{
		char address[INET_ADDRSTRLEN]{};
		InetNtopA(AF_INET, &endpoint.sin_addr, address, sizeof(address));
		std::ostringstream stream;
		stream << address << ':' << ntohs(endpoint.sin_port);
		return stream.str();
	}

	bool SameEndpoint(const sockaddr_in& left, const sockaddr_in& right)
	{
		return left.sin_family == right.sin_family
			&& left.sin_port == right.sin_port
			&& left.sin_addr.S_un.S_addr == right.sin_addr.S_un.S_addr;
	}

	ClientSlot& SlotForRole(std::array<ClientSlot, 2>& clients, Role role)
	{
		return clients[role == Role::Host ? 0 : 1];
	}

	ClientSlot* FindClient(std::array<ClientSlot, 2>& clients, const sockaddr_in& endpoint,
		std::uint32_t playerId)
	{
		for (auto& client : clients)
		{
			if (client.occupied && client.playerId == playerId && SameEndpoint(client.endpoint, endpoint))
				return &client;
		}
		return nullptr;
	}

	bool SendBytes(SOCKET socket, const sockaddr_in& endpoint, const void* bytes, int size)
	{
		return sendto(socket, static_cast<const char*>(bytes), size, 0,
			reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) == size;
	}

	void SendWelcome(SOCKET socket, const sockaddr_in& endpoint, std::uint32_t sessionId,
		std::uint32_t sequence, bool accepted, std::uint32_t playerId, const char* message)
	{
		WelcomePayload payload{};
		payload.assignedPlayerId = playerId;
		payload.accepted = accepted ? 1 : 0;
		strncpy_s(payload.message, message, _TRUNCATE);
		auto packet = MakePacket(PacketType::Welcome, sessionId, playerId, sequence, payload);
		SendBytes(socket, endpoint, &packet, sizeof(packet));
	}

	void NotifyPeerLeft(SOCKET socket, const std::array<ClientSlot, 2>& clients,
		std::uint32_t sessionId, const ClientSlot& departing)
	{
		PeerLeftPayload payload{};
		payload.playerId = departing.playerId;
		payload.role = departing.role;
		auto packet = MakePacket(PacketType::PeerLeft, sessionId, 0, 0, payload);
		for (const auto& client : clients)
		{
			if (client.occupied && client.playerId != departing.playerId)
				SendBytes(socket, client.endpoint, &packet, sizeof(packet));
		}
	}

	std::uint32_t MakeSessionId()
	{
		std::random_device random;
		const std::uint32_t value = (static_cast<std::uint32_t>(random()) << 16) ^ random();
		return value == 0 ? 1 : value;
	}

	int SelfTest()
	{
		ClientCommandPayload command{};
		strncpy_s(command.desiredState.mapName, "Chapter1_W1_P", _TRUNCATE);
		command.desiredState.actorId = 2;
		auto commandPacket = MakePacket(PacketType::ClientCommand, 123, 2, 99, command);
		HostSnapshotPayload snapshot{};
		snapshot.hostState.actorId = 1;
		snapshot.clientState.actorId = 2;
		auto snapshotPacket = MakePacket(PacketType::HostSnapshot, 123, 1, 100, snapshot);
		AnimationGraphPayload animationGraph{};
		animationGraph.frameNumber = 7;
		animationGraph.actorId = 1;
		animationGraph.sequenceCount = 1;
		strncpy_s(animationGraph.sequences[0].sequenceName,
			"AliceW_Jump", _TRUNCATE);
		auto animationPacket = MakePacket(PacketType::AnimationGraph,
			123, 1, 101, animationGraph);
		ProjectileEventPayload projectile{};
		projectile.eventSerial = 8;
		projectile.actorId = 2;
		projectile.projectileId = 4;
		projectile.kind = ProjectileEventKind::ClockBombSpawn;
		auto projectilePacket = MakePacket(PacketType::ProjectileEvent,
			123, 2, 102, projectile);
		SharedWorldEventPayload worldEvent{};
		worldEvent.eventSerial = 9;
		worldEvent.actorId = 2;
		worldEvent.originActorId = 2;
		worldEvent.entityKey = 0x123456789abcdef0ull;
		worldEvent.kind =
			SharedWorldEventKind::EnemyDamageRequest;
		worldEvent.damage = 7;
		auto worldEventPacket = MakePacket(
			PacketType::SharedWorldEvent,
			123, 2, 103, worldEvent);
		SaveSyncChunkPayload saveChunk{};
		saveChunk.transferId = 77;
		saveChunk.kind = SaveSyncFileKind::Checkpoint;
		saveChunk.chunkCount = 1;
		saveChunk.dataSize = 3;
		auto saveChunkPacket = MakePacket(
			PacketType::SaveSyncChunk, 123, 1, 104, saveChunk);
		const bool valid = IsValidHeader(commandPacket.header, sizeof(commandPacket))
			&& commandPacket.header.payloadSize == sizeof(ClientCommandPayload)
			&& commandPacket.header.type == PacketType::ClientCommand
			&& IsValidHeader(snapshotPacket.header, sizeof(snapshotPacket))
			&& snapshotPacket.header.payloadSize == sizeof(HostSnapshotPayload)
			&& snapshotPacket.header.type == PacketType::HostSnapshot
			&& IsValidHeader(animationPacket.header, sizeof(animationPacket))
			&& animationPacket.header.payloadSize == sizeof(AnimationGraphPayload)
			&& animationPacket.header.type == PacketType::AnimationGraph
			&& IsValidHeader(projectilePacket.header,
				sizeof(projectilePacket))
			&& projectilePacket.header.payloadSize
				== sizeof(ProjectileEventPayload)
			&& projectilePacket.header.type == PacketType::ProjectileEvent
			&& IsValidHeader(worldEventPacket.header,
				sizeof(worldEventPacket))
			&& worldEventPacket.header.payloadSize
				== sizeof(SharedWorldEventPayload)
			&& worldEventPacket.header.type
				== PacketType::SharedWorldEvent
			&& IsValidHeader(saveChunkPacket.header,
				sizeof(saveChunkPacket))
			&& saveChunkPacket.header.payloadSize
				== sizeof(SaveSyncChunkPayload)
			&& saveChunkPacket.header.type
				== PacketType::SaveSyncChunk;
		std::cout << (valid ? "AliceCoop protocol self-test passed." : "AliceCoop protocol self-test failed.")
			<< std::endl;
		return valid ? 0 : 1;
	}
}

int wmain(int argc, wchar_t** argv)
{
	std::wstring bindAddress = L"127.0.0.1";
	std::uint16_t port = AliceCoopProtocol::DefaultPort;
	std::filesystem::path logDirectory = L"logs";

	for (int i = 1; i < argc; ++i)
	{
		const std::wstring argument = argv[i];
		if (argument == L"--self-test")
			return SelfTest();
		if (argument == L"--bind" && i + 1 < argc)
			bindAddress = argv[++i];
		else if (argument == L"--port" && i + 1 < argc)
			port = static_cast<std::uint16_t>(std::wcstoul(argv[++i], nullptr, 10));
		else if (argument == L"--log-dir" && i + 1 < argc)
			logDirectory = argv[++i];
	}

	std::error_code filesystemError;
	std::filesystem::create_directories(logDirectory, filesystemError);
	const auto logPath = logDirectory / L"AliceCoopServer.log";
	g_logFile.open(logPath, std::ios::app);
	SetConsoleCtrlHandler(ConsoleHandler, TRUE);
	SetConsoleTitleW(L"AliceCoop relay server");

	WSADATA winsock{};
	if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0)
	{
		Log("ERROR: WSAStartup failed.");
		return 1;
	}

	const SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (socketHandle == INVALID_SOCKET)
	{
		Log("ERROR: socket() failed.");
		WSACleanup();
		return 1;
	}

	sockaddr_in bindEndpoint{};
	bindEndpoint.sin_family = AF_INET;
	bindEndpoint.sin_port = htons(port);
	if (InetPtonW(AF_INET, bindAddress.c_str(), &bindEndpoint.sin_addr) != 1)
	{
		Log("ERROR: invalid --bind address.");
		closesocket(socketHandle);
		WSACleanup();
		return 1;
	}

	if (bind(socketHandle, reinterpret_cast<const sockaddr*>(&bindEndpoint), sizeof(bindEndpoint)) != 0)
	{
		Log("ERROR: bind() failed with WSA error " + std::to_string(WSAGetLastError()) + '.');
		closesocket(socketHandle);
		WSACleanup();
		return 1;
	}

	const std::uint32_t sessionId = MakeSessionId();
	Log("AliceCoop relay protocol v"
		+ std::to_string(AliceCoopProtocol::Version)
		+ " started on " + EndpointToString(bindEndpoint)
		+ ", session=" + std::to_string(sessionId) + '.');
	Log("Waiting for HOST and CLIENT. Ctrl+C stops the server.");

	std::array<ClientSlot, 2> clients{};
	auto lastSummary = Clock::now();
	std::array<std::uint8_t, AliceCoopProtocol::MaxDatagramSize> buffer{};

	while (g_running)
	{
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(socketHandle, &readSet);
		timeval timeout{ 0, 100000 };
		const int ready = select(0, &readSet, nullptr, nullptr, &timeout);

		if (ready > 0 && FD_ISSET(socketHandle, &readSet))
		{
			sockaddr_in source{};
			int sourceLength = sizeof(source);
			const int received = recvfrom(socketHandle, reinterpret_cast<char*>(buffer.data()),
				static_cast<int>(buffer.size()), 0, reinterpret_cast<sockaddr*>(&source), &sourceLength);
			if (received >= static_cast<int>(sizeof(Header)))
			{
				const auto* header = reinterpret_cast<const Header*>(buffer.data());
				if (!IsValidHeader(*header, received))
				{
					Log("WARN: rejected malformed packet from " + EndpointToString(source) + '.');
					continue;
				}

				const auto now = Clock::now();
				if (header->type == PacketType::Hello && header->payloadSize == sizeof(HelloPayload))
				{
					const auto* hello = reinterpret_cast<const HelloPayload*>(buffer.data() + sizeof(Header));
					if (hello->role != Role::Host && hello->role != Role::Client)
					{
						SendWelcome(socketHandle, source, sessionId, header->sequence, false, 0, "Role must be host or client.");
						continue;
					}

					auto& slot = SlotForRole(clients, hello->role);
					const bool slotExpired = slot.occupied
						&& now - slot.lastSeen > std::chrono::seconds(5);
					if (slot.occupied && !slotExpired && !SameEndpoint(slot.endpoint, source))
					{
						SendWelcome(socketHandle, source, sessionId, header->sequence, false, 0, "Requested role is already connected.");
						Log("WARN: rejected duplicate role from " + EndpointToString(source) + '.');
						continue;
					}

					if (!slot.occupied || slotExpired || !SameEndpoint(slot.endpoint, source))
					{
						slot = {};
						slot.occupied = true;
						slot.role = hello->role;
						slot.playerId = hello->role == Role::Host ? 1u : 2u;
						slot.processId = hello->processId;
						slot.endpoint = source;
						slot.label.assign(hello->label, strnlen_s(hello->label, sizeof(hello->label)));
						Log(std::string(slot.role == Role::Host ? "HOST" : "CLIENT")
							+ " connected from " + EndpointToString(source)
							+ ", pid=" + std::to_string(slot.processId) + '.');
					}
					slot.lastSeen = now;
					SendWelcome(socketHandle, source, sessionId, header->sequence, true, slot.playerId, "Connected.");
				}
				else
				{
					ClientSlot* sender = FindClient(clients, source, header->playerId);
					if (!sender || (header->sessionId != 0 && header->sessionId != sessionId))
						continue;
					sender->lastSeen = now;

					if (header->type == PacketType::State && header->payloadSize == sizeof(PlayerStatePayload))
					{
						++sender->receivedStates;
						const auto* state = reinterpret_cast<const PlayerStatePayload*>(
							buffer.data() + sizeof(Header));
						const std::string mapName(state->mapName,
							strnlen_s(state->mapName, sizeof(state->mapName)));
						if (mapName != sender->mapName)
						{
							sender->mapName = mapName;
							Log(std::string(sender->role == Role::Host ? "HOST" : "CLIENT")
								+ " active map=" + (mapName.empty() ? "<empty>" : mapName) + '.');
						}
						for (const auto& destination : clients)
						{
							if (destination.occupied && destination.playerId != sender->playerId)
								SendBytes(socketHandle, destination.endpoint, buffer.data(), received);
						}
					}
					else if (header->type == PacketType::ClientCommand
						&& header->payloadSize == sizeof(ClientCommandPayload)
						&& sender->role == Role::Client)
					{
						++sender->receivedCommands;
						const auto* command = reinterpret_cast<const ClientCommandPayload*>(
							buffer.data() + sizeof(Header));
						const std::string mapName(command->desiredState.mapName,
							strnlen_s(command->desiredState.mapName,
								sizeof(command->desiredState.mapName)));
						if (mapName != sender->mapName)
						{
							sender->mapName = mapName;
							Log("CLIENT active map="
								+ (mapName.empty() ? std::string("<empty>") : mapName) + '.');
						}
						const auto& host = clients[0];
						if (host.occupied)
							SendBytes(socketHandle, host.endpoint, buffer.data(), received);
					}
					else if (header->type == PacketType::HostSnapshot
						&& header->payloadSize == sizeof(HostSnapshotPayload)
						&& sender->role == Role::Host)
					{
						++sender->receivedSnapshots;
						const auto* snapshot = reinterpret_cast<const HostSnapshotPayload*>(
							buffer.data() + sizeof(Header));
						const std::string mapName(snapshot->hostState.mapName,
							strnlen_s(snapshot->hostState.mapName,
								sizeof(snapshot->hostState.mapName)));
						if (mapName != sender->mapName)
						{
							sender->mapName = mapName;
							Log("HOST active map="
								+ (mapName.empty() ? std::string("<empty>") : mapName) + '.');
						}
						const auto& client = clients[1];
						if (client.occupied)
							SendBytes(socketHandle, client.endpoint, buffer.data(), received);
					}
					else if (header->type == PacketType::AnimationGraph
						&& header->payloadSize == sizeof(AnimationGraphPayload))
					{
						for (const auto& destination : clients)
						{
							if (destination.occupied
								&& destination.playerId != sender->playerId)
							{
								SendBytes(socketHandle, destination.endpoint,
									buffer.data(), received);
							}
						}
					}
					else if (header->type == PacketType::ProjectileEvent
						&& header->payloadSize
							== sizeof(ProjectileEventPayload))
					{
						const auto* event = reinterpret_cast<
							const ProjectileEventPayload*>(
								buffer.data() + sizeof(Header));
						if (event->actorId != sender->playerId
							|| event->eventSerial == 0
							|| event->projectileId == 0)
						{
							continue;
						}
						++sender->receivedProjectiles;
						for (const auto& destination : clients)
						{
							if (destination.occupied
								&& destination.playerId
									!= sender->playerId)
							{
								SendBytes(socketHandle,
									destination.endpoint,
									buffer.data(), received);
							}
						}
					}
					else if (header->type
							== PacketType::SharedWorldEvent
						&& header->payloadSize
							== sizeof(SharedWorldEventPayload))
					{
						const auto* event = reinterpret_cast<const
							SharedWorldEventPayload*>(
								buffer.data() + sizeof(Header));
						const bool validDirection =
							(sender->role == Role::Client
								&& event->kind
									== SharedWorldEventKind::
										EnemyDamageRequest)
							|| (sender->role == Role::Host
								&& event->kind
									== SharedWorldEventKind::
										EnemyAuthoritativeState)
							|| event->kind
								== SharedWorldEventKind::
									VentStateApplied
							|| event->kind
								== SharedWorldEventKind::
									InteractionUsed
							|| event->kind
								== SharedWorldEventKind::
									InteractionMatineeStarted
							|| event->kind
								== SharedWorldEventKind::
									InteractionInLondon
							|| event->kind
								== SharedWorldEventKind::
									InteractionTriggerUsed
							|| event->kind
								== SharedWorldEventKind::
									InteractionContextActionActivated
							|| event->kind
								== SharedWorldEventKind::
									InteractionContextActorStarted
							|| event->kind
								== SharedWorldEventKind::
									BreakableDestroyed;
						if (!validDirection
							|| event->actorId != sender->playerId
							|| event->eventSerial == 0
							|| event->entityKey == 0)
						{
							continue;
						}
						++sender->receivedWorldEvents;
						for (const auto& destination : clients)
						{
							if (destination.occupied
								&& destination.playerId
									!= sender->playerId)
							{
								SendBytes(socketHandle,
									destination.endpoint,
									buffer.data(), received);
							}
						}
					}
					else if ((header->type == PacketType::SaveSyncRequest
							&& header->payloadSize == sizeof(SaveSyncRequestPayload)
							&& sender->role == Role::Client)
						|| (header->type == PacketType::SaveSyncManifest
							&& header->payloadSize == sizeof(SaveSyncManifestPayload)
							&& sender->role == Role::Host)
						|| (header->type == PacketType::SaveSyncChunk
							&& header->payloadSize == sizeof(SaveSyncChunkPayload)
							&& sender->role == Role::Host)
						|| (header->type == PacketType::SaveSyncAck
							&& header->payloadSize == sizeof(SaveSyncAckPayload)))
					{
						++sender->receivedSavePackets;
						const auto& destination = sender->role == Role::Host
							? clients[1] : clients[0];
						if (destination.occupied)
							SendBytes(socketHandle, destination.endpoint,
								buffer.data(), received);
					}
					else if (header->type == PacketType::Ping)
					{
						Header pong = *header;
						pong.type = PacketType::Pong;
						pong.sessionId = sessionId;
						SendBytes(socketHandle, source, &pong, sizeof(pong));
					}
				}
			}
		}

		const auto now = Clock::now();
		for (auto& client : clients)
		{
			if (client.occupied && now - client.lastSeen > std::chrono::seconds(5))
			{
				const ClientSlot departing = client;
				Log(std::string(departing.role == Role::Host ? "HOST" : "CLIENT")
					+ " timed out after states=" + std::to_string(departing.receivedStates)
					+ ", commands=" + std::to_string(departing.receivedCommands)
					+ ", snapshots=" + std::to_string(departing.receivedSnapshots)
					+ ", projectiles=" + std::to_string(
						departing.receivedProjectiles)
					+ ", worldEvents=" + std::to_string(
						departing.receivedWorldEvents) + '.');
				client = {};
				NotifyPeerLeft(socketHandle, clients, sessionId, departing);
			}
		}

		if (now - lastSummary >= std::chrono::seconds(2))
		{
			std::ostringstream summary;
			summary << "status:";
			for (std::size_t index = 0; index < clients.size(); ++index)
			{
				const auto& client = clients[index];
				summary << ' ' << (index == 0 ? "host" : "client") << '=';
				if (client.occupied)
				{
					summary << "online(states=" << client.receivedStates;
					if (client.role == Role::Host)
						summary << ", snapshots=" << client.receivedSnapshots;
					else
						summary << ", commands=" << client.receivedCommands;
					summary << ", projectiles="
						<< client.receivedProjectiles;
					summary << ", worldEvents="
						<< client.receivedWorldEvents;
					summary << ')';
				}
				else
					summary << "waiting";
			}
			Log(summary.str());
			lastSummary = now;
		}
	}

	Log("Server stopping.");
	closesocket(socketHandle);
	WSACleanup();
	return 0;
}
