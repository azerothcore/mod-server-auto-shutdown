/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "GameEventMgr.h"
#include "ServerAutoShutdown.h"
#include "StringConvert.h"
#include "TaskScheduler.h"
#include "Tokenize.h"
#include "World.h"
#include "WorldSessionMgr.h"

namespace
{
    // Scheduler - for update
    TaskScheduler scheduler;

    time_t GetNextShutdownTime(time_t timestamp, uint8 weekdayMask, uint32 restartDays, uint8 restartHour, uint8 restartMinute, uint8 restartSecond)
    {
        tm timeLocal = Acore::Time::TimeBreakdown(timestamp);
        timeLocal.tm_hour = restartHour;
        timeLocal.tm_min = restartMinute;
        timeLocal.tm_sec = restartSecond;

        if (weekdayMask != 0)
        {
            for (int weekdayIndex = 0; weekdayIndex < 7; ++weekdayIndex)  //Sunday=0 Monday=1 ... Saturday=6
            {
                if (weekdayMask & (1 << ((timeLocal.tm_wday + weekdayIndex) % 7)))  // Check if the target day of the week (current day + weekdayIndex) is match in the weekday mask
                {
                    timeLocal.tm_mday += weekdayIndex;	//max hit times is 2 ,if 2, first time weekdayIndex must 0, so mktime will both right wherenever hit 1 or 2 times
                    time_t ShutdownMaskTime = mktime(&timeLocal);
                    if (ShutdownMaskTime - 10 > timestamp)
                        return ShutdownMaskTime;
                }
            }
            timeLocal.tm_mday += 7; // if no match, set the day of next week
            return mktime(&timeLocal);
        }

        time_t ShutdownDaysTime = mktime(&timeLocal);
        if (restartDays > 1 || ShutdownDaysTime - 10 <= timestamp)
            ShutdownDaysTime += DAY * restartDays;
        return ShutdownDaysTime;
    }
}

/*static*/ ServerAutoShutdown* ServerAutoShutdown::instance()
{
    static ServerAutoShutdown instance;
    return &instance;
}

