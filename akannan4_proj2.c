/*
*
* 	Distance vector routing protocol
*
* 	@author 	Abhishek Kannan
* 	@email		akannan4@buffalo.edu
*
*/

#include <net/if.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <string.h> 
#include <stdlib.h>
#include <netdb.h>
#include <getopt.h>
#include <limits.h> // for USHRT_MAX definition


/* data structure for routing table */
 struct server{
	uint32_t server_ip;
	uint16_t server_id;
	uint16_t server_port;
	uint16_t cost;

	int is_neighbor;
	int num_of_skips;
	int is_alive;
	int next_hop;
};

/* data struture for update message */
/* distance vector format */
 struct  distance_vector {
	uint32_t server_ip; 
	uint16_t server_port;
	uint16_t padding; 
	uint16_t server_id; 
	uint16_t cost; 
} ;



/* routing packet format */
 struct routing_update_pkt{
	uint16_t num_of_updates; 
	uint16_t sender_port; 
	uint32_t sender_ip; 
	struct distance_vector* updates; 
} ;

int ** adj_matrix;

int my_port;
int num_of_servers;
uint32_t my_ip;
char * my_ip_raw;

int my_id;
int my_socket;
struct server *servers;

int cmdNo;
char** parsedCommand;
char* commands[11] = {"update","step","packets","display","disable","crash"};

int num_of_pkts_received=0;

char response_message[100];



/*
*
*	Stores the host's IP Address in the global variable 'my_ip_raw'
*
*	@return 
*		Integer indicating success/failure of function
*
*	Source: http://jhshi.me/2013/11/02/how-to-get-hosts-ip-address/
*
*/
int get_my_ip_address(){
	
	char* target_name = "8.8.8.8";
    char* target_port = "53";

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* info;
    int ret = 0;
    if ((ret = getaddrinfo(target_name, target_port, &hints, &info)) != 0) {
        printf("[ERROR]: getaddrinfo error: %s\n", gai_strerror(ret));
        return -1;
    }

    if (info->ai_family == AF_INET6) {
        printf("[ERROR]: do not support IPv6 yet.\n");
        return -1;
    }

    int sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    if (sock <= 0) {
        perror("socket");
        return -1;
    }

    if (connect(sock, info->ai_addr, info->ai_addrlen) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }

    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    if (getsockname(sock, (struct sockaddr*)&local_addr, &addr_len) < 0) {
        perror("getsockname");
        close(sock);
        return -1;
    }

    char * myip=(char*) malloc(sizeof(char)*INET6_ADDRSTRLEN);
    if (inet_ntop(local_addr.sin_family, &(local_addr.sin_addr), myip, INET6_ADDRSTRLEN) == NULL) {
        perror("inet_ntop");
        return -1;
    }

    my_ip_raw=myip;
}


/*
*
*	Resets the num_of_skips variable in the server structure
*
*/

void reset_skip_flag(int server_id){
	servers[server_id-1].num_of_skips=0;

}

/*
*
*	Bellman ford algorithm to find minimum distance to other servers
*
*/
void bellman_ford(){ 
	int i, j; 
	uint16_t dist, min_dist; 
	int src, dest,intermediate;
	dist = USHRT_MAX; 
	src = my_id-1;

	/*find the least cost to the router from current router*/

	for (i = 0; i < num_of_servers; i++){ 
		dest = i; 
       	if (src==dest) // if src and dest are same 
       		continue;
		
		min_dist = adj_matrix[src][dest];


		for (j = 0; j < num_of_servers; j++){ 


			intermediate = j;
			if (adj_matrix[intermediate][dest] == USHRT_MAX) {// no path b/w intermediate and dest
				if(servers[dest].next_hop==servers[intermediate].server_id){ // check if i originally reached dest through intermediate. if so, set cost to dest as INF
					min_dist=USHRT_MAX; 
					servers[dest].next_hop=-1;
				}

				continue;
			}
			// so there's a path between intermediate node and destination node
			if(servers[intermediate].is_neighbor==0  ) // intermediate is not my neighbor 
				continue;
			// so intermediate is my neighbox
			if(servers[intermediate].is_alive==0) // intermediate is dead to me
				continue;

			//so there's a path between intermediate and dest, intermediate is my neighbox and is alive

			// new distance = cost to go to intermediate node + cost from intermediate to dest node
			dist = adj_matrix[intermediate][dest] + servers[intermediate].cost; 
			
			// if new dist is lesser than prev cost, make it min cost and first hop as intermediate node
			if ((dist < min_dist) ){

				min_dist = dist;
				//printf("intermediate minimum cost %d\n",dist);
				servers[dest].next_hop = servers[intermediate].server_id;  // to go to dest, first hop is intermediate node
			}
						
		}
		//printf("from %d to %d new minimum cost %d\n",src,dest,min_dist);
		servers[dest].cost=min_dist;
		adj_matrix[src][dest] = min_dist; 

	} 
			

}

