#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <time.h>

#pragma comment(lib, "Ws2_32.lib")

#define PORT 5924
#define TIME_WINDOW 10
#define ZONES_FILE "zones.txt"
#define MAX_ALLOWED_IPS 10

time_t lastConnectionDisplayTime = 0;
static int connectionCount = 0;  // Compteur global des connexions

typedef struct {
    char ip[INET_ADDRSTRLEN];
    time_t lastConnectionTime;
    int connectionCount;
} IPRecord;

const char* allowedIPs[MAX_ALLOWED_IPS] = {
    "74.210.183.203",
    //"77.57.57.9",
    //"184.160.229.142"
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
        printf("Erreur d'allocation mémoire pour les zones.\n");
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
        printf("Erreur d'ouverture du fichier zones.txt pour la sauvegarde.\n");
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
        printf("Fichier zones.txt non trouvé. Création d'un nouveau fichier.\n");
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
    // Libération de la mémoire et initialisation des zones
    free(zones);
    zones = NULL;
    zoneCount = 0;
    loadZonesFromFile();

    // Création d'un buffer pour stocker uniquement les zones
    char buffer[2048] = { 0 };
    size_t offset = 0;

    // Ajout des zones dans le buffer sans la version
    for (size_t i = 0; i < zoneCount; i++) {
        int requiredSize = snprintf(NULL, 0, "%f,%f,%f,%f,%lf", zones[i].x1, zones[i].y1, zones[i].x2, zones[i].y2, zones[i].maxDistance);
        if (offset + requiredSize < sizeof(buffer)) {
            offset += sprintf_s(buffer + offset, sizeof(buffer) - offset,
                "%f,%f,%f,%f,%lf",
                zones[i].x1, zones[i].y1, zones[i].x2, zones[i].y2, zones[i].maxDistance);

            // Ajouter un séparateur entre les zones, sauf après la dernière zone
            if (i < zoneCount - 1) {
                offset += sprintf_s(buffer + offset, sizeof(buffer) - offset, ";");
            }
        }
        else {
            printf("Erreur : le buffer est plein, certaines zones n'ont pas été incluses.\n");
            break;
        }
    }

    // Envoi du buffer contenant uniquement les zones
    send(clientSocket, buffer, (int)strlen(buffer), 0);
}

static void sendVersionZones(SOCKET clientSocket) {
    // Libération de la mémoire et initialisation des zones
    free(zones);
    zones = NULL;
    zoneCount = 0;
    loadZonesFromFile();

    // Création d'un buffer pour stocker la version et les zones
    char buffer[4096] = { 0 };
    size_t offset = 0;

    // Ajout de la version dans le buffer
    const char* version = "2.0.0";  // Exemple de version
    offset += sprintf_s(buffer + offset, sizeof(buffer) - offset, "VERSION: %s\n", version);

    // Ajout des zones dans le même buffer
    offset += sprintf_s(buffer + offset, sizeof(buffer) - offset, "ZONES: ");
    for (size_t i = 0; i < zoneCount; i++) {
        int requiredSize = snprintf(NULL, 0, "%f,%f,%f,%f,%lf", zones[i].x1, zones[i].y1, zones[i].x2, zones[i].y2, zones[i].maxDistance);
        if (offset + requiredSize < sizeof(buffer)) {
            offset += sprintf_s(buffer + offset, sizeof(buffer) - offset,
                "%f,%f,%f,%f,%lf",
                zones[i].x1, zones[i].y1, zones[i].x2, zones[i].y2, zones[i].maxDistance);

            // Ajouter un séparateur entre les zones, sauf après la dernière zone
            if (i < zoneCount - 1) {
                offset += sprintf_s(buffer + offset, sizeof(buffer) - offset, ";");
            }
        }
        else {
            printf("Erreur : le buffer est plein, certaines zones n'ont pas été incluses.\n");
            break;
        }
    }

    // Terminer la chaîne par un '\n'
    offset += sprintf_s(buffer + offset, sizeof(buffer) - offset, "\n");

    // Envoi du buffer complet en un seul paquet
    send(clientSocket, buffer, (int)strlen(buffer), 0);
}

