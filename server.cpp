#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <algorithm>
#include <vector>
#include <string>
#include <map>
#include <pthread.h>
#include <unistd.h>
#include <random>
#include <chrono>

#define DEFAULT_PORT "27015"
#define DEFAULT_BUFLEN 512

struct Player{
    SOCKET socket;         
    std::string name;       
    bool alive;             
    std::string role;       
    bool protectedByHealer; 
};

struct Room{
    std::string name;
    std::vector<Player> players; 
};

std::vector<Player> players;
std::map<SOCKET, std::string> socketToPlayerName;
std::vector<Room> rooms;

pthread_mutex_t roomMutex;
pthread_mutex_t playerMutex;
pthread_cond_t actionCond;
pthread_mutex_t actionMutex;

void BroadcastMessage(const std::string& message, const std::vector<Player>& roomPlayers){
    std::string msgWithNewline = message + "\n";
    for(const auto& player : roomPlayers){
        if(player.alive){ 
            send(player.socket, msgWithNewline.c_str(), msgWithNewline.size(), 0);
        }
    }
}

void SendMessageToClient(SOCKET clientSocket, const std::string& message){
    std::string msgWithNewline = message + "\n";
    send(clientSocket, msgWithNewline.c_str(), msgWithNewline.size(), 0);
}

//Gan vai tro cho cac nguoi choi trong phong choi
void AssignRoles(std::vector<Player>& roomPlayers){
    std::vector<std::string> roles = {"Werewolf", "Villager", "Seer", "BodyGuard", "Witch"};
    std::shuffle(roles.begin(), roles.end(), std::default_random_engine(std::random_device()()));

    for(size_t i = 0; i < roomPlayers.size(); ++i){
        roomPlayers[i].role = roles[i % roles.size()];
        roomPlayers[i].protectedByHealer = false;
    }

    for(const auto& player : roomPlayers){
        std::string roleMessage = "Your role is: " + player.role;
        SendMessageToClient(player.socket, roleMessage);
    }
}

void BodyGuardAction(Room& room){
    BroadcastMessage("BodyGuard, choose someone to protect.", room.players);
    int target = rand() % room.players.size();  
    room.players[target].protectedByHealer = true;
    BroadcastMessage(room.players[target].name + " is protected by the BodyGuard.", room.players);

    pthread_mutex_lock(&actionMutex);
    pthread_cond_signal(&actionCond);
    pthread_mutex_unlock(&actionMutex);
}

void WerewolfAction(Room& room){
    BroadcastMessage("Werewolves, choose someone to kill.", room.players);
    int target = rand() % room.players.size(); 

    if(room.players[target].protectedByHealer){
        BroadcastMessage("The attack was prevented by the BodyGuard.", room.players);
    } else{
        room.players[target].alive = false;
        BroadcastMessage(room.players[target].name + " was killed by Werewolves.", room.players);
    }

    pthread_mutex_lock(&actionMutex);
    pthread_cond_signal(&actionCond);
    pthread_mutex_unlock(&actionMutex);
}

// Hàm thực hiện hành động của Witch
void WitchAction(Room& room){
    BroadcastMessage("Witch, you have potions to use.", room.players);
    int target = rand() % room.players.size(); 
    bool useHealingPotion = rand() % 2; 
    
    if(useHealingPotion){
        room.players[target].alive = true;
        BroadcastMessage(room.players[target].name + " was saved by the Witch.", room.players);
    }else{
        room.players[target].alive = false;
        BroadcastMessage(room.players[target].name + " was killed by the Witch.", room.players);
    }

    pthread_mutex_lock(&actionMutex);
    pthread_cond_signal(&actionCond);
    pthread_mutex_unlock(&actionMutex);
}

void SeerAction(Room& room){
    BroadcastMessage("Seer, choose someone to reveal.", room.players);
    int target = rand() % room.players.size(); 
    std::string roleMessage = "The role of " + room.players[target].name + " is: " + room.players[target].role;
    BroadcastMessage(roleMessage, room.players);

    pthread_mutex_lock(&actionMutex);
    pthread_cond_signal(&actionCond);
    pthread_mutex_unlock(&actionMutex);
}

