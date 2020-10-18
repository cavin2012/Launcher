/* $Id: campaign_difficulty.hpp 49603 2011-05-22 17:56:17Z mordante $ */
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

#ifndef GUI_DIALOGS_EXPLORER_HPP_INCLUDED
#define GUI_DIALOGS_EXPLORER_HPP_INCLUDED

#include "gui/dialogs/rexplorer.hpp"

#include "rdp_client.h"

namespace gui2 {

class texplorer_slot: public trexplorer::tslot
{
public:
	explicit texplorer_slot(net::RdpClientRose& rdpc);

private:
	void rexplorer_pre_show(twindow& window) override;
	void rexplorer_click_edit(tbutton& widget) override;
	void rexplorer_app_timer_handler(uint32_t now) override;

private:
	net::RdpClientRose& rdpc_;
};


}

#endif /* ! GUI_DIALOGS_EXPLORER_HPP_INCLUDED */
