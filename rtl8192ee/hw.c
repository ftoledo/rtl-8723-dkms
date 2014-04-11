/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "../wifi.h"
#include "../efuse.h"
#include "../base.h"
#include "../regd.h"
#include "../cam.h"
#include "../ps.h"
#include "../pci.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "dm.h"
#include "fw.h"
#include "led.h"
#include "hw.h"
#include "pwrseqcmd.h"
#include "pwrseq.h"

#define LLT_CONFIG	5

static void _rtl92ee_set_bcn_ctrl_reg(struct ieee80211_hw *hw,
				      u8 set_bits, u8 clear_bits)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpci->reg_bcn_ctrl_val |= set_bits;
	rtlpci->reg_bcn_ctrl_val &= ~clear_bits;

	rtl_write_byte(rtlpriv, REG_BCN_CTRL, (u8) rtlpci->reg_bcn_ctrl_val);
}

static void _rtl92ee_stop_tx_beacon(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp1byte;

	tmp1byte = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2);
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2, tmp1byte & (~BIT(6)));
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1, 0x64);
	tmp1byte = rtl_read_byte(rtlpriv, REG_TBTT_PROHIBIT + 2);
	tmp1byte &= ~(BIT(0));
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 2, tmp1byte);
}

static void _rtl92ee_resume_tx_beacon(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp1byte;

	tmp1byte = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2);
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2, tmp1byte | BIT(6));
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1, 0xff);
	tmp1byte = rtl_read_byte(rtlpriv, REG_TBTT_PROHIBIT + 2);
	tmp1byte |= BIT(0);
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 2, tmp1byte);
}

static void _rtl92ee_enable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl92ee_set_bcn_ctrl_reg(hw, 0, BIT(1));
}

static void _rtl92ee_return_beacon_queue_skb(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring = &rtlpci->tx_ring[BEACON_QUEUE];

	while (skb_queue_len(&ring->queue)) {
		struct rtl_tx_desc *entry = &ring->desc[ring->idx];
		struct sk_buff *skb = __skb_dequeue(&ring->queue);

		pci_unmap_single(rtlpci->pdev,
				 le32_to_cpu(rtlpriv->cfg->ops->get_desc(
				 (u8 *) entry, true, HW_DESC_TXBUFF_ADDR)),
				 skb->len, PCI_DMA_TODEVICE);
		kfree_skb(skb);
		ring->idx = (ring->idx + 1) % ring->entries;
	}

}
static void _rtl92ee_disable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl92ee_set_bcn_ctrl_reg(hw, BIT(1), 0);
}

static void _rtl92ee_set_fw_clock_on(struct ieee80211_hw *hw,
	u8 rpwm_val, bool b_need_turn_off_ckk)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool b_support_remote_wake_up;
	u32 count = 0,isr_regaddr,content;
	bool b_schedule_timer = b_need_turn_off_ckk;
	rtlpriv->cfg->ops->get_hw_reg(hw, HAL_DEF_WOWLAN,
					(u8 *) (&b_support_remote_wake_up));

	if (!rtlhal->bfw_ready)
		return;
	if (!rtlpriv->psc.b_fw_current_inpsmode)
		return;

	while (1) {
		spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
		if (rtlhal->bfw_clk_change_in_progress) {
			while (rtlhal->bfw_clk_change_in_progress) {
				spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
				count++;
				udelay(100);
				if (count > 1000)
					return;
				spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
			}
			spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
		} else {
			rtlhal->bfw_clk_change_in_progress = false;
			spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
		}
	}

	if (IS_IN_LOW_POWER_STATE_92E(rtlhal->fw_ps_state)) {
		rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_SET_RPWM,
					(u8 *) (&rpwm_val));
		if (FW_PS_IS_ACK(rpwm_val)) {
			isr_regaddr = REG_HISR;
			content = rtl_read_dword(rtlpriv, isr_regaddr);
			while (!(content & IMR_CPWM) && (count < 500)) {
				udelay(50);
				count++;
				content = rtl_read_dword(rtlpriv, isr_regaddr);
			}

			if (content & IMR_CPWM) {
			rtl_write_word(rtlpriv,isr_regaddr, 0x0100);
			rtlhal->fw_ps_state = FW_PS_STATE_RF_ON_92E;
			RT_TRACE(COMP_POWER, DBG_LOUD, ("Receive CPWM INT!!! Set pHalData->FwPSState = %X\n", rtlhal->fw_ps_state));
			}
		}

		spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
		rtlhal->bfw_clk_change_in_progress = false;
		spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
		if (b_schedule_timer) {
			mod_timer(&rtlpriv->works.fw_clockoff_timer,
				  jiffies + MSECS(10));
		}

	} else  {
		spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
		rtlhal->bfw_clk_change_in_progress = false;
		spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
	}


}

static void _rtl92ee_set_fw_clock_off(struct ieee80211_hw *hw,
	u8 rpwm_val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring;
	enum rf_pwrstate rtstate;
	bool b_schedule_timer = false;
	u8 queue;

	if (!rtlhal->bfw_ready)
		return;
	if (!rtlpriv->psc.b_fw_current_inpsmode)
		return;
	if (!rtlhal->ballow_sw_to_change_hwclc)
		return;
	rtlpriv->cfg->ops->get_hw_reg(hw,HW_VAR_RF_STATE,(u8 *)(&rtstate));
	if (rtstate == ERFOFF ||rtlpriv->psc.inactive_pwrstate ==ERFOFF)
		return;

	for (queue = 0; queue < RTL_PCI_MAX_TX_QUEUE_COUNT; queue++) {
		ring = &rtlpci->tx_ring[queue];
		if (skb_queue_len(&ring->queue)) {
			b_schedule_timer = true;
			break;
		}
	}

	if (b_schedule_timer) {
		mod_timer(&rtlpriv->works.fw_clockoff_timer,
					  jiffies + MSECS(10));
		return;
	}

	if (FW_PS_STATE(rtlhal->fw_ps_state) != FW_PS_STATE_RF_OFF_LOW_PWR_92E) {
		spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
		if (!rtlhal->bfw_clk_change_in_progress) {
			rtlhal->bfw_clk_change_in_progress = true;
			spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
			rtlhal->fw_ps_state = FW_PS_STATE(rpwm_val);
			rtl_write_word(rtlpriv, REG_HISR, 0x0100);
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SET_RPWM,
				(u8 *) (&rpwm_val));
			spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
			rtlhal->bfw_clk_change_in_progress = false;
			spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
		} else {
			spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
			mod_timer(&rtlpriv->works.fw_clockoff_timer,
					  jiffies + MSECS(10));
		}
	}

}

static void _rtl92ee_set_fw_ps_rf_on(struct ieee80211_hw *hw)
{
	u8 rpwm_val = 0;
	rpwm_val |= (FW_PS_STATE_RF_OFF_92E | FW_PS_ACK);
	_rtl92ee_set_fw_clock_on(hw, rpwm_val, true);
}

static void _rtl92ee_set_fw_ps_rf_off_low_power(struct ieee80211_hw *hw)
{
	u8 rpwm_val = 0;
	rpwm_val |= FW_PS_STATE_RF_OFF_LOW_PWR_92E;
	_rtl92ee_set_fw_clock_off(hw, rpwm_val);
}
void rtl92ee_fw_clk_off_timer_callback(unsigned long data)
{
	struct ieee80211_hw *hw = (struct ieee80211_hw *)data;

	_rtl92ee_set_fw_ps_rf_off_low_power(hw);
}

static void _rtl92ee_fwlps_leave(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool b_fw_current_inps = false;
	u8 rpwm_val = 0,fw_pwrmode = FW_PS_ACTIVE_MODE;

	if (ppsc->b_low_power_enable){
		rpwm_val = (FW_PS_STATE_ALL_ON_92E|FW_PS_ACK);/* RF on */
		_rtl92ee_set_fw_clock_on(hw, rpwm_val, false);
		rtlhal->ballow_sw_to_change_hwclc = false;
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
				(u8 *) (&fw_pwrmode));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
				(u8 *) (&b_fw_current_inps));
	} else {
		rpwm_val = FW_PS_STATE_ALL_ON_92E;	/* RF on */
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SET_RPWM,
				(u8 *) (&rpwm_val));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
				(u8 *) (&fw_pwrmode));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
				(u8 *) (&b_fw_current_inps));
	}

}

static void _rtl92ee_fwlps_enter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool b_fw_current_inps = true;
	u8 rpwm_val;

	if (ppsc->b_low_power_enable){
		rpwm_val = FW_PS_STATE_RF_OFF_LOW_PWR_92E;	/* RF off */
		rtlpriv->cfg->ops->set_hw_reg(hw,
				HW_VAR_FW_PSMODE_STATUS,
				(u8 *) (&b_fw_current_inps));
		rtlpriv->cfg->ops->set_hw_reg(hw,
				HW_VAR_H2C_FW_PWRMODE,
				(u8 *) (&ppsc->fwctrl_psmode));
		rtlhal->ballow_sw_to_change_hwclc = true;
		_rtl92ee_set_fw_clock_off(hw, rpwm_val);


	} else {
		rpwm_val = FW_PS_STATE_RF_OFF_92E;	/* RF off */
		rtlpriv->cfg->ops->set_hw_reg(hw,
				HW_VAR_FW_PSMODE_STATUS,
				(u8 *) (&b_fw_current_inps));
		rtlpriv->cfg->ops->set_hw_reg(hw,
				HW_VAR_H2C_FW_PWRMODE,
				(u8 *) (&ppsc->fwctrl_psmode));
		rtlpriv->cfg->ops->set_hw_reg(hw,
				HW_VAR_SET_RPWM,
				(u8 *) (&rpwm_val));
	}

}

void rtl92ee_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	switch (variable) {
	case HW_VAR_RCR:
		*((u32 *) (val)) = rtlpci->receive_config;
		break;
	case HW_VAR_RF_STATE:
		*((enum rf_pwrstate *)(val)) = ppsc->rfpwr_state;
		break;
	case HW_VAR_FWLPS_RF_ON:{
			enum rf_pwrstate rfState;
			u32 val_rcr;

			rtlpriv->cfg->ops->get_hw_reg(hw,
						      HW_VAR_RF_STATE,
						      (u8 *) (&rfState));
			if (rfState == ERFOFF) {
				*((bool *) (val)) = true;
			} else {
				val_rcr = rtl_read_dword(rtlpriv, REG_RCR);
				val_rcr &= 0x00070000;
				if (val_rcr)
					*((bool *) (val)) = false;
				else
					*((bool *) (val)) = true;
			}
			break;
		}
	case HW_VAR_FW_PSMODE_STATUS:
		*((bool *) (val)) = ppsc->b_fw_current_inpsmode;
		break;
	case HW_VAR_CORRECT_TSF:{
			u64 tsf;
			u32 *ptsf_low = (u32 *) & tsf;
			u32 *ptsf_high = ((u32 *) & tsf) + 1;

			*ptsf_high = rtl_read_dword(rtlpriv, (REG_TSFTR + 4));
			*ptsf_low = rtl_read_dword(rtlpriv, REG_TSFTR);

			*((u64 *) (val)) = tsf;

			break;
		}
	default:
		RT_TRACE(COMP_ERR, DBG_EMERG,
			 ("switch case not process %x\n",variable));
		break;
	}
}

void _rtl92ee_download_rsvd_page(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp_regcr, tmp_reg422;
	u8 bcnvalid_reg, txbc_reg;
	u8 count = 0, dlbcn_count = 0;
	bool b_recover = false;

	/*Set REG_CR bit 8. DMA beacon by SW.*/
	tmp_regcr = rtl_read_byte(rtlpriv, REG_CR + 1);
	rtl_write_byte(rtlpriv, REG_CR + 1,
			   tmp_regcr | BIT(0));

	/* Disable Hw protection for a time which revserd for Hw sending beacon.
	  * Fix download reserved page packet fail that access collision with the protection time.
	  * 2010.05.11. Added by tynli. */
	_rtl92ee_set_bcn_ctrl_reg(hw, 0, BIT(3));
	_rtl92ee_set_bcn_ctrl_reg(hw, BIT(4), 0);

	/* Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame.*/
	tmp_reg422 = rtl_read_byte(rtlpriv,
			  			REG_FWHW_TXQ_CTRL + 2);
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2,
			   tmp_reg422 & (~BIT(6)));
	
	if (tmp_reg422 & BIT(6))
		b_recover = true;
	
	do {
		/* Clear beacon valid check bit */
		bcnvalid_reg = rtl_read_byte(rtlpriv, REG_DWBCN0_CTRL + 2);
		rtl_write_byte(rtlpriv, REG_DWBCN0_CTRL + 2, bcnvalid_reg | BIT(0));
		
		/* Return Beacon TCB */
		_rtl92ee_return_beacon_queue_skb(hw);
		
		/* download rsvd page */
		rtl92ee_set_fw_rsvdpagepkt(hw, false);
		
		txbc_reg = rtl_read_byte(rtlpriv, REG_MGQ_TXBD_NUM + 3);
		count = 0;
		while ((txbc_reg & BIT(4)) && count < 20) {
			count++;
			udelay(10);
			txbc_reg = rtl_read_byte(rtlpriv, REG_MGQ_TXBD_NUM + 3);
		}
		rtl_write_byte(rtlpriv, REG_MGQ_TXBD_NUM + 3, txbc_reg | BIT(4));
		
		/* check rsvd page download OK. */
		bcnvalid_reg = rtl_read_byte(rtlpriv, REG_DWBCN0_CTRL + 2);
		count = 0;
		while (!(bcnvalid_reg & BIT(0)) && count < 20) {
			count++;
			udelay(50);
			bcnvalid_reg = rtl_read_byte(rtlpriv, REG_DWBCN0_CTRL + 2);
		}

		if (bcnvalid_reg & BIT(0))
			rtl_write_byte(rtlpriv, REG_DWBCN0_CTRL + 2, BIT(0));
		
		dlbcn_count++;
	} while (!(bcnvalid_reg & BIT(0)) && dlbcn_count < 5);
	
	if(!(bcnvalid_reg & BIT(0)))
		RT_TRACE(COMP_INIT, DBG_LOUD, ("HalDownloadRSVDPage(): 1 Download RSVD page failed!\n"));

	/* Enable Bcn */
	_rtl92ee_set_bcn_ctrl_reg(hw, BIT(3), 0);
	_rtl92ee_set_bcn_ctrl_reg(hw, 0, BIT(4));

	/* To make sure that if there exists an adapter which would like to send beacon.
	  * If exists, the origianl value of 0x422[6] will be 1, we should check this to
	  * prevent from setting 0x422[6] to 0 after download reserved page, or it will cause 
	  * the beacon cannot be sent by HW.
	  * 2010.06.23. Added by tynli. */
	if (b_recover)
		rtl_write_byte(rtlpriv,
				   REG_FWHW_TXQ_CTRL + 2, tmp_reg422);

	rtl_write_byte(rtlpriv, REG_CR + 1,
				tmp_regcr & (~BIT(0)));

}

