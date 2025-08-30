#pragma once

class GameInstance;

class AsymmetricBattle {
public:
    explicit AsymmetricBattle(GameInstance* game);
    void update(); // 外界每帧调用

private:
    struct AsymmetricBattleInternal;
    AsymmetricBattleInternal* internal; // 实例成员指针
};