/*
*
*	Parses the user's commands and stores it in global variable 'parsedCommand'
*
*	@param cmd 
*		Command entered by the user
*
*	@return 
*		Integer indicating command ID
*
*/
int parse(char* cmd){ // used in project 1
    // Strip newline from STDIN. Is this really required ?

    int numberOfArgs=0;
    int x=strcspn(cmd, "\n");
    if(x>0)
        cmd[x] = '\0';
    

    int argsCount=0;
    char** args = (char**) malloc(3*sizeof(char*));
    int cmdNo=0;

    args[argsCount] = strtok(cmd," ");
    int length = strlen( args[argsCount] ); 
    char* cmdLower = ( char* )malloc( length +1 ); 
    int i;
    for(i = 0; i < length; i++ ) {
        cmdLower[i] = tolower( args[argsCount][i] );
    }
    //printf("Command in lower case is: %s\n",cmdLower);
    cmdLower[length]='\0';

    // check if command is valid
    for(cmdNo=0;cmdNo<6;cmdNo++) {

        if(strcmp(cmdLower,commands[cmdNo]) ==0) {

            break;
        }
    }
    //printf("%s\n",args[argsCount]);
    //printf("%s\n",cmdLower);
    if(cmdNo > 5) // Invalid command
    {
        printf("Invalid command \n");
        return -1;
    }
    
    while( args[argsCount] != NULL ) {
        numberOfArgs++;
    
      args[++argsCount] = strtok(NULL, " ");
   }
   parsedCommand = args;

   // check if usage is correct!

   if (cmdNo==4){
        if (numberOfArgs==2)
        return cmdNo;
    else {
        printf("Invalid command - Wrong Arguments \n");
        printf("Usage: disable <server-ID>\n");

        return -1;
    }
   }
   if (cmdNo==0){
        if (numberOfArgs==4)
        return cmdNo;
    else {
        printf("Invalid command - Wrong Arguments \n");
        printf("Usage: update  <server-ID1> <server-ID2> <Link Cost>\n");

        return -1;
    }
   }

   return cmdNo;

}


