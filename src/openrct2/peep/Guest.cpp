/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "../Context.h"
#include "../Game.h"
#include "../OpenRCT2.h"
#include "../audio/audio.h"
#include "../config/Config.h"
#include "../core/Guard.hpp"
#include "../interface/Window_internal.h"
#include "../localisation/Localisation.h"
#include "../management/Finance.h"
#include "../management/Marketing.h"
#include "../management/NewsItem.h"
#include "../network/network.h"
#include "../rct2/RCT2.h"
#include "../ride/Ride.h"
#include "../ride/RideData.h"
#include "../ride/ShopItem.h"
#include "../ride/Station.h"
#include "../ride/Track.h"
#include "../scenario/Scenario.h"
#include "../util/Util.h"
#include "../windows/Intent.h"
#include "../world/Climate.h"
#include "../world/Footpath.h"
#include "../world/LargeScenery.h"
#include "../world/Map.h"
#include "../world/Park.h"
#include "../world/Scenery.h"
#include "../world/Sprite.h"
#include "../world/Surface.h"
#include "../world/TileElementsView.h"
#include "GuestPathfinding.h"
#include "Peep.h"
#include "Staff.h"

#include <algorithm>
#include <iterator>

using namespace OpenRCT2;

// Locations of the spiral slide platform that a peep walks from the entrance of the ride to the
// entrance of the slide. Up to 4 waypoints for each 4 sides that an ride entrance can be located
// and 4 different rotations of the ride. 4 * 4 * 4 = 64 locations.
// clang-format off
static constexpr const CoordsXY SpiralSlideWalkingPath[64] = {
    {  56,   8 },
    {   8,   8 },
    {   8,  32 },
    {  32,  32 },
    {   8,   8 },
    {   8,   8 },
    {   8,  32 },
    {  32,  32 },
    {   8,  32 },
    {   8,  32 },
    {   8,  32 },
    {  32,  32 },
    {   8,  56 },
    {   8,  32 },
    {   8,  32 },
    {  32,  32 },
    {  56,  24 },
    {  32,  24 },
    {  32,  24 },
    {  32,   0 },
    {  56, -24 },
    {  56,  24 },
    {  32,  24 },
    {  32,   0 },
    {   8,  24 },
    {  32,  24 },
    {  32,  24 },
    {  32,   0 },
    {  32,  24 },
    {  32,  24 },
    {  32,  24 },
    {  32,   0 },
    {  24,   0 },
    {  24,   0 },
    {  24,   0 },
    {   0,   0 },
    {  24, -24 },
    {  24,   0 },
    {  24,   0 },
    {   0,   0 },
    { -24, -24 },
    {  24, -24 },
    {  24,   0 },
    {   0,   0 },
    {  24,  24 },
    {  24,   0 },
    {  24,   0 },
    {   0,   0 },
    {  24,   8 },
    {   0,   8 },
    {   0,   8 },
    {   0,  32 },
    {   0,   8 },
    {   0,   8 },
    {   0,   8 },
    {   0,  32 },
    { -24,   8 },
    {   0,   8 },
    {   0,   8 },
    {   0,  32 },
    { -24,  56 },
    { -24,   8 },
    {   0,   8 },
    {   0,  32 },
};

/** rct2: 0x00981F4C, 0x00981F4E */
static constexpr const CoordsXY _WatchingPositionOffsets[] = {
    {  7,  5 },
    {  5, 25 },
    { 25,  5 },
    {  5,  7 },
    {  7,  9 },
    {  9, 25 },
    { 25,  9 },
    {  9,  7 },
    {  7, 23 },
    { 23, 25 },
    { 25, 23 },
    { 23,  7 },
    {  7, 27 },
    { 27, 25 },
    { 25, 27 },
    { 27,  7 },
    {  7,  0 },
    {  0, 25 },
    { 25,  0 },
    {  0,  7 },
    {  7,  0 },
    {  0, 25 },
    { 25,  0 },
    {  0,  7 },
    {  7,  0 },
    {  0, 25 },
    { 25,  0 },
    {  0,  7 },
    {  7,  0 },
    {  0, 25 },
    { 25,  0 },
    {  0,  7 },
};

static constexpr const ride_rating NauseaMaximumThresholds[] = {
    300, 600, 800, 1000
};

/** rct2: 009823AC */
static constexpr const PeepThoughtType crowded_thoughts[] = {
    PeepThoughtType::Lost,
    PeepThoughtType::Tired,
    PeepThoughtType::BadLitter,
    PeepThoughtType::Hungry,
    PeepThoughtType::Thirsty,
    PeepThoughtType::VeryClean,
    PeepThoughtType::Crowded,
    PeepThoughtType::Scenery,
    PeepThoughtType::VeryClean,
    PeepThoughtType::Music,
    PeepThoughtType::Watched,
    PeepThoughtType::NotHungry,
    PeepThoughtType::NotThirsty,
    PeepThoughtType::Toilet,
    PeepThoughtType::None,
    PeepThoughtType::None,
};

static constexpr const char *gPeepEasterEggNames[] = {
    "MICHAEL SCHUMACHER",
    "JACQUES VILLENEUVE",
    "DAMON HILL",
    "MR BEAN",
    "CHRIS SAWYER",
    "KATIE BRAYSHAW",
    "MELANIE WARN",
    "SIMON FOSTER",
    "JOHN WARDLEY",
    "LISA STIRLING",
    "DONALD MACRAE",
    "KATHERINE MCGOWAN",
    "FRANCES MCGOWAN",
    "CORINA MASSOURA",
    "CAROL YOUNG",
    "MIA SHERIDAN",
    "KATIE RODGER",
    "EMMA GARRELL",
    "JOANNE BARTON",
    "FELICITY ANDERSON",
    "KATIE SMITH",
    "EILIDH BELL",
    "NANCY STILLWAGON",
    "DAVID ELLIS"
};

// These arrays contain the base minimum and maximum nausea ratings for peeps, based on their nausea tolerance level.
static constexpr const ride_rating NauseaMinimumThresholds[] = {
    0, 0, 200, 400
};
// clang-format on

static bool peep_has_voucher_for_free_ride(Peep* peep, Ride* ride);
static void peep_ride_is_too_intense(Guest* peep, Ride* ride, bool peepAtRide);
static void peep_reset_ride_heading(Peep* peep);
static void peep_tried_to_enter_full_queue(Peep* peep, Ride* ride);
static int16_t peep_calculate_ride_satisfaction(Guest* peep, Ride* ride);
static void peep_update_favourite_ride(Peep* peep, Ride* ride);
static int16_t peep_calculate_ride_value_satisfaction(Peep* peep, Ride* ride);
static int16_t peep_calculate_ride_intensity_nausea_satisfaction(Peep* peep, Ride* ride);
static void peep_update_ride_nausea_growth(Peep* peep, Ride* ride);
static bool peep_should_go_on_ride_again(Peep* peep, Ride* ride);
static bool peep_should_preferred_intensity_increase(Peep* peep);
static bool peep_really_liked_ride(Peep* peep, Ride* ride);
static PeepThoughtType peep_assess_surroundings(int16_t centre_x, int16_t centre_y, int16_t centre_z);
static void peep_update_hunger(Peep* peep);
static void peep_decide_whether_to_leave_park(Peep* peep);
static void peep_leave_park(Peep* peep);
static void peep_head_for_nearest_ride_type(Guest* peep, int32_t rideType);
static void peep_head_for_nearest_ride_with_flags(Guest* peep, int32_t rideTypeFlags);
bool loc_690FD0(Peep* peep, ride_id_t* rideToView, uint8_t* rideSeatToView, TileElement* tileElement);

template<> bool SpriteBase::Is<Guest>() const
{
    return Type == EntityType::Guest;
}

bool Guest::GuestHasValidXY() const
{
    if (x != LOCATION_NULL)
    {
        if (map_is_location_valid({ x, y }))
        {
            return true;
        }
    }

    return false;
}

void Guest::ApplyEasterEggToNearbyGuests(easter_egg_function easter_egg)
{
    if (!GuestHasValidXY())
        return;

    for (auto* otherGuest : EntityTileList<Guest>({ x, y }))
    {
        auto zDiff = std::abs(otherGuest->z - z);
        if (zDiff <= 32)
        {
            (*this.*easter_egg)(otherGuest);
        }
    }
}

void Guest::GivePassingPeepsPurpleClothes(Guest* passingPeep)
{
    passingPeep->TshirtColour = COLOUR_BRIGHT_PURPLE;
    passingPeep->TrousersColour = COLOUR_BRIGHT_PURPLE;
    passingPeep->Invalidate();
}

void Guest::GivePassingPeepsPizza(Guest* passingPeep)
{
    if (passingPeep->HasItem(ShopItem::Pizza))
        return;

    passingPeep->GiveItem(ShopItem::Pizza);

    int32_t peepDirection = (sprite_direction >> 3) ^ 2;
    int32_t otherPeepOppositeDirection = passingPeep->sprite_direction >> 3;
    if (peepDirection == otherPeepOppositeDirection)
    {
        if (passingPeep->Action == PeepActionType::None1 || passingPeep->Action == PeepActionType::None2)
        {
            passingPeep->Action = PeepActionType::Wave2;
            passingPeep->ActionFrame = 0;
            passingPeep->ActionSpriteImageOffset = 0;
            passingPeep->UpdateCurrentActionSpriteType();
        }
    }
}

void Guest::MakePassingPeepsSick(Guest* passingPeep)
{
    if (this == passingPeep)
        return;
    if (passingPeep->State != PeepState::Walking)
        return;

    if (passingPeep->Action == PeepActionType::None1 || passingPeep->Action == PeepActionType::None2)
    {
        passingPeep->Action = PeepActionType::ThrowUp;
        passingPeep->ActionFrame = 0;
        passingPeep->ActionSpriteImageOffset = 0;
        passingPeep->UpdateCurrentActionSpriteType();
    }
}

void Guest::GivePassingPeepsIceCream(Guest* passingPeep)
{
    if (this == passingPeep)
        return;
    if (passingPeep->HasItem(ShopItem::IceCream))
        return;

    passingPeep->GiveItem(ShopItem::IceCream);
    passingPeep->UpdateSpriteType();
}

/**
 *
 *  rct2: 0x0068FD3A
 */
void Guest::UpdateEasterEggInteractions()
{
    if (PeepFlags & PEEP_FLAGS_PURPLE)
    {
        ApplyEasterEggToNearbyGuests(&Guest::GivePassingPeepsPurpleClothes);
    }

    if (PeepFlags & PEEP_FLAGS_PIZZA)
    {
        ApplyEasterEggToNearbyGuests(&Guest::GivePassingPeepsPizza);
    }

    if (PeepFlags & PEEP_FLAGS_CONTAGIOUS)
    {
        ApplyEasterEggToNearbyGuests(&Guest::MakePassingPeepsSick);
    }

    if (PeepFlags & PEEP_FLAGS_JOY)
    {
        if (scenario_rand() <= 1456)
        {
            if (Action == PeepActionType::None1 || Action == PeepActionType::None2)
            {
                Action = PeepActionType::Joy;
                ActionFrame = 0;
                ActionSpriteImageOffset = 0;
                UpdateCurrentActionSpriteType();
            }
        }
    }

    if (PeepFlags & PEEP_FLAGS_ICE_CREAM)
    {
        ApplyEasterEggToNearbyGuests(&Guest::GivePassingPeepsIceCream);
    }
}

int32_t Guest::GetEasterEggNameId() const
{
    char buffer[256]{};

    Formatter ft;
    FormatNameTo(ft);
    format_string(buffer, sizeof(buffer), STR_STRINGID, ft.Data());

    for (uint32_t i = 0; i < std::size(gPeepEasterEggNames); i++)
        if (_stricmp(buffer, gPeepEasterEggNames[i]) == 0)
            return static_cast<int32_t>(i);

    return -1;
}

void Guest::HandleEasterEggName()
{
    PeepFlags &= ~PEEP_FLAGS_WAVING;
    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_KATIE_BRAYSHAW))
    {
        PeepFlags |= PEEP_FLAGS_WAVING;
    }

    PeepFlags &= ~PEEP_FLAGS_PHOTO;
    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_CHRIS_SAWYER))
    {
        PeepFlags |= PEEP_FLAGS_PHOTO;
    }

    PeepFlags &= ~PEEP_FLAGS_PAINTING;
    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_SIMON_FOSTER))
    {
        PeepFlags |= PEEP_FLAGS_PAINTING;
    }

    PeepFlags &= ~PEEP_FLAGS_WOW;
    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_JOHN_WARDLEY))
    {
        PeepFlags |= PEEP_FLAGS_WOW;
    }

    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_MELANIE_WARN))
    {
        Happiness = 250;
        HappinessTarget = 250;
        Energy = 127;
        EnergyTarget = 127;
        Nausea = 0;
        NauseaTarget = 0;
    }

    PeepFlags &= ~PEEP_FLAGS_LITTER;
    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_LISA_STIRLING))
    {
        PeepFlags |= PEEP_FLAGS_LITTER;
    }

    PeepFlags &= ~PEEP_FLAGS_LOST;
    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_DONALD_MACRAE))
    {
        PeepFlags |= PEEP_FLAGS_LOST;
    }

    PeepFlags &= ~PEEP_FLAGS_HUNGER;
    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_KATHERINE_MCGOWAN))
    {
        PeepFlags |= PEEP_FLAGS_HUNGER;
    }

    PeepFlags &= ~PEEP_FLAGS_TOILET;
    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_FRANCES_MCGOWAN))
    {
        PeepFlags |= PEEP_FLAGS_TOILET;
    }

    PeepFlags &= ~PEEP_FLAGS_CROWDED;
    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_CORINA_MASSOURA))
    {
        PeepFlags |= PEEP_FLAGS_CROWDED;
    }

    PeepFlags &= ~PEEP_FLAGS_HAPPINESS;
    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_CAROL_YOUNG))
    {
        PeepFlags |= PEEP_FLAGS_HAPPINESS;
    }

    PeepFlags &= ~PEEP_FLAGS_NAUSEA;
    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_MIA_SHERIDAN))
    {
        PeepFlags |= PEEP_FLAGS_NAUSEA;
    }

    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_KATIE_RODGER))
    {
        PeepFlags |= PEEP_FLAGS_LEAVING_PARK;
        PeepFlags &= ~PEEP_FLAGS_PARK_ENTRANCE_CHOSEN;
    }

    PeepFlags &= ~PEEP_FLAGS_PURPLE;
    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_EMMA_GARRELL))
    {
        PeepFlags |= PEEP_FLAGS_PURPLE;
    }

    PeepFlags &= ~PEEP_FLAGS_PIZZA;
    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_JOANNE_BARTON))
    {
        PeepFlags |= PEEP_FLAGS_PIZZA;
    }

    PeepFlags &= ~PEEP_FLAGS_CONTAGIOUS;
    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_FELICITY_ANDERSON))
    {
        PeepFlags |= PEEP_FLAGS_CONTAGIOUS;
    }

    PeepFlags &= ~PEEP_FLAGS_JOY;
    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_KATIE_SMITH))
    {
        PeepFlags |= PEEP_FLAGS_JOY;
    }

    PeepFlags &= ~PEEP_FLAGS_ANGRY;
    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_EILIDH_BELL))
    {
        PeepFlags |= PEEP_FLAGS_ANGRY;
    }

    PeepFlags &= ~PEEP_FLAGS_ICE_CREAM;
    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_NANCY_STILLWAGON))
    {
        PeepFlags |= PEEP_FLAGS_ICE_CREAM;
    }

    PeepFlags &= ~PEEP_FLAGS_HERE_WE_ARE;
    if (CheckEasterEggName(EASTEREGG_PEEP_NAME_DAVID_ELLIS))
    {
        PeepFlags |= PEEP_FLAGS_HERE_WE_ARE;
    }
}

/**
 *
 *  rct2: 0x0069A5A0
 * tests if a peep's name matches a cheat code, normally returns using a register flag
 */
int32_t Guest::CheckEasterEggName(int32_t index) const
{
    char buffer[256]{};

    Formatter ft;
    FormatNameTo(ft);
    format_string(buffer, sizeof(buffer), STR_STRINGID, ft.Data());

    return _stricmp(buffer, gPeepEasterEggNames[index]) == 0;
}

void Guest::loc_68F9F3()
{
    // Idle peep happiness tends towards 127 (50%).
    if (HappinessTarget >= 128)
        HappinessTarget--;
    else
        HappinessTarget++;

    NauseaTarget = std::max(NauseaTarget - 2, 0);

    if (Energy <= 50)
    {
        Energy = std::max(Energy - 2, 0);
    }

    if (Hunger < 10)
    {
        Hunger = std::max(Hunger - 1, 0);
    }

    if (Thirst < 10)
    {
        Thirst = std::max(Thirst - 1, 0);
    }

    if (Toilet >= 195)
    {
        Toilet--;
    }

    if (State == PeepState::Walking && NauseaTarget >= 128)
    {
        if ((scenario_rand() & 0xFF) <= static_cast<uint8_t>((Nausea - 128) / 2))
        {
            if (Action >= PeepActionType::None1)
            {
                Action = PeepActionType::ThrowUp;
                ActionFrame = 0;
                ActionSpriteImageOffset = 0;
                UpdateCurrentActionSpriteType();
            }
        }
    }
}

void Guest::loc_68FA89()
{
    // 68FA89
    if (TimeToConsume == 0 && HasFoodOrDrink())
    {
        TimeToConsume += 3;
    }

    if (TimeToConsume != 0 && State != PeepState::OnRide)
    {
        TimeToConsume = std::max(TimeToConsume - 3, 0);

        if (HasDrink())
        {
            Thirst = std::min(Thirst + 7, 255);
        }
        else
        {
            Hunger = std::min(Hunger + 7, 255);
            Thirst = std::max(Thirst - 3, 0);
            Toilet = std::min(Toilet + 2, 255);
        }

        if (TimeToConsume == 0)
        {
            int32_t chosen_food = bitscanforward(GetFoodOrDrinkFlags());
            if (chosen_food != -1)
            {
                ShopItem food = ShopItem(chosen_food);
                RemoveItem(food);

                auto discardContainer = GetShopItemDescriptor(food).DiscardContainer;
                if (discardContainer != ShopItem::None)
                {
                    GiveItem(discardContainer);
                }

                WindowInvalidateFlags |= PEEP_INVALIDATE_PEEP_INVENTORY;
                UpdateSpriteType();
            }
        }
    }

    uint8_t newEnergy = Energy;
    uint8_t newTargetEnergy = EnergyTarget;
    if (newEnergy >= newTargetEnergy)
    {
        newEnergy -= 2;
        if (newEnergy < newTargetEnergy)
            newEnergy = newTargetEnergy;
    }
    else
    {
        newEnergy = std::min(PEEP_MAX_ENERGY_TARGET, newEnergy + 4);
        if (newEnergy > newTargetEnergy)
            newEnergy = newTargetEnergy;
    }

    if (newEnergy < PEEP_MIN_ENERGY)
        newEnergy = PEEP_MIN_ENERGY;

    /* Previous code here suggested maximum energy is 128. */
    newEnergy = std::min(static_cast<uint8_t>(PEEP_MAX_ENERGY), newEnergy);

    if (newEnergy != Energy)
    {
        Energy = newEnergy;
        WindowInvalidateFlags |= PEEP_INVALIDATE_PEEP_2;
    }

    uint8_t newHappiness = Happiness;
    uint8_t newHappinessGrowth = HappinessTarget;
    if (newHappiness >= newHappinessGrowth)
    {
        newHappiness = std::max(newHappiness - 4, 0);
        if (newHappiness < newHappinessGrowth)
            newHappiness = newHappinessGrowth;
    }
    else
    {
        newHappiness = std::min(255, newHappiness + 4);
        if (newHappiness > newHappinessGrowth)
            newHappiness = newHappinessGrowth;
    }

    if (newHappiness != Happiness)
    {
        Happiness = newHappiness;
        WindowInvalidateFlags |= PEEP_INVALIDATE_PEEP_2;
    }

    uint8_t newNausea = Nausea;
    uint8_t newNauseaGrowth = NauseaTarget;
    if (newNausea >= newNauseaGrowth)
    {
        newNausea = std::max(newNausea - 4, 0);
        if (newNausea < newNauseaGrowth)
            newNausea = newNauseaGrowth;
    }
    else
    {
        newNausea = std::min(255, newNausea + 4);
        if (newNausea > newNauseaGrowth)
            newNausea = newNauseaGrowth;
    }

    if (newNausea != Nausea)
    {
        Nausea = newNausea;
        WindowInvalidateFlags |= PEEP_INVALIDATE_PEEP_2;
    }
}

