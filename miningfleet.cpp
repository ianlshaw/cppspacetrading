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

using json = nlohmann::json;

using namespace std;

string callsign;

json error_json = {{"error", "default"}};
int http_calls = 0;

const int max_retries = 5;
const int retry_delay = 30;
const int turn_length = 120;

const int desired_number_of_surveyor_ships = 5;
const int desired_number_of_mining_ships = 1;
const int desired_number_of_shuttle_ships = 1;

int number_of_surveyor_ships = 0;
int number_of_mining_ships = 0;
int number_of_shuttle_ships = 0;
bool transport_is_on_site = false;

int credits = 0;

// lazy
string transport_ship_symbol;

string system_symbol;
json contracts_list;
json system_waypoints_list;
json target_contract;
string target_resource;
string target_contract_id;
string asteroid_belt_symbol;
string delivery_waypoint_symbol;
string mining_ship_shipyard_symbol;
string surveyor_ship_shipyard_symbol;
string shuttle_ship_shipyard_symbol;
float survey_score_threshold = 0.3;    // command frigate uses this to decide its role
vector <string> resource_keep_list;    // storage for cargoSymbols. everything else gets jettisoned
json market_data;

// this is needed by libcurl to retrieve data from the HTTP responses the server will send us
namespace
{
    std::size_t callback(
            const char* in,
            std::size_t size,
            std::size_t num,
            std::string* out)
    {
        const std::size_t totalBytes(size * num);
        out->append(in, totalBytes);
        return totalBytes;
    }
}

// we want to be able to 
class survey {       
  public:           
    json surveyObject;  // Json object for an individual survey
    int marketValue = 0;
    float targetResourcePercentage = 0.0;        // Used to prioritize which survey is used to mine 
};

vector <survey> surveys; // Storage for the surveys we will create
json best_survey;        // Json object for the survey with the highest score
float best_survey_score = 0.0;

// when printing an entire json object is required, this makes it easier on the eyes
void printJson(json jsonObject){
    int indent = 4;
    string pretty_json = jsonObject.dump(indent);
    cout << pretty_json;
}

// auth token file will only exist after first run
bool doesAuthFileExist(const string& authTokenFile) {
    ifstream auth_token_file_stream(authTokenFile.c_str());
    return auth_token_file_stream.good();
}

// Register Agent returns an auth token we want to keep safe 
void writeAuthTokenToFile(const string token){
     ofstream myfile;
     string filename = callsign + ".token";
     cout << "[INFO] Wrote auth file to: " << filename << endl;
     myfile.open (filename);
     myfile << token;
     myfile.close();
}

// a corresponding *.token in .gitignore is a simple way to avoid leaking a callsigns authentication token
string readAuthTokenFromFile(const string authTokenFile){
    string myText;
    string wholeDocument;
    ifstream MyReadFile(authTokenFile);
    while (getline (MyReadFile, myText)) {
      wholeDocument = wholeDocument + myText;
    }
    MyReadFile.close();
    return wholeDocument;
}

string timestamp(){
    time_t t = time(nullptr);
    tm tm = *gmtime(&t);
    stringstream ss;
	ss << put_time(&tm, "%Y-%m-%dT%H:%M:%S");
	return ss.str();
}

void log(const string level, const string message){
    cout << timestamp() << " [" << level << "] " << message << endl;
}

void report_cargo(const json &cargo){
    const int units = cargo["units"]; 
    const int capacity = cargo["capacity"];
    log("INFO", "Cargo [" + to_string(units) + "/" + to_string(capacity) + "]"); 
}

// wrapper to make libcurl usable enough for what we need
json http_get(const string endpoint){
    //cout << "[INFO] Sending GET request to " << endpoint << endl;
    CURL *curl;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_ALL);

    /* get a curl handle */
    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        string auth_token_header = "Authorization: Bearer " + readAuthTokenFromFile(callsign + ".token");
        headers = curl_slist_append(headers, auth_token_header.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        long httpCode(0);
        std::unique_ptr<std::string> httpData(new std::string());

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, httpData.get());
   
        for (int i = 1; i <= max_retries &&
            (res = curl_easy_perform(curl)) != CURLE_OK; i++) {
                cout << "[WARN] HTTP error, retrying..." << endl;
                sleep(retry_delay);
        }
        
        /* Check for errors */
        if(res != CURLE_OK){
          fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }

        // populate httpCode for later use
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        
        http_calls++;

        // parse the response body into a json object.
        json output_as_json = json::parse(*httpData);

        if (httpCode != 200 && httpCode != 201){
            cout << "http_get error: " << endl;
            printJson(output_as_json);
        }

        //cout << "[DEBUG] curl easy reset" << endl; 
        curl_easy_reset(curl);
        curl_easy_cleanup(curl);
        return output_as_json;
    }
    curl_easy_cleanup(curl);

    return error_json;
}

