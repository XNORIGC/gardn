# gardn 项目 / gardn project
开源、自托管的多人 PvP 项目，基于并致敬原始的 florr.io，使用 C++ 编写。

Open-source self-hostable multiplayer PvP project inspired by florr.io, written in C++.

详细的搭建与运行方法请参见 `INSTALLATION.md`。

See `INSTALLATION.md` for setup and running instructions.

# 关于 / About
虽然本项目深受 florr.io 启发，但代码均为原创实现。由于无法直接取得原始游戏源码，项目中的游戏逻辑是根据可见行为自行实现并进行了合理改进。

Although inspired by florr.io, the codebase is original; game logic has been independently implemented and adapted where necessary.

## ECS（实体-组件-系统） / ECS (Entity-Component-System)
本项目使用自研的实体组件系统来组织仿真与实体逻辑。实体属性通过 `Shared/EntityDef.hh` 中的宏定义，实体结构体在 `Shared/Entity.hh` 中生成。仿真系统通过多个系统/处理函数运行游戏逻辑。

This project uses a custom ECS to structure simulation and entity logic. Entity fields are defined via macros in `Shared/EntityDef.hh`, and the entity structure is generated in `Shared/Entity.hh`.

## 服务端 / Server
服务端可以编译为原生二进制（依赖 uWebSockets）或编译为 WASM 并在 Node.js 上运行（作为替代方案）。碰撞检测使用均匀网格以加速广义碰撞查询。

The server can be built as a native binary (using uWebSockets) or as WASM for Node.js. Collision detection uses a uniform spatial grid for efficient broadphase queries.

## 客户端 / Client
客户端使用 C++ 编写并通过 Emscripten 编译为 WASM/JS。渲染基于 Canvas2D，且绝大部分 UI 由自研引擎（`Client/Ui`）绘制以获得更高性能。

The client is written in C++ and compiled to WASM/JS via Emscripten. Rendering is done on Canvas2D and the UI is implemented by an in-engine layout system (`Client/Ui`) for performance.

资源（如部分精灵）来自对原始页面的逆向提取脚本（见 `Scripts/`），部分敌对单位具有基于 WASM 反向工程得到的动画。

Some assets and animations were extracted/reversed using scripts (see `Scripts/`).
