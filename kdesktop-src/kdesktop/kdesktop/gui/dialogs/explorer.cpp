/* $Id: campaign_difficulty.cpp 49602 2011-05-22 17:56:13Z mordante $ */
/*
   Copyright (C) 2010 - 2011 by Ignacio Riquelme Morelle <shadowm2006@gmail.com>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

#define GETTEXT_DOMAIN "desktop-lib"

#include "gui/dialogs/explorer.hpp"

#include "formula_string_utils.hpp"
#include "gettext.hpp"
#include "filesystem.hpp"
#include "rose_config.hpp"

#include "gui/dialogs/helper.hpp"
#include "gui/widgets/settings.hpp"
#include "gui/widgets/window.hpp"
#include "gui/widgets/label.hpp"
#include "gui/widgets/button.hpp"
#include "gui/widgets/toggle_button.hpp"
#include "gui/widgets/toggle_panel.hpp"
#include "gui/widgets/listbox.hpp"
#include "gui/widgets/report.hpp"
#include "gui/widgets/text_box.hpp"
#include "gui/dialogs/combo_box.hpp"
#include "gui/dialogs/message.hpp"
#include "gui/dialogs/menu.hpp"
#include "gui/dialogs/edit_box.hpp"

#include <freerdp/channels/cliprdr_common2.hpp>

namespace gui2 {

texplorer_slot::texplorer_slot(net::RdpClientRose& rdpc)
	: rdpc_(rdpc)
{
}

void texplorer_slot::rexplorer_pre_show(twindow& window)
{
	tpanel* panel = find_widget<tpanel>(&window, "statusbar_panel", false, true);
	panel->set_visible(twidget::INVISIBLE);
}

void texplorer_slot::rexplorer_click_edit(tbutton& widget)
{
	enum {copy, cut, paste};

	std::vector<gui2::tmenu::titem> items;
	
	tlistbox* file_list_ = rexplorer->file_list();
	if (file_list_->cursel() != nullptr || !file_list_->multiselected_rows().empty()) {
		items.push_back(gui2::tmenu::titem(cliprdr_msgid_2_str(cliprdr_msgid_copy, null_str), copy));
		// items.push_back(gui2::tmenu::titem(_("Cut"), cut));
	}
	if (rdpc_.can_hdrop_paste()) {
		// paste
		items.push_back(gui2::tmenu::titem(cliprdr_msgid_2_str(cliprdr_msgid_paste, null_str), paste));
	}

	if (items.empty()) {
		gui2::show_message(null_str, cliprdr_msgid_2_str(cliprdr_msgid_nooperator, null_str));
		return;
	}

	int selected;
	{
		gui2::tmenu dlg(items, nposm);
		dlg.show(widget.get_x(), widget.get_y() + widget.get_height() + 16 * twidget::hdpi_scale);
		int retval = dlg.get_retval();
		if (dlg.get_retval() != gui2::twindow::OK) {
			return;
		}
		// absolute_draw();
		selected = dlg.selected_val();
	}

	if (selected == copy) {
		std::vector<std::string> files;
		if (file_list_->cursel() != nullptr) {
			files.push_back(rexplorer->selected_full_name(nullptr));
		} else {
			files = rexplorer->selected_full_multinames();
		}
		std::string files_str = utils::join_with_null(files);
		rdpc_.hdrop_copied(files_str);

	} else if (selected == paste) {
		// gui2::run_with_progress_widget(false, _("Paste"), boost::bind(&net::trdpd_manager::hdrop_paste, &rdpd_mgr_, _1, current_dir_), 0);
		int err_code = cliprdr_errcode_ok;
		char err_msg[512];

		const std::string title = cliprdr_msgid_2_str(cliprdr_msgid_pastingwarn, game_config::get_app_msgstr(null_str));
		gui2::run_with_progress_dlg(title, cliprdr_msgid_2_str(cliprdr_msgid_paste, null_str), boost::bind(&net::RdpClientRose::hdrop_paste, &rdpc_, _1, rexplorer->current_dir(), &err_code, err_msg, sizeof(err_msg)), 0, "misc/remove.png");
		const std::string msg = cliprdr_code_2_str(err_msg, err_code);
		if (!msg.empty()) {
			gui2::show_message(null_str, msg);
		}
		rexplorer->update_file_lists();

	} else if (selected == cut) {
		// cut_selection();
	}
}

void texplorer_slot::rexplorer_app_timer_handler(uint32_t now)
{
	//refresh_statusbar_grid();
}

}
