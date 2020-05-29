#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include "cc2420.h" // signal strenght

#include <stdio.h>
#include <string.h>

#define MAX_RETRANSMISSIONS 3
/*---------------------------------------------------------------------------*/
PROCESS(broadcast_process, "broadcast");
PROCESS(runicast_process, "runicast");
AUTOSTART_PROCESSES(&broadcast_process,&runicast_process);
/*---------------------------------------------------------------------------*/
static int parent[2];
static int rank = 999; 
static int threshold = -10;
static int parent_RSSI = -999;
static int valve_addr[2];

struct msg {
  int sender_type; // sender msg type : 1 : discovery ; 2 : up (data) ; 3 : down (action to do)
  int sender_rank; // sender_rank
  int origin_addr[2]; // origin address
  int sender_data_value; // data value
  char sender_data[64]; // data msg
};

struct route {
  int TTL;
  int addr_to_find[2];
  int next_node[2]; 
  // bool taken = false;
};

struct data {
  struct route *route;
  int next_slot_data;
  int full;
  int data[30];
};

static struct data taken_list[5];
static struct route route_table[64]; // static = init to 0 for all value
static int route_table_len = sizeof(route_table)/sizeof(route_table[0]);

static int 
least_squarred(struct data data) {

	int sum_x = 0;
	int sum_y = 0;
	int sum_xx = 0;
	int sum_xy = 0;
	int n = 30;

	int i;
	for (i = 0 ; i < 30 ; i++){

		int index = ( i + data.next_slot_data ) % 30;
		sum_x = sum_x + i;
		sum_xx = sum_xx + (i^2);
		sum_y = sum_y + data.data[index];
		sum_xy = sum_xy + i * data.data[index];

	}

	return (n*sum_xy - sum_x*sum_y)/(n*sum_xx - (sum_x^2));

}

static void
add_to_routing_table(int node_addr[2], int next[2], int value)
{
  static struct route new_route;
  new_route.TTL = 3;
  new_route.addr_to_find[0] = node_addr[0];
  new_route.addr_to_find[1] = node_addr[1];
  new_route.next_node[0] = next[0];
  new_route.next_node[1] = next[1];
  
  int n;
  int yes = -1;
  for (n = 0; n < 5; n++) {
  
    if ( taken_list[n].route  ) {
        if ( (*taken_list[n].route).TTL == 0 ) {
            yes = n;
        }
    } else {
        yes = n;
    }
    
  }

  if (yes >= 0 ) {
    static struct data new_data;
    new_data.route = &new_route;
	new_data.full = 0;
    new_data.next_slot_data = 0;
    new_data.data[new_data.next_slot_data] = value;
    new_data.next_slot_data = (new_data.next_slot_data + 1) % 30;
    taken_list[yes] = new_data;
    
  }

  int has_place = 0;
  int i;
  for (i = 0 ; i < route_table_len ; i++){
	if ( route_table[i].TTL == 0 ) {
		route_table[i] = new_route;
		has_place = 1;
		break;
	}
  }
  if (has_place == 0){
	printf("Error : Routing Table Full ! (Add more computation nodes or extend routing table size)\n"); // should not happen
	/*printf("Warning : Route Table Full : Reset with new route as first element\n"); // should not happen
	route_table[0] = new_route;
	for (i = 1 ; i < route_table_len ; i++){
		route_table[i].TTL = 0;
  	}*/
  }
}


// set struct msg
static void
set_packet(struct msg *new_msg, int type, int rank, int addr[2], int value, char msg[64])
{
	packetbuf_clear();
	packetbuf_set_datalen(sizeof(struct msg));
	new_msg = packetbuf_dataptr();
	memset(new_msg, 0, sizeof(struct msg));
	new_msg->sender_type = type;
	new_msg->sender_rank = rank;
	new_msg->origin_addr[0] = addr[0];
	new_msg->origin_addr[1] = addr[1];
	new_msg->sender_data_value = value;
	int i ;
	for (i=0 ; i < sizeof(msg) ; i++){
		new_msg->sender_data[i] = msg[i] ;
	}
}
//

