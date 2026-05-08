# Movie Collection Manager

A **multi-user, real-time desktop application** for managing shared movie collections, built in modern **C++17**. Multiple users on a network can add, edit, and delete movies simultaneously — every change is broadcast instantly to all connected clients.

> Developed by **Hackrizen** as part of the CodingBurgas Sprint Project (2025–2026).

---

## Team

| Name | Role |
|------|------|
| Rafail Kolibarov | Scrum-Trainer |
| Petar Kapralev | Developer |
| Nikolai Peshev | Developer |
| Hristiyan Mihailov | Developer |

---

## Features

- **Real-time collaboration** — changes made by any client are immediately reflected on all others
- **Native desktop GUI** — powered by [Dear ImGui](https://github.com/ocornut/imgui) (GLFW + OpenGL3), with a sortable and searchable movie table
- **JSON persistence** — the server automatically saves the collection to a `.json` file
- **WebSocket hub** — low-latency, atomic updates via [IXWebSocket](https://github.com/machinezone/IXWebSocket)
- **Search & sort** — linear and binary search, recursive quicksort by any field
- **Statistics** — recursive total duration aggregation and average rating calculation
- **Input validation** — enforced at the logic layer before any state change

---

## Architecture

The project follows a strict **three-tier structural programming** design. No global state, no application-defined classes, no member functions — only structs and free functions.

```
┌─────────────────────────────────────────────┐
│           Presentation Layer                │
│  include/presentation.h / src/presentation  │
│  Dear ImGui GUI — never touches data layer  │
├─────────────────────────────────────────────┤
│              Logic Layer                    │
│     include/logic.h / src/logic.cpp         │
│  Quicksort, search, validation, statistics  │
├─────────────────────────────────────────────┤
│               Data Layer                    │
│      include/data.h / src/data.cpp          │
│  Movie/Collection structs, CRUD, JSON I/O   │
└─────────────────────────────────────────────┘

┌─────────────────────────────────────────────┐
│            Network / Protocol               │
│  include/network.h   include/protocol.h     │
│  src/network.cpp     src/protocol.cpp       │
│  IXWebSocket server & client, JSON wire     │
└─────────────────────────────────────────────┘
```

| Module | Files | Responsibility |
|---|---|---|
| **Data** | `include/data.h`, `src/data.cpp` | Thread-safe `Movie` / `Collection` structs, CRUD helpers, JSON persistence |
| **Logic** | `include/logic.h`, `src/logic.cpp` | Recursive quicksort, linear + binary search, duration aggregation, validation |
| **Presentation** | `include/presentation.h`, `src/presentation.cpp` | Dear ImGui desktop GUI — sortable, searchable, selectable table |
| **Protocol** | `include/protocol.h`, `src/protocol.cpp` | JSON wire format with strongly-typed `Message` structs |
| **Network** | `include/network.h`, `src/network.cpp` | IXWebSocket server (owns canonical state, broadcasts events) and client |

---

## Building

**Requirements:** CMake ≥ 3.20, a C++17 compiler, OpenGL drivers.

All other dependencies — [nlohmann/json](https://github.com/nlohmann/json), [IXWebSocket](https://github.com/machinezone/IXWebSocket), [GLFW](https://www.glfw.org/), [Dear ImGui](https://github.com/ocornut/imgui) — are fetched automatically by CMake's `FetchContent` on the first configure. No manual installs needed.

### Windows (MSVC)

```bash
cmake -S . -B build
cmake --build build --config Debug -j
```

**Server only** (no GUI, no OpenGL required):

```bash
cmake -S . -B build -DMCM_BUILD_CLIENT=OFF
cmake --build build --config Debug -j
```

### Linux / macOS

```bash
cmake -S . -B build
cmake --build build -j
```

---

## Running

The application runs as two separate processes: a **server hub** and one or more **client GUIs**.

### Windows

Open two separate terminal windows.

```bash
# Terminal 1 — server (persists collection to movies.json)
.\build\Debug\mcm.exe server 9275 movies.json

# Terminal 2+ — client GUI (repeat for each user)
.\build\Debug\mcm.exe client 127.0.0.1 9275
```

> For a Release build, replace `Debug` with `Release` in the paths above.

### Linux / macOS

```bash
# Terminal 1 — server
./build/mcm server 9275 movies.json

# Terminal 2+ — client GUI
./build/mcm client 127.0.0.1 9275
```

To connect from **another machine on the network**, replace `127.0.0.1` with the server's local IP address.

---

## Multi-User Workflow

1. **Start one server** on any machine on the network.
2. **Launch any number of clients**, each pointing at the server's IP and port.
3. Every add / update / delete performed by any client is:
   - validated and applied by the server atomically
   - persisted to `movies.json` within one second
   - broadcast in real time to every connected GUI

The client status bar shows **ONLINE** when connected. If the server isn't reachable at startup, click **Connect** once the server is running.

---

## Project Structure

```
.
├── include/
│   ├── data.h
│   ├── logic.h
│   ├── network.h
│   ├── presentation.h
│   └── protocol.h
├── src/
│   ├── data.cpp
│   ├── logic.cpp
│   ├── main.cpp
│   ├── network.cpp
│   ├── presentation.cpp
│   └── protocol.cpp
├── build/
├── out/
├── CMakeLists.txt
└── README.md
```

---

## Tech Stack

| Technology | Purpose |
|---|---|
| C++17 | Core language |
| CMake + FetchContent | Build system & dependency management |
| Dear ImGui | Desktop GUI |
| GLFW + OpenGL3 | Window & rendering backend for ImGui |
| IXWebSocket | WebSocket server and client |
| nlohmann/json | JSON serialization / persistence |

### Development
![C++](https://img.shields.io/badge/C%2B%2B-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)
![Visual Studio](https://img.shields.io/badge/Visual%20Studio-5C2D91?style=for-the-badge&logo=visual-studio&logoColor=white)
![Git](https://img.shields.io/badge/Git-F05032?style=for-the-badge&logo=git&logoColor=white)
![GitHub](https://img.shields.io/badge/GitHub-181717?style=for-the-badge&logo=github&logoColor=white)
![Microsoft Word](https://img.shields.io/badge/Microsoft%20Word-2B579A?style=for-the-badge&logo=microsoft-word&logoColor=white)
![Microsoft PowerPoint](https://img.shields.io/badge/Microsoft%20PowerPoint-B7472A?style=for-the-badge&logo=microsoft-powerpoint&logoColor=white)

---

## Docker

The server can be run inside Docker (no display required — server-only build, no OpenGL/GLFW).

### Quick start

```bash
# Build the image
docker build -t mcm-server .

# Run the server (persists collection to a named volume)
docker run -p 9275:9275 -v mcm-data:/data mcm-server
```

### Docker Compose

```bash
docker compose up --build   # build and start
docker compose up -d        # start in background
docker compose down         # stop and remove containers
docker compose logs -f      # stream logs
```

### Connecting a local client to the containerised server

```bash
# Windows
.\build\Debug\mcm.exe client 127.0.0.1 9275

# Linux / macOS
./build/mcm client 127.0.0.1 9275
```

### Custom port

```bash
docker run -p 8080:8080 mcm-server 8080 /data/collection.json
```

### Wipe saved data

```bash
docker volume rm mcm-data
```

---

## License

This project was created for educational purposes as part of the **CodingBurgas 2025–2026 Sprint Programme**.
