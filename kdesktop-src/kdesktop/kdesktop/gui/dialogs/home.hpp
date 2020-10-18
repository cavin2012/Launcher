#ifndef GUI_DIALOGS_HOME_HPP_INCLUDED
#define GUI_DIALOGS_HOME_HPP_INCLUDED

#include "gui/dialogs/dialog.hpp"
#include "game_config.hpp"

namespace gui2 {

class tbutton;
class ttuggle_button;
class ttext_box;

class thome: public tdialog
{
public:
	enum tresult {SCAN = 1, DESKTOP};
	explicit thome(tpbremotes& pbremotes);

	const trdpcookie& rdpcookie() const { return rdpcookie_; }

private:
	/** Inherited from tdialog. */
	void pre_show() override;

	/** Inherited from tdialog. */
	void post_show() override;

	/** Inherited from tdialog, implemented by REGISTER_DIALOG. */
	virtual const std::string& window_id() const;

	void click_rdp(tbutton& widget);
	void click_scan(tbutton& widget);

	void did_ratio_switchable_changed(ttoggle_button& widget);
	void did_text_box_changed(ttext_box& widget);

	void reload_devices(tpbremotes& pbremotes);
	bool can_rdp() const;

private:
	tpbremotes& pbremotes_;
	ttext_box* ipaddr_;
	trdpcookie rdpcookie_;
};

} // namespace gui2

#endif

