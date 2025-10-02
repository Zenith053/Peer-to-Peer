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

using namespace std;

struct PeerInfo {
    std::string username;
    std::string ip;
    int port;
    std::string file_path;
};

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
                }
            } else if (tokens[0] == "logged_out") {
                username = "INVALID";
                cout << "logged out of server" << endl;
            } else {
                cout << "server says: " << msg << endl;
            }
        }
    }
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
void download_piece(int file_size,string file_name,int total_pieces,vector<vector<PeerInfo>>& peer_list,int sock_fd) {
    cout << "reached download piece" << endl ;
    int fd = open("downloaded_file", O_WRONLY | O_CREAT, 0666); //it's already created
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
            } else {
                close(fd);
                return; // no more pieces
            }
        }

        // Find a peer that has this piece
        string peer_ip;
        int peer_port = -1;
        string peer_file_path;
        //randomly select a peer for a piece
        // Randomly select a peer for this piece (my_piece)
        if (my_piece >= 0 && my_piece < (int)peer_list.size() && !peer_list[my_piece].empty()) {
            int idx = rand() % peer_list[my_piece].size();
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
        if (peer_sock < 0) {
            cerr << "Failed to connect to peer " << peer_ip << ":" << peer_port << endl;
            continue; // try next piece

            // -------------------------------------------------------


            // check again for different peer

            // -------------------------------------------------------
        }
        // Request the piece
        string request = "get_piece " + to_string(my_piece) + " " + file_name;

        sendMessage(peer_sock, request);
        // Receive the piece
        // Calculate where to write in the file
        off_t write_offset = (off_t)my_piece * piece_size;
        size_t total_received = 0;

        while (total_received < (size_t)piece_size) {
            size_t remaining = piece_size - total_received;
            vector<char> buffer(remaining);

            ssize_t bytes_current_read = recv(sock_fd, buffer.data(), remaining, 0);
            if (bytes_current_read <= 0) {
                cerr << "Error receiving piece data or connection closed" << endl;
                close(fd);
                return;
            }

            if (lseek(fd, write_offset + total_received, SEEK_SET) == (off_t)-1) {
                cerr << "Error seeking to position in file" << endl;
                close(fd);
                return;
            }

            ssize_t bytes_written = write(fd, buffer.data(), bytes_current_read);
            if (bytes_written != bytes_current_read) {
                cerr << "Error writing data to file" << endl;
                close(fd);
                return;
            }

            total_received += bytes_current_read;
        }
    }

}

void handle_download_file(vector<string> tokens) {
    //first we wait for header from server
    // header will contain the file size
    // then we will create a file of the same size
    string group_id = tokens[1] ;
    string file_name = tokens[2];
    string destination_path = tokens[3];

    string message = "download " + group_id + " " + file_name + " " ;
    //the file will contain chunk no, username, ip address, port, source file path
    sendMessage(global_sock, message);

    string header = recvMessage(global_sock);
    cout << "header is " << header << endl;
    istringstream iss(header);
    string file_info;
    int file_size;
    iss >> file_info >> file_size;
    cout << "file info " << file_info << " " << file_size << endl;
    int total_pieces = (file_size + 512*1024 - 1) / (512*1024);
    if (file_size <= 0) {
        std::cerr << "Error: File size must be positive." << std::endl;
        return;
    }

     cout << "problem in ath " << destination_path << endl ;
    // Open the file
    int fd = open(destination_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0666);
    
    
    // Check if file opened successfully
    
    if (fd == -1) {
        perror("Error opening destination file");
        return;
    }
    
    // Move the file pointer 
    // We seek to the position (file_size - 1), casted to off_t
    off_t seek_offset = (off_t)file_size - 1;
    
    if (lseek(fd, seek_offset, SEEK_SET) == -1) {
        perror("Error seeking file position");
        close(fd);
        return;
    }
    
    char zero_byte = '\0'; 
    if (write(fd, &zero_byte, 1) != 1) {
        perror("Error writing final byte for allocation");
        close(fd);
        return;
    }
    //now we allocated the space for the file
    close(fd);
    cout << "problem in ath 1" << endl << flush;
    
    vector<tuple<int, string, string, string, int>> pieces; 
    // piece_no, username, ip, file_path, port

    // Receive all lines until END_OF_FILE
    vector<string> all_lines;
    while (true) {
        string response = recvMessage(global_sock);
        cout << "response is " << response << endl;
        if (response.empty()) {
            cerr << "Connection closed by server while receiving piece info." << endl;
            return;
        }
        istringstream read_line(response);
        string line;
        while (getline(read_line, line)) {
            if (line == "END_OF_FILE") break;
            all_lines.push_back(line);
        }
        if (response.find("END_OF_FILE") != string::npos) break;
    }

    for (const auto& line : all_lines) {
        istringstream ls(line); 
        int piece_no;
        string username, ip, file_path;
        int port;
        ls >> piece_no >> username >> ip >> port >> file_path;
        cout << piece_no << " " << username << " " << ip << " " << port << " " << file_path << endl;
        if (ls.fail()) continue; // skip malformed lines
        pieces.emplace_back(piece_no, username, ip, file_path, port);
    }
    for(auto it:pieces){
        cout << get<0>(it) << " " << get<1>(it) << " " << get<2>(it) << " " << get<3>(it) << " " << get<4>(it) << endl;
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

        if (piece_no >= 0 && piece_no < total_pieces)
            peer_list[piece_no].push_back(peer_info);
    }

        int THREADS = 4;
        fd = open(destination_path.c_str(), O_WRONLY | O_TRUNC, 0644);
        vector<thread> workers;
        for (int i = 0; i < THREADS; i++) {
            workers.emplace_back(download_piece,file_size,file_name,total_pieces ,ref(peer_list), global_sock);
        }
        for (auto &t : workers) t.join();
        
        if(fd < 0){
            cout << "file does not exist" << endl;
            return;         
        }
        const size_t CHUNK_SIZE = 512 * 1024; // 512KB
       
        
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

    thread reader(handle_thread);

    // Initial hello
    sendMessage(global_sock, "hello this is a message from client\n");

    // Input loop
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
                th.detach();

            }
            break;
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
