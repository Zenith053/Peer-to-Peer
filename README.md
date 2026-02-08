# Peer-to-Peer File Sharing System with Tracker Synchronization

A robust P2P file sharing system featuring tracker-based architecture with automatic failover, chunk-based file transfer, and strong consistency through tracker synchronization.

## Table of Contents
- [Features](#features)
- [Architecture](#architecture)
- [Prerequisites](#prerequisites)
- [Installation](#installation)
- [Usage](#usage)
- [System Design](#system-design)
- [Protocol Specification](#protocol-specification)
- [Assumptions and Limitations](#assumptions-and-limitations)

## Features

✅ **Chunk-based File Transfer**: Files split into 512KB pieces with SHA-1 integrity verification  
✅ **Tracker Synchronization**: Primary-secondary tracker model with automatic state replication  
✅ **Group Management**: Create, join, and manage file-sharing groups  
✅ **Parallel Downloads**: Multi-threaded piece downloading for optimal performance  
✅ **Thread-safe Operations**: Safe concurrent file writes using `pwrite`  
✅ **Fault Tolerance**: Secondary tracker maintains synchronized state for failover  
✅ **Hash Verification**: SHA-1 checksums ensure data integrity during transfer  

## Architecture

The system implements a **tracker-based P2P architecture** with the following components:

### Components

1. **Primary Tracker**
   - Handles all client requests (authentication, group operations, file coordination)
   - Broadcasts state changes to secondary tracker
   - Maintains session state and group membership

2. **Secondary Tracker**
   - Passively synchronizes with primary tracker
   - Maintains identical state for failover scenarios
   - Can take over if primary fails

3. **Peers (Clients)**
   - Connect to tracker for coordination
   - Exchange file chunks directly with other peers
   - Manage local file storage and verification

### Synchronization Architecture

```
┌──────────────┐                    ┌──────────────┐
│   Primary    │  FLAG messages     │  Secondary   │
│   Tracker    │───────────────────>│   Tracker    │
└──────┬───────┘                    └──────────────┘
       │
       │ Client requests
       │
   ┌───▼────┐        P2P Transfer        ┌─────────┐
   │ Peer 1 │<──────────────────────────>│ Peer 2  │
   └────────┘                             └─────────┘
```

## Prerequisites

### System Requirements
- **Operating System**: Linux (POSIX-compliant)
- **Compiler**: GCC 11+ with C++17 support
- **Libraries**:
  - OpenSSL (`libssl-dev`) - for SHA-1 hashing
  - POSIX threads
  - Standard C++ libraries

### Installing Dependencies

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install g++ libssl-dev build-essential
```

**Fedora/RHEL:**
```bash
sudo dnf install gcc-c++ openssl-devel
```

## Installation


### 2. Compile Trackers

**Primary Tracker:**
```bash
g++ tcptracker1.cpp -o tracker1
```

**Secondary Tracker:**
```bash
g++ secondaryserver.cpp -o tracker2
```

### 3. Compile Client
```bash
g++ tcpclient2.cpp -lcrypto -DOPENSSL_SUPPRESS_DEPRECATED -o client
```

## Usage

### Starting the System

#### 1. Start Primary Tracker
```bash
./tracker1
```
Default port: `8080` (configurable in source)

#### 2. Start Secondary Tracker
```bash
./tracker2
```
Default port: `8081` (configurable in source)

#### 3. Start Peer Clients
```bash
./client
```

### Client Commands

#### Authentication
```bash
register <username> <password>
login <username> <password>
logout
```

#### Group Management
```bash
create_group <group_id>
join_group <group_id>
leave_group <group_id>
list_groups
list_requests <group_id>
accept_request <group_id> <username>
```

#### File Operations
```bash
upload_file <file_path> <group_id>
download_file <group_id> <file_name> <destination_path>
list_files <group_id>
show_downloads
stop_share <group_id> <file_name>
```

### Example Workflow

```bash
# Peer 1 (Seeder)
login alice password123
create_group dev_team
upload_file /path/to/document.pdf dev_team

# Peer 2 (Leecher)
login bob password456
join_group dev_team
download_file dev_team document.pdf /path/to/save/
```

## System Design

### Data Structures

#### Peer List
```cpp
pair<string,string> p = {username,file_path};
groupInfo[groupId]->files[file_name][pieceNo][username] = {file_path, hash_value};
groupInfo[groupId]->file_size[file_name] = file_size;

// Maps piece numbers to peers that have that piece
```

#### Group Management
```cpp
struct Group {
    string owner;
    set<string> members;
    set<string> pending_requests;
    map<string, FileInfo> shared_files;
};
unordered_map<string, Group> groups;
```

#### File Information
```cpp
struct FileInfo {
    string file_path;
    int file_size;
    int total_pieces;
    vector<string> piece_hashes;  // SHA-1 for each piece
};
```

### Key Algorithms

#### 1. Chunk-based File Transfer
- Files divided into **512KB pieces**
- Each piece hashed using **SHA-1**
- Pieces downloaded in parallel from multiple peers
- Hash verification before writing to disk

```cpp
int piece_size = 512 * 1024;  // 512KB
int total_pieces = (file_size + piece_size - 1) / piece_size;
```

#### 2. Tracker Synchronization
Primary tracker broadcasts updates using `FLAG` prefix:

```
FLAG<username> <operation> <args...>
```

**Example:**
```
FLAGalice login alice password123
FLAGalice create_group dev_team
FLAGalice upload dev_team document.pdf 0 1048576 5000 sha1hash
```

Secondary tracker:
- Listens for `FLAG` messages
- Parses and applies state changes
- Maintains identical data structures

#### 3. Parallel Download Algorithm
```cpp
void download_piece(int file_size, string file_name, int total_pieces,
                    vector<vector<PeerInfo>>& peer_list, int fd) {
    while (true) {
        int my_piece = -1;
        {
            lock_guard<mutex> lock(mtx);
            if (next_piece < total_pieces) {
                my_piece = next_piece++;
            } else {
                return;
            }
        }
        // Download piece from selected peer
        // Verify hash
        // Write using pwrite() for thread safety
    }
}
```

### Thread Safety

- **Mutex-protected piece assignment**: Ensures no duplicate downloads
- **`pwrite()` for file writes**: Thread-safe positional writes
- **Separate read/write locks**: Minimizes contention

## Protocol Specification

### Client-Tracker Protocol

#### Upload Request
```
upload <group_id> <file_path> <piece_no> <file_size> <listen_port> <hash>
```

#### Download Request
```
download <group_id> <file_name>
```

**Response Format:**
```
<total_pieces>
<file_size>
<peer_count_for_piece_0> <ip1>:<port1>:<filepath1> <ip2>:<port2>:<filepath2> ...
<peer_count_for_piece_1> ...
```

#### Group Operations
```
create_group <group_id>
join_group <group_id>
leave_group <group_id>
list_groups
accept_request <group_id> <username>
```

### Peer-to-Peer Protocol

#### Piece Request
```
get_piece <piece_number> <file_name>
```

**Response:** Raw binary data of the requested piece

### Tracker-Tracker Synchronization

```
FLAG<username> <original_client_message>
```

The secondary tracker strips the `FLAG<username>` prefix and processes the message as if it came from the client.

## Assumptions and Limitations

### Assumptions
- Single active primary tracker at any time
- Reliable TCP connections between trackers
- Secondary tracker does not serve clients directly (only synchronizes state)
- SHA-1 sufficient for integrity (not for cryptographic security)

### Limitations
- **No encryption**: Data transmitted in plaintext
- **No authentication between trackers**: Assumes trusted environment
- **Single point of failure**: No automatic primary failover implemented
- **Memory-based state**: Tracker state not persisted to disk
- **No NAT traversal**: Requires direct connectivity between peers
- **SHA-1 deprecation**: Consider upgrading to SHA-256 for production

### Known Issues
- Fast local downloads may cause thread starvation (one thread grabbing all pieces)
- No rate limiting on tracker synchronization messages
- Missing piece retry mechanism if peer disconnects mid-transfer

## Configuration

### Port Configuration
Edit source files to change default ports:

**Primary Tracker** (`tcptracker1.cpp`):
```cpp
#define TRACKER_PORT 54000
```

**Secondary Tracker** (`secondaryserver.cpp`):
```cpp
#define TRACKER_PORT 54001
```

**Client** (`tcpclient2.cpp`):
```cpp
#define DEFAULT_TRACKER_IP "127.0.0.1"
#define DEFAULT_TRACKER_PORT [Randomly chosen]
```

### Chunk Size
Modify in client source:
```cpp
int piece_size = 512 * 1024;  // 512KB default
```

## Performance Tuning

### Thread Count
Adjust concurrent download threads:
```cpp
int num_threads = 4;  // Optimal: 2-8 depending on network
```

### Buffer Sizes
Increase for high-bandwidth networks:
```cpp
vector<char> buffer(piece_size);  // Larger = fewer syscalls
```



## Contributing


## Authors

[Peeyush Prashant]

## Acknowledgments

Built using POSIX sockets, OpenSSL, and standard C++ threading libraries.