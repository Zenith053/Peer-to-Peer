#include <stdio.h>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fstream>
#include <string.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include <sstream>
#include <iterator>
// #include <set>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <netdb.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <openssl/sha.h>
#include <queue>
#include <condition_variable>
#include <functional>
using namespace std;

struct PeerInfo {
    std::string username;
    std::string ip;
    int port;
    std::string file_path;
    std::string hash_value;
};

int LISTEN_PORT = -1; // default port for peer to peer connection
string username = "INVALID";
int sendMessage(int sock, const string &msg) {
    string temp;
    
    temp = username + " " + msg;
    
    uint32_t len = temp.size();
    
    uint32_t netlen = htonl(len);  // convert to network byte order (big endian)
    int bytes_sent = 0;

    // Send length first
    bytes_sent += send(sock, &netlen, sizeof(netlen), 0);

    // Send message data
    bytes_sent += send(sock, temp.c_str(), temp.size(), 0);

    if (bytes_sent < 0) {
        cerr << "Error sending message" << endl;
    }
    return bytes_sent;
}
int uploadMessage(int sock, const string &temp) {
    // string temp;
    
    // temp = username + " " + msg;
    
    uint32_t len = temp.size();
    
    uint32_t netlen = htonl(len);  // convert to network byte order (big endian)
    int bytes_sent = 0;

    // Send length first
    bytes_sent += send(sock, &netlen, sizeof(netlen), 0);

    // Send message data
    bytes_sent += send(sock, temp.c_str(), temp.size(), 0);

    if (bytes_sent < 0) {
        cerr << "Error sending message" << endl;
    }
    return bytes_sent;
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
class MessageQueue {
    queue<string> q;
    mutex mtx;
    condition_variable cv;

public:
    void push(const string &msg) {
        {
            std::lock_guard<std::mutex> lk(mtx);
            q.push(msg);
        }
        cv.notify_one();
    }

    std::string pop() {
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait(lk, [this]{ return !q.empty(); });
        std::string msg = q.front();
        q.pop();
        return msg;
    }

    bool empty() {
        std::lock_guard<std::mutex> lk(mtx);
        return q.empty();
    }
};
atomic<bool> stop_all(false);
mutex sock_mtx;        // to safely replace the global socket
int global_sock = -1;

// Helper: connect to ip:port, return fd or -1
int try_connect(const string &ip, uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        close(fd);
        return -1;
    }
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}


