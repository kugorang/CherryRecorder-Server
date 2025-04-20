// include/ChatRoom.hpp
#pragma once

#include <string>
#include <set>
#include <memory> // for shared_ptr and weak_ptr
#include <mutex>
#include <vector>

// Forward declaration
class ChatSession;

/**
 * @class ChatRoom
 * @brief 채팅방을 관리하는 클래스
 */
class ChatRoom {
public:
    explicit ChatRoom(const std::string& name);
    ~ChatRoom();
    
    void broadcast(const std::string& message, const std::shared_ptr<ChatSession>& sender = nullptr);
    
    std::string name() const {
        return name_;
    }

    void add_participant(std::shared_ptr<ChatSession> participant);
    void remove_participant(std::shared_ptr<ChatSession> participant);
    bool empty() const;

    std::set<std::weak_ptr<ChatSession>, std::owner_less<std::weak_ptr<ChatSession>>> sessions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return sessions_;
    }

private:
    std::string name_;
    mutable std::mutex mutex_;
    std::set<std::weak_ptr<ChatSession>, std::owner_less<std::weak_ptr<ChatSession>>> sessions_;
};