#ifndef GUI_DIALOGS_DESKTOP_HPP_INCLUDED
#define GUI_DIALOGS_DESKTOP_HPP_INCLUDED

#include "gui/dialogs/dialog.hpp"
#include "rtc_client.hpp"
#include "rdp_client.h"
#include "game_config.hpp"

namespace gui2 {

class tdesktop: public tdialog, public trtc_client::tadapter
{
public:
	explicit tdesktop(const trdpcookie& rdpcookie);

	void did_h264_frame(const uint8_t* data, size_t len);

	class tsend_suppress_output_lock
	{
	public:
		tsend_suppress_output_lock(rdpContext* context)
			: context_(context)
		{
			rose_send_suppress_output(context_, TRUE);
		}

		~tsend_suppress_output_lock()
		{
			rose_send_suppress_output(context_, FALSE);
		}
	private:
		rdpContext* context_;
	};

	struct tmsg
	{
		tmsg(uint32_t ticks, const std::string& msg, bool fail)
			: ticks(ticks)
			, msg(msg)
			, fail(fail)
		{}

		uint32_t ticks;
		std::string msg;
		bool fail;
	};

	struct tmsg_startup_msg: public rtc::MessageData {
		explicit tmsg_startup_msg(uint32_t ticks, const std::string& msg, bool fail, int rdpstatus)
			: ticks(ticks)
			, msg(msg)
			, fail(fail)
			, rdpstatus(rdpstatus)
		{}

		const uint32_t ticks;
		const std::string msg;
		bool fail;
		int rdpstatus;
	};
	struct tmsg_explorer: public rtc::MessageData {
	};
	
	struct tmsg_visible_percent: public rtc::MessageData {
	};
	enum {MSG_STARTUP_MSG = POST_MSG_MIN_APP, MSG_EXPLORER, MSG_VISIBLE_PERCENT};

private:
	/** Inherited from tdialog. */
	void pre_show() override;

	/** Inherited from tdialog. */
	void post_show() override;

	/** Inherited from tdialog, implemented by REGISTER_DIALOG. */
	virtual const std::string& window_id() const;

	void app_first_drawn() override;
	void app_resize_screen(const int width, const int height, bool release) override;

	texture did_create_background_tex(ttrack& widget, const SDL_Rect& draw_rect);
	void did_draw_paper(ttrack& widget, const SDL_Rect& widget_rect, const bool bg_drawn);
	void did_mouse_leave_paper(ttrack& widget, const tpoint&, const tpoint& /*last_coordinate*/);
	void did_mouse_motion_paper(ttrack& widget, const tpoint& first, const tpoint& last);
	void did_left_button_down(ttrack& widget, const tpoint& coordinate);
	void did_right_button_up(ttrack& widget, const tpoint& coordinate);
	void signal_handler_longpress_paper(bool& halt, const tpoint& coordinate);
	void signal_handler_left_button_up(const tpoint& coordinate);
	void signal_handler_right_button_down(bool& handled, bool& halt, const tpoint& coordinate);

	void signal_handler_sdl_wheel_up(bool& handled, const tpoint& coordinate);
	void signal_handler_sdl_wheel_down(bool& handled, const tpoint& coordinate);
	void signal_handler_sdl_wheel_left(bool& handled, const tpoint& coordinate);
	void signal_handler_sdl_wheel_right(bool& handled, const tpoint& coordinate);

	void start_avcapture();
	void stop_avcapture();

	std::vector<trtc_client::tusing_vidcap> app_video_capturer(int id, bool remote, const std::vector<std::string>& device_names) override;
	void app_handle_notify(int id, int ncode, int at, int64_t i64_value, const std::string& str_value) override;
	// trtc_client::VideoRenderer* app_create_video_renderer(trtc_client&, webrtc::VideoTrackInterface*, const std::string& name, bool remote, int at, bool encode) override;
	void did_draw_slice(int id, SDL_Renderer* renderer, trtc_client::VideoRenderer** locals, int locals_count, trtc_client::VideoRenderer** remotes, int remotes_count, const SDL_Rect& draw_rect) override;

	void load_texture();
	void load_button_surface();
	enum {bar_main, bar_minimap, bar_count};
	struct tbar
	{
		tbar(int type, int size)
			: type(type)
			, size(size)
			, rect(empty_rect)
			, delta(SDL_Point{0, 0})
			, visible(false)
			, gap(10 * twidget::hdpi_scale)
		{}

		const int type;
		const int size;
		SDL_Rect rect;
		SDL_Point delta;
		texture tex;
		bool visible;
		int gap;
		std::vector<int> btns;
	};
	void refresh_bar(tbar& bar);
	void calculate_bar_ptr();
	void click_visible_percent(int x, int y);

	enum {barchg_connectionfinished, barchg_min = barchg_connectionfinished, barchg_closed, barchg_count};
	void handle_bar_changed(int type);
	bool btn_in_current_bar(int btn) const;

	void insert_startup_msg(uint32_t ticks, const std::string& msg, bool fail);
	void create_startup_msgs_tex(SDL_Renderer* renderer);

	// rdp relative
	SDL_Point wf_scale_mouse_pos(const SDL_Rect& rdpc_rect, int x, int y);
	void wf_scale_mouse_event(ttrack& widget, uint16_t flags, int x, int y);
	void did_rdp_client_connectionfinished();
	void did_rdp_client_reset();
	bool send_extra_mouse_up_event(ttrack& widget, const tpoint& coordinate);

	void app_OnMessage(rtc::Message* msg) override;

private:
	const trdpcookie rdpcookie_;
	ttrack* paper_;
	std::unique_ptr<tmemcapture> avcapture_;
	tdisable_up_result_swipe disable_up_result_swipe_;
	const bool ratio_switchable_;

	class tsetting_lock
	{
	public:
		tsetting_lock(tdesktop& home)
			: home_(home)
		{
			VALIDATE(!home_.setting_, null_str);
			home_.setting_ = true;
		}
		~tsetting_lock()
		{
			home_.setting_ = false;
		}

	private:
		tdesktop& home_;
	};
	bool setting_;

	std::unique_ptr<net::trdp_manager> rdp_client_;

	texture startup_msgs_tex_;
	std::vector<tmsg> startup_msgs_;
	bool avcapture_ready_;
	twebrtc_send_helper send_helper_;

	const int mainbar_y_offset_;

	rdpContext* rdp_context_;
	SDL_Rect rdpd_crop_;
	SDL_Rect rdpc_rect_;

	enum {btn_mode, btn_off, btn_copy, btn_margin, btn_minimap, btn_percent, btn_count};
	std::map<int, surface> btn_surfs_;

	std::vector<tbar> bars_;
	bool require_calculate_bar_xy_;

	const int margin_height_;
	bool margining_;

	tbar* moving_bar_;
	int moving_btn_;
	tbar* scrolling_bar_;
	uint32_t will_longpress_ticks_;
	SDL_Point scroll_delta_;
	tbar* main_bar_;
	tbar* minimap_bar_;
	SDL_Rect minimap_dst_;
	texture percent_tex_;
	SDL_Rect percent_rect_;
	bool maybe_percent_;
	texture move_tex_;
	texture mask_tex_;
	int bar_change_;
	texture wall_tex_;

	int screen_mode_;
	std::string screen_mode_msg_;
	texture screen_mode_tex_;
	const int screen_mode_show_threshold_;
	uint32_t hide_screen_mode_ticks_;
	int visible_percent_;
};

} // namespace gui2

#endif

