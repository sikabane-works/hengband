﻿/*!
 * @file autopick.c
 * @brief 自動拾い機能の実装 / Object Auto-picker/Destroyer
 * @date 2014/01/02
 * @author
 * Copyright (c) 2002  Mogami\n
 *\n
 * This software may be copied and distributed for educational, research, and\n
 * not for profit purposes provided that this copyright and statement are\n
 * included in all such copies.\n
 * 2014 Deskull rearranged comment for Doxygen.\n
 */

#include "angband.h"
#include "util.h"
#include "autopick/autopick-commands-table.h"
#include "autopick/autopick-dirty-flags.h"
#include "autopick/autopick-flags-table.h"
#include "autopick/autopick-initializer.h"
#include "autopick/autopick-key-flag-process.h"
#include "autopick/autopick-menu-data-table.h"
#include "autopick/autopick-methods-table.h"
#include "autopick/autopick-keys-table.h"
#include "autopick/autopick-entry.h"
#include "gameterm.h"
#include "autopick/autopick.h"
#include "core.h"
#include "core/show-file.h"
#include "cmd/cmd-save.h"
#include "io/read-pref-file.h"

#include "mind.h"

#include "market/store.h"
#include "player-status.h"
#include "player-move.h"
#include "player-class.h"
#include "player-race.h"
#include "player-inventory.h"
#include "view/display-player.h"
#include "object/object-kind.h"
#include "object-ego.h"
#include "object-flavor.h"
#include "object-hook.h"

#include "files.h"
#include "floor.h"
#include "world.h"
#include "monster.h"
#include "monsterrace.h"
#include "view-mainwindow.h" // 暫定。後で消す

/*
 * Automatically destroy an item if it is to be destroyed
 *
 * When always_pickup is 'yes', we disable auto-destroyer function of
 * auto-picker/destroyer, and do only easy-auto-destroyer.
 */
static object_type autopick_last_destroyed_object;

/*
 *  Get file name for autopick preference
 */
static concptr pickpref_filename(player_type *player_ptr, int filename_mode)
{
	static const char namebase[] = _("picktype", "pickpref");

	switch (filename_mode)
	{
	case PT_DEFAULT:
		return format("%s.prf", namebase);

	case PT_WITH_PNAME:
		return format("%s-%s.prf", namebase, player_ptr->base_name);

	default:
		return NULL;
	}
}


/*
 * Load an autopick preference file
 */
void autopick_load_pref(player_type *player_ptr, bool disp_mes)
{
	GAME_TEXT buf[80];
	init_autopick();
	my_strcpy(buf, pickpref_filename(player_ptr, PT_WITH_PNAME), sizeof(buf));
	errr err = process_autopick_file(player_ptr, buf);
	if (err == 0 && disp_mes)
	{
		msg_format(_("%sを読み込みました。", "Loaded '%s'."), buf);
	}

	if (err < 0)
	{
		my_strcpy(buf, pickpref_filename(player_ptr, PT_DEFAULT), sizeof(buf));
		err = process_autopick_file(player_ptr, buf);
		if (err == 0 && disp_mes)
		{
			msg_format(_("%sを読み込みました。", "Loaded '%s'."), buf);
		}
	}

	if (err && disp_mes)
	{
		msg_print(_("自動拾い設定ファイルの読み込みに失敗しました。", "Failed to reload autopick preference."));
	}
}


/*
 * Add one line to autopick_list[]
 */
static void add_autopick_list(autopick_type *entry)
{
	if (max_autopick >= max_max_autopick)
	{
		int old_max_max_autopick = max_max_autopick;
		autopick_type *old_autopick_list = autopick_list;
		max_max_autopick += MAX_AUTOPICK_DEFAULT;
		C_MAKE(autopick_list, max_max_autopick, autopick_type);
		(void)C_COPY(autopick_list, old_autopick_list, old_max_max_autopick, autopick_type);
		C_KILL(old_autopick_list, old_max_max_autopick, autopick_type);
	}

	autopick_list[max_autopick] = *entry;
	max_autopick++;
}


/*
 *  Process line for auto picker/destroyer.
 */
void process_autopick_file_command(char *buf)
{
	autopick_type an_entry, *entry = &an_entry;
	int i;
	for (i = 0; buf[i]; i++)
	{
#ifdef JP
		if (iskanji(buf[i]))
		{
			i++;
			continue;
		}
#endif
		if (iswspace(buf[i]) && buf[i] != ' ')
			break;
	}

	buf[i] = 0;
	if (!autopick_new_entry(entry, buf, FALSE)) return;

	for (i = 0; i < max_autopick; i++)
	{
		if (!strcmp(entry->name, autopick_list[i].name)
			&& entry->flag[0] == autopick_list[i].flag[0]
			&& entry->flag[1] == autopick_list[i].flag[1]
			&& entry->dice == autopick_list[i].dice
			&& entry->bonus == autopick_list[i].bonus)
		{
			autopick_free_entry(entry);
			return;
		}
	}

	add_autopick_list(entry);
	return;
}


/*
 * A function for Auto-picker/destroyer
 * Examine whether the object matches to the entry
 */
static bool is_autopick_aux(player_type *player_ptr, object_type *o_ptr, autopick_type *entry, concptr o_name)
{
	concptr ptr = entry->name;
	if (IS_FLG(FLG_UNAWARE) && object_is_aware(o_ptr))
		return FALSE;

	if (IS_FLG(FLG_UNIDENTIFIED)
		&& (object_is_known(o_ptr) || (o_ptr->ident & IDENT_SENSE)))
		return FALSE;

	if (IS_FLG(FLG_IDENTIFIED) && !object_is_known(o_ptr))
		return FALSE;

	if (IS_FLG(FLG_STAR_IDENTIFIED) &&
		(!object_is_known(o_ptr) || !OBJECT_IS_FULL_KNOWN(o_ptr)))
		return FALSE;

	if (IS_FLG(FLG_BOOSTED))
	{
		object_kind *k_ptr = &k_info[o_ptr->k_idx];
		if (!object_is_melee_weapon(o_ptr))
			return FALSE;

		if ((o_ptr->dd == k_ptr->dd) && (o_ptr->ds == k_ptr->ds))
			return FALSE;

		if (!object_is_known(o_ptr) && object_is_quest_target(o_ptr))
		{
			return FALSE;
		}
	}

	if (IS_FLG(FLG_MORE_DICE))
	{
		if (o_ptr->dd * o_ptr->ds < entry->dice)
			return FALSE;
	}

	if (IS_FLG(FLG_MORE_BONUS))
	{
		if (!object_is_known(o_ptr)) return FALSE;

		if (o_ptr->pval)
		{
			if (o_ptr->pval < entry->bonus) return FALSE;
		}
		else
		{
			if (o_ptr->to_h < entry->bonus &&
				o_ptr->to_d < entry->bonus &&
				o_ptr->to_a < entry->bonus &&
				o_ptr->pval < entry->bonus)
				return FALSE;
		}
	}

	if (IS_FLG(FLG_WORTHLESS) && object_value(o_ptr) > 0)
		return FALSE;

	if (IS_FLG(FLG_ARTIFACT))
	{
		if (!object_is_known(o_ptr) || !object_is_artifact(o_ptr))
			return FALSE;
	}

	if (IS_FLG(FLG_EGO))
	{
		if (!object_is_ego(o_ptr)) return FALSE;
		if (!object_is_known(o_ptr) &&
			!((o_ptr->ident & IDENT_SENSE) && o_ptr->feeling == FEEL_EXCELLENT))
			return FALSE;
	}

	if (IS_FLG(FLG_GOOD))
	{
		if (!object_is_equipment(o_ptr)) return FALSE;
		if (object_is_known(o_ptr))
		{
			if (!object_is_nameless(o_ptr))
				return FALSE;

			if (o_ptr->to_a <= 0 && (o_ptr->to_h + o_ptr->to_d) <= 0)
				return FALSE;
		}
		else if (o_ptr->ident & IDENT_SENSE)
		{
			switch (o_ptr->feeling)
			{
			case FEEL_GOOD:
				break;

			default:
				return FALSE;
			}
		}
		else
		{
			return FALSE;
		}
	}

	if (IS_FLG(FLG_NAMELESS))
	{
		if (!object_is_equipment(o_ptr)) return FALSE;
		if (object_is_known(o_ptr))
		{
			if (!object_is_nameless(o_ptr))
				return FALSE;
		}
		else if (o_ptr->ident & IDENT_SENSE)
		{
			switch (o_ptr->feeling)
			{
			case FEEL_AVERAGE:
			case FEEL_GOOD:
			case FEEL_BROKEN:
			case FEEL_CURSED:
				break;

			default:
				return FALSE;
			}
		}
		else
		{
			return FALSE;
		}
	}

	if (IS_FLG(FLG_AVERAGE))
	{
		if (!object_is_equipment(o_ptr)) return FALSE;
		if (object_is_known(o_ptr))
		{
			if (!object_is_nameless(o_ptr))
				return FALSE;

			if (object_is_cursed(o_ptr) || object_is_broken(o_ptr))
				return FALSE;

			if (o_ptr->to_a > 0 || (o_ptr->to_h + o_ptr->to_d) > 0)
				return FALSE;
		}
		else if (o_ptr->ident & IDENT_SENSE)
		{
			switch (o_ptr->feeling)
			{
			case FEEL_AVERAGE:
				break;

			default:
				return FALSE;
			}
		}
		else
		{
			return FALSE;
		}
	}

	if (IS_FLG(FLG_RARE) && !object_is_rare(o_ptr))
		return FALSE;

	if (IS_FLG(FLG_COMMON) && object_is_rare(o_ptr))
		return FALSE;

	if (IS_FLG(FLG_WANTED) && !object_is_bounty(o_ptr))
		return FALSE;

	if (IS_FLG(FLG_UNIQUE) &&
		((o_ptr->tval != TV_CORPSE && o_ptr->tval != TV_STATUE) ||
			!(r_info[o_ptr->pval].flags1 & RF1_UNIQUE)))
		return FALSE;

	if (IS_FLG(FLG_HUMAN) &&
		(o_ptr->tval != TV_CORPSE ||
			!my_strchr("pht", r_info[o_ptr->pval].d_char)))
		return FALSE;

	if (IS_FLG(FLG_UNREADABLE) &&
		(o_ptr->tval < TV_LIFE_BOOK ||
			check_book_realm(player_ptr, o_ptr->tval, o_ptr->sval)))
		return FALSE;

	if (IS_FLG(FLG_REALM1) &&
		(REALM1_BOOK != o_ptr->tval ||
			player_ptr->pclass == CLASS_SORCERER ||
			player_ptr->pclass == CLASS_RED_MAGE))
		return FALSE;

	if (IS_FLG(FLG_REALM2) &&
		(REALM2_BOOK != o_ptr->tval ||
			player_ptr->pclass == CLASS_SORCERER ||
			player_ptr->pclass == CLASS_RED_MAGE))
		return FALSE;

	if (IS_FLG(FLG_FIRST) &&
		(o_ptr->tval < TV_LIFE_BOOK || 0 != o_ptr->sval))
		return FALSE;

	if (IS_FLG(FLG_SECOND) &&
		(o_ptr->tval < TV_LIFE_BOOK || 1 != o_ptr->sval))
		return FALSE;

	if (IS_FLG(FLG_THIRD) &&
		(o_ptr->tval < TV_LIFE_BOOK || 2 != o_ptr->sval))
		return FALSE;

	if (IS_FLG(FLG_FOURTH) &&
		(o_ptr->tval < TV_LIFE_BOOK || 3 != o_ptr->sval))
		return FALSE;

	if (IS_FLG(FLG_WEAPONS))
	{
		if (!object_is_weapon(o_ptr))
			return FALSE;
	}
	else if (IS_FLG(FLG_FAVORITE_WEAPONS))
	{
		if (!object_is_favorite(o_ptr))
			return FALSE;
	}
	else if (IS_FLG(FLG_ARMORS))
	{
		if (!object_is_armour(o_ptr))
			return FALSE;
	}
	else if (IS_FLG(FLG_MISSILES))
	{
		if (!object_is_ammo(o_ptr)) return FALSE;
	}
	else if (IS_FLG(FLG_DEVICES))
	{
		switch (o_ptr->tval)
		{
		case TV_SCROLL: case TV_STAFF: case TV_WAND: case TV_ROD:
			break;
		default: return FALSE;
		}
	}
	else if (IS_FLG(FLG_LIGHTS))
	{
		if (!(o_ptr->tval == TV_LITE))
			return FALSE;
	}
	else if (IS_FLG(FLG_JUNKS))
	{
		switch (o_ptr->tval)
		{
		case TV_SKELETON: case TV_BOTTLE:
		case TV_JUNK: case TV_STATUE:
			break;
		default: return FALSE;
		}
	}
	else if (IS_FLG(FLG_CORPSES))
	{
		if (o_ptr->tval != TV_CORPSE && o_ptr->tval != TV_SKELETON)
			return FALSE;
	}
	else if (IS_FLG(FLG_SPELLBOOKS))
	{
		if (!(o_ptr->tval >= TV_LIFE_BOOK))
			return FALSE;
	}
	else if (IS_FLG(FLG_HAFTED))
	{
		if (!(o_ptr->tval == TV_HAFTED))
			return FALSE;
	}
	else if (IS_FLG(FLG_SHIELDS))
	{
		if (!(o_ptr->tval == TV_SHIELD))
			return FALSE;
	}
	else if (IS_FLG(FLG_BOWS))
	{
		if (!(o_ptr->tval == TV_BOW))
			return FALSE;
	}
	else if (IS_FLG(FLG_RINGS))
	{
		if (!(o_ptr->tval == TV_RING))
			return FALSE;
	}
	else if (IS_FLG(FLG_AMULETS))
	{
		if (!(o_ptr->tval == TV_AMULET))
			return FALSE;
	}
	else if (IS_FLG(FLG_SUITS))
	{
		if (!(o_ptr->tval == TV_DRAG_ARMOR ||
			o_ptr->tval == TV_HARD_ARMOR ||
			o_ptr->tval == TV_SOFT_ARMOR))
			return FALSE;
	}
	else if (IS_FLG(FLG_CLOAKS))
	{
		if (!(o_ptr->tval == TV_CLOAK))
			return FALSE;
	}
	else if (IS_FLG(FLG_HELMS))
	{
		if (!(o_ptr->tval == TV_CROWN || o_ptr->tval == TV_HELM))
			return FALSE;
	}
	else if (IS_FLG(FLG_GLOVES))
	{
		if (!(o_ptr->tval == TV_GLOVES))
			return FALSE;
	}
	else if (IS_FLG(FLG_BOOTS))
	{
		if (!(o_ptr->tval == TV_BOOTS))
			return FALSE;
	}

	if (*ptr == '^')
	{
		ptr++;
		if (strncmp(o_name, ptr, strlen(ptr))) return FALSE;
	}
	else
	{
		if (!my_strstr(o_name, ptr)) return FALSE;
	}

	if (!IS_FLG(FLG_COLLECTING)) return TRUE;

	for (int j = 0; j < INVEN_PACK; j++)
	{
		/*
		 * 'Collecting' means the item must be absorbed
		 * into an inventory slot.
		 * But an item can not be absorbed into itself!
		 */
		if ((&player_ptr->inventory_list[j] != o_ptr) &&
			object_similar(&player_ptr->inventory_list[j], o_ptr))
			return TRUE;
	}

	return FALSE;
}


