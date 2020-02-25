﻿/*!
 * @brief プレーヤー表示に関するあれこれ (整理中)
 * @date 2020/02/25
 * @author Hourier
 * @details
 * ここにこれ以上関数を引っ越してくるのは禁止。何ならここから更に分割していく
 */

#include "display-player.h"
#include "player-personality.h"
#include "term.h"
#include "status-first-page.h"
#include "player-sex.h"
#include "patron.h"
#include "world.h"
#include "quest.h"
#include "core.h" // 暫定。後で消す
#include "player/permanent-resistances.h"
#include "player/temporary-resistances.h"
#include "files.h"
#include "mutation.h"
#include "view-mainwindow.h" // 暫定。後で消す
#include "player-skill.h"
#include "player-effects.h"
#include "realm-song.h"
#include "object-hook.h"
#include "shoot.h"
#include "dungeon-file.h"
#include "objectkind.h"
#include "view/display-util.h"

/*!
 * @brief プレイヤーの種族による免疫フラグを返す
 * @param creature_ptr プレーヤーへの参照ポインタ
 * @param flgs フラグを保管する配列
 * @return なし
 * @todo
 * xtra1.c周りと多重実装になっているのを何とかする
 */
static void player_immunity(player_type *creature_ptr, BIT_FLAGS flgs[TR_FLAG_SIZE])
{
	for (int i = 0; i < TR_FLAG_SIZE; i++)
		flgs[i] = 0L;

	if (PRACE_IS_(creature_ptr, RACE_SPECTRE))
		add_flag(flgs, TR_RES_NETHER);
	if (creature_ptr->mimic_form == MIMIC_VAMPIRE || PRACE_IS_(creature_ptr, RACE_VAMPIRE))
		add_flag(flgs, TR_RES_DARK);
	if (creature_ptr->mimic_form == MIMIC_DEMON_LORD)
		add_flag(flgs, TR_RES_FIRE);
	else if (PRACE_IS_(creature_ptr, RACE_YEEK) && creature_ptr->lev > 19)
		add_flag(flgs, TR_RES_ACID);
}


/*!
 * @brief プレイヤーの一時的魔法効果による免疫フラグを返す
 * @param creature_ptr プレーヤーへの参照ポインタ
 * @param flgs フラグを保管する配列
 * @return なし
 * @todo
 * xtra1.c周りと多重実装になっているのを何とかする
 */
static void tim_player_immunity(player_type *creature_ptr, BIT_FLAGS flgs[TR_FLAG_SIZE])
{
	for (int i = 0; i < TR_FLAG_SIZE; i++)
		flgs[i] = 0L;

	if (creature_ptr->special_defense & DEFENSE_ACID)
		add_flag(flgs, TR_RES_ACID);
	if (creature_ptr->special_defense & DEFENSE_ELEC)
		add_flag(flgs, TR_RES_ELEC);
	if (creature_ptr->special_defense & DEFENSE_FIRE)
		add_flag(flgs, TR_RES_FIRE);
	if (creature_ptr->special_defense & DEFENSE_COLD)
		add_flag(flgs, TR_RES_COLD);
	if (creature_ptr->wraith_form)
		add_flag(flgs, TR_RES_DARK);
}


/*!
 * @brief プレイヤーの装備による免疫フラグを返す
 * @param creature_ptr プレーヤーへの参照ポインタ
 * @param flgs フラグを保管する配列
 * @return なし
 * @todo
 * xtra1.c周りと多重実装になっているのを何とかする
 */
static void known_obj_immunity(player_type *creature_ptr, BIT_FLAGS flgs[TR_FLAG_SIZE])
{
	for (int i = 0; i < TR_FLAG_SIZE; i++)
		flgs[i] = 0L;

	for (int i = INVEN_RARM; i < INVEN_TOTAL; i++)
	{
		u32b o_flgs[TR_FLAG_SIZE];
		object_type *o_ptr;
		o_ptr = &creature_ptr->inventory_list[i];
		if (!o_ptr->k_idx) continue;

		object_flags_known(o_ptr, o_flgs);
		if (have_flag(o_flgs, TR_IM_ACID)) add_flag(flgs, TR_RES_ACID);
		if (have_flag(o_flgs, TR_IM_ELEC)) add_flag(flgs, TR_RES_ELEC);
		if (have_flag(o_flgs, TR_IM_FIRE)) add_flag(flgs, TR_RES_FIRE);
		if (have_flag(o_flgs, TR_IM_COLD)) add_flag(flgs, TR_RES_COLD);
	}
}


/*!
 * @brief プレイヤーの種族による弱点フラグを返す
 * @param creature_ptr プレーヤーへの参照ポインタ
 * @param flgs フラグを保管する配列
 * @return なし
 * @todo
 * xtra1.c周りと多重実装になっているのを何とかする
 */
static void player_vuln_flags(player_type *creature_ptr, BIT_FLAGS flgs[TR_FLAG_SIZE])
{
	for (int i = 0; i < TR_FLAG_SIZE; i++)
		flgs[i] = 0L;

	if ((creature_ptr->muta3 & MUT3_VULN_ELEM) || (creature_ptr->special_defense & KATA_KOUKIJIN))
	{
		add_flag(flgs, TR_RES_ACID);
		add_flag(flgs, TR_RES_ELEC);
		add_flag(flgs, TR_RES_FIRE);
		add_flag(flgs, TR_RES_COLD);
	}

	if (PRACE_IS_(creature_ptr, RACE_ANDROID))
		add_flag(flgs, TR_RES_ELEC);
	if (PRACE_IS_(creature_ptr, RACE_ENT))
		add_flag(flgs, TR_RES_FIRE);
	if (PRACE_IS_(creature_ptr, RACE_VAMPIRE) || PRACE_IS_(creature_ptr, RACE_S_FAIRY) ||
		(creature_ptr->mimic_form == MIMIC_VAMPIRE))
		add_flag(flgs, TR_RES_LITE);
}


/*!
 * @brief プレイヤーの特性フラグ一種を表示するサブルーチン /
 * Helper function, see below
 * @param creature_ptr プレーヤーへの参照ポインタ
 * @param row コンソール表示位置の左上行
 * @param col コンソール表示位置の左上列
 * @param header コンソール上で表示する特性名
 * @param flag1 参照する特性ID
 * @param f プレイヤーの特性情報構造体
 * @param mode 表示オプション
 * @return なし
 */
static void display_flag_aux(player_type *creature_ptr, TERM_LEN row, TERM_LEN col, concptr header, int flag1, all_player_flags *f, u16b mode)
{
	byte header_color = TERM_L_DARK;
	int header_col = col;

	bool vuln = FALSE;
	if (have_flag(f->player_vuln, flag1) &&
		!(have_flag(f->known_obj_imm, flag1) ||
			have_flag(f->player_imm, flag1) ||
			have_flag(f->tim_player_imm, flag1)))
		vuln = TRUE;

	col += strlen(header) + 1;

	/* Weapon flags need only two column */
	int max_i = (mode & DP_WP) ? INVEN_LARM + 1 : INVEN_TOTAL;

	for (int i = INVEN_RARM; i < max_i; i++)
	{
		BIT_FLAGS flgs[TR_FLAG_SIZE];
		object_type *o_ptr;
		o_ptr = &creature_ptr->inventory_list[i];
		object_flags_known(o_ptr, flgs);
		if (!(mode & DP_IMM))
			c_put_str((byte)(vuln ? TERM_RED : TERM_SLATE), ".", row, col);

		if (mode & DP_CURSE)
		{
			if (have_flag(flgs, TR_ADD_L_CURSE) || have_flag(flgs, TR_ADD_H_CURSE))
			{
				c_put_str(TERM_L_DARK, "+", row, col);
				header_color = TERM_WHITE;
			}

			if (o_ptr->curse_flags & (TRC_CURSED | TRC_HEAVY_CURSE))
			{
				c_put_str(TERM_WHITE, "+", row, col);
				header_color = TERM_WHITE;
			}

			if (o_ptr->curse_flags & TRC_PERMA_CURSE)
			{
				c_put_str(TERM_WHITE, "*", row, col);
				header_color = TERM_WHITE;
			}

			col++;
			continue;
		}

		if (flag1 == TR_LITE_1)
		{
			if (HAVE_DARK_FLAG(flgs))
			{
				c_put_str(TERM_L_DARK, "+", row, col);
				header_color = TERM_WHITE;
			}
			else if (HAVE_LITE_FLAG(flgs))
			{
				c_put_str(TERM_WHITE, "+", row, col);
				header_color = TERM_WHITE;
			}

			col++;
			continue;
		}

		if (have_flag(flgs, flag1))
		{
			c_put_str((byte)(vuln ? TERM_L_RED : TERM_WHITE),
				(mode & DP_IMM) ? "*" : "+", row, col);
			header_color = TERM_WHITE;
		}

		col++;
	}

	if (mode & DP_IMM)
	{
		if (header_color != TERM_L_DARK)
		{
			c_put_str(header_color, header, row, header_col);
		}

		return;
	}

	c_put_str((byte)(vuln ? TERM_RED : TERM_SLATE), ".", row, col);
	if (have_flag(f->player_flags, flag1))
	{
		c_put_str((byte)(vuln ? TERM_L_RED : TERM_WHITE), "+", row, col);
		header_color = TERM_WHITE;
	}

	if (have_flag(f->tim_player_flags, flag1))
	{
		c_put_str((byte)(vuln ? TERM_ORANGE : TERM_YELLOW), "#", row, col);
		header_color = TERM_WHITE;
	}

	if (have_flag(f->tim_player_imm, flag1))
	{
		c_put_str(TERM_YELLOW, "*", row, col);
		header_color = TERM_WHITE;
	}

	if (have_flag(f->player_imm, flag1))
	{
		c_put_str(TERM_WHITE, "*", row, col);
		header_color = TERM_WHITE;
	}

	if (vuln) c_put_str(TERM_RED, "v", row, col + 1);
	c_put_str(header_color, header, row, header_col);
}


