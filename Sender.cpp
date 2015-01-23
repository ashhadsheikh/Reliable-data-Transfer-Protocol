#include<iostream>
#include<sys/socket.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<fstream>
#include<string.h>
#include<cmath>
#include <time.h>
#include <cstdlib>
#include <stdio.h>
#include <stdlib.h>
#include<sstream>
#include <pthread.h>
#include<errno.h>
#include <unistd.h>
#include "time.h"
using namespace std;
pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
#define PACKET_SIZE 200
struct to_send {
	char * fileName;
	int conn_sock;
	struct sockaddr_in client_addr;
};
struct serverConfig {
	string server_IP;
	int server_Port;
	string server_File;
	int PacketSize;
	int PacketNumberToDrop;
	int PacketNumberToCorrupt;
}* Configuration;
serverConfig * configureServer() {
	serverConfig * config = new serverConfig();
	fstream fin("serverconfig.txt");
	fin >> config->server_IP;
	fin >> config->server_Port;
	fin >> config->server_File;
	fin >> config->PacketSize;
	fin >> config->PacketNumberToDrop;
	fin >> config->PacketNumberToCorrupt;
	return config;

}

struct rcv {
	int conn_sock;
	struct sockaddr_in client_addr;
};

bool is_file_exist(const char *fileName) {
	ifstream infile(fileName);
	return infile.good();
}
char * filesize(const char* filename) {
	ifstream in(filename, std::ifstream::ate | std::ifstream::binary);
	string s;
	stringstream out;
	out << in.tellg();
	s = out.str();
	char *st = new char[10];
	for (int j = 0; j < 10; j++) {
		st[j] = '\0';
	}
	int i = 0;
	do {

		st[i] = s[i];
		i++;
	} while (s[i] != '\0');
	in.close();
	return st;

}
unsigned long long ACK_Checksum(int val) {
	string a;
	stringstream convert; // stringstream used for the conversion
	convert << val; //add the value of Number to the characters in the stream
	a = convert.str(); //set Result to the content of the stream
	unsigned long long value = 0;
	int length = a.size();
	for (int i = 0; i < length; i++) {
		int x = int(a[i]) * pow(2, i % 60);
		value += x;
	}
	return (value);
}
unsigned int CRC16_2(char *buf) {
	int len = strlen(buf);
	unsigned int crc = 0xFFFF;
	for (int pos = 0; pos < len; pos++) {
		crc ^= (unsigned int) buf[pos];  // XOR byte into least sig. byte of crc

		for (int i = 8; i != 0; i--) {    // Loop over each bit
			if ((crc & 0x0001) != 0) {      // If the LSB is set
				crc >>= 1;                    // Shift right and XOR 0xA001
				crc ^= 0xA001;
			} else
				// Else LSB is not set
				crc >>= 1;                    // Just shift right
		}
	}

	return crc;
}

/////////////////////////////////////////////////

typedef struct tcp_header {
	char data[PACKET_SIZE];
	unsigned int sequence;        // sequence number - 32 bits
	unsigned long checksum; // checksum
	unsigned char fin :1; //Finish Flag
	int packetsize;
	unsigned char syn :1; //Synchronise Flag
	unsigned char ack :1; //Acknowledgement Flag
} TCP_HDR;

//####################################################################
//####################################################################

struct node {
	TCP_HDR info;
	struct node *next;
}*last;

/*
 * Class Declaration
 */
class circular_llist {
public:

	void create_node(TCP_HDR value) {
		struct node *temp;
		temp = new (struct node);
		temp->info = value;
		if (last == NULL) {
			last = temp;
			temp->next = last;
		} else {
			temp->next = last->next;
			last->next = temp;
			last = temp;
		}
	}

