#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h> // For sleep() and getopt
#include <libcups3/cups/cups.h>
#include <ctype.h>

//const char *username = "bill"; // Replace with your PAM username
//const char *password = "ppp"; // Replace with your PAM password

// Function to get the job state string
//const char *jobStateString(ipp_jstate_t state) {
//    switch (state) {
//        case IPP_JSTATE_PENDING:   return "pending";
//        case IPP_JSTATE_PROCESSING: return "processing";
//        case IPP_JSTATE_HELD:       return "held";
//        case IPP_JSTATE_COMPLETED:  return "completed";
//        case IPP_JSTATE_CANCELED:   return "canceled";
//        case IPP_JSTATE_ABORTED:    return "aborted";
//        default:                  return "unknown";
//    }
//}
//
//// Function to get the printer state string
//const char *printerStateString(ipp_pstate_t state) {
//    switch (state) {
//        case IPP_PSTATE_IDLE:       return "idle";
//        case IPP_PSTATE_PROCESSING: return "processing";
//        case IPP_PSTATE_STOPPED:    return "stopped";
//        default:                  return "unknown";
//    }
//}

char* base64Encoder(char input_str[], int len_str);

// Function to poll printer state
//void pollPrinterState(http_t *http, const char *printer_uri_str);

// Function to poll job status
//void pollJobStatus(http_t *http, const char *printer_uri_str, int job_id);

int main(int argc, char *argv[]) {
    const char *filename = NULL;
    const char *filetype = NULL;
    const char *uri_hostname = NULL;
    int port = 631; // Default port
    http_t *http = NULL;
    ipp_t *request = NULL, *response = NULL;
    int job_id;
    ipp_status_t status;
    char printer_uri_str[256]; // Buffer for constructing the printer URI

    int opt;
    opterr = 0; // Disable getopt's default error printing

    while ((opt = getopt(argc, argv, "h:p:f:m:")) != -1) {
        switch (opt) {
            case 'h':
                uri_hostname = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'f':
                filename = optarg;
                break;
            case 'm':
                filetype = optarg;
                break;
            case '?':
                if (optopt == 'h' || optopt == 'p' || optopt == 'f' || optopt == 'm')
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

    if (uri_hostname == NULL || filename == NULL || filetype == NULL) {
        fprintf(stderr, "Usage: %s -h <hostname> [-p <port>] -f <filename> -m <mime_type>\n", argv[0]);
        fprintf(stderr, "  -h <hostname>:  Hostname or IP address of the printer (required).\n");
        fprintf(stderr, "  -p <port>:    Port number for the printer (optional, default is 8000).\n");
        fprintf(stderr, "  -f <filename>:  Path to the file to print (required).\n");
        fprintf(stderr, "  -m <mime_type>: MIME type of the file (e.g., image/jpeg, application/pdf) (required).\n");
        return 1;
    }

    // 2. Establish a connection to the printer
    http = httpConnect(uri_hostname, port, NULL, AF_UNSPEC, HTTP_ENCRYPTION_ALWAYS /*HTTP_ENCRYPTION_IF_REQUESTED*/, 1, 30000, NULL);
    if (!http) {
        fprintf(stderr, "Error: Unable to connect to printer at %s:%d: %s\n", uri_hostname, port, cupsGetErrorString());
        return 1;
    }

    // 2.a Construct printer URI string
    snprintf(printer_uri_str, sizeof(printer_uri_str), "ipp://%s:%d/ipp/print", uri_hostname, port);

//    // 2.b Set Basic Authentication header (for PAM AUTH)
//    char credentials[256];
//    snprintf(credentials, sizeof(credentials), "%s:%s", username, password);
//    char *auth_string = base64Encoder(credentials, strlen(credentials)); //httpEncode64(NULL, 0, credentials, strlen(credentials), false); // Base64 encode //<--- broken? well wont work right.
//    printf("base64: %s\n", auth_string);
//
//    httpSetAuthString(http, "Basic", auth_string); // Set Authorization header
//    free(auth_string);

    // 3. Create a new IPP request
    request = ippNewRequest(IPP_OP_PRINT_JOB);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_uri_str);			// Printer URI (already constructed)
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsGetUser());	// Requesting User Name
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, filetype); 			// Document Format (MIME Type)

    // --- Constructing media-col collection - media-size ---
    ipp_t *media_col = ippNew();
    ippAddString(media_col, IPP_TAG_JOB, IPP_TAG_KEYWORD, "media-type", NULL, "labels-continuous");
    ippAddString(media_col, IPP_TAG_JOB, IPP_TAG_KEYWORD, "media-source", NULL, "main");
    ippAddString(media_col, IPP_TAG_JOB, IPP_TAG_KEYWORD, "media-tracking", NULL, "mark");

    ipp_t *media_size = ippNew();
    ippAddInteger(media_size, IPP_TAG_JOB, IPP_TAG_INTEGER, "x-dimension", 10160);
    ippAddInteger(media_size, IPP_TAG_JOB, IPP_TAG_INTEGER, "y-dimension", 15240);
    ippAddCollection(media_col, IPP_TAG_JOB, "media-size", media_size);
    ippDelete(media_size);

    // set margins
    ippAddInteger(media_col, IPP_TAG_JOB, IPP_TAG_INTEGER, "media-bottom-margin", 0);
    ippAddInteger(media_col, IPP_TAG_JOB, IPP_TAG_INTEGER, "media-left-margin", 0);
    ippAddInteger(media_col, IPP_TAG_JOB, IPP_TAG_INTEGER, "media-right-margin", 0);
    ippAddInteger(media_col, IPP_TAG_JOB, IPP_TAG_INTEGER, "media-top-margin", 0);
    ippAddInteger(media_col, IPP_TAG_JOB, IPP_TAG_INTEGER, "media-top-offset", 0);
    ippDelete(media_col);

    // --- media-col construction complete ---
    ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_INTEGER, "print-darkness", 100);					// print darkness
    ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_INTEGER, "print-speed", 500);						// print speed
    ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "print-color-mode", NULL, "monochrome"); 	// Request Monochrome Printing:

    // 5. Send the request and receive the response
    response = cupsDoFileRequest(http, request, "/ipp/print", filename);

    if (response == NULL) {
        fprintf(stderr, "Error sending print request: %s\n", cupsGetErrorString());
        httpClose(http);
        ippDelete(request);
        return 1;
    }

    status = ippGetStatusCode(response);

    if (status > IPP_STATUS_OK) {
        fprintf(stderr, "Print job submission failed: %s\n", cupsGetErrorString());
        ippDelete(response);
        ippDelete(request);
        httpClose(http);
        return 1;
    }

    // Get job ID
    job_id = ippGetInteger(ippFindAttribute(response, "job-id", IPP_TAG_INTEGER), 0);

    fprintf(stdout, "Print job submitted successfully, job ID: %d\n", job_id);
    ippDelete(response);
    ippDelete(request);

    // --- Job Status Polling ---
    //pollJobStatus(http, printer_uri_str, job_id); // Poll job status after submission
    // --- End Job Status Polling ---

    httpClose(http);

    return 0;
}

