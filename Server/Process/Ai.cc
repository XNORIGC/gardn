#include <Server/Process.hh>

#include <Server/EntityFunctions.hh>
#include <Server/Spawn.hh>
#include <Shared/Entity.hh>
#include <Shared/Simulation.hh>
#include <Shared/StaticData.hh>

#include <cmath>

static void _focus_lose_clause(Entity &ent, Vector const &v) {
    if (v.magnitude() > 1.5 * ent.detection_radius) ent.target = NULL_ENTITY;
}

static void default_tick_idle(Simulation *sim, Entity &ent) {
    if (ent.ai_tick >= 1 * TPS) {
        ent.ai_tick = 0;
        ent.set_angle(frand() * 2 * M_PI);
        ent.ai_state = AIState::kIdleMoving;
    }
}

static void default_tick_idle_moving(Simulation *sim, Entity &ent) {
    if (ent.ai_tick > 2.5 * TPS) {
        ent.ai_tick = 0;
        ent.ai_state = AIState::kIdle;
        return;
    }
    if (ent.ai_tick < 0.5 * TPS) return;
    float r = (ent.ai_tick - 0.5 * TPS) / (2 * TPS);
    ent.acceleration
        .unit_normal(ent.get_angle())
        .set_magnitude(2 * PLAYER_ACCELERATION * (r - r * r));
}

static void default_tick_returning(Simulation *sim, Entity &ent, float speed = 1.0) {
    if (!sim->ent_alive(ent.get_parent())) {
        ent.ai_tick = 0;
        ent.ai_state = AIState::kIdle;
        return;
    }
    Entity &parent = sim->get_ent(ent.get_parent());
    Vector delta(parent.get_x() - ent.get_x(), parent.get_y() - ent.get_y());
    if (delta.magnitude() > 300) {
        ent.ai_tick = 0;
    } else if (ent.ai_tick > 2 * TPS || delta.magnitude() < 100) {
        ent.ai_tick = 0;
        ent.ai_state = AIState::kIdle;
        return;
    } 
    delta.set_magnitude(PLAYER_ACCELERATION * speed);
    ent.acceleration = delta;
    ent.set_angle(delta.angle());
}


static void tick_default_passive(Simulation *sim, Entity &ent) {
    switch(ent.ai_state) {
        case AIState::kIdle: {
            default_tick_idle(sim, ent);
            break;
        }
        case AIState::kIdleMoving: {
            default_tick_idle_moving(sim, ent);
            break;
        }
        case AIState::kReturning: {
            default_tick_returning(sim, ent);
            break;
        }
        default:
            ent.ai_state = AIState::kIdle;
            break;
    }
}

static void tick_default_neutral(Simulation *sim, Entity &ent) {
    if (sim->ent_alive(ent.target)) {
        Entity &target = sim->get_ent(ent.target);
        Vector v(target.get_x() - ent.get_x(), target.get_y() - ent.get_y());
        v.set_magnitude(PLAYER_ACCELERATION * 0.975);
        ent.acceleration = v;
        ent.set_angle(v.angle());
        return;
    } else {
        if (!(ent.target == NULL_ENTITY)) {
            ent.target = NULL_ENTITY;
            ent.ai_state = AIState::kIdle;
            ent.ai_tick = 0;
        }
        tick_default_passive(sim, ent);
    }
}

static void tick_default_aggro(Simulation *sim, Entity &ent, float speed) {
    if (sim->ent_alive(ent.target)) {
        Entity &target = sim->get_ent(ent.target);
        Vector v(target.get_x() - ent.get_x(), target.get_y() - ent.get_y());
        _focus_lose_clause(ent, v);
        v.set_magnitude(PLAYER_ACCELERATION * speed);
        ent.acceleration = v;
        ent.set_angle(v.angle());
        return;
    } else {
        if (!(ent.target == NULL_ENTITY)) {
            ent.ai_state = AIState::kIdle;
            ent.ai_tick = 0;
        }
        //if (ent.ai_state != AIState::kReturning) 
        ent.target = find_nearest_enemy(sim, ent, ent.detection_radius + ent.get_radius());
        tick_default_passive(sim, ent);
    }
}