void handle_seed_hash(vector<string> tokens) {
    string group_id = tokens[1] ;
    string file_path = tokens[2];
    // string file_name = extract_base_filename(file_path);

    int fd = open(file_path.c_str(), O_RDONLY );
    if(fd < 0){
        cout << "file not found" << endl;
    }
    struct stat file_stat;
    if(fstat(fd, &file_stat) < 0){
        cout << "could not get file size" << endl;
        close(fd);
    }
    off_t file_size = file_stat.st_size;
    cout << "file size: " << file_size << endl;
    off_t read_bytes = 0;
    //now we need to read the file and also send the sha256 hash of all the pieces
    //we will read the file in chunks of 512KB
    const size_t CHUNK_SIZE = 512 * 1024; // 512KB
    while(read_bytes < file_size){

        size_t total_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
        vector<string> piece_hashes;
        char buffer[CHUNK_SIZE];
        for(size_t i = 0; i < total_chunks; i++){
            ssize_t bytes_read = read(fd, buffer, CHUNK_SIZE);
            read_bytes += bytes_read;
            if(bytes_read < 0){
                cout << "error reading file" << endl;
                return;
            }
            

            SHA_CTX ctx;                               // SHA-1 context
            SHA1_Init(&ctx);                           // initialize
            SHA1_Update(&ctx, buffer, bytes_read);     // feed data chunk

            unsigned char hash[SHA_DIGEST_LENGTH];     // SHA_DIGEST_LENGTH = 20
            SHA1_Final(hash, &ctx);                    // finalize digest

            // Printing hash as hex (for debugging only)
            for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
                printf("%02x", hash[i]);
            }
            string message_type = "upload ";
            //we also need to send the file name 
            message_type += group_id + " " ;
            message_type += file_path + " " + to_string(i) + " ";
            message_type += to_string(file_size) + " ";
            message_type += to_string(LISTEN_PORT) + " ";
            // message_type += <file_name>;
            char hexbuf[SHA_DIGEST_LENGTH * 2 + 1];
            for (int j = 0; j < SHA_DIGEST_LENGTH; j++) {
                sprintf(&hexbuf[j*2], "%02x", hash[j]);
            }
            hexbuf[SHA_DIGEST_LENGTH * 2] = '\0'; // null terminate

            sendMessage(global_sock, message_type + string(hexbuf)); 
            printf("\n");
            
            //compute sha256 hash of the chunk
            // string piece_hash = compute_sha256(buffer, bytes_read);
            // piece_hashes.push_back(piece_hash);
        }
        
    }
    close(fd);
    return;
    //read file content

    // we read file as binary
    // close(fd);
}
mutex mtx;
int next_piece = 0;
void download_piece(int file_size,string file_name,int total_pieces,vector<vector<PeerInfo>>& peer_list,int fd,string groupId,string file_path) {
    cout << "total pieces : " << total_pieces << endl ;
    // cout << "reached download piece" << endl ;
    // int fd = open("downloaded_file", O_WRONLY | O_CREAT, 0666); //it's already created
    if (fd < 0) {
        cerr << "Error opening file for writing" << endl;
        return;
    }

    int piece_size = 512 * 1024; // 512KB
    
    while (true) {
        int my_piece = -1;
        
        {
            lock_guard<mutex> lock(mtx);
            if (next_piece < total_pieces) {
                my_piece = next_piece++;
                // cout << "my_piece " << my_piece << endl; 
                // cout << "Thread " << this_thread::get_id() << " assigned piece: " << my_piece << endl;
            } else {
                 cout << "Thread " << this_thread::get_id() 
             << " found no more pieces" << endl << flush;
                // close(fd);
                return; // no more pieces
            }
        }

        cout << "downloading piece: " << my_piece << endl;
        // Find a peer that has this piece
        string peer_ip;
        int peer_port = -1;
        string peer_file_path;
        //randomly select a peer for a piece
        int idx = 0;
        // Randomly select a peer for this piece (my_piece)
        if (my_piece >= 0 && my_piece < (int)peer_list.size() && !peer_list[my_piece].empty()) {
            idx = rand() % peer_list[my_piece].size();
            const auto& peer = peer_list[my_piece][idx];
            peer_ip = peer.ip;
            peer_port = peer.port;
            peer_file_path = peer.file_path;
        }
        if (peer_port == -1) {
            cerr << "No peer found for piece " << my_piece << endl;
            continue; // try next piece
        }   
        int peer_sock = try_connect(peer_ip, peer_port);
        cout << "Thread " << this_thread::get_id() 
        << " connected to peer " << peer_ip << ":" << peer_port 
        << " requesting piece " << my_piece << endl;
        if (peer_sock < 0) {
            cerr << "Failed to connect to peer " << peer_ip << ":" << peer_port << endl;
            continue; // try next piece

            // -------------------------------------------------------


            // check again for different peer

            // -------------------------------------------------------
        }

        int item_sent = 0;
        // Request the piece
        string request = "get_piece " + to_string(my_piece) + " " + file_name;

        sendMessage(peer_sock, request); // this is received
        // Receive the piece
        // Calculate where to write in the file

        // Calculate correct offset
        off_t write_offset = (off_t)my_piece * piece_size;
        size_t real_piece_size = (my_piece == total_pieces - 1)
                                   ? (file_size - (piece_size * (total_pieces - 1)))
                                   : piece_size;

        size_t total_received = 0;

       
       // Allocate a string buffer for the piece
        string buffer;
        buffer.reserve(real_piece_size);
        while (total_received < real_piece_size) {
            string chunk = recvMessage(peer_sock);
            if (chunk.empty()) {
                cerr << "Connection closed while downloading piece " << my_piece << endl;
                close(peer_sock);
                return;
            }

            // Append chunk to buffer (trim if last chunk exceeds piece size)
            size_t bytes_to_append = chunk.size();
            // if (total_received + bytes_to_append > real_piece_size) {
            //     bytes_to_append = real_piece_size - total_received;
            // }
            buffer.append(chunk.data(), bytes_to_append);

            total_received += bytes_to_append;
        }
        // if(my_piece == total_pieces-1)cout << buffer << endl;
        cout << "total received/total expected = " << total_received << "/" << real_piece_size << endl;
        // Compute SHA-1 hash
        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA_CTX ctx;
        SHA1_Init(&ctx);
        SHA1_Update(&ctx, buffer.data(), buffer.size());
        SHA1_Final(hash, &ctx);
        char hexbuf[SHA_DIGEST_LENGTH * 2 + 1];
        for (int j = 0; j < SHA_DIGEST_LENGTH; j++) {
            sprintf(&hexbuf[j*2], "%02x", hash[j]);
        }
        hexbuf[SHA_DIGEST_LENGTH * 2] = '\0'; // null terminate
        string hash_str = string(hexbuf);
        // Convert hash to hex string

        // Verify hash
        if (hash_str == peer_list[my_piece][idx].hash_value) {
            cout << "HASH VALUE MATCHES WITH THE ACTUAL HASH FOR :" << my_piece << endl;
            // Thread-safe write to file
            ssize_t bytes_written = pwrite(fd, buffer.data(), buffer.size(), write_offset);
            if (bytes_written != (ssize_t)buffer.size()) {
                cerr << "Error writing piece " << my_piece << endl;
                close(peer_sock);
                return;
            }

            // Notify tracker/server about new piece
            string message_type = "upload " + groupId + " " + peer_file_path + " "
                                + to_string(my_piece) + " " + to_string(file_size) + " "
                                + to_string(LISTEN_PORT) + " " + hash_str;

            {
                lock_guard<mutex> lk(sock_mtx);
                sendMessage(global_sock, message_type);
            }

        } else {
            cerr << "Hash mismatch for piece " << my_piece << ", re-requesting..." << endl;
            return;
            // discard buffer and retry later
        }

        close(peer_sock);
    }
    close(fd);

}

