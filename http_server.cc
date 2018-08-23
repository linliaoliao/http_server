#include "http_server.h"
#include "util.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sstream>
#include <sys/wait.h>

typedef struct sockaddr sockaddr;
typedef struct sockaddr_in sockaddr_in;

namespace http_server{

//建立socket
int HttpServer::Start(const std::string& ip,short port){
  int listen_sock = socket(AF_INET,SOCK_STREAM,0);
  if(listen_sock < 0){
    perror("socket");
    return -1;
  }

  //要给socket加上一个选项，能够重用连接
  int opt = 1;
  setsockopt(listen_sock,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(ip.c_str());
  addr.sin_port = htons(port);
  //绑定
  int ret = bind(listen_sock,(sockaddr*)&addr,sizeof(addr));
  if(ret < 0){
    perror("bind");
    return -1;
  }
  //监听
  ret = listen(listen_sock,5);
  if(ret < 0){
    perror("listen");
    return -1;
  }
  //printf("ServerStart ok!\n");
  LOG(INFO) << "ServerStart ok!\n";
  while(1){
   //基于多线程来实现一个TCP服务器
   sockaddr_in peer;
   socklen_t len = sizeof(peer);
   int new_sock = accept(listen_sock,(sockaddr*)&peer,&len);
   if(new_sock < 0){
     perror("accept");
     continue;
  }
 //如果成功后，创建新线程，使用新线程完成此次请求的计算
  pthread_t tid;
  Context* context = new Context();
  context->new_sock = new_sock;
  context->server = this; // 使用this指针调用类成员函数
  pthread_create(&tid,NULL,ThreadEntry,reinterpret_cast<void*>(context));
  pthread_detach(tid);
  }
  close(listen_sock);
  return 0;
}


//线程入口函数
//static只需要在声明的时候加上，不用在定义的时候加static
void* HttpServer::ThreadEntry(void* arg){
  //准备工作
  Context* context = reinterpret_cast<Context*>(arg);
  HttpServer* server = context->server;
  //1.从文件描述符中读取数据，转换成Request 对象
  int ret = 0;
  ret = context->server->ReadOneRequest(context);
  if(ret < 0){
    LOG(ERROR) << "ReadOneRequest error!" << "\n";
    //用这个函数构造404的HTTP响应对象
    server->Process404(context);
    goto END;
  }


  //TEST测试 通过以下函数将一个解析出来的请求打印出来
  server->PrintRequest(context->req); 
  
  //2.把Request 对象计算生成 Response 对象
  ret = server->HandlerRequest(context);
  if(ret < 0){
    LOG(ERROR) << "HandlerRequest error!" << "\n";
    //用这个函数构造404的HTTP响应对象
    server->Process404(context);
    goto END;
  }

END:
  //3.把Response 对象进行序列化，写回到客户端
  server->WriteOneResponse(context);
  //处理失败的情况
  //收尾工作,当前请求处理完成，主动关闭
  close(context->new_sock);
  delete context;
  return NULL;
}

//构造一个404响应对象函数
int HttpServer::Process404(Context* context){
  Response* resp = &context->resp;
  resp->code = 404;
  resp->desc = "NOT FOUND";
  resp->body = "<head><meta http-equiv=\"content-type\""
                     "content=\"text/html;charset=utf-8\"></head><h1>404!您的页面坏掉了(＾－＾)</h1>";
  std::stringstream ss;
  ss << resp->body.size();
  std::string size;
  ss >> size;
  resp->header["Content-Length"] = size;
  return 0;
}

//从socket读取字符串，构造生成 Request 对象
int HttpServer::ReadOneRequest(Context* context){
  Request* req = &context->req;
  //1.从socket中读取一行数据作物Request 的首行
  //  按行读取的分割符应该是  \n  \r \r\n
  std::string first_line;
  //右值引用，避免拷贝
  //1.如果参数用于输入，const T&
  //2.如果参数用于输出，T*
  FileUtil::ReadLine(context->new_sock,&first_line);
  //2.解析首行，获取到请求的方法method 和 url
  int ret = ParseFirstLine(first_line,&req->method,&req->url);
  if(ret < 0){
    LOG(ERROR) << "ParseFirstLine error! first_line=" << first_line << "\n";
    return -1;
  }
  //3.解析url，获取到url_path和query_string 
  ret = ParseUrl(req->url,&req->url_path,&req->query_string);
  if(ret < 0){
    LOG(ERROR) << "ParseUrl error! first_line=" << req->url << "\n";
    return -1;
  }
  //4.循环的按行读取数据，每次读取到一行数据，就进行一次解析header,读到空行说明header解析完毕
  std::string header_line;
  while(1){
    FileUtil::ReadLine(context->new_sock,&header_line);
    // 如果是header_line 是空行，就退出循环
    // 由于ReadLine返回的header_line不包含\n等分隔符，所以读到空行的时候header_line返回的其实是空字符串
    if(header_line == ""){
      break;
    }
    ret = ParseHeader(header_line,&req->header);
    if(ret < 0){
      LOG(ERROR) << "ParseHeader error! first_line=" << header_line << "\n";
      return -1;
    }
  }
  // 5. 如果是POST 请求，但是没有Content-Length字段，认为这次请求失败，直接返回错误
  Header::iterator it = req->header.find("Context-Length");
  if(req->method == "POST" && it == req->header.end()){
    LOG(ERROR) << "POST Request has no Context-Length!\n";
    return -1;
  }
  //  如果是GET请求，就不用 读 body
  if(req->method == "GET"){
    return 0;
  }
  //如果是POST请求，并且header中包含了Content-Length字段
  //  继续读取 socket，获取到 body 内容；  
  int content_length = atoi(it->second.c_str());
  //从文件描述符中读指定长度
  ret = FileUtil::ReadN(context->new_sock,content_length,&req->body); 
  if(ret < 0){
    LOG(ERROR) << "ReadN error! context_length=" << content_length << "\n";
    return -1;
  }
  return 0;
}

int HttpServer::ParseFirstLine(const std::string& first_line,
                               std::string* method,
                               std::string* url){
  std::vector<std::string> tokens;
  StringUtil::Split(first_line," ",&tokens);
  if(tokens.size() != 3){
    //首行的格式不对
    LOG(ERROR) <<  "ParseFirstLine error! split error! first_line=" << first_line << "\n";
    return -1;
  }
  //如果版本号不包含 HTTP 关键字，也认为出错
  if(tokens[2].find("HTTP") == std::string::npos){
    //首行格式不对，版本信息中不包含HTTP关键字
    LOG(ERROR) << "ParseFirstLine error! version error! first_line=" << first_line << "\n";
    return -1;
  }
  *method = tokens[0];
  *url = tokens[1];
  return 0;
}

//解析一个标准url，其实比较复杂，核心思路是以？作为分割，从？左边
//来查找url_path，从？右边来查找query_string
//我们此处只实现一个简化版本，只考虑不包含域名和协议的情况
//只是单纯的以？作为分割，左边为path，右边为query_string
//如果是/path/index.html
int HttpServer::ParseUrl(const std::string& url,
             std::string* url_path,
             std::string* query_string){
  size_t pos = url.find("?");
  if(pos == std::string::npos){
    //没找到
    *url_path = url;
    *query_string = "";
    return 0;
  }
  //找到了
  *url_path = url.substr(0,pos);
  *query_string = url.substr(pos+1);
  return 0;
}

int HttpServer::ParseHeader(const std::string& header_line,Header* header){
  size_t pos = header_line.find(":");
  if(pos == std::string::npos){
    //找不到： 说明header格式有问题
    LOG(ERROR) << "ParseHeader error! has no : header_line=" << header_line << "\n";
    return -1;
  }
  //这个header格式还是不正确，没有value
  if(pos + 2 >= header_line.size()){
    LOG(ERROR) << "ParseHeader error! has no value! header_line=" << header_line << "\n";
    return -1;
  } 
  //查找最后一个，使用最后一次更改的
  (*header)[header_line.substr(0,pos)] = header_line.substr(pos + 2);
  return 0;
}


//实现序列化，把Response 对象转换成一个string 
//写回到 socket中
//此函数完全按照http协议要求来构造响应数据
//我们实现这个函数的细节可能有很大差异，但是只要能遵守http协议就都是可以的
int HttpServer::WriteOneResponse(Context* context){
  //定义100k的缓冲区
  //char buf[1024 * 100];
  //sprintf(buf,"",context->resp);
  
  //iostream 和 stringstream 之间的关系类似于
  //printf 和 sprintf 之间的关系
  //stringstream会动态分配缓冲区，函数内部自动管理
  //但是重新开辟空间以及拷贝，但是也可以提前分配好空间，是比较灵活的，但是灵活也不一定是好事
  //1.序列化字符串
  const Response& resp = context->resp;
  std::stringstream ss;
  //ss中插入的的首行数据
  ss << "HTTP/1.1 " << resp.code << " " << resp.desc << "\n"; 
  //c++基于区间的循环auto
  if(resp.cgi_resp == ""){
    for(auto item : resp.header){
    ss << item.first << ": " << item.second << "\n";
    }
    //普通的静态页面情况生成的界面内容有header和body
    ss << "\n";
    ss << resp.body;
  }else {
    //当前是在处理CGI生成的界面
    //cgi_resp同时包含了响应数据的header空行和body
    ss << resp.cgi_resp;
  } 
  //header和body之间还有一个空行
  //2.将序列化的结果写到socket 中
  const std::string& str = ss.str();//获取到ss对象的内部的string对象使用引用拿出来，没有发生拷贝
  write(context->new_sock,str.c_str(),str.size());
  return 0;
}

//通过输入的request 对象计算生成response对象
//1.静态页面
// a)GET请求没有query_string作为参数
//2.动态页面生成(使用CGI的方式动态生成,效率较低)
// a)GET请求并且存在 query_string 作为参数
// b)POST请求，带有body
int HttpServer::HandlerRequest(Context* context){
  const Request& req = context->req;
  Response* resp = &context->resp;
  resp->code = 200;
  resp->desc = "OK";
  //判定当前的处理方式是按照静态文件处理还是动态生成
  if(req.method == "GET" && req.query_string == ""){
    return context->server->ProcessStaticFile(context);
  }else if((req.method == "GET" && req.query_string != "")
      || req.method == "POST"){
    return context->server->ProcessCGI(context);
  }else {
    LOG(ERROR) << "Unsupport Method! method=" << req.method << "\n";
    return -1;
  }
  return -1;
}

//1.通过Request中的url_path字段，计算出文件在磁盘上的路径是什么
//    例如：url_path /index.html,想要得到的磁盘上的文件 ./wwwroot/index.html
int HttpServer::ProcessStaticFile(Context* context){
  const Request& req = context->req;
  Response* resp = &context->resp;
  //1.获取到静态文件完整路径
  std::string file_path;
  GetFilePath(req.url_path,&file_path);
  //2.打开并读取完整的文件
  int ret = FileUtil::ReadAll(file_path,&resp->body);
  if(ret < 0){
    LOG(ERROR) << "ReadAll error! file_path=" << file_path << "\n";  
    return -1;
  }
  return 0;
}

//通过 url_path找到对应的文件路径
//例如 请求url可能是 http://192.168.47.128:9090
//这种情况下 url_path 是 /
//此时等价于请求 /index.html
//例如 请求url 可能是 http://192.168.47.128:9090/image
//这种情况下 url_path 是 /
//如果 url_path指向的是一个目录，就尝试在这个目录访问一个叫做index.html 的文件（这也是一种简单约定）
void HttpServer::GetFilePath(const std::string& url_path,std::string* file_path){
  *file_path = "./wwwroot" + url_path;
  //判定一个路径是普通文件还是目录文件
  //1.linux 的 stat 函数，可以帮助查看文件类型
  //2.通过 boost filesystem 模块来进行判定
  if(FileUtil::IsDir(*file_path)){
    //1./image/
    //2./image
    if(file_path->back() != '/'){
      file_path->push_back('/');
    }
    (*file_path) += "index.html";
  }
  return;
}

int HttpServer::ProcessCGI(Context* context){
  const Request& req = context->req;
  Response* resp = &context->resp;
  //1.创建一对匿名管道（父子进程要双向通信）
  int fd1[2],fd2[2];
  pipe(fd1);
  pipe(fd2);
  //父进程用来写的
  int father_write = fd1[1];
  int child_read = fd1[0];
  int father_read = fd2[0];
  int child_write = fd2[1];

  //2.设置环境变量
  //  a)METHOD请求方法
  //设置环境变量
  std::string env = "REQUEST_METHOD=" + req.method;
  putenv(const_cast<char*>(env.c_str()));
  if(req.method == "GET"){
    //  b)GET方法，QUERY_STRING请求参数
    env = "QUERY_STRING=" + req.query_string;
    putenv(const_cast<char*>(env.c_str()));
  }else if(req.method == "POST"){
    //  c)POST方法，就设置CONTENT_LENGTH
    //为什么要用常量迭代器？？？
    //迭代器不应该修改request中的数据，handler request不应该修改，所以使用const迭代器
    //Header::const_iterator pos = req.header.find("Content-Length");
    auto pos = req.header.find("Content-Length");
    env = "CONTENT_LENGTH=" + pos->second;
    putenv(const_cast<char*>(env.c_str()));
  }
  //3.fork,父子进程
  pid_t ret = fork();
  if(ret < 0){
    perror("fork");
    goto END;
  }
  if(ret > 0){
    //父进程流程
    close(child_read);
    close(child_write);
    // a)如果是POST请求，父进程就要把body写入到管道中
    if(req.method == "POST"){
     write(father_write,req.body.c_str(),req.body.size());
    }
    // b)阻塞式的读取管道，尝试吧子进程的结果读取出来，并且放到Response对象中
    FileUtil::ReadAll(father_read,&resp->cgi_resp);
    // c)对子进程进行进程等待（为了避免僵尸进程）
    wait(NULL);//表示只关注回收子进程资源
  }else {
    //子进程流程
    close(father_read);
    close(father_write);
    //  a)把标准输入和标准输出进行重定向(dup)，重定向是忘new写数据就相当于往old写数据，
    //  期望往标准输出写数据相当于往管道写数据
    dup2(child_read,0);//从标准输入读数据 
    dup2(child_write,1);//从标准输出写数据
    //  b)先获取到要替换的可执行文件是哪个（通过url_path来获取）
    std::string file_path;
    GetFilePath(req.url_path,&file_path);
    //  c)进行进程的程序替换
    execl(file_path.c_str(),file_path.c_str(),NULL);
    //  d)有我们的CGI可执行程序完成动态页面的计算，并且写回数据到管道中
    //这部分需要单独的文件来实现，根据该文件编译生成CGI可执行程序
    
  }
END:
  //统一处理收尾工作，如果不释放就会文件描述符泄漏的问题
  close(father_read);
  close(father_write);
  close(child_read);
  close(child_write);
  return 0; 
}

////////////////////////////////////////////////////
//以下为测试函数
////////////////////////////////////////////////////
void HttpServer::PrintRequest(const Request& req){
  LOG(DEBUG) << "Request:" << "\n" << req.method << " " << req.url << "\n" 
    << req.url_path << " " << req.query_string << "\n";
  //for(Header::const_iterator it = req.header.begin();it != req.header.end();++it){
  // LOG(DEBUG) << it->first << ":" << it->second << "\n";
  //}
  //c++11  range  based  for
  for(auto it : req.header){
    LOG(DEBUG) << it.first << ":" << it.second << "\n";
  }
  LOG(DEBUG) << "\n";
  LOG(DEBUG) << req.body << "\n";
}



}//end of http_server
