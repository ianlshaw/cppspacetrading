#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <sstream> 
#include <ctime>
#include <unistd.h>
#include "json.hpp" 

using namespace std;
using json = nlohmann::json;

// when printing an entire json object is required, this makes it easier on the eyes
void printJson(json jsonObject){
    int indent = 4;
    string pretty_json = jsonObject.dump(indent);
    cout << pretty_json;
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