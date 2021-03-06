//
//  config.cc
//

#include "plat_os.h"
#include "plat_net.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <functional>
#include <algorithm>
#include <thread>
#include <mutex>
#include <memory>
#include <string>
#include <vector>
#include <deque>
#include <map>

#include "io.h"
#include "url.h"
#include "log.h"
#include "socket.h"
#include "resolver.h"
#include "config_parser.h"
#include "config.h"
#include "pollset.h"
#include "protocol.h"


/* config */

config::config() :
    client_connections(CLIENT_CONNECTIONS_DEFAULT),
    server_connections(SERVER_CONNECTIONS_DEFAULT),
    listen_backlog(LISTEN_BACKLOG_DEFAULT),
    max_headers(MAX_HEADERS_DEFAULT),
    header_buffer_size(HEADER_BUFFER_SIZE_DEFAULT),
    io_buffer_size(IO_BUFFER_SIZE_DEFAULT),
    ipc_buffer_size(IPC_BUFFER_SIZE_DEFAULT),
    log_buffers(LOG_BUFFERS_DEFAULT),
    keepalive_timeout(KEEPALIVE_TIMEOUT_DEFAULT),
    connection_timeout(CONNETION_TIMEOUT_DEFAULT),
    tls_session_timeout(TLS_SESSION_TIMEOUT_DEFAULT),
    tls_session_count(TLS_SESSION_COUNT_DEFAULT)
{    
    config_fn_map["os_user"] =             {2,  2,  [&] (config *cfg, config_line &line) { os_user = line[1]; }};
    config_fn_map["os_group"] =            {2,  2,  [&] (config *cfg, config_line &line) { os_group = line[1]; }};
    config_fn_map["pid_file"] =            {2,  2,  [&] (config *cfg, config_line &line) { pid_file = line[1]; }};
    config_fn_map["pid_file"] =            {2,  2,  [&] (config *cfg, config_line &line) { pid_file = line[1]; }};
    config_fn_map["tls_ca_file"] =         {2,  2,  [&] (config *cfg, config_line &line) { tls_ca_file = line[1]; }};
    config_fn_map["tls_key_file"] =        {2,  2,  [&] (config *cfg, config_line &line) { tls_key_file = line[1]; }};
    config_fn_map["tls_cert_file"] =       {2,  2,  [&] (config *cfg, config_line &line) { tls_cert_file = line[1]; }};
    config_fn_map["tls_cipher_list"] =     {2,  2,  [&] (config *cfg, config_line &line) { tls_cipher_list = line[1]; }};
    config_fn_map["tls_session_timeout"] = {2,  2,  [&] (config *cfg, config_line &line) { tls_session_timeout = atoi(line[1].c_str()); }};
    config_fn_map["tls_session_count"] =   {2,  2,  [&] (config *cfg, config_line &line) { tls_session_count = atoi(line[1].c_str()); }};
    config_fn_map["client_connections"] =  {2,  2,  [&] (config *cfg, config_line &line) { client_connections = atoi(line[1].c_str()); }};
    config_fn_map["server_connections"] =  {2,  2,  [&] (config *cfg, config_line &line) { server_connections = atoi(line[1].c_str()); }};
    config_fn_map["listen_backlog"] =      {2,  2,  [&] (config *cfg, config_line &line) { listen_backlog = atoi(line[1].c_str()); }};
    config_fn_map["max_headers"] =         {2,  2,  [&] (config *cfg, config_line &line) { max_headers = atoi(line[1].c_str()); }};
    config_fn_map["header_buffer_size"] =  {2,  2,  [&] (config *cfg, config_line &line) { header_buffer_size = atoi(line[1].c_str()); }};
    config_fn_map["io_buffer_size"] =      {2,  2,  [&] (config *cfg, config_line &line) { io_buffer_size = atoi(line[1].c_str()); }};
    config_fn_map["ipc_buffer_size"] =     {2,  2,  [&] (config *cfg, config_line &line) { ipc_buffer_size = atoi(line[1].c_str()); }};
    config_fn_map["log_buffers"] =         {2,  2,  [&] (config *cfg, config_line &line) { log_buffers = atoi(line[1].c_str()); }};
    config_fn_map["keepalive_timeout"] =   {2,  2,  [&] (config *cfg, config_line &line) { keepalive_timeout = atoi(line[1].c_str()); }};
    config_fn_map["connection_timeout"] =  {2,  2,  [&] (config *cfg, config_line &line) { connection_timeout = atoi(line[1].c_str()); }};
    config_fn_map["client_threads"] =      {3,  3,  [&] (config *cfg, config_line &line) {
        client_threads.push_back(std::pair<std::string,size_t>(line[1], atoi(line[2].c_str())));
    }};
    config_fn_map["server_threads"] =      {3,  3,  [&] (config *cfg, config_line &line) {
        server_threads.push_back(std::pair<std::string,size_t>(line[1], atoi(line[2].c_str())));
    }};
    config_fn_map["proto_threads"] =      {3,  3,  [&] (config *cfg, config_line &line) {
        proto_threads.push_back(std::pair<std::string,size_t>(line[1], atoi(line[2].c_str())));
    }};
    config_fn_map["proto_listener"] =     {3,  4,  [&] (config *cfg, config_line &line) {
        auto proto = (*protocol::get_map())[line[1]];
        if (!proto) {
            log_fatal_exit("configuration error: proto_listener: invalid protocol: %s", line[1].c_str());
        }
        socket_addr addr;
        if (socket_addr::string_to_addr(line[2], addr) < 0) {
            log_fatal_exit("configuration error: proto_listener: invalid address: %s", line[2].c_str());
        }
        if (line.size() == 4) {
            if (line[3] != "tls") {
                log_fatal_exit("configuration error: proto_listener: invalid option: %s", line[3].c_str());
            }
            proto_listeners.push_back(std::tuple<protocol*,socket_addr,socket_mode>(proto, addr, socket_mode_tls));
        } else {
            proto_listeners.push_back(std::tuple<protocol*,socket_addr,socket_mode>(proto, addr, socket_mode_plain));
        }
    }};
    config_fn_map["mime_type"] =           {3, -1,  [&] (config *cfg, config_line &line) {
        for (size_t s = 2; s < line.size(); s++) {
            mime_types[line[s]] = line[1];
        }
    }};
}

