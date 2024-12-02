MIT License

Copyright (c) 2024 Dino_Rex

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is provided to do so, subject to the following conditions:

1. Plugin creators must credit the author (Dino_Rex) and include a link to the Discord https://discord.gg/tFBbQzmDaZ in both the source code and the description of the compiled Mumble plugin.
2. All usage of this code must remain open source. A link to the open-source project must be shared on the Discord https://discord.gg/tFBbQzmDaZ in the "open-source" channel.
3. Redistribution, modification, or use of the code in any way is allowed only if these conditions are met.
4. This code may not be used in any proprietary, non-open-source software.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <time.h>

#pragma comment(lib, "Ws2_32.lib")

#define PORT 1234
#define TIME_WINDOW 10
#define ZONES_FILE "zones.txt"
#define MAX_ALLOWED_IPS 10

time_t lastConnectionDisplayTime = 0;
static int connectionCount = 0;  // Global connection counter

typedef struct {
    char ip[INET_ADDRSTRLEN];
    time_t lastConnectionTime;
    int connectionCount;
} IPRecord;

const char* allowedIPs[MAX_ALLOWED_IPS] = {
    "192.168.0.1",
    //"192.168.0.1",
    //"192.168.0.1"
};
size_t allowedIPCount = 3;

static int isIPAllowed(const char* ip) {
    for (size_t i = 0; i < allowedIPCount; i++) {
        if (strcmp(allowedIPs[i], ip) == 0) {
            return 1;
        }
    }
    return 0;
}

struct Zone {
    float x1, y1, x2, y2;
    double maxDistance;
};

struct Zone* zones = NULL;
size_t zoneCount = 0;

static void addZone(float x1, float y1, float x2, float y2, double maxDistance) {
    struct Zone* temp = (struct Zone*)realloc(zones, (zoneCount + 1) * sizeof(struct Zone));
    if (temp == NULL) {
        printf("Memory allocation error for zones.\n");
        return;
    }
    zones = temp;
    zones[zoneCount].x1 = x1;
    zones[zoneCount].y1 = y1;
    zones[zoneCount].x2 = x2;
    zones[zoneCount].y2 = y2;
    zones[zoneCount].maxDistance = maxDistance;
    zoneCount++;
}

static void saveZonesToFile() {
    FILE* file;
    fopen_s(&file, ZONES_FILE, "w");
    if (file == NULL) {
        printf("Error opening zones.txt file for saving.\n");
        return;
    }
    for (size_t i = 0; i < zoneCount; i++) {
        fprintf(file, "%f %f %f %f %lf\n", zones[i].x1, zones[i].y1, zones[i].x2, zones[i].y2, zones[i].maxDistance);
    }
    fclose(file);
}

static void loadZonesFromFile() {
    FILE* file;
    fopen_s(&file, ZONES_FILE, "r");
    if (file == NULL) {
        printf("zones.txt file not found. Creating a new file.\n");
        saveZonesToFile();
        return;
    }
    float x1, y1, x2, y2;
    double maxDistance;
    while (fscanf_s(file, "%f %f %f %f %lf", &x1, &y1, &x2, &y2, &maxDistance) == 5) {
        addZone(x1, y1, x2, y2, maxDistance);
    }
    fclose(file);
}

static void sendZonesOnly(SOCKET clientSocket) {
    // Free memory and initialize zones
    free(zones);
    zones = NULL;
    zoneCount = 0;
    loadZonesFromFile();

    // Create a buffer to store only zones
    char buffer[2048] = { 0 };
    size_t offset = 0;

    // Add zones to the buffer without version
    for (size_t i = 0; i < zoneCount; i++) {
        int requiredSize = snprintf(NULL, 0, "%f,%f,%f,%f,%lf", zones[i].x1, zones[i].y1, zones[i].x2, zones[i].y2, zones[i].maxDistance);
        if (offset + requiredSize < sizeof(buffer)) {
            offset += sprintf_s(buffer + offset, sizeof(buffer) - offset,
                "%f,%f,%f,%f,%lf",
                zones[i].x1, zones[i].y1, zones[i].x2, zones[i].y2, zones[i].maxDistance);

            // Add a separator between zones, except after the last zone
            if (i < zoneCount - 1) {
                offset += sprintf_s(buffer + offset, sizeof(buffer) - offset, ";");
            }
        }
        else {
            printf("Error: buffer is full, some zones were not included.\n");
            break;
        }
    }

    // Send the buffer containing only the zones
    send(clientSocket, buffer, (int)strlen(buffer), 0);
}