/*!
 * @brief プレイヤーの特性フラグ一覧表示2a /
 * @param creature_ptr プレーヤーへの参照ポインタ
 * Special display, part 2a
 * @return なし
 */
static void display_player_misc_info(player_type *creature_ptr)
{
#ifdef JP
	put_str("名前  :", 1, 26);
	put_str("性別  :", 3, 1);
	put_str("種族  :", 4, 1);
	put_str("職業  :", 5, 1);
#else
	put_str("Name  :", 1, 26);
	put_str("Sex   :", 3, 1);
	put_str("Race  :", 4, 1);
	put_str("Class :", 5, 1);
#endif

	char	buf[80];
	char	tmp[80];
	strcpy(tmp, ap_ptr->title);
#ifdef JP
	if (ap_ptr->no == 1)
		strcat(tmp, "の");
#else
	strcat(tmp, " ");
#endif
	strcat(tmp, creature_ptr->name);

	c_put_str(TERM_L_BLUE, tmp, 1, 34);
	c_put_str(TERM_L_BLUE, sp_ptr->title, 3, 9);
	c_put_str(TERM_L_BLUE, (creature_ptr->mimic_form ? mimic_info[creature_ptr->mimic_form].title : rp_ptr->title), 4, 9);
	c_put_str(TERM_L_BLUE, cp_ptr->title, 5, 9);

#ifdef JP
	put_str("レベル:", 6, 1);
	put_str("ＨＰ  :", 7, 1);
	put_str("ＭＰ  :", 8, 1);
#else
	put_str("Level :", 6, 1);
	put_str("Hits  :", 7, 1);
	put_str("Mana  :", 8, 1);
#endif

	(void)sprintf(buf, "%d", (int)creature_ptr->lev);
	c_put_str(TERM_L_BLUE, buf, 6, 9);
	(void)sprintf(buf, "%d/%d", (int)creature_ptr->chp, (int)creature_ptr->mhp);
	c_put_str(TERM_L_BLUE, buf, 7, 9);
	(void)sprintf(buf, "%d/%d", (int)creature_ptr->csp, (int)creature_ptr->msp);
	c_put_str(TERM_L_BLUE, buf, 8, 9);
}


/*!
 * @brief プレイヤーの特性フラグ一覧表示2b /
 * Special display, part 2b
 * @param creature_ptr プレーヤーへの参照ポインタ
 * @return なし
 * @details
 * <pre>
 * How to print out the modifications and sustains.
 * Positive mods with no sustain will be light green.
 * Positive mods with a sustain will be dark green.
 * Sustains (with no modification) will be a dark green 's'.
 * Negative mods (from a curse) will be red.
 * Huge mods (>9), like from MICoMorgoth, will be a '*'
 * No mod, no sustain, will be a slate '.'
 * </pre>
 */
