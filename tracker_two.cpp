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
// #include "createconnection.h"
// #include "thread_spawn.h"

using namespace std;
int tracker2_fd = 0;
int sequence_number = 0;

struct group
{
    string group_id;
    string owner;
    set<string>waitList;
    set<string> members;
};



void sendMessage(int sock, const std::string &msg) {
    uint32_t len = msg.size();
    uint32_t netlen = htonl(len);  // convert to network byte order (big endian)
    int sent = 0;
    send(sock, &netlen, sizeof(netlen), 0);

    while (sent < len) {
        // cout << "reachere" << endl;
        ssize_t n = send(sock, msg.c_str() + sent, len - sent, 0);
        // cout << "and   here" << endl;
        if (n <= 0) {
            perror("send");            // prints system error string
            break;// don’t just return silently
        }
        sent += n;
    }

}

int create_connection(int port){
    int listening = socket(AF_INET,SOCK_STREAM,0);
    if(listening < 0){
        cerr << "Unable to create socket";
        return -1;
    }
    int enable = 1;
    if (setsockopt(listening, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        cerr << "setsockopt(SO_REUSEADDR) failed" << endl;
        // Optionally, you can return an error here, but the program can continue.
    }
    // no we need to bind the socket to IP / port
    sockaddr_in hint;
    memset(&hint,0,sizeof(hint));
    hint.sin_family = AF_INET;
    hint.sin_port = htons(port);  
    inet_pton(AF_INET, "0.0.0.0", &hint.sin_addr);
    // now we need to bind to local port
    
    if(bind(listening, (sockaddr *)&hint ,sizeof(hint)) == -1){
        cerr << "Cant bind to IP";
        return -2;
    }
    
    //mark the port as listening
    //  2nd param is how many backlog can be there
    if(listen(listening,SOMAXCONN) == -1){
        cerr << "Cant listen" << endl;
        return -3;
    }
    return listening;
    
}

//-------------------------------------------------------------------------------

map <string,string> user_base;
// map <string,string> IsLoggedIn;
// map <int,vector<int>> createGroup;
map <string ,group*> groupInfo;
map<string,bool> login_status;
map<int,string> fd_to_user;
//-------------------------------------------------------------------------------
// map <int,

vector<char*> tokenise(string &s) {
    vector<char*> args;
    // string temp = s;
    char* start = &s[0];
    char *token = strtok(start, " ");
    while(token != nullptr){
        // cout << token << endl;   // this is why you see the 2nd "ls" printed
        if (strlen(token) > 0) {       // skip empty tokens
            args.push_back(token);
        }
        token = strtok(nullptr, " ");
    }
    args.push_back(nullptr); // sentinel for execvp
    return args;
}

// void login()
void parse(vector<char *>&vec,int clientSocket,string username,string temp){
    if(vec[0] == nullptr)return;
    string cmd(vec[0]);
    if(cmd == "create_user"){
        if(vec.size() != 4){
            cerr << "invalid args" << endl;
            return;
        }
        cout << "created user " << endl;
        string username(vec[1]);
        string password(vec[2]);
        if(user_base.find(username) != user_base.end()){
            sendMessage(clientSocket,"username already exists\n");
            return;
        }   
        else user_base[username] = password;
        
    }
    else if(cmd == "login"){
        if(vec.size() != 4){
            cerr << "invalid args" << endl;
            return;
        }
        string username(vec[1]);
        string password(vec[2]);
        if(user_base.find(username) == user_base.end()){
           sendMessage(clientSocket,"invalid username\n");  
            return;
        }
        else{
            if(user_base[username] == password){
                login_status[username] = true;
                cout << "Welcome " << username << endl;
                sendMessage(clientSocket,"successful" + username + "\n");
                // update_tracker(temp,tracker2_fd,clientSocket); 
            }
            else{
                cout << "invalid password!!" << endl;
            }
            
        }

    }
    //at this point the client should get its username

    else if(login_status[username] == false || username == "INVALID"){
        cout << "you first need to login" << endl;
        return;
    }
    else if(cmd == "create_group"){
        //this should return the local sequence number

        //args size must be 3
        if(vec.size() != 3){
            cerr << "invalid arguments" << endl;
            return;
        }
        string groupId(vec[1]);
        // int groupId = stoi(str_groupId);
        group *newGroup = new group;
        newGroup->group_id = groupId;
        newGroup->owner = username;
        groupInfo[groupId] = newGroup;
        cout << "size of group info is " << groupInfo.size() << endl;
        newGroup->members.insert(username);
        cout << "created group successfully" << endl;
        sendMessage(clientSocket,"created group successfully\n");   
        // update_tracker(temp,tracker2_fd,clientSocket);
        // newGroup.members = vec;

    }
    else if(cmd == "join_group"){
        //when i join a group i should be able to insert my fd into that group
        if(vec.size() != 3){
            cout << "invalid arguments" << endl;
            return;
        }
        else{
            string id(vec[1]);
            // int id = stoi(str_groupId);
            if(groupInfo.find(id) == groupInfo.end()){
                cout << "invalid group id" << endl;
                sendMessage(clientSocket,"invalid group id\n");
                return; 
            }
            else{
                if(groupInfo[id]->members.find(username) != groupInfo[id]->members.end()){
                    cout << "already a member of the group" << endl;
                    sendMessage(clientSocket,"already a member of the group\n");
                    return;
                }
                else{
                    groupInfo[id]->waitList.insert(username);
                    cout << "added to waitlist successfully" << endl;
                    sendMessage(clientSocket,"added to waitlist successfully\n");
                    // update_tracker(temp,tracker2_fd,clientSocket);
                    return;
                }
            }
        }
    }
    else if(cmd == "leave_group"){
        if(vec.size() != 3){
            cout << "invalid arguments" << endl;
            return;
        }
        string id(vec[1]);
        // int id = stoi(str_groupId);
        if(groupInfo.find(id) == groupInfo.end()){
            cout << "invalid group id" << endl;
        }
        else{
            if(groupInfo[id]->members.find(username) == groupInfo[id]->members.end()){
                cout << "not a member of the group" << endl;
                sendMessage(clientSocket,"not a member of the group\n");
                return;
            }
            else{
                groupInfo[id]->members.erase(username);
                cout << "removed from group successfully" << endl;
                sendMessage(clientSocket,"removed from group successfully\n");
                // update_tracker(temp,tracker2_fd,clientSocket);
                return;
            }
        }
    }
    else if(cmd == "accept_requests"){
        if(vec.size() != 4){
            cout << "invalid arguments" << endl;
            return;
        }
        string id(vec[1]);
        // int id = stoi(str_groupId);
        string waiting_user(vec[2]);
        if(groupInfo[id]->owner == username){
            //the owner will enter this area
            if(groupInfo[id]->waitList.find(waiting_user) != groupInfo[id]->waitList.end()){
                //user exist in waitlist
                groupInfo[id]->members.insert(waiting_user);
                groupInfo[id]->waitList.erase(waiting_user);
                cout << "user accepted successfully" << endl;
                sendMessage(clientSocket,"user accepted successfully\n");
            }
            else{
               cout << "No such pending request in accept_request" << endl;
               sendMessage(clientSocket,"No such pending request in accept_request\n");
            }
        }
        else{
            cerr << "permission denied" << endl;
            sendMessage(clientSocket,"permission denied\n");
            return;
        }
        return;
    }
    else if(cmd == "list_groups"){
        if(vec.size() != 2){
            cout << "invalid arguments" << endl;
            return;
        }
        cout << "group size " << groupInfo.size() << endl; 
        string reply = "group size " + to_string(groupInfo.size()) + "\n";
        for(auto it:groupInfo){
            cout << it.first << endl;
            reply += (it.first) + "\n";
        }
        sendMessage(clientSocket,reply);
    }
    else if(cmd == "list_requests"){
        if(vec.size() != 2){
            cout << "invalid arguments" << endl;
            return;
        }
        string id(vec[1]);
        // int id = stoi(str_groupId);
        if(groupInfo[id]->owner == username){
            string reply = "";
            for(auto it:groupInfo[id]->waitList){
                cout << it << endl;
                reply += it + "\n";
            }
            sendMessage(clientSocket,reply);
        }
        else{
            cerr << "permission denied" << endl;
            sendMessage(clientSocket,"permission denied\n");
            return;
        }
        return;
    }
    else if(cmd == "leave_group"){
        if(vec.size() != 3){
            cout << "invalid arguments" << endl;
            return;
        }
        string id(vec[1]);
        // int id = stoi(str_groupId);
        if(groupInfo.find(id) == groupInfo.end()){
            cout << "invalid group id" << endl;
        }
        else{
            groupInfo[id]->members.erase(username);
            cout << "removed from group successfully" << endl;
            sendMessage(clientSocket,"removed from group successfully\n");
            // update_tracker(temp,tracker2_fd,clientSocket);
            return;
        }
        
    }
    else if(cmd == "logout"){
        login_status[username] = false;
        cout << username << " logged out successfully" << endl;
        sendMessage(clientSocket,username + " logged out successfully\n");
        return;
    }
    else{
        cout << "invalid command" << endl;
        sendMessage(clientSocket,"invalid command\n");
        return;
    }
}
void middle_ware(vector<char *>&vec,int clientSocket,string &temp){

    if(vec[0] == nullptr)return;
    string cmd(vec[0]);
    string username;
    if(cmd == "FLAG"){
        string temp(vec[1]);
        username = temp;
        vec.erase(vec.begin());
        vec.erase(vec.begin());
    }
    else{
        username = string(vec[0]);
        cout << "heree user name is " << username << endl;  
        vec.erase(vec.begin());

    }
        //username can be invalid
    // if(cmd == "FLAG"){
    //     //this message is sent by the other tracker
    //     //we should extract the username
    //     string username(vec[1]);
    //     //username can be invalid
    //     vec.erase(vec.begin());
    //     vec.erase(vec.begin());
    //     parse(vector<char *>vec,clientSocket)
        
    // }
    
        //first will be username for sure
    parse(vec,clientSocket,username,temp);
    
    


}


void p_execution(int clientSocket){
    while(true){
        
        // memset(&buffer[0],0,sizeof(buffer));
        // cout << "atleast here" << endl;
        // cout << clientSocket << endl;      
        int size; //size is like a buffer to store data , in my case it's like 
        int read = recv(clientSocket,&size,sizeof(size),MSG_WAITALL); // here read denotes the amount of data read
        if(read <= 0){
            cout << "a client got disconnected" << endl;
            close(tracker2_fd);
            return;
            
        }
        //size amount it is goint to recieve
        
        else {
            int t = ntohl(size);
            cout << "this size of data is " << t << endl;
            string buffer(t,'\n');
            read = recv(clientSocket,&buffer[0],t,MSG_WAITALL);
            if(read < 0)cout << "error" << endl;
            else if(read == 0)cout << "nothing to read" << endl;
            else{
                cout << "client sent " << string(buffer) << endl;
                string temp = buffer;
                cout << "message is " << temp << endl;
                vector<char*> args = tokenise(buffer);

                middle_ware(args,clientSocket,temp);
                
                for(auto it:login_status){
                    cout << it.first << " " << it.second << endl;
                }
                string  reply = "server got: " + string(buffer) + '\n';
                // sleep(5);
                cout << reply << endl;
                // cout << "login status is " << login_status[clientSocket] << endl;fclien
             
            }
        }
    }

}

int main(){
    //create a socket
    int port_fd = create_connection(54001); // this port is used for listening for upcoming request;
    cout << port_fd << endl;
    const char* ip = "127.0.0.1";
    // tracker2_fd = connect_to_tracker_2(54000,ip);

    //first we need to set it
    while(1){

        sockaddr_in client;  
        socklen_t clientSize = sizeof(client);
        
        int clientSocket = accept(port_fd,(sockaddr *)&client,&clientSize);
        if(clientSocket == -1){
            cerr <<"problem with client connections";
            return -4;
        }
        else cerr << "accepted successfully" << endl;
        thread th(p_execution,clientSocket);    
        th.detach();

    }
  
}