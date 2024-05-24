#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <sstream> 
#include <ctime>
#include <unistd.h>   
#include <curl/curl.h>
#include "json.hpp"
#include "log-utils.h"
#include "just-enough-curl.h"
#include "tvrj-spacetradersapi-sdk.h"

using namespace std;
using json = nlohmann::json;

string callsign;

const string category_wallet = "WALLET | ";
const string category_survey = "SURVEY | ";

int http_calls = 0;

const int turn_length = 90;


const int maximum_excavators = 8;
const int maximum_shuttles = 1;

float number_of_surveyor_ships = 0;
float number_of_mining_ships = 0;
float number_of_shuttles = 0;
float number_of_haulers = 0;
float number_of_satellites = 0;

const float desired_number_of_satellites = 2;

const float desired_hauler_to_miner_ratio = 0.08;
const float desired_surveyor_to_miner_ratio = 0.08;

const int desired_credits_per_cargo_unit = 50;

float hauler_to_miner_ratio = 0.0;
float surveyor_to_miner_ratio = 0.0;

bool buyer_satellite_exists = false;

vector <string> transports_on_site;

int credits = 0;


string current_system_symbol;
json contracts_list;
json system_waypoints_list;
json target_contract;
string target_resource;
string target_contract_id;
string asteroid_belt_symbol;
string delivery_waypoint_symbol;
string satellite_shipyard_symbol;
string mining_ship_shipyard_symbol;
string surveyor_ship_shipyard_symbol;
string shuttle_ship_shipyard_symbol;
string light_hauler_shipyard_symbol;

float survey_score_threshold = 0.3;    // command frigate uses this to decide its role
vector <string> resource_keep_list;    // storage for cargoSymbols. everything else gets jettisoned
json cached_market_data;



int priceCheck(const string good_to_check){

    //log("DEBUG", "priceCheck " + good_to_check);

    // TODO: Make market data an argument, remove global.
    json trade_goods = cached_market_data["tradeGoods"];
    for (json market_good: trade_goods){
        if (good_to_check == market_good["symbol"]){ 
            //log("DEBUG", to_string(market_good["sellPrice"]));
            return market_good["sellPrice"];
        }
    }
    // if a resource symbol does not appear in market_data['tradeGoods'], it is worthless.
    return 0;
}

class survey {       
  public:           
    json jsonObject;  // Json object for an individual survey
    float targetResourcePercentage = 0.0;        // Used to prioritize which survey is used to mine 
    int marketValue = 0;
                                                 //
    float scoreSurveyForTargetFarming(const string symbol_to_check){
        const json deposits = jsonObject["deposits"];
        const float deposits_size = deposits.size();
        float found_resource_count = 0;
        for (json deposit: deposits){
            const string deposit_symbol = deposit["symbol"];
            if (deposit_symbol == symbol_to_check){
                found_resource_count++;
            }
        }
        targetResourcePercentage = found_resource_count / deposits_size;
        //log("DEBUG", "targetResourcePercentage: " + to_string(targetResourcePercentage));
        return targetResourcePercentage;
    }

    int scoreSurveyForProfitability(){
         int sum_of_deposit_prices = 0;
         json deposits = jsonObject["deposits"];
         int number_of_deposits = deposits.size();
         for (json deposit: deposits){
             string deposit_symbol = deposit["symbol"];
             int value = priceCheck(deposit_symbol);
             sum_of_deposit_prices = sum_of_deposit_prices + value;
         }

         int value_average = sum_of_deposit_prices / number_of_deposits;

         marketValue = value_average;
         return marketValue;
    }

    bool is_null(){
        if (jsonObject.is_null()){
            return true;
        } 
        return false;
    }

};

vector <survey> surveys; // Storage for the surveys we will create
survey active_survey;        // Json object for the survey with the highest score

survey bestSurveyForTargetFarming(){
    float best_score = 0;
    survey best_survey;
    for (survey each_survey: surveys){
        const float score = each_survey.targetResourcePercentage;
        if(score > best_score){
            best_score = score;
            best_survey = each_survey;
        }
    }
    return best_survey;
}

survey bestSurveyForProfiteering(){
    float best_score = 0;
    survey best_survey;
    for (survey each_survey: surveys){
        const float score = each_survey.marketValue;
        if(score > best_score){
            best_score = score;
            best_survey = each_survey;
        }
    }
    return best_survey;
}


// until this is true, we do not want to sell the target_resource
bool isContractFulfilled(const json &contract_json){
    if (contract_json["fulfilled"].is_boolean()){
        return contract_json["fulfilled"];
    }
    log("ERROR", "isContractFulfilled contract_json['fulfilled'] is not boolean");
    return false;
}

void update_credits(const int new_credits){
    credits = new_credits;
	//log("INFO", "Balance: " + to_string(credits));
}

bool haveEnoughCredits(const int price){
    return(credits > price ? true : false);
}

json listWaypointsInSystem(const string system_symbol){
    return http_get(callsign, "https://api.spacetraders.io/v2/systems/" + system_symbol + "/waypoints");
}

string shipSymbolFromJson(const json ship_json){
    if (ship_json["data"]["symbol"].is_string()){
        return ship_json["data"]["symbol"];
    }
    log("ERROR", "shipSymbolFromJson ship_json['data']['symbol'] not string");
    return "ERROR SHIP JSON DOES NOT CONTAIN SYMBOL";
}

bool hasContractBeenAccepted(const json contract_json){
    if (contract_json["accepted"].is_boolean()){
        return contract_json["accepted"];
    } 
    return false;
}

string findWaypointByType(const string systemSymbol, const string type){
    json result = http_get(callsign, "https://api.spacetraders.io/v2/systems/" + systemSymbol + "/waypoints?type=" + type);
    if (!result["data"][0]["symbol"].is_string()){
        log("ERROR", "findWaypointByType['data'][0]['symbol'] not string");
        return "ERROR findWaypointByType";
    }
    return result["data"][0]["symbol"];
}

json findWaypointsByTrait(const string system_symbol, const string trait){
    json result = http_get(callsign, "https://api.spacetraders.io/v2/systems/" + system_symbol + "/waypoints?traits=" + trait);
    if (result["data"].is_null()){
        log("ERROR", "findWaypointsByType['data'] is null");
        return result;
        
    }
    return result["data"];
}