static void display_player_stat_info(player_type *creature_ptr)
{
	int stat_col = 22;
	int row = 3;
	c_put_str(TERM_WHITE, _("能力", "Stat"), row, stat_col + 1);
	c_put_str(TERM_BLUE, _("  基本", "  Base"), row, stat_col + 7);
	c_put_str(TERM_L_BLUE, _(" 種 職 性 装 ", "RacClaPerMod"), row, stat_col + 13);
	c_put_str(TERM_L_GREEN, _("合計", "Actual"), row, stat_col + 28);
	c_put_str(TERM_YELLOW, _("現在", "Current"), row, stat_col + 35);

	char buf[80];
	for (int i = 0; i < A_MAX; i++)
	{
		int r_adj;
		if (creature_ptr->mimic_form) r_adj = mimic_info[creature_ptr->mimic_form].r_adj[i];
		else r_adj = rp_ptr->r_adj[i];

		int e_adj = 0;

		if ((creature_ptr->stat_max[i] > 18) && (creature_ptr->stat_top[i] > 18))
			e_adj = (creature_ptr->stat_top[i] - creature_ptr->stat_max[i]) / 10;
		if ((creature_ptr->stat_max[i] <= 18) && (creature_ptr->stat_top[i] <= 18))
			e_adj = creature_ptr->stat_top[i] - creature_ptr->stat_max[i];
		if ((creature_ptr->stat_max[i] <= 18) && (creature_ptr->stat_top[i] > 18))
			e_adj = (creature_ptr->stat_top[i] - 18) / 10 - creature_ptr->stat_max[i] + 18;

		if ((creature_ptr->stat_max[i] > 18) && (creature_ptr->stat_top[i] <= 18))
			e_adj = creature_ptr->stat_top[i] - (creature_ptr->stat_max[i] - 19) / 10 - 19;

		if (PRACE_IS_(creature_ptr, RACE_ENT))
		{
			switch (i)
			{
			case A_STR:
			case A_CON:
				if (creature_ptr->lev > 25) r_adj++;
				if (creature_ptr->lev > 40) r_adj++;
				if (creature_ptr->lev > 45) r_adj++;
				break;
			case A_DEX:
				if (creature_ptr->lev > 25) r_adj--;
				if (creature_ptr->lev > 40) r_adj--;
				if (creature_ptr->lev > 45) r_adj--;
				break;
			}
		}

		e_adj -= r_adj;
		e_adj -= cp_ptr->c_adj[i];
		e_adj -= ap_ptr->a_adj[i];

		if (creature_ptr->stat_cur[i] < creature_ptr->stat_max[i])
			c_put_str(TERM_WHITE, stat_names_reduced[i], row + i + 1, stat_col + 1);
		else
			c_put_str(TERM_WHITE, stat_names[i], row + i + 1, stat_col + 1);

		/* Internal "natural" max value.  Maxes at 18/100 */
		/* This is useful to see if you are maxed out */
		cnv_stat(creature_ptr->stat_max[i], buf);
		if (creature_ptr->stat_max[i] == creature_ptr->stat_max_max[i])
			c_put_str(TERM_WHITE, "!", row + i + 1, _(stat_col + 6, stat_col + 4));

		c_put_str(TERM_BLUE, buf, row + i + 1, stat_col + 13 - strlen(buf));

		(void)sprintf(buf, "%3d", r_adj);
		c_put_str(TERM_L_BLUE, buf, row + i + 1, stat_col + 13);
		(void)sprintf(buf, "%3d", (int)cp_ptr->c_adj[i]);
		c_put_str(TERM_L_BLUE, buf, row + i + 1, stat_col + 16);
		(void)sprintf(buf, "%3d", (int)ap_ptr->a_adj[i]);
		c_put_str(TERM_L_BLUE, buf, row + i + 1, stat_col + 19);
		(void)sprintf(buf, "%3d", (int)e_adj);
		c_put_str(TERM_L_BLUE, buf, row + i + 1, stat_col + 22);

		cnv_stat(creature_ptr->stat_top[i], buf);
		c_put_str(TERM_L_GREEN, buf, row + i + 1, stat_col + 26);

		if (creature_ptr->stat_use[i] < creature_ptr->stat_top[i])
		{
			cnv_stat(creature_ptr->stat_use[i], buf);
			c_put_str(TERM_YELLOW, buf, row + i + 1, stat_col + 33);
		}
	}

	int col = stat_col + 41;
	c_put_str(TERM_WHITE, "abcdefghijkl@", row, col);
	c_put_str(TERM_L_GREEN, _("能力修正", "Modification"), row - 1, col);

	BIT_FLAGS flgs[TR_FLAG_SIZE];
	for (int i = INVEN_RARM; i < INVEN_TOTAL; i++)
	{
		object_type *o_ptr;
		o_ptr = &creature_ptr->inventory_list[i];
		object_flags_known(o_ptr, flgs);
		for (int stat = 0; stat < A_MAX; stat++)
		{
			byte a = TERM_SLATE;
			char c = '.';
			if (have_flag(flgs, stat))
			{
				c = '*';

				if (o_ptr->pval > 0)
				{
					a = TERM_L_GREEN;
					if (o_ptr->pval < 10) c = '0' + o_ptr->pval;
				}

				if (have_flag(flgs, stat + TR_SUST_STR))
				{
					a = TERM_GREEN;
				}

				if (o_ptr->pval < 0)
				{
					a = TERM_RED;
					if (o_ptr->pval > -10) c = '0' - o_ptr->pval;
				}
			}
			else if (have_flag(flgs, stat + TR_SUST_STR))
			{
				a = TERM_GREEN;
				c = 's';
			}

			Term_putch(col, row + stat + 1, a, c);
		}

		col++;
	}

	player_flags(creature_ptr, flgs);
	for (int stat = 0; stat < A_MAX; stat++)
	{
		byte a = TERM_SLATE;
		char c = '.';

		if (creature_ptr->muta3 || creature_ptr->tsuyoshi)
		{
			int dummy = 0;

			if (stat == A_STR)
			{
				if (creature_ptr->muta3 & MUT3_HYPER_STR) dummy += 4;
				if (creature_ptr->muta3 & MUT3_PUNY) dummy -= 4;
				if (creature_ptr->tsuyoshi) dummy += 4;
			}
			else if (stat == A_WIS || stat == A_INT)
			{
				if (creature_ptr->muta3 & MUT3_HYPER_INT) dummy += 4;
				if (creature_ptr->muta3 & MUT3_MORONIC) dummy -= 4;
			}
			else if (stat == A_DEX)
			{
				if (creature_ptr->muta3 & MUT3_IRON_SKIN) dummy -= 1;
				if (creature_ptr->muta3 & MUT3_LIMBER) dummy += 3;
				if (creature_ptr->muta3 & MUT3_ARTHRITIS) dummy -= 3;
			}
			else if (stat == A_CON)
			{
				if (creature_ptr->muta3 & MUT3_RESILIENT) dummy += 4;
				if (creature_ptr->muta3 & MUT3_XTRA_FAT) dummy += 2;
				if (creature_ptr->muta3 & MUT3_ALBINO) dummy -= 4;
				if (creature_ptr->muta3 & MUT3_FLESH_ROT) dummy -= 2;
				if (creature_ptr->tsuyoshi) dummy += 4;
			}
			else if (stat == A_CHR)
			{
				if (creature_ptr->muta3 & MUT3_SILLY_VOI) dummy -= 4;
				if (creature_ptr->muta3 & MUT3_BLANK_FAC) dummy -= 1;
				if (creature_ptr->muta3 & MUT3_FLESH_ROT) dummy -= 1;
				if (creature_ptr->muta3 & MUT3_SCALES) dummy -= 1;
				if (creature_ptr->muta3 & MUT3_WART_SKIN) dummy -= 2;
				if (creature_ptr->muta3 & MUT3_ILL_NORM) dummy = 0;
			}

			if (dummy != 0)
			{
				c = '*';
				if (dummy > 0)
				{
					/* Good */
					a = TERM_L_GREEN;

					/* Label boost */
					if (dummy < 10) c = '0' + dummy;
				}

				if (dummy < 0)
				{
					a = TERM_RED;
					if (dummy > -10) c = '0' - dummy;
				}
			}
		}

		if (have_flag(flgs, stat + TR_SUST_STR))
		{
			a = TERM_GREEN;
			c = 's';
		}

		Term_putch(col, row + stat + 1, a, c);
	}
}


/*!
 * @brief プレイヤーの特性フラグ一覧表示1
 * @param creature_ptr プレーヤーへの参照ポインタ
 * Special display, part 1
 * @return なし
 */
