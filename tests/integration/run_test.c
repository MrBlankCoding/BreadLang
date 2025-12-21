#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/core/function.h"
#include "../../include/core/value.h"
#include "../../include/core/var.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <test_file.bread>\n", argv[0]);
        return 1;
    }
    
    // Initialize core components
    value_init();
    
    // Run the test file
    FILE* file = fopen(argv[1], "r");
    if (!file) {
        perror("Failed to open test file");
        return 1;
    }
    
    // Read and execute the test file
    // (This is a simplified version - you'll need to adapt it to your actual execution model)
    // For now, we'll just print the file contents
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), file)) {
        printf("%s", buffer);
    }
    
    fclose(file);
    return 0;
}
