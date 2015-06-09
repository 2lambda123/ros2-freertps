#include "freertps/freertps.h"
#include "freertps/spdp.h"
#include "freertps/udp.h"
#include <string.h>

#define FRUDP_MAX_PARTICIPANTS 10
static frudp_participant_t g_frudp_spdp_participants[FRUDP_MAX_PARTICIPANTS];
static int g_frudp_spdp_num_participants = 0;

static frudp_participant_t g_frudp_spdp_rx_participant; // just for rx buffer

#define FRUDP_DISCOVERY_TX_BUFLEN 1500
static uint8_t g_frudp_discovery_tx_buf[FRUDP_DISCOVERY_TX_BUFLEN];

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void frudp_spdp_rx(frudp_receiver_state_t *rcvr,
                          const frudp_submsg_t *submsg,
                          const uint16_t scheme,
                          const uint8_t *data)
{
  FREERTPS_INFO("    spdp_rx\n");
  if (scheme != FRUDP_DATA_ENCAP_SCHEME_PL_CDR_LE)
  {
    FREERTPS_ERROR("expected spdp data to be PL_CDR_LE. bailing...\n");
    return;
  }
  frudp_participant_t *part = &g_frudp_spdp_rx_participant;
  // todo: spin through this param list and save it
  frudp_parameter_list_item_t *item = (frudp_parameter_list_item_t *)data;
  while ((uint8_t *)item < submsg->contents + submsg->header.len)
  {
    const frudp_parameterid_t pid = item->pid;
    if (pid == FRUDP_PID_SENTINEL)
      break;
    const uint8_t *pval = item->value;
    /*
    FREERTPS_INFO("      unhandled spdp rx param 0x%x len %d\n", 
                  (unsigned)pid, item->len);
    */
    if (pid == FRUDP_PID_PROTOCOL_VERSION)
    {
      FREERTPS_INFO("      spdp proto version 0x%04x\n", *((uint16_t *)(pval)));
      part->pver = *((frudp_pver_t *)(pval)); // todo: what about alignment?
    }
    else if (pid == FRUDP_PID_VENDORID)
    {
      part->vid = htons(*((frudp_vid_t *)pval));
      FREERTPS_INFO("      spdp vendorid 0x%04x = %s\n", part->vid, frudp_vendor(part->vid));
    }
    else if (pid == FRUDP_PID_DEFAULT_UNICAST_LOCATOR)
    {
      frudp_locator_t *loc = (frudp_locator_t *)pval;
      part->default_unicast_locator = *loc; // todo: worry about alignment
      if (loc->kind == FRUDP_LOCATOR_KIND_UDPV4)
      {
        FREERTPS_INFO("      spdp unicast locator udpv4: %d.%d.%d.%d:%d\n",
                      loc->address[12],
                      loc->address[13],
                      loc->address[14],
                      loc->address[15],
                      loc->port);
      }
    }
    else if (pid == FRUDP_PID_DEFAULT_MULTICAST_LOCATOR)
    {
      frudp_locator_t *loc = (frudp_locator_t *)pval;
      part->default_multicast_locator = *loc; // todo: worry about alignment
      if (loc->kind == FRUDP_LOCATOR_KIND_UDPV4)
      {
        FREERTPS_INFO("      spdp multicast locator udpv4: %d.%d.%d.%d:%d\n",
                      loc->address[12],
                      loc->address[13],
                      loc->address[14],
                      loc->address[15],
                      loc->port);
      }
      else
        FREERTPS_INFO("        spdp unknown mcast locator kind: %d\n", loc->kind);
    }
    else if (pid == FRUDP_PID_METATRAFFIC_UNICAST_LOCATOR)
    {
      frudp_locator_t *loc = (frudp_locator_t *)pval;
      part->metatraffic_unicast_locator = *loc; // todo: worry about alignment
      if (loc->kind == FRUDP_LOCATOR_KIND_UDPV4)
      {
        FREERTPS_INFO("      spdp metatraffic unicast locator udpv4: %d.%d.%d.%d:%d\n",
                      loc->address[12], loc->address[13],
                      loc->address[14], loc->address[15],
                      loc->port);
      }
      else
        FREERTPS_INFO("        spdp unknown metatraffic mcast locator kind: %d\n", loc->kind);
    }
    else if (pid == FRUDP_PID_METATRAFFIC_MULTICAST_LOCATOR)
    {
      frudp_locator_t *loc = (frudp_locator_t *)pval;
      part->metatraffic_multicast_locator = *loc; // todo: worry about alignment
      if (loc->kind == FRUDP_LOCATOR_KIND_UDPV4)
      {
        FREERTPS_INFO("      spdp metatraffic multicast locator udpv4: %d.%d.%d.%d:%d\n",
                      loc->address[12], loc->address[13],
                      loc->address[14], loc->address[15],
                      loc->port);
      }
      else
        FREERTPS_INFO("        spdp unknown metatraffic mcast locator kind: %d\n", loc->kind);
    }
    else if (pid == FRUDP_PID_PARTICIPANT_LEASE_DURATION)
    {
      frudp_duration_t *dur = (frudp_duration_t *)pval;
      part->lease_duration = *dur;
      FREERTPS_INFO("      spdp lease duration: %d.%09d\n", 
                    dur->sec, dur->nanosec);
    }
    else if (pid == FRUDP_PID_PARTICIPANT_GUID)
    {
      frudp_guid_t *guid = (frudp_guid_t *)pval;
      memcpy(&part->guid_prefix, &guid->guid_prefix, FRUDP_GUIDPREFIX_LEN);
      uint8_t *p = guid->guid_prefix;
      FREERTPS_INFO("      guid 0x%02x%02x%02x%02x"
                                 "%02x%02x%02x%02x"
                                 "%02x%02x%02x%02x\n",
                    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                    p[8], p[9], p[10], p[11]);
    }
    else if (pid == FRUDP_PID_BUILTIN_ENDPOINT_SET)
    {
      part->builtin_endpoints = *((frudp_builtin_endpoint_set_t *)pval);
      FREERTPS_INFO("      builtin endpoints: 0x%08x\n", 
                    part->builtin_endpoints);
    }
    else
      FREERTPS_INFO("      unhandled spdp rx param 0x%x len %d\n", 
                    (unsigned)pid, item->len);

    // todo: do something with parameter value
    // now, advance to next item in list...
    item = (frudp_parameter_list_item_t *)(((uint8_t *)item) + 4 + item->len);
  }
  // now that we have stuff the "part" buffer, spin through our
  // participant list and see if we already have this one
  bool found = false;
  for (int i = 0; !found && i < g_frudp_spdp_num_participants; i++)
  {
    frudp_participant_t *p = &g_frudp_spdp_participants[i];
    if (frudp_guid_prefix_identical(&p->guid_prefix, 
                                    &part->guid_prefix))
    {
      printf("found match at participant slot %d\n", i);
      found = true;
      // TODO: see if anything has changed. update if needed
    }
  }
  if (!found)
  {
    printf("didn't have this participant already.\n");
    if (g_frudp_spdp_num_participants < FRUDP_MAX_PARTICIPANTS)
    {
      const int p_idx = g_frudp_spdp_num_participants; // save typing
      frudp_participant_t *p = &g_frudp_spdp_participants[p_idx];
      *p = *part; // save everything plz
      printf("saved the new participant in slot %d\n", p_idx);
      g_frudp_spdp_num_participants++;
    }
    else
      printf("not enough room to save the new participant.\n");
  }
}

