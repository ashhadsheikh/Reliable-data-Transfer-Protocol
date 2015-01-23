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
#include<stdlib.h>
#include<sstream>
#include<pthread.h>
#include <cstdlib>
#include<cmath>
#include <sys/timeb.h>
#include<stdio.h>

using namespace std;
#define PACKET_SIZE 200

struct RTTList{
	struct RTT{
		int rtt;
		RTT *next;
		RTT()
		{
			rtt=0;
			next=NULL;
		}
	};
	RTT *head;
	float estimatedRTT;
	float alpha;
	int totalnodes;
	float deviationRTT;
	float beta;
	int timeinterval;
	RTTList()
	{
		timeinterval = 1;
		totalnodes=0;
		alpha = 0.125;
		estimatedRTT=1;
		deviationRTT = 1;
		beta = 0.25;
		head=NULL;
	}
	void addRTT(int roundtriptime)
	{
		struct RTT *temp = new RTT();
		temp->rtt = roundtriptime;
			if (head == NULL) {
				head = temp;
			}
			else
			{
				RTT *current = head;
				while(current->next)
				{
					current=current->next;
				}
				current->next = temp;
			}

		totalnodes++;
		calculateTimeInterval(roundtriptime);
	}
	void print()
	{
	 	RTT *current = head;
		int i=0;
		while(current->next)
		{
			cout<<current->rtt<<endl;
			current = current->next;
		}
	}
	void calculateEstimatedRTT(int sampleRTT)
	{
		estimatedRTT = ( 1 - alpha ) * estimatedRTT + ( alpha * sampleRTT);
	}
	void calculateDeviationRTT(int sampleRTT)
	{
		deviationRTT = ( 1 - beta ) * deviationRTT + beta * abs(sampleRTT - estimatedRTT);

	}
	void calculateTimeInterval(int roundtriptime)
	{
		calculateEstimatedRTT(roundtriptime);
		calculateDeviationRTT(roundtriptime);
		float timeout = estimatedRTT+(4*deviationRTT);
		timeinterval = ceil(timeout);
	}
};

int getMilliCount(){
	timeb tb;
	ftime(&tb);
	int nCount = tb.millitm + (tb.time & 0xfffff) * 1000;
	return nCount;
}

int getMilliSpan(int nTimeStart){
	int nSpan = getMilliCount() - nTimeStart;
	if(nSpan < 0)
		nSpan += 0x100000 * 1000;
	return nSpan;
}
const std::string currentDateTime() {
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);
    // Visit http://en.cppreference.com/w/cpp/chrono/c/strftime
    // for more information about date/time format
    strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);

    return buf;
}

struct rcv {

