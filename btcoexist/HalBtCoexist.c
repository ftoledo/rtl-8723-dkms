/*============================================================
  * Description:
  *
  * This file is for 92CE/92CU dynamic mechanism
  * for BT Co-exist only
  *
  * By cosa 08/17/2010
  *
  *============================================================*/

/*============================================================
  * include files
  *============================================================*/

#include "halbt_precomp.h"
#include "rtl_btc.h"

/*#if(BT_30_SUPPORT == 1)*/
#if 1
/*============================================================
  * local function
  *============================================================*/
void btdm_pwdb_monitor(struct rtl_priv *adapter)
{
}

/*============================================================
  * extern function
  *============================================================*/
void btdm_initlize_variables(struct rtl_priv *adapter)
{
	exhalbtc_initlize_variables((void *)adapter);
}

void btdm_init_all_workitem(struct rtl_priv *adapter)
{
}

void btdm_free_all_workitem(struct rtl_priv *adapter)
{
}

void btdm_display_bt_coex_info(struct rtl_priv *adapter)
{
	exhalbtc_display_bt_coex_info(&gl_bt_coexist);
}

bool btdm_is_bt40(struct rtl_priv *adapter)
{
	struct rtl_priv *rtlpriv = adapter;
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	bool is_ht40 = true;
	enum ht_channel_width bw = rtlphy->current_chan_bw;
	
	
	if (bw == HT_CHANNEL_WIDTH_20)
		is_ht40 = false;
	else if (bw == HT_CHANNEL_WIDTH_20_40)
		is_ht40 = true;

	return is_ht40;
}

bool btdm_legacy(struct rtl_priv *adapter)
{
	struct rtl_priv *rtlpriv = adapter;
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	
	bool is_legacy = false;

	if ((mac->mode == WIRELESS_MODE_B) || (mac->mode == WIRELESS_MODE_B))
		is_legacy = true;

	return is_legacy;
}

s32 btdm_get_wifi_rssi(struct rtl_priv *adapter)
{
	struct rtl_priv *rtlpriv = adapter;
	s32	undecorated_smoothed_pwdb = 0;
	
	if (rtlpriv->mac80211.link_state >= MAC80211_LINKED)
		undecorated_smoothed_pwdb = rtlpriv->dm.undecorated_smoothed_pwdb;
	else /* associated entry pwdb */
		undecorated_smoothed_pwdb = rtlpriv->dm.undecorated_smoothed_pwdb;
	return undecorated_smoothed_pwdb;
}

bool btdm_is_disable_edca_turbo(struct rtl_priv *adapter)
{
	struct rtl_priv *rtlpriv = adapter;
	bool bt_change_edca = false;
	u32 cur_edca_val;
	u32 edca_bt_hs_uplink = 0x5ea42b, edca_bt_hs_downlink = 0x5ea42b;
	u32 edca_hs;
	u32 edca_addr = REG_EDCA_VI_PARAM;

	cur_edca_val = rtl_read_dword(rtlpriv, edca_addr);
	if (btdm_is_wifi_uplink(rtlpriv)){
		if (cur_edca_val != edca_bt_hs_uplink){
			edca_hs = edca_bt_hs_uplink;
			bt_change_edca = true;
		}
	}else{
		if (cur_edca_val != edca_bt_hs_downlink){
			edca_hs = edca_bt_hs_downlink;
			bt_change_edca = true;
		}
	}
	
	if(bt_change_edca)
		rtl_write_dword(rtlpriv, edca_addr, edca_hs);
	
	return true;
}

void btdm_coexist(struct rtl_priv *adapter)
{
	btdm_pwdb_monitor(adapter);
	exhalbtc_periodical(&gl_bt_coexist);
}

int btdm_get_hs_wifi_rssi(struct rtl_priv *adapter)
{
	struct rtl_priv *rtlpriv = adapter;
	int undecorated_smoothed_pwdb=0;

	undecorated_smoothed_pwdb = rtlpriv->dm.undecorated_smoothed_pwdb;
	return undecorated_smoothed_pwdb;
}

bool btdm_is_bt_busy(struct rtl_priv *adapter)
{
	return gl_bt_coexist.bt_info.bt_busy;
}

bool btdm_is_limited_dig(struct rtl_priv *adapter)
{
	return gl_bt_coexist.bt_info.limited_dig;
}

bool btdm_is_wifi_busy(struct rtl_priv *adapter)
{

	struct rtl_priv *rtlpriv = adapter;
	
	if (rtlpriv->link_info.b_busytraffic)
		return true;
	else
		return false;
}

bool btdm_is_wifi_uplink(struct rtl_priv *adapter)
{
	struct rtl_priv *rtlpriv = adapter;
	
	if (rtlpriv->link_info.b_tx_busy_traffic)
		return true;
	else
		return false;
}

void btdm_stack_operation_notify(struct rtl_priv *adapter, u8 stack_op_type)
{
	exhalbtc_stack_operation_notify(&gl_bt_coexist, stack_op_type);
}

