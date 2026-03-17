/* Start Header
*****************************************************************/
/*!
\file   client.cpp
\author Elijah Chua & Guan Shao Jun
\par    e.chua@digipen.edu, shaojun.g@digipen.edu
\date   16 March 2026
\brief
	This file implements a multi-threaded TCP/IP client designed to
	interact with a custom echo server. Key features include:
	- Asynchronous message reception via a dedicated receiver thread.
	- Implementation of an auto-responder state machine that replies
	  to REQ_ECHO (0x02) with RSP_ECHO (0x05) to satisfy protocol invariants.
	- Network Byte Order (Big-Endian) management for IP addresses,
	  ports, and message lengths.
	- Support for interactive commands (/e for echo, /l for list, /q for quit).

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
#include "winsock2.h"		// ...or Winsock alone
#include "ws2tcpip.h"		// getaddrinfo()
#include "protocol.h"

#pragma comment(lib, "ws2_32.lib") // Tell linker to include Winsock library

#include <iostream>			// cout, cerr
#include <string>			// string
#include <vector>
#include <fstream>
#include <cctype>
#include <io.h>
#include <thread>
#include <mutex>
#include <filesystem>
#include <set>

// --- Global Configuration Variables ---
static std::string serverUdpPort{};
static std::string clientUdpPort{};
static std::string downloadPath{};
static std::string lastRequestedFilename{};

// Mutex to prevent multiple threads from printing to the console at the exact same time
// which would result in garbled text.
std::mutex _stdoutMutex;

#ifndef WINSOCK_VERSION
#define WINSOCK_VERSION     2
#endif

#define WINSOCK_SUBVERSION  2
#define MAX_STR_LEN         1000

// Forward declaration of the UDP Receiver thread function
void UdpReceiveFileThread(uint32_t serverIpNet, uint16_t serverPortNet, uint32_t sessionID, uint32_t fileLen, std::string filename);

/**
 * @brief Helper to ensure all bytes are sent over TCP.
 * TCP streams can fragment, so a single send() might not send the whole buffer.
 */
bool sendAll(SOCKET s, const char* data, int size) {
	int totalSent = 0;
	while (totalSent < size) {
		int result = send(s, data + totalSent, size - totalSent, 0);
		if (result == SOCKET_ERROR) {
			return false; // Connection broke
		}
		totalSent += result;
	}
	return true;
}

/**
 * @brief Helper to ensure exact number of bytes are received over TCP.
 * Prevents reading partial packets which would desync the protocol.
 */
bool recvAll(SOCKET s, char* buffer, int size) {
	int received = 0;
	while (received < size) {
		int res = recv(s, buffer + received, size - received, 0);
		if (res <= 0) return false; // Graceful close or error
		received += res;
	}
	return true;
}

/**
 * @brief Parses user input from the console and sends the corresponding TCP command.
 * Handles /q (quit), /l (list files), and /d (download).
 */
