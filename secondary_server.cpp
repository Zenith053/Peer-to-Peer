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
int sequence_no = 1000;

struct user
{
    string username;
    string password;
    string ip_address;
    int port;
};

// struct file_info{
//     string file_name;
//     int file_size;
//     int piece_size;
//     int no_of_pieces;
//     vector<string> piece_hashes; // this is the sha256 hash of each piece
//     map<int,vector<pair<struct user,string>>> seeders; // this is the map of piece number to the list of seeders for that piece
//     // first int is the piece number and second is the list of seeders for that piece
//     // struct user is the user info and string is the source address for that piece
//     // map<string,vector<struct user>> leechers; // this is the map of file name to the list of leechers for that file
// };
struct group
{
    string group_id;
    string owner;
    set<string>waitList;
    set<string> members;
    vector<string> piece_hashes = {}; // by default empty
    map<string,map<int,map<string,pair<string,string>>>> files;
    map<string,int> file_size; // file name to file size
    //filename, piece no, username, file path, hash value
    
};


int sequence_number = 0;


void sendMessage(int sock, const std::string &msg) {
    uint32_t len = msg.size();
    uint32_t netlen = htonl(len);
    
    
    const char* len_ptr = (const char*)&netlen;
    size_t len_bytes_left = sizeof(netlen);
    
    while (len_bytes_left > 0) {
        ssize_t n = send(sock, len_ptr, len_bytes_left, 0);
        
        if (n <= 0) {
            perror("send length failed");
            return;
        }
        len_ptr += n;
        len_bytes_left -= n;
    }

    size_t msg_bytes_left = len;
    const char* msg_ptr = msg.c_str();

    while (msg_bytes_left > 0) {
        ssize_t n = send(sock, msg_ptr, msg_bytes_left, 0);
        
        if (n <= 0) {
            perror("send message failed");
            return;
        }
        msg_ptr += n;
        msg_bytes_left -= n;
    }
}
std::string recvMessage(int sock) {
    // First, receive the 4-byte length prefix
    uint32_t netlen;
    char* len_ptr = (char*)&netlen;
    size_t len_bytes_left = sizeof(netlen);
    
    while (len_bytes_left > 0) {
        ssize_t n = recv(sock, len_ptr, len_bytes_left, 0);
        if (n <= 0) {
            perror("recv length failed");
            return "";
        }
        len_ptr += n;
        len_bytes_left -= n;
    }
    
    uint32_t len = ntohl(netlen);
    
    // Now receive the actual message
    std::string msg;
    msg.resize(len);
    char* msg_ptr = &msg[0];
    size_t msg_bytes_left = len;
    
    while (msg_bytes_left > 0) {
        ssize_t n = recv(sock, msg_ptr, msg_bytes_left, 0);
        if (n <= 0) {
            perror("recv message failed");
            return "";
        }
        msg_ptr += n;
        msg_bytes_left -= n;
    }
    
    return msg;
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
map<string,pair<string,int>> seed_info; // where int represent the piece number and struct seed_info* is the information about the piece
// map <string,string> IsLoggedIn;
// map <int,vector<int>> createGroup;
map <string ,group*> groupInfo;
map<int, group*> waitList;
// map<string,string>store_hash; 
map<string,bool> login_status;


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

bool check_connection(int socket){
    ssize_t n = send(socket, "", 0, MSG_NOSIGNAL);
    if(n == -1){
        return false;
    }
    return true;
}
void update_tracker(string message,int tracker_fd,string username){
    cout << "message is_ " << message << endl;
    if(tracker_fd <= 0)return;
    cout << "updating other tracker" << endl;
    // string temp = message;
    if(check_connection(tracker_fd))sendMessage(tracker_fd,message); 
    else{
        cout << "unable to sych" << endl;
    }
    // close(tracker_fd);
}

string extract_base_filename(const string& file_path_full) {
    string filename = file_path_full;

    size_t last_slash = filename.find_last_of('/');
    if (last_slash != string::npos) {
        // If found, erase everything up to and including the slash
        filename.erase(0, last_slash + 1);
    }
    // size_t last_dot = filename.find_last_of('.');
    
    // if (last_dot != std::string::npos && last_dot != 0) {
    //     // If found, erase everything from the period to the end
    //     filename.erase(last_dot);
    // }

    return filename;
}
pair<int,string> get_client_info(int client_socket_fd) {
    // Structure to hold the client's address information
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // 1. Retrieve the address of the peer (client)
    if (getpeername(client_socket_fd, (struct sockaddr*)&client_addr, &addr_len) == 0) {
        
        // 2. Convert the binary IP address to a human-readable string (e.g., "192.168.1.5")
        char* client_ip = inet_ntoa(client_addr.sin_addr);

        // 3. Convert the network-order port number to host-order integer
        int client_port = ntohs(client_addr.sin_port);

        return {client_port, string(client_ip)};
    } else {
        perror("unable to fetch client info");
    }
    return {-1, ""}; // Return an invalid port and empty string on failure
}
void parse(vector<char *>&vec,int clientSocket,string username,string temp){
    if(vec[0] == nullptr)return;
    cout << vec.size() << endl;
    string cmd(vec[0]);
    if(cmd == "create_user"){
        if(vec.size() != 4){
            cerr << "invalid args" << endl;
            return;
        }
        string username(vec[1]);
        string password(vec[2]);
        cout << "created user " << username<< endl;
        if(user_base.find(username) != user_base.end()){
            sendMessage(clientSocket,"username already exists\n");
            return;
        }   
        else {
            user_base[username] = password;
            // update_tracker(temp,tracker2_fd,username);
        }
        
    }

    else if(cmd == "login"){
        // for(auto it:login_status){
            //     cout << "login "<<  it.first << " " << it.second << endl;
        // }
        if(username != "INVALID" && login_status[username] == true){
            cout << "already logged in" << endl;
            sendMessage(clientSocket,"already logged in\n");
            return;
        }
        if(vec.size() != 4){
            cerr << "invalid args" << endl;
            return;
        }
        string username(vec[1]);
        string password(vec[2]);
        cout << "logged in " << username << endl;
        if(user_base.find(username) == user_base.end()){
            sendMessage(clientSocket,"invalid username\n");  
            return;
        }
        else{
            if(user_base[username] == password){
                login_status[username] = true;
                pair<int,string> p = get_client_info(clientSocket);
                int port = p.first;
                string ip = p.second;
                seed_info[username] = {ip,port};
                // update_tracker(temp,tracker2_fd,username);
                cout << "Welcome " << username << endl;
                sendMessage(clientSocket,"successful " + username + "\n");

                // update_tracker(temp,tracker2_fd,clientSocket); 
            }
            else{
                cout << "invalid password!!" << endl;
            }
        }
        
    }
    else if(login_status[username] == false || username == "INVALID"){
        cout << "you first need to login" << endl;
        return;
    }
    else if(cmd == "upload"){
        //the  we store the hash in the database
        if(vec.size() != 8){
            cout << vec.size() << endl;
            cerr << "invalid args" << endl;
            return;
        }
        if(vec[1] == nullptr){
            cerr << "invalid groupId" << endl;
            return; 
        }
        else if(vec[2] == nullptr){
            cerr << "invalid pieceId" << endl;
            return; 
        }
        else if(vec[3] == nullptr){
            cerr << "invalid file_path" << endl;
            return; 
        }
        else if(vec[4] == nullptr){
            cerr << "invalid file_size" << endl;
            return; 
        }
        else if(vec[5] == nullptr){
            cerr << "invalid Port_value" << endl;
            return; 
        }
        else if(vec[6] == nullptr){
            cerr << "invalid hash_value" << endl;
            return; 
        }
        string groupId(vec[1]);
        string file_path(vec[2]);
        string piece_id(vec[3]);
        string file_size_str(vec[4]);
        int file_size = stoi(file_size_str);
        string listen_port_str(vec[5]);
        int listen_port = stoi(listen_port_str);
        string hash_value(vec[6]);
        // int pieceNo = stoi(piece_id);
        string file_name = extract_base_filename(file_path);
        if(username == "INVALID"){
            cout << "you need to login first" << endl;
            sendMessage(clientSocket,"you need to login first\n");
            return;
        }
        if(groupInfo.find(groupId) == groupInfo.end()){
            cout << "invalid group id" << endl;
            return;
        }
        if(groupInfo[groupId]->members.find(username) == groupInfo[groupId]->members.end()){
            cout << "you are not a member of this group" << endl;
            return;
        }
        groupInfo[groupId]->file_size[file_name] = file_size;
        int pieceNo = stoi(piece_id);
        seed_info[username].second = listen_port;
        //-------------------------------------------
        pair<string,string> p = {username,file_path};
        // pair<int,vector<pair<string,string>>> pr = {pieceNo,{p}};
        groupInfo[groupId]->files[file_name][pieceNo][username] = {file_path, hash_value};
        groupInfo[groupId]->file_size[file_name] = file_size;
        cout << "debug: " << file_name << " " << pieceNo << " " << username << " " << file_path << endl;
        //-------------------------------------------
        cout << "hash stored successfully" << endl;
        sendMessage(clientSocket,"hash stored successfully\n");
        // cout << "hash value is " << hash_value << endl; 
        // update_tracker(temp,tracker2_fd,username);
        //no need to check if piece no is valid or not
        //if the user is valid then we update the 



    }
    else if(cmd == "download"){
        cout << "reached download" << endl;

        
        if(vec.size() != 4){
            cerr << "invalid args" << endl;
            return;
        }
        if(vec[1] == nullptr){
            cerr << "invalid groupId" << endl;
            return; 
        }
        else if(vec[2] == nullptr){
            cerr << "invalid file_name" << endl;
            return; 
        }
  
        string group_id = vec[1] ;
        string file_name = vec[2];

        map<int,map<string,pair<string,string>>> seeders = groupInfo[group_id]->files[file_name];
        
        string message;
        string header;
        //we first send the message of file size of the file to the client
        header = "abcdef " + to_string(groupInfo[group_id]->file_size[file_name]) + " " + "\n";
        cout << "header length: " << endl;
        cout << "header hex: ";
        for (size_t i = 0; i < header.size(); i++) {
            cout << hex << (int)(unsigned char)header[i] << " ";
        }

        cout << "header is " << header << endl;
        sendMessage(clientSocket,header);

        for(auto it:seeders){
            int piece_no = it.first;
            for(auto jt:it.second){
                string username = jt.first;
                if(login_status[username] == false)continue;
                string file_path = jt.second.first;
                string hash_value = jt.second.second;
                string ip = seed_info[username].first;
                int port = seed_info[username].second;
                string port_str = to_string(port);
                // but we also need ip and port of other peers

                // Now we have piece_no, username, file_path
                // We need to send this information to the client
                message += to_string(piece_no) + " " + username + " " + ip + " " + port_str + " " + file_path + " " + hash_value + "\n";
                cout << "debug: " << piece_no << " " << username << " " << ip << " " << port_str << " " << file_path << " " + hash_value << endl;
            }
        }
        message += "END_OF_FILE\n"; 
        cout << message << endl << flush;
        sendMessage(clientSocket, message);

        // return;

    }
    else if(cmd == "have_piece"){
        if(vec.size() != 7){
            cout << "something wrong in the updation message" << endl;
            return ;
        }
        int piece = 0;
        string file_name;
        string username;
        string groupId;
        string file_path;
        string hash_value;
        if(vec[1] != nullptr){
            string piece_str(vec[1]);
            piece = stoi(piece_str);
        }
        else {
            cout << "error" << endl;
            return ;
        }
        if(vec[2] != nullptr){
            file_name = string(vec[2]);
        }
        else {
            cout << "error" << endl;
            return ;
        }
        if(vec[3] != nullptr){
            username = string(vec[3]);
        }
        else {
            cout << "error" << endl;
            return ;
        }
        if(vec[4] != nullptr){
            groupId = string(vec[3]);
        }
        else {
            cout << "error" << endl;
            return ;
        }
        if(vec[4] != nullptr){
            file_path = string(vec[3]);
        }
        else {
            cout << "error" << endl;
            return ;
        }
        if(vec[5] != nullptr){
            hash_value = string(vec[3]);
        }
        else {
            cout << "error" << endl;
            return ;
        }
        //we have to update the file info that we have another seeder available
        groupInfo[groupId]->files[file_name][piece][username] = {file_path,hash_value};
        //now new user will be able to use this as a seeder

        
        
    }
    //at this point the client should get its username
    else if(cmd == "list_files"){
        if(vec.size() != 3){
            cerr << "invalid args" << endl;
            return;
        }
        if(vec[1] == nullptr){
            cerr << "invalid groupId" << endl;
            return; 
        }
        string groupId(vec[1]);
        if(groupInfo.find(groupId) == groupInfo.end()){
            cout << "invalid group id" << endl;
            return;
        }
        if(groupInfo[groupId]->members.find(username) == groupInfo[groupId]->members.end()){
            cout << "you are not a member of this group" << endl;
            return;
        }
        string reply = "";
        for(auto it:groupInfo[groupId]->files){
            reply += it.first + "\n";
            cout << it.first << endl;
        }
        cout << reply << endl;
        sendMessage(clientSocket,reply);
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
        newGroup->members.insert(username);
        // update_tracker(temp,tracker2_fd,username);
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
                    // update_tracker(temp,tracker2_fd,username);
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
                // update_tracker(temp,tracker2_fd,username);
                cout << "removed from group successfully" << endl;
                sendMessage(clientSocket,"removed from group successfully\n");
                // update_tracker(temp,tracker2_fd,clientSocket);
                return;
            }
        }
    }
    else if(cmd == "accept_request"){
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
                // update_tracker(temp,tracker2_fd,username);
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
        if(vec.size() != 3){
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
            // update_tracker(temp,tracker2_fd,username);
            sendMessage(clientSocket,"removed from group successfully\n");
            // update_tracker(temp,tracker2_fd,clientSocket);
            return;
        }
        
    }
    else if(cmd == "logout"){
        login_status[username] = false;
        cout << username << " logged out successfully" << endl;
        // update_tracker(temp,tracker2_fd,username);
        sendMessage(clientSocket,"logged_out" + username + '\n');
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
    parse(vec,clientSocket,username,temp);
    
    


}

