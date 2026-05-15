// src/network/peer.cpp
#include "peer.h"
#include <iostream>

namespace nexus {

Peer::Peer(boost::asio::io_context& io_context)
    : state(PeerState::DISCONNECTED),
      socket(std::make_unique<boost::asio::ip::tcp::socket>(io_context)),
      last_seen(0),
      failed_attempts(0) {
}

Peer::~Peer() {
    disconnect();
}

void Peer::connect(const std::string& addr, int port) {
    if (state != PeerState::DISCONNECTED) {
        std::cout << "⚠️ Peer already connecting, ignoring connect request" << std::endl;
        return;
    }
    
    address = addr;
    this->port = port;
    state = PeerState::CONNECTING;
    
    std::cout << "🔌 Connecting to " << addr << ":" << port << "..." << std::endl;
    
    // Копируем нужные значения для лямбды
    std::string target_addr = addr;
    int target_port = port;
    
    boost::asio::ip::tcp::endpoint endpoint(
        boost::asio::ip::make_address(target_addr), 
        static_cast<unsigned short>(target_port)
    );
    
    socket->async_connect(endpoint, 
        [this, target_addr, target_port](const boost::system::error_code& error) {
            if (!error) {
                state = PeerState::CONNECTED;
                last_seen = time(nullptr);
                std::cout << "✅ Connected to " << target_addr << ":" << target_port << std::endl;
            } else {
                state = PeerState::DISCONNECTED;
                failed_attempts++;
                std::cout << "❌ Failed to connect to " << target_addr << ":" << target_port 
                          << " - " << error.message() << std::endl;
            }
        });
}

void Peer::disconnect() {
    if (socket && socket->is_open()) {
        boost::system::error_code ec;
        socket->close(ec);
    }
    state = PeerState::DISCONNECTED;
    std::cout << "🔌 Disconnected from " << get_endpoint() << std::endl;
}

bool Peer::is_connected() const {
    return socket && socket->is_open() && 
           (state == PeerState::CONNECTED || state == PeerState::READY);
}

std::string Peer::get_endpoint() const {
    return address + ":" + std::to_string(port);
}

void Peer::send(const std::string& data) {
    if (!is_connected()) {
        std::cout << "⚠️ Cannot send to " << get_endpoint() << " - not connected" << std::endl;
        return;
    }
    
    std::string message = data + "\n";
    
    boost::asio::async_write(*socket, boost::asio::buffer(message),
        [this](const boost::system::error_code& error, size_t bytes) {
            if (error) {
                std::cout << "❌ Send error to " << get_endpoint() 
                          << ": " << error.message() << std::endl;
                disconnect();
            } else {
                last_seen = time(nullptr);
            }
        });
}

void Peer::read(std::function<void(const std::string&)> callback) {
    if (!is_connected()) {
        return;
    }
    
    // УБИРАЕМ shared_from_this() - не нужно
    boost::asio::async_read_until(*socket, read_buffer_, '\n',
        [this, callback](const boost::system::error_code& error, size_t bytes) {
            if (!error) {
                std::string data;
                std::istream is(&read_buffer_);
                std::getline(is, data);
                read_buffer_.consume(bytes);
                
                if (!data.empty()) {
                    last_seen = time(nullptr);
                    callback(data);
                }
                
                // Продолжаем чтение
                read(callback);
            } else if (error != boost::asio::error::operation_aborted) {
                std::cout << "❌ Read error: " << error.message() << std::endl;
                disconnect();
            }
        });
}

} // namespace nexus