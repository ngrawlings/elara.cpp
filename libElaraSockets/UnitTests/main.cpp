//
//  main.cpp
//  UnitTests
//
//  Created by Nyhl Rawlings on 07/06/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include <libelaracore/memory/Ref.h>
#include <libelarathreads/Task.h>
#include <libelarathreads/Thread.h>

#include "Sock5ProxyServer/Server.h"

#include <libelaradebug/UnitTests.h>

#include "DataLoopServer/Server.h"
#include "DataLoopServer/Client.h"
#include "brpc/BRpcTests.h"

using namespace elara;

Ref<Server> socks5_server;
EventBase *ev;

bool testLaunchSoxks5ProxyServer() {
    socks5_server = Ref<Server>(new Server);
    socks5_server.getPtr()->runEventLoop(true);
    
    return true;
}

bool testCloseSock5ProxyServer() {
    sleep(30);
    
    socks5_server.getPtr()->breakEventLoop();
    socks5_server.getPtr()->stop();
    socks5_server = Ref<Server>(0);
    
    return true;
}

bool testLaunchListenServer() {
    DataLoopServer server(ev);
    server.runEventLoop(false);
    return true;
}

bool testLaunchClient() {
    Client c;
    
    ev->runEventLoop(false);
    
    return true;
}


int main(int argc, const char * argv[]) {
    Task::staticInit();
    Thread::init(8);
    
    ev = new EventBase();
    
    Socket::init(ev);
    
    UnitTests tests;

    // ── BRpc codec tests (no network required) ────────────────────────────────
    tests.addTest("brpc_roundtrip_byte",           brpc_roundtrip_byte);
    tests.addTest("brpc_roundtrip_short",          brpc_roundtrip_short);
    tests.addTest("brpc_roundtrip_int",            brpc_roundtrip_int);
    tests.addTest("brpc_roundtrip_long",           brpc_roundtrip_long);
    tests.addTest("brpc_roundtrip_string",         brpc_roundtrip_string);
    tests.addTest("brpc_roundtrip_empty_string",   brpc_roundtrip_empty_string);
    tests.addTest("brpc_roundtrip_named_byte",     brpc_roundtrip_named_byte);
    tests.addTest("brpc_roundtrip_named_short",    brpc_roundtrip_named_short);
    tests.addTest("brpc_roundtrip_named_int",      brpc_roundtrip_named_int);
    tests.addTest("brpc_roundtrip_named_long",     brpc_roundtrip_named_long);
    tests.addTest("brpc_roundtrip_named_string",   brpc_roundtrip_named_string);
    tests.addTest("brpc_roundtrip_array_unnamed",  brpc_roundtrip_array_unnamed);
    tests.addTest("brpc_roundtrip_object",         brpc_roundtrip_object);
    tests.addTest("brpc_roundtrip_nested_arrays",  brpc_roundtrip_nested_arrays);
    tests.addTest("brpc_roundtrip_empty_array",    brpc_roundtrip_empty_array);
    tests.addTest("brpc_roundtrip_mixed_object",   brpc_roundtrip_mixed_object);
    tests.addTest("brpc_sequential_reads",         brpc_sequential_reads);
    tests.addTest("brpc_skip_value",               brpc_skip_value);
    tests.addTest("brpc_boundary_values",          brpc_boundary_values);
    tests.addTest("brpc_truncated_input",          brpc_truncated_input);
    tests.addTest("brpc_wrong_type_rejected",      brpc_wrong_type_rejected);
    tests.addTest("brpc_wire_layout_byte",         brpc_wire_layout_byte);
    tests.addTest("brpc_wire_layout_array_header", brpc_wire_layout_array_header);

    //tests.addTest("testLaunchSoxks5ProxyServer", testLaunchSoxks5ProxyServer);
    //tests.addTest("testCloseSock5ProxyServer", testCloseSock5ProxyServer);
    if (argc == 2 && argv[1][0] == 'c') {
        tests.addTest("testLaunchClient", testLaunchClient);
    } else
        tests.addTest("testLaunchListenServer", testLaunchListenServer);

    tests.run();
    
    Thread::stopAllThreads();
    Thread::staticCleanUp();
    Task::staticCleanup();
    
    return 0;
}
