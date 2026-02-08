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
#include "connect_to_tracker.h"
using namespace std;
int client_fd = 0;

vector<char*> tokenise(string &s);
void update_tracker(string &message);
void parse(vector<char *>&vec,string &s,bool &loginStatus);
void sendMessage(int sock, const std::string &msg);

void sync_tracker(string s){

}

void p_execution(int clientSocket){
    bool loginStatus = false;
    //we need to connect to the second_tracker

    while(true){
        
        // memset(&buffer[0],0,sizeof(buffer));
        // cout << "atleast here" << endl;
        int size; //size is like a buffer to store data , in my case it's like 
        int read = recv(clientSocket,&size,sizeof(size),MSG_WAITALL); // here read denotes the amount of data read
        if(read <= 0){
            cout << "client is not available" << endl;
        }
        //size amount it is goint to recieve
        
        else {
            int t = ntohl(size);
            string buffer(t,'\n');
            read = recv(clientSocket,&buffer[0],t,MSG_WAITALL);
            if(read < 0)cout << "error" << endl;
            else if(read == 0)cout << "nothing to read" << endl;
            else{
                cout << "client sent " << string(buffer) << endl;
                string temp = buffer;
                vector<char*> args = tokenise(buffer);
                parse(args,buffer,loginStatus);
                sync_tracker(temp);
                //here we need to send the update the tracker2
                // string sender(args[args.size()-2]);
                // for(auto it:user_base){
                //     cout << it.first << " " << it.second << endl;
                // }

                string  reply = "server got: " + string(buffer) + '\n';
                // sleep(5);
                cout << reply << endl;
                sendMessage(clientSocket,reply);
            }
        }
    }

}
