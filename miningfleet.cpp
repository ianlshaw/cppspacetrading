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

const json null_json = {};

json contracts_list;
json target_contract;
string target_resource;
string target_contract_id;
string asteroid_belt_symbol;
string delivery_waypoint_symbol;
float survey_score_threshold = 0.3;    // command frigate uses this to decide its role
vector <string> resource_keep_list;    // storage for cargoSymbols. everything else gets jettisoned

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
    float score;        // Used to prioritize which survey is used to mine 
    json surveyObject;  // Json object for an individual survey
};

vector <survey> surveys; // Storage for the surveys we will create
json best_survey;        // Json object for the survey with the highest score

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
   
        /* Perform the request, res gets the return code */
        res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

        /* Check for errors */
        if(res != CURLE_OK){
          fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }

        // parse the response body into a json object.
        json output_as_json = json::parse(*httpData);

        if (httpCode != 200 && httpCode != 201){
            cout << "http_get error: " << endl;
            printJson(output_as_json);
        }

        //cout << "[DEBUG] curl easy reset" << endl; 
        curl_easy_reset(curl);
        return output_as_json;
    }
    return null_json;
}

// libcurl wrapper. payload parameter is optional
json http_post(const string endpoint, const json payload = {}){
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
   
       /* Perform the request, res gets the return code */
       res = curl_easy_perform(curl);

       /* Check for curl level errors */
       if(res != CURLE_OK){
           fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
       }
         
       // retrieve the http response code
       curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
       
       // parse the response body into a json object.
       json output_as_json = json::parse(*httpData);

       // print entire response body when http return code is non-20X
       if (httpCode != 200 && httpCode != 201){
           cout << "http post error" << endl;
           printJson(output_as_json);
       }

       // always reset curl handle after use   
       curl_easy_reset(curl);

       // return the whole object and let other methods deal with it
       return output_as_json; 
     }
    return null_json;
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

