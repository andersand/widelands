/*
 * Copyright (C) 2002-2004, 2006-2008 by the Widelands Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "building.h"

#include "constructionsite.h"
#include "editor_game_base.h"
#include "filesystem.h"
#include "font_handler.h"
#include "game.h"
#include "graphic.h"
#include "interactive_base.h"
#include "layered_filesystem.h"
#include "map.h"
#include "militarysite.h"
#include "player.h"
#include "productionsite.h"
#include "profile.h"
#include "rendertarget.h"
#include "request.h"
#include "sound/sound_handler.h"
#include "trainingsite.h"
#include "transport.h"
#include "tribe.h"
#include "warehouse.h"
#include "wexception.h"
#include "worker.h"

#include "upcast.h"

#include <stdio.h>

namespace Widelands {

static const int32_t BUILDING_LEAVE_INTERVAL = 1000;


/*
===============
Building_Descr::Building_Descr

Initialize with sane defaults
===============
*/
Building_Descr::Building_Descr
	(Tribe_Descr const & tribe_descr, std::string const & building_name)
:
m_stopable       (false),
m_tribe          (tribe_descr),
m_name           (building_name),
m_buildable      (true),
m_buildicon      (0),
m_buildicon_fname(0),
m_size           (BaseImmovable::SMALL),
m_mine           (false),
m_vision_range   (0)
{}


/*
===============
Building_Descr::~Building_Descr

Cleanup
===============
*/
Building_Descr::~Building_Descr()
{
	free(m_buildicon_fname);
}

/*
===============
Create a building of this type. Does not perform any sanity checks.

if old != 0 this is an enhancing
===============
*/
Building* Building_Descr::create
	(Editor_Game_Base     &       egbase,
	 Player               &       owner,
	 Coords                 const pos,
	 bool                   const construct,
	 bool                   const fill,
	 Building_Descr const * const old)
	const
{
	Building* b = construct ? create_constructionsite(old) : create_object();
	b->m_position = pos;
	b->set_owner(&owner);
	if (fill)
		b->fill(dynamic_cast<Game &>(egbase));
	b->init(&egbase);

	return b;
}

/*
===============
Building_Descr::parse

Parse the basic building settings from the given profile and directory
===============
*/
void Building_Descr::parse
	(char         const * const directory,
	 Profile            * const prof,
	 enhancements_map_t &        enhancements_map,
	 EncodeData   const * const encdata)
{
	char fname[256];

	Section & global_s = prof->get_safe_section("global");
	m_descname = global_s.get_safe_string("descname");
	{
		char const * const string = global_s.get_safe_string("size");
		if      (!strcasecmp(string, "small"))
			m_size = BaseImmovable::SMALL;
		else if (!strcasecmp(string, "medium"))
			m_size = BaseImmovable::MEDIUM;
		else if (!strcasecmp(string, "big"))
			m_size = BaseImmovable::BIG;
		else if (!strcasecmp(string, "mine")) {
			m_size = BaseImmovable::SMALL;
			m_mine = true;
		} else
			throw wexception
				("Section [global], unknown size '%s'. Valid values are small, "
				 "medium, big, mine",
				 string);
	}

	// Parse build options
	m_buildable = global_s.get_bool("buildable", true);
	std::set<std::string> enhancement_names;
	while
		(Section::Value const * const v = global_s.get_next_val("enhancement"))
		try {
			std::string const target_name = v->get_string();
			if (enhancement_names.count(target_name))
				throw wexception("this has already been declared");
			enhancement_names.insert(target_name);
		} catch (_wexception const & e) {
			throw wexception
				("\"enhancements=%s\": %s", v->get_string(), e.what());
		}
	enhancements_map.insert
		(std::pair<Building_Descr *, std::set<std::string> >
		 	(this, enhancement_names));
	m_enhanced_building = global_s.get_bool("enhanced_building", false);
	if (m_buildable || m_enhanced_building) {
		//  get build icon
		snprintf
			(fname, sizeof(fname),
			 "%s/%s",
			 directory,
			 global_s.get_string("buildicon", (m_name + "_build.png").c_str()));

		//  Prevent memory leak in case someone would try to call parse twice.
		assert(not m_buildicon_fname);
		m_buildicon_fname = strdup(fname);

		//  build animation
		if (Section * const build_s = prof->get_section("build")) {
			if (build_s->get_int("fps", -1) != -1)
				throw wexception("fps defined for build animation!");
			if (!is_animation_known("build"))
				add_animation
					("build", g_anim.get(directory, *build_s, 0, encdata));
		} else
			throw wexception("Missing build animation");

		// Get costs
		Section & buildcost_s = prof->get_safe_section("buildcost");
		while (Section::Value const * const val = buildcost_s.get_next_val(0))
			try {
				if (Ware_Index const idx = m_tribe.ware_index(val->get_name())) {
					if (m_buildcost.count(idx))
						throw wexception
							("a buildcost item of this ware type has already been "
							 "defined");
					int32_t const value = val->get_int();
					if (value < 1 or 255 < value)
						throw wexception("count is out of range 1 .. 255");
					m_buildcost.insert(std::pair<Ware_Index, uint8_t>(idx, value));
				} else
					throw wexception
						("tribe does not define a ware type with this name");
			} catch (_wexception const & e) {
				throw wexception
					("buildcost \"%s=%s\": %s",
					 val->get_name(), val->get_string(), e.what());
			}
	}

	if ((m_stopable = global_s.get_bool("stopable", m_stopable))) {
		if (global_s.get_string("stopicon")) {
			m_stop_icon = directory;
			m_stop_icon+="/";
			m_stop_icon += global_s.get_string("stopicon");
		}
		else
			m_stop_icon = "pics/stop.png";
		if (global_s.get_string("continueicon")) {
			m_continue_icon = directory;
			m_continue_icon+="/";
			m_continue_icon += global_s.get_string("continueicon");
		}
		else
			m_continue_icon = "pics/continue.png";
	}

	{ //  parse basic animation data
		Section & idle_s = prof->get_safe_section("idle");
		if (!is_animation_known("idle"))
			add_animation("idle", g_anim.get(directory, idle_s, 0, encdata));
	}

	while (Section::Value const * const v = global_s.get_next_val("soundfx"))
		g_sound_handler.load_fx(directory, v->get_string());

	m_hints.parse (prof);

	m_vision_range = global_s.get_int("vision_range");
}