void rtl92ee_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	u8 idx;

	switch (variable) {
	case HW_VAR_ETHER_ADDR:{
			for (idx = 0; idx < ETH_ALEN; idx++) {
				rtl_write_byte(rtlpriv, (REG_MACID + idx),
					       val[idx]);
			}
			break;
		}
	case HW_VAR_BASIC_RATE:{
			u16 b_rate_cfg = ((u16 *) val)[0];
			b_rate_cfg = b_rate_cfg & 0x15f;
			b_rate_cfg |= 0x01;
			b_rate_cfg = (b_rate_cfg | 0xd) & (~BIT(1));
			rtl_write_byte(rtlpriv, REG_RRSR, b_rate_cfg & 0xff);
			rtl_write_byte(rtlpriv, REG_RRSR + 1,
				       (b_rate_cfg >> 8) & 0xff);
			break;
		}
	case HW_VAR_BSSID:{
			for (idx = 0; idx < ETH_ALEN; idx++) {
				rtl_write_byte(rtlpriv, (REG_BSSID + idx),
					       val[idx]);
			}
			break;
		}
	case HW_VAR_SIFS:{
			rtl_write_byte(rtlpriv, REG_SIFS_CTX + 1, val[0]);
			rtl_write_byte(rtlpriv, REG_SIFS_TRX + 1, val[1]);

			rtl_write_byte(rtlpriv, REG_SPEC_SIFS + 1, val[0]);
			rtl_write_byte(rtlpriv, REG_MAC_SPEC_SIFS + 1, val[0]);

			if (!mac->ht_enable)
				rtl_write_word(rtlpriv, REG_RESP_SIFS_OFDM,
					       0x0e0e);
			else
				rtl_write_word(rtlpriv, REG_RESP_SIFS_OFDM,
					       *((u16 *) val));
			break;
		}
	case HW_VAR_SLOT_TIME:{
			u8 e_aci;

			RT_TRACE(COMP_MLME, DBG_LOUD,
				 ("HW_VAR_SLOT_TIME %x\n", val[0]));

			rtl_write_byte(rtlpriv, REG_SLOT, val[0]);

			for (e_aci = 0; e_aci < AC_MAX; e_aci++) {
				rtlpriv->cfg->ops->set_hw_reg(hw,
							      HW_VAR_AC_PARAM,
							      (u8 *) (&e_aci));
			}
			break;
		}
	case HW_VAR_ACK_PREAMBLE:{
			u8 reg_tmp;
			u8 short_preamble = (bool) (*(u8 *) val);
			reg_tmp = (rtlpriv->mac80211.cur_40_prime_sc) << 5;
			if (short_preamble)
				reg_tmp |= 0x80;
			rtl_write_byte(rtlpriv, REG_RRSR + 2, reg_tmp);
			rtlpriv->mac80211.short_preamble = short_preamble;
			break;
		}
	case HW_VAR_WPA_CONFIG:
		rtl_write_byte(rtlpriv, REG_SECCFG, *((u8 *) val));
		break;
	/*case HW_VAR_AMPDU_MIN_SPACE:{
			u8 min_spacing_to_set;
			u8 sec_min_space;

			min_spacing_to_set = *((u8 *) val);
			if (min_spacing_to_set <= 7) {
				sec_min_space = 0;

				if (min_spacing_to_set < sec_min_space)
					min_spacing_to_set = sec_min_space;

				mac->min_space_cfg = ((mac->min_space_cfg &
						       0xf8) |
						      min_spacing_to_set);

				*val = min_spacing_to_set;

				RT_TRACE(COMP_MLME, DBG_LOUD,
					 ("Set HW_VAR_AMPDU_MIN_SPACE: %#x\n",
					  mac->min_space_cfg));

				rtl_write_byte(rtlpriv, REG_AMPDU_MIN_SPACE,
					       mac->min_space_cfg);
			}
			break;
		}
	case HW_VAR_SHORTGI_DENSITY:{
			u8 density_to_set;

			density_to_set = *((u8 *) val);
			mac->min_space_cfg |= (density_to_set << 3);

			RT_TRACE(COMP_MLME, DBG_LOUD,
				 ("Set HW_VAR_SHORTGI_DENSITY: %#x\n",
				  mac->min_space_cfg));

			rtl_write_byte(rtlpriv, REG_AMPDU_MIN_SPACE,
				       mac->min_space_cfg);
			
			break;
		}*/
	case HW_VAR_AMPDU_FACTOR:{
			u8 regtoset_normal[4] = { 0x41, 0xa8, 0x72, 0xb9 };
			u8 factor_toset;
			u8 *p_regtoset = NULL;
			u8 index = 0;

			p_regtoset = regtoset_normal;

			factor_toset = *((u8 *) val);
			if (factor_toset <= 3) {
				factor_toset = (1 << (factor_toset + 2));
				if (factor_toset > 0xf)
					factor_toset = 0xf;

				for (index = 0; index < 4; index++) {
					if ((p_regtoset[index] & 0xf0) >
					    (factor_toset << 4))
						p_regtoset[index] =
						    (p_regtoset[index] & 0x0f) |
						    (factor_toset << 4);

					if ((p_regtoset[index] & 0x0f) >
					    factor_toset)
						p_regtoset[index] =
						    (p_regtoset[index] & 0xf0) |
						    (factor_toset);

					rtl_write_byte(rtlpriv,
						       (REG_AGGLEN_LMT + index),
						       p_regtoset[index]);

				}

				RT_TRACE(COMP_MLME, DBG_LOUD,
					 ("Set HW_VAR_AMPDU_FACTOR: %#x\n",
					  factor_toset));
			}
			break;
		}
	case HW_VAR_AC_PARAM:{
			u8 e_aci = *((u8 *) val);

			if (rtlpci->acm_method != eAcmWay2_SW)
				rtlpriv->cfg->ops->set_hw_reg(hw,
							      HW_VAR_ACM_CTRL,
							      (u8 *) (&e_aci));
			break;
		}
	case HW_VAR_ACM_CTRL:{
			u8 e_aci = *((u8 *) val);
			union aci_aifsn *p_aci_aifsn =
			    (union aci_aifsn *)(&(mac->ac[0].aifs));
			u8 acm = p_aci_aifsn->f.acm;
			u8 acm_ctrl = rtl_read_byte(rtlpriv, REG_ACMHWCTRL);

			acm_ctrl =
			    acm_ctrl | ((rtlpci->acm_method == 2) ? 0x0 : 0x1);

			if (acm) {
				switch (e_aci) {
				case AC0_BE:
					acm_ctrl |= AcmHw_BeqEn;
					break;
				case AC2_VI:
					acm_ctrl |= AcmHw_ViqEn;
					break;
				case AC3_VO:
					acm_ctrl |= AcmHw_VoqEn;
					break;
				default:
					RT_TRACE(COMP_ERR, DBG_WARNING,
						 ("HW_VAR_ACM_CTRL acm set "
						  "failed: eACI is %d\n", acm));
					break;
				}
			} else {
				switch (e_aci) {
				case AC0_BE:
					acm_ctrl &= (~AcmHw_BeqEn);
					break;
				case AC2_VI:
					acm_ctrl &= (~AcmHw_ViqEn);
					break;
				case AC3_VO:
					acm_ctrl &= (~AcmHw_BeqEn);
					break;
				default:
					RT_TRACE(COMP_ERR, DBG_EMERG,
						 ("switch case not process \n"));
					break;
				}
			}

			RT_TRACE(COMP_QOS, DBG_TRACE,
				 ("SetHwReg8190pci(): [HW_VAR_ACM_CTRL] "
				  "Write 0x%X\n", acm_ctrl));
			rtl_write_byte(rtlpriv, REG_ACMHWCTRL, acm_ctrl);
			break;
		}
	case HW_VAR_RCR:{
			rtl_write_dword(rtlpriv, REG_RCR, ((u32 *) (val))[0]);
			rtlpci->receive_config = ((u32 *) (val))[0];
			break;
		}
	case HW_VAR_RETRY_LIMIT:{
			u8 retry_limit = ((u8 *) (val))[0];

			rtl_write_word(rtlpriv, REG_RETRY_LIMIT,
				       retry_limit << RETRY_LIMIT_SHORT_SHIFT |
				       retry_limit << RETRY_LIMIT_LONG_SHIFT);
			break;
		}
	case HW_VAR_DUAL_TSF_RST:
		rtl_write_byte(rtlpriv, REG_DUAL_TSF_RST, (BIT(0) | BIT(1)));
		break;
	case HW_VAR_EFUSE_BYTES:
		rtlefuse->efuse_usedbytes = *((u16 *) val);
		break;
	case HW_VAR_EFUSE_USAGE:
		rtlefuse->efuse_usedpercentage = *((u8 *) val);
		break;
	case HW_VAR_IO_CMD:
		rtl92ee_phy_set_io_cmd(hw, (*(enum io_type *)val));
		break;
	case HW_VAR_SET_RPWM:{
			u8 rpwm_val;

			rpwm_val = rtl_read_byte(rtlpriv, REG_PCIE_HRPWM);
			udelay(1);

			if (rpwm_val & BIT(7)) {
				rtl_write_byte(rtlpriv, REG_PCIE_HRPWM,
					       (*(u8 *) val));
			} else {
				rtl_write_byte(rtlpriv, REG_PCIE_HRPWM,
					       ((*(u8 *) val) | BIT(7)));
			}

			break;
		}
	case HW_VAR_H2C_FW_PWRMODE:{
			rtl92ee_set_fw_pwrmode_cmd(hw, (*(u8 *) val));
			break;
		}
	case HW_VAR_FW_PSMODE_STATUS:
		ppsc->b_fw_current_inpsmode = *((bool *) val);
		break;

	case HW_VAR_RESUME_CLK_ON:
		_rtl92ee_set_fw_ps_rf_on(hw);
		break;

	case HW_VAR_FW_LPS_ACTION:{
		bool b_enter_fwlps = *((bool *) val);

		if (b_enter_fwlps)
			_rtl92ee_fwlps_enter(hw);
		 else
			_rtl92ee_fwlps_leave(hw);

		 break;
		}

	case HW_VAR_H2C_FW_JOINBSSRPT:{
			u8 mstatus = (*(u8 *) val);

			if (mstatus == RT_MEDIA_CONNECT) {
				rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AID, 0);
				_rtl92ee_download_rsvd_page(hw);
			}
			rtl92ee_set_fw_joinbss_report_cmd(hw, mstatus);
			break;
		}
	case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:{
		rtl92ee_set_p2p_ps_offload_cmd(hw,(*(u8 *) val));
		break;
	}

	case HW_VAR_AID:{
			u16 u2btmp;
			u2btmp = rtl_read_word(rtlpriv, REG_BCN_PSR_RPT);
			u2btmp &= 0xC000;
			rtl_write_word(rtlpriv, REG_BCN_PSR_RPT, (u2btmp |
						mac->assoc_id));

			break;
		}
	case HW_VAR_CORRECT_TSF:{
			u8 btype_ibss = ((u8 *) (val))[0];

			if (btype_ibss == true)
				_rtl92ee_stop_tx_beacon(hw);

			_rtl92ee_set_bcn_ctrl_reg(hw, 0, BIT(3));

			rtl_write_dword(rtlpriv, REG_TSFTR,
					(u32) (mac->tsf & 0xffffffff));
			rtl_write_dword(rtlpriv, REG_TSFTR + 4,
					(u32) ((mac->tsf >> 32) & 0xffffffff));

			_rtl92ee_set_bcn_ctrl_reg(hw, BIT(3), 0);

			if (btype_ibss == true)
				_rtl92ee_resume_tx_beacon(hw);

			break;

		}
	case HW_VAR_KEEP_ALIVE: {
			u8 array[2];
			array[0] = 0xff;
			array[1] = *((u8 *)val);
			rtl92ee_fill_h2c_cmd(hw, H2C_92E_KEEP_ALIVE_CTRL, 2, array);
		}
	default:
		RT_TRACE(COMP_ERR, DBG_EMERG, ("switch case "
							"not process %x\n",variable));
		break;
	}
}
/*
static bool _rtl92ee_llt_write(struct ieee80211_hw *hw, u32 address, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	bool status = true;
	long count = 0;
	u32 value = _LLT_INIT_ADDR(address) |
	    _LLT_INIT_DATA(data) | _LLT_OP(_LLT_WRITE_ACCESS);

	rtl_write_dword(rtlpriv, REG_LLT_INIT, value);

	do {
		value = rtl_read_dword(rtlpriv, REG_LLT_INIT);
		if (_LLT_NO_ACTIVE == _LLT_OP_VALUE(value))
			break;

		if (count > POLLING_LLT_THRESHOLD) {
			RT_TRACE(COMP_ERR, DBG_EMERG,
				 ("Failed to polling write LLT done at "
				  "address %d!\n", address));
			status = false;
			break;
		}
	} while (++count);

	return status;
}
*/
static bool _rtl92ee_llt_table_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 txpktbuf_bndy;
	u8 u8tmp, testcnt = 0;

	txpktbuf_bndy = 0xFA;

	rtl_write_dword(rtlpriv, REG_RQPN, 0x80E90808);

	rtl_write_byte(rtlpriv, REG_TRXFF_BNDY, txpktbuf_bndy);
	rtl_write_word(rtlpriv, REG_TRXFF_BNDY + 2, 0x3d00 - 1);

	rtl_write_byte(rtlpriv, REG_DWBCN0_CTRL + 1, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_DWBCN1_CTRL + 1, txpktbuf_bndy);

	rtl_write_byte(rtlpriv, REG_BCNQ_BDNY, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_BCNQ1_BDNY, txpktbuf_bndy);

	rtl_write_byte(rtlpriv, REG_MGQ_BDNY, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, 0x45D, txpktbuf_bndy);
	
	rtl_write_byte(rtlpriv, REG_PBP, 0x31);
	rtl_write_byte(rtlpriv, REG_RX_DRVINFO_SZ, 0x4);

	u8tmp = rtl_read_byte(rtlpriv, REG_AUTO_LLT + 2);
	rtl_write_byte(rtlpriv, REG_AUTO_LLT + 2, u8tmp | BIT(0));

	while (u8tmp & BIT(0)) {
		u8tmp = rtl_read_byte(rtlpriv, REG_AUTO_LLT + 2);
		udelay(10);
		testcnt ++;
		if (testcnt > 10)
			break;
	}
	
	return true;
}