static void tick_bee_passive(Simulation *sim, Entity &ent) {
    switch(ent.ai_state) {
        case AIState::kIdle: {
            if (ent.ai_tick >= 5 * TPS) {
                ent.ai_tick = 0;
                ent.set_angle(frand() * 2 * M_PI);
                ent.ai_state = AIState::kIdle;
            }
            ent.set_angle(ent.get_angle() + 1.5 * sinf(((float) ent.lifetime) / (TPS / 2)) / TPS);
            Vector v(cosf(ent.get_angle()), sinf(ent.get_angle()));
            v *= 1.5;
            if (ent.lifetime % (TPS * 3 / 2) < TPS / 2)
                v *= 0.5;
            ent.acceleration = v;
            break;
        }
        case AIState::kIdleMoving: {
            break;
        }
        case AIState::kReturning: {
            default_tick_returning(sim, ent);
            break;
        }
        default:
            ent.ai_state = AIState::kIdle;
            break;
    }
}

static void tick_hornet_aggro(Simulation *sim, Entity &ent) {
    if (sim->ent_alive(ent.target)) {
        Entity &target = sim->get_ent(ent.target);
        Vector v(target.get_x() - ent.get_x(), target.get_y() - ent.get_y());
        _focus_lose_clause(ent, v);
        float dist = v.magnitude();
        if (dist > 300) {
            v.set_magnitude(PLAYER_ACCELERATION * 0.975);
            ent.acceleration = v;
        } else {
            ent.acceleration.set(0,0);
        }
        ent.set_angle(v.angle());
        if (ent.ai_tick >= 1.5 * TPS && dist < 800) {
            ent.ai_tick = 0;
            //spawn missile;
            Entity &missile = alloc_petal(sim, PetalID::kMissile, ent);
            missile.damage = 10;
            missile.health = missile.max_health = 10;
            //missile.health = missile.max_health = 20;
            //missile.despawn_tick = 1;
            entity_set_despawn_tick(missile, 3 * TPS);
            missile.set_angle(ent.get_angle());
            missile.acceleration.unit_normal(ent.get_angle()).set_magnitude(40 * PLAYER_ACCELERATION);
            Vector kb;
            kb.unit_normal(ent.get_angle() - M_PI).set_magnitude(2.5 * PLAYER_ACCELERATION);
            ent.velocity += kb;            
        }
        return;
    } else {
        if (!(ent.target == NULL_ENTITY)) {
            ent.ai_state = AIState::kIdle;
            ent.ai_tick = 0;
            ent.target = NULL_ENTITY;
        }
        ent.target = find_nearest_enemy(sim, ent, ent.detection_radius);
        tick_bee_passive(sim, ent);;
    }
}

