// src/core/node.cpp
#include "node.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <algorithm>

namespace nexus {

Node::Node(const std::string& dbPath, int p2pPort, int metricsPort, const std::string& nodeId)
    : nodeId_(nodeId), p2pPort_(p2pPort), metricsPort_(metricsPort)
    , work_(std::make_unique<boost::asio::io_context::work>(ioContext_)) {
    
    blockchain_ = std::make_unique<Blockchain>(dbPath);

    // Загружаем сохранённых пиров из БД
    auto saved_peers = blockchain_->getDB()->getPeers(10);
    std::cout << "Loaded " << saved_peers.size() << " saved peers from database" << std::endl;
    for (const auto& [ip, port] : saved_peers) {
        // Не подключаемся к самому себе
        if (port != p2pPort_) {
            connectToPeer(ip, port);
        }
    }
    // Если нет ни одного сохранённого пира, используем seed-узлы (пример)
    if (saved_peers.empty()) {
        std::cout << "No saved peers, using seed nodes..." << std::endl;
        // Здесь можно задать список seed-узлов. Для демо используем localhost с разными портами.
        // В реальной сети это должны быть известные стабильные узлы.
        std::vector<std::pair<std::string, int>> seeds = {
            {"127.0.0.1", 8000},
            {"127.0.0.1", 8001},
            {"127.0.0.1", 8002}
        };
        for (const auto& [ip, port] : seeds) {
            if (port != p2pPort_) {
                connectToPeer(ip, port);
            }
        }
    }

    server_ = std::make_unique<Server>(ioContext_, p2pPort);
    
    if (metricsPort_ > 0) {
        try {
            metrics_ = std::make_unique<MetricsRegistry>(metricsPort_);
            std::cout << "Metrics server started on port " << metricsPort_ << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to start metrics: " << e.what() << std::endl;
            metrics_ = nullptr;
        }
    } else {
        std::cout << "Metrics disabled" << std::endl;
        metrics_ = nullptr;
    }
    
    startHttpServer();

    setupHandlers();
    
    std::cout << "Node created: " << nodeId_ << " (P2P: " << p2pPort_ << ")" << std::endl;
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

    std::thread([this]() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(15));
        if (!running_) break;
        for (auto& client : clients_) {
            if (client->is_connected()) {
                auto ping = Message::create_ping(nodeId_);
                client->send(ping);
            }
        }
    }
    }).detach();

    background_threads_.emplace_back([this]() { gossipPeers(); }); // Запускаем периодическую рассылку пиров

    // Периодический запрос списка пиров у всех подключённых
    background_threads_.emplace_back([this]() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(45));
            if (!running_) break;   // ← добавить
            Message get_peers_msg = Message::create_get_peers(nodeId_);
            for (auto& client : clients_) {
                if (client && client->is_connected()) {
                    client->send(get_peers_msg);
                    if (metrics_) metrics_->incPacketsSent("GET_PEERS");
                }
            }
            std::cout << "Requested peer lists from " << clients_.size() << " peers" << std::endl;
        }
    });
    
    // Периодическая синхронизация каждые 30 секунд
    background_threads_.emplace_back([this]() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            if (!running_) break;
            if (!clients_.empty() && running_) {
                std::cout << "Periodic sync: requesting blocks from first peer" << std::endl;
                syncWithPeer(clients_[0]->get_peer());
                broadcastPeersToAll();
            }
        }
    });

    // Периодическая очистка mempool
    mempool_cleaner_thread_ = std::thread([this]() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            if (!running_) break;
            int removed = blockchain_->cleanMempool();
            if (removed > 0) {
                std::cout << "Cleaned " << removed << " invalid transactions from mempool" << std::endl;
            }
        }
    });

    blockchain_->cleanMempool();

     // Запускаем майнинг
    mining_ = true;
    mining_thread_ = std::thread([this]() { mine_loop(); });
    
    updateMetrics();
    
    std::cout << "Node " << nodeId_ << " started on port " << p2pPort_ << std::endl;
}

