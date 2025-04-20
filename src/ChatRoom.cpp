#include "ChatRoom.hpp"
#include "ChatSession.hpp" // Need full definition for deliver()
#include "spdlog/spdlog.h"
#include <set>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

//------------------------------------------------------------------------------
// ChatRoom Implementation
//------------------------------------------------------------------------------
ChatRoom::ChatRoom(const std::string &name) : name_(name)
{
    spdlog::info("[ChatRoom] Room '{}' created.", name);
}

ChatRoom::~ChatRoom()
{
    spdlog::info("[ChatRoom] Room '{}' destroyed.", name_);
}

void ChatRoom::add_participant(std::shared_ptr<ChatSession> participant)
{
    if (!participant) return; // Avoid inserting null weak_ptr
    std::lock_guard<std::mutex> lock(mutex_);
    // Insert a weak_ptr created from the shared_ptr
    auto result = sessions_.insert(std::weak_ptr(participant));
    if (result.second)
    {
        spdlog::debug("ChatRoom[{}]: Participant added (weak_ptr)", name_);
    }
}

void ChatRoom::remove_participant(std::shared_ptr<ChatSession> participant)
{
    if (!participant) return;
    std::lock_guard<std::mutex> lock(mutex_);
    size_t initial_size = sessions_.size();

    // Use std::erase_if to remove the participant or any expired weak_ptrs
    // Capture 'this' to allow access to member variable 'name_' inside the lambda
    size_t num_erased = std::erase_if(sessions_, 
        [this, &participant](const std::weak_ptr<ChatSession>& weak_session) {
            if (weak_session.expired()) {
                // Access name_ via 'this' which is captured
                spdlog::warn("ChatRoom[{}]: Removing expired participant during erase_if.", this->name_);
                return true; // Remove expired pointers
            } else if (auto locked_session = weak_session.lock()) {
                return locked_session == participant; // Remove if it matches the target participant
            }
            return false; // Should not happen if not expired, but keep for safety
    });

    if (num_erased > 0)
    {
        spdlog::debug("ChatRoom[{}]: erase_if removed {} elements (participant or expired). Remaining: {}", 
                      name_, num_erased, sessions_.size());
    } else {
        spdlog::warn("ChatRoom[{}]: remove_participant called, but participant not found and no expired sessions removed.", name_);
    }
}

bool ChatRoom::empty() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    // Consider cleaning up expired weak_ptrs here too, or periodically
    return sessions_.empty(); // Basic check, might include expired pointers
}

void ChatRoom::broadcast(const std::string& message, const std::shared_ptr<ChatSession>& sender)
{
    spdlog::debug("[ChatRoom {}] Broadcasting message (sender: {}): {}", 
                  this->name_, 
                  sender ? fmt::ptr(sender.get()) : "system", 
                  message);

    std::vector<std::weak_ptr<ChatSession>> sessions_to_broadcast;
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        sessions_to_broadcast.assign(this->sessions_.begin(), this->sessions_.end());
    }

    for (const auto& session_weak : sessions_to_broadcast)
    {
        if (auto session = session_weak.lock())
        {
            // Check if the current session is NOT the sender
            if (session != sender) {
                session->deliver(message);
                spdlog::debug("[ChatRoom {}] Delivered message to session: {:p}", this->name_, static_cast<void*>(session.get()));
            } else {
                spdlog::debug("[ChatRoom {}] Skipping delivery to sender: {:p}", this->name_, static_cast<void*>(session.get()));
            }
        }
        else
        {
            spdlog::warn("[ChatRoom {}] Found an expired session weak_ptr during broadcast.", this->name_);
        }
    }
    spdlog::debug("[ChatRoom {}] Broadcast finished.", this->name_);
}

// sessions() 함수는 헤더에 이미 인라인으로 정의되어 있으므로 여기서는 구현하지 않음