/*
 * A function for Auto-picker/destroyer
 * Examine whether the object matches to the list of keywords or not.
 */
int is_autopick(player_type *player_ptr, object_type *o_ptr)
{
	GAME_TEXT o_name[MAX_NLEN];
	if (o_ptr->tval == TV_GOLD) return -1;

	object_desc(player_ptr, o_name, o_ptr, (OD_NO_FLAVOR | OD_OMIT_PREFIX | OD_NO_PLURAL));
	str_tolower(o_name);
	for (int i = 0; i < max_autopick; i++)
	{
		autopick_type *entry = &autopick_list[i];
		if (is_autopick_aux(player_ptr, o_ptr, entry, o_name))
			return i;
	}

	return -1;
}


/*
 *  Auto inscription
 */
static void auto_inscribe_item(player_type *player_ptr, object_type *o_ptr, int idx)
{
	if (idx < 0 || !autopick_list[idx].insc) return;

	if (!o_ptr->inscription)
		o_ptr->inscription = quark_add(autopick_list[idx].insc);

	player_ptr->window |= (PW_EQUIP | PW_INVEN);
	player_ptr->update |= (PU_BONUS);
}


/*
 * Automatically destroy items in this grid.
 */
static bool is_opt_confirm_destroy(player_type *player_ptr, object_type *o_ptr)
{
	if (!destroy_items) return FALSE;

	if (leave_worth)
		if (object_value(o_ptr) > 0) return FALSE;

	if (leave_equip)
		if (object_is_weapon_armour_ammo(o_ptr)) return FALSE;

	if (leave_chest)
		if ((o_ptr->tval == TV_CHEST) && o_ptr->pval) return FALSE;

	if (leave_wanted)
	{
		if (object_is_bounty(o_ptr)) return FALSE;
	}

	if (leave_corpse)
		if (o_ptr->tval == TV_CORPSE) return FALSE;

	if (leave_junk)
		if ((o_ptr->tval == TV_SKELETON) || (o_ptr->tval == TV_BOTTLE) || (o_ptr->tval == TV_JUNK) || (o_ptr->tval == TV_STATUE)) return FALSE;

	if (leave_special)
	{
		if (player_ptr->prace == RACE_DEMON)
		{
			if (o_ptr->tval == TV_CORPSE &&
				o_ptr->sval == SV_CORPSE &&
				my_strchr("pht", r_info[o_ptr->pval].d_char))
				return FALSE;
		}

		if (player_ptr->pclass == CLASS_ARCHER)
		{
			if (o_ptr->tval == TV_SKELETON ||
				(o_ptr->tval == TV_CORPSE && o_ptr->sval == SV_SKELETON))
				return FALSE;
		}
		else if (player_ptr->pclass == CLASS_NINJA)
		{
			if (o_ptr->tval == TV_LITE &&
				o_ptr->name2 == EGO_LITE_DARKNESS && object_is_known(o_ptr))
				return FALSE;
		}
		else if (player_ptr->pclass == CLASS_BEASTMASTER ||
			player_ptr->pclass == CLASS_CAVALRY)
		{
			if (o_ptr->tval == TV_WAND &&
				o_ptr->sval == SV_WAND_HEAL_MONSTER && object_is_aware(o_ptr))
				return FALSE;
		}
	}

	if (o_ptr->tval == TV_GOLD) return FALSE;

	return TRUE;
}


static void auto_destroy_item(player_type *player_ptr, object_type *o_ptr, int autopick_idx)
{
	bool destroy = FALSE;
	if (is_opt_confirm_destroy(player_ptr, o_ptr)) destroy = TRUE;

	if (autopick_idx >= 0 &&
		!(autopick_list[autopick_idx].action & DO_AUTODESTROY))
		destroy = FALSE;

	if (!always_pickup)
	{
		if (autopick_idx >= 0 &&
			(autopick_list[autopick_idx].action & DO_AUTODESTROY))
			destroy = TRUE;
	}

	if (!destroy) return;

	disturb(player_ptr, FALSE, FALSE);
	if (!can_player_destroy_object(o_ptr))
	{
		GAME_TEXT o_name[MAX_NLEN];
		object_desc(player_ptr, o_name, o_ptr, 0);
		msg_format(_("%sは破壊不能だ。", "You cannot auto-destroy %s."), o_name);
		return;
	}

	(void)COPY(&autopick_last_destroyed_object, o_ptr, object_type);
	o_ptr->marked |= OM_AUTODESTROY;
	player_ptr->update |= PU_AUTODESTROY;
}


/*
 *  Auto-destroy marked item
 */
static void autopick_delayed_alter_aux(player_type *player_ptr, INVENTORY_IDX item)
{
	object_type *o_ptr;
	o_ptr = REF_ITEM(player_ptr, player_ptr->current_floor_ptr, item);

	if (o_ptr->k_idx == 0 || !(o_ptr->marked & OM_AUTODESTROY)) return;

	GAME_TEXT o_name[MAX_NLEN];
	object_desc(player_ptr, o_name, o_ptr, 0);
	if (item >= 0)
	{
		inven_item_increase(player_ptr, item, -(o_ptr->number));
		inven_item_optimize(player_ptr, item);
	}
	else
	{
		delete_object_idx(player_ptr, 0 - item);
	}

	msg_format(_("%sを自動破壊します。", "Auto-destroying %s."), o_name);
}


/*
 *  Auto-destroy marked items in inventry and on floor
 */
void autopick_delayed_alter(player_type *owner_ptr)
{
	INVENTORY_IDX item;

	/*
	 * Scan inventry in reverse order to prevent
	 * skipping after inven_item_optimize()
	 */
	for (item = INVEN_TOTAL - 1; item >= 0; item--)
		autopick_delayed_alter_aux(owner_ptr, item);

	floor_type *floor_ptr = owner_ptr->current_floor_ptr;
	item = floor_ptr->grid_array[owner_ptr->y][owner_ptr->x].o_idx;
	while (item)
	{
		OBJECT_IDX next = floor_ptr->o_list[item].next_o_idx;
		autopick_delayed_alter_aux(owner_ptr, -item);
		item = next;
	}
}


/*
 * Auto-inscription and/or destroy
 *
 * Auto-destroyer works only on inventory or on floor stack only when
 * requested.
 */
void autopick_alter_item(player_type *player_ptr, INVENTORY_IDX item, bool destroy)
{
	object_type *o_ptr;
	o_ptr = REF_ITEM(player_ptr, player_ptr->current_floor_ptr, item);
	int idx = is_autopick(player_ptr, o_ptr);
	auto_inscribe_item(player_ptr, o_ptr, idx);
	if (destroy && item <= INVEN_PACK)
		auto_destroy_item(player_ptr, o_ptr, idx);
}


/*
 * Automatically pickup/destroy items in this grid.
 */
void autopick_pickup_items(player_type* player_ptr, grid_type *g_ptr)
{
	OBJECT_IDX this_o_idx, next_o_idx = 0;
	for (this_o_idx = g_ptr->o_idx; this_o_idx; this_o_idx = next_o_idx)
	{
		object_type *o_ptr = &player_ptr->current_floor_ptr->o_list[this_o_idx];
		next_o_idx = o_ptr->next_o_idx;
		int idx = is_autopick(player_ptr, o_ptr);
		auto_inscribe_item(player_ptr, o_ptr, idx);
		bool is_auto_pickup = idx >= 0;
		is_auto_pickup &= (autopick_list[idx].action & (DO_AUTOPICK | DO_QUERY_AUTOPICK)) != 0;
		if (!is_auto_pickup)
		{
			auto_destroy_item(player_ptr, o_ptr, idx);
			continue;
		}

		disturb(player_ptr, FALSE, FALSE);
		if (!inven_carry_okay(o_ptr))
		{
			GAME_TEXT o_name[MAX_NLEN];
			object_desc(player_ptr, o_name, o_ptr, 0);
			msg_format(_("ザックには%sを入れる隙間がない。", "You have no room for %s."), o_name);
			o_ptr->marked |= OM_NOMSG;
			continue;
		}

		if (!(autopick_list[idx].action & DO_QUERY_AUTOPICK))
		{
			py_pickup_aux(player_ptr, this_o_idx);
			continue;
		}

		char out_val[MAX_NLEN + 20];
		GAME_TEXT o_name[MAX_NLEN];
		if (o_ptr->marked & OM_NO_QUERY)
		{
			continue;
		}

		object_desc(player_ptr, o_name, o_ptr, 0);
		sprintf(out_val, _("%sを拾いますか? ", "Pick up %s? "), o_name);
		if (!get_check(out_val))
		{
			o_ptr->marked |= OM_NOMSG | OM_NO_QUERY;
			continue;
		}

		py_pickup_aux(player_ptr, this_o_idx);
	}
}


static const char autoregister_header[] = "?:$AUTOREGISTER";

/*
 *  Clear auto registered lines in the picktype.prf .
 */
static bool clear_auto_register(player_type *player_ptr)
{
	char tmp_file[1024];
	char pref_file[1024];
	char buf[1024];
	FILE *pref_fff;
	FILE *tmp_fff;
	int num = 0;
	bool autoregister = FALSE;
	bool okay = TRUE;

	path_build(pref_file, sizeof(pref_file), ANGBAND_DIR_USER, pickpref_filename(player_ptr, PT_WITH_PNAME));
	pref_fff = my_fopen(pref_file, "r");

	if (!pref_fff)
	{
		path_build(pref_file, sizeof(pref_file), ANGBAND_DIR_USER, pickpref_filename(player_ptr, PT_DEFAULT));
		pref_fff = my_fopen(pref_file, "r");
	}

	if (!pref_fff)
	{
		return TRUE;
	}

	tmp_fff = my_fopen_temp(tmp_file, sizeof(tmp_file));
	if (!tmp_fff)
	{
		fclose(pref_fff);
		msg_format(_("一時ファイル %s を作成できませんでした。", "Failed to create temporary file %s."), tmp_file);
		msg_print(NULL);
		return FALSE;
	}

	while (TRUE)
	{
		if (my_fgets(pref_fff, buf, sizeof(buf))) break;

		if (autoregister)
		{
			if (buf[0] != '#' && buf[0] != '?') num++;
			continue;
		}

		if (streq(buf, autoregister_header))
		{
			autoregister = TRUE;
		}
		else
		{
			fprintf(tmp_fff, "%s\n", buf);
		}
	}

	my_fclose(pref_fff);
	my_fclose(tmp_fff);

	if (num)
	{
		msg_format(_("以前のキャラクター用の自動設定(%d行)が残っています。",
			"Auto registered lines (%d lines) for previous character are remaining."), num);
		strcpy(buf, _("古い設定行は削除します。よろしいですか？", "These lines will be deleted.  Are you sure? "));

		if (!get_check(buf))
		{
			okay = FALSE;
			autoregister = FALSE;

			msg_print(_("エディタのカット&ペースト等を使って必要な行を避難してください。",
				"Use cut & paste of auto picker editor (_) to keep old prefs."));
		}
	}

	if (autoregister)
	{
		tmp_fff = my_fopen(tmp_file, "r");
		pref_fff = my_fopen(pref_file, "w");

		while (!my_fgets(tmp_fff, buf, sizeof(buf)))
			fprintf(pref_fff, "%s\n", buf);

		my_fclose(pref_fff);
		my_fclose(tmp_fff);
	}

	fd_kill(tmp_file);
	return okay;
}


/*
 *  Automatically register an auto-destroy preference line
 */
bool autopick_autoregister(player_type *player_ptr, object_type *o_ptr)
{
	char buf[1024];
	char pref_file[1024];
	FILE *pref_fff;
	autopick_type an_entry, *entry = &an_entry;
	int match_autopick = is_autopick(player_ptr, o_ptr);
	if (match_autopick != -1)
	{
		concptr what;
		byte act = autopick_list[match_autopick].action;
		if (act & DO_AUTOPICK) what = _("自動で拾う", "auto-pickup");
		else if (act & DO_AUTODESTROY) what = _("自動破壊する", "auto-destroy");
		else if (act & DONT_AUTOPICK) what = _("放置する", "leave on floor");
		else what = _("確認して拾う", "query auto-pickup");

		msg_format(_("そのアイテムは既に%sように設定されています。", "The object is already registered to %s."), what);
		return FALSE;
	}

	if ((object_is_known(o_ptr) && object_is_artifact(o_ptr)) ||
		((o_ptr->ident & IDENT_SENSE) &&
		(o_ptr->feeling == FEEL_TERRIBLE || o_ptr->feeling == FEEL_SPECIAL)))
	{
		GAME_TEXT o_name[MAX_NLEN];
		object_desc(player_ptr, o_name, o_ptr, 0);
		msg_format(_("%sは破壊不能だ。", "You cannot auto-destroy %s."), o_name);
		return FALSE;
	}

	if (!player_ptr->autopick_autoregister)
	{
		if (!clear_auto_register(player_ptr)) return FALSE;
	}

	path_build(pref_file, sizeof(pref_file), ANGBAND_DIR_USER, pickpref_filename(player_ptr, PT_WITH_PNAME));
	pref_fff = my_fopen(pref_file, "r");

	if (!pref_fff)
	{
		path_build(pref_file, sizeof(pref_file), ANGBAND_DIR_USER, pickpref_filename(player_ptr, PT_DEFAULT));
		pref_fff = my_fopen(pref_file, "r");
	}

	if (pref_fff)
	{
		while (TRUE)
		{
			if (my_fgets(pref_fff, buf, sizeof(buf)))
			{
				player_ptr->autopick_autoregister = FALSE;
				break;
			}

			if (streq(buf, autoregister_header))
			{
				player_ptr->autopick_autoregister = TRUE;
				break;
			}
		}

		fclose(pref_fff);
	}
	else
	{
		/*
		 * File could not be opened for reading.  Assume header not
		 * present.
		 */
		player_ptr->autopick_autoregister = FALSE;
	}

	pref_fff = my_fopen(pref_file, "a");
	if (!pref_fff)
	{
		msg_format(_("%s を開くことができませんでした。", "Failed to open %s."), pref_file);
		msg_print(NULL);
		return FALSE;
	}

	if (!player_ptr->autopick_autoregister)
	{
		fprintf(pref_fff, "%s\n", autoregister_header);

		fprintf(pref_fff, "%s\n", _("# *警告!!* 以降の行は自動登録されたものです。",
			"# *Warning!* The lines below will be deleted later."));
		fprintf(pref_fff, "%s\n", _("# 後で自動的に削除されますので、必要な行は上の方へ移動しておいてください。",
			"# Keep it by cut & paste if you need these lines for future characters."));
		player_ptr->autopick_autoregister = TRUE;
	}

	autopick_entry_from_object(player_ptr, entry, o_ptr);
	entry->action = DO_AUTODESTROY;
	add_autopick_list(entry);

	concptr tmp = autopick_line_from_entry(entry);
	fprintf(pref_fff, "%s\n", tmp);
	string_free(tmp);
	fclose(pref_fff);
	return TRUE;
}