static void display_player_flag_info(player_type *creature_ptr)
{
	all_player_flags f;
	player_flags(creature_ptr, f.player_flags);
	tim_player_flags(creature_ptr, f.tim_player_flags);
	player_immunity(creature_ptr, f.player_imm);
	tim_player_immunity(creature_ptr, f.tim_player_imm);
	known_obj_immunity(creature_ptr, f.known_obj_imm);
	player_vuln_flags(creature_ptr, f.player_vuln);

	/*** Set 1 ***/
	TERM_LEN row = 12;
	TERM_LEN col = 1;
	display_player_equippy(creature_ptr, row - 2, col + 8, 0);
	c_put_str(TERM_WHITE, "abcdefghijkl@", row - 1, col + 8);

#ifdef JP
	display_flag_aux(creature_ptr, row + 0, col, "耐酸  :", TR_RES_ACID, &f, 0);
	display_flag_aux(creature_ptr, row + 0, col, "耐酸  :", TR_IM_ACID, &f, DP_IMM);
	display_flag_aux(creature_ptr, row + 1, col, "耐電撃:", TR_RES_ELEC, &f, 0);
	display_flag_aux(creature_ptr, row + 1, col, "耐電撃:", TR_IM_ELEC, &f, DP_IMM);
	display_flag_aux(creature_ptr, row + 2, col, "耐火炎:", TR_RES_FIRE, &f, 0);
	display_flag_aux(creature_ptr, row + 2, col, "耐火炎:", TR_IM_FIRE, &f, DP_IMM);
	display_flag_aux(creature_ptr, row + 3, col, "耐冷気:", TR_RES_COLD, &f, 0);
	display_flag_aux(creature_ptr, row + 3, col, "耐冷気:", TR_IM_COLD, &f, DP_IMM);
	display_flag_aux(creature_ptr, row + 4, col, "耐毒  :", TR_RES_POIS, &f, 0);
	display_flag_aux(creature_ptr, row + 5, col, "耐閃光:", TR_RES_LITE, &f, 0);
	display_flag_aux(creature_ptr, row + 6, col, "耐暗黒:", TR_RES_DARK, &f, 0);
	display_flag_aux(creature_ptr, row + 7, col, "耐破片:", TR_RES_SHARDS, &f, 0);
	display_flag_aux(creature_ptr, row + 8, col, "耐盲目:", TR_RES_BLIND, &f, 0);
	display_flag_aux(creature_ptr, row + 9, col, "耐混乱:", TR_RES_CONF, &f, 0);
#else
	display_flag_aux(creature_ptr, row + 0, col, "Acid  :", TR_RES_ACID, &f, 0);
	display_flag_aux(creature_ptr, row + 0, col, "Acid  :", TR_IM_ACID, &f, DP_IMM);
	display_flag_aux(creature_ptr, row + 1, col, "Elec  :", TR_RES_ELEC, &f, 0);
	display_flag_aux(creature_ptr, row + 1, col, "Elec  :", TR_IM_ELEC, &f, DP_IMM);
	display_flag_aux(creature_ptr, row + 2, col, "Fire  :", TR_RES_FIRE, &f, 0);
	display_flag_aux(creature_ptr, row + 2, col, "Fire  :", TR_IM_FIRE, &f, DP_IMM);
	display_flag_aux(creature_ptr, row + 3, col, "Cold  :", TR_RES_COLD, &f, 0);
	display_flag_aux(creature_ptr, row + 3, col, "Cold  :", TR_IM_COLD, &f, DP_IMM);
	display_flag_aux(creature_ptr, row + 4, col, "Poison:", TR_RES_POIS, &f, 0);
	display_flag_aux(creature_ptr, row + 5, col, "Light :", TR_RES_LITE, &f, 0);
	display_flag_aux(creature_ptr, row + 6, col, "Dark  :", TR_RES_DARK, &f, 0);
	display_flag_aux(creature_ptr, row + 7, col, "Shard :", TR_RES_SHARDS, &f, 0);
	display_flag_aux(creature_ptr, row + 8, col, "Blind :", TR_RES_BLIND, &f, 0);
	display_flag_aux(creature_ptr, row + 9, col, "Conf  :", TR_RES_CONF, &f, 0);
#endif

	/*** Set 2 ***/
	row = 12;
	col = 26;
	display_player_equippy(creature_ptr, row - 2, col + 8, 0);
	c_put_str(TERM_WHITE, "abcdefghijkl@", row - 1, col + 8);

#ifdef JP
	display_flag_aux(creature_ptr, row + 0, col, "耐轟音:", TR_RES_SOUND, &f, 0);
	display_flag_aux(creature_ptr, row + 1, col, "耐地獄:", TR_RES_NETHER, &f, 0);
	display_flag_aux(creature_ptr, row + 2, col, "耐因混:", TR_RES_NEXUS, &f, 0);
	display_flag_aux(creature_ptr, row + 3, col, "耐カオ:", TR_RES_CHAOS, &f, 0);
	display_flag_aux(creature_ptr, row + 4, col, "耐劣化:", TR_RES_DISEN, &f, 0);
	display_flag_aux(creature_ptr, row + 5, col, "耐恐怖:", TR_RES_FEAR, &f, 0);
	display_flag_aux(creature_ptr, row + 6, col, "反射  :", TR_REFLECT, &f, 0);
	display_flag_aux(creature_ptr, row + 7, col, "火炎オ:", TR_SH_FIRE, &f, 0);
	display_flag_aux(creature_ptr, row + 8, col, "電気オ:", TR_SH_ELEC, &f, 0);
	display_flag_aux(creature_ptr, row + 9, col, "冷気オ:", TR_SH_COLD, &f, 0);
#else
	display_flag_aux(creature_ptr, row + 0, col, "Sound :", TR_RES_SOUND, &f, 0);
	display_flag_aux(creature_ptr, row + 1, col, "Nether:", TR_RES_NETHER, &f, 0);
	display_flag_aux(creature_ptr, row + 2, col, "Nexus :", TR_RES_NEXUS, &f, 0);
	display_flag_aux(creature_ptr, row + 3, col, "Chaos :", TR_RES_CHAOS, &f, 0);
	display_flag_aux(creature_ptr, row + 4, col, "Disnch:", TR_RES_DISEN, &f, 0);
	display_flag_aux(creature_ptr, row + 5, col, "Fear  :", TR_RES_FEAR, &f, 0);
	display_flag_aux(creature_ptr, row + 6, col, "Reflct:", TR_REFLECT, &f, 0);
	display_flag_aux(creature_ptr, row + 7, col, "AuFire:", TR_SH_FIRE, &f, 0);
	display_flag_aux(creature_ptr, row + 8, col, "AuElec:", TR_SH_ELEC, &f, 0);
	display_flag_aux(creature_ptr, row + 9, col, "AuCold:", TR_SH_COLD, &f, 0);
#endif

	/*** Set 3 ***/
	row = 12;
	col = 51;
	display_player_equippy(creature_ptr, row - 2, col + 12, 0);
	c_put_str(TERM_WHITE, "abcdefghijkl@", row - 1, col + 12);

#ifdef JP
	display_flag_aux(creature_ptr, row + 0, col, "加速      :", TR_SPEED, &f, 0);
	display_flag_aux(creature_ptr, row + 1, col, "耐麻痺    :", TR_FREE_ACT, &f, 0);
	display_flag_aux(creature_ptr, row + 2, col, "透明体視認:", TR_SEE_INVIS, &f, 0);
	display_flag_aux(creature_ptr, row + 3, col, "経験値保持:", TR_HOLD_EXP, &f, 0);
	display_flag_aux(creature_ptr, row + 4, col, "警告      :", TR_WARNING, &f, 0);
	display_flag_aux(creature_ptr, row + 5, col, "遅消化    :", TR_SLOW_DIGEST, &f, 0);
	display_flag_aux(creature_ptr, row + 6, col, "急回復    :", TR_REGEN, &f, 0);
	display_flag_aux(creature_ptr, row + 7, col, "浮遊      :", TR_LEVITATION, &f, 0);
	display_flag_aux(creature_ptr, row + 8, col, "永遠光源  :", TR_LITE_1, &f, 0);
	display_flag_aux(creature_ptr, row + 9, col, "呪い      :", 0, &f, DP_CURSE);
#else
	display_flag_aux(creature_ptr, row + 0, col, "Speed     :", TR_SPEED, &f, 0);
	display_flag_aux(creature_ptr, row + 1, col, "FreeAction:", TR_FREE_ACT, &f, 0);
	display_flag_aux(creature_ptr, row + 2, col, "SeeInvisi.:", TR_SEE_INVIS, &f, 0);
	display_flag_aux(creature_ptr, row + 3, col, "Hold Exp  :", TR_HOLD_EXP, &f, 0);
	display_flag_aux(creature_ptr, row + 4, col, "Warning   :", TR_WARNING, &f, 0);
	display_flag_aux(creature_ptr, row + 5, col, "SlowDigest:", TR_SLOW_DIGEST, &f, 0);
	display_flag_aux(creature_ptr, row + 6, col, "Regene.   :", TR_REGEN, &f, 0);
	display_flag_aux(creature_ptr, row + 7, col, "Levitation:", TR_LEVITATION, &f, 0);
	display_flag_aux(creature_ptr, row + 8, col, "Perm Lite :", TR_LITE_1, &f, 0);
	display_flag_aux(creature_ptr, row + 9, col, "Cursed    :", 0, &f, DP_CURSE);
#endif
}


/*!
 * @brief プレイヤーの特性フラグ一覧表示2 /
 * @param creature_ptr プレーヤーへの参照ポインタ
 * Special display, part 2
 * @return なし
 */
