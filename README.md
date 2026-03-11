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
