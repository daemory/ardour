/*
 * Copyright (C) 2021 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef WAF_BUILD
#include "gtk2ardour-config.h"
#endif

#include "ardour/dB.h"
#include "ardour/logmeter.h"
#include "ardour/parameter_descriptor.h"
#include "ardour/port_manager.h"

#include "gtkmm2ext/utils.h"

#include "widgets/fastmeter.h"
#include "widgets/tooltips.h"

#include "input_port_monitor.h"
#include "ui_config.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace ArdourWidgets;

#define PX_SCALE(px) std::max ((float)px, rintf ((float)px* UIConfiguration::instance ().get_ui_scale ()))

InputPortMonitor::InputPortMonitor (ARDOUR::DataType dt, samplecnt_t sample_rate)
	: _dt (dt)
	, _audio_meter (0)
	, _audio_scope (0)
	, _midi_meter (0)
	, _midi_monitor (0)
{
	if (_dt == DataType::AUDIO) {
		_audio_meter = new FastMeter (
		    (uint32_t)floor (UIConfiguration::instance ().get_meter_hold ()),
		    18, FastMeter::Horizontal, PX_SCALE (200),
		    UIConfiguration::instance ().color ("meter color0"),
		    UIConfiguration::instance ().color ("meter color1"),
		    UIConfiguration::instance ().color ("meter color2"),
		    UIConfiguration::instance ().color ("meter color3"),
		    UIConfiguration::instance ().color ("meter color4"),
		    UIConfiguration::instance ().color ("meter color5"),
		    UIConfiguration::instance ().color ("meter color6"),
		    UIConfiguration::instance ().color ("meter color7"),
		    UIConfiguration::instance ().color ("meter color8"),
		    UIConfiguration::instance ().color ("meter color9"),
		    UIConfiguration::instance ().color ("meter background bottom"),
		    UIConfiguration::instance ().color ("meter background top"),
		    0x991122ff, // red highlight gradient Bot
		    0x551111ff, // red highlight gradient Top
		    (115.0 * log_meter0dB (-18)),
		    89.125,  // 115.0 * log_meter0dB(-9);
		    106.375, // 115.0 * log_meter0dB(-3);
		    115.0,   // 115.0 * log_meter0dB(0);
		    (UIConfiguration::instance ().get_meter_style_led () ? 3 : 1));

		_audio_scope = new InputScope (sample_rate, PX_SCALE (200), 25);

		_audio_meter->show ();
		_audio_scope->show ();

		ArdourWidgets::set_tooltip (_audio_scope, _("5 second history waveform"));

		pack_start (*_audio_meter, false, false);
		pack_start (*_audio_scope, true, true);

	} else if (_dt == DataType::MIDI) {
		_midi_meter   = new EventMeter ();
		_midi_monitor = new EventMonitor ();
		_midi_meter->show ();
		_midi_monitor->show ();

		ArdourWidgets::set_tooltip (_midi_meter, _("Highlight incoming MIDI data per MIDI channel"));
		ArdourWidgets::set_tooltip (_midi_monitor, _("Display most recently received MIDI messages"));

		pack_start (*_midi_meter, false, false);
		pack_start (*_midi_monitor, true, true);
	}
}

InputPortMonitor::~InputPortMonitor ()
{
	delete _audio_meter;
	delete _audio_scope;
	delete _midi_meter;
	delete _midi_monitor;
}

void
InputPortMonitor::update (float l, float p)
{
	assert (_dt == DataType::AUDIO && _audio_meter);
	_audio_meter->set (log_meter0dB (l), log_meter0dB (p));
}

void
InputPortMonitor::update (ARDOUR::CircularSampleBuffer& csb)
{
	assert (_dt == DataType::AUDIO && _audio_scope);
	_audio_scope->update (csb);
}

void
InputPortMonitor::update (float const* v)
{
	assert (_dt == DataType::MIDI && _midi_meter);
	_midi_meter->update (v);
}

void
InputPortMonitor::update (ARDOUR::CircularEventBuffer& ceb)
{
	assert (_dt == DataType::MIDI && _midi_monitor);
	_midi_monitor->update (ceb);
}

/* ****************************************************************************/

InputPortMonitor::InputScope::InputScope (samplecnt_t rate, int w, int h)
	: _xpos (0)
	, _rate (rate)
	, _min_width (w)
	, _min_height (h)
{
	_surface = Cairo::ImageSurface::create (Cairo::FORMAT_ARGB32, w, h);

	UIConfiguration::instance().ParameterChanged.connect (sigc::mem_fun (*this, &InputScope::parameter_changed));
	parameter_changed ("waveform-clip-level");
	parameter_changed ("show-waveform-clipping");
	parameter_changed ("waveform-scale");
}

