/* Start Header
*****************************************************************/
/*!
\file   server.cpp
\author Elijah Chua
\par
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
 // #include "winsock2.h"	// ...or Winsock alone
#include "ws2tcpip.h"		// getaddrinfo()
#include "protocol.h"

// Tell the Visual Studio linker to include the following library in linking.
// Alternatively, we could add this file to the linker command-line parameters,
// but including it in the source code simplifies the configuration.
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
#define RETURN_CODE_1       1
#define RETURN_CODE_2       2
#define RETURN_CODE_3       3
#define RETURN_CODE_4       4

static std::string portString{};
static std::string udpPortString{};
static std::string filesPath{};

bool sendAll(SOCKET s, const char* data, int size) {
	int totalSent = 0;
	while (totalSent < size) {
		int result = send(s, data + totalSent, size - totalSent, 0);

		if (result == SOCKET_ERROR) {
			// Log the error if necessary
			return false;
		}

		totalSent += result;
	}
	return true;
}

bool receiveAll(SOCKET s, char* buffer, int totalBytes) {
	int receivedSoFar = 0;
	while (receivedSoFar < totalBytes) {
		int res = recv(s, buffer + receivedSoFar, totalBytes - receivedSoFar, 0);
		if (res <= 0) return false; // Connection closed or error [cite: 222]
		receivedSoFar += res;
	}
	return true;
}
//This struct is used to keep track of other client's IP Address and their port numbers
struct User {
	uint32_t ip;
	uint16_t port;
	bool operator<(const User& rhs) const {
		return std::tie(ip, port) < std::tie(rhs.ip, rhs.port);
	}
};

static std::map<User, SOCKET> userRegisteration;
static std::mutex mutexRegisteration;

bool execute(SOCKET clientSocket);
void disconnect(SOCKET& listenerSocket);

int main()
{

	// -------------------------------------------------------------------------
	// Start up Winsock, asking for version 2.2.
	//
	// WSAStartup()
	// -------------------------------------------------------------------------

	// This object holds the information about the version of Winsock that we
	// are using, which is not necessarily the version that we requested.
	WSADATA wsaData{};

	// Initialize Winsock. You must call WSACleanup when you are finished.
	// As this function uses a reference counter, for each call to WSAStartup,
	// you must call WSACleanup or suffer memory issues.
	int errorCode = WSAStartup(MAKEWORD(WINSOCK_VERSION, WINSOCK_SUBVERSION), &wsaData);
	if (NO_ERROR != errorCode)
	{
		std::cerr << "WSAStartup() failed." << std::endl;
		return errorCode;
	}

	//Starts the gettign the informations
	// Get Port Number
	//Detecting if human or redirected via << returns non zero if is keyboard.console
	bool isManual = _isatty(_fileno(stdin));

	auto clean = [&](std::string& s) { //Removes \n and \r
		s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
		s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());

		// Optional: still trim trailing spaces if you want to be safe
		while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
			s.pop_back();
		}

	};
	
	//Server TCP port number, listen on it's IP Address and port and disply received message
	std::cout << "Server TCP Port Number: ";
	if (!std::getline(std::cin, portString)) return 0;
	clean(portString);

	//Getting UDP port number for downloads
	std::cout << "Server UDP Port Number: ";
	if (!std::getline(std::cin, udpPortString)) return 0;
	clean(udpPortString);

	//Location of the download files
	std::cout << "Files Path: ";
	if (!std::getline(std::cin, filesPath)) return 0;
	clean(filesPath);
	


	// -------------------------------------------------------------------------
	// Resolve own host name into IP addresses (in a singly-linked list).
	//
	// getaddrinfo()
	// -------------------------------------------------------------------------

	// Object hints indicates which protocols to use to fill in the info.
	addrinfo hints{};
	SecureZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;			// IPv4
	// For UDP use SOCK_DGRAM instead of SOCK_STREAM.
	hints.ai_socktype = SOCK_STREAM;	// Reliable delivery
	// Could be 0 for autodetect, but reliable delivery over IPv4 is always TCP.
	hints.ai_protocol = IPPROTO_TCP;	// TCP
	// Create a passive socket that is suitable for bind() and listen().
	hints.ai_flags = AI_PASSIVE;

	addrinfo* info = nullptr;
	errorCode = getaddrinfo(nullptr, portString.c_str(), &hints, &info);
	if ((errorCode) || (info == nullptr))
	{
		std::cerr << "getaddrinfo() failed." << std::endl;
		WSACleanup();
		return errorCode;
	}

	/* PRINT SERVER IP ADDRESS AND PORT NUMBER */
	char host[MAX_STR_LEN];
	if (gethostname(host, MAX_STR_LEN) == 0) {
		addrinfo nameHints{}, * nameResult = nullptr;
		nameHints.ai_family = AF_INET; // IPv4 only
		nameHints.ai_socktype = SOCK_STREAM; //Establishment
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
	


	// -------------------------------------------------------------------------
	// Create a socket and bind it to own network interface controller.
	//
	// socket()
	// bind()
	// -------------------------------------------------------------------------

	SOCKET listenerSocket = socket(
		hints.ai_family,
		hints.ai_socktype,
		hints.ai_protocol);
	if (listenerSocket == INVALID_SOCKET)
	{
		std::cerr << "socket() failed." << std::endl;
		freeaddrinfo(info);
		WSACleanup();
		return 1;
	}

	errorCode = bind(
		listenerSocket,
		info->ai_addr,
		static_cast<int>(info->ai_addrlen));
	if (errorCode != NO_ERROR)
	{
		std::cerr << "bind() failed." << std::endl;
		closesocket(listenerSocket);
		listenerSocket = INVALID_SOCKET;
	}

	freeaddrinfo(info);

	if (listenerSocket == INVALID_SOCKET)
	{
		std::cerr << "bind() failed." << std::endl;
		WSACleanup();
		return 2;
	}


	// -------------------------------------------------------------------------
	// Set a socket in a listening mode and accept 1 incoming client.
	//
	// listen()
	// accept()
	// -------------------------------------------------------------------------

	errorCode = listen(listenerSocket, SOMAXCONN);
	if (NO_ERROR != errorCode)
	{
		std::cerr << "listen() failed." << std::endl;
		closesocket(listenerSocket);
		WSACleanup();
		return 3;
	}

	{
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
			if (clientSocket == INVALID_SOCKET)
			{
				break;
			}
			sockaddr_in* pClientAddr = reinterpret_cast<sockaddr_in*>(&clientAddress); //To get the IP Address and port number
			char clientIP[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &(pClientAddr->sin_addr), clientIP, INET_ADDRSTRLEN);
			{
				std::lock_guard<std::mutex> lock{ _stdoutMutex };
				std::cout << std::endl;
				std::cout << "Client IP Address: " << clientIP << std::endl;
				std::cout << "Client Port Number: " << ntohs(pClientAddr->sin_port) << std::endl;

			}
			tq.produce(clientSocket);
		}
	}

	// -------------------------------------------------------------------------
	// Clean-up after Winsock.
	//
	// WSACleanup()
	// -------------------------------------------------------------------------

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