//void pollPrinterState(http_t *http, const char *printer_uri_str)
//{
//    ipp_t *request, *response;
//    ipp_attribute_t *attr;
//    ipp_pstate_t printer_state;
//    const char *printer_state_str;
//    const char *printer_state_reasons_str;
//    bool accepting_jobs;
//
//    fprintf(stdout, "\n--- Printer Status ---\n");
//
//    // 1. Create a new IPP request for Get-Printer-Attributes
//    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
//    if (!request) {
//        fprintf(stderr, "Error: Failed to create Get-Printer-Attributes request.\n");
//        return;
//    }
//
//    // 2. Set the printer URI
//    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_uri_str);
//    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsGetUser()); // Add username for authentication if needed
//
//    // 3. Specify attributes to request
//    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requested-attributes", 3, NULL,
//                  (const char *[]){"printer-state", "printer-state-reasons", "printer-is-accepting-jobs"});
//
//    // 4. Send the request
//    response = cupsDoRequest(http, request, "/ipp/printer"); // Use "/ipp/printer" for printer operations
//    ippDelete(request); // Request is no longer needed
//
//    if (!response) {
//        fprintf(stderr, "Error: Could not get printer status: %s\n", cupsGetErrorString());
//        return;
//    }
//
//    // 5. Check status code
//    ipp_status_t status_code = ippGetStatusCode(response);
//    if (status_code != IPP_STATUS_OK) {
//        fprintf(stderr, "Error: Get-Printer-Attributes failed: %s\n", ippErrorString(status_code));
//        ippDelete(response);
//        return;
//    }
//
//    // 6. Get printer-state
//    attr = ippFindAttribute(response, "printer-state", IPP_TAG_ENUM);
//    if (attr) {
//        printer_state = (ipp_pstate_t)ippGetInteger(attr, 0);
//        printer_state_str = printerStateString(printer_state);
//        fprintf(stdout, "Printer State: %s\n", printer_state_str);
//    } else {
//        fprintf(stderr, "Warning: printer-state attribute not found.\n");
//    }
//
//    // 7. Get printer-state-reasons
//    attr = ippFindAttribute(response, "printer-state-reasons", IPP_TAG_KEYWORD);
//    if (attr) {
//        fprintf(stdout, "Printer State Reasons: ");
//        int count = ippGetCount(attr);
//        for (int i = 0; i < count; i++) {
//            printer_state_reasons_str = ippGetString(attr, i, NULL);
//            fprintf(stdout, "%s%s", printer_state_reasons_str, (i < count - 1) ? ", " : "");
//        }
//        fprintf(stdout, "\n");
//    } else {
//        fprintf(stdout, "Printer State Reasons: none\n");
//    }
//
//    // 8. Get printer-is-accepting-jobs
//    attr = ippFindAttribute(response, "printer-is-accepting-jobs", IPP_TAG_BOOLEAN);
//    if (attr) {
//        accepting_jobs = ippGetBoolean(attr, 0);
//        fprintf(stdout, "Accepting Jobs: %s\n", accepting_jobs ? "yes" : "no");
//    } else {
//        fprintf(stderr, "Warning: printer-is-accepting-jobs attribute not found.\n");
//    }
//
//    ippDelete(response); // Response is no longer needed
//    fprintf(stdout, "--- Printer Status End ---\n\n");
//}
//
//void pollJobStatus(http_t *http, const char *printer_uri_str, int job_id)
//{
//    while (1)
//    {
//    	pollPrinterState(http, printer_uri_str);
//
//        ipp_t *status_request = ippNewRequest(IPP_OP_GET_JOB_ATTRIBUTES);
//        if (!status_request) {
//            fprintf(stderr, "Error: Failed to create Get-Job-Attributes request.\n");
//            break;
//        }
//
//        // Set printer-uri for status request
//        ippAddString(status_request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_uri_str);
//        ippAddInteger(status_request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", job_id);
//        ippAddString(status_request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsGetUser()); // Add username for authentication if needed
//        ippAddStrings(status_request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requested-attributes", 1, NULL, (const char*[]){"job-state"});
//
//
//        ipp_t *status_response = cupsDoRequest(http, status_request, "/ipp/print"); // or "/ipp/job" - check your printer
//
//        ippDelete(status_request); // Request is no longer needed
//
//        if (!status_response) {
//            fprintf(stderr, "Error: Could not get job status: %s\n", cupsGetErrorString());
//            break; // Exit loop on error
//        }
//
//        ipp_status_t status_code = ippGetStatusCode(status_response);
//        if (status_code != IPP_STATUS_OK) {
//            fprintf(stderr, "Error: Get-Job-Attributes failed: %s\n", ippErrorString(status_code));
//            ippDelete(status_response);
//            break; // Exit loop on IPP error
//        }
//
//        ipp_attribute_t *job_state_attr = ippFindAttribute(status_response, "job-state", IPP_TAG_ENUM);
//        if (!job_state_attr) {
//            fprintf(stderr, "Error: job-state attribute not found in response.\n");
//            ippDelete(status_response);
//            break; // Exit loop if job-state is missing
//        }
//
//        ipp_jstate_t current_job_state = (ipp_jstate_t)ippGetInteger(job_state_attr, 0);
//        fprintf(stdout, "Job %d state: %s\n", job_id, jobStateString(current_job_state));
//        ippDelete(status_response); // No longer need status response
//
//        if (current_job_state == IPP_JSTATE_COMPLETED ||
//            current_job_state == IPP_JSTATE_CANCELED ||
//            current_job_state == IPP_JSTATE_ABORTED ||
//            current_job_state == IPP_JSTATE_STOPPED) {
//            fprintf(stdout, "Job %d finished with state: %s\n", job_id, jobStateString(current_job_state));
//            break; // Job is done, exit loop
//        }
//
//        usleep(5000*1000); // Poll every 5 seconds
//    }
//}