json getShipyard(const string system_symbol, const string waypoint_symbol){
    json result = http_get(callsign, "https://api.spacetraders.io/v2/systems/" + system_symbol + "/waypoints/" + waypoint_symbol + "/shipyard");
    if (result.contains("error")){
        printJson(result);
        return result;
    }
    return result["data"];
}

string findShipyardByShipType(const string ship_type){
    //log("DEBUG", "findShipyardByShipType " + ship_type);
    json shipyard_waypoints = findWaypointsByTrait(current_system_symbol, "SHIPYARD"); 

    for (json waypoint : shipyard_waypoints) {
        string waypoint_symbol = waypoint["symbol"];
        json shipyard = getShipyard(current_system_symbol, waypoint_symbol);
        json ship_types = shipyard["shipTypes"];
        for (json available_ship_type : ship_types){
            string type = available_ship_type["type"];
            if (type == ship_type){
                return waypoint_symbol;
            }
        }
    }
    return "ERROR findShipyardByShipType no shipyard found with ship_type";
}

int howMuchDoesShipCost(const string ship_type, const string shipyard_waypoint_symbol){
	const json get_shipyard_result = getShipyard(current_system_symbol, shipyard_waypoint_symbol);
	json ships = get_shipyard_result["ships"];
	for(json ship: ships){
		if (ship_type == ship["type"]){
			return ship["purchasePrice"];
		}
	}
	log("ERROR", "howMuchDoesShipCost ship not found. Returning some insane cost to force haveEnoughCredits to return false");
	return 10000000;
}

bool isShipAffordable(const string ship_type, const string shipyard_symbol){
	// can i afford SHIP SURVEYOR from surveyor_ship_shipyard_symbol
	const json agent = getAgent(callsign);
	update_credits(agent["credits"]);
	
	int ship_price = howMuchDoesShipCost(ship_type, shipyard_symbol);
	
	if (haveEnoughCredits(ship_price)){
	    return true;
	}  
	log("INFO", category_wallet + 
                "Cannot afford " + ship_type + 
                " from " + shipyard_symbol + 
                " it costs: " + to_string(ship_price) + 
                " but you only have " + to_string(credits) + 
                " boss");
	return false;
}

json getMarket(const string system_symbol, const string waypoint_symbol){
    const json result = http_get(callsign, "https://api.spacetraders.io/v2/systems/" + system_symbol + "/waypoints/" + waypoint_symbol + "/market");
    if (!result.contains("data")){
        log("ERROR", "getMarket result does not contain key data");
        return result;
    }
    return result["data"];
}

void initializeGlobals(){
    contracts_list = listContracts(callsign);
    // this next one is potentially an error. since the item target_contract holds right now is the first in the result
    // of the list contracts method, NOT the result of a get contract against the contract ID.
    // i should check what is the difference.
    target_contract = contracts_list[0];
    target_resource = target_contract["terms"]["deliver"][0]["tradeSymbol"];
    target_contract_id = target_contract["id"];
    log("INFO", "target_resource = " + target_resource);
    log("INFO", "survey_score_threshold: " + to_string(survey_score_threshold));
    log("INFO", "contract_fulfilled = " + to_string(target_contract["fulfilled"]));
    json first_ship = getShip(callsign, callsign + "-1");
    current_system_symbol = first_ship["nav"]["systemSymbol"];
    system_waypoints_list = listWaypointsInSystem(current_system_symbol);
    asteroid_belt_symbol = findWaypointByType(current_system_symbol, "ENGINEERED_ASTEROID"); 
    delivery_waypoint_symbol = target_contract["terms"]["deliver"][0]["destinationSymbol"];
    satellite_shipyard_symbol = findShipyardByShipType("SHIP_PROBE");
    log("INFO", "satellite_shipyard_symbol = " + satellite_shipyard_symbol);
    surveyor_ship_shipyard_symbol = findShipyardByShipType("SHIP_SURVEYOR");
    log("INFO", "surveyor_ship_shipyard_symbol = " + surveyor_ship_shipyard_symbol);
    mining_ship_shipyard_symbol = findShipyardByShipType("SHIP_MINING_DRONE");
    log("INFO", "mining_ship_shipyard_symbol = " + mining_ship_shipyard_symbol);
    shuttle_ship_shipyard_symbol = findShipyardByShipType("SHIP_LIGHT_SHUTTLE");
    log("INFO", "shuttle_ship_shipyard_symbol = " + shuttle_ship_shipyard_symbol);
    light_hauler_shipyard_symbol = findShipyardByShipType("SHIP_LIGHT_HAULER");
    log("INFO", "light_hauler_shipyard_symbol = " + light_hauler_shipyard_symbol);


    json get_market_result = getMarket(current_system_symbol, delivery_waypoint_symbol);
    json imports = get_market_result["imports"];
    for (json an_import : imports){
        string import_symbol = an_import["symbol"];
        resource_keep_list.push_back(import_symbol);
    }

    cout << endl;
}

bool isShipDocked(const json &ship_json){
    if (ship_json["nav"]["status"].is_string()) {
        return (ship_json["nav"]["status"] == "DOCKED" ? true : false);
    } else {
        log("ERROR", "isShipDocked ship_json['nav]['status'] is not string");
        return false;
    }
}

bool isShipInTransit(const json &ship_json){
    //log("DEBUG", "isShipInTransit");
    if (!ship_json["nav"]["status"].is_string()) { 
        log("ERROR", "isShipInTransit ship_json['nav']['status'] is not a string");
        return false;
    }
    const string status = ship_json["nav"]["status"];
    if (status == "IN_TRANSIT"){
        const string ship_symbol = ship_json["symbol"];
        const string role = ship_json["registration"]["role"];
		log("INFO", ship_symbol + " | " + status); 
    	return true;
    } 
	return false;
}

bool isShipInOrbit(const json &ship_json){
    if (ship_json["nav"]["status"].is_string()) { 
        return (ship_json["nav"]["status"] == "IN_ORBIT" ? true : false);
    } else {
        log("ERROR", "isShipInOrbit ship_json['nav']['status'] not string");
        return false;
    }
}

void createSurvey(const string ship_symbol){
    //log("DEBUG", "createSurvey");

    json result = http_post(callsign, "https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/survey");

	log("INFO", ship_symbol + " | createSurvey");
    
    for (json each_survey: result["data"]["surveys"]){
        survey a_survey;
        a_survey.jsonObject = each_survey;
        surveys.push_back(a_survey);
    }
}

