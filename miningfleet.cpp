#include <stdio.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include "spacetraders/nlohmann/json.hpp"

using json = nlohmann::json;

using namespace std;

string callsign;
string targetResource;
string targetContractId;

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

void print_json(json jsonObject){
    int indent = 4;
    string pretty_json = jsonObject.dump(indent);
    cout << pretty_json;
}

void populate_target_resource(string tradeSymbol){
  targetResource = tradeSymbol;
  cout << "targetResource updated to: " << targetResource << endl;
}

void populate_target_contract_id(string contractId){
  targetContractId = contractId;
  cout << "targetContractId updated to: " << targetContractId << endl;
}

bool does_auth_file_exist(const string& authTokenFile) {
    cout << "[INFO] Checking for existence of " << authTokenFile << endl;
    if (std::__fs::filesystem::exists(authTokenFile)){
        cout << "[INFO] Auth file found\n";
        return true;
    } else {
        cout << "[INFO] Auth file not found\n";
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



void http_post(const string endpoint, const json payload = {}){
    // so we can see whats going on
    cout << "POST-ing to:" << endl;
    cout << endpoint << endl;
    //

    CURL *curl;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_ALL);

    /* get a curl handle */
    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
        //curl_easy_setopt(curl, CURLOPT_POST, 1L);
        
        /* ask libcurl to show us the verbose output */
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

       // POSTFIELDS 
       if (payload.empty()){
           cout << "payload is empty" << endl;
           curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
       } else {
           cout << "payload is not empty" << endl;
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
       print_json(output_as_json);

       // always reset curl handle after use   
       curl_easy_reset(curl);

       // this wll be moved once we start reusing this function
       curl_easy_cleanup(curl);

     }
}

void write_auth_token_to_file(const string token){
     ofstream myfile;
     string filename = callsign + ".token";
     cout << "[INFO] Wrote auth file to: " << filename << endl;
     myfile.open (filename);
     myfile << token;
     myfile.close();
}

void register_agent() {
    cout << "Enter desired CALLSIGN:" << endl;
    cin >> callsign;

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
    http_post("https://api.spacetraders.io/v2/register", register_agent_json_object);
    //json result = http_post("https://api.spacetraders.io/v2/register", payload);

}

void populate_contract_globals(){
     cout << "[INFO] Checking starter contract for starter resource" << endl;
     CURL *curl;
     CURLcode res;
   
     /* In windows, this inits the winsock stuff */
     curl_global_init(CURL_GLOBAL_ALL);
   
     /* get a curl handle */
     curl = curl_easy_init();
     if(curl) {
       curl_easy_setopt(curl, CURLOPT_URL, "https://api.spacetraders.io/v2/my/contracts");
   
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
                    
   
       if (httpCode != 200) {
         cout << "!!! ERROR !!!" << endl;
         cout << "HTTP RETURN CODE:" << endl;
         cout << httpCode << endl;
         cout << "HTTP BODY:" << endl;
         json output_as_json = json::parse(*httpData);
         int indent = 4;
         string pretty_json = output_as_json.dump(indent);
         cout << pretty_json;
         exit(1);
       }

       /* Check for errors */
       if(res != CURLE_OK)
         fprintf(stderr, "curl_easy_perform() failed: %s\n",
               curl_easy_strerror(res));

   
       // parse the response body into a json object.
       json output_as_json = json::parse(*httpData);

       json data = output_as_json["data"];
       
       // Populate global variables for starter contract
       string resource = data[0]["terms"]["deliver"][0]["tradeSymbol"];
       populate_target_resource(resource);

       string contractId = data[0]["id"];
       populate_target_contract_id(contractId);


       // Remove this once it works
       int indent = 4;
       string pretty_json = data.dump(indent);
       cout << pretty_json;
       //


       cout << "curl easy reset" << endl; 
       curl_easy_reset(curl);
     }
}

void acceptContract(const string contractId) {
    cout << "attempting to accept contract " + contractId << endl;
    string endpoint = "https://api.spacetraders.io/v2/my/contracts/" + contractId + "/accept";
    http_post(endpoint);
    //print_json(result);
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
 
int main(void)
{
  register_agent();
  return 0;
  populate_contract_globals();
  acceptContract(targetContractId);
  curl_global_cleanup();
  return 0;
}
