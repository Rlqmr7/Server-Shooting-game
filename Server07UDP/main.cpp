#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <algorithm>
#include <string>

#pragma comment(lib, "ws2_32.lib")
using namespace std;

struct PLAYER_DATA { int x, y, angle, type; };
struct PLAYER_INTERNAL { int x, y, id; chrono::steady_clock::time_point lastSeen; string ipStr; };
struct ClientInfo {
    sockaddr_in addr;
    bool operator<(const ClientInfo& other) const {
        if (addr.sin_addr.s_addr != other.addr.sin_addr.s_addr) return addr.sin_addr.s_addr < other.addr.sin_addr.s_addr;
        return addr.sin_port < other.addr.sin_port;
    }
};

struct Enemy { int id; float baseX, y, speedY, timer; };
struct EBullet { float x, y; };
struct PBullet { float x, y; int ownerId; };

int main() {
    WSADATA wsaData; WSAStartup(MAKEWORD(2, 2), &wsaData);
    unsigned long arg = 0x01;

    SOCKET tcpListen = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in tcpAddr = { AF_INET, htons(8888), INADDR_ANY };
    bind(tcpListen, (sockaddr*)&tcpAddr, sizeof(tcpAddr));
    listen(tcpListen, 5);
    ioctlsocket(tcpListen, FIONBIO, &arg);

    SOCKET udpSock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in udpAddr = { AF_INET, htons(8889), INADDR_ANY };
    bind(udpSock, (sockaddr*)&udpAddr, sizeof(udpAddr));
    ioctlsocket(udpSock, FIONBIO, &arg);

    map<ClientInfo, PLAYER_INTERNAL> clients;
    vector<Enemy> enemies;
    vector<EBullet> eBullets;
    vector<PBullet> pBullets;
    int enemyIdCounter = 1, teamScore = 0, nextPlayerId = 0;
    auto startTime = chrono::steady_clock::now();

    cout << "========================================" << endl;
    cout << "   1-MINUTE SHOOTER GAME STARTED" << endl;
    cout << "========================================" << endl;
    cout << "Waiting for players..." << endl;

    while (true) {
        // --- 新規TCP接続（参加の検知） ---
        sockaddr_in tempAddr; int tempLen = sizeof(tempAddr);
        SOCKET newTcp = accept(tcpListen, (sockaddr*)&tempAddr, &tempLen);
        if (newTcp != INVALID_SOCKET) {
            if (clients.empty()) {
                startTime = chrono::steady_clock::now();
                teamScore = 0;
                cout << ">>> Game Started (First player joined)" << endl;
            }
            closesocket(newTcp); // 今回は接続確認のみに使用
        }

        // --- UDPデータ受信 ---
        PLAYER_DATA recvBuf; sockaddr_in cAddr; int cLen = sizeof(cAddr);
        while (recvfrom(udpSock, (char*)&recvBuf, sizeof(PLAYER_DATA), 0, (sockaddr*)&cAddr, &cLen) > 0) {
            ClientInfo info = { cAddr };
            int type = ntohl(recvBuf.type);

            // 新規プレイヤーの登録とログ出力
            if (clients.find(info) == clients.end()) {
                char ipBuf[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &cAddr.sin_addr, ipBuf, sizeof(ipBuf));

                clients[info].id = nextPlayerId++;
                clients[info].ipStr = string(ipBuf);
                clients[info].lastSeen = chrono::steady_clock::now();

                cout << "[JOIN] Player ID:" << clients[info].id << " from " << clients[info].ipStr << " (Total: " << clients.size() << ")" << endl;
            }

            if (type == 7) pBullets.push_back({ (float)ntohl(recvBuf.x), (float)ntohl(recvBuf.y), clients[info].id });
            else if (type == 8) teamScore = max(0, teamScore - 500);
            else {
                clients[info].x = ntohl(recvBuf.x);
                clients[info].y = ntohl(recvBuf.y);
                clients[info].lastSeen = chrono::steady_clock::now();
            }
        }

        auto now = chrono::steady_clock::now();
        float elapsed = (float)chrono::duration_cast<chrono::milliseconds>(now - startTime).count() / 1000.0f;

        // --- タイムアウト判定（離脱の検知） ---
        for (auto it = clients.begin(); it != clients.end(); ) {
            if (chrono::duration_cast<chrono::seconds>(now - it->second.lastSeen).count() > 3) {
                cout << "[LEAVE] Player ID:" << it->second.id << " timed out. (Remaining: " << clients.size() - 1 << ")" << endl;
                it = clients.erase(it);
                if (clients.empty()) {
                    cout << ">>> All players left. Resetting game..." << endl;
                    enemies.clear(); eBullets.clear(); pBullets.clear();
                }
            }
            else ++it;
        }

        // --- ゲームメインロジック ---
        if (!clients.empty() && elapsed < 61.0f) {
            int maxEnemies = 5 + (int)(elapsed * 0.6f);
            int shootChance = max(8, 70 - (int)(elapsed * 1.1f));

            if ((int)enemies.size() < maxEnemies) {
                enemies.push_back({ enemyIdCounter++, (float)(100 + rand() % 1700), -100.0f, 2.5f + (elapsed * 0.1f), (float)(rand() % 100) });
            }

            for (auto it = enemies.begin(); it != enemies.end(); ) {
                it->y += it->speedY; it->timer += 0.03f;
                if (rand() % shootChance == 0) eBullets.push_back({ it->baseX + sinf(it->timer) * 100.0f, it->y + 40 });
                if (it->y > 1150) it = enemies.erase(it); else ++it;
            }
            for (auto it = eBullets.begin(); it != eBullets.end(); ) { it->y += 7.0f; if (it->y > 1100) it = eBullets.erase(it); else ++it; }
            for (auto it = pBullets.begin(); it != pBullets.end(); ) {
                it->y -= 20.0f; bool hit = false;
                for (auto& e : enemies) {
                    float ex = e.baseX + sinf(e.timer) * 100.0f;
                    if (hypot(it->x - ex, it->y - e.y) < 50) { e.y = 2000; teamScore += 100; hit = true; break; }
                }
                if (hit || it->y < -100) it = pBullets.erase(it); else ++it;
            }

            vector<PLAYER_DATA> sendList;
            sendList.push_back({ (int)htonl(teamScore), (int)htonl((int)elapsed), 0, (int)htonl(10) });
            for (auto const& c : clients) sendList.push_back({ (int)htonl(c.second.x), (int)htonl(c.second.y), (int)htonl(c.second.id), (int)htonl(0) });
            for (auto const& e : enemies) sendList.push_back({ (int)htonl((int)(e.baseX + sinf(e.timer) * 100.0f)), (int)htonl((int)e.y), (int)htonl(e.id), (int)htonl(1) });
            for (auto const& eb : eBullets) sendList.push_back({ (int)htonl((int)eb.x), (int)htonl((int)eb.y), 0, (int)htonl(2) });
            for (auto const& pb : pBullets) sendList.push_back({ (int)htonl((int)pb.x), (int)htonl((int)pb.y), (int)htonl(pb.ownerId), (int)htonl(3) });

            for (auto const& target : clients) sendto(udpSock, (char*)sendList.data(), (int)(sendList.size() * sizeof(PLAYER_DATA)), 0, (sockaddr*)&target.first.addr, sizeof(sockaddr_in));
        }

        this_thread::sleep_for(chrono::milliseconds(16));
    }
    return 0;
}