static void updateZone(SOCKET clientSocket, const char* request) {
    free(zones);
    zones = NULL;
    zoneCount = 0;

    char* context = NULL;
    char* token = strtok_s((char*)request + 7, ";", &context);  // Ignorer "UPDATE "

    while (token != NULL) {
        float x1, y1, x2, y2;
        double maxDistance;

        // Séparer les coordonnées avec sscanf
        if (sscanf_s(token, "%f,%f,%f,%f,%lf", &x1, &y1, &x2, &y2, &maxDistance) == 5) {
            addZone(x1, y1, x2, y2, maxDistance);
        }
        else {
            const char* response = "Format de commande incorrect pour une ou plusieurs zones.\n";
            send(clientSocket, response, (int)strlen(response), 0);
            return;
        }

        token = strtok_s(NULL, ";", &context);  // Passer à la prochaine zone
    }

    saveZonesToFile();  // Sauvegarder les zones dans le fichier après mise à jour
    const char* response = "Zones mises à jour et sauvegardées.\n";
    send(clientSocket, response, (int)strlen(response), 0);
}

// Fonction qui gère une connexion client
static DWORD WINAPI handleClientConnection(LPVOID lpParam) {
    SOCKET clientSocket = (SOCKET)lpParam;  // Récupère le socket du client
    struct sockaddr_in clientAddr = { 0 };
    int clientAddrLen = sizeof(clientAddr);

    // Récupère l'IP du client
    getpeername(clientSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));

    // Obtient l'heure actuelle
    time_t currentTime;
    time(&currentTime);

    // Vérifie si 5 secondes se sont écoulées depuis le dernier affichage de l'IP
    if (difftime(currentTime, lastConnectionDisplayTime) >= 5) {
        // Si oui, affiche l'IP, le nombre total de connexions et met à jour l'heure de l'affichage
        printf("Connexion d'un client avec l'IP: %s (Total des connexions: %d)\n", clientIP, ++connectionCount);
        lastConnectionDisplayTime = currentTime;  // Met à jour l'heure de l'affichage
    }

    // Traitement des requêtes du client
    char buffer[2048] = { 0 };
    int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesRead > 0) {
        buffer[bytesRead] = '\0';

        // Traitement des requêtes
        char* context = NULL;
        char* request = strtok_s(buffer, "\n", &context);  // Découpe les requêtes par lignes

        while (request != NULL) {
            printf("Traitement de la requête: %s\n", request);

            // Traitement des commandes

            if (strncmp(request, "FUSION", 5) == 0) {
                sendVersionZones(clientSocket);  // Envoie version et zones
            }
            else if (strncmp(request, "ZONES_ONLY", 10) == 0) {
                sendZonesOnly(clientSocket);  // Envoie uniquement les zones
            }
            else if (strncmp(request, "UPDATE ", 7) == 0) {
                if (isIPAllowed(clientIP)) {
                    updateZone(clientSocket, request);  // Mise à jour des zones
                }
                else {
                    const char* response = "Vous n'êtes pas autorisé à mettre à jour les zones.\n";
                    send(clientSocket, response, (int)strlen(response), 0);
                }
            }
            else {
                const char* response = "Commande inconnue.\n";
                send(clientSocket, response, (int)strlen(response), 0);
            }

            // Passe à la prochaine requête
            request = strtok_s(NULL, "\n", &context);
        }
    }

    closesocket(clientSocket);  // Ferme la connexion une fois traitée
    return 0;
}

int main() {
    WSADATA wsaData;
    SOCKET serverSocket, clientSocket;
    struct sockaddr_in serverAddr = { 0 }, clientAddr = { 0 };
    int clientAddrLen = sizeof(clientAddr);

    SetConsoleOutputCP(CP_UTF8);

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Erreur de WSAStartup: %d\n", WSAGetLastError());
        return 1;
    }

    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        printf("Erreur de création de socket: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        printf("Erreur de bind: %d\n", WSAGetLastError());
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        printf("Erreur de listen: %d\n", WSAGetLastError());
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    printf("Serveur en écoute sur le port %d...\n", PORT);

    // Accepter les connexions et créer un thread pour chaque client
    while (1) {
        clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            printf("Erreur de connexion client: %d\n", WSAGetLastError());
            continue;
        }

        // Crée un thread pour gérer la connexion du client
        CreateThread(NULL, 0, handleClientConnection, (LPVOID)clientSocket, 0, NULL);
    }

    closesocket(serverSocket);
    WSACleanup();
    free(zones);
    return 0;
}