void Guest::Tick128UpdateGuest(int32_t index)
{
    if (static_cast<uint32_t>(index & 0x1FF) == (gCurrentTicks & 0x1FF))
    {
        /* Effect of masking with 0x1FF here vs mask 0x7F,
         * which is the condition for calling this function, is
         * to reduce how often the content in this conditional
         * is executed to once every four calls. */
        if (PeepFlags & PEEP_FLAGS_CROWDED)
        {
            PeepThoughtType thought_type = crowded_thoughts[scenario_rand() & 0xF];
            if (thought_type != PeepThoughtType::None)
            {
                InsertNewThought(thought_type, PEEP_THOUGHT_ITEM_NONE);
            }
        }

        if (PeepFlags & PEEP_FLAGS_EXPLODE && x != LOCATION_NULL)
        {
            if (State == PeepState::Walking || State == PeepState::Sitting)
            {
                OpenRCT2::Audio::Play3D(OpenRCT2::Audio::SoundId::Crash, { x, y, z });

                sprite_misc_explosion_cloud_create({ x, y, z + 16 });
                sprite_misc_explosion_flare_create({ x, y, z + 16 });

                Remove();
                return;
            }
            else
            {
                PeepFlags &= ~PEEP_FLAGS_EXPLODE;
            }
        }

        if (PeepFlags & PEEP_FLAGS_HUNGER)
        {
            if (Hunger >= 15)
                Hunger -= 15;
        }

        if (PeepFlags & PEEP_FLAGS_TOILET)
        {
            if (Toilet <= 180)
                Toilet += 50;
        }

        if (PeepFlags & PEEP_FLAGS_HAPPINESS)
        {
            HappinessTarget = 5;
        }

        if (PeepFlags & PEEP_FLAGS_NAUSEA)
        {
            NauseaTarget = 200;
            if (Nausea <= 130)
                Nausea = 130;
        }

        if (Angriness != 0)
            Angriness--;

        if (State == PeepState::Walking || State == PeepState::Sitting)
        {
            SurroundingsThoughtTimeout++;
            if (SurroundingsThoughtTimeout >= 18)
            {
                SurroundingsThoughtTimeout = 0;
                if (x != LOCATION_NULL)
                {
                    PeepThoughtType thought_type = peep_assess_surroundings(x & 0xFFE0, y & 0xFFE0, z);

                    if (thought_type != PeepThoughtType::None)
                    {
                        InsertNewThought(thought_type, PEEP_THOUGHT_ITEM_NONE);
                        HappinessTarget = std::min(PEEP_MAX_HAPPINESS, HappinessTarget + 45);
                    }
                }
            }
        }

        UpdateSpriteType();

        if (State == PeepState::OnRide || State == PeepState::EnteringRide)
        {
            GuestTimeOnRide = std::min(255, GuestTimeOnRide + 1);

            if (PeepFlags & PEEP_FLAGS_WOW)
            {
                InsertNewThought(PeepThoughtType::Wow2, PEEP_THOUGHT_ITEM_NONE);
            }

            if (GuestTimeOnRide > 15)
            {
                HappinessTarget = std::max(0, HappinessTarget - 5);

                if (GuestTimeOnRide > 22)
                {
                    auto ride = get_ride(CurrentRide);
                    if (ride != nullptr)
                    {
                        PeepThoughtType thought_type = ride->GetRideTypeDescriptor().HasFlag(RIDE_TYPE_FLAG_IN_RIDE)
                            ? PeepThoughtType::GetOut
                            : PeepThoughtType::GetOff;

                        InsertNewThought(thought_type, CurrentRide);
                    }
                }
            }
        }

        if (State == PeepState::Walking && !OutsideOfPark && !(PeepFlags & PEEP_FLAGS_LEAVING_PARK) && GuestNumRides == 0
            && GuestHeadingToRideId == RIDE_ID_NULL)
        {
            uint32_t time_duration = gScenarioTicks - ParkEntryTime;
            time_duration /= 2048;

            if (time_duration >= 5)
            {
                PickRideToGoOn();

                if (GuestHeadingToRideId == RIDE_ID_NULL)
                {
                    HappinessTarget = std::max(HappinessTarget - 128, 0);
                    peep_leave_park(this);
                    peep_update_hunger(this);
                    loc_68F9F3();
                    loc_68FA89();
                    return;
                }
            }
        }

        if ((scenario_rand() & 0xFFFF) <= ((HasItem(ShopItem::Map)) ? 8192U : 2184U))
        {
            PickRideToGoOn();
        }

        if (static_cast<uint32_t>(index & 0x3FF) == (gCurrentTicks & 0x3FF))
        {
            /* Effect of masking with 0x3FF here vs mask 0x1FF,
             * which is used in the encompassing conditional, is
             * to reduce how often the content in this conditional
             * is executed to once every second time the encompassing
             * conditional executes. */

            if (!OutsideOfPark && (State == PeepState::Walking || State == PeepState::Sitting))
            {
                uint8_t num_thoughts = 0;
                PeepThoughtType possible_thoughts[5];

                if (PeepFlags & PEEP_FLAGS_LEAVING_PARK)
                {
                    possible_thoughts[num_thoughts++] = PeepThoughtType::GoHome;
                }
                else
                {
                    if (Energy <= 70 && Happiness < 128)
                    {
                        possible_thoughts[num_thoughts++] = PeepThoughtType::Tired;
                    }

                    if (Hunger <= 10 && !HasFoodOrDrink())
                    {
                        possible_thoughts[num_thoughts++] = PeepThoughtType::Hungry;
                    }

                    if (Thirst <= 25 && !HasFoodOrDrink())
                    {
                        possible_thoughts[num_thoughts++] = PeepThoughtType::Thirsty;
                    }

                    if (Toilet >= 160)
                    {
                        possible_thoughts[num_thoughts++] = PeepThoughtType::Toilet;
                    }

                    if (!(gParkFlags & PARK_FLAGS_NO_MONEY) && CashInPocket <= MONEY(9, 00) && Happiness >= 105 && Energy >= 70)
                    {
                        /* The energy check was originally a second check on happiness.
                         * This was superfluous so should probably check something else.
                         * Guessed that this should really be checking energy, since
                         * the addresses for happiness and energy are quite close,
                         * 70 is also the threshold for tired thoughts (see above) and
                         * it makes sense that a tired peep might not think about getting
                         * more money. */
                        possible_thoughts[num_thoughts++] = PeepThoughtType::RunningOut;
                    }
                }

                if (num_thoughts != 0)
                {
                    PeepThoughtType chosen_thought = possible_thoughts[scenario_rand() % num_thoughts];

                    InsertNewThought(chosen_thought, PEEP_THOUGHT_ITEM_NONE);

                    switch (chosen_thought)
                    {
                        case PeepThoughtType::Hungry:
                            peep_head_for_nearest_ride_with_flags(this, RIDE_TYPE_FLAG_SELLS_FOOD);
                            break;
                        case PeepThoughtType::Thirsty:
                            peep_head_for_nearest_ride_with_flags(this, RIDE_TYPE_FLAG_SELLS_DRINKS);
                            break;
                        case PeepThoughtType::Toilet:
                            peep_head_for_nearest_ride_with_flags(this, RIDE_TYPE_FLAG_IS_TOILET);
                            break;
                        case PeepThoughtType::RunningOut:
                            peep_head_for_nearest_ride_type(this, RIDE_TYPE_CASH_MACHINE);
                            break;
                        default:
                            break;
                    }
                }
            }
        }
        else
        {
            /* This branch of the conditional is executed on the
             * remaining times the encompassing conditional is
             * executed (which is also every second time, but
             * the alternate time to the true branch). */
            if (Nausea >= 140)
            {
                PeepThoughtType thought_type = PeepThoughtType::Sick;
                if (Nausea >= 200)
                {
                    thought_type = PeepThoughtType::VerySick;
                    peep_head_for_nearest_ride_type(this, RIDE_TYPE_FIRST_AID);
                }
                InsertNewThought(thought_type, PEEP_THOUGHT_ITEM_NONE);
            }
        }

        switch (State)
        {
            case PeepState::Walking:
            case PeepState::LeavingPark:
            case PeepState::EnteringPark:
                peep_decide_whether_to_leave_park(this);
                peep_update_hunger(this);
                break;

            case PeepState::Sitting:
                if (EnergyTarget <= 135)
                    EnergyTarget += 5;

                if (Thirst >= 5)
                {
                    Thirst -= 4;
                    Toilet = std::min(255, Toilet + 3);
                }

                if (NauseaTarget >= 50)
                    NauseaTarget -= 6;

                // In the original this branched differently
                // but it would mean setting the peep happiness from
                // a thought type entry which i think is incorrect.
                peep_update_hunger(this);
                break;

            case PeepState::Queuing:
                if (TimeInQueue >= 2000)
                {
                    /* Peep happiness is affected once the peep has been waiting
                     * too long in a queue. */
                    bool found = false;
                    for (auto* pathElement : TileElementsView<PathElement>(NextLoc))
                    {
                        if (pathElement->GetBaseZ() != NextLoc.z)
                            continue;

                        // Check if the footpath has a queue line TV monitor on it
                        if (pathElement->HasAddition() && !pathElement->AdditionIsGhost())
                        {
                            auto pathSceneryIndex = pathElement->GetAdditionEntryIndex();
                            rct_scenery_entry* sceneryEntry = get_footpath_item_entry(pathSceneryIndex);
                            if (sceneryEntry != nullptr && (sceneryEntry->path_bit.flags & PATH_BIT_FLAG_IS_QUEUE_SCREEN))
                            {
                                found = true;
                            }
                        }
                        break;
                    }

                    if (found)
                    {
                        /* Queue line TV monitors make the peeps waiting in the queue
                         * slowly happier, up to a certain level. */
                        /* Why don't queue line TV monitors start affecting the peeps
                         * as soon as they join the queue?? */
                        if (HappinessTarget < 90)
                            HappinessTarget = 90;

                        if (HappinessTarget < 165)
                            HappinessTarget += 2;
                    }
                    else
                    {
                        /* Without a queue line TV monitor peeps waiting too long
                         * in a queue get less happy. */
                        HappinessTarget = std::max(HappinessTarget - 4, 0);
                    }
                }
                peep_update_hunger(this);
                break;
            case PeepState::EnteringRide:
                if (SubState == 17 || SubState == 15)
                {
                    peep_decide_whether_to_leave_park(this);
                }
                peep_update_hunger(this);
                break;
            default:
                break;
        }

        loc_68F9F3();
    }

    loc_68FA89();
}

/**
 *
 *  rct2: 0x00691677
 */
void Guest::TryGetUpFromSitting()
{
    // Eats all food first
    if (HasFoodOrDrink())
        return;

    TimeToSitdown--;
    if (TimeToSitdown)
        return;

    SetState(PeepState::Walking);

    // Set destination to the centre of the tile.
    auto destination = GetLocation().ToTileCentre();
    SetDestination(destination, 5);
    UpdateCurrentActionSpriteType();
}

/**
 *
 *  rct2: 0x0069152B
 */
void Guest::UpdateSitting()
{
    if (SittingSubState == PeepSittingSubState::TryingToSit)
    {
        if (!CheckForPath())
            return;
        // 691541

        uint8_t pathingResult;
        PerformNextAction(pathingResult);
        if (!(pathingResult & PATHING_DESTINATION_REACHED))
            return;

        auto loc = CoordsXYZ{ x, y, z }.ToTileStart() + CoordsXYZ{ BenchUseOffsets[Var37 & 0x7], 0 };

        MoveTo(loc);

        sprite_direction = ((Var37 + 2) & 3) * 8;
        Action = PeepActionType::None1;
        NextActionSpriteType = PeepActionSpriteType::SittingIdle;
        SwitchNextActionSpriteType();

        SittingSubState = PeepSittingSubState::SatDown;

        // Sets time to sit on seat
        TimeToSitdown = (129 - Energy) * 16 + 50;
    }
    else if (SittingSubState == PeepSittingSubState::SatDown)
    {
        if (Action < PeepActionType::None1)
        {
            UpdateAction();
            if (Action != PeepActionType::None2)
                return;

            Action = PeepActionType::None1;
            TryGetUpFromSitting();
            return;
        }

        if ((PeepFlags & PEEP_FLAGS_LEAVING_PARK))
        {
            SetState(PeepState::Walking);

            // Set destination to the centre of the tile
            auto destination = GetLocation().ToTileCentre();
            SetDestination(destination, 5);
            UpdateCurrentActionSpriteType();
            return;
        }

        if (SpriteType == PeepSpriteType::Umbrella)
        {
            TryGetUpFromSitting();
            return;
        }

        if (HasFoodOrDrink())
        {
            if ((scenario_rand() & 0xFFFF) > 1310)
            {
                TryGetUpFromSitting();
                return;
            }
            Action = PeepActionType::SittingEatFood;
            ActionFrame = 0;
            ActionSpriteImageOffset = 0;
            UpdateCurrentActionSpriteType();
            return;
        }

        int32_t rand = scenario_rand();
        if ((rand & 0xFFFF) > 131)
        {
            TryGetUpFromSitting();
            return;
        }
        if (SpriteType == PeepSpriteType::Balloon || SpriteType == PeepSpriteType::Hat)
        {
            TryGetUpFromSitting();
            return;
        }

        Action = PeepActionType::SittingLookAroundLeft;
        if (rand & 0x80000000)
        {
            Action = PeepActionType::SittingLookAroundRight;
        }

        if (rand & 0x40000000)
        {
            Action = PeepActionType::SittingCheckWatch;
        }
        ActionFrame = 0;
        ActionSpriteImageOffset = 0;
        UpdateCurrentActionSpriteType();
        return;
    }
}

/**
 * To simplify check of 0x36BA3E0 and 0x11FF78
 * returns false on no food.
 */
int64_t Guest::GetFoodOrDrinkFlags() const
{
    return GetItemFlags() & (ShopItemsGetAllFoods() | ShopItemsGetAllDrinks());
}

int64_t Guest::GetEmptyContainerFlags() const
{
    return GetItemFlags() & ShopItemsGetAllContainers();
}

bool Guest::HasFoodOrDrink() const
{
    return GetFoodOrDrinkFlags() != 0;
}

/**
 * To simplify check of NOT(0x12BA3C0 and 0x118F48)
 * returns 0 on no food.
 */
bool Guest::HasDrink() const
{
    return GetItemFlags() & ShopItemsGetAllDrinks();
}

bool Guest::HasEmptyContainer() const
{
    return GetEmptyContainerFlags() != 0;
}

/**
 *
 *  rct2: 0x69C308
 * Check if lost.
 */
void Guest::CheckIfLost()
{
    if (!(PeepFlags & PEEP_FLAGS_LOST))
    {
        if (ride_get_count() < 2)
            return;
        PeepFlags ^= PEEP_FLAGS_21;

        if (!(PeepFlags & PEEP_FLAGS_21))
            return;

        TimeLost++;
        if (TimeLost != 254)
            return;
        TimeLost = 230;
    }
    InsertNewThought(PeepThoughtType::Lost, PEEP_THOUGHT_ITEM_NONE);

    HappinessTarget = std::max(HappinessTarget - 30, 0);
}

/**
 *
 *  rct2: 0x69C26B
 * Check if can't find ride.
 */
void Guest::CheckCantFindRide()
{
    if (GuestHeadingToRideId == RIDE_ID_NULL)
        return;

    // Peeps will think "I can't find ride X" twice before giving up completely.
    if (GuestIsLostCountdown == 30 || GuestIsLostCountdown == 60)
    {
        InsertNewThought(PeepThoughtType::CantFind, GuestHeadingToRideId);
        HappinessTarget = std::max(HappinessTarget - 30, 0);
    }

    GuestIsLostCountdown--;
    if (GuestIsLostCountdown != 0)
        return;

    GuestHeadingToRideId = RIDE_ID_NULL;
    rct_window* w = window_find_by_number(WC_PEEP, sprite_index);

    if (w)
    {
        window_event_invalidate_call(w);
    }

    window_invalidate_by_number(WC_PEEP, sprite_index);
}

/**
 *
 *  rct2: 0x69C2D0
 * Check if can't find exit.
 */
void Guest::CheckCantFindExit()
{
    if (!(PeepFlags & PEEP_FLAGS_LEAVING_PARK))
        return;

    // Peeps who can't find the park exit will continue to get less happy until they find it.
    if (GuestIsLostCountdown == 1)
    {
        InsertNewThought(PeepThoughtType::CantFindExit, PEEP_THOUGHT_ITEM_NONE);
        HappinessTarget = std::max(HappinessTarget - 30, 0);
    }

    if (--GuestIsLostCountdown == 0)
        GuestIsLostCountdown = 90;
}

/** Main logic to decide whether a peep should buy an item in question
 *
 * Also handles the purchase as well, so once it returns, the peep will have the
 * item and the money will have been deducted.
 *
 * eax: shopItem | (rideIndex << 8)
 * ecx: price
 * esi: *peep
 *
 * Returns 0 or 1 depending on if the peep decided to buy the item
 *
 *  rct2: 0x0069AF1E
 */
bool Guest::DecideAndBuyItem(Ride* ride, ShopItem shopItem, money32 price)
{
    money32 itemValue;

    bool hasVoucher = false;

    bool isRainingAndUmbrella = shopItem == ShopItem::Umbrella && climate_is_raining();

    if ((HasItem(ShopItem::Voucher)) && (VoucherType == VOUCHER_TYPE_FOOD_OR_DRINK_FREE) && (VoucherShopItem == shopItem))
    {
        hasVoucher = true;
    }

    if (HasItem(shopItem))
    {
        InsertNewThought(PeepThoughtType::AlreadyGot, EnumValue(shopItem));
        return false;
    }

    if (GetShopItemDescriptor(shopItem).IsFoodOrDrink())
    {
        int32_t food = bitscanforward(GetFoodOrDrinkFlags());
        if (food != -1)
        {
            InsertNewThought(PeepThoughtType::HaventFinished, food);
            return false;
        }
        else if (Nausea >= 145)
            return false;
    }

    if ((shopItem == ShopItem::Balloon || shopItem == ShopItem::IceCream || shopItem == ShopItem::Candyfloss
         || shopItem == ShopItem::Sunglasses)
        && climate_is_raining())
    {
        return false;
    }

    if ((shopItem == ShopItem::Sunglasses || shopItem == ShopItem::IceCream) && gClimateCurrent.Temperature < 12)
    {
        return false;
    }

    if (GetShopItemDescriptor(shopItem).IsFood() && (Hunger > 75))
    {
        InsertNewThought(PeepThoughtType::NotHungry, PEEP_THOUGHT_ITEM_NONE);
        return false;
    }

    if (GetShopItemDescriptor(shopItem).IsDrink() && (Thirst > 75))
    {
        InsertNewThought(PeepThoughtType::NotThirsty, PEEP_THOUGHT_ITEM_NONE);
        return false;
    }

    if (!isRainingAndUmbrella && (shopItem != ShopItem::Map) && GetShopItemDescriptor(shopItem).IsSouvenir() && !hasVoucher)
    {
        if (((scenario_rand() & 0x7F) + 0x73) > Happiness || GuestNumRides < 3)
            return false;
    }

    if (!hasVoucher)
    {
        if (price != 0 && !(gParkFlags & PARK_FLAGS_NO_MONEY))
        {
            if (CashInPocket == 0)
            {
                InsertNewThought(PeepThoughtType::SpentMoney, PEEP_THOUGHT_ITEM_NONE);
                return false;
            }
            if (price > CashInPocket)
            {
                InsertNewThought(PeepThoughtType::CantAffordItem, EnumValue(shopItem));
                return false;
            }
        }

        if (gClimateCurrent.Temperature >= 21)
            itemValue = GetShopItemDescriptor(shopItem).HotValue;
        else if (gClimateCurrent.Temperature <= 11)
            itemValue = GetShopItemDescriptor(shopItem).ColdValue;
        else
            itemValue = GetShopItemDescriptor(shopItem).BaseValue;

        if (itemValue < price)
        {
            itemValue -= price;

            if (!isRainingAndUmbrella)
            {
                itemValue = -itemValue;
                if (Happiness >= 128)
                {
                    itemValue /= 2;
                    if (Happiness >= 180)
                        itemValue /= 2;
                }
                if (itemValue > (static_cast<money16>(scenario_rand() & 0x07)))
                {
                    // "I'm not paying that much for x"
                    InsertNewThought(GetShopItemDescriptor(shopItem).TooMuchThought, ride->id);
                    return false;
                }
            }
        }
        else
        {
            itemValue -= price;
            itemValue = std::max(8, itemValue);

            if (!(gParkFlags & PARK_FLAGS_NO_MONEY))
            {
                if (itemValue >= static_cast<money32>(scenario_rand() & 0x07))
                {
                    // "This x is a really good value"
                    InsertNewThought(GetShopItemDescriptor(shopItem).GoodValueThought, ride->id);
                }
            }

            int32_t happinessGrowth = itemValue * 4;
            HappinessTarget = std::min((HappinessTarget + happinessGrowth), PEEP_MAX_HAPPINESS);
            Happiness = std::min((Happiness + happinessGrowth), PEEP_MAX_HAPPINESS);
        }

        // reset itemValue for satisfaction calculation
        if (gClimateCurrent.Temperature >= 21)
            itemValue = GetShopItemDescriptor(shopItem).HotValue;
        else if (gClimateCurrent.Temperature <= 11)
            itemValue = GetShopItemDescriptor(shopItem).ColdValue;
        else
            itemValue = GetShopItemDescriptor(shopItem).BaseValue;
        itemValue -= price;
        uint8_t satisfaction = 0;
        if (itemValue > -8)
        {
            satisfaction++;
            if (itemValue > -3)
            {
                satisfaction++;
                if (itemValue > 3)
                    satisfaction++;
            }
        }
        ride_update_satisfaction(ride, satisfaction);
    }

    // The peep has now decided to buy the item (or, specifically, has not been
    // dissuaded so far).
    GiveItem(shopItem);

    if (shopItem == ShopItem::TShirt)
        TshirtColour = ride->track_colour[0].main;

    if (shopItem == ShopItem::Hat)
        HatColour = ride->track_colour[0].main;

    if (shopItem == ShopItem::Balloon)
        BalloonColour = ride->track_colour[0].main;

    if (shopItem == ShopItem::Umbrella)
        UmbrellaColour = ride->track_colour[0].main;

    if (shopItem == ShopItem::Map)
        ResetPathfindGoal();

    uint16_t consumptionTime = GetShopItemDescriptor(shopItem).ConsumptionTime;
    TimeToConsume = std::min((TimeToConsume + consumptionTime), 255);

    if (shopItem == ShopItem::Photo)
        Photo1RideRef = ride->id;

    if (shopItem == ShopItem::Photo2)
        Photo2RideRef = ride->id;

    if (shopItem == ShopItem::Photo3)
        Photo3RideRef = ride->id;

    if (shopItem == ShopItem::Photo4)
        Photo4RideRef = ride->id;

    WindowInvalidateFlags |= PEEP_INVALIDATE_PEEP_INVENTORY;
    UpdateSpriteType();
    if (PeepFlags & PEEP_FLAGS_TRACKING)
    {
        auto ft = Formatter();
        FormatNameTo(ft);
        ft.Add<rct_string_id>(GetShopItemDescriptor(shopItem).Naming.Indefinite);
        if (gConfigNotifications.guest_bought_item)
        {
            News::AddItemToQueue(News::ItemType::PeepOnRide, STR_PEEP_TRACKING_NOTIFICATION_BOUGHT_X, sprite_index, ft);
        }
    }

    if (GetShopItemDescriptor(shopItem).IsFood())
        AmountOfFood++;

    if (GetShopItemDescriptor(shopItem).IsDrink())
        AmountOfDrinks++;

    if (GetShopItemDescriptor(shopItem).IsSouvenir())
        AmountOfSouvenirs++;

    money16* expend_type = &PaidOnSouvenirs;
    ExpenditureType expenditure = ExpenditureType::ShopStock;

    if (GetShopItemDescriptor(shopItem).IsFood())
    {
        expend_type = &PaidOnFood;
        expenditure = ExpenditureType::FoodDrinkStock;
    }

    if (GetShopItemDescriptor(shopItem).IsDrink())
    {
        expend_type = &PaidOnDrink;
        expenditure = ExpenditureType::FoodDrinkStock;
    }

    if (!(gParkFlags & PARK_FLAGS_NO_MONEY))
        finance_payment(GetShopItemDescriptor(shopItem).Cost, expenditure);

    // Sets the expenditure type to *_FOODDRINK_SALES or *_SHOP_SALES appropriately.
    expenditure = static_cast<ExpenditureType>(static_cast<int32_t>(expenditure) - 1);
    if (hasVoucher)
    {
        RemoveItem(ShopItem::Voucher);
        WindowInvalidateFlags |= PEEP_INVALIDATE_PEEP_INVENTORY;
    }
    else if (!(gParkFlags & PARK_FLAGS_NO_MONEY))
    {
        SpendMoney(*expend_type, price, expenditure);
    }
    ride->total_profit += (price - GetShopItemDescriptor(shopItem).Cost);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;
    ride->cur_num_customers++;
    ride->total_customers++;
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_CUSTOMER;

    return true;
}

/**
 * Updates various peep stats upon entering a ride, as well as updating the
 * ride's satisfaction value.
 *  rct2: 0x0069545B
 */
void Guest::OnEnterRide(ride_id_t rideIndex)
{
    auto ride = get_ride(rideIndex);
    if (ride == nullptr)
        return;

    // Calculate how satisfying the ride is for the peep. Can range from -140 to +105.
    int16_t satisfaction = peep_calculate_ride_satisfaction(this, ride);

    // Update the satisfaction stat of the ride.
    uint8_t rideSatisfaction = 0;
    if (satisfaction >= 40)
        rideSatisfaction = 3;
    else if (satisfaction >= 20)
        rideSatisfaction = 2;
    else if (satisfaction >= 0)
        rideSatisfaction = 1;

    ride_update_satisfaction(ride, rideSatisfaction);

    // Update various peep stats.
    if (GuestNumRides < 255)
        GuestNumRides++;

    SetHasRidden(ride);
    peep_update_favourite_ride(this, ride);
    HappinessTarget = std::clamp(HappinessTarget + satisfaction, 0, PEEP_MAX_HAPPINESS);
    peep_update_ride_nausea_growth(this, ride);
}

/**
 *
 *  rct2: 0x0069576E
 */
void Guest::OnExitRide(ride_id_t rideIndex)
{
    auto ride = get_ride(rideIndex);
    if (PeepFlags & PEEP_FLAGS_RIDE_SHOULD_BE_MARKED_AS_FAVOURITE)
    {
        PeepFlags &= ~PEEP_FLAGS_RIDE_SHOULD_BE_MARKED_AS_FAVOURITE;
        FavouriteRide = rideIndex;
        // TODO fix this flag name or add another one
        WindowInvalidateFlags |= PEEP_INVALIDATE_STAFF_STATS;
    }
    Happiness = HappinessTarget;
    Nausea = NauseaTarget;
    WindowInvalidateFlags |= PEEP_INVALIDATE_PEEP_STATS;

    if (PeepFlags & PEEP_FLAGS_LEAVING_PARK)
        PeepFlags &= ~(PEEP_FLAGS_PARK_ENTRANCE_CHOSEN);

    if (ride != nullptr && peep_should_go_on_ride_again(this, ride))
    {
        GuestHeadingToRideId = rideIndex;
        GuestIsLostCountdown = 200;
        ResetPathfindGoal();
        WindowInvalidateFlags |= PEEP_INVALIDATE_PEEP_ACTION;
    }

    if (peep_should_preferred_intensity_increase(this))
    {
        if (Intensity.GetMaximum() < 15)
        {
            Intensity = Intensity.WithMaximum(Intensity.GetMaximum() + 1);
        }
    }

    if (ride != nullptr && peep_really_liked_ride(this, ride))
    {
        InsertNewThought(PeepThoughtType::WasGreat, rideIndex);

        static constexpr OpenRCT2::Audio::SoundId laughs[3] = { OpenRCT2::Audio::SoundId::Laugh1,
                                                                OpenRCT2::Audio::SoundId::Laugh2,
                                                                OpenRCT2::Audio::SoundId::Laugh3 };
        int32_t laughType = scenario_rand() & 7;
        if (laughType < 3)
        {
            OpenRCT2::Audio::Play3D(laughs[laughType], { x, y, z });
        }
    }

    if (ride != nullptr)
    {
        ride->total_customers++;
        ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_CUSTOMER;
    }
}

/**
 *
 *  rct2: 0x00695DD2
 */
void Guest::PickRideToGoOn()
{
    if (State != PeepState::Walking)
        return;
    if (GuestHeadingToRideId != RIDE_ID_NULL)
        return;
    if (PeepFlags & PEEP_FLAGS_LEAVING_PARK)
        return;
    if (HasFoodOrDrink())
        return;
    if (x == LOCATION_NULL)
        return;

    auto ride = FindBestRideToGoOn();
    if (ride != nullptr)
    {
        // Head to that ride
        GuestHeadingToRideId = ride->id;
        GuestIsLostCountdown = 200;
        ResetPathfindGoal();
        WindowInvalidateFlags |= PEEP_INVALIDATE_PEEP_ACTION;

        // Make peep look at their map if they have one
        if (HasItem(ShopItem::Map))
        {
            ReadMap();
        }
    }
}

Ride* Guest::FindBestRideToGoOn()
{
    // Pick the most exciting ride
    auto rideConsideration = FindRidesToGoOn();
    Ride* mostExcitingRide = nullptr;
    for (auto& ride : GetRideManager())
    {
        if (rideConsideration.size() > ride.id && rideConsideration[ride.id])
        {
            if (!(ride.lifecycle_flags & RIDE_LIFECYCLE_QUEUE_FULL))
            {
                if (ShouldGoOnRide(&ride, 0, false, true) && ride_has_ratings(&ride))
                {
                    if (mostExcitingRide == nullptr || ride.excitement > mostExcitingRide->excitement)
                    {
                        mostExcitingRide = &ride;
                    }
                }
            }
        }
    }
    return mostExcitingRide;
}