/*
*
*	Broadcasts distance vector to all neighbors
*
*/
void send_update_pkt(){
	struct sockaddr_in dest_ip_struct; 
	char ip_presentation[15];
	int i;


	for(i=0;i<num_of_servers;i++) {
		inet_ntop(AF_INET,&servers[i].server_ip,ip_presentation,sizeof(ip_presentation));


		if(servers[i].is_neighbor==1 && servers[i].is_alive==1){
			//printf("Sending update packet to ID: %d IP: %s on %d\n",servers[i].server_id,ip_presentation,servers[i].server_port);

			memset(&dest_ip_struct, 0, sizeof(struct sockaddr_in));

			dest_ip_struct.sin_family = AF_INET;

			//struct in_addr ip_struct;
			//inet_aton(servers[i].server_ip, &ip_struct); 

			dest_ip_struct.sin_addr.s_addr= servers[i].server_ip;
			dest_ip_struct.sin_port = htons(servers[i].server_port); 
			int MAX_PKT_SIZE=sizeof(struct routing_update_pkt)+(sizeof(struct distance_vector)*num_of_servers);

			char send_buf[1000]; 
			struct routing_update_pkt *packet_to_send;
			packet_to_send=(struct routing_update_pkt *)malloc(sizeof(struct routing_update_pkt) + sizeof(struct distance_vector) * num_of_servers);
			packet_to_send->updates=malloc(sizeof(struct distance_vector) * num_of_servers);

			prepare_update_pkt(packet_to_send); // fills packet_to_send with routing information
			serialize_packet(packet_to_send,&send_buf); 

			if(sendto(my_socket, &send_buf, sizeof(send_buf), 0, &dest_ip_struct, sizeof(dest_ip_struct))<0){
				perror("send");


			}
			else{
				printf("Sent update packet to ID: %d IP: %s on %d\n",servers[i].server_id,ip_presentation,servers[i].server_port); //segfault
						

			}
				

		}
	}
}


/*
*
*	Disables the link to a neighbor
*
*	@param server_id
*		Neighbor ID whose link has to be disabled
*
*	@return 
*		Integer indicating success/failure of function
*
*/
int disable(int server_id){
	memset(response_message,0,sizeof(response_message));
	if(server_id>num_of_servers){
		sprintf(response_message, "Server %d is invalid", server_id);
		return -1;

	}
	if(servers[server_id-1].is_neighbor==0){
		sprintf(response_message, "Server %d is not a neighbor", server_id);
		return -1;
	}

	
	servers[server_id-1].is_neighbor=0;
	servers[server_id-1].cost=USHRT_MAX;
	servers[server_id-1].next_hop=-1;
	adj_matrix[my_id-1][server_id-1]=USHRT_MAX;
	adj_matrix[server_id-1][my_id-1]=USHRT_MAX;
		

	strcpy(response_message,"SUCCESS");
	
	return 1;
	
}


/*
*
*	Updates the link cost to a neighbor
*
*	@param from
*		Source ID
*
*	@param to
*		Destination ID
*
*	@param cost
*		New cost
*
*	@return 
*		Integer indicating success/failure of function
*
*/
int update_link_cost(int from,int to,char* cost){
	int i;
	memset(response_message,0,sizeof(response_message));
	int new_cost;
	int inf_flag=0;
	if(strcmp(cost,"inf")==0 || strcmp(cost,"INF")==0){
		inf_flag=1;
		new_cost=USHRT_MAX;

	}
	else {
		new_cost=atoi(cost);

	}
	if(from>num_of_servers){
		sprintf(response_message, "Server %d is invalid", from);
		return -1;
	}
	if(to>num_of_servers){
		sprintf(response_message, "Server %d is invalid", to);
		return -1;
	}	
	if(from!=my_id && to!=my_id){
		strcpy(response_message,"You can only change link cost of neighbors");

		return -1;
	}
	if(from==to){
		strcpy(response_message,"Self links are always 0. You cannot modify self links");
		return -1;
	}	
	if(servers[to-1].is_neighbor==0){
		sprintf(response_message, "Server %d is not a neighbor", to);

		return -1;

	}	

	printf("%d %d %d\n",from,to,new_cost);
	
	adj_matrix[from-1][to-1]=new_cost;
	adj_matrix[to-1][from-1]=new_cost;
	servers[to-1].cost=new_cost;
	servers[to-1].next_hop=from;


	if(inf_flag==1){
		servers[to-1].next_hop=-1;

		for(i=0;i<num_of_servers;i++){
			if(servers[i].next_hop==to){
				servers[i].cost=USHRT_MAX;
				adj_matrix[from-1][i]=USHRT_MAX;
				adj_matrix[i][from-1]=USHRT_MAX;
				servers[i].next_hop=-1;
			}
		}
	}

	send_update_pkt(); //inform about link cost change
	strcpy(response_message,"SUCCESS");	
	return 1;

	
}

