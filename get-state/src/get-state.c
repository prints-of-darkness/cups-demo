#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libcups3/cups/cups.h>
#include <ctype.h>

void print_octetstring_attribute(ipp_attribute_t *attr, const char *name);
void print_enum_attribute(ipp_attribute_t *attr, const char *name);

int main(int argc, char *argv[]) {
    const char *uri_hostname = NULL;
    int port = 8000;
    http_t *http = NULL;
    ipp_t *request = NULL, *response = NULL;
    ipp_status_t status;
    char printer_uri_str[256];

    int opt;
    opterr = 0;

    while ((opt = getopt(argc, argv, "h:p:")) != -1) {
        switch (opt) {
            case 'h':
                uri_hostname = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case '?':
                if (optopt == 'h' || optopt == 'p')
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                else if (isprint(optopt))
                    fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
                return 1;
            default:
                return 1;
        }
    }

    if (uri_hostname == NULL) {
        fprintf(stderr, "Usage: %s -h <hostname> [-p <port>]\n", argv[0]);
        fprintf(stderr, "  -h <hostname>:  Hostname or IP address of the printer (required).\n");
        fprintf(stderr, "  -p <port>:      Port number for the printer (optional, default is 631).\n");
        return 1;
    }

    http_encryption_t encryption = (port == 8000) ? HTTP_ENCRYPTION_ALWAYS : HTTP_ENCRYPTION_IF_REQUESTED;
    http = httpConnect(uri_hostname, port, NULL, AF_UNSPEC, encryption, 1, 30000, NULL);

    if (!http) {
        fprintf(stderr, "Error: Unable to connect to printer at %s:%d: %s\n", uri_hostname, port, cupsGetErrorString());
        return 1;
    }

    snprintf(printer_uri_str, sizeof(printer_uri_str), "ipp://%s:%d/ipp/print", uri_hostname, port);

    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_uri_str);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsGetUser());

    const char *requested_attrs[] = {"printer-alert", "printer-state"};
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_CONST_TAG(IPP_TAG_KEYWORD),
                  "requested-attributes", 2, NULL, requested_attrs);

    response = cupsDoRequest(http, request, "/ipp/print");

    if (response == NULL) {
        fprintf(stderr, "Error sending Get-Printer-Attributes request: %s\n", cupsGetErrorString());
        httpClose(http);
        ippDelete(request);
        return 1;
    }

    status = ippGetStatusCode(response);

    if (status > IPP_STATUS_OK_EVENTS_COMPLETE) {
        fprintf(stderr, "Get-Printer-Attributes request failed: %s\n", cupsGetErrorString());
        ippDelete(response);
        ippDelete(request);
        httpClose(http);
        return 1;
    }

	print_octetstring_attribute(ippFindAttribute(response, "printer-alert", IPP_TAG_STRING), "printer-alert");
	print_enum_attribute(ippFindAttribute(response, "printer-state", IPP_TAG_ENUM), "printer-state");

    ippDelete(response);
    ippDelete(request);
    httpClose(http);

    return 0;
}


void print_octetstring_attribute(ipp_attribute_t *attr, const char *name) {
    if (attr != NULL) {
        int count = ippGetCount(attr);
        printf("%s:\n", name);
        for (int i = 0; i < count; i++) {
            size_t len = 0;
            const char *value = (const char *)ippGetOctetString(attr, i, &len);

            if (value != NULL) {
                // Check to prevent crashes
                printf("  %.*s\n", (int)len, value);
            } else {
                printf("  (null)\n");
            }
        }
    } else {
        printf("%s attribute not found in the response.\n", name);
    }
}

void print_enum_attribute(ipp_attribute_t *attr, const char *name) {
    if (attr != NULL) {
        int count = ippGetCount(attr);
        printf("%s:\n", name);
        for (int i = 0; i < count; i++) {
        	ipp_pstate_t enum_value = ippGetInteger(attr, i);
			const char *state_string = "unknown";

            //printer-state
            if (strcmp(name, "printer-state") == 0)
            {
                switch (enum_value) {
                    case IPP_PSTATE_IDLE:  state_string = "Idle"; break;
                    case IPP_PSTATE_PROCESSING: state_string = "Processing"; break;
                    case IPP_PSTATE_STOPPED: state_string = "Stopped"; break;
                    default: break;
                }
            }
            else{ // no known name.
                 printf("Unsupported keyword");
            }
			printf("  %s\n", state_string);  // Print the string
        }
    } else {
        printf("%s attribute not found in the response.\n", name);
    }
}