static void _rtl92ee_gen_refresh_led_state(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_led *pLed0 = &(pcipriv->ledctl.sw_led0);

	if (rtlpriv->rtlhal.up_first_time)
		return;

	if (ppsc->rfoff_reason == RF_CHANGE_BY_IPS)
		rtl92ee_sw_led_on(hw, pLed0);
	else if (ppsc->rfoff_reason == RF_CHANGE_BY_INIT)
		rtl92ee_sw_led_on(hw, pLed0);
	else
		rtl92ee_sw_led_off(hw, pLed0);
}

static bool _rtl92ee_init_mac(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	u8 bytetmp;
	u16 wordtmp;
	u32 dwordtmp;
	u8 queue_idx;

	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x0);
	
	dwordtmp = rtl_read_dword(rtlpriv, REG_SYS_CFG1);
	if (dwordtmp & BIT(24)) {
		rtl_write_byte(rtlpriv, 0x7c, 0xc3);
	} else {
		bytetmp = rtl_read_byte(rtlpriv, 0x16);
		rtl_write_byte(rtlpriv, 0x16, bytetmp | BIT(4) | BIT(6));
		rtl_write_byte(rtlpriv, 0x7c, 0x83);
	}
	/* 1. 40Mhz crystal source*/
	bytetmp = rtl_read_byte(rtlpriv, REG_AFE_CTRL2);
	bytetmp &= 0xfb;
	rtl_write_byte(rtlpriv, REG_AFE_CTRL2, bytetmp);
	
	dwordtmp = rtl_read_dword(rtlpriv, REG_AFE_CTRL4);
	dwordtmp &= 0xfffffc7f;
	rtl_write_dword(rtlpriv, REG_AFE_CTRL4, dwordtmp);

	/* 2. 92E AFE parameter 
	  *MP chip then check version */
	bytetmp = rtl_read_byte(rtlpriv, REG_AFE_CTRL2);
	bytetmp &= 0xbf;
	rtl_write_byte(rtlpriv, REG_AFE_CTRL2, bytetmp);
	
	dwordtmp = rtl_read_dword(rtlpriv, REG_AFE_CTRL4);
	dwordtmp &= 0xffdfffff;
	rtl_write_dword(rtlpriv, REG_AFE_CTRL4, dwordtmp);

	/* HW Power on sequence */
	if (!rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
		PWR_INTF_PCI_MSK, Rtl8192E_NIC_ENABLE_FLOW)) {
		RT_TRACE(COMP_INIT,DBG_LOUD,("init MAC Fail as rtl_hal_pwrseqcmdparsing\n"));
		return false;
	}

	/* Release MAC IO register reset */
	bytetmp = rtl_read_byte(rtlpriv, REG_CR);
	bytetmp = 0xff;
	rtl_write_byte(rtlpriv, REG_CR, bytetmp);
	mdelay(2);
	bytetmp = 0x7f;
	rtl_write_byte(rtlpriv, REG_HWSEQ_CTRL, bytetmp);
	mdelay(2);

	/* Add for wakeup online */
	bytetmp = rtl_read_byte(rtlpriv, REG_SYS_CLKR);
	rtl_write_byte(rtlpriv, REG_SYS_CLKR, bytetmp | BIT(3));
	bytetmp = rtl_read_byte(rtlpriv, REG_GPIO_MUXCFG + 1);
	rtl_write_byte(rtlpriv, REG_GPIO_MUXCFG + 1, bytetmp & (~BIT(4)));
	/* Release MAC IO register reset */
	rtl_write_word(rtlpriv, REG_CR, 0x2ff);
	
	if (!rtlhal->b_mac_func_enable) {
		if (_rtl92ee_llt_table_init(hw) == false) {
			RT_TRACE(COMP_INIT,DBG_LOUD,("LLT table init fail \n"));
			return false;
		}
	}

	rtl_write_dword(rtlpriv, REG_HISR, 0xffffffff);
	rtl_write_dword(rtlpriv, REG_HISRE, 0xffffffff);

	wordtmp = rtl_read_word(rtlpriv, REG_TRXDMA_CTRL);
	wordtmp &= 0xf;
	wordtmp |= 0xF5B1;
	rtl_write_word(rtlpriv, REG_TRXDMA_CTRL, wordtmp);
	/* Reported Tx status from HW for rate adaptive.*/
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 1, 0x1F);
	
	/* Set RCR register */
	rtl_write_dword(rtlpriv, REG_RCR, rtlpci->receive_config);
	rtl_write_word(rtlpriv, REG_RXFLTMAP2, 0xffff);
	
	/* Set TCR register */
	rtl_write_dword(rtlpriv, REG_TCR, rtlpci->transmit_config);
	
	/* Set TX/RX descriptor physical address(from OS API). */
	rtl_write_dword(rtlpriv, REG_BCNQ_DESA,
			((u64) rtlpci->tx_ring[BEACON_QUEUE].dma) &
			DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_MGQ_DESA,
			(u64) rtlpci->tx_ring[MGNT_QUEUE].dma &
			DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_VOQ_DESA,
			(u64) rtlpci->tx_ring[VO_QUEUE].dma & 
			DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_VIQ_DESA,
			(u64) rtlpci->tx_ring[VI_QUEUE].dma & 
			DMA_BIT_MASK(32));
	
	rtl_write_dword(rtlpriv, REG_BEQ_DESA,
			(u64) rtlpci->tx_ring[BE_QUEUE].dma & 
			DMA_BIT_MASK(32));
	
	dwordtmp = rtl_read_dword(rtlpriv, REG_BEQ_DESA);
	
	rtl_write_dword(rtlpriv, REG_BKQ_DESA,
			(u64) rtlpci->tx_ring[BK_QUEUE].dma & 
			DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_HQ0_DESA,
			(u64) rtlpci->tx_ring[HIGH_QUEUE].dma &
			DMA_BIT_MASK(32));
	
	rtl_write_dword(rtlpriv, REG_RX_DESA,
			(u64) rtlpci->rx_ring[RX_MPDU_QUEUE].dma &
			DMA_BIT_MASK(32));

	/* if we want to support 64 bit DMA, we should set it here,
	 * but now we do not support 64 bit DMA*/


	/* porting from InitializeTxVariables()*/
	for (queue_idx = 0; queue_idx < MAX_TX_QUEUE; queue_idx++)
		rtlpriv->mac80211.num_tx_desc[queue_idx] = TX_DESC_NUM_92E;

	rtlpriv->mac80211.num_tx_desc[BEACON_QUEUE] = 2;
	
	for (queue_idx = 0; queue_idx < MAX_TX_QUEUE; queue_idx++) {
		rtlpriv->mac80211.current_tx_read_point[queue_idx] = 0;
		rtlpriv->mac80211.current_tx_write_point[queue_idx] = 0;
		rtlpriv->mac80211.available_desc_to_write[queue_idx] = 
							rtlpriv->mac80211.num_tx_desc[queue_idx];
	}
	rtl_write_dword(rtlpriv, REG_TSFTIMER_HCI, 0x3fffffff);

	bytetmp = rtl_read_byte(rtlpriv, REG_PCIE_CTRL_REG + 3);
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 3, bytetmp | 0xF7);
	
	rtl_write_dword(rtlpriv, REG_INT_MIG, 0);

	rtl_write_dword(rtlpriv, REG_MCUTST_1, 0x0);

	rtl_write_word(rtlpriv, REG_MGQ_TXBD_NUM, 
		TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12 ) & 0x3000));
	rtl_write_word(rtlpriv, REG_VOQ_TXBD_NUM, 
		TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12 ) & 0x3000));
	rtl_write_word(rtlpriv, REG_VIQ_TXBD_NUM, 
		TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12 ) & 0x3000));
	rtl_write_word(rtlpriv, REG_BEQ_TXBD_NUM, 
		TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12 ) & 0x3000));
	rtl_write_word(rtlpriv, REG_VOQ_TXBD_NUM, 
		TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12 ) & 0x3000));
	rtl_write_word(rtlpriv, REG_BKQ_TXBD_NUM, 
		TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12 ) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI0Q_TXBD_NUM, 
		TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12 ) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI1Q_TXBD_NUM, 
		TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12 ) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI2Q_TXBD_NUM, 
		TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12 ) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI3Q_TXBD_NUM, 
		TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12 ) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI4Q_TXBD_NUM, 
		TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12 ) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI5Q_TXBD_NUM, 
		TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12 ) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI6Q_TXBD_NUM, 
		TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12 ) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI7Q_TXBD_NUM, 
		TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12 ) & 0x3000));		
	/*Rx*/
	rtl_write_word(rtlpriv, REG_RX_RXBD_NUM, 
		RX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 13 ) & 0x6000) | 0x8000);
	
	rtl_write_dword(rtlpriv, REG_TSFTIMER_HCI, 0XFFFFFFFF);

	_rtl92ee_gen_refresh_led_state(hw);
	return true;;
}

