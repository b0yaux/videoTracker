//
//  main.cpp
//
//  Audiovisual Sequencer Example
//

#include "ofMain.h"
#include "ofApp.h"
#include <string>

// External declaration for CLI command (defined in ofApp.cpp)
extern std::string g_cliCommandOrFile;

int main(int argc, char* argv[]) {
    std::string cliCommandOrFile;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--cli" || arg == "-c") {
            // Next argument is command or file
            if (i + 1 < argc) {
                cliCommandOrFile = argv[i + 1];
                i++;  // Skip next argument
            }
            // If no argument provided, will read from stdin
            break;  // Found --cli, stop parsing
        }
    }
    
    // Store in static variable for ofApp to access
    g_cliCommandOrFile = cliCommandOrFile;
    
    ofSetupOpenGL(1280, 720, OF_WINDOW);
    ofRunApp(new ofApp());
    
    return 0;
}
