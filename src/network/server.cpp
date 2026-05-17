// src/network/server.cpp
#include "server.h"
#include <iostream>

namespace nexus {

Server::Server(boost::asio::io_context& io_context, int port)
    : io_context_(io_context),
      acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)) {
    std::cout << "🌐 Server listening on port " << port << std::endl;
}

Server::~Server() {
    stop();
}

void Server::start() {
    running_ = true;
    start_accept();
}

void Server::stop() {
    running_ = false;
    
    boost::system::error_code ec;
    acceptor_.close(ec);
    
    for (auto& client : clients_) {
        client->disconnect();
    }
    clients_.clear();
    
    std::cout << "🛑 Server stopped" << std::endl;
}

void Server::start_accept() {
    if (!running_) return;
    
    auto peer = std::make_shared<Peer>(io_context_);
    
    acceptor_.async_accept(*peer->socket,
        [this, peer](const boost::system::error_code& error) {
            handle_accept(peer, error);
        });
}

void Server::handle_accept(std::shared_ptr<Peer> peer, const boost::system::error_code& error) {
    if (!error) {
        // Проверяем первые байты на HTTP заголовки
        peer->read([this, peer](const std::string& data) {
            // Игнорируем HTTP запросы (они идут на P2P порт по ошибке)
            if (data.size() > 0 && (data[0] == 'P' || data[0] == 'G' || data[0] == 'H')) {
                if (data.find("POST") == 0 || data.find("GET") == 0 || data.find("HTTP") == 0) {
                    // Убираем вывод, чтобы не засорять логи
                    return;
                }
            }
            try {
                Message msg = Message::deserialize(data);
                if (message_handler_) {
                    message_handler_(msg, peer);
                }
            } catch (const std::exception& e) {
                // Убираем вывод ошибки для HTTP запросов
            }
        });
        peer->state = PeerState::CONNECTED;
        peer->address = peer->socket->remote_endpoint().address().to_string();
        peer->port = peer->socket->remote_endpoint().port();
        peer->update_last_seen();
        
        clients_.push_back(peer);
        
        std::cout << "✅ New incoming connection from " << peer->get_endpoint() << std::endl;
        
        if (connection_handler_) {
            connection_handler_(peer);
        }
        
        // УБИРАЕМ ЭТУ СТРОКУ - read() будет вызван в connection_handler
        // peer->read([this, peer](const std::string& data) { ... });
        
    } else {
        std::cout << "❌ Accept error: " << error.message() << std::endl;
    }
    
    start_accept();
}

void Server::broadcast(const Message& msg, std::shared_ptr<Peer> exclude) {
    std::string data = msg.serialize();
    
    for (auto& client : clients_) {
        if (client != exclude && client->is_connected()) {
            client->send(data);
        }
    }
}

void Server::send_to_peer(const Message& msg, std::shared_ptr<Peer> peer) {
    if (peer && peer->is_connected()) {
        peer->send(msg.serialize());
    }
}

void Server::remove_peer(std::shared_ptr<Peer> peer) {
    auto it = std::find(clients_.begin(), clients_.end(), peer);
    if (it != clients_.end()) {
        clients_.erase(it);
        std::cout << "🔌 Removed peer " << peer->get_endpoint() << std::endl;
    }
}

} // namespace nexus