std::bitset<MAX_RIDES> Guest::FindRidesToGoOn()
{
    std::bitset<MAX_RIDES> rideConsideration;

    // FIX  Originally checked for a toy, likely a mistake and should be a map,
    //      but then again this seems to only allow the peep to go on
    //      rides they haven't been on before.
    if (HasItem(ShopItem::Map))
    {
        // Consider rides that peep hasn't been on yet
        for (auto& ride : GetRideManager())
        {
            if (!HasRidden(&ride))
            {
                rideConsideration[ride.id] = true;
            }
        }
    }
    else
    {
        // Take nearby rides into consideration
        constexpr auto radius = 10 * 32;
        int32_t cx = floor2(x, 32);
        int32_t cy = floor2(y, 32);
        for (int32_t tileX = cx - radius; tileX <= cx + radius; tileX += COORDS_XY_STEP)
        {
            for (int32_t tileY = cy - radius; tileY <= cy + radius; tileY += COORDS_XY_STEP)
            {
                auto location = CoordsXY{ tileX, tileY };
                if (!map_is_location_valid(location))
                    continue;

                for (auto* trackElement : TileElementsView<TrackElement>(location))
                {
                    auto rideIndex = trackElement->GetRideIndex();
                    rideConsideration[rideIndex] = true;
                }
            }
        }

        // Always take the tall rides into consideration (realistic as you can usually see them from anywhere in the park)
        for (auto& ride : GetRideManager())
        {
            if (ride.highest_drop_height > 66 || ride.excitement >= RIDE_RATING(8, 00))
            {
                rideConsideration[ride.id] = true;
            }
        }
    }

    return rideConsideration;
}

/**
 * This function is called whenever a peep is deciding whether or not they want
 * to go on a ride or visit a shop. They may be physically present at the
 * ride/shop, or they may just be thinking about it.
 *  rct2: 0x006960AB
 */
bool Guest::ShouldGoOnRide(Ride* ride, int32_t entranceNum, bool atQueue, bool thinking)
{
    // Indicates whether a peep is physically at the ride, or is just thinking about going on the ride.
    bool peepAtRide = !thinking;

    if (ride->status == RIDE_STATUS_OPEN && !(ride->lifecycle_flags & RIDE_LIFECYCLE_BROKEN_DOWN))
    {
        // Peeps that are leaving the park will refuse to go on any rides, with the exception of free transport rides.
        assert(ride->type < std::size(RideTypeDescriptors));
        if (!ride->GetRideTypeDescriptor().HasFlag(RIDE_TYPE_FLAG_TRANSPORT_RIDE) || ride->value == RIDE_VALUE_UNDEFINED
            || ride_get_price(ride) != 0)
        {
            if (PeepFlags & PEEP_FLAGS_LEAVING_PARK)
            {
                ChoseNotToGoOnRide(ride, peepAtRide, false);
                return false;
            }
        }

        if (ride->GetRideTypeDescriptor().HasFlag(RIDE_TYPE_FLAG_IS_SHOP))
        {
            return ShouldGoToShop(ride, peepAtRide);
        }

        // This used to check !(flags & 2), but the function is only ever called with flags = 0, 1 or 6.
        // This means we can use the existing !(flags & 4) check.
        if (peepAtRide)
        {
            // Peeps won't join a queue that has 1000 peeps already in it.
            if (ride->stations[entranceNum].QueueLength >= 1000)
            {
                peep_tried_to_enter_full_queue(this, ride);
                return false;
            }

            // Rides without queues can only have one peep waiting at a time.
            if (!atQueue)
            {
                if (ride->stations[entranceNum].LastPeepInQueue != SPRITE_INDEX_NULL)
                {
                    peep_tried_to_enter_full_queue(this, ride);
                    return false;
                }
            }
            else
            {
                // Check if there's room in the queue for the peep to enter.
                Peep* lastPeepInQueue = GetEntity<Guest>(ride->stations[entranceNum].LastPeepInQueue);
                if (lastPeepInQueue != nullptr && (abs(lastPeepInQueue->z - z) <= 6))
                {
                    int32_t dx = abs(lastPeepInQueue->x - x);
                    int32_t dy = abs(lastPeepInQueue->y - y);
                    int32_t maxD = std::max(dx, dy);

                    // Unlike normal paths, peeps cannot overlap when queueing for a ride.
                    // This check enforces a minimum distance between peeps entering the queue.
                    if (maxD < 8)
                    {
                        peep_tried_to_enter_full_queue(this, ride);
                        return false;
                    }

                    // This checks if there's a peep standing still at the very end of the queue.
                    if (maxD <= 13 && lastPeepInQueue->TimeInQueue > 10)
                    {
                        peep_tried_to_enter_full_queue(this, ride);
                        return false;
                    }
                }
            }
        }

        // Assuming the queue conditions are met, peeps will always go on free transport rides.
        // Ride ratings, recent crashes and weather will all be ignored.
        money16 ridePrice = ride_get_price(ride);
        if (!ride->GetRideTypeDescriptor().HasFlag(RIDE_TYPE_FLAG_TRANSPORT_RIDE) || ride->value == RIDE_VALUE_UNDEFINED
            || ridePrice != 0)
        {
            if (PreviousRide == ride->id)
            {
                ChoseNotToGoOnRide(ride, peepAtRide, false);
                return false;
            }

            // Basic price checks
            if (ridePrice != 0 && !peep_has_voucher_for_free_ride(this, ride) && !(gParkFlags & PARK_FLAGS_NO_MONEY))
            {
                if (ridePrice > CashInPocket)
                {
                    if (peepAtRide)
                    {
                        if (CashInPocket <= 0)
                        {
                            InsertNewThought(PeepThoughtType::SpentMoney, PEEP_THOUGHT_ITEM_NONE);
                        }
                        else
                        {
                            InsertNewThought(PeepThoughtType::CantAffordRide, ride->id);
                        }
                    }
                    ChoseNotToGoOnRide(ride, peepAtRide, true);
                    return false;
                }
            }

            // If happy enough, peeps will ignore the fact that a ride has recently crashed.
            if (ride->last_crash_type != RIDE_CRASH_TYPE_NONE && Happiness < 225)
            {
                if (peepAtRide)
                {
                    InsertNewThought(PeepThoughtType::NotSafe, ride->id);
                    if (HappinessTarget >= 64)
                    {
                        HappinessTarget -= 8;
                    }
                    ride_update_popularity(ride, 0);
                }
                ChoseNotToGoOnRide(ride, peepAtRide, true);
                return false;
            }

            if (ride_has_ratings(ride))
            {
                // If a peep has already decided that they're going to go on a ride, they'll skip the weather and
                // excitement check and will only do a basic intensity check when they arrive at the ride itself.
                if (ride->id == GuestHeadingToRideId)
                {
                    if (ride->intensity > RIDE_RATING(10, 00) && !gCheatsIgnoreRideIntensity)
                    {
                        peep_ride_is_too_intense(this, ride, peepAtRide);
                        return false;
                    }
                }

                // Peeps won't go on rides that aren't sufficiently undercover while it's raining.
                // The threshold is fairly low and only requires about 10-15% of the ride to be undercover.
                if (climate_is_raining() && (ride->sheltered_eighths) < 3)
                {
                    if (peepAtRide)
                    {
                        InsertNewThought(PeepThoughtType::NotWhileRaining, ride->id);
                        if (HappinessTarget >= 64)
                        {
                            HappinessTarget -= 8;
                        }
                        ride_update_popularity(ride, 0);
                    }
                    ChoseNotToGoOnRide(ride, peepAtRide, true);
                    return false;
                }

                if (!gCheatsIgnoreRideIntensity)
                {
                    // Intensity calculations. Even though the max intensity can go up to 15, it's capped
                    // at 10.0 (before happiness calculations). A full happiness bar will increase the max
                    // intensity and decrease the min intensity by about 2.5.
                    ride_rating maxIntensity = std::min(Intensity.GetMaximum() * 100, 1000) + Happiness;
                    ride_rating minIntensity = (Intensity.GetMinimum() * 100) - Happiness;
                    if (ride->intensity < minIntensity)
                    {
                        if (peepAtRide)
                        {
                            InsertNewThought(PeepThoughtType::MoreThrilling, ride->id);
                            if (HappinessTarget >= 64)
                            {
                                HappinessTarget -= 8;
                            }
                            ride_update_popularity(ride, 0);
                        }
                        ChoseNotToGoOnRide(ride, peepAtRide, true);
                        return false;
                    }
                    if (ride->intensity > maxIntensity)
                    {
                        peep_ride_is_too_intense(this, ride, peepAtRide);
                        return false;
                    }

                    // Nausea calculations.
                    ride_rating maxNausea = NauseaMaximumThresholds[(EnumValue(NauseaTolerance) & 3)] + Happiness;

                    if (ride->nausea > maxNausea)
                    {
                        if (peepAtRide)
                        {
                            InsertNewThought(PeepThoughtType::Sickening, ride->id);
                            if (HappinessTarget >= 64)
                            {
                                HappinessTarget -= 8;
                            }
                            ride_update_popularity(ride, 0);
                        }
                        ChoseNotToGoOnRide(ride, peepAtRide, true);
                        return false;
                    }

                    // Very nauseous peeps will only go on very gentle rides.
                    if (ride->nausea >= FIXED_2DP(1, 40) && Nausea > 160)
                    {
                        ChoseNotToGoOnRide(ride, peepAtRide, false);
                        return false;
                    }
                }
            }

            // If the ride has not yet been rated and is capable of having g-forces,
            // there's a 90% chance that the peep will ignore it.
            if (!ride_has_ratings(ride) && ride->GetRideTypeDescriptor().HasFlag(RIDE_TYPE_FLAG_PEEP_CHECK_GFORCES))
            {
                if ((scenario_rand() & 0xFFFF) > 0x1999U)
                {
                    ChoseNotToGoOnRide(ride, peepAtRide, false);
                    return false;
                }

                if (!gCheatsIgnoreRideIntensity)
                {
                    if (ride->max_positive_vertical_g > FIXED_2DP(5, 00) || ride->max_negative_vertical_g < FIXED_2DP(-4, 00)
                        || ride->max_lateral_g > FIXED_2DP(4, 00))
                    {
                        ChoseNotToGoOnRide(ride, peepAtRide, false);
                        return false;
                    }
                }
            }

            uint32_t value = ride->value;

            // If the value of the ride hasn't yet been calculated, peeps will be willing to pay any amount for the ride.
            if (value != 0xFFFF && !peep_has_voucher_for_free_ride(this, ride) && !(gParkFlags & PARK_FLAGS_NO_MONEY))
            {
                // The amount peeps are willing to pay is decreased by 75% if they had to pay to enter the park.
                if (PeepFlags & PEEP_FLAGS_HAS_PAID_FOR_PARK_ENTRY)
                    value /= 4;

                // Peeps won't pay more than twice the value of the ride.
                ridePrice = ride_get_price(ride);
                if (ridePrice > static_cast<money16>(value * 2))
                {
                    if (peepAtRide)
                    {
                        InsertNewThought(PeepThoughtType::BadValue, ride->id);
                        if (HappinessTarget >= 60)
                        {
                            HappinessTarget -= 16;
                        }
                        ride_update_popularity(ride, 0);
                    }
                    ChoseNotToGoOnRide(ride, peepAtRide, true);
                    return false;
                }

                // A ride is good value if the price is 50% or less of the ride value and the peep didn't pay to enter the park.
                if (ridePrice <= static_cast<money16>(value / 2) && peepAtRide)
                {
                    if (!(gParkFlags & PARK_FLAGS_NO_MONEY))
                    {
                        if (!(PeepFlags & PEEP_FLAGS_HAS_PAID_FOR_PARK_ENTRY))
                        {
                            InsertNewThought(PeepThoughtType::GoodValue, ride->id);
                        }
                    }
                }
            }
        }

        // At this point, the peep has decided to go on the ride.
        if (peepAtRide)
        {
            ride_update_popularity(ride, 1);
        }

        if (ride->id == GuestHeadingToRideId)
        {
            peep_reset_ride_heading(this);
        }

        ride->lifecycle_flags &= ~RIDE_LIFECYCLE_QUEUE_FULL;
        return true;
    }

    ChoseNotToGoOnRide(ride, peepAtRide, false);
    return false;
}

bool Guest::ShouldGoToShop(Ride* ride, bool peepAtShop)
{
    // Peeps won't go to the same shop twice in a row.
    if (ride->id == PreviousRide)
    {
        ChoseNotToGoOnRide(ride, peepAtShop, true);
        return false;
    }

    if (ride->type == RIDE_TYPE_TOILETS)
    {
        if (Toilet < 70)
        {
            ChoseNotToGoOnRide(ride, peepAtShop, true);
            return false;
        }

        // The amount that peeps are willing to pay to use the Toilets scales with their toilet stat.
        // It effectively has a minimum of $0.10 (due to the check above) and a maximum of $0.60.
        if (ride_get_price(ride) * 40 > Toilet)
        {
            if (peepAtShop)
            {
                InsertNewThought(PeepThoughtType::NotPaying, ride->id);
                if (HappinessTarget >= 60)
                {
                    HappinessTarget -= 16;
                }
                ride_update_popularity(ride, 0);
            }
            ChoseNotToGoOnRide(ride, peepAtShop, true);
            return false;
        }
    }

    if (ride->type == RIDE_TYPE_FIRST_AID)
    {
        if (Nausea < 128)
        {
            ChoseNotToGoOnRide(ride, peepAtShop, true);
            return false;
        }
    }

    // Basic price checks
    auto ridePrice = ride_get_price(ride);
    if (ridePrice != 0 && ridePrice > CashInPocket)
    {
        if (peepAtShop)
        {
            if (CashInPocket <= 0)
            {
                InsertNewThought(PeepThoughtType::SpentMoney, PEEP_THOUGHT_ITEM_NONE);
            }
            else
            {
                InsertNewThought(PeepThoughtType::CantAffordRide, ride->id);
            }
        }
        ChoseNotToGoOnRide(ride, peepAtShop, true);
        return false;
    }

    if (peepAtShop)
    {
        ride_update_popularity(ride, 1);
        if (ride->id == GuestHeadingToRideId)
        {
            peep_reset_ride_heading(this);
        }
    }
    return true;
}

// Used when no logging to an expend type required
void Guest::SpendMoney(money32 amount, ExpenditureType expenditure)
{
    money16 unused;
    SpendMoney(unused, amount, expenditure);
}

/**
 *
 *  rct2: 0x0069926C
 * Expend type was previously an offset saved in 0x00F1AEC0
 */
void Guest::SpendMoney(money16& peep_expend_type, money32 amount, ExpenditureType expenditure)
{
    assert(!(gParkFlags & PARK_FLAGS_NO_MONEY));

    CashInPocket = std::max(0, CashInPocket - amount);
    CashSpent += amount;

    peep_expend_type += static_cast<money16>(amount);

    window_invalidate_by_number(WC_PEEP, sprite_index);

    finance_payment(-amount, expenditure);

    if (gConfigGeneral.show_guest_purchases && !(gScreenFlags & SCREEN_FLAGS_TITLE_DEMO))
    {
        // HACK Currently disabled for multiplayer due to limitation of all sprites
        //      needing to be synchronised
        if (network_get_mode() == NETWORK_MODE_NONE && !gOpenRCT2Headless)
        {
            MoneyEffect::CreateAt(amount, { x, y, z }, true);
        }
    }

    OpenRCT2::Audio::Play3D(OpenRCT2::Audio::SoundId::Purchase, { x, y, z });
}

void Guest::SetHasRidden(const Ride* ride)
{
    RidesBeenOn[ride->id / 8] |= 1 << (ride->id % 8);
    SetHasRiddenRideType(ride->type);
}

bool Guest::HasRidden(const Ride* ride) const
{
    return RidesBeenOn[ride->id / 8] & (1 << (ride->id % 8));
}

void Guest::SetHasRiddenRideType(int32_t rideType)
{
    // This is needed to avoid desyncs. TODO: remove once the new save format is introduced.
    rideType = OpenRCT2RideTypeToRCT2RideType(rideType);
    RideTypesBeenOn[rideType / 8] |= 1 << (rideType % 8);
}

bool Guest::HasRiddenRideType(int32_t rideType) const
{
    // This is needed to avoid desyncs. TODO: remove once the new save format is introduced.
    rideType = OpenRCT2RideTypeToRCT2RideType(rideType);
    return RideTypesBeenOn[rideType / 8] & (1 << (rideType % 8));
}

void Guest::SetParkEntryTime(int32_t entryTime)
{
    ParkEntryTime = entryTime;
}

int32_t Guest::GetParkEntryTime() const
{
    return ParkEntryTime;
}

void Guest::ChoseNotToGoOnRide(Ride* ride, bool peepAtRide, bool updateLastRide)
{
    if (peepAtRide && updateLastRide)
    {
        PreviousRide = ride->id;
        PreviousRideTimeOut = 0;
    }

    if (ride->id == GuestHeadingToRideId)
    {
        peep_reset_ride_heading(this);
    }
}

void Guest::ReadMap()
{
    if (Action == PeepActionType::None1 || Action == PeepActionType::None2)
    {
        Action = PeepActionType::ReadMap;
        ActionFrame = 0;
        ActionSpriteImageOffset = 0;
        UpdateCurrentActionSpriteType();
    }
}

static bool peep_has_voucher_for_free_ride(Peep* peep, Ride* ride)
{
    return peep->HasItem(ShopItem::Voucher) && peep->VoucherType == VOUCHER_TYPE_RIDE_FREE && peep->VoucherRideId == ride->id;
}

/**
 * When the queue is full, peeps will ignore the ride when thinking about what to go on next.
 * Does not effect peeps that walk up to the queue entrance.
 * This flag is reset the next time a peep successfully joins the queue.
 */
static void peep_tried_to_enter_full_queue(Peep* peep, Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_QUEUE_FULL;
    peep->PreviousRide = ride->id;
    peep->PreviousRideTimeOut = 0;
    // Change status "Heading to" to "Walking" if queue is full
    if (ride->id == peep->GuestHeadingToRideId)
    {
        peep_reset_ride_heading(peep);
    }
}

static void peep_reset_ride_heading(Peep* peep)
{
    peep->GuestHeadingToRideId = RIDE_ID_NULL;
    peep->WindowInvalidateFlags |= PEEP_INVALIDATE_PEEP_ACTION;
}

static void peep_ride_is_too_intense(Guest* peep, Ride* ride, bool peepAtRide)
{
    if (peepAtRide)
    {
        peep->InsertNewThought(PeepThoughtType::Intense, ride->id);
        if (peep->HappinessTarget >= 64)
        {
            peep->HappinessTarget -= 8;
        }
        ride_update_popularity(ride, 0);
    }
    peep->ChoseNotToGoOnRide(ride, peepAtRide, true);
}

/**
 *
 *  rct2: 0x00691C6E
 */
static Vehicle* peep_choose_car_from_ride(Peep* peep, Ride* ride, std::vector<uint8_t>& car_array)
{
    uint8_t chosen_car = scenario_rand();
    if (ride->GetRideTypeDescriptor().HasFlag(RIDE_TYPE_FLAG_HAS_G_FORCES) && ((chosen_car & 0xC) != 0xC))
    {
        chosen_car = (scenario_rand() & 1) ? 0 : static_cast<uint8_t>(car_array.size()) - 1;
    }
    else
    {
        chosen_car = (chosen_car * static_cast<uint16_t>(car_array.size())) >> 8;
    }

    peep->CurrentCar = car_array[chosen_car];

    Vehicle* vehicle = GetEntity<Vehicle>(ride->vehicles[peep->CurrentTrain]);
    if (vehicle == nullptr)
    {
        return nullptr;
    }
    return vehicle->GetCar(peep->CurrentCar);
}

/**
 *
 *  rct2: 0x00691CD1
 */
static void peep_choose_seat_from_car(Peep* peep, Ride* ride, Vehicle* vehicle)
{
    if (vehicle == nullptr)
    {
        return;
    }
    uint8_t chosen_seat = vehicle->next_free_seat;

    if (ride->mode == RideMode::ForwardRotation || ride->mode == RideMode::BackwardRotation)
    {
        chosen_seat = (((~vehicle->vehicle_sprite_type + 1) >> 3) & 0xF) * 2;
        if (vehicle->next_free_seat & 1)
        {
            chosen_seat++;
        }
    }
    peep->CurrentSeat = chosen_seat;
    vehicle->next_free_seat++;

    vehicle->peep[peep->CurrentSeat] = peep->sprite_index;
    vehicle->peep_tshirt_colours[peep->CurrentSeat] = peep->TshirtColour;
}

/**
 *
 *  rct2: 0x00691D27
 */
void Guest::GoToRideEntrance(Ride* ride)
{
    TileCoordsXYZD tileLocation = ride_get_entrance_location(ride, CurrentRideStation);
    if (tileLocation.isNull())
    {
        RemoveFromQueue();
        return;
    }

    auto location = tileLocation.ToCoordsXYZD().ToTileCentre();
    int16_t x_shift = DirectionOffsets[location.direction].x;
    int16_t y_shift = DirectionOffsets[location.direction].y;

    uint8_t shift_multiplier = 21;
    rct_ride_entry* rideEntry = get_ride_entry(ride->subtype);
    if (rideEntry != nullptr)
    {
        if (rideEntry->vehicles[rideEntry->default_vehicle].flags & VEHICLE_ENTRY_FLAG_MINI_GOLF
            || rideEntry->vehicles[rideEntry->default_vehicle].flags
                & (VEHICLE_ENTRY_FLAG_CHAIRLIFT | VEHICLE_ENTRY_FLAG_GO_KART))
        {
            shift_multiplier = 32;
        }
    }

    x_shift *= shift_multiplier;
    y_shift *= shift_multiplier;

    location.x += x_shift;
    location.y += y_shift;

    SetDestination(location, 2);
    SetState(PeepState::EnteringRide);
    RideSubState = PeepRideSubState::InEntrance;

    RejoinQueueTimeout = 0;
    GuestTimeOnRide = 0;

    RemoveFromQueue();
}

bool Guest::FindVehicleToEnter(Ride* ride, std::vector<uint8_t>& car_array)
{
    uint8_t chosen_train = RideStation::NO_TRAIN;

    if (ride->mode == RideMode::Dodgems || ride->mode == RideMode::Race)
    {
        if (ride->lifecycle_flags & RIDE_LIFECYCLE_PASS_STATION_NO_STOPPING)
            return false;

        for (int32_t i = 0; i < ride->num_vehicles; ++i)
        {
            Vehicle* vehicle = GetEntity<Vehicle>(ride->vehicles[i]);
            if (vehicle == nullptr)
                continue;

            if (vehicle->next_free_seat >= vehicle->num_seats)
                continue;

            if (vehicle->status != Vehicle::Status::WaitingForPassengers)
                continue;
            chosen_train = i;
            break;
        }
    }
    else
    {
        chosen_train = ride->stations[CurrentRideStation].TrainAtStation;
    }
    if (chosen_train == RideStation::NO_TRAIN || chosen_train >= MAX_VEHICLES_PER_RIDE)
    {
        return false;
    }

    CurrentTrain = chosen_train;

    int32_t i = 0;

    uint16_t vehicle_id = ride->vehicles[chosen_train];
    for (Vehicle* vehicle = GetEntity<Vehicle>(vehicle_id); vehicle != nullptr;
         vehicle = GetEntity<Vehicle>(vehicle->next_vehicle_on_train), ++i)
    {
        uint8_t num_seats = vehicle->num_seats;
        if (vehicle->IsUsedInPairs())
        {
            if (vehicle->next_free_seat & 1)
            {
                car_array.clear();
                car_array.push_back(i);
                return true;
            }
            num_seats &= VEHICLE_SEAT_NUM_MASK;
        }
        if (num_seats == vehicle->next_free_seat)
            continue;

        if (ride->mode == RideMode::ForwardRotation || ride->mode == RideMode::BackwardRotation)
        {
            uint8_t position = (((~vehicle->vehicle_sprite_type + 1) >> 3) & 0xF) * 2;
            if (vehicle->peep[position] != SPRITE_INDEX_NULL)
                continue;
        }
        car_array.push_back(i);
    }

    return !car_array.empty();
}

static void peep_update_ride_at_entrance_try_leave(Guest* peep)
{
    // Destination Tolerance is zero when peep has completely
    // entered entrance
    if (peep->DestinationTolerance == 0)
    {
        peep->RemoveFromQueue();
        peep->SetState(PeepState::Falling);
    }
}

static bool peep_check_ride_price_at_entrance(Guest* peep, Ride* ride, money32 ridePrice)
{
    if ((peep->HasItem(ShopItem::Voucher)) && peep->VoucherType == VOUCHER_TYPE_RIDE_FREE
        && peep->VoucherRideId == peep->CurrentRide)
        return true;

    if (peep->CashInPocket <= 0 && !(gParkFlags & PARK_FLAGS_NO_MONEY))
    {
        peep->InsertNewThought(PeepThoughtType::SpentMoney, PEEP_THOUGHT_ITEM_NONE);
        peep_update_ride_at_entrance_try_leave(peep);
        return false;
    }

    if (ridePrice > peep->CashInPocket)
    {
        peep->InsertNewThought(PeepThoughtType::CantAffordRide, peep->CurrentRide);
        peep_update_ride_at_entrance_try_leave(peep);
        return false;
    }

    uint16_t value = ride->value;
    if (value != RIDE_VALUE_UNDEFINED)
    {
        if (value * 2 < ridePrice)
        {
            peep->InsertNewThought(PeepThoughtType::BadValue, peep->CurrentRide);
            peep_update_ride_at_entrance_try_leave(peep);
            return false;
        }
    }
    return true;
}

