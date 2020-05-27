#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include "cc2420.h" // signal strenght

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RETRANSMISSIONS 3
/*---------------------------------------------------------------------------*/
PROCESS(broadcast_process, "Broadcast example");
PROCESS(runicast_process, "runicast test");
AUTOSTART_PROCESSES(&broadcast_process,&runicast_process);
/*---------------------------------------------------------------------------*/
static int parent[2];
static int routing_table[100][3]; // addr to join / next node / TTL
static int rank = 999;
static int parent_RSSI = -999;

static int last_temp = 10;

struct msg {
  int sender_type; // sender msg type : 0 : node down ; 1 : discovery ; 2 : up (data) for runicast ; 3 : down (action to do) / 4 : down broadcast ; 5 up (data) for broadcast (if runicast timed out) ;
  int sender_rank; // sender_rank
  int origin_addr[2]; // origin address
  int sender_data_value; // data value
  char sender_data[64]; // data msg
};

struct route {
  int TTL; // Time to live
  int addr_to_find[2]; // Target addr
  int next_node[2]; // next addr
};

static struct route route_table[64]; // static = init to 0 for all value
static int route_table_len = sizeof(route_table)/sizeof(route_table[0]);

static struct msg broadcast_received_msg;
static struct msg runicast_received_msg;

static struct msg current_target;

static int
data_generate()
{
  int offset;
  if ( last_temp >= 30 ){
	offset = (random_rand() % 5) - 4;
  }
  if ( last_temp <= -6 ){
	offset = random_rand() % 5;
  }
  else {
	offset = (random_rand() % 5) - 2;
  }
  last_temp = last_temp + offset;
   return last_temp;
}