/*
*
*	Prepares the routing update packet that has to broadcasted to all neighbors
*
*	@param packet_to_send
*		Routing update packet
*
*/

void prepare_update_pkt(struct routing_update_pkt * packet_to_send){
	int j;

	packet_to_send->num_of_updates=htons(num_of_servers);
	packet_to_send->sender_port=htons(my_port);
	packet_to_send->sender_ip=htonl(my_ip);

	for(j=0;j<num_of_servers;j++){
		packet_to_send->updates[j].server_ip=htonl(servers[j].server_ip);
		packet_to_send->updates[j].server_port=htons(servers[j].server_port);
		packet_to_send->updates[j].padding=0;
		packet_to_send->updates[j].server_id=htons(servers[j].server_id);
		packet_to_send->updates[j].cost=htons(adj_matrix[my_id-1][j]);
	}


}

/*
*
*	Serializes the routing update packet packet_to_send and stores it in serialized_packet
*
*	@param packet_to_send
*		Routing update packet
*
*	@param serialized_packet
*		Serialized packet
*
*/

void serialize_packet(struct routing_update_pkt * packet_to_send,void *serialized_packet) {

	int j;
	
	memset(serialized_packet,0,1000);

	void*cur=serialized_packet;

	memcpy(cur,&packet_to_send->num_of_updates,sizeof(uint16_t));
	cur+=2;
	
	//Server port
	memcpy(cur,&packet_to_send->sender_port,sizeof(uint16_t));
	cur+=2;
	//Server IP
	memcpy(cur,&packet_to_send->sender_ip,sizeof(uint32_t));
	cur+=4;		

	for(j =0;j<num_of_servers;j++) {

		memcpy(cur,&packet_to_send->updates[j].server_ip,sizeof(uint32_t));
		cur+=4;

		memcpy(cur,&packet_to_send->updates[j].server_port,sizeof(uint16_t));
		cur+=2;
		//0x0
		cur+=2;

		memcpy(cur,&packet_to_send->updates[j].server_id,sizeof(uint16_t));
		cur+=2;

		memcpy(cur,&packet_to_send->updates[j].cost,sizeof(uint16_t));
		cur+=2;

	}
	
}

/*
*
*	Prints all neighbors
*
*/
void print_my_neighbors(){
	int i;
	for(i=0;i<num_of_servers;i++) {
		if(servers[i].is_neighbor==1)
			printf("Server ID %d \n",servers[i].server_id);
	}
}

/*
*
*	Prints all the distance vectors
*
*/


void display_all_distance_vectors(){
	printf("All distance vectors\n");
	int i,j;
	for (i = 0; i < num_of_servers; i++){
		printf("Server %d\t",servers[i].server_id);
		for (j = 0; j < num_of_servers; j++){
			printf ("%d\t\t\t", adj_matrix[i][j]); 
		} 
		printf ("\n"); 
	}

}


/*
*
*	Prints the routing table
*
*/

void display_routes(){
	int i,j;
	printf("Server ID\t Cost\t Next Hop\n");
	for (i = 0; i < num_of_servers; i++){
		printf ("%d\t %d\t %d\n",servers[i].server_id,servers[i].cost,servers[i].next_hop); 
	}

}

/*
*
*	Prints information about all servers
*
*/

void print_all_servers(){
	int i;
	for(i=0;i<num_of_servers;i++){
		char ip_presentation[15];
		
		printf("----------\n");
		printf("Server ID: %d \n",servers[i].server_id);
		inet_ntop(AF_INET,&servers[i].server_ip,ip_presentation,sizeof(ip_presentation));
		printf("Server IP: %s \n",ip_presentation);
		printf("Server Port: %d \n",servers[i].server_port);
		printf("----------\n");

	}
}

/*
*
*	Processes the received update packet 
*
*	@param packet
*		Packet to be processed
*
*	@return
*		Sender's ID
*
*/

