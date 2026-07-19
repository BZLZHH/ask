#include "ask/http.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <mutex>
#include <stdexcept>

#include <curl/curl.h>

namespace ask {
namespace {

struct WriteContext {
  std::string* output;
  std::size_t maximum;
  bool overflow{false};
};

struct StreamContext {
  std::string* output;
  std::size_t maximum;
  const std::function<bool(std::string_view)>* callback;
  std::exception_ptr error;
  bool overflow{false};
  bool stopped{false};
};

size_t write_callback(char* data, size_t size, size_t count, void* userdata) {
  auto* context = static_cast<WriteContext*>(userdata);
  const auto bytes = size * count;
  if (context->output->size() + bytes > context->maximum) {
    context->overflow = true;
    return 0;
  }
  context->output->append(data, bytes);
  return bytes;
}

size_t stream_callback(char* data, size_t size, size_t count, void* userdata) {
  auto* context = static_cast<StreamContext*>(userdata);
  const auto bytes = size * count;
  if (context->output->size() + bytes > context->maximum) {
    context->overflow = true;
    return 0;
  }
  context->output->append(data, bytes);
  try {
    if (!(*context->callback)(std::string_view(data, bytes))) {
      context->stopped = true;
      return 0;
    }
  } catch (...) {
    context->error = std::current_exception();
    return 0;
  }
  return bytes;
}

int progress_callback(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  const auto* cancelled = static_cast<const volatile std::sig_atomic_t*>(userdata);
  return cancelled && *cancelled ? 1 : 0;
}

size_t header_callback(char* data, size_t size, size_t count, void* userdata) {
  auto* headers = static_cast<std::map<std::string, std::string>*>(userdata);
  std::string line(data, size * count);
  auto colon = line.find(':');
  if (colon == std::string::npos) return size * count;
  auto name = line.substr(0, colon);
  auto value = line.substr(colon + 1);
  auto trim = [](std::string& text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
  };
  trim(name);
  trim(value);
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  (*headers)[name] = value;
  return size * count;
}

void initialize_curl() {
  static std::once_flag flag;
  std::call_once(flag, [] {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
      throw std::runtime_error("failed to initialize libcurl");
    }
  });
}

}  // namespace

HttpClient::HttpClient() { initialize_curl(); }
HttpClient::~HttpClient() = default;

HttpResponse HttpClient::request(const std::string& method,
                                 const std::string& url,
                                 const std::vector<std::string>& headers,
                                 const std::string& body,
                                 int timeout_seconds,
                                 std::size_t max_bytes,
                                 bool follow_redirects,
                                 const std::vector<std::string>& resolve) const {
  CURL* curl = curl_easy_init();
  if (!curl) throw std::runtime_error("cannot create HTTP client");

  HttpResponse response;
  WriteContext context{&response.body, max_bytes};
  curl_slist* header_list = nullptr;
  for (const auto& header : headers) header_list = curl_slist_append(header_list, header.c_str());
  curl_slist* resolve_list = nullptr;
  for (const auto& entry : resolve) resolve_list = curl_slist_append(resolve_list, entry.c_str());

  char error[CURL_ERROR_SIZE] = {};
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, std::max(timeout_seconds, 1));
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, std::min(std::max(timeout_seconds, 1), 20));
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, follow_redirects ? 1L : 0L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
  if (resolve_list) curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve_list);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "ask/" ASK_VERSION);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  if (!body.empty() || method == "POST" || method == "PUT" || method == "PATCH") {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
  }

  auto code = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
  curl_slist_free_all(header_list);
  curl_slist_free_all(resolve_list);
  curl_easy_cleanup(curl);
  if (code != CURLE_OK) {
    if (context.overflow) throw std::runtime_error("HTTP response exceeded size limit");
    throw std::runtime_error(std::string("HTTP request failed: ") +
                             (error[0] ? error : curl_easy_strerror(code)));
  }
  return response;
}

HttpResponse HttpClient::request_stream(
    const std::string& method,
    const std::string& url,
    const std::vector<std::string>& headers,
    const std::string& body,
    const std::function<bool(std::string_view)>& on_chunk,
    int timeout_seconds,
    std::size_t max_bytes,
    const volatile std::sig_atomic_t* cancelled) const {
  CURL* curl = curl_easy_init();
  if (!curl) throw std::runtime_error("cannot create HTTP client");

  HttpResponse response;
  StreamContext context{&response.body, max_bytes, &on_chunk, nullptr, false, false};
  curl_slist* header_list = nullptr;
  for (const auto& header : headers) header_list = curl_slist_append(header_list, header.c_str());

  char error[CURL_ERROR_SIZE] = {};
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, std::max(timeout_seconds, 1));
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, std::min(std::max(timeout_seconds, 1), 20));
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "ask/" ASK_VERSION);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancelled);
  if (!body.empty() || method == "POST" || method == "PUT" || method == "PATCH") {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
  }

  const auto code = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
  curl_slist_free_all(header_list);
  curl_easy_cleanup(curl);

  if (context.error) std::rethrow_exception(context.error);
  if (cancelled && *cancelled) throw RequestCancelled();
  if (code != CURLE_OK) {
    if (context.overflow) throw std::runtime_error("HTTP response exceeded size limit");
    if (context.stopped) throw RequestCancelled();
    throw std::runtime_error(std::string("HTTP request failed: ") +
                             (error[0] ? error : curl_easy_strerror(code)));
  }
  return response;
}

std::string HttpClient::url_encode(const std::string& input) {
  initialize_curl();
  CURL* curl = curl_easy_init();
  if (!curl) throw std::runtime_error("cannot create URL encoder");
  char* encoded = curl_easy_escape(curl, input.c_str(), static_cast<int>(input.size()));
  if (!encoded) {
    curl_easy_cleanup(curl);
    throw std::runtime_error("URL encoding failed");
  }
  std::string result(encoded);
  curl_free(encoded);
  curl_easy_cleanup(curl);
  return result;
}

}  // namespace ask
