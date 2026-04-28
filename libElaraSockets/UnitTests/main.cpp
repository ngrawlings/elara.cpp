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

using namespace nrcore;

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
