#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // For getopt
#include <libcups3/cups/cups.h>
#include <libcups3/cups/ipp.h>

// Function to handle CUPS errors
void handle_cups_error(const char *message) {
    fprintf(stderr, "%s: %s\n", message, cupsGetErrorString());
    exit(1);
}

int main(int argc, char *argv[]) {
    // 1. Define variables for getopt
    int opt;
    char *hostname = NULL;
    int port = 8000; // Default port
    char *printer_uri = NULL;
    const int encryption = HTTP_ENCRYPTION_ALWAYS; // Hardcoded encryption

    // 3. Use getopt to parse command-line arguments
    while ((opt = getopt(argc, argv, "h:p:u:")) != -1) {
        switch (opt) {
            case 'h':
                hostname = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'u':
                printer_uri = optarg;
                break;
            case '?':
                fprintf(stderr, "Usage: %s [-h hostname] [-p port] [-u printer_uri]\n", argv[0]);
                return 1;
            default:
                fprintf(stderr, "Usage: %s [-h hostname] [-p port] [-u printer_uri]\n", argv[0]);
                return 1;
        }
    }

    // 4. Check for required arguments
    if (hostname == NULL || printer_uri == NULL) {
        fprintf(stderr, "Error: -h (hostname) and -u (printer_uri) are required.\n");
        fprintf(stderr, "Usage: %s [-h hostname] [-p port] [-u printer_uri]\n", argv[0]);
        return 1;
    }

    // 5. Construct the server URI
    char server_uri[256];
    snprintf(server_uri, sizeof(server_uri), "http://%s:%d", hostname, port); // Or ipp://, adjust as needed

    printf("Server URI: %s\n", server_uri);
    printf("Printer URI: %s\n", printer_uri);

    // 6. Create IPP request
    ipp_t *request = ippNewRequest(IPP_OP_SET_PRINTER_ATTRIBUTES);
    if (!request) {
        fprintf(stderr, "Error creating IPP request.\n");
        return 1;
    }

    // 7. Add printer-uri and requesting-user-name attributes
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_uri);

    // Get the username
    const char *username = cupsGetUser();
    if (username == NULL) {
        fprintf(stderr, "Error getting username.\n");
        ippDelete(request);
        return 1;
    }
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, username);

    // 8. Hardcode the attribute settings
    ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-location", NULL, "New Office Location"); // ippAddString
    ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-geo-location", NULL, "geo:47.6062,-122.3321");   // ippAddString
    //ippAddInteger(request, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "printer-darkness-configured", 75); // ippAddInteger

    // 9. Perform the request
    http_t *http = httpConnect(hostname, port, NULL, AF_UNSPEC, encryption, 1, 30000, NULL);

    if (!http) {
        handle_cups_error("Unable to connect to the CUPS server");
    }

    ipp_t *response = cupsDoRequest(http, request, "/admin/");

    // 10. Handle the response
    if (response) {
        ipp_status_t status_code = ippGetStatusCode(response);

        if (status_code > IPP_STATUS_OK) {
            // Get the status message
            const char *status_message = ippGetString(ippFindAttribute(response, "status-message", IPP_TAG_TEXT), 0, NULL);

            if (status_message) {
                fprintf(stderr, "IPP Error: %s\n", status_message);
            } else {
                fprintf(stderr, "IPP Error: (No status message available)\n");
            }
        } else {
            printf("Printer attributes updated successfully!\n");
        }
        ippDelete(response);
    } else {
        handle_cups_error("CUPS request failed");
    }

    // 11. Clean up
    ippDelete(request);
    httpClose(http);

    return 0;
}