	char * fileName;
	int conn_sock;
	struct sockaddr_in client_addr;
};
struct clientConfig {
	string server_IP;
	int server_Port;
	int client_Port;
	string client_File;
	int client_PacketSize;
	int ServerTimeout;
	int AckNumberToDrop;
	int AckNumberToCorrupt;
}*Configuration;
clientConfig * configureClient() {
	clientConfig * config = new clientConfig();
	fstream fin("clientconfig.txt");
	fin >> config->server_IP;
	fin >> config->server_Port;
	fin >> config->client_Port;
	fin >> config->client_File;
	fin >> config->client_PacketSize;
	fin >> config->ServerTimeout;
	fin >> config->AckNumberToDrop;
	fin >> config->AckNumberToCorrupt;
	return config;

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
unsigned int CRC16(char *buf) {
	int len = strlen(buf);
	len -= 1;
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
typedef struct tcp_header {
	char data[PACKET_SIZE];
	unsigned int sequence;        // sequence number - 32 bits
	unsigned long checksum; // checksum
	int packetsize;
	unsigned char fin :1; //Finish Flag
	unsigned char syn :1; //Synchronise Flag
	unsigned char ack :1; //Acknowledgement Flag
} TCP_HDR;
//####################################################################
unsigned int ExpectedSeqNumber = 1;
//&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%setting header%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
TCP_HDR make_ACK_packet(int seqNo) {
	TCP_HDR tcphdr;
	for (int i = 0; i < PACKET_SIZE; i++) {
		tcphdr.data[i] = '\0';
	}
	tcphdr.checksum = ACK_Checksum(seqNo);
	tcphdr.sequence = seqNo;
	tcphdr.packetsize=PACKET_SIZE;
	tcphdr.ack = 1;
	return tcphdr;
}
TCP_HDR make_packet(char* s, int seqNo) {
	TCP_HDR tcphdr;
	for (int i = 0; i < PACKET_SIZE; i++) {
		tcphdr.data[i] = '\0';
	}
	for (unsigned int i = 0; i < strlen(s); i++) {
		tcphdr.data[i] = s[i];
	}

	tcphdr.packetsize=PACKET_SIZE;
	cout<<tcphdr.packetsize<<endl;
	tcphdr.checksum = CRC16_2(s);
	tcphdr.sequence = seqNo;
	tcphdr.ack = 0;

	return tcphdr;
}

void* Recieve_Packet(void* ptr) {
	ofstream outfile;
	rcv T;
	T = *((rcv *) ptr);
	TCP_HDR Packet;		//=recieved
	int structSize = sizeof(T.client_addr);
	outfile.open(T.fileName);
	outfile.close();
	while (1) {
		recvfrom(T.conn_sock, &Packet, sizeof(Packet), 0,
				(struct sockaddr *) &T.client_addr, (socklen_t*) &structSize);

		if (Packet.checksum == CRC16(Packet.data)
				&& ExpectedSeqNumber == Packet.sequence) {
			for (int i = 0; i < PACKET_SIZE; i++) {
				outfile.open(T.fileName, ios::app);
				if (Packet.data[i] != '\0')
					outfile << Packet.data[i];
				outfile.close();
			}
			cout << "SEGMENT #" << Packet.sequence << endl;

			TCP_HDR ACK = make_ACK_packet(ExpectedSeqNumber);
			if (ExpectedSeqNumber % Configuration->AckNumberToDrop != 0) {
				if (ExpectedSeqNumber % Configuration->AckNumberToCorrupt == 0)
					ACK.checksum -= 1000;
				sendto(T.conn_sock, &ACK, sizeof(ACK), 0,
						(struct sockaddr *) &T.client_addr, structSize);
			}
			ExpectedSeqNumber++;
		} else if (ExpectedSeqNumber != Packet.sequence) {
			cout << "Out Of Order SEGMENT #" << Packet.sequence << endl;
		} else if (Packet.checksum != CRC16(Packet.data)) {
			cout << "CORRUPT SEGMENT #" << Packet.sequence << endl;
		}
	}
}

int main() {
	RTTList timelist;
	Configuration = configureClient();
	char message[50] ;//= "file.txt";
	cout<<"Enter File to request\n";
	cin>>message;
	int connection;
	int structSize;
	struct sockaddr_in server_addr;
	connection = socket(AF_INET, SOCK_DGRAM, 0);

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(Configuration->server_Port);
	server_addr.sin_addr.s_addr = inet_addr((Configuration->server_IP).c_str());

	structSize = sizeof(server_addr);
	TCP_HDR Packet = make_packet(message, ExpectedSeqNumber);
	int start = getMilliCount();
	sendto(connection, &Packet, sizeof(Packet), 0,
			(struct sockaddr *) &server_addr, structSize);
	recvfrom(connection, &Packet, sizeof(Packet), 0,
			(struct sockaddr *) &server_addr, (socklen_t*) &structSize);
	int milliSecondsElapsed = getMilliSpan(start);
	timelist.addRTT(milliSecondsElapsed);
	ofstream outfile;
	outfile.open(Configuration->client_File.c_str(),ios::app);
	outfile<<"-----------------------------------"<<endl;
	outfile<< currentDateTime()<<endl;
	outfile<<"File Request:[" << message<<"]" << endl;
	outfile<<"File Size:[" << Packet.data<<" B]" << endl;
	outfile<<"Server IP:[" << Configuration->server_IP<<"]" << endl;
	outfile<<"Server Port:[" << Configuration->server_Port<<"]" << endl;
	outfile<<"RTT:" << milliSecondsElapsed << endl;
	outfile<<"Estimated RTT: "<<timelist.estimatedRTT<<endl;
	outfile<<"Deviation RTT: "<<timelist.deviationRTT<<endl;
	outfile<<"Time Interval: "<<timelist.timeinterval<<endl;
	outfile<<"-----------------------------------"<<endl;

	cout << "OBTAINED:" << Packet.data << endl;
	if (strcmp(Packet.data, "File Not Exists") != 0) {
		///thread here
		pthread_t a_thread;
		rcv arg2;
		arg2.fileName = message;
		arg2.conn_sock = connection;
		arg2.client_addr = server_addr;
		pthread_create(&a_thread, NULL, Recieve_Packet, (void*) &arg2);
		pthread_join(a_thread, NULL);
	}

	return 0;
}
