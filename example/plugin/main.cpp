#include <string>

#include <grpcpp/grpcpp.h>

#include "go_plugin/server.hpp"
#include "greeter.grpc.pb.h"
#include "greeter.pb.h"

class GreeterServiceImpl final : public greeter::Greeter::Service {
  grpc::Status SayHello(grpc::ServerContext * /*ctx*/, const greeter::HelloRequest *req,
                        greeter::HelloReply *reply) override {
    reply->set_message("Hello, " + req->name() + "!");
    return grpc::Status::OK;
  }
};

int main() {
  GreeterServiceImpl svc;

  auto result = go_plugin::Serve({
      .handshake =
          {
              .magic_cookie_key = "GREETER_PLUGIN",
              .magic_cookie_value = "hello",
              .protocol_version = 1,
          },
      .services = {&svc},
  });

  if (!result.ok) {
    fprintf(stderr, "greeter_plugin: %s\n", result.error.c_str());
    return 1;
  }
  return 0;
}