char* base64Encoder(char input_str[], int len_str) {
    // Character set of base64 encoding scheme
    char char_set[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	static int SIZE = 1000;

    // Resultant string
    char *res_str = (char *) malloc(SIZE * sizeof(char));
    int index, no_of_bits = 0, padding = 0, val = 0, count = 0, temp;
    int i, j, k = 0;

    // Loop takes 3 characters at a time from
    // input_str and stores it in val
    for (i = 0; i < len_str; i += 3) {
		val = 0, count = 0, no_of_bits = 0;
		for (j = i; j < len_str && j <= i + 2; j++) {
			// binary data of input_str is stored in val
			val = val << 8;
			// (A + 0 = A) stores character in val
			val = val | input_str[j];
			// calculates how many time loop
			// ran if "MEN" -> 3 otherwise "ON" -> 2
			count++;
		}

		no_of_bits = count * 8;
		// calculates how many "=" to append after res_str.
		padding = no_of_bits % 3;
		// extracts all bits from val (6 at a time)
		// and find the value of each block
		while (no_of_bits != 0) {
			// retrieve the value of each block
			if (no_of_bits >= 6) {
				temp = no_of_bits - 6;
				// binary of 63 is (111111) f
				index = (val >> temp) & 63;
				no_of_bits -= 6;
			} else {
				temp = 6 - no_of_bits;
				// append zeros to right if bits are less than 6
				index = (val << temp) & 63;
				no_of_bits = 0;
			}
			res_str[k++] = char_set[index];
		}
    }
    // padding is done here
    for (i = 1; i <= padding; i++) {
        res_str[k++] = '=';
    }
    res_str[k] = '\0';

    return res_str;

}