/*
 * Describe which kind of object is Auto-picked/destroyed
 */
static void describe_autopick(char *buff, autopick_type *entry)
{
	concptr str = entry->name;
	byte act = entry->action;
	concptr insc = entry->insc;
	int i;

	bool top = FALSE;

#ifdef JP
	concptr before_str[100], body_str;
	int before_n = 0;

	body_str = "アイテム";
	if (IS_FLG(FLG_COLLECTING))
		before_str[before_n++] = "収集中で既に持っているスロットにまとめられる";

	if (IS_FLG(FLG_UNAWARE))
		before_str[before_n++] = "未鑑定でその効果も判明していない";

	if (IS_FLG(FLG_UNIDENTIFIED))
		before_str[before_n++] = "未鑑定の";

	if (IS_FLG(FLG_IDENTIFIED))
		before_str[before_n++] = "鑑定済みの";

	if (IS_FLG(FLG_STAR_IDENTIFIED))
		before_str[before_n++] = "完全に鑑定済みの";

	if (IS_FLG(FLG_BOOSTED))
	{
		before_str[before_n++] = "ダメージダイスが通常より大きい";
		body_str = "武器";
	}

	if (IS_FLG(FLG_MORE_DICE))
	{
		static char more_than_desc_str[] = "___";
		before_str[before_n++] = "ダメージダイスの最大値が";
		body_str = "武器";

		sprintf(more_than_desc_str, "%d", entry->dice);
		before_str[before_n++] = more_than_desc_str;
		before_str[before_n++] = "以上の";
	}

	if (IS_FLG(FLG_MORE_BONUS))
	{
		static char more_bonus_desc_str[] = "___";
		before_str[before_n++] = "修正値が(+";

		sprintf(more_bonus_desc_str, "%d", entry->bonus);
		before_str[before_n++] = more_bonus_desc_str;
		before_str[before_n++] = ")以上の";
	}

	if (IS_FLG(FLG_WORTHLESS))
		before_str[before_n++] = "店で無価値と判定される";

	if (IS_FLG(FLG_ARTIFACT))
	{
		before_str[before_n++] = "アーティファクトの";
		body_str = "装備";
	}

	if (IS_FLG(FLG_EGO))
	{
		before_str[before_n++] = "エゴアイテムの";
		body_str = "装備";
	}

	if (IS_FLG(FLG_GOOD))
	{
		before_str[before_n++] = "上質の";
		body_str = "装備";
	}

	if (IS_FLG(FLG_NAMELESS))
	{
		before_str[before_n++] = "エゴでもアーティファクトでもない";
		body_str = "装備";
	}

	if (IS_FLG(FLG_AVERAGE))
	{
		before_str[before_n++] = "並の";
		body_str = "装備";
	}

	if (IS_FLG(FLG_RARE))
	{
		before_str[before_n++] = "ドラゴン装備やカオス・ブレード等を含む珍しい";
		body_str = "装備";
	}

	if (IS_FLG(FLG_COMMON))
	{
		before_str[before_n++] = "ありふれた(ドラゴン装備やカオス・ブレード等の珍しい物ではない)";
		body_str = "装備";
	}

	if (IS_FLG(FLG_WANTED))
	{
		before_str[before_n++] = "ハンター事務所で賞金首とされている";
		body_str = "死体や骨";
	}

	if (IS_FLG(FLG_HUMAN))
	{
		before_str[before_n++] = "悪魔魔法で使うための人間やヒューマノイドの";
		body_str = "死体や骨";
	}

	if (IS_FLG(FLG_UNIQUE))
	{
		before_str[before_n++] = "ユニークモンスターの";
		body_str = "死体や骨";
	}

	if (IS_FLG(FLG_UNREADABLE))
	{
		before_str[before_n++] = "あなたが読めない領域の";
		body_str = "魔法書";
	}

	if (IS_FLG(FLG_REALM1))
	{
		before_str[before_n++] = "第一領域の";
		body_str = "魔法書";
	}

	if (IS_FLG(FLG_REALM2))
	{
		before_str[before_n++] = "第二領域の";
		body_str = "魔法書";
	}

	if (IS_FLG(FLG_FIRST))
	{
		before_str[before_n++] = "全4冊の内の1冊目の";
		body_str = "魔法書";
	}

	if (IS_FLG(FLG_SECOND))
	{
		before_str[before_n++] = "全4冊の内の2冊目の";
		body_str = "魔法書";
	}

	if (IS_FLG(FLG_THIRD))
	{
		before_str[before_n++] = "全4冊の内の3冊目の";
		body_str = "魔法書";
	}

	if (IS_FLG(FLG_FOURTH))
	{
		before_str[before_n++] = "全4冊の内の4冊目の";
		body_str = "魔法書";
	}

	if (IS_FLG(FLG_ITEMS))
		; /* Nothing to do */
	else if (IS_FLG(FLG_WEAPONS))
		body_str = "武器";
	else if (IS_FLG(FLG_FAVORITE_WEAPONS))
		body_str = "得意武器";
	else if (IS_FLG(FLG_ARMORS))
		body_str = "防具";
	else if (IS_FLG(FLG_MISSILES))
		body_str = "弾や矢やクロスボウの矢";
	else if (IS_FLG(FLG_DEVICES))
		body_str = "巻物や魔法棒や杖やロッド";
	else if (IS_FLG(FLG_LIGHTS))
		body_str = "光源用のアイテム";
	else if (IS_FLG(FLG_JUNKS))
		body_str = "折れた棒等のガラクタ";
	else if (IS_FLG(FLG_CORPSES))
		body_str = "死体や骨";
	else if (IS_FLG(FLG_SPELLBOOKS))
		body_str = "魔法書";
	else if (IS_FLG(FLG_HAFTED))
		body_str = "鈍器";
	else if (IS_FLG(FLG_SHIELDS))
		body_str = "盾";
	else if (IS_FLG(FLG_BOWS))
		body_str = "スリングや弓やクロスボウ";
	else if (IS_FLG(FLG_RINGS))
		body_str = "指輪";
	else if (IS_FLG(FLG_AMULETS))
		body_str = "アミュレット";
	else if (IS_FLG(FLG_SUITS))
		body_str = "鎧";
	else if (IS_FLG(FLG_CLOAKS))
		body_str = "クローク";
	else if (IS_FLG(FLG_HELMS))
		body_str = "ヘルメットや冠";
	else if (IS_FLG(FLG_GLOVES))
		body_str = "籠手";
	else if (IS_FLG(FLG_BOOTS))
		body_str = "ブーツ";

	*buff = '\0';
	if (!before_n)
		strcat(buff, "全ての");
	else for (i = 0; i < before_n && before_str[i]; i++)
		strcat(buff, before_str[i]);

	strcat(buff, body_str);

	if (*str)
	{
		if (*str == '^')
		{
			str++;
			top = TRUE;
		}

		strcat(buff, "で、名前が「");
		strncat(buff, str, 80);
		if (top)
			strcat(buff, "」で始まるもの");
		else
			strcat(buff, "」を含むもの");
	}

	if (insc)
	{
		strncat(buff, format("に「%s」", insc), 80);

		if (my_strstr(insc, "%%all"))
			strcat(buff, "(%%allは全能力を表す英字の記号で置換)");
		else if (my_strstr(insc, "%all"))
			strcat(buff, "(%allは全能力を表す記号で置換)");
		else if (my_strstr(insc, "%%"))
			strcat(buff, "(%%は追加能力を表す英字の記号で置換)");
		else if (my_strstr(insc, "%"))
			strcat(buff, "(%は追加能力を表す記号で置換)");

		strcat(buff, "と刻んで");
	}
	else
		strcat(buff, "を");

	if (act & DONT_AUTOPICK)
		strcat(buff, "放置する。");
	else if (act & DO_AUTODESTROY)
		strcat(buff, "破壊する。");
	else if (act & DO_QUERY_AUTOPICK)
		strcat(buff, "確認の後に拾う。");
	else
		strcat(buff, "拾う。");

	if (act & DO_DISPLAY)
	{
		if (act & DONT_AUTOPICK)
			strcat(buff, "全体マップ('M')で'N'を押したときに表示する。");
		else if (act & DO_AUTODESTROY)
			strcat(buff, "全体マップ('M')で'K'を押したときに表示する。");
		else
			strcat(buff, "全体マップ('M')で'M'を押したときに表示する。");
	}
	else
		strcat(buff, "全体マップには表示しない。");

#else /* JP */

	concptr before_str[20], after_str[20], which_str[20], whose_str[20], body_str;
	int before_n = 0, after_n = 0, which_n = 0, whose_n = 0;
	body_str = "items";
	if (IS_FLG(FLG_COLLECTING))
		which_str[which_n++] = "can be absorbed into an existing inventory list slot";

	if (IS_FLG(FLG_UNAWARE))
	{
		before_str[before_n++] = "unidentified";
		whose_str[whose_n++] = "basic abilities are not known";
	}

	if (IS_FLG(FLG_UNIDENTIFIED))
		before_str[before_n++] = "unidentified";

	if (IS_FLG(FLG_IDENTIFIED))
		before_str[before_n++] = "identified";

	if (IS_FLG(FLG_STAR_IDENTIFIED))
		before_str[before_n++] = "fully identified";

	if (IS_FLG(FLG_RARE))
	{
		before_str[before_n++] = "very rare";
		body_str = "equipments";
		after_str[after_n++] = "such as Dragon armor, Blades of Chaos, etc.";
	}

	if (IS_FLG(FLG_COMMON))
	{
		before_str[before_n++] = "relatively common";
		body_str = "equipments";
		after_str[after_n++] = "compared to very rare Dragon armor, Blades of Chaos, etc.";
	}

	if (IS_FLG(FLG_WORTHLESS))
	{
		before_str[before_n++] = "worthless";
		which_str[which_n++] = "can not be sold at stores";
	}

	if (IS_FLG(FLG_ARTIFACT))
	{
		before_str[before_n++] = "artifact";
	}

	if (IS_FLG(FLG_EGO))
	{
		before_str[before_n++] = "ego";
	}

	if (IS_FLG(FLG_GOOD))
	{
		body_str = "equipment";
		which_str[which_n++] = "have good quality";
	}

	if (IS_FLG(FLG_NAMELESS))
	{
		body_str = "equipment";
		which_str[which_n++] = "is neither ego-item nor artifact";
	}

	if (IS_FLG(FLG_AVERAGE))
	{
		body_str = "equipment";
		which_str[which_n++] = "have average quality";
	}

	if (IS_FLG(FLG_BOOSTED))
	{
		body_str = "weapons";
		whose_str[whose_n++] = "damage dice is bigger than normal";
	}

	if (IS_FLG(FLG_MORE_DICE))
	{
		static char more_than_desc_str[] =
			"maximum damage from dice is bigger than __";
		body_str = "weapons";

		sprintf(more_than_desc_str + sizeof(more_than_desc_str) - 3,
			"%d", entry->dice);
		whose_str[whose_n++] = more_than_desc_str;
	}

	if (IS_FLG(FLG_MORE_BONUS))
	{
		static char more_bonus_desc_str[] =
			"magical bonus is bigger than (+__)";

		sprintf(more_bonus_desc_str + sizeof(more_bonus_desc_str) - 4,
			"%d)", entry->bonus);
		whose_str[whose_n++] = more_bonus_desc_str;
	}

	if (IS_FLG(FLG_WANTED))
	{
		body_str = "corpse or skeletons";
		which_str[which_n++] = "is wanted at the Hunter's Office";
	}

	if (IS_FLG(FLG_HUMAN))
	{
		before_str[before_n++] = "humanoid";
		body_str = "corpse or skeletons";
		which_str[which_n++] = "can be used for Daemon magic";
	}

	if (IS_FLG(FLG_UNIQUE))
	{
		before_str[before_n++] = "unique monster's";
		body_str = "corpse or skeletons";
	}

	if (IS_FLG(FLG_UNREADABLE))
	{
		body_str = "spellbooks";
		after_str[after_n++] = "of different realms from yours";
	}

	if (IS_FLG(FLG_REALM1))
	{
		body_str = "spellbooks";
		after_str[after_n++] = "of your first realm";
	}

	if (IS_FLG(FLG_REALM2))
	{
		body_str = "spellbooks";
		after_str[after_n++] = "of your second realm";
	}

	if (IS_FLG(FLG_FIRST))
	{
		before_str[before_n++] = "first one of four";
		body_str = "spellbooks";
	}

	if (IS_FLG(FLG_SECOND))
	{
		before_str[before_n++] = "second one of four";
		body_str = "spellbooks";
	}

	if (IS_FLG(FLG_THIRD))
	{
		before_str[before_n++] = "third one of four";
		body_str = "spellbooks";
	}

	if (IS_FLG(FLG_FOURTH))
	{
		before_str[before_n++] = "fourth one of four";
		body_str = "spellbooks";
	}

	if (IS_FLG(FLG_ITEMS))
		; /* Nothing to do */
	else if (IS_FLG(FLG_WEAPONS))
		body_str = "weapons";
	else if (IS_FLG(FLG_FAVORITE_WEAPONS))
		body_str = "favorite weapons";
	else if (IS_FLG(FLG_ARMORS))
		body_str = "armors";
	else if (IS_FLG(FLG_MISSILES))
		body_str = "shots, arrows or crossbow bolts";
	else if (IS_FLG(FLG_DEVICES))
		body_str = "scrolls, wands, staffs or rods";
	else if (IS_FLG(FLG_LIGHTS))
		body_str = "light sources";
	else if (IS_FLG(FLG_JUNKS))
		body_str = "junk such as broken sticks";
	else if (IS_FLG(FLG_CORPSES))
		body_str = "corpses or skeletons";
	else if (IS_FLG(FLG_SPELLBOOKS))
		body_str = "spellbooks";
	else if (IS_FLG(FLG_HAFTED))
		body_str = "hafted weapons";
	else if (IS_FLG(FLG_SHIELDS))
		body_str = "shields";
	else if (IS_FLG(FLG_BOWS))
		body_str = "slings, bows or crossbows";
	else if (IS_FLG(FLG_RINGS))
		body_str = "rings";
	else if (IS_FLG(FLG_AMULETS))
		body_str = "amulets";
	else if (IS_FLG(FLG_SUITS))
		body_str = "body armors";
	else if (IS_FLG(FLG_CLOAKS))
		body_str = "cloaks";
	else if (IS_FLG(FLG_HELMS))
		body_str = "helms or crowns";
	else if (IS_FLG(FLG_GLOVES))
		body_str = "gloves";
	else if (IS_FLG(FLG_BOOTS))
		body_str = "boots";

	if (*str)
	{
		if (*str == '^')
		{
			str++;
			top = TRUE;
			whose_str[whose_n++] = "name begins with \"";
		}
		else
			which_str[which_n++] = "have \"";
	}


	if (act & DONT_AUTOPICK)
		strcpy(buff, "Leave on floor ");
	else if (act & DO_AUTODESTROY)
		strcpy(buff, "Destroy ");
	else if (act & DO_QUERY_AUTOPICK)
		strcpy(buff, "Ask to pick up ");
	else
		strcpy(buff, "Pickup ");

	if (insc)
	{
		strncat(buff, format("and inscribe \"%s\"", insc), 80);

		if (my_strstr(insc, "%all"))
			strcat(buff, ", replacing %all with code string representing all abilities,");
		else if (my_strstr(insc, "%"))
			strcat(buff, ", replacing % with code string representing extra random abilities,");

		strcat(buff, " on ");
	}

	if (!before_n)
		strcat(buff, "all ");
	else for (i = 0; i < before_n && before_str[i]; i++)
	{
		strcat(buff, before_str[i]);
		strcat(buff, " ");
	}

	strcat(buff, body_str);

	for (i = 0; i < after_n && after_str[i]; i++)
	{
		strcat(buff, " ");
		strcat(buff, after_str[i]);
	}

	for (i = 0; i < whose_n && whose_str[i]; i++)
	{
		if (i == 0)
			strcat(buff, " whose ");
		else
			strcat(buff, ", and ");

		strcat(buff, whose_str[i]);
	}

	if (*str && top)
	{
		strcat(buff, str);
		strcat(buff, "\"");
	}

	if (whose_n && which_n)
		strcat(buff, ", and ");

	for (i = 0; i < which_n && which_str[i]; i++)
	{
		if (i == 0)
			strcat(buff, " which ");
		else
			strcat(buff, ", and ");

		strcat(buff, which_str[i]);
	}

	if (*str && !top)
	{
		strncat(buff, str, 80);
		strcat(buff, "\" as part of its name");
	}
	strcat(buff, ".");

	if (act & DO_DISPLAY)
	{
		if (act & DONT_AUTOPICK)
			strcat(buff, "  Display these items when you press the N key in the full 'M'ap.");
		else if (act & DO_AUTODESTROY)
			strcat(buff, "  Display these items when you press the K key in the full 'M'ap.");
		else
			strcat(buff, "  Display these items when you press the M key in the full 'M'ap.");
	}
	else
		strcat(buff, " Not displayed in the full map.");
#endif /* JP */
}


