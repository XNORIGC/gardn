#include <Server/EntityFunctions.hh>

#include <Server/Spawn.hh>
#include <Shared/Entity.hh>
#include <Shared/Simulation.hh>

#include <cmath>

static bool _yggdrasil_revival_clause(Simulation *sim, Entity &player) {
    if (BitMath::at(player.flags, EntityFlags::kZombie)) return false;
    for (uint32_t i = 0; i < player.get_loadout_count(); ++i) {
        if (!player.loadout[i].already_spawned) continue;
        if (player.loadout[i].get_petal_id() != PetalID::kYggdrasil) continue;
        if (frand() > 0.5) continue;
        return true;
    }
    return false;
}

void inflict_damage(Simulation *sim, EntityID const atk_id, EntityID const def_id, float amt, uint8_t type) {
    {
        Entity& def_ent = sim->get_ent(def_id);
        Entity& atk_ent = sim->get_ent(atk_id);

        // 判断是否有一方是 TargetDummy
        bool def_is_dummy = def_ent.has_component(kMob) && def_ent.get_mob_id() == MobID::kTargetDummy;
        bool atk_is_dummy = atk_ent.has_component(kMob) && atk_ent.get_mob_id() == MobID::kTargetDummy;

        // 获取父实体（如果存在）
        Entity* def_parent = (def_ent.get_parent() != NULL_ENTITY && sim->ent_alive(def_ent.get_parent())) ? &sim->get_ent(def_ent.get_parent()) : nullptr;
        Entity* atk_parent = (atk_ent.get_parent() != NULL_ENTITY && sim->ent_alive(atk_ent.get_parent())) ? &sim->get_ent(atk_ent.get_parent()) : nullptr;

        // 如果一方是 TargetDummy，另一方是生物或者父是生物，则跳过
        if (def_is_dummy) {
            if (atk_ent.has_component(kMob) || (atk_parent && atk_parent->has_component(kMob)))
                return;
        }
        else if (atk_is_dummy) {
            if (def_ent.has_component(kMob) || (def_parent && def_parent->has_component(kMob)))
                return;
        }

    }
    if (amt <= 0) return;
    if (!sim->ent_alive(def_id)) return;
    Entity &defender = sim->get_ent(def_id);
    if (!defender.has_component(kHealth)) return;
    DEBUG_ONLY(assert(!defender.pending_delete);)
    DEBUG_ONLY(assert(defender.has_component(kHealth));)
    // if defender is in ghost mode, ignore damage
    if (defender.get_ghost_mode() ||  sim->get_ent(atk_id).get_ghost_mode()) return;
    if (defender.immunity_ticks > 0) return;
    if (type == DamageType::kContact) amt -= defender.armor;
    else if (type == DamageType::kPoison) amt -= defender.poison_armor;
    if (amt <= 0) return;
    //if (amt <= defender.armor) return;
    float old_health = defender.health;
    defender.set_damaged(1);
    defender.health = fclamp(defender.health - amt, 0, defender.health);  
    float damage_dealt = old_health - defender.health;
    //ant hole spawns
    //floor start, ceil end
    if (defender.has_component(kMob) && defender.get_mob_id() == MobID::kAntHole) {
        uint32_t const num_waves = ANTHOLE_SPAWNS.size() - 1;
        uint32_t start = ceilf((defender.max_health - old_health) / defender.max_health * num_waves);
        uint32_t end = ceilf((defender.max_health - defender.health) / defender.max_health * num_waves);
        if (defender.health <= 0) end = num_waves + 1;
        for (uint32_t i = start; i < end; ++i) {
            for (MobID::T mob_id : ANTHOLE_SPAWNS[i]) {
                Entity &child = alloc_mob(sim, mob_id, defender.get_x(), defender.get_y(), defender.get_team());
                child.set_parent(defender.id);
                child.target = defender.target;
            }
        }
    }
    if (defender.has_component(kMob) && defender.get_mob_id() == MobID::kTargetDummy) {
        const float drop_interval = 0.025f;
        uint32_t start = ceilf((defender.max_health - old_health) / (defender.max_health * drop_interval));
        uint32_t end = ceilf((defender.max_health - defender.health) / (defender.max_health * drop_interval));
        if (defender.health == 0) {
            defender.set_ghost_mode(1);
            defender.health = 1;
        }
        for (uint32_t i = start; i < end; ++i) {
            // 掉落史诗道具
            std::vector<uint32_t> epic_indices;
            for (uint32_t idx = 0; idx < PetalID::kNumPetals; ++idx) {
                if (PETAL_DATA[idx].rarity == RarityID::kEpic)
                    epic_indices.push_back(idx);
            }
            if (!epic_indices.empty()) {
                uint32_t chosen_idx = epic_indices[rand() % epic_indices.size()];
                Entity& drop = alloc_drop(sim, chosen_idx);
                float radius = defender.get_radius() + 35;
                float angle = frand() * 2.0f * M_PI;
                float dist = radius + frand() * 35.0f;
                drop.set_x(defender.get_x() + cos(angle) * dist);
                drop.set_y(defender.get_y() + sin(angle) * dist);
            }
            if (frand() < 0.02f) {
                Entity& ygg = alloc_drop(sim, PetalID::kYggdrasil);
                float radius = defender.get_radius() + 35;
                float angle = frand() * 2.0f * M_PI;
                float dist = radius + frand() * 35.0f;
                ygg.set_x(defender.get_x() + cos(angle) * dist);
                ygg.set_y(defender.get_y() + sin(angle) * dist);
            }
            if (frand() < 0.02f) {
                Entity& tank = alloc_mob(sim, MobID::kTank, defender.get_x(), defender.get_y(), NULL_ENTITY);
            }
            // 追溯攻击者父实体
            Entity* attacker = nullptr;
            if (sim->ent_alive(atk_id)) {
                Entity& atk_ent = sim->get_ent(atk_id);

                // 如果不是 Flower 且有父实体，则优先取父实体
                if (!atk_ent.has_component(kFlower) && !(atk_ent.get_parent() == NULL_ENTITY)) {
                    attacker = &sim->get_ent(atk_ent.get_parent());
                }
                else {
                    // 本体攻击者或者父实体不存在，直接用 atk_ent
                    attacker = &atk_ent;
                }
            }
            if (attacker) {
                const int missile_count = 12;
                const float circle_radius = attacker->get_radius() + 200.0f; // 圆半径 = 父实体半径 + 200
                float prediction_factor = 7.0f;
                float predicted_x = attacker->get_x() + attacker->velocity.x * prediction_factor;
                float predicted_y = attacker->get_y() + attacker->velocity.y * prediction_factor;
                for (int j = 0; j < missile_count; ++j) {
                    float angle = 2.0f * M_PI * j / missile_count;
                    float x = predicted_x + cos(angle) * circle_radius;
                    float y = predicted_y + sin(angle) * circle_radius;

                    Entity& missile = alloc_petal(sim, PetalID::kMissile, defender);
                    missile.damage = 5;
                    missile.health = missile.max_health = 50;
                    missile.set_team(defender.get_team());
                    entity_set_despawn_tick(missile, 3 * TPS);
                    missile.set_x(x);
                    missile.set_y(y);

                    // 导弹角度指向圆心
                    Vector v(predicted_x - x, predicted_y - y);
                    missile.set_angle(v.angle());
                }
            }
        }
    }
    // yggdrasil revive clause
    if (defender.health == 0 && defender.has_component(kFlower)) {
        if (_yggdrasil_revival_clause(sim, defender)) {
            defender.set_revived(1);
            defender.health = defender.max_health;
            defender.poison_ticks = 0;
            defender.slow_ticks = 0;
            defender.dandy_ticks = 0;
            defender.immunity_ticks = 3.0 * TPS;
            const int missile_count = 24;
            const float circle_radius = defender.get_radius(); 

            float center_x = defender.get_x();
            float center_y = defender.get_y();

            for (int j = 0; j < missile_count; ++j) {
                float angle = 2.0f * M_PI * j / missile_count;
                float x = center_x + cos(angle) * circle_radius;
                float y = center_y + sin(angle) * circle_radius;

                Entity& missile = alloc_petal(sim, PetalID::kDandelion, defender);
                missile.damage = 0;
                missile.health = missile.max_health = 5000;
                missile.set_radius(60);
                missile.mass = 10;
                missile.set_team(defender.get_team());
                entity_set_despawn_tick(missile, 3 * TPS);
                missile.set_x(x);
                missile.set_y(y);

                // 导弹角度指向中心
                Vector v(center_x - x, center_y - y);
                missile.set_angle(v.angle());
            }
        }
    }
    if (!sim->ent_exists(atk_id)) return;
    Entity &attacker = sim->get_ent(atk_id);

    if (type != DamageType::kReflect && defender.damage_reflection > 0)
        inflict_damage(sim, def_id, attacker.base_entity, damage_dealt * defender.damage_reflection, DamageType::kReflect);

    if (type == DamageType::kContact && defender.get_revived() == 0) {
        if (defender.poison_ticks < attacker.poison_damage.time * TPS) {
            defender.poison_ticks = attacker.poison_damage.time * TPS;
            defender.poison_inflicted = attacker.poison_damage.damage / TPS;
            defender.poison_dealer = attacker.base_entity;
        }

        if (defender.slow_ticks < attacker.slow_inflict)
            defender.slow_ticks = attacker.slow_inflict;

        if (attacker.has_component(kPetal) &&
            attacker.get_petal_id() == PetalID::kDandelion &&
            defender.dandy_ticks < 10 * TPS)
            defender.dandy_ticks = 10 * TPS;
    }

    if (!sim->ent_alive(defender.target)) {
        if (attacker.has_component(kPetal) && sim->ent_alive(attacker.get_parent()))
            defender.target = attacker.get_parent();
        else if (sim->ent_alive(atk_id))
            defender.target = atk_id;
    }
    defender.last_damaged_by = attacker.base_entity;
}

void inflict_heal(Simulation *sim, Entity &ent, float amt) {
    DEBUG_ONLY(assert(ent.has_component(kHealth));)
    if (ent.pending_delete || ent.health <= 0) return;
    if (ent.dandy_ticks > 0) return;
    if (BitMath::at(ent.flags, EntityFlags::kZombie)) return;
    ent.health = fclamp(ent.health + amt, 0, ent.max_health);
}