void p_execution(int clientSocket){
    bool loginStatus = false;
    //we need to connect to the second_tracker
    
        cout << clientSocket << endl;
        // cout << "atleast here" << endl;
    while (true) {
        // Use your robust recvMessage
        string msg = recvMessage(clientSocket);
        if (msg.empty()) {
            cout << "other tracker is now disconnected" << endl;
            close(clientSocket);
            return;
        }

        cout << "message is: " << msg << endl;

        // Tokenize and pass into your middleware
        vector<char*> args = tokenise(msg);
        middle_ware(args, clientSocket, msg);

        cout << "reached end1" << endl;

        // If you want to send acknowledgment back:
        string reply = "ACK: " + msg;
        // sendMessage(clientSocket, reply);   // use sendMessage, not raw send
        cout << reply << endl << flush;
        
    }

}

int main(){
    //create a socket
    int port_fd = create_connection(54001); // this port is used for listening for upcoming request;
    // fd_set fr,fw,fe;  
    cout << port_fd << endl;
    const char* ip = "127.0.0.1";
    // tracker2_fd = connect_to_tracker_2(54001,ip);

    while(1){

        sockaddr_in client;  
        socklen_t clientSize = sizeof(client);
        // char host[NI_MAXHOST];
        // char svc[NI_MAXHOST];
        
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