static void display_player_other_flag_info(player_type *creature_ptr)
{
	/* Extract flags and store */
	all_player_flags f;
	player_flags(creature_ptr, f.player_flags);
	tim_player_flags(creature_ptr, f.tim_player_flags);
	player_immunity(creature_ptr, f.player_imm);
	tim_player_immunity(creature_ptr, f.tim_player_imm);
	known_obj_immunity(creature_ptr, f.known_obj_imm);
	player_vuln_flags(creature_ptr, f.player_vuln);

	/*** Set 1 ***/
	TERM_LEN row = 3;
	TERM_LEN col = 1;
	display_player_equippy(creature_ptr, row - 2, col + 12, DP_WP);
	c_put_str(TERM_WHITE, "ab@", row - 1, col + 12);

#ifdef JP
	display_flag_aux(creature_ptr, row + 0, col, "邪悪 倍打 :", TR_SLAY_EVIL, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 0, col, "邪悪 倍打 :", TR_KILL_EVIL, &f, (DP_WP | DP_IMM));
	display_flag_aux(creature_ptr, row + 1, col, "不死 倍打 :", TR_SLAY_UNDEAD, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 1, col, "不死 倍打 :", TR_KILL_UNDEAD, &f, (DP_WP | DP_IMM));
	display_flag_aux(creature_ptr, row + 2, col, "悪魔 倍打 :", TR_SLAY_DEMON, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 2, col, "悪魔 倍打 :", TR_KILL_DEMON, &f, (DP_WP | DP_IMM));
	display_flag_aux(creature_ptr, row + 3, col, "龍 倍打   :", TR_SLAY_DRAGON, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 3, col, "龍 倍打   :", TR_KILL_DRAGON, &f, (DP_WP | DP_IMM));
	display_flag_aux(creature_ptr, row + 4, col, "人間 倍打 :", TR_SLAY_HUMAN, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 4, col, "人間 倍打 :", TR_KILL_HUMAN, &f, (DP_WP | DP_IMM));
	display_flag_aux(creature_ptr, row + 5, col, "動物 倍打 :", TR_SLAY_ANIMAL, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 5, col, "動物 倍打 :", TR_KILL_ANIMAL, &f, (DP_WP | DP_IMM));
	display_flag_aux(creature_ptr, row + 6, col, "オーク倍打:", TR_SLAY_ORC, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 6, col, "オーク倍打:", TR_KILL_ORC, &f, (DP_WP | DP_IMM));
	display_flag_aux(creature_ptr, row + 7, col, "トロル倍打:", TR_SLAY_TROLL, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 7, col, "トロル倍打:", TR_KILL_TROLL, &f, (DP_WP | DP_IMM));
	display_flag_aux(creature_ptr, row + 8, col, "巨人 倍打 :", TR_SLAY_GIANT, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 8, col, "巨人 倍打 :", TR_KILL_GIANT, &f, (DP_WP | DP_IMM));
	display_flag_aux(creature_ptr, row + 9, col, "溶解      :", TR_BRAND_ACID, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 10, col, "電撃      :", TR_BRAND_ELEC, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 11, col, "焼棄      :", TR_BRAND_FIRE, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 12, col, "凍結      :", TR_BRAND_COLD, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 13, col, "毒殺      :", TR_BRAND_POIS, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 14, col, "切れ味    :", TR_VORPAL, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 15, col, "地震      :", TR_IMPACT, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 16, col, "吸血      :", TR_VAMPIRIC, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 17, col, "カオス効果:", TR_CHAOTIC, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 18, col, "理力      :", TR_FORCE_WEAPON, &f, DP_WP);
#else
	display_flag_aux(creature_ptr, row + 0, col, "Slay Evil :", TR_SLAY_EVIL, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 0, col, "Slay Evil :", TR_KILL_EVIL, &f, (DP_WP | DP_IMM));
	display_flag_aux(creature_ptr, row + 1, col, "Slay Und. :", TR_SLAY_UNDEAD, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 1, col, "Slay Und. :", TR_KILL_UNDEAD, &f, (DP_WP | DP_IMM));
	display_flag_aux(creature_ptr, row + 2, col, "Slay Demon:", TR_SLAY_DEMON, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 2, col, "Slay Demon:", TR_KILL_DEMON, &f, (DP_WP | DP_IMM));
	display_flag_aux(creature_ptr, row + 3, col, "Slay Drag.:", TR_SLAY_DRAGON, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 3, col, "Slay Drag.:", TR_KILL_DRAGON, &f, (DP_WP | DP_IMM));
	display_flag_aux(creature_ptr, row + 4, col, "Slay Human:", TR_SLAY_HUMAN, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 4, col, "Slay Human:", TR_KILL_HUMAN, &f, (DP_WP | DP_IMM));
	display_flag_aux(creature_ptr, row + 5, col, "Slay Anim.:", TR_SLAY_ANIMAL, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 5, col, "Slay Anim.:", TR_KILL_ANIMAL, &f, (DP_WP | DP_IMM));
	display_flag_aux(creature_ptr, row + 6, col, "Slay Orc  :", TR_SLAY_ORC, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 6, col, "Slay Orc  :", TR_KILL_ORC, &f, (DP_WP | DP_IMM));
	display_flag_aux(creature_ptr, row + 7, col, "Slay Troll:", TR_SLAY_TROLL, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 7, col, "Slay Troll:", TR_KILL_TROLL, &f, (DP_WP | DP_IMM));
	display_flag_aux(creature_ptr, row + 8, col, "Slay Giant:", TR_SLAY_GIANT, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 8, col, "Slay Giant:", TR_KILL_GIANT, &f, (DP_WP | DP_IMM));
	display_flag_aux(creature_ptr, row + 9, col, "Acid Brand:", TR_BRAND_ACID, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 10, col, "Elec Brand:", TR_BRAND_ELEC, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 11, col, "Fire Brand:", TR_BRAND_FIRE, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 12, col, "Cold Brand:", TR_BRAND_COLD, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 13, col, "Poison Brd:", TR_BRAND_POIS, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 14, col, "Sharpness :", TR_VORPAL, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 15, col, "Quake     :", TR_IMPACT, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 16, col, "Vampiric  :", TR_VAMPIRIC, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 17, col, "Chaotic   :", TR_CHAOTIC, &f, DP_WP);
	display_flag_aux(creature_ptr, row + 18, col, "Force Wep.:", TR_FORCE_WEAPON, &f, DP_WP);
#endif

	/*** Set 2 ***/
	row = 3;
	col = col + 12 + 7;
	display_player_equippy(creature_ptr, row - 2, col + 13, 0);
	c_put_str(TERM_WHITE, "abcdefghijkl@", row - 1, col + 13);

#ifdef JP
	display_flag_aux(creature_ptr, row + 0, col, "テレパシー :", TR_TELEPATHY, &f, 0);
	display_flag_aux(creature_ptr, row + 1, col, "邪悪ESP    :", TR_ESP_EVIL, &f, 0);
	display_flag_aux(creature_ptr, row + 2, col, "無生物ESP  :", TR_ESP_NONLIVING, &f, 0);
	display_flag_aux(creature_ptr, row + 3, col, "善良ESP    :", TR_ESP_GOOD, &f, 0);
	display_flag_aux(creature_ptr, row + 4, col, "不死ESP    :", TR_ESP_UNDEAD, &f, 0);
	display_flag_aux(creature_ptr, row + 5, col, "悪魔ESP    :", TR_ESP_DEMON, &f, 0);
	display_flag_aux(creature_ptr, row + 6, col, "龍ESP      :", TR_ESP_DRAGON, &f, 0);
	display_flag_aux(creature_ptr, row + 7, col, "人間ESP    :", TR_ESP_HUMAN, &f, 0);
	display_flag_aux(creature_ptr, row + 8, col, "動物ESP    :", TR_ESP_ANIMAL, &f, 0);
	display_flag_aux(creature_ptr, row + 9, col, "オークESP  :", TR_ESP_ORC, &f, 0);
	display_flag_aux(creature_ptr, row + 10, col, "トロルESP  :", TR_ESP_TROLL, &f, 0);
	display_flag_aux(creature_ptr, row + 11, col, "巨人ESP    :", TR_ESP_GIANT, &f, 0);
	display_flag_aux(creature_ptr, row + 12, col, "ユニークESP:", TR_ESP_UNIQUE, &f, 0);
	display_flag_aux(creature_ptr, row + 13, col, "腕力維持   :", TR_SUST_STR, &f, 0);
	display_flag_aux(creature_ptr, row + 14, col, "知力維持   :", TR_SUST_INT, &f, 0);
	display_flag_aux(creature_ptr, row + 15, col, "賢さ維持   :", TR_SUST_WIS, &f, 0);
	display_flag_aux(creature_ptr, row + 16, col, "器用維持   :", TR_SUST_DEX, &f, 0);
	display_flag_aux(creature_ptr, row + 17, col, "耐久維持   :", TR_SUST_CON, &f, 0);
	display_flag_aux(creature_ptr, row + 18, col, "魅力維持   :", TR_SUST_CHR, &f, 0);
#else
	display_flag_aux(creature_ptr, row + 0, col, "Telepathy  :", TR_TELEPATHY, &f, 0);
	display_flag_aux(creature_ptr, row + 1, col, "ESP Evil   :", TR_ESP_EVIL, &f, 0);
	display_flag_aux(creature_ptr, row + 2, col, "ESP Noliv. :", TR_ESP_NONLIVING, &f, 0);
	display_flag_aux(creature_ptr, row + 3, col, "ESP Good   :", TR_ESP_GOOD, &f, 0);
	display_flag_aux(creature_ptr, row + 4, col, "ESP Undead :", TR_ESP_UNDEAD, &f, 0);
	display_flag_aux(creature_ptr, row + 5, col, "ESP Demon  :", TR_ESP_DEMON, &f, 0);
	display_flag_aux(creature_ptr, row + 6, col, "ESP Dragon :", TR_ESP_DRAGON, &f, 0);
	display_flag_aux(creature_ptr, row + 7, col, "ESP Human  :", TR_ESP_HUMAN, &f, 0);
	display_flag_aux(creature_ptr, row + 8, col, "ESP Animal :", TR_ESP_ANIMAL, &f, 0);
	display_flag_aux(creature_ptr, row + 9, col, "ESP Orc    :", TR_ESP_ORC, &f, 0);
	display_flag_aux(creature_ptr, row + 10, col, "ESP Troll  :", TR_ESP_TROLL, &f, 0);
	display_flag_aux(creature_ptr, row + 11, col, "ESP Giant  :", TR_ESP_GIANT, &f, 0);
	display_flag_aux(creature_ptr, row + 12, col, "ESP Unique :", TR_ESP_UNIQUE, &f, 0);
	display_flag_aux(creature_ptr, row + 13, col, "Sust Str   :", TR_SUST_STR, &f, 0);
	display_flag_aux(creature_ptr, row + 14, col, "Sust Int   :", TR_SUST_INT, &f, 0);
	display_flag_aux(creature_ptr, row + 15, col, "Sust Wis   :", TR_SUST_WIS, &f, 0);
	display_flag_aux(creature_ptr, row + 16, col, "Sust Dex   :", TR_SUST_DEX, &f, 0);
	display_flag_aux(creature_ptr, row + 17, col, "Sust Con   :", TR_SUST_CON, &f, 0);
	display_flag_aux(creature_ptr, row + 18, col, "Sust Chr   :", TR_SUST_CHR, &f, 0);
#endif

	/*** Set 3 ***/
	row = 3;
	col = col + 12 + 17;
	display_player_equippy(creature_ptr, row - 2, col + 14, 0);
	c_put_str(TERM_WHITE, "abcdefghijkl@", row - 1, col + 14);

#ifdef JP
	display_flag_aux(creature_ptr, row + 0, col, "追加攻撃    :", TR_BLOWS, &f, 0);
	display_flag_aux(creature_ptr, row + 1, col, "採掘        :", TR_TUNNEL, &f, 0);
	display_flag_aux(creature_ptr, row + 2, col, "赤外線視力  :", TR_INFRA, &f, 0);
	display_flag_aux(creature_ptr, row + 3, col, "魔法道具支配:", TR_MAGIC_MASTERY, &f, 0);
	display_flag_aux(creature_ptr, row + 4, col, "隠密        :", TR_STEALTH, &f, 0);
	display_flag_aux(creature_ptr, row + 5, col, "探索        :", TR_SEARCH, &f, 0);

	display_flag_aux(creature_ptr, row + 7, col, "乗馬        :", TR_RIDING, &f, 0);
	display_flag_aux(creature_ptr, row + 8, col, "投擲        :", TR_THROW, &f, 0);
	display_flag_aux(creature_ptr, row + 9, col, "祝福        :", TR_BLESSED, &f, 0);
	display_flag_aux(creature_ptr, row + 10, col, "反テレポート:", TR_NO_TELE, &f, 0);
	display_flag_aux(creature_ptr, row + 11, col, "反魔法      :", TR_NO_MAGIC, &f, 0);
	display_flag_aux(creature_ptr, row + 12, col, "消費魔力減少:", TR_DEC_MANA, &f, 0);

	display_flag_aux(creature_ptr, row + 14, col, "経験値減少  :", TR_DRAIN_EXP, &f, 0);
	display_flag_aux(creature_ptr, row + 15, col, "乱テレポート:", TR_TELEPORT, &f, 0);
	display_flag_aux(creature_ptr, row + 16, col, "反感        :", TR_AGGRAVATE, &f, 0);
	display_flag_aux(creature_ptr, row + 17, col, "太古の怨念  :", TR_TY_CURSE, &f, 0);
#else
	display_flag_aux(creature_ptr, row + 0, col, "Add Blows   :", TR_BLOWS, &f, 0);
	display_flag_aux(creature_ptr, row + 1, col, "Add Tunnel  :", TR_TUNNEL, &f, 0);
	display_flag_aux(creature_ptr, row + 2, col, "Add Infra   :", TR_INFRA, &f, 0);
	display_flag_aux(creature_ptr, row + 3, col, "Add Device  :", TR_MAGIC_MASTERY, &f, 0);
	display_flag_aux(creature_ptr, row + 4, col, "Add Stealth :", TR_STEALTH, &f, 0);
	display_flag_aux(creature_ptr, row + 5, col, "Add Search  :", TR_SEARCH, &f, 0);

	display_flag_aux(creature_ptr, row + 7, col, "Riding      :", TR_RIDING, &f, 0);
	display_flag_aux(creature_ptr, row + 8, col, "Throw       :", TR_THROW, &f, 0);
	display_flag_aux(creature_ptr, row + 9, col, "Blessed     :", TR_BLESSED, &f, 0);
	display_flag_aux(creature_ptr, row + 10, col, "No Teleport :", TR_NO_TELE, &f, 0);
	display_flag_aux(creature_ptr, row + 11, col, "Anti Magic  :", TR_NO_MAGIC, &f, 0);
	display_flag_aux(creature_ptr, row + 12, col, "Econom. Mana:", TR_DEC_MANA, &f, 0);

	display_flag_aux(creature_ptr, row + 14, col, "Drain Exp   :", TR_DRAIN_EXP, &f, 0);
	display_flag_aux(creature_ptr, row + 15, col, "Rnd.Teleport:", TR_TELEPORT, &f, 0);
	display_flag_aux(creature_ptr, row + 16, col, "Aggravate   :", TR_AGGRAVATE, &f, 0);
	display_flag_aux(creature_ptr, row + 17, col, "TY Curse    :", TR_TY_CURSE, &f, 0);
#endif

}


