//===========================================
// The following is for 8723B 2Ant BT Co-exist definition
//===========================================
#define	BT_AUTO_REPORT_ONLY_8723B_2ANT				1


#define	BT_INFO_8723B_2ANT_B_FTP						BIT7
#define	BT_INFO_8723B_2ANT_B_A2DP					BIT6
#define	BT_INFO_8723B_2ANT_B_HID						BIT5
#define	BT_INFO_8723B_2ANT_B_SCO_BUSY				BIT4
#define	BT_INFO_8723B_2ANT_B_ACL_BUSY				BIT3
#define	BT_INFO_8723B_2ANT_B_INQ_PAGE				BIT2
#define	BT_INFO_8723B_2ANT_B_SCO_ESCO				BIT1
#define	BT_INFO_8723B_2ANT_B_CONNECTION				BIT0

#define		BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT		2

typedef enum _BT_INFO_SRC_8723B_2ANT{
	BT_INFO_SRC_8723B_2ANT_WIFI_FW			= 0x0,
	BT_INFO_SRC_8723B_2ANT_BT_RSP				= 0x1,
	BT_INFO_SRC_8723B_2ANT_BT_ACTIVE_SEND		= 0x2,
	BT_INFO_SRC_8723B_2ANT_MAX
}BT_INFO_SRC_8723B_2ANT,*PBT_INFO_SRC_8723B_2ANT;

typedef enum _BT_8723B_2ANT_BT_STATUS{
	BT_8723B_2ANT_BT_STATUS_NON_CONNECTED_IDLE	= 0x0,
	BT_8723B_2ANT_BT_STATUS_CONNECTED_IDLE		= 0x1,
	BT_8723B_2ANT_BT_STATUS_INQ_PAGE				= 0x2,
	BT_8723B_2ANT_BT_STATUS_ACL_BUSY				= 0x3,
	BT_8723B_2ANT_BT_STATUS_SCO_BUSY				= 0x4,
	BT_8723B_2ANT_BT_STATUS_ACL_SCO_BUSY			= 0x5,
	BT_8723B_2ANT_BT_STATUS_MAX
}BT_8723B_2ANT_BT_STATUS,*PBT_8723B_2ANT_BT_STATUS;

typedef enum _BT_8723B_2ANT_COEX_ALGO{
	BT_8723B_2ANT_COEX_ALGO_UNDEFINED			= 0x0,
	BT_8723B_2ANT_COEX_ALGO_SCO				= 0x1,
	BT_8723B_2ANT_COEX_ALGO_HID				= 0x2,
	BT_8723B_2ANT_COEX_ALGO_A2DP				= 0x3,
	BT_8723B_2ANT_COEX_ALGO_A2DP_PANHS		= 0x4,
	BT_8723B_2ANT_COEX_ALGO_PANEDR			= 0x5,
	BT_8723B_2ANT_COEX_ALGO_PANHS			= 0x6,
	BT_8723B_2ANT_COEX_ALGO_PANEDR_A2DP		= 0x7,
	BT_8723B_2ANT_COEX_ALGO_PANEDR_HID		= 0x8,
	BT_8723B_2ANT_COEX_ALGO_HID_A2DP_PANEDR	= 0x9,
	BT_8723B_2ANT_COEX_ALGO_HID_A2DP			= 0xa,
	BT_8723B_2ANT_COEX_ALGO_MAX				= 0xb,
}BT_8723B_2ANT_COEX_ALGO,*PBT_8723B_2ANT_COEX_ALGO;

