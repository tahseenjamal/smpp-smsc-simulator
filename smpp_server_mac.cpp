// main.cpp
// High TPS SMPP Simulator with DLR (macOS kqueue)

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <queue>
#include <unordered_map>
#include <vector>

static const int MAX_EVENTS = 4096;
static const int BUFFER_SIZE = 8192;
static const uint64_t DLR_DELAY_USEC = 2000000ULL;  // 2 seconds

//////////////////////////////////////////////////////////////
// Time
//////////////////////////////////////////////////////////////

uint64_t nowUsec() {
    timeval tv;
    gettimeofday(&tv, nullptr);
    return (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
}

//////////////////////////////////////////////////////////////
// Socket Helpers
//////////////////////////////////////////////////////////////

inline void setNonBlocking(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

inline void tuneSocket(int fd) {
    int buf = 4 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));

    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
}

//////////////////////////////////////////////////////////////
// Delivery Queue
//////////////////////////////////////////////////////////////

struct DeliveryItem {
    uint64_t deliverAt;
    uint32_t seq;
};

struct CompareDelivery {
    bool operator()(const DeliveryItem& a, const DeliveryItem& b) {
        return a.deliverAt > b.deliverAt;
    }
};

//////////////////////////////////////////////////////////////
// Session
//////////////////////////////////////////////////////////////

struct Session {
    int fd;
    uint8_t buffer[BUFFER_SIZE];
    int bufferLen = 0;

    std::priority_queue<DeliveryItem, std::vector<DeliveryItem>,
                        CompareDelivery>
        deliveryQueue;
};

//////////////////////////////////////////////////////////////
// Main
//////////////////////////////////////////////////////////////