/*!
 * @brief プレイヤーの打撃能力修正を表示する
 * @param creature_ptr プレーヤーへの参照ポインタ
 * @param hand 武器の装備部位ID
 * @param hand_entry 項目ID
 * @return なし
 */
static void display_player_melee_bonus(player_type *creature_ptr, int hand, int hand_entry)
{
	HIT_PROB show_tohit = creature_ptr->dis_to_h[hand];
	HIT_POINT show_todam = creature_ptr->dis_to_d[hand];
	object_type *o_ptr = &creature_ptr->inventory_list[INVEN_RARM + hand];

	if (object_is_known(o_ptr)) show_tohit += o_ptr->to_h;
	if (object_is_known(o_ptr)) show_todam += o_ptr->to_d;

	show_tohit += creature_ptr->skill_thn / BTH_PLUS_ADJ;

	char buf[160];
	sprintf(buf, "(%+d,%+d)", (int)show_tohit, (int)show_todam);

	if (!has_melee_weapon(creature_ptr, INVEN_RARM) && !has_melee_weapon(creature_ptr, INVEN_LARM))
		display_player_one_line(ENTRY_BARE_HAND, buf, TERM_L_BLUE);
	else if (creature_ptr->ryoute)
		display_player_one_line(ENTRY_TWO_HANDS, buf, TERM_L_BLUE);
	else
		display_player_one_line(hand_entry, buf, TERM_L_BLUE);
}


/*!
 * @brief プレイヤーステータス表示の中央部分を表示するサブルーチン
 * @param creature_ptr プレーヤーへの参照ポインタ
 * Prints the following information on the screen.
 * @return なし
 */