// libcurl wrapper. payload parameter is optional
json http_post(const string endpoint, const json payload = {}){
    // if payload is null here, bail. since you will at best get some garbage json in return
    if (payload == error_json){
        cout << "[ERROR] http_post bailed because bad payload was provided" << endl;
        return error_json;
    }

    //cout << "[INFO] Sending POST request to " << endpoint << endl;

    // initialize libcurl
    CURL *curl;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_ALL);

    /* get a curl handle */
    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
        //curl_easy_setopt(curl, CURLOPT_POST, 1L);
        
        /* ask libcurl to show us the verbose output */
        //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

       // POSTFIELDS 
       if (payload.empty()){
           //cout << "[DEBUG] payload is empty" << endl;
           curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
       } else {
           //cout << "[DEBUG] payload is not empty" << endl;
           string payload_as_string = payload.dump();
           //cout << payload_as_string.c_str() << endl;
           //long payloadLength = strlen(payload_as_string.c_str());
           //cout << payloadLength << endl;
           const char* payload_as_c_string = payload_as_string.c_str();
           //curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload_as_c_string);
           curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, payload_as_c_string);

           //curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payloadLength);
       }

       // construct the headers according to libcurl
       struct curl_slist *headers = NULL;

       // everybody needs a Content-Type header
       headers = curl_slist_append(headers, "Content-Type: application/json");

       // only set bearer if auth file exists. this is to support initial register agent
       if (doesAuthFileExist(callsign + ".token")){
           string auth_token_header = "Authorization: Bearer " + readAuthTokenFromFile(callsign + ".token");
           headers = curl_slist_append(headers, auth_token_header.c_str());
       }

       // provide curl handle its carefully crafted headers
       curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

       // this will definitely become 20X 
       long httpCode(0);

       // prepare a container for returned data
       std::unique_ptr<std::string> httpData(new std::string());

       // Hook up data handling function.
       curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);
       curl_easy_setopt(curl, CURLOPT_WRITEDATA, httpData.get());
   
       for (int i = 1; i <= max_retries &&
           (res = curl_easy_perform(curl)) != CURLE_OK; i++) {
               cout << "[WARN] HTTP error, retrying..." << endl;
               sleep(retry_delay);
       }

       /* Check for curl level errors */
       if(res != CURLE_OK){
           fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
       }

       http_calls++;
         
       // retrieve the http response code
       curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
       
       // parse the response body into a json object.
       json output_as_json = json::parse(*httpData);

       // print entire response body when http return code is non-20X
       if (httpCode != 200 && httpCode != 201){
           cout << "[WARN] http_post() returned non-20X" << endl;
           cout << "payload" << endl;
           cout << payload << endl;
           cout << "output_as_json" << endl;
           printJson(output_as_json);
       }

       // always reset curl handle after use   
       curl_easy_reset(curl);
       curl_easy_cleanup(curl);

       // return the whole object and let other methods deal with it
       return output_as_json; 
     }
    curl_easy_cleanup(curl);
    return error_json;
}

void update_credits(const int new_credits){
    credits = new_credits;
	log("INFO", "Balance: " + to_string(credits));
}

bool haveEnoughCredits(const int price){
    return(credits > price ? true : false);
}

void registerAgent(const string callsign, const string faction) {

    // we don't want to ever accidently overwrite a .token file
    if (doesAuthFileExist(callsign + ".token")) {
        return;
    }

    cout << "[INFO] Attempting to register new agent " << callsign << endl;

    json register_agent_json_object = {};
    register_agent_json_object["symbol"] = callsign;
    register_agent_json_object["faction"] = faction;
    json result = http_post("https://api.spacetraders.io/v2/register", register_agent_json_object);
    writeAuthTokenToFile(result["data"]["token"]);
}

json getAgent(){
	return(http_get("https://api.spacetraders.io/v2/my/agent"));
}

json listShips(){
    const json result = http_get("https://api.spacetraders.io/v2/my/ships");
    return result["data"];
}

json getShip(const string ship_symbol){
    const json result = http_get("https://api.spacetraders.io/v2/my/ships/" + ship_symbol);
    return result["data"];
}

json listWaypointsInSystem(const string system_symbol){
    return http_get("https://api.spacetraders.io/v2/systems/" + system_symbol + "/waypoints");
}

string shipSymbolFromJson(const json ship_json){
    if (ship_json["data"]["symbol"].is_string()){
        return ship_json["data"]["symbol"];
    }
    cout << "[ERROR] shipSymbolFromJson ship_json['data']['symbol'] not string" << endl;
    return "";
}

json getContract(const string contract_id){
    return http_get("https://api.spacetraders.io/v2/my/contracts/" + contract_id);
}

json listContracts(){
    return http_get("https://api.spacetraders.io/v2/my/contracts");
}

void acceptContract(const string contractId) {
    cout << "[DEBUG] Attempting to accept contract " + contractId << endl;
    http_post("https://api.spacetraders.io/v2/my/contracts/" + contractId + "/accept");
}

bool hasContractBeenAccepted(const json contract_json){
    if (contract_json["accepted"].is_boolean()){
        return contract_json["accepted"];
    } 
    return false;
}

string findWaypointByType(const string systemSymbol, const string type){
    json result = http_get("https://api.spacetraders.io/v2/systems/" + systemSymbol + "/waypoints?type=" + type);
    if (!result["data"][0]["symbol"].is_string()){
        cout << "[ERROR] findWaypointByType['data'][0]['symbol'] not string" << endl;
        return "ERROR findWaypointByType";
    }
    return result["data"][0]["symbol"];
}

json findWaypointsByTrait(const string system_symbol, const string trait){
    json result = http_get("https://api.spacetraders.io/v2/systems/" + system_symbol + "/waypoints?traits=" + trait);
    if (result["data"].is_null()){
        cout << "[ERROR] findWaypointsByType['data'] is null" << endl;
        return error_json;
    }
    return result["data"];
}

json getShipyard(const string system_symbol, const string waypoint_symbol){
    json result = http_get("https://api.spacetraders.io/v2/systems/" + system_symbol + "/waypoints/" + waypoint_symbol + "/shipyard");
    if (result["data"].is_null()){
        cout << "[ERROR] getShipyard['data'] is null" << endl;
        return error_json;
    }
    return result["data"];
}

