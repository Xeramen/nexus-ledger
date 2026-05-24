// src/main.cpp
#include <iostream>
#include <iomanip>
#include <memory>
#include <thread>
#include <chrono>
#include <csignal>
#include <boost/asio.hpp>

#include "blockchain/blockchain.h"
#include "network/server.h"
#include "network/client.h"
#include "network/message.h"

#include "core/node.h"

using namespace nexus;

bool running = true;

void signal_handler(int signal) {
    std::cout << "\n🛑 Shutting down..." << std::endl;
    running = false;
}

void printBalance(Blockchain& chain, const std::string& name, const std::string& address) {
    std::cout << "  " << name << " (" << address << "): " 
              << std::fixed << std::setprecision(2) << chain.getBalance(address) << std::endl;
}

void testBlockchain(const std::string& dbPath) {
    std::cout << "=== Nexus Ledger Blockchain Test ===\n" << std::endl;
    std::cout << "Database: " << dbPath << std::endl << std::endl;
    
    try {
        Blockchain chain(dbPath);
        
        std::cout << "Current chain height: " << chain.getHeight() << std::endl;
        
        auto genesis = chain.getBlock(0);
        if (genesis) {
            std::cout << "Genesis block found!" << std::endl;
            std::cout << "  Hash: " << genesis->hash << std::endl;
            std::cout << "  Miner: " << genesis->minedBy << std::endl;
        } else {
            std::cerr << "ERROR: Genesis block not found!" << std::endl;
            return;
        }
        
        std::cout << "\nInitial balances:" << std::endl;
        printBalance(chain, "Genesis miner", "genesis_miner");
        printBalance(chain, "Miner1", "miner1");
        printBalance(chain, "Alice", "alice");
        printBalance(chain, "Bob", "bob");
        
        Transaction tx1;
        tx1.fromAddress = "genesis_miner";
        tx1.toAddress = "alice";
        tx1.amount = 10.0;
        tx1.fee = 0.1;
        tx1.signature = "test_sig_1";
        tx1.timestamp = time(nullptr);
        tx1.txHash = tx1.calculateHash();
        
        if (chain.addTransaction(tx1)) {
            std::cout << "\nTransaction 1 added: genesis_miner -> alice (10.0)" << std::endl;
        }
        
        std::cout << "\nMining new block..." << std::endl;
        auto newBlock = chain.createBlock("miner1");
        
        if (newBlock.mine(1000000)) {
            std::cout << "Block mined!" << std::endl;
            std::cout << "  Height: " << newBlock.height << std::endl;
            std::cout << "  Hash: " << newBlock.hash.substr(0, 16) << "..." << std::endl;
            std::cout << "  Nonce: " << newBlock.nonce << std::endl;
            std::cout << "  Transactions: " << newBlock.transactions.size() << std::endl;
            
            if (chain.addBlock(newBlock)) {
                std::cout << "Block added to chain!" << std::endl;
            }
        }
        
        std::cout << "\nFinal balances:" << std::endl;
        printBalance(chain, "Genesis miner", "genesis_miner");
        printBalance(chain, "Miner1", "miner1");
        printBalance(chain, "Alice", "alice");
        printBalance(chain, "Bob", "bob");
        
        std::cout << "\nChain height: " << chain.getHeight() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}

void testNetwork(unsigned short port) {
    std::cout << "=== Nexus Ledger Network Test ===" << std::endl;
    std::cout << "Starting server on port " << port << "..." << std::endl;
    
    boost::asio::io_context io_context;
    
    nexus::Server server(io_context, port);
    
    server.set_connection_handler([](std::shared_ptr<nexus::Peer> peer) {
        std::cout << "🔌 New connection from: " << peer->get_endpoint() << std::endl;
        
        // Отправляем приветственное сообщение
        auto msg = nexus::Message::create_handshake("test-server", 8000);
        peer->send(msg.serialize());
    });
    
    server.set_message_handler([](const nexus::Message& msg, std::shared_ptr<nexus::Peer> peer) {
        std::cout << "📨 Received: " << nexus::message_type_to_string(msg.type) 
                  << " from " << peer->get_endpoint() << std::endl;
        
        // Отвечаем на ping
        if (msg.type == nexus::MessageType::PING) {
            auto pong = nexus::Message::create_pong("test-server");
            peer->send(pong.serialize());
            std::cout << "  ↳ Sent PONG" << std::endl;
        }
    });
    
    server.start();
    
    // Запускаем io_context в отдельном потоке
    std::thread io_thread([&io_context]() {
        io_context.run();
    });
    
    std::cout << "✅ Server running. Press Enter to stop..." << std::endl;
    std::cin.get();
    
    server.stop();
    io_context.stop();
    io_thread.join();
    
    std::cout << "Server stopped." << std::endl;
}

void testClient(const std::string& server_ip, int server_port) {
    std::cout << "=== Nexus Ledger Client Test ===" << std::endl;
    std::cout << "Connecting to " << server_ip << ":" << server_port << "..." << std::endl;
    
    boost::asio::io_context io_context;
    
    nexus::Client client(io_context);
    
    client.set_connection_handler([&client](bool connected) {
        if (connected) {
            std::cout << "✅ Connected to server!" << std::endl;
            
            // Отправляем ping
            auto ping = nexus::Message::create_ping("test-client");
            client.send(ping);
            std::cout << "📤 Sent PING" << std::endl;
        } else {
            std::cout << "❌ Connection failed!" << std::endl;
        }
    });
    
    client.set_message_handler([](const nexus::Message& msg, std::shared_ptr<nexus::Peer> peer) {
        std::cout << "📨 Received: " << nexus::message_type_to_string(msg.type) << std::endl;
    });
    
    client.connect(server_ip, server_port, "test-client");
    
    // Запускаем io_context на 5 секунд
    std::thread io_thread([&io_context]() {
        io_context.run_for(std::chrono::seconds(5));
    });
    
    io_thread.join();
    
    std::cout << "Client finished." << std::endl;
}

void printUsage(const char* program_name) {
    std::cout << "Usage:" << std::endl;
    std::cout << "  " << program_name << " blockchain [db_path]          - Run blockchain test" << std::endl;
    std::cout << "  " << program_name << " server <port>                 - Run P2P server" << std::endl;
    std::cout << "  " << program_name << " client <ip> <port>            - Run P2P client" << std::endl;
    std::cout << "  " << program_name << " network-test                  - Run network test" << std::endl;
    std::cout << "  " << program_name << " node <p2p_port> <db_path> <metrics_port> [connect_to] - Run P2P node" << std::endl;
    std::cout << "  " << program_name << " sendtx <node_port> <from> <to> <amount> - Send test transaction" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " blockchain" << std::endl;
    std::cout << "  " << program_name << " blockchain node1.db" << std::endl;
    std::cout << "  " << program_name << " server 8000" << std::endl;
    std::cout << "  " << program_name << " client 127.0.0.1 8000" << std::endl;
    std::cout << "  " << program_name << " node 8000 node1.db 9100" << std::endl;
    std::cout << "  " << program_name << " node 8001 node2.db 9101 127.0.0.1:8000" << std::endl;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    
    std::string command = argv[1];
    
    if (command == "blockchain") {
        std::string dbPath = "nexus.db"; // значение по умолчанию
        if (argc > 2) {
            dbPath = argv[2];
        }
        std::cout << "Using database: " << dbPath << std::endl;
        testBlockchain(dbPath);
    } 
    else if (command == "server") {
        if (argc < 3) {
            std::cerr << "Error: Port required" << std::endl;
            printUsage(argv[0]);
            return 1;
        }
        unsigned short port = static_cast<unsigned short>(std::stoi(argv[2]));
        testNetwork(port);
    }
    else if (command == "client") {
        if (argc < 4) {
            std::cerr << "Error: IP and port required" << std::endl;
            printUsage(argv[0]);
            return 1;
        }
        std::string ip = argv[2];
        int port = std::stoi(argv[3]);
        testClient(ip, port);
    }
    else if (command == "network-test") {
        std::cout << "=== Running Network Test ===" << std::endl;
        
        // Запускаем сервер в отдельном потоке
        unsigned short port = 8888;
        
        std::thread server_thread([port]() {
            boost::asio::io_context io_context;
            nexus::Server server(io_context, port);
            
            server.set_message_handler([](const nexus::Message& msg, std::shared_ptr<nexus::Peer> peer) {
                std::cout << "[SERVER] Received: " << nexus::message_type_to_string(msg.type) << std::endl;
                if (msg.type == nexus::MessageType::PING) {
                    auto pong = nexus::Message::create_pong("test-server");
                    peer->send(pong.serialize());
                }
            });
            
            server.start();
            
            std::thread io_thread([&io_context]() { io_context.run(); });
            std::this_thread::sleep_for(std::chrono::seconds(2));
            io_context.stop();
            io_thread.join();
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Запускаем клиента
        boost::asio::io_context io_context;
        nexus::Client client(io_context);
        
        std::atomic<bool> received_response{false};
        
        client.set_connection_handler([&client, &received_response](bool connected) {
            if (connected) {
                std::cout << "[CLIENT] Connected! Sending PING..." << std::endl;
                auto ping = nexus::Message::create_ping("test-client");
                client.send(ping);
            }
        });
        
        client.set_message_handler([&received_response](const nexus::Message& msg, std::shared_ptr<nexus::Peer>) {
            std::cout << "[CLIENT] Received: " << nexus::message_type_to_string(msg.type) << std::endl;
            if (msg.type == nexus::MessageType::PONG) {
                received_response = true;
                std::cout << "[CLIENT] ✅ PONG received! Network works!" << std::endl;
            }
        });
        
        client.connect("127.0.0.1", port, "test-client");
        
        std::thread io_thread([&io_context]() {
            io_context.run_for(std::chrono::seconds(5));
        });
        
        io_thread.join();
        
        if (received_response) {
            std::cout << "\n✅ NETWORK TEST PASSED!" << std::endl;
        } else {
            std::cout << "\n❌ NETWORK TEST FAILED - No response received" << std::endl;
        }
        
        server_thread.join();
    }

    else if (command == "node") {
        if (argc < 4) {
            std::cerr << "Error: missing arguments" << std::endl;
            std::cout << "Usage: " << argv[0] << " node <p2p_port> <db_path> <metrics_port> [connect_to]" << std::endl;
            return 1;
        }
        
        int p2p_port = std::stoi(argv[2]);
        std::string dbPath = argv[3];
        int metrics_port = std::stoi(argv[4]);
        std::string connect_to = (argc > 5) ? argv[5] : "";
        
        std::cout << "=== Starting Nexus Node ===" << std::endl;
        std::cout << "Node ID: node_" << p2p_port << std::endl;
        std::cout << "P2P port: " << p2p_port << std::endl;
        std::cout << "Database: " << dbPath << std::endl;
        std::cout << "Metrics port: " << metrics_port << std::endl;
        if (!connect_to.empty()) {
            std::cout << "Connect to: " << connect_to << std::endl;
        }
        
        nexus::Node node(dbPath, p2p_port, metrics_port, "node_" + std::to_string(p2p_port));
        node.start();
        
        if (!connect_to.empty()) {
            size_t colon = connect_to.find(':');
            if (colon != std::string::npos) {
                std::string ip = connect_to.substr(0, colon);
                int port = std::stoi(connect_to.substr(colon + 1));
                node.connectToPeer(ip, port);
            }
        }
        
        std::cout << "Node running. Press Enter to stop..." << std::endl;
        std::cin.get();
        node.stop();
    }

    else if (command == "sendtx") {
        if (argc < 6) {
            std::cerr << "Usage: " << argv[0] << " sendtx <node_port> <from> <to> <amount>" << std::endl;
            return 1;
        }
        
        int node_port = std::stoi(argv[2]);
        std::string from = argv[3];
        std::string to = argv[4];
        double amount = std::stod(argv[5]);
        
        // Подключаемся напрямую к ноде через сокет (P2P протокол)
        try {
            boost::asio::io_context io_context;
            boost::asio::ip::tcp::socket socket(io_context);
            boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::make_address("127.0.0.1"), node_port);
            
            socket.connect(endpoint);
            
            // Формируем P2P сообщение типа NEW_TRANSACTION
            nlohmann::json msg;
            msg["type"] = 7;  // NEW_TRANSACTION
            msg["sender_id"] = "cli_sender";
            msg["timestamp"] = time(nullptr);
            msg["payload"] = {
                {"from", from},
                {"to", to},
                {"amount", amount}
            };
            
            std::string data = msg.dump() + "\n";
            boost::asio::write(socket, boost::asio::buffer(data));
            
            std::cout << "✅ Transaction sent to node " << node_port << std::endl;
            std::cout << "   From: " << from << " -> To: " << to << " Amount: " << amount << std::endl;
            
            socket.close();
            
        } catch (const std::exception& e) {
            std::cerr << "❌ Failed to send transaction: " << e.what() << std::endl;
            return 1;
        }
    }

    else {
        std::cerr << "Unknown command: " << command << std::endl;
        printUsage(argv[0]);
        return 1;
    }
    
    return 0;
}
