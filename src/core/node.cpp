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
            std::vector<Block> blocks;
            for (int h = from; h <= blockchain_->getHeight(); ++h) {
                auto block = blockchain_->getBlock(h);
                if (block) blocks.push_back(*block);
            }
            nlohmann::json jsonBlocks = nlohmann::json::array();
            for (const auto& b : blocks) {
                jsonBlocks.push_back(b.toJson());
            }
            Message response;
            response.type = MessageType::BLOCKS_RESPONSE;
            response.sender_id = nodeId_;
            response.payload = jsonBlocks;
            peer->send(response.serialize());
            if (metrics_) metrics_->incPacketsSent("BLOCKS_RESPONSE");
            break;
        }
        
        case MessageType::BLOCKS_RESPONSE: {
            if (msg.payload.is_array()) {
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
                    blockchain_->addBlock(block);
                }
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
    std::cout << "🔌 New connection from " << peer->get_endpoint() << std::endl;
    
    // Запускаем чтение сообщений - используем лямбду, которая вызывает handleMessage
    peer->read([this, peer](const std::string& data) {
        try {
            Message msg = Message::deserialize(data);
            handleMessage(msg, peer);  // ← вызываем handleMessage напрямую
        } catch (const std::exception& e) {
            std::cout << "❌ Error parsing message: " << e.what() << std::endl;
        }
    });
    
    auto handshake = Message::create_handshake(nodeId_, p2pPort_);
    peer->send(handshake.serialize());
    if (metrics_) metrics_->incPacketsSent("HANDSHAKE");
    updateMetrics();
}

void Node::syncWithPeer(std::shared_ptr<Peer> peer) {
    Message req;
    req.type = MessageType::GET_BLOCKS;
    req.sender_id = nodeId_;
    req.payload = {{"from_height", blockchain_->getHeight() + 1}};
    peer->send(req.serialize());
    if (metrics_) metrics_->incPacketsSent("GET_BLOCKS");
    
    Message getPeers;
    getPeers.type = MessageType::GET_PEERS;
    getPeers.sender_id = nodeId_;
    peer->send(getPeers.serialize());
    if (metrics_) metrics_->incPacketsSent("GET_PEERS");
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

} // namespace nexus