// src/network/server.h
#pragma once
#include <boost/asio.hpp>
#include <memory>
#include <functional>
#include <vector>
#include "peer.h"
#include "message.h"

namespace nexus {

class Server {
public:
    using MessageHandler = std::function<void(const Message&, std::shared_ptr<Peer>)>;
    using ConnectionHandler = std::function<void(std::shared_ptr<Peer>)>;

private:
    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::vector<std::shared_ptr<Peer>> clients_;
    MessageHandler message_handler_;
    ConnectionHandler connection_handler_;
    bool running_{false};
    
    void start_accept();
    void handle_accept(std::shared_ptr<Peer> peer, const boost::system::error_code& error);

public:
    Server(boost::asio::io_context& io_context, int port);
    ~Server();
    
    void set_message_handler(MessageHandler handler) { message_handler_ = std::move(handler); }
    void set_connection_handler(ConnectionHandler handler) { connection_handler_ = std::move(handler); }
    
    void start();
    void stop();
    
    void broadcast(const Message& msg, std::shared_ptr<Peer> exclude = nullptr);
    void send_to_peer(const Message& msg, std::shared_ptr<Peer> peer);
    
    size_t get_client_count() const { return clients_.size(); }
    const std::vector<std::shared_ptr<Peer>>& get_clients() const { return clients_; }
    void remove_peer(std::shared_ptr<Peer> peer);
};

} // namespace nexus