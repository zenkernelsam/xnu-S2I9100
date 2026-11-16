/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_WLAN_CONTROL_H
#define I9100_WLAN_CONTROL_H
#include "i9100_bcm4330.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_wlan_security {I9100_WLAN_OPEN=0,I9100_WLAN_WPA2_PSK_CCMP=1};
enum i9100_wlan_state {I9100_WLAN_IDLE=0,I9100_WLAN_CONFIGURING=1,I9100_WLAN_JOINING=2,
 I9100_WLAN_VERIFYING=3,I9100_WLAN_UP=4,I9100_WLAN_DISCONNECTING=5,I9100_WLAN_FAULT=6,I9100_WLAN_RESTORING=7};
enum i9100_wlan_error {I9100_WLAN_ARGUMENT=-1100,I9100_WLAN_DISABLED=-1101,I9100_WLAN_BUSY=-1102,
 I9100_WLAN_IO=-1103,I9100_WLAN_EVENT=-1104,I9100_WLAN_UNSUPPORTED=-1105,
 I9100_WLAN_TIMEOUT=-1106,I9100_WLAN_FIRMWARE=-1107,I9100_WLAN_NOT_LINKED=-1108};
struct i9100_wlan_event {
 uint16_t version,flags;uint32_t type,status,reason,auth_type,bytes;
 uint8_t address[6],ifidx,bsscfgidx;char ifname[17];const uint8_t *data;
};
struct i9100_wlan_config {
 uint32_t ssid_bytes,security,pmk_bytes;uint8_t ssid[32],bssid[6],pmk[32];
};
struct i9100_wlan_io {void *cookie;int (*lock)(void *);void (*unlock)(void *);uint64_t (*now_ns)(void *);};
struct i9100_wlan_control {
 struct i9100_bcm4330 *bus;struct i9100_wlan_io io;struct i9100_wlan_config config;
 uintptr_t self;uint64_t started,request_started;uint32_t initialized,enabled,generation,state,step,pending;
 uint32_t request_command,request_capacity,join_attempted,join_issued,ssid_ok,link_seen,keyed,verification_step,event_mask_owned;
 uint32_t event_count,link_losses,tx_frames;uint8_t saved_event_mask[16],event_mask[16],event_address[6],key_address[6],bssid[6];
 uint16_t request_id;int32_t error,firmware_status;uint32_t last_event,last_event_status,last_reason;
};
int i9100_wlan_event_decode(const uint8_t *ethernet,unsigned bytes,struct i9100_wlan_event *);
int i9100_wlan_control_init(struct i9100_wlan_control *,int enabled,struct i9100_bcm4330 *,const struct i9100_wlan_io *,uint32_t firmware_generation);
int i9100_wlan_join(struct i9100_wlan_control *,const struct i9100_wlan_config *);
int i9100_wlan_service(struct i9100_wlan_control *);
int i9100_wlan_reply(struct i9100_wlan_control *,const struct i9100_bcm_frame *);
int i9100_wlan_event(struct i9100_wlan_control *,const struct i9100_wlan_event *,uint32_t firmware_generation);
int i9100_wlan_send(struct i9100_wlan_control *,const uint8_t *,unsigned,unsigned);
int i9100_wlan_data_allowed(struct i9100_wlan_control *,const struct i9100_bcm_frame *);
int i9100_wlan_receive(struct i9100_wlan_control *,const struct i9100_bcm_frame *);
int i9100_wlan_disconnect(struct i9100_wlan_control *);
int i9100_wlan_control_close(struct i9100_wlan_control *);
/* Default OFF, single primary STA, OPEN or WPA2-PSK/CCMP with a caller-owned
 * prederived32B PMK. No passphrase hashing, secret logging, scan/RF activity
 * or hardware registration by init. Preserve event mask, send actual vendor
 * WLC/iovar commands and await each owned request reply before advancing.
 * SET_SSID success is not link-up. Require LINK+SET_SSID, WPA2 KEYED6, and
 * verified nonzero unicast BSSID and matching SSID replies before TX and RX.
 * One join attempt per fresh firmware generation; no late event reuse after
 * clean close. Link-down, deauth, disassoc or supplicant failure immediately
 * revokes data permission; no auto reconnect or open-security fallback.
 * Primary v2 event interface/bss0 only; v1 parser is provided, control rejects
 * it until interface identity can be proven. Protocol worker holds a separate
 * controller lock; never call while already holding the BCM semantic lock.
 * All receives must be generation-scoped; output event views retain input
 * lifetime. Close only after disconnect or fault; fault cancellation keeps
 * bus quarantined so late replies cannot become a new owner's state.
 * Disconnect sends DISASSOC once, awaits ACK plus down, then restores the
 * original event mask. CDC timeout500ms; join15s/disconnect5s. Fresh malformed
 * event packets must be treated as faults by the routing worker, not ignored.
 * receive takes an already parsed BCM frame:0 consumed control/event,1 allowed
 * Ethernet data,negative error. A valid BCM_FIRMWARE reply must still be
 * passed here; a transport parse failure must instead stop the worker.
 * No IO80211/IOEthernet interface is claimed by this command/state core. */
#ifdef __cplusplus
}
#endif
#endif