bool HandleMessage(SOCKET source, std::string const& msg) {
	if (msg == "/q") {
		// --- QUIT COMMAND ---
		char id = static_cast<char>(REQ_QUIT);
		{
			std::lock_guard<std::mutex> lock{ _stdoutMutex };
			std::cout << "disconnection..." << std::endl;
		}
		sendAll(source, &id, 1);
		return false; // Returning false breaks the main input loop
	}
	else if (msg.length() >= 2 && msg.substr(0, 2) == "/l") {
		// --- LIST COMMAND ---
		char id = static_cast<char>(REQ_LISTFILES);
		if (!sendAll(source, &id, 1)) {
			return false;
		}
		return true;
	}
	else if (msg.length() >= 3 && msg.substr(0, 2) == "/d") {
		// --- DOWNLOAD COMMAND ---
		// Expected format: /d 192.168.1.3:9010 filename.ext
		std::string content = msg.substr(3);
		size_t colonPos = content.find(':');
		size_t spacePos = content.find(' ');
		if (colonPos == std::string::npos || spacePos == std::string::npos || spacePos < colonPos) {
			std::lock_guard<std::mutex> lock{ _stdoutMutex };
			std::cout << "[ERROR] Invalid download format!" << std::endl;
			std::cout << "Usage: /d <Your_IP>:<Your_UDP_Port> <filename>" << std::endl;
			std::cout << "Example: /d 192.168.43.10:9000 testfile.txt" << std::endl;
			return true;
		}
		// Validate the format
		try {
			std::string ipString = content.substr(0, colonPos);
			std::string portString = content.substr(colonPos + 1, spacePos - colonPos - 1);
			std::string filename = content.substr(spacePos + 1);

			//If the strings are empty
			if (ipString.empty() || portString.empty() || filename.empty()) {
				throw std::runtime_error("Missing fields");
			}

			// Save globally so the background thread knows what to name the file once the server accepts
			lastRequestedFilename = filename;

			// We manually pack the bytes into a vector to strictly avoid C++ struct padding issues
			std::vector<char> fullPacket;
			fullPacket.reserve(11 + filename.length());

			// 1. Command ID (1 byte)
			fullPacket.push_back(static_cast<char>(REQ_DOWNLOAD));

			// 2. IP Address (4 bytes, Network Byte Order)
			uint32_t ipNet;
			inet_pton(AF_INET, ipString.c_str(), &ipNet);
			char* pIp = reinterpret_cast<char*>(&ipNet);
			fullPacket.insert(fullPacket.end(), pIp, pIp + 4);

			// 3. Port Number (2 bytes, Network Byte Order)
			uint16_t portNet = htons(static_cast<unsigned short>(std::stoi(portString)));
			char* pPort = reinterpret_cast<char*>(&portNet);
			fullPacket.insert(fullPacket.end(), pPort, pPort + 2);

			// 4. Filename Length (4 bytes, Network Byte Order)
			uint32_t nameLenNet = htonl(static_cast<unsigned int>(filename.length()));
			char* pNameLen = reinterpret_cast<char*>(&nameLenNet);
			fullPacket.insert(fullPacket.end(), pNameLen, pNameLen + 4);

			// 5. Actual Filename String
			fullPacket.insert(fullPacket.end(), filename.begin(), filename.end());

			if (!sendAll(source, fullPacket.data(), static_cast<int>(fullPacket.size()))) {
				return false;
			}
		}
		catch (const std::exception&) {
			std::lock_guard<std::mutex> lock{ _stdoutMutex };
			std::cout << "[ERROR] Could not parse download command. Please check your IP and Port." << std::endl;
			return true;
		}
	}
	else {
		// Invalid command fallback
		std::lock_guard<std::mutex> lock{ _stdoutMutex };
		std::cout << "Invalid command! Use /l to list files, or /d <IP>:<UDP_Port> <filename>" << std::endl;
		return true;
	}
	return true;
}

/**
 * @brief Infinite loop run by the background TCP thread to catch server responses.
 */
