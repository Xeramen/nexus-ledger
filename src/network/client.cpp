// src/network/client.cpp
#include "client.h"
#include <iostream>

namespace nexus {

Client::Client(boost::asio::io_context& io_context) 
    : io_context_(io_context), peer_(nullptr), is_connecting_(false) {}

bool Client::connect(const std::string& address, int port, const std::string& node_id) {
    if (is_connected() || is_connecting_) {
        std::cout << "⚠️ Already connected or connecting to " 
                  << (peer_ ? peer_->get_endpoint() : "unknown") << std::endl;
        return false;
    }
    
    is_connecting_ = true;
    
    peer_ = std::make_shared<Peer>(io_context_);
    peer_->id = node_id;
    
    // Устанавливаем обработчик сообщений для чтения
    peer_->read([this](const std::string& data) {
        try {
            Message msg = Message::deserialize(data);
            if (message_handler_) {
                message_handler_(msg, peer_);
            }
        } catch (const std::exception& e) {
            std::cout << "❌ Error parsing message: " << e.what() << std::endl;
        }
    });
    
    try {
        boost::asio::ip::tcp::endpoint endpoint(
            boost::asio::ip::make_address(address), 
            static_cast<unsigned short>(port));
        
        boost::system::error_code ec;
        peer_->socket->connect(endpoint, ec);
        
        if (!ec) {
            peer_->state = PeerState::CONNECTED;
            peer_->address = address;
            peer_->port = port;
            peer_->update_last_seen();
            
            // НАЧИНАЕМ ЧИТАТЬ ОТВЕТЫ
            peer_->read([this](const std::string& data) {
                try {
                    Message msg = Message::deserialize(data);
                    if (message_handler_) {
                        message_handler_(msg, peer_);
                    }
                } catch (const std::exception& e) {
                    std::cout << "❌ Error parsing message: " << e.what() << std::endl;
                }
            });
            return true;
        } else {
            std::cout << "❌ Connection failed to " << address << ":" << port 
                      << " - " << ec.message() << std::endl;
            is_connecting_ = false;
            peer_.reset();
            
            if (connection_handler_) {
                connection_handler_(false);
            }
            return false;
        }
    } catch (const std::exception& e) {
        std::cout << "❌ Exception during connection: " << e.what() << std::endl;
        is_connecting_ = false;
        peer_.reset();
        return false;
    }
}

void Client::disconnect() {
    if (peer_) {
        peer_->disconnect();
        peer_.reset();
    }
    is_connecting_ = false;
}

void Client::send(const Message& msg) {
    if (peer_ && peer_->is_connected()) {
        peer_->send(msg.serialize());
    } else {
        std::cout << "⚠️ Cannot send message to " 
                  << (peer_ ? peer_->get_endpoint() : "unknown") 
                  << " - not connected" << std::endl;
    }
}

} // namespace nexus