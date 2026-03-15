/* Start Header
*****************************************************************/
/*!
\file   client.cpp
\author Elijah Chua
\co-author Guan Shao Jun
\par
\date   21 Feb 2026
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

#pragma comment(lib, "ws2_32.lib")

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

static std::string serverUdpPort{};
static std::string clientUdpPort{};
static std::string downloadPath{};
static std::string lastRequestedFilename{};

std::mutex _stdoutMutex;

#ifndef WINSOCK_VERSION
#define WINSOCK_VERSION     2
#endif

#define WINSOCK_SUBVERSION  2
#define MAX_STR_LEN         1000

void UdpReceiveFileThread(uint32_t serverIpNet, uint16_t serverPortNet, uint32_t sessionID, uint32_t fileLen, std::string filename);

bool sendAll(SOCKET s, const char* data, int size) {
	int totalSent = 0;
	while (totalSent < size) {
		int result = send(s, data + totalSent, size - totalSent, 0);
		if (result == SOCKET_ERROR) {
			return false;
		}
		totalSent += result;
	}
	return true;
}

bool recvAll(SOCKET s, char* buffer, int size) {
	int received = 0;
	while (received < size) {
		int res = recv(s, buffer + received, size - received, 0);
		if (res <= 0) return false;
		received += res;
	}
	return true;
}

bool HandleMessage(SOCKET source, std::string const& msg) {
	if (msg == "/q") {
		char id = static_cast<char>(REQ_QUIT);
		{
			std::lock_guard<std::mutex> lock{ _stdoutMutex };
			std::cout << "disconnection..." << std::endl;
		}
		sendAll(source, &id, 1);
		return false;
	}
	else if (msg.length() >= 2 && msg.substr(0, 2) == "/l") {
		char id = static_cast<char>(REQ_LISTFILES);
		if (!sendAll(source, &id, 1)) {
			return false;
		}
		return true;
	}
	else if (msg.length() >= 3 && msg.substr(0, 2) == "/d") {
		std::string content = msg.substr(3);
		size_t colonPos = content.find(':');
		size_t spacePos = content.find(' ');

		if (colonPos != std::string::npos && spacePos != std::string::npos && spacePos > colonPos) {
			std::string ipString = content.substr(0, colonPos);
			std::string portString = content.substr(colonPos + 1, spacePos - colonPos - 1);
			std::string filename = content.substr(spacePos + 1);

			lastRequestedFilename = filename;

			std::vector<char> fullPacket;
			fullPacket.reserve(11 + filename.length());

			fullPacket.push_back(static_cast<char>(REQ_DOWNLOAD));

			uint32_t ipNet;
			inet_pton(AF_INET, ipString.c_str(), &ipNet);
			char* pIp = reinterpret_cast<char*>(&ipNet);
			fullPacket.insert(fullPacket.end(), pIp, pIp + 4);

			uint16_t portNet = htons(static_cast<unsigned short>(std::stoi(portString)));
			char* pPort = reinterpret_cast<char*>(&portNet);
			fullPacket.insert(fullPacket.end(), pPort, pPort + 2);

			uint32_t nameLenNet = htonl(static_cast<unsigned int>(filename.length()));
			char* pNameLen = reinterpret_cast<char*>(&nameLenNet);
			fullPacket.insert(fullPacket.end(), pNameLen, pNameLen + 4);

			fullPacket.insert(fullPacket.end(), filename.begin(), filename.end());

			if (!sendAll(source, fullPacket.data(), static_cast<int>(fullPacket.size()))) {
				return false;
			}
		}
	}
	else {
		std::lock_guard<std::mutex> lock{ _stdoutMutex };
		std::cout << "Invalid command! Use /l to list files, or /d <IP>:<UDP_Port> <filename>" << std::endl;
		return true;
	}
	return true;
}

bool ReceiveFromServer(SOCKET source) {
	char id;
	if (recv(source, &id, 1, 0) <= 0) {
		return false;
	}

	if (id == REQ_QUIT) {
		{
			std::lock_guard<std::mutex> lock{ _stdoutMutex };
			std::cout << "disconnection..." << std::endl;
		}
		return false;
	}
	else if (id == RSP_DOWNLOAD) {
		uint32_t serverIpNet;
		if (!recvAll(source, reinterpret_cast<char*>(&serverIpNet), 4)) return false;

		uint16_t serverPortNet;
		if (!recvAll(source, reinterpret_cast<char*>(&serverPortNet), 2)) return false;

		uint32_t sessionIDNet;
		if (!recvAll(source, reinterpret_cast<char*>(&sessionIDNet), 4)) return false;

		uint32_t fileLenNet;
		if (!recvAll(source, reinterpret_cast<char*>(&fileLenNet), 4)) return false;

		unsigned int sessionID = ntohl(sessionIDNet);
		unsigned int totalSize = ntohl(fileLenNet);

		{
			std::lock_guard<std::mutex> lock{ _stdoutMutex };
			std::cout << "Download Accepted! Session ID: " << sessionID
				<< " | Total size: " << totalSize << " bytes"
				<< std::endl;
		}

		std::thread udpThread(UdpReceiveFileThread,
			serverIpNet,
			serverPortNet,
			sessionID,
			totalSize,
			lastRequestedFilename);

		udpThread.detach();
	}
	else if (id == RSP_LISTFILES) {
		uint16_t numFilesNet;
		if (!recvAll(source, reinterpret_cast<char*>(&numFilesNet), 2)) return false;
		uint16_t numFiles = ntohs(numFilesNet);

		uint32_t listLenNet;
		if (!recvAll(source, reinterpret_cast<char*>(&listLenNet), 4)) return false;

		std::lock_guard<std::mutex> lock{ _stdoutMutex };
		std::cout << "------ Available Files ------" << std::endl;

		for (int idx = 0; idx < numFiles; ++idx) {
			uint32_t nameLen;

			if (!recvAll(source, reinterpret_cast<char*>(&nameLen), 4)) return false;
			nameLen = ntohl(nameLen);

			if (nameLen > 10000) {
				std::cerr << "\n[CRITICAL ERROR] Network Packet Padding Desync Detected! Stream Corrupted." << std::endl;
				return false;
			}

			std::vector<char> actualName(nameLen + 1, 0);
			if (!recvAll(source, actualName.data(), nameLen)) return false;

			std::cout << "[" << idx + 1 << "] " << actualName.data() << std::endl;
		}
		std::cout << "-----------------------------" << std::endl;
	}
	else {
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

	auto clean = [&](std::string& s) {
		s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
		s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
		while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
			s.pop_back();
		}
		};

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

	std::cout << "Path to store downloads: ";
	if (!std::getline(std::cin, downloadPath)) return 0;
	clean(downloadPath);
	std::cout << std::endl;

	portString = portNumber;

	WSADATA wsaData{};
	SecureZeroMemory(&wsaData, sizeof(wsaData));

	if (WSAStartup(MAKEWORD(WINSOCK_VERSION, WINSOCK_SUBVERSION), &wsaData) != NO_ERROR) return 1;

	addrinfo hints{};
	SecureZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	addrinfo* info = nullptr;
	if (getaddrinfo(host.c_str(), portString.c_str(), &hints, &info) != NO_ERROR || info == nullptr) return 1;

	SOCKET clientSocket = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
	if (INVALID_SOCKET == clientSocket) return 2;

	if (connect(clientSocket, info->ai_addr, static_cast<int>(info->ai_addrlen)) == SOCKET_ERROR) return 3;

	std::thread receiver([clientSocket]() {
		while (true) {
			if (!ReceiveFromServer(clientSocket)) {
				std::cout << "Connection lost or server closed." << std::endl;
				exit(0);
			}
		}
		});
	receiver.detach();

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

	shutdown(clientSocket, SD_SEND);
	closesocket(clientSocket);
	WSACleanup();
}

void UdpReceiveFileThread(uint32_t serverIpNet, uint16_t serverPortNet, uint32_t sessionID, uint32_t fileLen, std::string filename) {
	std::filesystem::path fullPath = std::filesystem::path(downloadPath) / filename;
	std::ofstream file(fullPath, std::ios::binary | std::ios::trunc);
	if (!file.is_open()) return;

	SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (udpSocket == INVALID_SOCKET) return;

	sockaddr_in clientUdpAddr{};
	clientUdpAddr.sin_family = AF_INET;
	clientUdpAddr.sin_addr.s_addr = INADDR_ANY;
	clientUdpAddr.sin_port = htons(static_cast<uint16_t>(std::stoi(clientUdpPort)));

	if (bind(udpSocket, (sockaddr*)&clientUdpAddr, sizeof(clientUdpAddr)) == SOCKET_ERROR) {
		closesocket(udpSocket);
		return;
	}

	uint32_t totalBytesReceived = 0;
	const int MAX_BUFFER = 2048;
	std::vector<char> recvBuffer(MAX_BUFFER);
	std::set<uint32_t> receivedOffsets;

	sockaddr_in fromAddr;
	int fromLen = sizeof(fromAddr);

	std::cout << "UDP Thread: Listening for Session " << sessionID << "..." << std::endl;

	DWORD timeout = 5000;
	setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

	int lastPercent = -1;

	while (totalBytesReceived < fileLen) {
		int bytesRead = recvfrom(udpSocket, recvBuffer.data(), MAX_BUFFER, 0,
			(sockaddr*)&fromAddr, &fromLen);

		if (bytesRead > 0 && bytesRead >= sizeof(UdpDataHeader)) {
			UdpDataHeader* header = reinterpret_cast<UdpDataHeader*>(recvBuffer.data());

			uint32_t incomingSession = ntohl(header->sessionID);
			uint32_t incomingOffset = ntohl(header->fileOffset);
			uint32_t incomingDataLen = ntohl(header->dataLen);

			if (incomingSession == sessionID) {
				if (incomingDataLen > static_cast<uint32_t>(bytesRead - sizeof(UdpDataHeader))) continue;

				if (receivedOffsets.find(incomingOffset) == receivedOffsets.end()) {
					file.seekp(incomingOffset);
					file.write(recvBuffer.data() + sizeof(UdpDataHeader), incomingDataLen);

					receivedOffsets.insert(incomingOffset);
					totalBytesReceived += incomingDataLen;

					// --- PROGRESS BAR LOGIC ---
					int currentPercent = static_cast<int>((totalBytesReceived * 100.0) / fileLen);

					// Only redraw if the percentage actually changed (saves CPU and prevents screen flickering)
					if (currentPercent != lastPercent) {
						lastPercent = currentPercent;
						int barWidth = 50;
						int pos = static_cast<int>(barWidth * (totalBytesReceived / static_cast<double>(fileLen)));

						std::lock_guard<std::mutex> lock{ _stdoutMutex };
						std::cout << "\r[";
						for (int i = 0; i < barWidth; ++i) {
							if (i < pos) std::cout << "=";
							else if (i == pos) std::cout << ">";
							else std::cout << " ";
						}
						std::cout << "] " << currentPercent << "%";
						std::cout.flush(); // Force the console to draw it immediately
					}
					// -----------------------------------
				}

				UdpAckHeader ack;
				ack.flags = 0x01;
				ack.ackNum = htonl(incomingOffset + incomingDataLen);
				ack.seqNum = header->fileOffset;

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

	std::cout << std::endl; // Move to the next line after the bar finishes

	if (totalBytesReceived == fileLen) {
		std::cout << "UDP Thread: Download complete! Saved to " << fullPath << std::endl;
	}

	closesocket(udpSocket);
	file.close();
}