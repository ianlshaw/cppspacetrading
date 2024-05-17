#include <stdio.h>
#include <iostream>
#include <unistd.h>   
#include <curl/curl.h>
#include "json.hpp"
#include "auth-file-utils.h"
#include "log-utils.h"

using namespace std;

using json = nlohmann::json;

const int max_retries = 5;
const int retry_delay = 30;

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

// wrapper to make libcurl usable enough for what we need
json http_get(const string callsign, const string endpoint){
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
        
        // parse the response body into a json object.
        json output_as_json = json::parse(*httpData);

        if (httpCode != 200 && httpCode != 201){
            cout << "http_get error: " << endl;
            cout << *httpData << endl;
            printJson(output_as_json);
        }

        //cout << "[DEBUG] curl easy reset" << endl; 
        curl_easy_reset(curl);
        curl_easy_cleanup(curl);
        return output_as_json;
    }
    curl_easy_cleanup(curl);

    return {{"error", "default"}};
}

// libcurl wrapper. payload parameter is optional
json http_post(const string callsign, const string endpoint, const json payload = {}, const long possible_error = 200){
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
         
       // retrieve the http response code
       curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
       
       // parse the response body into a json object.
       json output_as_json = json::parse(*httpData);

       // print entire response body when http return code is non-20X
       if (httpCode != 200 && httpCode != 201 && httpCode != possible_error){
           log("ERROR", "http_post() returned non-20X");
           log("ERROR", "payload: " + payload.dump());
           log("ERROR", to_string(httpCode));
           cout << *httpData << endl;
           printJson(output_as_json);
       }

       

       // always reset curl handle after use   
       curl_easy_reset(curl);
       curl_easy_cleanup(curl);

       // return the whole object and let other methods deal with it
       return output_as_json; 
     }
    curl_easy_cleanup(curl);
    return {{"error", "default"}};
}