u8 btdm_adjust_rssi_for_coex(struct rtl_priv *adapter)
{
	u8 adj_rssi_val=0;
	struct btc_coexist *btcoexist = &gl_bt_coexist;
	
	if (btcoexist->adapter != adapter)
		return 0;

	if (gl_bt_coexist.board_info.btdm_ant_num == 1){
		adj_rssi_val = btcoexist->bt_info.rssi_adjust_for_agc_table_on+
				btcoexist->bt_info.rssi_adjust_for_1ant_coex_type;
	}else if(gl_bt_coexist.board_info.btdm_ant_num == 2){
		adj_rssi_val = btcoexist->bt_info.rssi_adjust_for_agc_table_on;
	}
	return adj_rssi_val;
}

void btdm_init_hal_vars(struct rtl_priv *adapter)
{
	u8 ant_num = 2;
	struct rtl_priv *rtlpriv = adapter;
	
	ant_num = rtl_get_hwpg_ant_num(rtlpriv);
	RT_TRACE(COMP_INIT, DBG_DMESG, ("%s, antNum is %d\n", __func__, ant_num));
	
	btdm_set_bt_exist(rtlpriv);
	btdm_set_bt_chip_type(rtlpriv);
	btdm_set_bt_coex_ant_num(rtlpriv, BT_COEX_ANT_TYPE_PG, ant_num);
	
}

void btdm_set_bt_exist(struct rtl_priv *adapter)
{
	u8 bt_exist;
	struct rtl_priv *rtlpriv = adapter;
	
	bt_exist = rtl_get_hwpg_bt_exist(rtlpriv);
	RT_TRACE(COMP_INIT, DBG_DMESG, ("%s, bt_exist is %d\n", __func__, bt_exist));
	exhalbtc_set_bt_exist(bt_exist);
}

void btdm_set_bt_chip_type(struct rtl_priv *adapter)
{
	u8 bt_type;
	struct rtl_priv *rtlpriv = adapter;

	bt_type = rtl_get_hwpg_bt_type(rtlpriv);
	RT_TRACE(COMP_INIT, DBG_DMESG, ("%s, bt_type is %d\n", __func__, bt_type));
	exhalbtc_set_chip_type(bt_type);
}

void btdm_set_bt_coex_ant_num(struct rtl_priv *adapter, u8 type, u8 ant_num)
{	
	exhalbtc_set_ant_num(type, ant_num);
}

void btdm_init_hw_config(struct rtl_priv *adapter)
{
	exhalbtc_init_hw_config(&gl_bt_coexist);
	exhalbtc_init_coex_dm(&gl_bt_coexist);
}

void btdm_ips_notify(struct rtl_priv *adapter, u8 type)
{	
	exhalbtc_ips_notify(&gl_bt_coexist, type);
}

void btdm_lps_notify(struct rtl_priv *adapter,	u8 type)
{
	exhalbtc_lps_notify(&gl_bt_coexist, type);
}

void btdm_scan_notify(struct rtl_priv *adapter, u8 scan_type)
{
	exhalbtc_scan_notify(&gl_bt_coexist, scan_type);
}

void btdm_connect_notify(struct rtl_priv *adapter, u8 action)
{
	exhalbtc_connect_notify(&gl_bt_coexist, action);
}

void btdm_media_status_notify(struct rtl_priv *adapter, enum rt_media_status mstatus)
{
	exhalbtc_mediastatus_notify(&gl_bt_coexist, mstatus);
}

void btdm_special_packet_notify(struct rtl_priv *adapter,	u8 pkt_type)
{
	exhalbtc_special_packet_notify(&gl_bt_coexist, pkt_type);
}

void btdm_bt_info_notify(struct rtl_priv *adapter, u8 *tmp_buf, u8 length)
{
	exhalbtc_bt_info_notify(&gl_bt_coexist, tmp_buf, length);
}

bool btdm_is_bt_disabled(struct rtl_priv *adapter)
{
	if (gl_bt_coexist.bt_info.bt_disabled)
		return true;
	else
		return false;
}

void btdm_ap_status_watchdog(struct rtl_priv *adapter)
{
}

void btdm_reject_ap_aggregated_packet(struct rtl_priv *adapter, bool reject)
{
}

void btdm_set_manual_control(bool manual)
{
	gl_bt_coexist.manual_control = manual;
}

void btdm_signal_compensation(struct rtl_priv *adapter, u8 *rssi_wifi, u8 *rssi_bt)
{
	exhalbtc_signal_compensation(&gl_bt_coexist,rssi_wifi,rssi_bt);
}

void btdm_lps_leave(struct rtl_priv *adapter)
{
	exhalbtc_lps_leave(&gl_bt_coexist);
}

void btdm_low_wifi_traffic_notify(struct rtl_priv *adapter)
{
	exhalbtc_low_wifi_traffic_notify(&gl_bt_coexist);
}
#endif
