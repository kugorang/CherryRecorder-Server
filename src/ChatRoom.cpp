#include "ChatRoom.hpp"
#include "ChatSession.hpp" // Need full definition for deliver()
#include "spdlog/spdlog.h"
#include <set>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm> // for std::remove

//------------------------------------------------------------------------------
// ChatRoom Implementation
//------------------------------------------------------------------------------
ChatRoom::ChatRoom(const std::string& name)
    : name_(name), max_participants_(100) // 예시로 최대 인원 100명
{
    spdlog::info("ChatRoom '{}' created.", name_);
}

void ChatRoom::join(SessionPtr participant)
{
    if (participants_.size() >= max_participants_) {
        participant->deliver("Error: 방 '" + name_ + "'이(가) 꽉 찼습니다.\r\n");
        return;
    }
    participants_.insert(participant);
    participant->set_current_room(name_); // 세션에 현재 방 이름 설정
    
    std::string join_msg = "* " + participant->nickname() + "님이 '" + name_ + "' 방에 입장했습니다.\r\n";
    broadcast(join_msg, nullptr); // 모두에게 알림 (입장한 사람 포함)
}

void ChatRoom::leave(SessionPtr participant)
{
    size_t erased_count = participants_.erase(participant);
    if (erased_count > 0) {
        participant->set_current_room(""); // 세션의 현재 방 이름 초기화
        
        std::string leave_msg = "* " + participant->nickname() + "님이 '" + name_ + "' 방에서 나갔습니다.\r\n";
        broadcast(leave_msg, nullptr);
    }
}

void ChatRoom::broadcast(const std::string& message, SessionPtr sender)
{
    std::string formatted_message = "[" + name_ + "] " + message + "\r\n";
    
    for (const auto& participant : participants_) {
        if (participant != sender) { // 메시지를 보낸 사람 제외
            participant->deliver(formatted_message);
        }
    }
}

std::vector<std::string> ChatRoom::get_participant_nicknames() const
{
    std::vector<std::string> nicknames;
    nicknames.reserve(participants_.size());
    for (const auto& p : participants_) {
        nicknames.push_back(p->nickname());
    }
    return nicknames;
}

bool ChatRoom::is_full() const
{
    return participants_.size() >= max_participants_;
}

// sessions() 함수는 헤더에 이미 인라인으로 정의되어 있으므로 여기서는 구현하지 않음