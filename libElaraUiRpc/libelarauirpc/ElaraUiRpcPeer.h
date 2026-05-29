#ifndef ELARA_UI_RPC_PEER_H
#define ELARA_UI_RPC_PEER_H

#include <pthread.h>

#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/ByteArray.h>
#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/String.h>
#include <libelarathreads/Mutex.h>
#include <libelarasockets/rpc/json/JsonRPCRegistry.h>
#include <libelarasockets/rpc/json/JsonRPCService.h>

namespace elara {
namespace ui {
namespace rpc {

class ElaraUiRpcPendingCall;

class ElaraUiRpcPeer {
public:
    ElaraUiRpcPeer();
    virtual ~ElaraUiRpcPeer();

    bool connect(const String& remote_address, unsigned short port);
    bool attach(int accepted_fd);
    void close();
    bool isConnected() const;

    // Switch codec before calling attach(). Default is BRPC (true).
    // Set false to use JSON RPC (original protocol).
    void setUseBrpc(bool use_brpc);

    void addService(Ref<sockets::rpc::json::JsonRPCService> service);

    bool call(
        const String& method,
        const String& params_json,
        String& result_json,
        String& error_code,
        String& error_message,
        int timeout_ms = 5000
    );

    bool notify(
        const String& method,
        const String& params_json,
        int timeout_ms = 5000
    );

protected:
    bool sendAll(const char* buffer, size_t length);
    bool recvAll(char* buffer, size_t length);
    bool sendPayload(const String& payload);
    bool sendFramedBytes(const ByteArray& framed);

    void receiverLoop();
    void handleIncomingPayload(const char* data, size_t len);
    bool waitForPendingCall(
        Ref<ElaraUiRpcPendingCall> pending_call,
        String& result_json,
        String& error_code,
        String& error_message,
        int timeout_ms
    );
    void completePendingCall(
        const String& id,
        bool ok,
        const String& result_json,
        const String& error_code,
        const String& error_message
    );
    void removePendingCall(const String& id);

    static void* receiverThreadEntry(void* instance);

private:
    int fd;
    bool running;
    bool receiver_started;
    bool use_brpc;
    unsigned long long next_request_id;
    pthread_t receiver_thread;

    Mutex send_lock;
    Mutex pending_lock;

    sockets::rpc::json::JsonRPCRegistry registry;
    Array< Ref<ElaraUiRpcPendingCall> > pending_calls;
};

}
}
}

#endif
