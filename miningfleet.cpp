#include <stdio.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include "spacetraders/nlohmann/json.hpp"
#include <unistd.h>   

using json = nlohmann::json;

using namespace std;

string callsign;

json contracts_list;
json target_contract;
string target_resource;
string asteroid_belt_symbol;
float survey_score_threshold = 0.5;

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
           cout << payload_as_string.c_str() << endl;
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

       // for now just print the result to stdout
       //print_json(output_as_json);

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
    print_json(result);
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

void initializeGlobals(){
    contracts_list = listContracts();
    target_contract = contracts_list["data"][0];
    target_resource = target_contract["terms"]["deliver"][0]["tradeSymbol"];
    cout << "Target resource is : " + target_resource << endl;
    json first_ship = getShip(callsign + "-1");
    asteroid_belt_symbol = find_waypoint_by_type(first_ship["data"]["nav"]["systemSymbol"], "ENGINEERED_ASTEROID"); 
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
    http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/orbit");
}

void dockShip(const string ship_symbol){
    http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/dock");
}

void navigateShip(const string ship_symbol, const string waypoint_symbol){
    cout << "[INFO] navigateShip " + ship_symbol + " to " + waypoint_symbol << endl;
    json payload;
    payload["waypointSymbol"] = waypoint_symbol;
    json result = http_post("https://api.spacetraders.io/v2/my/ships/" + ship_symbol + "/navigate", payload);
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
        cout << "[INFO] Survey result: " + score << endl;
        a_survey.score = score;
        a_survey.surveyObject = individual_survey;
        surveys.push_back(a_survey);
    }
}

bool isShipCargoHoldFull(const json shipJson){
    return (shipJson["data"]["cargo"]["units"] == shipJson["data"]["cargo"]["capacity"] ? true : false);
}

bool isShipAtWaypoint(const json ship_json, string waypoint_symbol){
    //cout << "[DEBUG] isShipAtWaypoint" << endl;
    return (ship_json["data"]["nav"]["waypointSymbol"] == waypoint_symbol ? true : false);
}

void commandShipLoop(const string ship_symbol){
    cout << "[DEBUG] commandShipLoop" << endl;
    // get the state of the command frigate
    json ship_json = getShip(ship_symbol);
    if (isShipCargoHoldFull(ship_json)){
    //   does it contain any garbage?
    //   go to marketplace
    } else {
    // is this ship already at the asteroid be << endl;lt?
        if (isShipAtWaypoint(ship_json, asteroid_belt_symbol)){
            cout << "[INFO] " + ship_symbol + " is already on site at asteroid belt" << endl;
            // do we already have a good enough survey?
            createSurvey(ship_symbol);
        } else {
            if (isShipDocked(ship_json))
                orbitShip(ship_symbol);
            navigateShip(ship_symbol, asteroid_belt_symbol);
        }
    }
}

int main(int argc, char* argv[])
{
    callsign = argv[1];
    registerAgent(callsign);
    initializeGlobals();

    while (true){
        cout << "Start turn" << endl;
        commandShipLoop(callsign + "-1");
        sleep(120);
    }

    // there is currently no curl easy cleanup. i think there needs to be exactly 1. 
    // so right now my inclination is to curl easy init once globally instead of in each http_ function.
    curl_global_cleanup();
    return 0;
}