uint16_t process_pkt(void * packet){
	int i;
	uint16_t server_count;
	uint16_t server_port;
	uint32_t server_ip;
	uint16_t server_id;
	uint16_t server_cost;	

	uint16_t sender_id;


	memcpy(&server_count,packet,2);
	packet=packet+2;
	memcpy(&server_port,packet,2);
	packet=packet+2;
	memcpy(&server_ip,packet,4);
	packet=packet+4;

	for(i=0;i<num_of_servers;i++){
		if(ntohl(server_ip)==servers[i].server_ip){
			sender_id=servers[i].server_id;
			
		}
	}
	/*
	printf("-----Header-----\n");
	printf("%d\t%d\n",ntohs(server_count),ntohs(server_port));
	printf("%d\n",ntohl(server_ip));
	printf("-----Update-----\n");
	*/


	for(i=0;i<ntohs(server_count);i++){
			memcpy(&server_ip,packet,4);
			//printf("%d\n",ntohl(server_ip));
			packet=packet+4;

			memcpy(&server_port,packet,2);
			//printf("%d\t",ntohs(server_port));
			packet=packet+2;

			//printf("0x0\n");

			packet=packet+2;

			memcpy(&server_id,packet,2);
			//printf("%d\t",ntohs(server_id));
			packet=packet+2;

			memcpy(&server_cost,packet,2);
			//printf("%d\t\n",ntohs(server_cost));

			packet=packet+2;

			//printf("\n\n");


			adj_matrix[sender_id-1][ntohs(server_id)-1]=ntohs(server_cost);
			




	}

	return sender_id;

}

/*
*
*	Deserialized the received packet
*
*	@param packet
*		Packet to be deserialized
*
*/

void deserialize_pkt(void * packet){

	uint16_t sender_id;
	sender_id=process_pkt(packet);

	if(servers[sender_id-1].is_alive==1 && servers[sender_id-1].is_neighbor==1){ // accept packet only if its from an active and neighnor server
		printf("RECEIVED A MESSAGE FROM SERVER %d\n",sender_id);
		bellman_ford();

		num_of_pkts_received++;

		reset_skip_flag(sender_id);
	}
	else{ //discard packet
		printf("PACKET FROM SERVER %d DISCARDED\n",sender_id);
		
	}
}

/*
*
*	Parses the topology file
*
*	@param topology_file
*		Topology file name
*
*/