/**
 * The satisfaction values calculated here are used to determine how happy the peep is with the ride,
 * and also affects the satisfaction stat of the ride itself. The factors that affect satisfaction include:
 * - The price of the ride compared to the ride's value
 * - How closely the intensity and nausea of the ride matches the peep's preferences
 * - How long the peep was waiting in the queue
 * - If the peep has been on the ride before, or on another ride of the same type
 */
static int16_t peep_calculate_ride_satisfaction(Guest* peep, Ride* ride)
{
    int16_t satisfaction = peep_calculate_ride_value_satisfaction(peep, ride);
    satisfaction += peep_calculate_ride_intensity_nausea_satisfaction(peep, ride);

    // Calculate satisfaction based on how long the peep has been in the queue for.
    // (For comparison: peeps start thinking "I've been queueing for a long time" at 3500 and
    // start leaving the queue at 4300.)
    if (peep->TimeInQueue >= 4500)
        satisfaction -= 35;
    else if (peep->TimeInQueue >= 2250)
        satisfaction -= 10;
    else if (peep->TimeInQueue <= 750)
        satisfaction += 10;

    // Peeps get a small boost in satisfaction if they've been on a ride of the same type before,
    // and this boost is doubled if they've already been on this particular ride.
    if (peep->HasRiddenRideType(ride->type))
        satisfaction += 10;

    if (peep->HasRidden(get_ride(peep->CurrentRide)))
        satisfaction += 10;

    return satisfaction;
}

/**
 * Check to see if the specified ride should become the peep's favourite.
 * For this, a "ride rating" is calculated based on the excitement of the ride and the peep's current happiness.
 * As this value cannot exceed 255, the happier the peep is, the more irrelevant the ride's excitement becomes.
 * Due to the minimum happiness requirement, an excitement rating of more than 3.8 has no further effect.
 *
 * If the ride rating is higher than any ride the peep has already been on and the happiness criteria is met,
 * the ride becomes the peep's favourite. (This doesn't happen right away, but will be updated once the peep
 * exits the ride.)
 */
static void peep_update_favourite_ride(Peep* peep, Ride* ride)
{
    peep->PeepFlags &= ~PEEP_FLAGS_RIDE_SHOULD_BE_MARKED_AS_FAVOURITE;
    uint8_t peepRideRating = std::clamp((ride->excitement / 4) + peep->Happiness, 0, PEEP_MAX_HAPPINESS);
    if (peepRideRating >= peep->FavouriteRideRating)
    {
        if (peep->Happiness >= 160 && peep->HappinessTarget >= 160)
        {
            peep->FavouriteRideRating = peepRideRating;
            peep->PeepFlags |= PEEP_FLAGS_RIDE_SHOULD_BE_MARKED_AS_FAVOURITE;
        }
    }
}

/* rct2: 0x00695555 */
static int16_t peep_calculate_ride_value_satisfaction(Peep* peep, Ride* ride)
{
    if (gParkFlags & PARK_FLAGS_NO_MONEY)
    {
        return -30;
    }

    if (ride->value == RIDE_VALUE_UNDEFINED)
    {
        return -30;
    }

    money16 ridePrice = ride_get_price(ride);
    if (ride->value >= ridePrice)
    {
        return -5;
    }

    if ((ride->value + ((ride->value * peep->Happiness) / 256)) >= ridePrice)
    {
        return -30;
    }

    return 0;
}

/**
 * Calculate satisfaction based on the intensity and nausea of the ride.
 * The best possible score from this section is achieved by having the intensity and nausea
 * of the ride fall exactly within the peep's preferences, but lower scores can still be achieved
 * if the peep's happiness is enough to offset it.
 */
static int16_t peep_calculate_ride_intensity_nausea_satisfaction(Peep* peep, Ride* ride)
{
    if (!ride_has_ratings(ride))
    {
        return 70;
    }

    uint8_t intensitySatisfaction = 3;
    uint8_t nauseaSatisfaction = 3;
    ride_rating maxIntensity = peep->Intensity.GetMaximum() * 100;
    ride_rating minIntensity = peep->Intensity.GetMinimum() * 100;
    if (minIntensity <= ride->intensity && maxIntensity >= ride->intensity)
    {
        intensitySatisfaction--;
    }
    minIntensity -= peep->Happiness * 2;
    maxIntensity += peep->Happiness;
    if (minIntensity <= ride->intensity && maxIntensity >= ride->intensity)
    {
        intensitySatisfaction--;
    }
    minIntensity -= peep->Happiness * 2;
    maxIntensity += peep->Happiness;
    if (minIntensity <= ride->intensity && maxIntensity >= ride->intensity)
    {
        intensitySatisfaction--;
    }

    // Although it's not shown in the interface, a peep with Average or High nausea tolerance
    // has a minimum preferred nausea value. (For peeps with None or Low, this is set to zero.)
    ride_rating minNausea = NauseaMinimumThresholds[(EnumValue(peep->NauseaTolerance) & 3)];
    ride_rating maxNausea = NauseaMaximumThresholds[(EnumValue(peep->NauseaTolerance) & 3)];
    if (minNausea <= ride->nausea && maxNausea >= ride->nausea)
    {
        nauseaSatisfaction--;
    }
    minNausea -= peep->Happiness * 2;
    maxNausea += peep->Happiness;
    if (minNausea <= ride->nausea && maxNausea >= ride->nausea)
    {
        nauseaSatisfaction--;
    }
    minNausea -= peep->Happiness * 2;
    maxNausea += peep->Happiness;
    if (minNausea <= ride->nausea && maxNausea >= ride->nausea)
    {
        nauseaSatisfaction--;
    }

    uint8_t highestSatisfaction = std::max(intensitySatisfaction, nauseaSatisfaction);
    uint8_t lowestSatisfaction = std::min(intensitySatisfaction, nauseaSatisfaction);

    switch (highestSatisfaction)
    {
        default:
        case 0:
            return 70;
        case 1:
            switch (lowestSatisfaction)
            {
                default:
                case 0:
                    return 50;
                case 1:
                    return 35;
            }
        case 2:
            switch (lowestSatisfaction)
            {
                default:
                case 0:
                    return 35;
                case 1:
                    return 20;
                case 2:
                    return 10;
            }
        case 3:
            switch (lowestSatisfaction)
            {
                default:
                case 0:
                    return -35;
                case 1:
                    return -50;
                case 2:
                    return -60;
                case 3:
                    return -60;
            }
    }
}

/**
 * Update the nausea growth of the peep based on a ride. This is calculated based on:
 * - The nausea rating of the ride
 * - Their new happiness growth rate (the higher, the less nauseous)
 * - How hungry the peep is (+0% nausea at 50% hunger up to +100% nausea at 100% hunger)
 * - The peep's nausea tolerance (Final modifier: none: 100%, low: 50%, average: 25%, high: 12.5%)
 */
static void peep_update_ride_nausea_growth(Peep* peep, Ride* ride)
{
    uint32_t nauseaMultiplier = std::clamp(256 - peep->HappinessTarget, 64, 200);
    uint32_t nauseaGrowthRateChange = (ride->nausea * nauseaMultiplier) / 512;
    nauseaGrowthRateChange *= std::max(static_cast<uint8_t>(128), peep->Hunger) / 64;
    nauseaGrowthRateChange >>= (EnumValue(peep->NauseaTolerance) & 3);
    peep->NauseaTarget = static_cast<uint8_t>(std::min(peep->NauseaTarget + nauseaGrowthRateChange, 255u));
}

static bool peep_should_go_on_ride_again(Peep* peep, Ride* ride)
{
    if (!ride->GetRideTypeDescriptor().HasFlag(RIDE_TYPE_FLAG_PEEP_WILL_RIDE_AGAIN))
        return false;
    if (!ride_has_ratings(ride))
        return false;
    if (ride->intensity > RIDE_RATING(10, 00) && !gCheatsIgnoreRideIntensity)
        return false;
    if (peep->Happiness < 180)
        return false;
    if (peep->Energy < 100)
        return false;
    if (peep->Nausea > 160)
        return false;
    if (peep->Hunger < 30)
        return false;
    if (peep->Thirst < 20)
        return false;
    if (peep->Toilet > 170)
        return false;

    uint8_t r = (scenario_rand() & 0xFF);
    if (r <= 128)
    {
        if (peep->GuestNumRides > 7)
            return false;
        if (r > 64)
            return false;
    }

    return true;
}

static bool peep_should_preferred_intensity_increase(Peep* peep)
{
    if (gParkFlags & PARK_FLAGS_PREF_LESS_INTENSE_RIDES)
        return false;
    if (peep->Happiness < 200)
        return false;

    return (scenario_rand() & 0xFF) >= static_cast<uint8_t>(peep->Intensity);
}

static bool peep_really_liked_ride(Peep* peep, Ride* ride)
{
    if (peep->Happiness < 215)
        return false;
    if (peep->Nausea > 120)
        return false;
    if (!ride_has_ratings(ride))
        return false;
    if (ride->intensity > RIDE_RATING(10, 00) && !gCheatsIgnoreRideIntensity)
        return false;
    return true;
}

/**
 *
 *  rct2: 0x0069BC9A
 */
static PeepThoughtType peep_assess_surroundings(int16_t centre_x, int16_t centre_y, int16_t centre_z)
{
    if ((tile_element_height({ centre_x, centre_y })) > centre_z)
        return PeepThoughtType::None;

    uint16_t num_scenery = 0;
    uint16_t num_fountains = 0;
    uint16_t nearby_music = 0;
    uint16_t num_rubbish = 0;

    int16_t initial_x = std::max(centre_x - 160, 0);
    int16_t initial_y = std::max(centre_y - 160, 0);
    int16_t final_x = std::min(centre_x + 160, MAXIMUM_MAP_SIZE_BIG);
    int16_t final_y = std::min(centre_y + 160, MAXIMUM_MAP_SIZE_BIG);

    for (int16_t x = initial_x; x < final_x; x += COORDS_XY_STEP)
    {
        for (int16_t y = initial_y; y < final_y; y += COORDS_XY_STEP)
        {
            for (auto* tileElement : TileElementsView({ x, y }))
            {
                Ride* ride;
                rct_scenery_entry* scenery;

                switch (tileElement->GetType())
                {
                    case TILE_ELEMENT_TYPE_PATH:
                        if (!tileElement->AsPath()->HasAddition())
                            break;

                        scenery = tileElement->AsPath()->GetAdditionEntry();
                        if (scenery == nullptr)
                        {
                            return PeepThoughtType::None;
                        }
                        if (tileElement->AsPath()->AdditionIsGhost())
                            break;

                        if (scenery->path_bit.flags
                            & (PATH_BIT_FLAG_JUMPING_FOUNTAIN_WATER | PATH_BIT_FLAG_JUMPING_FOUNTAIN_SNOW))
                        {
                            num_fountains++;
                            break;
                        }
                        if (tileElement->AsPath()->IsBroken())
                        {
                            num_rubbish++;
                        }
                        break;
                    case TILE_ELEMENT_TYPE_LARGE_SCENERY:
                    case TILE_ELEMENT_TYPE_SMALL_SCENERY:
                        num_scenery++;
                        break;
                    case TILE_ELEMENT_TYPE_TRACK:
                        ride = get_ride(tileElement->AsTrack()->GetRideIndex());
                        if (ride != nullptr)
                        {
                            if (ride->lifecycle_flags & RIDE_LIFECYCLE_MUSIC && ride->status != RIDE_STATUS_CLOSED
                                && !(ride->lifecycle_flags & (RIDE_LIFECYCLE_BROKEN_DOWN | RIDE_LIFECYCLE_CRASHED)))
                            {
                                if (ride->type == RIDE_TYPE_MERRY_GO_ROUND)
                                {
                                    nearby_music |= 1;
                                    break;
                                }

                                if (ride->music == MUSIC_STYLE_ORGAN)
                                {
                                    nearby_music |= 1;
                                    break;
                                }

                                if (ride->type == RIDE_TYPE_DODGEMS)
                                {
                                    // Dodgems drown out music?
                                    nearby_music |= 2;
                                }
                            }
                        }
                        break;
                }
            }
        }
    }

    for (auto litter : EntityList<Litter>())
    {
        int16_t dist_x = abs(litter->x - centre_x);
        int16_t dist_y = abs(litter->y - centre_y);
        if (std::max(dist_x, dist_y) <= 160)
        {
            num_rubbish++;
        }
    }

    if (num_fountains >= 5 && num_rubbish < 20)
        return PeepThoughtType::Fountains;

    if (num_scenery >= 40 && num_rubbish < 8)
        return PeepThoughtType::Scenery;

    if (nearby_music == 1 && num_rubbish < 20)
        return PeepThoughtType::Music;

    if (num_rubbish < 2 && !gCheatsDisableLittering)
        // if disable littering cheat is enabled, peeps will not have the "clean and tidy park" thought
        return PeepThoughtType::VeryClean;

    return PeepThoughtType::None;
}

/**
 *
 *  rct2: 0x0068F9A9
 */
static void peep_update_hunger(Peep* peep)
{
    if (peep->Hunger >= 3)
    {
        peep->Hunger -= 2;

        peep->EnergyTarget = std::min(peep->EnergyTarget + 2, PEEP_MAX_ENERGY_TARGET);
        peep->Toilet = std::min(peep->Toilet + 1, 255);
    }
}

/**
 * Main purpose is to decide when peeps leave the park due to
 * low happiness, low energy and (if appropriate) low money.
 *
 *  rct2: 0x0068F8CD
 */
static void peep_decide_whether_to_leave_park(Peep* peep)
{
    if (peep->EnergyTarget >= 33)
    {
        peep->EnergyTarget -= 2;
    }

    if (gClimateCurrent.Temperature >= 21 && peep->Thirst >= 5)
    {
        peep->Thirst--;
    }

    if (peep->OutsideOfPark)
    {
        return;
    }

    /* Peeps that are happy enough, have enough energy and
     * (if appropriate) have enough money will always stay
     * in the park. */
    if (!(peep->PeepFlags & PEEP_FLAGS_LEAVING_PARK))
    {
        if (gParkFlags & PARK_FLAGS_NO_MONEY)
        {
            if (peep->Energy >= 70 && peep->Happiness >= 60)
            {
                return;
            }
        }
        else
        {
            if (peep->Energy >= 55 && peep->Happiness >= 45 && peep->CashInPocket >= MONEY(5, 00))
            {
                return;
            }
        }
    }

    // Approx 95% chance of staying in the park
    if ((scenario_rand() & 0xFFFF) > 3276)
    {
        return;
    }

    // In the remaining 5% chance the peep leaves the park.
    peep_leave_park(peep);
}

/**
 *
 *  rct2: 0x0068F93E
 */
static void peep_leave_park(Peep* peep)
{
    peep->GuestHeadingToRideId = RIDE_ID_NULL;
    if (peep->PeepFlags & PEEP_FLAGS_LEAVING_PARK)
    {
        if (peep->GuestIsLostCountdown < 60)
        {
            return;
        }
    }
    else
    {
        peep->GuestIsLostCountdown = 254;
        peep->PeepFlags |= PEEP_FLAGS_LEAVING_PARK;
        peep->PeepFlags &= ~PEEP_FLAGS_PARK_ENTRANCE_CHOSEN;
    }

    peep->InsertNewThought(PeepThoughtType::GoHome, PEEP_THOUGHT_ITEM_NONE);

    rct_window* w = window_find_by_number(WC_PEEP, peep->sprite_index);
    if (w != nullptr)
        window_event_invalidate_call(w);
    window_invalidate_by_number(WC_PEEP, peep->sprite_index);
}

template<typename T> static void peep_head_for_nearest_ride(Guest* peep, bool considerOnlyCloseRides, T predicate)
{
    if (peep->State != PeepState::Sitting && peep->State != PeepState::Watching && peep->State != PeepState::Walking)
    {
        return;
    }
    if (peep->PeepFlags & PEEP_FLAGS_LEAVING_PARK)
        return;
    if (peep->x == LOCATION_NULL)
        return;
    if (peep->GuestHeadingToRideId != RIDE_ID_NULL)
    {
        auto ride = get_ride(peep->GuestHeadingToRideId);
        if (ride != nullptr && predicate(*ride))
        {
            return;
        }
    }

    std::bitset<MAX_RIDES> rideConsideration;
    if (!considerOnlyCloseRides && (peep->HasItem(ShopItem::Map)))
    {
        // Consider all rides in the park
        for (const auto& ride : GetRideManager())
        {
            if (predicate(ride))
            {
                rideConsideration[ride.id] = true;
            }
        }
    }
    else
    {
        // Take nearby rides into consideration
        constexpr auto searchRadius = 10 * 32;
        int32_t cx = floor2(peep->x, 32);
        int32_t cy = floor2(peep->y, 32);
        for (auto x = cx - searchRadius; x <= cx + searchRadius; x += COORDS_XY_STEP)
        {
            for (auto y = cy - searchRadius; y <= cy + searchRadius; y += COORDS_XY_STEP)
            {
                auto location = CoordsXY{ x, y };
                if (!map_is_location_valid(location))
                    continue;

                for (auto* trackElement : TileElementsView<TrackElement>(location))
                {
                    auto rideIndex = trackElement->GetRideIndex();
                    auto ride = get_ride(rideIndex);
                    if (ride == nullptr)
                        continue;

                    if (!predicate(*ride))
                        continue;

                    rideConsideration[rideIndex] = true;
                }
            }
        }
    }

    // Filter the considered rides
    uint8_t potentialRides[MAX_RIDES];
    size_t numPotentialRides = 0;
    for (auto& ride : GetRideManager())
    {
        if (rideConsideration[ride.id])
        {
            if (!(ride.lifecycle_flags & RIDE_LIFECYCLE_QUEUE_FULL))
            {
                if (peep->ShouldGoOnRide(&ride, 0, false, true))
                {
                    potentialRides[numPotentialRides++] = ride.id;
                }
            }
        }
    }

    // Pick the closest ride
    Ride* closestRide{};
    auto closestRideDistance = std::numeric_limits<int32_t>::max();
    for (size_t i = 0; i < numPotentialRides; i++)
    {
        auto ride = get_ride(potentialRides[i]);
        if (ride != nullptr)
        {
            auto rideLocation = ride->stations[0].Start;
            int32_t distance = abs(rideLocation.x - peep->x) + abs(rideLocation.y - peep->y);
            if (distance < closestRideDistance)
            {
                closestRide = ride;
                closestRideDistance = distance;
            }
        }
    }
    if (closestRide != nullptr)
    {
        // Head to that ride
        peep->GuestHeadingToRideId = closestRide->id;
        peep->GuestIsLostCountdown = 200;
        peep->ResetPathfindGoal();
        peep->WindowInvalidateFlags |= PEEP_INVALIDATE_PEEP_ACTION;
        peep->TimeLost = 0;
    }
}

static void peep_head_for_nearest_ride_type(Guest* peep, int32_t rideType)
{
    auto considerOnlyCloseRides = rideType == RIDE_TYPE_FIRST_AID;
    return peep_head_for_nearest_ride(
        peep, considerOnlyCloseRides, [rideType](const Ride& ride) { return ride.type == rideType; });
}

static void peep_head_for_nearest_ride_with_flags(Guest* peep, int32_t rideTypeFlags)
{
    if ((rideTypeFlags & RIDE_TYPE_FLAG_IS_TOILET) && peep->HasFoodOrDrink())
    {
        return;
    }
    peep_head_for_nearest_ride(
        peep, false, [rideTypeFlags](const Ride& ride) { return ride.GetRideTypeDescriptor().HasFlag(rideTypeFlags); });
}

/**
 *
 *  rct2: 0x00699FE3
 * Stops peeps that are having thoughts
 * such as "I'm hungry" after visiting a food shop.
 * Works for Thirst/Hungry/Low Money/Toilet
 */
void Guest::StopPurchaseThought(uint8_t ride_type)
{
    auto thoughtType = PeepThoughtType::Hungry;

    if (!GetRideTypeDescriptor(ride_type).HasFlag(RIDE_TYPE_FLAG_SELLS_FOOD))
    {
        thoughtType = PeepThoughtType::Thirsty;
        if (!GetRideTypeDescriptor(ride_type).HasFlag(RIDE_TYPE_FLAG_SELLS_DRINKS))
        {
            thoughtType = PeepThoughtType::RunningOut;
            if (ride_type != RIDE_TYPE_CASH_MACHINE)
            {
                thoughtType = PeepThoughtType::Toilet;
                if (!GetRideTypeDescriptor(ride_type).HasFlag(RIDE_TYPE_FLAG_IS_TOILET))
                {
                    return;
                }
            }
        }
    }

    // Remove the related thought
    for (int32_t i = 0; i < PEEP_MAX_THOUGHTS; ++i)
    {
        rct_peep_thought* thought = &Thoughts[i];

        if (thought->type == PeepThoughtType::None)
            break;

        if (thought->type != thoughtType)
            continue;

        if (i < PEEP_MAX_THOUGHTS - 1)
        {
            memmove(thought, thought + 1, sizeof(rct_peep_thought) * (PEEP_MAX_THOUGHTS - i - 1));
        }

        Thoughts[PEEP_MAX_THOUGHTS - 1].type = PeepThoughtType::None;

        WindowInvalidateFlags |= PEEP_INVALIDATE_PEEP_THOUGHTS;
        i--;
    }
}

/**
 *
 *  rct2: 0x0069AEB7
 */
static bool peep_should_use_cash_machine(Peep* peep, ride_id_t rideIndex)
{
    if (gParkFlags & PARK_FLAGS_NO_MONEY)
        return false;
    if (peep->PeepFlags & PEEP_FLAGS_LEAVING_PARK)
        return false;
    if (peep->CashInPocket > MONEY(20, 00))
        return false;
    if (115 + (scenario_rand() % 128) > peep->Happiness)
        return false;
    if (peep->Energy < 80)
        return false;

    auto ride = get_ride(rideIndex);
    if (ride != nullptr)
    {
        ride_update_satisfaction(ride, peep->Happiness >> 6);
        ride->cur_num_customers++;
        ride->total_customers++;
        ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_CUSTOMER;
    }
    return true;
}

/**
 *
 *  rct2: 0x006912A3
 */
void Guest::UpdateBuying()
{
    if (!CheckForPath())
        return;

    auto ride = get_ride(CurrentRide);
    if (ride == nullptr || ride->status != RIDE_STATUS_OPEN)
    {
        SetState(PeepState::Falling);
        return;
    }

    if (SubState == 1)
    {
        if (Action != PeepActionType::None2)
        {
            UpdateAction();
            Invalidate();
            return;
        }

        if (ride->type == RIDE_TYPE_CASH_MACHINE)
        {
            if (CurrentRide != PreviousRide)
            {
                CashInPocket += MONEY(50, 00);
            }
            window_invalidate_by_number(WC_PEEP, sprite_index);
        }
        sprite_direction ^= 0x10;

        auto destination = CoordsXY{ 16, 16 } + NextLoc;
        SetDestination(destination);
        PeepDirection = direction_reverse(PeepDirection);

        SetState(PeepState::Walking);
        return;
    }

    bool item_bought = false;

    if (CurrentRide != PreviousRide)
    {
        if (ride->type == RIDE_TYPE_CASH_MACHINE)
        {
            item_bought = peep_should_use_cash_machine(this, CurrentRide);
            if (!item_bought)
            {
                PreviousRide = CurrentRide;
                PreviousRideTimeOut = 0;
            }
            else
            {
                Action = PeepActionType::WithdrawMoney;
                ActionFrame = 0;
                ActionSpriteImageOffset = 0;

                UpdateCurrentActionSpriteType();

                ride->no_primary_items_sold++;
            }
        }
        else
        {
            rct_ride_entry* ride_type = get_ride_entry(ride->subtype);
            if (ride_type == nullptr)
            {
                return;
            }
            if (ride_type->shop_item[1] != ShopItem::None)
            {
                money16 price = ride->price[1];

                item_bought = DecideAndBuyItem(ride, ride_type->shop_item[1], price);
                if (item_bought)
                {
                    ride->no_secondary_items_sold++;
                }
            }

            if (!item_bought && ride_type->shop_item[0] != ShopItem::None)
            {
                money16 price = ride->price[0];

                item_bought = DecideAndBuyItem(ride, ride_type->shop_item[0], price);
                if (item_bought)
                {
                    ride->no_primary_items_sold++;
                }
            }
        }
    }

    if (item_bought)
    {
        ride_update_popularity(ride, 1);

        StopPurchaseThought(ride->type);
    }
    else
    {
        ride_update_popularity(ride, 0);
    }
    SubState = 1;
}

/**
 *
 *  rct2: 0x00691A3B
 */