// Hàm xử lý cho mỗi client kết nối, bao gồm nhận tin nhắn từ client 
void* HandleClient(void* ClientSocketPtr){
    SOCKET ClientSocket = *(SOCKET*)ClientSocketPtr;
    char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;
    int iResult;

    iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
    if(iResult > 0){
        std::string playerName(recvbuf, iResult);
        {
            pthread_mutex_lock(&playerMutex);
            Player newPlayer = { ClientSocket, playerName, true, "", false };
            players.push_back(newPlayer);
            socketToPlayerName[ClientSocket] = playerName;
            pthread_mutex_unlock(&playerMutex);
        }
        std::string welcomeMessage = "Welcome " + playerName + " to the game!";
        SendMessageToClient(ClientSocket, welcomeMessage);
        BroadcastMessage(playerName + " has joined the game!", players);
    }
    while(true){
        iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
        if(iResult > 0){
            std::string message(recvbuf, iResult);
            std::string sender = socketToPlayerName[ClientSocket];
            if(message.rfind("/create", 0) == 0){
                std::string roomName = message.substr(8);
                pthread_mutex_lock(&roomMutex);
                rooms.push_back({roomName, {}});
                pthread_mutex_unlock(&roomMutex);
                SendMessageToClient(ClientSocket, "Room created successfully");
            } else if(message.rfind("/invite", 0) == 0){
                size_t spacePos = message.find(' ', 8);
                std::string roomName = message.substr(8, spacePos - 8);
                std::string playerName = message.substr(spacePos + 1);

                pthread_mutex_lock(&roomMutex);
                auto roomIt = std::find_if(rooms.begin(), rooms.end(), [&roomName](const Room& room){
                    return room.name == roomName;
                });

                if(roomIt != rooms.end()){
                    auto playerIt = std::find_if(players.begin(), players.end(), [&playerName](const Player& player){
                        return player.name == playerName;
                    });

                    if (playerIt != players.end()){
                        roomIt->players.push_back(*playerIt);
                        SendMessageToClient(ClientSocket, "Player invited successfully");
                    } else {
                        SendMessageToClient(ClientSocket, "Player not found");
                    }
                } else {
                    SendMessageToClient(ClientSocket, "Room not found");
                }
                pthread_mutex_unlock(&roomMutex);
    
            }else if(message.rfind("/start", 0) == 0){
                std::string roomName = message.substr(7);
                pthread_mutex_lock(&roomMutex);
                auto roomIt = std::find_if(rooms.begin(), rooms.end(), [&roomName](const Room& room){
                    return room.name == roomName;
                });

                if(roomIt != rooms.end()){
                    AssignRoles(roomIt->players);
                    BroadcastMessage("Game started!", roomIt->players);

                    pthread_mutex_lock(&actionMutex);
                    BroadcastMessage("BodyGuard, choose someone to protect using /protect <player_name>.", roomIt->players);
                    pthread_cond_wait(&actionCond, &actionMutex);

                    BroadcastMessage("Werewolves, choose someone to kill using /kill <player_name>.", roomIt->players);
                    pthread_cond_wait(&actionCond, &actionMutex);

                    BroadcastMessage("Witch, use /heal <player_name> to save or /poison <player_name> to kill.", roomIt->players);
                    pthread_cond_wait(&actionCond, &actionMutex);

                    BroadcastMessage("Seer, choose someone to reveal using /reveal <player_name>.", roomIt->players);
                    pthread_cond_wait(&actionCond, &actionMutex);
                    pthread_mutex_unlock(&actionMutex);

                    int aliveVillagers = std::count_if(roomIt->players.begin(), roomIt->players.end(), [](const Player& player){
                        return player.alive && player.role != "Werewolf";
                    });

                    int aliveWerewolves = std::count_if(roomIt->players.begin(), roomIt->players.end(), [](const Player& player){
                        return player.alive && player.role == "Werewolf";
                    });

                    if (aliveVillagers == 0) {
                        BroadcastMessage("Werewolves win!", roomIt->players);
                    } else if (aliveWerewolves == 0) {
                        BroadcastMessage("Villagers win!", roomIt->players);
                    } else {
                        BroadcastMessage("Next night...", roomIt->players);
                    }
                } else {
                    SendMessageToClient(ClientSocket, "Room not found");
                }
                pthread_mutex_unlock(&roomMutex);
            } else if(message.rfind("/protect", 0) == 0) {
                std::string targetName = message.substr(9);
                pthread_mutex_lock(&roomMutex);
                for (auto& room : rooms) {
                    for (auto& player : room.players) {
                        if (player.name == targetName) {
                            player.protectedByHealer = true;
                            BroadcastMessage(player.name + " is protected by the BodyGuard.", room.players);
                            pthread_mutex_lock(&actionMutex);
                            pthread_cond_signal(&actionCond);
                            pthread_mutex_unlock(&actionMutex);
                        }
                    }
                }
                pthread_mutex_unlock(&roomMutex);
            } else if(message.rfind("/kill", 0) == 0) {
                std::string targetName = message.substr(6);
                pthread_mutex_lock(&roomMutex);
                for (auto& room : rooms) {
                    for (auto& player : room.players) {
                        if (player.name == targetName) {
                            if (player.protectedByHealer) {
                                BroadcastMessage("The attack on " + player.name + " was prevented by the BodyGuard.", room.players);
                            } else {
                                player.alive = false;
                                BroadcastMessage(player.name + " was killed by Werewolves.", room.players);
                            }
                            pthread_mutex_lock(&actionMutex);
                            pthread_cond_signal(&actionCond);
                            pthread_mutex_unlock(&actionMutex);
                        }
                    }
                }
                pthread_mutex_unlock(&roomMutex);
            } else if(message.rfind("/heal", 0) == 0) {
                std::string targetName = message.substr(6);
                pthread_mutex_lock(&roomMutex);
                for (auto& room : rooms) {
                    for (auto& player : room.players) {
                        if (player.name == targetName) {
                            player.alive = true;
                            BroadcastMessage(player.name + " was saved by the Witch.", room.players);
                            pthread_mutex_lock(&actionMutex);
                            pthread_cond_signal(&actionCond);
                            pthread_mutex_unlock(&actionMutex);
                        }
                    }
                }
                pthread_mutex_unlock(&roomMutex);
            } else if(message.rfind("/poison", 0) == 0) {
                std::string targetName = message.substr(8);
                pthread_mutex_lock(&roomMutex);
                for (auto& room : rooms) {
                    for (auto& player : room.players) {
                        if (player.name == targetName) {
                            player.alive = false;
                            BroadcastMessage(player.name + " was killed by the Witch.", room.players);
                            pthread_mutex_lock(&actionMutex);
                            pthread_cond_signal(&actionCond);
                            pthread_mutex_unlock(&actionMutex);
                        }
                    }
                }
                pthread_mutex_unlock(&roomMutex);
            } else if(message.rfind("/reveal", 0) == 0) {
                std::string targetName = message.substr(8);
                pthread_mutex_lock(&roomMutex);
                for (auto& room : rooms) {
                    for (auto& player : room.players) {
                        if (player.name == targetName) {
                            std::string roleMessage = "The role of " + player.name + " is: " + player.role;
                            BroadcastMessage(roleMessage, room.players);
                            pthread_mutex_lock(&actionMutex);
                            pthread_cond_signal(&actionCond);
                            pthread_mutex_unlock(&actionMutex);
                        }
                    }
                }
                pthread_mutex_unlock(&roomMutex);
            } else {
                BroadcastMessage(sender + ": " + message, players);
            }
        } else if(iResult == 0){
            std::string playerName = socketToPlayerName[ClientSocket];
            pthread_mutex_lock(&playerMutex);
            auto it = std::remove_if(players.begin(), players.end(), [ClientSocket](const Player& player) {
                return player.socket == ClientSocket;
            });
            players.erase(it, players.end());
            socketToPlayerName.erase(ClientSocket);
            pthread_mutex_unlock(&playerMutex);
            BroadcastMessage(playerName + " has left the game.", players);
            closesocket(ClientSocket);
            break;
        } else{
            std::string playerName = socketToPlayerName[ClientSocket];
            pthread_mutex_lock(&playerMutex);
            auto it = std::remove_if(players.begin(), players.end(), [ClientSocket](const Player& player) {
                return player.socket == ClientSocket;
            });
            players.erase(it, players.end());
            socketToPlayerName.erase(ClientSocket);
            pthread_mutex_unlock(&playerMutex);
            BroadcastMessage(playerName + " has left the game due to an error.", players);
            closesocket(ClientSocket);
            break;
        }
    }

    pthread_exit(NULL);
}

