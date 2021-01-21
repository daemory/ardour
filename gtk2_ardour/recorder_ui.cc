/*
 * Copyright (C) 2020 Robin Gareus <robin@gareus.org>
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

#include <algorithm>
#include <gtkmm/stock.h>

#include "pbd/string_convert.h"

#include "ardour/audioengine.h"
#include "ardour/audio_port.h"
#include "ardour/audio_track.h"
#include "ardour/midi_port.h"
#include "ardour/midi_track.h"
#include "ardour/profile.h"
#include "ardour/region.h"
#include "ardour/session.h"

#include "gtkmm2ext/gtk_ui.h"
#include "gtkmm2ext/keyboard.h"
#include "gtkmm2ext/utils.h"
#include "gtkmm2ext/window_title.h"

#include "widgets/prompter.h"
#include "widgets/tooltips.h"

#include "actions.h"
#include "ardour_dialog.h"
#include "ardour_ui.h"
#include "gui_thread.h"
#include "instrument_selector.h"
#include "public_editor.h"
#include "recorder_group_tabs.h"
#include "recorder_ui.h"
#include "timers.h"
#include "track_record_axis.h"
#include "ui_config.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace Gtkmm2ext;
using namespace ArdourWidgets;
using namespace Gtk;
using namespace std;

RecorderUI::RecorderUI ()
	: Tabbable (_content, _("Recorder"), X_("recorder"))
	, _toolbar_sep (1.0)
	, _btn_rec_all (_("Rec Arm All"))
	, _btn_rec_none (_("Rec Arm None"))
	, _btn_new_take (_("New Take"))
	, _btn_peak_reset (_("Reset Peak Hold"))
	, _meter_box_width (1)
	, _meter_area_cols (2)
	, _ruler_sep (1.0)
{

	load_bindings ();
	register_actions ();

	_meter_area.set_spacing (0);
	_meter_area.pack_start (_meter_table, true, true);
	_meter_area.signal_size_request().connect (sigc::mem_fun (*this, &RecorderUI::meter_area_size_request));
	_meter_area.signal_size_allocate ().connect (mem_fun (this, &RecorderUI::meter_area_size_allocate));
	_meter_scroller.add (_meter_area);
	_meter_scroller.set_policy (POLICY_NEVER, POLICY_AUTOMATIC);

	_scroller_base.set_flags (CAN_FOCUS);
	_scroller_base.add_events (Gdk::BUTTON_PRESS_MASK|Gdk::BUTTON_RELEASE_MASK);
	_scroller_base.signal_button_release_event().connect (sigc::mem_fun(*this, &RecorderUI::scroller_button_release));

	_rec_area.set_spacing (0);
	_rec_area.pack_end (_scroller_base, true, true);
	_rec_area.pack_end (_ruler_sep, false, false, 1);

	/* HBox groups | tracks */
	_rec_group_tabs = new RecorderGroupTabs (this);
	_rec_groups.pack_start (*_rec_group_tabs, false, false);
	_rec_groups.pack_start (_rec_area, true, true);

	/* vertical scroll, all tracks */
	_rec_scroller.add (_rec_groups);
	_rec_scroller.set_shadow_type(SHADOW_NONE);
	_rec_scroller.set_policy (POLICY_NEVER, POLICY_AUTOMATIC);

	/* HBox, ruler on top */
	_ruler_box.pack_start (_space, false, false);
	_ruler_box.pack_start (_ruler, true, true);

	/* VBox, toplevel of upper pane */
	_rec_container.pack_start (_ruler_box, false, false);
	_rec_container.pack_start (_rec_scroller, true, true);

	_pane.add (_rec_container);
	_pane.add (_meter_scroller);

	_content.pack_start (_toolbar_sep, false, false, 1);
	_content.pack_start (_toolbar, false, false, 2);
	_content.pack_start (_pane, true, true);
	_content.set_data ("ardour-bindings", bindings);

	_btn_rec_all.set_name ("generic button");
	_btn_rec_all.set_can_focus (true);
	_btn_rec_all.show ();
	_btn_rec_all.signal_clicked.connect (sigc::mem_fun (*this, &RecorderUI::arm_all));

	_btn_rec_none.set_name ("generic button");
	_btn_rec_none.set_can_focus (true);
	_btn_rec_none.show ();
	_btn_rec_none.signal_clicked.connect (sigc::mem_fun (*this, &RecorderUI::arm_none));

	_btn_new_take.set_name ("generic button");
	_btn_new_take.set_can_focus (true);
	_btn_new_take.show ();
	_btn_new_take.signal_clicked.connect (sigc::mem_fun (*this, &RecorderUI::new_take));

	_btn_peak_reset.set_name ("generic button");
	_btn_peak_reset.set_can_focus (true);
	_btn_peak_reset.show ();
	_btn_peak_reset.signal_clicked.connect (sigc::mem_fun (*this, &RecorderUI::peak_reset));

	_toolbar.set_spacing (4);
	_toolbar.pack_start (_btn_rec_all, false, false, 2);
	_toolbar.pack_start (_btn_rec_none, false, false);
	_toolbar.pack_start (*manage (new ArdourVSpacer), false, false);
	_toolbar.pack_start (_btn_new_take, false, false);
	_toolbar.pack_start (*manage (new ArdourVSpacer), false, false);
	_toolbar.pack_start (_btn_peak_reset, false, false);

	set_tooltip (_btn_rec_all, _("Record enable all tracks"));
	set_tooltip (_btn_rec_none, _("Disable recording of all tracks"));
	set_tooltip (_btn_new_take, _("Create new playlists for all tracks"));
	set_tooltip (_btn_peak_reset, _("Reset peak-hold indicator of all input meters"));

	update_title ();
	update_sensitivity ();

	_ruler.show ();
	_space.show ();
	_ruler_box.show ();
	_ruler_sep.show ();
	_toolbar_sep.show ();
	_rec_area.show ();
	_rec_scroller.show ();
	_rec_groups.show ();
	_rec_group_tabs->show ();
	_rec_container.show ();
	_meter_table.show ();
	_meter_area.show ();
	_meter_scroller.show ();
	_pane.show ();
	_content.show ();

	AudioEngine::instance ()->Running.connect (_engine_connections, invalidator (*this), boost::bind (&RecorderUI::start_updating, this), gui_context ());
	AudioEngine::instance ()->Stopped.connect (_engine_connections, invalidator (*this), boost::bind (&RecorderUI::stop_updating, this), gui_context ());
	AudioEngine::instance ()->Halted.connect (_engine_connections, invalidator (*this), boost::bind (&RecorderUI::stop_updating, this), gui_context ());
	AudioEngine::instance ()->PortConnectedOrDisconnected.connect (_engine_connections, invalidator (*this), boost::bind (&RecorderUI::port_connected_or_disconnected, this, _2, _4), gui_context ());
	AudioEngine::instance ()->PortPrettyNameChanged.connect (_engine_connections, invalidator (*this), boost::bind (&RecorderUI::port_pretty_name_changed, this, _1), gui_context ());
	AudioEngine::instance ()->PhysInputChanged.connect (_engine_connections, invalidator (*this), boost::bind (&RecorderUI::add_or_remove_io, this, _1, _2, _3), gui_context ());

	PresentationInfo::Change.connect (*this, invalidator (*this), boost::bind (&RecorderUI::presentation_info_changed, this, _1), gui_context());
	Config->ParameterChanged.connect (*this, invalidator (*this), boost::bind (&RecorderUI::parameter_changed, this, _1), gui_context ());
	//ARDOUR_UI::instance()->Escape.connect (*this, invalidator (*this), boost::bind (&RecorderUI::escape, this), gui_context());

	float          fract;
	XMLNode const* settings = ARDOUR_UI::instance()->recorder_settings();
	if (!settings || !settings->get_property ("recorder-vpane-pos", fract) || fract > 1.0) {
		fract = 0.75f;
	}
	_pane.set_divider (0, fract);
}