void Guest::UpdateRideAtEntrance()
{
    auto ride = get_ride(CurrentRide);
    if (ride == nullptr)
        return;

    // The peep will keep advancing in the entranceway
    // whilst in this state. When it has reached the very
    // front of the queue destination tolerance is set to
    // zero to indicate it is final decision time (try_leave will pass).
    // When a peep has to return to the queue without getting on a ride
    // this is the state it will return to.
    if (DestinationTolerance != 0)
    {
        int16_t xy_distance;
        if (auto loc = UpdateAction(xy_distance))
        {
            int16_t actionZ = z;
            if (xy_distance < 16)
            {
                auto entrance = ride_get_entrance_location(ride, CurrentRideStation).ToCoordsXYZ();
                actionZ = entrance.z + 2;
            }
            MoveTo({ *loc, actionZ });
        }
        else
        {
            DestinationTolerance = 0;
            sprite_direction ^= (1 << 4);
            Invalidate();
        }
    }

    std::vector<uint8_t> carArray;

    if (ride->GetRideTypeDescriptor().HasFlag(RIDE_TYPE_FLAG_NO_VEHICLES))
    {
        if (ride->num_riders >= ride->operation_option)
            return;
    }
    else
    {
        if (!FindVehicleToEnter(ride, carArray))
            return;
    }

    if (ride->status != RIDE_STATUS_OPEN || ride->vehicle_change_timeout != 0)
    {
        peep_update_ride_at_entrance_try_leave(this);
        return;
    }

    if (ride->lifecycle_flags & RIDE_LIFECYCLE_BROKEN_DOWN)
        return;

    money16 ridePrice = ride_get_price(ride);
    if (ridePrice != 0)
    {
        if (!peep_check_ride_price_at_entrance(this, ride, ridePrice))
            return;
    }

    if (!ride->GetRideTypeDescriptor().HasFlag(RIDE_TYPE_FLAG_NO_VEHICLES))
    {
        Vehicle* vehicle = peep_choose_car_from_ride(this, ride, carArray);
        peep_choose_seat_from_car(this, ride, vehicle);
    }
    GoToRideEntrance(ride);
}

/** rct2: 0x00981FD4, 0x00981FD6 */
static constexpr const CoordsXY _MazeEntranceStart[] = {
    { 8, 8 },
    { 8, 24 },
    { 24, 24 },
    { 24, 8 },
};

static void peep_update_ride_leave_entrance_maze(Guest* peep, Ride* ride, CoordsXYZD& entrance_loc)
{
    peep->MazeLastEdge = entrance_loc.direction + 1;

    entrance_loc.x += CoordsDirectionDelta[entrance_loc.direction].x;
    entrance_loc.y += CoordsDirectionDelta[entrance_loc.direction].y;

    uint8_t direction = entrance_loc.direction * 4 + 11;
    if (scenario_rand() & 0x40)
    {
        direction += 4;
        peep->MazeLastEdge += 2;
    }

    direction &= 0xF;
    // Direction is 11, 15, 3, or 7
    peep->Var37 = direction;
    peep->MazeLastEdge &= 3;

    entrance_loc.x += _MazeEntranceStart[direction / 4].x;
    entrance_loc.y += _MazeEntranceStart[direction / 4].y;

    peep->SetDestination(entrance_loc, 3);

    ride->cur_num_customers++;
    peep->OnEnterRide(peep->CurrentRide);
    peep->RideSubState = PeepRideSubState::MazePathfinding;
}

static void peep_update_ride_leave_entrance_spiral_slide(Guest* peep, Ride* ride, CoordsXYZD& entrance_loc)
{
    entrance_loc = { ride->stations[peep->CurrentRideStation].GetStart(), entrance_loc.direction };

    TileElement* tile_element = ride_get_station_start_track_element(ride, peep->CurrentRideStation);

    uint8_t direction_track = (tile_element == nullptr ? 0 : tile_element->GetDirection());

    peep->Var37 = (entrance_loc.direction << 2) | (direction_track << 4);

    entrance_loc += SpiralSlideWalkingPath[peep->Var37];

    peep->SetDestination(entrance_loc);
    peep->CurrentCar = 0;

    ride->cur_num_customers++;
    peep->OnEnterRide(peep->CurrentRide);
    peep->RideSubState = PeepRideSubState::ApproachSpiralSlide;
}

static uint8_t peep_get_waypointed_seat_location(
    Peep* peep, Ride* ride, rct_ride_entry_vehicle* vehicle_type, uint8_t track_direction)
{
    // The seatlocation can be split into segments around the ride base
    // to decide the segment first split off the segmentable seat location
    // from the fixed section
    uint8_t seatLocationSegment = peep->CurrentSeat & 0x7;
    uint8_t seatLocationFixed = peep->CurrentSeat & 0xF8;

    // Enterprise has more segments (8) compared to the normal (4)
    if (ride->type != RIDE_TYPE_ENTERPRISE)
        track_direction *= 2;

    // Type 1 loading doesn't do segments and all peeps go to the same
    // location on the ride
    if (vehicle_type->peep_loading_waypoint_segments == 0)
    {
        track_direction /= 2;
        seatLocationSegment = 0;
        seatLocationFixed = 0;
    }
    seatLocationSegment += track_direction;
    seatLocationSegment &= 0x7;
    return seatLocationSegment + seatLocationFixed;
}

static void peep_update_ride_leave_entrance_waypoints(Peep* peep, Ride* ride)
{
    TileCoordsXYZD entranceLocation = ride_get_entrance_location(ride, peep->CurrentRideStation);
    Guard::Assert(!entranceLocation.isNull());
    uint8_t direction_entrance = entranceLocation.direction;

    CoordsXY waypoint = ride->stations[peep->CurrentRideStation].Start.ToTileCentre();

    TileElement* tile_element = ride_get_station_start_track_element(ride, peep->CurrentRideStation);

    uint8_t direction_track = (tile_element == nullptr ? 0 : tile_element->GetDirection());

    auto vehicle = GetEntity<Vehicle>(ride->vehicles[peep->CurrentTrain]);
    if (vehicle == nullptr)
    {
        // TODO: Goto ride exit on failure.
        return;
    }
    auto ride_entry = vehicle->GetRideEntry();
    auto vehicle_type = &ride_entry->vehicles[vehicle->vehicle_type];

    peep->Var37 = (direction_entrance | peep_get_waypointed_seat_location(peep, ride, vehicle_type, direction_track) * 4) * 4;

    if (ride->type == RIDE_TYPE_ENTERPRISE)
    {
        waypoint.x = vehicle->x;
        waypoint.y = vehicle->y;
    }

    Guard::Assert(vehicle_type->peep_loading_waypoints.size() >= static_cast<size_t>(peep->Var37 / 4));
    waypoint.x += vehicle_type->peep_loading_waypoints[peep->Var37 / 4][0].x;
    waypoint.y += vehicle_type->peep_loading_waypoints[peep->Var37 / 4][0].y;

    peep->SetDestination(waypoint);
    peep->RideSubState = PeepRideSubState::ApproachVehicleWaypoints;
}

/**
 *
 *  rct2: 0x006921D3
 */
void Guest::UpdateRideAdvanceThroughEntrance()
{
    auto ride = get_ride(CurrentRide);
    if (ride == nullptr)
        return;

    int16_t actionZ, xy_distance;

    auto ride_entry = ride->GetRideEntry();

    if (auto loc = UpdateAction(xy_distance))
    {
        uint16_t distanceThreshold = 16;
        if (ride_entry != nullptr)
        {
            uint8_t vehicle = ride_entry->default_vehicle;
            if (ride_entry->vehicles[vehicle].flags & VEHICLE_ENTRY_FLAG_MINI_GOLF
                || ride_entry->vehicles[vehicle].flags & (VEHICLE_ENTRY_FLAG_CHAIRLIFT | VEHICLE_ENTRY_FLAG_GO_KART))
            {
                distanceThreshold = 28;
            }
        }

        if (RideSubState == PeepRideSubState::InEntrance && xy_distance < distanceThreshold)
        {
            RideSubState = PeepRideSubState::FreeVehicleCheck;
        }

        actionZ = ride->stations[CurrentRideStation].GetBaseZ();

        distanceThreshold += 4;
        if (xy_distance < distanceThreshold)
        {
            actionZ += ride->GetRideTypeDescriptor().Heights.PlatformHeight;
        }

        MoveTo({ *loc, actionZ });
        return;
    }

    Guard::Assert(RideSubState == PeepRideSubState::LeaveEntrance, "Peep ridesubstate should be LeaveEntrance");
    if (ride->GetRideTypeDescriptor().HasFlag(RIDE_TYPE_FLAG_NO_VEHICLES))
    {
        auto entranceLocation = ride_get_entrance_location(ride, CurrentRideStation).ToCoordsXYZD();
        Guard::Assert(!entranceLocation.isNull());

        if (ride->type == RIDE_TYPE_MAZE)
        {
            peep_update_ride_leave_entrance_maze(this, ride, entranceLocation);
            return;
        }
        else if (ride->type == RIDE_TYPE_SPIRAL_SLIDE)
        {
            peep_update_ride_leave_entrance_spiral_slide(this, ride, entranceLocation);
            return;
        }
        else
        {
            // If the ride type was changed guests will become stuck.
            // Inform the player about this if its a new issue or hasn't been addressed within 120 seconds.
            if ((ride->current_issues & RIDE_ISSUE_GUESTS_STUCK) == 0 || gCurrentTicks - ride->last_issue_time > 3000)
            {
                ride->current_issues |= RIDE_ISSUE_GUESTS_STUCK;
                ride->last_issue_time = gCurrentTicks;

                auto ft = Formatter();
                ride->FormatNameTo(ft);
                if (gConfigNotifications.ride_warnings)
                {
                    News::AddItemToQueue(News::ItemType::Ride, STR_GUESTS_GETTING_STUCK_ON_RIDE, CurrentRide, ft);
                }
            }

            return;
        }
    }

    Vehicle* vehicle = GetEntity<Vehicle>(ride->vehicles[CurrentTrain]);
    if (vehicle == nullptr)
    {
        return;
    }

    vehicle = vehicle->GetCar(CurrentCar);

    ride_entry = vehicle->GetRideEntry();
    if (ride_entry == nullptr)
    {
        return;
    }

    rct_ride_entry_vehicle* vehicle_type = &ride_entry->vehicles[vehicle->vehicle_type];

    if (vehicle_type->flags & VEHICLE_ENTRY_FLAG_LOADING_WAYPOINTS)
    {
        peep_update_ride_leave_entrance_waypoints(this, ride);
        return;
    }

    if (vehicle_type->flags & VEHICLE_ENTRY_FLAG_DODGEM_CAR_PLACEMENT)
    {
        SetDestination(vehicle->GetLocation(), 15);
        RideSubState = PeepRideSubState::ApproachVehicle;
        return;
    }

    int8_t load_position = 0;
    // Safe, in case current seat > number of loading positions
    uint16_t numSeatPositions = static_cast<uint16_t>(vehicle_type->peep_loading_positions.size());
    if (numSeatPositions != 0)
    {
        size_t loadPositionIndex = numSeatPositions - 1;
        if (CurrentSeat < numSeatPositions)
        {
            loadPositionIndex = CurrentSeat;
        }
        load_position = vehicle_type->peep_loading_positions[loadPositionIndex];
    }

    auto destination = GetDestination();
    switch (vehicle->sprite_direction / 8)
    {
        case 0:
            destination.x = vehicle->x - load_position;
            break;
        case 1:
            destination.y = vehicle->y + load_position;
            break;
        case 2:
            destination.x = vehicle->x + load_position;
            break;
        case 3:
            destination.y = vehicle->y - load_position;
            break;
    }
    SetDestination(destination);

    RideSubState = PeepRideSubState::ApproachVehicle;
}

/**
 *
 *  rct2: 0x0069321D
 */
static void peep_go_to_ride_exit(Peep* peep, Ride* ride, int16_t x, int16_t y, int16_t z, uint8_t exit_direction)
{
    z += ride->GetRideTypeDescriptor().Heights.PlatformHeight;

    peep->MoveTo({ x, y, z });

    Guard::Assert(peep->CurrentRideStation < MAX_STATIONS);
    auto exit = ride_get_exit_location(ride, peep->CurrentRideStation);
    Guard::Assert(!exit.isNull());
    x = exit.x;
    y = exit.y;
    x *= 32;
    y *= 32;
    x += 16;
    y += 16;

    int16_t x_shift = DirectionOffsets[exit_direction].x;
    int16_t y_shift = DirectionOffsets[exit_direction].y;

    int16_t shift_multiplier = 20;

    rct_ride_entry* rideEntry = get_ride_entry(ride->subtype);
    if (rideEntry != nullptr)
    {
        rct_ride_entry_vehicle* vehicle_entry = &rideEntry->vehicles[rideEntry->default_vehicle];
        if (vehicle_entry->flags & VEHICLE_ENTRY_FLAG_MINI_GOLF
            || vehicle_entry->flags & (VEHICLE_ENTRY_FLAG_CHAIRLIFT | VEHICLE_ENTRY_FLAG_GO_KART))
        {
            shift_multiplier = 32;
        }
    }

    x_shift *= shift_multiplier;
    y_shift *= shift_multiplier;

    x -= x_shift;
    y -= y_shift;

    peep->SetDestination({ x, y }, 2);

    peep->sprite_direction = exit_direction * 8;
    peep->RideSubState = PeepRideSubState::ApproachExit;
}

/**
 *
 *  rct2: 0x006920B4
 */
void Guest::UpdateRideFreeVehicleEnterRide(Ride* ride)
{
    money16 ridePrice = ride_get_price(ride);
    if (ridePrice != 0)
    {
        if ((HasItem(ShopItem::Voucher)) && (VoucherType == VOUCHER_TYPE_RIDE_FREE) && (VoucherRideId == CurrentRide))
        {
            RemoveItem(ShopItem::Voucher);
            WindowInvalidateFlags |= PEEP_INVALIDATE_PEEP_INVENTORY;
        }
        else
        {
            ride->total_profit += ridePrice;
            ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;
            SpendMoney(PaidOnRides, ridePrice, ExpenditureType::ParkRideTickets);
        }
    }

    RideSubState = PeepRideSubState::LeaveEntrance;
    uint8_t queueTime = DaysInQueue;
    if (queueTime < 253)
        queueTime += 3;

    queueTime /= 2;
    if (queueTime != ride->stations[CurrentRideStation].QueueTime)
    {
        ride->stations[CurrentRideStation].QueueTime = queueTime;
        window_invalidate_by_number(WC_RIDE, CurrentRide);
    }

    if (PeepFlags & PEEP_FLAGS_TRACKING)
    {
        auto ft = Formatter();
        FormatNameTo(ft);
        ride->FormatNameTo(ft);

        rct_string_id msg_string;
        if (ride->GetRideTypeDescriptor().HasFlag(RIDE_TYPE_FLAG_IN_RIDE))
            msg_string = STR_PEEP_TRACKING_PEEP_IS_IN_X;
        else
            msg_string = STR_PEEP_TRACKING_PEEP_IS_ON_X;

        if (gConfigNotifications.guest_on_ride)
        {
            News::AddItemToQueue(News::ItemType::PeepOnRide, msg_string, sprite_index, ft);
        }
    }

    if (ride->type == RIDE_TYPE_SPIRAL_SLIDE)
    {
        SwitchToSpecialSprite(1);
    }

    UpdateRideAdvanceThroughEntrance();
}

/**
 *
 *  rct2: 0x00691FD4
 */
static void peep_update_ride_no_free_vehicle_rejoin_queue(Peep* peep, Ride* ride)
{
    TileCoordsXYZD entranceLocation = ride_get_entrance_location(ride, peep->CurrentRideStation);

    int32_t x = entranceLocation.x * 32;
    int32_t y = entranceLocation.y * 32;
    x += 16 - DirectionOffsets[entranceLocation.direction].x * 20;
    y += 16 - DirectionOffsets[entranceLocation.direction].y * 20;

    peep->SetDestination({ x, y }, 2);
    peep->SetState(PeepState::QueuingFront);
    peep->RideSubState = PeepRideSubState::AtEntrance;

    ride->QueueInsertGuestAtFront(peep->CurrentRideStation, peep);
}

/**
 *
 *  rct2: 0x00691E42
 * Note: Before this was the entry
 * point for sub state 1 and 3. The
 * check has been removed that would
 * branch it out to 1 and 3. Now uses
 * separate functions.
 */
void Guest::UpdateRideFreeVehicleCheck()
{
    auto ride = get_ride(CurrentRide);
    if (ride == nullptr)
        return;

    if (ride->GetRideTypeDescriptor().HasFlag(RIDE_TYPE_FLAG_NO_VEHICLES))
    {
        if (ride->status != RIDE_STATUS_OPEN || ride->vehicle_change_timeout != 0 || (++RejoinQueueTimeout) == 0)
        {
            peep_update_ride_no_free_vehicle_rejoin_queue(this, ride);
            return;
        }

        UpdateRideFreeVehicleEnterRide(ride);
        return;
    }

    Vehicle* vehicle = GetEntity<Vehicle>(ride->vehicles[CurrentTrain]);
    if (vehicle == nullptr)
    {
        // TODO: Leave ride on failure goes for all returns on nullptr in this function
        return;
    }
    vehicle = vehicle->GetCar(CurrentCar);
    if (vehicle == nullptr)
        return;

    rct_ride_entry* ride_entry = vehicle->GetRideEntry();
    if (ride_entry == nullptr)
    {
        return;
    }

    if (ride_entry->vehicles[0].flags & VEHICLE_ENTRY_FLAG_MINI_GOLF)
    {
        vehicle->mini_golf_flags &= ~(1 << 5);

        for (size_t i = 0; i < ride->num_vehicles; ++i)
        {
            Vehicle* train = GetEntity<Vehicle>(ride->vehicles[i]);
            if (train == nullptr)
                continue;

            Vehicle* second_vehicle = GetEntity<Vehicle>(train->next_vehicle_on_train);
            if (second_vehicle == nullptr)
                continue;

            if (second_vehicle->num_peeps == 0)
                continue;

            if (second_vehicle->mini_golf_flags & (1 << 5))
                continue;

            return;
        }
    }

    if (!vehicle->IsUsedInPairs())
    {
        UpdateRideFreeVehicleEnterRide(ride);
        return;
    }

    if (ride->mode == RideMode::ForwardRotation || ride->mode == RideMode::BackwardRotation)
    {
        if (CurrentSeat & 1 || !(vehicle->next_free_seat & 1))
        {
            UpdateRideFreeVehicleEnterRide(ride);
            return;
        }
    }
    else
    {
        uint8_t seat = CurrentSeat | 1;
        if (seat < vehicle->next_free_seat)
        {
            UpdateRideFreeVehicleEnterRide(ride);
            return;
        }
    }

    Vehicle* currentTrain = GetEntity<Vehicle>(ride->vehicles[CurrentTrain]);
    if (currentTrain == nullptr)
    {
        return;
    }
    if (ride->status == RIDE_STATUS_OPEN && ++RejoinQueueTimeout != 0
        && !currentTrain->HasUpdateFlag(VEHICLE_UPDATE_FLAG_TRAIN_READY_DEPART))
    {
        return;
    }

    if (ride->mode != RideMode::ForwardRotation && ride->mode != RideMode::BackwardRotation)
    {
        if (vehicle->next_free_seat - 1 != CurrentSeat)
            return;
    }

    vehicle->next_free_seat--;
    vehicle->peep[CurrentSeat] = SPRITE_INDEX_NULL;

    peep_update_ride_no_free_vehicle_rejoin_queue(this, ride);
}

void Guest::UpdateRideApproachVehicle()
{
    if (auto loc = UpdateAction())
    {
        MoveTo({ *loc, z });
        return;
    }
    RideSubState = PeepRideSubState::EnterVehicle;
}

void Guest::UpdateRideEnterVehicle()
{
    auto* ride = get_ride(CurrentRide);
    if (ride != nullptr)
    {
        auto* vehicle = GetEntity<Vehicle>(ride->vehicles[CurrentTrain]);
        if (vehicle != nullptr)
        {
            vehicle = vehicle->GetCar(CurrentCar);

            if (ride->mode != RideMode::ForwardRotation && ride->mode != RideMode::BackwardRotation)
            {
                if (CurrentSeat != vehicle->num_peeps)
                    return;
            }

            if (vehicle->IsUsedInPairs())
            {
                auto* seatedGuest = GetEntity<Guest>(vehicle->peep[CurrentSeat ^ 1]);
                if (seatedGuest != nullptr)
                {
                    if (seatedGuest->RideSubState != PeepRideSubState::EnterVehicle)
                        return;

                    vehicle->num_peeps++;
                    ride->cur_num_customers++;

                    vehicle->ApplyMass(seatedGuest->Mass);
                    seatedGuest->MoveTo({ LOCATION_NULL, 0, 0 });
                    seatedGuest->SetState(PeepState::OnRide);
                    seatedGuest->GuestTimeOnRide = 0;
                    seatedGuest->RideSubState = PeepRideSubState::OnRide;
                    seatedGuest->OnEnterRide(CurrentRide);
                }
            }

            vehicle->num_peeps++;
            ride->cur_num_customers++;

            vehicle->ApplyMass(Mass);
            vehicle->Invalidate();

            MoveTo({ LOCATION_NULL, 0, 0 });

            SetState(PeepState::OnRide);

            GuestTimeOnRide = 0;
            RideSubState = PeepRideSubState::OnRide;
            OnEnterRide(CurrentRide);
        }
    }
}

/**
 *
 *  rct2: 0x00693028
 */
void Guest::UpdateRideLeaveVehicle()
{
    auto ride = get_ride(CurrentRide);
    if (ride == nullptr)
        return;

    Vehicle* vehicle = GetEntity<Vehicle>(ride->vehicles[CurrentTrain]);
    if (vehicle == nullptr)
        return;

    uint8_t ride_station = vehicle->current_station;
    vehicle = vehicle->GetCar(CurrentCar);
    if (vehicle == nullptr)
    {
        return;
    }

    // Check if ride is NOT Ferris Wheel.
    if (ride->mode != RideMode::ForwardRotation && ride->mode != RideMode::BackwardRotation)
    {
        if (vehicle->num_peeps - 1 != CurrentSeat)
            return;
    }

    ActionSpriteImageOffset++;
    if (ActionSpriteImageOffset & 3)
        return;

    ActionSpriteImageOffset = 0;

    vehicle->num_peeps--;
    vehicle->ApplyMass(-Mass);
    vehicle->Invalidate();

    if (ride_station >= MAX_STATIONS)
    {
        // HACK #5658: Some parks have hacked rides which end up in this state
        auto bestStationIndex = ride_get_first_valid_station_exit(ride);
        if (bestStationIndex == STATION_INDEX_NULL)
        {
            bestStationIndex = 0;
        }
        ride_station = bestStationIndex;
    }
    CurrentRideStation = ride_station;
    rct_ride_entry* rideEntry = vehicle->GetRideEntry();
    if (rideEntry == nullptr)
    {
        return;
    }

    rct_ride_entry_vehicle* vehicle_entry = &rideEntry->vehicles[vehicle->vehicle_type];

    if (!(vehicle_entry->flags & VEHICLE_ENTRY_FLAG_LOADING_WAYPOINTS))
    {
        assert(CurrentRideStation < MAX_STATIONS);
        TileCoordsXYZD exitLocation = ride_get_exit_location(ride, CurrentRideStation);
        CoordsXYZD platformLocation;
        platformLocation.z = ride->stations[CurrentRideStation].GetBaseZ();

        platformLocation.direction = direction_reverse(exitLocation.direction);

        if (!ride->GetRideTypeDescriptor().HasFlag(RIDE_TYPE_FLAG_VEHICLE_IS_INTEGRAL))
        {
            for (; vehicle != nullptr && !vehicle->IsHead(); vehicle = GetEntity<Vehicle>(vehicle->prev_vehicle_on_ride))
            {
                auto trackType = vehicle->GetTrackType();
                if (trackType == TrackElemType::Flat || trackType > TrackElemType::MiddleStation)
                    continue;

                bool foundStation = false;
                for (auto* trackElement : TileElementsView<TrackElement>(vehicle->TrackLocation))
                {
                    if (trackElement->GetBaseZ() != vehicle->TrackLocation.z)
                        continue;

                    if (trackElement->GetStationIndex() != CurrentRideStation)
                        continue;

                    foundStation = true;
                    break;
                }

                if (foundStation)
                    break;
            }

            if (vehicle == nullptr)
            {
                return;
            }
            uint8_t shiftMultiplier = 12;
            uint8_t specialDirection = platformLocation.direction;

            rideEntry = get_ride_entry(ride->subtype);

            if (rideEntry != nullptr)
            {
                vehicle_entry = &rideEntry->vehicles[rideEntry->default_vehicle];

                if (vehicle_entry->flags & VEHICLE_ENTRY_FLAG_GO_KART)
                {
                    shiftMultiplier = 9;
                }

                if (vehicle_entry->flags & (VEHICLE_ENTRY_FLAG_CHAIRLIFT | VEHICLE_ENTRY_FLAG_GO_KART))
                {
                    specialDirection = ((vehicle->sprite_direction + 3) / 8) + 1;
                    specialDirection &= 3;

                    if (vehicle->TrackSubposition == VehicleTrackSubposition::GoKartsRightLane)
                        specialDirection = direction_reverse(specialDirection);
                }
            }

            int16_t xShift = DirectionOffsets[specialDirection].x;
            int16_t yShift = DirectionOffsets[specialDirection].y;

            platformLocation.x = vehicle->x + xShift * shiftMultiplier;
            platformLocation.y = vehicle->y + yShift * shiftMultiplier;

            peep_go_to_ride_exit(
                this, ride, platformLocation.x, platformLocation.y, platformLocation.z, platformLocation.direction);
            return;
        }

        platformLocation.x = vehicle->x + DirectionOffsets[platformLocation.direction].x * 12;
        platformLocation.y = vehicle->y + DirectionOffsets[platformLocation.direction].y * 12;

        // This can evaluate to false with buggy custom rides.
        if (CurrentSeat < vehicle_entry->peep_loading_positions.size())
        {
            int8_t loadPosition = vehicle_entry->peep_loading_positions[CurrentSeat];

            switch (vehicle->sprite_direction / 8)
            {
                case 0:
                    platformLocation.x -= loadPosition;
                    break;
                case 1:
                    platformLocation.y += loadPosition;
                    break;
                case 2:
                    platformLocation.x += loadPosition;
                    break;
                case 3:
                    platformLocation.y -= loadPosition;
                    break;
            }
        }
        else
        {
            log_verbose(
                "CurrentSeat %d is too large! (Vehicle entry has room for %d.)", CurrentSeat,
                vehicle_entry->peep_loading_positions.size());
        }

        platformLocation.z = ride->stations[CurrentRideStation].GetBaseZ();

        peep_go_to_ride_exit(
            this, ride, platformLocation.x, platformLocation.y, platformLocation.z, platformLocation.direction);
        return;
    }

    auto exitLocation = ride_get_exit_location(ride, CurrentRideStation).ToCoordsXYZD();
    Guard::Assert(!exitLocation.isNull());

    auto waypointLoc = CoordsXYZ{ ride->stations[CurrentRideStation].Start.ToTileCentre(),
                                  exitLocation.z + ride->GetRideTypeDescriptor().Heights.PlatformHeight };

    TileElement* trackElement = ride_get_station_start_track_element(ride, CurrentRideStation);

    Direction station_direction = (trackElement == nullptr ? 0 : trackElement->GetDirection());

    vehicle = GetEntity<Vehicle>(ride->vehicles[CurrentTrain]);
    if (vehicle == nullptr)
    {
        return;
    }

    rideEntry = vehicle->GetRideEntry();
    rct_ride_entry_vehicle* vehicleEntry = &rideEntry->vehicles[vehicle->vehicle_type];
    if (vehicleEntry == nullptr)
        return;

    Var37 = ((exitLocation.direction | peep_get_waypointed_seat_location(this, ride, vehicleEntry, station_direction) * 4) * 4)
        | 1;

    if (ride->type == RIDE_TYPE_ENTERPRISE)
    {
        waypointLoc.x = vehicle->x;
        waypointLoc.y = vehicle->y;
    }

    Guard::Assert(vehicleEntry->peep_loading_waypoints.size() >= static_cast<size_t>(Var37 / 4));
    CoordsXYZ exitWaypointLoc = waypointLoc;

    exitWaypointLoc.x += vehicleEntry->peep_loading_waypoints[Var37 / 4][2].x;
    exitWaypointLoc.y += vehicleEntry->peep_loading_waypoints[Var37 / 4][2].y;

    if (ride->type == RIDE_TYPE_MOTION_SIMULATOR)
        exitWaypointLoc.z += 15;

    MoveTo(exitWaypointLoc);

    waypointLoc.x += vehicleEntry->peep_loading_waypoints[Var37 / 4][1].x;
    waypointLoc.y += vehicleEntry->peep_loading_waypoints[Var37 / 4][1].y;

    SetDestination(waypointLoc, 2);
    RideSubState = PeepRideSubState::ApproachExitWaypoints;
}