/**
 * Normal buildings don't conquer anything, do this returns 0 by default.
 *
 * \return the radius (in number of fields) of the conquered area.
 */
uint32_t Building_Descr::get_conquers() const
{
	return 0;
}


/**
 * \return the radius (in number of fields) of the area seen by this
 * building.
 */
uint32_t Building_Descr::vision_range() const throw ()
{
	if (m_vision_range > 0)
		return m_vision_range;
	else
		return get_conquers() + 4;
}


/*
===============
Building_Descr::load_graphics

Called whenever building graphics need to be loaded.
===============
*/
void Building_Descr::load_graphics()
{
	if (m_buildicon_fname)
		m_buildicon =
			g_gr->get_picture(PicMod_Game, m_buildicon_fname);
}

/*
===============
Building_Descr::create_constructionsite

Create a construction site for this type of building

if old != 0 this is an enhancement from an older building
===============
*/
Building * Building_Descr::create_constructionsite
	(Building_Descr const * const old) const
{
	if
		(Building_Descr * const descr =
		 	m_tribe.get_building_descr
		 		(m_tribe.building_index("constructionsite")))
	{

		ConstructionSite & csite = *static_cast<ConstructionSite *>
			(descr->create_object());
		csite.set_building(*this);
		if (old) csite.set_previous_building(old);

		return &csite;
	} else
		throw wexception
			("Tribe %s has no constructionsite", m_tribe.name().c_str());
}


/*
===============
Building_Descr::create_from_dir

Open the appropriate configuration file and check if a building description
is there.

May return 0.
===============
*/
Building_Descr* Building_Descr::create_from_dir
	(Tribe_Descr  const &       tribe,
	 enhancements_map_t &       enhancements_map,
	 char         const * const directory,
	 EncodeData  const * const encdata)
{
	const char* name;

	// name = last element of path
	const char* slash = strrchr(directory, '/');
	const char* backslash = strrchr(directory, '\\');

	if (backslash && (!slash || backslash > slash))
		slash = backslash;

	if (slash)
		name = slash+1;
	else
		name = directory;

	// Open the config file
	Building_Descr* descr = 0;
	char fname[256];

	snprintf(fname, sizeof(fname), "%s/conf", directory);

	if (!g_fs->FileExists(fname))
		return 0;

	try {
		Profile prof(fname, "global"); // section-less file
		char const * const type =
			prof.get_safe_section("global").get_safe_string("type");

		if (!strcasecmp(type, "warehouse"))
			descr = new Warehouse_Descr(tribe, name);
		else if (!strcasecmp(type, "production"))
			descr = new ProductionSite_Descr(tribe, name);
		else if (!strcasecmp(type, "construction"))
			descr = new ConstructionSite_Descr(tribe, name);
		else if (!strcasecmp(type, "military"))
			descr = new MilitarySite_Descr(tribe, name);
		else if (!strcasecmp(type, "training"))
			descr = new TrainingSite_Descr(tribe, name);
		else
			throw wexception("Unknown building type '%s'", type);

		descr->parse(directory, &prof, enhancements_map, encdata);
	} catch (std::exception const & e) {
		enhancements_map.erase(descr); //  Must remove the pointer from the map.
		delete descr;
		throw wexception("Error reading building %s: %s", name, e.what());
	} catch (...) {
		enhancements_map.erase(descr); //  Must remove the pointer from the map.
		delete descr;
		throw;
	}

	return descr;
}