static void tick_tank_aggro(Simulation* sim, Entity& ent) {
    if (sim->ent_alive(ent.target)) {
        Entity& target = sim->get_ent(ent.target);
        Vector v(target.get_x() - ent.get_x(), target.get_y() - ent.get_y());
        _focus_lose_clause(ent, v);
        float dist = v.magnitude();

        // 运动逻辑
        if (dist < 380) {
            v.set_magnitude(-PLAYER_ACCELERATION * 0.35f); // 反向加速度
            ent.acceleration = v;
        }
        else if (dist > 400) {
            v.set_magnitude(PLAYER_ACCELERATION * 0.975f);
            ent.acceleration = v;
        }
        else {
            Vector circle_v(v.y, -v.x); // 右手法向量，顺时针
            circle_v.set_magnitude(PLAYER_ACCELERATION * 0.7f);
            ent.acceleration = circle_v;
        }

        // --- 目标位置预测与射击逻辑 ---
        const float BULLET_ACCEL = 4.0f * PLAYER_ACCELERATION;
        const float BULLET_FRICTION = DEFAULT_FRICTION * 1.5f;
        const float offset = ent.get_radius() * 1.8f;
        const int MAX_ITERATIONS = 50;

        // 子弹的发射点
        Vector spawn_offset;
        spawn_offset.unit_normal(ent.get_angle()).set_magnitude(offset);
        Vector bullet_spawn_pos(ent.get_x() + spawn_offset.x, ent.get_y() + spawn_offset.y);

        // 复制目标初始状态
        Vector simulated_target_pos = Vector(target.get_x(), target.get_y());
        Vector simulated_target_vel = Vector(target.velocity.x, target.velocity.y);
        int simulated_target_slow_ticks = target.slow_ticks;

        float lower_bound = 0.0f;
        float upper_bound = (Vector(target.get_x(), target.get_y()) - bullet_spawn_pos).magnitude() / 10.0f;

        // 二分查找最佳飞行时间
        for (int i = 0; i < MAX_ITERATIONS; ++i) {
            float mid_time = (lower_bound + upper_bound) / 2.0f;
            if (mid_time < 0.01f) {
                break;
            }

            // 模拟目标和子弹到 mid_time
            Vector current_target_pos = simulated_target_pos;
            Vector current_target_vel = simulated_target_vel;
            int current_target_slow_ticks = simulated_target_slow_ticks;

            Vector current_bullet_vel = Vector(0, 0);
            Vector current_bullet_pos = bullet_spawn_pos;

            // 临时预测方向，用于子弹加速度
            Vector temp_predicted_v = Vector(current_target_pos.x - ent.get_x(), current_target_pos.y - ent.get_y());

            for (int frame = 0; frame < (int)ceil(mid_time); ++frame) {
                if (current_target_slow_ticks > 0) {
                    current_target_vel *= 0.5f;
                    current_target_slow_ticks--;
                }
                current_target_vel *= (1.0f - target.friction);
                current_target_vel += target.acceleration * target.speed_ratio;
                current_target_pos += current_target_vel;

                current_bullet_vel *= (1.0f - BULLET_FRICTION);
                current_bullet_vel += temp_predicted_v.unit_normal(temp_predicted_v.angle()) * BULLET_ACCEL * 1.0f; // 1.0f 对应 BULLET_SPEED_RATIO
                current_bullet_pos += current_bullet_vel;
            }

            float target_dist = (current_target_pos - bullet_spawn_pos).magnitude();
            float bullet_dist = (current_bullet_pos - bullet_spawn_pos).magnitude();

            if (bullet_dist > target_dist) {
                upper_bound = mid_time;
            }
            else {
                lower_bound = mid_time;
            }
        }

        // 找到最佳飞行时间后，再次模拟目标到该时间点
        float final_flight_time = lower_bound;
        Vector final_target_pos = simulated_target_pos;
        Vector final_target_vel = simulated_target_vel;
        int final_target_slow_ticks = simulated_target_slow_ticks;

        for (int frame = 0; frame < (int)ceil(final_flight_time); ++frame) {
            if (final_target_slow_ticks > 0) {
                final_target_vel *= 0.5f;
                final_target_slow_ticks--;
            }
            final_target_vel *= (1.0f - target.friction);
            final_target_vel += target.acceleration * target.speed_ratio;
            final_target_pos += final_target_vel;
        }

        // 预测向量
        Vector predicted_v(final_target_pos.x - ent.get_x(), final_target_pos.y - ent.get_y());
        ent.set_angle(predicted_v.angle());

        // 射击
        if (ent.ai_tick >= 0.5f * TPS && dist < 800) {
            ent.ai_tick = 0;
            Entity& bullet = alloc_petal(sim, PetalID::kBullet, ent);
            entity_set_despawn_tick(bullet, 3 * TPS);
            bullet.damage = 5;
            bullet.health = bullet.max_health = 10;
            bullet.set_radius(ent.get_radius() * 0.4f);

            bullet.set_angle(predicted_v.angle());

            Vector spawn_offset_bullet;
            spawn_offset_bullet.unit_normal(ent.get_angle()).set_magnitude(offset);
            bullet.set_x(ent.get_x() + spawn_offset_bullet.x);
            bullet.set_y(ent.get_y() + spawn_offset_bullet.y);

            bullet.acceleration.unit_normal(predicted_v.angle()).set_magnitude(BULLET_ACCEL);

            // 后坐力
            Vector kb;
            kb.unit_normal(ent.get_angle() - M_PI).set_magnitude(2.5f * PLAYER_ACCELERATION);
            ent.velocity += kb;
        }

        return;
    }
    else {
        if (!(ent.target == NULL_ENTITY)) {
            ent.ai_state = AIState::kIdle;
            ent.ai_tick = 0;
            ent.target = NULL_ENTITY;
        }
        ent.target = find_nearest_enemy(sim, ent, ent.detection_radius);
        tick_bee_passive(sim, ent);
    }
}

static void tick_centipede_passive(Simulation *sim, Entity &ent) {
    switch(ent.ai_state) {
        case AIState::kIdle: {
            ent.set_angle(ent.get_angle() + 0.25 / TPS);
            if (frand() < 1 / (5.0 * TPS)) ent.ai_state = AIState::kIdleMoving;
            break;
        }
        case AIState::kIdleMoving: {
            ent.set_angle(ent.get_angle() - 0.25 / TPS);
            if (frand() < 1 / (5.0 * TPS)) ent.ai_state = AIState::kIdle;
            break;
        }
        case AIState::kReturning: {
            default_tick_returning(sim, ent);
            break;
        }
    }
    ent.acceleration.unit_normal(ent.get_angle()).set_magnitude(PLAYER_ACCELERATION / 10);
}