	void delete_element(unsigned int value) {
		struct node *temp, *s;
		s = last->next;
		/* If List has only one element*/
		if (last->next == last && last->info.sequence == value) {
			temp = last;
			last = NULL;
			free(temp);
			return;
		}
		if (s->info.sequence == value) /*First Element Deletion*/
		{
			temp = s;
			last->next = s->next;
			free(temp);
			return;
		}
		while (s->next != last) {
			/*Deletion of Element in between*/
			if (s->next->info.sequence == value) {
				temp = s->next;
				s->next = temp->next;
				free(temp);
				return;
			}
			s = s->next;
		}
		/*Deletion of last element*/
		if (s->next->info.sequence == value) {
			temp = s->next;
			s->next = last->next;
			free(temp);
			last = s;
			return;
		}
	}
	circular_llist() {
		last = NULL;
	}
};

//####################################################################
timer TIMER;
unsigned int NextSeqNumber = 1;
unsigned int Base = 1;
circular_llist window;

//&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%setting header%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

TCP_HDR make_packet(char* s, int seqNo) {
	TCP_HDR tcphdr;
	for (int i = 0; i < PACKET_SIZE; i++) {
		tcphdr.data[i] = '\0';
	}
	for (int i = 0; i < strlen(s); i++) {
		tcphdr.data[i] = s[i];
	}
	tcphdr.packetsize = PACKET_SIZE;
	tcphdr.checksum = CRC16_2(s);
	tcphdr.sequence = seqNo;
	tcphdr.ack = 0;

	return tcphdr;
}
/*******************************************/
void *rdt_send(void * ptr) {
	to_send T;
	T = *((to_send *) ptr);

	char send[PACKET_SIZE] = { '\0' };
	ifstream infile;
	infile.open(T.fileName);
	while (!infile.eof()) {
		infile.read(send, PACKET_SIZE);
		if (NextSeqNumber <= (Base + 10)) {
			TCP_HDR Packet;
			pthread_mutex_lock(&mutex1);
			Packet = make_packet(send, NextSeqNumber);
			window.create_node(Packet);
			if (NextSeqNumber % Configuration->PacketNumberToDrop != 0) {
				if (NextSeqNumber % Configuration->PacketNumberToCorrupt == 0)
					Packet.checksum -= 1000;

				sendto(T.conn_sock, &Packet, sizeof(Packet), 0,
						(struct sockaddr *) &T.client_addr,
						sizeof(T.client_addr));
				cout << "SENT: Packet #" << Packet.sequence << endl;
			}
			pthread_mutex_unlock(&mutex1);
			if (Base == NextSeqNumber) {
				TIMER.Start();
			}
			NextSeqNumber++;

		} else {
			int pos = infile.tellg();
			infile.seekg(pos - PACKET_SIZE);
		}

	}
	pthread_exit(0);
}
void *timeout(void * ptr) {
	rcv T;
	T = *((rcv *) ptr);
	while (1) {
		if (TIMER.IsActive() && TIMER.GetTicks() >= 1000) {
			pthread_mutex_lock(&mutex1);
			cout << "Timeout\n";

			TIMER.Reset();
			struct node *s;
			s = last->next;

			while (s != last) {

				sendto(T.conn_sock, &s->info, sizeof(s->info), 0,
						(struct sockaddr *) &T.client_addr,
						sizeof(T.client_addr));
				cout << "RESENT: Packet #" << s->info.sequence << endl;
				s = s->next;
			}
			sendto(T.conn_sock, &s->info, sizeof(s->info), 0,
					(struct sockaddr *) &T.client_addr, sizeof(T.client_addr));
			cout << "RESENT: Packet #" << s->info.sequence << endl;

			s = s->next;
			pthread_mutex_unlock(&mutex1);
		}
	}
}
void *ACKrecieved(void * ptr) {
	rcv T;
	T = *((rcv *) ptr);
	while (1) {
		TCP_HDR Packet;
		int s = sizeof(T.client_addr);
		while (1) {
			recvfrom(T.conn_sock, &Packet, sizeof(Packet), 0,
					(struct sockaddr *) &T.client_addr, (socklen_t*) &s);
			pthread_mutex_lock(&mutex1);
			unsigned long long checksum = ACK_Checksum(Packet.sequence);
			if (checksum == Packet.checksum) {

				for (int i = 1; i <= Packet.sequence; i++) {
					window.delete_element(i);
				}
				cout << "ACK: Packet #" << Packet.sequence << endl;

				Base = Packet.sequence + 1;

				if (Base == NextSeqNumber) {
					TIMER.Stop();
				} else {

					TIMER.Reset();
				}

			} else {
				cout << "CORRUPTED ACK #" << Packet.sequence << endl;
			}

			pthread_mutex_unlock(&mutex1);
		}
		pthread_exit(0);
	}

}
/*******************************************/
const std::string currentDateTime() {
	time_t now = time(0);
	struct tm tstruct;
	char buf[80];
	tstruct = *localtime(&now);
	// Visit http://en.cppreference.com/w/cpp/chrono/c/strftime
	// for more information about date/time format
	strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);

	return buf;
}
/****************************************/
int main() {
	Configuration = configureServer();
	int conn_sock;
	int structSize;
	struct sockaddr_in server_addr, client_addr;

	conn_sock = socket(AF_INET, SOCK_DGRAM, 0);

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(Configuration->server_Port);
	server_addr.sin_addr.s_addr = inet_addr((Configuration->server_IP).c_str());
	structSize = sizeof(server_addr);

	bind(conn_sock, (struct sockaddr *) &server_addr, sizeof(server_addr));
	while (1) {
		cout << "Looking for client" << endl;
		TCP_HDR Packet;

		/////////////////////////Request Recieved
		recvfrom(conn_sock, &Packet, sizeof(Packet), 0,
				(struct sockaddr *) &client_addr, (socklen_t*) &structSize);
		ofstream outfile;
		outfile.open(Configuration->server_File.c_str(), ios::app);
		outfile << "Time:[" << currentDateTime() << "],File Request:["
				<< Packet.data << "],";
		outfile << "Client IP:[" << client_addr.sin_addr.s_addr << "],";
		outfile << "Client Port:[" << client_addr.sin_port << "]" << endl;
		if (sizeof(Packet.data)== PACKET_SIZE) {
			if (CRC16_2(Packet.data) == Packet.checksum) {
				if (is_file_exist(Packet.data)) {
					cout << "ACCEPTED: " << Packet.data << endl;
					char *s = filesize(Packet.data);
					TCP_HDR Packet2 = make_packet(s, 0);
					sendto(conn_sock, &Packet2, sizeof(Packet2), 0,
							(struct sockaddr *) &client_addr, structSize);
					////////////////////////////////////////////////
					pthread_t a_thread, b_thread, c_thread;
					to_send arg;
					arg.conn_sock = conn_sock;
					arg.client_addr = client_addr;
					arg.fileName = Packet.data;
					rcv arg2;
					arg2.conn_sock = conn_sock;
					arg2.client_addr = client_addr;
					pthread_create(&a_thread, NULL, rdt_send, (void*) &arg);
					pthread_create(&b_thread, NULL, ACKrecieved, (void*) &arg2);
					pthread_create(&c_thread, NULL, timeout, (void*) &arg2);

					while (1) {
					}

					//////////////////////////////////////////////////
				} else {
					cout << "NO FILE: " << Packet.data << endl;
					TCP_HDR Packet = make_packet("NO FILE", 0);
					sendto(conn_sock, &Packet, sizeof(Packet), 0,
							(struct sockaddr *) &client_addr, structSize);
				}
			}
		} else {
			cout << "ERROR:" << " Packets Size not compatible" << endl;
			TCP_HDR Packet = make_packet("ERROR: Packets Size not compatible",
					0);
			sendto(conn_sock, &Packet, sizeof(Packet), 0,
					(struct sockaddr *) &client_addr, structSize);
		}
	}
	return 0;
}

