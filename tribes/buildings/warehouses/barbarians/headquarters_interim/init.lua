dirname = path.dirname(__file__)

tribes:new_warehouse_type {
   msgctxt = "barbarians_building",
   name = "barbarians_headquarters_interim",
   -- TRANSLATORS: This is a building name used in lists of buildings
   descname = pgettext("barbarians_building", "Headquarters"),
   directory = dirname,
   size = "big",
   destructible = false,

	-- The interim headquarters of the barbarians is a simple stone building thatched
	-- with reed and coated with many different wares.
   animations = {
		idle = {
			pictures = path.list_directory(dirname, "idle_\\d+.png"),
			hotspot = { 64, 106 }
		},
	},

	aihints = {},

	heal_per_second = 170,
	conquers = 9,
}