void Node::stop() {
    if (!running_) return;
    running_ = false;

    // Остановка майнинга
    mining_ = false;
    if (mining_thread_.joinable()) {
        mining_thread_.join();
    }

    // Остановка io_context
    work_.reset();
    ioContext_.stop();
    if (ioThread_.joinable()) {
        ioThread_.join();
    }

    // Остановка сервера (закрывает сокеты)
    server_->stop();

    // Ожидание завершения всех фоновых потоков
    for (auto& th : background_threads_) {
        if (th.joinable()) th.join();
    }
    background_threads_.clear();

    std::cout << "Node " << nodeId_ << " stopped" << std::endl;
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

    // Обновляем время последнего контакта с пиром в БД
    if (peer) {
        blockchain_->getDB()->updatePeerSeen(peer->address, peer->port);
    }

    std::cout << "Received " << message_type_to_string(msg.type) << " from " << peer->get_endpoint() << std::endl;
    
    switch (msg.type) {
        case MessageType::HANDSHAKE: {
            peer->id = msg.sender_id;
            // Сохраняем настоящий P2P-порт узла
            if (msg.payload.contains("port")) {
                peer->p2p_port = msg.payload["port"].get<int>();
            }
            peer->state = PeerState::READY;
            std::cout << "Handshake with " << peer->id 
                    << " (p2p_port=" << peer->p2p_port << ")" << std::endl;
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
                        // Сохраняем в БД
                        blockchain_->getDB()->addPeer(ip, port, "");
                        // Подключаемся, если ещё не подключены
                        connectToPeer(ip, port);
                    }
                }
            }
            break;
        }
        
        case MessageType::GET_BLOCKS: {
            int from = msg.payload.value("from_height", 0);
            int current_height = blockchain_->getHeight();
            std::cout << "[DEBUG] GET_BLOCKS: from=" << from << ", my height=" << current_height << std::endl;
            
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
                std::cout << "Sent " << blocks.size() << " blocks (heights " << from << "-" << current_height << ")" << std::endl;
            }
            break;
        }
        
        case MessageType::BLOCKS_RESPONSE: {
            if (msg.payload.is_array()) {
                std::cout << "Received " << msg.payload.size() << " blocks" << std::endl;
                int added = 0;
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
                    
                    // ← ПРОВЕРКА: если блок уже есть, пропускаем
                    if (block.height <= blockchain_->getHeight()) {
                        std::cout << "Block #" << block.height << " already exists, skipping" << std::endl;
                        continue;
                    }
                    
                    std::cout << "Processing block #" << block.height << " (prev: " << block.prevHash.substr(0, 8) << "...)" << std::endl;

                    if (blockchain_->addBlock(block)) {
                        added++;
                        std::cout << "Added block #" << block.height << std::endl;
                    } else {
                        std::cout << "Failed to add block #" << block.height << std::endl;
                    }
                }
                if (added > 0) {
                    updateMetrics();
                    std::cout << "Synced " << added << " new blocks" << std::endl;
                }
            }
            break;
        }
        
        case MessageType::NEW_TRANSACTION: {
            Transaction tx;
            tx.fromAddress = msg.payload.value("from", "");
            tx.toAddress = msg.payload.value("to", "");
            tx.amount = msg.payload.value("amount", 0.0);
            tx.fee = msg.payload.value("fee", 0.001);
            tx.nonce = msg.payload.value("nonce", 1ULL);
            tx.timestamp = time(nullptr);
            tx.signature = msg.payload.value("signature", "p2p_sig");
            tx.txHash = tx.calculateHash();

            if (blockchain_->addTransaction(tx)) {
                if (metrics_) metrics_->incTransactionsProcessed();
                broadcastTransaction(tx);
                std::cout << "New transaction: " << tx.fromAddress << " -> " << tx.toAddress 
                        << " (" << tx.amount << ", fee=" << tx.fee << ")" << std::endl;
            }
            break;
        }
        
        case MessageType::NEW_BLOCK: {
            Block block;
            block.fromJson(msg.payload);
            
            int my_height = blockchain_->getHeight();
            auto last_block = blockchain_->getBlock(my_height);
            
            // 1. Блок является прямым продолжением
            if (block.height == my_height + 1 && block.prevHash == last_block->hash) {
                if (blockchain_->addBlock(block)) {
                    // Останавливаем майнинг и перезапускаем
                    mining_ = false;
                    if (mining_thread_.joinable()) mining_thread_.join();
                    broadcastBlock(block);
                    mining_ = true;
                    mining_thread_ = std::thread([this]() { mine_loop(); });
                }
            }
            // 2. Блок с той же высотой, что и текущий последний (конкурирующий блок)
            else if (block.height == my_height && block.prevHash == last_block->prevHash) {
                // Заменяем последний блок
                if (blockchain_->replaceLastBlock(block)) {
                    std::cout << "Fork resolved by replacing block #" << my_height << std::endl;
                    broadcastBlock(block);
                    // Перезапускаем майнинг
                    mining_ = false;
                    if (mining_thread_.joinable()) mining_thread_.join();
                    mining_ = true;
                    mining_thread_ = std::thread([this]() { mine_loop(); });
                }
            }
            // 3. Блок выше текущей цепи, но не является прямым продолжением (форк с отставанием)
            else if (block.height > my_height + 1 || (block.height == my_height + 1 && block.prevHash != last_block->hash)) {
                std::cout << "Potential fork detected: received block #" << block.height 
                        << " with prevHash " << block.prevHash.substr(0,8)
                        << ", my last block #" << my_height << " hash " << last_block->hash.substr(0,8)
                        << ". Requesting missing blocks." << std::endl;
                // Запрашиваем у отправителя цепочку начиная с высоты расхождения
                // Ищем общий предок – проще всего запросить с высоты my_height+1
                Message req;
                req.type = MessageType::GET_BLOCKS;
                req.sender_id = nodeId_;
                req.payload = {{"from_height", my_height + 1}};
                peer->send(req.serialize());
                // Сохраняем запрошенную цепочку для последующей обработки (можно сохранить в отдельный кеш)
                // Для упрощения будем обрабатывать при получении BLOCKS_RESPONSE
            }
            else {
                std::cout << "Ignoring block #" << block.height << " (my height=" << my_height << ")" << std::endl;
            }
            break;
        }
                
        default:
            break;
    }
    updateMetrics();
}

