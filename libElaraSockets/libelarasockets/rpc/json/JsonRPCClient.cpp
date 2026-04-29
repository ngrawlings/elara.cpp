#include "JsonRPCClient.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <libelaracore/exception/Exception.h>
#include <libelarasockets/Address.h>

#include "JsonRPCCodec.h"

namespace elara {
namespace sockets {
namespace rpc {
namespace json {

    JsonRPCClient::JsonRPCClient() : fd(0), next_request_id(1), call_lock("json-rpc-client") {
    }

    JsonRPCClient::~JsonRPCClient() {
        close();
    }

    bool JsonRPCClient::connect(const String &remote_address, unsigned short port) {
        close();

        String address_text(remote_address);
        Address address(Address::ADDR, address_text);
        int family = address.getType() == Address::IPV6 ? AF_INET6 : AF_INET;
        fd = socket(family, SOCK_STREAM, 0);
        if (fd < 0)
            return false;

        if (family == AF_INET) {
            sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            memcpy(&addr.sin_addr.s_addr, address.getAddr(), 4);
            if (::connect(fd, (const sockaddr*)&addr, sizeof(addr)) != 0) {
                close();
                return false;
            }
        } else {
            sockaddr_in6 addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin6_family = AF_INET6;
            addr.sin6_port = htons(port);
            memcpy(&addr.sin6_addr, address.getAddr(), 16);
            if (::connect(fd, (const sockaddr*)&addr, sizeof(addr)) != 0) {
                close();
                return false;
            }
        }

        return true;
    }

    void JsonRPCClient::close() {
        if (fd > 0) {
            ::close(fd);
            fd = 0;
        }
    }

    bool JsonRPCClient::isConnected() const {
        return fd > 0;
    }

    bool JsonRPCClient::sendAll(const char *buffer, size_t length) {
        size_t written = 0;
        while (written < length) {
            ssize_t sent = ::send(fd, &buffer[written], length - written, 0);
            if (sent <= 0)
                return false;
            written += (size_t)sent;
        }
        return true;
    }

    bool JsonRPCClient::recvAll(char *buffer, size_t length) {
        size_t received = 0;
        while (received < length) {
            ssize_t got = ::recv(fd, &buffer[received], length - received, 0);
            if (got <= 0)
                return false;
            received += (size_t)got;
        }
        return true;
    }

    bool JsonRPCClient::call(const String &method, const String &params_json, String &result_json, String &error_code, String &error_message) {
        Mutex::Lock lock(call_lock);

        if (!isConnected())
            return false;

        String id = String((unsigned long long)next_request_id++);

        String request = JsonRPCCodec::buildRequest(id, method, params_json);
        ByteArray frame = JsonRPCCodec::framePayload(request);
        if (!sendAll((const char*)frame, frame.length()))
            return false;

        char prefix[4];
        if (!recvAll(prefix, 4))
            return false;

        uint32_t length = ((uint32_t)(prefix[0] & 0xFF) << 24)
            | ((uint32_t)(prefix[1] & 0xFF) << 16)
            | ((uint32_t)(prefix[2] & 0xFF) << 8)
            | (uint32_t)(prefix[3] & 0xFF);

        ByteArray payload;
        payload.append((int)length);
        if (!recvAll((char*)payload, length))
            return false;

        String response((char*)payload, payload.length());
        String response_id;
        bool ok = false;
        String parse_error;
        String response_error_code;
        String response_error_message;
        String response_result_json;
        if (!JsonRPCCodec::parseResponse(response, response_id, ok, response_result_json, response_error_code, response_error_message, parse_error))
            return false;

        if (!(response_id == id))
            return false;

        if (ok) {
            result_json = response_result_json;
            return true;
        }

        error_code = response_error_code;
        error_message = response_error_message;
        return false;
    }

}
}
}
}