void parse_topology_file(char * topology_file){
	char buffer[1024]; // to store line read from topology file
	int num_of_neighbors;
	int i,j;
	int from;
	int to;
	int cost;
	int server_port;
	int server_id;
	char * server_ip;


	FILE* topology_file_ptr=fopen(topology_file, "r");
	if(!topology_file_ptr){
		printf("Error opening file %s \n",topology_file);
		exit(0);
	}
	/*
	while(fgets (buffer, 1024, topology_file_ptr)){
		printf("%s",buffer);
	}
	*/
	fgets (buffer, 1024, topology_file_ptr);
	num_of_servers=atoi(buffer);

	fgets (buffer, 1024, topology_file_ptr);
	num_of_neighbors=atoi(buffer);

	//printf("Number of servers: %d\n",num_of_servers);
	//printf("Number of neighbors: %d\n",num_of_neighbors);

	//Start processing all servers 

	servers = ( struct server *)malloc(sizeof(struct server) * num_of_servers);


	for(i=0;i<num_of_servers;i++){


		fgets(buffer, 1024, topology_file_ptr);
		strtok(buffer," ");
		server_id=atoi(buffer);
		server_ip=strtok (NULL, " ");
		server_port=atoi(strtok (NULL, " "));
		if(strcmp(my_ip_raw,server_ip)==0){
			my_id=server_id;
			my_ip=inet_addr(server_ip);
			my_port=server_port;
			/*
			printf("I am Server ID %d \n",my_id);
			printf("I run on port %d \n",my_port);
			printf("My IP address(network order) %d \n",my_ip);

			inet_ntop(AF_INET,&my_ip,my_ip_raw,sizeof(my_ip_raw));
			printf("My IP address(presentation) %s\n",my_ip_raw);
			exit(0);
			*/
		}

		servers[i].server_ip = malloc( sizeof(char) * ( 16 ) );

		servers[i].server_id=server_id;
		servers[i].server_ip=inet_addr(server_ip); //store char* ip address as unsigned int
		//inet_pton(AF_INET,server_ip,servers[i].server_ip);
		servers[i].server_port=server_port;
		servers[i].is_alive=1;
		servers[i].num_of_skips=0;
		servers[i].cost=USHRT_MAX;
		servers[i].next_hop=-1;
		servers[i].is_neighbor=0;



	}

	//End processing all servers

	// Setup a num_of_servers * num_of_servers matrix for routing table
	adj_matrix = (uint16_t**)malloc(num_of_servers *sizeof(uint16_t*));

	for(i=0;i<num_of_servers;i++){
		adj_matrix[i] = (uint16_t**)malloc(num_of_servers *sizeof(uint16_t*));

	}


	for(i=0; i<num_of_servers; i++) {
		for(j=0;j<num_of_servers;j++){
			if(i==j)
				adj_matrix[i][i]=0;
			else
				adj_matrix[i][j]=USHRT_MAX; // infinity

		}
        
    }
    // print routing table
    //printf("-----Initial Routing Table-----\n");
    //display_all_distance_vectors();

	// update routing table based on topology file 

	for(i=0;i<num_of_neighbors;i++){

		fgets(buffer, 1024, topology_file_ptr);
		from=atoi(strtok(buffer," "));
		to=atoi(strtok(NULL," "));
		cost=atoi(strtok(NULL," "));

		adj_matrix[from-1][to-1]=cost; 
		adj_matrix[to-1][from-1]=cost;


		//save to my_neighbors
		servers[to-1].is_neighbor=1;
		servers[to-1].next_hop=my_id;
		servers[to-1].cost=cost;

	}

	servers[my_id-1].cost=0;
	servers[my_id-1].next_hop=my_id;

	// print routing table (updated)
	//printf("-----Updated Routing Table-----\n");
	//display_all_distance_vectors();



	fclose(topology_file_ptr);


}