/*
 * Read whole lines of a file to memory
 */
static concptr *read_text_lines(concptr filename)
{
	concptr *lines_list = NULL;
	FILE *fff;

	int lines = 0;
	char buf[1024];

	path_build(buf, sizeof(buf), ANGBAND_DIR_USER, filename);
	fff = my_fopen(buf, "r");
	if (!fff) return NULL;

	C_MAKE(lines_list, MAX_LINES, concptr);
	while (my_fgets(fff, buf, sizeof(buf)) == 0)
	{
		lines_list[lines++] = string_make(buf);
		if (lines >= MAX_LINES - 1) break;
	}

	if (lines == 0)
		lines_list[0] = string_make("");

	my_fclose(fff);
	return lines_list;
}


/*
 * Copy the default autopick file to the user directory
 */
static void prepare_default_pickpref(player_type *player_ptr)
{
	const concptr messages[] = {
		_("あなたは「自動拾いエディタ」を初めて起動しました。", "You have activated the Auto-Picker Editor for the first time."),
		_("自動拾いのユーザー設定ファイルがまだ書かれていないので、", "Since user pref file for autopick is not yet created,"),
		_("基本的な自動拾い設定ファイルをlib/pref/picktype.prfからコピーします。", "the default setting is loaded from lib/pref/pickpref.prf ."),
		NULL
	};

	concptr filename = pickpref_filename(player_ptr, PT_DEFAULT);
	for (int i = 0; messages[i]; i++)
	{
		msg_print(messages[i]);
	}

	msg_print(NULL);
	char buf[1024];
	path_build(buf, sizeof(buf), ANGBAND_DIR_USER, filename);
	FILE *user_fp;
	user_fp = my_fopen(buf, "w");
	if (!user_fp) return;

	fprintf(user_fp, "#***\n");
	for (int i = 0; messages[i]; i++)
	{
		fprintf(user_fp, "#***  %s\n", messages[i]);
	}

	fprintf(user_fp, "#***\n\n\n");
	path_build(buf, sizeof(buf), ANGBAND_DIR_PREF, filename);
	FILE *pref_fp;
	pref_fp = my_fopen(buf, "r");

	if (!pref_fp)
	{
		my_fclose(user_fp);
		return;
	}

	while (!my_fgets(pref_fp, buf, sizeof(buf)))
	{
		fprintf(user_fp, "%s\n", buf);
	}

	my_fclose(user_fp);
	my_fclose(pref_fp);
}

/*
 * Read an autopick prefence file to memory
 * Prepare default if no user file is found
 */
static concptr *read_pickpref_text_lines(player_type *player_ptr, int *filename_mode_p)
{
	/* Try a filename with player name */
	*filename_mode_p = PT_WITH_PNAME;
	char buf[1024];
	strcpy(buf, pickpref_filename(player_ptr, *filename_mode_p));
	concptr *lines_list;
	lines_list = read_text_lines(buf);

	if (!lines_list)
	{
		*filename_mode_p = PT_DEFAULT;
		strcpy(buf, pickpref_filename(player_ptr, *filename_mode_p));
		lines_list = read_text_lines(buf);
	}

	if (!lines_list)
	{
		prepare_default_pickpref(player_ptr);
		lines_list = read_text_lines(buf);
	}

	if (!lines_list)
	{
		C_MAKE(lines_list, MAX_LINES, concptr);
		lines_list[0] = string_make("");
	}

	return lines_list;
}


/*
 * Write whole lines of memory to a file.
 */
static bool write_text_lines(concptr filename, concptr *lines_list)
{
	char buf[1024];
	path_build(buf, sizeof(buf), ANGBAND_DIR_USER, filename);
	FILE *fff;
	fff = my_fopen(buf, "w");
	if (!fff) return FALSE;

	for (int lines = 0; lines_list[lines]; lines++)
	{
		my_fputs(fff, lines_list[lines], 1024);
	}

	my_fclose(fff);
	return TRUE;
}


/*
 * Free memory of lines_list.
 */
static void free_text_lines(concptr *lines_list)
{
	for (int lines = 0; lines_list[lines]; lines++)
	{
		string_free(lines_list[lines]);
	}

	/* free list of pointers */
	C_KILL(lines_list, MAX_LINES, concptr);
}


/*
 * Delete or insert string
 */
static void toggle_keyword(text_body_type *tb, BIT_FLAGS flg)
{
	int by1, by2;
	bool add = TRUE;
	bool fixed = FALSE;
	if (tb->mark)
	{
		by1 = MIN(tb->my, tb->cy);
		by2 = MAX(tb->my, tb->cy);
	}
	else /* if (!tb->mark) */
	{
		by1 = by2 = tb->cy;
	}

	for (int y = by1; y <= by2; y++)
	{
		autopick_type an_entry, *entry = &an_entry;
		if (!autopick_new_entry(entry, tb->lines_list[y], !fixed)) continue;

		string_free(tb->lines_list[y]);
		if (!fixed)
		{
			if (!IS_FLG(flg)) add = TRUE;
			else add = FALSE;

			fixed = TRUE;
		}

		if (FLG_NOUN_BEGIN <= flg && flg <= FLG_NOUN_END)
		{
			int i;
			for (i = FLG_NOUN_BEGIN; i <= FLG_NOUN_END; i++)
				REM_FLG(i);
		}
		else if (FLG_UNAWARE <= flg && flg <= FLG_STAR_IDENTIFIED)
		{
			int i;
			for (i = FLG_UNAWARE; i <= FLG_STAR_IDENTIFIED; i++)
				REM_FLG(i);
		}
		else if (FLG_ARTIFACT <= flg && flg <= FLG_AVERAGE)
		{
			int i;
			for (i = FLG_ARTIFACT; i <= FLG_AVERAGE; i++)
				REM_FLG(i);
		}
		else if (FLG_RARE <= flg && flg <= FLG_COMMON)
		{
			int i;
			for (i = FLG_RARE; i <= FLG_COMMON; i++)
				REM_FLG(i);
		}

		if (add) ADD_FLG(flg);
		else REM_FLG(flg);

		tb->lines_list[y] = autopick_line_from_entry_kill(entry);
		tb->dirty_flags |= DIRTY_ALL;
		tb->changed = TRUE;
	}
}


/*
 * Change command letter
 */
static void toggle_command_letter(text_body_type *tb, byte flg)
{
	autopick_type an_entry, *entry = &an_entry;
	int by1, by2, y;
	bool add = TRUE;
	bool fixed = FALSE;
	if (tb->mark)
	{
		by1 = MIN(tb->my, tb->cy);
		by2 = MAX(tb->my, tb->cy);
	}
	else /* if (!tb->mark) */
	{
		by1 = by2 = tb->cy;
	}

	for (y = by1; y <= by2; y++)
	{
		int wid = 0;

		if (!autopick_new_entry(entry, tb->lines_list[y], FALSE)) continue;

		string_free(tb->lines_list[y]);

		if (!fixed)
		{
			if (!(entry->action & flg)) add = TRUE;
			else add = FALSE;

			fixed = TRUE;
		}

		if (entry->action & DONT_AUTOPICK) wid--;
		else if (entry->action & DO_AUTODESTROY) wid--;
		else if (entry->action & DO_QUERY_AUTOPICK) wid--;
		if (!(entry->action & DO_DISPLAY)) wid--;

		if (flg != DO_DISPLAY)
		{
			entry->action &= ~(DO_AUTOPICK | DONT_AUTOPICK | DO_AUTODESTROY | DO_QUERY_AUTOPICK);
			if (add) entry->action |= flg;
			else entry->action |= DO_AUTOPICK;
		}
		else
		{
			entry->action &= ~(DO_DISPLAY);
			if (add) entry->action |= flg;
		}

		if (tb->cy == y)
		{
			if (entry->action & DONT_AUTOPICK) wid++;
			else if (entry->action & DO_AUTODESTROY) wid++;
			else if (entry->action & DO_QUERY_AUTOPICK) wid++;
			if (!(entry->action & DO_DISPLAY)) wid++;

			if (wid > 0) tb->cx++;
			if (wid < 0 && tb->cx > 0) tb->cx--;
		}

		tb->lines_list[y] = autopick_line_from_entry_kill(entry);
		tb->dirty_flags |= DIRTY_ALL;
		tb->changed = TRUE;
	}
}


/*
 * Delete or insert string
 */
static void add_keyword(text_body_type *tb, BIT_FLAGS flg)
{
	int by1, by2;
	if (tb->mark)
	{
		by1 = MIN(tb->my, tb->cy);
		by2 = MAX(tb->my, tb->cy);
	}
	else
	{
		by1 = by2 = tb->cy;
	}

	for (int y = by1; y <= by2; y++)
	{
		autopick_type an_entry, *entry = &an_entry;
		if (!autopick_new_entry(entry, tb->lines_list[y], FALSE)) continue;

		if (IS_FLG(flg))
		{
			autopick_free_entry(entry);
			continue;
		}

		string_free(tb->lines_list[y]);
		if (FLG_NOUN_BEGIN <= flg && flg <= FLG_NOUN_END)
		{
			int i;
			for (i = FLG_NOUN_BEGIN; i <= FLG_NOUN_END; i++)
				REM_FLG(i);
		}

		ADD_FLG(flg);
		tb->lines_list[y] = autopick_line_from_entry_kill(entry);
		tb->dirty_flags |= DIRTY_ALL;
		tb->changed = TRUE;
	}
}


/*
 * Check if this line is expression or not.
 * And update it if it is.
 */
static void check_expression_line(text_body_type *tb, int y)
{
	concptr s = tb->lines_list[y];

	if ((s[0] == '?' && s[1] == ':') ||
		(tb->states[y] & LSTAT_BYPASS))
	{
		tb->dirty_flags |= DIRTY_EXPRESSION;
	}
}


/*
 * Add an empty line at the last of the file
 */
static bool add_empty_line(text_body_type *tb)
{
	int num_lines;
	for (num_lines = 0; tb->lines_list[num_lines]; num_lines++);

	if (num_lines >= MAX_LINES - 2) return FALSE;
	if (!tb->lines_list[num_lines - 1][0]) return FALSE;

	tb->lines_list[num_lines] = string_make("");
	tb->dirty_flags |= DIRTY_EXPRESSION;
	tb->changed = TRUE;
	return TRUE;
}