void handle_download_file(vector<string> tokens) {
    //first we wait for header from server
    // header will contain the file size
    // then we will create a file of the same size
    string server_ip = "127.0.0.1";
    int download_port = 54000;
    int download_sock = try_connect(server_ip, download_port);
    if (download_sock < 0) {
        cerr << "Failed to connect to server for downloading" << endl;
        return;
    } 



    string group_id = tokens[1] ;
    string file_name = tokens[2];
    string destination_path = tokens[3];

    string message = "download " + group_id + " " + file_name;
    //the file will contain chunk no, username, ip address, port, source file path
    sendMessage(download_sock, message);

    string header = recvMessage(download_sock);

    cout << "header is " << header << endl;
    // return;
    istringstream iss(header);
    string file_info;
    int file_size;
    iss >> file_info >> file_size;
    cout << "file info " << file_info << " " << file_size << endl;
    int total_pieces = (file_size + 512*1024 - 1) / (512*1024);
    cout << total_pieces << endl ;
    if (file_size <= 0) {
        std::cerr << "Error: File size must be positive." << std::endl;
        return;
    }

    //  cout << "problem in ath " << destination_path << endl ;
    // Open the file
    int fd = open(destination_path.c_str(), O_CREAT | O_WRONLY |O_TRUNC , 0666);
    cout << "fd of file is " << fd << endl ;
    
    // // // Check if file opened successfully
    
    if (fd == -1) {
        perror("Error opening destination file");
        return;
    }
    if (ftruncate(fd, file_size) < 0) {
        perror("ftruncate");
        close(fd);
        return;
    }
    

    vector<tuple<int, string, string, string, int, string>> pieces;
    // piece_no, username, ip, file_path, port, hash_value

    // Receive all lines until END_OF_FILE
    vector<string> all_lines;
    // while (true) {
    cout << "reached before recv message" << endl;
        string response = recvMessage(download_sock);
        // cout << "response is " << response << endl;
        if (response.empty()) {
            cerr << "Connection closed by server while receiving piece info." << endl;
            return;
        }
        istringstream read_line(response);
        string line;
        while (getline(read_line, line)) {
            if (line == "END_OF_FILE") {
                cout << "reached end of file" << endl;
                break;
            }
            all_lines.push_back(line);
        }
        // if (response.find("END_OF_FILE") != string::npos) ;
    // }
    cout << "reached before thread creation" << endl;
    for (const auto& line : all_lines) {
        istringstream ls(line); 
        int piece_no;
        string username, ip, file_path;
        int port;
        string hash_value;
        ls >> piece_no >> username >> ip >> port >> file_path >> hash_value;
        cout << piece_no << " " << username << " " << ip << " " << port << " " << file_path << " " << hash_value << endl;
        if (ls.fail()) continue; // skip malformed lines
        pieces.emplace_back(piece_no, username, ip, file_path, port, hash_value);
    }
    cout << endl;
    for(auto it:pieces){
        cout << get<0>(it) << " " << get<1>(it) << " " << get<2>(it) << " " << get<3>(it) << " " << get<4>(it) << " " << get<5>(it) << endl;
    }
    // Properly size peer_list to avoid out-of-bounds
    vector<vector<PeerInfo>> peer_list(total_pieces);
    for (const auto& p : pieces) {
        PeerInfo peer_info;
        int piece_no = get<0>(p);
        peer_info.username = get<1>(p);
        peer_info.ip = get<2>(p);
        peer_info.port = get<4>(p);
        peer_info.file_path = get<3>(p);
        peer_info.hash_value = get<5>(p);

        if (piece_no >= 0 && piece_no < total_pieces)
        {
            peer_list[piece_no].push_back(peer_info);

        }
    }

    int THREADS = 4;
    next_piece = 0;
    int xd = open(destination_path.c_str(), O_WRONLY|O_CREAT , 0644);
    if(xd < 0){
        cout << "file does not exist" << endl;
        return;         
    }
    vector<thread> workers;
    for (int i = 0; i < THREADS; i++) {
        workers.emplace_back(download_piece,file_size,file_name,total_pieces ,ref(peer_list), xd,group_id,destination_path);
    }
    for (auto &t : workers) t.join();
    
    const size_t CHUNK_SIZE = 512 * 1024; // 512KB
       
        
    
}
void handle_peer_connection(int client_sock) {
    cout << "reached seperate thread for upload  " << endl;
    // Handle peer connections, 0644)
    // a peer will only request for a piece using prefix "download_piece "
    string request = recvMessage(client_sock); 
    //we will tokenise the request
    istringstream iss(request);
    vector<string> tokens{
        istream_iterator<string>{iss},
        istream_iterator<string>{}
    };
    cout << "request is " << request << endl ;
    cout << "tokens size is " << tokens.size() << endl ;
    if (tokens.size() != 4 || tokens[1] != "get_piece") {
        cerr << "Invalid peer request: " << request << endl;
        close(client_sock);
        return;
    }

    // Extract piece information from the request
    int piece_no = stoi(tokens[2]);
    string file_path = tokens[3];

    // Handle the download_piece request
    // ...
    int fd = open(file_path.c_str(), O_RDONLY, 0666);
    if (fd < 0) {
        cerr << "Error opening file for reading: " << file_path << endl;
        close(client_sock);
        return;
    }
    const size_t CHUNK_SIZE = 512 * 1024; // 512KB
    off_t offset = (off_t)piece_no * CHUNK_SIZE;
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        cerr << "Error seeking to piece position in file" << endl;
        close(fd);
        close(client_sock);
        return;
    }
    size_t total_sent = 0;
    // const size_t CHUNK_SIZE = 512 * 1024; // 512KB

    while (total_sent < CHUNK_SIZE) {
        size_t to_read = CHUNK_SIZE - total_sent;
        string data(to_read, '\0'); // allocate buffer

        ssize_t bytes_read = read(fd, &data[0], to_read);
        if (bytes_read < 0) {
            cerr << "Error reading from file" << endl;
            close(fd);
            close(client_sock);
            return;
        }

        if (bytes_read == 0) {
            // EOF reached, trim buffer to actual read size
            data.resize(total_sent); 
            break;
        }
        data.resize(bytes_read);
        // Send the data chunk
        uploadMessage(client_sock, data);
        total_sent += bytes_read;
    }
    cout << "TOTAL SENT = "<<  total_sent << endl;
    close(fd);
    close(client_sock);
}