static void _rtl92ee_hw_configure(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u32 reg_rrsr;

	reg_rrsr = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
	/* Init value for RRSR. */
	rtl_write_dword(rtlpriv, REG_RRSR, reg_rrsr);
	
	/* ARFB table 9 for 11ac 5G 2SS */
	rtl_write_dword(rtlpriv, REG_ARFR0, 0x00000010);
	rtl_write_dword(rtlpriv, REG_ARFR0 + 4, 0x3e0ff000);
	
	/* ARFB table 10 for 11ac 5G 1SS */
	rtl_write_dword(rtlpriv, REG_ARFR1, 0x00000010);
	rtl_write_dword(rtlpriv, REG_ARFR1 + 4, 0x000ff000);
	
	/* Set SLOT time */
	rtl_write_byte(rtlpriv, REG_SLOT, 0x09);
	
	/* CF-End setting. */
	rtl_write_word(rtlpriv, REG_FWHW_TXQ_CTRL, 0x1F80);
	
	/* Set retry limit */
	rtl_write_word(rtlpriv, REG_RETRY_LIMIT, 0x0707);
	
	/* BAR settings */
	rtl_write_dword(rtlpriv, REG_BAR_MODE_CTRL, 0x0201ffff);
	
	/* Set Data / Response auto rate fallack retry count */
	rtl_write_dword(rtlpriv, REG_DARFRC, 0x01000000);
	rtl_write_dword(rtlpriv, REG_DARFRC + 4, 0x07060504);
	rtl_write_dword(rtlpriv, REG_RARFRC, 0x01000000);
	rtl_write_dword(rtlpriv, REG_RARFRC + 4, 0x07060504);
			
	/* Beacon related, for rate adaptive */
	rtl_write_byte(rtlpriv, REG_ATIMWND, 0x2);
	rtl_write_byte(rtlpriv, REG_BCN_MAX_ERR, 0xff);
		
	rtlpci->reg_bcn_ctrl_val = 0x1d;
	rtl_write_byte(rtlpriv, REG_BCN_CTRL, rtlpci->reg_bcn_ctrl_val);

	/* Marked out by Bruce, 2010-09-09.
	  * This register is configured for the 2nd Beacon (multiple BSSID). 
	  * We shall disable this register if we only support 1 BSSID.
	  *vivi guess 92d also need this, also 92d now doesnot set this reg */
	rtl_write_byte(rtlpriv, REG_BCN_CTRL_1, 0);
	
	/* TBTT prohibit hold time. Suggested by designer TimChen. */
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1,0xff); /* 8 ms */
	
	rtl_write_byte(rtlpriv, REG_PIFS, 0);
	rtl_write_byte(rtlpriv, REG_AGGR_BREAK_TIME, 0x16);
	
	rtl_write_word(rtlpriv, REG_NAV_PROT_LEN, 0x0040);
	rtl_write_word(rtlpriv, REG_PROT_MODE_CTRL, 0x08ff);
	
	/*For Rx TP. Suggested by SD1 Richard. Added by tynli. 2010.04.12.*/
	rtl_write_dword(rtlpriv, REG_FAST_EDCA_CTRL, 0x03086666);
	
	/* ACKTO for IOT issue. */
	rtl_write_byte(rtlpriv, REG_ACKTO, 0x40);
	
	/* Set Spec SIFS (used in NAV) */
	rtl_write_word(rtlpriv, REG_SPEC_SIFS, 0x100a);
	rtl_write_word(rtlpriv, REG_MAC_SPEC_SIFS, 0x100a);
	
	/* Set SIFS for CCK */
	rtl_write_word(rtlpriv, REG_SIFS_CTX, 0x100a); 

	/* Set SIFS for OFDM */
	rtl_write_word(rtlpriv, REG_SIFS_TRX, 0x100a);
	
	/*Note Data sheet don't define*/
	rtl_write_word(rtlpriv, 0x4C7, 0x80);
	
	rtl_write_byte(rtlpriv, REG_RX_PKT_LIMIT, 0x20);
	
	rtl_write_word(rtlpriv, REG_MAX_AGGR_NUM, 0x1717);
	
	/* Set Multicast Address. 2009.01.07. by tynli. */
	rtl_write_dword(rtlpriv, REG_MAR, 0xffffffff);
	rtl_write_dword(rtlpriv, REG_MAR + 4, 0xffffffff);
}
/*
static void _rtl92ee_enable_aspm_back_door(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	u8 tmp1byte = 0;
	u32 tmp4Byte = 0, count = 0;

	rtl_write_word(rtlpriv, 0x354, 0x8104);
	rtl_write_word(rtlpriv, 0x358, 0x24);

	rtl_write_word(rtlpriv, 0x350, 0x70c);
	rtl_write_byte(rtlpriv, 0x352, 0x2);
	tmp1byte = rtl_read_byte(rtlpriv, 0x352);
	count = 0;
	while (tmp1byte && count < 20){
		udelay(10);
		tmp1byte = rtl_read_byte(rtlpriv, 0x352);
		count++;
	}
	if (0 == tmp1byte){
		tmp4Byte = rtl_read_dword(rtlpriv, 0x34c);
		rtl_write_dword(rtlpriv, 0x348, tmp4Byte|BIT(31));
		rtl_write_word(rtlpriv, 0x350, 0xf70c);
		rtl_write_byte(rtlpriv, 0x352, 0x1);
	}

	tmp1byte = rtl_read_byte(rtlpriv, 0x352);
	count = 0;
	while (tmp1byte && count < 20){
		udelay(10);
		tmp1byte = rtl_read_byte(rtlpriv, 0x352);
		count++;
	}

	rtl_write_word(rtlpriv, 0x350, 0x718);
	rtl_write_byte(rtlpriv, 0x352, 0x2);
	tmp1byte = rtl_read_byte(rtlpriv, 0x352);
	count = 0;
	while (tmp1byte && count < 20){
		udelay(10);
		tmp1byte = rtl_read_byte(rtlpriv, 0x352);
		count++;
	}

	if (ppsc->b_support_backdoor || (0 == tmp1byte)){
		tmp4Byte = rtl_read_dword(rtlpriv, 0x34c);
		rtl_write_dword(rtlpriv, 0x348, tmp4Byte|BIT(11)|BIT(12));
		rtl_write_word(rtlpriv, 0x350, 0xf718);
		rtl_write_byte(rtlpriv, 0x352, 0x1);
	}

	tmp1byte = rtl_read_byte(rtlpriv, 0x352);
	count = 0;
	while (tmp1byte && count < 20){
		udelay(10);
		tmp1byte = rtl_read_byte(rtlpriv, 0x352);
		count++;
	}
}
*/
void rtl92ee_enable_hw_security_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 sec_reg_value;
	u8 tmp;

	RT_TRACE(COMP_INIT, DBG_DMESG,
		 ("PairwiseEncAlgorithm = %d GroupEncAlgorithm = %d\n",
		  rtlpriv->sec.pairwise_enc_algorithm,
		  rtlpriv->sec.group_enc_algorithm));

	if (rtlpriv->cfg->mod_params->sw_crypto || rtlpriv->sec.use_sw_sec) {
		RT_TRACE(COMP_SEC, DBG_DMESG, ("not open "
							"hw encryption\n"));
		return;
	}

	sec_reg_value = SCR_TxEncEnable | SCR_RxDecEnable;

	if (rtlpriv->sec.use_defaultkey) {
		sec_reg_value |= SCR_TxUseDK;
		sec_reg_value |= SCR_RxUseDK;
	}

	sec_reg_value |= (SCR_RXBCUSEDK | SCR_TXBCUSEDK);

	tmp = rtl_read_byte(rtlpriv, REG_CR + 1);
	rtl_write_byte(rtlpriv, REG_CR + 1, tmp | BIT(1));

	RT_TRACE(COMP_SEC, DBG_DMESG,
		 ("The SECR-value %x \n", sec_reg_value));

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_WPA_CONFIG, &sec_reg_value);

}
void rtl92ee_allow_error_packet(struct ieee80211_hw *hw,bool b_allow_err_pkt)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	if (b_allow_err_pkt)
		rtlpci->receive_config |= (RCR_ACRC32 | RCR_AICV);
	else
		rtlpci->receive_config &= ~(RCR_ACRC32 | RCR_AICV);

	rtl_write_dword(rtlpriv, REG_RCR, rtlpci->receive_config);
}

int rtl92ee_hw_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool rtstatus = true;
	int err = 0;
	u8 tmp_u1b,u1byte;
	u32 tmp_u4b;

	RT_TRACE(COMP_INIT,DBG_LOUD,(" Rtl8192EE hw init\n"));
	rtlpriv->rtlhal.being_init_adapter = true;
	rtlpriv->intf_ops->disable_aspm(hw);

	tmp_u1b = rtl_read_byte(rtlpriv, REG_SYS_CLKR+1);
	u1byte = rtl_read_byte(rtlpriv, REG_CR);
	if ((tmp_u1b & BIT(3)) && (u1byte !=0 && u1byte != 0xEA)){
		rtlhal->b_mac_func_enable = true;
	} else {
		rtlhal->b_mac_func_enable = false;
		rtlhal->fw_ps_state = FW_PS_STATE_ALL_ON_92E;
	}

	rtstatus = _rtl92ee_init_mac(hw);

	rtl_write_byte(rtlpriv, 0x577, 0x03);
	
	/*for Crystal 40 Mhz setting */
	rtl_write_byte(rtlpriv, REG_AFE_CTRL4, 0x2A);
	rtl_write_byte(rtlpriv, REG_AFE_CTRL4 + 1, 0x00);
	rtl_write_byte(rtlpriv, REG_AFE_CTRL2, 0x83);
	
	/*Forced the antenna b to wifi */
	if (rtl_pcipriv(hw)->btcoexist.eeprom_bt_coexist == 1) {
		rtl_write_byte(rtlpriv, 0x64, 0);
		rtl_write_byte(rtlpriv, 0x65, 1);
	}
	if (rtstatus != true) {
		RT_TRACE(COMP_ERR, DBG_EMERG, ("Init MAC failed\n"));
		err = 1;
		return err;
	}
	rtlhal->rx_tag = 0;
	rtl_write_word(rtlpriv, REG_PCIE_CTRL_REG, 0x8000);
	err = rtl92ee_download_fw(hw,false);
	if (err) {
		RT_TRACE(COMP_ERR, DBG_WARNING,
			 ("Failed to download FW. Init HW "
			  "without FW now..\n"));
		err = 1;
		rtlhal->bfw_ready = false;
		return err;
	} else {
		rtlhal->bfw_ready = true;
	}
	/*fw related variable initialize */
	ppsc->b_fw_current_inpsmode = false;
	rtlhal->fw_ps_state = FW_PS_STATE_ALL_ON_92E;
	rtlhal->bfw_clk_change_in_progress = false;
	rtlhal->ballow_sw_to_change_hwclc = false;
	rtlhal->last_hmeboxnum = 0;


	rtl92ee_phy_mac_config(hw);

	rtl92ee_phy_bb_config(hw);

	rtl92ee_phy_rf_config(hw);

	rtlphy->rfreg_chnlval[0] = rtl_get_rfreg(hw, RF90_PATH_A,
						 RF_CHNLBW, RFREG_OFFSET_MASK);
	rtlphy->rfreg_chnlval[1] = rtl_get_rfreg(hw, RF90_PATH_B,
						 RF_CHNLBW, RFREG_OFFSET_MASK);
	rtlphy->backup_rf_0x1a = (u32) rtl_get_rfreg(hw, RF90_PATH_A,
						 RF_RX_G1, RFREG_OFFSET_MASK);
	rtlphy->rfreg_chnlval[0] = (rtlphy->rfreg_chnlval[0] & 0xfffff3ff) |
						 BIT(10) | BIT(11);

	rtl_set_rfreg(hw, RF90_PATH_A, RF_CHNLBW, RFREG_OFFSET_MASK,
						 rtlphy->rfreg_chnlval[0]);
	rtl_set_rfreg(hw, RF90_PATH_B, RF_CHNLBW, RFREG_OFFSET_MASK,
						 rtlphy->rfreg_chnlval[0]);

	/*---- Set CCK and OFDM Block "ON"----*/
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BCCKEN, 0x1);			    
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BOFDMEN, 0x1);

	/*Must set this, otherwise the rx sensitivity will be very pool. Maddest*/
	rtl_set_rfreg(hw, RF90_PATH_A, 0xB1, RFREG_OFFSET_MASK, 0x54418);
	
	/*Set Hardware(MAC default setting.)*/
	_rtl92ee_hw_configure(hw);
	
	rtlhal->b_mac_func_enable = true;
	
	rtl_cam_reset_all_entry(hw);
	rtl92ee_enable_hw_security_config(hw);

	ppsc->rfpwr_state = ERFON;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ETHER_ADDR, mac->mac_addr);
	rtlpriv->intf_ops->enable_aspm(hw);

	rtl92ee_bt_hw_init(hw);

	rtlpriv->rtlhal.being_init_adapter = false;

	if (ppsc->rfpwr_state == ERFON) {
		if (rtlphy->iqk_initialized) {
			rtl92ee_phy_iq_calibrate(hw, true);
		} else {
			rtl92ee_phy_iq_calibrate(hw, false);
			rtlphy->iqk_initialized = true;
		}
	}

	rtlphy->rfpath_rx_enable[0] = true;
	if (rtlphy->rf_type == RF_2T2R)
		rtlphy->rfpath_rx_enable[1] = true;

	efuse_one_byte_read(hw, 0x1FA, &tmp_u1b);
	if (!(tmp_u1b & BIT(0))) {
		rtl_set_rfreg(hw, RF90_PATH_A, 0x15, 0x0F, 0x05);
		RT_TRACE(COMP_INIT, DBG_LOUD, ("PA BIAS path A\n"));
	}

	if ((!(tmp_u1b & BIT(1))) && (rtlphy->rf_type == RF_2T2R)) {
		rtl_set_rfreg(hw, RF90_PATH_B, 0x15, 0x0F, 0x05);
		RT_TRACE(COMP_INIT, DBG_LOUD, ("PA BIAS path B\n"));
	}
	
	rtl_write_byte(rtlpriv, REG_NAV_UPPER, ((30000 + 127) / 128));
	
	/*Fixed LDPC rx hang issue. */
	tmp_u4b= rtl_read_dword(rtlpriv, REG_SYS_SWR_CTRL1);
	rtl_write_byte(rtlpriv, REG_SYS_SWR_CTRL2, 0x75);
	tmp_u4b =  (tmp_u4b & 0xfff00fff) | (0x7E << 12);
	rtl_write_dword(rtlpriv, REG_SYS_SWR_CTRL1, tmp_u4b);

	
	//rtl92ee_dm_init(hw);
	
	RT_TRACE(COMP_INIT,DBG_LOUD,("end of Rtl8192EE hw init %x\n",err));
	return 0;
}

static enum version_8192e _rtl92ee_read_chip_version(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	enum version_8192e version = VERSION_UNKNOWN;
	u32 value32;

	rtlphy->rf_type = RF_2T2R;

	value32 = rtl_read_dword(rtlpriv, REG_SYS_CFG1);
	if (value32 & TRP_VAUX_EN)
		version = (enum version_8192e) VERSION_TEST_CHIP_2T2R_8192E;
	else
		version = (enum version_8192e) VERSION_NORMAL_CHIP_2T2R_8192E;

	RT_TRACE(COMP_INIT, DBG_LOUD,
		 ("Chip RF Type: %s\n", (rtlphy->rf_type == RF_2T2R) ?
		  "RF_2T2R" : "RF_1T1R"));

	return version;
}

static int _rtl92ee_set_media_status(struct ieee80211_hw *hw,
				     enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 bt_msr = rtl_read_byte(rtlpriv, MSR);
	enum led_ctl_mode ledaction = LED_CTL_NO_LINK;
	bt_msr &= 0xfc;

	if (type == NL80211_IFTYPE_UNSPECIFIED ||
	    type == NL80211_IFTYPE_STATION) {
		_rtl92ee_stop_tx_beacon(hw);
		_rtl92ee_enable_bcn_sub_func(hw);
	} else if (type == NL80211_IFTYPE_ADHOC ||
		type == NL80211_IFTYPE_AP ||
		type == NL80211_IFTYPE_MESH_POINT) {
		_rtl92ee_resume_tx_beacon(hw);
		_rtl92ee_disable_bcn_sub_func(hw);
	} else {
		RT_TRACE(COMP_ERR, DBG_WARNING,
			 ("Set HW_VAR_MEDIA_STATUS: "
			  "No such media status(%x).\n", type));
	}

	switch (type) {
	case NL80211_IFTYPE_UNSPECIFIED:
		bt_msr |= MSR_NOLINK;
		ledaction = LED_CTL_LINK;
		RT_TRACE(COMP_INIT, DBG_TRACE,
			 ("Set Network type to NO LINK!\n"));
		break;
	case NL80211_IFTYPE_ADHOC:
		bt_msr |= MSR_ADHOC;
		RT_TRACE(COMP_INIT, DBG_TRACE,
			 ("Set Network type to Ad Hoc!\n"));
		break;
	case NL80211_IFTYPE_STATION:
		bt_msr |= MSR_INFRA;
		ledaction = LED_CTL_LINK;
		RT_TRACE(COMP_INIT, DBG_TRACE,
			 ("Set Network type to STA!\n"));
		break;
	case NL80211_IFTYPE_AP:
		bt_msr |= MSR_AP;
		RT_TRACE(COMP_INIT, DBG_TRACE,
			 ("Set Network type to AP!\n"));
		break;
	case NL80211_IFTYPE_MESH_POINT:
		bt_msr |= MSR_ADHOC;
		RT_TRACE(COMP_INIT, DBG_TRACE,
		 	("Set Network type to Mesh Point!\n"));
		break;
	default:
		RT_TRACE(COMP_ERR, DBG_EMERG,
			 ("Network type %d not support!\n", type));
		return 1;
		break;

	}

	rtl_write_byte(rtlpriv, (MSR), bt_msr);
	rtlpriv->cfg->ops->led_control(hw, ledaction);
	if ((bt_msr & 0xfc) == MSR_AP)
		rtl_write_byte(rtlpriv, REG_BCNTCFG + 1, 0x00);
	else
		rtl_write_byte(rtlpriv, REG_BCNTCFG + 1, 0x66);
	return 0;
}