// this is used to decide if the command frigate will survey or mine.
// behaviour can be tuned via survey_score_threshold global
bool isSurveyGoodEnough(const survey survey_to_test){
    //log("DEBUG", "isSurveyGoodEnough");
    //log("DEBUG", "targetResourcePercentage: " + to_string(survey_to_test.targetResourcePercentage));
    //log("DEBUG", "marketValue: " + to_string(survey_to_test.targetResourcePercentage));
    //log("DEBUG", "survey_score_threshold: " + to_string(survey_score_threshold));

    float score;

    if (isContractFulfilled(target_contract) && !cached_market_data.is_null()){
        score = survey_to_test.marketValue;
    } else {
        score = survey_to_test.targetResourcePercentage;
    }
   
    return (score >= survey_score_threshold ? true : false);
}

bool isShipCargoHoldFull(const json &cargo){
    if (!cargo["units"].is_number_integer()){
		log("ERROR", "isShipCargoHoldFull cargo['units'] not integer");
        return false;
    }
    if (!cargo["capacity"].is_number_integer()){
		log("ERROR", "isShipCargoHoldFull cargo['capacity'] not integer");
        return false;
    }
    return (cargo["units"] == cargo["capacity"] ? true : false);
}

bool isShipCargoHoldAlmostFull(const json &cargo){
    const float units = cargo["units"];
    const float capacity = cargo["capacity"];
    const float percent_full = units / capacity;
    const float almost_full_threshold = 0.66;
    if (percent_full >= almost_full_threshold){
        //log("DEBUG",  + " | Cargo hold almost full");
        return true;
    }
    return false;
}

bool isShipCargoHoldEmpty(const json &cargo){
    if (!cargo["units"].is_number_integer()){
		log("ERROR", "isShipCargoHoldEmpty cargo['units'] not integer");
        return false;
    }
    return (cargo["units"] == 0 ? true : false);
}

bool isShipAtWaypoint(const json &ship_json, string waypoint_symbol){
    if (!ship_json["nav"]["waypointSymbol"].is_string()){
        log("ERROR", "ship_json['nav']['waypointSymbol'] not string");
    }
    return (ship_json["nav"]["waypointSymbol"] == waypoint_symbol ? true : false);
}

bool isSurveyListEmpty(){
    int number_of_surveys = surveys.size();
    return (number_of_surveys == 0 ? true : false);
}

bool isSurveyExpired(const json survey){

    const string expiration = survey["expiration"];

    tm expiration_time_handle{};
    istringstream expiration_string_stream(expiration);
    
    // scan the json string using a hardcoded format
    expiration_string_stream >> get_time(&expiration_time_handle, "%Y-%m-%dT%H:%M:%S");

    if (expiration_string_stream.fail()) {
        throw runtime_error{"failed to parse time string"};
    }   

    time_t expiration_timestamp = mktime(&expiration_time_handle);

    // time now
    time_t current_timestamp = time(NULL);

    // convert time now to utc
    tm* utc_time_now = gmtime(&current_timestamp);

    // convert it back into a time_t for subsequent comparison
    time_t utc_time_now_timestamp = mktime(utc_time_now);

    // find the difference between the utc time now and surveys expiration
    double seconds_until_expiry = difftime(expiration_timestamp, utc_time_now_timestamp);

    return (seconds_until_expiry <= 30 ? true : false);
}

//TODO remove this
void promoteBestSurveyForTargetFarming(){
    //log("DEBUG", "promoteBestSurveyForTargetFarming");

    active_survey = bestSurveyForTargetFarming();

}

// TODO remove this
void promoteBestSurveyForProfitability(){
	//log("DEBUG", "promoteBestSurveyForProfitability");
    
    active_survey = bestSurveyForProfiteering();

}

void promoteBestSurvey(){
    //log("DEBUG", "promoteBestSurvey");
    if (isContractFulfilled(target_contract) && !cached_market_data.is_null()){
        promoteBestSurveyForProfitability();
    } else {
    // by default we farm for target_resource
        promoteBestSurveyForTargetFarming();
    }
}


void scoreSurveysForTargetFarming(const string trade_good){

	//log("DEBUG", "scoreSurveysForTargetFarming " + trade_good);

    int index = 0;
    for (survey item: surveys){
        if (item.targetResourcePercentage > 0){
            index++;
            continue;
        }
        float score = item.scoreSurveyForTargetFarming(trade_good);
        surveys.at(index).targetResourcePercentage = score;
        index++;
    }
}

void scoreSurveysForProfitability(){

	//log("DEBUG", "scoreSurveysForProfitability");

	if (cached_market_data.is_null()){
		log("ERROR", "market_data is null");
        return;
	}

    int index = 0;
    for (survey item: surveys){
        float score = item.scoreSurveyForProfitability();
        surveys.at(index).marketValue = score;
        index++;
    } 
}

void resetBestSurvey(){
	active_survey = {};
}

bool isAtLeastOneTransportOnSite(){
	return (transports_on_site.size() > 0 ? true : false);
}

string firstOnSiteTransport(){
	return transports_on_site.at(0);
}

bool isTransportPresentInOnSiteVector(const string &ship_symbol){
	for (string transport_symbol: transports_on_site){
		if (transport_symbol == ship_symbol){
			return true;
		}
	}
	return false;
}

void removeTransportFromOnSiteVector(const string &ship_symbol){
	//log("DEBUG", "removeTransportFromOnSiteVector " + ship_symbol);
	int vector_index = 0;
 	for (string transport_symbol: transports_on_site){
		if (transport_symbol == ship_symbol){
			transports_on_site.erase(transports_on_site.begin() + vector_index);
			return;
		}
	}
}

void sendHaulerToMarket(const string ship_symbol){
    log("INFO", ship_symbol + " | Heading to market");
    removeTransportFromOnSiteVector(ship_symbol);
    navigateShip(callsign, ship_symbol, delivery_waypoint_symbol);
}

void removeExpiredSurveys(){
    int vector_index = 0;
    while(vector_index < surveys.size()){
        survey each_survey = surveys.at(vector_index);
        json each_survey_object = each_survey.jsonObject;
        if (isSurveyExpired(each_survey_object)){
            log("INFO", category_survey + "Erasing expired survey");
            surveys.erase(surveys.begin() + vector_index);
            if (each_survey_object == active_survey.jsonObject){
            	log("INFO", category_survey + "Best survey erased! Updating best survey...");
				resetBestSurvey();
            }
          
        } else {
            vector_index++;
        }
    }
}