json getShip(const string ship_symbol){
    return http_get("https://api.spacetraders.io/v2/my/ships/" + ship_symbol);
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

string findWaypointByType(const string systemSymbol, const string type){
    json result = http_get("https://api.spacetraders.io/v2/systems/" + systemSymbol + "/waypoints?type=" + type);
    if (!result["data"][0]["symbol"].is_string()){
        cout << "[ERROR] findWaypointByType['data'][0]['symbol'] not string" << endl;
    }
    return result["data"][0]["symbol"];
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
    cout << "[INFO] target_resource = " + target_resource << endl;
    cout << "[INFO] survey_score_threshold: " << survey_score_threshold << endl;
    cout << "[INFO] contract_fulfilled = " << target_contract["fulfilled"] << endl;
    json first_ship = getShip(callsign + "-1");
    string system_symbol = first_ship["data"]["nav"]["systemSymbol"];
    asteroid_belt_symbol = findWaypointByType(system_symbol, "ENGINEERED_ASTEROID"); 
    delivery_waypoint_symbol = target_contract["terms"]["deliver"][0]["destinationSymbol"];


    json get_market_result = getMarket(system_symbol, delivery_waypoint_symbol);
    json imports = get_market_result["data"]["imports"];
    for (json an_import : imports){
        string import_symbol = an_import["symbol"];
        resource_keep_list.push_back(import_symbol);
    }
}

bool isShipDocked(const json ship_json){
    if (ship_json["data"]["nav"]["status"].is_string()) {
        return (ship_json["data"]["nav"]["status"] == "DOCKED" ? true : false);
    } else {
        cout << "[ERROR] isShipDocked json null" << endl;
        return false;
    }
}

bool isShipInTransit(const json ship_json){
    if (ship_json["data"]["nav"]["status"].is_string()) { 
        return (ship_json["data"]["nav"]["status"] == "IN_TRANSIT" ? true : false);
    } else {
        cout << "[ERROR] isShipInTransit json null" << endl;
        return false;
    }
}

bool isShipInOrbit(const json ship_json){
    if (ship_json["data"]["nav"]["status"].is_string()) { 
        return (ship_json["data"]["nav"]["status"] == "IN_ORBIT" ? true : false);
    } else {
        cout << "[ERROR] isShipInOrbit ship_json['data']['nav']['status'] not string" << endl;
        return false;
    }
}

void orbitShip(const string ship_symbol){
    //cout << "[DEBUG] orbitShip " << ship_symbol << endl;
    const json result = http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/orbit");
    cout << "[INFO] " << ship_symbol << " " << result["data"]["nav"]["status"] << endl;
}

void dockShip(const string ship_symbol){
    //cout << "[DEBUG] dockShip " + ship_symbol << endl;
    const json result = http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/dock");
    cout << "[INFO] " << ship_symbol << " " << result["data"]["nav"]["status"] << endl;
}

void navigateShip(const string ship_symbol, const string waypoint_symbol){
    //cout << "[DEBUG] navigateShip " + ship_symbol + " to " + waypoint_symbol << endl;
    json payload;
    payload["waypointSymbol"] = waypoint_symbol;
    const json result = http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/navigate", payload);
    const json nav = result["data"]["nav"];
    const json status = nav["status"];
    const json origin_symbol = nav["route"]["origin"]["symbol"];
    const json destination_symbol = nav["route"]["destination"]["symbol"];
    cout << "[INFO] " << ship_symbol << " " << status 
         << " from " << origin_symbol << " to " << destination_symbol << endl;
}

void createSurvey(const string ship_symbol){
    json result = http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/survey");
    
    for (json individual_survey : result["data"]["surveys"]){
        float hits = 0;
        for (json deposit : individual_survey["deposits"]){
            string resource = deposit["symbol"];
            if (resource == target_resource){
                hits++;
            }
        }

        float deposit_size = individual_survey["deposits"].size();

        survey a_survey;
        float score = hits / deposit_size;
        cout << "[INFO] Survey result: " << score << endl;
        a_survey.score = score;
        a_survey.surveyObject = individual_survey;
        surveys.push_back(a_survey);
    }
}

bool isShipCargoHoldFull(const json ship_json){
    if (!ship_json["data"]["cargo"]["units"].is_number_integer()){
        cout << "[ERROR] isShipCargoHoldFull ship_json['data']['cargo']['units'] not integer" << endl;
        return false;
    }
    if (!ship_json["data"]["cargo"]["capacity"].is_number_integer()){
         cout << "[ERROR] isShipCargoHoldFull ship_json['data']['cargo']['capacity'] not integer" << endl;
         return false;
    }
    return (ship_json["data"]["cargo"]["units"] == ship_json["data"]["cargo"]["capacity"] ? true : false);
}

bool isShipCargoHoldEmpty(const json ship_json){
    if (!ship_json["data"]["cargo"]["units"].is_number_integer()){
        cout << "[ERROR] isShipCargoHoldEmpty ship_json['data']['cargo']['units'] not integer" << endl;
        return false;
    }
    return (ship_json["data"]["cargo"]["units"] == 0 ? true : false);
}

bool isShipAtWaypoint(const json ship_json, string waypoint_symbol){
    //cout << "[DEBUG] isShipAtWaypoint" << endl;
    if (!ship_json["data"]["nav"]["waypointSymbol"].is_string()){
        cout << "[ERROR] ship_json['data']['nav']['waypointSymbol'] not string" << endl;
    }
    return (ship_json["data"]["nav"]["waypointSymbol"] == waypoint_symbol ? true : false);
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

    return (seconds_until_expiry <= 5 ? true : false);
}

void removeExpiredSurveys(){
    //cout << "[DEBUG] removeExpiredSurveys" << endl;
    int vector_index = 0;
    while(vector_index < surveys.size()){
        survey each_survey = surveys.at(vector_index);
        json each_survey_object = each_survey.surveyObject;
        if (isSurveyExpired(each_survey_object)){
            //const string signature = each_survey_object["signature"];
            const float survey_score = each_survey.score;
            cout << "[INFO] Erasing expired survey " << "with score: " << survey_score << endl;
            //cout << "[INFO] Erasing expired survey " << signature << " with score: " << survey_score << endl;
            surveys.erase(surveys.begin() + vector_index);
            //cout << "[DEBUG] after surveys.erase" << endl;
        } else {
            vector_index++;
        }
    }
    //cout << "[DEBUG] removeExpiredSurveys AFTER WHILE LOOP" << endl;
}

// ships with this role should go to the asteroid belt and survey for the contract's target resource over and over.
void applyRoleSurveyor(const string ship_symbol){

    json ship_json = getShip(ship_symbol);
    if (isShipAtWaypoint(ship_json, asteroid_belt_symbol)){
        cout << "[INFO] " + ship_symbol + " is already on site at asteroid belt" << endl;
        createSurvey(ship_symbol);
    } else {
        if (isShipDocked(ship_json))
            orbitShip(ship_symbol);
        navigateShip(ship_symbol, asteroid_belt_symbol);
    }
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

    cout << "[INFO] Jettisoning " 
         << units 
         << " units of " 
         << cargo_symbol 
         << endl;

    json payload;
    payload["symbol"] = cargo_symbol;
    payload["units"] = units;
    const json result = http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/jettison", payload);

    const json stored_cargo = result["data"]["cargo"]["units"];
    const json capacity = result["data"]["cargo"]["capacity"];

    cout << "[INFO] Jettisoned " 
         << "["
         << stored_cargo 
         << "/"
         << capacity 
         << "]"
         << endl;
}

json extractResourcesWithSurvey(const string ship_symbol, const json target_survey){
    //cout << "[DEBUG] extractResourcesWithSurvey" << endl;
    const json result = http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/extract/survey", target_survey);
    const string extracted_resource_symbol = result["data"]["extraction"]["yield"]["symbol"];
    const int extracted_resource_units = result["data"]["extraction"]["yield"]["units"];
    const json cargo = result["data"]["cargo"];
    const int capacity = cargo["capacity"];
    const int units = cargo["units"];
    cout << "[INFO] Extracted " 
         << extracted_resource_units 
         << " units of " 
         << extracted_resource_symbol 
         << " [" 
         << units 
         << "/" 
         << capacity 
         << "]" 
         << endl;
    return result;
}

void refuelShip(const string ship_symbol){
    //cout << "[DEBUG] refuelShip " + ship_symbol << endl;
    const json result = http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/refuel");
    const json transaction = result["data"]["transaction"];
    cout << "[INFO] " << ship_symbol  << " refuelled costing " << transaction["totalPrice"] << endl;
}

int howMuchOfCargoDoesShipHaveInCargoHold(const json inventory, const string cargo_symbol){
    //cout << "[DEBUG] howMuchOfCargoDoesShipHaveInCargoHold " + cargo_symbol << endl;
    
    for (json item: inventory){
        if (item["symbol"] == cargo_symbol){
            //cout << "[DEBUG] " << item["units"] << " of " << cargo_symbol << endl;
            return item["units"];
        }
    }
    return 0;
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
    int units_fulfilled = result["data"]["contract"]["terms"]["deliver"][0]["unitsFulfilled"];
    int units_required = result["data"]["contract"]["terms"]["deliver"][0]["unitsRequired"];

    cout << "[INFO] " << ship_symbol << " Delivered " << units << " of " << trade_symbol 
         << " [" << units_fulfilled << "/" << units_required << "]" << endl;

    return result;
}

// until this is true, we do not want to sell the target_resource
bool isContractFulfilled(const json contract_json){
    //cout << "[DEBUG] isContractFulfilled" << endl;
    if (contract_json["fulfilled"].is_boolean()){
        return contract_json["fulfilled"];
    }
    cout << "[ERROR] isContractFulfilled json null" << endl;
    return false;
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
        cout << "[ERROR] areContractRequirementsMet json null" << endl;
        return false;
    }
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
    cout << "[INFO] " << ship_symbol << " Sold " << units_sold 
         << " " << trade_symbol << " for " << total_price << endl;
}