// void initialize_random_seed() {
//     // Uses the current system time as the seed
//     srand(time(NULL)); 
// }

void create_connection(string user_name){
    // initialize_random_seed(); 
    // string username;

    int peer_listen_port = 55000 + std::hash<std::string>()(user_name) % 1000;
    LISTEN_PORT = peer_listen_port ;
    cout << "choosen port is " << peer_listen_port << endl ;
    int peer_listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (peer_listen_sock < 0) {
        cerr << "Failed to create peer listening socket\n";
        return ;
    }
    int opt = 1;
    if (setsockopt(peer_listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        cerr << "Failed to set SO_REUSEADDR\n";
        return ;
    }
    sockaddr_in peer_addr{};
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_addr.s_addr = INADDR_ANY;
    peer_addr.sin_port = htons(peer_listen_port);
    
    thread peer_listener_thread([peer_listen_sock, peer_addr]() {
        if (bind(peer_listen_sock, (struct sockaddr*)&peer_addr, sizeof(peer_addr)) < 0) {
            cerr << "Failed to bind peer listening socket\n";
            return;
        }
        if (listen(peer_listen_sock, 5) < 0) {
            cerr << "Failed to listen on peer listening socket\n";
            return;
        }
        cout << "Peer listening on port " << ntohs(peer_addr.sin_port) << endl;

        while (true) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_sock = accept(peer_listen_sock, (struct sockaddr*)&client_addr, &client_len);
            // if(username == "INVALID")return;
            if (client_sock < 0) {
                cerr << "Failed to accept peer connection\n";
                return;
                continue;
            }
            cout << "Accepted peer connection from " << inet_ntoa(client_addr.sin_addr) << endl;

            // Handle peer connection in a separate thread
            thread peer_thread(handle_peer_connection, client_sock);
            peer_thread.detach();
        }
    });
    peer_listener_thread.detach();

}
MessageQueue global_msg_queue;
// Thread: keep receiving until the connection dies 
void handle_thread() {
    while (!stop_all) {
        int sock;
        { lock_guard<mutex> lk(sock_mtx); sock = global_sock; }

        string msg = recvMessage(sock);
        if (msg.empty()) {        // recvMessage should return "" on EOF/error
            cerr << "\nPrimary server lost. Attempting failover...\n";

            // Try backup
            int backup_fd = try_connect("127.0.0.1", 54001);
            if (backup_fd < 0) {
                cerr << "Backup server also down. Exiting.\n";
                stop_all = true;
                break;
            }

            else {
                lock_guard<mutex> lk(sock_mtx);
                close(global_sock);
                global_sock = backup_fd;
            }
            cerr << "Connected to backup server (54001)\n";
            continue;  // now loop continues reading from new socket
        }

        // Normal message handling
        istringstream iss(msg);
        vector<string> tokens{
            istream_iterator<string>{iss},
            istream_iterator<string>{}
        };

        if (!tokens.empty()) {
            if (tokens[0] == "successful") {
                if (tokens.size() >= 2) {
                    username = tokens[1];
                    cout << "logged in as " << username << endl;
                    create_connection(username);
                }
            } else if (tokens[0] == "logged_out") {
                username = "INVALID";
                cout << "logged out of server" << endl;
                close(LISTEN_PORT);
            } else {
                cout << msg << endl;
            }
        }
        // global_msg_queue.push(msg);
    }
}








