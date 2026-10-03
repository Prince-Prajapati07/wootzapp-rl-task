#include <arpa/inet.h>
#include <csignal>
#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <netdb.h>
#include <openssl/sha.h>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include "json.hpp"

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

struct UrlParts { std::string host, path; int port; };

static pid_t chrome_pid = -1;
bool debug_on() { return getenv("DEBUG_ENV") != nullptr; }
void debug(const std::string& s) { if (debug_on()) std::cerr << "[env] " << s << std::endl; }

std::string b64(const unsigned char* data, int len) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (int i = 0; i < len; i += 3) {
        int v = data[i] << 16;
        if (i + 1 < len) v |= data[i + 1] << 8;
        if (i + 2 < len) v |= data[i + 2];
        out.push_back(table[(v >> 18) & 63]);
        out.push_back(table[(v >> 12) & 63]);
        out.push_back(i + 1 < len ? table[(v >> 6) & 63] : '=');
        out.push_back(i + 2 < len ? table[v & 63] : '=');
    }
    return out;
}

int connect_tcp(const std::string& host, int port) {
    addrinfo hints{}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    std::string port_s = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res) != 0) throw std::runtime_error("getaddrinfo failed");
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) throw std::runtime_error("socket failed");
    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) throw std::runtime_error("connect failed");
    freeaddrinfo(res);
    return fd;
}

int find_free_port() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("socket failed");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) throw std::runtime_error("bind failed");
    socklen_t len = sizeof(addr);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) throw std::runtime_error("getsockname failed");
    int port = ntohs(addr.sin_port);
    close(fd);
    return port;
}

std::string http_request(const std::string& method, const std::string& path, int port) {
    int fd = connect_tcp("127.0.0.1", port);
    timeval tv{3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    std::string req = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    send(fd, req.data(), req.size(), 0);
    std::string data;
    char buf[4096];
    int n;
    int content_len = -1;
    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) {
        data.append(buf, n);
        auto split = data.find("\r\n\r\n");
        if (split != std::string::npos && content_len < 0) {
            std::string head = data.substr(0, split);
            auto p = head.find("Content-Length:");
            if (p != std::string::npos) content_len = std::stoi(head.substr(p + 15));
        }
        auto split2 = data.find("\r\n\r\n");
        if (split2 != std::string::npos && content_len >= 0 && (int)(data.size() - split2 - 4) >= content_len) break;
    }
    close(fd);
    auto pos = data.find("\r\n\r\n");
    return pos == std::string::npos ? data : data.substr(pos + 4);
}

UrlParts parse_ws_url(const std::string& url) {
    std::string s = url.substr(5);
    auto slash = s.find('/');
    std::string hp = s.substr(0, slash);
    auto colon = hp.find(':');
    return {hp.substr(0, colon), s.substr(slash), std::stoi(hp.substr(colon + 1))};
}

