#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libcups3/cups/cups.h>
#include <libcups3/cups/ipp.h>

int main() {
    // --------  USER CONFIGURATION - MODIFY THESE  --------
    const char *printer_uri = "ipp://matt-linux.local:8000/ipp/print"; // Replace with your printer URI
    const char *attribute_name = "printer-darkness-configured";       // Setting printer-location for this test
    int attribute_value = 75; // New location value
    // --------  END USER CONFIGURATION --------

    http_t *http = NULL;
    ipp_t *request = NULL;
    ipp_t *response = NULL;

    // 1. Connect to the printer.
    http = httpConnect("matt-linux.local", 8000, NULL, AF_UNSPEC, HTTP_ENCRYPTION_ALWAYS, 1, 30000, NULL);
    if (!http) {
        fprintf(stderr, "Error connecting to printer: %s\n", cupsGetErrorString());
        return 1;
    }

    // 2. Create IPP request - Explicitly set IPP version to 2.0 (although CUPS handles this)
    request = ippNewRequest(IPP_OP_SET_PRINTER_ATTRIBUTES);
    ippSetVersion(request, 2, 0); // Good practice, though often unnecessary with CUPS

    // 3. Add Operation Attributes - Correct tags and structure
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsGetUser());
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_CHARSET, "attributes-charset", NULL, "utf-8");
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE, "attributes-natural-language", NULL, "en-us");

    // 4. Add Printer Attribute - printer-location (text) - Correct tags and structure
    ippAddInteger(request, IPP_TAG_PRINTER, IPP_TAG_INTEGER, attribute_name, attribute_value);

    // 5. Send the IPP request.  "/ipp/printer" is the correct path for many setups.
    response = cupsDoRequest(http, request, "/ipp/printer");

    // 6. Check the response
    if (response) {
        ipp_status_t status = ippGetStatusCode(response);
        if (status >= IPP_STATUS_OK && status <= IPP_STATUS_OK_EVENTS_COMPLETE) { // Check for *any* success code
            printf("Successfully set attribute '%s' to '%d'\n", attribute_name, attribute_value);
        } else {
            fprintf(stderr, "Error setting attribute '%s': %s (%04x)\n", attribute_name, ippErrorString(status), status);
            ipp_attribute_t *msg_attr = ippFindAttribute(response, "status-message", IPP_TAG_TEXT);
            if (msg_attr) fprintf(stderr, "Status message: %s\n", ippGetString(msg_attr, 0, NULL));
        }
        ippDelete(response);
    } else {
        fprintf(stderr, "No response from printer: %s\n", cupsGetErrorString());
    }

    // 7. Cleanup
    ippDelete(request);
    httpClose(http);

    return 0;
}
