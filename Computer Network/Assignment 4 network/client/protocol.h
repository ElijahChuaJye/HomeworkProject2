/* Start Header
*****************************************************************/
/*!
\file   protocol.h
\author Elijah Chua & Shao Jun
\par    e.chua@digipen.edu,
\date   3/9/2025
\brief  Shared protocol definitions for Assignment 4.
...
*/
/* End Header
*******************************************************************/
#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <cstdint>
#pragma pack(push, 1) // Prevents the compiler from adding "hidden" padding bytes

enum CMDID : unsigned char {
    REQ_QUIT = 0x1,
    REQ_DOWNLOAD = 0x2,
    RSP_DOWNLOAD = 0x3,
    REQ_LISTFILES = 0x4,
    RSP_LISTFILES = 0x5,
    DOWNLOAD_ERROR = 0x30
};

struct ReqDownloadHeader {
    unsigned char cmd;         //1 byte for command
    unsigned char ip[4];       //4 bytes for IP
    unsigned short port;       //2 bytes for port
    unsigned int nameLen;     //4 bytes for filename length
    //Followed by filename, name of the file include path. Example: filelist.cpp
};

struct RspDownloadHeader {
    unsigned char cmd;          //1 byte for command
    unsigned char ip[4];        //4 bytes Server's portnumber
    unsigned short port;        //2 bytes Server port number for file downloading
    unsigned int sessionID;    //4 bytes ID for the file download session. Each download is a new session
    unsigned int fileLen;      //4 bytes File length
};

struct RspListHeader {
    unsigned char cmd;          //1 bytes c
    unsigned short numFiles;   //2 bytes
    unsigned int listLen;      //4 bytes
};
struct UdpDataHeader {
    uint32_t sessionID;   // 4 bytes: the downloading session [cite: 298]
    uint32_t fileLen;     // 4 bytes: the download file length [cite: 299]
    uint32_t fileOffset;  // 4 bytes: the position in the download file [cite: 300]
    uint32_t dataLen;     // 4 bytes: length of file data in this message [cite: 302]
    // The actual File Data bytes will be appended immediately after this header [cite: 303]
};

// UDP ACK Datagram [cite: 305]
struct UdpAckHeader {
    unsigned char flags;  // 1 byte: LSB indicates whether this is an ACK [cite: 306]
    uint32_t ackNum;      // 4 bytes: acknowledgement for receiving datagram [cite: 307]
    uint32_t seqNum;      // 4 bytes: sequence number of the datagram carrying file data [cite: 308]
};
#pragma pack(pop)
#endif