/*
==============================

Implementation

==============================
*/

Building::Building(const Building_Descr & building_descr) :
PlayerImmovable(building_descr),
m_optionswindow(0),
m_flag         (0),
m_stop            (false),
m_defeating_player(0),
m_priority (DEFAULT_PRIORITY)
{}

Building::~Building()
{
	if (m_optionswindow)
		hide_options();
}

/*
===============
Building::get_type
Building::get_size
Building::get_passable
Building::get_base_flag
===============
*/
int32_t Building::get_type() const throw () {return BUILDING;}

int32_t Building::get_size() const throw () {return descr().get_size();}

bool Building::get_passable() const throw () {return false;}

Flag* Building::get_base_flag()
{
	return m_flag;
}


/*
===============
Building::get_playercaps

Return a bitfield of commands the owning player can issue for this building.
The bits are (1 << PCap_XXX).
By default, all buildable buildings can be bulldozed.
===============
*/
uint32_t Building::get_playercaps() const throw () {
	uint32_t caps = 0;
	if (descr().get_buildable() or descr().get_enhanced_building())
		caps                                |= 1 << PCap_Bulldoze;
	if (descr().get_stopable())       caps |= 1 << PCap_Stopable;
	if (descr().enhancements().size()) caps |= 1 << PCap_Enhancable;
	return caps;
}


std::string const & Building::name() const throw () {return descr().name();}


/*
===============
Building::start_animation

Start the given animation
===============
*/
void Building::start_animation(Editor_Game_Base* g, uint32_t anim)
{
	m_anim = anim;
	m_animstart = g->get_gametime();
}

/*
===============
Building::init

Common building initialization code. You must call this from derived class' init.
===============
*/
void Building::init(Editor_Game_Base* g)
{
	PlayerImmovable::init(g);

	// Set the building onto the map
	Map* map = g->get_map();
	Coords neighb;

	set_position(g, m_position);

	if (get_size() == BIG) {
		map->get_ln(m_position, &neighb);
		set_position(g, neighb);

		map->get_tln(m_position, &neighb);
		set_position(g, neighb);

		map->get_trn(m_position, &neighb);
		set_position(g, neighb);
	}

	// Make sure the flag is there


	map->get_brn(m_position, &neighb);
	{
		Flag * flag = dynamic_cast<Flag *>(map->get_immovable(neighb));
		if (not flag) flag = Flag::create(g, &owner(), neighb);
		m_flag = flag;
		flag->attach_building(g, this);
	}

	// Start the animation
	start_animation(g, descr().get_animation("idle"));

	m_leave_time = g->get_gametime();
}

/*
===============
Building::cleanup

Cleanup the building
===============
*/
void Building::cleanup(Editor_Game_Base* g)
{
	// Remove from flag
	m_flag->detach_building(g);

	// Unset the building
	unset_position(g, m_position);

	if (get_size() == BIG) {
		Map* map = g->get_map();
		Coords neighb;

		map->get_ln(m_position, &neighb);
		unset_position(g, neighb);

		map->get_tln(m_position, &neighb);
		unset_position(g, neighb);

		map->get_trn(m_position, &neighb);
		unset_position(g, neighb);
	}

	PlayerImmovable::cleanup(g);
}


/*
===============
Building::burn_on_destroy [virtual]

Return true if a "fire" should be created when the building is destroyed.
By default, burn always.
===============
*/
bool Building::burn_on_destroy()
{
	return true;
}


/*
===============
Building::destroy

Remove the building from the world now, and create a fire in its place if
applicable.
===============
*/
void Building::destroy(Editor_Game_Base* g)
{
	const bool fire           = burn_on_destroy();
	const Coords pos          = m_position;
	const Tribe_Descr & tribe = descr().tribe();
	PlayerImmovable::destroy(g);
	// We are deleted. Only use stack variables beyond this point
	if (fire) g->create_immovable(pos, "destroyed_building", &tribe);
}