RecorderUI::~RecorderUI ()
{
	delete _rec_group_tabs;
}

void
RecorderUI::cleanup ()
{
	_visible_recorders.clear ();
	stop_updating ();
	_engine_connections.drop_connections ();
}

Gtk::Window*
RecorderUI::use_own_window (bool and_fill_it)
{
	bool new_window = !own_window ();

	Gtk::Window* win = Tabbable::use_own_window (and_fill_it);

	if (win && new_window) {
		win->set_name ("RecorderWindow");
		ARDOUR_UI::instance ()->setup_toplevel_window (*win, _("Recorder"), this);
		win->signal_event ().connect (sigc::bind (sigc::ptr_fun (&Keyboard::catch_user_event_for_pre_dialog_focus), win));
		win->set_data ("ardour-bindings", bindings);
		update_title ();
#if 0 // TODO
		if (!win->get_focus()) {
			win->set_focus (scroller);
		}
#endif
	}

	contents ().show ();

	_meter_box_width = 1;
	_meter_area.queue_resize ();

	return win;
}

XMLNode&
RecorderUI::get_state ()
{
	XMLNode* node = new XMLNode (X_("Recorder"));
	node->add_child_nocopy (Tabbable::get_state ());
	node->set_property (X_("recorder-vpane-pos"), _pane.get_divider ());
	return *node;
}

int
RecorderUI::set_state (const XMLNode& node, int version)
{
	return Tabbable::set_state (node, version);
}

void
RecorderUI::load_bindings ()
{
	bindings = Bindings::get_bindings (X_("Recorder"));
}

