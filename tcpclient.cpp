#include <stdio.h>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fstream>
#include <atomic>
#include <thread>
// #include <netdb.h>

using namespace std;
int sock_fd = 0;
int tracker_two = 0;
int sendMessage(int sock, const std::string &msg) {
    uint32_t len = msg.size();
    uint32_t netlen = htonl(len);  // convert to network byte order (big endian)

    // Send length first
    int n;
    n += send(sock, &netlen, sizeof(netlen), 0);

    // Send message data
    n += send(sock, msg.c_str(), msg.size(), 0);
    return n;
}
string recvMessage(int sock) {
    uint32_t netlen;
    int bytes = recv(sock, &netlen, sizeof(netlen), MSG_WAITALL);
    if (bytes <= 0) return "";  // connection closed or error

    uint32_t len = ntohl(netlen);  // convert length to host order
    string msg(len, '\0');    // allocate buffer of right size

    bytes = recv(sock, &msg[0], len, MSG_WAITALL);
    if (bytes <= 0) return "";

    return msg;
}

int main(int argc,char *argv[]){
    sock_fd = socket(AF_INET,SOCK_STREAM,0);
    if(sock_fd < 0){
        cout << "unable to create socket" << endl;
        return(1);
    }
    string ip = "127.0.0.1";
    struct sockaddr_in address;
    // address.sin_addr.s_addr = inet_pton()
    address.sin_port = htons(54000);

    address.sin_family = AF_INET;
    inet_pton(AF_INET,&ip[0],&address.sin_addr);
    string s = "hello this is a message from client";
    // cout << "reached_here" << endl;
    //this is where we connect
    int connection = connect(sock_fd,(sockaddr *)&address,sizeof address);
    
    // now once connection is 
    sendMessage(sock_fd,s);
    // string reply = recvMessage(sock_fd);
    // cout <<reply << endl;
    

    atomic<bool> alive(true);

    if(connection ==0){

        cout << "the client is now connected to tracker 1" << endl;
        thread heart_beat([&] {
            string ping = "PING PONG";
            while(alive.load()){

                ssize_t n = sendMessage(sock_fd, ping);
                if (n <= 0) {
                    cerr << "lost connection to tracker 1\n";
                    alive.store(false);        // signal main thread
                    break;
                }
                this_thread::sleep_for(std::chrono::seconds(7));
            }
        });
        while (alive.load())
        {
                
            string line;
            // line += "clt";
            if(line == "quit")exit(0);
            sendMessage(sock_fd,line);
            string ans = recvMessage(sock_fd);
            // if(ans == "")
            cout << ans << endl;
            getline(cin,line);
            // sendMessage(sock_fd,s);
            
        }   
    }
    alive.store(true);

    tracker_two = socket(AF_INET,SOCK_STREAM,0);
    if(tracker_two < 0){
        cout << "unable to create socket" << endl;
        return(1);
    }
    // string ip = "127.0.0.1";
    // struct sockaddr_in address;
    // address.sin_addr.s_addr = inet_pton()
    address.sin_port = htons(54001);

    // address.sin_family = AF_INET;
    inet_pton(AF_INET,&ip[0],&address.sin_addr);
    // string s = "hello this is a message from client";
    // cout << "reached_here" << endl;
    //this is where we connect
    int connection_two = connect(tracker_two,(sockaddr *)&address,sizeof address);
    
    // now once connection is 
    sendMessage(sock_fd,s);
        //now the client
    if(connection_two == 0){
        cout << "the client is now connected to tracker 2" << endl;
        thread heart_beat([&] {
            string ping = "PING";
            while(alive.load()){

                ssize_t n = sendMessage(sock_fd, ping);
                if (n <= 0) {
                    cerr << "lost connection to tracker 1\n";
                    alive.store(false);        // signal main thread
                    break;
                }
                this_thread::sleep_for(std::chrono::seconds(7));
            }
        });
        while (alive.load())
        {
                
            string line;
            // line += "clt";
            getline(cin,line);  
            if(line == "quit")exit(0);
            sendMessage(sock_fd,line);
            string ans = recvMessage(sock_fd);
            // if(ans == "")
            cout << ans << endl;
            // sendMessage(sock_fd,s);
            
        }   
    }
        cout << "no tracker available" << endl;
        exit(0);
            

    
}