static void sendVersionZones(SOCKET clientSocket) {
    // Free memory and initialize zones
    free(zones);
    zones = NULL;
    zoneCount = 0;
    loadZonesFromFile();

    // Create a buffer to store version and zones
    char buffer[4096] = { 0 };
    size_t offset = 0;

    // Add version to the buffer
    const char* version = "2.0.0";  // Example version
    offset += sprintf_s(buffer + offset, sizeof(buffer) - offset, "VERSION: %s\n", version);

    // Add zones to the same buffer
    offset += sprintf_s(buffer + offset, sizeof(buffer) - offset, "ZONES: ");
    for (size_t i = 0; i < zoneCount; i++) {
        int requiredSize = snprintf(NULL, 0, "%f,%f,%f,%f,%lf", zones[i].x1, zones[i].y1, zones[i].x2, zones[i].y2, zones[i].maxDistance);
        if (offset + requiredSize < sizeof(buffer)) {
            offset += sprintf_s(buffer + offset, sizeof(buffer) - offset,
                "%f,%f,%f,%f,%lf",
                zones[i].x1, zones[i].y1, zones[i].x2, zones[i].y2, zones[i].maxDistance);

            // Add a separator between zones, except after the last zone
            if (i < zoneCount - 1) {
                offset += sprintf_s(buffer + offset, sizeof(buffer) - offset, ";");
            }
        }
        else {
            printf("Error: buffer is full, some zones were not included.\n");
            break;
        }
    }

    // End the string with a '\n'
    offset += sprintf_s(buffer + offset, sizeof(buffer) - offset, "\n");

    // Send the complete buffer in a single packet
    send(clientSocket, buffer, (int)strlen(buffer), 0);
}

static void updateZone(SOCKET clientSocket, const char* request) {
    free(zones);
    zones = NULL;
    zoneCount = 0;

    char* context = NULL;
    char* token = strtok_s((char*)request + 7, ";", &context);  // Ignore "UPDATE "

    while (token != NULL) {
        float x1, y1, x2, y2;
        double maxDistance;

        // Separate coordinates with sscanf
        if (sscanf_s(token, "%f,%f,%f,%f,%lf", &x1, &y1, &x2, &y2, &maxDistance) == 5) {
            addZone(x1, y1, x2, y2, maxDistance);
        }
        else {
            const char* response = "Incorrect command format for one or more zones.\n";
            send(clientSocket, response, (int)strlen(response), 0);
            return;
        }

        token = strtok_s(NULL, ";", &context);  // Move to the next zone
    }

    saveZonesToFile();  // Save zones to file after update
    const char* response = "Zones updated and saved.\n";
    send(clientSocket, response, (int)strlen(response), 0);
}

// Function to handle a client connection
static DWORD WINAPI handleClientConnection(LPVOID lpParam) {
    SOCKET clientSocket = (SOCKET)lpParam;  // Get client socket
    struct sockaddr_in clientAddr = { 0 };
    int clientAddrLen = sizeof(clientAddr);

    // Get the client's IP
    getpeername(clientSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));

    // Get current time
    time_t currentTime;
    time(&currentTime);

    // Check if 5 seconds have passed since the last IP display
    if (difftime(currentTime, lastConnectionDisplayTime) >= 5) {
        // If so, display the IP, total connection count, and update the display time
        printf("Client connected with IP: %s (Total connections: %d)\n", clientIP, ++connectionCount);
        lastConnectionDisplayTime = currentTime;  // Update display time
    }

    // Process client requests
    char buffer[2048] = { 0 };
    int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesRead > 0) {
        buffer[bytesRead] = '\0';

        // Process requests
        char* context = NULL;
        char* request = strtok_s(buffer, "\n", &context);  // Split requests by line

        while (request != NULL) {
            printf("Processing request: %s\n", request);

            // Process commands

            if (strncmp(request, "FUSION", 5) == 0) {
                sendVersionZones(clientSocket);  // Send version and zones
            }
            else if (strncmp(request, "ZONES_ONLY", 10) == 0) {
                sendZonesOnly(clientSocket);  // Send only zones
            }
            else if (strncmp(request, "UPDATE ", 7) == 0) {
                if (isIPAllowed(clientIP)) {
                    updateZone(clientSocket, request);  // Update zones
                }
                else {
                    const char* response = "You are not authorized to update the zones.\n";
                    send(clientSocket, response, (int)strlen(response), 0);
                }
            }
            else {
                const char* response = "Unknown command.\n";
                send(clientSocket, response, (int)strlen(response), 0);
            }

            // Move to the next request
            request = strtok_s(NULL, "\n", &context);
        }
    }

    closesocket(clientSocket);  // Close the connection after processing
    return 0;
}

int main() {
    WSADATA wsaData;
    SOCKET serverSocket, clientSocket;
    struct sockaddr_in serverAddr = { 0 }, clientAddr = { 0 };
    int clientAddrLen = sizeof(clientAddr);

    SetConsoleOutputCP(CP_UTF8);

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup error: %d\n", WSAGetLastError());
        return 1;
    }

    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        printf("Socket creation error: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        printf("Bind error: %d\n", WSAGetLastError());
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        printf("Listen error: %d\n", WSAGetLastError());
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    printf("Server listening on port %d...\n", PORT);

    // Accept connections and create a thread for each client
    while (1) {
        clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            printf("Client connection error: %d\n", WSAGetLastError());
            continue;
        }

        // Create a thread to handle the client connection
        CreateThread(NULL, 0, handleClientConnection, (LPVOID)clientSocket, 0, NULL);
    }

    closesocket(serverSocket);
    WSACleanup();
    free(zones);
    return 0;
}
