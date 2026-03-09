/* Start Header
*****************************************************************/
/*!
\file   client.cpp
\author Elijah Chua
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


/*******************************************************************************
 * A simple TCP/IP client application
 ******************************************************************************/

#pragma pack(pop)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "Windows.h"		// Entire Win32 API...
#include "winsock2.h"		// ...or Winsock alone
#include "ws2tcpip.h"		// getaddrinfo()
#include "protocol.h"

 // Tell the Visual Studio linker to include the following library in linking.
 // Alternatively, we could add this file to the linker command-line parameters,
 // but including it in the source code simplifies the configuration.
#pragma comment(lib, "ws2_32.lib")

#include <iostream>			// cout, cerr
#include <string>			// string
#include <vector>
#include <fstream>
#include <cctype>
#include <io.h>
#include <thread>
#include <mutex>

static std::string serverUdpPort{};  // (b) Server UDP Port
static std::string clientUdpPort{};  // (c) Client UDP Port
static std::string downloadPath{};   // (d) Download Path

std::mutex _stdoutMutex;

#ifndef WINSOCK_VERSION
#define WINSOCK_VERSION     2
#endif

#define WINSOCK_SUBVERSION  2
#define MAX_STR_LEN         1000
#define RETURN_CODE_1       1
#define RETURN_CODE_2       2
#define RETURN_CODE_3       3
#define RETURN_CODE_4       4

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

bool recvAll(SOCKET s, char* buffer, int size) {
	int received = 0;
	while (received < size) {
		int res = recv(s, buffer + received, size - received, 0);
		if (res <= 0) return false; // Server closed or error [cite: 222]
		received += res;
	}
	return true;
}

std::vector<char> HexTranslation(const std::string& hex) {
	std::vector<char> bytes;
	std::string noSpaceHex{};
	
	//Why iterate by 2? Since we are dealing with hexadecimals,
	//hexadecimals comes in a pair
	for (char c : hex) {
		if (std::isxdigit(static_cast<unsigned char>(c)) && 
			!std::isspace(static_cast<unsigned char>(c)) && 
			c != ':') {
			noSpaceHex += c;
		}
	}

	for (size_t idx = 0 ; idx + 1 < noSpaceHex.length() ; idx += 2) {
		try {
			//std::stoul converts tehs tring into an unsigned long integer based 16(hexadecimal)
			//Using static_cast to convert it to words
			//Since we are dealing with hexadecimal, always takes 2 char,stopping pooint, and the specific based we are chaing to. 10 is the normal decimal.
			std::string byteString = noSpaceHex.substr(idx, 2);
			unsigned long byteValue = std::stoul(byteString, nullptr, 16);
			bytes.push_back(static_cast<char>(byteValue));
		}
		catch (...) { //If is invalid characters then berak it
			break;
		}
	}

	return bytes;
}

bool HandleMessage(SOCKET source, std::string const& msg) {
	if (msg == "/q") { //Checking for quit
		char id = static_cast<char>(REQ_QUIT);
		//Params for send:
		//socket, data pointer, the size, special instructions
		{
			std::lock_guard<std::mutex> lock{ _stdoutMutex };
			std::cout << "disconection..." << std::endl;
		}
		sendAll(source, &id, 1);
		return false; //stops loop
	}
	else if (msg.length() >= 2 && msg.substr(0, 2) == "/l") {	//Requesting to list files
		char id = static_cast<char>(REQ_LISTFILES);
		if (!sendAll(source, &id, 1)) {
			return false;
		}
		return true;
	}

	else if (msg.length() >= 2 && msg.substr(0, 2) == "/d") {	//Requesting to downalod files
		//Expected format: /d 192.168.1.3:9010 filelist.cpp
		std::string content = msg.substr(3);
		size_t colonPos = content.find(':');
		size_t spacePos = content.find(' ');

		if (colonPos != std::string::npos && spacePos != std::string::npos) { //Meaning correct format
			std::string ipString = content.substr(0, colonPos);
			std::string portString = content.substr(colonPos + 1, spacePos - colonPos); //Start positinon follow by length
			std::string filename = content.substr(spacePos + 1);

			//Header from protocol.h
			ReqDownloadHeader request;
			request.cmd = REQ_DOWNLOAD;	//The command

			//Fill the ipadress with inet_pton. From CPU small endian to internet's big endian
			inet_pton(AF_INET, ipString.c_str(), &request.ip);	//The IP Adress

			//htons for 2 bytes
			request.port = htons(static_cast<unsigned short>(std::stoi(portString)));	//Port number

			//htonl for 4
			request.nameLen = htonl(static_cast<unsigned int>(filename.length()));		//Name length

			//Packing everything together
			std::vector<char> fullPacket;
			fullPacket.reserve(sizeof(request) + filename.length());

			//Set up the 11-bytes header and push into the packet first
			char* header = reinterpret_cast<char*>(&request);
			fullPacket.insert(fullPacket.end(), header, header + sizeof(request));

			//Insertion of the filename string
			fullPacket.insert(fullPacket.end(), filename.begin(), filename.end());

			if (!sendAll(source, fullPacket.data(), static_cast<int>(fullPacket.size()))) {
				return false;
			}
		}
	}
		else {
		std::vector<char> emptyPacket;
	
		sendAll(source, emptyPacket.data(), static_cast<int>(emptyPacket.size()));
		return false;
	}

		
	

	return true;
}

