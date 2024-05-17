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
json deliverCargoToContract(const string callsign, const string contract_id, const string ship_symbol, const string trade_symbol, const int units){
    json payload;
    payload["shipSymbol"] = ship_symbol;
    payload["tradeSymbol"] = trade_symbol;
    payload["units"] = units;
    json result = http_post(callsign, "https://api.spacetraders.io/v2/my/contracts/" + contract_id + "/deliver", payload);

    if (result.contains("error")){
        return result;
    }

    if (!result["data"]["contract"]["terms"]["deliver"][0]["unitsFulfilled"].is_number_integer()){
        log("ERROR", "[ERROR] deliverCargoToContract result['data']['contract']['terms']['deliver'][0]['unitsFulfilled'] is not an integer");
        return result;
    }

    if (!result["data"]["contract"]["terms"]["deliver"][0]["unitsRequired"].is_number_integer()){
        log("ERROR", "deliverCargoToContract result['data']['contract']['terms']['deliver'][0]['unitsRequired'] is not an integer");
        return result;
    }

    int units_fulfilled = result["data"]["contract"]["terms"]["deliver"][0]["unitsFulfilled"];
    int units_required = result["data"]["contract"]["terms"]["deliver"][0]["unitsRequired"];

    log("INFO", ship_symbol + " | Delivered " + to_string(units) + " of " + trade_symbol + " [" + to_string(units_fulfilled) + "/" +
    to_string(units_required) + "]");

    return result["data"];
}

// Fulfill Contract
json fulfillContract(const string callsign, const string contract_id){
    json result = http_post(callsign, "https://api.spacetraders.io/v2/my/contracts/" + contract_id + "/fulfill");

    if (result.contains("error")){
        log("ERROR", "fulfillContract returned error");
        return result;
    }

    if (!result.contains("data")){
        log("ERROR", "fulfillContract result does not contain data key");
        return result;
    }
    const json data = result["data"];

    if(!data.contains("contract")){
        log("ERROR", "fulfillContract data does not contain contract key");
        return result;
    }

    const json contract = data["contract"];

    const bool contract_fulfilled = contract["fulfilled"];

    if (contract_fulfilled){
        log("INFO", "Contract fulfilled, rejoice!");
        return data;
    } else {
        log("ERROR", "fulfillContract failed to fulfill contract.");
        return result;
    }
}

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