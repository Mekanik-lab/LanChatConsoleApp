#include <iostream>
#include <thread>
#include <cstring>
#include <string>
#include <vector>
#include <map> 
#include <mutex>
#include <algorithm>
#include <functional>

// Winsock headers with explicit library linkage
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10

std::mutex clientsMutex;
std::vector<SOCKET> clientSockets;
std::map<SOCKET, std::string> clientNicknames;

// Broadcast message to all clients except the sender
void broadcastMessage(const std::string& message, SOCKET senderSocket) {
    std::lock_guard<std::mutex> lock(clientsMutex);
    for (const auto& socket : clientSockets) {
        if (socket != senderSocket) {
            send(socket, message.c_str(), static_cast<int>(message.length()), 0);
        }
    }
}

// Handle client connection
void handleClient(SOCKET clientSocket) {
    char buffer[BUFFER_SIZE];
    std::string nickname;

    // First message should be the nickname
    memset(buffer, 0, BUFFER_SIZE);
    int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
    if (bytesReceived > 0) {
        nickname = std::string(buffer);

        // Store the nickname
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            clientNicknames[clientSocket] = nickname;
        }

        // Announce new user
        std::string welcomeMsg = nickname + " has joined the chat!";
        broadcastMessage(welcomeMsg, INVALID_SOCKET);
        std::cout << welcomeMsg << std::endl;
    }

    // Message loop
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);

        if (bytesReceived <= 0) {
            // Client disconnected
            std::string disconnectMsg;
            {
                std::lock_guard<std::mutex> lock(clientsMutex);
                auto nicknameIt = clientNicknames.find(clientSocket);
                if (nicknameIt != clientNicknames.end()) {
                    disconnectMsg = nicknameIt->second + " has left the chat!";
                }
                else {
                    disconnectMsg = "A user has left the chat!";
                }

                // Remove client from data structures
                auto it = std::find(clientSockets.begin(), clientSockets.end(), clientSocket);
                if (it != clientSockets.end()) {
                    clientSockets.erase(it);
                }
                clientNicknames.erase(clientSocket);
            }

            broadcastMessage(disconnectMsg, INVALID_SOCKET);
            std::cout << disconnectMsg << std::endl;
            closesocket(clientSocket);
            break;
        }

        // Format and broadcast the message
        std::string senderNickname;
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            auto nicknameIt = clientNicknames.find(clientSocket);
            if (nicknameIt != clientNicknames.end()) {
                senderNickname = nicknameIt->second;
            }
            else {
                senderNickname = "Unknown";
            }
        }

        std::string message = senderNickname + ": " + buffer;
        broadcastMessage(message, clientSocket);
        std::cout << message << std::endl;
    }
}

void startServer() {
    // Initialize Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "Failed to initialize Winsock. Error Code: " << WSAGetLastError() << std::endl;
        return;
    }

    // Create a socket
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Socket failed. Error Code: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return;
    }

    // Prepare the sockaddr_in structure
    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    // Bind
    if (bind(serverSocket, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        std::cerr << "Bind failed. Error Code: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return;
    }

    // Listen
    if (listen(serverSocket, 10) == SOCKET_ERROR) {
        std::cerr << "Listen failed. Error Code: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return;
    }

    std::cout << "Chat server started on port " << PORT << std::endl;
    std::cout << "Waiting for connections...\n";

    // Accept connections
    sockaddr_in client;
    int clientSize = sizeof(sockaddr_in);

    while (true) {
        SOCKET clientSocket = accept(serverSocket, (struct sockaddr*)&client, &clientSize);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Accept failed. Error Code: " << WSAGetLastError() << std::endl;
            continue;
        }

        // Check if max clients reached
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            if (clientSockets.size() >= MAX_CLIENTS) {
                const char* msg = "Server is full. Please try again later.";
                send(clientSocket, msg, static_cast<int>(strlen(msg)), 0);
                closesocket(clientSocket);
                continue;
            }

            // Add to client list
            clientSockets.push_back(clientSocket);
        }

        // Start client thread
        try {
            std::thread clientThread(handleClient, clientSocket);
            clientThread.detach();
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to create thread: " << e.what() << std::endl;
            std::lock_guard<std::mutex> lock(clientsMutex);
            auto it = std::find(clientSockets.begin(), clientSockets.end(), clientSocket);
            if (it != clientSockets.end()) {
                clientSockets.erase(it);
            }
            closesocket(clientSocket);
        }
    }

    // Cleanup
    closesocket(serverSocket);
    WSACleanup();
}

void receiveMessages(SOCKET socket) {
    char buffer[BUFFER_SIZE];
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytesReceived = recv(socket, buffer, BUFFER_SIZE, 0);
        if (bytesReceived <= 0) {
            std::cout << "Disconnected from server.\n";
            break;
        }
        std::cout << buffer << std::endl;
    }
}

void startClient(const char* server_ip) {
    // Initialize Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "Failed to initialize Winsock. Error Code: " << WSAGetLastError() << std::endl;
        return;
    }

    // Create a socket
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation error. Error Code: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return;
    }

    // Prepare the sockaddr_in structure
    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);

    // Convert IP address from text to binary
    if (inet_pton(AF_INET, server_ip, &server.sin_addr) <= 0) {
        std::cerr << "Invalid address. Error Code: " << WSAGetLastError() << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return;
    }

    // Connect to server
    if (connect(clientSocket, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        std::cerr << "Connection Failed. Error Code: " << WSAGetLastError() << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return;
    }

    // Get nickname from user
    std::string nickname;
    std::cout << "Enter your nickname: ";
    std::getline(std::cin, nickname);

    // Send nickname to server
    send(clientSocket, nickname.c_str(), static_cast<int>(nickname.length()), 0);

    std::cout << "Connected to server as " << nickname << "!\n";

    // Start receive thread
    std::thread recvThread(receiveMessages, clientSocket);
    recvThread.detach();

    // Message loop
    std::string message;
    while (true) {
        std::getline(std::cin, message);
        if (message == "/quit") {
            break;
        }
        send(clientSocket, message.c_str(), static_cast<int>(message.length()), 0);
    }

    // Cleanup
    closesocket(clientSocket);
    WSACleanup();
}

int main(int argc, char const* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: \n";
        std::cout << "  Server: " << argv[0] << " server\n";
        std::cout << "  Client: " << argv[0] << " client <server_ip>\n";
        return 1;
    }

    if (strcmp(argv[1], "server") == 0) {
        startServer();
    }
    else if (strcmp(argv[1], "client") == 0 && argc == 3) {
        startClient(argv[2]);
    }
    else {
        std::cout << "Invalid arguments.\n";
        return 1;
    }

    return 0;
}