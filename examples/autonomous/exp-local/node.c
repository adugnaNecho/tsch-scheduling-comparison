/*
 * Local interaction test.
 * Nodes periodically generate messages to their parents, the parents reply.
 */
#include "contiki.h"
#include "sys/node-id.h"
#include "lib/random.h"
#include "net/ipv6/uip-ds6.h"
#include "net/ipv6/simple-udp.h"
#include "net/routing/routing.h"

#include "sys/log.h"
#define LOG_MODULE "Node"
#define LOG_LEVEL LOG_LEVEL_INFO

/*---------------------------------------------------------------------------*/
static struct simple_udp_connection udp_conn;
/*---------------------------------------------------------------------------*/
static void
udp_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  uint32_t seqnum;
  memcpy(&seqnum, data, sizeof(seqnum));
  LOG_INFO("seqnum=%" PRIu32 " from=", (seqnum & ~(1 << 31u)));
  LOG_INFO_6ADDR(sender_addr);
  LOG_INFO_("\n");

  if((seqnum & (1 << 31u)) == 0) {
    /* modify the seqnum to signal it's a reply */
    seqnum |= 1 << 31u;
    /* simply send back to the sender */
    simple_udp_sendto(&udp_conn, &seqnum, sizeof(seqnum), sender_addr);
  }
}
/*---------------------------------------------------------------------------*/
PROCESS(node_process, "Node");
AUTOSTART_PROCESSES(&node_process);
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(node_process, ev, data)
{
  static struct etimer periodic_timer;
  static uint32_t seqnum;
  uip_ipaddr_t parent_ipaddr;

  PROCESS_BEGIN();

  printf("FIRMWARE_TYPE=%u\n", FIRMWARE_TYPE);
  printf("ORCHESTRA_CONF_UNICAST_PERIOD=%u\n", ORCHESTRA_CONF_UNICAST_PERIOD);

  simple_udp_register(&udp_conn, UDP_PORT, NULL,
                      UDP_PORT, udp_rx_callback);

  if(node_id == MAIN_GW_ID) {
    /* RPL root automatically becomes coordinator */  
    LOG_INFO("set as root\n");
    NETSTACK_ROUTING.root_start();
  }

  /* do it here because of the root rule */
  extern void orchestra_init(void);
  orchestra_init();

  /* start TSCH */
  NETSTACK_MAC.on();

  LOG_INFO("exp-local node started\n");

  if(node_id != MAIN_GW_ID) {

    etimer_set(&periodic_timer, WARM_UP_PERIOD_SEC * CLOCK_SECOND + random_rand() % (SEND_INTERVAL_SEC * CLOCK_SECOND));

    while(1) {
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));

      seqnum++;
      if(NETSTACK_ROUTING.node_is_reachable()
          && NETSTACK_ROUTING.get_parent_ipaddr(&parent_ipaddr)) {
        simple_udp_sendto(&udp_conn, &seqnum, sizeof(seqnum), &parent_ipaddr);
        LOG_INFO("seqnum=%" PRIu32 " to=", seqnum);
        LOG_INFO_6ADDR(&parent_ipaddr);
        LOG_INFO_("\n");
      } else {
        LOG_INFO("seqnum=%" PRIu32 " skipped: no parent\n", seqnum);
      }

      etimer_set(&periodic_timer, SEND_INTERVAL_SEC * CLOCK_SECOND);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