void Node::handleConnection(std::shared_ptr<Peer> peer) {
    // Проверяем, не подключен ли уже ЭТОТ ЖЕ пир (по порту)
    for (const auto& client : clients_) {
        if (client->get_peer()->address == peer->address && 
            client->get_peer()->port == peer->port) {
            std::cout << "Peer " << peer->get_endpoint() << " already connected, rejecting" << std::endl;
            peer->disconnect();
            return;
        }
    }
    
    // НЕ БЛОКИРУЕМ подключения с одного IP, если порты разные!
    std::cout << "New connection from " << peer->get_endpoint() << std::endl;
    
    auto client = std::make_shared<Client>(ioContext_);
    client->set_peer(peer);
    clients_.push_back(client);
    
    peer->read([this, peer](const std::string& data) {
        try {
            Message msg = Message::deserialize(data);
            handleMessage(msg, peer);
        } catch (const std::exception& e) {
            std::cout << "Error parsing message: " << e.what() << std::endl;
        }
    });
    
    auto handshake = Message::create_handshake(nodeId_, p2pPort_);
    peer->send(handshake.serialize());
    updateMetrics();
}

void Node::syncWithPeer(std::shared_ptr<Peer> peer) {
    if (!peer || !peer->is_connected()) return;
    
    int my_height = blockchain_->getHeight();
    
    Message req;
    req.type = MessageType::GET_BLOCKS;
    req.sender_id = nodeId_;
    req.payload = {{"from_height", my_height + 1}};  // Запрашиваем ТОЛЬКО новые блоки
    peer->send(req.serialize());
    
    std::cout << "Requesting blocks from height " << (my_height + 1) << std::endl;
}