/*
===============
Building::get_ui_anim [virtual]

Return the animation ID that is used for the building in UI items
(the building UI, messages, etc..)
===============
*/
uint32_t Building::get_ui_anim() const {return descr().get_ui_anim();}


/*
===============
Building::get_census_string [virtual]

Return the overlay string that is displayed on the map view when
enabled by the player.

Default is the descriptive name of the building, but e.g. construction
sites may want to override this.
===============
*/
const std::string & Building::census_string() const throw ()
{return descname();}


/*
===============
Building::get_statistics_string [virtual]

Return the overlay string that is displayed on the map view when enabled
by the player.

By default, there is no such string. Production buildings will want to
override this with a percentage indicating how well the building works, etc.
===============
*/
std::string Building::get_statistics_string()
{
	return "";
}


void Building::fill(Game &) {}


/*
===============
Building::get_building_work [virtual]

This function is called by workers in the buildingwork task.
Give the worker w a new task.
success is true if the previous task was finished successfully (without a
signal).
Return false if there's nothing to be done.
===============
*/
bool Building::get_building_work(Game *, Worker * w, bool)
{
	throw wexception
		("MO(%u): get_building_work() for unknown worker %u",
		 get_serial(), w->get_serial());
}


/**
 * Maintains the building leave queue. This ensures that workers don't leave
 * a building (in particular a military building or warehouse) all at once.
 * This is mostly for aesthetic purpose.
 *
 * \return \c true if the given worker can leave the building immediately.
 * Otherwise, the worker will be added to the buildings leave queue, and
 * \ref Worker::wakeup_leave_building() will be called as soon as the worker
 * can leave the building.
 *
 * \see Worker::start_task_leavebuilding(), leave_skip()
 */
bool Building::leave_check_and_wait(Game* g, Worker* w)
{
	Map_Object* allow = m_leave_allow.get(g);

	if (w == allow) {
		m_leave_allow = 0;
		return true;
	}

	// Check time and queue
	uint32_t time = g->get_gametime();

	if (!m_leave_queue.size())
	{
		if (static_cast<int32_t>(time - m_leave_time) >= 0) {
			m_leave_time = time + BUILDING_LEAVE_INTERVAL;
			return true;
		}

		schedule_act(g, m_leave_time - time);
	}

	m_leave_queue.push_back(w);
	return false;
}


/**
 * Indicate that the given worker wants to leave the building leave queue.
 * This function must be called when a worker aborts the waiting task for
 * some reason (e.g. the worker is carrying a ware, and the ware's transfer
 * has been cancelled).
 *
 * \see Building::leave_check_and_wait()
 */
void Building::leave_skip(Game* g, Worker* w)
{
	Leave_Queue::iterator it = std::find(m_leave_queue.begin(), m_leave_queue.end(), Object_Ptr(w));

	if (it != m_leave_queue.end())
		m_leave_queue.erase(it);
}


/*
===============
Building::act

Advance the leave queue.
===============
*/
void Building::act(Game* g, uint32_t data)
{
	uint32_t time = g->get_gametime();

	if (static_cast<int32_t>(time - m_leave_time) >= 0)
	{
		bool wakeup = false;

		// Wake up one worker
		while (m_leave_queue.size())
		{
			upcast(Worker, worker, m_leave_queue[0].get(g));

			m_leave_queue.erase(m_leave_queue.begin());

			if (!worker)
				continue;

			m_leave_allow = worker;

			if (worker->wakeup_leave_building(g, this)) {
				m_leave_time = time + BUILDING_LEAVE_INTERVAL;
				wakeup = true;
				break;
			}
		}

		if (m_leave_queue.size())
			schedule_act(g, m_leave_time - time);

		if (!wakeup)
			m_leave_time = time; // make sure leave_time doesn't get too far behind
	}

	PlayerImmovable::act(g, data);
}


/*
===============
Building::fetch_from_flag [virtual]

This function is called by our base flag to indicate that some item on the
flag wants to move into this building.
Return true if we can service that request (even if it is delayed), or false
otherwise.
===============
*/
bool Building::fetch_from_flag(Game *)
{
	molog("TODO: Implement Building::fetch_from_flag\n");

	return false;
}


/*
===============
Building::draw

Draw the building.
===============
*/
void Building::draw
	(Editor_Game_Base const &       game,
	 RenderTarget           &       dst,
	 FCoords                  const coords,
	 Point                    const pos)
{
	if (coords == m_position) { // draw big buildings only once
		dst.drawanim
			(pos, m_anim, game.get_gametime() - m_animstart, get_owner());

		//  door animation?

		//  overlay strings (draw when enabled)
		draw_help(game, dst, coords, pos);
	}
}


