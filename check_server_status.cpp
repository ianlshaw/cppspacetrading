#include <curl/curl.h>
#include <iostream>
#include <string>
#include "json.hpp"
using json = nlohmann::json;


size_t writeFunction(void *ptr, size_t size, size_t nmemb, std::string* data) {
    data->append((char*) ptr, size * nmemb);
    return size * nmemb;
}

int main(int argc, char** argv) {
    auto curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.spacetraders.io/v2/");
                
        std::string response_string;
        std::string header_string;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunction);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_string);
        
        char* url;
        long response_code;
        double elapsed;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        curl = NULL;

        json data = json::parse(response_string);
        std::cout << data["status"] << "\n";
        std::cout << "Last reset: " << data["resetDate"] << "\n";
        std::cout << "Next reset: " << data["serverResets"]["next"];

    }
}
