#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <map>
#include <chrono>
#include <thread>

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
    unsigned long arg = 0x01;

    // TCPサーバー (接続確認用)
    SOCKET tcpListen = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in tcpAddr = { AF_INET, htons(8888), INADDR_ANY };
    bind(tcpListen, (sockaddr*)&tcpAddr, sizeof(tcpAddr));
    listen(tcpListen, 5);
    ioctlsocket(tcpListen, FIONBIO, &arg);

    // UDPサーバー (ゲームデータ用)
    SOCKET udpSock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in udpAddr = { AF_INET, htons(8889), INADDR_ANY };
    bind(udpSock, (sockaddr*)&udpAddr, sizeof(udpAddr));
    ioctlsocket(udpSock, FIONBIO, &arg);

    map<ClientInfo, PLAYER_INTERNAL> clients;
    vector<Enemy> enemies;
    vector<EBullet> eBullets;
    int enemyIdCounter = 1;
    int teamScore = 0; // ★共有スコア

    cout << "=== TEAM SERVER READY (Score Sync Mode) ===" << endl;

    while (true) {
        // TCP接続チェック
        sockaddr_in tempAddr; int tempLen = sizeof(tempAddr);
        SOCKET newTcp = accept(tcpListen, (sockaddr*)&tempAddr, &tempLen);
        if (newTcp != INVALID_SOCKET) {
            char ip[INET_ADDRSTRLEN]; inet_ntop(AF_INET, &tempAddr.sin_addr, ip, sizeof(ip));
            cout << "New Player Connected: " << ip << endl;
        }

        // UDP受信
        PLAYER_DATA recvBuf;
        sockaddr_in cAddr;
        int cLen = sizeof(cAddr);
        while (recvfrom(udpSock, (char*)&recvBuf, sizeof(PLAYER_DATA), 0, (sockaddr*)&cAddr, &cLen) > 0) {
            ClientInfo info = { cAddr };
            int type = ntohl(recvBuf.type);

            if (type == 9) { // 敵撃破通知
                int targetId = ntohl(recvBuf.x);
                for (auto it = enemies.begin(); it != enemies.end(); ) {
                    if (it->id == targetId) { it = enemies.erase(it); teamScore += 100; }
                    else ++it;
                }
            }
            else if (type == 8) { // 被弾通知
                teamScore -= 500;
                if (teamScore < 0) teamScore = 0;
            }
            else { // 座標更新
                clients[info].x = ntohl(recvBuf.x);
                clients[info].y = ntohl(recvBuf.y);
                clients[info].lastSeen = chrono::steady_clock::now();
            }
        }

        // タイムアウト
        auto now = chrono::steady_clock::now();
        for (auto it = clients.begin(); it != clients.end(); ) {
            if (chrono::duration_cast<chrono::seconds>(now - it->second.lastSeen).count() > 3) it = clients.erase(it);
            else ++it;
        }

        if (!clients.empty()) {
            // 敵の処理
            if (enemies.size() < 8) enemies.push_back({ enemyIdCounter++, (float)(150 + rand() % 900), -100.0f, 2.0f, (float)(rand() % 100) });
            for (auto it = enemies.begin(); it != enemies.end(); ) {
                it->y += it->speedY; it->timer += 0.02f;
                if (rand() % 100 == 0) eBullets.push_back({ it->baseX + sinf(it->timer) * 80.0f, it->y + 40 });
                if (it->y > 800) it = enemies.erase(it); else ++it;
            }
            for (auto it = eBullets.begin(); it != eBullets.end(); ) {
                it->y += 7.0f; if (it->y > 800) it = eBullets.erase(it); else ++it;
            }

            // 送信リスト作成
            vector<PLAYER_DATA> sendList;
            // ★先頭パケットにスコアを乗せる (type: 10)
            sendList.push_back({ (int)htonl(teamScore), 0, 0, (int)htonl(10) });

            for (auto const& c : clients) sendList.push_back({ (int)htonl(c.second.x), (int)htonl(c.second.y), 0, (int)htonl(0) });
            for (auto const& e : enemies) sendList.push_back({ (int)htonl((int)(e.baseX + sinf(e.timer) * 80.0f)), (int)htonl((int)e.y), (int)htonl(e.id), (int)htonl(1) });
            for (auto const& eb : eBullets) sendList.push_back({ (int)htonl((int)eb.x), (int)htonl((int)eb.y), 0, (int)htonl(2) });

            for (auto const& target : clients) {
                sendto(udpSock, (char*)sendList.data(), (int)(sendList.size() * sizeof(PLAYER_DATA)), 0, (sockaddr*)&target.first.addr, sizeof(sockaddr_in));
            }
        }
        else { enemies.clear(); eBullets.clear(); teamScore = 0; }
        this_thread::sleep_for(chrono::milliseconds(16));
    }
    return 0;
}