typedef struct _COEX_DM_8723B_2ANT{
	// fw mechanism
	bool		bPreDecBtPwr;
	bool		bCurDecBtPwr;
	u8		preFwDacSwingLvl;
	u8		curFwDacSwingLvl;
	bool		bCurIgnoreWlanAct;
	bool		bPreIgnoreWlanAct;
	u8		prePsTdma;
	u8		curPsTdma;
	u8		psTdmaPara[5];
	u8		psTdmaDuAdjType;
	bool		bResetTdmaAdjust;
	bool		bAutoTdmaAdjust;
	bool		bPrePsTdmaOn;
	bool		bCurPsTdmaOn;
	bool		bPreBtAutoReport;
	bool		bCurBtAutoReport;

	// sw mechanism
	bool		bPreRfRxLpfShrink;
	bool		bCurRfRxLpfShrink;
	u32		btRf0x1eBackup;
	bool 	bPreLowPenaltyRa;
	bool		bCurLowPenaltyRa;
	bool		bPreDacSwingOn;
	u32		preDacSwingLvl;
	bool		bCurDacSwingOn;
	u32		curDacSwingLvl;
	bool		bPreAdcBackOff;
	bool		bCurAdcBackOff;
	bool 	bPreAgcTableEn;
	bool		bCurAgcTableEn;
	u32		preVal0x6c0;
	u32		curVal0x6c0;
	u32		preVal0x6c4;
	u32		curVal0x6c4;
	u32		preVal0x6c8;
	u32		curVal0x6c8;
	u8		preVal0x6cc;
	u8		curVal0x6cc;
	bool		limited_dig;

	// algorithm related
	u8		preAlgorithm;
	u8		curAlgorithm;
	u8		btStatus;
	u8		wifiChnlInfo[3];

	bool		bNeedRecover0x948;
	u16		backup0x948;
} COEX_DM_8723B_2ANT, *PCOEX_DM_8723B_2ANT;

typedef struct _COEX_STA_8723B_2ANT{	
	bool					bBtLinkExist;
	bool					bScoExist;
	bool					bA2dpExist;
	bool					bHidExist;
	bool					bPanExist;

	bool					bUnderLps;
	bool					bUnderIps;
	u32					highPriorityTx;
	u32					highPriorityRx;
	u32					lowPriorityTx;
	u32					lowPriorityRx;
	u8					btRssi;
	u8					preBtRssiState;
	u8					preWifiRssiState[4];
	bool					bC2hBtInfoReqSent;
	u8					btInfoC2h[BT_INFO_SRC_8723B_2ANT_MAX][10];
	u32					btInfoC2hCnt[BT_INFO_SRC_8723B_2ANT_MAX];
	bool					bC2hBtInquiryPage;
	u8					btRetryCnt;
	u8					btInfoExt;
}COEX_STA_8723B_2ANT, *PCOEX_STA_8723B_2ANT;

//===========================================
// The following is interface which will notify coex module.
//===========================================
void
ex_halbtc8723b2ant_init_hwconfig(
	IN	struct btc_coexist *		pBtCoexist
	);
void
ex_halbtc8723b2ant_init_coex_dm(
	IN	struct btc_coexist *		pBtCoexist
	);
void
ex_halbtc8723b2ant_ips_notify(
	IN	struct btc_coexist *		pBtCoexist,
	IN	u8			type
	);
void
ex_halbtc8723b2ant_lps_notify(
	IN	struct btc_coexist *		pBtCoexist,
	IN	u8			type
	);
void
ex_halbtc8723b2ant_scan_notify(
	IN	struct btc_coexist *		pBtCoexist,
	IN	u8			type
	);
void
ex_halbtc8723b2ant_connect_notify(
	IN	struct btc_coexist *		pBtCoexist,
	IN	u8			type
	);
void
ex_halbtc8723b2ant_media_status_notify(
	IN	struct btc_coexist *			pBtCoexist,
	IN	u8				type
	);
void
ex_halbtc8723b2ant_special_packet_notify(
	IN	struct btc_coexist *			pBtCoexist,
	IN	u8				type
	);
void
ex_halbtc8723b2ant_bt_info_notify(
	IN	struct btc_coexist *		pBtCoexist,
	IN	u8 *			tmpBuf,
	IN	u8			length
	);
void
ex_halbtc8723b2ant_stack_operation_notify(
	IN	struct btc_coexist *			pBtCoexist,
	IN	u8				type
	);
void
ex_halbtc8723b2ant_halt_notify(
	IN	struct btc_coexist *			pBtCoexist
	);
void
ex_halbtc8723b2ant_periodical(
	IN	struct btc_coexist *			pBtCoexist
	);
void
ex_halbtc8723b2ant_display_coex_info(
	IN	struct btc_coexist *		pBtCoexist
	);