/**
 *
 *  rct2: 0x0069376A
 */
static void peep_update_ride_prepare_for_exit(Peep* peep)
{
    auto ride = get_ride(peep->CurrentRide);
    if (ride == nullptr || peep->CurrentRideStation >= std::size(ride->stations))
        return;

    auto exit = ride_get_exit_location(ride, peep->CurrentRideStation);
    int16_t x = exit.x;
    int16_t y = exit.y;
    uint8_t exit_direction = exit.direction;

    x *= 32;
    y *= 32;
    x += 16;
    y += 16;

    int16_t x_shift = DirectionOffsets[exit_direction].x;
    int16_t y_shift = DirectionOffsets[exit_direction].y;

    int16_t shift_multiplier = 20;

    rct_ride_entry* ride_type = get_ride_entry(ride->subtype);
    if (ride_type != nullptr)
    {
        rct_ride_entry_vehicle* vehicle_entry = &ride_type->vehicles[ride_type->default_vehicle];
        if (vehicle_entry->flags & (VEHICLE_ENTRY_FLAG_CHAIRLIFT | VEHICLE_ENTRY_FLAG_GO_KART))
        {
            shift_multiplier = 32;
        }
    }

    x_shift *= shift_multiplier;
    y_shift *= shift_multiplier;

    x -= x_shift;
    y -= y_shift;

    peep->SetDestination({ x, y }, 2);
    peep->RideSubState = PeepRideSubState::InExit;
}

/**
 *
 *  rct2: 0x0069374F
 */
void Guest::UpdateRideApproachExit()
{
    if (auto loc = UpdateAction())
    {
        MoveTo({ *loc, z });
        return;
    }

    peep_update_ride_prepare_for_exit(this);
}

/**
 *
 *  rct2: 0x0069382E
 */
void Guest::UpdateRideInExit()
{
    auto ride = get_ride(CurrentRide);
    if (ride == nullptr)
        return;

    int16_t xy_distance;

    if (auto loc = UpdateAction(xy_distance))
    {
        if (xy_distance >= 16)
        {
            int16_t actionZ = ride->stations[CurrentRideStation].GetBaseZ();

            actionZ += ride->GetRideTypeDescriptor().Heights.PlatformHeight;
            MoveTo({ *loc, actionZ });
            return;
        }

        SwitchToSpecialSprite(0);
        MoveTo({ *loc, z });
    }

    if (ride->lifecycle_flags & RIDE_LIFECYCLE_ON_RIDE_PHOTO)
    {
        ShopItem secondaryItem = ride->GetRideTypeDescriptor().PhotoItem;
        if (DecideAndBuyItem(ride, secondaryItem, ride->price[1]))
        {
            ride->no_secondary_items_sold++;
        }
    }
    RideSubState = PeepRideSubState::LeaveExit;
}
#pragma warning(default : 6011)
/**
 *
 *  rct2: 0x006926AD
 */
void Guest::UpdateRideApproachVehicleWaypoints()
{
    auto ride = get_ride(CurrentRide);
    if (ride == nullptr)
        return;

    int16_t xy_distance;
    uint8_t waypoint = Var37 & 3;

    if (auto loc = UpdateAction(xy_distance))
    {
        int16_t actionZ;
        // Motion simulators have steps this moves the peeps up the steps
        if (ride->type == RIDE_TYPE_MOTION_SIMULATOR)
        {
            actionZ = ride->stations[CurrentRideStation].GetBaseZ() + 2;

            if (waypoint == 2)
            {
                xy_distance -= 12;
                if (xy_distance < 0)
                    xy_distance = 0;

                if (xy_distance <= 15)
                {
                    actionZ += 15 - xy_distance;
                }
            }
        }
        else
        {
            actionZ = z;
        }
        MoveTo({ *loc, actionZ });
        return;
    }

    if (waypoint == 2)
    {
        RideSubState = PeepRideSubState::EnterVehicle;
        return;
    }

    waypoint++;
    // This is incrementing the actual peep waypoint
    Var37++;

    Vehicle* vehicle = GetEntity<Vehicle>(ride->vehicles[CurrentTrain]);
    if (vehicle == nullptr)
    {
        return;
    }

    CoordsXY targetLoc = ride->stations[CurrentRideStation].Start.ToTileCentre();

    if (ride->type == RIDE_TYPE_ENTERPRISE)
    {
        targetLoc.x = vehicle->x;
        targetLoc.y = vehicle->y;
    }

    rct_ride_entry* ride_entry = vehicle->GetRideEntry();
    if (ride_entry == nullptr)
    {
        return;
    }

    rct_ride_entry_vehicle* vehicle_type = &ride_entry->vehicles[vehicle->vehicle_type];
    Guard::Assert(waypoint < 3);
    targetLoc.x += vehicle_type->peep_loading_waypoints[Var37 / 4][waypoint].x;
    targetLoc.y += vehicle_type->peep_loading_waypoints[Var37 / 4][waypoint].y;

    SetDestination(targetLoc);
}

/**
 *
 *  rct2: 0x0069357D
 */
void Guest::UpdateRideApproachExitWaypoints()
{
    auto ride = get_ride(CurrentRide);
    if (ride == nullptr)
        return;

    int16_t xy_distance;

    if (auto loc = UpdateAction(xy_distance))
    {
        int16_t actionZ;
        if (ride->type == RIDE_TYPE_MOTION_SIMULATOR)
        {
            actionZ = ride->stations[CurrentRideStation].GetBaseZ() + 2;

            if ((Var37 & 3) == 1)
            {
                if (xy_distance > 15)
                    xy_distance = 15;

                actionZ += xy_distance;
            }
        }
        else
        {
            actionZ = z;
        }
        MoveTo({ *loc, actionZ });
        return;
    }

    if ((Var37 & 3) != 0)
    {
        if ((Var37 & 3) == 3)
        {
            peep_update_ride_prepare_for_exit(this);
            return;
        }

        Var37--;
        Vehicle* vehicle = GetEntity<Vehicle>(ride->vehicles[CurrentTrain]);
        if (vehicle == nullptr)
        {
            return;
        }
        CoordsXY targetLoc = ride->stations[CurrentRideStation].Start.ToTileCentre();

        if (ride->type == RIDE_TYPE_ENTERPRISE)
        {
            targetLoc.x = vehicle->x;
            targetLoc.y = vehicle->y;
        }

        rct_ride_entry* rideEntry = vehicle->GetRideEntry();
        rct_ride_entry_vehicle* vehicleEntry = &rideEntry->vehicles[vehicle->vehicle_type];

        Guard::Assert((Var37 & 3) < 3);
        targetLoc.x += vehicleEntry->peep_loading_waypoints[Var37 / 4][Var37 & 3].x;
        targetLoc.y += vehicleEntry->peep_loading_waypoints[Var37 / 4][Var37 & 3].y;

        SetDestination(targetLoc);
        return;
    }

    Var37 |= 3;

    auto targetLoc = ride_get_exit_location(ride, CurrentRideStation).ToCoordsXYZD().ToTileCentre();
    uint8_t exit_direction = direction_reverse(targetLoc.direction);

    int16_t x_shift = DirectionOffsets[exit_direction].x;
    int16_t y_shift = DirectionOffsets[exit_direction].y;

    int16_t shift_multiplier = 20;

    auto rideEntry = get_ride_entry(ride->subtype);
    if (rideEntry != nullptr)
    {
        auto vehicleEntry = &rideEntry->vehicles[rideEntry->default_vehicle];
        if (vehicleEntry->flags & (VEHICLE_ENTRY_FLAG_CHAIRLIFT | VEHICLE_ENTRY_FLAG_GO_KART))
        {
            shift_multiplier = 32;
        }
    }

    x_shift *= shift_multiplier;
    y_shift *= shift_multiplier;

    targetLoc.x -= x_shift;
    targetLoc.y -= y_shift;

    SetDestination(targetLoc);
}

/**
 *
 *  rct2: 0x006927B3
 */
void Guest::UpdateRideApproachSpiralSlide()
{
    auto ride = get_ride(CurrentRide);
    if (ride == nullptr)
        return;

    if (auto loc = UpdateAction())
    {
        MoveTo({ *loc, z });
        return;
    }

    uint8_t waypoint = Var37 & 3;

    if (waypoint == 3)
    {
        SubState = 15;
        SetDestination({ 0, 0 });
        Var37 = (Var37 / 4) & 0xC;
        MoveTo({ LOCATION_NULL, y, z });
        return;
    }
    else if (waypoint == 2)
    {
        bool lastRide = false;
        if (ride->status != RIDE_STATUS_OPEN)
            lastRide = true;
        else if (CurrentCar++ != 0)
        {
            if (ride->mode == RideMode::SingleRidePerAdmission)
                lastRide = true;
            if (static_cast<uint8_t>(CurrentCar - 1) > (scenario_rand() & 0xF))
                lastRide = true;
        }

        if (lastRide)
        {
            auto exit = ride_get_exit_location(ride, CurrentRideStation);
            waypoint = 1;
            Var37 = (exit.direction * 4) | (Var37 & 0x30) | waypoint;
            CoordsXY targetLoc = ride->stations[CurrentRideStation].Start;

            assert(ride->type == RIDE_TYPE_SPIRAL_SLIDE);
            targetLoc += SpiralSlideWalkingPath[Var37];

            SetDestination(targetLoc);
            RideSubState = PeepRideSubState::LeaveSpiralSlide;
            return;
        }
    }
    waypoint++;
    // Actually increment the real peep waypoint
    Var37++;

    CoordsXY targetLoc = ride->stations[CurrentRideStation].Start;

    assert(ride->type == RIDE_TYPE_SPIRAL_SLIDE);
    targetLoc += SpiralSlideWalkingPath[Var37];

    SetDestination(targetLoc);
}

/** rct2: 0x00981F0C, 0x00981F0E */
static constexpr const CoordsXY _SpiralSlideEnd[] = {
    { 25, 56 },
    { 56, 7 },
    { 7, -24 },
    { -24, 25 },
};

/** rct2: 0x00981F1C, 0x00981F1E */
static constexpr const CoordsXY _SpiralSlideEndWaypoint[] = {
    { 8, 56 },
    { 56, 24 },
    { 24, -24 },
    { -24, 8 },
};

/**
 *
 *  rct2: 0x00692D83
 */
void Guest::UpdateRideOnSpiralSlide()
{
    auto ride = get_ride(CurrentRide);
    if (ride == nullptr || ride->type != RIDE_TYPE_SPIRAL_SLIDE)
        return;

    auto destination = GetDestination();
    if ((Var37 & 3) == 0)
    {
        switch (destination.x)
        {
            case 0:
                destination.y++;
                if (destination.y >= 30)
                    destination.x++;

                SetDestination(destination);
                return;
            case 1:
                if (ride->slide_in_use != 0)
                    return;

                ride->slide_in_use++;
                ride->slide_peep = sprite_index;
                ride->slide_peep_t_shirt_colour = TshirtColour;
                ride->spiral_slide_progress = 0;
                destination.x++;

                SetDestination(destination);
                return;
            case 2:
                return;
            case 3:
            {
                auto newLocation = ride->stations[CurrentRideStation].Start;
                uint8_t dir = (Var37 / 4) & 3;

                // Set the location that the peep walks to go on slide again
                destination = newLocation + _SpiralSlideEndWaypoint[dir];
                SetDestination(destination);

                // Move the peep sprite to just at the end of the slide
                newLocation.x += _SpiralSlideEnd[dir].x;
                newLocation.y += _SpiralSlideEnd[dir].y;

                MoveTo({ newLocation, z });

                sprite_direction = (Var37 & 0xC) * 2;

                Var37++;
                return;
            }
            default:
                return;
        }
    }

    if (auto loc = UpdateAction())
    {
        MoveTo({ *loc, z });
        return;
    }

    uint8_t waypoint = 2;
    Var37 = (Var37 * 4 & 0x30) + waypoint;

    CoordsXY targetLoc = ride->stations[CurrentRideStation].Start;

    assert(ride->type == RIDE_TYPE_SPIRAL_SLIDE);
    targetLoc += SpiralSlideWalkingPath[Var37];

    SetDestination(targetLoc);
    RideSubState = PeepRideSubState::ApproachSpiralSlide;
}

/**
 *
 *  rct2: 0x00692C6B
 */
void Guest::UpdateRideLeaveSpiralSlide()
{
    // Iterates through the spiral slide waypoints until it reaches
    // waypoint 0. Then it readies to leave the ride by the entrance.
    if (auto loc = UpdateAction())
    {
        MoveTo({ *loc, z });
        return;
    }

    auto ride = get_ride(CurrentRide);
    if (ride == nullptr)
        return;

    uint8_t waypoint = Var37 & 3;

    if (waypoint != 0)
    {
        if (waypoint == 3)
        {
            peep_update_ride_prepare_for_exit(this);
            return;
        }

        waypoint--;
        // Actually decrement the peep waypoint
        Var37--;
        CoordsXY targetLoc = ride->stations[CurrentRideStation].Start;

        assert(ride->type == RIDE_TYPE_SPIRAL_SLIDE);
        targetLoc += SpiralSlideWalkingPath[Var37];

        SetDestination(targetLoc);
        return;
    }
    waypoint = 3;
    // Actually force the final waypoint
    Var37 |= 3;

    auto targetLoc = ride_get_exit_location(ride, CurrentRideStation).ToCoordsXYZD().ToTileCentre();

    int16_t xShift = DirectionOffsets[direction_reverse(targetLoc.direction)].x;
    int16_t yShift = DirectionOffsets[direction_reverse(targetLoc.direction)].y;

    int16_t shiftMultiplier = 20;

    xShift *= shiftMultiplier;
    yShift *= shiftMultiplier;

    targetLoc.x -= xShift;
    targetLoc.y -= yShift;

    SetDestination(targetLoc);
}

/** rct2: 0x00981FE4 */
static constexpr const uint8_t _MazeGetNewDirectionFromEdge[][4] = {
    { 15, 7, 15, 7 },
    { 11, 3, 11, 3 },
    { 7, 15, 7, 15 },
    { 3, 11, 3, 11 },
};

/** rct2: 0x00981FF4 */
static constexpr const uint8_t _MazeCurrentDirectionToOpenHedge[][4] = {
    { 1, 2, 14, 0 },
    { 4, 5, 6, 2 },
    { 6, 8, 9, 10 },
    { 14, 10, 12, 13 },
};

/**
 *
 *  rct2: 0x00692A83
 */
