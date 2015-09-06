-- This include can be removed when all help texts have been defined.
include "tribes/scripting/help/global_helptexts.lua"

function building_helptext_lore()
	-- #TRANSLATORS: Lore helptext for a building
	return no_lore_text_yet()
end

function building_helptext_lore_author()
	-- #TRANSLATORS: Lore author helptext for a building
	return no_lore_author_text_yet()
end

function building_helptext_purpose()
	-- TRANSLATORS: Purpose helptext for a building
	return pgettext("empire_building", "Accommodation for your people. Also stores your wares and tools.")
end

function building_helptext_note()
	-- TRANSLATORS: Note helptext for a building
	return
		pgettext("empire_building", "The headquarters is your main building.")
		.. "<br>" ..
		no_purpose_text_yet()
end

function building_helptext_performance()
	-- #TRANSLATORS: Performance helptext for a building
	return ""
end