void rtl92ee_set_check_bssid(struct ieee80211_hw *hw, bool check_bssid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u32 reg_rcr = rtlpci->receive_config;

	if (rtlpriv->psc.rfpwr_state != ERFON)
		return;

	if (check_bssid == true) {
		reg_rcr |= (RCR_CBSSID_DATA | RCR_CBSSID_BCN);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR,
					      (u8 *) (&reg_rcr));
		_rtl92ee_set_bcn_ctrl_reg(hw, 0, BIT(4));
	} else if (check_bssid == false) {
		reg_rcr &= (~(RCR_CBSSID_DATA | RCR_CBSSID_BCN));
		_rtl92ee_set_bcn_ctrl_reg(hw, BIT(4), 0);
		rtlpriv->cfg->ops->set_hw_reg(hw,
			HW_VAR_RCR, (u8 *) (&reg_rcr));
	}

}

int rtl92ee_set_network_type(struct ieee80211_hw *hw, enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (_rtl92ee_set_media_status(hw, type))
		return -EOPNOTSUPP;

	if (rtlpriv->mac80211.link_state == MAC80211_LINKED) {
		if (type != NL80211_IFTYPE_AP &&
			type != NL80211_IFTYPE_MESH_POINT)
			rtl92ee_set_check_bssid(hw, true);
	} else {
		rtl92ee_set_check_bssid(hw, false);
	}

	return 0;
}

/* don't set REG_EDCA_BE_PARAM here because mac80211 will send pkt when scan */
void rtl92ee_set_qos(struct ieee80211_hw *hw, int aci)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	rtl92ee_dm_init_edca_turbo(hw);
	switch (aci) {
	case AC1_BK:
		rtl_write_dword(rtlpriv, REG_EDCA_BK_PARAM, 0xa44f);
		break;
	case AC0_BE:
		/* rtl_write_dword(rtlpriv, REG_EDCA_BE_PARAM, u4b_ac_param); */
		break;
	case AC2_VI:
		rtl_write_dword(rtlpriv, REG_EDCA_VI_PARAM, 0x5e4322);
		break;
	case AC3_VO:
		rtl_write_dword(rtlpriv, REG_EDCA_VO_PARAM, 0x2f3222);
		break;
	default:
		RT_ASSERT(false, ("invalid aci: %d !\n", aci));
		break;
	}
}

void rtl92ee_enable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, REG_HIMR, rtlpci->irq_mask[0] & 0xFFFFFFFF);
	rtl_write_dword(rtlpriv, REG_HIMRE, rtlpci->irq_mask[1] & 0xFFFFFFFF);
	rtlpci->irq_enabled = true;
}

void rtl92ee_disable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, REG_HIMR, IMR_DISABLED);
	rtl_write_dword(rtlpriv, REG_HIMRE, IMR_DISABLED);
	rtlpci->irq_enabled = false;
	synchronize_irq(rtlpci->pdev->irq);
}

static void _rtl92ee_poweroff_adapter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 u1b_tmp;
	rtlhal->b_mac_func_enable = false;
	//rtlpriv->intf_ops->enable_aspm(hw);

	RT_TRACE(COMP_INIT,DBG_LOUD,("POWER OFF adapter \n"));
	
	/* Run LPS WL RFOFF flow */
	rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
						PWR_INTF_PCI_MSK, Rtl8192E_NIC_LPS_ENTER_FLOW);
	/* turn off RF */
	rtl_write_byte(rtlpriv, REG_RF_CTRL, 0x00);
	
	/* ==== Reset digital sequence   ======  */
	if ((rtl_read_byte(rtlpriv, REG_MCUFWDL) & BIT(7)) && rtlhal->bfw_ready)
		rtl92ee_firmware_selfreset(hw);

	/* Reset MCU  */
	u1b_tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, (u1b_tmp & (~BIT(2))));

	/* reset MCU ready status */
	rtl_write_byte(rtlpriv, REG_MCUFWDL, 0x00);

    /* HW card disable configuration. */
    rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
						PWR_INTF_PCI_MSK, Rtl8192E_NIC_DISABLE_FLOW);

	/* Reset MCU IO Wrapper */
	u1b_tmp = rtl_read_byte(rtlpriv, REG_RSV_CTRL + 1);
	rtl_write_byte(rtlpriv, REG_RSV_CTRL + 1, (u1b_tmp & (~BIT(0))));
	u1b_tmp = rtl_read_byte(rtlpriv, REG_RSV_CTRL + 1);
	rtl_write_byte(rtlpriv, REG_RSV_CTRL + 1, (u1b_tmp | BIT(0)));

	/* lock ISO/CLK/Power control register */
	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x0E);
}

void rtl92ee_card_disable(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	enum nl80211_iftype opmode;

	RT_TRACE(COMP_INIT,DBG_LOUD,("RTL8192ee card disable\n"));

	RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);

	mac->link_state = MAC80211_NOLINK;
	opmode = NL80211_IFTYPE_UNSPECIFIED;

	_rtl92ee_set_media_status(hw, opmode);

	if (rtlpriv->rtlhal.driver_is_goingto_unload ||
	    ppsc->rfoff_reason > RF_CHANGE_BY_PS)
		rtlpriv->cfg->ops->led_control(hw, LED_CTL_POWER_OFF);

	_rtl92ee_poweroff_adapter(hw);

	/* after power off we should do iqk again */
	rtlpriv->phy.iqk_initialized = false;
}

void rtl92ee_interrupt_recognized(struct ieee80211_hw *hw,
				  u32 *p_inta, u32 *p_intb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	*p_inta = rtl_read_dword(rtlpriv, ISR) & rtlpci->irq_mask[0];
	rtl_write_dword(rtlpriv, ISR, *p_inta);


	*p_intb = rtl_read_dword(rtlpriv, REG_HISRE) & rtlpci->irq_mask[1];
	rtl_write_dword(rtlpriv, REG_HISRE, *p_intb);

}

void rtl92ee_set_beacon_related_registers(struct ieee80211_hw *hw)
{

	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u16 bcn_interval, atim_window;

	bcn_interval = mac->beacon_interval;
	atim_window = 2;	/*FIX MERGE */
	rtl92ee_disable_interrupt(hw);
	rtl_write_word(rtlpriv, REG_ATIMWND, atim_window);
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	rtl_write_word(rtlpriv, REG_BCNTCFG, 0x660f);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_CCK, 0x18);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_OFDM, 0x18);
	rtl_write_byte(rtlpriv, 0x606, 0x30);
	rtlpci->reg_bcn_ctrl_val |= BIT(3);
	rtl_write_byte(rtlpriv, REG_BCN_CTRL, (u8) rtlpci->reg_bcn_ctrl_val);
	/*rtl92ee_enable_interrupt(hw);*/
}

void rtl92ee_set_beacon_interval(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval = mac->beacon_interval;

	RT_TRACE(COMP_BEACON, DBG_DMESG,
		 ("beacon_interval:%d\n", bcn_interval));
	/*rtl92ee_disable_interrupt(hw);*/
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	/*rtl92ee_enable_interrupt(hw);*/
}

void rtl92ee_update_interrupt_mask(struct ieee80211_hw *hw,
				   u32 add_msr, u32 rm_msr)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	RT_TRACE(COMP_INTR, DBG_LOUD,
		 ("add_msr:%x, rm_msr:%x\n", add_msr, rm_msr));

	if (add_msr)
		rtlpci->irq_mask[0] |= add_msr;
	if (rm_msr)
		rtlpci->irq_mask[0] &= (~rm_msr);
	rtl92ee_disable_interrupt(hw);
	rtl92ee_enable_interrupt(hw);
}

