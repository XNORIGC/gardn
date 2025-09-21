#pragma once

#include <Server/TeamManager.hh>

#include <Shared/Simulation.hh>

#include <set>

class Client;

class GameInstance {
    std::set<Client *> clients;
public:
    TeamManager team_manager;
public:
    Simulation simulation;
    GameInstance();
    void init();
    void tick();
    void add_client(Client *, uint64_t);
    void remove_client(Client *);
    void broadcast_message(const std::string &msg);
};