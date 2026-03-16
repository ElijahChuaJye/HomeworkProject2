/* Start Header
*****************************************************************/
/*!
\file   server.cpp
\author Elijah Chua & Guan Shao Jun
\par    e.chua@digipen.edu, shaojun.g@digipen.edu
\date   21 Feb 2026
\brief
	This file implements a high-performance, multi-threaded TCP/IP
	server utilizing a Task Queue (Thread Pool) architecture.
	The server facilitates:
	- Client registration and session management using a thread-safe
	  std::map of user metadata.
	- Protocol-level message forwarding (REQ_ECHO/RSP_ECHO) with
	  header rewriting to maintain IP/Port transparency (The Invariant Rule).
	- Robust error handling, including ECHO_ERROR signaling for
	  unreachable targets.
	- Thread-safe console logging via mutex synchronization to prevent
	  interleaved output during concurrent client tasks.

Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "Windows.h"		// Entire Win32 API...
#include "ws2tcpip.h"		// getaddrinfo()
#include "protocol.h"

#pragma comment(lib, "ws2_32.lib")

#include <iostream>			// cout, cerr
#include <string>			// string
#include <cstdio>
#include <fstream>
#include <vector>
#include <io.h>
#include <map>
#include <filesystem>

#include "taskqueue.h"

#ifndef WINSOCK_VERSION
#define WINSOCK_VERSION     2
#endif
#define WINSOCK_SUBVERSION  2
#define MAX_STR_LEN         1000

// --- Global Configuration Variables ---
static std::string portString{};
static std::string udpPortString{};
static std::string filesPath{};

// Forward declaration of the UDP Sender thread function
void UdpSendFileThread(uint32_t clientIpNet, uint16_t clientPortNet, std::string fullPath, uint32_t sessionID, uint32_t fileLen);

/**
 * @brief Helper to ensure all bytes are sent over TCP.
 * Prevents partial sends if the OS network buffer gets full.
 */
bool sendAll(SOCKET s, const char* data, int size) {
	int totalSent = 0;
	while (totalSent < size) {
		int result = send(s, data + totalSent, size - totalSent, 0);
		if (result == SOCKET_ERROR) return false;
		totalSent += result;
	}
	return true;
}

/**
 * @brief Helper to ensure an exact number of bytes are received over TCP.
 * Prevents reading partial packets which would desync the command protocol.
 */
bool receiveAll(SOCKET s, char* buffer, int totalBytes) {
	int receivedSoFar = 0;
	while (receivedSoFar < totalBytes) {
		int res = recv(s, buffer + receivedSoFar, totalBytes - receivedSoFar, 0);
		if (res <= 0) return false;
		receivedSoFar += res;
	}
	return true;
}

// Used as a unique key for the std::map to track connected clients
struct User {
	uint32_t ip;
	uint16_t port;
	bool operator<(const User& rhs) const {
		return std::tie(ip, port) < std::tie(rhs.ip, rhs.port);
	}
};

// Thread-safe client registry
static std::map<User, SOCKET> userRegisteration;
static std::mutex mutexRegisteration;

// Mutex to prevent console output from garbling when multiple threads print simultaneously
extern std::mutex _stdoutMutex;

bool execute(SOCKET clientSocket);
void disconnect(SOCKET& listenerSocket);