static u8 _rtl92ee_get_chnl_group(u8 chnl)
{
	u8 group = 0;

	if (chnl <= 14) {
		if (1 <= chnl && chnl <= 2 )
			group = 0;
        else if (3 <= chnl && chnl <= 5 )
			group = 1;
        else if (6 <= chnl && chnl <= 8 )
			group = 2;
        else if (9 <= chnl && chnl <= 11)
			group = 3;
        else if (12 <= chnl && chnl <= 14)
			group = 4;
	} else {
		if (36 <= chnl && chnl <= 42)   
			group = 0;
        else if (44 <= chnl && chnl <= 48)   
			group = 1;
        else if (50 <= chnl && chnl <= 58)   
			group = 2;
        else if (60 <= chnl && chnl <= 64)   
			group = 3;
        else if (100 <= chnl && chnl <= 106)  
			group = 4;
        else if (108 <= chnl && chnl <= 114)
			group = 5;
        else if (116 <= chnl && chnl <= 122)
			group = 6;
        else if (124 <= chnl && chnl <= 130)
			group = 7;
        else if (132 <= chnl && chnl <= 138)
			group = 8;
        else if (140 <= chnl && chnl <= 144)
			group = 9;
        else if (149 <= chnl && chnl <= 155)
			group = 10;
        else if (157 <= chnl && chnl <= 161)
			group = 11;
        else if (165 <= chnl && chnl <= 171)
			group = 12;
        else if (173 <= chnl && chnl <= 177)
			group = 13;
	}
	return group;
}
static void _rtl8192ee_read_power_value_fromprom(struct ieee80211_hw *hw,
	struct txpower_info_2g *pwrinfo24g,
	struct txpower_info_5g *pwrinfo5g,
	bool autoload_fail,
	u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 rfPath, eeAddr=EEPROM_TX_PWR_INX, group,TxCount=0;

	RT_TRACE(COMP_INIT, DBG_LOUD, ("hal_ReadPowerValueFromPROM92E(): PROMContent[0x%x]=0x%x\n", (eeAddr+1), hwinfo[eeAddr+1]));
	if (0xFF == hwinfo[eeAddr+1])  /*YJ,add,120316*/
		autoload_fail = true;

	if (autoload_fail) {
		RT_TRACE(COMP_INIT, DBG_LOUD, ("auto load fail : Use Default value!\n"));
		for (rfPath = 0 ; rfPath < MAX_RF_PATH ; rfPath++) {
			//2.4G default value
			for (group = 0 ; group < MAX_CHNL_GROUP_24G; group++) {
				pwrinfo24g->index_cck_base[rfPath][group] =	0x2D;
				pwrinfo24g->index_bw40_base[rfPath][group] = 0x2D;
			}
			for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {
				if (TxCount == 0) {
					pwrinfo24g->bw20_diff[rfPath][0] = 0x02;
					pwrinfo24g->ofdm_diff[rfPath][0] = 0x04;
				} else {
					pwrinfo24g->bw20_diff[rfPath][TxCount] = 0xFE;
					pwrinfo24g->bw40_diff[rfPath][TxCount] = 0xFE;
					pwrinfo24g->cck_diff[rfPath][TxCount] = 0xFE;
					pwrinfo24g->ofdm_diff[rfPath][TxCount] = 0xFE;
				}
			}
			
			/*5G default value*/
			for (group = 0 ; group < MAX_CHNL_GROUP_5G; group++)
				pwrinfo5g->index_bw40_base[rfPath][group] = 0x2A;
			
			for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {
				if (TxCount == 0) {
					pwrinfo5g->ofdm_diff[rfPath][0] = 0x04;
					pwrinfo5g->bw20_diff[rfPath][0] = 0x00;
					pwrinfo5g->bw80_diff[rfPath][0] = 0xFE;
					pwrinfo5g->bw160_diff[rfPath][0] = 0xFE;
				} else {
					pwrinfo5g->ofdm_diff[rfPath][0] = 0xFE;
					pwrinfo5g->bw20_diff[rfPath][0] = 0xFE;
					pwrinfo5g->bw40_diff[rfPath][0] = 0xFE;
					pwrinfo5g->bw80_diff[rfPath][0] = 0xFE;
					pwrinfo5g->bw160_diff[rfPath][0] = 0xFE;
				}
			}
		}
		return;
	}

	rtl_priv(hw)->efuse.b_txpwr_fromeprom = true;

	for (rfPath = 0 ; rfPath < MAX_RF_PATH ; rfPath++) {
		/*2.4G default value*/
		for (group = 0 ; group < MAX_CHNL_GROUP_24G; group++) {
			pwrinfo24g->index_cck_base[rfPath][group] = hwinfo[eeAddr++];
			if (pwrinfo24g->index_cck_base[rfPath][group] == 0xFF)
				pwrinfo24g->index_cck_base[rfPath][group] = 0x2D;

		}
		for (group = 0 ; group < MAX_CHNL_GROUP_24G - 1; group++) {
			pwrinfo24g->index_bw40_base[rfPath][group] = hwinfo[eeAddr++];
			if (pwrinfo24g->index_bw40_base[rfPath][group] == 0xFF)
				pwrinfo24g->index_bw40_base[rfPath][group] = 0x2D;
		}
		for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {
			if (TxCount == 0) {
				pwrinfo24g->bw40_diff[rfPath][TxCount] = 0;
				if (hwinfo[eeAddr] == 0xFF) {
					pwrinfo24g->bw20_diff[rfPath][TxCount] = 0x02;
				} else {
					pwrinfo24g->bw20_diff[rfPath][TxCount] = (hwinfo[eeAddr] & 0xf0) >> 4;
				 	if (pwrinfo24g->bw20_diff[rfPath][TxCount] & BIT(3))	/*bit sign number to 8 bit sign number*/
						pwrinfo24g->bw20_diff[rfPath][TxCount] |= 0xF0;
				}

				if (hwinfo[eeAddr] == 0xFF) {
					pwrinfo24g->ofdm_diff[rfPath][TxCount] = 0x04;
				} else {
					pwrinfo24g->ofdm_diff[rfPath][TxCount] = (hwinfo[eeAddr] & 0x0f);
					if(pwrinfo24g->ofdm_diff[rfPath][TxCount] & BIT(3))		/*bit sign number to 8 bit sign number*/
						pwrinfo24g->ofdm_diff[rfPath][TxCount] |= 0xF0;
				}
				pwrinfo24g->cck_diff[rfPath][TxCount] = 0;
				eeAddr++;
			} else {
				if (hwinfo[eeAddr] == 0xFF) {
					pwrinfo24g->bw40_diff[rfPath][TxCount] = 0xFE;
				} else {
					pwrinfo24g->bw40_diff[rfPath][TxCount] = (hwinfo[eeAddr] & 0xf0) >> 4;
					if (pwrinfo24g->bw40_diff[rfPath][TxCount] & BIT(3))
						pwrinfo24g->bw40_diff[rfPath][TxCount] |= 0xF0;
				}

				if (hwinfo[eeAddr] == 0xFF) {
					pwrinfo24g->bw20_diff[rfPath][TxCount] = 0xFE;
				} else {
					pwrinfo24g->bw20_diff[rfPath][TxCount] = (hwinfo[eeAddr] & 0x0f);
					if (pwrinfo24g->bw20_diff[rfPath][TxCount] & BIT(3))
						pwrinfo24g->bw20_diff[rfPath][TxCount] |= 0xF0;
				}
				eeAddr++;

				if (hwinfo[eeAddr] == 0xFF) {
					pwrinfo24g->ofdm_diff[rfPath][TxCount] = 0xFE;
				} else {
					pwrinfo24g->ofdm_diff[rfPath][TxCount] = (hwinfo[eeAddr] & 0xf0) >> 4;
					if(pwrinfo24g->ofdm_diff[rfPath][TxCount] & BIT(3))
						pwrinfo24g->ofdm_diff[rfPath][TxCount] |= 0xF0;
				}

				if (hwinfo[eeAddr] == 0xFF) {
					pwrinfo24g->cck_diff[rfPath][TxCount] =	0xFE;
				} else {
					pwrinfo24g->cck_diff[rfPath][TxCount] =	(hwinfo[eeAddr] & 0x0f);
					if(pwrinfo24g->cck_diff[rfPath][TxCount] & BIT(3))
						pwrinfo24g->cck_diff[rfPath][TxCount] |= 0xF0;
				}
				eeAddr++;
			}
		}

		/*5G default value*/
		for (group = 0 ; group < MAX_CHNL_GROUP_5G; group++) {
			pwrinfo5g->index_bw40_base[rfPath][group] = hwinfo[eeAddr++];
			if (pwrinfo5g->index_bw40_base[rfPath][group] == 0xFF)
				pwrinfo5g->index_bw40_base[rfPath][group] = 0xFE;
		}

		for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {
			if (TxCount == 0) {
				pwrinfo5g->bw40_diff[rfPath][TxCount] = 0;

				if (hwinfo[eeAddr] == 0xFF) {
					pwrinfo5g->bw20_diff[rfPath][TxCount] = 0;
				} else {
					pwrinfo5g->bw20_diff[rfPath][0] = (hwinfo[eeAddr] & 0xf0) >> 4;
					if(pwrinfo5g->bw20_diff[rfPath][TxCount] & BIT(3))
						pwrinfo5g->bw20_diff[rfPath][TxCount] |= 0xF0;
				}

				if (hwinfo[eeAddr] == 0xFF) {
					pwrinfo5g->ofdm_diff[rfPath][TxCount] = 0x04;
				} else {
					pwrinfo5g->ofdm_diff[rfPath][0] = (hwinfo[eeAddr] & 0x0f);
					if(pwrinfo5g->ofdm_diff[rfPath][TxCount] & BIT(3))
						pwrinfo5g->ofdm_diff[rfPath][TxCount] |= 0xF0;
				}
				eeAddr++;
			} else {
				if (hwinfo[eeAddr] == 0xFF) {
					pwrinfo5g->bw40_diff[rfPath][TxCount] =	0xFE;
				} else {
					pwrinfo5g->bw40_diff[rfPath][TxCount]= (hwinfo[eeAddr] & 0xf0) >> 4;
					if(pwrinfo5g->bw40_diff[rfPath][TxCount] & BIT(3))
					pwrinfo5g->bw40_diff[rfPath][TxCount] |= 0xF0;
				}

				if (hwinfo[eeAddr] == 0xFF) {
					pwrinfo5g->bw20_diff[rfPath][TxCount] =	0xFE;
				} else {
					pwrinfo5g->bw20_diff[rfPath][TxCount] = (hwinfo[eeAddr] & 0x0f);
					if(pwrinfo5g->bw20_diff[rfPath][TxCount] & BIT(3))
					pwrinfo5g->bw20_diff[rfPath][TxCount] |= 0xF0;
				}
				eeAddr++;
			}
		}

		if (hwinfo[eeAddr] == 0xFF) {
			pwrinfo5g->ofdm_diff[rfPath][1] = 0xFE;
			pwrinfo5g->ofdm_diff[rfPath][2] = 0xFE;
		} else {
			pwrinfo5g->ofdm_diff[rfPath][1] = (hwinfo[eeAddr] & 0xf0) >> 4;
			pwrinfo5g->ofdm_diff[rfPath][2] = (hwinfo[eeAddr] & 0x0f);
		}
		eeAddr++;

		if(hwinfo[eeAddr] == 0xFF)
			pwrinfo5g->ofdm_diff[rfPath][3] = 0xFE;
		else
			pwrinfo5g->ofdm_diff[rfPath][3] = (hwinfo[eeAddr] & 0x0f);
		eeAddr++;

		for (TxCount = 1; TxCount < MAX_TX_COUNT; TxCount++) {
			if(pwrinfo5g->ofdm_diff[rfPath][TxCount] == 0xFF)
				pwrinfo5g->ofdm_diff[rfPath][TxCount] =	0xFE;
			else if(pwrinfo5g->ofdm_diff[rfPath][TxCount] & BIT(3))
				pwrinfo5g->ofdm_diff[rfPath][TxCount] |= 0xF0;
		}

		for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {		
			if (hwinfo[eeAddr] == 0xFF) {
				pwrinfo5g->bw80_diff[rfPath][TxCount] =	0xFE;												
			} else {
				pwrinfo5g->bw80_diff[rfPath][TxCount] =	(hwinfo[eeAddr] & 0xf0) >> 4;			
				if(pwrinfo5g->bw80_diff[rfPath][TxCount] & BIT(3))		//4bit sign number to 8 bit sign number
					pwrinfo5g->bw80_diff[rfPath][TxCount] |= 0xF0;			
			}
			
			if (hwinfo[eeAddr] == 0xFF) {
				pwrinfo5g->bw160_diff[rfPath][TxCount] = 0xFE;												
			} else {
				pwrinfo5g->bw160_diff[rfPath][TxCount]=	(hwinfo[eeAddr] & 0x0f);			
				if(pwrinfo5g->bw160_diff[rfPath][TxCount] & BIT(3))		//4bit sign number to 8 bit sign number
					pwrinfo5g->bw160_diff[rfPath][TxCount] |= 0xF0;						
			}	
			eeAddr++;
		}
	}
}
static void _rtl92ee_read_txpower_info_from_hwpg(struct ieee80211_hw *hw,
						 bool autoload_fail,
						 u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct txpower_info_2g pwrinfo24g;
	struct txpower_info_5g pwrinfo5g;
	u8 channel5g[CHANNEL_MAX_NUMBER_5G] = 
				{36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,100,102,104,106,108,110,112,
                 114,116,118,120,122,124,126,128,130,132,134,136,138,140,142,144,149,151,
                 153,155,157,159,161,163,165,167,168,169,171,173,175,177};
	u8 channel5g_80m[CHANNEL_MAX_NUMBER_5G_80M] = {42, 58, 106, 122, 138, 155, 171};
	u8 rf_path, index;
	u8 i;

	_rtl8192ee_read_power_value_fromprom(hw, &pwrinfo24g, &pwrinfo5g, autoload_fail, hwinfo);

	for (rf_path = 0; rf_path < MAX_RF_PATH; rf_path++) {
		for (i = 0; i < 14; i++) {
			index = _rtl92ee_get_chnl_group(i + 1);

			if (i == CHANNEL_MAX_NUMBER_2G - 1) {
				rtlefuse->txpwrlevel_cck[rf_path][i] = pwrinfo24g.index_cck_base[rf_path][5];
				rtlefuse->txpwrlevel_ht40_1s[rf_path][i] = pwrinfo24g.index_bw40_base[rf_path][index];
			} else {
				rtlefuse->txpwrlevel_cck[rf_path][i] = pwrinfo24g.index_cck_base[rf_path][index];
				rtlefuse->txpwrlevel_ht40_1s[rf_path][i] = pwrinfo24g.index_bw40_base[rf_path][index];
			}
		}
		for (i = 0; i < CHANNEL_MAX_NUMBER_5G; i++) {
			index = _rtl92ee_get_chnl_group(channel5g[i]);
			rtlefuse->txpwr_5g_bw40base[rf_path][i] = pwrinfo5g.index_bw40_base[rf_path][index];
		}
		for (i = 0; i < CHANNEL_MAX_NUMBER_5G_80M; i++) {
			u8 upper, lower;
			index = _rtl92ee_get_chnl_group(channel5g_80m[i]);
			upper = pwrinfo5g.index_bw40_base[rf_path][index];
			lower = pwrinfo5g.index_bw40_base[rf_path][index + 1];
				
			rtlefuse->txpwr_5g_bw80base[rf_path][i] = (upper + lower) / 2;
		}
		for (i = 0; i < MAX_TX_COUNT; i++) {
			rtlefuse->txpwr_cckdiff[rf_path][i] = pwrinfo24g.cck_diff[rf_path][i];
			rtlefuse->txpwr_legacyhtdiff[rf_path][i] = pwrinfo24g.ofdm_diff[rf_path][i];
			rtlefuse->txpwr_ht20diff[rf_path][i] = pwrinfo24g.bw20_diff[rf_path][i];
			rtlefuse->txpwr_ht40diff[rf_path][i] = pwrinfo24g.bw40_diff[rf_path][i];
		
			rtlefuse->txpwr_5g_ofdmdiff[rf_path][i] = pwrinfo5g.ofdm_diff[rf_path][i];
			rtlefuse->txpwr_5g_bw20diff[rf_path][i] = pwrinfo5g.bw20_diff[rf_path][i];
			rtlefuse->txpwr_5g_bw40diff[rf_path][i] = pwrinfo5g.bw40_diff[rf_path][i];
			rtlefuse->txpwr_5g_bw80diff[rf_path][i] = pwrinfo5g.bw80_diff[rf_path][i];
		}
	}

	if (!autoload_fail)
		rtlefuse->eeprom_thermalmeter = hwinfo[EEPROM_THERMAL_METER_92E];
	else
		rtlefuse->eeprom_thermalmeter = EEPROM_DEFAULT_THERMALMETER;

	if (rtlefuse->eeprom_thermalmeter == 0xff || autoload_fail) {
		rtlefuse->b_apk_thermalmeterignore = true;
		rtlefuse->eeprom_thermalmeter = EEPROM_DEFAULT_THERMALMETER;
	}

	rtlefuse->thermalmeter[0] = rtlefuse->eeprom_thermalmeter;
	RTPRINT(rtlpriv, FINIT, INIT_TxPower,
		("thermalmeter = 0x%x\n", rtlefuse->eeprom_thermalmeter));

	if (!autoload_fail){
		rtlefuse->eeprom_regulatory = hwinfo[EEPROM_RF_BOARD_OPTION_92E] & 0x07;/*bit0~2*/
		if (hwinfo[EEPROM_RF_BOARD_OPTION_92E] == 0xFF)
			rtlefuse->eeprom_regulatory = 0;
	} else {
		rtlefuse->eeprom_regulatory = 0;
	}
	RTPRINT(rtlpriv, FINIT, INIT_TxPower,
	("eeprom_regulatory = 0x%x\n", rtlefuse->eeprom_regulatory ));
}

