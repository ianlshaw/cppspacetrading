#pragma once

#include <stdio.h>
#include <iostream>
#include "json.hpp"
#include "auth-file-utils.h"
#include "log-utils.h"
#include "just-enough-curl.h"

using namespace std;

using json = nlohmann::json;


void registerAgent(const string callsign, const string faction) {

    // we don't want to ever accidently overwrite a .token file
    if (doesAuthFileExist(callsign + ".token")) {
        return;
    }

    log("INFO", "Attempting to register new agent " + callsign);

    json register_agent_json_object = {};
    register_agent_json_object["symbol"] = callsign;
    register_agent_json_object["faction"] = faction;
    json result = http_post(callsign, "https://api.spacetraders.io/v2/register", register_agent_json_object);
    writeAuthTokenToFile(result["data"]["token"], callsign);
}


json getAgent(const string callsign){
    const json result = http_get(callsign, "https://api.spacetraders.io/v2/my/agent");
    if (result.contains("data")){
        return result["data"];
    }
    return result;
}