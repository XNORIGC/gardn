#include <Server/Client.hh>

#include <Server/Game.hh>
#include <Server/PetalTracker.hh>
#include <Server/Server.hh>
#include <Server/Spawn.hh>
#include <Server/picosha2.h>

#include <Helpers/UTF8.hh>

#include <Shared/Binary.hh>
#include <Shared/Config.hh>

#include <array>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <limits>

constexpr std::array<uint32_t, RarityID::kNumRarities> RARITY_TO_XP = { 2, 10, 50, 200, 1000, 5000, 0 };

Client::Client() : game(nullptr) {}

void Client::init() {
    DEBUG_ONLY(assert(game == nullptr);)
    Server::game.add_client(this);    
}

void Client::remove() {
    if (game == nullptr) return;
    game->remove_client(this);
}

void Client::disconnect(int reason, std::string const &message) {
    if (ws == nullptr) return;
    remove();
    ws->end(reason, message);
}

uint8_t Client::alive() {
    if (game == nullptr) return false;
    Simulation *simulation = &game->simulation;
    return simulation->ent_exists(camera) 
    && simulation->ent_exists(simulation->get_ent(camera).player);
}

void Client::on_message(WebSocket *ws, std::string_view message, uint64_t code) {
    if (ws == nullptr) return;
    uint8_t const *data = reinterpret_cast<uint8_t const *>(message.data());
    Reader reader(data);
    Validator validator(data, data + message.size());
    Client *client = ws->getUserData();
    if (client == nullptr) {
        ws->end(CloseReason::kServer, "Server Error");
        return;
    }
    if (!client->verified) {
        if (client->check_invalid(validator.validate_uint8() && validator.validate_uint64())) return;
        if (reader.read<uint8_t>() != Serverbound::kVerify) {
            client->disconnect();
            return;
        }
        if (reader.read<uint64_t>() != VERSION_HASH) {
            client->disconnect(CloseReason::kOutdated, "Outdated Version");
            return;
        }
        client->verified = 1;
        client->init();
        return;
    }
    if (client->game == nullptr) {
        client->disconnect();
        return;
    }
    if (client->check_invalid(validator.validate_uint8())) return;
    switch (reader.read<uint8_t>()) {
        case Serverbound::kVerify:
            client->disconnect();
            return;
        case Serverbound::kClientInput: {
            if (!client->alive()) break;
            Simulation *simulation = &client->game->simulation;
            Entity &camera = simulation->get_ent(client->camera);
            Entity &player = simulation->get_ent(camera.player);
            if (client->check_invalid(
                validator.validate_float() &&
                validator.validate_float() &&
                validator.validate_uint8()
            )) return;
            float x = reader.read<float>();
            float y = reader.read<float>();
            if (x || y) client->x = x, client->y = y;
            if (x == 0 && y == 0) player.acceleration.set(0,0);
            else {
                if (std::abs(x) > 5e3 || std::abs(y) > 5e3) break;
                Vector accel(x,y);
                float m = accel.magnitude();
                if (m > 200) accel.set_magnitude(PLAYER_ACCELERATION);
                else accel.set_magnitude(m / 200 * PLAYER_ACCELERATION);
                player.acceleration = accel;
            }
            player.input = reader.read<uint8_t>();
            break;
        }
        case Serverbound::kClientSpawn: {
            if (client->alive()) break;
            //check string length
            std::string name;
            if (client->check_invalid(validator.validate_string(MAX_NAME_LENGTH))) return;
            reader.read<std::string>(name);
            if (client->check_invalid(UTF8Parser::is_valid_utf8(name))) return;
            Simulation *simulation = &client->game->simulation;
            Entity &camera = simulation->get_ent(client->camera);
            Entity &player = alloc_player(simulation, camera.team);
            player_spawn(simulation, camera, player);
            player.set_name(name);
            std::string password;
            if (client->check_invalid(validator.validate_string(MAX_PASSWORD_LENGTH))) return;
            reader.read<std::string>(password);
            if (client->check_invalid(UTF8Parser::is_valid_utf8(password))) return;
            #ifdef DEV
            client->isAdmin = true;
            #else
            client->isAdmin = picosha2::hash256_hex_string(password) == PASSWORD;
            #endif
            std::cout << "player_spawn " << (player.name.size() ? player.name : "Unnamed")
                << " <" << +player.id.hash << "," << +player.id.id << ">" << std::endl;
            break;
        }
        case Serverbound::kPetalDelete: {
            if (!client->alive()) break;
            Simulation *simulation = &client->game->simulation;
            Entity &camera = simulation->get_ent(client->camera);
            Entity &player = simulation->get_ent(camera.player);
            if (client->check_invalid(validator.validate_uint8())) return;
            uint8_t pos = reader.read<uint8_t>();
            if (pos >= MAX_SLOT_COUNT + player.loadout_count) break;
            PetalID::T old_id = player.loadout_ids[pos];
            #ifdef DEV
            if (old_id == PetalID::kCorruption) break;
            #endif
            if (old_id != PetalID::kNone && old_id != PetalID::kBasic) {
                uint8_t rarity = PETAL_DATA[old_id].rarity;
                player.set_score(player.score + RARITY_TO_XP[rarity]);
                //need to delete if over cap
                if (player.deleted_petals.size() == player.deleted_petals.capacity())
                    //removes old trashed old petal
                    PetalTracker::remove_petal(simulation, player.deleted_petals[0]);
                player.deleted_petals.push_back(old_id);
            }
            player.set_loadout_ids(pos, PetalID::kNone);
            break;
        }
        case Serverbound::kPetalSwap: {
            if (!client->alive()) break;
            Simulation *simulation = &client->game->simulation;
            Entity &camera = simulation->get_ent(client->camera);
            Entity &player = simulation->get_ent(camera.player);
            if (client->check_invalid(validator.validate_uint8() && validator.validate_uint8())) return;
            uint8_t pos1 = reader.read<uint8_t>();
            if (pos1 >= MAX_SLOT_COUNT + player.loadout_count) break;
            uint8_t pos2 = reader.read<uint8_t>();
            if (pos2 >= MAX_SLOT_COUNT + player.loadout_count) break;
            #ifdef DEV
            if (player.loadout_ids[pos1] == PetalID::kCorruption || player.loadout_ids[pos2] == PetalID::kCorruption) break;
            #endif
            PetalID::T tmp = player.loadout_ids[pos1];
            player.set_loadout_ids(pos1, player.loadout_ids[pos2]);
            player.set_loadout_ids(pos2, tmp);
            break;
        }
        case Serverbound::kChatSend: {
            if (!client->alive()) break;
            Simulation *simulation = &client->game->simulation;
            Entity &camera = simulation->get_ent(client->camera);
            Entity &player = simulation->get_ent(camera.player);
            if (player.chat_sent != NULL_ENTITY) break;
            std::string text;
            if (client->check_invalid(validator.validate_string(MAX_CHAT_LENGTH))) return;
            reader.read<std::string>(text);
            if (client->check_invalid(UTF8Parser::is_valid_utf8(text))) return;
            text = UTF8Parser::trunc_string(text, MAX_CHAT_LENGTH);
            if (text.size() == 0) break;
            player.chat_sent = alloc_chat(simulation, text, player).id;
            std::cout << "chat " << (player.name.size() ? player.name : "Unnamed") << ": " << text << std::endl;
            //commands
            if (text[0] == '/') client->command(text.substr(1));
            break;
        }
    }
}

