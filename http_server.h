#pragma once 
//基于哈希表来实现的
//增删查改的时间复杂度是O(1)
#include <unordered_map>
 
namespace http_server{

typedef std::unordered_map<std::string,std::string> Header;

//前置声明
class HttpServer;

//请求结构
struct Request{
  std::string method; //表示方法
  std::string url; //表示地址
  //例如:url形如 http://www.baidu.com/index.html?kwd="cpp"
  std::string url_path; //url路径名 /index.html
  std::string query_string; //url键值对参数 kwd="cpp"
  //std::string version; //版本号，暂时先不考虑
  Header header; //一组字符串键值对
  std::string body; //表示内容实体
};  

//响应结构
struct Response{
  int code; // 状态码
  std::string desc; //状态码的描述
  //std::string version; //版本号
  
  //下面这连个变量专门给静态页面使用的
  //当前请求如果是请求静态页面，这两个字段就被填充
  //并且 cgi_resp字段为空
  Header header; //响应报文中的header 数据
  std::string body; // 响应报文中的body 数据
  
  //这个变量专门给CGI使用，如果当前请求是CGI的话，cgi_resp就会被 CGI程序进行填充，header和body两个字段为空
  //表示CGI程序返回给父进程的内容，包含了部分header和body，
  //引入这个变量是避免解析 CGI 程序返回的内容，
  //因为这部分内容可以直接返回 到socket 中
  std::string cgi_resp;
};

//当前请求的上下文，包含了这次请求的所有需要的中间数据
//最大的好处：方便进行扩展,整个处理请求的过程中，每个环节都能拿到所有和这次请求相关的数据


struct Context{
  Request req;
  Response resp;
  int new_sock;
  int file_fd;
  HttpServer* server;
};

//实现核心流程的类
class HttpServer{
  //以下的几个函数，返回0表示成功，返回小于0表示执行失败
public:
  //初始化模块
  //表示服务器启动
  //什么是const 引用？引用是别名，对应同一个对象同一块内存
  int Start(const std::string& ip,short port);
private:
  //根据HTTP请求字符串，进行反序列化，从socket中读取一个字符串，输出Request 对象
  int ReadOneRequest(Context* context);
  //根据Response 对象，拼接成一个字符串，写回到客户端
  int WriteOneResponse(Context* context);
  //根据 Request 对象，构造Response 对象
  int HandlerRequest(Context* context);
  int Process404(Context* context);
  int ProcessStaticFile(Context* context);
  int ProcessCGI(Context* context);
  void GetFilePath(const std::string& url_path,std::string* file_path);

  //静态成员函数，把这个类也当作命名空间
  static void* ThreadEntry(void* arg);
  int ParseFirstLine(const std::string& first_line,std::string* method,std::string* url);
  int ParseUrl(const std::string& url,std::string* url_path,std::string* query_string);
  
  int ParseHeader(const std::string& header_line,Header* header);
 
  //下面为测试函数
  void PrintRequest(const Request& req);
};

}//end of http_server 


