#include <Server/Client.hh>

#include <Server/Game.hh>
#include <Server/PetalTracker.hh>
#include <Server/Server.hh>
#include <Server/Spawn.hh>

#include <Helpers/UTF8.hh>
#include <Helpers/picosha2.h>

#include <Shared/Binary.hh>
#include <Shared/Config.hh>

#include <array>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <limits>
#include <cmath>

constexpr std::array<uint32_t, RarityID::kNumRarities> RARITY_TO_XP = { 2, 10, 50, 200, 1000, 5000, 0 };

Client::Client() : game(nullptr) {}

void Client::init(uint64_t recovery_id) {
    DEBUG_ONLY(assert(game == nullptr);)
    Server::game.add_client(this, recovery_id);
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
    && simulation->ent_exists(simulation->get_ent(camera).get_player());
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
        if (client->check_invalid(
            validator.validate_uint8() &&
            validator.validate_uint64() &&
            validator.validate_uint64()
        )) return;
        if (reader.read<uint8_t>() != Serverbound::kVerify) {
            client->disconnect();
            return;
        }
        if (reader.read<uint64_t>() != VERSION_HASH) {
            client->disconnect(CloseReason::kOutdated, "Outdated Version");
            return;
        }
        client->verified = 1;
        client->init(reader.read<uint64_t>());
        return;
    }
    if (client->game == nullptr) {
        client->disconnect(CloseReason::kServer, "Server Error");
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
            Entity &player = simulation->get_ent(camera.get_player());
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
            // 先计算鼠标在世界空间的坐标
            float mouse_world_x = player.get_x() + client->x / camera.get_fov();
            float mouse_world_y = player.get_y() + client->y / camera.get_fov();

            // 遍历玩家装备的花瓣
            for (uint32_t i = 0; i < player.get_loadout_count(); ++i) {
                LoadoutSlot const& slot = player.loadout[i];
                PetalID::T slot_petal_id = slot.get_petal_id();
                PetalData const& petal_data = PETAL_DATA[slot_petal_id];

                if (petal_data.attributes.controls != PetalID::kNone) {
                    PetalID::T controlled_id = petal_data.attributes.controls;

                    simulation->for_each_entity([&](Simulation* sim2, Entity& ent) {
                        if (ent.get_parent() != player.id) return;          // 必须是该玩家的
                        if (ent.get_petal_id() != controlled_id) return;    // 必须是被控制的花瓣类型

                        // ==== 检查与其他同类实体的重叠 ====
                        sim2->for_each_entity([&](Simulation* sim3, Entity& other) {
                            if (&other == &ent) return;               // 跳过自己
                            if (other.get_parent() != player.id) return;    // 只管本玩家的
                            if (other.get_petal_id() != controlled_id) return;

                            float dx = ent.get_x() - other.get_x();
                            float dy = ent.get_y() - other.get_y();
                            float dist2 = dx * dx + dy * dy;
                            float min_dist = ent.get_radius() + other.get_radius();

                            if (dist2 < min_dist * min_dist) {
                                float dist = std::sqrt(dist2);
                                if (dist < 0.0001f) dist = 0.0001f; // 防止除零

                                // 计算分离向量
                                float overlap = 0.5f * (min_dist - dist);
                                float nx = dx / dist;
                                float ny = dy / dist;

                                // 推开双方
                                ent.set_x(ent.get_x() + nx * overlap * 4);
                                ent.set_y(ent.get_y() + ny * overlap * 4);
                                other.set_x(other.get_x() - nx * overlap * 4);
                                other.set_y(other.get_y() - ny * overlap * 4);
                            }
                        });

                        // ==== 控制朝向 ====
                        Vector aim(mouse_world_x - ent.get_x(), mouse_world_y - ent.get_y());
                        ent.set_angle(aim.angle());
                        if (BitMath::at(player.input, InputFlags::kDefending))
                            ent.set_angle(aim.angle() + M_PI);
                    });
                }
            }
            player.input = reader.read<uint8_t>();
            break;
        }
        case Serverbound::kClientSpawn: {
            if (client->alive()) break;
            //check string length
            std::string name, pwd;
            if (client->check_invalid(validator.validate_string(MAX_NAME_LENGTH))) return;
            if (client->check_invalid(validator.validate_string(MAX_DEV_PWD_LENGTH))) return;
            reader.read<std::string>(name);
            reader.read<std::string>(pwd);
            if (client->check_invalid(UTF8Parser::is_valid_utf8(name))) return;
            if (client->check_invalid(UTF8Parser::is_valid_utf8(pwd))) return;
            Simulation *simulation = &client->game->simulation;
            Entity &camera = simulation->get_ent(client->camera);
            Entity &player = alloc_player(simulation, camera.get_team());
            player_spawn(simulation, camera, player);
            player.set_name(name);
            uint8_t dev = pwd == "ez hax"; // feel free to use
            camera.set_dev(dev);
            player.set_dev(dev);
            std::cout << "player_spawn" << (dev ? "_dev " : " ") << name_or_unnamed(name)
                << " <" << +player.id.hash << "," << +player.id.id << ">" << std::endl;
            break;
        }
        case Serverbound::kPetalDelete: {
            if (!client->alive()) break;
            Simulation *simulation = &client->game->simulation;
            Entity &camera = simulation->get_ent(client->camera);
            Entity &player = simulation->get_ent(camera.get_player());
            if (client->check_invalid(validator.validate_uint8())) return;
            uint8_t pos = reader.read<uint8_t>();
            if (pos >= MAX_SLOT_COUNT + player.get_loadout_count()) break;
            PetalID::T old_id = player.get_loadout_ids(pos);
            if (old_id == PetalID::kCorruption) break;
            if (old_id != PetalID::kNone && old_id != PetalID::kBasic) {
                uint8_t rarity = PETAL_DATA[old_id].rarity;
                player.set_score(player.get_score() + RARITY_TO_XP[rarity]);
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
            Entity &player = simulation->get_ent(camera.get_player());
            if (client->check_invalid(validator.validate_uint8() && validator.validate_uint8())) return;
            uint8_t pos1 = reader.read<uint8_t>();
            if (pos1 >= MAX_SLOT_COUNT + player.get_loadout_count()) break;
            uint8_t pos2 = reader.read<uint8_t>();
            if (player.get_loadout_ids(pos1) == PetalID::kCorruption || player.get_loadout_ids(pos2) == PetalID::kCorruption) break;
            if (pos2 >= MAX_SLOT_COUNT + player.get_loadout_count()) break;
            PetalID::T tmp = player.get_loadout_ids(pos1);
            player.set_loadout_ids(pos1, player.get_loadout_ids(pos2));
            player.set_loadout_ids(pos2, tmp);
            break;
        }
        case Serverbound::kChatSend: {
            if (!client->alive()) break;
            std::string text;
            if (client->check_invalid(validator.validate_string(MAX_CHAT_LENGTH))) return;
            reader.read<std::string>(text);
            if (client->check_invalid(UTF8Parser::is_valid_utf8(text))) return;
            // text = UTF8Parser::trunc_string(text, MAX_CHAT_LENGTH);
            if (text.size() == 0) break;
            Simulation *simulation = &client->game->simulation;
            Entity &camera = simulation->get_ent(client->camera);
            Entity &player = simulation->get_ent(camera.get_player());
            if (player.chat_sent != NULL_ENTITY) break;
            player.chat_sent = alloc_chat(simulation, text, player).id;
            std::cout << "chat " << name_or_unnamed(player.get_name()) << ": " << text << std::endl;
            break;
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