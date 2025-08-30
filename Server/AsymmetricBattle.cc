#include "AsymmetricBattle.hh"
#include <Server/Game.hh>         // GameInstance (Server::game)
#include <Server/EntityFunctions.hh>    // inflict_damage, DamageType
#include <Shared/Entity.hh>
#include <Shared/StaticData.hh>           // MobID, ColorID
#include <chrono>
#include <cmath>
#include <string>
#include <unistd.h>
#include <cstdlib>


struct AsymmetricBattle::AsymmetricBattleInternal {
    explicit AsymmetricBattleInternal(GameInstance* game)
        : game_instance(game),
        started(false),
        finished(false),
        restart_message_sent(false),
        countdown_seconds(1800),
        last_broadcast_second(-1),
        winner_color(-1) {
    }



    void tick() {
        using namespace std::chrono;

        // 如果已经结束，则持续击杀非胜利队玩家并处理重启消息
        if (finished) {
            kill_losers_continuously();
            // 新增逻辑：在游戏结束10秒后播报服务器重启
            auto now = steady_clock::now();
            int elapsed_since_finish = static_cast<int>(duration_cast<seconds>(now - finish_time).count());
            if(elapsed_since_finish >= 10) {
                game_instance->broadcast_message("SERVER RESTARTING...");
                throw;
            }
            return;
        }

        // 首次启动倒计时
        if (!started) {
            started = true;
            start_time = steady_clock::now();
            last_broadcast_second = countdown_seconds;
        }

        // 先检测场上是否存在 TargetDummy（若不存在 -> BLUE 胜利）
        {
            bool dummy_exists = false;
            Simulation& sim = game_instance->simulation;
            for (uint16_t i = 0; i < ENTITY_CAP; ++i) {
                EntityID id(i, 0);
                if (!sim.ent_exists(id)) continue;
                Entity& ent = sim.get_ent(id);
                if (ent.mob_id == MobID::kTargetDummy) {
                    dummy_exists = true;
                    break;
                }
            }
            if (!dummy_exists) {
                // BLUE 胜利
                finished = true;
                finish_time = steady_clock::now(); // 记录结束时间
                winner_color = static_cast<int>(ColorID::kBlue);
                game_instance->broadcast_message("BLUE HAS WON THE GAME!");
                // 立刻也对输家执行一次击杀（后续每帧也会继续杀）
                kill_losers_continuously();
                return;
            }
        }

        // 计算剩余时间（基于系统时间）
        auto now = steady_clock::now();
        int elapsed = static_cast<int>(duration_cast<seconds>(now - start_time).count());
        int remaining = countdown_seconds - elapsed;
        if (remaining <= 0) {
            // RED 胜利
            finished = true;
            finish_time = steady_clock::now(); // 记录结束时间
            winner_color = static_cast<int>(ColorID::kRed);
            game_instance->broadcast_message("RED HAS WON THE GAME!");
            // 立刻执行一次击杀（后续每帧也会继续杀）
            kill_losers_continuously();
            return;
        }

        // ==================== 新的播报逻辑开始 ====================

        bool should_broadcast = false;

        if (remaining > 60) {
            // 剩余时间 > 1 分钟，检查是否跨越了 2 分钟（120秒）的边界
            // 通过整数除法来确定时间所属的“块”，如果块不同，则播报
            if ((remaining / 120) != (last_broadcast_second / 120)) {
                should_broadcast = true;
            }
        }
        else { // remaining <= 60
            // 剩余时间 <= 1 分钟
            // 1. 如果上次播报时时间还在 60 秒以上，说明是刚进入最后一分钟，需要立即播报
            if (last_broadcast_second > 60) {
                should_broadcast = true;
            }
            // 2. 如果之前和现在都在 60 秒以内，则检查是否跨越了 10 秒的边界
            else {
                // 使用向上取整到 10 秒的逻辑判断
                if (((remaining + 9) / 10) != ((last_broadcast_second + 9) / 10)) {
                    should_broadcast = true;
                }
            }
        }

        if (should_broadcast) {
            last_broadcast_second = remaining;
            std::string msg;
            if (remaining > 60) {
                int minutes = static_cast<int>(std::ceil(remaining / 60.0));
                msg = std::to_string(minutes) + " minutes until RED wins!";
            }
            else {
                // 当 remaining <= 60 时，直接显示秒数
                msg = std::to_string(remaining) + " seconds until RED wins!";
            }
            game_instance->broadcast_message(msg);
        }
        // ==================== 新的播报逻辑结束 ====================
    }

    // 持续击杀非胜利队玩家（在 finished==true 时被调用）
    void kill_losers_continuously() {
        Simulation& sim = game_instance->simulation;
        if (winner_color < 0) return;

        for (uint16_t i = 0; i < ENTITY_CAP; ++i) {
            EntityID id(i, 0);
            Entity& ent = sim.get_ent(id);
            if (!ent.has_component(kFlower)) continue;
            if (static_cast<int>(ent.color) != winner_color) {
                ent.health = 0; // 直接改 health，和 killallmobs 命令一致
            }
        }
    }

    GameInstance* game_instance;
    bool started;
    bool finished;
    bool restart_message_sent; // 新增一个标志位，防止重复播报
    const int countdown_seconds; // 总秒数（1800）
    int last_broadcast_second;    // 上次记录的秒数（用于避免重复广播）
    int winner_color;            // -1 未定，或 ColorID 值
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point finish_time; // 新增，记录游戏结束时间
};

// ---------------- AsymmetricBattle 对外接口 ----------------
AsymmetricBattle::AsymmetricBattle(GameInstance* game)
    : internal(new AsymmetricBattleInternal(game)) {
}

void AsymmetricBattle::update() {
    internal->tick();
}