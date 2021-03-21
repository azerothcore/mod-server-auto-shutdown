/*
* This file is part of the WarheadCore Project. See AUTHORS file for Copyright information
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License along
* with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "ServerAutoShutdown.h"
#include "Config.h"
#include "Duration.h"
#include "Language.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "TaskScheduler.h"
#include "Tokenize.h"
#include "Util.h"
#include "World.h"

namespace
{
    // Scheduler - for update
    TaskScheduler scheduler;

    time_t GetLocalHourTimestamp(time_t time, uint8 hour, uint8 minute, uint8 secs)
    {
        tm timeLocal = TimeBreakdown(time);
        timeLocal.tm_hour = hour;
        timeLocal.tm_min = minute;
        timeLocal.tm_sec = secs;

        time_t midnightLocal = mktime(&timeLocal);

        if (midnightLocal <= time)
            midnightLocal += DAY;

        return midnightLocal;
    }

    time_t GetNextResetTime(time_t t, uint8 hour, uint8 minute, uint8 second)
    {
        return GetLocalHourTimestamp(t, hour, minute, second);
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

    std::string configTime = sConfigMgr->GetOption<std::string>("ServerAutoShutdown.Time", "04:00:00");
    auto const& tokens = acore::Tokenize(configTime, ':', false);

    if (tokens.size() != 3)
    {
        sLog->outError("> ServerAutoShutdown: Incorrect time in config option 'ServerAutoShutdown.Time' - '%s'", configTime.c_str());
        _isEnableModule = false;
        return;
    }

    // Check convert to int
    auto CheckTime = [tokens](std::initializer_list<uint8> index)
    {
        for (auto const& itr : index)
        {
            if (acore::StringTo<uint8>(tokens.at(itr)) == std::nullopt)
                return false;
        }

        return true;
    };

    if (!CheckTime({ 0, 1, 2 }))
    {
        sLog->outError("> ServerAutoShutdown: Incorrect time in config option 'ServerAutoShutdown.Time' - '%s'", configTime.c_str());
        _isEnableModule = false;
        return;
    }

    uint8 hour = *acore::StringTo<uint8>(tokens.at(0));
    uint8 minute = *acore::StringTo<uint8>(tokens.at(1));
    uint8 second = *acore::StringTo<uint8>(tokens.at(2));

    if (hour > 23)
    {
        sLog->outError("> ServerAutoShutdown: Incorrect hour in config option 'ServerAutoShutdown.Time' - '%s'", configTime.c_str());
        _isEnableModule = false;
    }
    else if (minute >= 60)
    {
        sLog->outError("> ServerAutoShutdown: Incorrect minute in config option 'ServerAutoShutdown.Time' - '%s'", configTime.c_str());
        _isEnableModule = false;
    }
    else if (second >= 60)
    {
        sLog->outError("> ServerAutoShutdown: Incorrect second in config option 'ServerAutoShutdown.Time' - '%s'", configTime.c_str());
        _isEnableModule = false;
    }

    auto nowTime = time(nullptr);
    uint64 nextResetTime = GetNextResetTime(nowTime, hour, minute, second);
    uint32 diffToShutdown = nextResetTime - static_cast<uint32>(nowTime);

    sLog->outString();
    sLog->outString("> ServerAutoShutdown: System loading");

    // Cancel all task for support reload config
    scheduler.CancelAll();

    sLog->outString("> ServerAutoShutdown: Next time to shutdown - %s", TimeToHumanReadable(nextResetTime).c_str());
    sLog->outString("> ServerAutoShutdown: Remaining time to shutdown - %s", secsToTimeString(diffToShutdown).c_str());
    sLog->outString();

    uint32 shutdownDelay = sConfigMgr->GetOption<uint32>("ServerAutoShutdown.Delay", 10);

    // Add task for shutdown server
    scheduler.Schedule(Seconds(diffToShutdown), [&](TaskContext /*context*/)
    {
        sWorld->ShutdownServ(shutdownDelay, 0, SHUTDOWN_EXIT_CODE);
    });

    uint32 preAnnounceDelay = sConfigMgr->GetOption<uint32>("ServerAutoShutdown.PreAnnounce.Delay", 3600);
    if (preAnnounceDelay > DAY)
    {
        sLog->outError("> ServerAutoShutdown: Ahah, how could this happen? Time to preannouce more 1 day? (%u). Set to 1 hour (3600)", preAnnounceDelay);
        preAnnounceDelay = 3600;
    }

    if (diffToShutdown < preAnnounceDelay)
        return;

    uint32 timeToPreAnnounce = static_cast<uint32>(nextResetTime) - preAnnounceDelay;
    uint32 diffToPreAnnounce = timeToPreAnnounce - static_cast<uint32>(nowTime);

    sLog->outString("> ServerAutoShutdown: Next time to pre annouce - %s", TimeToHumanReadable(timeToPreAnnounce).c_str());
    sLog->outString("> ServerAutoShutdown: Remaining time to pre annouce - %s", secsToTimeString(diffToPreAnnounce).c_str());
    sLog->outString();

    // Add task for pre shutdown announce
    scheduler.Schedule(Seconds(diffToPreAnnounce), [&](TaskContext /*context*/)
    {
        std::string preAnnounceMessageFormat = sConfigMgr->GetOption<std::string>("ServerAutoShutdown.PreAnnounce.Message", "[SERVER]: Automated (quick) server restart in %s");
        std::string message = acore::StringFormat(preAnnounceMessageFormat, secsToTimeString(preAnnounceDelay));

        sLog->outString("> %s", message.c_str());

        sWorld->SendServerMessage(SERVER_MSG_STRING, message.c_str());
    });
}

void ServerAutoShutdown::OnUpdate(uint32 diff)
{
    // If module disable, why do the update? hah
    if (!_isEnableModule)
        return;

    scheduler.Update(diff);
}
