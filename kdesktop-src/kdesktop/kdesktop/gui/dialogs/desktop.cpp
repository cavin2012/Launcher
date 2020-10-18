#define GETTEXT_DOMAIN "kdesktop-lib"

#include "gui/dialogs/desktop.hpp"

#include "gui/widgets/label.hpp"
#include "gui/widgets/track.hpp"
#include "gui/widgets/window.hpp"
#include "gui/dialogs/explorer.hpp"
#include "gui/dialogs/menu.hpp"
#include "gettext.hpp"
#include "formula_string_utils.hpp"
#include "font.hpp"
#include "filesystem.hpp"

#include <boost/bind.hpp>

extern std::string format_elapse_smsec(time_t elapse_ms);

void did_h264_frame(void* custom, const uint8_t* data, size_t len)
{
	if (game_config::os == os_windows) {
		static int time = 0;
		SDL_Log("#%u[%u]did_h264_frame, data: %p, len: %u", time ++, SDL_GetTicks(), data, (uint32_t)len);
	}
	tmemcapture* accapture = reinterpret_cast<tmemcapture*>(custom);
	accapture->decode(0, data, len);
}

namespace gui2 {

REGISTER_DIALOG(kdesktop, desktop)

tdesktop::tdesktop(const trdpcookie& rdpcookie)
	: rdpcookie_(rdpcookie)
	, paper_(nullptr)
	, setting_(false)
	, avcapture_ready_(false)
	, send_helper_(this)
	, mainbar_y_offset_(16 * twidget::hdpi_scale)
	, rdp_context_(nullptr)
	, rdpd_crop_(empty_rect)
	, rdpc_rect_(empty_rect)
	, require_calculate_bar_xy_(true)
	, margin_height_(64 * twidget::hdpi_scale)
	, margining_(false)
	, main_bar_(nullptr)
	, minimap_bar_(nullptr)
	, moving_bar_(nullptr)
	, moving_btn_(nposm)
	, scrolling_bar_(nullptr)
	, will_longpress_ticks_(0)
	, screen_mode_(preferences::screenmode())
	, screen_mode_show_threshold_(5000) // 5 second
	, hide_screen_mode_ticks_(0)
	, ratio_switchable_(preferences::ratioswitchable())
	, visible_percent_(preferences::visiblepercent())
	, scroll_delta_(SDL_Point{0, 0})
	, minimap_dst_(empty_rect)
	, percent_rect_(empty_rect)
	, maybe_percent_(false)
	, bar_change_(nposm)
{
	VALIDATE(rdpcookie_.valid(), null_str);

	load_texture();

	load_button_surface();

	bars_.push_back(tbar(bar_main, 48 * twidget::hdpi_scale));
	bars_.push_back(tbar(bar_minimap, 152 * twidget::hdpi_scale));
	VALIDATE((int)bars_.size() == bar_count, null_str);

	for (int at = 0; at < (int)bars_.size(); at ++) {
		tbar& bar = bars_.at(at);

		if (bar.type == bar_main) {
			bar.btns.push_back(btn_off);

		} else if (bar.type == bar_minimap) {
			bar.btns.push_back(btn_minimap);
		}

		refresh_bar(bar);
	}
	calculate_bar_ptr();
	VALIDATE(minimap_bar_->btns.size() == 1, null_str);
}

void tdesktop::pre_show()
{
	window_->set_escape_disabled(true);
	window_->set_label("misc/white-background.png");

	paper_ = find_widget<ttrack>(window_, "paper", false, true);
	paper_->set_did_draw(boost::bind(&tdesktop::did_draw_paper, this, _1, _2, _3));
	paper_->set_did_create_background_tex(boost::bind(&tdesktop::did_create_background_tex, this, _1, _2));
	paper_->set_did_mouse_leave(boost::bind(&tdesktop::did_mouse_leave_paper, this, _1, _2, _3));
	paper_->set_did_mouse_motion(boost::bind(&tdesktop::did_mouse_motion_paper, this, _1, _2, _3));
	paper_->set_did_left_button_down(boost::bind(&tdesktop::did_left_button_down, this, _1, _2));
	paper_->set_did_right_button_up(boost::bind(&tdesktop::did_right_button_up, this, _1, _2));
	paper_->connect_signal<event::LONGPRESS>(
		boost::bind(
			&tdesktop::signal_handler_longpress_paper
			, this
			, _4, _5)
		, event::tdispatcher::back_child);

	paper_->connect_signal<event::LEFT_BUTTON_UP>(boost::bind(
				&tdesktop::signal_handler_left_button_up, this, _5));

	paper_->connect_signal<event::RIGHT_BUTTON_DOWN>(boost::bind(
				&tdesktop::signal_handler_right_button_down
					, this, _3, _4, _5));

	paper_->connect_signal<event::WHEEL_UP>(
		boost::bind(
				&tdesktop::signal_handler_sdl_wheel_up
			, this
			, _3
			, _6)
		, event::tdispatcher::back_child);

	paper_->connect_signal<event::WHEEL_DOWN>(
			boost::bind(
				&tdesktop::signal_handler_sdl_wheel_down
				, this
				, _3
				, _6)
			, event::tdispatcher::back_child);

	paper_->connect_signal<event::WHEEL_LEFT>(
			boost::bind(
				&tdesktop::signal_handler_sdl_wheel_left
				, this
				, _3
				, _6)
			, event::tdispatcher::back_child);

	paper_->connect_signal<event::WHEEL_RIGHT>(
			boost::bind(
				&tdesktop::signal_handler_sdl_wheel_right
				, this
				, _3
				, _6)
			, event::tdispatcher::back_child);
}

void tdesktop::post_show()
{
	stop_avcapture();
	paper_ = nullptr;
}

void tdesktop::app_first_drawn()
{
	start_avcapture();

	VALIDATE(avcapture_.get() != nullptr, null_str);

	// @ip1: 192.168.1.1 => 0x0101a8c0
	rdp_client_.reset(new net::trdp_manager);
	// 192.168.1.113 ==> 0x7101a8c0
	// 192.168.1.109 ==> 0x6d01a8c0
	uint32_t ip = rdpcookie_.ipv4;
	rdp_client_->start(ip, 3389, avcapture_.get(), *this, send_helper_);
}

void tdesktop::app_resize_screen(const int width, const int height, bool release)
{
	if (release) {
		stop_avcapture();

		// tdesktop will may in resize_screen
		startup_msgs_tex_ = nullptr;
		screen_mode_tex_ = nullptr;
		move_tex_ = nullptr;
		mask_tex_ = nullptr;
		wall_tex_ = nullptr;
		percent_tex_ = nullptr;

		rdpc_rect_ = empty_rect;
	} else {
		load_texture();
		start_avcapture();
	}
}

void tdesktop::start_avcapture()
{
	VALIDATE(avcapture_.get() == nullptr, null_str);
	VALIDATE(rdp_client_.get() == nullptr, null_str);

	tsetting_lock setting_lock(*this);
/*
	threading::lock lock(recognition_mutex_);

	memset(&nocamera_reporter_, 0, sizeof(nocamera_reporter_));
	memset(http_xmiter_, 0, sizeof(http_xmiter_));
	memset(compare_card_fails_, 0, sizeof(compare_card_fails_));
	camera_directs_.clear();
*/
	VALIDATE(startup_msgs_.empty(), null_str);
	VALIDATE(!avcapture_ready_, null_str);
	VALIDATE(startup_msgs_tex_.get() == nullptr, null_str);
	
	std::vector<trtsp_settings> rtsps;
	trtsp_settings settings("rdp", true, tmemcapture::mem_url);
	rtsps.push_back(settings);

	insert_startup_msg(SDL_GetTicks(), _("Start decoder"), false);
	avcapture_.reset(new tmemcapture(0, *this, *this, rtsps, true, tpoint(nposm, nposm), true));
	// remote vidcap isn't enumulated during tavcapture's contructor, so use rtsps.size().

	paper_->set_timer_interval(30);
}

void tdesktop::load_button_surface()
{
	VALIDATE(btn_surfs_.empty(), null_str);
	surface surf = image::get_image("misc/crop.png");
	VALIDATE(surf.get() != nullptr, null_str);
	btn_surfs_.insert(std::make_pair(btn_mode, surf));

	surf = image::get_image("misc/off.png");
	VALIDATE(surf.get() != nullptr, null_str);
	btn_surfs_.insert(std::make_pair(btn_off, surf));

	surf = image::get_image("misc/copy.png");
	VALIDATE(surf.get() != nullptr, null_str);
	btn_surfs_.insert(std::make_pair(btn_copy, surf));

	surf = image::get_image("misc/margin.png");
	VALIDATE(surf.get() != nullptr, null_str);
	btn_surfs_.insert(std::make_pair(btn_margin, surf));
/*
	surf = image::get_image("misc/minimap.png");
	VALIDATE(surf.get() != nullptr, null_str);
	btn_surfs_.insert(std::make_pair(btn_minimap, surf));
*/
	VALIDATE((int)btn_surfs_.size() == btn_count - 2, null_str);
}

void tdesktop::load_texture()
{
	VALIDATE(move_tex_.get() == nullptr, null_str);
	surface surf = image::get_image("misc/move.png");
	VALIDATE(surf.get() != nullptr, null_str);
	move_tex_ = SDL_CreateTextureFromSurface(get_renderer(), surf);

	VALIDATE(mask_tex_.get() == nullptr, null_str);
	surface mask_surf = create_neutral_surface(128 * twidget::hdpi_scale, 128 * twidget::hdpi_scale);
	fill_surface(mask_surf, 0x80000000);
	// adjust_surface_rect_alpha2(mask_surf, 0, ::create_rect(rect.x - video_dst.x, rect.y - video_dst.y, rect.w, rect.h), true);
	mask_tex_ = SDL_CreateTextureFromSurface(get_renderer(), mask_surf);
}

void tdesktop::refresh_bar(tbar& bar)
{
	VALIDATE(bar.size > 0 && bar.delta.x == 0 && bar.delta.y == 0, null_str);
	VALIDATE(!bar.btns.empty(), null_str);

	if (bar.type != bar_minimap) {
		VALIDATE(!bar.btns.empty(), null_str);
		surface bg_surf = create_neutral_surface(bar.btns.size() * bar.size + (bar.btns.size() - 1) * bar.gap, bar.size);
		VALIDATE(bg_surf.get(), null_str);

		const uint32_t bg_color = 0xffffc04d;
		SDL_Rect dst_rect{0, 0, bg_surf->w, bg_surf->h};
		sdl_fill_rect(bg_surf, &dst_rect, bg_color);
			
		int xoffset = 0;
		for (std::vector<int>::const_iterator it = bar.btns.begin(); it != bar.btns.end(); ++ it, xoffset += bar.size + bar.gap) {
			surface surf = btn_surfs_.find(*it)->second;
			VALIDATE(surf.get(), null_str);
			surf = scale_surface(surf, bar.size, bar.size);
			SDL_Rect dst_rect{xoffset, 0, surf->w, surf->h};
			sdl_blit(surf, nullptr, bg_surf, &dst_rect);
		}

		bar.rect = ::create_rect(nposm, nposm, bg_surf->w, bg_surf->h);
		VALIDATE(bar.rect.h == bar.size, null_str);
		bar.visible = true;
		bar.tex = SDL_CreateTextureFromSurface(get_renderer(), bg_surf);

	} else {
		bar.rect = ::create_rect(nposm, nposm, bar.size, bar.size);
		VALIDATE(bar.rect.h == bar.size, null_str);
		bar.visible = screen_mode_ == screenmode_partial;
	}
}

void tdesktop::calculate_bar_ptr()
{
	main_bar_ = nullptr;
	minimap_bar_ = nullptr;

	for (int at = 0; at < (int)bars_.size(); at ++) {
		tbar& bar = bars_.at(at);

		if (bar.type == bar_main) {
			main_bar_ = &bar;

		} else if (bar.type == bar_minimap) {
			minimap_bar_ = &bar;
		}
	}
	VALIDATE(main_bar_ != nullptr && minimap_bar_ != nullptr, null_str);
}

void tdesktop::click_visible_percent(int x, int y)
{
	VALIDATE(rdp_client_ != nullptr && screen_mode_ == screenmode_partial, null_str);
	tsend_suppress_output_lock suppress_output_lock(rdp_context_);

	std::vector<int> percents;
	percents.push_back(95);
	percents.push_back(90);
	percents.push_back(85);
	percents.push_back(80);
	percents.push_back(60);
	percents.push_back(50);
	percents.push_back(25);

	std::vector<gui2::tmenu::titem> items;
	int initial = nposm;

	std::stringstream ss;
	for (std::vector<int>::const_iterator it = percents.begin(); it != percents.end(); ++ it) {
		int percent = *it;
		ss.str("");
		ss << percent << '%';
		items.push_back(gui2::tmenu::titem(ss.str(), percent));
		if (percent == visible_percent_) {
			initial = percent;
		}
	}
	
	int selected;
	{
		gui2::tmenu dlg(items, initial);
		dlg.show(x, y);
		int retval = dlg.get_retval();
		if (dlg.get_retval() != gui2::twindow::OK) {
			return;
		}
		// absolute_draw();
		selected = dlg.selected_val();
	}

	preferences::set_visiblepercent(selected);
	visible_percent_ = selected;

	rdpc_rect_ = empty_rect;
	 // make recalculate
	rdpd_crop_ = empty_rect;
	percent_tex_ = nullptr;
}

void tdesktop::handle_bar_changed(int type)
{
	VALIDATE(type >= barchg_min && type < barchg_count, null_str);
	VALIDATE(bar_change_ == nposm || bar_change_ == type, null_str);

	std::vector<int> desire_btns;
	if (type == barchg_connectionfinished) {
		desire_btns.push_back(btn_mode);
		desire_btns.push_back(btn_off);
		desire_btns.push_back(btn_copy);
		desire_btns.push_back(btn_margin);

	} else if (type == barchg_closed) {

		desire_btns.push_back(btn_off);
	}

	tbar& bar = *main_bar_;
	if (bar.btns == desire_btns) {
		return;
	}
	bar.btns = desire_btns;
	refresh_bar(bar);

	calculate_bar_ptr();
	require_calculate_bar_xy_ = true;
	bar_change_ = nposm;
}

bool tdesktop::btn_in_current_bar(int btn) const
{
	for (std::vector<tbar>::const_iterator it = bars_.begin(); it != bars_.end(); ++ it) {
		const tbar& bar = *it;
		if (!bar.visible) {
			continue;
		}
		for (std::vector<int>::const_iterator it2 = bar.btns.begin(); it2 != bar.btns.end(); ++ it2) {
			int that = *it2;
			if (that == btn) {
				return true;
			}
		}
	}
	return false;
}

void tdesktop::did_rdp_client_connectionfinished()
{
	VALIDATE(rdp_context_ == nullptr, null_str);
	rdp_context_ = &rdp_client_->rdp_client().rdp_context;
	if (scrolling_bar_ == nullptr && moving_bar_ == nullptr) {
		handle_bar_changed(barchg_connectionfinished);
	} else {
		bar_change_ = barchg_connectionfinished;
	}
}

void tdesktop::did_rdp_client_reset()
{
	rdp_context_ = nullptr;
	rdpc_rect_ = empty_rect;
	// avcapture_ready_ = false;

	if (scrolling_bar_ == nullptr && moving_bar_ == nullptr) {
		handle_bar_changed(barchg_closed);
	} else {
		bar_change_ = barchg_closed;
	}
}

void tdesktop::stop_avcapture()
{
	VALIDATE(window_ != nullptr, null_str);

	did_rdp_client_reset();
	send_helper_.clear_msg();
	rdp_client_.reset();

	// It's almost impossible, but avoid to stop avcapture maybe spend more wdg_timer_s + 2.
	{
		tsetting_lock setting_lock(*this);
		// threading::lock lock(recognition_mutex_);
		avcapture_.reset();
		paper_->set_timer_interval(0);

		startup_msgs_.clear();
		avcapture_ready_ = false;
		startup_msgs_tex_ = nullptr;
	}
	// executor_.reset();
}

void tdesktop::did_h264_frame(const uint8_t* data, size_t len)
{
	avcapture_->decode(0, data, len);
}

void tdesktop::insert_startup_msg(uint32_t ticks, const std::string& msg, bool fail)
{
	VALIDATE_IN_MAIN_THREAD();

	startup_msgs_.push_back(tmsg(ticks, msg, fail));
	create_startup_msgs_tex(get_renderer());
}

void tdesktop::create_startup_msgs_tex(SDL_Renderer* renderer)
{
	if (startup_msgs_.empty()) {
		startup_msgs_tex_ = nullptr;
		return;
	}

	std::stringstream ss;
	uint32_t first_ticks = UINT32_MAX;
	for (std::vector<tmsg>::const_iterator it = startup_msgs_.begin(); it != startup_msgs_.end(); ++ it) {
		const tmsg& msg = *it;
		if (first_ticks == UINT32_MAX) {
			first_ticks = msg.ticks;
		}
		if (!ss.str().empty()) {
			ss << "\n";
		}
		ss << format_elapse_smsec(msg.ticks - first_ticks) << " ";
		if (msg.fail) {
			ss << ht::generate_format(msg.msg, color_to_uint32(font::BAD_COLOR));
		} else {
			ss << msg.msg;
		}
	}

	const int font_size = font::SIZE_SMALLER;
	surface text_surf = font::get_rendered_text(ss.str(), 0, font_size, font::BLACK_COLOR);
	startup_msgs_tex_ = SDL_CreateTextureFromSurface(renderer, text_surf);
}

std::vector<trtc_client::tusing_vidcap> tdesktop::app_video_capturer(int id, bool remote, const std::vector<std::string>& device_names)
{
	VALIDATE(remote && device_names.size() == 1, null_str);

	std::vector<trtc_client::tusing_vidcap> ret;
	for (std::vector<std::string>::const_iterator it = device_names.begin(); it != device_names.end(); ++ it) {
		const std::string& name = *it;
		ret.push_back(trtc_client::tusing_vidcap(name, false));
	}
	return ret;
}

void tdesktop::app_handle_notify(int id, int ncode, int at, int64_t i64_value, const std::string& str_value)
{
	VALIDATE(window_ != nullptr, null_str);
	VALIDATE(window_->drawn(), null_str);

	std::string msg;
	utils::string_map symbols;

	uint32_t now = SDL_GetTicks();
	int net_errcode = nposm;
	bool fail = false;
	if (ncode == trtc_client::ncode_startlive555finished) {
		if (at != nposm) {
			fail = i64_value == 0;
			symbols["result"] = fail? _("Fail"): _("Success");
			msg = vgettext2("Start decoder $result", symbols);
		} else {
			msg = vgettext2("Decoder is ready", symbols);
			avcapture_ready_ = true;
		}
	}

	if (!msg.empty()) {
		insert_startup_msg(SDL_GetTicks(), msg, fail);
		did_draw_paper(*paper_, paper_->get_draw_rect(), false);
	}
}

void tdesktop::did_draw_slice(int id, SDL_Renderer* renderer, trtc_client::VideoRenderer** locals, int locals_count, trtc_client::VideoRenderer** remotes, int remotes_count, const SDL_Rect& draw_rect)
{
	VALIDATE(draw_rect.w > 0 && draw_rect.h > 0, null_str);
	VALIDATE(locals_count == 0 && remotes_count == 1, null_str);

	trtc_client::VideoRenderer* sink = remotes[0];
	VALIDATE(sink->frames > 0, null_str);

	SDL_Rect dst = draw_rect;

	if (SDL_RectEmpty(&rdpc_rect_)) {
		hide_screen_mode_ticks_ = SDL_GetTicks() + screen_mode_show_threshold_;
	}
	//
	// renderer "background" section
	//
	{
		SDL_Rect draw_rect2{draw_rect.x, margining_? draw_rect.y + margin_height_: draw_rect.y,
			draw_rect.w, margining_? draw_rect.h - 2 * margin_height_: draw_rect.h};

		dst = draw_rect2;
		float widget_per_desktop = 0;
		SDL_Rect crop_rect {0, 0, sink->app_width_, sink->app_height_};
		if (screen_mode_ == screenmode_ratio) {
			tpoint ratio_size(draw_rect2.w, draw_rect2.h);
			ratio_size = calculate_adaption_ratio_size(draw_rect2.w, draw_rect2.h, sink->app_width_, sink->app_height_);
			dst = ::create_rect(draw_rect2.x + (draw_rect2.w - ratio_size.x) / 2, draw_rect2.y + (draw_rect2.h - ratio_size.y) / 2, ratio_size.x, ratio_size.y);

		} else if (screen_mode_ == screenmode_partial) {
			float ratio_hori = 1.0 * draw_rect2.w / sink->app_width_;
			float ratio_vert = 1.0 * draw_rect2.h / sink->app_height_;
			crop_rect = rdpd_crop_;
			if (ratio_hori <= ratio_vert) {
				// cut 90% from horizontal axis
				widget_per_desktop = 100.0 * draw_rect2.w / (sink->app_width_ * visible_percent_);
				int require_draw_h = 1.0 * sink->app_height_ * widget_per_desktop;
				if (crop_rect.w == 0) {
					crop_rect.x = 1.0 * sink->app_width_ * (100 - visible_percent_) / 200;
					crop_rect.w = sink->app_width_ - crop_rect.x * 2;
				}
				if (require_draw_h <= draw_rect2.h) {
					if (crop_rect.h == 0) {
						crop_rect.y = 0;
						crop_rect.h = sink->app_height_;
					}

					dst.h = require_draw_h;
					dst.y = draw_rect2.y + (draw_rect2.h - require_draw_h) / 2;

				} else {
					if (crop_rect.h == 0) {
						crop_rect.h = draw_rect2.h / widget_per_desktop;
						crop_rect.y = (sink->app_height_ - crop_rect.h) / 2;
					}
				}
				
			} else {
				// cut 90% from vertical axis
				widget_per_desktop = 100.0 * draw_rect2.h / (sink->app_height_ * visible_percent_);
				int require_draw_w = 1.0 * sink->app_width_ * widget_per_desktop;
				if (crop_rect.h == 0) {
					crop_rect.y = 1.0 * sink->app_height_ * (100 - visible_percent_) / 200;
					crop_rect.h = sink->app_height_ - crop_rect.y * 2;
				}
				if (require_draw_w <= draw_rect2.w) {
					if (crop_rect.w == 0) {
						crop_rect.x = 0;
						crop_rect.w = sink->app_width_;
					}

					dst.w = require_draw_w;
					dst.x = draw_rect2.x + (draw_rect2.w - require_draw_w) / 2;

				} else {
					if (crop_rect.w == 0) {
						crop_rect.w = draw_rect2.w / widget_per_desktop;
						crop_rect.x = (sink->app_width_ - crop_rect.w) / 2;
					}
				}
			}
			
		}
		rdpd_crop_ = crop_rect;
		rdpc_rect_ = dst;

		if (scrolling_bar_ != nullptr && screen_mode_ == screenmode_partial) {
			VALIDATE(widget_per_desktop != 0, null_str);
			// x axis
			scroll_delta_.x = scrolling_bar_->delta.x / widget_per_desktop;
			if (crop_rect.x + scroll_delta_.x < 0) {
				scroll_delta_.x = 0 - crop_rect.x;
			} else if (crop_rect.x + scroll_delta_.x + crop_rect.w > sink->app_width_) {
				scroll_delta_.x = sink->app_width_ - crop_rect.x - crop_rect.w;
			}
			crop_rect.x += scroll_delta_.x;

			// y axis
			scroll_delta_.y = scrolling_bar_->delta.y / widget_per_desktop;
			if (crop_rect.y + scroll_delta_.y < 0) {
				scroll_delta_.y = 0 - crop_rect.y;
			} else if (crop_rect.y + scroll_delta_.y + crop_rect.h > sink->app_height_) {
				scroll_delta_.y = sink->app_height_ - crop_rect.y - crop_rect.h;
			}
			crop_rect.y += scroll_delta_.y;
		}

		SDL_RendererFlip flip = SDL_FLIP_NONE; // SDL_FLIP_HORIZONTAL: SDL_FLIP_NONE;
		SDL_RenderCopyEx(renderer, sink->tex_.get(), &crop_rect, &dst, 0, NULL, flip);
	}

	if (rdp_context_ == nullptr) {
		// The connection was successful and is now disconnected
		dst = draw_rect;
		SDL_RenderCopy(renderer, mask_tex_.get(), NULL, &dst);
	}

	const uint32_t now = SDL_GetTicks();
	if (scrolling_bar_ != nullptr && will_longpress_ticks_ != 0 && now >= will_longpress_ticks_) {
		// if not motion, did_mouse_motion_paper cannot be called, so place in this timer handler.
		VALIDATE(moving_bar_ == nullptr, null_str);
		VALIDATE(moving_btn_ == nposm, null_str);
		moving_bar_ = scrolling_bar_;
		moving_btn_ = moving_bar_->btns.back();

		scrolling_bar_->delta.y = 0;
		scrolling_bar_ = nullptr;
		will_longpress_ticks_ = 0;
		maybe_percent_ = false;
	}

	if (!require_calculate_bar_xy_ && minimap_bar_->visible) {
		// mini desktop
		tpoint ratio_size(minimap_bar_->rect.w, minimap_bar_->rect.h);
		ratio_size = calculate_adaption_ratio_size(minimap_bar_->rect.w, minimap_bar_->rect.h, sink->app_width_, sink->app_height_);
		SDL_Rect desktop_dst = ::create_rect(minimap_bar_->rect.x + (minimap_bar_->rect.w - ratio_size.x) / 2, 
			minimap_bar_->rect.y + (minimap_bar_->rect.h - ratio_size.y) / 2, ratio_size.x, ratio_size.y);

		if (moving_bar_ == minimap_bar_) {
			desktop_dst.x += minimap_bar_->delta.x;
		}
		SDL_RenderCopyEx(renderer, sink->tex_.get(), nullptr, &desktop_dst, 0, NULL, SDL_FLIP_NONE);

		minimap_dst_ = desktop_dst;

		// widget background
		dst = minimap_dst_;
		render_rect_frame(renderer, dst, 0xffff0000);

		bool show_percent = true;
		if (show_percent) {
			if (percent_tex_.get() == nullptr) {
				std::stringstream msg_ss;
				msg_ss << visible_percent_ << '%';
				surface surf = font::get_rendered_text(msg_ss.str(), 0, font::SIZE_LARGEST + 10 * twidget::hdpi_scale, font::BLUE_COLOR);
				percent_tex_ = SDL_CreateTextureFromSurface(renderer, surf);
			}

			int width2, height2;
			SDL_QueryTexture(percent_tex_.get(), NULL, NULL, &width2, &height2);

			dst.x = minimap_dst_.x + minimap_dst_.w - width2;
			dst.y = minimap_dst_.y;
			dst.w = width2;
			dst.h = height2;
			SDL_RenderCopy(renderer, percent_tex_.get(), nullptr, &dst);
			percent_rect_ = dst;
		}

		// croped frame
		float ratio = 1.0 * ratio_size.x / sink->app_width_;
		dst.x = desktop_dst.x + ratio * (rdpd_crop_.x + scroll_delta_.x);
		dst.w = ratio * rdpd_crop_.w;
		dst.y = desktop_dst.y + ratio * (rdpd_crop_.y + scroll_delta_.y);
		dst.h = ratio * rdpd_crop_.h;
		render_rect_frame(renderer, dst, 0xff00ff00);

		if (moving_bar_ == minimap_bar_) {
			int width2, height2;
			SDL_QueryTexture(move_tex_.get(), NULL, NULL, &width2, &height2);

			dst = minimap_dst_;
			dst.w = width2;
			dst.h = height2;
			SDL_RenderCopy(renderer, move_tex_.get(), nullptr, &dst);
		}
	}
}

texture tdesktop::did_create_background_tex(ttrack& widget, const SDL_Rect& draw_rect)
{
	surface bg_surf = create_neutral_surface(draw_rect.w, draw_rect.h);
	uint32_t bg_color = 0xff303030;
	fill_surface(bg_surf, bg_color);

	wall_tex_ = SDL_CreateTextureFromSurface(get_renderer(), bg_surf.get());
	return wall_tex_;
}

void tdesktop::did_draw_paper(ttrack& widget, const SDL_Rect& draw_rect, const bool bg_drawn)
{
	SDL_Renderer* renderer = get_renderer();
	ttrack::tdraw_lock lock(renderer, widget);

	if (!bg_drawn) {
		SDL_RenderCopy(renderer, widget.background_texture().get(), NULL, &draw_rect);
	}

	if (avcapture_ready_) {
		avcapture_->draw_slice(renderer, draw_rect);
	}

	SDL_Rect dstrect;
	if ((SDL_RectEmpty(&rdpc_rect_) || rdp_context_ == nullptr) && startup_msgs_tex_.get()) {
		int width2, height2;
		SDL_QueryTexture(startup_msgs_tex_.get(), NULL, NULL, &width2, &height2);

		dstrect = ::create_rect(draw_rect.x, draw_rect.y + draw_rect.h - height2 - 64 * twidget::hdpi_scale, width2, height2);
		SDL_RenderCopy(renderer, startup_msgs_tex_.get(), nullptr, &dstrect);
	}

	if (require_calculate_bar_xy_) {
		for (int at = 0; at < (int)bars_.size(); at ++) {
			tbar& bar = bars_.at(at);
			VALIDATE(bar.rect.w > 0 && bar.rect.w > 0, null_str);

			if (bar.type == bar_main) {
				bar.rect.x = draw_rect.x + preferences::mainbarx();
				if (bar.rect.x < 0 || bar.rect.x + bar.rect.w > draw_rect.x + draw_rect.w) {
					bar.rect.x = draw_rect.x;
				}
				bar.rect.y = draw_rect.y + mainbar_y_offset_;
				
			} else if (bar.type == bar_minimap) {
				bar.rect.x = draw_rect.x + preferences::minimapbarx();
				if (bar.rect.x < 0 || bar.rect.x + bar.rect.w > draw_rect.x + draw_rect.w) {
					bar.rect.x = draw_rect.x + (draw_rect.w - bar.rect.w) / 2;
				}
				bar.rect.y = draw_rect.y + (draw_rect.h - bar.rect.h) / 2;
			}
		}
		require_calculate_bar_xy_ = false;
	}


	for (std::vector<tbar>::const_iterator it = bars_.begin(); it != bars_.end(); ++ it) {
		const tbar& bar = *it;
		if (!bar.visible || bar.type == bar_minimap) {
			continue;
		}
		dstrect = bar.rect;
		dstrect.x += bar.delta.x;
		SDL_RenderCopy(renderer, bar.tex.get(), nullptr, &dstrect);
	}

	const uint32_t now = SDL_GetTicks();
	if (hide_screen_mode_ticks_ != 0 && now < hide_screen_mode_ticks_) {
		utils::string_map symbols;
		std::string msg = game_config::screen_modes.find(screen_mode_)->second;
		if (screen_mode_ == screenmode_partial) {
			utils::string_map symbols;
			symbols["percent"] = str_cast(visible_percent_);
			msg = vgettext2("Visible area display $percent%", symbols);
		}

		if (screen_mode_tex_.get() == nullptr || msg != screen_mode_msg_) {
			surface surf = font::get_rendered_text(msg, 0, font::SIZE_LARGEST, font::GOOD_COLOR);
			screen_mode_tex_ = SDL_CreateTextureFromSurface(renderer, surf);
			screen_mode_msg_ = msg;
		}

		int width2, height2;
		SDL_QueryTexture(screen_mode_tex_.get(), NULL, NULL, &width2, &height2);

		SDL_Rect dstrect{draw_rect.x, draw_rect.y + mainbar_y_offset_, width2, height2};
		SDL_RenderCopy(renderer, screen_mode_tex_.get(), nullptr, &dstrect);

	} else if (hide_screen_mode_ticks_ != 0) {
		hide_screen_mode_ticks_ = 0;
	}
}

SDL_Point tdesktop::wf_scale_mouse_pos(const SDL_Rect& rdpc_rect, int x, int y)
{
	int ww, wh, dw, dh;
	rdpContext* context = rdp_context_;
	rdpSettings* settings;

	SDL_Point result{nposm, nposm};
	if (moving_bar_ != nullptr || scrolling_bar_ != nullptr) {
		return result;
	}

	const bool all_valid = true;
	if (all_valid) {
		if (x < rdpc_rect.x) {
			x = rdpc_rect.x;
		} else if (x >= rdpc_rect.x + rdpc_rect.w) {
			x = rdpc_rect.x + rdpc_rect.w - 1;
		}

		if (y < rdpc_rect.y) {
			y = rdpc_rect.y;
		} else if (y >= rdpc_rect.y + rdpc_rect.h) {
			y = rdpc_rect.y + rdpc_rect.h - 1;
		}
	}

	if (!context || !point_in_rect(x, y, rdpc_rect)) {
		return result;
	}

	x -= rdpc_rect.x;
	y -= rdpc_rect.y;

	settings = context->settings;
	VALIDATE(settings != nullptr, null_str);

	ww = rdpc_rect.w;
	wh = rdpc_rect.h;
	dw = rdpd_crop_.w; // settings->DesktopWidth
	dh = rdpd_crop_.h; // settings->DesktopHeight

	result.x = x * dw / ww + rdpd_crop_.x + scroll_delta_.x;
	result.y = y * dh / wh + rdpd_crop_.y + scroll_delta_.y;

	return result;
}

bool tdesktop::send_extra_mouse_up_event(ttrack& widget, const tpoint& coordinate)
{
	if (!SDL_RectEmpty(&rdpc_rect_) && !point_in_rect(coordinate.x, coordinate.y, rdpc_rect_)) {
		int x, y;
		uint32_t status = SDL_GetMouseState(&x, &y);
		if (x < rdpc_rect_.x) {
			x = rdpc_rect_.x;
		} else if (x >= rdpc_rect_.x + rdpc_rect_.w) {
			x = rdpc_rect_.x + rdpc_rect_.w - 1;
		}

		if (y < rdpc_rect_.y) {
			y = rdpc_rect_.y;
		} else if (y >= rdpc_rect_.y + rdpc_rect_.h) {
			y = rdpc_rect_.y + rdpc_rect_.h - 1;
		}

		if (status & SDL_BUTTON(1)) {
			SDL_Log("send_extra_mouse_up_event, left up, (%i, %i)", x, y);
			wf_scale_mouse_event(widget, PTR_FLAGS_BUTTON1, x, y);
		}
		if (status & SDL_BUTTON(3)) {
			SDL_Log("send_extra_mouse_up_event, right up, (%i, %i)", x, y);
			wf_scale_mouse_event(widget, PTR_FLAGS_BUTTON2, x, y);
		}
		return true;
	}
	return false;
}

void tdesktop::did_mouse_leave_paper(ttrack& widget, const tpoint& first, const tpoint& last)
{
	send_extra_mouse_up_event(widget, last);

	int width = widget.get_width();

	int desire_btn = maybe_percent_? btn_percent: moving_btn_;
	if (moving_bar_ != nullptr) {
		VALIDATE(scrolling_bar_ == nullptr, null_str);
		VALIDATE(moving_bar_->delta.y == 0, null_str);
		VALIDATE(!maybe_percent_, null_str);

		moving_bar_->rect.x += moving_bar_->delta.x;
		moving_bar_->delta.x = 0;

		if (moving_bar_->rect.x < 0) {
			moving_bar_->rect.x = 0;
		} else if (moving_bar_->rect.x > width - moving_bar_->rect.w) {
			moving_bar_->rect.x = width - moving_bar_->rect.w;
		}

		const int preferencesx = moving_bar_->rect.x - widget.get_x();
		if (moving_bar_->type == bar_main) {
			preferences::set_mainbarx(preferencesx);

		} else if (moving_bar_->type == bar_minimap) {
			preferences::set_minimapbarx(preferencesx);
		}
		moving_bar_ = nullptr;
		moving_btn_ = nposm;

	} else {
		VALIDATE(moving_btn_ = nposm, null_str);
		
		if (scrolling_bar_ != nullptr) {
			rdpd_crop_.x += scroll_delta_.x;
			rdpd_crop_.y += scroll_delta_.y;
			scroll_delta_ = SDL_Point{0, 0};

			scrolling_bar_->delta = SDL_Point{0, 0};
			scrolling_bar_ = nullptr;
			will_longpress_ticks_ = 0;
			maybe_percent_ = false;

		} else {
			VALIDATE(will_longpress_ticks_ == 0, null_str);
			VALIDATE(!maybe_percent_, null_str);
		}
	}

	if (bar_change_ != nposm) {
		handle_bar_changed(bar_change_);

		if (!btn_in_current_bar(desire_btn)) {
			desire_btn = nposm;
		}
	}

	if (is_null_coordinate(last)) {
		return;
	}

	if (desire_btn == btn_mode) {
		screen_mode_ ++;
		if (!ratio_switchable_ && screen_mode_ == screenmode_ratio) {
			screen_mode_ ++;
		}
		if (screen_mode_ == screenmode_count) {
			// below switch screen_mode_ logic requrie screenmode_min not be screenmode_ratio.
			screen_mode_ = screenmode_min;
		}
		minimap_bar_->visible = screen_mode_ == screenmode_partial;

		rdpc_rect_ = empty_rect;
		rdpd_crop_ = empty_rect;
		minimap_dst_ = empty_rect;
		percent_rect_ = empty_rect;
		scroll_delta_ = SDL_Point{0, 0};
		preferences::set_screenmode(screen_mode_);

	} else if (desire_btn == btn_off) {
		window_->set_retval(twindow::CANCEL);

	} else if (desire_btn == btn_copy) {
		VALIDATE(avcapture_.get() != nullptr, null_str);
		gui2::tdesktop::tmsg_explorer* pdata = new gui2::tdesktop::tmsg_explorer;
		rtc::Thread::Current()->Post(RTC_FROM_HERE, this, tdesktop::MSG_EXPLORER, nullptr);

	} else if (desire_btn == btn_margin) {
		margining_ = !margining_;

	} else if (desire_btn == btn_percent) {
		gui2::tdesktop::tmsg_visible_percent* pdata = new gui2::tdesktop::tmsg_visible_percent;
		rtc::Thread::Current()->Post(RTC_FROM_HERE, this, tdesktop::MSG_VISIBLE_PERCENT, nullptr);
	}
}

void tdesktop::wf_scale_mouse_event(ttrack& widget, uint16_t flags, int x, int y)
{
	SDL_Point rdpd_pt = wf_scale_mouse_pos(rdpc_rect_, x, y);
	if (rdpd_pt.x != nposm && rdpd_pt.y != nposm) {
		// SDL_Log("wf_scale_mouse_event, flags: 0x%04x, (%i, %i)==>(%i, %i)", flags, x, y, rdpd_pt.x, rdpd_pt.y);
		freerdp_input_send_mouse_event(rdp_context_->input, flags, rdpd_pt.x, rdpd_pt.y);
	}
}

void tdesktop::did_mouse_motion_paper(ttrack& widget, const tpoint& first, const tpoint& last)
{
	// if (!send_extra_mouse_up_event(widget, last)) {
		wf_scale_mouse_event(widget, PTR_FLAGS_MOVE, last.x, last.y);
	// }

	if (is_null_coordinate(first)) {
		return;
	}

	if (moving_bar_ != nullptr) {
		VALIDATE(scrolling_bar_ == nullptr, null_str);
		VALIDATE(moving_bar_->delta.y == 0, null_str);

		moving_bar_->delta.x = last.x - first.x;
		if (moving_btn_ != nposm) {
			const int threshold = 3 * twidget::hdpi_scale; // moving_bar_->size / 4
			moving_btn_ = posix_abs(moving_bar_->delta.x) <= threshold? moving_btn_: nposm;	
		}
		if (moving_btn_ != nposm) {
			if (last.y < moving_bar_->rect.y || last.y >= moving_bar_->rect.y + moving_bar_->rect.h) {
				moving_btn_ = nposm;
			}
		}
	} else if (scrolling_bar_ != nullptr) {
		scrolling_bar_->delta.x = last.x - first.x;
		scrolling_bar_->delta.y = last.y - first.y;
		if (will_longpress_ticks_ != 0) {
			if (SDL_GetTicks() < will_longpress_ticks_) {
				const int threshold = 3 * twidget::hdpi_scale; // scrolling_bar_->size / 4
				will_longpress_ticks_ = posix_abs(scrolling_bar_->delta.x) <= threshold? will_longpress_ticks_: 0;
				if (maybe_percent_) {
					maybe_percent_ = posix_abs(scrolling_bar_->delta.x) <= threshold;
				}
			}
		}
	}
	did_draw_paper(*paper_, paper_->get_draw_rect(), false);
}

void tdesktop::signal_handler_longpress_paper(bool& halt, const tpoint& coordinate)
{
	halt = true;
/*
	if (!work_mode_) {
		return;
	}

	// long-press requires complex processes, current maybe pending other app_message task.
	// use desire_enter_settingsmode_, dont' waste this long-press.
	desire_enter_settingsmode_ = true;
*/
}

void tdesktop::signal_handler_left_button_up(const tpoint& coordinate)
{
	wf_scale_mouse_event(*paper_, PTR_FLAGS_BUTTON1, coordinate.x, coordinate.y);
}

void tdesktop::signal_handler_right_button_down(bool& handled, bool& halt, const tpoint& coordinate)
{
	halt = handled = true;
	for (std::vector<tbar>::const_iterator it = bars_.begin(); it != bars_.end(); ++ it) {
		const tbar& bar = *it;
		if (!bar.visible) {
			continue;
		}
		if (point_in_rect(coordinate.x, coordinate.y, bar.type != bar_minimap? bar.rect: minimap_dst_)) {
			return;
		}
	}
	wf_scale_mouse_event(*paper_, PTR_FLAGS_DOWN | PTR_FLAGS_BUTTON2, coordinate.x, coordinate.y);
}

void tdesktop::did_left_button_down(ttrack& widget, const tpoint& coordinate)
{
	VALIDATE(moving_bar_ == nullptr, null_str);
	VALIDATE(moving_btn_ == nposm, null_str);
	VALIDATE(scrolling_bar_ == nullptr, null_str);
	VALIDATE(will_longpress_ticks_ == 0, null_str);
	VALIDATE(!maybe_percent_, null_str);
	
	for (std::vector<tbar>::iterator it = bars_.begin(); it != bars_.end(); ++ it) {
		tbar& bar = *it;
		if (!bar.visible) {
			continue;
		}
		if (point_in_rect(coordinate.x, coordinate.y, bar.type != bar_minimap? bar.rect: minimap_dst_)) {
			if (bar.type != bar_minimap) {
				moving_bar_ = &bar;
				int xoffset = bar.rect.x;
				for (std::vector<int>::const_iterator it2 = bar.btns.begin(); it2 != bar.btns.end(); ++ it2, xoffset += bar.size + bar.gap) {
					SDL_Rect rect{xoffset, bar.rect.y, bar.size, bar.size};
					if (point_in_rect(coordinate.x, coordinate.y, rect)) {
						moving_btn_ = *it2;
					}
				}

			} else {
				scrolling_bar_ = &bar;
				const int longpress_threshold = 2000;
				will_longpress_ticks_ = SDL_GetTicks() + longpress_threshold;
				maybe_percent_ = point_in_rect(coordinate.x, coordinate.y, percent_rect_);
			} 
			return;
		}
	}
	wf_scale_mouse_event(widget, PTR_FLAGS_DOWN | PTR_FLAGS_BUTTON1, coordinate.x, coordinate.y);
}

void tdesktop::did_right_button_up(ttrack& widget, const tpoint& coordinate)
{
	wf_scale_mouse_event(widget, PTR_FLAGS_BUTTON2, coordinate.x, coordinate.y);
}

void tdesktop::signal_handler_sdl_wheel_up(bool& handled, const tpoint& coordinate)
{
	// why 120? reference to wf_event_process_WM_MOUSEWHEEL in <FreeRDP>/client/Windows/wf_event.c
	wf_scale_mouse_event(*paper_, PTR_FLAGS_WHEEL | PTR_FLAGS_WHEEL_NEGATIVE | 120, coordinate.x, coordinate.y);
	handled = true;
}

void tdesktop::signal_handler_sdl_wheel_down(bool& handled, const tpoint& coordinate)
{
	// why 120? reference to wf_event_process_WM_MOUSEWHEEL in <FreeRDP>/client/Windows/wf_event.c
	wf_scale_mouse_event(*paper_, PTR_FLAGS_WHEEL | 120, coordinate.x, coordinate.y);
	handled = true;
}

void tdesktop::signal_handler_sdl_wheel_left(bool& handled, const tpoint& coordinate)
{
	// why 120? reference to wf_event_process_WM_MOUSEWHEEL in <FreeRDP>/client/Windows/wf_event.c
	wf_scale_mouse_event(*paper_, PTR_FLAGS_HWHEEL | PTR_FLAGS_WHEEL_NEGATIVE | 120, coordinate.x, coordinate.y);
	handled = true;
}

void tdesktop::signal_handler_sdl_wheel_right(bool& handled, const tpoint& coordinate)
{
	// why 120? reference to wf_event_process_WM_MOUSEWHEEL in <FreeRDP>/client/Windows/wf_event.c
	wf_scale_mouse_event(*paper_, PTR_FLAGS_HWHEEL | 120, coordinate.x, coordinate.y);
	handled = true;
}

void tdesktop::app_OnMessage(rtc::Message* msg)
{
	const uint32_t now = SDL_GetTicks();

	switch (msg->message_id) {
	case MSG_STARTUP_MSG:
		{
			tmsg_startup_msg* pdata = static_cast<tmsg_startup_msg*>(msg->pdata);
			VALIDATE(pdata->ticks != 0 && !pdata->msg.empty(), null_str);
			if (pdata->rdpstatus == rdpstatus_connectionfinished) {
				did_rdp_client_connectionfinished();

			} else if (pdata->rdpstatus == rdpstatus_connectionclosed) {
				did_rdp_client_reset();
			}
			insert_startup_msg(pdata->ticks, pdata->msg, pdata->fail);
		}
		break;

	case MSG_EXPLORER:
		{
			gui2::trexplorer::tentry extra(null_str, null_str, null_str);
			if (game_config::os == os_windows) {
				extra = gui2::trexplorer::tentry(game_config::path + "/data/gui/default/scene", _("gui/scene"), "misc/dir-res.png");
			} else if (game_config::os == os_android) {
				extra = gui2::trexplorer::tentry("/sdcard", "/sdcard", "misc/dir-res.png");
			}

			tsend_suppress_output_lock suppress_output_lock(rdp_context_);

			gui2::texplorer_slot slot(rdp_client_->rdp_delegate());
			// since launcher has used unified rules for the all os, here according to it.
			gui2::trexplorer dlg(slot, null_str, extra, false, true); // game_config::mobile
			dlg.show();
			int res = dlg.get_retval();
			if (res != gui2::twindow::OK) {
				// continue;
			}
		}
		break;

	case MSG_VISIBLE_PERCENT:
		if (rdp_client_ != nullptr) {
			click_visible_percent(percent_rect_.x + percent_rect_.w, percent_rect_.y + percent_rect_.h);
		}
		break;
	}

	if (msg->pdata) {
		delete msg->pdata;
	}
}

} // namespace gui2