bool ReceiveFromServer(SOCKET source) {
	char id;
	if (recv(source, &id, 1, 0) <= 0) { //Checking if socket pipe is broken
		return false;
	}

	if (id == REQ_QUIT) {
		{
			std::lock_guard<std::mutex> lock{ _stdoutMutex };
			std::cout << "disconection..." << std::endl;
		}
		return false; //To quit
	}
	else if (id == RSP_DOWNLOAD) { //To print the messages
		RspDownloadHeader response;
		response.cmd = id;
		
		//Total for response from server is 14 bytes
		if (!recvAll(source, (char*)&response.ip, 14)) {
			return false;
		}

		unsigned int sessionID = ntohl(response.sessionID);	//
		unsigned int totalSize = ntohl(response.fileLen);

		std::cout << "Download Accepted! Session ID: " << sessionID
				  << " | Total size: " << totalSize << "bytes" 
			      << std::endl;

		//TODO: NEED TO USE UDP THREAD
	}
	else if (id == RSP_LISTFILES) { //To list the downloadable files
		RspListHeader header;
		header.cmd = id;

		//Read 6 bytes, 2 for number of files, 4 for filename length
		if (!recvAll(source, (char*)&header.numFiles, 6)) { //If do not recive correctly
			return false;
		}

		uint16_t numFiles = ntohs(header.numFiles);	//Conversion from Big endian to small endian to read proper values

		for (int idx = 0; idx < numFiles; ++idx) {
			uint32_t nameLen; //4bytes

			//Fixed file name length of 4 bytes
			if (!recvAll(source, (char*)&nameLen, 4)) { //Reads the next 4 bytes
				return false;
			}
			
			//Converts the big endian from network to CPU little endian 
			nameLen = ntohl(nameLen);

			std::vector<char> actualName(nameLen + 1, 0);	//+1 for null terminator and initalize the vector value to be 0 first.
			if (!recvAll(source, actualName.data(), nameLen)) {
				return false;
			}

			//std::cout automatically prints out all characters when given .data() till null terminator therefore this can work.
			std::cout << "[" << idx + 1 << actualName.data() << std::endl;

		}

		std::cout << "------ End of all downloadable files ------" << std::endl;

	}
	else{ //If Error
		std::lock_guard<std::mutex> lock{ _stdoutMutex };
		std::cout << "==========RECV START==========" << std::endl;
		std::cout << "File Error" << std::endl;
		std::cout << "==========RECV END ===========" << std::endl;
		std::cout << std::endl;
	}

	return true;
}

