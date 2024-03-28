#include <stdio.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <sstream> 
#include <ctime>
#include <unistd.h>   
#include "json.hpp"

using json = nlohmann::json;

using namespace std;

string callsign;

json contracts_list;
json target_contract;
string target_resource;
string target_contract_id;
string asteroid_belt_symbol;
string delivery_waypoint_symbol;
float survey_score_threshold = 0.1;
vector <string> resource_keep_list;

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

class survey {       // The class
  public:             // Access specifier
    float score;        // Attribute (int variable)
    json surveyObject;  // Attribute (string variable)
};

vector <survey> surveys;
json best_survey;

void print_json(json jsonObject){
    int indent = 4;
    string pretty_json = jsonObject.dump(indent);
    cout << pretty_json;
}

bool does_auth_file_exist(const string& authTokenFile) {
    //cout << "[INFO] Checking for existence of " << authTokenFile << endl;
    if (std::__fs::filesystem::exists(authTokenFile)){
        //cout << "[INFO] Auth file found\n";
        return true;
    } else {
        //cout << "[INFO] Auth file not found\n";
        return false;
    }
}

string read_auth_token_from_file(const string authTokenFile){
    string myText;
    string wholeDocument;
    ifstream MyReadFile(authTokenFile);
    while (getline (MyReadFile, myText)) {
      wholeDocument = wholeDocument + myText;
    }
    MyReadFile.close();
    return wholeDocument;
}

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
     //    cout << payload_as_string.c_str() << endl;
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
       if (does_auth_file_exist(callsign + ".token")){
           string auth_token_header = "Authorization: Bearer " + read_auth_token_from_file(callsign + ".token");
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
       if(res != CURLE_OK)
         fprintf(stderr, "curl_easy_perform() failed: %s\n",
                 curl_easy_strerror(res));


       curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
       
       // parse the response body into a json object.
       json output_as_json = json::parse(*httpData);

       if (httpCode != 200 && httpCode != 201){
           cout << "http post error" << endl;
           print_json(output_as_json);
       }

       // always reset curl handle after use   
       curl_easy_reset(curl);

       return output_as_json; 
     }
    json null_json = {};
    return null_json;
}

void write_auth_token_to_file(const string token){
     ofstream myfile;
     string filename = callsign + ".token";
     cout << "[INFO] Wrote auth file to: " << filename << endl;
     myfile.open (filename);
     myfile << token;
     myfile.close();
}

void registerAgent(const string callsign) {

    if (does_auth_file_exist(callsign + ".token")) {
        return;
    }

    cout << "[INFO] Attempting to register new agent " << callsign << endl;
    cout << "Enter faction name" << endl;
    string faction;
    cin >> faction;

    json register_agent_json_object = {};
    register_agent_json_object["symbol"] = callsign;
    register_agent_json_object["faction"] = faction;
    json result = http_post("https://api.spacetraders.io/v2/register", register_agent_json_object);
    write_auth_token_to_file(result["data"]["token"]);
}

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
        string auth_token_header = "Authorization: Bearer " + read_auth_token_from_file(callsign + ".token");
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
        if(res != CURLE_OK)
          fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));

        // parse the response body into a json object.
        json output_as_json = json::parse(*httpData);

        if (httpCode != 200 && httpCode != 201){
            cout << "http_get error: " << endl;
            print_json(output_as_json);
        }

        //cout << "[DEBUG] curl easy reset" << endl; 
        curl_easy_reset(curl);
        return output_as_json;
    }
    json null_json = {};
    return null_json;
}

json getShip(const string ship_symbol){
    string endpoint = "https://api.spacetraders.io/v2/my/ships/" + ship_symbol;
    json result = http_get(endpoint);
    return result;
}

string shipSymbolFromJson(const json ship_json){
    return ship_json["data"]["symbol"];
}

json getContract(const string contract_id){
    return http_get("https://api.spacetraders.io/v2/my/contracts/" + contract_id);
}

json listContracts(){
   json result = http_get("https://api.spacetraders.io/v2/my/contracts");
   return result;
}

void acceptContract(const string contractId) {
    cout << "[INFO] Attempting to accept contract " + contractId << endl;
    string endpoint = "https://api.spacetraders.io/v2/my/contracts/" + contractId + "/accept";
    json result = http_post(endpoint);
}

void useful_but_not_yet(){
//    if (httpCode != 201) {
//      cout << "!!! ERROR !!!" << endl;
//      cout << "HTTP RETURN CODE:" << endl;
//      cout << httpCode << endl;
//      cout << "HTTP BODY:" << endl;
//      json output_as_json = json::parse(*httpData);
//      int indent = 4;
//      string pretty_json = output_as_json.dump(indent);
//      cout << pretty_json;
//      exit(1);
//    }
}