// mining ships should go to the asteroid belt and mine until full
// then they should head to the marketplace, deliver contract goods sell everything else
// once the contract is complete, they should sell everything.
void applyRoleMiner(const string ship_symbol){
    cout << "[INFO] " << ship_symbol << " applyRoleMiner" << endl;

    // get ship state
    json ship_json = getShip(ship_symbol);

    // if ship is currently travelling, dont waste any further cycles.
    if (isShipInTransit(ship_json)){
        return;
    }

    if (isShipAtWaypoint(ship_json, delivery_waypoint_symbol)){
        // ship is at the delivery waypoint
        if (isShipCargoHoldEmpty(ship_json)){
            // and its cargo hold is empty
            if (isShipDocked(ship_json)){
                orbitShip(ship_symbol);
            }
            // go to the asteroid belt
            navigateShip(ship_symbol, asteroid_belt_symbol);
        } else {
            // ship is at delivery waypoint, and its cargo hold is not empty.
            if (isShipInOrbit(ship_json)){
                dockShip(ship_symbol);
            }
            refuelShip(ship_symbol);

            if (!isContractFulfilled(target_contract)){
                // until the contract is complete, we prioritize delivering its goods.
                // and only sell whats left
                const json inventory = ship_json["data"]["cargo"]["inventory"];
                int units = howMuchOfCargoDoesShipHaveInCargoHold(inventory, target_resource);
                const json deliver_result = deliverCargoToContract(target_contract_id, ship_symbol, target_resource, units);

                // check if the contract can be handed in
                const json contract_after_delivery = deliver_result["data"]["contract"];
                if (areContractRequirementsMet(contract_after_delivery)){
                    fulfillContract(target_contract_id);
                }

                // sell what remains in the cargo hold after delvering to the contract
                const json post_deliver_inventory = deliver_result["data"]["cargo"]["inventory"];
                for (json item: post_deliver_inventory){
                    string cargo_symbol = item["symbol"];
                    int units = howMuchOfCargoDoesShipHaveInCargoHold(post_deliver_inventory, cargo_symbol);
                    sellCargo(ship_symbol, cargo_symbol, units);
                }
            } else {
                // contract is fulfilled, sell everything
                const json inventory = ship_json["data"]["cargo"]["inventory"];
                for (json item: inventory){
                    string cargo_symbol = item["symbol"];
                    int units = howMuchOfCargoDoesShipHaveInCargoHold(inventory, cargo_symbol);
                    sellCargo(ship_symbol, cargo_symbol, units);
                }
            }
        }
    }

    if(isShipAtWaypoint(ship_json, asteroid_belt_symbol)){
        // ship is at asteroid belt.
        if(isShipCargoHoldFull(ship_json)){
            // and its cargo hold is full
            cout << "[INFO] " + ship_symbol + " cargo hold full. Heading to delivery waypoint." << endl;
            // goto MARKETPLACE
            navigateShip(ship_symbol, delivery_waypoint_symbol);
        } else {
            // ship is at asteroid belt, but its cargo hold is not full
            // so we mine.
            json result = extractResourcesWithSurvey(ship_symbol, best_survey);
            const string extracted_resource_symbol = result["data"]["extraction"]["yield"]["symbol"];
            const int extracted_resource_units = result["data"]["extraction"]["yield"]["units"];
                    
            // immidiately jettison anything which is not on the resource_keep_list
            if (!isItemWorthKeeping(extracted_resource_symbol)){
                jettisonCargo(ship_symbol, extracted_resource_symbol, extracted_resource_units);
            }
        }
    }
}

