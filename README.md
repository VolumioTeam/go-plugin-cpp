# go-plugin-cpp

A CMake/C++ library that lets you write [hashicorp/go-plugin](https://github.com/hashicorp/go-plugin) plugins in C++,
using **gRPC** as the transport (net/RPC is not supported).

## How it works

go-plugin launches a plugin as a child process and communicates via gRPC over loopback TCP. The child process (plugin)
signals readiness by printing a single *handshake line* to stdout:

```
CORE-PROTO|APP-PROTO|tcp|127.0.0.1:PORT|grpc|SERVER-CERT
```

This library handles:

- **Magic-cookie validation** – aborts startup if the binary is run directly rather than by a go-plugin host.
- **Port selection** – respects `PLUGIN_MIN_PORT`/`PLUGIN_MAX_PORT` if set by the host.
- **Auto-mTLS** – when the host sets `PLUGIN_CLIENT_CERT`, a fresh server certificate is generated and mutual TLS is
  configured automatically.
- **Handshake line output** – writes the correctly formatted line to stdout so the host can connect.
- **Health-check service** – the built-in gRPC health-check service is registered automatically.
- **Logging the host can read** – see below.

## Logging

A host parses a plugin's standard error as hclog JSON and reads nothing else. A line in any other shape reaches the
host's logs as one opaque string at the host's own level, with the plugin's severity and fields buried inside it — so
a plugin error cannot surface as an error, and nothing downstream can filter on a field.

`go_plugin::log` writes that format. It depends on nothing but the standard library:

```cpp
#include "go_plugin/log.hpp"

go_plugin::log::Info("sink opened", {{"rate", 48000}, {"path", pipe_path}});
go_plugin::log::Error("write failed", {{"error", strerror(errno)}});
```

### Bridging an existing logging library

A plugin that already logs through a library keeps its call sites and installs a bridge. Each backend is a separate
target, so a plugin links only the one it uses:

| Backend | Target | Header | Install with |
|---------|--------|--------|--------------|
| Abseil (`LOG`/`VLOG`) | `go_plugin::go_plugin_log_absl` | `go_plugin/log_absl.hpp` | `go_plugin::log::InstallAbslBridge()` |

```cpp
absl::InitializeLog();
go_plugin::log::InstallAbslBridge();   // LOG(WARNING) now reaches the host as a warning
```

Abseil has no debug or trace severity of its own — they exist only as `VLOG` verbosities — so the bridge maps `VLOG(1)`
to debug and `VLOG(2)` and above to trace, and it stops Abseil writing its own copy of each line to standard error.

To add another backend, translate its records into `go_plugin::log::Submit` and add a target beside the Abseil one;
nothing in the core changes.

### Logging from a C library

A C library that writes its own diagnostics can be routed through the same path rather than left to print unattributed
text. FFmpeg, for example, takes a callback, which lets the library's own name travel as a field instead of a pointer
address that makes every line unique:

```cpp
av_log_set_level(AV_LOG_WARNING);
av_log_set_callback([](void *avcl, int level, const char *fmt, va_list args) {
  // format into a buffer, then:
  go_plugin::log::Write(LevelFor(level), text, {{"avclass", av_default_item_name(avcl)}});
});
```

## Building

### Prerequisites

- CMake ≥ 3.20
- A C++17 compiler
- [vcpkg](https://github.com/microsoft/vcpkg) with the `VCPKG_ROOT` environment variable set (or pass
  `-DCMAKE_TOOLCHAIN_FILE`)

```bash
cmake -B build \
      -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Running the tests

```bash
cd build
ctest --output-on-failure
```

### Running the example

Build the project first, then:

```bash
cd example/host
go run . ../../build/example/greeter_plugin
```