int main()
{
	WSADATA wsaData{};
	if (WSAStartup(MAKEWORD(WINSOCK_VERSION, WINSOCK_SUBVERSION), &wsaData) != NO_ERROR) return 1;

	bool isManual = _isatty(_fileno(stdin));

	// Helper to clean invisible carriage returns (\r) from console input
	auto clean = [&](std::string& s) {
		s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
		s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
		while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
			s.pop_back();
		}
		};

	// --- Console Setup Phase ---
	std::cout << "Server TCP Port Number: ";
	if (!std::getline(std::cin, portString)) return 0;
	clean(portString);

	std::cout << "Server UDP Port Number: ";
	if (!std::getline(std::cin, udpPortString)) return 0;
	clean(udpPortString);

	std::cout << "Files Path: ";
	if (!std::getline(std::cin, filesPath)) return 0;
	clean(filesPath);

	// --- TCP Socket Setup ---
	addrinfo hints{};
	SecureZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	addrinfo* info = nullptr;
	if (getaddrinfo(nullptr, portString.c_str(), &hints, &info) != NO_ERROR || info == nullptr) return 1;

	// Print the Server's actual local IP address so clients know where to connect
	char host[MAX_STR_LEN];
	if (gethostname(host, MAX_STR_LEN) == 0) {
		addrinfo nameHints{}, * nameResult = nullptr;
		nameHints.ai_family = AF_INET;
		nameHints.ai_socktype = SOCK_STREAM;
		if (getaddrinfo(host, NULL, &nameHints, &nameResult) == 0) {
			if (nameResult != nullptr) {
				char serverIPAddr[INET_ADDRSTRLEN];
				sockaddr_in* ipv4 = reinterpret_cast<sockaddr_in*>(nameResult->ai_addr);

				getnameinfo(info->ai_addr, static_cast <socklen_t> (info->ai_addrlen), serverIPAddr, sizeof(serverIPAddr), nullptr, 0, NI_NUMERICHOST);
				if (inet_ntop(AF_INET, &(ipv4->sin_addr), serverIPAddr, INET_ADDRSTRLEN)) {
					std::cout << std::endl;
					std::cout << "Server IP Address: " << serverIPAddr << std::endl;
					std::cout << "Server Port Number: " << portString << std::endl;
				};
			}
			freeaddrinfo(nameResult);
		}
	}

	SOCKET listenerSocket = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
	if (listenerSocket == INVALID_SOCKET) return 1;

	if (bind(listenerSocket, info->ai_addr, static_cast<int>(info->ai_addrlen)) != NO_ERROR) {
		closesocket(listenerSocket);
		return 2;
	}
	freeaddrinfo(info);

	if (listen(listenerSocket, SOMAXCONN) != NO_ERROR) {
		closesocket(listenerSocket);
		return 3;
	}

	{
		// --- THREAD POOL ARCHITECTURE (TaskQueue) ---
		// We use a TaskQueue instead of manually spinning up a new thread for every client.
		// This keeps memory usage stable and prevents the server from crashing under heavy load.
		const auto onDisconnect = [&]() { disconnect(listenerSocket); };
		auto tq = TaskQueue<SOCKET, decltype(execute), decltype(onDisconnect)>{ 10, 20, execute, onDisconnect };

		while (listenerSocket != INVALID_SOCKET)
		{
			sockaddr clientAddress{};
			SecureZeroMemory(&clientAddress, sizeof(clientAddress));
			int clientAddressSize = sizeof(clientAddress);
			SOCKET clientSocket = accept(
				listenerSocket,
				&clientAddress,
				&clientAddressSize);

			if (clientSocket == INVALID_SOCKET) break;

			sockaddr_in* pClientAddr = reinterpret_cast<sockaddr_in*>(&clientAddress);
			char clientIP[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &(pClientAddr->sin_addr), clientIP, INET_ADDRSTRLEN);
			{
				std::lock_guard<std::mutex> lock{ _stdoutMutex };
				std::cout << std::endl << "Client IP Address: " << clientIP << std::endl;
				std::cout << "Client Port Number: " << ntohs(pClientAddr->sin_port) << std::endl;
			}

			// Push the client socket to the Task Queue to be handled by an available worker thread
			tq.produce(clientSocket);
		}
	}

	WSACleanup();
}

void disconnect(SOCKET& listenerSocket)
{
	if (listenerSocket != INVALID_SOCKET)
	{
		shutdown(listenerSocket, SD_BOTH);
		closesocket(listenerSocket);
		listenerSocket = INVALID_SOCKET;
	}
}

/**
 * @brief Worker thread execution block. Parses TCP commands and routes them accordingly.
 */
