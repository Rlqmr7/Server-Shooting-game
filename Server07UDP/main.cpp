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

struct PLAYER_DATA { int x, y, angle, type; };
struct PLAYER_INTERNAL { int x, y; chrono::steady_clock::time_point lastSeen; };
struct ClientInfo {
    sockaddr_in addr;
    bool operator<(const ClientInfo& other) const {
        if (addr.sin_addr.s_addr != other.addr.sin_addr.s_addr) return addr.sin_addr.s_addr < other.addr.sin_addr.s_addr;
        return addr.sin_port < other.addr.sin_port;
    }
};

struct Enemy { int id; float baseX, y, speedY, timer; };
struct EBullet { float x, y; };

int main() {
    WSADATA wsaData; WSAStartup(MAKEWORD(2, 2), &wsaData);
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in serverAddr = { AF_INET, htons(8888), INADDR_ANY };
    bind(sock, (sockaddr*)&serverAddr, sizeof(serverAddr));
    unsigned long arg = 0x01; ioctlsocket(sock, FIONBIO, &arg);

    map<ClientInfo, PLAYER_INTERNAL> clients;
    vector<Enemy> enemies;
    vector<EBullet> eBullets;
    int enemyIdCounter = 1;

    while (true) {
        PLAYER_DATA recvBuf;
        sockaddr_in cAddr;
        int cLen = sizeof(cAddr);
        while (recvfrom(sock, (char*)&recvBuf, sizeof(PLAYER_DATA), 0, (sockaddr*)&cAddr, &cLen) > 0) {
            int type = ntohl(recvBuf.type);
            if (type == 9) { // 敵死亡
                int targetId = ntohl(recvBuf.x);
                for (auto it = enemies.begin(); it != enemies.end(); ) {
                    if (it->id == targetId) it = enemies.erase(it); else ++it;
                }
            }
            else {
                clients[{cAddr}].x = ntohl(recvBuf.x);
                clients[{cAddr}].y = ntohl(recvBuf.y);
                clients[{cAddr}].lastSeen = chrono::steady_clock::now();
            }
        }

        // タイムアウト処理
        auto now = chrono::steady_clock::now();
        for (auto it = clients.begin(); it != clients.end(); ) {
            if (chrono::duration_cast<chrono::seconds>(now - it->second.lastSeen).count() > 3) it = clients.erase(it);
            else ++it;
        }

        if (!clients.empty()) {
            if (enemies.size() < 8) enemies.push_back({ enemyIdCounter++, (float)(150 + rand() % 900), -100.0f, 2.0f, (float)(rand() % 100) });

            for (auto it = enemies.begin(); it != enemies.end(); ) {
                it->y += it->speedY; it->timer += 0.02f;
                if (rand() % 100 == 0) eBullets.push_back({ it->baseX + sinf(it->timer) * 80.0f, it->y + 40 });
                if (it->y > 800) it = enemies.erase(it); else ++it;
            }
            for (auto it = eBullets.begin(); it != eBullets.end(); ) {
                it->y += 7.0f; // 弾は速めに
                if (it->y > 800) it = eBullets.erase(it); else ++it;
            }

            // 送信データ作成
            vector<PLAYER_DATA> sendList;
            for (auto const& c : clients) sendList.push_back({ (int)htonl(c.second.x), (int)htonl(c.second.y), 0, (int)htonl(0) });
            for (auto const& e : enemies) sendList.push_back({ (int)htonl((int)(e.baseX + sinf(e.timer) * 80.0f)), (int)htonl((int)e.y), (int)htonl(e.id), (int)htonl(1) });
            for (auto const& eb : eBullets) sendList.push_back({ (int)htonl((int)eb.x), (int)htonl((int)eb.y), 0, (int)htonl(2) });

            for (auto const& target : clients) {
                sendto(sock, (char*)sendList.data(), (int)(sendList.size() * sizeof(PLAYER_DATA)), 0, (sockaddr*)&target.first.addr, sizeof(sockaddr_in));
            }
        }
        this_thread::sleep_for(chrono::milliseconds(16)); // 60FPSに合わせる
    }
    return 0;
}