/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 * Copyright (C) 2021+ WarheadCore <https://github.com/WarheadCore>
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

    time_t GetNextResetTime(time_t time, uint8 hour, uint8 minute, uint8 second)
    {
        tm timeLocal = TimeBreakdown(time);
        timeLocal.tm_hour = hour;
        timeLocal.tm_min = minute;
        timeLocal.tm_sec = second;

        time_t midnightLocal = mktime(&timeLocal);

        if (midnightLocal <= time)
            midnightLocal += DAY;

        return midnightLocal;
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

    if (diffToShutdown < 10)
    {
        sLog->outString("> ServerAutoShutdown: Next time to shutdown < 10 seconds, Set next day", TimeToHumanReadable(nextResetTime).c_str());
        nextResetTime += DAY;
    }

    diffToShutdown = nextResetTime - static_cast<uint32>(nowTime);

    sLog->outString();
    sLog->outString("> ServerAutoShutdown: System loading");

    // Cancel all task for support reload config
    scheduler.CancelAll();
    sWorld->ShutdownCancel();

    sLog->outString("> ServerAutoShutdown: Next time to shutdown - %s", TimeToHumanReadable(nextResetTime).c_str());
    sLog->outString("> ServerAutoShutdown: Remaining time to shutdown - %s", secsToTimeString(diffToShutdown).c_str());
    sLog->outString();

    uint32 preAnnounceSeconds = sConfigMgr->GetOption<uint32>("ServerAutoShutdown.PreAnnounce.Seconds", 3600);
    if (preAnnounceSeconds > DAY)
    {
        sLog->outError("> ServerAutoShutdown: Ahah, how could this happen? Time to preannouce more 1 day? (%u). Set to 1 hour (3600)", preAnnounceSeconds);
        preAnnounceSeconds = 3600;
    }

    uint32 timeToPreAnnounce = static_cast<uint32>(nextResetTime) - preAnnounceSeconds;
    uint32 diffToPreAnnounce = timeToPreAnnounce - static_cast<uint32>(nowTime);

    // Ingnore pre announce time and set is left
    if (diffToShutdown < preAnnounceSeconds)
    {
        timeToPreAnnounce = static_cast<uint32>(nowTime) + 1;
        diffToPreAnnounce = 1;
        preAnnounceSeconds = diffToShutdown;
    }

    /*sLog->outString("> nextResetTime - %lu (%s)", nextResetTime, TimeToHumanReadable(nextResetTime).c_str());
    sLog->outString("> diffToShutdown - %u", diffToShutdown);
    sLog->outString("> timeToPreAnnounce - %u (%s)", timeToPreAnnounce, TimeToHumanReadable(timeToPreAnnounce).c_str());
    sLog->outString("> diffToPreAnnounce - %u", diffToPreAnnounce);
    sLog->outString("> preAnnounceSeconds - %u", preAnnounceSeconds);*/

    sLog->outString("> ServerAutoShutdown: Next time to pre annouce - %s", TimeToHumanReadable(timeToPreAnnounce).c_str());
    sLog->outString("> ServerAutoShutdown: Remaining time to pre annouce - %s", secsToTimeString(diffToPreAnnounce).c_str());
    sLog->outString();

    // Add task for pre shutdown announce
    scheduler.Schedule(Seconds(diffToPreAnnounce), [preAnnounceSeconds](TaskContext /*context*/)
    {
        std::string preAnnounceMessageFormat = sConfigMgr->GetOption<std::string>("ServerAutoShutdown.PreAnnounce.Message", "[SERVER]: Automated (quick) server restart in %s");
        std::string message = acore::StringFormat(preAnnounceMessageFormat, secsToTimeString(preAnnounceSeconds));

        sLog->outString("> %s", message.c_str());

        sWorld->SendServerMessage(SERVER_MSG_STRING, message.c_str());
        sWorld->ShutdownServ(preAnnounceSeconds, 0, SHUTDOWN_EXIT_CODE);
    });
}

void ServerAutoShutdown::OnUpdate(uint32 diff)
{
    // If module disable, why do the update? hah
    if (!_isEnableModule)
        return;

    scheduler.Update(diff);
}