static void tick_centipede_neutral(Simulation *sim, Entity &ent, float speed) {
    if (sim->ent_alive(ent.target)) {
        Entity &target = sim->get_ent(ent.target);
        Vector v(target.get_x() - ent.get_x(), target.get_y() - ent.get_y());
        v.set_magnitude(PLAYER_ACCELERATION * speed);
        ent.acceleration = v;
        ent.set_angle(v.angle());
        return;
    } else {
        if (!(ent.target == NULL_ENTITY)) {
            ent.ai_state = AIState::kIdle;
            ent.ai_tick = 0;
        }
        switch(ent.ai_state) {
            case AIState::kIdle: {
                ent.set_angle(ent.get_angle() + 0.25 / TPS);
                if (frand() < 1 / (5.0 * TPS)) ent.ai_state = AIState::kIdleMoving;
                ent.acceleration.unit_normal(ent.get_angle()).set_magnitude(PLAYER_ACCELERATION * speed);
                break;
            }
            case AIState::kIdleMoving: {
                ent.set_angle(ent.get_angle() - 0.25 / TPS);
                if (frand() < 1 / (5.0 * TPS)) ent.ai_state = AIState::kIdle;
                ent.acceleration.unit_normal(ent.get_angle()).set_magnitude(PLAYER_ACCELERATION * speed);
                break;
            }
            case AIState::kReturning: {
                default_tick_returning(sim, ent);
                break;
            }
        }
    }
}

static void tick_centipede_aggro(Simulation *sim, Entity &ent) {
    if (sim->ent_alive(ent.target)) {
        Entity &target = sim->get_ent(ent.target);
        Vector v(target.get_x() - ent.get_x(), target.get_y() - ent.get_y());
        _focus_lose_clause(ent, v);
        v.set_magnitude(PLAYER_ACCELERATION * 0.95);
        ent.acceleration = v;
        ent.set_angle(v.angle());
        return;
    } else {
        if (!(ent.target == NULL_ENTITY)) {
            ent.ai_state = AIState::kIdle;
            ent.ai_tick = 0;
        }
        ent.target = find_nearest_enemy(sim, ent, ent.detection_radius + ent.get_radius());
        switch(ent.ai_state) {
            case AIState::kIdle: {
                ent.set_angle(ent.get_angle() + 0.25 / TPS);
                if (frand() < 1 / (5.0 * TPS)) ent.ai_state = AIState::kIdleMoving;
                ent.acceleration.unit_normal(ent.get_angle()).set_magnitude(PLAYER_ACCELERATION / 10);
                break;
            }
            case AIState::kIdleMoving: {
                ent.set_angle(ent.get_angle() - 0.25 / TPS);
                if (frand() < 1 / (5.0 * TPS)) ent.ai_state = AIState::kIdle;
                ent.acceleration.unit_normal(ent.get_angle()).set_magnitude(PLAYER_ACCELERATION / 10);
                break;
            }
            case AIState::kReturning: {
                default_tick_returning(sim, ent);
                break;
            }
        }
    }
}

static void tick_sandstorm(Simulation *sim, Entity &ent) {
    switch(ent.ai_state) {
        case AIState::kIdle: {
            if (frand() > 1.0f / TPS) {
                ent.ai_tick = 0;
                ent.heading_angle = frand() * 2 * M_PI;
                ent.ai_state = AIState::kIdleMoving;
            }
            Vector rand = Vector::rand(PLAYER_ACCELERATION * 0.5);
            ent.acceleration.set(rand.x, rand.y);
            break;
        }
        case AIState::kIdleMoving: {
            if (ent.ai_tick >= 2.5 * TPS) {
                ent.ai_tick = 0;
                ent.ai_state = AIState::kIdle;
            }
            if (frand() > 2.5f / TPS)
                ent.heading_angle += frand() * M_PI - M_PI / 2;
            Vector head;
            head.unit_normal(ent.heading_angle);
            head.set_magnitude(PLAYER_ACCELERATION);
            Vector rand;
            rand.unit_normal(ent.heading_angle + frand() * M_PI - M_PI / 2);
            rand.set_magnitude(PLAYER_ACCELERATION * 0.5);
            head += rand;
            ent.acceleration.set(head.x, head.y);
            break;
        }
        case AIState::kReturning: {
            default_tick_returning(sim, ent, 1.5);
            break;
        }
        default:
            ent.ai_state = AIState::kIdle;
            break;
    }
    if (sim->ent_alive(ent.get_parent())) {
        Entity &parent = sim->get_ent(ent.get_parent());
        ent.acceleration = (ent.acceleration + parent.acceleration) * 0.75;
    }
}

