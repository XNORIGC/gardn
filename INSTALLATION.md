
# 系统要求 / Requirements

请下载安装最新版本的 Emscripten（用于将客户端编译为 WASM/JS）和 CMake。服务器/客户端均需支持 C++20 的 gcc/g++ 编译器。

Please install the latest versions of Emscripten (for compiling the client to WASM/JS) and CMake. A C++20-capable gcc/g++ is required for building server and client.

# 安装说明 / Installation

## 原生服务端（性能更好） / Native server (more performant)

1. 克隆仓库（包含子模块）：
```
git clone --recurse-submodules https://github.com/XNORIGC/gardn.git
```

Clone the repository (with submodules):
```
git clone --recurse-submodules https://github.com/XNORIGC/gardn.git
```

2. 编译 `uWebSockets`（服务端依赖），详见官方仓库说明：
```
cd gardn/Server/uWebSockets
make
```

Build uWebSockets (server dependency):
```
cd gardn/Server/uWebSockets
make
```

3. 编译并运行服务器：
```
cd gardn/Server
mkdir build
cd build
cmake ..
make
./gardn-server
```

Build and run the server:
```
cd gardn/Server
mkdir build
cd build
cmake ..
make
./gardn-server
```

## WebAssembly（WASM）服务端（不依赖 uWebSockets，但需 Node.js） / WebAssembly Server (doesn't require uWebSockets, but requires Node.js)

如果无法编译 uWebSockets，可用 WASM 服务端：
```
git clone https://github.com/XNORIGC/gardn.git
cd gardn/Server
mkdir build
cd build
cmake .. -DWASM_SERVER=1
make
npm install ws fs http
node ./gardn-server.js
```

If you cannot build uWebSockets, use the WASM server instead:
```
git clone https://github.com/XNORIGC/gardn.git
cd gardn/Server
mkdir build
cd build
cmake .. -DWASM_SERVER=1
make
npm install ws fs http
node ./gardn-server.js
```

## 客户端编译 / Client build

```
cd gardn/Client
mkdir build
cd build
# 可选：加入 -DTEST=1 以在编译时把 WS_URL 设为 localhost（用于本地测试）
cmake .. -DTEST=1 -DDEV=1
make
```

编译完成后，将生成的 `.wasm`、`.js`（以及 `html`）文件复制到 `Client/public`；若运行 WASM 服务端，也可放到 `Server/build`。

After building, copy the generated `.wasm`, `.js`, and `html` files into `Client/public`. If you run the WASM server, you may place them in `Server/build` as well.

注意：为了让客户端连接到本地 `ws://localhost:<port>`，需要在构建客户端时传入 `-DTEST=1`；我们已在 `Client/CMakeLists.txt` 中支持该选项（会把 `-DTEST` 转为编译器宏）。

Note: To force the client to connect to `ws://localhost:<port>` for testing, build the client with `-DTEST=1`. `Client/CMakeLists.txt` forwards this option to the compiler.

默认服务器地址为 `localhost:9001`（或由 `Shared/Config.cc` 中的 `WS_URL` 指定）。如需修改端口或切换 websocket 地址：

- 修改端口：编辑 `Shared/Config.cc` 中的 `SERVER_PORT` 并重建服务端/客户端；
- 切换为本地测试：在编译客户端时使用 `-DTEST=1`（详见上文）。

The server serves content by default at `localhost:9001`, or as specified by `WS_URL` in `Shared/Config.cc`. To change port or websocket address:

- Edit `SERVER_PORT` in `Shared/Config.cc` and rebuild server/client;
- Build client with `-DTEST=1` to use local `WS_URL` for testing.

# 部署 / Hosting

客户端可使用任意静态 HTTP 服务托管（如 `nginx`、`http-server`）。WASM 服务端会在 `localhost:9001` 自动托管静态内容。

Host the client with any static HTTP server (e.g., `nginx`, `http-server`). The WASM server also serves content at `localhost:9001`.

如果部署到非 `localhost` 的主机，请在 `Shared/Config.cc` 中设置 `WS_URL` 为目标 websocket 地址。

If you host on a non-localhost domain, set `WS_URL` in `Shared/Config.cc` to your websocket URL.

# 编译选项 / Compilation Flags

- `DEBUG`（Server & Client，默认 0）：开启断言与调试功能。  
- `WASM_SERVER`（仅 Server，默认 0）：编译为 WASM/JS 在 Node 上运行（替代原生 uWebSockets 服务端）。  
- `TDM`（仅 Server，默认 0）：启用团队 deathmatch 模式（TDM）。  
- `GENERAL_SPATIAL_HASH`（仅 Server，默认 0）：使用通用空间哈希以支持更大实体。  
- `USE_CODEPOINT_LEN`（Server & Client，默认 0）：在字符串验证/截断时使用字符数（codepoints）而非字节数，适用于非英文字符；服务端与客户端应一致。

- `DEBUG` (Server & Client, default 0): enable assertions and debug features.
- `WASM_SERVER` (Server only, default 0): build server as WASM/JS for Node.js instead of native binary.
- `TDM` (Server only, default 0): enable team deathmatch mode.
- `GENERAL_SPATIAL_HASH` (Server only, default 0): use canonical spatial hash for large entities.
- `USE_CODEPOINT_LEN` (Server & Client, default 0): use codepoint count for string validation/truncation (useful for non-English characters); ensure both server and client use same setting.

# License
[LICENSE](./LICENSE)