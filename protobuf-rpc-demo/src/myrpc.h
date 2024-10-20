#include <iostream>
#include "rpc_meta.pb.h"
#include "google/protobuf/service.h"
#include "google/protobuf/stubs/common.h"
#include "boost/asio.hpp"
#include "boost/scoped_ptr.hpp"
#include "boost/shared_ptr.hpp"
#include "boost/make_shared.hpp"

class MyController : public ::google::protobuf::RpcController {
public:
  virtual void Reset() { };

  virtual bool Failed() const { return false; };
  virtual std::string ErrorText() const { return ""; };
  virtual void StartCancel() { };
  virtual void SetFailed(const std::string& /* reason */) { };
  virtual bool IsCanceled() const { return false; };
  virtual void NotifyOnCancel(::google::protobuf::Closure* /* callback */) { };
};//MyController

// 客户端发送数据，继承自RpcChannel
class MyChannel : public ::google::protobuf::RpcChannel {
public:
    void init(const std::string& ip, const int port) {
        _io = boost::make_shared<boost::asio::io_service>();
        _sock = boost::make_shared<boost::asio::ip::tcp::socket>(*_io);
        boost::asio::ip::tcp::endpoint ep(
                boost::asio::ip::address::from_string(ip), port);
        _sock->connect(ep);
    }

    virtual void CallMethod(const ::google::protobuf::MethodDescriptor* method,
            ::google::protobuf::RpcController* /* controller */,
            const ::google::protobuf::Message* request,
            ::google::protobuf::Message* response,
            ::google::protobuf::Closure*) {
        std::string serialzied_data = request->SerializeAsString();

        myrpc::RpcMeta rpc_meta;
        rpc_meta.set_service_name(method->service()->name());
        rpc_meta.set_method_name(method->name());
        rpc_meta.set_data_size(serialzied_data.size());

        std::string serialzied_str = rpc_meta.SerializeAsString();

        // 将要发送的数据长度插到字符串最前面，这样服务端在解析时，能够知道本次请求一次性读取多少数据长度
        int serialzied_size = serialzied_str.size();
        serialzied_str.insert(0, std::string((const char*)&serialzied_size, sizeof(int)));
        serialzied_str += serialzied_data;

        // 发送数据
        // rpc_meta大小（定长4字节)|rpc_meta序列化数据（不定长）|request序列化数据（不定长）
        _sock->send(boost::asio::buffer(serialzied_str));

        // 接收4个字节的数据，char类型，表示res数据整体大小
        char resp_data_size[sizeof(int)];
        _sock->receive(boost::asio::buffer(resp_data_size));

        int resp_data_len = *(int*)resp_data_size;
        std::vector<char> resp_data(resp_data_len, 0);
        _sock->receive(boost::asio::buffer(resp_data));

        response->ParseFromString(std::string(&resp_data[0], resp_data.size()));
    }

private:
    boost::shared_ptr<boost::asio::io_service> _io;
    boost::shared_ptr<boost::asio::ip::tcp::socket> _sock;
};//mychannel

class MyServer {
public:
    void add(::google::protobuf::Service* service) {
        ServiceInfo service_info;
        service_info.service = service;
        service_info.sd = service->GetDescriptor();
        for (int i = 0; i < service_info.sd->method_count(); ++i) {
            service_info.mds[service_info.sd->method(i)->name()] = service_info.sd->method(i);
        }

        _services[service_info.sd->name()] = service_info;
    }