/*
 * Insert return code and split the line
 */
static bool insert_return_code(text_body_type *tb)
{
	char buf[MAX_LINELEN];
	int i, j, num_lines;

	for (num_lines = 0; tb->lines_list[num_lines]; num_lines++);

	if (num_lines >= MAX_LINES - 2) return FALSE;
	num_lines--;

	for (; tb->cy < num_lines; num_lines--)
	{
		tb->lines_list[num_lines + 1] = tb->lines_list[num_lines];
		tb->states[num_lines + 1] = tb->states[num_lines];
	}

	for (i = j = 0; tb->lines_list[tb->cy][i] && i < tb->cx; i++)
	{
#ifdef JP
		if (iskanji(tb->lines_list[tb->cy][i]))
			buf[j++] = tb->lines_list[tb->cy][i++];
#endif
		buf[j++] = tb->lines_list[tb->cy][i];
	}

	buf[j] = '\0';
	tb->lines_list[tb->cy + 1] = string_make(&tb->lines_list[tb->cy][i]);
	string_free(tb->lines_list[tb->cy]);
	tb->lines_list[tb->cy] = string_make(buf);
	tb->dirty_flags |= DIRTY_EXPRESSION;
	tb->changed = TRUE;
	return TRUE;
}


/*
 * Choose an item and get auto-picker entry from it.
 */
static bool entry_from_choosed_object(player_type *player_ptr, autopick_type *entry)
{
	concptr q = _("どのアイテムを登録しますか? ", "Enter which item? ");
	concptr s = _("アイテムを持っていない。", "You have nothing to enter.");
	object_type *o_ptr;
	o_ptr = choose_object(player_ptr, NULL, q, s, USE_INVEN | USE_FLOOR | USE_EQUIP, 0);
	if (!o_ptr) return FALSE;

	autopick_entry_from_object(player_ptr, entry, o_ptr);
	return TRUE;
}


/*
 * Choose an item for search
 */
static bool get_object_for_search(player_type *player_ptr, object_type **o_handle, concptr *search_strp)
{
	concptr q = _("どのアイテムを検索しますか? ", "Enter which item? ");
	concptr s = _("アイテムを持っていない。", "You have nothing to enter.");
	object_type *o_ptr;
	o_ptr = choose_object(player_ptr, NULL, q, s, USE_INVEN | USE_FLOOR | USE_EQUIP, 0);
	if (!o_ptr) return FALSE;

	*o_handle = o_ptr;
	string_free(*search_strp);
	char buf[MAX_NLEN + 20];
	object_desc(player_ptr, buf, *o_handle, (OD_NO_FLAVOR | OD_OMIT_PREFIX | OD_NO_PLURAL));
	*search_strp = string_make(format("<%s>", buf));
	return TRUE;
}


/*
 * Prepare for search by destroyed object
 */
static bool get_destroyed_object_for_search(player_type *player_ptr, object_type **o_handle, concptr *search_strp)
{
	if (!autopick_last_destroyed_object.k_idx) return FALSE;

	*o_handle = &autopick_last_destroyed_object;
	string_free(*search_strp);
	char buf[MAX_NLEN + 20];
	object_desc(player_ptr, buf, *o_handle, (OD_NO_FLAVOR | OD_OMIT_PREFIX | OD_NO_PLURAL));
	*search_strp = string_make(format("<%s>", buf));
	return TRUE;
}


/*
 * Choose an item or string for search
 */
static byte get_string_for_search(player_type *player_ptr, object_type **o_handle, concptr *search_strp)
{
	/*
	 * Text color
	 * TERM_YELLOW : Overwrite mode
	 * TERM_WHITE : Insert mode
	 */
	byte color = TERM_YELLOW;
	char buf[MAX_NLEN + 20];
	const int len = 80;
	char prompt[] = _("検索(^I:持ち物 ^L:破壊された物): ", "Search key(^I:inven ^L:destroyed): ");
	int col = sizeof(prompt) - 1;
	if (*search_strp) strcpy(buf, *search_strp);
	else buf[0] = '\0';

	if (*o_handle) color = TERM_L_GREEN;

	prt(prompt, 0, 0);
	int pos = 0;
	while (TRUE)
	{
		bool back = FALSE;
		int skey;

		Term_erase(col, 0, 255);
		Term_putstr(col, 0, -1, color, buf);
		Term_gotoxy(col + pos, 0);

		skey = inkey_special(TRUE);
		switch (skey)
		{
		case SKEY_LEFT:
		case KTRL('b'):
		{
			int i = 0;
			color = TERM_WHITE;
			if (pos == 0) break;

			while (TRUE)
			{
				int next_pos = i + 1;

#ifdef JP
				if (iskanji(buf[i])) next_pos++;
#endif
				if (next_pos >= pos) break;

				i = next_pos;
			}

			pos = i;
			break;
		}

		case SKEY_RIGHT:
		case KTRL('f'):
			color = TERM_WHITE;
			if ('\0' == buf[pos]) break;

#ifdef JP
			if (iskanji(buf[pos])) pos += 2;
			else pos++;
#else
			pos++;
#endif
			break;

		case ESCAPE:
			return 0;

		case KTRL('r'):
			back = TRUE;
			/* Fall through */

		case '\n':
		case '\r':
		case KTRL('s'):
			if (*o_handle) return (back ? -1 : 1);
			string_free(*search_strp);
			*search_strp = string_make(buf);
			*o_handle = NULL;
			return (back ? -1 : 1);

		case KTRL('i'):
			return get_object_for_search(player_ptr, o_handle, search_strp);

		case KTRL('l'):
			if (get_destroyed_object_for_search(player_ptr, o_handle, search_strp))
				return 1;
			break;

		case '\010':
		{
			int i = 0;
			color = TERM_WHITE;
			if (pos == 0) break;

			while (TRUE)
			{
				int next_pos = i + 1;
#ifdef JP
				if (iskanji(buf[i])) next_pos++;
#endif
				if (next_pos >= pos) break;

				i = next_pos;
			}

			pos = i;
		}
			/* Fall through */

		case 0x7F:
		case KTRL('d'):
		{
			int dst, src;
			color = TERM_WHITE;
			if (buf[pos] == '\0') break;

			src = pos + 1;
#ifdef JP
			if (iskanji(buf[pos])) src++;
#endif
			dst = pos;
			while ('\0' != (buf[dst++] = buf[src++]));

			break;
		}

		default:
		{
			char tmp[100];
			char c;
			if (skey & SKEY_MASK) break;

			c = (char)skey;
			if (color != TERM_WHITE)
			{
				if (color == TERM_L_GREEN)
				{
					*o_handle = NULL;
					string_free(*search_strp);
					*search_strp = NULL;
				}

				buf[0] = '\0';
				color = TERM_WHITE;
			}

			strcpy(tmp, buf + pos);
#ifdef JP
			if (iskanji(c))
			{
				char next;
				inkey_base = TRUE;
				next = inkey();

				if (pos + 1 < len)
				{
					buf[pos++] = c;
					buf[pos++] = next;
				}
				else
				{
					bell();
				}
			}
			else
#endif
			{
#ifdef JP
				if (pos < len && (isprint(c) || iskana(c)))
#else
				if (pos < len && isprint(c))
#endif
				{
					buf[pos++] = c;
				}
				else
				{
					bell();
				}
			}

			buf[pos] = '\0';
			my_strcat(buf, tmp, len + 1);

			break;
		}
		}

		if (*o_handle == NULL || color == TERM_L_GREEN) continue;

		*o_handle = NULL;
		buf[0] = '\0';
		string_free(*search_strp);
		*search_strp = NULL;
	}
}


/*
 * Search next line matches for o_ptr
 */
static void search_for_object(player_type *player_ptr, text_body_type *tb, object_type *o_ptr, bool forward)
{
	autopick_type an_entry, *entry = &an_entry;
	GAME_TEXT o_name[MAX_NLEN];
	int bypassed_cy = -1;
	int i = tb->cy;
	object_desc(player_ptr, o_name, o_ptr, (OD_NO_FLAVOR | OD_OMIT_PREFIX | OD_NO_PLURAL));
	str_tolower(o_name);

	while (TRUE)
	{
		bool match;
		if (forward)
		{
			if (!tb->lines_list[++i]) break;
		}
		else
		{
			if (--i < 0) break;
		}

		if (!autopick_new_entry(entry, tb->lines_list[i], FALSE)) continue;

		match = is_autopick_aux(player_ptr, o_ptr, entry, o_name);
		autopick_free_entry(entry);
		if (!match)	continue;

		if (tb->states[i] & LSTAT_BYPASS)
		{
			if (bypassed_cy == -1) bypassed_cy = i;
			continue;
		}

		tb->cx = 0;
		tb->cy = i;
		if (bypassed_cy != -1)
		{
			tb->dirty_flags |= DIRTY_SKIP_INACTIVE;
		}

		return;
	}

	if (bypassed_cy == -1)
	{
		tb->dirty_flags |= DIRTY_NOT_FOUND;
		return;
	}

	tb->cx = 0;
	tb->cy = bypassed_cy;
	tb->dirty_flags |= DIRTY_INACTIVE;
}


/*
 * Search next line matches to the string
 */
static void search_for_string(text_body_type *tb, concptr search_str, bool forward)
{
	int bypassed_cy = -1;
	int bypassed_cx = 0;

	int i = tb->cy;
	while (TRUE)
	{
		concptr pos;
		if (forward)
		{
			if (!tb->lines_list[++i]) break;
		}
		else
		{
			if (--i < 0) break;
		}

		pos = my_strstr(tb->lines_list[i], search_str);
		if (!pos) continue;

		if ((tb->states[i] & LSTAT_BYPASS) &&
			!(tb->states[i] & LSTAT_EXPRESSION))
		{
			if (bypassed_cy == -1)
			{
				bypassed_cy = i;
				bypassed_cx = (int)(pos - tb->lines_list[i]);
			}

			continue;
		}

		tb->cx = (int)(pos - tb->lines_list[i]);
		tb->cy = i;

		if (bypassed_cy != -1)
		{
			tb->dirty_flags |= DIRTY_SKIP_INACTIVE;
		}

		return;
	}

	if (bypassed_cy == -1)
	{
		tb->dirty_flags |= DIRTY_NOT_FOUND;
		return;
	}

	tb->cx = bypassed_cx;
	tb->cy = bypassed_cy;
	tb->dirty_flags |= DIRTY_INACTIVE;
}


/*
 * Find a command by 'key'.
 */
static int get_com_id(char key)
{
	for (int i = 0; menu_data[i].name; i++)
	{
		if (menu_data[i].key == key)
		{
			return menu_data[i].com_id;
		}
	}

	return 0;
}


/*
 * Display the menu, and get a command
 */
static int do_command_menu(int level, int start)
{
	int max_len = 0;
	int col0 = 5 + level * 7;
	int row0 = 1 + level * 3;
	int menu_id_list[26];
	bool redraw = TRUE;
	char linestr[MAX_LINELEN];

	byte menu_key = 0;
	for (int i = start; menu_data[i].level >= level; i++)
	{
		int len;

		/* Ignore lower level sub menus */
		if (menu_data[i].level > level) continue;

		len = strlen(menu_data[i].name);
		if (len > max_len) max_len = len;

		menu_id_list[menu_key] = i;
		menu_key++;
	}

	while (menu_key < 26)
	{
		menu_id_list[menu_key] = -1;
		menu_key++;
	}

	int max_menu_wid = max_len + 3 + 3;

	/* Prepare box line */
	linestr[0] = '\0';
	strcat(linestr, "+");
	for (int i = 0; i < max_menu_wid + 2; i++)
	{
		strcat(linestr, "-");
	}

	strcat(linestr, "+");

	while (TRUE)
	{
		int com_id;
		char key;
		int menu_id;

		if (redraw)
		{
			int row1 = row0 + 1;
			Term_putstr(col0, row0, -1, TERM_WHITE, linestr);

			menu_key = 0;
			for (int i = start; menu_data[i].level >= level; i++)
			{
				char com_key_str[3];
				concptr str;
				if (menu_data[i].level > level) continue;

				if (menu_data[i].com_id == -1)
				{
					strcpy(com_key_str, _("▼", ">"));
				}
				else if (menu_data[i].key != -1)
				{
					com_key_str[0] = '^';
					com_key_str[1] = menu_data[i].key + '@';
					com_key_str[2] = '\0';
				}
				else
				{
					com_key_str[0] = '\0';
				}

				str = format("| %c) %-*s %2s | ", menu_key + 'a', max_len, menu_data[i].name, com_key_str);

				Term_putstr(col0, row1++, -1, TERM_WHITE, str);

				menu_key++;
			}

			Term_putstr(col0, row1, -1, TERM_WHITE, linestr);
			redraw = FALSE;
		}

		prt(format(_("(a-%c) コマンド:", "(a-%c) Command:"), menu_key + 'a' - 1), 0, 0);
		key = inkey();

		if (key == ESCAPE) return 0;

		bool is_alphabet = key >= 'a' && key <= 'z';
		if (!is_alphabet)
		{
			com_id = get_com_id(key);
			if (com_id)
			{
				return com_id;
			}

			continue;
		}

		menu_id = menu_id_list[key - 'a'];

		if (menu_id < 0) continue;

		com_id = menu_data[menu_id].com_id;

		if (com_id == -1)
		{
			com_id = do_command_menu(level + 1, menu_id + 1);

			if (com_id) return com_id;
			else redraw = TRUE;
		}
		else if (com_id)
		{
			return com_id;
		}
	}
}


static chain_str_type *new_chain_str(concptr str)
{
	chain_str_type *chain;
	size_t len = strlen(str);
	chain = (chain_str_type *)ralloc(sizeof(chain_str_type) + len * sizeof(char));
	strcpy(chain->s, str);
	chain->next = NULL;
	return chain;
}


static void kill_yank_chain(text_body_type *tb)
{
	chain_str_type *chain = tb->yank;
	tb->yank = NULL;
	tb->yank_eol = TRUE;

	while (chain)
	{
		chain_str_type *next = chain->next;
		size_t len = strlen(chain->s);

		rnfree(chain, sizeof(chain_str_type) + len * sizeof(char));

		chain = next;
	}
}