class WebSocket {
    int fd = -1;
    bool read_exact(unsigned char* dst, size_t len) {
        size_t got = 0;
        auto deadline = Clock::now() + std::chrono::seconds(90);
        while (got < len && Clock::now() < deadline) {
            int n = recv(fd, dst + got, len - got, 0);
            if (n > 0) {
                got += n;
                continue;
            }
            if (n == 0) return false;
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            return false;
        }
        return got == len;
    }
public:
    void connect_url(const std::string& url) {
        UrlParts u = parse_ws_url(url);
        fd = connect_tcp(u.host, u.port);
        timeval tv{5, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
        std::ostringstream req;
        req << "GET " << u.path << " HTTP/1.1\r\n"
            << "Host: " << u.host << ":" << u.port << "\r\n"
            << "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            << "Sec-WebSocket-Key: " << key << "\r\n"
            << "Sec-WebSocket-Version: 13\r\n\r\n";
        std::string r = req.str();
        send(fd, r.data(), r.size(), 0);
        std::string head;
        char c;
        while (head.find("\r\n\r\n") == std::string::npos && recv(fd, &c, 1, 0) == 1) head.push_back(c);
        if (head.find("101") == std::string::npos) throw std::runtime_error("websocket handshake failed");
    }
    void send_text(const std::string& s) {
        std::vector<unsigned char> f;
        f.push_back(0x81);
        if (s.size() < 126) f.push_back(0x80 | s.size());
        else { f.push_back(0x80 | 126); f.push_back((s.size() >> 8) & 255); f.push_back(s.size() & 255); }
        unsigned char mask[4] = {1, 2, 3, 4};
        f.insert(f.end(), mask, mask + 4);
        for (size_t i = 0; i < s.size(); i++) f.push_back(s[i] ^ mask[i % 4]);
        ::send(fd, f.data(), f.size(), 0);
    }
    std::string recv_text() {
        unsigned char h[2];
        if (!read_exact(h, 2)) throw std::runtime_error("websocket read failed");
        int opcode = h[0] & 15;
        uint64_t len = h[1] & 127;
        if (len == 126) { unsigned char e[2]; if (!read_exact(e, 2)) throw std::runtime_error("websocket length read failed"); len = (e[0] << 8) | e[1]; }
        else if (len == 127) {
            unsigned char e[8]; if (!read_exact(e, 8)) throw std::runtime_error("websocket length read failed"); len = 0;
            for (int i = 0; i < 8; i++) len = (len << 8) | e[i];
        }
        bool masked = h[1] & 128;
        unsigned char mask[4] = {0,0,0,0};
        if (masked && !read_exact(mask, 4)) throw std::runtime_error("websocket mask read failed");
        std::string out(len, '\0');
        if (len > 0 && !read_exact(reinterpret_cast<unsigned char*>(out.data()), len)) throw std::runtime_error("websocket payload read failed");
        if (masked) for (size_t i = 0; i < out.size(); i++) out[i] ^= mask[i % 4];
        if (opcode == 8) throw std::runtime_error("websocket closed");
        return out;
    }
    void close_ws() { if (fd >= 0) close(fd); fd = -1; }
};

class Env {
    WebSocket ws;
    int next_id = 1;
    int step_no = 0;
    int episode = 0;
    int cur_seed = 0;
    std::string site_path, log_path, goal;
    std::ofstream log;
    json last_obs = json::object();
public:
    Env(std::string site, std::string lp) : site_path(std::move(site)), log_path(std::move(lp)) {
        if (!log_path.empty()) log.open(log_path, std::ios::app);
    }
    void start() {
        int pipefd[2];
        if (pipe(pipefd) != 0) throw std::runtime_error("pipe failed");
        int debug_port = find_free_port();
        chrome_pid = fork();
        if (chrome_pid == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], 1);
            dup2(pipefd[1], 2);
            std::string profile = "--user-data-dir=/tmp/minishop-cdp-" + std::to_string(getpid());
            std::string port_arg = "--remote-debugging-port=" + std::to_string(debug_port);
            execlp("google-chrome", "google-chrome", "--headless=new", "--disable-gpu", "--no-sandbox",
                  "--disable-dev-shm-usage", port_arg.c_str(), profile.c_str(), "about:blank", nullptr);
            execlp("chromium-browser", "chromium-browser", "--headless=new", "--disable-gpu", "--no-sandbox",
                  "--disable-dev-shm-usage", port_arg.c_str(), profile.c_str(), "about:blank", nullptr);
            execlp("chromium", "chromium", "--headless=new", "--disable-gpu", "--no-sandbox",
                  "--disable-dev-shm-usage", port_arg.c_str(), profile.c_str(), "about:blank", nullptr);
            _exit(1);
        }
        close(pipefd[1]);
        fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
        std::string output, browser_ws;
        auto deadline = Clock::now() + std::chrono::seconds(90);
        while (Clock::now() < deadline) {
            try {
                std::string body = http_request("GET", "/json/version", debug_port);
                std::string from_http = json::parse(body).value("webSocketDebuggerUrl", "");
                if (!from_http.empty()) {
                    browser_ws = from_http;
                    break;
                }
            } catch (...) {}
            char buf[512];
            int n = read(pipefd[0], buf, sizeof(buf));
            if (n > 0) {
                output.append(buf, n);
                auto pos = output.find("DevTools listening on ");
                if (pos != std::string::npos) {
                    auto start = pos + std::string("DevTools listening on ").size();
                    auto end = output.find('\n', start);
                    if (end != std::string::npos) {
                        browser_ws = output.substr(start, end - start);
                        break;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        close(pipefd[0]);
        if (browser_ws.empty()) throw std::runtime_error("chromium did not print DevTools URL: " + output);
        if (browser_ws.find("127.0.0.1/") != std::string::npos) {
            browser_ws.replace(browser_ws.find("127.0.0.1/"), 10, "127.0.0.1:" + std::to_string(debug_port) + "/");
        }
        debug("browser ws " + browser_ws);
        debug("http new on port " + std::to_string(debug_port));
        std::string body = http_request("PUT", "/json/new?about:blank", debug_port);
        debug("new body " + body.substr(0, 160));
        std::string ws_url = json::parse(body).value("webSocketDebuggerUrl", "");
        if (ws_url.find("127.0.0.1/") != std::string::npos) {
            ws_url.replace(ws_url.find("127.0.0.1/"), 10, "127.0.0.1:" + std::to_string(debug_port) + "/");
        }
        debug("page ws " + ws_url);
        ws.connect_url(ws_url);
        debug("ws connected");
        cdp("Runtime.enable");
        debug("runtime enabled");
        cdp("Page.enable");
        debug("page enabled");
    }
    json cdp(const std::string& method, json params = json::object()) {
        int id = next_id++;
        ws.send_text(json{{"id", id}, {"method", method}, {"params", params}}.dump());
        while (true) {
            json msg = json::parse(ws.recv_text());
            if (msg.contains("id") && msg["id"] == id) return msg;
        }
    }
    json eval(const std::string& expr) {
        auto r = cdp("Runtime.evaluate", {{"expression", expr}, {"returnByValue", true}});
        return r["result"]["result"].value("value", json());
    }
    json observe() {
        std::string script = R"JS((() => {
 const s = window.__shop || {};
 const blocked = !!s.popupVisible;
 const buttons = Array.from(document.querySelectorAll('button')).filter(b => {
   const r = b.getBoundingClientRect();
   const st = getComputedStyle(b);
   return r.width > 0 && r.height > 0 && st.visibility !== 'hidden' && st.display !== 'none';
 }).map((b, i) => {
   const r = b.getBoundingClientRect();
   return {i, text:b.textContent.trim(), clickable:blocked ? b.id === 'dismiss' : !b.disabled, x:r.left+r.width/2, y:r.top+r.height/2};
 });
 const goalEl = document.getElementById("goal");
 return {screen:s.screen || "unknown", goal:goalEl ? goalEl.textContent.replace("Goal: ","") : "",
         visibleText:document.body ? document.body.innerText : "",
         buttons, popup:blocked, orderPlaced:!!s.orderPlaced, orderItem:s.orderItem || "", orderQty:s.orderQty || 0};
})())JS";
        last_obs = eval(script);
        return last_obs;
    }
    json reset(const json& req) {
        episode++;
        step_no = 0;
        auto task = req["task"];
        cur_seed = req.value("seed", 1);
        goal = task["item"].get<std::string>() + " x" + std::to_string(task["qty"].get<int>());
        std::ostringstream url;
        url << "file://" << site_path << "?item=" << task["item"].get<std::string>()
            << "&qty=" << task["qty"].get<int>() << "&seed=" << cur_seed
            << "&popup_p=" << req.value("popup_p", 0.15) << "&delay_p=" << req.value("delay_p", 0.0);
        cdp("Page.navigate", {{"url", url.str()}});
        json obs;
        for (int i = 0; i < 30; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            obs = observe();
            if (!obs.is_null() && obs.value("screen", "unknown") != "unknown" && obs.value("goal", "") == goal) return obs;
        }
        return obs;
    }
    json step(const std::string& action) {
        auto start = Clock::now();
        std::string err;
        if (action.rfind("click(", 0) == 0) {
            int i = std::stoi(action.substr(6));
            if (last_obs.contains("buttons") && i >= 0 && i < (int)last_obs["buttons"].size() && last_obs["buttons"][i].value("clickable", false)) {
                double x = last_obs["buttons"][i]["x"], y = last_obs["buttons"][i]["y"];
                cdp("Input.dispatchMouseEvent", {{"type", "mousePressed"}, {"x", x}, {"y", y}, {"button", "left"}, {"clickCount", 1}});
                cdp("Input.dispatchMouseEvent", {{"type", "mouseReleased"}, {"x", x}, {"y", y}, {"button", "left"}, {"clickCount", 1}});
            } else err = "invalid_or_unclickable_action";
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        json obs = observe();
        step_no++;
        bool placed = obs.value("orderPlaced", false);
        bool correct = placed && (obs.value("orderItem", "") + " x" + std::to_string(obs.value("orderQty", 0)) == goal);
        bool done = placed;
        bool truncated = step_no >= 20 && !done;
        double reward = placed ? (correct ? 1.0 : -1.0) : -0.01;
        long ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        json out = obs;
        out["reward"] = reward; out["done"] = done; out["truncated"] = truncated; out["step_ms"] = ms;
        out["info"] = err.empty() ? json::object() : json{{"error", err}};
        if (log) {
            log << json{{"episode", episode}, {"seed", cur_seed}, {"goal", goal}, {"step", step_no}, {"observation", obs},
                        {"action", action}, {"reward", reward}, {"done", done}, {"truncated", truncated},
                        {"step_ms", ms}, {"popup_visible", obs.value("popup", false)}}.dump() << "\n";
            log.flush();
        }
        return out;
    }
    void close_env() { ws.close_ws(); if (log) log.close(); }
};

void cleanup(int) {
    if (chrome_pid > 0) { kill(chrome_pid, SIGTERM); waitpid(chrome_pid, nullptr, 0); }
    std::_Exit(0);
}

int main(int argc, char** argv) {
    std::string site, log_path;
    char cwd[4096]; getcwd(cwd, sizeof(cwd));
    site = std::string(cwd) + "/site/index.html";
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--site" && i + 1 < argc) site = argv[++i];
        if (a == "--log" && i + 1 < argc) log_path = argv[++i];
    }
    signal(SIGINT, cleanup); signal(SIGTERM, cleanup);
    try {
        Env env(site, log_path);
        env.start();
        std::string line;
        while (std::getline(std::cin, line)) {
            json req = json::parse(line);
            if (req["cmd"] == "reset") std::cout << env.reset(req).dump() << std::endl;
            else if (req["cmd"] == "step") std::cout << env.step(req.value("action", "wait")).dump() << std::endl;
            else if (req["cmd"] == "close") { env.close_env(); std::cout << "{\"ok\":true}" << std::endl; break; }
        }
    } catch (const std::exception& e) {
        std::cout << json{{"error", e.what()}, {"truncated", true}}.dump() << std::endl;
    }
    cleanup(0);
}
