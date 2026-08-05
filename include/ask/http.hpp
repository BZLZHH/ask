#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ask {

struct HttpResponse {
  long status{0};
  std::string body;
  std::map<std::string, std::string> headers;
};

class RequestCancelled : public std::runtime_error {
 public:
  RequestCancelled() : std::runtime_error("request cancelled") {}
};

class HttpClient {
 public:
  HttpClient();
  ~HttpClient();

  HttpResponse request(const std::string& method,
                       const std::string& url,
                       const std::vector<std::string>& headers = {},
                       const std::string& body = {},
                       int timeout_seconds = 120,
                       std::size_t max_bytes = 16ULL * 1024 * 1024,
                       bool follow_redirects = true,
                       const std::vector<std::string>& resolve = {}) const;

  HttpResponse request_stream(
      const std::string& method,
      const std::string& url,
      const std::vector<std::string>& headers,
      const std::string& body,
      const std::function<bool(std::string_view)>& on_chunk,
      int timeout_seconds = 120,
      std::size_t max_bytes = 16ULL * 1024 * 1024,
      const std::atomic<int>* cancelled = nullptr) const;

  static std::string url_encode(const std::string& input);
};

}  // namespace ask
