//////////////////////////////////////////
//字符串转数字
//1.atoi 
//2.sscanf
//3.stringstream
//4.boost lexcial_cast
//5.stoi(C++11)
//
//数字转字符串
//1.itoa
//2.sprintf
//3.stringstream
//4.boost lexcial_cast
//5.to_string(C++11)
/////////////////////////////////////////

#include <iostream>
#include <string>
#include "util.hpp"
#include <sstream>

void HttpResponse(const std::string& body){
  std::cout << "Content-Length: " << body.size() << "\n";
  std::cout << "\n"; //这个\n是HTTP协议中的空行
  std::cout << body;
  return;
}



//生成的是CGI程序，通过这个CGI程序完成不同的业务。
//不同的业务有不同的CGI程序
//这个CGI程序是完成两个数的相加运算
int main(){
  //1.获取方法method
  const char* method = getenv("REQUEST_METHOD");
  if(method == NULL){
    HttpResponse("No env REQUEST_METHOD!");
    return 1; 
  }
  //2.如果是GET请求，从QUERY_STRING中读取请求参数
  //std::string(method)构造的是匿名对象，用来取出字符串内容和GET比较
  StringUtil::UrlParam params;
  if(std::string(method) == "GET"){
    const char* query_string = getenv("QUERY_STRING");
    StringUtil::ParseUrlParam(query_string,&params);
  }else if(std::string(method) == "POST"){
    //3.如果是POST请求，从CONTENT_LENGTH中读取body的长度，
    //  根据body的长度从标准输入中读取请求的body(重定向是生效的，所以是从管道中标准输入的读取数据)
    //const char* content_length = getenv("CONTENT_LENGTH");
   //4.解析query_string或者body的数据
    char buf[1024 * 10] = {0};
    read(0,buf,sizeof(buf)-1);
    StringUtil::ParseUrlParam(buf,&params);
  }

  //5.根据业务进行计算，此处计算是a+b的值
  int a = std::stoi(params["a"]);
  int b = std::stoi(params["b"]);
  int result = a + b;
  //6.根据计算结果，构造响应数据，写回到标准输出中
  std::stringstream ss;
  ss << "<h1> result = " << result << "</h1>";
  HttpResponse(ss.str());
  return 0;
}