int main() {
    string ip = "127.0.0.1";

    // Connect to primary
    int sock_fd = try_connect(ip, 54000);
    if (sock_fd < 0) {
        cerr << "Primary server unavailable, trying backup...\n";
        sock_fd = try_connect(ip, 54001);
        if (sock_fd < 0) {
            cerr << "Both servers down. Exiting.\n";
            return 1;
        }
        cerr << "Connected to backup server directly.\n";
    } else {
        cerr << "Connected to primary server.\n";
    }

    {
        lock_guard<mutex> lk(sock_mtx);
        global_sock = sock_fd;
    }
    //call the thread to handle peer connections
    
    // Initial hello
    sendMessage(global_sock, "hello this is a message from client\n");
    
    // Input loop
    thread reader(handle_thread);
    while (!stop_all) {
        // cout << "> ";
        string line;
        if (!getline(cin, line)) break;
        //we need to read the line as stream of token
        stringstream ss(line);
        string token;
        vector<string> tokens;
        while (ss >> token) {
            tokens.push_back(token);
        }
        if(tokens.size()>0 && tokens[0] == "upload_file"){
            if(tokens.size() != 3){
                cout << "invalid arguments" << endl;
                continue;
            }
            else{
                
                thread the(handle_seed_hash,tokens);
                the.detach();
            }
        }
        else if(tokens.size()>0 && string(tokens[0]) == "download_file"){
            if(tokens.size() != 4){
                cout << "invalid arguments" << endl;
                continue;
            }
            else{
                thread th(handle_download_file,tokens);
                th.join();
                cout << "download finished" << endl ;
            }
            
        }
        lock_guard<mutex> lk(sock_mtx);
        if (sendMessage(global_sock, line) < 0) {
            cerr << "Send failed. Will rely on reader to trigger failover.\n";
        }
    }

    stop_all = true;
    reader.join();
    lock_guard<mutex> lk(sock_mtx);
    if (global_sock != -1) close(global_sock);
    return 0;
}