bool ReceiveFromServer(SOCKET source) {
	char id;
	// Block until a 1-byte Command ID arrives
	if (recv(source, &id, 1, 0) <= 0) {
		return false; // Server died or closed
	}

	if (id == REQ_QUIT) {
		// Server requested graceful shutdown
		{
			std::lock_guard<std::mutex> lock{ _stdoutMutex };
			std::cout << "disconnection..." << std::endl;
		}
		return false;
	}
	else if (id == RSP_DOWNLOAD) {
		// --- SERVER ACCEPTED DOWNLOAD ---
		// Manually unpack the response to avoid padding issues
		uint32_t serverIpNet;
		if (!recvAll(source, reinterpret_cast<char*>(&serverIpNet), 4)) return false;

		uint16_t serverPortNet;
		if (!recvAll(source, reinterpret_cast<char*>(&serverPortNet), 2)) return false;

		uint32_t sessionIDNet;
		if (!recvAll(source, reinterpret_cast<char*>(&sessionIDNet), 4)) return false;

		uint32_t fileLenNet;
		if (!recvAll(source, reinterpret_cast<char*>(&fileLenNet), 4)) return false;

		// Convert back to CPU (Host) Endianness for logic
		unsigned int sessionID = ntohl(sessionIDNet);
		unsigned int totalSize = ntohl(fileLenNet);

		{
			std::lock_guard<std::mutex> lock{ _stdoutMutex };
			std::cout << "Download Accepted! Session ID: " << sessionID
				<< " | Total size: " << totalSize << " bytes"
				<< std::endl;
		}

		// Spin up a detached thread to handle the incoming UDP packets asynchronously
		std::thread udpThread(UdpReceiveFileThread,
			serverIpNet,
			serverPortNet,
			sessionID,
			totalSize,
			lastRequestedFilename);

		udpThread.detach(); // Let it run freely in the background
	}
	else if (id == RSP_LISTFILES) {
		// --- SERVER SENT FILE LIST ---
		uint16_t numFilesNet;
		if (!recvAll(source, reinterpret_cast<char*>(&numFilesNet), 2)) return false;
		uint16_t numFiles = ntohs(numFilesNet);

		uint32_t listLenNet;
		if (!recvAll(source, reinterpret_cast<char*>(&listLenNet), 4)) return false;

		std::lock_guard<std::mutex> lock{ _stdoutMutex };
		std::cout << "------ Available Files ------" << std::endl;

		// Loop through and extract each filename based on the provided length
		for (int idx = 0; idx < numFiles; ++idx) {
			uint32_t nameLen;

			if (!recvAll(source, reinterpret_cast<char*>(&nameLen), 4)) return false;
			nameLen = ntohl(nameLen);

			// Sanity check: If a packet gets shifted, nameLen will look like billions of bytes.
			// Reject it before std::vector tries to allocate gigabytes of RAM and crashes.
			if (nameLen > 10000) {
				std::cerr << "\n[CRITICAL ERROR] Network Packet Padding Desync Detected! Stream Corrupted." << std::endl;
				return false;
			}

			std::vector<char> actualName(nameLen + 1, 0); // +1 for null terminator
			if (!recvAll(source, actualName.data(), nameLen)) return false;

			std::cout << "[" << idx + 1 << "] " << actualName.data() << std::endl;
		}
		std::cout << "-----------------------------" << std::endl;
	}
	else {
		// Error or unknown command ID
		std::lock_guard<std::mutex> lock{ _stdoutMutex };
		std::cout << "==========RECV START==========" << std::endl;
		std::cout << "File Error" << std::endl;
		std::cout << "==========RECV END ===========" << std::endl;
		std::cout << std::endl;
	}

	return true;
}

