// src/network/message.h
#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace nexus {

enum class MessageType {
    HANDSHAKE = 0,
    PING = 1,
    PONG = 2,
    GET_PEERS = 3,
    PEERS_LIST = 4,
    GET_BLOCKS = 5,
    BLOCKS_RESPONSE = 6,
    NEW_TRANSACTION = 7,
    NEW_BLOCK = 8,
    SYNC_REQUEST = 9,
    SYNC_RESPONSE = 10,
    ERROR = 99
};

// Вспомогательная функция для конвертации enum в строку (для логов)
inline std::string message_type_to_string(MessageType type) {
    switch(type) {
        case MessageType::HANDSHAKE: return "HANDSHAKE";
        case MessageType::PING: return "PING";
        case MessageType::PONG: return "PONG";
        case MessageType::GET_PEERS: return "GET_PEERS";
        case MessageType::PEERS_LIST: return "PEERS_LIST";
        case MessageType::GET_BLOCKS: return "GET_BLOCKS";
        case MessageType::BLOCKS_RESPONSE: return "BLOCKS_RESPONSE";
        case MessageType::NEW_TRANSACTION: return "NEW_TRANSACTION";
        case MessageType::NEW_BLOCK: return "NEW_BLOCK";
        case MessageType::SYNC_REQUEST: return "SYNC_REQUEST";
        case MessageType::SYNC_RESPONSE: return "SYNC_RESPONSE";
        default: return "UNKNOWN";
    }
}

struct Message {
    MessageType type;
    std::string sender_id;
    int64_t timestamp;
    nlohmann::json payload;
    
    Message() : timestamp(time(nullptr)) {}
    explicit Message(MessageType t) : type(t), timestamp(time(nullptr)) {}
    
    // Сериализация в JSON строку
    std::string serialize() const {
        nlohmann::json j;
        j["type"] = static_cast<int>(type);
        j["sender_id"] = sender_id;
        j["timestamp"] = timestamp;
        j["payload"] = payload;
        return j.dump();
    }
    
    // Десериализация из JSON строки
    static Message deserialize(const std::string& data) {
        Message msg;
        auto j = nlohmann::json::parse(data);
        msg.type = static_cast<MessageType>(j["type"].get<int>());
        msg.sender_id = j.value("sender_id", "");
        msg.timestamp = j.value("timestamp", time(nullptr));
        msg.payload = j.value("payload", nlohmann::json::object());
        return msg;
    }
    
    static Message create_handshake(const std::string& node_id, int port, const std::string& version = "1.0") {
        Message msg(MessageType::HANDSHAKE);
        msg.sender_id = node_id;
        msg.payload = {
            {"port", port},
            {"version", version},
            {"node_id", node_id}
        };
        return msg;
    }
    
    static Message create_ping(const std::string& node_id) {
        Message msg(MessageType::PING);
        msg.sender_id = node_id;
        return msg;
    }
    
    static Message create_pong(const std::string& node_id) {
        Message msg(MessageType::PONG);
        msg.sender_id = node_id;
        return msg;
    }
    
    static Message create_get_peers(const std::string& node_id) {
        Message msg(MessageType::GET_PEERS);
        msg.sender_id = node_id;
        return msg;
    }
    
    static Message create_peers_list(const std::string& node_id, 
                                      const std::vector<std::pair<std::string, int>>& peers) {
        Message msg(MessageType::PEERS_LIST);
        msg.sender_id = node_id;
        
        nlohmann::json peers_json = nlohmann::json::array();
        for (const auto& [ip, port] : peers) {
            peers_json.push_back({{"ip", ip}, {"port", port}});
        }
        msg.payload = peers_json;
        return msg;
    }
    
    static Message create_get_blocks(const std::string& node_id, int from_height) {
        Message msg(MessageType::GET_BLOCKS);
        msg.sender_id = node_id;
        msg.payload = {{"from_height", from_height}};
        return msg;
    }
    
    static Message create_new_transaction(const std::string& node_id, const nlohmann::json& tx_json) {
        Message msg(MessageType::NEW_TRANSACTION);
        msg.sender_id = node_id;
        msg.payload = tx_json;
        return msg;
    }
    
    static Message create_new_block(const std::string& node_id, const nlohmann::json& block_json) {
        Message msg(MessageType::NEW_BLOCK);
        msg.sender_id = node_id;
        msg.payload = block_json;
        return msg;
    }
    
    static Message create_error(const std::string& node_id, const std::string& error_msg) {
        Message msg(MessageType::ERROR);
        msg.sender_id = node_id;
        msg.payload = {{"error", error_msg}};
        return msg;
    }
};

} // namespace nexus