static struct msg broadcast_received_msg;
static struct msg runicast_received_msg;
/*---------------------------------------------------------------------------*/
//	BROADCAST
/*---------------------------------------------------------------------------*/
static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
  memcpy(&broadcast_received_msg, packetbuf_dataptr(), sizeof(struct msg));
  printf("Rank : %d broadcast message of type %d received from %d.%d: rank '%d'\n",
         rank, broadcast_received_msg.sender_type, from->u8[0], from->u8[1], broadcast_received_msg.sender_rank);

  if( broadcast_received_msg.sender_type == 1 && broadcast_received_msg.sender_rank < rank ){
	if( broadcast_received_msg.sender_rank < rank-1 ) {
		parent[0] = from->u8[0];
  		parent[1] = from->u8[1];
		parent_RSSI = cc2420_last_rssi;
		rank = broadcast_received_msg.sender_rank + 1;
		process_post(&broadcast_process, PROCESS_EVENT_MSG, "Go");
		process_post(&runicast_process, PROCESS_EVENT_MSG, "Go2");	
	}
	else if ( broadcast_received_msg.sender_rank == rank-1){
		if ( (parent[0] != from->u8[0] || parent[1] != from->u8[1]) && parent_RSSI <= cc2420_last_rssi ){
			parent[0] = from->u8[0];
		  	parent[1] = from->u8[1];
			parent_RSSI = cc2420_last_rssi;
			process_post(&broadcast_process, PROCESS_EVENT_MSG, "Go");
			process_post(&runicast_process, PROCESS_EVENT_MSG, "Go2");
		}
		else if (parent[0] == from->u8[0] && parent[1] == from->u8[1]){
			process_post(&broadcast_process, PROCESS_EVENT_MSG, "Go");
		}
	}
  } 
  else if ( broadcast_received_msg.sender_type == 0 && parent[0] == from->u8[0] && parent[1] == from->u8[1] ){
	  printf("Reset Rank/Parent/RSSI\n");
	  rank = 999;
	  parent[0] = 0;
	  parent[1] = 0;
	  parent_RSSI = -999;
	  process_post(&broadcast_process, PROCESS_EVENT_MSG, "Out");
	  process_post(&runicast_process, PROCESS_EVENT_MSG, "TimerOut");
	  int i;
	  for (i = 0 ; i < route_table_len ; i++){
			route_table[i].TTL = 0;
	  }
  }
  else if ( broadcast_received_msg.sender_type == 5 && broadcast_received_msg.origin_addr[0] == linkaddr_node_addr.u8[0] && broadcast_received_msg.origin_addr[1] == linkaddr_node_addr.u8[1] ){
	  // Open valve
	  printf("Error No Valve To Open Here\n");
  }
  else if ( broadcast_received_msg.sender_type == 5 && parent[0] == from->u8[0] && parent[1] == from->u8[1] && (broadcast_received_msg.origin_addr[0] != linkaddr_node_addr.u8[0] || broadcast_received_msg.origin_addr[1] != linkaddr_node_addr.u8[1]) ){		
		process_post(&broadcast_process, PROCESS_EVENT_MSG, "SeekNode");
  }
  
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;