int main(int argc, char** argv)
{
	std::string host{};
	std::string portString{};

	bool isManual = _isatty(_fileno(stdin));

	// Lambda helper to strip \r\n from user console inputs
	auto clean = [&](std::string& s) {
		s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
		s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
		while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
			s.pop_back();
		}
		};

	// --- Console Setup Phase ---
	std::cout << "Server IP Address: ";
	if (!std::getline(std::cin, host)) return 0;
	clean(host);

	std::string portNumber;
	std::cout << "Server Port Number: ";
	if (!std::getline(std::cin, portNumber)) return 0;
	clean(portNumber);

	std::cout << "Server UDP Port Number: ";
	if (!std::getline(std::cin, serverUdpPort)) return 0;
	clean(serverUdpPort);

	std::cout << "Client UDP Port Number: ";
	if (!std::getline(std::cin, clientUdpPort)) return 0;
	clean(clientUdpPort);

	while (true) {
		std::cout << "Path to store downloads: ";
		if (!std::getline(std::cin, downloadPath)) return 0;
		clean(downloadPath);

		std::cout << std::endl;

		std::error_code ec;
		if (std::filesystem::exists(downloadPath, ec)) {
			if (std::filesystem::is_directory(downloadPath, ec)) {
				break; // Valid folder
			}
			else { //If is a file and not a folder location
				std::cout << "\n[ERROR] Path exists but is a file, not a folder.\n" << std::endl;
			}
		}
		else { //Help user to create
			std::cout << "Path missing. Create it? (y/n): ";
			std::string choice;
			std::getline(std::cin, choice);
			if (choice == "y" || choice == "Y") {
				if (std::filesystem::create_directories(downloadPath, ec)) {
					std::cout << "Directory created successfully." << std::endl;
					break;
				}
				else {
					std::cout << "\n[ERROR] Could not create directory. Try a different path.\n" << std::endl;
				}
			}
		}
	}

	portString = portNumber;

	// --- Winsock Initialization ---
	WSADATA wsaData{};
	SecureZeroMemory(&wsaData, sizeof(wsaData));

	if (WSAStartup(MAKEWORD(WINSOCK_VERSION, WINSOCK_SUBVERSION), &wsaData) != NO_ERROR) return 1;

	// =========================================================================
	// Auto-Detect and Print Client's Local IP Address
	// We create a temporary UDP socket and "connect" it to a public DNS server (Google's 8.8.8.8).
	// No data is actually sent, but it forces Windows to bind the socket to the
	// correct Local IP Address (Ethernet/Wi-Fi/Hotspot) used for internet access.
	// =========================================================================
	SOCKET dummySocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (dummySocket != INVALID_SOCKET) {
		sockaddr_in dummyAddr{};
		dummyAddr.sin_family = AF_INET;
		dummyAddr.sin_port = htons(53); // DNS port
		inet_pton(AF_INET, "8.8.8.8", &dummyAddr.sin_addr);

		if (connect(dummySocket, (sockaddr*)&dummyAddr, sizeof(dummyAddr)) != SOCKET_ERROR) {
			sockaddr_in localAddr{};
			int addrLen = sizeof(localAddr);
			if (getsockname(dummySocket, (sockaddr*)&localAddr, &addrLen) != SOCKET_ERROR) {
				char myIP[INET_ADDRSTRLEN];
				inet_ntop(AF_INET, &(localAddr.sin_addr), myIP, INET_ADDRSTRLEN);
				std::cout << "-> Your local IP address: " << myIP << " <-" << std::endl;
			}
		}
		closesocket(dummySocket);
	}

	// Request a reliable TCP IPv4 socket
	addrinfo hints{};
	SecureZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	// Resolve the server host name to an IP Address
	addrinfo* info = nullptr;
	if (getaddrinfo(host.c_str(), portString.c_str(), &hints, &info) != NO_ERROR || info == nullptr) return 1;

	SOCKET clientSocket = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
	if (INVALID_SOCKET == clientSocket) return 2;

	if (connect(clientSocket, info->ai_addr, static_cast<int>(info->ai_addrlen)) == SOCKET_ERROR) return 3;

	// Start the background listening thread to monitor TCP responses
	std::thread receiver([clientSocket]() {
		while (true) {
			if (!ReceiveFromServer(clientSocket)) {
				std::cout << "Connection lost or server closed." << std::endl;
				exit(0);
			}
		}
		});
	receiver.detach();

	// --- Main Thread User Input Loop ---
	std::string input{};
	while (std::getline(std::cin, input)) {
		if (!input.empty() && (input.back() == '\r' || input.back() == '\n')) {
			input.pop_back();
		}
		if (!isManual) {
			if (input.find_first_not_of(" \t\n\r") == std::string::npos) continue;
		}
		if (!HandleMessage(clientSocket, input)) break;
	}

	// Graceful shutdown sequence
	shutdown(clientSocket, SD_SEND);
	closesocket(clientSocket);
	WSACleanup();
}

/**
 * @brief Thread spawned to asynchronously download a file over UDP using Selective Repeat.
 * Operates completely independently of the TCP command thread.
 */
