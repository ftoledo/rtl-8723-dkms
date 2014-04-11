#include "halbt_precomp.h"


#ifndef	__HALBT_COEXIST_H__
#define __HALBT_COEXIST_H__


#ifdef CONFIG_DEBUG
#define BT_DEBUG
#endif
#ifdef BT_DEBUG

#if 1 /*#ifdef PLATFORM_LINUX*/
#define RTPRINT(a,b,c) printk c
#define RTPRINT_ADDR(dbgtype, dbgflag, printstr, _Ptr)\
{\
	u32 __i;						\
	u8 *ptr = (u8*)_Ptr;	\
	printk printstr;				\
	printk(" ");					\
	for( __i=0; __i<6; __i++ )		\
		printk("%02X%s", ptr[__i], (__i==5)?"":"-");		\
	printk("\n");							\
}
#define RTPRINT_DATA(dbgtype, dbgflag, _TitleString, _HexData, _HexDataLen)\
{\
	u32 __i;									\
	u8 *ptr = (u8*)_HexData;			\
	printk(_TitleString);					\
	for( __i=0; __i<(u32)_HexDataLen; __i++ )	\
	{										\
		printk("%02X%s", ptr[__i], (((__i + 1) % 4) == 0)?"  ":" ");\
		if (((__i + 1) % 16) == 0)	printk("\n");\
	}										\
	printk("\n");							\
}

#define MAX_STR_LEN	64
#define PRINTABLE(_ch)	(_ch>=' ' &&_ch<='~')	/* I want to see ASCII 33 to 126 only. Otherwise, I print '?'. Annie, 2005-11-22.*/
#define RT_PRINT_STR(_Comp, _Level, _TitleString, _Ptr, _Len)					\
{\
/*	if (((_Comp) & GlobalDebugComponents) && (_Level <= GlobalDebugLevel))	*/\
	{									\
		u32 __i;						\
		u8 buffer[MAX_STR_LEN];					\
		u32	length = (_Len<MAX_STR_LEN)? _Len : (MAX_STR_LEN-1) ;	\
		memset(buffer, 0, MAX_STR_LEN);			\
		memcpy(buffer, (u8*)_Ptr, length);		\
		for (__i=0; __i<length; __i++)					\
		{								\
			if (!PRINTABLE(buffer[__i]))	buffer[__i] = '?';	\
		}								\
		buffer[length] = '\0';						\
/*		printk("Rtl819x: ");*/						\
		printk(_TitleString);						\
		printk(": %d, <%s>\n", _Len, buffer);				\
	}\
}
#endif 

#else 


#define RTPRINT_ADDR(...)
#define RTPRINT_DATA(...)
#define RT_PRINT_STR(...)

#endif 


#define BT_COEX_ANT_TYPE_PG 0
#define BT_COEX_ANT_TYPE_ANTDIV 1




void btdm_initlize_variables(struct rtl_priv *adapter);

s32 btdm_get_wifi_rssi(struct rtl_priv *adapter);

bool btdm_is_bt40(struct rtl_priv *adapter);

bool btdm_legacy(struct rtl_priv *adapter);

bool btdm_is_limited_dig(struct rtl_priv *adapter);

bool btdm_is_bt_busy(struct rtl_priv *adapter);

bool btdm_is_wifi_busy(struct rtl_priv *adapter);

bool btdm_is_wifi_uplink(struct rtl_priv *adapter);
/*======================================
  *	The following define the extern function called by 
  *	those no-bt module.
  *======================================*/

/*#if(BT_30_SUPPORT == 1)*/
#if 1 /*#ifdef CONFIG_BT_COEXIST*/

/*#define GET_BT_INFO(padapter)	(&GET_HAL_DATA(padapter)->BtInfo)*/

void btdm_display_bt_coex_info(struct rtl_priv *adapter);

void BTDM_SetBtCoexCurrAntNum(struct rtl_priv *adapter, u8 type, u8 ant_num);

void btdm_init_hal_vars(struct rtl_priv *adapter);
	
void btdm_set_bt_exist(struct rtl_priv *adapter);

void btdm_set_bt_chip_type(struct rtl_priv *adapter);

void btdm_set_bt_coex_ant_num(struct rtl_priv *adapter, u8 type, u8 ant_num);

void btdm_init_hw_config(struct rtl_priv *adapter);

bool btdm_is_disable_edca_turbo(struct rtl_priv *adapter);

int btdm_get_hs_wifi_rssi(struct rtl_priv *adapter);

u8 btdm_adjust_rssi_for_coex(struct rtl_priv *adapter);

void btdm_coexist(struct rtl_priv *adapter);

void btdm_ips_notify(struct rtl_priv *adapter, u8 type);

void btdm_lps_notify(struct rtl_priv *adapter, u8 type);

void btdm_scan_notify(struct rtl_priv *adapter, u8 scan_type);

void btdm_connect_notify(struct rtl_priv *adapter, u8 action);

void btdm_media_status_notify(struct rtl_priv *adapter, enum rt_media_status mstatus);

void btdm_special_packet_notify(struct rtl_priv *adapter, u8 pkt_type);

void btdm_bt_info_notify(struct rtl_priv *adapter, u8 *tmpBuf, u8 length);

void btdm_stack_operation_notify(struct rtl_priv *adapter, u8 stack_op_type);

bool btdm_is_bt_disabled(struct rtl_priv *adapter);

void btdm_ap_status_watchdog(struct rtl_priv *adapter);

void btdm_init_all_workitem(struct rtl_priv *adapter);

void btdm_free_all_workitem(struct rtl_priv *adapter);

void btdm_reject_ap_aggregated_packet(struct rtl_priv *adapter, bool reject);

void btdm_set_manual_control(bool manual);

void btdm_signal_compensation(struct rtl_priv *adapter, u8 *rssi_wifi, u8 *rssi_bt);

void btdm_lps_leave(struct rtl_priv *adapter);

void btdm_low_wifi_traffic_notify(struct rtl_priv *adapter);
#endif
#endif
