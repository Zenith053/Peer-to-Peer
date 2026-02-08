#include <bits/stdc++.h>
using namespace std;
#define SAMELINE '\r'
#define ff flush;
// constexpr char newline = '\r';

void user_registration(map<string,string> &userDetails){
while(1){
    cout << "register or login" << endl;
        string entry;
        cin >> entry;
        if(entry == "register"){
            cout << SAMELINE << "enter username " << flush;
            string username;
            getline(cin,username,'\n');
            cout << "enter password " << ff;
            string password;
             getline(cin,password);
            userDetails.insert({username,password});
            cout << SAMELINE << "confirm password " <<ff;
            string c_password;
             getline(cin,c_password);
            if(userDetails[username] == c_password){
                cout  <<"User registerd successfully " << ff;
                break;
            }
            else{
                cout << SAMELINE << "invalid password " << flush;
            }
        }
        else if(entry == "login"){
            break;
        }
        else{
            cout << SAMELINE << "invalid command" <<ff ;
        }
    }
    return;
}
void user_login(map<string,string> &userDetails){
    while(1){
        
        cout << SAMELINE << "Enter Username " <<ff ;
        string username;
        getline(cin,username);
        cout << SAMELINE << "Enter Password " << ff;
        string password;
         getline(cin,password);
        if(userDetails[username] == password){
            cout << SAMELINE << "authentication successful " << ff;
            break;
        }
        else {
            cout << SAMELINE <<  "invalid username or password " << ff;
        }
    }
    return;
}

int main(){
    //in this file we will create one peer node
    // what should it do it should be able to authenticate user and register 
    map<string,string> userDetails;
    while(1){
        cout << endl;
        user_registration(userDetails);
        //now user registration must have been done
        //now user login
        cout << endl;
        user_login(userDetails);
        cout << endl;
        cout << "sucessfull" << ff;
        cout << endl;
    }

}