static fr_time_t frudp_spdp_last_bcast;

void frudp_spdp_init()
{
  FREERTPS_INFO("sdp init\n");
  frudp_spdp_last_bcast.seconds = 0;
  frudp_spdp_last_bcast.fraction = 0;
  frudp_entityid_t unknown = { .u = 0 };
  frudp_entityid_t writer  = { .s = { .key = { 0x00, 0x01, 0x00 }, 
                                      .kind = 0xc2 } };
  frudp_subscribe(unknown, writer, frudp_spdp_rx);
}

void frudp_spdp_fini()
{
  FREERTPS_INFO("sdp fini\n");
}

// todo: this will all eventually be factored somewhere else. for now, 
// just work through what it takes to send messages

// todo: consolidate spdp and sedp into a 'discovery' module

frudp_msg_t *frudp_init_msg(uint8_t *buf)
{
  frudp_msg_t *msg = (frudp_msg_t *)g_frudp_discovery_tx_buf;
  msg->header.magic_word = 0x53505452;
  msg->header.pver.major = 2;
  msg->header.pver.minor = 1;
  msg->header.vid = FREERTPS_VENDOR_ID;
  msg->header.guid_prefix[0] = FREERTPS_VENDOR_ID >> 8;  // big endian (?)
  msg->header.guid_prefix[1] = FREERTPS_VENDOR_ID & 0xff;
  // todo: actually get mac address
  const uint8_t mac[6] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab };
  for (int i = 0; i < 6; i++)
    msg->header.guid_prefix[2+i] = mac[i];
  msg->header.guid_prefix[8] = 0;
  return NULL;
}

void frudp_spdp_tick()
{
  const fr_time_t t = fr_time_now();
  if (fr_time_diff(&t, &frudp_spdp_last_bcast).seconds >= 1) // every second
  {
    FREERTPS_INFO("spdp bcast\n");
    frudp_spdp_last_bcast = t;
    // TODO: actually broadcast
  }
}