void UdpReceiveFileThread(uint32_t serverIpNet, uint16_t serverPortNet, uint32_t sessionID, uint32_t fileLen, std::string filename) {

	// Create/Open the target file. std::ios::trunc wipes the file to 0 bytes if it already exists.
	std::filesystem::path fullPath = std::filesystem::path(downloadPath) / filename;
	std::ofstream file(fullPath, std::ios::binary | std::ios::trunc);
	if (!file.is_open()) return;

	SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (udpSocket == INVALID_SOCKET) return;

	// Bind the client's UDP socket so it has a specific port to listen on
	sockaddr_in clientUdpAddr{};
	clientUdpAddr.sin_family = AF_INET;
	clientUdpAddr.sin_addr.s_addr = INADDR_ANY;
	clientUdpAddr.sin_port = htons(static_cast<uint16_t>(std::stoi(clientUdpPort)));

	if (bind(udpSocket, (sockaddr*)&clientUdpAddr, sizeof(clientUdpAddr)) == SOCKET_ERROR) {
		closesocket(udpSocket);
		return;
	}

	uint32_t totalBytesReceived = 0;
	const int MAX_BUFFER = 2048; // Must be larger than the server's payload + header
	std::vector<char> recvBuffer(MAX_BUFFER);

	// Tracks the exact offsets we have successfully written.
	// Critical for Selective Repeat: if an ACK drops and the server resends a chunk,
	// this prevents us from counting it twice and ruining the file size.
	std::set<uint32_t> receivedOffsets;

	sockaddr_in fromAddr;
	int fromLen = sizeof(fromAddr);

	std::cout << "UDP Thread: Listening for Session " << sessionID << "..." << std::endl;

	// Set a 5-second receive timeout. If the server suddenly crashes, 
	// this prevents the client from hanging in an infinite loop forever.
	DWORD timeout = 5000;
	setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

	int lastPercent = -1;

	// --- Main Selective Repeat Receive Loop ---
	while (totalBytesReceived < fileLen) {
		int bytesRead = recvfrom(udpSocket, recvBuffer.data(), MAX_BUFFER, 0,
			(sockaddr*)&fromAddr, &fromLen);

		if (bytesRead > 0 && bytesRead >= sizeof(UdpDataHeader)) {
			// Cast the first chunk of bytes directly to our header struct
			UdpDataHeader* header = reinterpret_cast<UdpDataHeader*>(recvBuffer.data());

			// Convert Endianness for host logic comparison
			uint32_t incomingSession = ntohl(header->sessionID);
			uint32_t incomingOffset = ntohl(header->fileOffset);
			uint32_t incomingDataLen = ntohl(header->dataLen);

			// Ensure this packet belongs to this specific download thread
			if (incomingSession == sessionID) {

				// Sanity check: Ensure data length is valid to prevent buffer overflows
				if (incomingDataLen > static_cast<uint32_t>(bytesRead - sizeof(UdpDataHeader))) continue;

				// If we haven't seen this specific chunk offset before, process it
				if (receivedOffsets.find(incomingOffset) == receivedOffsets.end()) {

					// Seek to the absolute byte position in the file.
					// This allows packets to arrive fully out-of-order without corrupting the file!
					file.seekp(incomingOffset);
					file.write(recvBuffer.data() + sizeof(UdpDataHeader), incomingDataLen);

					receivedOffsets.insert(incomingOffset);
					totalBytesReceived += incomingDataLen;

					// --- DYNAMIC PROGRESS BAR LOGIC ---
					int currentPercent = static_cast<int>((totalBytesReceived * 100.0) / fileLen);

					// Only redraw if percentage changed (optimizes console I/O and prevents flicker)
					if (currentPercent != lastPercent) {
						lastPercent = currentPercent;
						int barWidth = 50;
						int pos = static_cast<int>(barWidth * (totalBytesReceived / static_cast<double>(fileLen)));

						std::lock_guard<std::mutex> lock{ _stdoutMutex };
						std::cout << "\r["; // \r returns cursor to the start of the line
						for (int i = 0; i < barWidth; ++i) {
							if (i < pos) std::cout << "=";
							else if (i == pos) std::cout << ">";
							else std::cout << " ";
						}
						std::cout << "] " << currentPercent << "%";
						std::cout.flush();
					}
					// -----------------------------------
				}

				// Always fire an ACK back to the server, even if it's a duplicate chunk.
				// (If it's a duplicate, it means our previous ACK was dropped by the network).
				UdpAckHeader ack;
				ack.flags = 0x01;
				ack.ackNum = htonl(incomingOffset + incomingDataLen);
				ack.seqNum = header->fileOffset; // Bounce the Network Byte Order sequence straight back

				sendto(udpSocket, reinterpret_cast<char*>(&ack), sizeof(UdpAckHeader), 0, (sockaddr*)&fromAddr, fromLen);
			}
		}
		else if (bytesRead == SOCKET_ERROR) {
			int err = WSAGetLastError();
			if (err == WSAETIMEDOUT) {
				std::cout << "UDP Thread: Timeout waiting for packets. Transfer incomplete." << std::endl;
				break;
			}
			else {
				std::cerr << "UDP Thread: Socket Error " << err << ". Transfer incomplete." << std::endl;
				break;
			}
		}
	}

	std::cout << std::endl; // Push to a new line after the \r progress bar completes

	if (totalBytesReceived == fileLen) {
		std::cout << "UDP Thread: Download complete! Saved to " << fullPath << std::endl;
	}

	closesocket(udpSocket);
	file.close();
}