bool removeSurveyBySignature(const string signature_to_remove){
    //log("DEBUG", "removeSurveyBySignature " + signature_to_remove);
    int vector_index = 0;
    for (survey each_survey: surveys){
        json each_survey_object = each_survey.jsonObject;
        string each_survey_signature = each_survey_object["signature"];
        //log("DEBUG", "signature:" + each_survey_signature);
        if (signature_to_remove == each_survey_signature){
            surveys.erase(surveys.begin() + vector_index);
            log("INFO", category_survey + "Removed exhausted survey: " + signature_to_remove);
            resetBestSurvey();
            promoteBestSurvey();
            return true;
        }
        vector_index++;
    }
    log("ERROR", "removeSurveyById signature not found in surveys vector");
    return false;
}

// useless goods can be obtained through mining, this method is used to decide what gets "spaced"
bool isItemWorthKeeping(const string item){
    for(string resource: resource_keep_list){
        if (item == resource){
            return true;
        }
    }
    return false;
}

json extractResourcesWithSurvey(const string ship_symbol, const json target_survey){

    const json result = http_post(callsign, "https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/extract/survey", target_survey);

    if (result.contains("error")){

        if (result["error"]["code"] == 4224){
            // survey exhausted, manually expire/remove it here
            log("WARN", "Survey exhausted!");
            const string signature = target_survey["signature"];
            removeSurveyBySignature(signature);
        }

        log("WARN", "extractResourcesWithSurvey contains error");
        return(result);
    }

    const string extracted_resource_symbol = result["data"]["extraction"]["yield"]["symbol"];
    const int extracted_resource_units = result["data"]["extraction"]["yield"]["units"];
    const json cargo = result["data"]["cargo"];

    log("INFO", ship_symbol + " | Extracted " + to_string(extracted_resource_units) + " units of " + extracted_resource_symbol + " [" + to_string(cargo["units"]) + "/" + to_string(cargo["capacity"]) + "]");  

    return result["data"];
}

int cargoCount(const json inventory, const string cargo_symbol){    
    for (json item: inventory){
        if (item["symbol"] == cargo_symbol){
            return item["units"];
        }
    }
    return 0;
}

int tradeVolumeOf(const json &market_data, const string cargo_symbol){
    int tradeVolume;
    if (!market_data["tradeGoods"].is_array()){
        log("ERROR", "market_data is not an array");
        return 0;
    }
    
    for(json tradeGood : market_data["tradeGoods"]){
        if (!tradeGood["symbol"].is_string()){
            log("ERROR", "tradeGood['symbol'] is not a string");
            return 0;
        }
        const string trade_good_symbol = tradeGood["symbol"];
        if (trade_good_symbol == cargo_symbol){
            if (!tradeGood["tradeVolume"].is_number_integer()){
                log("ERROR", "tradeGood['tradeVolume'] is not an integer");
                return 0;
            }
            tradeVolume = tradeGood["tradeVolume"];
        }
    }
    return tradeVolume;
}

int cargoRemaining(const json &cargo){
	const int units = cargo["units"];
	const int capacity = cargo["capacity"];
    return capacity - units;
}

int inventoryValue(const json &market_data, const json &inventory){
    if (!market_data.is_object()){
        log("ERROR", "inventoryValue market_data is not an object");
        return 0;
    }
    if(!inventory.is_array()){
        log("ERROR", "inventoryValue inventory is not an array");
        return 0;
    }
    if(!market_data["tradeGoods"].is_array()){
        log("ERROR", "inventoryValue market_data['tradeGoods'] is not an array");
        return 0;
    }
    int total_value = 0;
    for(json inventory_item : inventory){
        for(json market_data_item : market_data["tradeGoods"]){
            if(!inventory_item["symbol"].is_string()){
                log("ERROR", "inventoryValue inventory_item['symbol'] is not a string");
                return 0;
            }
            if(!market_data_item["symbol"].is_string()){
                log("ERROR", "inventoryValue market_data_item['symbol'] is not a string");
                return 0;
            }
            if (inventory_item["symbol"] == market_data_item["symbol"]){
                if(!market_data_item["sellPrice"].is_number_integer()){
                    log("ERROR", "inventoryValue market_data_item['sellPrice] is not an integer");
                    return 0;
                }
                int sellPrice = market_data_item["sellPrice"];
                if(!inventory_item["units"].is_number_integer()){
                    log("ERROR", "inventoryValue inventory_item['units'] is not an integer");
                    return 0;
                }
                int units = inventory_item["units"];
                int instrument_total = sellPrice * units;
                total_value = total_value + instrument_total;
            }
        }
    }
    log("INFO", "inventoryValue " + to_string(total_value));
    return total_value;
}

int creditsPerCargoUnit(const json &market_data, const json &cargo){
    if(!market_data.is_object()){
        log("ERROR", "creditsPerCargoUnit market_data is not an object");
        return 0;
    }
    if (!cargo["inventory"].is_array()){
        log("ERROR", "creditsPerCargoUnit cargo['inventory'] is not an array");
        return 0;
    }
    const json inventory = cargo["inventory"];
    int inventory_value = inventoryValue(market_data, inventory);
    if (!cargo["units"].is_number_integer()){
        log("ERROR", "creditsPerCargoUnit cargo['units'] is not an integer");
    }
    int units = cargo["units"];
    log("INFO", "creditsPerCargoUnit: " + to_string(inventory_value / units));
    return inventory_value / units;
}

void sellCargo(const string ship_symbol, const string cargo_symbol, const int units){
    json payload;
    payload["symbol"] = cargo_symbol;
    payload["units"] = units;

    json result = http_post(callsign, "https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/sell", payload);
    if (result.contains("error")){
        log("ERROR", "sellCargo returned error");
        return;
    }

    const json transaction = result["data"]["transaction"];
    const int units_sold = transaction["units"];
    const string trade_symbol = transaction["tradeSymbol"];
    const int total_price = transaction["totalPrice"];

    log("INFO", ship_symbol + " | Sold " + to_string(units_sold) + " " + trade_symbol + " for " + to_string(total_price));

    const int new_balance = result["data"]["agent"]["credits"];
    update_credits(new_balance);
    log("INFO", category_wallet + "Balance: " + to_string(new_balance));
}