int main(){
    WSADATA wsaData;
    SOCKET ListenSocket = INVALID_SOCKET;
    SOCKET ClientSocket = INVALID_SOCKET;
    struct addrinfo* result = NULL;
    struct addrinfo hints;
    struct sockaddr_in serverAddr;

    int iResult;

    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if(iResult != 0){
        printf("WSAStartup failed: %d\n", iResult);
        return 1;
    }

    ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ListenSocket == INVALID_SOCKET) {
        printf("socket failed: %ld\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(atoi(DEFAULT_PORT));

    iResult = bind(ListenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    if (iResult == SOCKET_ERROR) {
        printf("bind failed: %d\n", WSAGetLastError());
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    iResult = listen(ListenSocket, SOMAXCONN);
    if (iResult == SOCKET_ERROR) {
        printf("listen failed: %d\n", WSAGetLastError());
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    pthread_mutex_init(&playerMutex, NULL);
    pthread_mutex_init(&roomMutex, NULL);
    pthread_mutex_init(&actionMutex, NULL);
    pthread_cond_init(&actionCond, NULL);

    while(true){
        ClientSocket = accept(ListenSocket, NULL, NULL);
        if (ClientSocket == INVALID_SOCKET) {
            printf("accept failed: %d\n", WSAGetLastError());
            closesocket(ListenSocket);
            WSACleanup();
            return 1;
        }

        pthread_t clientThread;
        int ret = pthread_create(&clientThread, NULL, HandleClient, &ClientSocket);
        if(ret != 0){
            printf("pthread_create failed: %d\n", ret);
            closesocket(ClientSocket);
            continue;
        }
        pthread_detach(clientThread);
    }

    closesocket(ListenSocket);
    WSACleanup();

    return 0;
}