// This program requires one extra command-line parameter: a server hostname.
int main(int argc, char** argv)
{
	constexpr uint16_t port = 2048;

	// Get IP Address
	std::string host{};
	std::string portString{};
	
	std::vector<std::string> messages{};
	bool isManual = _isatty(_fileno(stdin));

	auto clean = [&](std::string& s) { //Removes \n and \r
		s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
		s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
		while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
			s.pop_back();
		}
	};
	
		std::cout << "Server IP Address: ";
		if (!std::getline(std::cin, host)) return 0;
		clean(host);
		std::cout << std::endl;

		// Get Port Number
		std::string portNumber;
		std::cout << "Server Port Number: ";
		if (!std::getline(std::cin, portNumber)) return 0;
		clean(portNumber);
		std::cout << std::endl;

		//The downaloding files to be send to all clients
		std::cout << "Server UDP Port Number: ";
		if (!std::getline(std::cin, serverUdpPort)) return 0;
		clean(serverUdpPort);
		std::cout << std::endl;

		std::cout << "Client UDP Port Number: ";
		if (!std::getline(std::cin, clientUdpPort)) return 0;
		clean(clientUdpPort);
		std::cout << std::endl;

		//Where to store the download path
		std::cout << "Path to store downloads: ";
		if (!std::getline(std::cin, downloadPath)) return 0;
		clean(downloadPath);
		std::cout << std::endl;

		portString = portNumber;
	



	// -------------------------------------------------------------------------
	// Start up Winsock, asking for version 2.2.
	//
	// WSAStartup()
	// -------------------------------------------------------------------------

	// This object holds the information about the version of Winsock that we
	// are using, which is not necessarily the version that we requested.
	WSADATA wsaData{};
	SecureZeroMemory(&wsaData, sizeof(wsaData));

	// Initialize Winsock. You must call WSACleanup when you are finished.
	// As this function uses a reference counter, for each call to WSAStartup,
	// you must call WSACleanup or suffer memory issues.
	int errorCode = WSAStartup(MAKEWORD(WINSOCK_VERSION, WINSOCK_SUBVERSION), &wsaData);
	if (NO_ERROR != errorCode)
	{
		std::cerr << "WSAStartup() failed." << std::endl;
		return errorCode;
	}


	// -------------------------------------------------------------------------
	// Resolve a server host name into IP addresses (in a singly-linked list).
	//
	// getaddrinfo()
	// -------------------------------------------------------------------------

	// Object hints indicates which protocols to use to fill in the info.
	addrinfo hints{};
	SecureZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;			// IPv4
	hints.ai_socktype = SOCK_STREAM;	// Reliable delivery
	// Could be 0 to autodetect, but reliable delivery over IPv4 is always TCP.
	hints.ai_protocol = IPPROTO_TCP;	// TCP

	addrinfo* info = nullptr;
	errorCode = getaddrinfo(host.c_str(), portString.c_str(), &hints, &info);
	if ((NO_ERROR != errorCode) || (nullptr == info))
	{
		std::cerr << "getaddrinfo() failed." << std::endl;
		WSACleanup();
		return errorCode;
	}


	// -------------------------------------------------------------------------
	// Create a socket and attempt to connect to the first resolved address.
	//
	// socket()
	// connect()
	// -------------------------------------------------------------------------

	SOCKET clientSocket = socket(
		info->ai_family,
		info->ai_socktype,
		info->ai_protocol);
	if (INVALID_SOCKET == clientSocket)
	{
		std::cerr << "socket() failed." << std::endl;
		freeaddrinfo(info);
		WSACleanup();
		return RETURN_CODE_2;
	}

	errorCode = connect(
		clientSocket,
		info->ai_addr,
		static_cast<int>(info->ai_addrlen));
	if (SOCKET_ERROR == errorCode)
	{
		std::cerr << "connect() failed." << std::endl;
		freeaddrinfo(info);
		closesocket(clientSocket);
		WSACleanup();
		return RETURN_CODE_3;
	}
	
	std::thread receiver([clientSocket]() {
		while (true) {
			if (!ReceiveFromServer(clientSocket)) {
				std::cout << "Connection lost or server closed." << std::endl;
				exit(0); // Exit if the server disconnects
			}
		}
		});

	receiver.detach();

		std::string input{};
		while (std::getline(std::cin ,input)) {

#ifdef DEBUG_ASSIGNMENT4_TEST
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(5000ms);
#endif
			if (!input.empty() && (input.back() == '\r' || input.back() == '\n')) {
				input.pop_back();
			}
			if (!isManual) {
				if (input.find_first_not_of(" \t\n\r") == std::string::npos) {
					continue;
				}
			}

			if (!HandleMessage(clientSocket, input)) {
				
				break;
			}
#ifdef DEBUG_ASSIGNMENT4_TEST
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(5000ms);
#endif
		}
	


	errorCode = shutdown(clientSocket, SD_SEND);
	if (SOCKET_ERROR == errorCode)
	{
		std::cerr << "shutdown() failed." << std::endl;
	}
	closesocket(clientSocket);


	// -------------------------------------------------------------------------
	// Clean-up after Winsock.
	//
	// WSACleanup()
	// -------------------------------------------------------------------------

	WSACleanup();
}