/*---------------------------------------------------------------------------*/
//	RUNICAST
/*---------------------------------------------------------------------------*/
static void
recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno)
{
  	memcpy(&runicast_received_msg, packetbuf_dataptr(), sizeof(struct msg));
  	printf("Rank : %d runicast message of type %d received from %d.%d: rank '%d'\n",
         rank, runicast_received_msg.sender_type, from->u8[0], from->u8[1], runicast_received_msg.sender_rank);

	if( runicast_received_msg.sender_type == 2 ){
		printf("Got Data From %d.%d : value : %d!\n",runicast_received_msg.origin_addr[0],runicast_received_msg.origin_addr[1],runicast_received_msg.sender_data_value);
		int from_addr[2];
		from_addr[0] = from->u8[0];
		from_addr[1] = from->u8[1];
		int is_in_table = 0;
		int i;
        int yes = -1;
        int correct_route_addr = -1;
        int pass = 1;
	for (i = 0 ; i < route_table_len ; i++){
	  if ( route_table[i].TTL != 0 && route_table[i].addr_to_find[0] == runicast_received_msg.origin_addr[0] && route_table[i].addr_to_find[1] == runicast_received_msg.origin_addr[1]) {
		route_table[i].TTL = 3;
		route_table[i].next_node[0] = from_addr[0];
		route_table[i].next_node[1] = from_addr[1];
                correct_route_addr = i;
                int n;
                for (n = 0; n < 5; n++) {
                
                    if ( (*taken_list[n].route).TTL == 0 ) {
                        yes = n;
                    }
                    if ( (*taken_list[n].route).addr_to_find[0] == runicast_received_msg.origin_addr[0] && (*taken_list[n].route).addr_to_find[1] == runicast_received_msg.origin_addr[1]) {
                        if ( taken_list[n].next_slot_data == 29 ) {
                            taken_list[n].full = 1;
                        }
                        
                        yes = -2;
                        taken_list[n].data[taken_list[n].next_slot_data] = runicast_received_msg.sender_data_value;
                        taken_list[n].next_slot_data = (taken_list[n].next_slot_data +1) % 30;
						if(taken_list[n].full){
                        	printf("Node %d.%d in Computation Node has 30 values!\n", (*taken_list[n].route).addr_to_find[0],(*taken_list[n].route).addr_to_find[1]);
						}
						break;
                    }
                }
                is_in_table = 1;
                break;
            }
          }
          
        if (yes >= 0 && correct_route_addr >= 0) {
            
            static struct data new_data;
            new_data.route = &route_table[correct_route_addr];
            new_data.next_slot_data = 0;
			new_data.full = 0;
            new_data.data[new_data.next_slot_data] = runicast_received_msg.sender_data_value;
            new_data.next_slot_data = (new_data.next_slot_data + 1) % 30;
            taken_list[yes] = new_data;
            
        }

	if(is_in_table == 0){
		add_to_routing_table(runicast_received_msg.origin_addr,from_addr, runicast_received_msg.sender_data_value);
	}
	
	//Computation
	int j;
	for (j = 0 ; j < 5 ; j++){
		if ( (*taken_list[j].route).TTL != 0 && (*taken_list[j].route).addr_to_find[0] == runicast_received_msg.origin_addr[0] && (*taken_list[j].route).addr_to_find[1] == runicast_received_msg.origin_addr[1]) {
			pass = 0; // the node is in the taken list, so we will not let the value pass to the root
        		if ( taken_list[j].full ) {
            		int slope = least_squarred(taken_list[j]);
				if ( slope > threshold ) {
					printf("GOT HERE %d\n",slope);
					valve_addr[0] = (*taken_list[j].route).addr_to_find[0]; //the valve to open
					valve_addr[1] = (*taken_list[j].route).addr_to_find[1];
					process_post(&runicast_process, PROCESS_EVENT_MSG, "OpenValveRunicast");
				}
			}
		}
	}

        if ( pass ) {
            process_post(&runicast_process, PROCESS_EVENT_MSG, "DataUp");	
        }
        
	}
	else if ( runicast_received_msg.sender_type == 3 && runicast_received_msg.origin_addr[0] == linkaddr_node_addr.u8[0] && runicast_received_msg.origin_addr[1] == linkaddr_node_addr.u8[1] ){
		  // Open valve
		  printf("Error No Valve To Open Here\n");
	}
	else if ( runicast_received_msg.sender_type == 3 && (runicast_received_msg.origin_addr[0] != linkaddr_node_addr.u8[0] || runicast_received_msg.origin_addr[1] != linkaddr_node_addr.u8[1])){
		  process_post(&runicast_process, PROCESS_EVENT_MSG, "RunicastSeekNode");
	}
	else if ( runicast_received_msg.sender_type == 4 && runicast_received_msg.origin_addr[0] == linkaddr_node_addr.u8[0] && runicast_received_msg.origin_addr[1] == linkaddr_node_addr.u8[1] ){
		  // Open valve
		  printf("Error No Valve To Open Here\n");
	}
	else if ( runicast_received_msg.sender_type == 4 && (runicast_received_msg.origin_addr[0] != linkaddr_node_addr.u8[0] || runicast_received_msg.origin_addr[1] != linkaddr_node_addr.u8[1])){
		  process_post(&runicast_process, PROCESS_EVENT_MSG, "DataUpForBroadcast");
	}
}
static void
sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
  //printf("runicast message sent to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}