static void tick_digger(Simulation *sim, Entity &ent) {
    ent.input = 0;
    if (sim->ent_alive(ent.target)) {
        Entity &target = sim->get_ent(ent.target);
        Vector v(target.get_x() - ent.get_x(), target.get_y() - ent.get_y());
        _focus_lose_clause(ent, v);
        v.set_magnitude(PLAYER_ACCELERATION * 0.95);
        if (ent.health / ent.max_health > 0.1) {
            BitMath::set(ent.input, InputFlags::kAttacking);
        } else {
            BitMath::set(ent.input, InputFlags::kDefending);
            v *= -1;
        }
        ent.acceleration = v;
        ent.set_angle(v.angle());
        return;
    } else {
        if (!(ent.target == NULL_ENTITY)) {
            ent.ai_state = AIState::kIdle;
            ent.ai_tick = 0;
            ent.target = NULL_ENTITY;
        }
        ent.target = find_nearest_enemy(sim, ent, ent.detection_radius + ent.get_radius());
        switch(ent.ai_state) {
            case AIState::kIdle: {
                ent.set_angle(frand() * M_PI * 2);
                ent.ai_state = AIState::kIdleMoving;
                ent.ai_tick = 0;
                break;
            }
            case AIState::kIdleMoving: {
                if (ent.ai_tick > 5 * TPS)
                    ent.ai_state = AIState::kIdle;
                ent.acceleration.unit_normal(ent.get_angle()).set_magnitude(PLAYER_ACCELERATION);
                break;
            }
            case AIState::kReturning: {
                default_tick_returning(sim, ent);
                break;
            }
        }
    }
}

static void tick_hornet_new_ai(Simulation *sim, Entity &ent) {
    if (sim->ent_alive(ent.target)) {
        Entity &target = sim->get_ent(ent.target);
        Vector v(target.get_x() - ent.get_x(), target.get_y() - ent.get_y());
        _focus_lose_clause(ent, v);

        float target_angle = v.angle();
        float rotation_step = M_PI / (TPS / 4); 
        if (std::abs(target_angle - ent.get_angle()) > rotation_step) {
            ent.set_angle(ent.get_angle() + rotation_step * ((target_angle > ent.get_angle()) ? 1 : -1));
        } else {
            ent.set_angle(target_angle);
        }

        if (ent.ai_tick >= TPS) { 
            ent.ai_tick = 0;
            Vector missile_dir = v;
            missile_dir.set_magnitude(300.0f); 

            Entity &missile = alloc_petal(sim, PetalID::kMissile, ent);
            missile.damage = 10;
            missile.health = missile.max_health = 10;
            entity_set_despawn_tick(missile, 3 * TPS);
            missile.set_angle(missile_dir.angle());
            missile.acceleration = missile_dir;

            target_angle = ent.get_angle() - M_PI;
            if (std::abs(target_angle - ent.get_angle()) > rotation_step) {
                ent.set_angle(ent.get_angle() + rotation_step * ((target_angle > ent.get_angle()) ? 1 : -1));
            } else {
                ent.set_angle(target_angle);
            }
        }
    } else {
        if (!(ent.target == NULL_ENTITY)) {
            ent.ai_state = AIState::kIdle;
            ent.ai_tick = 0;
            ent.target = NULL_ENTITY;
        }
        ent.target = find_nearest_enemy(sim, ent, ent.detection_radius);
        tick_bee_passive(sim, ent);
    }
}

