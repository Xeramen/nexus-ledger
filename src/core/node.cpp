// src/core/node.cpp
#include "node.h"
#include <iostream>
#include <chrono>
#include <thread>

namespace nexus {

Node::Node(const std::string& dbPath, int p2pPort, int metricsPort, const std::string& nodeId)
    : nodeId_(nodeId), p2pPort_(p2pPort), metricsPort_(metricsPort)
    , work_(std::make_unique<boost::asio::io_context::work>(ioContext_)) {
    
    blockchain_ = std::make_unique<Blockchain>(dbPath);
    server_ = std::make_unique<Server>(ioContext_, p2pPort);
    
    if (metricsPort_ > 0) {
        try {
            metrics_ = std::make_unique<MetricsRegistry>(metricsPort_);
            std::cout << "📊 Metrics server started on port " << metricsPort_ << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "⚠️ Failed to start metrics: " << e.what() << std::endl;
            metrics_ = nullptr;
        }
    } else {
        std::cout << "ℹ️ Metrics disabled" << std::endl;
        metrics_ = nullptr;
    }
    
    // startHttpServer();  // ← ДОБАВЬ ЭТУ СТРОКУ

    setupHandlers();
    
    std::cout << "✅ Node created: " << nodeId_ << " (P2P: " << p2pPort_ << ")" << std::endl;
}

Node::~Node() {
    stop();
}

void Node::setupHandlers() {
    server_->set_message_handler([this](const Message& msg, std::shared_ptr<Peer> peer) {
        if (metrics_) metrics_->incPacketsReceived(message_type_to_string(msg.type));
        handleMessage(msg, peer);
    });
    
    server_->set_connection_handler([this](std::shared_ptr<Peer> peer) {
        handleConnection(peer);
    });
}

void Node::start() {
    if (running_) return;
    running_ = true;
    
    server_->start();
    
    ioThread_ = std::thread([this]() {
        ioContext_.run();
    });
    
    // Периодическая синхронизация каждые 30 секунд
    std::thread([this]() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            if (!clients_.empty() && running_) {
                std::cout << "🔄 Periodic sync: requesting blocks from first peer" << std::endl;
                syncWithPeer(clients_[0]->get_peer());
                broadcastPeersToAll();
            }
        }
    }).detach();
    
    updateMetrics();
    
    std::cout << "🚀 Node " << nodeId_ << " started on port " << p2pPort_ << std::endl;
}

void Node::stop() {
    if (!running_) return;
    running_ = false;
    
    server_->stop();
    work_.reset();
    
    if (ioThread_.joinable()) {
        ioThread_.join();
    }
    
    std::cout << "🛑 Node " << nodeId_ << " stopped" << std::endl;
}

void Node::connectToPeer(const std::string& ip, int port) {
    auto client = std::make_shared<Client>(ioContext_);
    
    client->set_message_handler([this](const Message& msg, std::shared_ptr<Peer> peer) {
        if (metrics_) metrics_->incPacketsReceived(message_type_to_string(msg.type));
        handleMessage(msg, peer);
    });
    
    client->set_connection_handler([this, client](bool connected) {
        if (connected) {
            clients_.push_back(client);
            auto handshake = Message::create_handshake(nodeId_, p2pPort_);
            client->send(handshake);
            if (metrics_) metrics_->incPacketsSent("HANDSHAKE");
            syncWithPeer(client->get_peer());
            updateMetrics();
        }
    });
    
    client->connect(ip, port, nodeId_);
}