void config::read(std::string cfg_file)
{
    FILE *fp;
    struct stat statbuf;
    size_t len;
    std::vector<char> buf;
    
    if ((fp = fopen(cfg_file.c_str(), "r")) == NULL) {
        log_fatal_exit("config: fopen: %s: %s", cfg_file.c_str(), strerror(errno));
    }
    
    if (fstat(fileno(fp), &statbuf) < 0) {
        log_fatal_exit("config: fstat: %s", strerror(errno));
    }
    len = statbuf.st_size;
    
    // TODO - check all vector resize as they can throw bad_alloc and leak file pointer
    buf.resize(len + 1);
    if (fread(buf.data(), 1, len, fp) != len) {
        log_fatal_exit("config: short read");
    }
    
    if (!parse(buf.data(), len)) {
        log_fatal_exit("config: parse error");
    }
    
    fclose(fp);
}

void config::symbol(const char *value, size_t vlen)
{
    line.push_back(std::string(value, vlen));
}

void config::start_block()
{
    config_line block_line = line;
    
    // look up  block record
    block_record rec;
    bool found = lookup_block_start_fn(line[0], rec);
    if (found) {
        if (rec.parent_block && (block.size() < 1 || block[block.size() - 1][0] != std::string(rec.parent_block)))
        {
            log_fatal_exit("invalid parent block \"%s\" for block \"%s\"\n",
                           block[block.size() - 1][0].c_str(), block_line[0].c_str());
        } else if (!rec.parent_block && block.size() > 0) {
            log_fatal_exit("invalid parent block \"root\" for block \"%s\"\n",
                           block_line[0].c_str());
        } else if (rec.minargs == rec.maxargs && (int)line.size() != rec.minargs) {
            log_fatal_exit("%s requires %d argument(s)", line[0].c_str(), rec.minargs);
        } else if (rec.minargs > 0 && (int)line.size() < rec.minargs) {
            log_fatal_exit("%s requires at least %d argument(s)", line[0].c_str(), rec.minargs);
        } else if (rec.maxargs > 0 && (int)line.size() > rec.maxargs) {
            log_fatal_exit("%s requires no more than %d argument(s)", line[0].c_str(), rec.maxargs);
        }
        block.push_back(block_line);
        line.clear();
        rec.fn(this, block.back());
    } else {
        log_fatal_exit("unrecognized block: %s", line[0].c_str());
    }
}

void config::end_block()
{
    // look up  block record
    block_record rec;
    bool found = lookup_block_end_fn(block.back()[0], rec);
    if (found) {
        rec.fn(this, block.back());
    }
    
    block.pop_back();
}

void config::end_statement()
{
    if (line.size() > 0)
    {
        // look up protocol specific config record
        config_record rec;
        bool found = lookup_config_fn(line[0], rec);
        
        // if found call config function
        if (found) {
            if (rec.minargs == rec.maxargs && (int)line.size() != rec.minargs) {
                log_fatal_exit("%s requires %d argument(s)", line[0].c_str(), rec.minargs);
            } else if (rec.minargs > 0 && (int)line.size() < rec.minargs) {
                log_fatal_exit("%s requires at least %d argument(s)", line[0].c_str(), rec.minargs);
            } else if (rec.maxargs > 0 && (int)line.size() > rec.maxargs) {
                log_fatal_exit("%s requires no more than %d argument(s)", line[0].c_str(), rec.maxargs);
            }
            rec.fn(this, line);
        } else {
            log_fatal_exit("unrecognized directive: %s", line[0].c_str());
        }
    }
    line.clear();
}

void config::config_done()
{
}

