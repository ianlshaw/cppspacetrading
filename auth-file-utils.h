#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <fstream>

using namespace std;

// auth token file will only exist after first run
bool doesAuthFileExist(const string& authTokenFile) {
    ifstream auth_token_file_stream(authTokenFile.c_str());
    return auth_token_file_stream.good();
}

// Register Agent returns an auth token we want to keep safe 
void writeAuthTokenToFile(const string token, const string callsign){
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