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

#ifndef __gtk_ardour_input_port_monitor_h__
#define __gtk_ardour_input_port_monitor_h__

#include <gtkmm/box.h>

#include "gtkmm2ext/cairo_widget.h"

#include "ardour/circular_buffer.h"

namespace ArdourWidgets
{
	class FastMeter;
}

class InputPortMonitor : public Gtk::VBox
{
public:
	InputPortMonitor (ARDOUR::DataType, ARDOUR::samplecnt_t);
	~InputPortMonitor ();

	void update (float, float);                  // FastMeter
	void update (float const*);                  // EventMeter
	void update (ARDOUR::CircularSampleBuffer&); // InputScope
	void update (ARDOUR::CircularEventBuffer&);  // EventMonitor

private:
	class InputScope : public CairoWidget
	{
	public:
		InputScope (ARDOUR::samplecnt_t, int w , int h);
		void update (ARDOUR::CircularSampleBuffer&);

	protected:
		void render (Cairo::RefPtr<Cairo::Context> const&, cairo_rectangle_t*);
		void on_size_request (Gtk::Requisition*);
		void on_size_allocate (Gtk::Allocation&);

	private:
		void parameter_changed (std::string const&);

		int                 _xpos;
		ARDOUR::samplecnt_t _rate;
		int                 _min_width;
		int                 _min_height;
		float               _clip_level;
		bool                _show_clip;
		bool                _logscale;

		Cairo::RefPtr<Cairo::ImageSurface> _surface;
	};

	class EventMeter : public CairoWidget
	{
	public:
		EventMeter ();
		void update (float const*);

	protected:
		void render (Cairo::RefPtr<Cairo::Context> const&, cairo_rectangle_t*);
		void on_size_request (Gtk::Requisition*);

	private:
		void dpi_reset ();

		Glib::RefPtr<Pango::Layout> _layout;
		float                       _chn[17];
		int                         _width;
		int                         _height;
	};

	class EventMonitor : public CairoWidget
	{
	public:
		EventMonitor ();
		void update (ARDOUR::CircularEventBuffer&);

	protected:
		void render (Cairo::RefPtr<Cairo::Context> const&, cairo_rectangle_t*);
		void on_size_request (Gtk::Requisition*);

	private:
		ARDOUR::CircularEventBuffer::EventList _l;
		Glib::RefPtr<Pango::Layout>            _layout;
		int                                    _width;
		int                                    _height;
	};

	ARDOUR::DataType          _dt;
	ArdourWidgets::FastMeter* _audio_meter;
	InputScope*               _audio_scope;
	EventMeter*               _midi_meter;
	EventMonitor*             _midi_monitor;
};

#endif