bool execute(SOCKET clientSocket)
{
	sockaddr_in senderAddr;
	int addrLen = sizeof(senderAddr);

	if (getpeername(clientSocket, (sockaddr*)&senderAddr, &addrLen) == SOCKET_ERROR) {
		std::cerr << "Cannot find sender! Aborting client session." << std::endl;
		closesocket(clientSocket);
		return true;
	}

	User myKey{ senderAddr.sin_addr.s_addr, senderAddr.sin_port };
	{
		std::lock_guard<std::mutex> lock{ mutexRegisteration };
		userRegisteration[myKey] = clientSocket;
	}

	unsigned char rawID = 0;

	auto recvAll = [&](char* target, int size) -> bool {
		int totalReceived = 0;
		while (totalReceived < size) {
			int res = recv(clientSocket, target + totalReceived, size - totalReceived, 0);
			if (res <= 0) return false;
			totalReceived += res;
		}
		return true;
		};

	while (true)
	{
		// Wrap everything in a try-catch to prevent underlying C++ Standard Library 
		// exceptions (like unmappable filenames) from violently aborting the thread.
		try {
			// Wait for a 1-byte command ID
			const int bytesReceived = recv(clientSocket, reinterpret_cast<char*>(&rawID), 1, 0);
			if (bytesReceived <= 0) break; // Client disconnected

			CMDID ID = static_cast<CMDID>(rawID);

			if (ID == REQ_LISTFILES) {
				// --- LIST FILES COMMAND ---
				std::vector<std::string> fileNames;
				uint32_t totalListLen = 0;

				std::error_code ec;
				if (std::filesystem::exists(filesPath, ec) && std::filesystem::is_directory(filesPath, ec)) {
					auto it = std::filesystem::directory_iterator(filesPath, ec);
					auto end = std::filesystem::directory_iterator();
					while (it != end && !ec) {
						if (it->is_regular_file(ec)) {
							std::string name = it->path().filename().string();
							fileNames.push_back(name);
							totalListLen += (4 + static_cast<uint32_t>(name.length()));
						}
						it.increment(ec);
					}
				}

				// We manually serialize the response into a byte vector.
				// This completely bypasses MSVC Struct Padding, ensuring the client receives
				// exactly the bytes they expect without invisible padding desyncs.
				std::vector<char> fullPacket;
				fullPacket.reserve(7 + totalListLen);

				fullPacket.push_back(static_cast<char>(RSP_LISTFILES)); // 1 Byte

				uint16_t numNet = htons(static_cast<uint16_t>(fileNames.size()));
				char* pNum = reinterpret_cast<char*>(&numNet);
				fullPacket.insert(fullPacket.end(), pNum, pNum + 2); // 2 Bytes

				uint32_t lenNet = htonl(totalListLen);
				char* pLen = reinterpret_cast<char*>(&lenNet);
				fullPacket.insert(fullPacket.end(), pLen, pLen + 4); // 4 Bytes

				for (const auto& name : fileNames) {
					uint32_t nLen = htonl(static_cast<uint32_t>(name.length()));
					char* pnLen = reinterpret_cast<char*>(&nLen);
					fullPacket.insert(fullPacket.end(), pnLen, pnLen + 4);

					// Use .c_str() to bypass strict MSVC debug iterator mismatch assertions
					const char* nameStr = name.c_str();
					fullPacket.insert(fullPacket.end(), nameStr, nameStr + name.length());
				}

				if (!sendAll(clientSocket, fullPacket.data(), static_cast<int>(fullPacket.size()))) {
					break;
				}
			}
			else if (ID == REQ_DOWNLOAD) {
				// --- DOWNLOAD COMMAND ---
				// Read headers individually to avoid struct padding desyncs
				uint32_t ipNet;
				if (!recvAll((char*)&ipNet, 4)) break;

				uint16_t portNet;
				if (!recvAll((char*)&portNet, 2)) break;

				uint32_t nameLenNet;
				if (!recvAll((char*)&nameLenNet, 4)) break;

				uint32_t nameLen = ntohl(nameLenNet);

				// Safety net: If a packet shifts, nameLen becomes gigabytes.
				// Prevent std::bad_alloc from crashing the entire server.
				if (nameLen > 10000) {
					std::cerr << "Mangled packet detected! Dropping download request." << std::endl;
					break;
				}

				std::vector<char> nameBuf(nameLen + 1, 0);
				if (!recvAll(nameBuf.data(), nameLen)) break;
				std::string requestedFile(nameBuf.data());

				std::filesystem::path fullPath = std::filesystem::path(filesPath) / requestedFile;

				std::error_code ec;
				// Ensure the file exists before attempting to open it
				if (std::filesystem::exists(fullPath, ec) && std::filesystem::is_regular_file(fullPath, ec)) {

					std::vector<char> respPacket;
					respPacket.reserve(15);
					respPacket.push_back(static_cast<char>(RSP_DOWNLOAD)); // 1 Byte

					// Reply to the IP Address that actually made the request
					respPacket.insert(respPacket.end(), reinterpret_cast<char*>(&senderAddr.sin_addr.s_addr), reinterpret_cast<char*>(&senderAddr.sin_addr.s_addr) + 4);

					// Provide the Server's designated UDP transfer port
					uint16_t outPortNet = htons(static_cast<uint16_t>(std::stoi(udpPortString)));
					char* pOutPort = reinterpret_cast<char*>(&outPortNet);
					respPacket.insert(respPacket.end(), pOutPort, pOutPort + 2);

					// Generate unique Session ID
					static uint32_t globalSessionID = 7000;
					uint32_t sessNet = htonl(globalSessionID++);
					char* pSess = reinterpret_cast<char*>(&sessNet);
					respPacket.insert(respPacket.end(), pSess, pSess + 4);

					uint32_t fileLenNet = htonl(static_cast<uint32_t>(std::filesystem::file_size(fullPath, ec)));
					char* pFileLen = reinterpret_cast<char*>(&fileLenNet);
					respPacket.insert(respPacket.end(), pFileLen, pFileLen + 4);

					sendAll(clientSocket, respPacket.data(), static_cast<int>(respPacket.size()));

					std::cout << "Starting transfer for: " << requestedFile
						<< " (" << std::filesystem::file_size(fullPath, ec) << " bytes)" << std::endl;

					// Spin up a DETACHED background thread to handle the heavy UDP lifting.
					// This frees up the TCP execute thread to immediately listen for new commands!
					std::thread udpThread(UdpSendFileThread,
						ipNet,
						portNet,
						fullPath.string(),
						globalSessionID - 1,
						ntohl(fileLenNet));
					udpThread.detach();
				}
				else {
					// File not found error
					char errID = DOWNLOAD_ERROR;
					sendAll(clientSocket, &errID, 1);
				}
			}
			else {
				// --- QUIT / UNKNOWN COMMAND ---
				CMDID error = REQ_QUIT;
				{
					std::lock_guard<std::mutex> lock{ _stdoutMutex };
					std::cout << "Graceful Shutdown" << std::endl;
				}
				sendAll(clientSocket, (char*)&error, 1);
				break;
			}
		}
		catch (const std::exception& e) {
			std::cerr << "\n[CRITICAL SERVER EXCEPTION] " << e.what() << ". Terminating thread.\n";
			break;
		}
		catch (...) {
			std::cerr << "\n[CRITICAL SERVER EXCEPTION] Unknown failure. Terminating thread.\n";
			break;
		}
	}

	// Cleanup client registry before shutting down the socket
	{
		std::lock_guard<std::mutex> lock{ mutexRegisteration };
		userRegisteration.erase(myKey);
	}

	shutdown(clientSocket, SD_BOTH);
	closesocket(clientSocket);
	return true;
}

