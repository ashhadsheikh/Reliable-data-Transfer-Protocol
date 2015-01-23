all: 
	g++ -o Server Sender.cpp time.cpp -lpthread
	g++ -o Client Reciever.cpp -lpthread		
