#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"

#include <stdio.h>
#include <string.h>

#include "dev/serial-line.h"

#define MAX_RETRANSMISSIONS 3
/*---------------------------------------------------------------------------*/
PROCESS(broadcast_process, "Broadcast example");
PROCESS(runicast_process, "runicast test");
PROCESS(test_serial, "Serial line test process");
AUTOSTART_PROCESSES(&broadcast_process,&runicast_process,&test_serial);
/*---------------------------------------------------------------------------*/
static int parent[2];
static int rank = 1;

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
};

static struct route route_table[64]; // static = init to 0 for all value
static int route_table_len = sizeof(route_table)/sizeof(route_table[0]);

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
	printf("Warning : Route Table Full : Reset with new route as first element\n");
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
  printf("rank : %d broadcast message of type %d received from %d.%d: rank '%d'\n",
         rank, runicast_received_msg.sender_type, from->u8[0], from->u8[1], runicast_received_msg.sender_rank);
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

	if( runicast_received_msg.sender_type == 2 ){
		printf("Got Data From %d.%d : value : %d!\n",runicast_received_msg.origin_addr[0],runicast_received_msg.origin_addr[1],runicast_received_msg.sender_data_value);
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
		// do calculation
		// send answer if needed
		process_post(&runicast_process, PROCESS_EVENT_MSG, "OpenValveRunicast");
	}
	else if(runicast_received_msg.sender_type == 5){
		process_post(&broadcast_process, PROCESS_EVENT_MSG, "OpenValveBroadcast"); // if Runicast failed
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
  process_post(&runicast_process, PROCESS_EVENT_MSG, "OpenValveBroadcast"); // only case where root time out for now (see with server what to do)
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
    etimer_set(&et, CLOCK_SECOND * 40 + random_rand() % (CLOCK_SECOND * 20));

    PROCESS_WAIT_EVENT();

	if(etimer_expired(&et)){ // general broadcast from time to time to see if something changed

		char real_message[64];
		int new_addr[2];
		int new_value;
		sprintf(real_message,"Type:1,Rank:01,Data: Hello From Root");
		set_packet(&new_msg, 1, rank, new_addr, new_value, real_message);
	    
	    broadcast_send(&broadcast);
	}

	else if(strcmp(data,"OpenValveBroadcast") == 0){ // data down by broadcast
            
	    etimer_set(&et, CLOCK_SECOND * 5 + random_rand() % (CLOCK_SECOND * 10));
    	    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

	    char real_message[64];
	    sprintf(real_message,"Open Valve for Node : %d.%d\n",runicast_received_msg.origin_addr[0],runicast_received_msg.origin_addr[1]);
	    set_packet(&new_msg, 4, rank, runicast_received_msg.origin_addr, runicast_received_msg.sender_data_value, real_message);

	    broadcast_send(&broadcast);
	}

  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(runicast_process, ev, data)
{
  PROCESS_EXITHANDLER(runicast_close(&runicast);)

  PROCESS_BEGIN();

  runicast_open(&runicast, 144, &runicast_callbacks);
  static struct etimer et2;
  linkaddr_t recv;
  struct msg new_msg;

  while(1) {

    PROCESS_WAIT_EVENT();
	if(strcmp(data,"Got Something") == 0){

	}
	else if (etimer_expired(&et2)){
		  int i;
		  for (i = 0 ; i < route_table_len ; i++){
			if(route_table[i].TTL > 0){
				route_table[i].TTL = route_table[i].TTL - 1;
			}
		  }
		  etimer_set(&et2, CLOCK_SECOND * 60);
	}
	else if (strcmp(data,"OpenValveRunicast") == 0){
		if(!runicast_is_transmitting(&runicast)) {
			char real_message[64];
			sprintf(real_message,"Runicast Open Valve for Node : %d.%d\n",runicast_received_msg.origin_addr[0],runicast_received_msg.origin_addr[1]);
			set_packet(&new_msg, 3, rank, runicast_received_msg.origin_addr, runicast_received_msg.sender_data_value, real_message);

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
				process_post(&broadcast_process, PROCESS_EVENT_MSG, "OpenValveBroadcast");
			}
			else{
				printf("rank %d: sending data runicast to address %u.%u\n",rank,recv.u8[0],recv.u8[1]);
				runicast_send(&runicast, &recv, MAX_RETRANSMISSIONS);
				etimer_set(&et2, CLOCK_SECOND * 60); // TTL --
			}
		}
	}
    
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(test_serial, ev, data)
 {
   PROCESS_BEGIN();
 
   for(;;) {
     PROCESS_YIELD();
     if(ev == serial_line_event_message) {
       printf("received line: %s\n", (char *)data);
     }
   }
   PROCESS_END();
 }