/*
===============
Building::draw_help

Draw overlay help strings when enabled.
===============
*/
void Building::draw_help
	(Editor_Game_Base const &       game,
	 RenderTarget           &       dst,
	 FCoords,
	 Point                    const pos)
{
	const uint32_t dpyflags = game.get_iabase()->get_display_flags();

	if (dpyflags & Interactive_Base::dfShowCensus)
	{
		//  TODO make more here
		g_fh->draw_string
			(dst,
			 UI_FONT_SMALL,
			 UI_FONT_SMALL_CLR,
			 pos - Point(0, 45),
			 census_string().c_str(),
			 Align_Center);
	}

	if (dpyflags & Interactive_Base::dfShowStatistics)
	{
		g_fh->draw_string
			(dst,
			 UI_FONT_SMALL,
			 UI_FONT_SMALL_CLR,
			 pos - Point(0, 35),
			 get_statistics_string().c_str(),
			 Align_Center);
	}
}

void Building::set_stop(bool stop) {
	m_stop = stop;
	get_economy()->rebalance_supply();
}

/**
* Get priority of a requested ware.
* Currently always returns base priority - to be extended later
 */
int32_t Building::get_priority
	(int32_t const type, Ware_Index const ware_index, bool const adjust) const
{
	int32_t priority = m_priority;

	if (type == Request::WARE) {
		// if priority is defined for specific ware,
		// combine base priority and ware priority
		std::map<Ware_Index, int32_t>::const_iterator it =
			m_ware_priorities.find(ware_index);
		if (it != m_ware_priorities.end())
			priority = adjust
				? (priority * it->second / DEFAULT_PRIORITY)
				: it->second;
	}

	return priority;
}

/**
* Collect priorities assigned to wares of this building
* priorities are identified by ware type and index
 */
void Building::collect_priorities
	(std::map<int32_t, std::map<Ware_Index, int32_t> > & p) const
{
	if (m_ware_priorities.size() == 0)
		return;
	std::map<Ware_Index, int32_t> & ware_priorities = p[Request::WARE];
	std::map<Ware_Index, int32_t>::const_iterator it;
	for (it = m_ware_priorities.begin(); it != m_ware_priorities.end(); ++it) {
		if (it->second == DEFAULT_PRIORITY)
			continue;
		ware_priorities[it->first] = it->second;
	}
}

/**
* Set base priority for this building (applies for all wares)
 */
void Building::set_priority(int32_t new_priority) {
	m_priority = new_priority;
}

void Building::set_priority
	(int32_t    const type,
	 Ware_Index const ware_index,
	 int32_t    const new_priority)
{
	if (type == Request::WARE) {
		m_ware_priorities[ware_index] = new_priority;
	}
}

/**
 * Log basic infos
 */
void Building::log_general_info(Editor_Game_Base* egbase) {
	PlayerImmovable::log_general_info(egbase);

	molog("m_position: (%i, %i)\n", m_position.x, m_position.y);
	molog("m_flag: %p\n", m_flag);
	molog
		("* position: (%i, %i)\n",
		 m_flag->get_position().x, m_flag->get_position().y);

	molog("m_anim: %s\n", descr().get_animation_name(m_anim).c_str());
	molog("m_animstart: %i\n", m_animstart);

	molog("m_leave_time: %i\n", m_leave_time);
	molog("m_stop: %i\n", m_stop);

	molog
		("m_leave_queue.size(): %lu\n",
		 static_cast<long unsigned int>(m_leave_queue.size()));
	molog("m_leave_allow.get(): %p\n", m_leave_allow.get(egbase));
}


void Building::add_worker(Worker * worker) {
	if (not get_workers().size()) {
		//  The first worker will enter the building so it should start seeing.
		Player & player = owner();
		Map    & map    = player.egbase().map();
		player.see_area
			(Area<FCoords>(map.get_fcoords(get_position()), vision_range()));
	}
	PlayerImmovable::add_worker(worker);
}


void Building::remove_worker(Worker * worker) {
	PlayerImmovable::remove_worker(worker);
	if (not get_workers().size()) {
		//  The last worker has left the building so it should stop seeing.
		Player & player = owner();
		Map    & map    = player.egbase().map();
		player.unsee_area
			(Area<FCoords>(map.get_fcoords(get_position()), vision_range()));
	}
}

};
