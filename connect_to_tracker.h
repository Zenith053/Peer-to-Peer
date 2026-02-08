#ifndef TRACKER_CONNECT
#define TRACKER_CONNECT

#include <bits/stdc++.h>
#include <iostream>
#include <netdb.h>
#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>
#include <thread>
#include <unistd.h>
// #include <winsock.h>
#include <string>
#include <vector>

using namespace std;

int connect_to_tracker_2(int port,const char* ip){
    int sock = socket(AF_INET,SOCK_STREAM,0);
    int timeout = 5;
    sockaddr_in tracker_two;
    tracker_two.sin_family = AF_INET;
    tracker_two.sin_port = htons(port);

    inet_pton(AF_INET, ip, &tracker_two.sin_addr);
    auto start = chrono::steady_clock::now();
    while (true)
    {
        if(connect(sock,(sockaddr *)&tracker_two,sizeof(tracker_two)) == 0){
            cout << "connected successfully";
            return sock;
        }
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= timeout) {
            cerr << "Error: server not reachable within " << timeout << " seconds\n";
            close(sock);
            return -1;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1)); // wait a bit before retry
    }
    

}

#endif 