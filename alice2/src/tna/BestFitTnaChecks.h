#pragma once
namespace alice2 {
// Headless numerical regression entry point; never creates a graphics context.
int runBestFitTnaChecks(const char* objPath);
int runBestFitTnaBenchmark(const char* objPath);
}
