/*
 * Copyright (c) 2019, Institute of Electronics and Computer Science (EDI)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
/**
 * \file
 *         MSF: a slotframe dedicated to unicast data transmission. Designed for
 *         RPL storing mode only, as this is based on the knowledge of the children (and parent).
 *         Nodes listen/transmit at a timeslot defined as hash(MAC) % ORCHESTRA_SB_UNICAST_PERIOD
 *         Nodes also listen/transmit at hash(parent.MAC) % ORCHESTRA_SB_UNICAST_PERIOD
 *
 *         Based on the "6TiSCH Minimal Scheduling Function (MSF)" Internet Draft.
 *         draft-ietf-6tisch-msf-03
 *
 *         Code based on orchestra-rule-unicast-per-neighbor-rpl-ns.c
 *
 * \author Atis Elsts <atis.elsts@edi.lv>
 */

#include "contiki.h"
#include "orchestra.h"
#include "net/ipv6/uip-ds6-route.h"
#include "net/packetbuf.h"
#include "net/routing/routing.h"

static uint16_t slotframe_handle = 0;
static uint16_t channel_offset = ORCHESTRA_MULTIPLE_CHANNELS ? TSCH_DYNAMIC_CHANNEL_OFFSET : 1;
static struct tsch_slotframe *sf_unicast;

/*---------------------------------------------------------------------------*/
static uint16_t
get_node_timeslot(const linkaddr_t *addr)
{
  if(addr != NULL && ORCHESTRA_UNICAST_PERIOD > 0) {
    return ORCHESTRA_LINKADDR_HASH(addr) % ORCHESTRA_UNICAST_PERIOD;
  } else {
    return 0xffff;
  }
}
/*---------------------------------------------------------------------------*/
static int
select_packet(uint16_t *slotframe, uint16_t *timeslot)
{
  uint8_t is_child = 0;

  /* Select data packets we have a unicast link to */
  const linkaddr_t *dest = packetbuf_addr(PACKETBUF_ADDR_RECEIVER);
  if(packetbuf_attr(PACKETBUF_ATTR_FRAME_TYPE) == FRAME802154_DATAFRAME
     && !linkaddr_cmp(dest, &linkaddr_null)) {

    if(slotframe != NULL) {
      *slotframe = slotframe_handle;
    }

    /* handle the child case (valid for storing mode only) */
    if(nbr_table_get_from_lladdr(nbr_routes, (linkaddr_t *)dest) != NULL) {
      if(timeslot != NULL) {
        /* select the timeslot of the local node */
        *timeslot = get_node_timeslot(&linkaddr_node_addr);
        is_child = 1;
      }
    }

    if(!is_child) {
      /* handle the case of other nodes (including parent) */
      if(timeslot != NULL) {
        /* select the timeslot of the remote node */
        *timeslot = get_node_timeslot(dest);
      }
    }
    return 1;
  }
  return 0;
}
/*---------------------------------------------------------------------------*/
static void
add_parent_link(const linkaddr_t *linkaddr)
{
  if(linkaddr != NULL) {
    const uint16_t timeslot = get_node_timeslot(linkaddr);
    const uint8_t link_options = LINK_OPTION_RX | LINK_OPTION_TX | LINK_OPTION_SHARED;

    /* Add/update Rx link for the parent */
    tsch_schedule_add_link(sf_unicast, link_options, LINK_TYPE_NORMAL, &tsch_broadcast_address,
                           timeslot, channel_offset);
  }
}
/*---------------------------------------------------------------------------*/
static void
remove_parent_link(const linkaddr_t *linkaddr)
{
  if(linkaddr != NULL) {
    const uint16_t timeslot = get_node_timeslot(linkaddr);
    uint8_t link_options = LINK_OPTION_TX | LINK_OPTION_SHARED;

    if(timeslot == get_node_timeslot(&linkaddr_node_addr)) {
      /* We need this timeslot for Rx */
      link_options |= LINK_OPTION_RX;
    }

    /* Add/update link to remove the LINK_OPTION_RX flag */
    tsch_schedule_add_link(sf_unicast, link_options, LINK_TYPE_NORMAL, &tsch_broadcast_address,
                           timeslot, channel_offset);
  }
}
/*---------------------------------------------------------------------------*/
static void
new_time_source(const struct tsch_neighbor *old, const struct tsch_neighbor *new)
{
  if(new != old) {
    const linkaddr_t *old_addr = old != NULL ? &old->addr : NULL;
    const linkaddr_t *new_addr = new != NULL ? &new->addr : NULL;
    remove_parent_link(old_addr);
    add_parent_link(new_addr);
  }
}
/*---------------------------------------------------------------------------*/
static void
init(uint16_t sf_handle)
{
  int i;
  uint16_t rx_timeslot;
  slotframe_handle = sf_handle;
  /* Slotframe for unicast transmissions */
  sf_unicast = tsch_schedule_add_slotframe(slotframe_handle, ORCHESTRA_UNICAST_PERIOD);
  rx_timeslot = get_node_timeslot(&linkaddr_node_addr);
  /* Add a Tx link at each available timeslot. Make the link Rx at our own timeslot. */
  for(i = 0; i < ORCHESTRA_UNICAST_PERIOD; i++) {
    tsch_schedule_add_link(sf_unicast,
        LINK_OPTION_SHARED | LINK_OPTION_TX | ( i == rx_timeslot ? LINK_OPTION_RX : 0 ),
        LINK_TYPE_NORMAL, &tsch_broadcast_address,
        i, channel_offset);
  }
}
/*---------------------------------------------------------------------------*/
struct orchestra_rule unicast_msf = {
  init,
  new_time_source,
  select_packet,
  NULL,
  NULL,
  "unicast MSF"
};
