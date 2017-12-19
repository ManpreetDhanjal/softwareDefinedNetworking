/**
 * @dhanjal_assignment3
 * @author  Manpreet Dhanjal <dhanjal@buffalo.edu>
 * @version 1.0
 *
 * @section LICENSE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details at
 * http://www.gnu.org/copyleft/gpl.html
 *
 * @section DESCRIPTION
 *
 * This contains the main function. Add further description here....
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#define AUTHOR_STATEMENT "I, dhanjal, have read and understood the course academic integrity policy."
#define CNTRL_HEADER_SIZE 8
#define CNTRL_RESP_HEADER_SIZE 8
#define ROUTER_DETAIL_SIZE 12
#define INF 65535
#define BACKLOG 5
#define DATA_PACKET_SIZE 1036

// struct for router node
struct routerNode{
	uint16_t id; // 16 bits
	uint32_t ip; // 32 bits
	uint16_t router_port; // 16 bits
	uint16_t data_port; // 16 bits
	uint16_t control_port; // 16 bits
	uint16_t cost; // 16 bits
	uint16_t next_hop_id;
	uint16_t path_cost;
	int control_sock_id;
	int router_sock_id;
	int data_sock_id;
	int isNeighbour;
	int index;
	int isActive;
	int timeOutCount;
};

// struct for control request header
struct controlRequestHeader{
	uint32_t dest_ip_addr;
   	uint8_t control_code;
   	uint8_t response_time;
   	uint16_t payload_len;
};

// struct for control response header
struct controlResponseHeader{
	uint32_t controller_ip_addr;
    uint8_t control_code;
   	uint8_t response_code;
   	uint16_t payload_len;
};

struct fileStatus{
	uint8_t transfer_id;
	uint8_t ttl;
	uint16_t seq_num_arr[10240];
	int index;
	uint16_t last_seq_num;
	uint16_t first_seq_num;
	FILE *f; // file pointer
};

struct timerNode{
	int timerNodeIndex;
	struct timeval time;
	int isUpdatedEarly;
};

///////// global variables ///////////
struct routerNode self; // contains details about router itself
int fdMax; // maximum socked description id
fd_set masterfds; // contains all the sockets
fd_set controlfds;
//int activeControlSock;
uint16_t num_routers; // total number of routers
uint16_t interval; // time interval for periodic update
struct routerNode routerNodeList[5];
struct routerNode routingTable[5][5];

//struct timeval timerArr[5];
int timerStart = 0;
int timerEnd = 0;
//int timerRouterTrans[5];

struct timerNode timerArr[5];
char ultimateDataPacket[DATA_PACKET_SIZE];
char penultimateDataPacket[DATA_PACKET_SIZE];
struct fileStatus fileStatusArr[256]; // hashmap for transfer id
//int activeDataSock;
struct addrinfo *myaddress;
//FILE *f;

ssize_t recvALL(int sock_index, char *buffer, ssize_t nbytes)
{
    ssize_t bytes = 0;
    bytes = recv(sock_index, buffer, nbytes, 0);

    if(bytes == 0) return -1;
    while(bytes != nbytes)
        bytes += recv(sock_index, buffer+bytes, nbytes-bytes, 0);

    return bytes;
}

ssize_t sendALL(int sock_index, char *buffer, ssize_t nbytes)
{
    ssize_t bytes = 0;
    perror("before error:");
    bytes = send(sock_index, buffer, nbytes, 0);
    perror("sendallerror:\n");
    if(bytes == 0) return -1;
    while(bytes != nbytes)
        bytes += send(sock_index, buffer+bytes, nbytes-bytes, 0);

    return bytes;
}

ssize_t sendallUDP(struct routerNode router, char *buffer, ssize_t nbytes)
{

	struct sockaddr_in addr;

	uint32_t ip = htonl(router.ip);
    memcpy(&(addr.sin_addr), &ip, sizeof(struct in_addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(router.router_port);

    ssize_t bytes = 0;
    bytes = sendto(router.router_sock_id, buffer, nbytes, 0, (struct sockaddr*)&addr, sizeof addr);
    if(bytes == 0) return -1;
    while(bytes != nbytes)
    	bytes += sendto(router.router_sock_id, buffer+bytes, nbytes-bytes, 0, (struct sockaddr*)&addr, sizeof addr);

    return bytes;
}

ssize_t recvallUDP(int sock_index, char *buffer, ssize_t nbytes)
{
	struct sockaddr_in src;
    socklen_t addr_size;

    addr_size = sizeof(struct sockaddr_in);

    ssize_t bytes = 0;
    bytes = recvfrom(sock_index, buffer, nbytes, 0, (struct sockaddr *)&src, &addr_size);

    
    if(bytes == 0) return -1;
    while(bytes != nbytes)
    	bytes += recvfrom(sock_index, buffer+bytes, nbytes-bytes, 0, (struct sockaddr *)&src, &addr_size);

    return bytes;
}

// returns control sock id
int createRouterSock(uint16_t port, int isTCP){
	int sockDesc;
	struct addrinfo hints;
	struct addrinfo *availableResult;
	int isValid;
	int yes=1;

	char* portStr = malloc(sizeof(port));
	sprintf(portStr, "%d", port);

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	if(isTCP == 1){
    	hints.ai_socktype = SOCK_STREAM;
	}else{
		hints.ai_socktype = SOCK_DGRAM;
	}
    hints.ai_flags = AI_PASSIVE;

    if((isValid = getaddrinfo(NULL, portStr, &hints, &myaddress)) != 0){
    	fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(isValid));
    	exit(1);
	}

	// loop through all the results and bind to the first available
	for(availableResult = myaddress; availableResult != NULL; availableResult = availableResult->ai_next){
		if((sockDesc = socket(myaddress->ai_family, availableResult->ai_socktype, availableResult->ai_protocol)) == -1){
			continue;
		}
		if(setsockopt(sockDesc, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1){
			perror("here:");
			exit(1);
		}
		if(bind(sockDesc, availableResult->ai_addr, availableResult->ai_addrlen) == -1){
			close(sockDesc);
			continue;
		}
		break;
	}
	//freeaddrinfo(myaddress);
	if(availableResult == NULL){
		perror("availableResult:");		
		exit(1);
	}

	if(isTCP == 1){
		if(listen(sockDesc, BACKLOG) == -1){
			perror("listen:");
			exit(1);
		}
	}

	FD_SET(sockDesc, &masterfds);

	if(fdMax < sockDesc){
		fdMax = sockDesc; // this is the listener socket for remote controller
	}

	printf("returning\n");
	return sockDesc;
}

int new_control_conn(int sock_index){
    int fdaccept, caddr_len;
    struct sockaddr_in remote_controller_addr;

    caddr_len = sizeof(remote_controller_addr);
    fdaccept = accept(sock_index, (struct sockaddr *)&remote_controller_addr, &caddr_len);
    if(fdaccept < 0)
        perror("error new_control_conn\n");
    FD_SET(fdaccept, &masterfds);
    FD_SET(fdaccept, &controlfds);
    if(fdaccept > fdMax){
    	fdMax = fdaccept;
    }
    return fdaccept;
}

int new_data_conn(int sock_index){
	int fdaccept, caddr_len;
    struct sockaddr_in router_addr;

    caddr_len = sizeof(router_addr);
    fdaccept = accept(sock_index, (struct sockaddr *)&router_addr, &caddr_len);
    if(fdaccept < 0)
        perror("error new_data_conn\n");
    FD_SET(fdaccept, &masterfds);
    if(fdaccept > fdMax){
    	fdMax = fdaccept;
    }
    return fdaccept;
}

char* create_response_header(int sock_index, uint8_t control_code, uint8_t response_code, uint16_t payload_len){
	char *buffer;
	struct controlResponseHeader *cntrl_resp_header;

	struct sockaddr_in addr;
    socklen_t addr_size;

    buffer = (char *) malloc(sizeof(char)*CNTRL_RESP_HEADER_SIZE);
    cntrl_resp_header = (struct controlResponseHeader *) buffer;

    addr_size = sizeof(struct sockaddr_in);
    getpeername(sock_index, (struct sockaddr *)&addr, &addr_size);

    memcpy(&(cntrl_resp_header->controller_ip_addr), &(addr.sin_addr), sizeof(struct in_addr));
    /* Control Code */
    cntrl_resp_header->control_code = control_code;
    /* Response Code */
    cntrl_resp_header->response_code = response_code;
   	/* Payload Length */
   	cntrl_resp_header->payload_len = htons(payload_len);

   	return buffer;
}