void
RecorderUI::register_actions ()
{
	Glib::RefPtr<ActionGroup> group = ActionManager::create_action_group (bindings, X_("Recorder"));
}

void
RecorderUI::set_session (Session* s)
{
	SessionHandlePtr::set_session (s);

	_ruler.set_session (s);
	_rec_group_tabs->set_session (s);

	update_sensitivity ();

	if (!_session) {
		return;
	}

	XMLNode* node = ARDOUR_UI::instance()->recorder_settings();
	set_state (*node, Stateful::loading_state_version);

	_session->DirtyChanged.connect (_session_connections, invalidator (*this), boost::bind (&RecorderUI::update_title, this), gui_context ());
	_session->StateSaved.connect (_session_connections, invalidator (*this), boost::bind (&RecorderUI::update_title, this), gui_context ());

	_session->RouteAdded.connect (_session_connections, invalidator (*this), boost::bind (&RecorderUI::add_routes, this, _1), gui_context ());
	TrackRecordAxis::CatchDeletion.connect (*this, invalidator (*this), boost::bind (&RecorderUI::remove_route, this, _1), gui_context ());

	_session->config.ParameterChanged.connect (_session_connections, invalidator (*this), boost::bind (&RecorderUI::parameter_changed, this, _1), gui_context ());

	Region::RegionPropertyChanged.connect (*this, invalidator (*this), boost::bind (&RecorderUI::gui_extents_changed, this), gui_context());
	_session->StartTimeChanged.connect (_session_connections, invalidator (*this), boost::bind (&RecorderUI::gui_extents_changed, this), gui_context());
	_session->EndTimeChanged.connect (_session_connections, invalidator (*this), boost::bind (&RecorderUI::gui_extents_changed, this), gui_context());

	update_title ();
	initial_track_display ();
	gui_extents_changed ();
	start_updating ();
}

void
RecorderUI::session_going_away ()
{
	ENSURE_GUI_THREAD (*this, &RecorderUI::session_going_away);
	SessionHandlePtr::session_going_away ();
	update_title ();
}

void
RecorderUI::update_title ()
{
	if (!own_window ()) {
		return;
	}

	if (_session) {
		string n;

		if (_session->snap_name () != _session->name ()) {
			n = _session->snap_name ();
		} else {
			n = _session->name ();
		}

		if (_session->dirty ()) {
			n = "*" + n;
		}

		WindowTitle title (n);
		title += S_("Window|Recorder");
		title += Glib::get_application_name ();
		own_window ()->set_title (title.get_string ());

	} else {
		WindowTitle title (S_("Window|Recorder"));
		title += Glib::get_application_name ();
		own_window ()->set_title (title.get_string ());
	}
}

void
RecorderUI::update_sensitivity ()
{
	const bool en = _session ? true : false;

	_btn_rec_all.set_sensitive (en);
	_btn_rec_none.set_sensitive (en);
	_btn_new_take.set_sensitive (en);
}

void
RecorderUI::parameter_changed (string const& p)
{
}

bool
RecorderUI::scroller_button_release (GdkEventButton* ev)
{
	if (Keyboard::is_context_menu_event (ev)) {
		ARDOUR_UI::instance()->add_route ();
		return true;
	}
	return false;
}

void
RecorderUI::start_updating ()
{
	if (_input_ports.size ()) {
		stop_updating ();
	}

	/* Audio */
	PortManager::AudioInputPorts const aip (AudioEngine::instance ()->audio_input_ports ());

	for (PortManager::AudioInputPorts::const_iterator i = aip.begin (); i != aip.end (); ++i) {
		_input_ports[i->first] = boost::shared_ptr<RecorderUI::InputPort> (new InputPort (i->first, DataType::AUDIO, this));
		set_connection_count (i->first);
	}

	/* MIDI */
	PortManager::MIDIInputPorts const mip (AudioEngine::instance ()->midi_input_ports ());
	for (PortManager::MIDIInputPorts::const_iterator i = mip.begin (); i != mip.end (); ++i) {
		_input_ports[i->first] = boost::shared_ptr<RecorderUI::InputPort> (new InputPort (i->first, DataType::MIDI, this));
		set_connection_count (i->first);
	}

	meter_area_layout ();

	_fast_screen_update_connection.disconnect ();
	_fast_screen_update_connection = Timers::super_rapid_connect (sigc::mem_fun (*this, &RecorderUI::update_meters));
}

void
RecorderUI::stop_updating ()
{
	_fast_screen_update_connection.disconnect ();
	container_clear (_meter_table);
	_input_ports.clear ();
}

