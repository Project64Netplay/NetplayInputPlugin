#include "stdafx.h"

#include "server.h"
#include "room.h"
#include "user.h"

using namespace std;
using namespace asio;

const std::chrono::high_resolution_clock::time_point server::epoch = std::chrono::high_resolution_clock::now();

uint64_t server::time() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - epoch).count();
}

server::server(shared_ptr<io_service> io_s, bool multiroom) : io_s(io_s), multiroom(multiroom), acceptor(*io_s), timer(*io_s) { }

uint16_t server::open(uint16_t port) {
    error_code error;

    auto ipv = ip::tcp::v6();
    acceptor.open(ipv, error);
    if (error) { // IPv6 not available
        ipv = ip::tcp::v4();
        acceptor.open(ipv, error);
        if (error) throw error;
    }

    acceptor.bind(ip::tcp::endpoint(ipv, port), error);
    if (error) throw error;

    acceptor.listen(MAX_PLAYERS, error);
    if (error) throw error;

    timer.expires_from_now(std::chrono::seconds(1));
    timer.async_wait([=](const error_code& error) { if (!error) on_tick(); });

    accept();

    return acceptor.local_endpoint().port();
}

void server::close() {
    error_code error;

    if (acceptor.is_open()) {
        acceptor.close(error);
    }

    timer.cancel(error);

    for (auto& e : rooms) {
        e.second->close();
    }
}

void server::accept() {
    auto socket = make_shared<asio::ip::tcp::socket>(*io_s);
    acceptor.async_accept(*socket, [=](error_code error) {
        if (error) return;
        
        socket->set_option(ip::tcp::no_delay(true), error);
        if (error) return;

        auto u = make_shared<user>(socket, shared_from_this());
        u->send_protocol_version();
        u->process_packet();

        accept();
    });
}

void server::on_user_join(user_ptr user, string room_id) {
    if (multiroom) {
        if (room_id == "") room_id = get_random_room_id();
    } else {
        room_id = "";
    }

    if (rooms.find(room_id) == rooms.end()) {
        rooms[room_id] = make_shared<room>(room_id, shared_from_this());
    }
    rooms[room_id]->on_user_join(user);
}

void server::on_room_close(room_ptr room) {
    rooms.erase(room->get_id());
}

void server::on_tick() {
    for (auto& e : rooms) {
        e.second->on_tick();
    }

    timer.expires_from_now(std::chrono::seconds(1));
    timer.async_wait([=](const error_code& error) { if (!error) on_tick(); });
}

string server::get_random_room_id() {
    static const string base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    static uniform_int_distribution<size_t> dist(0, base64.length());
    static random_device rd;

    string result;
    result.resize(4);
    do {
        for (char& c : result) {
            c = base64[dist(rd)];
        }
    } while (rooms.find(result) != rooms.end());

    return result;
}