void Client::command(std::string const& text) {
    Simulation* simulation = &this->game->simulation;
    Entity& camera = simulation->get_ent(this->camera);
    Entity& player = simulation->get_ent(camera.player);
    float x = player.x + (this->x / camera.fov) * 1.03;
    float y = player.y + (this->y / camera.fov) * 1.03;

    std::istringstream iss(text);
    std::string command, arg;
    iss >> command;
    std::transform(command.begin(), command.end(), command.begin(), ::tolower);

    if (command == "kill") {
        simulation->get_ent(player.parent).set_killed_by(player.name.size() ? player.name : "Unnamed");
        simulation->request_delete(player.id);
    }

    if (!this->isAdmin) return;

    if (command == "drop" || command == "give") {
        PetalID::T id;
        while (iss >> arg) {
            try { id = std::stoi(arg); }
            catch (const std::invalid_argument&) { continue; }
            catch (const std::out_of_range&) { continue; }
            if (id >= PetalID::kNumPetals) continue;
            Entity& drop = alloc_drop(simulation, id);
            drop.set_x(player.x), drop.set_y(player.y);
        }
    }
    else if (command == "dropto") {
        PetalID::T id;
        while (iss >> arg) {
            try { id = std::stoi(arg); }
            catch (const std::invalid_argument&) { continue; }
            catch (const std::out_of_range&) { continue; }
            if (id >= PetalID::kNumPetals) continue;
            Entity& drop = alloc_drop(simulation, id);
            drop.set_x(x), drop.set_y(y);
        }
    }
    else if (command == "tp") {
        try { iss >> arg, x = std::stof(arg), iss >> arg, y = std::stof(arg); }
        catch (const std::invalid_argument&) { return; }
        catch (const std::out_of_range&) { return; }
        player.set_x(x), player.set_y(y);
    }
    else if (command == "tpto") {
        player.set_x(x), player.set_y(y);
    }
    else if (command == "xp") {
        uint32_t xp;
        try { iss >> arg, xp = uint32_t(std::stoul(arg)); }
        catch (const std::invalid_argument&) { return; }
        catch (const std::out_of_range&) { return; }
        player.set_score(player.score + xp);
    }
    else if (command == "spawn") {
        MobID::T id;
        while (iss >> arg) {
            try { id = std::stoi(arg); }
            catch (const std::invalid_argument&) { continue; }
            catch (const std::out_of_range&) { continue; }
            if (id >= MobID::kNumMobs) continue;
            alloc_mob(simulation, id, player.x, player.y, NULL_ENTITY);
        }
    }
    else if (command == "spawnto") {
        MobID::T id;
        while (iss >> arg) {
            try { id = std::stoi(arg); }
            catch (const std::invalid_argument&) { continue; }
            catch (const std::out_of_range&) { continue; }
            if (id >= MobID::kNumMobs) continue;
            alloc_mob(simulation, id, x, y, NULL_ENTITY);
        }
    }
    else if (command == "spawnally") {
        MobID::T id;
        while (iss >> arg) {
            try { id = std::stoi(arg); }
            catch (const std::invalid_argument&) { continue; }
            catch (const std::out_of_range&) { continue; }
            if (id >= MobID::kNumMobs) continue;
            alloc_mob(simulation, id, player.x, player.y, player.team);
        }
    }
    else if (command == "spawnallyto") {
        MobID::T id;
        while (iss >> arg) {
            try { id = std::stoi(arg); }
            catch (const std::invalid_argument&) { continue; }
            catch (const std::out_of_range&) { continue; }
            if (id >= MobID::kNumMobs) continue;
            alloc_mob(simulation, id, x, y, player.team);
        }
    }
    else if (command == "spawnenemy") {
        MobID::T id;
        while (iss >> arg) {
            try { id = std::stoi(arg); }
            catch (const std::invalid_argument&) { continue; }
            catch (const std::out_of_range&) { continue; }
            if (id >= MobID::kNumMobs) continue;
            alloc_mob(simulation, id, player.x, player.y, player.id);
        }
    }
    else if (command == "spawnenemyto") {
        MobID::T id;
        while (iss >> arg) {
            try { id = std::stoi(arg); }
            catch (const std::invalid_argument&) { continue; }
            catch (const std::out_of_range&) { continue; }
            if (id >= MobID::kNumMobs) continue;
            alloc_mob(simulation, id, x, y, player.id);
        }
    }
    else if (command == "ghost") {
        if (!simulation->ent_alive(player.parent)) return;
        Entity& camera_ent = simulation->get_ent(player.parent);
        Entity& player_ent = player;
        if (player_ent.ghost_mode) {
            // set ghost_mode via setter so change syncs to clients
            player_ent.set_ghost_mode(0);
            // restore collisions and immunity directly
            BitMath::unset(player_ent.flags, EntityFlags::kNoFriendlyCollision);
            player_ent.immunity_ticks = 0;
        }
        else {
            player_ent.set_ghost_mode(1);
            BitMath::set(player_ent.flags, EntityFlags::kNoFriendlyCollision);
            player_ent.immunity_ticks = std::numeric_limits<decltype(player_ent.immunity_ticks)>::max();
        }
        std::cout << "ghost mode toggled for " << (player_ent.name.size() ? player_ent.name : "Unnamed") << " -> " << +player_ent.ghost_mode << std::endl;
    }
    else if (command == "killallmobs") {
        for (uint16_t i = 0; i < ENTITY_CAP; ++i) {
            EntityID id(i, 0);
            if (!simulation->ent_alive(id)) continue;
            Entity& ent = simulation->get_ent(id);
            if (ent.has_component(kMob)) {
                ent.health = 0;
            }
        }
    }
    else  if (command == "broadcast") {
        std::string text;
        std::getline(iss, text);  // ����������ȡʣ�������ı�
        if (!text.empty()) {
            Server::game.broadcast_message(text);  // ���÷������㲥����
        }
    }
}

void Client::on_disconnect(WebSocket *ws, int code, std::string_view message) {
    std::printf("disconnect: [%d]\n", code);
    Client *client = ws->getUserData();
    if (client == nullptr) return;
    client->remove();
}

bool Client::check_invalid(bool valid) {
    if (valid) return false;
    std::cout << "client sent an invalid packet\n";
    //optional
    disconnect();

    return true;
}