// src/network/peer.h
#pragma once
#include <string>
#include <memory>
#include <functional>
#include <boost/asio.hpp>

namespace nexus {

enum class PeerState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    HANDSHAKE,
    READY
};

class Peer : public std::enable_shared_from_this<Peer> {
public:
    // Данные пира
    std::string id;
    std::string address;
    int port;
    int p2p_port;
    PeerState state;
    std::unique_ptr<boost::asio::ip::tcp::socket> socket;
    time_t last_seen;
    int failed_attempts;
    
    explicit Peer(boost::asio::io_context& io_context);
    ~Peer();
    
    // Соединение
    void connect(const std::string& addr, int port);
    void disconnect();
    bool is_connected() const;
    bool is_ready() const { return state == PeerState::READY; }
    
    // Отправка/получение данных
    void send(const std::string& data);
    void read(std::function<void(const std::string&)> callback);
    
    // Вспомогательные методы
    std::string get_endpoint() const;
    void update_last_seen() { last_seen = time(nullptr); }
    
private:
    boost::asio::streambuf read_buffer_;
};

} // namespace nexus