void tick_ai_behavior(Simulation *sim, Entity &ent) {
    if (ent.pending_delete) return;
    if (sim->ent_alive(ent.seg_head)) return;
    ent.acceleration.set(0,0);
    if (!(ent.get_parent() == NULL_ENTITY)) {
        if (!sim->ent_alive(ent.get_parent())) {
            if (BitMath::at(ent.flags, EntityFlags::kDieOnParentDeath))
                sim->request_delete(ent.id);
            ent.set_parent(NULL_ENTITY);
        } else {
            Entity const &parent = sim->get_ent(ent.get_parent());
            Vector delta(parent.get_x() - ent.get_x(), parent.get_y() - ent.get_y());
            if (delta.magnitude() > SUMMON_RETREAT_RADIUS) {
                ent.target = NULL_ENTITY;
                ent.ai_state = AIState::kReturning;
            }
            if (sim->ent_alive(ent.target)) {
                Entity const &target = sim->get_ent(ent.target);
                delta = Vector(parent.get_x() - target.get_x(), parent.get_y() - target.get_y());
                if (delta.magnitude() > SUMMON_RETREAT_RADIUS)
                    ent.target = NULL_ENTITY;
            }
        }
    }
    if (BitMath::at(ent.flags, EntityFlags::kIsCulled)) {
        ent.target = NULL_ENTITY;
        ent.ai_tick = 0;
        return;
    }
    if (!sim->ent_alive(ent.target) && sim->ent_alive(ent.last_damaged_by))
        ent.target = ent.last_damaged_by;
    switch(ent.get_mob_id()) {
        case MobID::kBabyAnt:            
        case MobID::kLadybug:
        case MobID::kMassiveLadybug:
            tick_default_passive(sim, ent);
            break;
        case MobID::kBee:
            tick_bee_passive(sim, ent);
            break;
        case MobID::kCentipede:
            tick_centipede_passive(sim, ent);
            break;
        case MobID::kEvilCentipede:
            tick_centipede_aggro(sim, ent);
            break;
        case MobID::kDesertCentipede:
            tick_centipede_neutral(sim, ent, 1.33);
            break;
        case MobID::kWorkerAnt:
        case MobID::kDarkLadybug:
        case MobID::kShinyLadybug:
            tick_default_neutral(sim, ent);
            break;
        case MobID::kSoldierAnt:
        case MobID::kBeetle:
        case MobID::kMassiveBeetle:
            tick_default_aggro(sim, ent, 0.95);
            break;
        case MobID::kScorpion:
            tick_default_aggro(sim, ent, 0.95);
            break;
        case MobID::kSpider:
            if (ent.lifetime % (TPS) == 0) 
                alloc_web(sim, 25, ent);
            tick_default_aggro(sim, ent, 0.95);
            break;
        case MobID::kQueenAnt:
            if (ent.lifetime % (2 * TPS) == 0) {
                Vector behind;
                behind.unit_normal(ent.get_angle() + M_PI);
                behind *= ent.get_radius();
                Entity &spawned = alloc_mob(sim, MobID::kSoldierAnt, ent.get_x() + behind.x, ent.get_y() + behind.y, ent.get_team());
                spawned.set_parent(ent.get_parent());
            }
            if (ent.lifetime + 1 % (10 * 60 * TPS) == 0) {
                Vector behind;
                behind.unit_normal(ent.get_angle() + M_PI);
                behind *= ent.get_radius();
                Entity &spawned = alloc_mob(sim, MobID::kQueenAnt, ent.get_x() + behind.x, ent.get_y() + behind.y, ent.get_team());
            }
            tick_default_aggro(sim, ent, 0.95);
            break;
        case MobID::kHornet:
            tick_hornet_aggro(sim, ent);
            break;
        case MobID::kBoulder:
        case MobID::kRock:
        case MobID::kCactus:
        case MobID::kSquare:
            break;
        case MobID::kSandstorm:
            tick_sandstorm(sim, ent);
            break;
        case MobID::kDigger:
            tick_digger(sim, ent);
            break;
        case MobID::kTank:
            tick_tank_aggro(sim, ent);
            break;
        default:
            break;
    }
    //wall avoidance
    if (!sim->ent_alive(ent.target)) {
        if (ent.get_x() - ent.get_radius() <= 0 && angle_within(ent.get_angle(), M_PI, M_PI / 2))
            ent.set_angle(M_PI - ent.get_angle());
        if (ent.get_x() + ent.get_radius() >= ARENA_WIDTH && angle_within(ent.get_angle(), 0, M_PI / 2))
            ent.set_angle(M_PI - ent.get_angle());
        if (ent.get_y() - ent.get_radius() <= 0 && angle_within(ent.get_angle(), 3 * M_PI / 2, M_PI / 2))
            ent.set_angle(0 - ent.get_angle());
        if (ent.get_y() + ent.get_radius() >= ARENA_HEIGHT && angle_within(ent.get_angle(), M_PI / 2, M_PI / 2))
            ent.set_angle(0 - ent.get_angle());
    }
    ++ent.ai_tick;
}