void Node::handleMessage(const Message& msg, std::shared_ptr<Peer> peer) {
    std::cout << "📨 Received " << message_type_to_string(msg.type) << " from " << peer->get_endpoint() << std::endl;
    
    switch (msg.type) {
        case MessageType::HANDSHAKE: {
            peer->id = msg.sender_id;
            peer->state = PeerState::READY;
            std::cout << "🤝 Handshake with " << peer->id << std::endl;
             // Отправляем ему список наших пиров
            broadcastPeersToAll();
            // Синхронизируем блокчейн
            syncWithPeer(peer);
            updateMetrics();
            break;
        }
        
        case MessageType::PING: {
            peer->send(Message::create_pong(nodeId_).serialize());
            if (metrics_) metrics_->incPacketsSent("PONG");
            break;
        }
        
        case MessageType::GET_PEERS: {
            broadcastPeers();
            break;
        }
        
        case MessageType::PEERS_LIST: {
            if (msg.payload.is_array()) {
                for (const auto& p : msg.payload) {
                    std::string ip = p.value("ip", "");
                    int port = p.value("port", 0);
                    if (!ip.empty() && port > 0 && port != p2pPort_) {
                        connectToPeer(ip, port);
                    }
                }
            }
            break;
        }
        
        case MessageType::GET_BLOCKS: {
            int from = msg.payload.value("from_height", 0);
            int current_height = blockchain_->getHeight();
            std::cout << "📤 [DEBUG] GET_BLOCKS: from=" << from << ", my height=" << current_height << std::endl;
            
            // Отправляем все блоки от запрошенной высоты
            std::vector<Block> blocks;
            for (int h = from; h <= current_height; ++h) {
                auto block = blockchain_->getBlock(h);
                if (block) blocks.push_back(*block);
            }
            
            if (!blocks.empty()) {
                nlohmann::json jsonBlocks = nlohmann::json::array();
                for (const auto& b : blocks) {
                    jsonBlocks.push_back(b.toJson());
                }
                
                Message response;
                response.type = MessageType::BLOCKS_RESPONSE;
                response.sender_id = nodeId_;
                response.payload = jsonBlocks;
                peer->send(response.serialize());
                std::cout << "📤 Sent " << blocks.size() << " blocks (heights " << from << "-" << current_height << ")" << std::endl;
            }
            break;
        }
        
        case MessageType::BLOCKS_RESPONSE: {
            if (msg.payload.is_array()) {
                std::cout << "📦 Received " << msg.payload.size() << " blocks" << std::endl;
                for (const auto& bj : msg.payload) {
                    Block block;
                    block.height = bj.value("height", 0);
                    block.hash = bj.value("hash", "");
                    block.prevHash = bj.value("prevHash", "");
                    block.merkleRoot = bj.value("merkleRoot", "");
                    block.timestamp = bj.value("timestamp", 0L);
                    block.nonce = bj.value("nonce", 0);
                    block.difficulty = bj.value("difficulty", 2.0);
                    block.minedBy = bj.value("minedBy", "");
                    
                    std::cout << "  📦 Processing block #" << block.height << " (prev: " << block.prevHash.substr(0, 8) << "...)" << std::endl;
                    
                    if (blockchain_->addBlock(block)) {
                        std::cout << "  ✅ Added block #" << block.height << std::endl;
                    } else {
                        std::cout << "  ❌ Failed to add block #" << block.height << std::endl;
                    }
                }
                updateMetrics();
            }
            break;
        }
        
        case MessageType::NEW_TRANSACTION: {
            Transaction tx;
            tx.fromAddress = msg.payload.value("from", "");
            tx.toAddress = msg.payload.value("to", "");
            tx.amount = msg.payload.value("amount", 0.0);
            if (blockchain_->addTransaction(tx)) {
                if (metrics_) metrics_->incTransactionsProcessed();
                broadcastTransaction(tx);
            }
            break;
        }
        
        case MessageType::NEW_BLOCK: {
            Block block;
            block.height = msg.payload.value("height", 0);
            block.hash = msg.payload.value("hash", "");
            block.prevHash = msg.payload.value("prevHash", "");
            block.merkleRoot = msg.payload.value("merkleRoot", "");
            block.timestamp = msg.payload.value("timestamp", 0L);
            block.nonce = msg.payload.value("nonce", 0);
            block.difficulty = msg.payload.value("difficulty", 2.0);
            block.minedBy = msg.payload.value("minedBy", "");
            if (blockchain_->addBlock(block)) {
                broadcastBlock(block);
                std::cout << "🧱 New block #" << block.height << " from " << peer->get_endpoint() << std::endl;
            }
            break;
        }
        
        default:
            break;
    }
    updateMetrics();
}