static void display_player_middle(player_type *creature_ptr)
{
	HIT_PROB show_tohit = creature_ptr->dis_to_h_b;
	HIT_POINT show_todam = 0;
	if (creature_ptr->migite)
	{
		display_player_melee_bonus(creature_ptr, 0, left_hander ? ENTRY_LEFT_HAND1 : ENTRY_RIGHT_HAND1);
	}

	if (creature_ptr->hidarite)
	{
		display_player_melee_bonus(creature_ptr, 1, left_hander ? ENTRY_RIGHT_HAND2 : ENTRY_LEFT_HAND2);
	}
	else if ((creature_ptr->pclass == CLASS_MONK) && (empty_hands(creature_ptr, TRUE) & EMPTY_HAND_RARM))
	{
		int i;
		if (creature_ptr->special_defense & KAMAE_MASK)
		{
			for (i = 0; i < MAX_KAMAE; i++)
			{
				if ((creature_ptr->special_defense >> i) & KAMAE_GENBU) break;
			}
			if (i < MAX_KAMAE)
			{
				display_player_one_line(ENTRY_POSTURE, format(_("%sの構え", "%s form"), kamae_shurui[i].desc), TERM_YELLOW);
			}
		}
		else
		{
			display_player_one_line(ENTRY_POSTURE, _("構えなし", "none"), TERM_YELLOW);
		}
	}

	object_type *o_ptr = &creature_ptr->inventory_list[INVEN_BOW];
	if (object_is_known(o_ptr)) show_tohit += o_ptr->to_h;
	if (object_is_known(o_ptr)) show_todam += o_ptr->to_d;

	if ((o_ptr->sval == SV_LIGHT_XBOW) || (o_ptr->sval == SV_HEAVY_XBOW))
		show_tohit += creature_ptr->weapon_exp[0][o_ptr->sval] / 400;
	else
		show_tohit += (creature_ptr->weapon_exp[0][o_ptr->sval] - (WEAPON_EXP_MASTER / 2)) / 200;

	show_tohit += creature_ptr->skill_thb / BTH_PLUS_ADJ;

	display_player_one_line(ENTRY_SHOOT_HIT_DAM, format("(%+d,%+d)", show_tohit, show_todam), TERM_L_BLUE);
	int tmul = 0;
	if (creature_ptr->inventory_list[INVEN_BOW].k_idx)
	{
		tmul = bow_tmul(creature_ptr->inventory_list[INVEN_BOW].sval);
		if (creature_ptr->xtra_might) tmul++;

		tmul = tmul * (100 + (int)(adj_str_td[creature_ptr->stat_ind[A_STR]]) - 128);
	}

	display_player_one_line(ENTRY_SHOOT_POWER, format("x%d.%02d", tmul / 100, tmul % 100), TERM_L_BLUE);
	display_player_one_line(ENTRY_BASE_AC, format("[%d,%+d]", creature_ptr->dis_ac, creature_ptr->dis_to_a), TERM_L_BLUE);

	int i = creature_ptr->pspeed - 110;
	if (creature_ptr->action == ACTION_SEARCH) i += 10;

	TERM_COLOR attr;
	if (i > 0)
	{
		if (!creature_ptr->riding)
			attr = TERM_L_GREEN;
		else
			attr = TERM_GREEN;
	}
	else if (i == 0)
	{
		if (!creature_ptr->riding)
			attr = TERM_L_BLUE;
		else
			attr = TERM_GREEN;
	}
	else
	{
		if (!creature_ptr->riding)
			attr = TERM_L_UMBER;
		else
			attr = TERM_RED;
	}

	int tmp_speed = 0;
	if (!creature_ptr->riding)
	{
		if (IS_FAST(creature_ptr)) tmp_speed += 10;
		if (creature_ptr->slow) tmp_speed -= 10;
		if (creature_ptr->lightspeed) tmp_speed = 99;
	}
	else
	{
		if (MON_FAST(&creature_ptr->current_floor_ptr->m_list[creature_ptr->riding])) tmp_speed += 10;
		if (MON_SLOW(&creature_ptr->current_floor_ptr->m_list[creature_ptr->riding])) tmp_speed -= 10;
	}

	char buf[160];
	if (tmp_speed)
	{
		if (!creature_ptr->riding)
			sprintf(buf, "(%+d%+d)", i - tmp_speed, tmp_speed);
		else
			sprintf(buf, _("乗馬中 (%+d%+d)", "Riding (%+d%+d)"), i - tmp_speed, tmp_speed);

		if (tmp_speed > 0)
			attr = TERM_YELLOW;
		else
			attr = TERM_VIOLET;
	}
	else
	{
		if (!creature_ptr->riding)
			sprintf(buf, "(%+d)", i);
		else
			sprintf(buf, _("乗馬中 (%+d)", "Riding (%+d)"), i);
	}

	display_player_one_line(ENTRY_SPEED, buf, attr);
	display_player_one_line(ENTRY_LEVEL, format("%d", creature_ptr->lev), TERM_L_GREEN);

	int e = (creature_ptr->prace == RACE_ANDROID) ? ENTRY_EXP_ANDR : ENTRY_CUR_EXP;
	if (creature_ptr->exp >= creature_ptr->max_exp)
		display_player_one_line(e, format("%ld", creature_ptr->exp), TERM_L_GREEN);
	else
		display_player_one_line(e, format("%ld", creature_ptr->exp), TERM_YELLOW);

	if (creature_ptr->prace != RACE_ANDROID)
		display_player_one_line(ENTRY_MAX_EXP, format("%ld", creature_ptr->max_exp), TERM_L_GREEN);

	e = (creature_ptr->prace == RACE_ANDROID) ? ENTRY_EXP_TO_ADV_ANDR : ENTRY_EXP_TO_ADV;

	if (creature_ptr->lev >= PY_MAX_LEVEL)
		display_player_one_line(e, "*****", TERM_L_GREEN);
	else if (creature_ptr->prace == RACE_ANDROID)
		display_player_one_line(e, format("%ld", (s32b)(player_exp_a[creature_ptr->lev - 1] * creature_ptr->expfact / 100L)), TERM_L_GREEN);
	else
		display_player_one_line(e, format("%ld", (s32b)(player_exp[creature_ptr->lev - 1] * creature_ptr->expfact / 100L)), TERM_L_GREEN);

	display_player_one_line(ENTRY_GOLD, format("%ld", creature_ptr->au), TERM_L_GREEN);

	int day, hour, min;
	extract_day_hour_min(creature_ptr, &day, &hour, &min);

	if (day < MAX_DAYS)
		sprintf(buf, _("%d日目 %2d:%02d", "Day %d %2d:%02d"), day, hour, min);
	else
		sprintf(buf, _("*****日目 %2d:%02d", "Day ***** %2d:%02d"), hour, min);

	display_player_one_line(ENTRY_DAY, buf, TERM_L_GREEN);

	if (creature_ptr->chp >= creature_ptr->mhp)
		display_player_one_line(ENTRY_HP, format("%4d/%4d", creature_ptr->chp, creature_ptr->mhp), TERM_L_GREEN);
	else if (creature_ptr->chp > (creature_ptr->mhp * hitpoint_warn) / 10)
		display_player_one_line(ENTRY_HP, format("%4d/%4d", creature_ptr->chp, creature_ptr->mhp), TERM_YELLOW);
	else
		display_player_one_line(ENTRY_HP, format("%4d/%4d", creature_ptr->chp, creature_ptr->mhp), TERM_RED);

	if (creature_ptr->csp >= creature_ptr->msp)
		display_player_one_line(ENTRY_SP, format("%4d/%4d", creature_ptr->csp, creature_ptr->msp), TERM_L_GREEN);
	else if (creature_ptr->csp > (creature_ptr->msp * mana_warn) / 10)
		display_player_one_line(ENTRY_SP, format("%4d/%4d", creature_ptr->csp, creature_ptr->msp), TERM_YELLOW);
	else
		display_player_one_line(ENTRY_SP, format("%4d/%4d", creature_ptr->csp, creature_ptr->msp), TERM_RED);

	u32b play_hour = current_world_ptr->play_time / (60 * 60);
	u32b play_min = (current_world_ptr->play_time / 60) % 60;
	u32b play_sec = current_world_ptr->play_time % 60;
	display_player_one_line(ENTRY_PLAY_TIME, format("%.2lu:%.2lu:%.2lu", play_hour, play_min, play_sec), TERM_L_GREEN);
}


/*!
 * @brief プレイヤーのステータス表示メイン処理
 * Display the character on the screen (various modes)
 * @param creature_ptr プレーヤーへの参照ポインタ
 * @param mode 表示モードID
 * @return なし
 * @details
 * <pre>
 * The top one and bottom two lines are left blank.
 * Mode 0 = standard display with skills
 * Mode 1 = standard display with history
 * Mode 2 = summary of various things
 * Mode 3 = summary of various things (part 2)
 * Mode 4 = mutations
 * </pre>
 */