void
RecorderUI::add_or_remove_io (DataType dt, vector<string> ports, bool add)
{
	_fast_screen_update_connection.disconnect ();
	bool spill_changed = false;

	if (add) {
		for (vector<string>::const_iterator i = ports.begin (); i != ports.end (); ++i) {
			_input_ports[*i] = boost::shared_ptr<RecorderUI::InputPort> (new InputPort (*i, dt, this));
			set_connection_count (*i);
		}
	} else {
		for (vector<string>::const_iterator i = ports.begin (); i != ports.end (); ++i) {
			_input_ports.erase (*i);
			spill_changed |= 0 != _spill_port_names.erase (*i);
		}
	}
	meter_area_layout ();

	if (spill_changed) {
		update_rec_table_layout ();
	}

	_fast_screen_update_connection = Timers::super_rapid_connect (sigc::mem_fun (*this, &RecorderUI::update_meters));
}

void
RecorderUI::update_meters ()
{
	PortManager::AudioInputPorts const aip (AudioEngine::instance ()->audio_input_ports ());

	/* scope data needs to be read contiously */
	for (PortManager::AudioInputPorts::const_iterator i = aip.begin (); i != aip.end (); ++i) {
		InputPortMap::iterator im = _input_ports.find (i->first);
		if (im != _input_ports.end()) {
			im->second->update (*(i->second.scope));
		}
	}

	if (!contents ().is_mapped ()) {
		return;
	}

	for (PortManager::AudioInputPorts::const_iterator i = aip.begin (); i != aip.end (); ++i) {
		InputPortMap::iterator im = _input_ports.find (i->first);
		if (im != _input_ports.end()) {
			im->second->update (accurate_coefficient_to_dB (i->second.meter->level), accurate_coefficient_to_dB (i->second.meter->peak));
		}
	}

	PortManager::MIDIInputPorts const mip (AudioEngine::instance ()->midi_input_ports ());
	for (PortManager::MIDIInputPorts::const_iterator i = mip.begin (); i != mip.end (); ++i) {
		InputPortMap::iterator im = _input_ports.find (i->first);
		im->second->update ((float const*)i->second.meter->chn_active);
		im->second->update (*(i->second.monitor));
	}

	for (list<TrackRecordAxis*>::const_iterator i = _recorders.begin (); i != _recorders.end (); ++i) {
		(*i)->fast_update ();
	}

	if (_session && _session->actively_recording ()) {
		/* maybe grow showing rec-regions */
		gui_extents_changed ();
	}
}

int
RecorderUI::calc_columns (int child_width, int parent_width)
{
	int n_col = parent_width / child_width;
	if (n_col <= 2) {
		/* at least 2 columns*/
		return 2;
	} else if (n_col <= 4) {
		/* allow 3 (2 audio + 1 MIDI) */
		return n_col;
	}
	/* otherwise only even number of cols */
	return n_col & 1;
}

void
RecorderUI::meter_area_layout ()
{
	container_clear (_meter_table);

	bool resize = false;
	int N_COL = -1;
	int col   = 0;
	int row   = 0;

	for (map<string, boost::shared_ptr<InputPort> >::iterator i = _input_ports.begin (); i != _input_ports.end (); ++i) {
		boost::shared_ptr<InputPort> ip = i->second;

		ip->show ();
		_meter_table.attach (*ip, col, col + 1, row, row + 1, SHRINK|FILL, SHRINK, 3, 2);

		Requisition r = ip->size_request ();
		if (_meter_box_width != r.width + 6) {
			_meter_box_width = r.width + 6;
			resize = true;
		}
		if (N_COL < 0) {
			N_COL = calc_columns (_meter_box_width, _meter_area.get_width ());
		}

		if (++col >= N_COL) {
			col = 0;
			++row;
		}
	}

	if (N_COL > 0) {
		_meter_area_cols = N_COL;
	}
	if (resize) {
		_meter_area.queue_resize ();
	}
}

void
RecorderUI::meter_area_size_allocate (Allocation& allocation)
{
	if (_meter_area_cols == calc_columns (_meter_box_width, allocation.get_width ())) {
		return;
	}
	meter_area_layout ();
}

void
RecorderUI::meter_area_size_request (GtkRequisition* requisition)
{
	Requisition r  = _meter_table.size_request ();
	requisition->width  = _meter_box_width * 2;
	requisition->height = r.height;
}

void
RecorderUI::port_connected_or_disconnected (string p1, string p2)
{
	if (_input_ports.find (p1) != _input_ports.end ()) {
		set_connection_count (p1);
	}
	if (_input_ports.find (p2) != _input_ports.end ()) {
		set_connection_count (p2);
	}
}

void
RecorderUI::port_pretty_name_changed (string pn)
{
	if (_input_ports.find (pn) != _input_ports.end ()) {
		_input_ports[pn]->setup_name ();
	}
}