json transferCargo(const string source_ship_symbol, const string destination_ship_symbol, const string trade_symbol, const int units){

	//log("DEBUG", source_ship_symbol + " | transferCargo "    
    //    + to_string(units) + " " 
    //    + trade_symbol + " to " 
    //    + destination_ship_symbol);

    json payload;
    payload["tradeSymbol"] = trade_symbol;
    payload["units"] = units;
    payload["shipSymbol"] = destination_ship_symbol;

    const json result = http_post(callsign, "https://api.spacetraders.io/v2/my/ships/" + source_ship_symbol + "/transfer", payload, 400);

    if (!result.contains("data")){
        return result;
    }
    const json data = result["data"];

    if (!data.contains("cargo")){
        log("ERROR", "transferCargo data does not contain cargo key");
        return result;
    }
    const json cargo = data["cargo"];

    if (!cargo.contains("units")){
        log("ERROR", "transferCargo cargo does not contain units key");
        return result;
    }
    const int units_after_transfer = cargo["units"];

    if (!cargo.contains("capacity")){
        log("ERROR", "transferCargo cargo does not contain capacity key");
        return result;
    }
    const int capacity_after_transfer = cargo["capacity"];


    log("INFO", source_ship_symbol + " | transferCargo "    
        + to_string(units) + " " 
        + trade_symbol + " to " 
        + destination_ship_symbol
        + " ["
        + to_string(units_after_transfer)
        + "/"
        + to_string(capacity_after_transfer)
        + "]");

    return result;
}

json transferAllCargo(const string source_ship_symbol, const string destination_ship_symbol, const json &source_ship_cargo){
    //log("DEBUG", "transferAllCargo");

    const json inventory = source_ship_cargo["inventory"];
	json source_cargo_after_transfer;

    for (json item: inventory){
        const string trade_symbol = item["symbol"];
        const int units = item["units"];
	    const json transfer_result = transferCargo(source_ship_symbol, destination_ship_symbol, trade_symbol, units);
        http_calls++;
        if (transfer_result.contains("error")) {
            const long error_code = transfer_result["error"]["code"];    
            if (error_code == 4217){
                //log("DEBUG", "4217 detected");
                const int destination_capacity = transfer_result["error"]["data"]["cargoCapacity"];
                const int destination_units = transfer_result["error"]["data"]["cargoUnits"];
                const int destination_available_space = destination_capacity - destination_units;

                if (destination_available_space == 0){
                    log("INFO", source_ship_symbol + " | Hauler already full");
                    sendHaulerToMarket(destination_ship_symbol);
                    return source_ship_cargo;
                }

                log("INFO", source_ship_symbol + " | Insufficient space in hauler. Attempting retransfer...");
                const json retransfer_result = transferCargo(source_ship_symbol, 
                                                        destination_ship_symbol, 
                                                        trade_symbol, 
                                                        destination_available_space);

                http_calls++;
                const json source_cargo_after_retransfer = retransfer_result["data"]["cargo"];
                sendHaulerToMarket(destination_ship_symbol);
                return source_cargo_after_retransfer;
            }
        }
		source_cargo_after_transfer = transfer_result["data"]["cargo"];
    }
	//log("INFO", source_ship_symbol + " | Emptied hold to TRANSPORT. Ready to mine, boss.");
	return source_cargo_after_transfer;
}

void updateMarketData(){
    //log("DEBUG", "updateMarketData");

    const json result = getMarket(current_system_symbol, delivery_waypoint_symbol);

    if (result.contains("error")){
        log("ERROR", "updateMarketData result contains error");
        return;
    }

    cached_market_data = result;
    log("INFO", "updateMarketData updated market data");
}

int countShipsByRole(const json &ship_list, const string role){
    int count = 0;
    for (json ship: ship_list){
        string ship_list_role = ship["registration"]["role"];
        if (ship_list_role == role){
            count++;
        }
    }
    return count;
}

// only once this is true should we attempt to fulfill the contract
bool areContractRequirementsMet(const json contract_json){
    if (contract_json["terms"]["deliver"][0]["unitsFulfilled"].is_number_integer() &&
        contract_json["terms"]["deliver"][0]["unitsRequired"].is_number_integer()){
            const int units_fulfilled = contract_json["terms"]["deliver"][0]["unitsFulfilled"];
            const int units_required = contract_json["terms"]["deliver"][0]["unitsRequired"];
            return (units_fulfilled == units_required ? true : false);
    } else {
        log("ERROR", "areContractRequirementsMet json null");
        return false;
    }
}


// ROLES

// ships with this role should go to the asteroid belt and survey for the contract's target resource over and over.
void applyRoleSurveyor(const json &ship_json){

    if (!ship_json["symbol"].is_string()){
        log("ERROR", "applyRoleSurveyor ship_json['symbol'] not a string");
        return;
    }

    const string ship_symbol = ship_json["symbol"];

    // if ship is currently travelling, dont waste any further cycles.
    if (isShipInTransit(ship_json)){
        return;
    }

    if (isShipAtWaypoint(ship_json, asteroid_belt_symbol)){
        // TODO: move this into isShipAtWaypoint
        log("INFO", ship_symbol + " | Already at " + asteroid_belt_symbol);

        createSurvey(ship_symbol);

        scoreSurveysForTargetFarming(target_resource);

        // if we have market data, score the surveys by value
		if (!cached_market_data.is_null()){
			scoreSurveysForProfitability();
		}

        promoteBestSurvey();

       
    } else {
        if (isShipDocked(ship_json)){
            orbitShip(callsign, ship_symbol);
        }
        navigateShip(callsign, ship_symbol, asteroid_belt_symbol);
    }
}