float bestSurveyScore(){
    float best_survey_score = 0.0;
    for(survey item: surveys){
        if (item.score > best_survey_score){
            best_survey_score = item.score;
            // probably should set this elsewhere. or just rename the function
            best_survey = item.surveyObject;
        }
    }
    cout << "[INFO] Best survey score: " << best_survey_score << endl;
    return best_survey_score;
}

// this is used to decide if the command frigate will survey or mine.
// behaviour can be tuned via survey_score_threshold global
bool isSurveyGoodEnough(){
    return (bestSurveyScore() >= survey_score_threshold ? true : false);
}

// command ship can both survey and mine, and it is good at both.
// to maximize efficiency, it should do both depending on the quality of the best available survey
void commandShipRoleDecider(const string ship_symbol){

    // do we have a good survey?
    if (isSurveyGoodEnough()){
        cout << "[INFO] Survey is good enough" << endl;
        // then lets mine.
        applyRoleMiner(ship_symbol);

    } else {
    // survey is not good enough. command frigate should survey.
        cout << "[INFO] Survey is not good enough" << endl;
        applyRoleSurveyor(ship_symbol);
    }
}

bool hasContractBeenAccepted(const json contract_json){
    if (contract_json["accepted"].is_boolean()){
        return contract_json["accepted"];
    }
    return false;
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

    // every turn...
    while (true){
        turn_number++;
        cout << "[INFO] Turn " << turn_number << endl;

        // remove expired surveys
        if (!isSurveyListEmpty()){
            removeExpiredSurveys();
            //cout << "[DEBUG] AFTER removeExpiredSurveys()" << endl;
        }

        // command ship can fulfill several roles, and so we have to decide
        commandShipRoleDecider(callsign + "-1");

        // this is arbitary but avoids most cooldown issues, and is easier on the server.
        sleep(120);
        cout << endl;
    }

    // there is currently no curl easy cleanup. i think there needs to be exactly 1. 
    // so right now my inclination is to curl easy init once globally instead of in each http_ function.
    curl_global_cleanup();
    return 0;
}