static void _rtl92ee_read_adapter_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u16 i, usvalue;
	u8 hwinfo[HWSET_MAX_SIZE];
	u16 eeprom_id;

	if (rtlefuse->epromtype == EEPROM_BOOT_EFUSE) {
		rtl_efuse_shadow_map_update(hw);

		memcpy(hwinfo, &rtlefuse->efuse_map[EFUSE_INIT_MAP][0],
		       HWSET_MAX_SIZE);
	} else if (rtlefuse->epromtype == EEPROM_93C46) {
		RT_TRACE(COMP_ERR, DBG_EMERG,
			 ("RTL819X Not boot from eeprom, check it !!"));
	}

	RT_PRINT_DATA(rtlpriv, COMP_INIT, DBG_DMESG, ("MAP \n"),
		      hwinfo, HWSET_MAX_SIZE);

	eeprom_id = *((u16 *) & hwinfo[0]);
	if (eeprom_id != RTL8192E_EEPROM_ID) {
		RT_TRACE(COMP_ERR, DBG_WARNING,
			 ("EEPROM ID(%#x) is invalid!!\n", eeprom_id));
		rtlefuse->autoload_failflag = true;
	} else {
		RT_TRACE(COMP_INIT, DBG_LOUD, ("Autoload OK\n"));
		rtlefuse->autoload_failflag = false;
	}

	if (rtlefuse->autoload_failflag == true)
		return;
	/*VID DID SVID SDID*/
	rtlefuse->eeprom_vid = *(u16 *) &hwinfo[EEPROM_VID];
	rtlefuse->eeprom_did = *(u16 *) &hwinfo[EEPROM_DID];
	rtlefuse->eeprom_svid = *(u16 *) &hwinfo[EEPROM_SVID];
	rtlefuse->eeprom_smid = *(u16 *) &hwinfo[EEPROM_SMID];
	RT_TRACE(COMP_INIT, DBG_LOUD,
		 ("EEPROMId = 0x%4x\n", eeprom_id));
	RT_TRACE(COMP_INIT, DBG_LOUD,
		 ("EEPROM VID = 0x%4x\n", rtlefuse->eeprom_vid));
	RT_TRACE(COMP_INIT, DBG_LOUD,
		 ("EEPROM DID = 0x%4x\n", rtlefuse->eeprom_did));
	RT_TRACE(COMP_INIT, DBG_LOUD,
		 ("EEPROM SVID = 0x%4x\n", rtlefuse->eeprom_svid));
	RT_TRACE(COMP_INIT, DBG_LOUD,
		 ("EEPROM SMID = 0x%4x\n", rtlefuse->eeprom_smid));
	/*customer ID*/
	rtlefuse->eeprom_oemid = *(u8 *) & hwinfo[EEPROM_CUSTOMER_ID];
	if (rtlefuse->eeprom_oemid == 0xFF)
		rtlefuse->eeprom_oemid = 0;

	RT_TRACE(COMP_INIT, DBG_LOUD,
		 ("EEPROM Customer ID: 0x%2x\n", rtlefuse->eeprom_oemid));
	/*EEPROM version*/
	rtlefuse->eeprom_version = *(u8 *) &hwinfo[EEPROM_VERSION];
	/*mac address*/
	for (i = 0; i < 6; i += 2) {
		usvalue = *(u16 *) &hwinfo[EEPROM_MAC_ADDR + i];
		*((u16 *) (&rtlefuse->dev_addr[i])) = usvalue;
	}

	RT_TRACE(COMP_INIT, DBG_DMESG,
		 ("dev_addr: %pM\n", rtlefuse->dev_addr));
	/*channel plan */
	rtlefuse->eeprom_channelplan = *(u8 *) & hwinfo[EEPROM_CHANNELPLAN];
	/* set channel paln to world wide 13 */
	rtlefuse->channel_plan = COUNTRY_CODE_WORLD_WIDE_13;
	/*tx power*/
	_rtl92ee_read_txpower_info_from_hwpg(hw,
					     rtlefuse->autoload_failflag,
					     hwinfo);
	
	rtl92ee_read_bt_coexist_info_from_hwpg(hw,
					     	 rtlefuse->autoload_failflag,
					     	 hwinfo);

	/*board type*/
	rtlefuse->board_type = (((*(u8 *) & hwinfo[EEPROM_RF_BOARD_OPTION_92E]) & 0xE0 ) >> 5);
	if ((*(u8 *) & hwinfo[EEPROM_RF_BOARD_OPTION_92E]) == 0xFF)
		rtlefuse->board_type = 0;
	
	rtlhal->boad_type = rtlefuse->board_type;
	/*parse xtal*/
	rtlefuse->crystalcap = hwinfo[EEPROM_XTAL_92E];
	if (hwinfo[EEPROM_XTAL_92E] == 0xFF)
		rtlefuse->crystalcap = 0x20;
	
	/*antenna diversity*/
	rtlefuse->antenna_div_type = NO_ANTDIV;
	rtlefuse->antenna_div_cfg = 0;

	if (rtlhal->oem_id == RT_CID_DEFAULT) {
		switch (rtlefuse->eeprom_oemid) {
		case EEPROM_CID_DEFAULT:
			if (rtlefuse->eeprom_did == 0x818B) {
				if ((rtlefuse->eeprom_svid == 0x10EC) &&
				     (rtlefuse->eeprom_smid == 0x001B))
					rtlhal->oem_id = RT_CID_819x_Lenovo;
			} else {
				rtlhal->oem_id = RT_CID_DEFAULT;
			}
			break;
		default:
			rtlhal->oem_id = RT_CID_DEFAULT;
			break;
		}
	}
}

static void _rtl92ee_hal_customized_behavior(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	pcipriv->ledctl.bled_opendrain = true;

	RT_TRACE(COMP_INIT, DBG_DMESG,
		 ("RT Customized ID: 0x%02X\n", rtlhal->oem_id));
}

void rtl92ee_read_eeprom_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tmp_u1b;

	rtlhal->version = _rtl92ee_read_chip_version(hw);
	if (get_rf_type(rtlphy) == RF_1T1R)
		rtlpriv->dm.brfpath_rxenable[0] = true;
	else
		rtlpriv->dm.brfpath_rxenable[0] =
		    rtlpriv->dm.brfpath_rxenable[1] = true;
	RT_TRACE(COMP_INIT, DBG_LOUD, ("VersionID = 0x%4x\n",
						rtlhal->version));
	tmp_u1b = rtl_read_byte(rtlpriv, REG_9346CR);
	if (tmp_u1b & BIT(4)) {
		RT_TRACE(COMP_INIT, DBG_DMESG, ("Boot from EEPROM\n"));
		rtlefuse->epromtype = EEPROM_93C46;
	} else {
		RT_TRACE(COMP_INIT, DBG_DMESG, ("Boot from EFUSE\n"));
		rtlefuse->epromtype = EEPROM_BOOT_EFUSE;
	}
	if (tmp_u1b & BIT(5)) {
		RT_TRACE(COMP_INIT, DBG_LOUD, ("Autoload OK\n"));
		rtlefuse->autoload_failflag = false;
		_rtl92ee_read_adapter_info(hw);
	} else {
		RT_TRACE(COMP_ERR, DBG_EMERG, ("Autoload ERR!!\n"));
	}
	_rtl92ee_hal_customized_behavior(hw);

	rtlphy->rfpath_rx_enable[0] = true;
	if (rtlphy->rf_type == RF_2T2R)
		rtlphy->rfpath_rx_enable[1] = true;
}

static void rtl92ee_update_hal_rate_table(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 ratr_value;
	u8 ratr_index = 0;
	u8 b_nmode = mac->ht_enable;
	u8 mimo_ps = IEEE80211_SMPS_OFF;
	u16 shortgi_rate;
	u32 tmp_ratr_value;
	u8 b_curtxbw_40mhz = mac->bw_40;
	u8 b_curshortgi_40mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40) ?
				1 : 0;
	u8 b_curshortgi_20mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20) ?
				1 : 0;
	enum wireless_mode wirelessmode = mac->mode;

	if (rtlhal->current_bandtype == BAND_ON_5G)
		ratr_value = sta->supp_rates[1] << 4;
	else
		ratr_value = sta->supp_rates[0];
	if (mac->opmode == NL80211_IFTYPE_ADHOC)
		ratr_value = 0xfff;
	ratr_value |= (sta->ht_cap.mcs.rx_mask[1] << 20 |
			sta->ht_cap.mcs.rx_mask[0] << 12);
	switch (wirelessmode) {
	case WIRELESS_MODE_B:
		if (ratr_value & 0x0000000c)
			ratr_value &= 0x0000000d;
		else
			ratr_value &= 0x0000000f;
		break;
	case WIRELESS_MODE_G:
		ratr_value &= 0x00000FF5;
		break;
	case WIRELESS_MODE_N_24G:
	case WIRELESS_MODE_N_5G:
		b_nmode = 1;
		if (mimo_ps == IEEE80211_SMPS_STATIC) {
			ratr_value &= 0x0007F005;
		} else {
			u32 ratr_mask;

			if (get_rf_type(rtlphy) == RF_1T2R ||
			    get_rf_type(rtlphy) == RF_1T1R)
				ratr_mask = 0x000ff005;
			else
				ratr_mask = 0x0f0ff005;

			ratr_value &= ratr_mask;
		}
		break;
	default:
		if (rtlphy->rf_type == RF_1T2R)
			ratr_value &= 0x000ff0ff;
		else
			ratr_value &= 0x0f0ff0ff;

		break;
	}

	if ( (rtlpcipriv->btcoexist.bt_coexistence) &&
	     (rtlpcipriv->btcoexist.bt_coexist_type == BT_CSR_BC4) &&
	     (rtlpcipriv->btcoexist.bt_cur_state) &&
	     (rtlpcipriv->btcoexist.bt_ant_isolation) &&
	     ((rtlpcipriv->btcoexist.bt_service == BT_SCO)||
	     (rtlpcipriv->btcoexist.bt_service == BT_BUSY)) )
		ratr_value &= 0x0fffcfc0;
	else
		ratr_value &= 0x0FFFFFFF;

	if (b_nmode && ((b_curtxbw_40mhz &&
			 b_curshortgi_40mhz) || (!b_curtxbw_40mhz &&
						 b_curshortgi_20mhz))) {

		ratr_value |= 0x10000000;
		tmp_ratr_value = (ratr_value >> 12);

		for (shortgi_rate = 15; shortgi_rate > 0; shortgi_rate--) {
			if ((1 << shortgi_rate) & tmp_ratr_value)
				break;
		}

		shortgi_rate = (shortgi_rate << 12) | (shortgi_rate << 8) |
		    (shortgi_rate << 4) | (shortgi_rate);
	}

	rtl_write_dword(rtlpriv, REG_ARFR0 + ratr_index * 4, ratr_value);

	RT_TRACE(COMP_RATR, DBG_DMESG,
		 ("%x\n", rtl_read_dword(rtlpriv, REG_ARFR0)));
}

static void rtl92ee_update_hal_rate_mask(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta, u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_sta_info * sta_entry = NULL;
	u32 ratr_bitmap;
	u8 ratr_index;
	u8 b_curtxbw_40mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40)
				? 1 : 0;
	u8 b_curshortgi_40mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40) ?
				1 : 0;
	u8 b_curshortgi_20mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20) ?
				1 : 0;
	enum wireless_mode wirelessmode = 0;
	bool b_shortgi = false;
	u8 rate_mask[5];
	u8 macid = 0;
	u8 mimo_ps = IEEE80211_SMPS_OFF;

	sta_entry = (struct rtl_sta_info *) sta->drv_priv;
	wirelessmode = sta_entry->wireless_mode;
	if (mac->opmode == NL80211_IFTYPE_STATION ||
		mac->opmode == NL80211_IFTYPE_MESH_POINT)
		b_curtxbw_40mhz = mac->bw_40;
	else if (mac->opmode == NL80211_IFTYPE_AP ||
		mac->opmode == NL80211_IFTYPE_ADHOC)
		macid = sta->aid + 1;

	if (rtlhal->current_bandtype == BAND_ON_5G)
		ratr_bitmap = sta->supp_rates[1] << 4;
	else
		ratr_bitmap = sta->supp_rates[0];
	if (mac->opmode == NL80211_IFTYPE_ADHOC)
		ratr_bitmap = 0xfff;
	ratr_bitmap |= (sta->ht_cap.mcs.rx_mask[1] << 20 |
			sta->ht_cap.mcs.rx_mask[0] << 12);
	switch (wirelessmode) {
	case WIRELESS_MODE_B:
		ratr_index = RATR_INX_WIRELESS_B;
		if (ratr_bitmap & 0x0000000c)
			ratr_bitmap &= 0x0000000d;
		else
			ratr_bitmap &= 0x0000000f;
		break;
	case WIRELESS_MODE_G:
		ratr_index = RATR_INX_WIRELESS_GB;

		if (rssi_level == 1)
			ratr_bitmap &= 0x00000f00;
		else if (rssi_level == 2)
			ratr_bitmap &= 0x00000ff0;
		else
			ratr_bitmap &= 0x00000ff5;
		break;
	case WIRELESS_MODE_A:
		ratr_index = RATR_INX_WIRELESS_A;
		ratr_bitmap &= 0x00000ff0;
		break;
	case WIRELESS_MODE_N_24G:
	case WIRELESS_MODE_N_5G:
		ratr_index = RATR_INX_WIRELESS_NGB;

		if (mimo_ps == IEEE80211_SMPS_STATIC) {
			if (rssi_level == 1)
				ratr_bitmap &= 0x00070000;
			else if (rssi_level == 2)
				ratr_bitmap &= 0x0007f000;
			else
				ratr_bitmap &= 0x0007f005;
		} else {
			if (rtlphy->rf_type == RF_1T2R ||
			    rtlphy->rf_type == RF_1T1R) {
				if (b_curtxbw_40mhz) {
					if (rssi_level == 1)
						ratr_bitmap &= 0x000f0000;
					else if (rssi_level == 2)
						ratr_bitmap &= 0x000ff000;
					else
						ratr_bitmap &= 0x000ff015;
				} else {
					if (rssi_level == 1)
						ratr_bitmap &= 0x000f0000;
					else if (rssi_level == 2)
						ratr_bitmap &= 0x000ff000;
					else
						ratr_bitmap &= 0x000ff005;
				}
			} else {
				if (b_curtxbw_40mhz) {
					if (rssi_level == 1)
						ratr_bitmap &= 0x0f8f0000;
					else if (rssi_level == 2)
						ratr_bitmap &= 0x0f8ff000;
					else
						ratr_bitmap &= 0x0f8ff015;
				} else {
					if (rssi_level == 1)
						ratr_bitmap &= 0x0f8f0000;
					else if (rssi_level == 2)
						ratr_bitmap &= 0x0f8ff000;
					else
						ratr_bitmap &= 0x0f8ff005;
				}
			}
		}

		if ((b_curtxbw_40mhz && b_curshortgi_40mhz) ||
		    (!b_curtxbw_40mhz && b_curshortgi_20mhz)) {

			if (macid == 0)
				b_shortgi = true;
			else if (macid == 1)
				b_shortgi = false;
		}
		break;
	default:
		ratr_index = RATR_INX_WIRELESS_NGB;

		if (rtlphy->rf_type == RF_1T2R)
			ratr_bitmap &= 0x000ff0ff;
		else
			ratr_bitmap &= 0x0f0ff0ff;
		break;
	}
	sta_entry->ratr_index = ratr_index;

	RT_TRACE(COMP_RATR, DBG_DMESG,
		 ("ratr_bitmap :%x\n", ratr_bitmap));
	*(u32 *) & rate_mask = EF4BYTE((ratr_bitmap & 0x0fffffff) |
				       (ratr_index << 28));
	rate_mask[4] = macid | (b_shortgi ? 0x20 : 0x00) | 0x80;
	RT_TRACE(COMP_RATR, DBG_DMESG, ("Rate_index:%x, "
						 "ratr_val:%x, %x:%x:%x:%x:%x\n",
						 ratr_index, ratr_bitmap,
						 rate_mask[0], rate_mask[1],
						 rate_mask[2], rate_mask[3],
						 rate_mask[4]));
	rtl92ee_fill_h2c_cmd(hw, H2C_92E_RA_MASK, 5, rate_mask);
	_rtl92ee_set_bcn_ctrl_reg(hw, BIT(3), 0);
}