void ServerAutoShutdown::Init()
{
    _isEnableModule = sConfigMgr->GetOption<bool>("ServerAutoShutdown.Enabled", false);

    if (!_isEnableModule)
        return;

    uint8 weekdayMask = sConfigMgr->GetOption<uint8>("ServerAutoShutdown.WeekdayMask", 0);
    uint32 restartDays = sConfigMgr->GetOption<uint32>("ServerAutoShutdown.EveryDays", 1);
    std::string configTime = sConfigMgr->GetOption<std::string>("ServerAutoShutdown.Time", "04:00:00");
    uint32 preAnnounceSeconds = sConfigMgr->GetOption<uint32>("ServerAutoShutdown.PreAnnounce.Seconds", HOUR);

    auto const& tokens = Acore::Tokenize(configTime, ':', false);
    if (tokens.size() != 3)
    {
        LOG_ERROR("module", "> ServerAutoShutdown: Incorrect time in config option 'ServerAutoShutdown.Time' - '{}'", configTime);
        _isEnableModule = false;
        return;
    }

    // Check convert to int
    auto CheckTime = [tokens](std::initializer_list<uint8> index)
    {
        for (auto const& itr : index)
            if (!Acore::StringTo<uint8>(tokens.at(itr)))
                return false;
        return true;
    };

    if (!CheckTime({ 0, 1, 2 }))
    {
        LOG_ERROR("module", "> ServerAutoShutdown: Incorrect time in config option 'ServerAutoShutdown.Time' - '{}'", configTime);
        _isEnableModule = false;
        return;
    }

    uint8 restartHour = *Acore::StringTo<uint8>(tokens.at(0));
    uint8 restartMinute = *Acore::StringTo<uint8>(tokens.at(1));
    uint8 restartSecond = *Acore::StringTo<uint8>(tokens.at(2));

    if (weekdayMask > 127)
    {
        LOG_ERROR("module", "> ServerAutoShutdown: Incorrect weekdayMask in config option 'ServerAutoShutdown.weekdayMask' - '{}'", weekdayMask);
        _isEnableModule = false;
        return;
    }

    if (restartDays < 1 || restartDays > 365)
    {
        LOG_ERROR("module", "> ServerAutoShutdown: Incorrect day in config option 'ServerAutoShutdown.EveryDays' - '{}'", restartDays);
        _isEnableModule = false;
        return;
    }

    if (restartHour < 0 or restartHour > 23 or restartMinute < 0 or restartMinute > 59 or restartSecond < 0 or restartSecond > 59)
    {
        LOG_ERROR("module", "> ServerAutoShutdown: Incorrect hour in config option 'ServerAutoShutdown.Time' - '{}'", configTime);
        _isEnableModule = false;
        return;
    }

    if (preAnnounceSeconds > DAY)
    {
        LOG_ERROR("module", "> ServerAutoShutdown: Ahah, how could this happen? Time to preannouce has been set to more than 1 day? ({}). Change to 1 hour (3600)", preAnnounceSeconds);
        preAnnounceSeconds = HOUR;
    }

    auto nowTime = time(nullptr);
    uint64 nextResetTime = GetNextShutdownTime(nowTime, weekdayMask, restartDays, restartHour, restartMinute, restartSecond);
    uint32 diffToShutdown = nextResetTime - static_cast<uint32>(nowTime);

    if (diffToShutdown < 10)
        LOG_WARN("module", "> ServerAutoShutdown: Next time to shutdown < 10 seconds, Set next period");
    LOG_INFO("module", " ");
    LOG_INFO("module", "> ServerAutoShutdown: System loading");

    // Cancel all task for support reload config
    scheduler.CancelAll();
    sWorld->ShutdownCancel();

    LOG_INFO("module", "> ServerAutoShutdown: Next time to shutdown - {}", Acore::Time::TimeToHumanReadable(Seconds(nextResetTime)));
    LOG_INFO("module", "> ServerAutoShutdown: Remaining time to shutdown - {}", Acore::Time::ToTimeString<Seconds>(diffToShutdown));
    LOG_INFO("module", " ");

    uint32 timeToPreAnnounce = static_cast<uint32>(nextResetTime) - preAnnounceSeconds;
    uint32 diffToPreAnnounce = timeToPreAnnounce - static_cast<uint32>(nowTime);

    // Ingnore pre announce time and set is left
    if (diffToShutdown < preAnnounceSeconds)
    {
        timeToPreAnnounce = static_cast<uint32>(nowTime) + 1;
        diffToPreAnnounce = 1;
        preAnnounceSeconds = diffToShutdown;
    }

    LOG_INFO("module", "> ServerAutoShutdown: Next time to pre announce - {}", Acore::Time::TimeToHumanReadable(Seconds(timeToPreAnnounce)));
    LOG_INFO("module", "> ServerAutoShutdown: Remaining time to pre announce - {}", Acore::Time::ToTimeString<Seconds>(diffToPreAnnounce));
    LOG_INFO("module", " ");

    StartPersistentGameEvents();

    // Add task for pre shutdown announce
    scheduler.Schedule(Seconds(diffToPreAnnounce), [preAnnounceSeconds](TaskContext /*context*/)
    {
        std::string preAnnounceMessageFormat = sConfigMgr->GetOption<std::string>("ServerAutoShutdown.PreAnnounce.Message", "[SERVER]: Automated (quick) server restart in {}");
        std::string message = Acore::StringFormat(preAnnounceMessageFormat, Acore::Time::ToTimeString<Seconds>(preAnnounceSeconds, TimeOutput::Seconds, TimeFormat::FullText));
        LOG_INFO("module", "> {}", message);
        sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, message);
        sWorld->ShutdownServ(preAnnounceSeconds, SHUTDOWN_MASK_RESTART, SHUTDOWN_EXIT_CODE);
    });
}

void ServerAutoShutdown::OnUpdate(uint32 diff)
{
    // If module disable, why do the update? hah
    if (!_isEnableModule)
        return;

    scheduler.Update(diff);
}

void ServerAutoShutdown::StartPersistentGameEvents()
{
    std::string eventList = sConfigMgr->GetOption<std::string>("ServerAutoShutdown.StartEvents", "");

    std::vector<std::string_view> tokens = Acore::Tokenize(eventList, ' ', false);
    GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();

    for (auto token : tokens)
    {
        if (token.empty())
            continue;

        uint32 eventId = *Acore::StringTo<uint32>(token);
        sGameEventMgr->StartEvent(eventId);

        GameEventData const& eventData = events[eventId];
        LOG_INFO("module", "> ServerAutoShutdown: Starting event {} ({}).", eventData.Description, eventId);
    }
}
