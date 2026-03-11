package main

import (
	"context"
	"fmt"
	"log"
	"os"
	"os/exec"

	"github.com/hashicorp/go-plugin"
	"google.golang.org/grpc"

	proto "github.com/VolumioTeam/go-plugin-cpp/example/host/proto"
)

// handshakeConfig must match the C++ plugin's ServeConfig.handshake exactly.
var handshakeConfig = plugin.HandshakeConfig{
	ProtocolVersion:  1,
	MagicCookieKey:   "GREETER_PLUGIN",
	MagicCookieValue: "hello",
}

// Greeter is the interface the host application uses.
type Greeter interface {
	SayHello(name string) (string, error)
}

// grpcGreeterClient wraps the generated gRPC stub.
type grpcGreeterClient struct {
	client proto.GreeterClient
}

func (c *grpcGreeterClient) SayHello(name string) (string, error) {
	resp, err := c.client.SayHello(context.Background(), &proto.HelloRequest{Name: name})
	if err != nil {
		return "", err
	}
	return resp.Message, nil
}

// GreeterPlugin implements plugin.GRPCPlugin so go-plugin can dispatch it.
type GreeterPlugin struct{ plugin.Plugin }

func (p *GreeterPlugin) GRPCServer(_ *plugin.GRPCBroker, _ *grpc.Server) error { return nil }
func (p *GreeterPlugin) GRPCClient(_ context.Context, _ *plugin.GRPCBroker, cc *grpc.ClientConn) (interface{}, error) {
	return &grpcGreeterClient{client: proto.NewGreeterClient(cc)}, nil
}

func main() {
	pluginBin := "./greeter_plugin"
	if len(os.Args) > 1 {
		pluginBin = os.Args[1]
	}

	client := plugin.NewClient(&plugin.ClientConfig{
		HandshakeConfig:  handshakeConfig,
		Plugins:          map[string]plugin.Plugin{"greeter": &GreeterPlugin{}},
		Cmd:              exec.Command(pluginBin),
		AllowedProtocols: []plugin.Protocol{plugin.ProtocolGRPC},
	})
	defer client.Kill()

	rpcClient, err := client.Client()
	if err != nil {
		log.Fatalf("client.Client(): %v", err)
	}

	raw, err := rpcClient.Dispense("greeter")
	if err != nil {
		log.Fatalf("Dispense: %v", err)
	}
	greeter := raw.(Greeter)

	names := []string{"World", "go-plugin", "C++"}
	for _, name := range names {
		msg, err := greeter.SayHello(name)
		if err != nil {
			log.Fatalf("SayHello(%q): %v", name, err)
		}
		fmt.Println(msg)
	}
}