void Guest::UpdateRideMazePathfinding()
{
    if (auto loc = UpdateAction())
    {
        MoveTo({ *loc, z });
        return;
    }

    auto ride = get_ride(CurrentRide);
    if (ride == nullptr)
        return;

    if (Var37 == 16)
    {
        peep_update_ride_prepare_for_exit(this);
        return;
    }

    if (Action >= PeepActionType::None1)
    {
        if (Energy > 64 && (scenario_rand() & 0xFFFF) <= 2427)
        {
            Action = PeepActionType::Jump;
            ActionFrame = 0;
            ActionSpriteImageOffset = 0;
            UpdateCurrentActionSpriteType();
        }
    }

    auto targetLoc = GetDestination().ToTileStart();

    int16_t stationBaseZ = ride->stations[0].GetBaseZ();

    // Find the station track element
    auto trackElement = map_get_track_element_at({ targetLoc, stationBaseZ });
    if (trackElement == nullptr)
    {
        return;
    }

    uint16_t mazeEntry = trackElement->GetMazeEntry();
    // Var37 is 3, 7, 11 or 15
    uint8_t hedges[4]{ 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t openCount = 0;
    uint8_t mazeReverseLastEdge = direction_reverse(MazeLastEdge);
    for (uint8_t i = 0; i < 4; ++i)
    {
        if (!(mazeEntry & (1 << _MazeCurrentDirectionToOpenHedge[Var37 / 4][i])) && i != mazeReverseLastEdge)
        {
            hedges[openCount++] = i;
        }
    }

    if (openCount == 0)
    {
        if ((mazeEntry & (1 << _MazeCurrentDirectionToOpenHedge[Var37 / 4][mazeReverseLastEdge])))
        {
            return;
        }
        hedges[openCount++] = mazeReverseLastEdge;
    }

    uint8_t chosenEdge = hedges[scenario_rand() % openCount];
    assert(chosenEdge != 0xFF);

    targetLoc = GetDestination() + CoordsDirectionDelta[chosenEdge] / 2;

    enum class maze_type
    {
        invalid,
        hedge,
        entrance_or_exit
    };
    maze_type mazeType = maze_type::invalid;

    auto tileElement = map_get_first_element_at(targetLoc);
    if (tileElement == nullptr)
        return;
    do
    {
        if (stationBaseZ != tileElement->GetBaseZ())
            continue;

        if (tileElement->GetType() == TILE_ELEMENT_TYPE_TRACK)
        {
            mazeType = maze_type::hedge;
            break;
        }

        if (tileElement->GetType() == TILE_ELEMENT_TYPE_ENTRANCE
            && tileElement->AsEntrance()->GetEntranceType() == ENTRANCE_TYPE_RIDE_EXIT)
        {
            mazeType = maze_type::entrance_or_exit;
            break;
        }
    } while (!(tileElement++)->IsLastForTile());

    switch (mazeType)
    {
        case maze_type::invalid:
            MazeLastEdge++;
            MazeLastEdge &= 3;
            return;
        case maze_type::hedge:
            SetDestination(targetLoc);
            Var37 = _MazeGetNewDirectionFromEdge[Var37 / 4][chosenEdge];
            MazeLastEdge = chosenEdge;
            break;
        case maze_type::entrance_or_exit:
            targetLoc = GetDestination();
            if (chosenEdge & 1)
            {
                targetLoc.x = targetLoc.ToTileCentre().x;
            }
            else
            {
                targetLoc.y = targetLoc.ToTileCentre().y;
            }
            SetDestination(targetLoc);
            Var37 = 16;
            MazeLastEdge = chosenEdge;
            break;
    }

    if (auto loc = UpdateAction())
    {
        MoveTo({ *loc, z });
        return;
    }
}

/**
 *
 *  rct2: 0x006938D2
 */
void Guest::UpdateRideLeaveExit()
{
    auto ride = get_ride(CurrentRide);

    if (auto loc = UpdateAction())
    {
        if (ride != nullptr)
        {
            MoveTo({ *loc, ride->stations[CurrentRideStation].GetBaseZ() });
        }
        return;
    }

    OnExitRide(CurrentRide);

    if (ride != nullptr && (PeepFlags & PEEP_FLAGS_TRACKING))
    {
        auto ft = Formatter();
        FormatNameTo(ft);
        ride->FormatNameTo(ft);

        if (gConfigNotifications.guest_left_ride)
        {
            News::AddItemToQueue(News::ItemType::PeepOnRide, STR_PEEP_TRACKING_LEFT_RIDE_X, sprite_index, ft);
        }
    }

    InteractionRideIndex = RIDE_ID_NULL;
    SetState(PeepState::Falling);

    CoordsXY targetLoc = { x, y };

    // Find the station track element
    for (auto* pathElement : TileElementsView<PathElement>(targetLoc))
    {
        int16_t height = map_height_from_slope(targetLoc, pathElement->GetSlopeDirection(), pathElement->IsSloped());
        height += pathElement->GetBaseZ();

        int16_t z_diff = z - height;
        if (z_diff > 0 || z_diff < -16)
            continue;

        MoveTo({ x, y, height });
        return;
    }
}

/**
 *
 *  rct2: 0x0069299C
 */
void Guest::UpdateRideShopApproach()
{
    if (auto loc = UpdateAction())
    {
        MoveTo({ *loc, z });
        return;
    }

    RideSubState = PeepRideSubState::InteractShop;
}

/**
 *
 *  rct2: 0x006929BB
 */
void Guest::UpdateRideShopInteract()
{
    auto ride = get_ride(CurrentRide);
    if (ride == nullptr)
        return;

    const int16_t tileCenterX = NextLoc.x + 16;
    const int16_t tileCenterY = NextLoc.y + 16;
    if (ride->type == RIDE_TYPE_FIRST_AID)
    {
        if (Nausea <= 35)
        {
            RideSubState = PeepRideSubState::LeaveShop;

            SetDestination({ tileCenterX, tileCenterY }, 3);
            HappinessTarget = std::min(HappinessTarget + 30, PEEP_MAX_HAPPINESS);
            Happiness = HappinessTarget;
        }
        else
        {
            Nausea--;
            NauseaTarget = Nausea;
        }
        return;
    }

    if (Toilet != 0)
    {
        Toilet--;
        return;
    }

    // Do not play toilet flush sound on title screen as it's considered loud and annoying
    if (!(gScreenFlags & SCREEN_FLAGS_TITLE_DEMO))
    {
        OpenRCT2::Audio::Play3D(OpenRCT2::Audio::SoundId::ToiletFlush, { x, y, z });
    }

    RideSubState = PeepRideSubState::LeaveShop;

    SetDestination({ tileCenterX, tileCenterY }, 3);

    HappinessTarget = std::min(HappinessTarget + 30, PEEP_MAX_HAPPINESS);
    Happiness = HappinessTarget;
    StopPurchaseThought(ride->type);
}

/**
 *
 *  rct2: 0x00692935
 */
void Guest::UpdateRideShopLeave()
{
    if (auto loc = UpdateAction())
    {
        MoveTo({ *loc, z });

        if ((x & 0xFFE0) != NextLoc.x)
            return;
        if ((y & 0xFFE0) != NextLoc.y)
            return;
    }

    //#11758 Previously SetState(PeepState::Walking) caused Peeps to double-back to exit point of shop
    SetState(PeepState::Falling);

    auto ride = get_ride(CurrentRide);
    if (ride != nullptr)
    {
        ride->total_customers++;
        ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_CUSTOMER;
        ride_update_satisfaction(ride, Happiness / 64);
    }
}

void Guest::UpdateGuest()
{
    switch (State)
    {
        case PeepState::QueuingFront:
            UpdateRide();
            break;
        case PeepState::LeavingRide:
            UpdateRide();
            break;
        case PeepState::Walking:
            UpdateWalking();
            break;
        case PeepState::Queuing:
            UpdateQueuing();
            break;
        case PeepState::EnteringRide:
            UpdateRide();
            break;
        case PeepState::Sitting:
            UpdateSitting();
            break;
        case PeepState::EnteringPark:
            UpdateEnteringPark();
            break;
        case PeepState::LeavingPark:
            UpdateLeavingPark();
            break;
        case PeepState::Buying:
            UpdateBuying();
            break;
        case PeepState::Watching:
            UpdateWatching();
            break;
        case PeepState::UsingBin:
            UpdateUsingBin();
            break;
        default:
            // TODO reset to default state
            assert(false);
            break;
    }
}

/**
 *
 *  rct2: 0x691A30
 * Used by entering_ride and queueing_front */
void Guest::UpdateRide()
{
    NextFlags &= ~PEEP_NEXT_FLAG_IS_SLOPED;

    switch (RideSubState)
    {
        case PeepRideSubState::AtEntrance:
            UpdateRideAtEntrance();
            break;
        case PeepRideSubState::InEntrance:
            UpdateRideAdvanceThroughEntrance();
            break;
        case PeepRideSubState::FreeVehicleCheck:
            UpdateRideFreeVehicleCheck();
            break;
        case PeepRideSubState::LeaveEntrance:
            UpdateRideAdvanceThroughEntrance();
            break;
        case PeepRideSubState::ApproachVehicle:
            UpdateRideApproachVehicle();
            break;
        case PeepRideSubState::EnterVehicle:
            UpdateRideEnterVehicle();
            break;
        case PeepRideSubState::OnRide:
            // No action, on ride.
            break;
        case PeepRideSubState::LeaveVehicle:
            UpdateRideLeaveVehicle();
            break;
        case PeepRideSubState::ApproachExit:
            UpdateRideApproachExit();
            break;
        case PeepRideSubState::InExit:
            UpdateRideInExit();
            break;
        case PeepRideSubState::ApproachVehicleWaypoints:
            UpdateRideApproachVehicleWaypoints();
            break;
        case PeepRideSubState::ApproachExitWaypoints:
            UpdateRideApproachExitWaypoints();
            break;
        case PeepRideSubState::ApproachSpiralSlide:
            UpdateRideApproachSpiralSlide();
            break;
        case PeepRideSubState::OnSpiralSlide:
            UpdateRideOnSpiralSlide();
            break;
        case PeepRideSubState::LeaveSpiralSlide:
            UpdateRideLeaveSpiralSlide();
            break;
        case PeepRideSubState::MazePathfinding:
            UpdateRideMazePathfinding();
            break;
        case PeepRideSubState::LeaveExit:
            UpdateRideLeaveExit();
            break;
        case PeepRideSubState::ApproachShop:
            UpdateRideShopApproach();
            break;
        case PeepRideSubState::InteractShop:
            UpdateRideShopInteract();
            break;
        case PeepRideSubState::LeaveShop:
            UpdateRideShopLeave();
            break;
        default:
            // Invalid peep sub-state
            assert(false);
            break;
    }
}

static void peep_update_walking_break_scenery(Peep* peep);
static bool peep_find_ride_to_look_at(Peep* peep, uint8_t edge, ride_id_t* rideToView, uint8_t* rideSeatToView);

/**
 *
 *  rct2: 0x0069030A
 */
void Guest::UpdateWalking()
{
    if (!CheckForPath())
        return;

    if (PeepFlags & PEEP_FLAGS_WAVING)
    {
        if (Action >= PeepActionType::None1)
        {
            if ((0xFFFF & scenario_rand()) < 936)
            {
                Action = PeepActionType::Wave2;
                ActionFrame = 0;
                ActionSpriteImageOffset = 0;

                UpdateCurrentActionSpriteType();
            }
        }
    }

    if (PeepFlags & PEEP_FLAGS_PHOTO)
    {
        if (Action >= PeepActionType::None1)
        {
            if ((0xFFFF & scenario_rand()) < 936)
            {
                Action = PeepActionType::TakePhoto;
                ActionFrame = 0;
                ActionSpriteImageOffset = 0;

                UpdateCurrentActionSpriteType();
            }
        }
    }

    if (PeepFlags & PEEP_FLAGS_PAINTING)
    {
        if (Action >= PeepActionType::None1)
        {
            if ((0xFFFF & scenario_rand()) < 936)
            {
                Action = PeepActionType::DrawPicture;
                ActionFrame = 0;
                ActionSpriteImageOffset = 0;

                UpdateCurrentActionSpriteType();
            }
        }
    }

    if (PeepFlags & PEEP_FLAGS_LITTER)
    {
        if (!GetNextIsSurface())
        {
            if ((0xFFFF & scenario_rand()) <= 4096)
            {
                static constexpr const LitterType litter_types[] = {
                    LITTER_TYPE_EMPTY_CAN,
                    LITTER_TYPE_RUBBISH,
                    LITTER_TYPE_EMPTY_BURGER_BOX,
                    LITTER_TYPE_EMPTY_CUP,
                };
                auto litterType = litter_types[scenario_rand() & 0x3];
                int32_t litterX = x + (scenario_rand() & 0x7) - 3;
                int32_t litterY = y + (scenario_rand() & 0x7) - 3;
                Direction litterDirection = (scenario_rand() & 0x3);

                litter_create({ litterX, litterY, z, litterDirection }, litterType);
            }
        }
    }
    else if (HasEmptyContainer())
    {
        if ((!GetNextIsSurface()) && (static_cast<uint32_t>(sprite_index & 0x1FF) == (gCurrentTicks & 0x1FF))
            && ((0xFFFF & scenario_rand()) <= 4096))
        {
            int32_t container = bitscanforward(GetEmptyContainerFlags());
            LitterType litterType = LITTER_TYPE_SICK;

            if (container != -1)
            {
                auto item = static_cast<ShopItem>(container);
                RemoveItem(item);
                litterType = LitterType(GetShopItemDescriptor(item).LitterType);
            }

            WindowInvalidateFlags |= PEEP_INVALIDATE_PEEP_INVENTORY;
            UpdateSpriteType();

            int32_t litterX = x + (scenario_rand() & 0x7) - 3;
            int32_t litterY = y + (scenario_rand() & 0x7) - 3;
            Direction litterDirection = (scenario_rand() & 0x3);

            litter_create({ litterX, litterY, z, litterDirection }, litterType);
        }
    }

    // Check if vehicle is blocking the destination tile
    auto curPos = TileCoordsXYZ(CoordsXYZ{ x, y, z });
    auto dstPos = TileCoordsXYZ(CoordsXYZ{ GetDestination(), NextLoc.z });
    if (curPos.x != dstPos.x || curPos.y != dstPos.y)
    {
        if (footpath_is_blocked_by_vehicle(dstPos))
        {
            // Wait for vehicle to pass
            return;
        }
    }

    uint8_t pathingResult;
    PerformNextAction(pathingResult);
    if (!(pathingResult & PATHING_DESTINATION_REACHED))
        return;

    if (GetNextIsSurface())
    {
        auto surfaceElement = map_get_surface_element_at(NextLoc);

        if (surfaceElement != nullptr)
        {
            int32_t water_height = surfaceElement->GetWaterHeight();
            if (water_height > 0)
            {
                MoveTo({ x, y, water_height });
                SetState(PeepState::Falling);
                return;
            }
        }
    }

    CheckIfLost();
    CheckCantFindRide();
    CheckCantFindExit();

    if (UpdateWalkingFindBench())
        return;

    if (UpdateWalkingFindBin())
        return;

    peep_update_walking_break_scenery(this);

    if (State != PeepState::Walking)
        return;

    if (PeepFlags & PEEP_FLAGS_LEAVING_PARK)
        return;

    if (Nausea > 140)
        return;

    if (Happiness < 120)
        return;

    if (Toilet > 140)
        return;

    uint16_t chance = HasFoodOrDrink() ? 13107 : 2849;

    if ((scenario_rand() & 0xFFFF) > chance)
        return;

    if (GetNextIsSurface() || GetNextIsSloped())
        return;

    TileElement* tileElement = map_get_first_element_at(NextLoc);
    if (tileElement == nullptr)
        return;

    for (;; tileElement++)
    {
        if (tileElement->GetType() == TILE_ELEMENT_TYPE_PATH)
        {
            if (NextLoc.z == tileElement->GetBaseZ())
                break;
        }
        if (tileElement->IsLastForTile())
        {
            return;
        }
    }

    int32_t positions_free = 15;

    if (tileElement->AsPath()->HasAddition())
    {
        if (!tileElement->AsPath()->AdditionIsGhost())
        {
            rct_scenery_entry* sceneryEntry = tileElement->AsPath()->GetAdditionEntry();
            if (sceneryEntry == nullptr)
            {
                return;
            }

            if (!(sceneryEntry->path_bit.flags & PATH_BIT_FLAG_IS_BENCH))
                positions_free = 9;
        }
    }

    int32_t edges = (tileElement->AsPath()->GetEdges()) ^ 0xF;
    if (edges == 0)
        return;

    uint8_t chosen_edge = scenario_rand() & 0x3;

    for (; !(edges & (1 << chosen_edge));)
        chosen_edge = (chosen_edge + 1) & 3;

    ride_id_t ride_to_view;
    uint8_t ride_seat_to_view;
    if (!peep_find_ride_to_look_at(this, chosen_edge, &ride_to_view, &ride_seat_to_view))
        return;

    // Check if there is a peep watching (and if there is place for us)
    for (auto peep : EntityTileList<Peep>({ x, y }))
    {
        if (peep->State != PeepState::Watching)
            continue;

        if (z != peep->z)
            continue;

        if ((peep->Var37 & 0x3) != chosen_edge)
            continue;

        positions_free &= ~(1 << ((peep->Var37 & 0x1C) >> 2));
    }

    if (!positions_free)
        return;

    uint8_t chosen_position = scenario_rand() & 0x3;

    for (; !(positions_free & (1 << chosen_position));)
        chosen_position = (chosen_position + 1) & 3;

    CurrentRide = ride_to_view;
    CurrentSeat = ride_seat_to_view;
    Var37 = chosen_edge | (chosen_position << 2);

    SetState(PeepState::Watching);
    SubState = 0;

    int32_t destX = (x & 0xFFE0) + _WatchingPositionOffsets[Var37 & 0x1F].x;
    int32_t destY = (y & 0xFFE0) + _WatchingPositionOffsets[Var37 & 0x1F].y;

    SetDestination({ destX, destY }, 3);

    if (CurrentSeat & 1)
    {
        InsertNewThought(PeepThoughtType::NewRide, PEEP_THOUGHT_ITEM_NONE);
    }
    if (CurrentRide == RIDE_ID_NULL)
    {
        InsertNewThought(PeepThoughtType::Scenery, PEEP_THOUGHT_ITEM_NONE);
    }
}

/**
 *
 *  rct2: 0x69185D
 */
void Guest::UpdateQueuing()
{
    if (!CheckForPath())
    {
        RemoveFromQueue();
        return;
    }
    auto ride = get_ride(CurrentRide);
    if (ride == nullptr || ride->status != RIDE_STATUS_OPEN)
    {
        RemoveFromQueue();
        SetState(PeepState::One);
        return;
    }

    // If not in the queue then at front of queue
    if (RideSubState != PeepRideSubState::InQueue)
    {
        bool is_front = true;
        // Fix #4819: Occasionally the peep->GuestNextInQueue is incorrectly set
        // to prevent this from causing the peeps to enter a loop
        // first check if the next in queue is actually nearby
        // if they are not then it's safe to assume that this is
        // the front of the queue.
        Peep* nextGuest = GetEntity<Guest>(GuestNextInQueue);
        if (nextGuest != nullptr)
        {
            if (abs(nextGuest->x - x) < 32 && abs(nextGuest->y - y) < 32)
            {
                is_front = false;
            }
        }

        if (is_front)
        {
            // Happens every time peep goes onto ride.
            DestinationTolerance = 0;
            SetState(PeepState::QueuingFront);
            RideSubState = PeepRideSubState::AtEntrance;
            return;
        }

        // Give up queueing for the ride
        sprite_direction ^= (1 << 4);
        Invalidate();
        RemoveFromQueue();
        SetState(PeepState::One);
        return;
    }

    uint8_t pathingResult;
    PerformNextAction(pathingResult);
    if (Action < PeepActionType::None1)
        return;
    if (SpriteType == PeepSpriteType::Normal)
    {
        if (TimeInQueue >= 2000 && (0xFFFF & scenario_rand()) <= 119)
        {
            // Eat Food/Look at watch
            Action = PeepActionType::EatFood;
            ActionFrame = 0;
            ActionSpriteImageOffset = 0;
            UpdateCurrentActionSpriteType();
        }
        if (TimeInQueue >= 3500 && (0xFFFF & scenario_rand()) <= 93)
        {
            // Create the I have been waiting in line ages thought
            InsertNewThought(PeepThoughtType::QueuingAges, CurrentRide);
        }
    }
    else
    {
        if (!(TimeInQueue & 0x3F) && Action == PeepActionType::None1 && NextActionSpriteType == PeepActionSpriteType::WatchRide)
        {
            switch (SpriteType)
            {
                case PeepSpriteType::IceCream:
                case PeepSpriteType::Chips:
                case PeepSpriteType::Burger:
                case PeepSpriteType::Drink:
                case PeepSpriteType::Candyfloss:
                case PeepSpriteType::Pizza:
                case PeepSpriteType::Popcorn:
                case PeepSpriteType::HotDog:
                case PeepSpriteType::Tentacle:
                case PeepSpriteType::ToffeeApple:
                case PeepSpriteType::Doughnut:
                case PeepSpriteType::Coffee:
                case PeepSpriteType::Chicken:
                case PeepSpriteType::Lemonade:
                case PeepSpriteType::Pretzel:
                case PeepSpriteType::SuJongkwa:
                case PeepSpriteType::Juice:
                case PeepSpriteType::FunnelCake:
                case PeepSpriteType::Noodles:
                case PeepSpriteType::Sausage:
                case PeepSpriteType::Soup:
                case PeepSpriteType::Sandwich:
                    // Eat food
                    Action = PeepActionType::EatFood;
                    ActionFrame = 0;
                    ActionSpriteImageOffset = 0;
                    UpdateCurrentActionSpriteType();
                    break;
                default:
                    break;
            }
        }
    }
    if (TimeInQueue < 4300)
        return;

    if (Happiness <= 65 && (0xFFFF & scenario_rand()) < 2184)
    {
        // Give up queueing for the ride
        sprite_direction ^= (1 << 4);
        Invalidate();
        RemoveFromQueue();
        SetState(PeepState::One);
    }
}

/**
 * rct2: 0x691451
 */
void Guest::UpdateEnteringPark()
{
    if (Var37 != 1)
    {
        uint8_t pathingResult;
        PerformNextAction(pathingResult);
        if ((pathingResult & PATHING_OUTSIDE_PARK))
        {
            decrement_guests_heading_for_park();
            peep_sprite_remove(this);
        }
        return;
    }
    if (auto loc = UpdateAction())
    {
        MoveTo({ *loc, z });
        return;
    }
    SetState(PeepState::Falling);

    OutsideOfPark = false;
    ParkEntryTime = gScenarioTicks;
    increment_guests_in_park();
    decrement_guests_heading_for_park();
    auto intent = Intent(INTENT_ACTION_UPDATE_GUEST_COUNT);
    context_broadcast_intent(&intent);
}

/**
 *
 *  rct2: 0x6914CD
 */
void Guest::UpdateLeavingPark()
{
    if (Var37 != 0)
    {
        uint8_t pathingResult;
        PerformNextAction(pathingResult);
        if (!(pathingResult & PATHING_OUTSIDE_PARK))
            return;
        peep_sprite_remove(this);
        return;
    }

    if (auto loc = UpdateAction())
    {
        MoveTo({ *loc, z });
        return;
    }

    OutsideOfPark = true;
    DestinationTolerance = 5;
    decrement_guests_in_park();
    auto intent = Intent(INTENT_ACTION_UPDATE_GUEST_COUNT);
    context_broadcast_intent(&intent);
    Var37 = 1;

    window_invalidate_by_class(WC_GUEST_LIST);
    uint8_t pathingResult;
    PerformNextAction(pathingResult);
    if (!(pathingResult & PATHING_OUTSIDE_PARK))
        return;
    Remove();
}

/**
 *
 *  rct2: 0x6916D6
 */
void Guest::UpdateWatching()
{
    if (SubState == 0)
    {
        if (!CheckForPath())
            return;
        uint8_t pathingResult;
        PerformNextAction(pathingResult);
        if (!(pathingResult & PATHING_DESTINATION_REACHED))
            return;

        SetDestination(GetLocation());

        sprite_direction = (Var37 & 3) * 8;

        Action = PeepActionType::None1;
        NextActionSpriteType = PeepActionSpriteType::WatchRide;

        SwitchNextActionSpriteType();

        SubState++;

        TimeToStand = std::clamp(((129 - Energy) * 16 + 50) / 2, 0, 255);
        UpdateSpriteType();
    }
    else if (SubState == 1)
    {
        if (Action < PeepActionType::None1)
        {
            // 6917F6
            UpdateAction();
            Invalidate();
            if (Action != PeepActionType::None2)
                return;
            Action = PeepActionType::None1;
        }
        else
        {
            if (HasFoodOrDrink())
            {
                if ((scenario_rand() & 0xFFFF) <= 1310)
                {
                    Action = PeepActionType::EatFood;
                    ActionFrame = 0;
                    ActionSpriteImageOffset = 0;
                    UpdateCurrentActionSpriteType();
                    return;
                }
            }

            if ((scenario_rand() & 0xFFFF) <= 655)
            {
                Action = PeepActionType::TakePhoto;
                ActionFrame = 0;
                ActionSpriteImageOffset = 0;
                UpdateCurrentActionSpriteType();
                return;
            }

            if ((StandingFlags & 1))
            {
                if ((scenario_rand() & 0xFFFF) <= 655)
                {
                    Action = PeepActionType::Wave;
                    ActionFrame = 0;
                    ActionSpriteImageOffset = 0;
                    UpdateCurrentActionSpriteType();
                    return;
                }
            }
        }

        StandingFlags ^= (1 << 7);
        if (!(StandingFlags & (1 << 7)))
            return;

        TimeToStand--;
        if (TimeToStand != 0)
            return;

        SetState(PeepState::Walking);
        UpdateSpriteType();
        // Send peep to the centre of current tile.

        auto destination = GetLocation().ToTileCentre();
        SetDestination(destination, 5);
        UpdateCurrentActionSpriteType();
    }
}

/**
 *
 *  rct2: 0x00691089
 */
void Guest::UpdateUsingBin()
{
    switch (UsingBinSubState)
    {
        case PeepUsingBinSubState::WalkingToBin:
        {
            if (!CheckForPath())
                return;

            uint8_t pathingResult;
            PerformNextAction(pathingResult);
            if (pathingResult & PATHING_DESTINATION_REACHED)
            {
                UsingBinSubState = PeepUsingBinSubState::GoingBack;
            }
            break;
        }
        case PeepUsingBinSubState::GoingBack:
        {
            if (Action != PeepActionType::None2)
            {
                UpdateAction();
                Invalidate();
                return;
            }

            PathElement* foundElement = nullptr;
            for (auto* pathElement : TileElementsView<PathElement>(NextLoc))
            {
                if (pathElement->GetBaseZ() != NextLoc.z)
                    continue;

                if (!pathElement->HasAddition())
                    break;

                rct_scenery_entry* sceneryEntry = pathElement->GetAdditionEntry();
                if (!(sceneryEntry->path_bit.flags & PATH_BIT_FLAG_IS_BIN))
                    break;

                if (pathElement->IsBroken())
                    break;

                if (pathElement->AdditionIsGhost())
                    break;

                foundElement = pathElement;
                break;
            }

            if (foundElement == nullptr)
            {
                StateReset();
                return;
            }

            // Bin selection is one of 4 corners
            uint8_t selectedBin = Var37 * 2;

            // This counts down 2 = No rubbish, 0 = full
            uint8_t spaceLeftInBin = 0x3 & (foundElement->GetAdditionStatus() >> selectedBin);
            uint64_t emptyContainers = GetEmptyContainerFlags();

            for (uint8_t curContainer = 0; curContainer < 64; curContainer++)
            {
                if (!(emptyContainers & (1ULL << curContainer)))
                    continue;

                auto item = ShopItem(curContainer);
                if (spaceLeftInBin != 0)
                {
                    // OpenRCT2 modification: This previously used
                    // the tick count as a simple random function
                    // switched to scenario_rand as it is more reliable
                    if ((scenario_rand() & 7) == 0)
                        spaceLeftInBin--;
                    RemoveItem(item);
                    WindowInvalidateFlags |= PEEP_INVALIDATE_PEEP_INVENTORY;
                    UpdateSpriteType();
                    continue;
                }

                LitterType litterType = LitterType(GetShopItemDescriptor(item).LitterType);

                int32_t litterX = x + (scenario_rand() & 7) - 3;
                int32_t litterY = y + (scenario_rand() & 7) - 3;

                litter_create({ litterX, litterY, z, static_cast<Direction>(scenario_rand() & 3) }, litterType);
                RemoveItem(item);
                WindowInvalidateFlags |= PEEP_INVALIDATE_PEEP_INVENTORY;

                UpdateSpriteType();
            }

            uint8_t additionStatus = foundElement->GetAdditionStatus();
            // Place new amount in bin by first clearing the value
            additionStatus &= ~(3 << selectedBin);
            // Then placing the new value.
            additionStatus |= spaceLeftInBin << selectedBin;
            foundElement->SetAdditionStatus(additionStatus);

            map_invalidate_tile_zoom0({ NextLoc, foundElement->GetBaseZ(), foundElement->GetClearanceZ() });
            StateReset();
            break;
        }
        default:
            Guard::Assert(false, "Invalid sub state");
            break;
    }
}

/* Simplifies 0x690582. Returns true if should find bench*/
bool Guest::ShouldFindBench()
{
    if (PeepFlags & PEEP_FLAGS_LEAVING_PARK)
    {
        return false;
    }

    if (HasFoodOrDrink())
    {
        if (Hunger < 128 || Happiness < 128)
        {
            if (!GetNextIsSurface() && !GetNextIsSloped())
            {
                return true;
            }
        }
    }

    if (Nausea <= 170 && Energy > 50)
    {
        return false;
    }

    return !GetNextIsSurface() && !GetNextIsSloped();
}

static PathElement* FindBench(const CoordsXYZ& loc)
{
    for (auto* pathElement : TileElementsView<PathElement>(loc))
    {
        if (pathElement->GetBaseZ() != loc.z)
            continue;

        if (!pathElement->HasAddition())
            continue;

        rct_scenery_entry* sceneryEntry = pathElement->GetAdditionEntry();
        if (sceneryEntry == nullptr || !(sceneryEntry->path_bit.flags & PATH_BIT_FLAG_IS_BENCH))
            continue;

        if (pathElement->IsBroken())
            continue;

        if (pathElement->AdditionIsGhost())
            continue;

        return pathElement;
    }

    return nullptr;
}

/**
 *
 *  rct2: 0x00690582
 * Returns true when the guest wants to sit down and has found a bench to sit on
 */
bool Guest::UpdateWalkingFindBench()
{
    if (!ShouldFindBench())
        return false;

    auto* pathElement = FindBench(NextLoc);
    if (pathElement == nullptr)
        return false;

    int32_t edges = pathElement->GetEdges() ^ 0xF;
    if (edges == 0)
        return false;
    uint8_t chosen_edge = scenario_rand() & 0x3;

    for (; !(edges & (1 << chosen_edge));)
        chosen_edge = (chosen_edge + 1) & 0x3;

    uint8_t free_edge = 3;

    // Check if there is no peep sitting in chosen_edge
    for (auto peep : EntityTileList<Peep>({ x, y }))
    {
        if (peep->State != PeepState::Sitting)
            continue;

        if (z != peep->z)
            continue;

        if ((peep->Var37 & 0x3) != chosen_edge)
            continue;

        free_edge &= ~(1 << ((peep->Var37 & 0x4) >> 2));
    }

    if (!free_edge)
        return false;

    free_edge ^= 0x3;
    if (!free_edge)
    {
        if (scenario_rand() & 0x8000000)
            free_edge = 1;
    }

    Var37 = ((free_edge & 1) << 2) | chosen_edge;

    SetState(PeepState::Sitting);

    SittingSubState = PeepSittingSubState::TryingToSit;

    int32_t benchX = (x & 0xFFE0) + BenchUseOffsets[Var37 & 0x7].x;
    int32_t benchY = (y & 0xFFE0) + BenchUseOffsets[Var37 & 0x7].y;

    SetDestination({ benchX, benchY }, 3);

    return true;
}

static PathElement* FindBin(const CoordsXYZ& loc)
{
    for (auto* pathElement : TileElementsView<PathElement>(loc))
    {
        if (pathElement->GetBaseZ() != loc.z)
            continue;

        if (!pathElement->HasAddition())
            continue;

        rct_scenery_entry* sceneryEntry = pathElement->GetAdditionEntry();
        if (sceneryEntry == nullptr || !(sceneryEntry->path_bit.flags & PATH_BIT_FLAG_IS_BIN))
            continue;

        if (pathElement->IsBroken())
            continue;

        if (pathElement->AdditionIsGhost())
            continue;

        return pathElement;
    }

    return nullptr;
}

bool Guest::UpdateWalkingFindBin()
{
    auto peep = this;
    if (!peep->HasEmptyContainer())
        return false;

    if (peep->GetNextIsSurface())
        return false;

    auto* pathElement = FindBin(peep->NextLoc);
    if (pathElement == nullptr)
        return false;

    int32_t edges = (pathElement->GetEdges()) ^ 0xF;
    if (edges == 0)
        return false;

    uint8_t chosen_edge = scenario_rand() & 0x3;

    // Note: Bin quantity is inverted 0 = full, 3 = empty
    uint8_t bin_quantities = pathElement->GetAdditionStatus();

    // Rotate the bin to the correct edge. Makes it easier for next calc.
    bin_quantities = ror8(ror8(bin_quantities, chosen_edge), chosen_edge);

    for (uint8_t free_edge = 4; free_edge != 0; free_edge--)
    {
        // If not full
        if (bin_quantities & 0x3)
        {
            if (edges & (1 << chosen_edge))
                break;
        }
        chosen_edge = (chosen_edge + 1) & 0x3;
        bin_quantities = ror8(bin_quantities, 2);
        if ((free_edge - 1) == 0)
            return 0;
    }

    peep->Var37 = chosen_edge;

    peep->SetState(PeepState::UsingBin);
    peep->UsingBinSubState = PeepUsingBinSubState::WalkingToBin;

    int32_t binX = (peep->x & 0xFFE0) + BinUseOffsets[peep->Var37 & 0x3].x;
    int32_t binY = (peep->y & 0xFFE0) + BinUseOffsets[peep->Var37 & 0x3].y;

    peep->SetDestination({ binX, binY }, 3);

    return true;
}

static PathElement* FindBreakableElement(const CoordsXYZ& loc)
{
    for (auto* pathElement : TileElementsView<PathElement>(loc))
    {
        if (pathElement->GetBaseZ() != loc.z)
            continue;

        if (!pathElement->HasAddition())
            continue;

        rct_scenery_entry* sceneryEntry = pathElement->GetAdditionEntry();
        if (sceneryEntry == nullptr || !(sceneryEntry->path_bit.flags & PATH_BIT_FLAG_BREAKABLE))
            continue;

        if (pathElement->IsBroken())
            continue;

        if (pathElement->AdditionIsGhost())
            continue;

        return pathElement;
    }

    return nullptr;
}

/**
 *
 *  rct2: 0x00690848
 */
static void peep_update_walking_break_scenery(Peep* peep)
{
    if (gCheatsDisableVandalism)
        return;

    if (!(peep->PeepFlags & PEEP_FLAGS_ANGRY))
    {
        if (peep->Happiness >= 48)
            return;
        if (peep->Energy < 85)
            return;
        if (peep->State != PeepState::Walking)
            return;

        if ((peep->LitterCount & 0xC0) != 0xC0 && (peep->DisgustingCount & 0xC0) != 0xC0)
            return;

        if ((scenario_rand() & 0xFFFF) > 3276)
            return;
    }

    if (peep->GetNextIsSurface())
        return;

    auto* tileElement = FindBreakableElement(peep->NextLoc);
    if (tileElement == nullptr)
        return;

    int32_t edges = tileElement->GetEdges();
    if (edges == 0xF)
        return;

    // Check if a peep is already sitting on the bench. If so, do not vandalise it.
    for (auto peep2 : EntityTileList<Peep>({ peep->x, peep->y }))
    {
        if ((peep2->State != PeepState::Sitting) || (peep->z != peep2->z))
        {
            continue;
        }

        return;
    }

    for (auto inner_peep : EntityList<Staff>())
    {
        if (inner_peep->AssignedStaffType != StaffType::Security)
            continue;

        if (inner_peep->x == LOCATION_NULL)
            continue;

        int32_t x_diff = abs(inner_peep->x - peep->x);
        int32_t y_diff = abs(inner_peep->y - peep->y);

        if (std::max(x_diff, y_diff) < 224)
        {
            inner_peep->StaffVandalsStopped++;
            return;
        }
    }

    tileElement->SetIsBroken(true);

    map_invalidate_tile_zoom1({ peep->NextLoc, tileElement->GetBaseZ(), tileElement->GetBaseZ() + 32 });

    peep->Angriness = 16;
}

/**
 * rct2: 0x0069101A
 *
 * @return (CF)
 */
static bool peep_should_watch_ride(TileElement* tileElement)
{
    // Ghosts are purely this-client-side and should not cause any interaction,
    // as that may lead to a desync.
    if (network_get_mode() != NETWORK_MODE_NONE)
    {
        if (tileElement->IsGhost())
            return false;
    }

    auto ride = get_ride(tileElement->AsTrack()->GetRideIndex());
    if (ride == nullptr || !ride->IsRide())
    {
        return false;
    }

    // This is most likely to have peeps watch new rides
    if (ride->excitement == RIDE_RATING_UNDEFINED)
    {
        return true;
    }

    if (ride->excitement >= RIDE_RATING(4, 70))
    {
        return true;
    }

    if (ride->intensity >= RIDE_RATING(4, 50))
    {
        return true;
    }

    if (ride->GetRideTypeDescriptor().HasFlag(RIDE_TYPE_FLAG_INTERESTING_TO_LOOK_AT))
    {
        if ((scenario_rand() & 0xFFFF) > 0x3333)
        {
            return false;
        }
    }
    else if (ride->GetRideTypeDescriptor().HasFlag(RIDE_TYPE_FLAG_SLIGHTLY_INTERESTING_TO_LOOK_AT))
    {
        if ((scenario_rand() & 0xFFFF) > 0x1000)
        {
            return false;
        }
    }
    else
    {
        return false;
    }

    return true;
}

bool loc_690FD0(Peep* peep, ride_id_t* rideToView, uint8_t* rideSeatToView, TileElement* tileElement)
{
    auto ride = get_ride(tileElement->AsTrack()->GetRideIndex());
    if (ride == nullptr)
        return false;

    *rideToView = ride->id;
    if (ride->excitement == RIDE_RATING_UNDEFINED)
    {
        *rideSeatToView = 1;
        if (ride->status != RIDE_STATUS_OPEN)
        {
            if (tileElement->GetClearanceZ() > peep->NextLoc.z + (8 * COORDS_Z_STEP))
            {
                *rideSeatToView |= (1 << 1);
            }

            return true;
        }
    }
    else
    {
        *rideSeatToView = 0;
        if (ride->status == RIDE_STATUS_OPEN && !(ride->lifecycle_flags & RIDE_LIFECYCLE_BROKEN_DOWN))
        {
            if (tileElement->GetClearanceZ() > peep->NextLoc.z + (8 * COORDS_Z_STEP))
            {
                *rideSeatToView = 0x02;
            }

            return true;
        }
    }

    return false;
}

/**
 *
 *  rct2: 0x00690B99
 *
 * @param edge (eax)
 * @param peep (esi)
 * @param[out] rideToView (cl)
 * @param[out] rideSeatToView (ch)
 * @return !CF
 */
static bool peep_find_ride_to_look_at(Peep* peep, uint8_t edge, ride_id_t* rideToView, uint8_t* rideSeatToView)
{
    TileElement* tileElement;

    auto surfaceElement = map_get_surface_element_at(peep->NextLoc);

    tileElement = reinterpret_cast<TileElement*>(surfaceElement);
    if (tileElement == nullptr)
    {
        return false;
    }

    do
    {
        // Ghosts are purely this-client-side and should not cause any interaction,
        // as that may lead to a desync.
        if (network_get_mode() != NETWORK_MODE_NONE)
        {
            if (tileElement->IsGhost())
                continue;
        }
        if (tileElement->GetType() != TILE_ELEMENT_TYPE_WALL)
            continue;
        if (tileElement->GetDirection() != edge)
            continue;
        auto wallEntry = tileElement->AsWall()->GetEntry();
        if (wallEntry == nullptr || (wallEntry->wall.flags2 & WALL_SCENERY_2_IS_OPAQUE))
            continue;
        if (peep->NextLoc.z + (4 * COORDS_Z_STEP) <= tileElement->GetBaseZ())
            continue;
        if (peep->NextLoc.z + (1 * COORDS_Z_STEP) >= tileElement->GetClearanceZ())
            continue;

        return false;
    } while (!(tileElement++)->IsLastForTile());

    uint16_t x = peep->NextLoc.x + CoordsDirectionDelta[edge].x;
    uint16_t y = peep->NextLoc.y + CoordsDirectionDelta[edge].y;
    if (!map_is_location_valid({ x, y }))
    {
        return false;
    }

    surfaceElement = map_get_surface_element_at(CoordsXY{ x, y });

    tileElement = reinterpret_cast<TileElement*>(surfaceElement);
    if (tileElement == nullptr)
    {
        return false;
    }

    do
    {
        // Ghosts are purely this-client-side and should not cause any interaction,
        // as that may lead to a desync.
        if (network_get_mode() != NETWORK_MODE_NONE)
        {
            if (tileElement->IsGhost())
                continue;
        }
        if (tileElement->GetType() != TILE_ELEMENT_TYPE_WALL)
            continue;
        if (direction_reverse(tileElement->GetDirection()) != edge)
            continue;
        auto wallEntry = tileElement->AsWall()->GetEntry();
        if (wallEntry == nullptr || (wallEntry->wall.flags2 & WALL_SCENERY_2_IS_OPAQUE))
            continue;
        // TODO: Check whether this shouldn't be <=, as the other loops use. If so, also extract as loop A.
        if (peep->NextLoc.z + (4 * COORDS_Z_STEP) >= tileElement->GetBaseZ())
            continue;
        if (peep->NextLoc.z + (1 * COORDS_Z_STEP) >= tileElement->GetClearanceZ())
            continue;

        return false;
    } while (!(tileElement++)->IsLastForTile());

    // TODO: Extract loop B
    tileElement = reinterpret_cast<TileElement*>(surfaceElement);
    do
    {
        // Ghosts are purely this-client-side and should not cause any interaction,
        // as that may lead to a desync.
        if (network_get_mode() != NETWORK_MODE_NONE)
        {
            if (tileElement->IsGhost())
                continue;
        }

        if (tileElement->GetClearanceZ() + (1 * COORDS_Z_STEP) < peep->NextLoc.z)
            continue;
        if (peep->NextLoc.z + (6 * COORDS_Z_STEP) < tileElement->GetBaseZ())
            continue;

        if (tileElement->GetType() == TILE_ELEMENT_TYPE_TRACK)
        {
            if (peep_should_watch_ride(tileElement))
            {
                return loc_690FD0(peep, rideToView, rideSeatToView, tileElement);
            }
        }

        if (tileElement->GetType() == TILE_ELEMENT_TYPE_LARGE_SCENERY)
        {
            const auto* sceneryEntry = tileElement->AsLargeScenery()->GetEntry();
            if (sceneryEntry == nullptr || !(sceneryEntry->large_scenery.flags & LARGE_SCENERY_FLAG_PHOTOGENIC))
            {
                continue;
            }

            *rideSeatToView = 0;
            if (tileElement->GetClearanceZ() >= peep->NextLoc.z + (8 * COORDS_Z_STEP))
            {
                *rideSeatToView = 0x02;
            }

            *rideToView = RIDE_ID_NULL;

            return true;
        }
    } while (!(tileElement++)->IsLastForTile());

    // TODO: Extract loop C
    tileElement = reinterpret_cast<TileElement*>(surfaceElement);
    do
    {
        // Ghosts are purely this-client-side and should not cause any interaction,
        // as that may lead to a desync.
        if (network_get_mode() != NETWORK_MODE_NONE)
        {
            if (tileElement->IsGhost())
                continue;
        }
        if (tileElement->GetClearanceZ() + (1 * COORDS_Z_STEP) < peep->NextLoc.z)
            continue;
        if (peep->NextLoc.z + (6 * COORDS_Z_STEP) < tileElement->GetBaseZ())
            continue;
        if (tileElement->GetType() == TILE_ELEMENT_TYPE_SURFACE)
            continue;
        if (tileElement->GetType() == TILE_ELEMENT_TYPE_PATH)
            continue;

        if (tileElement->GetType() == TILE_ELEMENT_TYPE_WALL)
        {
            auto wallEntry = tileElement->AsWall()->GetEntry();
            if (wallEntry == nullptr || (wallEntry->wall.flags2 & WALL_SCENERY_2_IS_OPAQUE))
            {
                continue;
            }
        }

        return false;
    } while (!(tileElement++)->IsLastForTile());

    x += CoordsDirectionDelta[edge].x;
    y += CoordsDirectionDelta[edge].y;
    if (!map_is_location_valid({ x, y }))
    {
        return false;
    }

    surfaceElement = map_get_surface_element_at(CoordsXY{ x, y });

    // TODO: extract loop A
    tileElement = reinterpret_cast<TileElement*>(surfaceElement);

    if (tileElement == nullptr)
    {
        return false;
    }

    do
    {
        // Ghosts are purely this-client-side and should not cause any interaction,
        // as that may lead to a desync.
        if (network_get_mode() != NETWORK_MODE_NONE)
        {
            if (tileElement->IsGhost())
                continue;
        }
        if (tileElement->GetType() != TILE_ELEMENT_TYPE_WALL)
            continue;
        if (direction_reverse(tileElement->GetDirection()) != edge)
            continue;
        auto wallEntry = tileElement->AsWall()->GetEntry();
        if (wallEntry == nullptr || (wallEntry->wall.flags2 & WALL_SCENERY_2_IS_OPAQUE))
            continue;
        if (peep->NextLoc.z + (6 * COORDS_Z_STEP) <= tileElement->GetBaseZ())
            continue;
        if (peep->NextLoc.z >= tileElement->GetClearanceZ())
            continue;

        return false;
    } while (!(tileElement++)->IsLastForTile());

    // TODO: Extract loop B
    tileElement = reinterpret_cast<TileElement*>(surfaceElement);
    do
    {
        // Ghosts are purely this-client-side and should not cause any interaction,
        // as that may lead to a desync.
        if (network_get_mode() != NETWORK_MODE_NONE)
        {
            if (tileElement->IsGhost())
                continue;
        }
        if (tileElement->GetClearanceZ() + (1 * COORDS_Z_STEP) < peep->NextLoc.z)
            continue;
        if (peep->NextLoc.z + (8 * COORDS_Z_STEP) < tileElement->GetBaseZ())
            continue;

        if (tileElement->GetType() == TILE_ELEMENT_TYPE_TRACK)
        {
            if (peep_should_watch_ride(tileElement))
            {
                return loc_690FD0(peep, rideToView, rideSeatToView, tileElement);
            }
        }

        if (tileElement->GetType() == TILE_ELEMENT_TYPE_LARGE_SCENERY)
        {
            auto sceneryEntry = tileElement->AsLargeScenery()->GetEntry();
            if (!(sceneryEntry == nullptr || sceneryEntry->large_scenery.flags & LARGE_SCENERY_FLAG_PHOTOGENIC))
            {
                continue;
            }

            *rideSeatToView = 0;
            if (tileElement->GetClearanceZ() >= peep->NextLoc.z + (8 * COORDS_Z_STEP))
            {
                *rideSeatToView = 0x02;
            }

            *rideToView = RIDE_ID_NULL;

            return true;
        }
    } while (!(tileElement++)->IsLastForTile());

    // TODO: Extract loop C
    tileElement = reinterpret_cast<TileElement*>(surfaceElement);
    do
    {
        // Ghosts are purely this-client-side and should not cause any interaction,
        // as that may lead to a desync.
        if (network_get_mode() != NETWORK_MODE_NONE)
        {
            if (tileElement->IsGhost())
                continue;
        }
        if (tileElement->GetClearanceZ() + (1 * COORDS_Z_STEP) < peep->NextLoc.z)
            continue;
        if (peep->NextLoc.z + (8 * COORDS_Z_STEP) < tileElement->GetBaseZ())
            continue;
        if (tileElement->GetType() == TILE_ELEMENT_TYPE_SURFACE)
            continue;
        if (tileElement->GetType() == TILE_ELEMENT_TYPE_PATH)
            continue;

        if (tileElement->GetType() == TILE_ELEMENT_TYPE_WALL)
        {
            auto wallEntry = tileElement->AsWall()->GetEntry();
            if (wallEntry == nullptr || (wallEntry->wall.flags2 & WALL_SCENERY_2_IS_OPAQUE))
            {
                continue;
            }
        }

        return false;
    } while (!(tileElement++)->IsLastForTile());

    x += CoordsDirectionDelta[edge].x;
    y += CoordsDirectionDelta[edge].y;
    if (!map_is_location_valid({ x, y }))
    {
        return false;
    }

    surfaceElement = map_get_surface_element_at(CoordsXY{ x, y });

    // TODO: extract loop A
    tileElement = reinterpret_cast<TileElement*>(surfaceElement);
    if (tileElement == nullptr)
    {
        return false;
    }

    do
    {
        // Ghosts are purely this-client-side and should not cause any interaction,
        // as that may lead to a desync.
        if (network_get_mode() != NETWORK_MODE_NONE)
        {
            if (tileElement->IsGhost())
                continue;
        }
        if (tileElement->GetType() != TILE_ELEMENT_TYPE_WALL)
            continue;
        if (direction_reverse(tileElement->GetDirection()) != edge)
            continue;
        auto wallEntry = tileElement->AsWall()->GetEntry();
        if (wallEntry == nullptr || (wallEntry->wall.flags2 & WALL_SCENERY_2_IS_OPAQUE))
            continue;
        if (peep->NextLoc.z + (8 * COORDS_Z_STEP) <= tileElement->GetBaseZ())
            continue;
        if (peep->NextLoc.z >= tileElement->GetClearanceZ())
            continue;

        return false;
    } while (!(tileElement++)->IsLastForTile());

    // TODO: Extract loop B
    tileElement = reinterpret_cast<TileElement*>(surfaceElement);
    do
    {
        // Ghosts are purely this-client-side and should not cause any interaction,
        // as that may lead to a desync.
        if (network_get_mode() != NETWORK_MODE_NONE)
        {
            if (tileElement->IsGhost())
                continue;
        }
        if (tileElement->GetClearanceZ() + (1 * COORDS_Z_STEP) < peep->NextLoc.z)
            continue;
        if (peep->NextLoc.z + (10 * COORDS_Z_STEP) < tileElement->GetBaseZ())
            continue;

        if (tileElement->GetType() == TILE_ELEMENT_TYPE_TRACK)
        {
            if (peep_should_watch_ride(tileElement))
            {
                return loc_690FD0(peep, rideToView, rideSeatToView, tileElement);
            }
        }

        if (tileElement->GetType() == TILE_ELEMENT_TYPE_LARGE_SCENERY)
        {
            const auto* entry = tileElement->AsLargeScenery()->GetEntry();
            if (entry == nullptr || !(entry->large_scenery.flags & LARGE_SCENERY_FLAG_PHOTOGENIC))
            {
                continue;
            }

            *rideSeatToView = 0;
            if (tileElement->GetClearanceZ() >= peep->NextLoc.z + (8 * COORDS_Z_STEP))
            {
                *rideSeatToView = 0x02;
            }

            *rideToView = RIDE_ID_NULL;

            return true;
        }
    } while (!(tileElement++)->IsLastForTile());

    return false;
}

/* Part of 0x0069B8CC rct2: 0x0069BC31 */
void Guest::SetSpriteType(PeepSpriteType new_sprite_type)
{
    if (SpriteType == new_sprite_type)
        return;

    SpriteType = new_sprite_type;
    ActionSpriteImageOffset = 0;
    WalkingFrameNum = 0;

    if (Action >= PeepActionType::None1)
        Action = PeepActionType::None2;

    PeepFlags &= ~PEEP_FLAGS_SLOW_WALK;
    Guard::Assert(EnumValue(new_sprite_type) < std::size(gSpriteTypeToSlowWalkMap));
    if (gSpriteTypeToSlowWalkMap[EnumValue(new_sprite_type)])
    {
        PeepFlags |= PEEP_FLAGS_SLOW_WALK;
    }

    ActionSpriteType = PeepActionSpriteType::Invalid;
    UpdateCurrentActionSpriteType();

    if (State == PeepState::Sitting)
    {
        Action = PeepActionType::None1;
        NextActionSpriteType = PeepActionSpriteType::SittingIdle;
        SwitchNextActionSpriteType();
    }
    if (State == PeepState::Watching)
    {
        Action = PeepActionType::None1;
        NextActionSpriteType = PeepActionSpriteType::WatchRide;
        SwitchNextActionSpriteType();
    }
}

struct item_pref_t
{
    ShopItem item;
    PeepSpriteType sprite_type;
};

// clang-format off
static item_pref_t item_order_preference[] = {
    { ShopItem::IceCream,         PeepSpriteType::IceCream    },
    { ShopItem::Chips,            PeepSpriteType::Chips       },
    { ShopItem::Pizza,            PeepSpriteType::Pizza       },
    { ShopItem::Burger,           PeepSpriteType::Burger      },
    { ShopItem::Drink,            PeepSpriteType::Drink       },
    { ShopItem::Coffee,           PeepSpriteType::Coffee      },
    { ShopItem::Chicken,          PeepSpriteType::Chicken     },
    { ShopItem::Lemonade,         PeepSpriteType::Lemonade    },
    { ShopItem::Candyfloss,       PeepSpriteType::Candyfloss  },
    { ShopItem::Popcorn,          PeepSpriteType::Popcorn     },
    { ShopItem::HotDog,           PeepSpriteType::HotDog      },
    { ShopItem::Tentacle,         PeepSpriteType::Tentacle    },
    { ShopItem::ToffeeApple,      PeepSpriteType::ToffeeApple },
    { ShopItem::Doughnut,         PeepSpriteType::Doughnut    },
    { ShopItem::Pretzel,          PeepSpriteType::Pretzel     },
    { ShopItem::Cookie,           PeepSpriteType::Pretzel     },
    { ShopItem::Chocolate,        PeepSpriteType::Coffee      },
    { ShopItem::IcedTea,          PeepSpriteType::Coffee      },
    { ShopItem::FunnelCake,       PeepSpriteType::FunnelCake  },
    { ShopItem::BeefNoodles,      PeepSpriteType::Noodles     },
    { ShopItem::FriedRiceNoodles, PeepSpriteType::Noodles     },
    { ShopItem::WontonSoup,       PeepSpriteType::Soup        },
    { ShopItem::MeatballSoup,     PeepSpriteType::Soup        },
    { ShopItem::FruitJuice,       PeepSpriteType::Juice       },
    { ShopItem::SoybeanMilk,      PeepSpriteType::SuJongkwa   },
    { ShopItem::Sujeonggwa,       PeepSpriteType::SuJongkwa   },
    { ShopItem::SubSandwich,      PeepSpriteType::Sandwich    },
    { ShopItem::RoastSausage,     PeepSpriteType::Sausage     },
    { ShopItem::Balloon,          PeepSpriteType::Balloon     },
    { ShopItem::Hat,              PeepSpriteType::Hat         },
    { ShopItem::Sunglasses,       PeepSpriteType::Sunglasses  },
};
// clang-format on

/**
 *
 *  rct2: 0x0069B8CC
 */
void Guest::UpdateSpriteType()
{
    if (SpriteType == PeepSpriteType::Balloon && (scenario_rand() & 0xFFFF) <= 327)
    {
        bool isBalloonPopped = false;
        if (x != LOCATION_NULL)
        {
            if ((scenario_rand() & 0xFFFF) <= 13107)
            {
                isBalloonPopped = true;
                OpenRCT2::Audio::Play3D(OpenRCT2::Audio::SoundId::BalloonPop, { x, y, z });
            }
            create_balloon({ x, y, z + 9 }, BalloonColour, isBalloonPopped);
        }
        RemoveItem(ShopItem::Balloon);
        WindowInvalidateFlags |= PEEP_INVALIDATE_PEEP_INVENTORY;
    }

    if (climate_is_raining() && (HasItem(ShopItem::Umbrella)) && x != LOCATION_NULL)
    {
        CoordsXY loc = { x, y };
        if (map_is_location_valid(loc.ToTileStart()))
        {
            TileElement* tileElement = map_get_first_element_at(loc);
            while (true)
            {
                if (tileElement == nullptr)
                    break;
                if (z < tileElement->GetBaseZ())
                    break;

                if (tileElement->IsLastForTile())
                {
                    SetSpriteType(PeepSpriteType::Umbrella);
                    return;
                }
                tileElement++;
            }
        }
    }

    for (auto& itemPref : item_order_preference)
    {
        if (HasItem(itemPref.item))
        {
            SetSpriteType(itemPref.sprite_type);
            return;
        }
    }

    if (State == PeepState::Watching && StandingFlags & (1 << 1))
    {
        SetSpriteType(PeepSpriteType::Watching);
        return;
    }

    if (Nausea > 170)
    {
        SetSpriteType(PeepSpriteType::VeryNauseous);
        return;
    }

    if (Nausea > 140)
    {
        SetSpriteType(PeepSpriteType::Nauseous);
        return;
    }

    if (Energy <= 64 && Happiness < 128)
    {
        SetSpriteType(PeepSpriteType::HeadDown);
        return;
    }

    if (Energy <= 80 && Happiness < 128)
    {
        SetSpriteType(PeepSpriteType::ArmsCrossed);
        return;
    }

    if (Toilet > 220)
    {
        SetSpriteType(PeepSpriteType::RequireToilet);
        return;
    }

    SetSpriteType(PeepSpriteType::Normal);
}

bool Guest::HeadingForRideOrParkExit() const
{
    return (PeepFlags & PEEP_FLAGS_LEAVING_PARK) || (GuestHeadingToRideId != RIDE_ID_NULL);
}