static void add_str_to_yank(text_body_type *tb, concptr str)
{
	tb->yank_eol = FALSE;
	if (NULL == tb->yank)
	{
		tb->yank = new_chain_str(str);
		return;
	}

	chain_str_type *chain;
	chain = tb->yank;

	while (TRUE)
	{
		if (!chain->next)
		{
			chain->next = new_chain_str(str);
			return;
		}

		/* Go to next */
		chain = chain->next;
	}
}


/*
 * Do work for the copy editor-command
 */
static void copy_text_to_yank(text_body_type *tb)
{
	int len = strlen(tb->lines_list[tb->cy]);
	if (tb->cx > len) tb->cx = len;

	if (!tb->mark)
	{
		tb->cx = 0;
		tb->my = tb->cy;
		tb->mx = len;
	}

	kill_yank_chain(tb);
	if (tb->my != tb->cy)
	{
		int by1 = MIN(tb->my, tb->cy);
		int by2 = MAX(tb->my, tb->cy);

		for (int y = by1; y <= by2; y++)
		{
			add_str_to_yank(tb, tb->lines_list[y]);
		}

		add_str_to_yank(tb, "");
		tb->mark = 0;
		tb->dirty_flags |= DIRTY_ALL;
		return;
	}

	char buf[MAX_LINELEN];
	int bx1 = MIN(tb->mx, tb->cx);
	int bx2 = MAX(tb->mx, tb->cx);
	if (bx2 > len) bx2 = len;

	if (bx1 == 0 && bx2 == len)
	{
		add_str_to_yank(tb, tb->lines_list[tb->cy]);
		add_str_to_yank(tb, "");
	}
	else
	{
		int end = bx2 - bx1;
		for (int i = 0; i < bx2 - bx1; i++)
		{
			buf[i] = tb->lines_list[tb->cy][bx1 + i];
		}

		buf[end] = '\0';
		add_str_to_yank(tb, buf);
	}

	tb->mark = 0;
	tb->dirty_flags |= DIRTY_ALL;
}


/*
 * Draw text
 */
static void draw_text_editor(player_type *player_ptr, text_body_type *tb)
{
	int i;
	int by1 = 0, by2 = 0;

	Term_get_size(&tb->wid, &tb->hgt);

	/*
	 * Top line (-1), description line (-3), separator (-1)
	 *  == -5
	 */
	tb->hgt -= 2 + DESCRIPT_HGT;

#ifdef JP
	/* Don't let cursor at second byte of kanji */
	for (i = 0; tb->lines_list[tb->cy][i]; i++)
		if (iskanji(tb->lines_list[tb->cy][i]))
		{
			i++;
			if (i == tb->cx)
			{
				/*
				 * Move to a correct position in the
				 * left or right
				 */
				if (i & 1) tb->cx--;
				else tb->cx++;
				break;
			}
		}
#endif
	if (tb->cy < tb->upper || tb->upper + tb->hgt <= tb->cy)
		tb->upper = tb->cy - (tb->hgt) / 2;
	if (tb->upper < 0)
		tb->upper = 0;
	if ((tb->cx < tb->left + 10 && tb->left > 0) || tb->left + tb->wid - 5 <= tb->cx)
		tb->left = tb->cx - (tb->wid) * 2 / 3;
	if (tb->left < 0)
		tb->left = 0;

	if (tb->old_wid != tb->wid || tb->old_hgt != tb->hgt)
		tb->dirty_flags |= DIRTY_SCREEN;
	else if (tb->old_upper != tb->upper || tb->old_left != tb->left)
		tb->dirty_flags |= DIRTY_ALL;

	if (tb->dirty_flags & DIRTY_SCREEN)
	{
		tb->dirty_flags |= (DIRTY_ALL | DIRTY_MODE);
		Term_clear();
	}

	if (tb->dirty_flags & DIRTY_MODE)
	{
		char buf[MAX_LINELEN];
		int sepa_length = tb->wid;
		for (i = 0; i < sepa_length; i++)
			buf[i] = '-';
		buf[i] = '\0';
		Term_putstr(0, tb->hgt + 1, sepa_length, TERM_WHITE, buf);
	}

	if (tb->dirty_flags & DIRTY_EXPRESSION)
	{
		byte state = 0;
		for (int y = 0; tb->lines_list[y]; y++)
		{
			char f;
			concptr v;
			concptr s = tb->lines_list[y];
			char *ss, *s_keep;
			int s_len;

			tb->states[y] = state;

			if (*s++ != '?') continue;
			if (*s++ != ':') continue;

			if (streq(s, "$AUTOREGISTER"))
				state |= LSTAT_AUTOREGISTER;

			s_len = strlen(s);
			ss = (char *)string_make(s);
			s_keep = ss;

			v = process_pref_file_expr(player_ptr, &ss, &f);

			if (streq(v, "0")) state |= LSTAT_BYPASS;
			else state &= ~LSTAT_BYPASS;

			C_KILL(s_keep, s_len + 1, char);

			tb->states[y] = state | LSTAT_EXPRESSION;
		}

		tb->dirty_flags |= DIRTY_ALL;
	}

	if (tb->mark)
	{
		tb->dirty_flags |= DIRTY_ALL;

		by1 = MIN(tb->my, tb->cy);
		by2 = MAX(tb->my, tb->cy);
	}

	for (i = 0; i < tb->hgt; i++)
	{
		int j;
		int leftcol = 0;
		concptr msg;
		byte color;
		int y = tb->upper + i;

		if (!(tb->dirty_flags & DIRTY_ALL) && (tb->dirty_line != y))
			continue;

		msg = tb->lines_list[y];
		if (!msg) break;

		for (j = 0; *msg; msg++, j++)
		{
			if (j == tb->left) break;
#ifdef JP
			if (j > tb->left)
			{
				leftcol = 1;
				break;
			}
			if (iskanji(*msg))
			{
				msg++;
				j++;
			}
#endif
		}

		Term_erase(0, i + 1, tb->wid);
		if (tb->states[y] & LSTAT_AUTOREGISTER)
		{
			color = TERM_L_RED;
		}
		else
		{
			if (tb->states[y] & LSTAT_BYPASS) color = TERM_SLATE;
			else color = TERM_WHITE;
		}

		if (!tb->mark || (y < by1 || by2 < y))
		{
			Term_putstr(leftcol, i + 1, tb->wid - 1, color, msg);
		}
		else if (by1 != by2)
		{
			Term_putstr(leftcol, i + 1, tb->wid - 1, TERM_YELLOW, msg);
		}
		else
		{
			int x0 = leftcol + tb->left;
			int len = strlen(tb->lines_list[tb->cy]);
			int bx1 = MIN(tb->mx, tb->cx);
			int bx2 = MAX(tb->mx, tb->cx);

			if (bx2 > len) bx2 = len;

			Term_gotoxy(leftcol, i + 1);
			if (x0 < bx1) Term_addstr(bx1 - x0, color, msg);
			if (x0 < bx2) Term_addstr(bx2 - bx1, TERM_YELLOW, msg + (bx1 - x0));
			Term_addstr(-1, color, msg + (bx2 - x0));
		}
	}

	for (; i < tb->hgt; i++)
	{
		Term_erase(0, i + 1, tb->wid);
	}

	bool is_dirty_diary = (tb->dirty_flags & (DIRTY_ALL | DIRTY_NOT_FOUND | DIRTY_NO_SEARCH)) != 0;
	bool is_updated = tb->old_cy != tb->cy || is_dirty_diary || tb->dirty_line == tb->cy;
	if (is_updated) return;

	autopick_type an_entry, *entry = &an_entry;
	concptr str1 = NULL, str2 = NULL;
	for (i = 0; i < DESCRIPT_HGT; i++)
	{
		Term_erase(0, tb->hgt + 2 + i, tb->wid);
	}

	if (tb->dirty_flags & DIRTY_NOT_FOUND)
	{
		str1 = format(_("パターンが見つかりません: %s", "Pattern not found: %s"), tb->search_str);
	}
	else if (tb->dirty_flags & DIRTY_SKIP_INACTIVE)
	{
		str1 = format(_("無効状態の行をスキップしました。(%sを検索中)",
			"Some inactive lines are skipped. (Searching %s)"), tb->search_str);
	}
	else if (tb->dirty_flags & DIRTY_INACTIVE)
	{
		str1 = format(_("無効状態の行だけが見付かりました。(%sを検索中)",
			"Found only an inactive line. (Searching %s)"), tb->search_str);
	}
	else if (tb->dirty_flags & DIRTY_NO_SEARCH)
	{
		str1 = _("検索するパターンがありません(^S で検索)。", "No pattern to search. (Press ^S to search.)");
	}
	else if (tb->lines_list[tb->cy][0] == '#')
	{
		str1 = _("この行はコメントです。", "This line is a comment.");
	}
	else if (tb->lines_list[tb->cy][0] && tb->lines_list[tb->cy][1] == ':')
	{
		switch (tb->lines_list[tb->cy][0])
		{
		case '?':
			str1 = _("この行は条件分岐式です。", "This line is a Conditional Expression.");
			break;
		case 'A':
			str1 = _("この行はマクロの実行内容を定義します。", "This line defines a Macro action.");
			break;
		case 'P':
			str1 = _("この行はマクロのトリガー・キーを定義します。", "This line defines a Macro trigger key.");
			break;
		case 'C':
			str1 = _("この行はキー配置を定義します。", "This line defines a Keymap.");
			break;
		}

		switch (tb->lines_list[tb->cy][0])
		{
		case '?':
			if (tb->states[tb->cy] & LSTAT_BYPASS)
			{
				str2 = _("現在の式の値は「偽(=0)」です。", "The expression is 'False'(=0) currently.");
			}
			else
			{
				str2 = _("現在の式の値は「真(=1)」です。", "The expression is 'True'(=1) currently.");
			}
			break;

		default:
			if (tb->states[tb->cy] & LSTAT_AUTOREGISTER)
			{
				str2 = _("この行は後で削除されます。", "This line will be delete later.");
			}

			else if (tb->states[tb->cy] & LSTAT_BYPASS)
			{
				str2 = _("この行は現在は無効な状態です。", "This line is bypassed currently.");
			}
			break;
		}
	}
	else if (autopick_new_entry(entry, tb->lines_list[tb->cy], FALSE))
	{
		char buf[MAX_LINELEN];
		char temp[MAX_LINELEN];
		concptr t;

		describe_autopick(buf, entry);

		if (tb->states[tb->cy] & LSTAT_AUTOREGISTER)
		{
			strcat(buf, _("この行は後で削除されます。", "  This line will be delete later."));
		}

		if (tb->states[tb->cy] & LSTAT_BYPASS)
		{
			strcat(buf, _("この行は現在は無効な状態です。", "  This line is bypassed currently."));
		}

		roff_to_buf(buf, 81, temp, sizeof(temp));
		t = temp;
		for (i = 0; i < 3; i++)
		{
			if (t[0] == 0)
				break;
			else
			{
				prt(t, tb->hgt + 1 + 1 + i, 0);
				t += strlen(t) + 1;
			}
		}
		autopick_free_entry(entry);
	}

	if (str1) prt(str1, tb->hgt + 1 + 1, 0);
	if (str2) prt(str2, tb->hgt + 1 + 2, 0);
}


/*
 * Kill segment of a line
 */
static void kill_line_segment(text_body_type *tb, int y, int x0, int x1, bool whole)
{
	concptr s = tb->lines_list[y];
	if (whole && x0 == 0 && s[x1] == '\0' && tb->lines_list[y + 1])
	{
		string_free(tb->lines_list[y]);

		int i;
		for (i = y; tb->lines_list[i + 1]; i++)
			tb->lines_list[i] = tb->lines_list[i + 1];
		tb->lines_list[i] = NULL;

		tb->dirty_flags |= DIRTY_EXPRESSION;

		return;
	}

	if (x0 == x1) return;

	char buf[MAX_LINELEN];
	char *d = buf;
	for (int x = 0; x < x0; x++)
		*(d++) = s[x];

	for (int x = x1; s[x]; x++)
		*(d++) = s[x];

	*d = '\0';
	string_free(tb->lines_list[y]);
	tb->lines_list[y] = string_make(buf);
	check_expression_line(tb, y);
	tb->changed = TRUE;
}


/*
 * Get a trigger key and insert ASCII string for the trigger
 */
static bool insert_macro_line(text_body_type *tb)
{
	int i, n = 0;
	flush();
	inkey_base = TRUE;
	i = inkey();
	char buf[1024];
	while (i)
	{
		buf[n++] = (char)i;
		inkey_base = TRUE;
		inkey_scan = TRUE;
		i = inkey();
	}

	buf[n] = '\0';
	flush();

	char tmp[1024];
	ascii_to_text(tmp, buf);
	if (!tmp[0]) return FALSE;

	tb->cx = 0;
	insert_return_code(tb);
	string_free(tb->lines_list[tb->cy]);
	tb->lines_list[tb->cy] = string_make(format("P:%s", tmp));

	i = macro_find_exact(buf);
	if (i == -1)
	{
		tmp[0] = '\0';
	}
	else
	{
		ascii_to_text(tmp, macro__act[i]);
	}

	insert_return_code(tb);
	string_free(tb->lines_list[tb->cy]);
	tb->lines_list[tb->cy] = string_make(format("A:%s", tmp));

	return TRUE;
}


/*
 * Get a command key and insert ASCII string for the key
 */
static bool insert_keymap_line(text_body_type *tb)
{
	BIT_FLAGS mode;
	if (rogue_like_commands)
	{
		mode = KEYMAP_MODE_ROGUE;
	}
	else
	{
		mode = KEYMAP_MODE_ORIG;
	}

	flush();
	char buf[2];
	buf[0] = inkey();
	buf[1] = '\0';

	flush();
	char tmp[1024];
	ascii_to_text(tmp, buf);
	if (!tmp[0]) return FALSE;

	tb->cx = 0;
	insert_return_code(tb);
	string_free(tb->lines_list[tb->cy]);
	tb->lines_list[tb->cy] = string_make(format("C:%d:%s", mode, tmp));

	concptr act = keymap_act[mode][(byte)(buf[0])];
	if (act)
	{
		ascii_to_text(tmp, act);
	}

	insert_return_code(tb);
	string_free(tb->lines_list[tb->cy]);
	tb->lines_list[tb->cy] = string_make(format("A:%s", tmp));

	return TRUE;
}


/*
 * Execute a single editor command
 */
