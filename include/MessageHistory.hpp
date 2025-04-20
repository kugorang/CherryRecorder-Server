// include/MessageHistory.hpp
#pragma once

#include <string>
#include <vector>
#include <memory> // Needed for unique_ptr if used elsewhere, or just general practice

class MessageHistory {
private:
    std::string history_dir_;
    bool enabled_ = false;
public:
    explicit MessageHistory(const std::string& history_dir);
    ~MessageHistory();

    void log_global_message(const std::string& message, const std::string& sender = "");
    void log_private_message(const std::string& message, const std::string& sender, const std::string& receiver);
    void log_room_message(const std::string& room_name, const std::string& message, const std::string& sender = "");

    std::vector<std::string> load_global_history(size_t limit = 0);
    std::vector<std::string> load_private_history(const std::string& user1, const std::string& user2, size_t limit = 0);
    std::vector<std::string> load_room_history(const std::string& room_name, size_t limit = 0);

    bool is_enabled() const { return enabled_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }
};
