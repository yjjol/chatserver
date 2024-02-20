#include "chatserver.hpp"
#include "json.hpp"
#include "chatservice.hpp"

#include <iostream>
#include <functional>
#include <string>
using namespace std;
using namespace placeholders;
using json = nlohmann::json;

// 初始化聊天服务器对象
ChatServer::ChatServer(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const string &nameArg)
    : _server(loop, listenAddr, nameArg), _loop(loop)
{
    // 注册链接回调
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));

    // 注册消息回调
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));

    // 设置线程数量
    _server.setThreadNum(4);
}

// 启动服务
void ChatServer::start()
{
    _server.start();
}

// 上报链接相关信息的回调函数
void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    // 客户端断开链接
    if (!conn->connected())
    {
        ChatService::instance()->clientCloseException(conn);
        conn->shutdown();
    }
}

// 上报读写事件相关信息的回调函数
void ChatServer::onMessage(const TcpConnectionPtr &conn,
                           Buffer *buffer,
                           Timestamp time)
{
    string buf = buffer->retrieveAllAsString();

    // 测试，添加json打印代码
    cout << buf << endl;

    // 数据的反序列化
    json js = json::parse(buf);
    // 达到的目的：完全解耦网络模块的代码和业务模块的代码
    // 通过js["msgid"] 获取=》业务handler=》conn  js  time
    auto msgHandler = ChatService::instance()->getHandler(js["msgid"].get<int>());
    // 回调消息绑定好的事件处理器，来执行相应的业务处理
    msgHandler(conn, js, time);
}

/*
***ChatServer类是服务器的设置（网络部分），ChatService类是服务器处理业务的相关设置（业务部分），实现网络和业务的解耦***
ChatServer类中利用onMessage()函数中的msgHandler函数进入业务处理：具体实现解释如下：
    在ChatService类中定义了一个_msgHandlerMap的hash表(<int,MsgHandler>)，可以看到，键值分别为msgid和相应的业务处理函数MsgHandler
    MsgHandler的设计：
        using MsgHandler = std::function<void(const TcpConnectionPtr &conn, json &js, Timestamp)>;//MsgHandler是封装的这个函数
        ChatService::ChatService(){}这个构造函数进行初始化注册不同的业务处理函数
        return _msgHandlerMap[msgid];在MsgHandler ChatService::getHandler(int msgid){}中根据不同的msgid返回对应的业务处理函数
        至此，就实现了在网络通信层根据msgid去业务处理层调用已经注册好的业务处理函数，进行一系列的业务处理
        
***下一步就是把业务层的代码和数据层的代码分开！！！***
先讲数据层的代码，db.c进行数据库的连接，其中，connect()进行数据库的连接返回数据的操作对象指针，update()和query()都是向数据库提交sql命令的，只是query会返回操作结果
user.c是user对象，不能说成对象，因为每次操作user表都是用user对象，进行操作的获取和设置操作
usermodel.c才是利用user对象进行user表的具体操作方法，if (mysql.connect())和if (mysql.update(sql))这两个判断的作用是先进行数据库连接，在向数据库发送sql指令。
至此，数据库主要代码框架就这样，在usermodel.c中的每个方法中传入一个user对象或者是数据主键id啥的，就可以进行对数据库的相关操作，并返回需要的值
上面可以说是数据层的所有内容了，在业务层只需要利用网络层获取的json数据，初始化好一个user对象，再把user对象传入usermodel相应方法就行。
可以看ChatService类中的reg注册方法，操作好数据库后，在设置json好数据通过网络层返回给客户端就行

*/

/*
关于第24节线程安全的理解：
    {lock_guard<mutex> lock(_connMutex);_userConnMap.insert({id, conn});}定义一个_userConnMap的哈希表记录用户和通信连接指针，当有用户登录时进行记录
    当别人要给他发消息时，先查询_userConnMap表中是否有该用户的记录，有的话说明他现在在线，则直接推送消息给他。若找不到，说明离线，则把消息存入数据库中，待对方登录后自动从数据库中拿消息。
*/

/*
关于第28节用户异常退出的理解：
    当通过ctrl+c退出用户时，数据库中的状态并不会改成ofline,下次登录就会显示用户已经登录，登不上
    所以，当用户异常退出时，我们要对用户的状态进行重置
    main.c中定义一个resetHandler()，通过usermodel的reset()利用sql指令把inline改为ofline
    在main.c中的main函数中设置个信号进行监视，当信号断掉后，进行reset操作
*/