void rtl92ee_update_hal_rate_tbl(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta, u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->dm.b_useramask)
		rtl92ee_update_hal_rate_mask(hw, sta, rssi_level);
	else
		rtl92ee_update_hal_rate_table(hw, sta);
}

void rtl92ee_update_channel_access_setting(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 sifs_timer;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SLOT_TIME,
				      (u8 *) & mac->slot_time);
	if (!mac->ht_enable)
		sifs_timer = 0x0a0a;
	else
		sifs_timer = 0x0e0e;
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SIFS, (u8 *) & sifs_timer);
}

bool rtl92ee_gpio_radio_on_off_checking(struct ieee80211_hw *hw, u8 * valid)
{
	*valid = 1;
	return true;
/*	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	enum rf_pwrstate e_rfpowerstate_toset, cur_rfstate;
	u8 u1tmp;
	bool b_actuallyset = false;

	if (rtlpriv->rtlhal.being_init_adapter)
		return false;

	if (ppsc->b_swrf_processing)
		return false;

	spin_lock(&rtlpriv->locks.rf_ps_lock);
	if (ppsc->rfchange_inprogress) {
		spin_unlock(&rtlpriv->locks.rf_ps_lock);
		return false;
	} else {
		ppsc->rfchange_inprogress = true;
		spin_unlock(&rtlpriv->locks.rf_ps_lock);
	}

	cur_rfstate = ppsc->rfpwr_state;

	u1tmp = rtl_read_dword(rtlpriv, REG_GPIO_PIN_CTRL);
	e_rfpowerstate_toset = (u1tmp & BIT(7)) ? ERFON : ERFOFF;

	*valid = 1;
	return true;
	if ((ppsc->b_hwradiooff == true) && (e_rfpowerstate_toset == ERFON)) {
		RT_TRACE(COMP_RF, DBG_DMESG,
			 ("GPIOChangeRF  - HW Radio ON, RF ON\n"));

		e_rfpowerstate_toset = ERFON;
		ppsc->b_hwradiooff = false;
		b_actuallyset = true;
	} else if ((ppsc->b_hwradiooff == false)
		   && (e_rfpowerstate_toset == ERFOFF)) {
		RT_TRACE(COMP_RF, DBG_DMESG,
			 ("GPIOChangeRF  - HW Radio OFF, RF OFF\n"));

		e_rfpowerstate_toset = ERFOFF;
		ppsc->b_hwradiooff = true;
		b_actuallyset = true;
	}

	if (b_actuallyset) {
		spin_lock(&rtlpriv->locks.rf_ps_lock);
		ppsc->rfchange_inprogress = false;
		spin_unlock(&rtlpriv->locks.rf_ps_lock);
	} else {
		if (ppsc->reg_rfps_level & RT_RF_OFF_LEVL_HALT_NIC)
			RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);

		spin_lock(&rtlpriv->locks.rf_ps_lock);
		ppsc->rfchange_inprogress = false;
		spin_unlock(&rtlpriv->locks.rf_ps_lock);
	}

	*valid = 1;
	return !ppsc->b_hwradiooff;
*/
}

void rtl92ee_set_key(struct ieee80211_hw *hw, u32 key_index,
		     u8 *p_macaddr, bool is_group, u8 enc_algo,
		     bool is_wepkey, bool clear_all)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 *macaddr = p_macaddr;
	u32 entry_id = 0;
	bool is_pairwise = false;

	static u8 cam_const_addr[4][6] = {
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x02},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x03}
	};
	static u8 cam_const_broad[] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	};

	if (clear_all) {
		u8 idx = 0;
		u8 cam_offset = 0;
		u8 clear_number = 5;

		RT_TRACE(COMP_SEC, DBG_DMESG, ("clear_all\n"));

		for (idx = 0; idx < clear_number; idx++) {
			rtl_cam_mark_invalid(hw, cam_offset + idx);
			rtl_cam_empty_entry(hw, cam_offset + idx);

			if (idx < 5) {
				memset(rtlpriv->sec.key_buf[idx], 0,
				       MAX_KEY_LEN);
				rtlpriv->sec.key_len[idx] = 0;
			}
		}

	} else {
		switch (enc_algo) {
		case WEP40_ENCRYPTION:
			enc_algo = CAM_WEP40;
			break;
		case WEP104_ENCRYPTION:
			enc_algo = CAM_WEP104;
			break;
		case TKIP_ENCRYPTION:
			enc_algo = CAM_TKIP;
			break;
		case AESCCMP_ENCRYPTION:
			enc_algo = CAM_AES;
			break;
		default:
			RT_TRACE(COMP_ERR, DBG_EMERG, ("switch case "
								"not process \n"));
			enc_algo = CAM_TKIP;
			break;
		}

		if (is_wepkey || rtlpriv->sec.use_defaultkey) {
			macaddr = cam_const_addr[key_index];
			entry_id = key_index;
		} else {
			if (is_group) {
				macaddr = cam_const_broad;
				entry_id = key_index;
			} else {
				if (mac->opmode == NL80211_IFTYPE_AP ||
					mac->opmode == NL80211_IFTYPE_MESH_POINT) {
					entry_id = rtl_cam_get_free_entry(hw, p_macaddr);
					if (entry_id >=  TOTAL_CAM_ENTRY) {
						RT_TRACE(COMP_SEC, DBG_EMERG,
								("Can not find free hw security cam entry\n"));
						return;
					}
				} else {
					entry_id = CAM_PAIRWISE_KEY_POSITION;
				}

				key_index = PAIRWISE_KEYIDX;
				is_pairwise = true;
			}
		}

		if (rtlpriv->sec.key_len[key_index] == 0) {
			RT_TRACE(COMP_SEC, DBG_DMESG,
				 ("delete one entry, entry_id is %d\n",entry_id));
			if (mac->opmode == NL80211_IFTYPE_AP ||
				mac->opmode == NL80211_IFTYPE_MESH_POINT )
				rtl_cam_del_entry(hw, p_macaddr);
			rtl_cam_delete_one_entry(hw, p_macaddr, entry_id);
		} else {
			RT_TRACE(COMP_SEC, DBG_DMESG, ("add one entry\n"));
			if (is_pairwise) {
				RT_TRACE(COMP_SEC, DBG_DMESG, ("set Pairwiase key\n"));

				rtl_cam_add_one_entry(hw, macaddr, key_index,
						      entry_id, enc_algo,
						      CAM_CONFIG_NO_USEDK,
						      rtlpriv->sec.key_buf[key_index]);
			} else {
				RT_TRACE(COMP_SEC, DBG_DMESG, ("set group key\n"));

				if (mac->opmode == NL80211_IFTYPE_ADHOC) {
					rtl_cam_add_one_entry(hw,
							rtlefuse->dev_addr,
							PAIRWISE_KEYIDX,
							CAM_PAIRWISE_KEY_POSITION,
							enc_algo,
							CAM_CONFIG_NO_USEDK,
							rtlpriv->sec.key_buf
							[entry_id]);
				}

				rtl_cam_add_one_entry(hw, macaddr, key_index,
						entry_id, enc_algo,
						CAM_CONFIG_NO_USEDK,
						rtlpriv->sec.key_buf[entry_id]);
			}

		}
	}
}

void rtl92ee_bt_var_init(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);

	rtlpcipriv->btcoexist.bt_coexistence =
						rtlpcipriv->btcoexist.eeprom_bt_coexist;
	rtlpcipriv->btcoexist.bt_ant_num =
						rtlpcipriv->btcoexist.eeprom_bt_ant_num;
	rtlpcipriv->btcoexist.bt_coexist_type =
						rtlpcipriv->btcoexist.eeprom_bt_type;
/*
	if (rtlpcipriv->btcoexist.b_reg_bt_iso == 2)
		rtlpcipriv->btcoexist.bt_ant_isolation =
				rtlpcipriv->btcoexist.eeprom_bt_ant_isolation;
	else
		rtlpcipriv->btcoexist.bt_ant_isolation =
				rtlpcipriv->btcoexist.b_reg_bt_iso;

	rtlpcipriv->btcoexist.bt_radio_shared_type =
					rtlpcipriv->btcoexist.eeprom_bt_radio_shared;

	if (rtlpcipriv->btcoexist.bt_coexistence) {

		if (rtlpcipriv->btcoexist.b_reg_bt_sco == 1)
			rtlpcipriv->btcoexist.bt_service = BT_OTHER_ACTION;
		else if (rtlpcipriv->btcoexist.b_reg_bt_sco == 2)
			rtlpcipriv->btcoexist.bt_service = BT_SCO;
		else if (rtlpcipriv->btcoexist.b_reg_bt_sco == 4)
			rtlpcipriv->btcoexist.bt_service = BT_BUSY;
		else if (rtlpcipriv->btcoexist.b_reg_bt_sco == 5)
			rtlpcipriv->btcoexist.bt_service = BT_OTHERBUSY;
		else
			rtlpcipriv->btcoexist.bt_service = BT_IDLE;

		rtlpcipriv->btcoexist.bt_edca_ul = 0;
		rtlpcipriv->btcoexist.bt_edca_dl = 0;
		rtlpcipriv->btcoexist.bt_rssi_state = 0xff;
	}*/
}

void rtl92ee_read_bt_coexist_info_from_hwpg(struct ieee80211_hw *hw,
					      bool auto_load_fail, u8 *hwinfo)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	u8 value;

	if (!auto_load_fail) {
		value = hwinfo[EEPROM_RF_BOARD_OPTION_92E];
		if (((value & 0xe0) >> 5) == 0x1)
			rtlpcipriv->btcoexist.eeprom_bt_coexist = 1;
		else
			rtlpcipriv->btcoexist.eeprom_bt_coexist = 0;

		rtlpcipriv->btcoexist.eeprom_bt_type = BT_RTL8192E;
		rtlpcipriv->btcoexist.eeprom_bt_ant_num = ANT_TOTAL_X1;
	} else {
		rtlpcipriv->btcoexist.eeprom_bt_coexist = 1;
		rtlpcipriv->btcoexist.eeprom_bt_type = BT_RTL8192E;
		rtlpcipriv->btcoexist.eeprom_bt_ant_num = ANT_TOTAL_X1;
	}
	/*temporarily disable bt coexist*/
	rtlpcipriv->btcoexist.eeprom_bt_coexist = 0;
	rtl92ee_bt_var_init(hw);
}

void rtl92ee_bt_reg_init(struct ieee80211_hw* hw)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);

	/* 0:Low, 1:High, 2:From Efuse. */
	rtlpcipriv->btcoexist.b_reg_bt_iso = 2;
	/* 0:Idle, 1:None-SCO, 2:SCO, 3:From Counter. */
	rtlpcipriv->btcoexist.b_reg_bt_sco= 3;
	/* 0:Disable BT control A-MPDU, 1:Enable BT control A-MPDU. */
	rtlpcipriv->btcoexist.b_reg_bt_sco= 0;
}

void rtl92ee_bt_hw_init(struct ieee80211_hw* hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->cfg->ops->get_btc_status()){
		rtlpriv->btcoexist.btc_ops->btc_init_hw_config(rtlpriv);
	}
}

void rtl92ee_suspend(struct ieee80211_hw *hw)
{
}

void rtl92ee_resume(struct ieee80211_hw *hw)
{
}

/* Turn on AAP (RCR:bit 0) for promicuous mode. */
void rtl92ee_allow_all_destaddr(struct ieee80211_hw *hw,
	bool allow_all_da, bool write_into_reg)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	if (allow_all_da) {/* Set BIT0 */
		rtlpci->receive_config |= RCR_AAP;
	} else {/* Clear BIT0 */
		rtlpci->receive_config &= ~RCR_AAP;
	}

	if(write_into_reg) {
		rtl_write_dword(rtlpriv, REG_RCR, rtlpci->receive_config);
	}

	RT_TRACE(COMP_TURBO | COMP_INIT, DBG_LOUD,
		("receive_config=0x%08X, write_into_reg=%d\n",
		rtlpci->receive_config, write_into_reg ));
}