static bool do_editor_command(player_type *player_ptr, text_body_type *tb, int com_id)
{
	switch (com_id)
	{
	case EC_QUIT:
		if (tb->changed)
		{
			if (!get_check(_("全ての変更を破棄してから終了します。よろしいですか？ ",
				"Discard all changes and quit. Are you sure? "))) break;
		}

		return QUIT_WITHOUT_SAVE;

	case EC_SAVEQUIT:
		return QUIT_AND_SAVE;

	case EC_REVERT:
		if (!get_check(_("全ての変更を破棄して元の状態に戻します。よろしいですか？ ",
			"Discard all changes and revert to original file. Are you sure? "))) break;

		free_text_lines(tb->lines_list);
		tb->lines_list = read_pickpref_text_lines(player_ptr, &tb->filename_mode);
		tb->dirty_flags |= DIRTY_ALL | DIRTY_MODE | DIRTY_EXPRESSION;
		tb->cx = tb->cy = 0;
		tb->mark = 0;

		tb->changed = FALSE;
		break;

	case EC_HELP:
		(void)show_file(player_ptr, TRUE, _("jeditor.txt", "editor.txt"), NULL, 0, 0);
		tb->dirty_flags |= DIRTY_SCREEN;

		break;

	case EC_RETURN:
		if (tb->mark)
		{
			tb->mark = 0;
			tb->dirty_flags |= DIRTY_ALL;
		}

		insert_return_code(tb);
		tb->cy++;
		tb->cx = 0;

		tb->dirty_flags |= DIRTY_ALL;
		break;

	case EC_LEFT:
	{
		if (0 < tb->cx)
		{
			int len;
#ifdef JP
			int i;
#endif
			tb->cx--;
			len = strlen(tb->lines_list[tb->cy]);
			if (len < tb->cx) tb->cx = len;
#ifdef JP
			for (i = 0; tb->lines_list[tb->cy][i]; i++)
			{
				if (iskanji(tb->lines_list[tb->cy][i]))
				{
					i++;
					if (i == tb->cx)
					{
						tb->cx--;
						break;
					}
				}
			}
#endif
		}
		else if (tb->cy > 0)
		{
			tb->cy--;
			tb->cx = strlen(tb->lines_list[tb->cy]);
		}

		break;
	}
	case EC_DOWN:
		if (!tb->lines_list[tb->cy + 1])
		{
			if (!add_empty_line(tb)) break;
		}

		tb->cy++;
		break;

	case EC_UP:
		if (tb->cy > 0) tb->cy--;
		break;

	case EC_RIGHT:
	{
#ifdef JP
		if (iskanji(tb->lines_list[tb->cy][tb->cx])) tb->cx++;
#endif
		tb->cx++;
		int len = strlen(tb->lines_list[tb->cy]);
		if (len < tb->cx)
		{
			tb->cx = len;
			if (!tb->lines_list[tb->cy + 1])
			{
				if (!add_empty_line(tb)) break;
			}

			tb->cy++;
			tb->cx = 0;
		}

		break;
	}
	case EC_BOL:
		tb->cx = 0;
		break;

	case EC_EOL:
		tb->cx = strlen(tb->lines_list[tb->cy]);
		break;

	case EC_PGUP:
		while (0 < tb->cy && tb->upper <= tb->cy)
			tb->cy--;
		while (0 < tb->upper && tb->cy + 1 < tb->upper + tb->hgt)
			tb->upper--;
		break;

	case EC_PGDOWN:
		while (tb->cy < tb->upper + tb->hgt)
		{
			if (!tb->lines_list[tb->cy + 1])
			{
				if (!add_empty_line(tb)) break;
			}

			tb->cy++;
		}

		tb->upper = tb->cy;
		break;

	case EC_TOP:
		tb->cy = 0;
		break;

	case EC_BOTTOM:
		while (TRUE)
		{
			if (!tb->lines_list[tb->cy + 1])
			{
				if (!add_empty_line(tb)) break;
			}

			tb->cy++;
		}

		tb->cx = 0;
		break;

	case EC_CUT:
	{
		copy_text_to_yank(tb);
		if (tb->my == tb->cy)
		{
			int bx1 = MIN(tb->mx, tb->cx);
			int bx2 = MAX(tb->mx, tb->cx);
			int len = strlen(tb->lines_list[tb->cy]);
			if (bx2 > len) bx2 = len;

			kill_line_segment(tb, tb->cy, bx1, bx2, TRUE);
			tb->cx = bx1;
		}
		else
		{
			int by1 = MIN(tb->my, tb->cy);
			int by2 = MAX(tb->my, tb->cy);

			for (int y = by2; y >= by1; y--)
			{
				int len = strlen(tb->lines_list[y]);

				kill_line_segment(tb, y, 0, len, TRUE);
			}

			tb->cy = by1;
			tb->cx = 0;
		}

		tb->mark = 0;
		tb->dirty_flags |= DIRTY_ALL;
		tb->changed = TRUE;
		break;
	}
	case EC_COPY:
		copy_text_to_yank(tb);

		/*
		 * Move cursor position to the end of the selection
		 *
		 * Pressing ^C ^V correctly duplicates the selection.
		 */
		if (tb->my != tb->cy)
		{
			tb->cy = MAX(tb->cy, tb->my);
			if (!tb->lines_list[tb->cy + 1])
			{
				if (!add_empty_line(tb)) break;
			}

			tb->cy++;
			break;
		}

		tb->cx = MAX(tb->cx, tb->mx);
		if (!tb->lines_list[tb->cy][tb->cx])
		{
			if (!tb->lines_list[tb->cy + 1])
			{
				if (!add_empty_line(tb)) break;
			}

			tb->cy++;
			tb->cx = 0;
		}

		break;

	case EC_PASTE:
	{
		chain_str_type *chain = tb->yank;
		int len = strlen(tb->lines_list[tb->cy]);
		if (!chain) break;
		if (tb->cx > len) tb->cx = len;

		if (tb->mark)
		{
			tb->mark = 0;
			tb->dirty_flags |= DIRTY_ALL;
		}

		while (chain)
		{
			concptr yank_str = chain->s;
			char buf[MAX_LINELEN];
			int i;
			char rest[MAX_LINELEN], *rest_ptr = rest;
			for (i = 0; i < tb->cx; i++)
				buf[i] = tb->lines_list[tb->cy][i];

			strcpy(rest, &(tb->lines_list[tb->cy][i]));
			while (*yank_str && i < MAX_LINELEN - 1)
			{
				buf[i++] = *yank_str++;
			}

			buf[i] = '\0';
			chain = chain->next;
			if (chain || tb->yank_eol)
			{
				insert_return_code(tb);
				string_free(tb->lines_list[tb->cy]);
				tb->lines_list[tb->cy] = string_make(buf);
				tb->cx = 0;
				tb->cy++;

				continue;
			}

			tb->cx = strlen(buf);
			while (*rest_ptr && i < MAX_LINELEN - 1)
			{
				buf[i++] = *rest_ptr++;
			}

			buf[i] = '\0';
			string_free(tb->lines_list[tb->cy]);
			tb->lines_list[tb->cy] = string_make(buf);
			break;
		}

		tb->dirty_flags |= DIRTY_ALL;
		tb->dirty_flags |= DIRTY_EXPRESSION;
		tb->changed = TRUE;
		break;
	}
	case EC_BLOCK:
	{
		if (tb->mark)
		{
			tb->mark = 0;
			tb->dirty_flags |= DIRTY_ALL;
			break;
		}

		tb->mark = MARK_MARK;
		if (com_id == tb->old_com_id)
		{
			int tmp = tb->cy;
			tb->cy = tb->my;
			tb->my = tmp;
			tmp = tb->cx;
			tb->cx = tb->mx;
			tb->mx = tmp;
			tb->dirty_flags |= DIRTY_ALL;
			break;
		}

		int len = strlen(tb->lines_list[tb->cy]);

		tb->my = tb->cy;
		tb->mx = tb->cx;
		if (tb->cx > len) tb->mx = len;
		break;
	}
	case EC_KILL_LINE:
	{
		int len = strlen(tb->lines_list[tb->cy]);
		if (tb->cx > len) tb->cx = len;

		if (tb->mark)
		{
			tb->mark = 0;
			tb->dirty_flags |= DIRTY_ALL;
		}

		if (tb->old_com_id != com_id)
		{
			kill_yank_chain(tb);
			tb->yank = NULL;
		}

		if (tb->cx < len)
		{
			add_str_to_yank(tb, &(tb->lines_list[tb->cy][tb->cx]));
			kill_line_segment(tb, tb->cy, tb->cx, len, FALSE);
			tb->dirty_line = tb->cy;
			break;
		}

		if (tb->yank_eol) add_str_to_yank(tb, "");

		tb->yank_eol = TRUE;
		do_editor_command(player_ptr, tb, EC_DELETE_CHAR);
		break;
	}
	case EC_DELETE_CHAR:
	{
		if (tb->mark)
		{
			tb->mark = 0;
			tb->dirty_flags |= DIRTY_ALL;
		}

#ifdef JP
		if (iskanji(tb->lines_list[tb->cy][tb->cx])) tb->cx++;
#endif
		tb->cx++;
		int len = strlen(tb->lines_list[tb->cy]);
		if (len >= tb->cx)
		{
			do_editor_command(player_ptr, tb, EC_BACKSPACE);
			break;
		}

		if (tb->lines_list[tb->cy + 1])
		{
			tb->cy++;
			tb->cx = 0;
		}
		else
		{
			tb->cx = len;
			break;
		}

		do_editor_command(player_ptr, tb, EC_BACKSPACE);
		break;
	}
	case EC_BACKSPACE:
	{
		int len, i, j, k;
		char buf[MAX_LINELEN];
		if (tb->mark)
		{
			tb->mark = 0;
			tb->dirty_flags |= DIRTY_ALL;
		}

		len = strlen(tb->lines_list[tb->cy]);
		if (len < tb->cx) tb->cx = len;

		if (tb->cx == 0)
		{
			if (tb->cy == 0) break;
			tb->cx = strlen(tb->lines_list[tb->cy - 1]);
			strcpy(buf, tb->lines_list[tb->cy - 1]);
			strcat(buf, tb->lines_list[tb->cy]);
			string_free(tb->lines_list[tb->cy - 1]);
			string_free(tb->lines_list[tb->cy]);
			tb->lines_list[tb->cy - 1] = string_make(buf);

			for (i = tb->cy; tb->lines_list[i + 1]; i++)
				tb->lines_list[i] = tb->lines_list[i + 1];

			tb->lines_list[i] = NULL;
			tb->cy--;
			tb->dirty_flags |= DIRTY_ALL;
			tb->dirty_flags |= DIRTY_EXPRESSION;
			tb->changed = TRUE;
			break;
		}

		for (i = j = k = 0; tb->lines_list[tb->cy][i] && i < tb->cx; i++)
		{
			k = j;
#ifdef JP
			if (iskanji(tb->lines_list[tb->cy][i]))
				buf[j++] = tb->lines_list[tb->cy][i++];
#endif
			buf[j++] = tb->lines_list[tb->cy][i];
		}

		while (j > k)
		{
			tb->cx--;
			j--;
		}

		for (; tb->lines_list[tb->cy][i]; i++)
		{
			buf[j++] = tb->lines_list[tb->cy][i];
		}

		buf[j] = '\0';
		string_free(tb->lines_list[tb->cy]);
		tb->lines_list[tb->cy] = string_make(buf);
		tb->dirty_line = tb->cy;
		check_expression_line(tb, tb->cy);
		tb->changed = TRUE;
		break;
	}
	case EC_SEARCH_STR:
	{
		byte search_dir;
		tb->dirty_flags |= DIRTY_SCREEN;
		search_dir = get_string_for_search(player_ptr, &tb->search_o_ptr, &tb->search_str);

		if (!search_dir) break;

		if (search_dir == 1) do_editor_command(player_ptr, tb, EC_SEARCH_FORW);
		else do_editor_command(player_ptr, tb, EC_SEARCH_BACK);
		break;
	}
	case EC_SEARCH_FORW:
		if (tb->search_o_ptr)
		{
			search_for_object(player_ptr, tb, tb->search_o_ptr, TRUE);
			break;
		}

		if (tb->search_str && tb->search_str[0])
		{
			search_for_string(tb, tb->search_str, TRUE);
			break;
		}

		tb->dirty_flags |= DIRTY_NO_SEARCH;
		break;

	case EC_SEARCH_BACK:
		if (tb->search_o_ptr)
		{
			search_for_object(player_ptr, tb, tb->search_o_ptr, FALSE);
			break;
		}

		if (tb->search_str && tb->search_str[0])
		{
			search_for_string(tb, tb->search_str, FALSE);
			break;
		}

		tb->dirty_flags |= DIRTY_NO_SEARCH;
		break;

	case EC_SEARCH_OBJ:
		tb->dirty_flags |= DIRTY_SCREEN;

		if (!get_object_for_search(player_ptr, &tb->search_o_ptr, &tb->search_str)) break;

		do_editor_command(player_ptr, tb, EC_SEARCH_FORW);
		break;

	case EC_SEARCH_DESTROYED:
		if (!get_destroyed_object_for_search(player_ptr, &tb->search_o_ptr, &tb->search_str))
		{
			tb->dirty_flags |= DIRTY_NO_SEARCH;
			break;
		}

		do_editor_command(player_ptr, tb, EC_SEARCH_FORW);
		break;

	case EC_INSERT_OBJECT:
	{
		autopick_type an_entry, *entry = &an_entry;
		if (!entry_from_choosed_object(player_ptr, entry))
		{
			tb->dirty_flags |= DIRTY_SCREEN;
			break;
		}

		tb->cx = 0;
		insert_return_code(tb);
		string_free(tb->lines_list[tb->cy]);
		tb->lines_list[tb->cy] = autopick_line_from_entry_kill(entry);
		tb->dirty_flags |= DIRTY_SCREEN;
		break;
	}
	case EC_INSERT_DESTROYED:
		if (!tb->last_destroyed) break;

		tb->cx = 0;
		insert_return_code(tb);
		string_free(tb->lines_list[tb->cy]);
		tb->lines_list[tb->cy] = string_make(tb->last_destroyed);
		tb->dirty_flags |= DIRTY_ALL;
		tb->changed = TRUE;
		break;

	case EC_INSERT_BLOCK:
	{
		char expression[80];
		sprintf(expression, "?:[AND [EQU $RACE %s] [EQU $CLASS %s] [GEQ $LEVEL %02d]]",
#ifdef JP
			rp_ptr->E_title, cp_ptr->E_title,
#else
			rp_ptr->title, cp_ptr->title,
#endif
			player_ptr->lev);
		tb->cx = 0;
		insert_return_code(tb);
		string_free(tb->lines_list[tb->cy]);
		tb->lines_list[tb->cy] = string_make(expression);
		tb->cy++;
		insert_return_code(tb);
		string_free(tb->lines_list[tb->cy]);
		tb->lines_list[tb->cy] = string_make("?:1");
		tb->dirty_flags |= DIRTY_ALL;
		tb->changed = TRUE;
		break;
	}

	case EC_INSERT_MACRO:
		draw_text_editor(player_ptr, tb);
		Term_erase(0, tb->cy - tb->upper + 1, tb->wid);
		Term_putstr(0, tb->cy - tb->upper + 1, tb->wid - 1, TERM_YELLOW, _("P:<トリガーキー>: ", "P:<Trigger key>: "));
		if (!insert_macro_line(tb)) break;

		tb->cx = 2;
		tb->dirty_flags |= DIRTY_ALL;
		tb->changed = TRUE;
		break;

	case EC_INSERT_KEYMAP:
		draw_text_editor(player_ptr, tb);
		Term_erase(0, tb->cy - tb->upper + 1, tb->wid);
		Term_putstr(0, tb->cy - tb->upper + 1, tb->wid - 1, TERM_YELLOW,
			format(_("C:%d:<コマンドキー>: ", "C:%d:<Keypress>: "), (rogue_like_commands ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG)));

		if (!insert_keymap_line(tb)) break;

		tb->cx = 2;
		tb->dirty_flags |= DIRTY_ALL;
		tb->changed = TRUE;
		break;

	case EC_CL_AUTOPICK: toggle_command_letter(tb, DO_AUTOPICK); break;
	case EC_CL_DESTROY: toggle_command_letter(tb, DO_AUTODESTROY); break;
	case EC_CL_LEAVE: toggle_command_letter(tb, DONT_AUTOPICK); break;
	case EC_CL_QUERY: toggle_command_letter(tb, DO_QUERY_AUTOPICK); break;
	case EC_CL_NO_DISP: toggle_command_letter(tb, DO_DISPLAY); break;

	case EC_IK_UNAWARE: toggle_keyword(tb, FLG_UNAWARE); break;
	case EC_IK_UNIDENTIFIED: toggle_keyword(tb, FLG_UNIDENTIFIED); break;
	case EC_IK_IDENTIFIED: toggle_keyword(tb, FLG_IDENTIFIED); break;
	case EC_IK_STAR_IDENTIFIED: toggle_keyword(tb, FLG_STAR_IDENTIFIED); break;
	case EC_KK_WEAPONS: toggle_keyword(tb, FLG_WEAPONS); break;
	case EC_KK_FAVORITE_WEAPONS: toggle_keyword(tb, FLG_FAVORITE_WEAPONS); break;
	case EC_KK_ARMORS: toggle_keyword(tb, FLG_ARMORS); break;
	case EC_KK_MISSILES: toggle_keyword(tb, FLG_MISSILES); break;
	case EC_KK_DEVICES: toggle_keyword(tb, FLG_DEVICES); break;
	case EC_KK_LIGHTS: toggle_keyword(tb, FLG_LIGHTS); break;
	case EC_KK_JUNKS: toggle_keyword(tb, FLG_JUNKS); break;
	case EC_KK_CORPSES: toggle_keyword(tb, FLG_CORPSES); break;
	case EC_KK_SPELLBOOKS: toggle_keyword(tb, FLG_SPELLBOOKS); break;
	case EC_KK_SHIELDS: toggle_keyword(tb, FLG_SHIELDS); break;
	case EC_KK_BOWS: toggle_keyword(tb, FLG_BOWS); break;
	case EC_KK_RINGS: toggle_keyword(tb, FLG_RINGS); break;
	case EC_KK_AMULETS: toggle_keyword(tb, FLG_AMULETS); break;
	case EC_KK_SUITS: toggle_keyword(tb, FLG_SUITS); break;
	case EC_KK_CLOAKS: toggle_keyword(tb, FLG_CLOAKS); break;
	case EC_KK_HELMS: toggle_keyword(tb, FLG_HELMS); break;
	case EC_KK_GLOVES: toggle_keyword(tb, FLG_GLOVES); break;
	case EC_KK_BOOTS: toggle_keyword(tb, FLG_BOOTS); break;
	case EC_OK_COLLECTING: toggle_keyword(tb, FLG_COLLECTING); break;
	case EC_OK_BOOSTED: toggle_keyword(tb, FLG_BOOSTED); break;
	case EC_OK_MORE_DICE: toggle_keyword(tb, FLG_MORE_DICE); break;
	case EC_OK_MORE_BONUS: toggle_keyword(tb, FLG_MORE_BONUS); break;
	case EC_OK_WORTHLESS: toggle_keyword(tb, FLG_WORTHLESS); break;
	case EC_OK_ARTIFACT: toggle_keyword(tb, FLG_ARTIFACT); break;
	case EC_OK_EGO: toggle_keyword(tb, FLG_EGO); break;
	case EC_OK_GOOD: toggle_keyword(tb, FLG_GOOD); break;
	case EC_OK_NAMELESS: toggle_keyword(tb, FLG_NAMELESS); break;
	case EC_OK_AVERAGE: toggle_keyword(tb, FLG_AVERAGE); break;
	case EC_OK_RARE: toggle_keyword(tb, FLG_RARE); break;
	case EC_OK_COMMON: toggle_keyword(tb, FLG_COMMON); break;
	case EC_OK_WANTED: toggle_keyword(tb, FLG_WANTED); break;
	case EC_OK_UNIQUE: toggle_keyword(tb, FLG_UNIQUE); break;
	case EC_OK_HUMAN: toggle_keyword(tb, FLG_HUMAN); break;
	case EC_OK_UNREADABLE:
		toggle_keyword(tb, FLG_UNREADABLE);
		add_keyword(tb, FLG_SPELLBOOKS);
		break;
	case EC_OK_REALM1:
		toggle_keyword(tb, FLG_REALM1);
		add_keyword(tb, FLG_SPELLBOOKS);
		break;
	case EC_OK_REALM2:
		toggle_keyword(tb, FLG_REALM2);
		add_keyword(tb, FLG_SPELLBOOKS);
		break;
	case EC_OK_FIRST:
		toggle_keyword(tb, FLG_FIRST);
		add_keyword(tb, FLG_SPELLBOOKS);
		break;
	case EC_OK_SECOND:
		toggle_keyword(tb, FLG_SECOND);
		add_keyword(tb, FLG_SPELLBOOKS);
		break;
	case EC_OK_THIRD:
		toggle_keyword(tb, FLG_THIRD);
		add_keyword(tb, FLG_SPELLBOOKS);
		break;
	case EC_OK_FOURTH:
		toggle_keyword(tb, FLG_FOURTH);
		add_keyword(tb, FLG_SPELLBOOKS);
		break;
	}

	tb->old_com_id = com_id;
	return FALSE;
}