string find_waypoint_by_type(const string systemSymbol, const string type){
    string endpoint = "https://api.spacetraders.io/v2/systems/" + systemSymbol + "/waypoints?type=" + type;
    json result = http_get(endpoint);
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
    cout << "[INFO] Target resource is: " + target_resource << endl;
    cout << "[INFO] Contract fulfilled? " << target_contract["fulfilled"] << endl;
    json first_ship = getShip(callsign + "-1");
    string system_symbol = first_ship["data"]["nav"]["systemSymbol"];
    asteroid_belt_symbol = find_waypoint_by_type(system_symbol, "ENGINEERED_ASTEROID"); 
    delivery_waypoint_symbol = target_contract["terms"]["deliver"][0]["destinationSymbol"];


    json get_market_result = getMarket(system_symbol, delivery_waypoint_symbol);
    json imports = get_market_result["data"]["imports"];
    for (json an_import : imports){
        string import_symbol = an_import["symbol"];
        resource_keep_list.push_back(import_symbol);
    }
}

bool isShipDocked(const json ship_json){
    return (ship_json["data"]["nav"]["status"] == "DOCKED" ? true : false);
}

bool isShipInTransit(const json ship_json){
    return (ship_json["data"]["nav"]["status"] == "IN_TRANSIT" ? true : false);
}

bool isShipInOrbit(const json ship_json){
    return (ship_json["data"]["nav"]["status"] == "IN_ORBIT" ? true : false);
}

void orbitShip(const string ship_symbol){
    cout << "[INFO] orbitShip " << ship_symbol << endl;
    http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/orbit");
}