string findShipyardByShipType(const string ship_type){
    log("INFO", "findShipyardByShipType " + ship_type);
    json shipyard_waypoints = findWaypointsByTrait(system_symbol, "SHIPYARD"); 

    for (json waypoint : shipyard_waypoints) {
        //cout << "[DEBUG] for (json waypoint : shipyard_waypoints)" << endl;
        string waypoint_symbol = waypoint["symbol"];
        json shipyard = getShipyard(system_symbol, waypoint_symbol);
        json ship_types = shipyard["shipTypes"];
        for (json available_ship_type : ship_types){
            //cout << "[DEBUG] for (json available_ship_type : ship_types)" << endl;
            string type = available_ship_type["type"];
            if (type == ship_type){
                //cout << "[DEBUG] (type == ship_type)" << endl;
                return waypoint_symbol;
            }
        }
    }
    return "ERROR findShipyardByShipType";
}

int howMuchDoesShipCost(const string ship_type, const string shipyard_waypoint_symbol){
	const json get_shipyard_result = getShipyard(system_symbol, shipyard_waypoint_symbol);
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
	const json agent = getAgent();
	update_credits(agent["data"]["credits"]);
	
	int ship_price = howMuchDoesShipCost(ship_type, shipyard_symbol);
	
	if (haveEnoughCredits(ship_price)){
	    return true;
	}  
	log("WARN", "Cannot afford to purchase " + ship_type + " from " + shipyard_symbol + " you are too poor, boss.");
	return false;
}

json getMarket(const string system_symbol, const string waypoint_symbol){
    return http_get("https://api.spacetraders.io/v2/systems/" + system_symbol + "/waypoints/" + waypoint_symbol + "/market");
}

void initializeGlobals(){
    contracts_list = listContracts();
    // this next one is potentially an error. since the item target_contract holds right now is the first in the result
    // of the list contracts method, NOT the result of a get contract against the contract ID.
    // i should check what is the difference.
    target_contract = contracts_list["data"][0];
    target_resource = target_contract["terms"]["deliver"][0]["tradeSymbol"];
    target_contract_id = target_contract["id"];
    log("INFO", "target_resource = " + target_resource);
    log("INFO", "survey_score_threshold: " + to_string(survey_score_threshold));
    log("INFO", "contract_fulfilled = " + to_string(target_contract["fulfilled"]));
    json first_ship = getShip(callsign + "-1");
    system_symbol = first_ship["nav"]["systemSymbol"];
    system_waypoints_list = listWaypointsInSystem(system_symbol);
    asteroid_belt_symbol = findWaypointByType(system_symbol, "ENGINEERED_ASTEROID"); 
    delivery_waypoint_symbol = target_contract["terms"]["deliver"][0]["destinationSymbol"];
    surveyor_ship_shipyard_symbol = findShipyardByShipType("SHIP_SURVEYOR");
    mining_ship_shipyard_symbol = findShipyardByShipType("SHIP_MINING_DRONE");
    shuttle_ship_shipyard_symbol = findShipyardByShipType("SHIP_LIGHT_SHUTTLE");


    json get_market_result = getMarket(system_symbol, delivery_waypoint_symbol);
    json imports = get_market_result["data"]["imports"];
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
        cout << "[ERROR] isShipDocked ship_json['nav]['status'] is not string" << endl;
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
		log("INFO", ship_symbol + " | " + role + " | " + status); 
    	return true;
    } 
	return false;
}

bool isShipInOrbit(const json &ship_json){
    if (ship_json["nav"]["status"].is_string()) { 
        return (ship_json["nav"]["status"] == "IN_ORBIT" ? true : false);
    } else {
        cout << "[ERROR] isShipInOrbit ship_json['nav']['status'] not string" << endl;
        return false;
    }
}

void orbitShip(const string ship_symbol){
    //cout << "[DEBUG] orbitShip " << ship_symbol << endl;
    const json result = http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/orbit");
    const string status = result["data"]["nav"]["status"];
    log("INFO", status);
}

void dockShip(const string ship_symbol){
    //cout << "[DEBUG] dockShip " + ship_symbol << endl;
    const json result = http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/dock");
    const string status = result["data"]["nav"]["status"];
    log("INFO", status);
}

void navigateShip(const string ship_symbol, const string waypoint_symbol){
    //cout << "[DEBUG] navigateShip " + ship_symbol + " to " + waypoint_symbol << endl;
    json payload;
    payload["waypointSymbol"] = waypoint_symbol;
    const json result = http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/navigate", payload);
    const json nav = result["data"]["nav"];
    const string status = nav["status"];
    const string origin_symbol = nav["route"]["origin"]["symbol"];
    const string destination_symbol = nav["route"]["destination"]["symbol"];
    log("INFO",  status + " from " + origin_symbol + " to " + destination_symbol);
}

void createSurvey(const string ship_symbol){
    //log("DEBUG", "createSurvey");

    json result = http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/survey");

	log("INFO", "createSurvey");
    
    for (json each_survey: result["data"]["surveys"]){
        survey a_survey;
        a_survey.surveyObject = each_survey;
        surveys.push_back(a_survey);
    }
}

