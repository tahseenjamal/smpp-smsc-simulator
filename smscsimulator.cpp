//
// smppserver.cpp
// Stable SMPP Simulator (Heavy Load Safe)
//

#include <iostream>
#include <string>
#include <map>
#include <list>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>

using namespace std;

//////////////////////////////////////////////////////////////
// Utilities
//////////////////////////////////////////////////////////////

uint64_t nowUsec()
{
    timeval tv;
    gettimeofday(&tv, nullptr);
    return (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
}

bool sendAll(int sock, const uint8_t* data, int len)
{
    int total = 0;
    while (total < len)
    {
        int n = send(sock, data + total, len - total, 0);
        if (n <= 0)
            return false;
        total += n;
    }
    return true;
}

bool recvAll(int sock, uint8_t* data, int len)
{
    int total = 0;
    while (total < len)
    {
        int n = recv(sock, data + total, len - total, 0);
        if (n <= 0)
            return false;
        total += n;
    }
    return true;
}

//////////////////////////////////////////////////////////////
// Delivery Queue
//////////////////////////////////////////////////////////////

class DeliverQueue
{
public:
    struct Item {
        string msgid;
        uint64_t deliverAt;
    };

    list<Item> items;

    void add(const string& id, uint64_t when)
    {
        items.push_back({id, when});
    }

    bool pop(Item& out)
    {
        uint64_t n = nowUsec();
        for (auto it = items.begin(); it != items.end(); ++it)
        {
            if (it->deliverAt <= n)
            {
                out = *it;
                items.erase(it);
                return true;
            }
        }
        return false;
    }
};

//////////////////////////////////////////////////////////////
// SMPP Session
//////////////////////////////////////////////////////////////

class SMPPSession
{
    int sock;
    bool bound = false;
    DeliverQueue queue;

public:
    SMPPSession(int s) : sock(s) {}

    bool readPDU(uint32_t& cmdid, uint32_t& seq)
    {
        uint8_t header[16];

        if (!recvAll(sock, header, 16))
            return false;

        uint32_t len = ntohl(*(uint32_t*)(header));
        cmdid = ntohl(*(uint32_t*)(header+4));
        seq   = ntohl(*(uint32_t*)(header+12));

        if (len > 16)
        {
            vector<uint8_t> body(len - 16);
            if (!recvAll(sock, body.data(), len - 16))
                return false;
        }

        return true;
    }

    bool run()
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);

        timeval tv{0,0};

        if (select(sock+1, &rfds, nullptr, nullptr, &tv) <= 0)
            return false;

        uint32_t cmdid, seq;

        if (!readPDU(cmdid, seq))
            return true;

        if (cmdid == 0x00000009) // BindTransceiver
        {
            bound = true;

            uint8_t resp[32];
            *(uint32_t*)(resp+0)  = htonl(16 + 5);
            *(uint32_t*)(resp+4)  = htonl(0x80000009);
            *(uint32_t*)(resp+8)  = htonl(0);
            *(uint32_t*)(resp+12) = htonl(seq);
            strcpy((char*)(resp+16), "SMSC");

            sendAll(sock, resp, 21);
            cout << "Bind OK\n";
        }
        else if (cmdid == 0x00000004) // SubmitSM
        {
            string msgid = "abc123";

            uint8_t resp[64];
            *(uint32_t*)(resp+0)  = htonl(16 + msgid.length() + 1);
            *(uint32_t*)(resp+4)  = htonl(0x80000004);
            *(uint32_t*)(resp+8)  = htonl(0);
            *(uint32_t*)(resp+12) = htonl(seq);
            strcpy((char*)(resp+16), msgid.c_str());

            sendAll(sock, resp, 16 + msgid.length() + 1);

            queue.add(msgid, nowUsec() + 3000000ULL);

            cout << "Submit OK\n";
        }

        return false;
    }

    bool timed()
    {
        if (!bound)
            return false;

        DeliverQueue::Item item;

        while (queue.pop(item))
        {
            cout << "Sending DeliverSM\n";

            uint8_t pdu[512];
            int idx = 16;

            pdu[idx++] = 0x00;
            pdu[idx++] = 0x01;
            pdu[idx++] = 0x01;
            strcpy((char*)(pdu+idx), "SMSC");
            idx += 6;

            pdu[idx++] = 0x01;
            pdu[idx++] = 0x01;
            strcpy((char*)(pdu+idx), "12345678");
            idx += 9;

            pdu[idx++] = 0x04;
            pdu[idx++] = 0x00;
            pdu[idx++] = 0x00;
            pdu[idx++] = 0x00;
            pdu[idx++] = 0x00;
            pdu[idx++] = 0x00;
            pdu[idx++] = 0x00;
            pdu[idx++] = 0x00;
            pdu[idx++] = 0x00;

            string receipt =
                "id:" + item.msgid +
                " sub:001 dlvrd:001 submit date:2402241200 done date:2402241205 "
                "stat:DELIVRD err:000 text:HelloWorldTest12";

            pdu[idx++] = receipt.length();
            memcpy(pdu+idx, receipt.c_str(), receipt.length());
            idx += receipt.length();

            *(uint32_t*)(pdu+0)  = htonl(idx);
            *(uint32_t*)(pdu+4)  = htonl(0x00000005);
            *(uint32_t*)(pdu+8)  = htonl(0);
            *(uint32_t*)(pdu+12) = htonl(1);

            if (!sendAll(sock, pdu, idx))
                return true;
        }

        return false;
    }
};

//////////////////////////////////////////////////////////////
// Server
//////////////////////////////////////////////////////////////

int main()
{
    signal(SIGPIPE, SIG_IGN);

    int server = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(2775);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(server, (sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        exit(1);
    }

    if (listen(server, 100) < 0)
    {
        perror("listen");
        exit(1);
    }

    cout << "SMPP Server listening on 2775\n";

    fd_set master, readfds;
    FD_ZERO(&master);
    FD_SET(server, &master);
    int maxfd = server;

    map<int, SMPPSession*> sessions;

    while (true)
    {
        readfds = master;
        timeval tv{1,0};

        select(maxfd+1, &readfds, nullptr, nullptr, &tv);

        for (int i=0; i<=maxfd; i++)
        {
            if (!FD_ISSET(i, &readfds))
                continue;

            if (i == server)
            {
                int client = accept(server, nullptr, nullptr);
                if (client > 0)
                {
                    FD_SET(client, &master);
                    if (client > maxfd) maxfd = client;
                    sessions[client] = new SMPPSession(client);
                    cout << "Client connected\n";
                }
            }
            else
            {
                if (sessions[i]->run())
                {
                    close(i);
                    FD_CLR(i, &master);
                    delete sessions[i];
                    sessions.erase(i);
                }
            }
        }

        for (auto it = sessions.begin(); it != sessions.end(); )
        {
            if (it->second->timed())
            {
                close(it->first);
                FD_CLR(it->first, &master);
                delete it->second;
                it = sessions.erase(it);
            }
            else
                ++it;
        }
    }
}