static void
timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
  printf("runicast message timed out when sending to %d.%d, retransmissions %d\n",
	 to->u8[0], to->u8[1], retransmissions);

  if (to->u8[0] != parent[0] && to->u8[0] != parent[1]){
	  process_post(&runicast_process, PROCESS_EVENT_MSG, "DataUpForBroadcast");
  }
  else {
	  printf("Reset Rank/Parent/RSSI\n");
	  rank = 999;
	  parent[0] = 0;
	  parent[1] = 0;
	  parent_RSSI = -999;
	  process_post(&broadcast_process, PROCESS_EVENT_MSG, "Out");
	  process_post(&runicast_process, PROCESS_EVENT_MSG, "TimerOut");
	  int i;
	  for (i = 0 ; i < route_table_len ; i++){
			route_table[i].TTL = 0;
	  }
  }
}
static const struct runicast_callbacks runicast_callbacks = {recv_runicast,
							     sent_runicast,
							     timedout_runicast};
static struct runicast_conn runicast;
/*---------------------------------------------------------------------------*/
//	PROCESSES
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(broadcast_process, ev, data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  broadcast_open(&broadcast, 129, &broadcast_call);
  static struct msg new_msg;

  while(1) {

    /* Delay ... seconds */
    etimer_set(&et, CLOCK_SECOND * 30 + random_rand() % (CLOCK_SECOND * 30));

    PROCESS_WAIT_EVENT();

	if(etimer_expired(&et)){
		printf("My rank is %d\n", rank);
	}
	else if(strcmp(data,"Go") == 0){ // start broadcast
            
	    etimer_set(&et, CLOCK_SECOND * 2 + random_rand() % (CLOCK_SECOND * 8));
    	    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
		
	    char real_message[64];
	    int new_addr[2];
	    sprintf(real_message,"Type:%d,Rank:%d,Data: Hello From Node",1,rank);
	    set_packet(&new_msg, 1, rank, new_addr, 0, real_message);
	
	    broadcast_send(&broadcast);
	}
	else if(strcmp(data,"Out") == 0){ // parent out
	    etimer_set(&et, CLOCK_SECOND * 1 + random_rand() % (CLOCK_SECOND * 5));
    	    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
	    
	    char real_message[64];
	    int new_addr[2];
	    sprintf(real_message,"I'm Out guys");
	    set_packet(&new_msg, 0, rank, new_addr, 0, real_message);

	    broadcast_send(&broadcast);
	}
	else if(strcmp(data,"SeekNode") == 0){ // Down Search for node with broadcast
	    etimer_set(&et, CLOCK_SECOND * 2 + random_rand() % (CLOCK_SECOND * 8));
    	    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
	    
	    set_packet(&new_msg, 5, rank, broadcast_received_msg.origin_addr, broadcast_received_msg.sender_data_value, broadcast_received_msg.sender_data);

	    broadcast_send(&broadcast);
	}
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(runicast_process, ev, data)
{
  PROCESS_EXITHANDLER(runicast_close(&runicast);)

  PROCESS_BEGIN();

  runicast_open(&runicast, 144, &runicast_callbacks);
  static struct etimer et1;
  static struct etimer et2;
  static linkaddr_t recv;
  static struct msg new_msg;

  while(1) {

    PROCESS_WAIT_EVENT();

    if(strcmp(data,"Go2") == 0){
	etimer_set(&et1, CLOCK_SECOND * 10 + random_rand() % (CLOCK_SECOND * 45)); // avoid transmitting at the same time (collisions) and can ajust parent based on signal strenght (> retrans broadcast);
    }
    else if (strcmp(data,"TimerOut") == 0){ // time out , stop data runicast up
		etimer_stop(&et1);
		etimer_stop(&et2);
    }
    else if (strcmp(data,"DataUp") == 0){ // data up to computation

	if(!runicast_is_transmitting(&runicast)) {
		set_packet(&new_msg, 2, runicast_received_msg.sender_rank - 1, runicast_received_msg.origin_addr, runicast_received_msg.sender_data_value, runicast_received_msg.sender_data);

		recv.u8[0] = parent[0];
		recv.u8[1] = parent[1];
		runicast_send(&runicast, &recv, MAX_RETRANSMISSIONS);

	}
    }
    else if (strcmp(data,"DataUpForBroadcast") == 0){ // runicast failed so ask root for general broadcast

	if(!runicast_is_transmitting(&runicast)) {
		set_packet(&new_msg, 4, rank, runicast_received_msg.origin_addr, runicast_received_msg.sender_data_value, runicast_received_msg.sender_data);

		recv.u8[0] = parent[0];
		recv.u8[1] = parent[1];

		runicast_send(&runicast, &recv, MAX_RETRANSMISSIONS);

	}
    }
    else if (strcmp(data,"RunicastSeekNode") == 0){ // down search for node with runicast
	if(!runicast_is_transmitting(&runicast)) {
		
		set_packet(&new_msg, 3, rank, runicast_received_msg.origin_addr, runicast_received_msg.sender_data_value, runicast_received_msg.sender_data);

		int is_in_table = 0;
		int i;
		for (i = 0 ; i < route_table_len ; i++){
			if ( route_table[i].TTL != 0 && route_table[i].addr_to_find[0] == runicast_received_msg.origin_addr[0] && route_table[i].addr_to_find[1] == runicast_received_msg.origin_addr[1]) {
				recv.u8[0] = route_table[i].next_node[0];
				recv.u8[1] = route_table[i].next_node[1];
				is_in_table = 1;
				break;
			}
		}
		if(is_in_table == 0){
			printf("Node disapeared ?\n"); // rip -> general broadcast
			process_post(&broadcast_process, PROCESS_EVENT_MSG, "DataUpForBroadcast");
		}
		else{
			runicast_send(&runicast, &recv, MAX_RETRANSMISSIONS);
		}

	}	
    }
    else if (strcmp(data,"OpenValveRunicast") == 0){
		if(!runicast_is_transmitting(&runicast)) {
			char real_message[64];
			sprintf(real_message,"Runicast Open Valve for Node : %d.%d\n",runicast_received_msg.origin_addr[0],runicast_received_msg.origin_addr[1]);
			set_packet(&new_msg, 3, rank, valve_addr, 1234, real_message);

			int is_in_table = 0;
			int i;
			for (i = 0 ; i < route_table_len ; i++){
				if ( route_table[i].TTL != 0 && route_table[i].addr_to_find[0] == runicast_received_msg.origin_addr[0] && route_table[i].addr_to_find[1] == runicast_received_msg.origin_addr[1]) {
					recv.u8[0] = route_table[i].next_node[0];
					recv.u8[1] = route_table[i].next_node[1];
					is_in_table = 1;
					break;
				}
			}
			if(is_in_table == 0){
				printf("Node disapeared ?????\n"); // rip
				process_post(&broadcast_process, PROCESS_EVENT_MSG, "DataUpForBroadcast");
			}
			else{
				runicast_send(&runicast, &recv, MAX_RETRANSMISSIONS);
			}
		}
    }
    else if (etimer_expired(&et2)){ // et2 first because et1 still expired

		  int i;
		  for (i = 0 ; i < route_table_len ; i++){ // TTL decreased
			if(route_table[i].TTL > 0){
				route_table[i].TTL = route_table[i].TTL - 1;
			}
		  }
		
		etimer_set(&et2, CLOCK_SECOND * 60);
		etimer_set(&et1, CLOCK_SECOND * 6000); // little tests from time to time (seems useless now but avoid et1 expirating for ever)
    }
    else if (etimer_expired(&et1)){ // runicast test when adding parent
	if(!runicast_is_transmitting(&runicast)) {
        	char real_message[64];
		sprintf(real_message,"Type:%d,Rank:%d,Data: Hello Parent",1,rank);
		set_packet(&new_msg, 1, rank,  (int *) linkaddr_node_addr.u8, 0, real_message); // parent will do nothing when receiving this packet

		recv.u8[0] = parent[0];
		recv.u8[1] = parent[1];
		runicast_send(&runicast, &recv, MAX_RETRANSMISSIONS);
		etimer_set(&et2, CLOCK_SECOND * 60);
        
	}
    }					// timers in last to avoid overwriting received packets
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
