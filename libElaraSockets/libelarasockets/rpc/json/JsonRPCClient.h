#ifndef ElaraSockets_JsonRPCClient_h
#define ElaraSockets_JsonRPCClient_h

#include <libelaracore/memory/String.h>
#include <libelarathreads/Mutex.h>

namespace elara {
namespace sockets {
namespace rpc {
namespace json {

    class JsonRPCClient {
    public:
        JsonRPCClient();
        virtual ~JsonRPCClient();

        bool connect(const String &remote_address, unsigned short port);
        void close();
        bool isConnected() const;

        bool call(const String &method, const String &params_json, String &result_json, String &error_code, String &error_message);

    private:
        int fd;
        unsigned long long next_request_id;
        Mutex call_lock;

        bool sendAll(const char *buffer, size_t length);
        bool recvAll(char *buffer, size_t length);
    };

}
}
}
}

#endif