/*
*
*	Prepares the routing update packet that has to broadcasted to all neighbors
*
*	@param argc
*		Number of command line arguments
*
*	@param argv
*		Command line arguments
*
*	@return
*		Integer indicating success/failure of function
*
*/
int main(int argc, char** argv) {
	int c;
	int t_flag=0;
	int i_flag=0;
	char* topology_file;
	char* update_interval;

	/* parsing command line arguments */
	static char usage[] = "usage: %s  -t <topology file name> -i <update interval>\n";

	while ((c = getopt (argc, argv, "t:i:")) != -1){
		switch (c) {
			case 't':
				t_flag=1;

				topology_file=optarg;
				break;
			case 'i':
				i_flag=1;

				update_interval=optarg;
				break;

			case '?':
				fprintf(stderr, usage, argv[0]);
				exit(0);
				break;

		}
	}

	if(t_flag==0) {
		fprintf(stderr, "%s: missing -i option\n", argv[0]);

		fprintf(stderr, usage, argv[0]);
		exit(0);
	}
	if(i_flag==0) {
		fprintf(stderr, "%s: missing -t option\n", argv[0]);
		fprintf(stderr, usage, argv[0]);
		exit(0);

	}

	/* end parsing command line arguments */

	get_my_ip_address();
	
	char msg[1024]; //read
	int numBytes; // number of bytes read

	int i,j,selected; //iterators
	int select_return;

	//server info
	int server_id;
	//end server info

	//my details
	struct sockaddr_in my_ip_struct;
	int optval = 1;  //for sockopt

	//end my details

	//FD
	fd_set read_fds, write_fds,read_fds_copy,write_fds_copy;
	int fdMax;

	struct timeval time_out;
	time_out.tv_sec=atoi(update_interval);

	parse_topology_file(topology_file);

	//create my socket

	if ((my_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0){ 
		perror("socket");
		return -1; 
    }
	memset(&my_ip_struct, 0 , sizeof(my_ip_struct));
	my_ip_struct.sin_family = AF_INET; 
    my_ip_struct.sin_addr.s_addr = INADDR_ANY; 
    my_ip_struct.sin_port = htons(my_port); 

    setsockopt(my_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));

    if (bind(my_socket, (struct sockaddr*)&my_ip_struct, sizeof(my_ip_struct)) < 0){ 
		perror("bind"); 
		return -1; 
    }

    printf("Server IP-> %s Port-> %d \n",my_ip_raw,my_port);


	FD_ZERO (&read_fds); 
	FD_SET (my_socket, &read_fds); 
	FD_SET (0, &read_fds);
	fdMax=my_socket;

    while(1) {

    	read_fds_copy = read_fds;
    	select_return=select(fdMax+1, &read_fds_copy,NULL, NULL, &time_out);
		if (select_return== -1) {
			perror("select");
			continue;
		}
		else if(select_return==0) { //timeout
			printf("Timeout! Sending updates to neighbors\n");

			for (i = 0; i < num_of_servers; i++){
				if(servers[i].is_neighbor==1)
					servers[i].num_of_skips++; 
			}

			for (i = 0; i < num_of_servers; i++){ 

				if(servers[i].is_neighbor){
					if (servers[i].num_of_skips == 3) {

						servers[i].is_alive = 0;
						servers[i].cost=USHRT_MAX;
						servers[i].is_neighbor=0;
						servers[i].next_hop=-1;
						adj_matrix[my_id-1][servers[i].server_id-1]= USHRT_MAX;
						adj_matrix[servers[i].server_id-1][my_id-1]= USHRT_MAX; 
						for(j=0;j<num_of_servers;j++){
							if(servers[j].next_hop==servers[i].server_id){
								servers[j].next_hop=-1;
								servers[j].cost=USHRT_MAX;

					
							}
						}
						//display_routes();
					}

				} 
			} 	

			send_update_pkt();
		
			time_out.tv_sec=atoi(update_interval); //reset timer value

			continue;
		}


		for(selected = 0;selected<=fdMax;selected++) {
			if(FD_ISSET(selected,&read_fds_copy)) {
				if(selected==0){
					memset(msg,0,sizeof(msg)); // clear msg array
					if ((numBytes = read(selected, msg, sizeof(msg))) <= 0)
						perror("Server read error");
					else{
						cmdNo=parse(msg);
						switch(cmdNo){
							case 0: //update
								update_link_cost(atoi(parsedCommand[1]),atoi(parsedCommand[2]),parsedCommand[3]);
								printf("UPDATE: %s\n",response_message);
							break;
							case 1: //step
								send_update_pkt();
								printf("%s SUCCESS\n",msg);								
							break;
							case 2: //packets
								printf("Number of packets received %d\n",num_of_pkts_received);
								num_of_pkts_received=0;
								printf("%s SUCCESS\n",msg);								
							break;
							case 3: //display

								display_routes();
								printf("%s SUCCESS\n",msg);								
							break;
							case 4: //disable
								disable(atoi(parsedCommand[1]));
								printf("DISABLE: %s\n",response_message);
							break;
							case 5: //crash
								close(my_socket);
								printf("%s SUCCESS\n",msg);
								return 1;
							break;
						}

					}


				}
				else if(selected==my_socket){ //receieved update packet from neighbors
					char recv_buf[1000];

					struct sockaddr_in src_ip_struct;
					socklen_t src_ip_struct_len=sizeof(struct sockaddr);
					memset(&src_ip_struct, 0, sizeof(struct sockaddr_in));


					if(recvfrom(my_socket, &recv_buf, sizeof(recv_buf),0, (struct sockaddr*)&src_ip_struct, &src_ip_struct_len)<0){
						perror("recv");
					}
					else {
						deserialize_pkt(recv_buf);
						
					}

					
				}

			}
		}

    }

	close(my_socket);
	printf("Closed socket bound to port %d \n",my_port);


}