void display_player(player_type *creature_ptr, int mode)
{
	if ((creature_ptr->muta1 || creature_ptr->muta2 || creature_ptr->muta3) && display_mutations)
		mode = (mode % 5);
	else
		mode = (mode % 4);

	clear_from(0);

	if (mode == 2)
	{
		display_player_misc_info(creature_ptr);
		display_player_stat_info(creature_ptr);
		display_player_flag_info(creature_ptr);
		return;
	}

	if (mode == 3)
	{
		display_player_other_flag_info(creature_ptr);
		return;
	}

	if (mode == 4)
	{
		do_cmd_knowledge_mutations(creature_ptr);
		return;
	}

	char tmp[64];
	if ((mode != 0) && (mode != 1)) return;

	/* Name, Sex, Race, Class */
#ifdef JP
	sprintf(tmp, "%s%s%s", ap_ptr->title, ap_ptr->no == 1 ? "の" : "", creature_ptr->name);
#else
	sprintf(tmp, "%s %s", ap_ptr->title, creature_ptr->name);
#endif

	display_player_one_line(ENTRY_NAME, tmp, TERM_L_BLUE);
	display_player_one_line(ENTRY_SEX, sp_ptr->title, TERM_L_BLUE);
	display_player_one_line(ENTRY_RACE, (creature_ptr->mimic_form ? mimic_info[creature_ptr->mimic_form].title : rp_ptr->title), TERM_L_BLUE);
	display_player_one_line(ENTRY_CLASS, cp_ptr->title, TERM_L_BLUE);

	if (creature_ptr->realm1)
	{
		if (creature_ptr->realm2)
			sprintf(tmp, "%s, %s", realm_names[creature_ptr->realm1], realm_names[creature_ptr->realm2]);
		else
			strcpy(tmp, realm_names[creature_ptr->realm1]);
		display_player_one_line(ENTRY_REALM, tmp, TERM_L_BLUE);
	}

	if ((creature_ptr->pclass == CLASS_CHAOS_WARRIOR) || (creature_ptr->muta2 & MUT2_CHAOS_GIFT))
		display_player_one_line(ENTRY_PATRON, chaos_patrons[creature_ptr->chaos_patron], TERM_L_BLUE);

	/* Age, Height, Weight, Social */
	/* 身長はセンチメートルに、体重はキログラムに変更してあります */
#ifdef JP
	display_player_one_line(ENTRY_AGE, format("%d才", (int)creature_ptr->age), TERM_L_BLUE);
	display_player_one_line(ENTRY_HEIGHT, format("%dcm", (int)((creature_ptr->ht * 254) / 100)), TERM_L_BLUE);
	display_player_one_line(ENTRY_WEIGHT, format("%dkg", (int)((creature_ptr->wt * 4536) / 10000)), TERM_L_BLUE);
	display_player_one_line(ENTRY_SOCIAL, format("%d  ", (int)creature_ptr->sc), TERM_L_BLUE);
#else
	display_player_one_line(ENTRY_AGE, format("%d", (int)creature_ptr->age), TERM_L_BLUE);
	display_player_one_line(ENTRY_HEIGHT, format("%d", (int)creature_ptr->ht), TERM_L_BLUE);
	display_player_one_line(ENTRY_WEIGHT, format("%d", (int)creature_ptr->wt), TERM_L_BLUE);
	display_player_one_line(ENTRY_SOCIAL, format("%d", (int)creature_ptr->sc), TERM_L_BLUE);
#endif
	display_player_one_line(ENTRY_ALIGN, format("%s", your_alignment(creature_ptr)), TERM_L_BLUE);

	char buf[80];
	for (int i = 0; i < A_MAX; i++)
	{
		if (creature_ptr->stat_cur[i] < creature_ptr->stat_max[i])
		{
			put_str(stat_names_reduced[i], 3 + i, 53);
			int value = creature_ptr->stat_use[i];
			cnv_stat(value, buf);
			c_put_str(TERM_YELLOW, buf, 3 + i, 60);
			value = creature_ptr->stat_top[i];
			cnv_stat(value, buf);
			c_put_str(TERM_L_GREEN, buf, 3 + i, 67);
		}
		else
		{
			put_str(stat_names[i], 3 + i, 53);
			cnv_stat(creature_ptr->stat_use[i], buf);
			c_put_str(TERM_L_GREEN, buf, 3 + i, 60);
		}

		if (creature_ptr->stat_max[i] == creature_ptr->stat_max_max[i])
		{
			c_put_str(TERM_WHITE, "!", 3 + i, _(58, 58 - 2));
		}
	}

	floor_type *floor_ptr = creature_ptr->current_floor_ptr;
	if (mode == 0)
	{
		display_player_middle(creature_ptr);
		display_player_various(creature_ptr);
		return;
	}

	char statmsg[1000];
	put_str(_("(キャラクターの生い立ち)", "(Character Background)"), 11, 25);

	for (int i = 0; i < 4; i++)
	{
		put_str(creature_ptr->history[i], i + 12, 10);
	}

	*statmsg = '\0';

	if (creature_ptr->is_dead)
	{
		if (current_world_ptr->total_winner)
		{
#ifdef JP
			sprintf(statmsg, "…あなたは勝利の後%sした。", streq(creature_ptr->died_from, "Seppuku") ? "切腹" : "引退");
#else
			sprintf(statmsg, "...You %s after winning.", streq(creature_ptr->died_from, "Seppuku") ? "committed seppuku" : "retired from the adventure");
#endif
		}
		else if (!floor_ptr->dun_level)
		{
#ifdef JP
			sprintf(statmsg, "…あなたは%sで%sに殺された。", map_name(creature_ptr), creature_ptr->died_from);
#else
			sprintf(statmsg, "...You were killed by %s in %s.", creature_ptr->died_from, map_name(creature_ptr));
#endif
		}
		else if (floor_ptr->inside_quest && is_fixed_quest_idx(floor_ptr->inside_quest))
		{
			/* Get the quest text */
			/* Bewere that INIT_ASSIGN resets the cur_num. */
			init_flags = INIT_NAME_ONLY;

			process_dungeon_file(creature_ptr, "q_info.txt", 0, 0, 0, 0);

#ifdef JP
			sprintf(statmsg, "…あなたは、クエスト「%s」で%sに殺された。", quest[floor_ptr->inside_quest].name, creature_ptr->died_from);
#else
			sprintf(statmsg, "...You were killed by %s in the quest '%s'.", creature_ptr->died_from, quest[floor_ptr->inside_quest].name);
#endif
		}
		else
		{
#ifdef JP
			sprintf(statmsg, "…あなたは、%sの%d階で%sに殺された。", map_name(creature_ptr), (int)floor_ptr->dun_level, creature_ptr->died_from);
#else
			sprintf(statmsg, "...You were killed by %s on level %d of %s.", creature_ptr->died_from, floor_ptr->dun_level, map_name(creature_ptr));
#endif
		}
	}
	else if (current_world_ptr->character_dungeon)
	{
		if (!floor_ptr->dun_level)
		{
			sprintf(statmsg, _("…あなたは現在、 %s にいる。", "...Now, you are in %s."), map_name(creature_ptr));
		}
		else if (floor_ptr->inside_quest && is_fixed_quest_idx(floor_ptr->inside_quest))
		{
			/* Clear the text */
			/* Must be done before doing INIT_SHOW_TEXT */
			for (int i = 0; i < 10; i++)
			{
				quest_text[i][0] = '\0';
			}

			quest_text_line = 0;
			init_flags = INIT_NAME_ONLY;
			process_dungeon_file(creature_ptr, "q_info.txt", 0, 0, 0, 0);
			sprintf(statmsg, _("…あなたは現在、 クエスト「%s」を遂行中だ。", "...Now, you are in the quest '%s'."), quest[floor_ptr->inside_quest].name);
		}
		else
		{
#ifdef JP
			sprintf(statmsg, "…あなたは現在、 %s の %d 階で探索している。", map_name(creature_ptr), (int)floor_ptr->dun_level);
#else
			sprintf(statmsg, "...Now, you are exploring level %d of %s.", floor_ptr->dun_level, map_name(creature_ptr));
#endif
		}
	}

	if (!*statmsg) return;

	char temp[64 * 2];
	roff_to_buf(statmsg, 60, temp, sizeof(temp));
	char  *t;
	t = temp;
	for (int i = 0; i < 2; i++)
	{
		if (t[0] == 0) return;

		put_str(t, i + 5 + 12, 10);
		t += strlen(t) + 1;
	}
}
