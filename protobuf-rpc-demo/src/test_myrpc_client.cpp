#include <iostream>
#include "echo.pb.h"
#include "myrpc.h"

int main() {
    MyChannel channel;// 进行客户端数据序列化（调用pb的序列化接口）、网络传输的工作，
    channel.init("127.0.0.1", 6688);

    echo::EchoRequest request;
    echo::EchoResponse response;
    request.set_msg("hello, myrpc.");

    // 内部实际通过channel的callMethod实现数据发送
    echo::EchoService_Stub stub(&channel);
    // 参数控制
    MyController cntl;
    // 调用channel.callmethod发送数据、处理resp数据
    stub.Echo(&cntl, &request, &response, NULL);
    std::cout << "resp:" << response.msg() << std::endl;

    return 0;
}

/* vim: set ts=4 sw=4 sts=4 tw=100 */
