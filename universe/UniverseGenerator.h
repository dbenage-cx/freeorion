// -*- C++ -*-
#ifndef _UniverseGenerator_h_
#define _UniverseGenerator_h_

#include "Condition.h"
#include "Universe.h"

#include "../util/MultiplayerCommon.h"


// Minimum distance between systems in universe units [0.0, m_universe_width]
const double    MIN_SYSTEM_SEPARATION       = 35.0;

struct PlayerSetupData;

// Class representing a position on the galaxy map, used
// to store the positions at which systems shall be created
struct SystemPosition {
    double x;
    double y;

    SystemPosition(double pos_x, double pos_y) :
        x(pos_x),
        y(pos_y)
    {}
    
    bool operator == (const SystemPosition &p)
    { return ((x == p.x) && (y == p.y)); }
};

/** A combination of names of ShipDesign that can be put together to make a
 * fleet of ships, and a name for such a fleet, loaded from starting_fleets.txt
 * ShipDesign names refer to designs listed in premade_ship_designs.txt.
 * Useful for saving or specifying prearranged combinations of prearranged
 * ShipDesigns to automatically put together, such as during universe creation.*/
class FleetPlan {
public:
    FleetPlan(const std::string& fleet_name, const std::vector<std::string>& ship_design_names,
              bool lookup_name_userstring = false) :
        m_name(fleet_name),
        m_ship_designs(ship_design_names),
        m_name_in_stringtable(lookup_name_userstring)
    {}
    FleetPlan() :
        m_name(""),
        m_ship_designs(),
        m_name_in_stringtable(false)
    {}
    virtual ~FleetPlan() {};
    const std::string&              Name() const;
    const std::vector<std::string>& ShipDesigns() const { return m_ship_designs; }
protected:
    std::string                     m_name;
    std::vector<std::string>        m_ship_designs;
    bool                            m_name_in_stringtable;
};

/** The combination of a FleetPlan and spawning instructions for start of game
  * monsters. */
class FO_COMMON_API MonsterFleetPlan : public FleetPlan {
public:
    MonsterFleetPlan(const std::string& fleet_name, const std::vector<std::string>& ship_design_names,
                     double spawn_rate = 1.0, int spawn_limit = 9999, const Condition::ConditionBase* location = 0,
                     bool lookup_name_userstring = false) :
        FleetPlan(fleet_name, ship_design_names, lookup_name_userstring),
        m_spawn_rate(spawn_rate),
        m_spawn_limit(spawn_limit),
        m_location(location)
    {}
    MonsterFleetPlan() :
        FleetPlan(),
        m_spawn_rate(1.0),
        m_spawn_limit(9999),
        m_location(0)
    {}
    virtual ~MonsterFleetPlan()
    { delete m_location; }
    double                          SpawnRate() const   { return m_spawn_rate; }
    int                             SpawnLimit() const  { return m_spawn_limit; }
    const Condition::ConditionBase* Location() const    { return m_location; }
protected:
    double                          m_spawn_rate;
    int                             m_spawn_limit;
    const Condition::ConditionBase* m_location;
};

// Calculates typical universe width based on number of systems
// A 150 star universe should be 1000 units across
double CalcTypicalUniverseWidth(int size);

/** Set active meter current values equal to target/max meter current
 * values.  Useful when creating new object after applying effects. */
void SetActiveMetersToTargetMaxCurrentValues(ObjectMap& object_map);

/** Set the population of unowned planets to a random fraction of 
 * their target values. */
void SetNativePopulationValues(ObjectMap& object_map);
    
/** Creates starlanes and adds them systems already generated. */
void GenerateStarlanes(int maxJumpsBetweenSystems, int maxStarlaneLength);

/** Sets empire homeworld
 * This includes setting ownership, capital, species,
 * preferred environment (planet type) for the species */
bool SetEmpireHomeworld(Empire* empire, int planet_id, std::string species_name);

/** Creates Empires objects for each entry in \a player_setup_data with
 * empire id equal to the specified player ids (so that the calling code
 * can know which empire belongs to which player). */
void InitEmpires(const std::map<int, PlayerSetupData>& player_setup_data);

#endif // _UniverseGenerator_h_