// mining ships should go to the asteroid belt and mine until full
// then they should head to the marketplace, deliver contract goods sell everything else
// once the contract is complete, they should sell everything.
void applyRoleMiner(const json &ship_json){

    if (!ship_json["symbol"].is_string()){
        log("ERROR", "applyRoleMiner ship_json['symbol'] is not a string");
        return;
    }

    const string ship_symbol = ship_json["symbol"];

    json cargo = ship_json["cargo"];

    // if ship is currently travelling, dont waste any further cycles.
    if (isShipInTransit(ship_json)){
        return;
    }

    // if mining ship is not at the asteroid belt. undock and go there.
    if (!isShipAtWaypoint(ship_json, asteroid_belt_symbol)){
        if (isShipDocked(ship_json)){
            orbitShip(callsign, ship_symbol);
            http_calls++;
        }
        navigateShip(callsign, ship_symbol, asteroid_belt_symbol);
        http_calls++;
        return;
    }

    // ship is at asteroid belt.
    if (isShipAtWaypoint(ship_json, asteroid_belt_symbol)){
        // make space for current mining cycle.
        if (isShipCargoHoldAlmostFull(cargo)){
            // is transport ship on site?
            if (isAtLeastOneTransportOnSite()){
				log("INFO", ship_symbol + " | Transport is on site. Attempting transfer...");
                const json transfer_result = transferAllCargo(ship_symbol, firstOnSiteTransport(), cargo);
				cargo = transfer_result;
            }
        }

        // mining while full yields no resources and wastes http calls.
        if (isShipCargoHoldFull(cargo)){
            log("WARN", ship_symbol + " | Cargo full, best not break the mining heads for nothing, boss.");
            return;
        }

        removeExpiredSurveys();

	    promoteBestSurvey();	

        // if there is no survey, we may as well wait.
        if (active_survey.is_null()){
            log("WARN", "No survey no point, boss.");
            return;
        }

        // execute mining operation
        if (isContractFulfilled(target_contract)){
            log("INFO", ship_symbol + " | active_survey.marketValue: " + to_string(active_survey.marketValue));
        } else {
            log("INFO", ship_symbol + " | active_survey.targetResourcePercentage: " + to_string(active_survey.targetResourcePercentage));
        }

        const json result = extractResourcesWithSurvey(ship_symbol, active_survey.jsonObject);

        http_calls++;

        if (result.contains("error")){
            //log("WARN", "extractResourcesWithSurvey returned error, skipping transfer subroutine.");
            return;
        }
        const string extracted_resource_symbol = result["extraction"]["yield"]["symbol"];
        const int extracted_resource_units = result["extraction"]["yield"]["units"];
        cargo = result["cargo"];
                
        // immidiately jettison anything which is not on the resource_keep_list
        if (!isItemWorthKeeping(extracted_resource_symbol)){
            jettisonCargo(callsign, ship_symbol, extracted_resource_symbol, extracted_resource_units);
            http_calls++;
        } else {
            // if theres a transport nearby, we can save a turn by transferring it immidiately.
			if (isAtLeastOneTransportOnSite()){
            	 transferAllCargo(ship_symbol, firstOnSiteTransport(), cargo);
			}
		}
    }
}

void applyRoleShipBuyer(const json &ship_json){

    if (!ship_json["symbol"].is_string()){
        log("ERROR", "applyRoleShipBuyer ship_json['symbol'] is not a string");
        return;
    }

    const string ship_symbol = ship_json["symbol"];

    if (isShipInTransit(ship_json)){
        return;
    }

    if (number_of_satellites < desired_number_of_satellites){
        // buy satellite
        if (isShipAtWaypoint(ship_json, satellite_shipyard_symbol)){
            if (!isShipDocked(ship_json)){
                dockShip(callsign, ship_symbol);
                http_calls++;
            }
            if (isShipAffordable("SHIP_PROBE", satellite_shipyard_symbol)){
                purchaseShip(callsign, "SHIP_PROBE", satellite_shipyard_symbol);
                http_calls++;
            }
            return;
 
        } else {
            // ship is not at satellite_shipyard_symbol
            if (isShipDocked(ship_json)){
                orbitShip(callsign, ship_symbol);
                http_calls++;
            }
            navigateShip(callsign, ship_symbol, satellite_shipyard_symbol);
            http_calls++;
            return;
        }
    }

    if (surveyor_to_miner_ratio < desired_surveyor_to_miner_ratio){
        //buy surveyor ship
        if (isShipAtWaypoint(ship_json, surveyor_ship_shipyard_symbol)){
            if (!isShipDocked(ship_json)){
                dockShip(callsign, ship_symbol);
                http_calls++;
            } 

			if (isShipAffordable("SHIP_SURVEYOR", surveyor_ship_shipyard_symbol)){
            	purchaseShip(callsign, "SHIP_SURVEYOR", surveyor_ship_shipyard_symbol);
                http_calls++;
			}
            return;

        } else {
            if (isShipDocked(ship_json)){
                orbitShip(callsign, ship_symbol);
                http_calls++;
            }
            navigateShip(callsign, ship_symbol, surveyor_ship_shipyard_symbol);
            http_calls++;
            return;
        }
    }

    if (number_of_shuttles < maximum_shuttles){
        // buy shuttle 
        if (isShipAtWaypoint(ship_json, shuttle_ship_shipyard_symbol)){
            if (!isShipDocked(ship_json)){
                dockShip(callsign, ship_symbol);
                http_calls++;
            }

			if (isShipAffordable("SHIP_LIGHT_SHUTTLE", shuttle_ship_shipyard_symbol)){
            	purchaseShip(callsign, "SHIP_LIGHT_SHUTTLE", shuttle_ship_shipyard_symbol);
                http_calls++;
			}
            return;

        } else {
            if (isShipDocked(ship_json)){
                orbitShip(callsign, ship_symbol);
                http_calls++;
            }
            navigateShip(callsign, ship_symbol, shuttle_ship_shipyard_symbol);
            http_calls++;
            return;
        }
    }

    if (hauler_to_miner_ratio < desired_hauler_to_miner_ratio){
        // buy shuttle 
        if (isShipAtWaypoint(ship_json, shuttle_ship_shipyard_symbol)){
            if (!isShipDocked(ship_json)){
                dockShip(callsign, ship_symbol);
                http_calls++;
            }

			if (isShipAffordable("SHIP_LIGHT_HAULER", shuttle_ship_shipyard_symbol)){
            	purchaseShip(callsign, "SHIP_LIGHT_HAULER", shuttle_ship_shipyard_symbol);
                http_calls++;
			}
            return;

        } else {
            if (isShipDocked(ship_json)){
                orbitShip(callsign, ship_symbol);
                http_calls++;
            }
            navigateShip(callsign, ship_symbol, shuttle_ship_shipyard_symbol);
            http_calls++;
            return;
        }
    }

    // only purchase an excavator once we have a shuttle.
    if (number_of_mining_ships < maximum_excavators){
        // buy mining ship
        if (isShipAtWaypoint(ship_json, mining_ship_shipyard_symbol)){
            if (!isShipDocked(ship_json)){
                dockShip(callsign, ship_symbol);
                http_calls++;
            }

             if (isShipAffordable("SHIP_MINING_DRONE", mining_ship_shipyard_symbol)){
                purchaseShip(callsign, "SHIP_MINING_DRONE", mining_ship_shipyard_symbol);
                http_calls++;
             }

        } else {
            if (isShipDocked(ship_json)){
                orbitShip(callsign, ship_symbol);
                http_calls++;
            }
            navigateShip(callsign, ship_symbol, mining_ship_shipyard_symbol);
            http_calls++;
            return;
        }
    }
    log("INFO", ship_symbol + " | Beep Boop, im a SATELLITE");
}