void Node::broadcastPeers() {
    if (clients_.empty()) return;
    nlohmann::json arr = nlohmann::json::array();
    for (auto& c : clients_) {
        auto peer = c->get_peer();
        if (peer && peer->p2p_port != 0 && !peer->id.empty()) {
            arr.push_back({
                {"ip", peer->address},
                {"port", peer->p2p_port}
            });
        }
    }
    if (arr.empty()) return;

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
    std::cout << "Updating metrics: peers=" << clients_.size() 
              << ", height=" << blockchain_->getHeight() 
              << ", mempool=" << blockchain_->getMempoolSize() << std::endl;
    metrics_->setPeers(clients_.size());
    metrics_->setBlockchainHeight(blockchain_->getHeight());
    metrics_->setMempoolSize(blockchain_->getMempoolSize());
}

void Node::startHttpServer() {
    int http_port = p2pPort_ + 1000;
    
    background_threads_.emplace_back([this, http_port]() {
        int server_fd, client_fd;
        struct sockaddr_in address;
        int opt = 1;
        int addrlen = sizeof(address);
        
        // Создаём сокет
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            std::cerr << "HTTP socket creation failed" << std::endl;
            return;
        }
        
        // Устанавливаем опции
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(http_port);
        
        // Привязываем
        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            std::cerr << "HTTP bind failed on port " << http_port << std::endl;
            close(server_fd);
            return;
        }
        
        // Начинаем слушать
        if (listen(server_fd, 3) < 0) {
            std::cerr << "HTTP listen failed" << std::endl;
            close(server_fd);
            return;
        }
        
        std::cout << "HTTP API started on port " << http_port << " (POST /transaction)" << std::endl;
        
        while (running_) {
            client_fd = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
            if (client_fd < 0) continue;
            
            // Читаем запрос
            char buffer[4096] = {0};
            int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
            
            if (bytes_read > 0) {
                std::string request(buffer);
                
                // Проверяем POST /transaction
                if (request.find("POST /transaction") != std::string::npos) {
                    // Находим тело запроса (после \r\n\r\n)
                    size_t body_pos = request.find("\r\n\r\n");
                    if (body_pos != std::string::npos) {
                        std::string body = request.substr(body_pos + 4);
                        // Очистка от любых пробельных символов и символов управления
                        body.erase(std::remove_if(body.begin(), body.end(), [](char c) {
                            return c == '\r' || c == '\n' || c == ' ' || c == '\t';
                        }), body.end());
                        // Если после очистки тело пустое – ошибка
                        if (body.empty()) {
                            std::string response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 15\r\n\r\nEmpty Body";
                            write(client_fd, response.c_str(), response.size());
                            close(client_fd);
                            continue;
                        }
                        
                        try {
                            auto j = nlohmann::json::parse(body);
                            
                            Transaction tx;
                            tx.fromAddress = j.value("from", "unknown");
                            tx.toAddress = j.value("to", "unknown");
                            tx.amount = j.value("amount", 0.0);
                            tx.fee = 0.001;
                            tx.timestamp = time(nullptr);
                            // Получаем корректный nonce для отправителя
                            tx.nonce = blockchain_->getDB()->getNextNonce(tx.fromAddress);
                            tx.txHash = tx.calculateHash();
                            tx.signature = "http_sig";
                            
                            if (blockchain_->addTransaction(tx)) {
                                std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
                                write(client_fd, response.c_str(), response.size());
                                std::cout << "HTTP transaction added: " << tx.fromAddress << " -> " << tx.toAddress << " (" << tx.amount << ")" << std::endl;
                            } else {
                                std::string response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 11\r\n\r\nInsufficient";
                                write(client_fd, response.c_str(), response.size());
                            }
                        } catch (const std::exception& e) {
                            std::string response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 15\r\n\r\nInvalid JSON";
                            write(client_fd, response.c_str(), response.size());
                            std::cerr << "HTTP error: " << e.what() << std::endl;
                        }
                    } else {
                        std::string response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 15\r\n\r\nNo Body";
                        write(client_fd, response.c_str(), response.size());
                    }
                } else {
                    std::string response = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
                    write(client_fd, response.c_str(), response.size());
                }
            }
            close(client_fd);
        }
        close(server_fd);
    });
}

