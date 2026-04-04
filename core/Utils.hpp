#include <iostream>
#include <string>

void log_info(const std::string& file, int line, const std::string& message) {
    std::cout << file << ":" << line << " " << message << std::endl;
}

void log_error(const std::string& file, int line, const std::string& message) {
    std::cerr << file << ":" << line << " " << message << std::endl;
}