static void
add_to_routing_table(int node_addr[2], int next[2])
{
  static struct route new_route;
  new_route.TTL = 3;
  new_route.addr_to_find[0] = node_addr[0];
  new_route.addr_to_find[1] = node_addr[1];
  new_route.next_node[0] = next[0];
  new_route.next_node[1] = next[1];
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
	printf("Warning : Route Table Full : Reset with new route as first element\n"); // should not happen
	route_table[0] = new_route;
	for (i = 1 ; i < route_table_len ; i++){
		route_table[i].TTL = 0;
  	}
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

/*---------------------------------------------------------------------------*/
//	BROADCAST
/*---------------------------------------------------------------------------*/
static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
  memcpy(&broadcast_received_msg, packetbuf_dataptr(), sizeof(struct msg));
  printf("rank : %d broadcast message of type %d received from %d.%d: rank '%d'\n",
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
  else if ( broadcast_received_msg.sender_type == 4 && broadcast_received_msg.origin_addr[0] == linkaddr_node_addr.u8[0] && broadcast_received_msg.origin_addr[1] == linkaddr_node_addr.u8[1] ){
	  // Open valve
	  printf("Open Valve Here, data was : %d\n",broadcast_received_msg.sender_data_value);
	  process_post(&runicast_process, PROCESS_EVENT_MSG, "StartLED");
  }
  else if ( broadcast_received_msg.sender_type == 4 && parent[0] == from->u8[0] && parent[1] == from->u8[1] && (broadcast_received_msg.origin_addr[0] != linkaddr_node_addr.u8[0] || broadcast_received_msg.origin_addr[1] != linkaddr_node_addr.u8[1]) ){
		printf("SEARCHING FOR %d.%d\n",broadcast_received_msg.origin_addr[0],broadcast_received_msg.origin_addr[1]);		
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
  	printf("rank : %d runicast message of type %d received from %d.%d: rank '%d'\n",
         rank, runicast_received_msg.sender_type, from->u8[0], from->u8[1], runicast_received_msg.sender_rank);

	if ( runicast_received_msg.sender_type == 2 ){ // routing_table
		int from_addr[2];
		from_addr[0] = from->u8[0];
		from_addr[1] = from->u8[1];
		int is_in_table = 0;
		int i;
		  for (i = 0 ; i < route_table_len ; i++){
			if ( route_table[i].TTL != 0 && route_table[i].addr_to_find[0] == runicast_received_msg.origin_addr[0] && route_table[i].addr_to_find[1] == runicast_received_msg.origin_addr[1]) {
				route_table[i].TTL = 3;
				route_table[i].next_node[0] = from_addr[0];
				route_table[i].next_node[1] = from_addr[1];
				is_in_table = 1;
				break;
			}
		  }
		if(is_in_table == 0){
			add_to_routing_table(runicast_received_msg.origin_addr,from_addr);
		}
		process_post(&runicast_process, PROCESS_EVENT_MSG, "DataUp");	
	}
	else if ( runicast_received_msg.sender_type == 3 && runicast_received_msg.origin_addr[0] == linkaddr_node_addr.u8[0] && runicast_received_msg.origin_addr[1] == linkaddr_node_addr.u8[1] ){
		  // Open valve
		  printf("Open Valve Here, data was : %d\n",runicast_received_msg.sender_data_value);
		  process_post(&runicast_process, PROCESS_EVENT_MSG, "StartLED");
	}
	else if ( runicast_received_msg.sender_type == 3 && (runicast_received_msg.origin_addr[0] != linkaddr_node_addr.u8[0] || runicast_received_msg.origin_addr[1] != linkaddr_node_addr.u8[1])){
		  process_post(&runicast_process, PROCESS_EVENT_MSG, "RunicastSeekNode");
	}
	else if ( runicast_received_msg.sender_type == 5 && runicast_received_msg.origin_addr[0] == linkaddr_node_addr.u8[0] && runicast_received_msg.origin_addr[1] == linkaddr_node_addr.u8[1] ){
		  // Open valve
		  printf("Open Valve Here, data was : %d\n",runicast_received_msg.sender_data_value);
		  process_post(&runicast_process, PROCESS_EVENT_MSG, "StartLED");
	}
	else if ( runicast_received_msg.sender_type == 5 && (runicast_received_msg.origin_addr[0] != linkaddr_node_addr.u8[0] || runicast_received_msg.origin_addr[1] != linkaddr_node_addr.u8[1])){
		  process_post(&runicast_process, PROCESS_EVENT_MSG, "DataUpForBroadcast");
	}
}
static void
sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
  printf("runicast message sent to %d.%d, retransmissions %d\n",
 	 to->u8[0], to->u8[1], retransmissions);
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
	    int new_value;
	    sprintf(real_message,"Type:%d,Rank:%d,Data: Hello From Node",1,rank);
	    set_packet(&new_msg, 1, rank, new_addr, new_value, real_message);
	
	    broadcast_send(&broadcast);
	}
	else if(strcmp(data,"Out") == 0){ // parent out
	    etimer_set(&et, CLOCK_SECOND * 1 + random_rand() % (CLOCK_SECOND * 5));
    	    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
	    
	    char real_message[64];
	    int new_addr[2];
	    int new_value;
	    sprintf(real_message,"I'm Out guys");
	    set_packet(&new_msg, 0, rank, new_addr, new_value, real_message);

	    broadcast_send(&broadcast);
	}
	else if(strcmp(data,"SeekNode") == 0){ // Down Search for node with broadcast
	    etimer_set(&et, CLOCK_SECOND * 2 + random_rand() % (CLOCK_SECOND * 8));
    	    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
	    
	    set_packet(&new_msg, 4, rank, broadcast_received_msg.origin_addr, broadcast_received_msg.sender_data_value, broadcast_received_msg.sender_data);

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
  leds_off(LEDS_ALL);

  runicast_open(&runicast, 144, &runicast_callbacks);
  static struct etimer et1;
  static struct etimer et2;
  static struct etimer et3; // LEDS
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
		set_packet(&new_msg, 5, rank, runicast_received_msg.origin_addr, runicast_received_msg.sender_data_value, runicast_received_msg.sender_data);

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
    else if (strcmp(data,"StartLED") == 0){
	leds_on(LEDS_GREEN);
	etimer_set(&et3, CLOCK_SECOND * 600); // 10 minutes timer
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
	if(!runicast_is_transmitting(&runicast)) { // data up sending
		char real_message[64];
		int new_value = (int) data_generate();
		sprintf(real_message,"Data sent by %d.%d : %d\n",linkaddr_node_addr.u8[0],linkaddr_node_addr.u8[1],new_value);
		set_packet(&new_msg, 2, rank, (int *) linkaddr_node_addr.u8, new_value, real_message);

		recv.u8[0] = parent[0];
		recv.u8[1] = parent[1];
		
		printf("Sending Data For Computation !\n");
		runicast_send(&runicast, &recv, MAX_RETRANSMISSIONS);
		
	}
    }
    else if (etimer_expired(&et1)){ // runicast test when adding parent
	if(!runicast_is_transmitting(&runicast)) {
		char real_message[64];
		int new_value = (int) data_generate();
		sprintf(real_message,"Type:%d,Rank:%d,Data: Hello Parent",1,rank);
		set_packet(&new_msg, 1, rank,  (int *) linkaddr_node_addr.u8, new_value, real_message);

		recv.u8[0] = parent[0];
		recv.u8[1] = parent[1];
		runicast_send(&runicast, &recv, MAX_RETRANSMISSIONS);
		etimer_set(&et2, CLOCK_SECOND * 60);
	}
    }					// timers in last to avoid overwriting received packets
    else if (etimer_expired(&et3)){ // Turn off LED after 10 min
	leds_off(LEDS_GREEN);
    }
  }
  leds_off(LEDS_ALL);
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