std::string config::to_string()
{
    std::stringstream ss;
    ss << "client_connections  " << client_connections << ";" << std::endl;
    ss << "server_connections  " << server_connections << ";" << std::endl;
    ss << "listen_backlog      " << listen_backlog << ";" << std::endl;
    ss << "max_headers         " << max_headers << ";" << std::endl;
    ss << "header_buffer_size  " << header_buffer_size << ";" << std::endl;
    ss << "io_buffer_size      " << io_buffer_size << ";" << std::endl;
    ss << "ipc_buffer_size     " << ipc_buffer_size << ";" << std::endl;
    ss << "log_buffers         " << log_buffers << ";" << std::endl;
    ss << "keepalive_timeout   " << keepalive_timeout << ";" << std::endl;
    ss << "connection_timeout  " << connection_timeout << ";" << std::endl;
    ss << "error_log           " << error_log << ";" << std::endl;
    ss << "access_log          " << access_log << ";" << std::endl;
    ss << "pid_file            " << pid_file << ";" << std::endl;
    ss << "tls_ca_file         " << tls_ca_file << ";" << std::endl;
    ss << "tls_key_file        " << tls_key_file << ";" << std::endl;
    ss << "tls_cert_file       " << tls_cert_file << ";" << std::endl;
    ss << "tls_cipher_list     " << tls_cipher_list << ";" << std::endl;
    ss << "tls_session_timeout " << tls_session_timeout << ";" << std::endl;
    ss << "tls_session_count   " << tls_session_count << ";" << std::endl;
    ss << "root                " << root << ";" << std::endl;
    for (auto thread : client_threads) {
        ss << "client_threads      " << thread.first << " " << thread.second << ";" << std::endl;
    }
    for (auto thread : server_threads) {
        ss << "server_threads      " << thread.first << " " << thread.second << ";" << std::endl;
    }
    for (auto proto_listener : proto_listeners) {
        std::string proto = std::get<0>(proto_listener)->name;
        std::string addr = socket_addr::addr_to_string(std::get<1>(proto_listener));
        std::string mode = std::get<2>(proto_listener) == socket_mode_tls ? " tls" : "";
        ss << "proto_listener      " << proto << " " << addr << mode << ";" << std::endl;
    }
    for (auto mime_type_ent : mime_types) {
        ss << "mime_type           " << mime_type_ent.first << " " << mime_type_ent.second << ";" << std::endl;
    }
    for (auto index_file : index_files) {
        ss << "index_file          " << index_file << ";" << std::endl;
    }

    // output protocol specific config
    for (auto ent : proto_conf_map) {
        protocol_config_ptr proto_conf = ent.second;
        ss << proto_conf->to_string();
    }

    return ss.str();
}

bool config::lookup_config_fn(std::string key, config_record &record)
{
    bool found = false;
    
    // look up protocol specific config record
    for (auto ent : proto_conf_map) {
        protocol_config_ptr proto_conf = ent.second;
        if (proto_conf->lookup_config_fn(key, record)) {
            found = true;
            break;
        }
    }
    
    // if not found lookup general config record
    if (!found) {
        auto it = config_fn_map.find(key);
        if (it != config_fn_map.end()) {
            record = it->second;
            found = true;
        }
    }
    
    return found;
}

bool config::lookup_block_start_fn(std::string key, block_record &record)
{
    bool found = false;
    
    // look up protocol specific block start record
    for (auto ent : proto_conf_map) {
        protocol_config_ptr proto_conf = ent.second;
        if (proto_conf->lookup_block_start_fn(key, record)) {
            found = true;
            break;
        }
    }

    // if not found lookup general block start record
    auto it = block_start_fn_map.find(key);
    if (it != block_start_fn_map.end()) {
        record = it->second;
        found = true;
    }
    
    return found;
}

bool config::lookup_block_end_fn(std::string key, block_record &record)
{
    bool found = false;
    
    // look up protocol specific block end record
    for (auto ent : proto_conf_map) {
        protocol_config_ptr proto_conf = ent.second;
        if (proto_conf->lookup_block_end_fn(key, record)) {
            found = true;
            break;
        }
    }
    
    // if not found lookup general block end record
    auto it = block_end_fn_map.find(key);
    if (it != block_end_fn_map.end()) {
        record = it->second;
        found = true;
    }
    
    return found;
}

std::pair<std::string,std::string> config::lookup_mime_type(std::string path)
{
    std::string mime_type, extension;
    auto mi = mime_types.end();
    size_t last_slash = path.find_last_of("/");
    size_t last_dot = path.find_last_of(".");
    if (last_dot > last_slash && last_dot != std::string::npos) {
        extension = path.substr(last_dot + 1);
        mi = mime_types.find(extension);
    }
    if (mi != mime_types.end()) {
        mime_type = (*mi).second;
    } else {
        mi = mime_types.find("default");
        if (mi != mime_types.end()) {
            mime_type = (*mi).second;
        } else {
            mime_type = "application/octet-stream";
        }
    }
    return std::pair<std::string,std::string>(extension, mime_type);
}
