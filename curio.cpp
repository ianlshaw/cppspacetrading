#include <stdio.h>
#include <curl/curl.h>
#include "nlohmann/json.hpp"
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <fstream>

using namespace std;
using json = nlohmann::json;

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

void register_agent() {
    cout << "Enter desired CALLSIGN:" << endl;
    cin >> callsign;
    cout << "[INFO] Attempting to register new agent " << callsign << endl;

    // cur setup
    CURL *curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_ALL);
 
  /* get a curl handle */
  curl = curl_easy_init();
  if(curl) {
    /* First set the URL that is about to receive our POST. This URL can
       just as well be an https:// URL if that is what should receive the
       data. */
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.spacetraders.io/v2/register");
    /* Now specify the POST data */

    /* ask libcurl to show us the verbose output */
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
  
    // user interaction
    cout << "Enter faction name" << endl;
    string faction;
    cin >> faction;

    // payload
    json payload;
    payload["faction"] = faction;
    payload["symbol"] = callsign;
    string payload_as_string = payload.dump();
    cout << payload_as_string.c_str() << endl;
    cout << strlen(payload_as_string.c_str()) << endl;
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload_as_string.c_str());

    // headers
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    //curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, -1L);

    long httpCode(0);
    std::unique_ptr<std::string> httpData(new std::string());
    // Hook up data handling function.
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, httpData.get());

 
    /* Perform the request, res gets the return code */
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);


    if (httpCode != 201) {
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

    // parse the response body into a json object.
    json output_as_json = json::parse(*httpData);

    // this is how to selectively return parts of the json object.
    cout << "[INFO] Callsign registration succeeded " << endl;
    string token = output_as_json["data"]["token"];
    ofstream myfile;
    string filename = callsign + ".token";
    cout << "[INFO] Wrote auth file to: " << filename << endl;
    myfile.open (filename);
    myfile << token;
    myfile.close();


    /* Check for errors */
    if(res != CURLE_OK)
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));
 
    cout << "curl easy reset" << endl;
    curl_easy_reset(curl);
  }
}

int main(void)
{
  register_agent();
  curl_global_cleanup();
  return 0;
}
