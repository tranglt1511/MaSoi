#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <iostream>
#include <pthread.h>
#include <string>

#define DEFAULT_PORT "27015"
#define DEFAULT_BUFLEN 512

std::string playerRole;

//Nhan va hien thi cac thong diep tu Server
void* ReceiveMessages(void* socket){
    SOCKET ConnectSocket = *(SOCKET*)socket;
    char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;
    int iResult;

    while(true){
        iResult = recv(ConnectSocket, recvbuf, recvbuflen, 0);
        if(iResult > 0){
            std::string message(recvbuf, iResult);
            std::cout << message << std::endl;

            if(message.find("Your role is:") != std::string::npos){
                playerRole = message.substr(message.find(":") + 2);
                std::cout << "You are a " << playerRole << "!" << std::endl;
            }

            if(message.find("It's your turn to act") != std::string::npos){
                if (playerRole == "Werewolf"){
                    std::string target;
                    std::cout << "Enter the name of the player you want to kill: ";
                    std::getline(std::cin, target);
                    std::string actionMessage = "/action kill " + target + "\n";
                    send(ConnectSocket, actionMessage.c_str(), actionMessage.size(), 0);
                } else if(playerRole == "BodyGuard"){
                    std::string target;
                    std::cout << "Enter the name of the player you want to protect: ";
                    std::getline(std::cin, target);
                    std::string actionMessage = "/action heal " + target + "\n";
                    send(ConnectSocket, actionMessage.c_str(), actionMessage.size(), 0);
                } else if(playerRole == "Witch"){
                    std::string action;
                    std::cout << "Enter 'save' to use healing potion or 'kill' to use poison potion: ";
                    std::getline(std::cin, action);
                    if(action == "save"){
                        std::cout << "Enter the name of the player you want to save: ";
                        std::string target;
                        std::getline(std::cin, target);
                        std::string actionMessage = "/action save " + target + "\n";
                        send(ConnectSocket, actionMessage.c_str(), actionMessage.size(), 0);
                    } else if(action == "kill") {
                        std::cout << "Enter the name of the player you want to kill: ";
                        std::string target;
                        std::getline(std::cin, target);
                        std::string actionMessage = "/action poison " + target + "\n";
                        send(ConnectSocket, actionMessage.c_str(), actionMessage.size(), 0);
                    }
                } else if(playerRole == "Seer"){
                    std::string target;
                    std::cout << "Enter the name of the player you want to check: ";
                    std::getline(std::cin, target);
                    std::string actionMessage = "/action check " + target + "\n";
                    send(ConnectSocket, actionMessage.c_str(), actionMessage.size(), 0);
                }
            }
        }else if(iResult == 0){
            printf("Connection closing...\n");
            break;
        }else{
            printf("recv failed: %d\n", WSAGetLastError());
            break;
        }
    }
    return NULL;
}

//Khoi tao client va ket noi toi server 
int main(){
    WSADATA wsaData;
    SOCKET ConnectSocket = INVALID_SOCKET;
    struct sockaddr_in server;
    char recvbuf[DEFAULT_BUFLEN];
    int iResult;

    std::string serverAddress = "192.168.1.42";  
    std::string playerName;

    std::cout << "Enter your name: ";
    std::getline(std::cin, playerName);
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); 

    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0){
        printf("WSAStartup failed: %d", iResult);
        return 1;
    }

    ConnectSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ConnectSocket == INVALID_SOCKET) {
        printf("Error at socket(): %d", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    
    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(DEFAULT_PORT));
    server.sin_addr.s_addr = inet_addr(serverAddress.c_str());

    iResult = connect(ConnectSocket, (struct sockaddr*)&server, sizeof(server));
    if(iResult == SOCKET_ERROR){
        printf("Unable to connect to server: %d", WSAGetLastError());
        closesocket(ConnectSocket);
        WSACleanup();
        return 1;
    }

    iResult = send(ConnectSocket, (playerName + "").c_str(), playerName.size() + 1, 0);
    if(iResult == SOCKET_ERROR){
        printf("send failed: %d", WSAGetLastError());
        closesocket(ConnectSocket);
        WSACleanup();
        return 1;
    }

    pthread_t recvThread;
    pthread_create(&recvThread, NULL, ReceiveMessages, &ConnectSocket);
    pthread_detach(recvThread);

    std::string message;
    while(true){
        std::getline(std::cin, message);
        message += "\n";
        iResult = send(ConnectSocket, message.c_str(), message.size(), 0);
        if(iResult == SOCKET_ERROR){
            printf("send failed: %d", WSAGetLastError());
            break;
        }
    }
    closesocket(ConnectSocket);
    WSACleanup();

    return 0;
}