void
RecorderUI::gui_extents_changed ()
{
	pair<samplepos_t, samplepos_t> ext = PublicEditor::instance().session_gui_extents ();

	if (ext.first == max_samplepos || ext.first >= ext.second) {
		return;
	}

	for (list<TrackRecordAxis*>::const_iterator i = _recorders.begin (); i != _recorders.end (); ++i) {
		(*i)->rec_extent (ext.first, ext.second);
	}

	_ruler.set_gui_extents (ext.first, ext.second);
	for (list<TrackRecordAxis*>::const_iterator i = _recorders.begin (); i != _recorders.end (); ++i) {
		(*i)->set_gui_extents (ext.first, ext.second);
	}
}

void
RecorderUI::set_connection_count (string const& p)
{
	if (!_session) {
		return;
	}

	uint32_t cnt = 0;

	boost::shared_ptr<RouteList> rl = _session->get_tracks ();
	for (RouteList::const_iterator r = rl->begin(); r != rl->end(); ++r) {
		if ((*r)->input()->connected_to (p)) {
			++cnt;
		}
	}

	_input_ports[p]->set_cnt (cnt);

	// TODO: think.
	// only clear when port is spilled and cnt == 0 ?
	// otherwise only update spilled tracks if port is spilled?
	if (!_spill_port_names.empty ()) {
		for (map<string, boost::shared_ptr<InputPort> >::iterator i = _input_ports.begin (); i != _input_ports.end (); ++i) {
			i->second->spill (false);
		}
		_spill_port_names.clear ();
		update_rec_table_layout ();
	}
}

void
RecorderUI::spill_port (string const& p)
{
	bool ok = false;
	if (_input_ports[p]->spilled ()) {
		ok = _input_ports[p]->spill (true);
		if (!ok) {
			new_track_for_port (_input_ports[p]->data_type (), p);
			return;
		}
	}

	bool update;
	if (ok) {
		pair<set<string>::iterator, bool> rv = _spill_port_names.insert (p);
		update = rv.second;
	} else {
		update = 0 != _spill_port_names.erase (p);
	}
	if (update) {
		update_rec_table_layout ();
	}
}

void
RecorderUI::initial_track_display ()
{
	boost::shared_ptr<RouteList> r = _session->get_tracks ();
	RouteList                    rl (*r);
	_recorders.clear ();
	add_routes (rl);
}

void
RecorderUI::add_routes (RouteList& rl)
{
	rl.sort (Stripable::Sorter ());
	for (RouteList::iterator r = rl.begin (); r != rl.end (); ++r) {
		/* we're only interested in Tracks */
		if (!boost::dynamic_pointer_cast<Track> (*r)) {
			continue;
		}

		TrackRecordAxis* rec = new TrackRecordAxis (/**this,*/ _session, *r);
		_recorders.push_back (rec);
		rec->signal_size_allocate().connect (sigc::bind (sigc::mem_fun (*this, &RecorderUI::update_spacer_width), rec));
	}
	update_rec_table_layout ();
}

void
RecorderUI::remove_route (TrackRecordAxis* ra)
{
	if (!_session || _session->deletion_in_progress ()) {
		_recorders.clear ();
		return;
	}
	list<TrackRecordAxis*>::iterator i = find (_recorders.begin (), _recorders.end (), ra);
	assert (i != _recorders.end ());
	_rec_area.remove (**i);
	_recorders.erase (i);
	update_rec_table_layout ();
}

struct TrackRecordAxisSorter {
	bool operator() (const TrackRecordAxis* ca, const TrackRecordAxis* cb)
	{
		boost::shared_ptr<Stripable> const& a = ca->stripable ();
		boost::shared_ptr<Stripable> const& b = cb->stripable ();
		return Stripable::Sorter(true)(a, b);
	}
};

void
RecorderUI::presentation_info_changed (PBD::PropertyChange const& what_changed)
{
	if (what_changed.contains (Properties::hidden)) {
		update_rec_table_layout ();
	} else if (what_changed.contains (Properties::order)) {
		/* test if effective order changed. When deleting tracks
		 * the PI:order_key changes, but the layout does not change.
		 */
		list<TrackRecordAxis*> rec (_recorders);
		_recorders.sort (TrackRecordAxisSorter ());
		if (_recorders != rec) {
			update_rec_table_layout ();
		}
	}
}

void
RecorderUI::update_rec_table_layout ()
{
	_visible_recorders.clear ();
	_recorders.sort (TrackRecordAxisSorter ());

	list<TrackRecordAxis*>::const_iterator i;
	for (i = _recorders.begin (); i != _recorders.end (); ++i) {
		if ((*i)->route ()->presentation_info ().hidden ()) {
			if ((*i)->get_parent ()) {
				_rec_area.remove (**i);
			}
			continue;
		}

		/* spill */
		if (!_spill_port_names.empty ()) {
			bool connected = false;
			for (set<string>::const_iterator j = _spill_port_names.begin(); j != _spill_port_names.end(); ++j) {
				if ((*i)->route ()->input()->connected_to (*j)) {
					connected = true;
					break;
				}
			}
			if (!connected) {
				if ((*i)->get_parent ()) {
					_rec_area.remove (**i);
				}
				continue;
			}
		}

		if (!(*i)->get_parent ()) {
			_rec_area.pack_start (**i, false, false);
		} else {
			_rec_area.reorder_child (**i, -1);
		}
		(*i)->show ();
		_visible_recorders.push_back (*i);
	}

	_rec_group_tabs->set_dirty ();
}

