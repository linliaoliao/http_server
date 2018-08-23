.PHONY:all
all:http_server cgi_main 

http_server:http_server.cc http_server_main.cc
	g++ $^ -o $@ -std=c++11 -lpthread -lboost_filesystem -lboost_system

cgi_main:cgi_main.cc 
	g++ $^ -o $@ -std=c++11 -lpthread -lboost_filesystem -lboost_system
	cp cgi_main ./wwwroot/add

.PHONY:clean
clean: 
	rm http_server cgi_main