void dockShip(const string ship_symbol){
    //cout << "[DEBUG] dockShip " + ship_symbol << endl;
    const json result = http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/dock");
    cout << "[INFO] " << ship_symbol << result["data"]["nav"]["status"] << endl;
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


bool isShipCargoHoldFull(const json shipJson){
    return (shipJson["data"]["cargo"]["units"] == shipJson["data"]["cargo"]["capacity"] ? true : false);
}

bool isShipCargoHoldEmpty(const json shipJson){
    return (shipJson["data"]["cargo"]["units"] == 0 ? true : false);
}

bool isShipAtWaypoint(const json ship_json, string waypoint_symbol){
    //cout << "[DEBUG] isShipAtWaypoint" << endl;
    return (ship_json["data"]["nav"]["waypointSymbol"] == waypoint_symbol ? true : false);
}

bool isSurveyListEmpty(){
    int number_of_surveys = surveys.size();
    return (number_of_surveys == 0 ? true : false);
}

bool isSurveyExpired(const json survey){
    //cout << "[DEBUG] isSurveyExpired" << endl;

    const string expiration = survey["expiration"];
    cout << "[DEBUG] expiration string: " << expiration << endl;

    tm expiration_time_handle{};
    istringstream expiration_string_stream(expiration);
    
    expiration_string_stream >> get_time(&expiration_time_handle, "%Y-%m-%dT%H:%M:%S");

    if (expiration_string_stream.fail()) {
        throw runtime_error{"failed to parse time string"};
    }   

    time_t expiration_time_stamp = mktime(&expiration_time_handle);
    cout << "[DEBUG] expiration_time_stamp: " << expiration_time_stamp << endl;

    time_t current_time_stamp = time(NULL);

    // time now
    tm* utc_time_now = gmtime(&current_time_stamp);
  
    time_t utc_time_now_timestamp = mktime(utc_time_now);

    cout << "[DEBUG] utc_time_now_timestamp = " << utc_time_now_timestamp << endl;

    cout << "[DEBUG] utc_time_now: " << asctime(utc_time_now);

    //double time_difference = difftime(current_time_stamp, expiration_time_stamp);
    double seconds_until_expiry = difftime(expiration_time_stamp, utc_time_now_timestamp);

    cout << "[DEBUG] difference between expiration and now: " << seconds_until_expiry << endl;

    return (seconds_until_expiry <= 5 ? true : false);
}

void removeExpiredSurveys(){
    cout << "[DEBUG] removeExpiredSurveys" << endl;
    int vector_index = 0;
    for (survey each_survey: surveys){
        const json each_survey_object = each_survey.surveyObject;
        if (isSurveyExpired(each_survey_object)){
            cout << "[DEBUG] SURVEY IS EXPIRED" << endl;
            surveys.erase(surveys.begin() + vector_index);
        }
        vector_index++;
    }
}

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

bool isItemWorthKeeping(const string item){
    for(string resource: resource_keep_list){
        if (item == resource){
            return true;
        }
    }
    return false;
}

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

int howMuchOfCargoDoesShipHaveInCargoHold(const json ship_json, const string cargo_symbol){
    //cout << "[DEBUG] howMuchOfCargoDoesShipHaveInCargoHold " + cargo_symbol << endl;
    json inventory = ship_json["data"]["cargo"]["inventory"];
    
    for (json item: inventory){
        if (item["symbol"] == cargo_symbol){
            //cout << "[DEBUG] " << item["units"] << " of " << cargo_symbol << endl;
            return item["units"];
        }
    }
    return 0;
}

json fulfillContract(const string contract_id){
    json result = http_post("https://api.spacetraders.io/v2/my/contracts/" + contract_id + "/fulfill");
    print_json(result);
    return result;
}

json deliverCargoToContract(const string contract_id, const string ship_symbol, const string trade_symbol, const int units){
    cout << "[DEBUG] deliverCargoToContract " << endl; 
    json payload;
    payload["shipSymbol"] = ship_symbol;
    payload["tradeSymbol"] = trade_symbol;
    payload["units"] = units;
    json result = http_post("https://api.spacetraders.io/v2/my/contracts/" + contract_id + "/deliver", payload);
    int units_fulfilled = result["data"]["contract"]["terms"]["deliver"][0]["unitsFulfilled"];
    int units_required = result["data"]["contract"]["terms"]["deliver"][0]["unitsRequired"];

    cout << "[INFO] " << ship_symbol << "Delivered " << units << " of " << trade_symbol 
         << " [" << units_fulfilled << "/" << units_required << "]" << endl;

    return result;
}

bool isContractFulfilled(const json contract_json){
    return contract_json["fulfilled"];
}

void sellCargo(const string ship_symbol, const string cargo_symbol, const int units){
    //cout << "[DEBUG] sellCargo " << ship_symbol << " " << cargo_symbol << " " << units << endl;
    json payload;
    payload["symbol"] = cargo_symbol;
    payload["units"] = units;
    json result = http_post("https://api.spacetraders.io/v2/my/ships/"+ ship_symbol + "/sell", payload);
    const json transaction = result["data"]["transaction"];
    const int units_sold = transaction["units"];
    const string trade_symbol = transaction["tradeSymbol"];
    const int total_price = transaction["totalPrice"];
    cout << "[INFO] " << ship_symbol << " Sold " << units_sold 
         << " " << trade_symbol << " for " << total_price << endl;
}

void applyRoleMiner(const string ship_symbol){
    cout << "[INFO] " << ship_symbol << " applyRoleMiner" << endl;

    // get ship state
    json ship_json = getShip(ship_symbol);

    // if ship is currently travelling, dont waste any further cycles.
    if (isShipInTransit(ship_json)){
        return;
    }

    if (isShipAtWaypoint(ship_json, delivery_waypoint_symbol)){
        if (isShipCargoHoldEmpty(ship_json)){
            if (isShipDocked(ship_json)){
                orbitShip(ship_symbol);
            }
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
                int units = howMuchOfCargoDoesShipHaveInCargoHold(ship_json, target_resource);
                json deliver_result = deliverCargoToContract(target_contract_id, ship_symbol, target_resource, units);
                const json inventory = deliver_result["data"]["cargo"]["inventory"];
                for (json item: inventory){
                    string cargo_symbol = item["symbol"];
                    int units = howMuchOfCargoDoesShipHaveInCargoHold(ship_json, cargo_symbol);
                    sellCargo(ship_symbol, cargo_symbol, units);
                }
            } else {
                // contract is fulfilled, sell everything
                const json inventory = ship_json["data"]["cargo"]["inventory"];
                for (json item: inventory){
                    string cargo_symbol = item["symbol"];
                    int units = howMuchOfCargoDoesShipHaveInCargoHold(ship_json, cargo_symbol);
                    sellCargo(ship_symbol, cargo_symbol, units);
                }
            }
        }
    }

    if(isShipAtWaypoint(ship_json, asteroid_belt_symbol)){
        // ship is at asteroid belt.
        if(isShipCargoHoldFull(ship_json)){
            cout << "[INFO] " + ship_symbol + " cargo hold full. Heading to delivery waypoint." << endl;
            navigateShip(ship_symbol, delivery_waypoint_symbol);
        } else {
            // ship is at asteroid belt, but its cargo hold is not full.
            json result = extractResourcesWithSurvey(ship_symbol, best_survey);
            const string extracted_resource_symbol = result["data"]["extraction"]["yield"]["symbol"];
            const int extracted_resource_units = result["data"]["extraction"]["yield"]["units"];
                    
            // jettison anything which is not on the resource_keep_list
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

bool isSurveyGoodEnough(){
    return (bestSurveyScore() >= survey_score_threshold ? true : false);
}

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
    return contract_json["accepted"];
}

int main(int argc, char* argv[])
{
    // once, at the start of the run.
    callsign = argv[1];
    registerAgent(callsign);
    initializeGlobals();
    // only do this if its needed.
    
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
        }


        commandShipRoleDecider(callsign + "-1");
        sleep(120);
        cout << endl;
    }

    // there is currently no curl easy cleanup. i think there needs to be exactly 1. 
    // so right now my inclination is to curl easy init once globally instead of in each http_ function.
    curl_global_cleanup();
    return 0;
}