void Node::handleConnection(std::shared_ptr<Peer> peer) {
    // Проверяем существующего пира
    for (const auto& client : clients_) {
        if (client->get_peer()->address == peer->address) {
            std::cout << "⚠️ Peer " << peer->address << " already connected, rejecting" << std::endl;
            peer->disconnect();
            return;
        }
    }

    // Проверяем, не подключен ли уже этот пир
    for (const auto& client : clients_) {
        if (client->get_peer()->address == peer->address && 
            client->get_peer()->port == peer->port) {
            std::cout << "⚠️ Peer " << peer->get_endpoint() << " already connected, ignoring" << std::endl;
            peer->disconnect();
            return;
        }
    }
    
    std::cout << "🔌 New connection from " << peer->get_endpoint() << std::endl;
    
    auto client = std::make_shared<Client>(ioContext_);
    client->set_peer(peer);
    clients_.push_back(client);
    
    peer->read([this, peer](const std::string& data) {
        try {
            Message msg = Message::deserialize(data);
            handleMessage(msg, peer);
        } catch (const std::exception& e) {
            std::cout << "❌ Error parsing message: " << e.what() << std::endl;
        }
    });
    
    auto handshake = Message::create_handshake(nodeId_, p2pPort_);
    peer->send(handshake.serialize());
    
    updateMetrics();
}

void Node::syncWithPeer(std::shared_ptr<Peer> peer) {
    if (!peer || !peer->is_connected()) return;
    
    int my_height = blockchain_->getHeight();
    std::cout << "🔄 [DEBUG] syncWithPeer: my height=" << my_height 
              << ", requesting from height " << (my_height + 1) << std::endl;
    
    Message req;
    req.type = MessageType::GET_BLOCKS;
    req.sender_id = nodeId_;
    req.payload = {{"from_height", 1}};
    peer->send(req.serialize());
    std::cout << "🔄 [DEBUG] syncWithPeer: requesting ALL blocks from height 1" << std::endl;
}

void Node::broadcastPeers() {
    nlohmann::json arr = nlohmann::json::array();
    for (auto& c : clients_) {
        arr.push_back({{"ip", c->get_peer()->address}, {"port", c->get_peer()->port}});
    }
    Message msg;
    msg.type = MessageType::PEERS_LIST;
    msg.sender_id = nodeId_;
    msg.payload = arr;
    for (auto& c : clients_) {
        c->send(msg);
        if (metrics_) metrics_->incPacketsSent("PEERS_LIST");
    }
}

void Node::broadcastTransaction(const Transaction& tx) {
    Message msg;
    msg.type = MessageType::NEW_TRANSACTION;
    msg.sender_id = nodeId_;
    msg.payload = {
        {"from", tx.fromAddress},
        {"to", tx.toAddress},
        {"amount", tx.amount}
    };
    for (auto& c : clients_) {
        c->send(msg);
        if (metrics_) metrics_->incPacketsSent("NEW_TRANSACTION");
    }
}

void Node::broadcastBlock(const Block& block) {
    Message msg;
    msg.type = MessageType::NEW_BLOCK;
    msg.sender_id = nodeId_;
    msg.payload = {
        {"height", block.height},
        {"hash", block.hash},
        {"prevHash", block.prevHash},
        {"merkleRoot", block.merkleRoot},
        {"timestamp", block.timestamp},
        {"nonce", block.nonce},
        {"difficulty", block.difficulty},
        {"minedBy", block.minedBy}
    };
    for (auto& c : clients_) {
        c->send(msg);
        if (metrics_) metrics_->incPacketsSent("NEW_BLOCK");
    }
}

