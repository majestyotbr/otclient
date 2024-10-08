/*
 * Copyright (c) 2010-2022 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "httplogin.h"

#include <framework/core/asyncdispatcher.h>
#include <framework/core/eventdispatcher.h>
#include <future>
#include <httplib.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

LoginHttp::LoginHttp() {
    this->characters.clear();
    this->worlds.clear();
    this->session.clear();
}

void LoginHttp::Logger(const auto& req, const auto& res) {
    std::cout << "======= LOG ======= " << std::endl;
    std::cout << "-- REQUEST --" << std::endl;
    std::cout << req.method << std::endl;
    std::cout << req.path << std::endl;
    std::cout << req.body << std::endl;

    for (auto itr = req.headers.begin(); itr != req.headers.end(); ++itr) {
        std::cout << itr->first << '\t' << itr->second << '\n';
    }
    std::cout << "-- RESPONSE --" << std::endl;
    std::cout << res.version << std::endl;
    std::cout << res.status << std::endl;
    std::cout << res.reason << std::endl;
    std::cout << res.body << std::endl;
    std::cout << res.location << std::endl;

    for (auto itr = res.headers.begin(); itr != res.headers.end(); ++itr) {
        std::cout << itr->first << '\t' << itr->second << '\n';
    }

    std::cout << "========= " << std::endl;
}

void LoginHttp::startHttpLogin(const std::string& host, const std::string& path,
                               uint16_t port, const std::string& email,
                               const std::string& password) {
    httplib::SSLClient cli(host, port);

    cli.set_logger(
        [this](const auto& req, const auto& res) { LoginHttp::Logger(req, res); });

    json body = json{ {"email", email}, {"password", password}, {"stayloggedin", true}, {"type", "login"} };
    httplib::Headers headers = { {"User-Agent", "Mozilla/5.0"} };

    if (auto res = cli.Post(path, headers, body.dump(1), "application/json")) {
        if (res->status == 200) {
            json bodyResponse = json::parse(res->body);
            std::cout << bodyResponse.dump() << std::endl;

            std::cout << std::boolalpha << json::accept(res->body) << std::endl;
        }
    } else {
        auto err = res.error();
        std::cout << "HTTP error: " << httplib::to_string(err) << std::endl;
    }
}

std::string LoginHttp::getCharacterList() { return this->characters; }

std::string LoginHttp::getWorldList() { return this->worlds; }

std::string LoginHttp::getSession() { return this->session; }

void LoginHttp::httpLogin(const std::string& host, const std::string& path,
                          uint16_t port, const std::string& email,
                          const std::string& password, int request_id,
                          bool httpLogin) {
    g_asyncDispatcher.detach_task(
        [this, host, path, port, email, password, request_id, httpLogin] {
        httplib::Result result =
            this->loginHttpsJson(host, path, port, email, password);
        if (httpLogin && (!result || result->status != Success)) {
            result = loginHttpJson(host, path, port, email, password);
        }

        if (result && result->status == Success) {
            g_dispatcher.addEvent([this, request_id] {
                g_lua.callGlobalField("EnterGame", "loginSuccess", request_id,
                this->getSession(), this->getWorldList(),
                this->getCharacterList());
            });
        } else {
            int status = 0;
            std::string msg = "";
            if (result) {
                status = result->status;
                try {
                    const auto body = json::parse(result->body);
                    if (body.contains("errorMessage")) {
                        msg = body["errorMessage"];
                    } else {
                        msg = "Unexpected JSON format.";
                    }
                } catch (const std::exception&) {
                    msg = httplib::to_string(result.error());
                }
            } else {
                status = -1;
                msg = "Unknown error.\nCheck: \n-Enable Http login\n-Check Apache\n-Check login.php\n-check port 80/8080\n-Check Cloudflare";
            }

            g_dispatcher.addEvent([this, request_id, status, msg] {
                g_lua.callGlobalField("EnterGame", "loginFailed", request_id, msg,
                status);
            });
        }
    });
}

httplib::Result LoginHttp::loginHttpsJson(const std::string& host,
                                          const std::string& path,
                                          uint16_t port,
                                          const std::string& email,
                                          const std::string& password) {
    httplib::SSLClient client(host, port);

    client.set_logger(
        [this](const auto& req, const auto& res) { LoginHttp::Logger(req, res); });

    json body = { {"email", email}, {"password", password}, {"stayloggedin", true}, {"type", "login"} };
    httplib::Headers headers = { {"User-Agent", "Mozilla/5.0"} };

    httplib::Result response =
        client.Post(path, headers, body.dump(), "application/json");
    if (!response) {
        std::cout << "HTTPS error: unknown" << std::endl;
    } else if (response->status != Success) {
        std::cout << "HTTPS error: " << httplib::to_string(response.error())
            << std::endl;
    } else {
        std::cout << "HTTPS status: " << httplib::to_string(response.error())
            << std::endl;
    }

    if (response && response->status == Success &&
        !parseJsonResponse(response->body)) {
        response->status = -1;
    }

    return response;
}

httplib::Result LoginHttp::loginHttpJson(const std::string& host,
                                         const std::string& path,
                                         uint16_t port,
                                         const std::string& email,
                                         const std::string& password) {
    httplib::Client client(host, port);
    client.set_logger(
        [this](const auto& req, const auto& res) { LoginHttp::Logger(req, res); });

    httplib::Headers headers = { {"User-Agent", "Mozilla/5.0"} };
    json body = { {"email", email}, {"password", password}, {"stayloggedin", true}, {"type", "login"} };

    httplib::Result response =
        client.Post(path, headers, body.dump(), "application/json");
    if (!response) {
        std::cout << "HTTP error: unknown" << std::endl;
    } else if (response->status != Success) {
        std::cout << "HTTP error: " << httplib::to_string(response.error())
            << std::endl;
    } else {
        std::cout << "HTTP status: " << httplib::to_string(response.error())
            << std::endl;
    }

    if (response && response->status == Success &&
        !parseJsonResponse(response->body)) {
        response->status = -1;
    }

    return response;
}

bool LoginHttp::parseJsonResponse(const std::string& body) {
    json responseJson;
    try {
        responseJson = json::parse(body);
    } catch (...) {
        return false;
    }

    if (!responseJson.contains("session")) {
        return false;
    }

    if (responseJson.contains("playdata")) {
        json playdata = responseJson.at("playdata");

        this->characters = "{}";
        if (playdata.contains("characters")) {
            this->characters = to_string(playdata.at("characters"));
        }

        this->worlds = "{}";
        if (playdata.contains("worlds")) {
            this->worlds = to_string(playdata.at("worlds"));
        }
    }

    this->session = to_string(responseJson.at("session"));

    return true;
}