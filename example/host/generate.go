package main

//go:generate ../../vcpkg_installed/x64-linux/tools/protobuf/protoc --proto_path=../proto --go_out=proto --go_opt=paths=source_relative --go-grpc_out=proto --go-grpc_opt=paths=source_relative ../proto/greeter.proto
