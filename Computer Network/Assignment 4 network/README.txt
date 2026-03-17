/* Start Header
**************************************************************** */
/*!
\file    README.txt
\author  Elijah Chua & Guan Shao Jun
\par     email: e.chua@digipen.edu, shaojun.g@digipen.edu
\date    16 Mar, 2026
\brief
    Documentation for the Reliable UDP File Transfer System.

Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the 
prior written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
****************************************************************** */

--- HOW TO RUN ---
1. Open Assignment4.slnx in Visual Studio.
2. Build and start the solution (F5). Ensure the Server starts before the Client.

--- SERVER SETUP ---
When prompted in the server terminal, provide the configuration:
- Server TCP Port Number: (e.g., 8888)
- Server UDP Port Number: (e.g., 9999)
- Files Path: The absolute path to the folder containing the files you want to host (e.g., E:\code).

--- CLIENT SETUP ---
When prompted in the client terminal, connect using the server's details:
- Server IP Address: The IPv4 address of the server machine.
- Server Port Number: The TCP port the server is listening on (e.g., 8888).
- Server UDP Port Number: The UDP port the server is using (e.g., 9999).
- Client UDP Port Number: The port this client will use to receive files (e.g., 9000). 
  *Note: If testing multiple clients on the exact same computer, every client MUST use a unique UDP port (e.g., 9000, 9001, 9002).*
- Path to store downloads: The absolute path to where downloaded files should be saved (e.g., E:\client).

--- CLIENT COMMANDS ---
Once connected, you can use the following commands in the client terminal:

/l 
    Lists all available downloadable files in the server's hosted directory.

/d <ClientIP>:<ClientUDPPort> <filename>
    Requests a file download from the server. 
    Example: /d 192.168.1.54:9000 test.zip

/q
    Gracefully disconnects from the server and quits the client application.

--- TROUBLESHOOTING ---
If testing across two different devices (e.g., over a Wi-Fi hotspot) and the download stays at 0%, ensure you have allowed the application through the Windows Defender Firewall for both "Private" and "Public" networks.