void Node::updateMetrics() {
    if (!metrics_) return;
    std::cout << "📊 Updating metrics: peers=" << clients_.size() 
              << ", height=" << blockchain_->getHeight() 
              << ", mempool=" << blockchain_->getMempoolSize() << std::endl;
    metrics_->setPeers(clients_.size());
    metrics_->setBlockchainHeight(blockchain_->getHeight());
    metrics_->setMempoolSize(blockchain_->getMempoolSize());
}

void Node::startHttpServer() {
    int http_port = p2pPort_ + 1000;
    
    std::thread([this, http_port]() {
        boost::asio::io_context http_io;
        boost::asio::ip::tcp::acceptor acceptor(http_io, 
            boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), http_port));
        
        std::cout << "🌐 HTTP API started on port " << http_port << " (POST /transaction)" << std::endl;
        
        while (running_) {
            auto socket = std::make_shared<boost::asio::ip::tcp::socket>(http_io);
            boost::system::error_code ec;
            acceptor.accept(*socket, ec);
            
            if (ec) continue;
            
            // Читаем HTTP запрос
            boost::asio::streambuf buffer;
            boost::system::error_code read_ec;
            boost::asio::read_until(*socket, buffer, "\r\n\r\n", read_ec);
            
            if (read_ec) {
                continue;
            }
            
            std::istream is(&buffer);
            std::string method, path, version;
            is >> method >> path >> version;
            
            // Парсим заголовки
            std::string line;
            int content_length = 0;
            while (std::getline(is, line) && line != "\r") {
                if (line.find("Content-Length:") != std::string::npos) {
                    content_length = std::stoi(line.substr(16));
                }
            }
            
            // Читаем тело запроса
            std::string body;
            if (content_length > 0) {
                body.resize(content_length);
                boost::asio::read(*socket, boost::asio::buffer(body), read_ec);
            }
            
            std::string response;
            
            if (method == "POST" && path == "/transaction") {
                try {
                    if (body.empty()) {
                        throw std::runtime_error("Empty body");
                    }
                    
                    auto j = nlohmann::json::parse(body);
                    
                    Transaction tx;
                    tx.fromAddress = j.value("from", "unknown");
                    tx.toAddress = j.value("to", "unknown");
                    tx.amount = j.value("amount", 0.0);
                    tx.fee = 0.001;
                    tx.timestamp = time(nullptr);
                    tx.txHash = tx.calculateHash();
                    tx.signature = "http_sig";
                    
                    if (blockchain_->addTransaction(tx)) {
                        response = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
                        std::cout << "💰 HTTP transaction added: " << tx.fromAddress << " -> " << tx.toAddress << " (" << tx.amount << ")" << std::endl;
                    } else {
                        response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 11\r\n\r\nInsufficient";
                    }
                } catch (const std::exception& e) {
                    response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 15\r\n\r\nInvalid JSON";
                    std::cerr << "❌ HTTP error: " << e.what() << std::endl;
                }
            } else {
                response = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
            }
            
            boost::asio::write(*socket, boost::asio::buffer(response), ec);
        }
    }).detach();
}

void Node::broadcastPeersToAll() {
    if (clients_.empty()) return;
    
    nlohmann::json arr = nlohmann::json::array();
    for (auto& c : clients_) {
        arr.push_back({{"ip", c->get_peer()->address}, {"port", c->get_peer()->port}});
    }
    
    Message msg;
    msg.type = MessageType::PEERS_LIST;
    msg.sender_id = nodeId_;
    msg.payload = arr;
    
    for (auto& c : clients_) {
        c->send(msg);
        if (metrics_) metrics_->incPacketsSent("PEERS_LIST");
    }
    std::cout << "📡 Broadcasted " << clients_.size() << " peers to all connections" << std::endl;
}

} // namespace nexus