list<TrackRecordAxis*>
RecorderUI::visible_recorders () const
{
	return _visible_recorders;
}

void
RecorderUI::update_spacer_width (Allocation&, TrackRecordAxis* rec)
{
	// Note: this is idempotent
	_space.set_size_request (rec->summary_xpos () + _rec_group_tabs->get_width (), -1);
}

void
RecorderUI::new_track_for_port (DataType dt, string const& port_name)
{
	ArdourDialog d (_("Create track for input"), true, false);

	Entry track_name_entry;
	InstrumentSelector instrument_combo;
	ComboBoxText strict_io_combo;

	string pn = AudioEngine::instance()->get_pretty_name_by_name (port_name);
	if (!pn.empty ()) {
		track_name_entry.set_text (pn);
	} else {
		track_name_entry.set_text (port_name);
	}

	strict_io_combo.append_text (_("Flexible-I/O"));
	strict_io_combo.append_text (_("Strict-I/O"));
	strict_io_combo.set_active (Config->get_strict_io () ? 1 : 0);

	Label* l;
	Table  t;
	int    row = 0;

	t.set_spacings (6);

	l = manage (new Label (string_compose (_("Create new track connected to port '%1'"), pn.empty() ? port_name : pn)));
	t.attach (*l, 0, 2, row, row + 1, EXPAND | FILL, SHRINK);
	++row;

	l = manage (new Label (_("Track name:")));
	t.attach (*l,                0, 1, row, row + 1, SHRINK, SHRINK);
	t.attach (track_name_entry,  1, 2, row, row + 1, EXPAND | FILL, SHRINK);
	++row;

	if (dt == DataType::MIDI) {
		l = manage (new Label (_("Instrument:")));
		t.attach (*l,               0, 1, row, row + 1, SHRINK, SHRINK);
		t.attach (instrument_combo, 1, 2, row, row + 1, EXPAND | FILL, SHRINK);
		++row;
	}

	if (Profile->get_mixbus ()) {
		strict_io_combo.set_active (1);
	} else {
		l = manage (new Label (_("Strict I/O:")));
		t.attach (*l,              0, 1, row, row + 1, SHRINK, SHRINK);
		t.attach (strict_io_combo, 1, 3, row, row + 1, FILL, SHRINK);
		set_tooltip (strict_io_combo, _("With strict-i/o enabled, Effect Processors will not modify the number of channels on a track. The number of output channels will always match the number of input channels."));
	}

	d.get_vbox()->pack_start (t, false, false);
	d.get_vbox()->set_border_width (12);

	d.add_button(Stock::CANCEL, RESPONSE_CANCEL);
	d.add_button(Stock::OK, RESPONSE_OK);
	d.set_default_response (RESPONSE_OK);
	d.set_position (WIN_POS_MOUSE);
	d.show_all ();

	track_name_entry.signal_activate().connect (sigc::bind (sigc::mem_fun (d, &Dialog::response), RESPONSE_OK));

	if (d.run() != RESPONSE_OK) {
		return;
	}

	d.hide ();

	bool strict_io = strict_io_combo.get_active_row_number () == 1;
	string track_name = track_name_entry.get_text();

	uint32_t outputs = 2;
	if (_session->master_out ()) {
		outputs = max (outputs, _session->master_out ()->n_inputs ().n_audio ());
	}

	if (dt == DataType::AUDIO) {
		boost::shared_ptr<Route> r;
		try {
			list<boost::shared_ptr<AudioTrack> > tl = _session->new_audio_track (1, outputs, NULL, 1, track_name, PresentationInfo::max_order, Normal, false);
			r = tl.front ();
		} catch (...) {
			return;
		}
		if (r) {
			r->set_strict_io (strict_io);
			r->input ()->audio (0)->connect (port_name);
		}
	} else if (dt == DataType::MIDI) {
		boost::shared_ptr<Route> r;
		try {
			list<boost::shared_ptr<MidiTrack> > tl = _session->new_midi_track (
					ChanCount (DataType::MIDI, 1), ChanCount (DataType::MIDI, 1),
					strict_io,
					instrument_combo.selected_instrument (), (Plugin::PresetRecord*) 0,
					(RouteGroup*) 0,
					1, track_name, PresentationInfo::max_order, Normal, false);
			r = tl.front ();
		} catch (...) {
			return;
		}
		if (r) {
			r->input ()->midi (0)->connect (port_name);
		}
	}
}

void
RecorderUI::arm_all ()
{
	if (_session) {
		_session->set_all_tracks_record_enabled (true);
	}
}

