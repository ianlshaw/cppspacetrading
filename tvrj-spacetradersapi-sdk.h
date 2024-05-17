#pragma once

#include <stdio.h>
#include <iostream>
#include "json.hpp"
#include "auth-file-utils.h"
#include "log-utils.h"
#include "just-enough-curl.h"

using namespace std;

using json = nlohmann::json;

// Get Status

// Agents

// Register New Agent
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

// Get Agent
json getAgent(const string callsign){
    const json result = http_get(callsign, "https://api.spacetraders.io/v2/my/agent");
    if (result.contains("data")){
        return result["data"];
    }
    log("ERROR", "getAgent response does not contain data key");
    cout << result;
    printJson(result);
    return result;
}

// List Agents

// Get Public Agent

// Contracts

// List Contracts
json listContracts(const string callsign){
    const json result = http_get(callsign, "https://api.spacetraders.io/v2/my/contracts");
    if (result.contains("data")){
        return result["data"];
    }
    log("ERROR", "listContracts response does not contain data key");
    cout << result;
    printJson(result);
    return result;
}

// Get Contract
json getContract(const string callsign, const string contract_id){
    const json result = http_get(callsign, "https://api.spacetraders.io/v2/my/contracts/" + contract_id);
    if (result.contains("data")){
        return result["data"];
    }
    log("ERROR", "getContract response does not contain data key");
    cout << result;
    printJson(result);
    return result;
}

// Accept Contract
json acceptContract(const string callsign, const string contractId) {
    log("INFO", "Attempting to accept contract" + contractId);
    const json result = http_post(callsign, "https://api.spacetraders.io/v2/my/contracts/" + contractId + "/accept");
    if (result.contains("data")){
        return result["data"];
    }
    log("ERROR", "acceptContract response does not contain data key");
    cout << result;
    printJson(result);
    return result;
}

// Deliver Cargo To Contract

// Fulfill Contract

// Factions

// List Factions

// Get Faction

// Fleet

// List Ships

// Purchase Ship

// Get Ship

// Get Ship Cargo

// Orbit Ship

// Ship Refine

// Create Chart

// Get Ship Cooldown

// Dock Ship

// Create Survey

// Extract Resources

// Siphon Resources

// Extract Resources With Survey

// Jettison Cargo

// Jump Ship

// Navigate Ship

// Patch Ship Nav

// Get Ship Nav

// Warp Ship

// Sell Cargo

// Scan Systems

// Scan Waypoints

// Scan Ships

// Refuel Ship

// Purchase Cargo

// Transfer Cargo

// Negotiate Contract

// Get Mounts

// Install Mount

// Remove Mount

// Get Scrap Ship

// Scrap Ship

// Get Repair Ship

// Repair Ship

// Systems

// List Systems

// Get System

// List Waypoints In System

// Get Waypoint

// Get Market

// Get Shipyard

// Get Jump Gate

// Get Construction Site

// Supply Construction Site