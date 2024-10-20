#include "myrpc.h"
#include "echo.pb.h"
// 服务端代码
// 继承pb生成的Service类，并实现接口逻辑
class MyEchoService : public echo::EchoService {
public:
  virtual void Echo(::google::protobuf::RpcController* /* controller */,
                       const ::echo::EchoRequest* request,
                       ::echo::EchoResponse* response,
                       ::google::protobuf::Closure* done) {
      std::cout << request->msg() << std::endl;
      response->set_msg(
              std::string("I have received '") + request->msg() + std::string("'"));
      // 进行resp序列化回包，写回socket
      done->Run();
  }
};//MyEchoService

// service面临的问题是，它只是从socket拿到一系列数据，数据具体应该读取多少？请求的是哪个service的哪个方法？这是需要服务端做的事情 
int main() {
   // 需要有一个Server类来进行服务注册、数据序列化、反序列化、网络传输，分发给各个Service
   // 循环监听
    MyServer my_server;
    MyEchoService echo_service;
    my_server.add(&echo_service);
    my_server.start("127.0.0.1", 6688); // server绑定到该ip:port

    return 0;
}

/* vim: set ts=4 sw=4 sts=4 tw=100 */