void applyRoleMarketWatcher(const json &ship_json){
    if (!ship_json["symbol"].is_string()){
        log("ERROR", "applyRoleHauler ship_json['symbol'] is not a string");
        return;
    }

    const string ship_symbol = ship_json["symbol"];

    if (isShipInTransit(ship_json)){
        return;
    }

    if (!isShipAtWaypoint(ship_json, delivery_waypoint_symbol)){
        if(isShipDocked(ship_json)){
            orbitShip(callsign, ship_symbol);
        }
        navigateShip(callsign, ship_symbol, delivery_waypoint_symbol);
    } else {
        // ship is at delivery waypoint symbol
        if(!isShipDocked(ship_json)){
            dockShip(callsign, ship_symbol);
        }
        updateMarketData();
        http_calls++;
    }

}

void applyRoleHauler(const json &ship_json){
    //log("DEBUG", "applyRoleHauler");

    if (!ship_json["symbol"].is_string()){
        log("ERROR", "applyRoleHauler ship_json['symbol'] is not a string");
        return;
    }

    const string ship_symbol = ship_json["symbol"];
    const json cargo = ship_json["cargo"];


    // if ship is currently travelling, dont waste any further cycles.
    if (isShipInTransit(ship_json)){
        return;
    }

    // mining ships need some way to know if the transport is present at the same waypoint.
    if (isShipAtWaypoint(ship_json, asteroid_belt_symbol)){
        log("INFO", ship_symbol + " | Ready for transfers, boss.");
		if (!isTransportPresentInOnSiteVector(ship_symbol)){
			transports_on_site.push_back(ship_symbol);
			//log("DEBUG", "pushing " + ship_symbol + " to transports_on_site vector:");
		}
	}

    // once full, leave the asteroid belt and head to the marketplace
    if (isShipAtWaypoint(ship_json, asteroid_belt_symbol) && isShipCargoHoldFull(cargo)){
        log("INFO", ship_symbol + " | Cargo full. Heading to market, boss.");
        navigateShip(callsign, ship_symbol, delivery_waypoint_symbol);
        http_calls++;
		removeTransportFromOnSiteVector(ship_symbol);
        return;
    }

    // ship is at the delivery waypoint
    if (isShipAtWaypoint(ship_json, delivery_waypoint_symbol)){
        // we have something to hand in or sell
        if (!isShipCargoHoldEmpty(cargo)){
            // dock if we arent already
            if (!isShipDocked(ship_json)){
                dockShip(callsign, ship_symbol);
                http_calls++; 
            }

            if (!cached_market_data.is_null()){
			    scoreSurveysForProfitability();
            }
            if (!isContractFulfilled(target_contract)){
                // until the contract is complete, we prioritize delivering its goods.
                // and only sell whats left

                // TODO verify this is an array/object before attempting to assign it.
                const json inventory = ship_json["cargo"]["inventory"];
                int units = cargoCount(inventory, target_resource);
                const json deliver_result = deliverCargoToContract(callsign, target_contract_id, ship_symbol, target_resource, units);
                http_calls++;
                if (deliver_result.contains("error")){
                    log("ERROR", "applyRoleHauler deliverCargoToContract result contains error. exiting role");
                    return;
                }

                // check if the contract can be handed in
                const json contract_after_delivery = deliver_result["contract"];
                if (areContractRequirementsMet(contract_after_delivery)){
                    const json fulfill_result = fulfillContract(callsign, target_contract_id);
                    http_calls++;
                    if (fulfill_result.contains("contract")){
                        target_contract = fulfill_result["contract"];
                    } else {
                        log("ERROR", "applyRoleHauler fulfillContract result does not contain contract key");
                    }
                }

                // sell what remains in the cargo hold after delvering to the contract
                const json post_deliver_inventory = deliver_result["cargo"]["inventory"];
                for (json item: post_deliver_inventory){
                    string cargo_symbol = item["symbol"];
                    units = cargoCount(post_deliver_inventory, cargo_symbol);
                    sellCargo(ship_symbol, cargo_symbol, units);
                    http_calls++;
                }
                // once everything is sold, head back to the belt
                // Only do this if its needed
                refuelShip(callsign, ship_symbol);
                http_calls++;
                orbitShip(callsign, ship_symbol);
                http_calls++;
                navigateShip(callsign, ship_symbol, asteroid_belt_symbol);
                http_calls++;
                return;
            } else {
                int credits_per_cargo_unit = creditsPerCargoUnit(cached_market_data, cargo);
                if (credits_per_cargo_unit < desired_credits_per_cargo_unit){
                    log("INFO", "Credits per cargo unit too low, gonna wait till the price recovers, boss.");
                    return;
                }

                // contract is fulfilled, sell everything
                const json inventory = ship_json["cargo"]["inventory"];
                for (json item: inventory){
                    string cargo_symbol = item["symbol"];
                    int units = cargoCount(inventory, cargo_symbol);
                    int tradeVolume = tradeVolumeOf(cached_market_data, cargo_symbol);
                    if (units >= tradeVolume){
                        sellCargo(ship_symbol, cargo_symbol, tradeVolume);
                        sellCargo(ship_symbol, cargo_symbol, units - tradeVolume);
                    } else {
                        sellCargo(ship_symbol, cargo_symbol, units);
                        http_calls++;
                    }

                }
                // once everything is sold, head back to the belt
                // Only do this if its needed
                refuelShip(callsign, ship_symbol);
                http_calls++;
                orbitShip(callsign, ship_symbol);
                http_calls++;
                navigateShip(callsign, ship_symbol, asteroid_belt_symbol);
                http_calls++;
                return;
            }
        } 
	}
    // catch all. if the ship is somewhere other than asteroid belt or marketplace, move it to the asteroid belt.
    if (!isShipAtWaypoint(ship_json, asteroid_belt_symbol)){
        if (isShipDocked(ship_json)){
            orbitShip(callsign, ship_symbol);
            http_calls++;
        }
        navigateShip(callsign, ship_symbol, asteroid_belt_symbol);
        http_calls++;
    }
}