/**
 * @brief Represents a single unacknowledged packet in the Sliding Window.
 */
struct PacketSlot {
	uint32_t offset;
	uint32_t dataLen;
	std::vector<char> buffer;
	ULONGLONG sendTime;
	bool acked;
};

/**
 * @brief Asynchronous thread that handles high-speed Selective Repeat UDP transmission.
 */
void UdpSendFileThread(uint32_t clientIpNet, uint16_t clientPortNet, std::string fullPath, uint32_t sessionID, uint32_t fileLen) {
	std::ifstream file(fullPath, std::ios::binary);
	if (!file.is_open()) return;

	SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (udpSocket == INVALID_SOCKET) return;

	// =========================================================================
	// EPHEMERAL PORT ASSIGNMENT
	// By *not* calling bind(9999), Windows OS assigns a random "Ephemeral Port" 
	// to this specific thread. This guarantees that multiple clients downloading
	// simultaneously will never experience "ACK Stealing" or socket starvation.
	// =========================================================================

	sockaddr_in targetAddr{};
	targetAddr.sin_family = AF_INET;
	targetAddr.sin_addr.s_addr = clientIpNet;
	targetAddr.sin_port = clientPortNet;

	// =========================================================================
	// UDP DEMULTIPLEXING
	// Calling connect() on a UDP socket assigns a strict 4-tuple filter 
	// (Local IP, Local Port, Remote IP, Remote Port) to the socket inside the OS.
	// This forces Windows to route ACKs securely back to this thread.
	// =========================================================================
	if (connect(udpSocket, (sockaddr*)&targetAddr, sizeof(targetAddr)) == SOCKET_ERROR) {
		std::cerr << "UDP Thread: Connect failed." << std::endl;
		closesocket(udpSocket);
		return;
	}

	const int MAX_DATA_PAYLOAD = 1024;
	const size_t WINDOW_SIZE = 10; // Number of unacknowledged packets allowed in flight

	uint32_t baseOffset = 0; // Tracks the start of the sliding window
	uint32_t nextOffset = 0; // Tracks the next byte to read from disk
	int timeoutCounter = 0;

	// The actual Sliding Window data structure
	std::map<uint32_t, PacketSlot> window;

	while (baseOffset < fileLen) {

		// ---------------------------------------------------------
		// PHASE 1: FILL THE WINDOW
		// Reads and sends new packets asynchronously until we hit 
		// the WINDOW_SIZE limit (Pipeline is full).
		// ---------------------------------------------------------
		while (window.size() < WINDOW_SIZE && nextOffset < fileLen) {
			file.seekg(nextOffset);
			std::vector<char> fileBuffer(MAX_DATA_PAYLOAD);
			file.read(fileBuffer.data(), MAX_DATA_PAYLOAD);
			uint32_t bytesRead = static_cast<uint32_t>(file.gcount());

			if (bytesRead == 0) break;

			UdpDataHeader header;
			header.sessionID = htonl(sessionID);
			header.fileLen = htonl(fileLen);
			header.fileOffset = htonl(nextOffset);
			header.dataLen = htonl(bytesRead);

			std::vector<char> packet;
			packet.reserve(sizeof(UdpDataHeader) + bytesRead);
			char* pHeader = reinterpret_cast<char*>(&header);
			packet.insert(packet.end(), pHeader, pHeader + sizeof(UdpDataHeader));
			packet.insert(packet.end(), fileBuffer.begin(), fileBuffer.begin() + bytesRead);

			// Save packet to the tracking map with current timestamp
			window[nextOffset] = { nextOffset, bytesRead, packet, GetTickCount64(), false };

			// Send to client without blocking
			sendto(udpSocket, packet.data(), static_cast<int>(packet.size()), 0, (sockaddr*)&targetAddr, sizeof(targetAddr));
			nextOffset += bytesRead;
		}

		// ---------------------------------------------------------
		// PHASE 2: WAIT FOR ACKS (Non-Blocking)
		// Pauses for a maximum of 10ms to check if the client replied.
		// ---------------------------------------------------------
		fd_set readFds;
		FD_ZERO(&readFds);
		FD_SET(udpSocket, &readFds);
		timeval timeout;
		timeout.tv_sec = 0;
		timeout.tv_usec = 10000; // 10 milliseconds

		int selRes = select(0, &readFds, NULL, NULL, &timeout);
		if (selRes > 0) {
			timeoutCounter = 0;

			UdpAckHeader ack;
			sockaddr_in fromAddr;
			int fromLen = sizeof(fromAddr);
			int recvBytes = recvfrom(udpSocket, reinterpret_cast<char*>(&ack), sizeof(UdpAckHeader), 0, (sockaddr*)&fromAddr, &fromLen);

			if (recvBytes == sizeof(UdpAckHeader)) {
				uint32_t ackedOffset = ntohl(ack.seqNum); // Match ACK against specific packet
				if (window.find(ackedOffset) != window.end()) {
					window[ackedOffset].acked = true;
				}
			}
		}
		else {
			// Safety timeout: If client vanishes mid-download, kill the thread
			timeoutCounter++;
			if (timeoutCounter > 1000) { // Approx 10 seconds of pure silence
				std::cerr << "UDP Thread: Client disconnected or max timeouts reached. Aborting." << std::endl;
				break;
			}
		}

		// ---------------------------------------------------------
		// PHASE 3: SLIDE THE WINDOW
		// If the oldest packet in the map has been ACKed, erase it.
		// This shrinks window.size(), allowing Phase 1 to read a new packet.
		// ---------------------------------------------------------
		while (!window.empty() && window.begin()->second.acked) {
			baseOffset += window.begin()->second.dataLen;
			window.erase(window.begin());
		}

		// ---------------------------------------------------------
		// PHASE 4: SELECTIVE RETRANSMISSION
		// Iterates through all un-ACKed packets. If 500ms has elapsed
		// since a specific packet was sent, assume it was dropped by the
		// router and re-send ONLY that specific packet.
		// ---------------------------------------------------------
		ULONGLONG currentTime = GetTickCount64();
		for (auto& pair : window) {
			if (!pair.second.acked && (currentTime - pair.second.sendTime > 500)) {
				sendto(udpSocket, pair.second.buffer.data(), static_cast<int>(pair.second.buffer.size()), 0, (sockaddr*)&targetAddr, sizeof(targetAddr));
				pair.second.sendTime = currentTime; // Reset the timeout
			}
		}
	}

	std::cout << "Transfer complete for Session " << sessionID << std::endl;
	closesocket(udpSocket);
}