// this is used to decide if the command frigate will survey or mine.
// behaviour can be tuned via survey_score_threshold global
bool isSurveyGoodEnough(){
    return (best_survey_score >= survey_score_threshold ? true : false);
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

bool isShipCargoHoldEmpty(const json &cargo){
    if (!cargo["units"].is_number_integer()){
		log("ERROR", "isShipCargoHoldEmpty cargo['units'] not integer");
        return false;
    }
    return (cargo["units"] == 0 ? true : false);
}

bool isShipAtWaypoint(const json &ship_json, string waypoint_symbol){
    //cout << "[DEBUG] isShipAtWaypoint" << endl;
    if (!ship_json["nav"]["waypointSymbol"].is_string()){
        cout << "[ERROR] ship_json['nav']['waypointSymbol'] not string" << endl;
    }
    return (ship_json["nav"]["waypointSymbol"] == waypoint_symbol ? true : false);
}

bool isSurveyListEmpty(){
    int number_of_surveys = surveys.size();
    return (number_of_surveys == 0 ? true : false);
}

bool isSurveyExpired(const json survey){
    //cout << "[DEBUG] isSurveyExpired" << endl;

    const string expiration = survey["expiration"];
    //cout << "[DEBUG] expiration string: " << expiration << endl;

    tm expiration_time_handle{};
    istringstream expiration_string_stream(expiration);
    
    // scan the json string using a hardcoded format
    expiration_string_stream >> get_time(&expiration_time_handle, "%Y-%m-%dT%H:%M:%S");

    if (expiration_string_stream.fail()) {
        throw runtime_error{"failed to parse time string"};
    }   

    time_t expiration_timestamp = mktime(&expiration_time_handle);
    //cout << "[DEBUG] expiration_timestamp: " << expiration_timestamp << endl;

    // time now
    time_t current_timestamp = time(NULL);

    // convert time now to utc
    tm* utc_time_now = gmtime(&current_timestamp);

    // convert it back into a time_t for subsequent comparison
    time_t utc_time_now_timestamp = mktime(utc_time_now);
    //cout << "[DEBUG] utc_time_now_timestamp = " << utc_time_now_timestamp << endl;

    // find the difference between the utc time now and surveys expiration
    double seconds_until_expiry = difftime(expiration_timestamp, utc_time_now_timestamp);
    //cout << "[DEBUG] Seconds until expiry: " << seconds_until_expiry << endl;

    return (seconds_until_expiry <= 30 ? true : false);
}

void promoteBestSurveyForTargetFarming(){
    //log("DEBUG", "promoteBestSurveyForTargetFarming");

    for (survey item: surveys){
        if (item.targetResourcePercentage > best_survey_score){
            best_survey_score = item.targetResourcePercentage;
            best_survey = item.surveyObject;
            log("INFO", "New best survey. Score = " + to_string(best_survey_score));
        }
    }
}

void promoteBestSurveyForProfitability(){
	//log("DEBUG", "promoteBestSurveyForProfitability");

	for (survey item: surveys){
		if (item.marketValue > best_survey_score){
			best_survey_score = item.marketValue;
			best_survey = item.surveyObject;
			log("INFO", "New best survey. Score = " + to_string(best_survey_score));
		}
	}
}

int priceCheck(const string good_to_check){

    //log("DEBUG", "priceCheck " + good_to_check);

    // market data will only be available after the transport has delivered/sold at least once.
    if (market_data.is_null()){
        cout << "[ERROR] priceCheck before market data is in. returning 0" << endl;
        return 0;
    }

    json trade_goods = market_data["tradeGoods"];
    for (json market_good: trade_goods){
        if (good_to_check == market_good["symbol"]){ 
            //log("DEBUG", to_string(market_good["sellPrice"]));
            return market_good["sellPrice"];
        }
    }
    return 0;
}

void scoreSurveysForTargetFarming(const string trade_good){

	//log("DEBUG", "scoreSurveysForTargetFarming " + trade_good);

    int index = 0;
    for (survey item: surveys){
        float hits = 0; 
        const json deposits = item.surveyObject["deposits"];
        const float total_deposits = deposits.size();
        for (json deposit: deposits){
            const string symbol = deposit["symbol"];
            if (symbol == trade_good){
                hits++;
            }
        }

        //cout << "[DEBUG] hits = " << hits << endl;
        //cout << "[DEBUG] total_deposits = " << total_deposits << endl;

        const float target_resource_percentage = hits / total_deposits;

        //cout << "[DEBUG] target_resource_percentage = " << target_resource_percentage << endl;

        surveys.at(index).targetResourcePercentage = target_resource_percentage;
        index++;
    }
}

void scoreSurveysForProfitability(){

	//log("DEBUG", "scoreSurveysForProfitability");

	if (market_data.is_null()){
		log("ERROR", "market_data is null");
	}

	int index = 0;
    for (survey item: surveys){
        int sum_of_deposit_prices = 0;
        json survey_object = item.surveyObject;
        json deposits = survey_object["deposits"];
        int number_of_deposits = deposits.size();
        for (json deposit: deposits){
            string deposit_symbol = deposit["symbol"];
            int value = priceCheck(deposit_symbol);
			sum_of_deposit_prices = sum_of_deposit_prices + value;
        }

		int value_average = sum_of_deposit_prices / number_of_deposits;

		surveys.at(index).marketValue = value_average;
		index++;
    } 
}

void resetBestSurvey(){
	best_survey_score = 0.0;
	best_survey = {};
}

void removeExpiredSurveys(){
    //cout << "[DEBUG] removeExpiredSurveys" << endl;
    int vector_index = 0;
    while(vector_index < surveys.size()){
        survey each_survey = surveys.at(vector_index);
        json each_survey_object = each_survey.surveyObject;
        if (isSurveyExpired(each_survey_object)){
            log("INFO", "Erasing expired survey");
            surveys.erase(surveys.begin() + vector_index);
            if (each_survey_object == best_survey){
            	log("INFO", "Best survey erased! Updating best survey...");
				resetBestSurvey();
            }
          
        } else {
            vector_index++;
        }
    }
}

// until this is true, we do not want to sell the target_resource
bool isContractFulfilled(const json &contract_json){
    //cout << "[DEBUG] isContractFulfilled" << endl;
    if (contract_json["fulfilled"].is_boolean()){
        return contract_json["fulfilled"];
    }
    log("ERROR", "isContractFulfilled json null");
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

// this is how we throw away useless goods obtained by mining
void jettisonCargo(const string ship_symbol, const string cargo_symbol, const int units){

	//log("DEBUG", "Jettisoning " + to_string(units) + " units of " + cargo_symbol);

    json payload;
    payload["symbol"] = cargo_symbol;
    payload["units"] = units;

    const json result = http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/jettison", payload);

    const json cargo = result["data"]["cargo"];

    log("INFO", "Jettisoned " + cargo_symbol);
    report_cargo(cargo);
}


json extractResourcesWithSurvey(const string ship_symbol, const json target_survey){

    //log("DEBUG", "extractResourcesWithSurvey");

    const json result = http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/extract/survey", target_survey);
    const string extracted_resource_symbol = result["data"]["extraction"]["yield"]["symbol"];
    const int extracted_resource_units = result["data"]["extraction"]["yield"]["units"];
    const json cargo = result["data"]["cargo"];

    log("INFO", "Extracted " + to_string(extracted_resource_units) + " units of " + extracted_resource_symbol);  
    report_cargo(cargo);

    return result["data"];
}

void refuelShip(const string ship_symbol){
    //cout << "[DEBUG] refuelShip " + ship_symbol << endl;
    const json result = http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/refuel");
    const json transaction = result["data"]["transaction"];
    log("INFO", "Refuelled costing " + to_string(transaction["totalPrice"]));
}

int cargoCount(const json inventory, const string cargo_symbol){
    //cout << "[DEBUG] cargoCount " + cargo_symbol << endl;
    
    for (json item: inventory){
        if (item["symbol"] == cargo_symbol){
            //cout << "[DEBUG] " << item["units"] << " of " << cargo_symbol << endl;
            return item["units"];
        }
    }
    return 0;
}

int cargoRemaining(const json &cargo){
	const int units = cargo["units"];
	const int capacity = cargo["capacity"];
    return capacity - units;
}

json fulfillContract(const string contract_id){
    cout << "[DEBUG] fulfillContract" << endl;
    json result = http_post("https://api.spacetraders.io/v2/my/contracts/" + contract_id + "/fulfill");
    // update the global target_contract after it is fulfilled.
    target_contract = result["data"]["contract"];
    printJson(result);
    return result;
}

json deliverCargoToContract(const string contract_id, const string ship_symbol, const string trade_symbol, const int units){
    //cout << "[DEBUG] deliverCargoToContract " << endl; 
    json payload;
    payload["shipSymbol"] = ship_symbol;
    payload["tradeSymbol"] = trade_symbol;
    payload["units"] = units;
    json result = http_post("https://api.spacetraders.io/v2/my/contracts/" + contract_id + "/deliver", payload);

    if (!result["data"]["contract"]["terms"]["deliver"][0]["unitsFulfilled"].is_number_integer()){
        cout << "[ERROR] deliverCargoToContract result['data']['contract']['terms']['deliver'][0]['unitsFulfilled'] " <<
                "is not an integer" << endl;
        return error_json;
    }

    if (!result["data"]["contract"]["terms"]["deliver"][0]["unitsRequired"].is_number_integer()){
        cout << "[ERROR] deliverCargoToContract result['data']['contract']['terms']['deliver'][0]['unitsRequired'] " <<
                " is not an integer" << endl;
    }

    int units_fulfilled = result["data"]["contract"]["terms"]["deliver"][0]["unitsFulfilled"];
    int units_required = result["data"]["contract"]["terms"]["deliver"][0]["unitsRequired"];

    log("INFO", "Delivered " + to_string(units) + " of " + trade_symbol + " [" + to_string(units_fulfilled) + "/" +
    to_string(units_required) + "]");

    return result["data"];
}


void sellCargo(const string ship_symbol, const string cargo_symbol, const int units){
    //cout << "[DEBUG] sellCargo " << ship_symbol << " " << cargo_symbol << " " << units << endl;
    json payload;
    payload["symbol"] = cargo_symbol;
    payload["units"] = units;
    json result = http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/sell", payload);
    const json transaction = result["data"]["transaction"];
    const int units_sold = transaction["units"];
    const string trade_symbol = transaction["tradeSymbol"];
    const int total_price = transaction["totalPrice"];

    log("INFO", "Sold " + to_string(units_sold) + " " + trade_symbol + " for " + to_string(total_price));

    const int new_balance = result["data"]["agent"]["credits"];
    update_credits(new_balance);
}

void purchaseShip(const string ship_type, const string waypoint_symbol){
    json payload;
    payload["shipType"] = ship_type;
    payload["waypointSymbol"] = waypoint_symbol;
    const json result = http_post("https://api.spacetraders.io/v2/my/ships", payload);
    if (result["data"].is_null()){
        return;
    }
    const int price = result["data"]["transaction"]["price"];
    const int balance = result["data"]["agent"]["credits"];
    const string role = result["data"]["ship"]["registration"]["role"];
    log("INFO", "Purchased " + role + " for " + to_string(price) + " new balance: " + to_string(balance));
}

json transferCargo(const string source_ship_symbol, const string destination_ship_symbol, const string trade_symbol, const int units){

	log("INFO", "transferCargo " + source_ship_symbol + " " + destination_ship_symbol + " " + trade_symbol + " " + to_string(units));

    json payload;
    payload["tradeSymbol"] = trade_symbol;
    payload["units"] = units;
    payload["shipSymbol"] = destination_ship_symbol;
    const json result = http_post("https://api.spacetraders.io/v2/my/ships/" + source_ship_symbol + "/transfer", payload);
    const json cargo = result["data"]["cargo"];
    report_cargo(cargo);
    return result;
}

json transferAllCargo(const string source_ship_symbol, const string destination_ship_symbol, const json &source_ship_cargo){
    //log("DEBUG", "transferAllCargo");

    const json destination_ship_json = getShip(destination_ship_symbol);
	const json destination_ship_cargo = destination_ship_json["cargo"];
	int destination_remaining_space = cargoRemaining(destination_ship_cargo);

	if (destination_remaining_space == 0){
		log("INFO", "Transport is already full, boss.");
		return source_ship_cargo;
	}

    const json inventory = source_ship_cargo["inventory"];
	json source_cargo_after_transfer;

    for (json item: inventory){
        const string trade_symbol = item["symbol"];
        const int units = item["units"];
		if (units == destination_remaining_space){
			log("INFO", "TRANSPORT Full");
			const json transfer_result = transferCargo(source_ship_symbol, destination_ship_symbol, trade_symbol, units);
			source_cargo_after_transfer = transfer_result["data"]["cargo"];
			return source_cargo_after_transfer;
		}
		if (units > destination_remaining_space){
			log("INFO", "TRANSPORT only has " + to_string(destination_remaining_space) + " space remaining, boss.");
			const json transfer_result = transferCargo(source_ship_symbol, destination_ship_symbol, trade_symbol, destination_remaining_space);
            source_cargo_after_transfer = transfer_result["data"]["cargo"];
			return source_cargo_after_transfer;
		}
		if (units < destination_remaining_space){
        	const json transfer_result = transferCargo(source_ship_symbol, destination_ship_symbol, trade_symbol, units);
			source_cargo_after_transfer = transfer_result["data"]["cargo"];
			destination_remaining_space = destination_remaining_space - units;
		}
    }
	log("INFO", "Emptied hold to TRANSPORT. Ready to mine, boss.");
	return source_cargo_after_transfer;
}

void updateMarketData(){
    log("INFO", "updateMarketData");

    const json result = getMarket(system_symbol, delivery_waypoint_symbol);
    market_data = result["data"];
}

string getTransportShipSymbol(const json &ship_list){
    for (json ship: ship_list){
        string ship_list_role = ship["registration"]["role"];
        if (ship_list_role == "TRANSPORT"){
            return ship["symbol"];
        }
    }
    return "error string";
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
    //cout << "[DEBUG] areContractRequirementsMet" << endl;
    //printJson(contract_json);
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
    //cout << "[DEBUG] applyRoleSurveyor" << endl;

    if (!ship_json["symbol"].is_string()){
        cout << "[ERROR] applyRoleSurveyor ship_json['symbol'] not a string" << endl;
        return;
    }

    const string ship_symbol = ship_json["symbol"];

    // if ship is currently travelling, dont waste any further cycles.
    if (isShipInTransit(ship_json)){
        return;
    }

    //cout << "[DEBUG] ship_symbol = " << ship_symbol << endl;

    if (isShipAtWaypoint(ship_json, asteroid_belt_symbol)){
        // TODO: move this into isShipAtWaypoint
        log("INFO", "Already at " + asteroid_belt_symbol);

        createSurvey(ship_symbol);

        scoreSurveysForTargetFarming(target_resource);

        // if we have market data, score the surveys by value
		if (!market_data.is_null()){
			scoreSurveysForProfitability();
		}

		if (isContractFulfilled(target_contract) && !market_data.is_null()){
            promoteBestSurveyForProfitability();
		} else {
			// by default we farm for target_resource
        	promoteBestSurveyForTargetFarming();
		}

       
    } else {
        if (isShipDocked(ship_json)){
            orbitShip(ship_symbol);
        }
        navigateShip(ship_symbol, asteroid_belt_symbol);
    }
}


// mining ships should go to the asteroid belt and mine until full
// then they should head to the marketplace, deliver contract goods sell everything else
// once the contract is complete, they should sell everything.
void applyRoleMiner(const json &ship_json){
    //cout << "[DEBUG] applyRoleMiner" << endl;

    if (!ship_json["symbol"].is_string()){
        cout << "[ERROR] applyRoleMiner ship_json['symbol'] is not a string" << endl;
        return;
    }

    const string ship_symbol = ship_json["symbol"];

    json cargo = ship_json["cargo"];

    //cout << "[DEBUG] " << ship_symbol << " applyRoleMiner" << endl;

    // if ship is currently travelling, dont waste any further cycles.
    if (isShipInTransit(ship_json)){
        return;
    }

    // if mining ship is not at the asteroid belt. undock and go there.
    if (!isShipAtWaypoint(ship_json, asteroid_belt_symbol)){
        if (isShipDocked(ship_json)){
            orbitShip(ship_symbol);
        }
        navigateShip(ship_symbol, asteroid_belt_symbol);
        return;
    }

    // ship is at asteroid belt.
    if (isShipAtWaypoint(ship_json, asteroid_belt_symbol)){
        // make space for current mining cycle.
        if (!isShipCargoHoldEmpty(cargo)){
            // is transport ship on site?
            if (transport_is_on_site){
				log("INFO", "Transport is on site. Attempting transfer...");
                const json transfer_result = transferAllCargo(ship_symbol, transport_ship_symbol, cargo);
				cargo = transfer_result;
            }
        }

        // mining while full yields no resources and wastes http calls.
        if (isShipCargoHoldFull(cargo)){
            log("WARN", ship_symbol + " | Cargo full, best not break the mining heads for nothing, boss.");
            return;
        }

        removeExpiredSurveys();

		if (isContractFulfilled(target_contract) && !market_data.is_null()){
            promoteBestSurveyForProfitability();
		} else {
			// by default we farm for target_resource
        	promoteBestSurveyForTargetFarming();
		}

        // if there is no survey, we may as well wait.
        if (best_survey.is_null()){
            log("WARN", "No survey no point, boss.");
            return;
        }

        // execute mining operation
        const json result = extractResourcesWithSurvey(ship_symbol, best_survey);
        const string extracted_resource_symbol = result["extraction"]["yield"]["symbol"];
        const int extracted_resource_units = result["extraction"]["yield"]["units"];
        json cargo = result["cargo"];
                
        // immidiately jettison anything which is not on the resource_keep_list
        if (!isItemWorthKeeping(extracted_resource_symbol)){
            jettisonCargo(ship_symbol, extracted_resource_symbol, extracted_resource_units);
        } else {
            // if theres a transport nearby, we can save a turn by transferring it immidiately.
			if (transport_is_on_site){
            	 transferAllCargo(ship_symbol, transport_ship_symbol, cargo);
			}
		}
    }
}

void applyRoleSatellite(const json &ship_json){

    const string ship_symbol = ship_json["symbol"];

    if (isShipInTransit(ship_json)){
        return;
    }

    if (number_of_surveyor_ships < desired_number_of_surveyor_ships){
        //buy surveyor ship
        if (isShipAtWaypoint(ship_json, surveyor_ship_shipyard_symbol)){
            if (!isShipDocked(ship_json)){
                dockShip(ship_symbol);
            } 

			if (isShipAffordable("SHIP_SURVEYOR", surveyor_ship_shipyard_symbol)){
            	purchaseShip("SHIP_SURVEYOR", surveyor_ship_shipyard_symbol);
			}

        } else {
            if (isShipDocked(ship_json)){
                orbitShip(ship_symbol);
            }
            navigateShip(ship_symbol, surveyor_ship_shipyard_symbol);
            return;
        }
    }

    if (number_of_shuttle_ships < desired_number_of_shuttle_ships){
        // buy shuttle 
        if (isShipAtWaypoint(ship_json, shuttle_ship_shipyard_symbol)){
            if (!isShipDocked(ship_json)){
                dockShip(ship_symbol);
            }

			if (isShipAffordable("SHIP_LIGHT_SHUTTLE", shuttle_ship_shipyard_symbol)){
            	purchaseShip("SHIP_LIGHT_SHUTTLE", shuttle_ship_shipyard_symbol);
			}

        } else {
            if (isShipDocked(ship_json)){
                orbitShip(ship_symbol);
            }
            navigateShip(ship_symbol, shuttle_ship_shipyard_symbol);
            return;
        }
    }

    // only purchase an excavator once we have a shuttle.
    if (number_of_mining_ships < desired_number_of_mining_ships && number_of_shuttle_ships >= desired_number_of_shuttle_ships){
        // buy mining ship
        if (isShipAtWaypoint(ship_json, mining_ship_shipyard_symbol)){
            if (!isShipDocked(ship_json)){
                dockShip(ship_symbol);
            }

             if (isShipAffordable("SHIP_MINING_DRONE", mining_ship_shipyard_symbol)){
                 purchaseShip("SHIP_MINING_DRONE", mining_ship_shipyard_symbol);
             }

        } else {
            if (isShipDocked(ship_json)){
                orbitShip(ship_symbol);
            }
            navigateShip(ship_symbol, mining_ship_shipyard_symbol);
            return;
        }
    }
    log("INFO", "Beep Boop, im a SATELLITE");
}

void applyRoleTransport(const json &ship_json){
    //log("DEBUG", "applyRoleTransport");

    if (!ship_json["symbol"].is_string()){
        cout << "[ERROR] applyRoleTransport ship_json['symbol'] is not a string" << endl;
        return;
    }

    const string ship_symbol = ship_json["symbol"];
    const json cargo = ship_json["cargo"];

    report_cargo(cargo);

    // if ship is currently travelling, dont waste any further cycles.
    if (isShipInTransit(ship_json)){
        return;
    }

    // mining ships need some way to know if the transport is present at the same waypoint.
    if (isShipAtWaypoint(ship_json, asteroid_belt_symbol)){
        log("INFO", "Transport on site, boss.");
        transport_is_on_site = true; 
    } else {
        transport_is_on_site = false;
    }

    // once full, leave the asteroid belt and head to the marketplace
    if (isShipAtWaypoint(ship_json, asteroid_belt_symbol) && isShipCargoHoldFull(cargo)){
        log("INFO", "Cargo full. Heading to market, boss.");
        navigateShip(ship_symbol, delivery_waypoint_symbol);
        transport_is_on_site = false;
        return;
    }

    // ship is at the delivery waypoint
    if (isShipAtWaypoint(ship_json, delivery_waypoint_symbol)){
        // we have something to hand in or sell
        if (!isShipCargoHoldEmpty(cargo)){
            // dock if we arent already
            if (!isShipDocked(ship_json)){
                dockShip(ship_symbol); 
            }
            refuelShip(ship_symbol);
            updateMarketData();
			scoreSurveysForProfitability();
            if (!isContractFulfilled(target_contract)){
                // until the contract is complete, we prioritize delivering its goods.
                // and only sell whats left

                // TODO verify this is an array/object before attempting to assign it.
                const json inventory = ship_json["cargo"]["inventory"];
                int units = cargoCount(inventory, target_resource);
                const json deliver_result = deliverCargoToContract(target_contract_id, ship_symbol, target_resource, units);

                // check if the contract can be handed in
                const json contract_after_delivery = deliver_result["contract"];
                if (areContractRequirementsMet(contract_after_delivery)){
                    fulfillContract(target_contract_id);
                }

                // sell what remains in the cargo hold after delvering to the contract
                const json post_deliver_inventory = deliver_result["cargo"]["inventory"];
                for (json item: post_deliver_inventory){
                    string cargo_symbol = item["symbol"];
                    int units = cargoCount(post_deliver_inventory, cargo_symbol);
                    sellCargo(ship_symbol, cargo_symbol, units);
                }
                // once everything is sold, head back to the belt
                orbitShip(ship_symbol);
                navigateShip(ship_symbol, asteroid_belt_symbol);
                return;
            } else {
                // contract is fulfilled, sell everything
                const json inventory = ship_json["cargo"]["inventory"];
                for (json item: inventory){
                    string cargo_symbol = item["symbol"];
                    int units = cargoCount(inventory, cargo_symbol);
                    sellCargo(ship_symbol, cargo_symbol, units);
                }
                // once everything is sold, head back to the belt
                orbitShip(ship_symbol);
                navigateShip(ship_symbol, asteroid_belt_symbol);
                return;
            }
        } 
	}
    // catch all. if the ship is somewhere other than asteroid belt or marketplace, move it to the asteroid belt.
    if (!isShipAtWaypoint(ship_json, asteroid_belt_symbol)){
        if (isShipDocked(ship_json)){
            orbitShip(ship_symbol);
        }
        navigateShip(ship_symbol, asteroid_belt_symbol);
    }
}

// command ship can both survey and mine, and it is good at both.
// to maximize efficiency, it should do both depending on the quality of the best available survey
void commandShipRoleDecider(const json &ship_json){

    const string ship_symbol = ship_json["symbol"];

    // do we have a good survey?
    if (isSurveyGoodEnough()){
		log("INFO", "Survey is good enough. Mining...");
        // then lets mine.
        applyRoleMiner(ship_json);

    } else {
    // survey is not good enough. command frigate should survey.
        log("INFO", "Survey is not good enough. Surveying...");
        applyRoleSurveyor(ship_json);
    }
}

void shipRoleApplicator(const json &ship_json){
    //cout << "[DEBUG] shipRoleApplicator" << endl;
    if (ship_json.is_null()){
        cout << "[ERROR] shipRoleApplicator ship_json is null" << endl;
        return;
    }
    if (ship_json["registration"]["role"].is_string() && ship_json["symbol"].is_string()){
        const string role = ship_json["registration"]["role"];
        const string ship_symbol = ship_json["symbol"];

        if (role == "COMMAND"){
            log("INFO", ship_symbol + " | " + role);
            commandShipRoleDecider(ship_json);
            return;
        }
        if (role == "SATELLITE"){
            log("INFO", ship_symbol + " | " + role);
            applyRoleSatellite(ship_json);
            return;
        }
        if (role == "EXCAVATOR"){
			log("INFO", ship_symbol + " | " + role);
            applyRoleMiner(ship_json);
            return;
        }
        if (role == "SURVEYOR"){
			log("INFO", ship_symbol + " | " + role);
            applyRoleSurveyor(ship_json);
        }
        if (role == "TRANSPORT"){
			log("INFO", ship_symbol + " | " + role);
            applyRoleTransport(ship_json);
        }
    }
}


int main(int argc, char* argv[])
{
    // once, at the start of the run.

    if (argc != 3){
        cout << "./miningfleet CALLSIGN FACTION" << endl;
        exit(1);
    }

    callsign = argv[1];
    const string faction = argv[2];
    registerAgent(callsign, faction);
    initializeGlobals();
   
    // attempting to accept an already accepted contract with throw http non-20X 
    if (!hasContractBeenAccepted(target_contract)){
        acceptContract(target_contract_id);
    }

    int turn_number = 0;

    // AD HOC TESTING


    // every turn...
    while (true){
        turn_number++;
        log("INFO", "Turn " + to_string(turn_number));

        json ships = listShips();

        int number_of_ships = ships.size();
        int delay_between_ships = turn_length / number_of_ships;

        number_of_surveyor_ships = countShipsByRole(ships, "SURVEYOR");
        number_of_mining_ships = countShipsByRole(ships, "EXCAVATOR");
        number_of_shuttle_ships = countShipsByRole(ships, "TRANSPORT");

        transport_ship_symbol = getTransportShipSymbol(ships);

        for (json ship : ships){
            string ship_symbol = ship["symbol"];
            json get_ship_result = getShip(ship_symbol);
            shipRoleApplicator(get_ship_result);
            cout << endl;
            sleep(delay_between_ships);
        }

        log("INFO", "HTTP Calls: " + to_string(http_calls));

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