void
RecorderUI::arm_none ()
{
	if (_session) {
		_session->set_all_tracks_record_enabled (false);
	}
}

void
RecorderUI::new_take ()
{
	//PublicEditor::instance().new_playlists ();
}

void
RecorderUI::peak_reset ()
{
	AudioEngine::instance ()->reset_input_meters ();
}

/* ****************************************************************************/

#define PX_SCALE(px) std::max ((float)px, rintf ((float)px* UIConfiguration::instance ().get_ui_scale ()))

bool RecorderUI::InputPort::_size_groups_initialized = false;

Glib::RefPtr<Gtk::SizeGroup> RecorderUI::InputPort::_name_size_group;
Glib::RefPtr<Gtk::SizeGroup> RecorderUI::InputPort::_spill_size_group;
Glib::RefPtr<Gtk::SizeGroup> RecorderUI::InputPort::_button_size_group;
Glib::RefPtr<Gtk::SizeGroup> RecorderUI::InputPort::_monitor_size_group;

RecorderUI::InputPort::InputPort (string const& name, DataType dt, RecorderUI* parent)
	: _dt (dt)
	, _monitor (dt, AudioEngine::instance()->sample_rate ())
	, _spill_button ("", ArdourButton::just_led_default_elements, true)
	, _name_button (name)
	, _name_label ("", ALIGN_CENTER, ALIGN_CENTER, false)
	, _connection_label ("0", ALIGN_CENTER, ALIGN_CENTER, false)
	, _port_name (name)
	, _n_connections (0)
{
	if (!_size_groups_initialized) {
		_size_groups_initialized = true;
		_name_size_group = Gtk::SizeGroup::create (Gtk::SIZE_GROUP_HORIZONTAL);
		_spill_size_group = Gtk::SizeGroup::create (Gtk::SIZE_GROUP_HORIZONTAL);
		_button_size_group = Gtk::SizeGroup::create (Gtk::SIZE_GROUP_VERTICAL);
		_monitor_size_group = Gtk::SizeGroup::create (Gtk::SIZE_GROUP_BOTH);
	}

	VBox* vbox_c = manage (new VBox);
	VBox* vbox_n = manage (new VBox);

	_spill_button.set_name ("generic button");
	_spill_button.set_can_focus (true);
	_spill_button.set_led_left (true);
	_spill_button.signal_clicked.connect (sigc::bind (sigc::mem_fun (*parent, &RecorderUI::spill_port), name));

	int nh = 120 * UIConfiguration::instance ().get_ui_scale ();
	_name_button.set_corner_radius (2);
	_name_button.set_name ("meterbridge label");
	_name_button.set_text_ellipsize (Pango::ELLIPSIZE_MIDDLE);
	_name_button.set_layout_ellipsize_width (nh * PANGO_SCALE);
	_name_button.set_sizing_text ("system:capture_123");
	_name_button.signal_clicked.connect (sigc::mem_fun (*this, &RecorderUI::InputPort::rename_port));

	_name_label.set_ellipsize (Pango::ELLIPSIZE_MIDDLE);
	_name_label.set_max_width_chars (18);

	setup_name ();

	set_tooltip (_name_button, _("Set or edit the custom name for this input port."));

	vbox_c->pack_start (_spill_button, true, true);
	vbox_c->pack_start (_connection_label, true, true);

	vbox_n->pack_start (_name_button, true, true);
#if 0 // MIXBUS ?
	vbox_n->pack_start (_name_label, true, true);
#endif

	_box.pack_start (*vbox_c, false, false, 1);
	_box.pack_start (*vbox_n, false, false, 1);
	_box.pack_start (_monitor, false, false);

	_name_size_group->add_widget (*vbox_n);
	_spill_size_group->add_widget (*vbox_c);
	_button_size_group->add_widget (_spill_button);
	_button_size_group->add_widget (_name_button);
	_monitor_size_group->add_widget (_monitor);

	pack_start (_box, true, false);
	_box.show_all ();
}

RecorderUI::InputPort::~InputPort ()
{
}

void
RecorderUI::InputPort::update (float l, float p)
{
	_monitor.update (l, p);
}

void
RecorderUI::InputPort::update (CircularSampleBuffer& csb)
{
	_monitor.update (csb);
}

void
RecorderUI::InputPort::update (float const* v)
{
	_monitor.update (v);
}

void
RecorderUI::InputPort::update (CircularEventBuffer& ceb)
{
	_monitor.update (ceb);
}

void
RecorderUI::InputPort::set_cnt (uint32_t cnt)
{
	_n_connections = cnt;
	_connection_label.set_text (PBD::to_string (cnt));

	if (cnt > 0) {
		_spill_button.set_elements (ArdourButton::just_led_default_elements);
		set_tooltip (_spill_button, _("Only display tracks that are received input from this source."));
	} else {
		_spill_button.set_elements (ArdourButton::Element (ArdourButton::Edge|ArdourButton::Body));
		set_tooltip (_spill_button, _("Create a new track connected to this source."));
	}
}