int main() {
    signal(SIGPIPE, SIG_IGN);

    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        perror("socket failed");
        return 1;
    }

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(2775);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(server, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        return 1;
    }

    if (::listen(server, 10000) < 0) {
        perror("listen failed");
        return 1;
    }

    setNonBlocking(server);

    int kq = kqueue();
    if (kq < 0) {
        perror("kqueue failed");
        return 1;
    }

    struct kevent change;
    EV_SET(&change, server, EVFILT_READ, EV_ADD, 0, 0, NULL);
    kevent(kq, &change, 1, NULL, 0, NULL);

    std::unordered_map<int, Session> sessions;
    struct kevent events[MAX_EVENTS];

    std::cout << "High TPS SMPP Server with DLR on 2775\n";

    while (true) {
        timespec ts{};
        ts.tv_sec = 0;
        ts.tv_nsec = 5000000;  // 5ms tick for DLR

        int nev = kevent(kq, NULL, 0, events, MAX_EVENTS, &ts);

        ////////////////////////////////////////////////////////////
        // Handle network events
        ////////////////////////////////////////////////////////////

        for (int i = 0; i < nev; ++i) {
            int fd = (int)events[i].ident;

            ////////////////////////////////////////////////////////////
            // Accept
            ////////////////////////////////////////////////////////////
            if (fd == server) {
                while (true) {
                    int client = accept(server, NULL, NULL);
                    if (client < 0) break;

                    setNonBlocking(client);
                    tuneSocket(client);

                    EV_SET(&change, client, EVFILT_READ, EV_ADD, 0, 0, NULL);
                    kevent(kq, &change, 1, NULL, 0, NULL);

                    sessions[client] = {client};
                }
                continue;
            }

            ////////////////////////////////////////////////////////////
            // Read
            ////////////////////////////////////////////////////////////
            Session& sess = sessions[fd];

            while (true) {
                int n = recv(fd, sess.buffer + sess.bufferLen,
                             BUFFER_SIZE - sess.bufferLen, 0);

                if (n <= 0) break;

                sess.bufferLen += n;

                while (sess.bufferLen >= 16) {
                    uint32_t len = ntohl(*(uint32_t*)(sess.buffer));
                    if (sess.bufferLen < (int)len) break;

                    uint32_t cmdid = ntohl(*(uint32_t*)(sess.buffer + 4));
                    uint32_t seq = ntohl(*(uint32_t*)(sess.buffer + 12));

                    ////////////////////////////////////////////////////
                    // Bind
                    ////////////////////////////////////////////////////
                    if (cmdid == 0x00000009) {
                        uint8_t resp[21];
                        *(uint32_t*)(resp + 0) = htonl(21);
                        *(uint32_t*)(resp + 4) = htonl(0x80000009);
                        *(uint32_t*)(resp + 8) = htonl(0);
                        *(uint32_t*)(resp + 12) = htonl(seq);
                        memcpy(resp + 16, "SMSC", 5);

                        send(fd, resp, 21, 0);
                    }

                    ////////////////////////////////////////////////////
                    // SubmitSM
                    ////////////////////////////////////////////////////
                    else if (cmdid == 0x00000004) {
                        char msgid[16];
                        int idlen = snprintf(msgid, sizeof(msgid), "%u", seq);

                        uint8_t resp[64];
                        *(uint32_t*)(resp + 0) = htonl(16 + idlen + 1);
                        *(uint32_t*)(resp + 4) = htonl(0x80000004);
                        *(uint32_t*)(resp + 8) = htonl(0);
                        *(uint32_t*)(resp + 12) = htonl(seq);
                        memcpy(resp + 16, msgid, idlen + 1);

                        send(fd, resp, 16 + idlen + 1, 0);

                        // Schedule delivery
                        sess.deliveryQueue.push(
                            {nowUsec() + DLR_DELAY_USEC, seq});
                    }

                    ////////////////////////////////////////////////////
                    // EnquireLink
                    ////////////////////////////////////////////////////
                    else if (cmdid == 0x00000015) {
                        uint8_t resp[16];
                        *(uint32_t*)(resp + 0) = htonl(16);
                        *(uint32_t*)(resp + 4) = htonl(0x80000015);
                        *(uint32_t*)(resp + 8) = htonl(0);
                        *(uint32_t*)(resp + 12) = htonl(seq);

                        send(fd, resp, 16, 0);
                    }

                    memmove(sess.buffer, sess.buffer + len,
                            sess.bufferLen - len);
                    sess.bufferLen -= len;
                }
            }

            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                close(fd);
                sessions.erase(fd);
            }
        }

        ////////////////////////////////////////////////////////////
        // Delivery processing (timed)
        ////////////////////////////////////////////////////////////

        uint64_t now = nowUsec();

        for (auto& kv : sessions) {
            Session& sess = kv.second;

            while (!sess.deliveryQueue.empty() &&
                   sess.deliveryQueue.top().deliverAt <= now) {
                auto item = sess.deliveryQueue.top();
                sess.deliveryQueue.pop();

                uint8_t pdu[512];
                int idx = 16;

                // service_type (cstring)
                pdu[idx++] = 0x00;

                // source_addr_ton
                pdu[idx++] = 0x01;
                // source_addr_npi
                pdu[idx++] = 0x01;
                // source_addr
                memcpy(pdu + idx, "SMSC", 5);
                idx += 5;
                pdu[idx++] = 0x00;

                // dest_addr_ton
                pdu[idx++] = 0x01;
                // dest_addr_npi
                pdu[idx++] = 0x01;
                // destination_addr
                memcpy(pdu + idx, "12345678", 8);
                idx += 8;
                pdu[idx++] = 0x00;

                // esm_class (0x04 = delivery receipt)
                pdu[idx++] = 0x04;

                // protocol_id
                pdu[idx++] = 0x00;

                // priority_flag
                pdu[idx++] = 0x00;

                // schedule_delivery_time (cstring)
                pdu[idx++] = 0x00;

                // validity_period (cstring)
                pdu[idx++] = 0x00;

                // registered_delivery
                pdu[idx++] = 0x00;

                // replace_if_present
                pdu[idx++] = 0x00;

                // data_coding
                pdu[idx++] = 0x00;

                // sm_default_msg_id
                pdu[idx++] = 0x00;

                // receipt text
                char receipt[256];
                int rlen = snprintf(
                    receipt, sizeof(receipt),
                    "id:%u sub:001 dlvrd:001 submit date:2402241200 "
                    "done date:2402241205 stat:DELIVRD err:000 text:OK",
                    item.seq);

                // sm_length
                pdu[idx++] = rlen;

                // short_message
                memcpy(pdu + idx, receipt, rlen);
                idx += rlen;

                // header
                *(uint32_t*)(pdu + 0) = htonl(idx);
                *(uint32_t*)(pdu + 4) = htonl(0x00000005);
                *(uint32_t*)(pdu + 8) = htonl(0);
                *(uint32_t*)(pdu + 12) = htonl(item.seq);

                send(sess.fd, pdu, idx, 0);
            }
        }
    }
}