void applyRoleTransport(const json &ship_json){
    // pretend to be a hauler until we have one
    if (number_of_haulers < 1){
        applyRoleHauler(ship_json);
        return;
    }

    // once we have a hauler, park the shuttle
    const string ship_symbol = ship_json["symbol"];
    log("INFO", ship_symbol + " | We got a hauler, boss. Whatcha want me to do?");

    // make sure shuttle no longer gets loaded by miners
    if (isTransportPresentInOnSiteVector(ship_symbol)){
        removeTransportFromOnSiteVector(ship_symbol);
    }
}

// command ship can both survey and mine, and it is good at both.
// to maximize efficiency, it should do both depending on the quality of the best available survey
void commandShipRoleDecider(const json &ship_json){

    const string ship_symbol = ship_json["symbol"];

    
    // do we have a good survey?
    if (isSurveyGoodEnough(active_survey)){
		log("INFO", ship_symbol +  " | Survey is good enough. Mining...");
        // then lets mine.
        applyRoleMiner(ship_json);

    } else {
    // survey is not good enough. command frigate should survey.
        log("INFO", ship_symbol +  " | Survey is not good enough. Surveying...");
        applyRoleSurveyor(ship_json);
    }
}

void shipRoleApplicator(const json &ship_json){
    if (ship_json.is_null()){
        log("ERROR", "shipRoleApplicator ship_json is null");
        return;
    }
    if (ship_json["registration"]["role"].is_string() && ship_json["symbol"].is_string()){
        const string role = ship_json["registration"]["role"];
        const string ship_symbol = ship_json["symbol"];
        const float ship_condition = ship_json["frame"]["condition"];
        const float ship_integrity = ship_json["frame"]["integrity"];
        const int ship_fuel = ship_json["fuel"]["current"];
        const int ship_fuel_max = ship_json["fuel"]["capacity"];
        const string ship_location = ship_json["nav"]["waypointSymbol"];
        const int cargo_units = ship_json["cargo"]["units"];
        const int cargo_capacity = ship_json["cargo"]["capacity"];

        log("INFO", ship_symbol + " | " + role);
        log("INFO", ship_symbol + " | Condition: " 
                                + to_string(ship_condition) 
                                + " Integrity: " 
                                + to_string(ship_integrity));

        log("INFO", ship_symbol + " | Fuel: [" + to_string(ship_fuel) + "/" + to_string(ship_fuel_max) + "]");
        log("INFO", ship_symbol + " | Cargo: [" + to_string(cargo_units) + "/" + to_string(cargo_capacity) + "]");
        log("INFO", ship_symbol + " | Location: " + ship_location);


        if (role == "COMMAND"){
            commandShipRoleDecider(ship_json);
            return;
        }
        if (role == "SATELLITE"){
            if (!buyer_satellite_exists){
                applyRoleShipBuyer(ship_json);
                buyer_satellite_exists = true;
                return;
            }
            applyRoleMarketWatcher(ship_json);
        }
        if (role == "EXCAVATOR"){
            applyRoleMiner(ship_json);
            return;
        }
        if (role == "SURVEYOR"){
            applyRoleSurveyor(ship_json);
            return;
        }

        // we run getShip on these two roles because their cargo holds can be modified out of turn
        // this therefore ensures their cargo reports and locations/status are accurate when their roles are applied
        if (role == "TRANSPORT"){
            const json transport_ship_json = getShip(callsign, ship_symbol);
            http_calls++;
            applyRoleTransport(transport_ship_json);
            return;
        }
        if (role == "HAULER"){
            const json transport_ship_json = getShip(callsign, ship_symbol);
            http_calls++;
            applyRoleHauler(transport_ship_json);
            return;
        }
    }
}


int main(int argc, char* argv[])
{
    // once, at the start of the run.

    if (argc != 3){
        log("ERROR", "./miningfleet CALLSIGN FACTION");
        exit(1);
    }

    callsign = argv[1];
    const string faction = argv[2];
    registerAgent(callsign, faction);
    initializeGlobals();
   
    // attempting to accept an already accepted contract with throw http non-20X 
    if (!hasContractBeenAccepted(target_contract)){
        acceptContract(callsign, target_contract_id);
    }

    int turn_number = 0;

    // AD HOC TESTING


    // every turn...
    while (true){
        turn_number++;
        log("INFO", "Turn " + to_string(turn_number));

        json ships = listShips(callsign);

        int number_of_ships = ships.size();
        log("INFO", "Number of ships: " + to_string(number_of_ships));

        int delay_between_ships = turn_length / number_of_ships;

        number_of_satellites = countShipsByRole(ships, "SATELLITE");
        number_of_surveyor_ships = countShipsByRole(ships, "SURVEYOR");
        number_of_shuttles = countShipsByRole(ships, "TRANSPORT");
        number_of_mining_ships = countShipsByRole(ships, "EXCAVATOR");
        number_of_haulers = countShipsByRole(ships, "HAULER");


        // COMMAND ship counts as two mining ships.
        number_of_mining_ships = number_of_mining_ships + 2;

        // these ratios are used to decide the order ships are purchased in
        hauler_to_miner_ratio = number_of_haulers / number_of_mining_ships; 
        surveyor_to_miner_ratio = number_of_surveyor_ships / number_of_mining_ships;


        for (json ship : ships){
            string ship_symbol = ship["symbol"];
            shipRoleApplicator(ship);
            cout << endl;
            sleep(delay_between_ships);
        }

        // reset this to false so that first satellite becomes buyer next turn
        buyer_satellite_exists = false;

        int calls_per_minute = http_calls / 1.5;

        log("INFO", "HTTP Calls: " + to_string(calls_per_minute) + "/m");

        // this is arbitary but avoids most cooldown issues, and is easier on the server.
        // eventually ships should pass their cooldown into this.
        //sleep(120);
        cout << endl;
        http_calls = 0;

    }

    // there is currently no curl easy cleanup. i think there needs to be exactly 1. 
    // so right now my inclination is to curl easy init once globally instead of in each http_ function.
    curl_global_cleanup();
    return 0;
}