void Node::broadcastPeersToAll() {
    if (clients_.empty()) return;
    nlohmann::json arr = nlohmann::json::array();
    for (auto& c : clients_) {
        auto peer = c->get_peer();
        // Добавляем только тех, у кого известен P2P-порт и есть id
        if (peer && peer->p2p_port != 0 && !peer->id.empty()) {
            arr.push_back({
                {"ip", peer->address},
                {"port", peer->p2p_port}
            });
        }
    }
    if (arr.empty()) return;

    Message msg;
    msg.type = MessageType::PEERS_LIST;
    msg.sender_id = nodeId_;
    msg.payload = arr;
    for (auto& c : clients_) {
        c->send(msg);
        if (metrics_) metrics_->incPacketsSent("PEERS_LIST");
    }
    std::cout << "Broadcasted " << arr.size() << " peers to all connections" << std::endl;
}

void Node::mine_loop() {
    using namespace std::chrono;
    auto last_time = steady_clock::now();
    uint64_t hashes_done = 0;

    while (mining_) {
        // Очищаем mempool от невалидных транзакций перед созданием нового блока
        blockchain_->cleanMempool();
        
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (!mining_) break;

        if (blockchain_->getMempoolSize() == 0) {
            continue;
        }

        int target_height = blockchain_->getHeight() + 1;
        Block new_block = blockchain_->createBlock(nodeId_);

        // Обновляем метрику сложности в Prometheus
        if (metrics_) {
            metrics_->setMiningDifficulty(blockchain_->getCurrentDifficulty());
        }

        bool mined = false;
        for (int nonce = 0; nonce < 1000000 && !mined && mining_; nonce++) {
            new_block.nonce = nonce;
            new_block.hash = new_block.calculateHash();
            hashes_done++;

            // Обновляем хэшрейт раз в 5 секунд
            auto now = steady_clock::now();
            auto elapsed = duration_cast<seconds>(now - last_time).count();
            if (elapsed >= 5 && metrics_) {
                double hashrate = static_cast<double>(hashes_done) / elapsed;
                metrics_->setHashrate(hashrate);
                hashes_done = 0;
                last_time = now;
            }

            // Если кто-то уже нашёл блок этой высоты, останавливаем майнинг
            if (blockchain_->getHeight() >= target_height) {
                std::cout << "Stopping mining, block #" << target_height << " already found" << std::endl;
                break;
            }

            std::string target(new_block.difficulty, '0');
            if (new_block.hash.substr(0, new_block.difficulty) == target) {
                if (blockchain_->addBlock(new_block)) {
                    broadcastBlock(new_block);
                    std::cout << "MINED BLOCK #" << new_block.height << "!" << std::endl;
                    mined = true;
                    if (metrics_) metrics_->incBlocksMined();
                }
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!mining_) break;
    }
}

void Node::gossipPeers() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        if (!running_) break;

        if (clients_.size() < 2) continue;

        int idx = rand() % clients_.size();
        auto target = clients_[idx];

        nlohmann::json peer_list = nlohmann::json::array();
        for (const auto& client : clients_) {
            auto peer = client->get_peer();
            if (client != target && peer && peer->p2p_port != 0 && !peer->id.empty()) {
                peer_list.push_back({
                    {"ip", peer->address},
                    {"port", peer->p2p_port}
                });
            }
        }

        if (!peer_list.empty()) {
            Message msg;
            msg.type = MessageType::PEERS_LIST;
            msg.sender_id = nodeId_;
            msg.payload = peer_list;
            target->send(msg);
            std::cout << "Gossip: sent " << peer_list.size()
                      << " peers to " << target->get_peer()->get_endpoint() << std::endl;
        }
    }
}

void Node::handleFork(const std::vector<Block>& alternative_chain) {
    if (alternative_chain.empty()) return;
    int current_height = blockchain_->getHeight();
    int new_height = alternative_chain.back().height;
    if (new_height <= current_height) return;

    std::cout << "Switching to longer chain (new height: " << new_height
              << ", current: " << current_height << ")" << std::endl;

    for (const auto& block : alternative_chain) {
        if (block.height > current_height) {
            blockchain_->addBlock(const_cast<Block&>(block));
        }
    }
}

} // namespace nexus