void
RecorderUI::InputPort::setup_name ()
{
	string pn = AudioEngine::instance()->get_pretty_name_by_name (_port_name);
	if (!pn.empty ()) {
		_name_button.set_text (pn);
		_name_label.set_text (_port_name);
	} else {
		_name_button.set_text (_port_name);
		_name_label.set_text ("");
	}
}

void
RecorderUI::InputPort::rename_port ()
{
	Prompter prompter (true, true);

	prompter.set_name ("Prompter");

	prompter.add_button (Stock::REMOVE, RESPONSE_NO);
	prompter.add_button (Stock::OK, RESPONSE_ACCEPT);

	prompter.set_title (_("Customize port name"));
	prompter.set_prompt (_("Port name"));
	prompter.set_initial_text (AudioEngine::instance()->get_pretty_name_by_name (_port_name));

	string name;
	switch (prompter.run ()) {
		case RESPONSE_ACCEPT:
			prompter.get_result (name);
			break;
		case RESPONSE_NO:
			/* use blank name, reset */
			break;
		default:
			return;
	}

	AudioEngine::instance()->set_port_pretty_name (_port_name, name);
}

bool
RecorderUI::InputPort::spill (bool en)
{
	bool active = _spill_button.get_active ();
	bool act = active;

	if (!en) {
		act = false;
	}

	if (_n_connections == 0) {
		act = false;
	}

	if (active != act) {
		_spill_button.set_active (act);
	}
	return act;
}

bool
RecorderUI::InputPort::spilled () const
{
	return _spill_button.get_active ();
}

string const&
RecorderUI::InputPort::name () const
{
	return _port_name;
}

DataType
RecorderUI::InputPort::data_type () const
{
	return _dt;
}

/* ****************************************************************************/

RecorderUI::RecRuler::RecRuler ()
	: _left (0)
	, _right (0)
{
	_layout = Pango::Layout::create (get_pango_context ());
	_layout->set_font_description (UIConfiguration::instance ().get_SmallMonospaceFont ());
	_layout->set_text ("88:88:88,88");
	_layout->get_pixel_size (_time_width, _time_height);
}

void
RecorderUI::RecRuler::set_gui_extents (samplepos_t start, samplepos_t end)
{
	if (_left == start && _right == end) {
		return;
	}
	_left = start;
	_right = end;
	queue_draw ();
}

void
RecorderUI::RecRuler::render (Cairo::RefPtr<Cairo::Context> const& cr, cairo_rectangle_t* r)
{
  cr->rectangle (r->x, r->y, r->width, r->height);
  cr->clip ();

	if (!_session || _left >= _right) {
		return;
	}

	const int width  = get_width ();
	const int height = get_height ();

	const int         n_labels         = floor (width / (_time_width * 1.5));
	const samplecnt_t time_span        = _right - _left;
	const samplecnt_t time_granularity = ceil (time_span / n_labels / _session->sample_rate ()) * _session->sample_rate ();
	const double      px_per_sample    = width / (double) time_span;

	const samplepos_t lower = (_left / time_granularity) * time_granularity;

	Gtkmm2ext::set_source_rgba (cr, UIConfiguration::instance().color ("ruler text"));
	cr->set_line_width (1);

	for (int i = 0; i < 2 + n_labels; ++i) {
		samplepos_t when = lower + i * time_granularity;
		double xpos      = (when - _left) * px_per_sample;
		if (xpos < 0) {
			continue;
		}

		char buf[32];
		int lw, lh;
		AudioClock::print_minsec (when, buf, sizeof (buf), _session->sample_rate ());
		_layout->set_text (string(buf).substr(1));
		_layout->get_pixel_size (lw, lh);

		if (xpos + lw > width) {
			break;
		}

		int x0 = xpos + 2;
		int y0 = height - _time_height - 3;

		cr->move_to (xpos + .5 , 0);
		cr->line_to (xpos + .5 , height);
		cr->stroke ();

		cr->move_to (x0, y0);
		_layout->show_in_cairo_context (cr);
	}
}

void
RecorderUI::RecRuler::on_size_request (Requisition* req)
{
	req->width = 200;
	req->height = _time_height + 4;
}

bool
RecorderUI::RecRuler::on_button_press_event (GdkEventButton* ev)
{
	if (!_session || _session->actively_recording()) {
		return false;
	}
	// TODO start "drag"  editor->_dragging_playhead = true
	// CursorDrag::start_grabA
	// RecRuler internal drag (leave editor + TC transmission alone?!)

	_session->request_locate (_left + (double) (_right - _left) * ev->x / get_width ());
	return true;
}