void
InputPortMonitor::InputScope::parameter_changed (std::string const& p)
{
	if (p == "waveform-clip-level") {
		_clip_level = dB_to_coefficient (UIConfiguration::instance ().get_waveform_clip_level ());
	} else if ("show-waveform-clipping") {
		_show_clip = UIConfiguration::instance ().get_show_waveform_clipping ();
	} else if ("waveform-scale") {
		_logscale = UIConfiguration::instance ().get_waveform_scale () == Logarithmic;
	}
}

void
InputPortMonitor::InputScope::update (CircularSampleBuffer& csb)
{
	int    w   = _surface->get_width ();
	int    h   = _surface->get_height ();
	double h_2 = h / 2.0;

	int spp = 5.0 /*sec*/ * _rate / w; // samples / pixel
	Cairo::RefPtr<Cairo::Context> cr;

	bool  have_data = false;
	float minf, maxf;

	while (csb.read (minf, maxf, spp)) {
		if (!have_data) {
			cr        = Cairo::Context::create (_surface);
			have_data = true;
		}
		/* see also ExportReport::draw_waveform */
		cr->rectangle (_xpos, 0, 1, h);
		cr->set_operator (Cairo::OPERATOR_SOURCE);
		cr->set_source_rgba (0, 0, 0, 0);
		cr->fill ();

		cr->set_operator (Cairo::OPERATOR_OVER);
		cr->set_line_width (1.0);

		if (_show_clip && (maxf >= _clip_level || -minf >= _clip_level)) {
			Gtkmm2ext::set_source_rgba (cr, UIConfiguration::instance ().color ("clipped waveform"));
		} else {
			Gtkmm2ext::set_source_rgba (cr, UIConfiguration::instance ().color ("waveform fill"));
		}

		if (_logscale) {
			if (maxf > 0) {
				maxf =  alt_log_meter (fast_coefficient_to_dB (maxf));
			} else {
				maxf = -alt_log_meter (fast_coefficient_to_dB (-maxf));
			}
			if (minf > 0) {
				minf =  alt_log_meter (fast_coefficient_to_dB (minf));
			} else {
				minf = -alt_log_meter (fast_coefficient_to_dB (-minf));
			}
		}

		cr->move_to (_xpos + .5, h_2 - h_2 * maxf);
		cr->line_to (_xpos + .5, h_2 - h_2 * minf);
		cr->stroke ();

		if (++_xpos >= w) {
			_xpos = 0;
		}
	}

	if (have_data) {
		_surface->flush ();
		queue_draw ();
	}
}

void
InputPortMonitor::InputScope::render (Cairo::RefPtr<Cairo::Context> const& cr, cairo_rectangle_t* r)
{
	cr->rectangle (r->x, r->y, r->width, r->height);
	cr->clip ();

	int w = _surface->get_width ();
	int h = _surface->get_height ();

	cr->set_operator (Cairo::OPERATOR_OVER);
	cr->set_source (_surface, 0.0 - _xpos, 0);
	cr->paint ();

	cr->set_source (_surface, w - _xpos, 0);
	cr->paint ();

	/* zero line */
	double h_2 = h / 2.0;
	cr->set_line_width (1.0);
	Gtkmm2ext::set_source_rgb_a (cr, UIConfiguration::instance ().color ("zero line"), .7);
	cr->move_to (0, h_2);
	cr->line_to (w, h_2);
	cr->stroke ();
}

void
InputPortMonitor::InputScope::on_size_request (Gtk::Requisition* req)
{
	req->width  = _min_width;
	req->height = _min_height;
}

void
InputPortMonitor::InputScope::on_size_allocate (Gtk::Allocation& a)
{
	CairoWidget::on_size_allocate (a);
	int w = _surface->get_width ();
	int h = _surface->get_height ();
	if (a.get_width () != w || a.get_height () != h) {
		_surface = Cairo::ImageSurface::create (Cairo::FORMAT_ARGB32, a.get_width (), a.get_height ());
	}
}

/* ****************************************************************************/

InputPortMonitor::EventMeter::EventMeter ()
{
	_layout = Pango::Layout::create (get_pango_context ());

	UIConfiguration::instance().DPIReset.connect (sigc::mem_fun (*this, &EventMeter::dpi_reset));
	dpi_reset ();
}

void
InputPortMonitor::EventMeter::dpi_reset ()
{
	_layout->set_font_description (UIConfiguration::instance ().get_SmallMonospaceFont ());
	_layout->set_text ("Cy5");
	_layout->get_pixel_size (_height, _width);
	_width += 2;
	_height += 2;
	queue_resize ();
}

void
InputPortMonitor::EventMeter::update (float const* v)
{
	memcpy (_chn, v, sizeof (_chn));
	queue_draw ();
}

