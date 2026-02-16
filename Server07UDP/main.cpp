#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <string>

#pragma comment(lib, "ws2_32.lib")
using namespace std;

struct PLAYER_DATA {
    int x, y, angle, type;
};

const unsigned short SERVER_PORT = 8888;

struct ClientInfo {
    sockaddr_in addr;
    chrono::steady_clock::time_point lastSeen;

    bool operator<(const ClientInfo& other) const {
        if (addr.sin_addr.s_addr != other.addr.sin_addr.s_addr)
            return addr.sin_addr.s_addr < other.addr.sin_addr.s_addr;
        return addr.sin_port < other.addr.sin_port;
    }
};

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in serverAddr = { AF_INET, htons(SERVER_PORT), INADDR_ANY };
    bind(sock, (sockaddr*)&serverAddr, sizeof(serverAddr));

    unsigned long arg = 0x01;
    ioctlsocket(sock, FIONBIO, &arg);

    map<ClientInfo, PLAYER_DATA> clients;
    cout << "--- UDP Shooting Server Started ---" << endl;
    cout << "Listening on port: " << SERVER_PORT << endl;

    while (true) {
        PLAYER_DATA recvData;
        sockaddr_in clientAddr;
        int addrLen = sizeof(clientAddr);

        int ret = recvfrom(sock, (char*)&recvData, sizeof(PLAYER_DATA), 0, (sockaddr*)&clientAddr, &addrLen);

        if (ret > 0) {
            ClientInfo info = { clientAddr, chrono::steady_clock::now() };

            // 新規クライアントかチェック
            if (clients.find(info) == clients.end()) {
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
                cout << "[Join] New Client: " << ipStr << ":" << ntohs(clientAddr.sin_port) << endl;
                cout << "Total clients: " << clients.size() + 1 << endl;
            }

            clients[info].x = ntohl(recvData.x);
            clients[info].y = ntohl(recvData.y);
            //clients[info].lastSeen = chrono::steady_clock::now(); // 生存時間を更新
        }

        // タイムアウト処理（5秒以上通信がない場合）
        auto now = chrono::steady_clock::now();
        for (auto it = clients.begin(); it != clients.end(); ) {
            if (chrono::duration_cast<chrono::seconds>(now - it->first.lastSeen).count() > 5) {
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &it->first.addr.sin_addr, ipStr, sizeof(ipStr));
                cout << "[Leave] Client Timeout: " << ipStr << endl;

                it = clients.erase(it);
                cout << "Total clients: " << clients.size() << endl;
            }
            else { ++it; }
        }

        // 送信処理
        if (!clients.empty()) {
            vector<PLAYER_DATA> sendList;
            for (auto const& c : clients) {
                PLAYER_DATA netData = { (int)htonl(c.second.x), (int)htonl(c.second.y), 0, 0 };
                sendList.push_back(netData);
            }
            for (auto const& target : clients) {
                sendto(sock, (char*)sendList.data(), sendList.size() * sizeof(PLAYER_DATA), 0, (sockaddr*)&target.first.addr, sizeof(sockaddr_in));
            }
        }
        this_thread::sleep_for(chrono::milliseconds(10));
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}