/*
 * Insert single letter at cursor position.
 */
static void insert_single_letter(text_body_type *tb, int key)
{
	int i, j, len;
	char buf[MAX_LINELEN];

	for (i = j = 0; tb->lines_list[tb->cy][i] && i < tb->cx; i++)
	{
		buf[j++] = tb->lines_list[tb->cy][i];
	}

#ifdef JP
	if (iskanji(key))
	{
		int next;

		inkey_base = TRUE;
		next = inkey();
		if (j + 2 < MAX_LINELEN)
		{
			buf[j++] = (char)key;
			buf[j++] = (char)next;
			tb->cx += 2;
		}
		else
			bell();
	}
	else
#endif
	{
		if (j + 1 < MAX_LINELEN)
			buf[j++] = (char)key;
		tb->cx++;
	}

	for (; tb->lines_list[tb->cy][i] && j + 1 < MAX_LINELEN; i++)
		buf[j++] = tb->lines_list[tb->cy][i];
	buf[j] = '\0';

	string_free(tb->lines_list[tb->cy]);
	tb->lines_list[tb->cy] = string_make(buf);
	len = strlen(tb->lines_list[tb->cy]);
	if (len < tb->cx) tb->cx = len;

	tb->dirty_line = tb->cy;
	check_expression_line(tb, tb->cy);
	tb->changed = TRUE;
}


/*
 * Check special key code and get a movement command id
 */
static int analyze_move_key(text_body_type *tb, int skey)
{
	int com_id;
	if (!(skey & SKEY_MASK)) return 0;

	switch (skey & ~SKEY_MOD_MASK)
	{
	case SKEY_DOWN:   com_id = EC_DOWN;   break;
	case SKEY_LEFT:   com_id = EC_LEFT;   break;
	case SKEY_RIGHT:  com_id = EC_RIGHT;  break;
	case SKEY_UP:     com_id = EC_UP;     break;
	case SKEY_PGUP:   com_id = EC_PGUP;   break;
	case SKEY_PGDOWN: com_id = EC_PGDOWN; break;
	case SKEY_TOP:    com_id = EC_TOP;    break;
	case SKEY_BOTTOM: com_id = EC_BOTTOM; break;
	default:
		return 0;
	}

	if (!(skey & SKEY_MOD_SHIFT))
	{
		/*
		 * Un-shifted cursor keys cancells
		 * selection created by shift+cursor.
		 */
		if (tb->mark & MARK_BY_SHIFT)
		{
			tb->mark = 0;
			tb->dirty_flags |= DIRTY_ALL;
		}

		return com_id;
	}

	if (tb->mark) return com_id;

	int len = strlen(tb->lines_list[tb->cy]);
	tb->mark = MARK_MARK | MARK_BY_SHIFT;
	tb->my = tb->cy;
	tb->mx = tb->cx;
	if (tb->cx > len) tb->mx = len;

	if (com_id == EC_UP || com_id == EC_DOWN)
	{
		tb->dirty_flags |= DIRTY_ALL;
	}
	else
	{
		tb->dirty_line = tb->cy;
	}

	return com_id;
}

/*
 * In-game editor of Object Auto-picker/Destoryer
 * @param player_ptr プレーヤーへの参照ポインタ
 */
void do_cmd_edit_autopick(player_type *player_ptr)
{
	static int cx_save = 0;
	static int cy_save = 0;
	text_body_type text_body, *tb = &text_body;
	autopick_type an_entry, *entry = &an_entry;
	char buf[MAX_LINELEN];
	int i;
	int key = -1;
	static s32b old_autosave_turn = 0L;
	byte quit = 0;

	tb->changed = FALSE;
	tb->cx = cx_save;
	tb->cy = cy_save;
	tb->upper = tb->left = 0;
	tb->mark = 0;
	tb->mx = tb->my = 0;
	tb->old_cy = tb->old_upper = tb->old_left = -1;
	tb->old_wid = tb->old_hgt = -1;
	tb->old_com_id = 0;

	tb->yank = NULL;
	tb->search_o_ptr = NULL;
	tb->search_str = NULL;
	tb->last_destroyed = NULL;
	tb->dirty_flags = DIRTY_ALL | DIRTY_MODE | DIRTY_EXPRESSION;
	tb->dirty_line = -1;
	tb->filename_mode = PT_DEFAULT;

	if (current_world_ptr->game_turn < old_autosave_turn)
	{
		while (old_autosave_turn > current_world_ptr->game_turn) old_autosave_turn -= TURNS_PER_TICK * TOWN_DAWN;
	}

	if (current_world_ptr->game_turn > old_autosave_turn + 100L)
	{
		do_cmd_save_game(player_ptr, TRUE);
		old_autosave_turn = current_world_ptr->game_turn;
	}

	update_playtime();
	init_autopick();
	if (autopick_last_destroyed_object.k_idx)
	{
		autopick_entry_from_object(player_ptr, entry, &autopick_last_destroyed_object);
		tb->last_destroyed = autopick_line_from_entry_kill(entry);
	}

	tb->lines_list = read_pickpref_text_lines(player_ptr, &tb->filename_mode);
	for (i = 0; i < tb->cy; i++)
	{
		if (!tb->lines_list[i])
		{
			tb->cy = tb->cx = 0;
			break;
		}
	}

	screen_save();
	while (!quit)
	{
		int com_id = 0;
		draw_text_editor(player_ptr, tb);
		prt(_("(^Q:終了 ^W:セーブして終了, ESC:メニュー, その他:入力)",
			"(^Q:Quit, ^W:Save&Quit, ESC:Menu, Other:Input text)"), 0, 0);
		if (!tb->mark)
		{
			prt(format("(%d,%d)", tb->cx, tb->cy), 0, 60);
		}
		else
		{
			prt(format("(%d,%d)-(%d,%d)", tb->mx, tb->my, tb->cx, tb->cy), 0, 60);
		}

		Term_gotoxy(tb->cx - tb->left, tb->cy - tb->upper + 1);
		tb->dirty_flags = 0;
		tb->dirty_line = -1;
		tb->old_cy = tb->cy;
		tb->old_upper = tb->upper;
		tb->old_left = tb->left;
		tb->old_wid = tb->wid;
		tb->old_hgt = tb->hgt;

		key = inkey_special(TRUE);

		if (key & SKEY_MASK)
		{
			com_id = analyze_move_key(tb, key);
		}
		else if (key == ESCAPE)
		{
			com_id = do_command_menu(0, 0);
			tb->dirty_flags |= DIRTY_SCREEN;
		}
		else if (!iscntrl((unsigned char)key))
		{
			if (tb->mark)
			{
				tb->mark = 0;
				tb->dirty_flags |= DIRTY_ALL;
			}

			insert_single_letter(tb, key);
			continue;
		}
		else
		{
			com_id = get_com_id((char)key);
		}

		if (com_id) quit = do_editor_command(player_ptr, tb, com_id);
	}

	screen_load();
	strcpy(buf, pickpref_filename(player_ptr, tb->filename_mode));

	if (quit == QUIT_AND_SAVE)
		write_text_lines(buf, tb->lines_list);

	free_text_lines(tb->lines_list);
	string_free(tb->search_str);
	string_free(tb->last_destroyed);
	kill_yank_chain(tb);

	process_autopick_file(player_ptr, buf);
	current_world_ptr->start_time = (u32b)time(NULL);
	cx_save = tb->cx;
	cy_save = tb->cy;
}