bool execute(SOCKET clientSocket) //Clientsocket can be seen with a number that stores information about it
{
	//For connectiing client and storing it.
	sockaddr_in senderAddr;
	int addrLen = sizeof(senderAddr);

	bool isManual = _isatty(_fileno(stdin));
	/**
	 * getpeername
	 *
	 * \param SOCKET	:	clientSocket
	 * \param name(Pointer to store the IP Address and the port of the peer)		:	(sockaddr*)&senderAddr
	 * \param namelen(Pointer to defines the size of the structure of the name)		:	addrLen
	 * \return true for success and 0 for failure
	 */
	int peer = getpeername(clientSocket, (sockaddr*)&senderAddr, &addrLen); //Used to identify who send

	if (peer == SOCKET_ERROR) {
		std::cout << "Cannot find sender!" << std::endl;
	}
	//This creates a unique ID for mutex
	User myKey{ senderAddr.sin_addr.s_addr, senderAddr.sin_port };
	{
		std::lock_guard<std::mutex> lock{ mutexRegisteration };
		userRegisteration[myKey] = clientSocket;

	}

	constexpr size_t BUFFER_SIZE = 10000;
	bool stay = true;

	unsigned char rawID = 0;
	unsigned char command{};

	auto recvAll = [&](char* target, int size) -> bool {
		int totalReceived = 0;
		while (totalReceived < size) {
			int res = recv(clientSocket, target + totalReceived, size - totalReceived, 0);
			if (res <= 0) return false; // Connection closed or error 
			totalReceived += res;
		}
		return true;
		};

	while (true)
	{
		const int bytesReceived = recv(clientSocket, reinterpret_cast<char*>(&rawID), 1, 0);

		if (bytesReceived == SOCKET_ERROR)
		{
			std::cerr << "recv() failed." << std::endl;
			break;
		}
		if (bytesReceived == 0)
		{
			std::cerr << "Graceful shutdown." << std::endl;
			break;
		}

		//If everything is correct. Check command and act accordingly
		if (bytesReceived > 0) {

			CMDID ID = static_cast<CMDID>(rawID);

			if (ID == REQ_LISTFILES) {
				std::vector<std::string> fileNames;
				uint32_t totalListLen = 0;

				if (std::filesystem::exists(filesPath) && std::filesystem::is_directory(filesPath)) {
					for (const auto& entry : std::filesystem::directory_iterator(filesPath)) {
						if (entry.is_regular_file()) {
							std::string name = entry.path().filename().string();
							fileNames.push_back(name);
							totalListLen += (4 + static_cast<uint32_t>(name.length())); // 4 bytes for Nlen + string
						}
					}
				}

				std::vector<char> fullPacket;
				fullPacket.reserve(7 + totalListLen); // 7 bytes for the fixed header

				RspListHeader header;
				header.cmd = RSP_LISTFILES; // 0x05
				header.numFiles = htons(static_cast<uint16_t>(fileNames.size()));
				header.listLen = htonl(totalListLen);

				char* sendHeader = reinterpret_cast<char*>(&header);
				fullPacket.insert(fullPacket.end(), sendHeader, sendHeader + sizeof(header));

				for (const auto& name : fileNames) {
					uint32_t nLen = htonl(static_cast<uint32_t>(name.length()));
					char* pLen = reinterpret_cast<char*>(&nLen);
					fullPacket.insert(fullPacket.end(), pLen, pLen + 4);

					fullPacket.insert(fullPacket.end(), name.begin(), name.end());
				}

				//Sending it all at once
				if (!sendAll(clientSocket, fullPacket.data(), static_cast<int>(fullPacket.size()))) {
					return false;
				}
			}
			else if (ID == REQ_DOWNLOAD) {
				#pragma pack(push, 1)
				struct {
					uint32_t ip;
					uint16_t port;
					uint32_t nameLen;
				} reqHeader;
				#pragma pack(pop)

				if (!recvAll((char*)&reqHeader, 10)) break;

				uint32_t nameLen = ntohl(reqHeader.nameLen);

				std::vector<char> nameBuf(nameLen + 1, 0);
				if (!recvAll(nameBuf.data(), nameLen)) break;
				std::string requestedFile(nameBuf.data());

				std::filesystem::path fullPath = std::filesystem::path(filesPath) / requestedFile;

				if (std::filesystem::exists(fullPath) && std::filesystem::is_regular_file(fullPath)) {
					RspDownloadHeader resp;
					resp.cmd = RSP_DOWNLOAD; // 0x03

					// Use Server's IP and the Global UDP Port you collected in main(). SO they know who to reply to
					memcpy(resp.ip, &senderAddr.sin_addr.s_addr, 4);

					//Use the UDP port from server 
					resp.port = htons(static_cast<uint16_t>(std::stoi(udpPortString)));

					// Generate a unique Session ID for this transfer
					static uint32_t globalSessionID = 7000;
					resp.sessionID = htonl(globalSessionID++);

					// Get the actual file size from the disk
					resp.fileLen = htonl(static_cast<uint32_t>(std::filesystem::file_size(fullPath)));

					// B. Send the 15-byte header all at once
					sendAll(clientSocket, (char*)&resp, sizeof(resp));

					std::cout << "Starting transfer for: " << requestedFile
						<< " (" << std::filesystem::file_size(fullPath) << " bytes)" << std::endl;

					// TODO: This is the trigger to start your UDP sender thread!
				}
				else {
					char errID = DOWNLOAD_ERROR; // DOWNLOAD_ERROR
					sendAll(clientSocket, &errID, 1);
				}
			}
			else {
				CMDID error = REQ_QUIT;
				{
					std::lock_guard<std::mutex> lock{ _stdoutMutex };
					std::cout << "Graceful Shutdown" << std::endl;
				}

				{
					std::lock_guard<std::mutex> lock{ mutexRegisteration };
					userRegisteration.erase(myKey);
				}
				sendAll(clientSocket, (char*)&error, 1);
			}
		}

	}

	// -------------------------------------------------------------------------
		// Shut down and close sockets.
		//
		// shutdown()
		// closesocket()
		// -------------------------------------------------------------------------

		{
			std::lock_guard<std::mutex> lock{ mutexRegisteration };
			userRegisteration.erase(myKey);
		}

		shutdown(clientSocket, SD_BOTH);
		closesocket(clientSocket);
		return stay;
}
