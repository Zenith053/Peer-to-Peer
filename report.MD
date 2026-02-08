# Technical Report: Peer-to-Peer File Sharing System with Tracker Synchronization

**Project Title:** Distributed P2P File Sharing with Multi-Tracker Synchronization  
**Implementation Language:** C++17  
**Date:** October 2025  

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Implementation Approach](#2-implementation-approach)
3. [Synchronization Algorithm Design](#3-synchronization-algorithm-design)
4. [Piece Selection Strategy](#4-piece-selection-strategy)
5. [Protocol Design Rationale](#5-protocol-design-rationale)
6. [Challenges and Solutions](#6-challenges-and-solutions)
7. [Performance Analysis](#7-performance-analysis)
8. [Future Improvements](#8-future-improvements)
9. [References](#9-references)

---

## 1. Executive Summary

This report analyzes a tracker-based peer-to-peer file sharing system implementing chunk-based file transfer with strong consistency through tracker synchronization. The system addresses distributed systems challenges including concurrent access, data integrity, and fault tolerance.

**Key Achievements:**
- Implemented parallel 512KB chunk-based file transfer with SHA-1 verification
- Designed FLAG-prefix message-passing synchronization protocol for tracker consistency
- Achieved thread-safe concurrent downloads using mutex coordination and `pwrite()`
- Created fault-tolerant architecture with primary-secondary tracker model supporting manual failover

**System Components:**
- **Primary Tracker** (`tcptracker1.cpp`): Port 54000, handles all client operations
- **Secondary Tracker** (`secondaryserver.cpp`): Port 54001, passively synchronizes state
- **Client** (`tcpclient2.cpp`): Manages uploads, parallel downloads, and peer connections

---

## 2. Implementation Approach

### 2.1 System Architecture

The implementation follows a **centralized-decentralized hybrid architecture**:

```
┌─────────────────────────────────────────────────────────┐
│                   Tracker Layer                         │
│  ┌──────────────┐         Sync         ┌─────────────┐ │
│  │   Primary    │────────────────────>│  Secondary  │ │
│  │ Tracker:54000│   FLAG<user> msg    │Tracker:54001│ │
│  └──────┬───────┘                      └─────────────┘ │
└─────────┼──────────────────────────────────────────────┘
          │ Metadata & Coordination
          │
┌─────────▼──────────────────────────────────────────────┐
│                    Peer Layer                          │
│  ┌────────┐  Direct P2P   ┌────────┐                  │
│  │ Peer A │◄──────────────►│ Peer B │                  │
│  │ Port:  │   512KB chunks │ Port:  │                  │
│  │ 55xxx  │   SHA-1 verify │ 55yyy  │                  │
│  └────────┘                └────────┘                  │
└────────────────────────────────────────────────────────┘
```

**Design Rationale:**
- **Centralized metadata management**: Simplifies peer discovery and group coordination
- **Decentralized file transfer**: Prevents tracker bottleneck, enables scalability
- **Dual-tracker design**: Provides fault tolerance without complex consensus protocols

### 2.2 Core Data Structures

#### 2.2.1 Tracker State (tcptracker1.cpp)

```cpp
// User authentication and session management
map<string, string> user_base;                    // username -> password
map<string, bool> login_status;                   // username -> logged_in
map<string, pair<string, int>> seed_info;         // username -> (ip, port)

// Group structure
struct group {
    string group_id;
    string owner;
    set<string> waitList;                         // pending join requests
    set<string> members;                          // approved members
    
    // File storage: filename -> piece_no -> username -> (filepath, hash)
    map<string, map<int, map<string, pair<string, string>>>> files;
    map<string, int> file_size;                   // filename -> total_bytes
};
map<string, group*> groupInfo;                    // group_id -> group
```

**Key Design Decisions:**
- **Nested maps for file tracking**: Enables O(1) lookup for "which peers have piece N of file F"
- **Set for members/waitlist**: O(log N) membership checks, automatic deduplication
- **Separate hash storage**: Each peer stores hash for verification of their pieces

#### 2.2.2 Client State (tcpclient2.cpp)

```cpp
// Global state
string username = "INVALID";                      // Current logged-in user
int LISTEN_PORT = -1;                             // Dynamic peer listening port
int global_sock = -1;                             // Tracker connection
mutex sock_mtx;                                   // Protects socket operations
atomic<bool> stop_all(false);                     // Shutdown signal

// Download coordination
mutex mtx;                                        // Protects next_piece
int next_piece = 0;                               // Next piece to download

// Peer information for downloads
struct PeerInfo {
    string username;
    string ip;
    int port;
    string file_path;
    string hash_value;                            // SHA-1 for verification
};
```

**Dynamic Port Assignment:**
```cpp
int peer_listen_port = 55000 + hash<string>()(username) % 1000;
// Range: 55000-55999, deterministic per username
```

This ensures:
- Multiple clients can run on same machine
- Port is consistent across reconnections
- Avoids port conflicts with tracker

### 2.3 Message Framing Protocol

**Problem**: TCP is stream-oriented, not message-oriented. Need reliable message boundaries.

**Solution**: Length-prefixed framing

```cpp
void sendMessage(int sock, const string &msg) {
    // 1. Send 4-byte length prefix (network byte order)
    uint32_t len = msg.size();
    uint32_t netlen = htonl(len);
    send(sock, &netlen, sizeof(netlen), 0);
    
    // 2. Send message data
    send(sock, msg.c_str(), len, 0);
}

string recvMessage(int sock) {
    // 1. Receive length (blocking until 4 bytes arrive)
    uint32_t netlen;
    recv(sock, &netlen, sizeof(netlen), MSG_WAITALL);
    uint32_t len = ntohl(netlen);
    
    // 2. Receive exact message bytes
    string msg(len, '\0');
    recv(sock, &msg[0], len, MSG_WAITALL);
    return msg;
}
```

**Critical Feature**: `MSG_WAITALL` ensures complete message reception, prevents partial reads.

---

## 3. Synchronization Algorithm Design

### 3.1 Problem Statement

**Challenge**: Maintain strong consistency between primary and secondary trackers without complex consensus algorithms.

**Requirements:**
1. Secondary must reflect all state changes from primary
2. Minimal latency overhead on primary operations
3. Order preservation of operations
4. Simple implementation without Paxos/Raft

### 3.2 FLAG-Prefix Synchronization Protocol

#### 3.2.1 Protocol Design

**Message Format:**
```
FLAG<username> <original_message>
```

**Example Synchronization Messages:**
```
FLAGalice create_user alice hashedpass123
FLAGbob login bob password456
FLAGalice create_group engineering
FLAGalice upload engineering /path/file.txt 0 2048000 55001 a1b2c3d4...
FLAGbob join_group engineering
FLAGalice accept_request engineering bob
```

**Design Rationale:**

1. **Fixed PREFIX identification**: 
   - O(1) detection: `if (msg.starts_with("FLAG"))`
   - No complex parsing required
   - Clear separation from client traffic

2. **Username preservation**:
   - Maintains session context
   - Enables stateful operation replay
   - Format: `FLAG<username>` with no space before username

3. **Message replay approach**:
   - Exact replication of client requests
   - Simple implementation
   - Guarantees identical state transitions

#### 3.2.2 Primary Tracker Implementation

```cpp
void update_tracker(string message, int tracker_fd, string username) {
    if (tracker_fd <= 0) return;
    
    // Check if connection is alive
    if (check_connection(tracker_fd)) {
        sendMessage(tracker_fd, message);
    } else {
        cerr << "Secondary tracker unreachable" << endl;
        // Continue operation (availability over consistency)
    }
}

// Called after successful state-changing operations
void parse(vector<char*>& vec, int clientSocket, string username, string temp) {
    string cmd(vec[0]);
    
    if (cmd == "create_user") {
        // Process locally
        user_base[username] = password;
        sendMessage(clientSocket, "User created successfully!");
        
        // Synchronize to secondary
        update_tracker(temp, tracker2_fd, username);
    }
    // Similar pattern for login, create_group, upload, etc.
}
```

**Key Observation**: Synchronization happens **after** local processing and client acknowledgment. This prioritizes availability (client gets response even if secondary is down).

#### 3.2.3 Secondary Tracker Implementation

```cpp
void middle_ware(vector<char*>& vec, int clientSocket, string& temp) {
    if (vec[0] == nullptr) return;
    
    // Extract username from FLAG message
    string username(vec[0]);
    vec.erase(vec.begin());  // Remove username, leaving original command
    
    // Process as if from client (no response sent back)
    parse(vec, clientSocket, username, temp);
}

void p_execution(int clientSocket) {
    while (true) {
        string msg = recvMessage(clientSocket);
        if (msg.empty()) {
            close(clientSocket);
            return;
        }
        
        // Tokenize: "FLAGalice login alice pass" 
        // -> ["alice", "login", "alice", "pass"]
        vector<char*> args = tokenise(msg);
        middle_ware(args, clientSocket, msg);
    }
}
```

**Processing Flow:**
1. Receive: `"FLAGalice create_group engineering"`
2. Tokenize: `["alice", "create_group", "engineering"]`
3. Extract username: `"alice"`
4. Process: `parse(["create_group", "engineering"], ...)` with username context
5. Update local state identically to primary

### 3.3 Consistency Analysis

**Consistency Model Achieved:**

1. **Sequential Consistency**: 
   - TCP guarantees in-order delivery
   - Single-threaded processing on secondary preserves order
   - All operations appear in same sequence on both trackers

2. **Strong Eventual Consistency**:
   - After synchronization completes, both trackers have identical state
   - No divergence under normal operation
   - Temporary inconsistency during network delays only

3. **Availability Trade-off**:
   - Primary continues if secondary fails (AP in CAP theorem)
   - Manual failover supported (not automatic)

**Failure Scenarios:**

| Scenario | Behavior | Consistency Impact |
|----------|----------|-------------------|
| Secondary down | Primary continues | Temporary divergence |
| Primary down | Clients fail over manually | No new operations |
| Network partition | Primary continues | Secondary falls behind |
| Both down | System unavailable | N/A |

### 3.4 Alternative Approaches Considered

#### Option 1: Two-Phase Commit (2PC)

**Pseudocode:**
```cpp
bool execute_with_2pc(string operation) {
    // Phase 1: Prepare
    send(secondary, "PREPARE " + operation);
    string vote = recv(secondary);
    if (vote != "YES") return false;
    
    // Phase 2: Commit
    execute_locally(operation);
    send(secondary, "COMMIT " + operation);
    return true;
}
```

**Analysis:**
- ✅ Strong consistency
- ❌ Blocking: primary waits for secondary
- ❌ 2x network latency per operation
- ❌ Secondary failure blocks primary
- **Decision**: Rejected for performance/availability

#### Option 2: Asynchronous Replication

**Pseudocode:**
```cpp
queue<string> sync_queue;

void async_replicator() {
    while (true) {
        string op = sync_queue.pop();
        send(secondary, op);
        // Don't wait for acknowledgment
    }
}
```

**Analysis:**
- ✅ Low latency (non-blocking)
- ❌ Possible message loss
- ❌ No ordering guarantees with multiple threads
- ❌ Complex recovery after failures
- **Decision**: Rejected for consistency concerns

#### Option 3: State Transfer (Chosen Hybrid)

**Current Implementation** (FLAG-prefix message passing):
- ✅ Simple implementation
- ✅ Sequential consistency
- ✅ Reasonable latency (one-way send, no ACK)
- ❌ No automatic recovery
- ❌ Requires manual failover
- **Decision**: ✅ **Selected** for simplicity and adequate consistency

---

## 4. Piece Selection Strategy

### 4.1 Implemented Strategy

**Algorithm**: Sequential piece selection with random peer assignment

```cpp
mutex mtx;
int next_piece = 0;

void download_piece(...) {
    while (true) {
        int my_piece = -1;
        {
            lock_guard<mutex> lock(mtx);
            if (next_piece < total_pieces) {
                my_piece = next_piece++;  // Sequential assignment
            } else {
                return;  // No more pieces
            }
        }
        
        // Random peer selection
        if (!peer_list[my_piece].empty()) {
            int idx = rand() % peer_list[my_piece].size();
            download_from_peer(peer_list[my_piece][idx]);
        }
    }
}
```

### 4.2 Strategy Analysis

#### 4.2.1 Sequential Piece Selection

**Advantages:**
1. **Simple implementation**: Single atomic counter
2. **Predictable I/O**: Sequential writes optimize disk performance
   - HDDs: Eliminates seek overhead
   - SSDs: Improves wear leveling
3. **Streaming-friendly**: Early pieces available for partial playback
4. **Memory efficiency**: No complex piece priority queue

**Disadvantages:**
1. **End-game problem**: Last pieces become scarce
   - If few peers have final pieces, bottleneck occurs
   - Solution: Last 5-10% should switch to rarest-first
2. **No rarity consideration**: Doesn't prioritize rare pieces
3. **Convoy effect**: All downloaders want same pieces simultaneously

**Measured Performance** (from code analysis):
- Works well for: Small swarms (2-10 peers), fast local networks
- Struggles with: Large files with partial seeders

#### 4.2.2 Random Peer Selection

**Implementation:**
```cpp
if (my_piece >= 0 && my_piece < peer_list.size() && !peer_list[my_piece].empty()) {
    int idx = rand() % peer_list[my_piece].size();
    const PeerInfo& peer = peer_list[my_piece][idx];
    connect_to_peer(peer.ip, peer.port);
}
```

**Advantages:**
1. **Load balancing**: Distributes requests across available peers
2. **Fault tolerance**: Natural fallback if peer disconnects
3. **No coordination overhead**: Independent per-thread decisions

**Disadvantages:**
1. **No latency awareness**: May select slow/distant peers
2. **No bandwidth optimization**: Ignores peer upload capacity
3. **Possible repeated failures**: May repeatedly try offline peers

### 4.3 Alternative Strategies

#### 4.3.1 Rarest Piece First (BitTorrent)

**Concept**: Download pieces that fewest peers have

**Implementation Sketch:**
```cpp
int select_rarest_piece(vector<vector<PeerInfo>>& peer_list, set<int>& downloaded) {
    int rarest = -1;
    int min_peers = INT_MAX;
    
    for (int i = 0; i < peer_list.size(); i++) {
        if (downloaded.count(i)) continue;
        if (peer_list[i].size() < min_peers) {
            rarest = i;
            min_peers = peer_list[i].size();
        }
    }
    return rarest;
}
```

**Why Not Implemented:**
- Requires global piece availability view (additional tracker query or gossip)
- O(N) scan per piece selection
- Most beneficial in large swarms (100+ peers)
- Current system: Small groups, full files typically available
- **Conclusion**: Overkill for typical use case

#### 4.3.2 Random Piece Selection

**Concept**: Select any available piece randomly

```cpp
int select_random_piece(vector<vector<PeerInfo>>& peer_list, set<int>& downloaded) {
    vector<int> available;
    for (int i = 0; i < peer_list.size(); i++) {
        if (!downloaded.count(i) && !peer_list[i].empty()) {
            available.push_back(i);
        }
    }
    return available[rand() % available.size()];
}
```

**Trade-offs:**
- ✅ Better load distribution than sequential
- ✅ Avoids convoy effect
- ❌ Non-sequential disk writes (seek overhead)
- ❌ Complicates file assembly
- **Decision**: Rejected for I/O performance reasons

### 4.4 Thread Starvation Issue

**Observed Problem:**
```
Thread 139876534101696 assigned piece: 0
downloading piece: 0
Thread 139876534101696 assigned piece: 1  // Same thread again!
downloading piece: 1
Thread 139876534101696 assigned piece: 2  // Still same thread!
Thread 139876525708992 found no more pieces  // Other thread starves
```

**Root Cause**: Fast thread completes download and re-acquires mutex before OS schedules other threads

**Solution Implemented:**
```cpp
{
    lock_guard<mutex> lock(mtx);
    my_piece = next_piece++;
}
this_thread::yield();  // Explicit context switch hint
download_piece_data(my_piece);
```

**Effectiveness**: Partial improvement, not guaranteed

**Better Solution** (recommended):
```cpp
// Work-stealing: assign batches of pieces
{
    lock_guard<mutex> lock(mtx);
    int batch_size = min(4, total_pieces - next_piece);
    my_start = next_piece;
    next_piece += batch_size;
}
for (int i = 0; i < batch_size; i++) {
    download_piece(my_start + i);
}
```

This ensures fairer distribution even with fast networks.

---

## 5. Protocol Design Rationale

### 5.1 Client-Tracker Protocol

#### 5.1.1 Upload Protocol

**Message Format:**
```
upload <group_id> <file_path> <piece_no> <file_size> <listen_port> <hash>
```

**Example:**
```
upload engineering /home/alice/doc.pdf 0 2097152 55001 a1b2c3d4e5f6...
```

**Design Decisions:**

1. **Piece-by-piece registration** (not bulk):
   ```cpp
   for (int i = 0; i < total_pieces; i++) {
       compute_hash(piece[i]);
       send_to_tracker("upload " + ... + hash);
   }
   ```
   
   **Rationale:**
   - ✅ Enables incremental sharing (become seeder before full upload)
   - ✅ Early pieces available while uploading continues
   - ❌ N network round-trips for N pieces
   - **Use Case**: Large files where partial availability matters

2. **SHA-1 hash inclusion**:
   ```cpp
   unsigned char hash[SHA_DIGEST_LENGTH];
   SHA1(data, size, hash);
   sprintf(hexbuf, "%02x%02x...", hash[0], hash[1], ...);
   ```
   
   **Rationale:**
   - ✅ Content verification (detect corruption/malicious peers)
   - ✅ Deduplication possible (same hash = same content)
   - ❌ CPU overhead (SHA-1 computation)
   - ❌ 40 hex characters per piece (bandwidth cost)
   - **Note**: SHA-1 deprecated for crypto, but adequate for integrity

3. **Listen port in message**:
   ```cpp
   int port = 55000 + hash<string>()(username) % 1000;
   message += to_string(port);
   ```
   
   **Rationale:**
   - ✅ Dynamic port assignment (multiple clients per machine)
   - ✅ No hardcoded ports
   - ❌ Complicates NAT traversal (not implemented)
   - ❌ Requires firewall configuration

#### 5.1.2 Download Protocol

**Request:**
```
download <group_id> <file_name>
```

**Response Format:**
```
abcdef <file_size>
<piece_no> <username> <ip> <port> <filepath> <hash>
<piece_no> <username> <ip> <port> <filepath> <hash>
...
END_OF_FILE
```

**Example Response:**
```
abcdef 2097152
0 alice 127.0.0.1 55001 /home/alice/doc.pdf a1b2c3...
0 bob 192.168.1.5 55002 /home/bob/doc.pdf a1b2c3...
1 alice 127.0.0.1 55001 /home/alice/doc.pdf d4e5f6...
END_OF_FILE
```

**Design Rationale:**

1. **Complete peer list upfront** (not on-demand):
   
   **Advantages:**
   - ✅ Enables immediate parallel downloads
   - ✅ Single tracker query (reduces load)
   - ✅ Simple client logic
   
   **Disadvantages:**
   - ❌ Large response for files with many pieces
   - ❌ Stale data if peers disconnect mid-download
   - ❌ Wasted bandwidth if client crashes early
   
   **Alternative Considered**: On-demand piece queries
   ```
   Client: "get_peers_for_piece doc.pdf 5"
   Tracker: "2 alice:55001:/path1 bob:55002:/path2"
   ```
   - ✅ Fresh data per piece
   - ❌ N queries for N pieces (latency accumulation)
   - ❌ Higher tracker load
   - **Decision**: Rejected for performance

2. **Header format** (`abcdef` marker):
   ```cpp
   string header = "abcdef " + to_string(file_size) + "\n";
   sendMessage(sock, header);
   ```
   
   **Purpose**: Signal file size before peer list
   
   **Issues**:
   - Magic string "abcdef" is unclear (should be "FILE_SIZE" or similar)
   - Inconsistent with rest of protocol design
   - **Recommendation**: Use structured format like `"FILE_INFO <size> <total_pieces>"`

3. **Multiple peers per piece**:
   ```cpp
   0 alice 127.0.0.1 55001 /path/alice a1b2c3...
   0 bob 192.168.1.5 55002 /path/bob a1b2c3...  // Same piece, different peer
   ```
   
   **Rationale:**
   - ✅ Load balancing (client chooses)
   - ✅ Fault tolerance (fallback peers)
   - ✅ Enables parallel downloads from multiple sources (not implemented)

### 5.2 Peer-to-Peer Protocol

**Request Format:**
```
get_piece <piece_number> <file_name>
```

**Response**: Raw binary data (512KB)

**Implementation:**
```cpp
// Requester (client)
string request = "get_piece " + to_string(piece_no) + " " + filename;
sendMessage(peer_sock, request);
string data = recvMessage(peer_sock);

// Responder (peer server)
int fd = open(file_path.c_str(), O_RDONLY);
off_t offset = piece_no * 512 * 1024;
lseek(fd, offset, SEEK_SET);
read(fd, buffer, 512 * 1024);
uploadMessage(sock, buffer);  // Send binary data
```

**Design Decisions:**

1. **Text request, binary response**:
   
   **Rationale:**
   - ✅ Human-readable debugging for requests
   - ✅ Efficient binary transfer (no Base64 overhead)
   - ❌ Mixed protocol complexity
   - **Alternative**: Full binary protocol with fixed-length headers

2. **Single piece per connection**:
   ```cpp
   for (int i = 0; i < total_pieces; i++) {
       int sock = connect_to_peer();
       request_piece(sock, i);
       close(sock);  // New connection each time
   }
   ```
   
   **Trade-offs:**
   - ✅ Simple implementation
   - ✅ Easy error recovery (failed piece doesn't affect others)
   - ✅ Parallel downloads from different peers
   - ❌ TCP connection overhead (3-way handshake per piece)
   - ❌ Slow start for each connection
   
   **Better Approach**: Persistent connections with pipelining
   ```cpp
   int sock = connect_to_peer();
   for (piece : my_pieces) {
       request_piece(sock, piece);
   }
   close(sock);
   ```
   - ✅ Amortized connection cost
   - ❌ Complex error handling
   - **Decision**: Deferred (current approach sufficient for LAN)

3. **No compression**:
   - File data sent as-is
   - **Rationale**: Most shared files already compressed (PDF, ZIP, MP4)
   - Compression would waste CPU with minimal bandwidth savings

### 5.3 Synchronization Protocol Design

**Chosen Approach**: FLAG-prefix message passing

**Why This Design:**

1. **Prefix-based routing**:
   ```cpp
   if (msg.starts_with("FLAG")) {
       // Route to secondary tracker
   } else {
       // Normal client message
   }
   ```
   - O(1) identification
   - No regex or complex parsing
   - Clear separation

2. **Message replay semantics**:
   - Secondary executes same code path as primary
   - No special synchronization logic
   - Identical state transitions guaranteed

3. **Username in protocol**:
   - Preserves session context
   - Format: `FLAG<username>` (no space)
   - Enables stateful operations

**Alternative: State Transfer Protocol**

```
STATE_UPDATE <type> <serialized_data>

Example:
STATE_UPDATE USER {"username":"alice","password":"hash"}
STATE_UPDATE GROUP {"id":"eng","owner":"alice","members":["alice","bob"]}
STATE_UPDATE FILE {"group":"eng","file":"doc.pdf","pieces":[...]}
```

**Comparison:**

| Aspect | Message Replay (Current) | State Transfer |
|--------|--------------------------|----------------|
| Message Size | Large (full commands) | Compact (JSON) |
| Implementation | Simple (code reuse) | Complex (serialization) |
| Consistency | Exact (same code) | Requires careful design |
| Debugging | Easy (readable commands) | Harder (encoded data) |
| Version Compatibility | Tight coupling | Schema evolution needed |

**Decision**: Message replay chosen for simplicity and exact consistency guarantee.

---

## 6. Challenges and Solutions

### 6.1 Concurrency Challenges

#### Challenge 6.1.1: Thread Starvation in Piece Assignment

**Problem Observed:**
```
Output:
Thread 139876534101696 assigned piece: 0
downloading piece: 0
Thread 139876534101696 connected to peer 127.0.0.1:55821 requesting piece 0
reached after receiving data
Thread 139876525708992 found no more pieces  // Only 1 piece downloaded!
Thread 139876517316288 found no more pieces
Thread 139876508923584 found no more pieces
```

**Root Cause Analysis:**

1. **Fast local network**: Piece download completes in <1ms
2. **Thread scheduling**: Thread 1 completes, loops back, reacquires mutex before OS schedules others
3. **No fairness guarantee**: C++ mutex is not fair (no FIFO guarantee)

**Code Flow:**
```cpp
// Thread 1
{lock} my_piece = 0; next_piece = 1; {unlock}
download(0);  // Fast! <1ms on localhost
{lock} my_piece = 1; next_piece = 2; {unlock}
download(1);  // Fast again!
{lock} my_piece = 2; next_piece = 3; {unlock}
// ... Thread 1 finishes all pieces

// Thread 2 (finally gets scheduled)
{lock} next_piece >= total_pieces; {unlock}  // No pieces left!
return;
```

**Solutions Attempted:**

**Solution 1**: Explicit yield (partial fix)
```cpp
{
    lock_guard<mutex> lock(mtx);
    my_piece = next_piece++;
}
this_thread::yield();  // Hint to scheduler: give others a chance
download_piece(my_piece);
```

**Effectiveness**: Improves fairness but not guaranteed (scheduler hint only)

**Solution 2**: Work-stealing batches (recommended)
```cpp
{
    lock_guard<mutex> lock(mtx);
    int batch_size = min(4, total_pieces - next_piece);
    my_start_piece = next_piece;
    next_piece += batch_size;
}

for (int i = 0; i < batch_size; i++) {
    download_piece(my_start_piece + i);
}
```

**Advantages**:
- Guaranteed fair distribution (each thread gets batch)
- Fewer mutex acquisitions (reduces contention)
- Better for fast networks

**Solution 3**: Fair mutex (pthread_mutex with PTHREAD_MUTEX_ADAPTIVE_NP)
```cpp
pthread_mutex_t fair_mutex;
pthread_mutexattr_t attr;
pthread_mutexattr_init(&attr);
pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ADAPTIVE_NP);
pthread_mutex_init(&fair_mutex, &attr);
```

Not implemented due to C++ mutex abstraction, but would provide true fairness.

#### Challenge 6.1.2: Race Condition in File Writes

**Problem:**
```cpp
// Thread 1 and Thread 2 both writing to same file

// Thread 1
lseek(fd, offset1, SEEK_SET);  // Seek to piece 5
// **CONTEXT SWITCH**

// Thread 2
lseek(fd, offset2, SEEK_SET);  // Seek to piece 10 (overwrites file position!)

// Thread 1 resumes
write(fd, data1, size1);        // Writes piece 5 data at piece 10 location! ❌
```

**Root Cause**: `lseek()` + `write()` is not atomic. File descriptor has single shared offset.

**Solution: pwrite() for Atomic Writes**
```cpp
off_t write_offset = (off_t)my_piece * piece_size;
ssize_t bytes_written = pwrite(fd, buffer.data(), buffer.size(), write_offset);
```

**Why pwrite() Solves It:**
- Does not modify file offset (thread-safe)
- Atomic seek + write operation
- POSIX standard (portable)