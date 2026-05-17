// src/network/client.h
#pragma once
#include <boost/asio.hpp>
#include <memory>
#include <functional>
#include "peer.h"
#include "message.h"

namespace nexus {

class Client {
public:
    using MessageHandler = std::function<void(const Message&, std::shared_ptr<Peer>)>;
    using ConnectionHandler = std::function<void(bool)>;

private:
    boost::asio::io_context& io_context_;
    std::shared_ptr<Peer> peer_;
    MessageHandler message_handler_;
    ConnectionHandler connection_handler_;
    bool is_connecting_{false};
    
public:
    explicit Client(boost::asio::io_context& io_context);
    
    void set_message_handler(MessageHandler handler) { message_handler_ = std::move(handler); }
    void set_connection_handler(ConnectionHandler handler) { connection_handler_ = std::move(handler); }
    
    bool connect(const std::string& address, int port, const std::string& node_id = "");
    void disconnect();
    void send(const Message& msg);
    
    bool is_connected() const { return peer_ && peer_->is_connected(); }
    bool is_connecting() const { return is_connecting_; }
    std::shared_ptr<Peer> get_peer() const { return peer_; }
    void set_peer(std::shared_ptr<Peer> peer) { peer_ = peer; }
};

} // namespace nexus