// include/ChatRoom.hpp
#pragma once

#include <string>
#include <memory>
#include <set>
#include <vector>
#include "SessionInterface.hpp" // ChatSession.hpp 대신 SessionInterface.hpp 포함

class ChatRoom {
private:
    std::string name_;
    std::set<SessionPtr> participants_; // SessionPtr 사용
    size_t max_participants_;
    // ChatServer에 대한 참조가 필요하다면 추가
    // ChatServer& server_; 

public:
    explicit ChatRoom(const std::string& name);
    void join(SessionPtr participant);
    void leave(SessionPtr participant);
    void add_participant(SessionPtr participant) { join(participant); }  // alias for join
    void remove_participant(SessionPtr participant) { leave(participant); } // alias for leave
    void broadcast(const std::string& message, SessionPtr sender);
    std::vector<std::string> get_participant_nicknames() const;
    bool is_full() const;
    bool empty() const { return participants_.empty(); }
    const std::string& name() const { return name_; }
    size_t participant_count() const { return participants_.size(); }
    const std::set<SessionPtr>& sessions() const { return participants_; }
};