    void start(const std::string& ip, const int port);

private:
    void dispatch_msg(
            const std::string& service_name,
            const std::string& method_name,
            const std::string& serialzied_data,
            const boost::shared_ptr<boost::asio::ip::tcp::socket>& sock);
    void on_resp_msg_filled(
            ::google::protobuf::Message* recv_msg,
            ::google::protobuf::Message* resp_msg,
            const boost::shared_ptr<boost::asio::ip::tcp::socket> sock);
    void pack_message(
            const ::google::protobuf::Message* msg,
            std::string* serialized_data) {
        int serialized_size = msg->ByteSize();
        serialized_data->assign(
                    (const char*)&serialized_size,
                    sizeof(serialized_size));
        msg->AppendToString(serialized_data);
    }

private:
    struct ServiceInfo{
        ::google::protobuf::Service* service;
        const ::google::protobuf::ServiceDescriptor* sd;
        std::map<std::string, const ::google::protobuf::MethodDescriptor*> mds;
    };//ServiceInfo

    // server端记录Service所有的方法
    //service_name -> {Service*, ServiceDescriptor*, MethodDescriptor* []}
    std::map<std::string, ServiceInfo> _services;
};//MyServer

void MyServer::start(const std::string& ip, const int port) {
    boost::asio::io_service io;
    boost::asio::ip::tcp::acceptor acceptor(
            io,
            boost::asio::ip::tcp::endpoint(
                boost::asio::ip::address::from_string(ip),
                port));

    while (true) {
        auto sock = boost::make_shared<boost::asio::ip::tcp::socket>(io);
        acceptor.accept(*sock);

        std::cout << "recv from client:"
            << sock->remote_endpoint().address()
            << std::endl;

        //接收meta长度
        char meta_size[sizeof(int)];
        sock->receive(boost::asio::buffer(meta_size));

        int meta_len = *(int*)(meta_size);

        //接收meta数据
        std::vector<char> meta_data(meta_len, 0);
        sock->receive(boost::asio::buffer(meta_data));

        myrpc::RpcMeta meta;
        meta.ParseFromString(std::string(&meta_data[0], meta_data.size()));

        //接收数据本身
        std::vector<char> data(meta.data_size(), 0);
        sock->receive(boost::asio::buffer(data));

        //
        dispatch_msg(
                meta.service_name(),
                meta.method_name(),
                std::string(&data[0], data.size()),
                sock);
    }
}

void MyServer::dispatch_msg(
        const std::string& service_name,
        const std::string& method_name,
        const std::string& serialzied_data,
        const boost::shared_ptr<boost::asio::ip::tcp::socket>& sock) {
    auto service = _services[service_name].service;
    auto md = _services[service_name].mds[method_name];

    std::cout << "recv service_name:" << service_name << std::endl;
    std::cout << "recv method_name:" << method_name << std::endl;
    std::cout << "recv type:" << md->input_type()->name() << std::endl;
    std::cout << "resp type:" << md->output_type()->name() << std::endl;

    // 根据GetRequestPrototype + 方法名，获取到对应的请求和响应message类型，构造具体的对象
    // GetRequestPrototype在pb.cc文件有实现
    auto recv_msg = service->GetRequestPrototype(md).New();
    recv_msg->ParseFromString(serialzied_data);
    auto resp_msg = service->GetResponsePrototype(md).New();

    MyController controller;
    auto done = ::google::protobuf::NewCallback(
            this,
            &MyServer::on_resp_msg_filled,
            recv_msg,
            resp_msg,
            sock);
    // CallMethod: pb生成的接口，根据method的descriptor通过反射调用相应的接口
    service->CallMethod(md, &controller, recv_msg, resp_msg, done);
}

void MyServer::on_resp_msg_filled(
        ::google::protobuf::Message* recv_msg,
        ::google::protobuf::Message* resp_msg,
        const boost::shared_ptr<boost::asio::ip::tcp::socket> sock) {
    boost::scoped_ptr<::google::protobuf::Message> recv_msg_guard(recv_msg);
    boost::scoped_ptr<::google::protobuf::Message> resp_msg_guard(resp_msg);

    std::string resp_str;
    pack_message(resp_msg, &resp_str);

    sock->send(boost::asio::buffer(resp_str));
}
/* vim: set ts=4 sw=4 sts=4 tw=100 */