void
InputPortMonitor::EventMeter::render (Cairo::RefPtr<Cairo::Context> const& cr, cairo_rectangle_t* r)
{
	cr->rectangle (r->x, r->y, r->width, r->height);
	cr->clip ();

	float y0 = 2.5;

	double bg_r, bg_g, bg_b, bg_a;
	double fg_r, fg_g, fg_b, fg_a;

	Gtkmm2ext::color_to_rgba (UIConfiguration::instance ().color ("meter bar"), bg_r, bg_g, bg_b, bg_a);
	Gtkmm2ext::color_to_rgba (UIConfiguration::instance ().color ("midi meter 56"), fg_r, fg_g, fg_b, fg_a);

	fg_r -= bg_r;
	fg_g -= bg_g;
	fg_b -= bg_b;

	cr->set_operator (Cairo::OPERATOR_OVER);
	for (uint32_t i = 0; i < 17; ++i) {
		float x0 = 1.5 + _width * i;
		cr->set_line_width (1.0);
		cr->rectangle (x0, y0, _width, _height);
		cr->set_source_rgba (bg_r + _chn[i] * fg_r, bg_g + _chn[i] * fg_g, bg_b + _chn[i] * fg_b, .9);
		cr->fill_preserve ();
		Gtkmm2ext::set_source_rgba (cr, UIConfiguration::instance ().color ("border color"));
		cr->stroke ();

		cr->save ();
		int w, h;
		if (i < 16) {
			_layout->set_text (PBD::to_string (i + 1));
		} else {
			_layout->set_text ("SyS");
		}
		Gtkmm2ext::set_source_rgba (cr, UIConfiguration::instance ().color ("neutral:foreground2"));
		_layout->get_pixel_size (h, w);
		cr->move_to (x0 + .5 * (_width - w), y0 + .5 * (_height + h));
		cr->rotate (M_PI / -2.0);
		_layout->show_in_cairo_context (cr);
		cr->restore ();
	}
}

void
InputPortMonitor::EventMeter::on_size_request (Gtk::Requisition* req)
{
	req->width  = _width * 17 + 4;
	req->height = _height + 4;
}

/* ****************************************************************************/

InputPortMonitor::EventMonitor::EventMonitor ()
{
	_layout = Pango::Layout::create (get_pango_context ());
	_layout->set_font_description (UIConfiguration::instance ().get_SmallMonospaceFont ());

	_layout->set_text ("OffC#-1"); // 7 chars
	_layout->get_pixel_size (_width, _height);
	_width += 2;
	_height += 2;
}

void
InputPortMonitor::EventMonitor::update (CircularEventBuffer& ceb)
{
	if (ceb.read (_l)) {
		queue_draw ();
	}
}

void
InputPortMonitor::EventMonitor::render (Cairo::RefPtr<Cairo::Context> const& cr, cairo_rectangle_t* r)
{
	int ww = get_width () - 12;

	for (CircularEventBuffer::EventList::const_iterator i = _l.begin (); i != _l.end (); ++i) {
		if (i->data[0] == 0) {
			break;
		}
		char tmp[32];
		switch (i->data[0] & 0xf0) {
			case MIDI_CMD_NOTE_OFF:
				sprintf (tmp, "Off%4s", ParameterDescriptor::midi_note_name (i->data[1]).c_str ());
				break;
			case MIDI_CMD_NOTE_ON:
				sprintf (tmp, "On %4s", ParameterDescriptor::midi_note_name (i->data[1]).c_str ());
				break;
			case MIDI_CMD_NOTE_PRESSURE:
				sprintf (tmp, "KP %4s", ParameterDescriptor::midi_note_name (i->data[1]).c_str ());
				break;
			case MIDI_CMD_CONTROL:
				sprintf (tmp, "CC%02x %02x", i->data[1], i->data[2]);
				break;
			case MIDI_CMD_PGM_CHANGE:
				sprintf (tmp, "PC %3d ", i->data[1]);
				break;
			case MIDI_CMD_CHANNEL_PRESSURE:
				sprintf (tmp, "CP %02x  ", i->data[1]);
				break;
			case MIDI_CMD_BENDER:
				sprintf (tmp, "PB %04x", i->data[1] | i->data[2] << 7);
				break;
			case MIDI_CMD_COMMON_SYSEX:
				// TODO sub-type ?
				sprintf (tmp, " SysEx ");
				break;
		}

		int w, h;
		_layout->set_text (tmp);
		_layout->get_pixel_size (w, h);

		Gtkmm2ext::set_source_rgb_a (cr, UIConfiguration::instance ().color ("widget:bg"), .7);
		Gtkmm2ext::rounded_rectangle (cr, ww - w - 1, 1, 2 + w, _height - 2, _height / 4.0);
		cr->fill ();

		Gtkmm2ext::set_source_rgba (cr, UIConfiguration::instance ().color ("neutral:foreground2"));
		cr->move_to (ww - w, .5 * (_height - h));
		_layout->show_in_cairo_context (cr);

		ww -= w + 12;

		if (ww < w) {
			break;
		}
	}
}

void
InputPortMonitor::EventMonitor::on_size_request (Gtk::Requisition* req)
{
	req->width  = PX_SCALE (200);
	req->height = _height;
}
