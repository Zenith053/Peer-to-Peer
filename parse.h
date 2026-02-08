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


struct group
{
    int group_id;
    int owner;
    set<int>waitList;
    set<int> members;
};





map <string,string> user_base;
// map <string,string> IsLoggedIn;
// map <int,vector<int>> createGroup;
map <int ,group*> groupInfo;
map<int, group*> waitList;
map<int,bool> login_status;


void parse(vector<char *>&vec,string &s,bool &loginStatus,bool &tracker_comms,int clientSocket){
    cout << "clientsocket is " << clientSocket  << endl;
 
    string cmd(vec[0]);
    cout << cmd << endl;
    if(cmd == "FLAG"){
        //handle sync
        cout << "this message is intended to sync" << endl;
        string csocket(vec[1]); //it is guaranteed that vec[1] will exist;
        clientSocket = stoi(csocket);
        tracker_comms = true;
        // vec[0] = nullptr;
        vec.erase(vec.begin());
        vec.erase(vec.begin());
        parse(vec,s,loginStatus,tracker_comms,clientSocket);
        return;
    }
    else if(cmd == "create_user"){
        if(vec.size() != 4){
            cerr << "invalid args" << endl;
            return;
        }
        cout << "created user " << endl;
        string username(vec[1]);
        string password(vec[2]);
        user_base[username] = password;
        login_status[clientSocket] = false;
        
    }
    else if(cmd == "login"){
        cout << vec.size() << endl; 
        for(int i=0;i<vec.size();i++){
            if(vec[i] != nullptr){
                string ans(vec[i]);
                cout << ans << endl;
            }
        }
        if(vec.size() != 4){
            cerr << "invalid args" << endl;
            return;
        }
        string username(vec[1]);
        string password(vec[2]);
        if(user_base.find(username) == user_base.end()){
            cerr << "invalid username" << endl;
            return;
        }
        else{
            if(user_base[username] == password){
                login_status[clientSocket] = true;
                cout << "Welcome " << username << endl; 
            }
            else{
                cout << "invalid password!!" << endl;
            }
            // loginStatus = true;
        }

    }
    else if(login_status[clientSocket] == false){
        cout << "you first need to login" << endl;
        return;
    }
    else if(cmd == "create_group"){
        //this should return the local sequence number

        //args size must be 3
        if(vec.size() != 3){
            cerr << "invalid arguments" << endl;
        }
        string str_groupId(vec[1]);
        int groupId = stoi(str_groupId);
        vector<int> vec;
        group *newGroup = new group;
        newGroup->group_id = groupId;
        newGroup->owner = clientSocket;
        groupInfo[groupId] = newGroup;
        // newGroup.members = vec;

    }
    else if(cmd == "join_group"){
        //when i join a group i should be able to insert my fd into that group
        if(vec.size() != 3){
            cout << "invalid arguments" << endl;
        }
        string str_groupId(vec[1]);
        int id = stoi(str_groupId);
        if(groupInfo.find(id) == groupInfo.end()){
            cout << "invalid group id" << endl;
        }
        else{
            groupInfo[id]->waitList.insert(clientSocket);
            cout << "added to waitlist successfully" << endl;
        }
    }
    else if(cmd == "leave_group"){

        if(vec.size() != 3){
            cout << "invalid arguments" << endl;
        }
        string str_groupId(vec[1]);
        int id = stoi(str_groupId);
        if(groupInfo.find(id) == groupInfo.end()){
            cout << "invalid group id" << endl;
        }
        else{
            groupInfo[id]->members.erase(clientSocket);
            cout << "removed from group successfully" << endl;
        }
        
    }
    else if(cmd == "list_group"){
        if(vec.size() != 2){
            cout << "invalid arguments" << endl;
            return;
        }
        cout << "group size " << groupInfo.size() << endl; 
        for(auto it:groupInfo){
            cout << it.first << endl;
        }
    }
    else if(cmd == "list_requests"){
        if(vec.size() != 3){
            cout << "invalid arguments" << endl;
            return;
        }
        string str_groupId(vec[1]);
        int id = stoi(str_groupId);
        if(groupInfo[id]->owner == clientSocket){
            for(auto it:groupInfo[id]->waitList){
                cout << it << endl;
            }
        }
        else{
            cerr << "permission denied" << endl;
            return;
        }
        return;
    }
    else if(cmd == "accept_request"){
        if(vec.size() != 4){
            cout << "invalid arguments" << endl;
            return;
        }
        string str_groupId(vec[1]);
        int id = stoi(str_groupId);
        string waiting_user(vec[2]);
        int user_wait = stoi(waiting_user);
        if(groupInfo[id]->owner == clientSocket){
            //the owner will enter this area
            if(groupInfo[id]->waitList.find(user_wait) != groupInfo[id]->waitList.end()){
                //user exist in waitlist
                groupInfo[id]->members.insert(user_wait);
                groupInfo[id]->waitList.erase(user_wait);

            }
            else{
               cout << "No such pending request in accept_request" << endl;
            }
        }
        else{
            cerr << "permission denied" << endl;
            return;
        }
        return;
    }
    else if(cmd == "logout"){
        exit(0);
    }
    else{
        cout << "invalid command" << endl;
    }
    return;
    
}