void author_response(int sock_index){
	uint16_t payload_len, response_len;
	char *cntrl_response_header, *cntrl_response_payload, *cntrl_response;

	payload_len = sizeof(AUTHOR_STATEMENT)-1; // Discount the NULL chararcter
	cntrl_response_payload = (char *) malloc(payload_len);
	memcpy(cntrl_response_payload, AUTHOR_STATEMENT, payload_len);

	cntrl_response_header = create_response_header(sock_index, 0, 0, payload_len);

	response_len = CNTRL_RESP_HEADER_SIZE+payload_len;
	cntrl_response = (char *) malloc(response_len);
	/* Copy Header */
	memcpy(cntrl_response, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
	free(cntrl_response_header);
	/* Copy Payload */
	memcpy(cntrl_response+CNTRL_RESP_HEADER_SIZE, cntrl_response_payload, payload_len);
	free(cntrl_response_payload);

	sendALL(sock_index, cntrl_response, response_len);

	free(cntrl_response);
}


void braodcastUpdates(){
	int selfEntryLen = 8;
	int size_per_router = 12;
	uint16_t padding = 0x00;
	int offset;
	char* routingUpdatePacket;

	char* selfEntry = (char*)malloc(sizeof(char) * selfEntryLen);
	bzero(selfEntry, selfEntryLen);
	uint16_t num = htons(num_routers);
	uint16_t router_port = htons(self.router_port);
	uint32_t sip = htonl(self.ip);
	memcpy(selfEntry, &num, sizeof(num_routers));
	memcpy(selfEntry+2, &router_port, sizeof(self.router_port));
	memcpy(selfEntry+4, &sip, sizeof(self.ip));

	int payload_len = num_routers*size_per_router;
	char* payload = (char*)malloc(sizeof(char)*payload_len);
	bzero(payload, payload_len);

	for(int i=0; i<num_routers; i++){
		offset = i*size_per_router;
		uint32_t ip = htonl(routerNodeList[i].ip);
		uint16_t d_port = htons(routerNodeList[i].router_port);
		uint16_t id = htons(routerNodeList[i].id);
		uint16_t path_cost = htons(routerNodeList[i].path_cost);
		
		memcpy(payload+offset, &ip, sizeof(routerNodeList[i].ip));
		memcpy(payload+offset+4, &d_port, sizeof(routerNodeList[i].router_port));
		memcpy(payload+offset+6, &padding, sizeof(padding));
		memcpy(payload+offset+8, &id, sizeof(routerNodeList[i].id));
		memcpy(payload+offset+10, &path_cost, sizeof(routerNodeList[i].path_cost));
	}

	// 2 for num of routers
	int len_packet = selfEntryLen+payload_len;
	routingUpdatePacket = (char*)malloc(sizeof(char)*len_packet);
	bzero(routingUpdatePacket, len_packet);
	memcpy(routingUpdatePacket, selfEntry, selfEntryLen);
	memcpy(routingUpdatePacket+selfEntryLen, payload, payload_len);

	// send to all neighbours
	//int len = strlen(routingUpdatePacket);
	for(int i=0; i<num_routers; i++){
		if(routerNodeList[i].isNeighbour == 1){
			printf("sending to:%d\n", routerNodeList[i].id);
			
			routerNodeList[i].router_sock_id = self.router_sock_id;
			sendallUDP(routerNodeList[i], routingUpdatePacket, len_packet);
			//close(routerNodeList[i].router_sock_id);
		}
	}

}

int getRouterById(uint16_t router_id){
	for(int i=0; i< num_routers; i++){
		if(routerNodeList[i].id == router_id){
			return routerNodeList[i].index;
		}
	}
	return -1;
}

int getRouterByIp(uint32_t router_ip){
	for(int i=0; i< num_routers; i++){
		if(routerNodeList[i].ip == router_ip){
			return routerNodeList[i].index;
		}
	}
	return -1;
}

int getRouterByRouterPort(uint16_t router_port){
	for(int i=0; i<num_routers; i++){
		if(routerNodeList[i].router_port == router_port){
			return routerNodeList[i].index;
		}
	}
	return -1;
}

void updateRoutingTable(){

	long minCost = INF;
	long calCost = 0;
	uint16_t next_hop_id = 0;
	// need to calculate distance between self and k node
	// source will always be self and destination will always be k
	for(int k=0; k<num_routers; k++){
		if(k == self.index){
			continue;
		}
		minCost = INF;
		next_hop_id = INF;
		for(int i=0; i<num_routers; i++){
			if(i == self.index || routerNodeList[i].isNeighbour != 1){
				continue;
			}
			calCost = routingTable[self.index][i].cost + routingTable[i][k].path_cost;
			if(calCost < minCost){
				next_hop_id = routerNodeList[i].id;
				minCost = calCost;
			}
		}

		// update in the routernodelist and routing table
		routerNodeList[k].path_cost = (uint16_t)minCost;
		routingTable[self.index][k].path_cost = (uint16_t)minCost;
		if(minCost == INF && routerNodeList[k].isNeighbour == 1){
			routerNodeList[k].next_hop_id = routerNodeList[k].id;
			routingTable[self.index][k].next_hop_id = routerNodeList[k].id;
		}else{
			routerNodeList[k].next_hop_id = next_hop_id;
			routingTable[self.index][k].next_hop_id = next_hop_id;
		}	
	}
}

void init_routing_table(){
	for(int i=0; i<num_routers; i++){
		for(int j=0; j<num_routers; j++){
			if(i == self.index){
				routingTable[i][j] = routerNodeList[j];
			}else if(i == j){
				routingTable[i][j].path_cost = 0;
			}else{
				// set initial path cost as INF till update is received
				routingTable[i][j].path_cost = INF;
			}
		}
	}
	// update the path_cost
	updateRoutingTable();
}

void init_response(int sock_index, char* cntrl_payload){
	int offset_interval = 2;
	int offset_router_details = 4;
	int offset_per_router=0;
	uint16_t x;

	int self_index = 0;

	memcpy(&num_routers, cntrl_payload, sizeof(num_routers));
	num_routers = ntohs(num_routers);
    memcpy(&interval, cntrl_payload+offset_interval, sizeof(interval));
    interval = ntohs(interval);

    // create router detail list
    for (int i=0; i<num_routers; i++){
    	offset_per_router = offset_router_details + (i * ROUTER_DETAIL_SIZE);
    	// while parsing look for node with cost 0, this is the self node

    	memcpy(&routerNodeList[i].id, cntrl_payload+offset_per_router, sizeof(routerNodeList[i].id));
    	routerNodeList[i].id = ntohs(routerNodeList[i].id);
    	memcpy(&routerNodeList[i].router_port, cntrl_payload+offset_per_router+2, sizeof(routerNodeList[i].router_port));
    	routerNodeList[i].router_port = ntohs(routerNodeList[i].router_port);
    	memcpy(&routerNodeList[i].data_port, cntrl_payload+offset_per_router+4, sizeof(routerNodeList[i].data_port));
    	routerNodeList[i].data_port = ntohs(routerNodeList[i].data_port);
    	memcpy(&routerNodeList[i].cost, cntrl_payload+offset_per_router+6, sizeof(routerNodeList[i].cost));
    	routerNodeList[i].cost = ntohs(routerNodeList[i].cost);
    	memcpy(&routerNodeList[i].ip, cntrl_payload+offset_per_router+8, sizeof(routerNodeList[i].ip));
    	routerNodeList[i].ip = ntohl(routerNodeList[i].ip);

    	
    	routerNodeList[i].index = i;

    	if(routerNodeList[i].cost == 0){ // self
    		routerNodeList[i].isNeighbour = 0;
    		routerNodeList[i].path_cost = 0;
    		routerNodeList[i].next_hop_id = routerNodeList[i].id;
    		self.id = routerNodeList[i].id;
    		self.router_port = routerNodeList[i].router_port;
    		self.data_port = routerNodeList[i].data_port;
    		self.ip = routerNodeList[i].ip;
    		self.index = i;
    	}else if(routerNodeList[i].cost == INF){
    		routerNodeList[i].isNeighbour = 0;
    		routerNodeList[i].path_cost = INF;
    		routerNodeList[i].next_hop_id = INF;
    	}else{
    		routerNodeList[i].isNeighbour = 1;
    		routerNodeList[i].path_cost = routerNodeList[i].cost;
    		routerNodeList[i].next_hop_id = routerNodeList[i].id;
    	}
    	
    	printf("id:%d\n", routerNodeList[i].id);
    	printf("port1:%d\n", routerNodeList[i].router_port);
    	printf("port2:%d\n", routerNodeList[i].data_port);
    	// printf("cost:%d\n", routerNodeList[i].cost);
    	// printf("pathcost:%d\n", routerNodeList[i].path_cost);
    	// printf("nexthop:%d\n", routerNodeList[i].next_hop_id);
    	
    }

    // initialise routing table to all INF
    // add same node to routingTable
	init_routing_table();

    printf("creating router sock:%d\n", self.router_port);
    // create udp socket for router
    self.router_sock_id = createRouterSock(self.router_port, 0);
    // create tcp socket for data
    printf("creating router sock:%d\n", self.data_port);
    self.data_sock_id = createRouterSock(self.data_port, 1);

    // start timer & broadcast first update
    //braodcastUpdates(); // broadcast after 1 time interval

    // update the timeout value
	// insert in timer array
	struct timeval tv;
	tv.tv_sec = interval;
	timerArr[timerEnd].time = tv;
	timerEnd++;


    // send control response // empty payload
    char* cntrl_response_header = create_response_header(sock_index, 1, 0, 0);
    sendALL(sock_index, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);

	//free(cntrl_response_header);
}

void routing_table_response(int sock_index){
	// create payload first
	char* payload;
	int offset = 0;
	int size_per_router = 8;
	uint16_t padding = 0x00;
	char* cntrl_response;

	int payload_len = num_routers * size_per_router;
	payload = (char*)malloc(sizeof(char)*payload_len);

	for(int i=0; i<num_routers; i++){
		offset = i*size_per_router;
		uint16_t id = htons(routerNodeList[i].id);
		uint16_t next_hop_id = htons(routerNodeList[i].next_hop_id);
		uint16_t path_cost = htons(routerNodeList[i].path_cost);

		memcpy(payload+offset, &id, sizeof(routerNodeList[i].id));
		memcpy(payload+offset+2, &padding, sizeof(padding));
		memcpy(payload+offset+4, &next_hop_id, sizeof(routerNodeList[i].next_hop_id));
		memcpy(payload+offset+6, &path_cost, sizeof(routerNodeList[i].path_cost));

		printf("id:%d\n", routerNodeList[i].id);
		printf("next_hop:%d\n", routerNodeList[i].next_hop_id);
		printf("path_cost%d\n", routerNodeList[i].path_cost);
	}

	// create response header
	char* cntrl_response_header = create_response_header(sock_index, 2, 0, payload_len);
	cntrl_response = (char*)malloc(CNTRL_RESP_HEADER_SIZE + payload_len);
	memcpy(cntrl_response, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
	memcpy(cntrl_response + CNTRL_RESP_HEADER_SIZE, payload, payload_len);

	sendALL(sock_index, cntrl_response, CNTRL_RESP_HEADER_SIZE + payload_len);

	free(payload);
	free(cntrl_response);

}

void update_response(int sock_index, char* cntrl_payload){
	// router id and cost
	// change cost
	// recalculate min dist path
	uint16_t router_id;
	uint16_t new_cost;
	memcpy(&router_id, cntrl_payload, sizeof(router_id));
	router_id = ntohs(router_id);
    memcpy(&new_cost, cntrl_payload+2, sizeof(new_cost));
    new_cost = ntohs(new_cost);

    // search for node and update the cost
    int index = getRouterById(router_id);
    routerNodeList[index].cost = new_cost;
    routingTable[self.index][index].cost = new_cost;

    // process and update the distance vector
    updateRoutingTable();

    // send control response // empty payload
    char* cntrl_response_header = create_response_header(sock_index, 3, 0, 0);
    sendALL(sock_index, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);

	free(cntrl_response_header);

}

void crash_response(int sock_index){
	// send control response // empty payload
    char* cntrl_response_header = create_response_header(sock_index, 4, 0, 0);
    sendALL(sock_index, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);

	free(cntrl_response_header);
}

int connectToRouter(uint32_t ip, uint16_t port){
	int sockDesc;
    int val;
    int yes =1;
    struct addrinfo hints;
    struct addrinfo *result;
    struct sockaddr_in addr;

	ip = htonl(ip);
    memcpy(&(addr.sin_addr), &ip, sizeof(struct in_addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    sockDesc = socket(addr.sin_family, SOCK_STREAM, 0);
    printf("sockdesc:%d\n", sockDesc);
    perror("actual error:");
    val = connect(sockDesc, (struct sockaddr*)&addr, sizeof addr);
    if(val == -1){
    	printf("error here\n");
        perror("error:");
       // close(sockDesc);
        return -1;
    }
    return sockDesc;
}


// isLast = 1 for last, 2 for second last and 0 for rest
int createAndSendDataPacket(uint32_t dest_ip, uint8_t transfer_id, uint8_t ttl, uint16_t seq_num, int isLast, char* file_data){
	
	uint16_t padding1 = 0x00;
	uint16_t padding2 = 0x00;
	char* payload = (char*)malloc(sizeof(char)*DATA_PACKET_SIZE);

	// send through TCP data port
	// get next hop id using destination ip
	// dont change sequence of statement, calling htonl on ip 
	uint16_t next_hop_id;
	int next_hop_index = -1;

	// find next hop router
	for(int i=0; i<num_routers; i++){
		if(routerNodeList[i].ip == dest_ip){
			next_hop_id = routerNodeList[i].next_hop_id;
			next_hop_index = getRouterById(next_hop_id);
			break;
		}
	}
	if(next_hop_index == -1){
		// cannot reach the destination
		// return
		return -1;
	}

	dest_ip = htonl(dest_ip);
	seq_num = htons(seq_num);
	memcpy(payload, &dest_ip, sizeof(dest_ip));
	memcpy(payload+4, &transfer_id, sizeof(transfer_id));
	memcpy(payload+5, &ttl, sizeof(ttl));
	memcpy(payload+6, &seq_num, sizeof(seq_num));
	if(isLast == 1){
		padding1 = 0x8000;
	}
	padding1 = htons(padding1);
	memcpy(payload+8, &padding1, sizeof(padding1));
	memcpy(payload+9, &padding2, sizeof(padding2));
	memcpy(payload+12, file_data, 1024);
	
	memcpy(penultimateDataPacket, ultimateDataPacket, DATA_PACKET_SIZE);
	memcpy(ultimateDataPacket, payload, DATA_PACKET_SIZE);

	if(fileStatusArr[transfer_id].transfer_id != transfer_id){ // this is the first packet
    	fileStatusArr[transfer_id].first_seq_num = ntohs(seq_num);
    	printf("first seq num---------------------------%d\n", ntohs(seq_num));
    	fileStatusArr[transfer_id].ttl = ttl;
    	fileStatusArr[transfer_id].transfer_id = transfer_id;
    	fileStatusArr[transfer_id].index = 0;
    }
    // insert sequence number
    fileStatusArr[transfer_id].seq_num_arr[fileStatusArr[transfer_id].index++] = seq_num;
    
    int activeDataSock = routerNodeList[next_hop_index].data_sock_id;
    if(activeDataSock == 0){
    	activeDataSock = connectToRouter(routerNodeList[next_hop_index].ip, routerNodeList[next_hop_index].data_port);
    	routerNodeList[next_hop_index].data_sock_id = activeDataSock;
    }
	
	if(activeDataSock == -1){
		printf("could not connect -----------------------\n");
		return -1 ;
	}
	printf("sendall\n");
	sendALL(activeDataSock, payload, DATA_PACKET_SIZE);

	if(isLast == 1){
		//close the socket
		close(activeDataSock);
		printf("closed\n");
		routerNodeList[next_hop_index].data_sock_id = 0;
	}
	printf("free payload\n");
	free(payload);
	return 1;
}

void sendfile_response(int sock_index, char* cntrl_payload, int payload_len){
	uint32_t dest_ip;
	uint8_t ttl;
	uint8_t transfer_id;
	uint16_t seq_num;

	// payload_len - filename size = 8
	char* file_name;

	memcpy(&dest_ip, cntrl_payload, sizeof(dest_ip));
	dest_ip = ntohl(dest_ip);
	memcpy(&ttl, cntrl_payload+4, sizeof(ttl));
	memcpy(&transfer_id, cntrl_payload+5, sizeof(transfer_id));
	memcpy(&seq_num, cntrl_payload+6, sizeof(seq_num));
	seq_num = ntohs(seq_num);
	int file_name_len = payload_len - 8 + 1;
	file_name = (char*)malloc(file_name_len); // for remaining size, 1 for null terminator
	memset(file_name, 0, file_name_len);
	strncpy(file_name, cntrl_payload+8, file_name_len-1);
	file_name[file_name_len] = '\0';

	if(ttl <= 0){
		char* cntrl_response_header = create_response_header(sock_index, 5, 0, 0);
		sendALL(sock_index, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
		return;
	}
	int file_len=0;
	int loop = 0;

	FILE *ptr_file;
	char buf[1024];
	// https://www.linuxquestions.org/questions/programming-9/c-howto-read-binary-file-into-buffer-172985/
	ptr_file = fopen(file_name, "rb");
	if(ptr_file == NULL){
		char* cntrl_response_header = create_response_header(sock_index, 5, 0, 0);
		sendALL(sock_index, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
		return;
	}
	fseek(ptr_file, 0, SEEK_END);
	file_len = ftell(ptr_file);
	fseek(ptr_file, 0, SEEK_SET);
	loop = file_len/1024;

	int isLast = 0;
	int stat = 0;
	for(int i=0; i<loop; i++){
		if(i == loop-1){
			// ultimate
			isLast = 1;
		}
		fseek(ptr_file, 0, SEEK_CUR);
		printf("bytesread:%d\n",fread(buf, 1, 1024, ptr_file)); // (buf, size of struct to read, number of elements, file pointer)
		// packetize & send 
		stat = createAndSendDataPacket(dest_ip, transfer_id, ttl, seq_num, isLast, buf);
		printf("seqnum---------------------------------:%d\n", seq_num);
		if(stat == -1){
			fclose(ptr_file);
			return;
		}
		seq_num++;
	}
	fclose(ptr_file);

	// send control signal
	char* cntrl_response_header = create_response_header(sock_index, 5, 0, 0);
	sendALL(sock_index, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
}

void last_data_packet_response(int sock_index){
	if(ultimateDataPacket[0] == '\0'){
		char* cntrl_response_header = create_response_header(sock_index, 7, 0, 0);
		sendALL(sock_index, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
		return;
	}

	char* cntrl_response_header = create_response_header(sock_index, 7, 0, DATA_PACKET_SIZE);
   	char* cntrl_response = (char*)malloc(sizeof(char) * (CNTRL_RESP_HEADER_SIZE+DATA_PACKET_SIZE));
    memcpy(cntrl_response, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
    memcpy(cntrl_response+CNTRL_RESP_HEADER_SIZE, ultimateDataPacket, DATA_PACKET_SIZE);

    sendALL(sock_index, cntrl_response, CNTRL_RESP_HEADER_SIZE+DATA_PACKET_SIZE);
    free(cntrl_response);
}

void penultimate_data_packet_response(int sock_index){
	if(penultimateDataPacket[0] == '\0'){
		char* cntrl_response_header = create_response_header(sock_index, 8, 0, 0);
		sendALL(sock_index, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
		return;
	}
	char* cntrl_response_header = create_response_header(sock_index, 8, 0, DATA_PACKET_SIZE);
   	char* cntrl_response = (char*)malloc(sizeof(char) * (CNTRL_RESP_HEADER_SIZE+DATA_PACKET_SIZE));
    memcpy(cntrl_response, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
    memcpy(cntrl_response+CNTRL_RESP_HEADER_SIZE, penultimateDataPacket, DATA_PACKET_SIZE);

    sendALL(sock_index, cntrl_response, CNTRL_RESP_HEADER_SIZE+DATA_PACKET_SIZE);
	free(cntrl_response);
}

void sendfile_stats_response(int sock_index, char* cntrl_payload){

	uint8_t transfer_id;
	memcpy(&transfer_id, cntrl_payload, sizeof(transfer_id));

	if(fileStatusArr[transfer_id].index == 0){
		char* cntrl_response_header = create_response_header(sock_index, 6, 0, 0);
		sendALL(sock_index, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
		return;
	}

	uint16_t padding = 0x00;

	int file_stat_index = fileStatusArr[transfer_id].index;
	int payload_len = 4 + (file_stat_index)*sizeof(uint16_t);

	char* cntrl_response_payload = (char*)malloc(sizeof(char)*payload_len);
	memcpy(cntrl_response_payload, &transfer_id, sizeof(transfer_id));
	memcpy(cntrl_response_payload+1, &fileStatusArr[transfer_id].ttl, sizeof(fileStatusArr[transfer_id].ttl));
	memcpy(cntrl_response_payload+2, &padding, sizeof(padding));
	int offset = 4;

	for(int i=0; i<file_stat_index; i++){
		memcpy(cntrl_response_payload+offset, &fileStatusArr[transfer_id].seq_num_arr[i], sizeof(fileStatusArr[transfer_id].seq_num_arr[i]));
		offset = offset + sizeof(fileStatusArr[transfer_id].seq_num_arr[i]);
	}

	char* cntrl_response_header = create_response_header(sock_index, 6, 0, payload_len);
	char* cntrl_response = (char*)malloc(CNTRL_RESP_HEADER_SIZE+payload_len);

	memcpy(cntrl_response, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
	memcpy(cntrl_response + CNTRL_RESP_HEADER_SIZE, cntrl_response_payload, payload_len);

	sendALL(sock_index, cntrl_response, CNTRL_RESP_HEADER_SIZE+payload_len);

	free(cntrl_response_payload);
	free(cntrl_response_header);
	free(cntrl_response);

}

int processControlCommands(int active_control_sock_id){

	// read control message
	// get control code and payload if any
	char *cntrl_header, *cntrl_payload;
    uint8_t control_code;
    uint16_t payload_len;

    /* Get control header */
    cntrl_header = (char *) malloc(sizeof(char)*CNTRL_HEADER_SIZE);
    bzero(cntrl_header, CNTRL_HEADER_SIZE);

    if(recvALL(active_control_sock_id, cntrl_header, CNTRL_HEADER_SIZE) < 0){
    	close(active_control_sock_id);
    	FD_CLR(active_control_sock_id, &masterfds);
    	FD_CLR(active_control_sock_id, &controlfds);
    	//activeControlSock = 0;
    	printf("closed\n");
    	return 0;
    }
    struct controlRequestHeader *header = (struct controlRequestHeader *) cntrl_header;
   	control_code = header->control_code;
   	payload_len = ntohs(header->payload_len);

   	free(cntrl_header);

   	/* Get control payload */
    if(payload_len != 0){
        cntrl_payload = (char *) malloc(sizeof(char)*payload_len);
        bzero(cntrl_payload, payload_len);

        if(recvALL(active_control_sock_id, cntrl_payload, payload_len) < 0){
        	close(active_control_sock_id);
    		FD_CLR(active_control_sock_id, &masterfds);
    		FD_CLR(active_control_sock_id, &controlfds);
    		//activeControlSock = 0;
    		printf("closed\n");
    		return 0;
        }
    }

	// respond accordingly
	switch(control_code){
		case 0: 
			printf("author_response\n");
			author_response(active_control_sock_id);
			break;
		case 1:
			printf("init_repsonse\n");
			init_response(active_control_sock_id, cntrl_payload);
			return 1;
		case 2:
			printf("routing_table_response\n");
			routing_table_response(active_control_sock_id);
			break;
		case 3:
			printf("update_response\n");
			update_response(active_control_sock_id, cntrl_payload);
			break;
		case 4:
			printf("crash_response\n");
			crash_response(active_control_sock_id);
			exit(0);
			break;
		case 5:
			printf("sendfile\n");
			sendfile_response(active_control_sock_id, cntrl_payload, payload_len);
			break;
		case 6:
			printf("sendfile stat\n");
			sendfile_stats_response(active_control_sock_id, cntrl_payload);
			break;
		case 7:
			printf("last data packet\n");
			last_data_packet_response(active_control_sock_id);
			break;
		case 8:
			printf("penultimate data packet\n");
			penultimate_data_packet_response(active_control_sock_id);
			break;
	}
	return 0;
}

struct timeval insertTimerNode(int node_index, struct timeval tv){
	// look for index to insert
	// timerStart is the current node
	// if expected update received then reset the timer value
	// else insert node and return the timer value
	struct timeval new_tv;
	struct timeval dummy;
	dummy.tv_sec = 0;
	dummy.tv_usec = 0;
	int i;
	int isPresent = 0;
	// check if it should be inserted at the last position
	printf("timerstart is--------------------------------%d\n", timerStart);
	printf("timer here---------------------%d:%d\n", tv.tv_sec, tv.tv_usec);

	struct timeval cur_tv = timerArr[timerStart+1].time;
	struct timeval next_tv = timerArr[timerStart+2 == timerEnd ? 1:timerStart+2].time;
	struct timeval diff1;
	struct timeval diff2;
	struct timeval diff;
	if(timerStart+2 == timerEnd){
		timersub(&cur_tv, &dummy, &diff1);
		timersub(&timerArr[0].time, &next_tv, &diff2);
		timeradd(&diff1, &diff2, &diff);
	}else{
		timersub(&cur_tv, &next_tv, &diff);
	}

	for(int j=0; j<timerEnd; j++){
		if(timerArr[j].timerNodeIndex == node_index){
			isPresent = 1;
			break;
		}
	}

	struct timeval model_tv;
	model_tv.tv_sec = 0;
	model_tv.tv_usec = 300;
	// if diff is small we can update early;

	if(timerArr[timerStart].timerNodeIndex == node_index || (timerStart == 0 && timerArr[timerEnd-1].timerNodeIndex == node_index)){
		printf("updating late\n");
		routerNodeList[node_index].timeOutCount = 0;
		timerArr[timerStart==0?timerEnd-1:timerStart].isUpdatedEarly = 0;
		new_tv = tv;
	// }else if((timerStart+2 < timerEnd && timerRouterTrans[timerStart+2] == node_index) 
	// 	|| (timerStart+2 == timerEnd && timerRouterTrans[1] == node_index)){
	// 	printf("updating early\n");
	// 	routerNodeList[node_index].timeOutCount = 0;
	// 	new_tv= tv;
	}else if(isPresent && timercmp(&diff, &model_tv, <) == 1){
		printf("updating early\n");
		routerNodeList[node_index].timeOutCount = 0;
		// search timer by node index and set isUpdateEarly;
		timerArr[timerStart+1 == timerEnd ? 1:timerStart+2].isUpdatedEarly = 1;
		new_tv = tv;
	}else if(timerStart+1 == timerEnd){
		printf("inserting in end\n");
		//timerRouterTrans[timerEnd] = node_index;
		timerArr[timerEnd].timerNodeIndex = node_index;
		timerArr[timerEnd].time = tv;
		timerArr[timerEnd].isUpdatedEarly = 0;
		//timersub(&timerArr[timerStart], &tv, &timerArr[timerEnd]);
		timerStart++;
		timerEnd++;
		new_tv = tv;
	}else if(timerArr[timerStart+1].timerNodeIndex != node_index){
		printf("new node\n");
		// new update
		for(i=timerEnd; i>timerStart+1; i--){
			timerArr[i] = timerArr[i-1];	
			//timerRouterTrans[i] = timerRouterTrans[i-1];	
		}
		timerArr[timerStart+1].timerNodeIndex = node_index;
		timerArr[timerStart+1].isUpdatedEarly = 0;
		//timersub(&timerArr[timerStart], &tv, &timerArr[timerStart+1]); // i and timerStart is same here
		//timerArr[timerStart+1] = tv;
		timeradd(&timerArr[timerStart+1].time, &tv, &timerArr[timerStart+1].time);
		timerStart++;
		timerEnd++;
		new_tv = tv;
	}else{
		printf("correct update\n");
	    timerArr[timerStart+1].isUpdatedEarly = 0;
	    timerStart++;
	    struct timeval next = (timerStart+1 != timerEnd ? timerArr[timerStart+1].time: dummy);
	    printf("-------------:%d:%d\n", next.tv_sec, next.tv_usec);
	    printf("-------------:%d:%d\n", timerArr[timerStart].time.tv_sec, timerArr[timerStart].time.tv_usec);
	    timersub(&timerArr[timerStart].time, &next, &new_tv);
	    printf("tv-sec-----:%d, new_tv-sec------:%d\n", tv.tv_sec, new_tv.tv_sec);
	    printf("tv-usec----:%d, new_tv-sec------:%d\n", tv.tv_usec, new_tv.tv_usec);
	    timeradd(&new_tv, &tv, &new_tv); // add the remaining time to the next timeout

	}
	if(new_tv.tv_sec == 0 && new_tv.tv_usec == 0){
		new_tv.tv_usec = 1;
	}

	return new_tv;
	
}

struct timeval deleteTimerNode(int node_index){
	// remove from timer arr and timerroutertrans
	struct timeval del_tv = timerArr[timerStart+1].time;
	for(int i=timerStart+1; i<timerEnd-1; i++){
		timerArr[i] = timerArr[i+1];
		//timerRouterTrans[i] = timerRouterTrans[i+1];
	}

	timerEnd--;
	return del_tv;
	
}

int processRoutingUpdates(int sock_index){

	int size_per_router = 12;
	int total_len = (num_routers * size_per_router)+8;

	char* update_packet = (char*)malloc(sizeof(char)*total_len);

	if(printf("fbyte:%d\n",recvallUDP(sock_index, update_packet, total_len)) < 0){
    	// close connection
    }

    uint16_t num_nodes;
    uint16_t router_port;
    uint32_t router_ip;
    memcpy(&num_nodes, update_packet, sizeof(num_nodes));
    num_nodes = ntohs(num_nodes);
    memcpy(&router_port, update_packet+2, sizeof(router_port));
    router_port = ntohs(router_port);
    memcpy(&router_ip, update_packet+4, sizeof(router_ip));
    router_ip = ntohl(router_ip);

    int src_index = getRouterByIp(router_ip);
    //int src_index = getRouterByRouterPort(router_port);
    if(src_index < 0){
    	// not valid value
    	return -1;
    }
    uint16_t router_id;
    uint16_t cost;
    uint32_t ip;
    uint16_t port;

    struct sockaddr_in addr;

    int offset =0;
    int dest_index = 0;
    // process the updates in payload
    for(int i=0; i<num_routers; i++){
    	offset = 8+(i*size_per_router);
    	memcpy(&router_id, update_packet+offset+8, sizeof(router_id));// for padding
    	router_id = ntohs(router_id);
    	memcpy(&cost, update_packet+offset+10, sizeof(cost));
    	cost = ntohs(cost);

    	dest_index = getRouterById(router_id);
    	routingTable[src_index][dest_index].path_cost = cost;

    }

    updateRoutingTable();
    // reset timeout count
    routerNodeList[src_index].timeOutCount = 0;
    // to set timer array
    return src_index;
}

char* getFileName(uint16_t transfer_id){
    
    // number of digits
    int num;
	if(transfer_id > 100){
		num = 3;
	}else if(transfer_id > 10){
		num = 2;
	}else{
		num = 1;
	}
   
    // 5 for 'file-' and one for null
    char* file_name = (char*)malloc(6+sizeof(char)*num);
    strcpy(file_name, "file-");
    sprintf(file_name+5, "%d", transfer_id);
    file_name[strlen(file_name)] = '\0';
    // return file_name;
    return file_name;

}

void processData(int sock_index){
	//int newSock = new_data_conn(sock_index);

	char file_data[1024];
	char* packet_info = (char *) malloc(sizeof(char)*DATA_PACKET_SIZE);
	bzero(packet_info, DATA_PACKET_SIZE);

	uint32_t dest_ip;
	uint8_t transfer_id;
	uint8_t ttl;
	uint16_t seq_num;
	uint16_t padding;
	int isLast = 0;

    if(recvALL(sock_index, packet_info, DATA_PACKET_SIZE) < 0){
    	close(sock_index);
    	FD_CLR(sock_index, &masterfds);
    }

    //close(newSock);
    memcpy(&dest_ip, packet_info, sizeof(dest_ip));
    dest_ip = ntohl(dest_ip);
    memcpy(&transfer_id, packet_info+4, sizeof(transfer_id));
    memcpy(&ttl, packet_info+5, sizeof(ttl));
    memcpy(&seq_num, packet_info+6, sizeof(seq_num));
    seq_num = ntohs(seq_num);
    memcpy(&padding, packet_info+8, sizeof(padding));
    padding = ntohs(padding);
    memcpy(&file_data, packet_info+12, 1024);

    if(padding == 0x8000){
    	isLast = 1;
    }
    ttl = ttl-1;
    if(ttl > 0){
    	if(self.ip == dest_ip){ // check if this is destination
    		// save file
    		char* file_name = getFileName(transfer_id);

    		if(fileStatusArr[transfer_id].transfer_id != transfer_id){
    			fileStatusArr[transfer_id].f = fopen(file_name, "wb");
    		}

    		printf("byteswritten:%d\n",fwrite(file_data, 1, 1024, fileStatusArr[transfer_id].f));


    		if(fileStatusArr[transfer_id].first_seq_num == 0){ // this is the first packet
    			printf("--------------------------receiving file------------------------------");
	    		fileStatusArr[transfer_id].first_seq_num = seq_num;
	    		fileStatusArr[transfer_id].ttl = ttl;
	    		fileStatusArr[transfer_id].transfer_id = transfer_id;
	    		fileStatusArr[transfer_id].index = 0;
    		}
    		// insert sequence number
    		fileStatusArr[transfer_id].seq_num_arr[fileStatusArr[transfer_id].index++] = htons(seq_num);

	    	if(isLast == 1){
	    		//fileStatusArr[transfer_id].last_seq_num = seq_num;
	    		fclose(fileStatusArr[transfer_id].f);
	    		fileStatusArr[transfer_id].f = NULL;
	    	}

	    	// save 
    		memcpy(penultimateDataPacket, ultimateDataPacket, DATA_PACKET_SIZE);

    		dest_ip = htonl(dest_ip);
    		seq_num = htons(seq_num);
    		memcpy(ultimateDataPacket, &dest_ip, sizeof(dest_ip));
			memcpy(ultimateDataPacket+4, &transfer_id, sizeof(transfer_id));
			memcpy(ultimateDataPacket+5, &ttl, sizeof(ttl));
			memcpy(ultimateDataPacket+6, &seq_num, sizeof(seq_num));
			uint16_t padding1 = htons(padding);
			uint16_t padding2 = 0x00;
			memcpy(ultimateDataPacket+8, &padding1, sizeof(padding1));
			memcpy(ultimateDataPacket+9, &padding2, sizeof(padding2));
			memcpy(ultimateDataPacket+12, file_data, 1024);

    	}else{

    		createAndSendDataPacket(dest_ip, transfer_id, ttl, seq_num, isLast, file_data);
    	}
    	// save transfer details
    	printf("saving details\n");
    	
    }
    if(isLast == 1){
    	FD_CLR(sock_index, &masterfds);
    	close(sock_index);
    	printf("closed\n");
    }
    // if ttl is 0 or <0 drop
}

/**
 * main function
 *
 * @param  argc Number of arguments
 * @param  argv The argument list
 * @return 0 EXIT_SUCCESS
 */

int main(int argc, char **argv)
{

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	int count;
	int node_index;
	fd_set readfds; // sockets for read
	fd_set writefds; // sockets for write

	FD_ZERO(&masterfds);
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);

	// assign port to self
	self.control_port = atoi(argv[1]);
	// create TCP socket and listen on this port 
	self.control_sock_id = createRouterSock(self.control_port, 1);

	ultimateDataPacket[0] = '\0';
	penultimateDataPacket[0] = '\0';
	while(1){
		// tells whether timeout or input
		int select_status = -1;

	    readfds = masterfds; // copy it  
	    printf("starting select\n");
	    // printf("before select sec:%d\n", tv.tv_sec);
	    // printf("before select usec:%d\n", tv.tv_usec);
	    // if(timerisset(&tv) && tv.tv_sec == 0 && tv.tv_usec == 0){
	    // 	tv.tv_usec = 2;
	    // }
	    
	    if(timerisset(&tv)){
	    	//printf("before select sec:%d\n", tv.tv_sec);
	    	//printf("before select usec:%d\n", tv.tv_usec);
	    	if(tv.tv_sec == 0 && tv.tv_usec == 0 || (tv.tv_sec < 0) || (tv.tv_sec == 0 && tv.tv_usec < 0)){
				tv.tv_usec = 1;
				tv.tv_sec = 0;
			}
	    }
	    if ((select_status = select(fdMax+1, &readfds, NULL, NULL, timerisset(&tv)?&tv:NULL)) == -1) {
	        perror("select");
	        exit(4); 
	    }
	    // check timeout
	    if(select_status == 0){

	    	struct timeval next;
	    	struct timeval del_tv;
	    	struct timeval dummy;
	    	dummy.tv_sec = 0;
	    	dummy.tv_usec = 0;

	    	// if last node timedout
	    	if(timerStart == timerEnd-1){
	    		braodcastUpdates();
	    		timerStart = (timerStart+1 != timerEnd ? timerStart+1:0);
	    		next = (timerStart+1 != timerEnd ? timerArr[timerStart+1].time: dummy);
	    		timersub(&timerArr[timerStart].time, &next, &tv);
	    	}else{
	    		node_index = timerArr[timerStart+1].timerNodeIndex;
	    		printf("timerSTart:%d\n", timerStart);
	    		printf("node index timed out:%d\n", node_index);
	    		routerNodeList[node_index].timeOutCount++;
	    		if((routerNodeList[node_index].timeOutCount == 3 && timerArr[timerStart+1].isUpdatedEarly == 0)
	    			|| (routerNodeList[node_index].timeOutCount == 4 && timerArr[timerStart+1].isUpdatedEarly == 1)){
	    			printf("removing neighbour\n");
	    			// set cost to INF
	    			routerNodeList[node_index].cost = INF;
	    			routingTable[self.index][node_index].cost = INF;
	    			updateRoutingTable();
	    			// stop sending updates
	    			routerNodeList[node_index].isNeighbour = 0;
	    			// remove timer from list
	    			del_tv = deleteTimerNode(node_index);
	    			// reset the timer for select
	    			next = (timerStart+1 != timerEnd ? timerArr[timerStart+1].time: dummy);
	    			timersub(&del_tv, &next, &tv);
		    	}else{
		    		timerStart = (timerStart+1 != timerEnd ? timerStart+1:0);
	    			next = (timerStart+1 != timerEnd ? timerArr[timerStart+1].time: dummy);
	    			timersub(&timerArr[timerStart].time, &next, &tv);
		    	}
	    	}

	    	// reset 
	    	
	    	//printf("timersc:%d\n", tv.tv_sec);
	    	//printf("timerusc:%d\n", tv.tv_usec);
	    	if(tv.tv_sec == 0 && tv.tv_usec == 0){
	    		tv.tv_sec = 0;
	    		tv.tv_usec = 1;
	    	}

	    }else{
	    	count = fdMax;
	    	for(int i=0; i<=count; i++){
	        	if(FD_ISSET(i, &readfds)) {
	        		// if reading control port
	        		printf("sockid:%d\n", i);
	        		int activeControlSock;
	        		if(i == self.control_sock_id){
	        			// used to accept control connection from remote server
	        			// close already existing connection??
	        			// if(FD_ISSET(activeControlSock, &masterfds)){
	        			// 	close(activeControlSock);
	        			// 	FD_CLR(activeControlSock, &masterfds);
	        			// 	printf("closed\n");
	        			// }
	        			activeControlSock = new_control_conn(i);
	        			//FD_SET(activeControlSock, &masterfds);
	        			printf("active control sock created\n");
	        			int resp;
	        			resp = processControlCommands(activeControlSock);
	        			printf("processed\n");
	        			if(resp == 1){ // assign timer for the first time when init command is processed
	        				tv = timerArr[0].time;
	        				timerArr[0].timerNodeIndex = self.index;
	        				//printf("sec:%d\n", tv.tv_sec);
	        				//printf("usec:%d\n", tv.tv_usec);
	        			}
	        		}else if(i == self.router_sock_id){ // for router updates // control plane
	        			node_index = processRoutingUpdates(i);
	        			printf("got update from %d\n", node_index);
	        			// insert into timer array if new node
	        			if(node_index >= 0){
	        				tv = insertTimerNode(node_index, tv);
	        			}
	        		}else if(i == self.data_sock_id){ // for data plane
	        			struct timeval curr_tv = tv;
	        			printf("process data\n");
	        			int data_sock = new_data_conn(i);
	        			// set into master list
	        			//FD_SET(data_sock, &masterfds);
	        			printf("new data sock created\n");
	        			processData(data_sock);
	        			tv = curr_tv;
	        		}else if(FD_ISSET(i, &controlfds)){
	        			struct timeval curr_tv = tv;
	        			printf("reached process control commands\n");
	        			processControlCommands(i);
	        			tv = curr_tv;
	        		}else{
	        			struct timeval curr_tv = tv;
	        			processData(i);
	        			tv = curr_tv;
	        		}
	        	}
	        }
	    }
	}
	return 0;
}
