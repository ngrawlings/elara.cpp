#include "ElaraUiRpcPeer.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <libelarasockets/Address.h>
#include <libelarasockets/rpc/json/JsonRPCClient.h>
#include <libelarasockets/rpc/json/JsonRPCCodec.h>
#include <libelarasockets/rpc/brpc/BRpcRpcCodec.h>

namespace elara {
namespace ui {
namespace rpc {

class ElaraUiRpcPendingCall {
public:
    String id;
    bool completed;
    bool ok;
    String result_json;
    String error_code;
    String error_message;
    Mutex mutex;

    ElaraUiRpcPendingCall(const String& call_id)
        : id(call_id),
          completed(false),
          ok(false),
          mutex("elara-ui-rpc-pending-call") {
    }
};

ElaraUiRpcPeer::ElaraUiRpcPeer()
    : fd(0),
      running(false),
      receiver_started(false),
      use_brpc(true),
      next_request_id(1),
      send_lock("elara-ui-rpc-send"),
      pending_lock("elara-ui-rpc-pending") {
}

void ElaraUiRpcPeer::setUseBrpc(bool b) {
    use_brpc = b;
}

ElaraUiRpcPeer::~ElaraUiRpcPeer() {
    close();
}

bool ElaraUiRpcPeer::connect(const String& remote_address, unsigned short port) {
    close();

    String address_text(remote_address);
    sockets::Address address(sockets::Address::ADDR, address_text);
    int family = address.getType() == sockets::Address::IPV6 ? AF_INET6 : AF_INET;
    fd = socket(family, SOCK_STREAM, 0);

    if(fd < 0) {
        return false;
    }

    if(family == AF_INET) {
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        memcpy(&addr.sin_addr.s_addr, address.getAddr(), 4);

        if(::connect(fd, (const sockaddr*)&addr, sizeof(addr)) != 0) {
            close();
            return false;
        }
    } else {
        sockaddr_in6 addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(port);
        memcpy(&addr.sin6_addr, address.getAddr(), 16);

        if(::connect(fd, (const sockaddr*)&addr, sizeof(addr)) != 0) {
            close();
            return false;
        }
    }

    return attach(fd);
}

bool ElaraUiRpcPeer::attach(int accepted_fd) {
    if(accepted_fd <= 0) {
        return false;
    }

    fd = accepted_fd;
    running = true;

    if(pthread_create(&receiver_thread, 0, receiverThreadEntry, this) != 0) {
        running = false;
        ::close(fd);
        fd = 0;
        return false;
    }

    receiver_started = true;
    return true;
}

void ElaraUiRpcPeer::close() {
    int current_fd = fd;
    fd = 0;
    running = false;

    if(current_fd > 0) {
        shutdown(current_fd, SHUT_RDWR);
        ::close(current_fd);
    }

    if(receiver_started) {
        pthread_join(receiver_thread, 0);
        receiver_started = false;
    }

    Mutex::Lock lock(pending_lock);

    for(int i = 0; i < (int)pending_calls.length(); i++) {
        if(pending_calls[i]) {
            Mutex::Lock call_lock(pending_calls[i]->mutex);
            pending_calls[i]->completed = true;
            pending_calls[i]->ok = false;
            pending_calls[i]->error_code = "connection_closed";
            pending_calls[i]->error_message = "The RPC peer connection was closed";
        }
    }

    pending_calls.clear();
}

bool ElaraUiRpcPeer::isConnected() const {
    return fd > 0 && running;
}

void ElaraUiRpcPeer::addService(Ref<sockets::rpc::json::JsonRPCService> service) {
    registry.addService(service);
}

bool ElaraUiRpcPeer::sendAll(const char* buffer, size_t length) {
    if(fd <= 0) {
        return false;
    }

    size_t written = 0;

    while(written < length) {
        ssize_t sent = ::send(fd, &buffer[written], length - written, 0);

        if(sent <= 0) {
            return false;
        }

        written += (size_t)sent;
    }

    return true;
}

bool ElaraUiRpcPeer::recvAll(char* buffer, size_t length) {
    if(fd <= 0) {
        return false;
    }

    size_t received = 0;

    while(received < length) {
        ssize_t got = ::recv(fd, &buffer[received], length - received, 0);

        if(got <= 0) {
            return false;
        }

        received += (size_t)got;
    }

    return true;
}

bool ElaraUiRpcPeer::sendPayload(const String& payload) {
    ByteArray frame = sockets::rpc::json::JsonRPCCodec::framePayload(payload);
    Mutex::Lock lock(send_lock);
    return sendAll((const char*)frame, frame.length());
}

bool ElaraUiRpcPeer::sendFramedBytes(const ByteArray& framed) {
    Mutex::Lock lock(send_lock);
    return sendAll((const char*)framed, framed.length());
}

bool ElaraUiRpcPeer::call(
    const String& method,
    const String& params_json,
    String& result_json,
    String& error_code,
    String& error_message,
    int timeout_ms
) {
    if(!isConnected()) {
        error_code = "not_connected";
        error_message = "The RPC peer is not connected";
        return false;
    }

    String id = String((unsigned long long)next_request_id++);
    Ref<ElaraUiRpcPendingCall> pending_call(new ElaraUiRpcPendingCall(id));

    {
        Mutex::Lock lock(pending_lock);
        pending_calls.push(pending_call);
    }

    bool send_ok;
    if(use_brpc) {
        using namespace sockets::rpc::brpc;
        ByteArray msg    = BRpcRpcCodec::buildRequest(id, method, params_json);
        ByteArray framed = BRpcRpcCodec::framePayload(msg);
        send_ok = sendFramedBytes(framed);
    } else {
        String request = sockets::rpc::json::JsonRPCCodec::buildRequest(id, method, params_json);
        send_ok = sendPayload(request);
    }

    if(!send_ok) {
        removePendingCall(id);
        error_code = "send_failed";
        error_message = "Failed to write request to the RPC peer";
        return false;
    }

    bool ok = waitForPendingCall(
        pending_call,
        result_json,
        error_code,
        error_message,
        timeout_ms
    );

    removePendingCall(id);
    return ok;
}

bool ElaraUiRpcPeer::notify(
    const String& method,
    const String& params_json,
    int timeout_ms
) {
    String result_json;
    String error_code;
    String error_message;

    return call(
        method,
        params_json,
        result_json,
        error_code,
        error_message,
        timeout_ms
    );
}

bool ElaraUiRpcPeer::waitForPendingCall(
    Ref<ElaraUiRpcPendingCall> pending_call,
    String& result_json,
    String& error_code,
    String& error_message,
    int timeout_ms
) {
    int waited_ms = 0;

    while(waited_ms <= timeout_ms) {
        {
            Mutex::Lock lock(pending_call->mutex);

            if(pending_call->completed) {
                result_json = pending_call->result_json;
                error_code = pending_call->error_code;
                error_message = pending_call->error_message;
                return pending_call->ok;
            }
        }

        usleep(1000);
        waited_ms++;
    }

    error_code = "timeout";
    error_message = "The RPC peer did not respond in time";
    return false;
}

void ElaraUiRpcPeer::completePendingCall(
    const String& id,
    bool ok,
    const String& result_json,
    const String& error_code,
    const String& error_message
) {
    Mutex::Lock lock(pending_lock);

    for(int i = 0; i < (int)pending_calls.length(); i++) {
        if(pending_calls[i] && pending_calls[i]->id == id) {
            Mutex::Lock call_lock(pending_calls[i]->mutex);
            pending_calls[i]->completed = true;
            pending_calls[i]->ok = ok;
            pending_calls[i]->result_json = result_json;
            pending_calls[i]->error_code = error_code;
            pending_calls[i]->error_message = error_message;
            return;
        }
    }
}

void ElaraUiRpcPeer::removePendingCall(const String& id) {
    Mutex::Lock lock(pending_lock);

    for(int i = 0; i < (int)pending_calls.length(); i++) {
        if(pending_calls[i] && pending_calls[i]->id == id) {
            pending_calls.remove(i);
            return;
        }
    }
}

void ElaraUiRpcPeer::handleIncomingPayload(const char* data, size_t len) {
    String id;
    bool ok = false;
    String result_json;
    String error_code;
    String error_message;
    String parse_error;

    if(use_brpc) {
        using namespace sockets::rpc::brpc;

        if(BRpcRpcCodec::parseResponse(data, len, id, ok, result_json, error_code, error_message, parse_error)) {
            completePendingCall(id, ok, result_json, error_code, error_message);
            return;
        }

        String notif_method, notif_params;
        if(BRpcRpcCodec::parseNotification(data, len, notif_method, notif_params, parse_error)) {
            registry.dispatchNotificationParsed(notif_method, notif_params);
            return;
        }

        String req_id, req_method, req_params;
        if(BRpcRpcCodec::parseRequest(data, len, req_id, req_method, req_params, parse_error)) {
            String res_json, err_code, err_msg;
            if(registry.dispatchParsed(req_id, req_method, req_params, res_json, err_code, err_msg)) {
                ByteArray resp   = BRpcRpcCodec::buildSuccessResponse(req_id, res_json);
                ByteArray framed = BRpcRpcCodec::framePayload(resp);
                sendFramedBytes(framed);
            } else {
                ByteArray resp   = BRpcRpcCodec::buildErrorResponse(req_id, err_code, err_msg);
                ByteArray framed = BRpcRpcCodec::framePayload(resp);
                sendFramedBytes(framed);
            }
        }
        return;
    }

    // JSON path
    String payload(data, (int)len);

    if(sockets::rpc::json::JsonRPCCodec::parseResponse(
        payload, id, ok, result_json, error_code, error_message, parse_error
    )) {
        completePendingCall(id, ok, result_json, error_code, error_message);
        return;
    }

    String notif_method, notif_params;
    if(sockets::rpc::json::JsonRPCCodec::parseNotification(
        payload, notif_method, notif_params, parse_error
    )) {
        registry.dispatchNotification(payload);
        return;
    }

    String response_json;
    registry.dispatch(payload, response_json);
    sendPayload(response_json);
}

void ElaraUiRpcPeer::receiverLoop() {
    while(running && fd > 0) {
        char prefix[4];

        if(!recvAll(prefix, 4)) {
            break;
        }

        uint32_t length = ((uint32_t)(prefix[0] & 0xFF) << 24)
            | ((uint32_t)(prefix[1] & 0xFF) << 16)
            | ((uint32_t)(prefix[2] & 0xFF) << 8)
            | (uint32_t)(prefix[3] & 0xFF);

        ByteArray payload;
        payload.append((int)length);

        if(!recvAll((char*)payload, length)) {
            break;
        }

        handleIncomingPayload((const char*)payload, (size_t)payload.length());
    }

    running = false;
}

void* ElaraUiRpcPeer::receiverThreadEntry(void* instance) {
    ((ElaraUiRpcPeer*)instance)->receiverLoop();
    return 0;
}

}
}
}
