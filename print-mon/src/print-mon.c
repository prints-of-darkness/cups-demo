#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <libcups3/cups/cups.h>

// --- Constants ---
#define PRINTER_URI_MAX 256
#define CREDENTIALS_MAX 256
#define MONITOR_INTERVAL_DEFAULT 2
#define DEFAULT_PORT 631
#define DEFAULT_X_DIMENSION 10160
#define DEFAULT_Y_DIMENSION 2540
#define DEFAULT_MEDIA_TRACKING "mark"

// --- Structures ---
typedef struct {
    const char *hostname;
    int         port;
    const char *filename;
    const char *filetype;
    int         x_dimension;
    int         y_dimension;
	const char *media_tracking;
    const char *username;
    const char *password;
    bool        use_auth;
} PrintParams;

// --- Function Prototypes ---
char *base64Encoder(const char *data, size_t input_length);
void print_keyword_attribute(ipp_attribute_t *attr, const char *name);
void print_enum_attribute(ipp_attribute_t *attr, const char *name);
ipp_t *get_job_attributes(http_t *http, const char *printer_uri, int job_id);
bool parse_command_line(int argc, char *argv[], PrintParams *params);
http_t *establish_ipp_connection(const char *hostname, int port);
ipp_t *create_print_job_request(const PrintParams *params, const char *printer_uri_str);
bool handle_authentication(http_t *http, const char *username, const char *password);
ipp_t *get_printer_attributes(http_t *http, const char *printer_uri_str);

int main(int argc, char *argv[]) {
    PrintParams params;
    memset(&params, 0, sizeof(params)); // Initialize all members to zero

    params.port = DEFAULT_PORT;
    params.x_dimension = DEFAULT_X_DIMENSION;
    params.y_dimension = DEFAULT_Y_DIMENSION;
	params.media_tracking = DEFAULT_MEDIA_TRACKING;

    // --- Parse command-line arguments ---
    if (!parse_command_line(argc, argv, &params)) {
        return 1; // Exit if parsing fails
    }

    // --- Construct printer URI ---
    char printer_uri_str[PRINTER_URI_MAX];
    snprintf(printer_uri_str, sizeof(printer_uri_str), "ipp://%s:%d/ipp/print", params.hostname, params.port);

    // --- Establish connection ---
    http_t *http = establish_ipp_connection(params.hostname, params.port);
    if (!http) {
        fprintf(stderr, "Error: Unable to connect to printer at %s:%d.\n", params.hostname, params.port);
        return 1;
    }

     // --- Handle Authentication ---
    if (params.use_auth && !handle_authentication(http, params.username, params.password)) {
        fprintf(stderr, "Error: Authentication failed.\n");
        httpClose(http);
        return 1;
    }

    // --- Create IPP print job request ---
    ipp_t *request = create_print_job_request(&params, printer_uri_str);
    if (!request) {
        fprintf(stderr, "Error: Failed to create IPP print job request.\n");
        httpClose(http);
        return 1;
    }

    // --- Send print request ---
    ipp_t *response = cupsDoFileRequest(http, request, "/ipp/print", params.filename);
    if (!response) {
        fprintf(stderr, "Error sending print request: %s\n", cupsGetErrorString());
        ippDelete(request);
        httpClose(http);
        return 1;
    }

    ipp_status_t status = ippGetStatusCode(response);
    if (status > IPP_STATUS_OK) {
        fprintf(stderr, "Print job submission failed: %s\n", cupsGetErrorString());
        ippDelete(response);
        ippDelete(request);
        httpClose(http);
        return 1;
    }

    // --- Get job ID ---
    int job_id = ippGetInteger(ippFindAttribute(response, "job-id", IPP_TAG_INTEGER), 0);
    fprintf(stdout, "Print job submitted successfully, job ID: %d\n", job_id);
    ippDelete(response);
    ippDelete(request);

    // --- Monitoring loop ---
    while (true) {
        printf("\n--- Monitoring Job ID %d and Printer at %s ---\n", job_id, params.hostname);

        // --- Get and print job attributes ---
        ipp_t *job_response = get_job_attributes(http, printer_uri_str, job_id);
        if (job_response) {
            print_enum_attribute(ippFindAttribute(job_response, "job-state", IPP_TAG_ENUM), "job-state");
            print_keyword_attribute(ippFindAttribute(job_response, "job-state-reasons", IPP_TAG_KEYWORD), "job-state-reasons");

            // Check if the job is completed, canceled, or aborted
            ipp_attribute_t *job_state_attr = ippFindAttribute(job_response, "job-state", IPP_TAG_ENUM);
            if (job_state_attr) {
                ipp_jstate_t job_state = ippGetInteger(job_state_attr, 0);
                if (job_state == IPP_JSTATE_COMPLETED || job_state == IPP_JSTATE_CANCELED || job_state == IPP_JSTATE_ABORTED) {
                    printf("Job is finished. Exiting monitoring.\n");
                    ippDelete(job_response);
                    break;
                }
            }
            ippDelete(job_response);
        } else {
            fprintf(stderr, "Error getting job attributes.\n");
        }

        // --- Get and print printer attributes ---
        ipp_t *printer_response = get_printer_attributes(http, printer_uri_str);
        if (!printer_response) {
            fprintf(stderr, "Error getting printer attributes.\n");
        } else {
            print_enum_attribute(ippFindAttribute(printer_response, "printer-state", IPP_TAG_ENUM), "printer-state");
            print_keyword_attribute(ippFindAttribute(printer_response, "printer-state-reasons", IPP_TAG_KEYWORD), "printer-state-reasons");
            ippDelete(printer_response);
        }

        sleep(MONITOR_INTERVAL_DEFAULT); // Wait before checking again
    }

    httpClose(http);
    return 0;
}

// --- Function to parse command-line arguments ---
bool parse_command_line(int argc, char *argv[], PrintParams *params) {
    int opt;
    opterr = 0;

    while ((opt = getopt(argc, argv, "h:p:f:m:U:P:ax:y:t:")) != -1) {
        switch (opt) {
            case 'h':
                params->hostname = optarg;
                break;
            case 'p':
                params->port = atoi(optarg);
                break;
            case 'f':
                params->filename = optarg;
                break;
            case 'm':
                params->filetype = optarg;
                break;
            case 'U':
                params->username = optarg;
                break;
            case 'P':
                params->password = optarg;
                break;
            case 'a':
                params->use_auth = true;
                break;
            case 'x':
                params->x_dimension = atoi(optarg);
                break;
            case 'y':
                params->y_dimension = atoi(optarg);
                break;
			case 't':
                params->media_tracking = optarg;
                break;

            case '?':
                if (optopt == 'h' || optopt == 'p' || optopt == 'f' || optopt == 'm' || optopt == 'U' || optopt == 'P')
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                else if (optopt == 'x' || optopt == 'y' || optopt == 't')
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                else if (isprint(optopt))
                    fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
                return false;
            default:
                return false;
        }
    }

    if (!params->hostname || !params->filename || !params->filetype) {
        fprintf(stderr, "Usage: %s -h <hostname> [-p <port>] -f <filename> -m <mime_type> [-x <xdim>] [-y <ydim>] [-t <tracking>] [-U <username> -P <password> -a]\n", argv[0]);
        fprintf(stderr, "  -h <hostname>:  Hostname or IP address of the printer (required).\n");
        fprintf(stderr, "  -p <port>:      Port number for the printer (optional, default is 631).\n");
        fprintf(stderr, "  -f <filename>:  Path to the file to print (required).\n");
        fprintf(stderr, "  -m <mime_type>: MIME type of the file (e.g., image/jpeg, application/pdf).\n");
        fprintf(stderr, "  -x <xdim>:      X dimension of the media in 1/1000 inch (optional, default is 10160).\n");
        fprintf(stderr, "  -y <ydim>:      Y dimension of the media in 1/1000 inch (optional, default is 2540).\n");
		fprintf(stderr, "  -t <tracking>:  Media Tracking (mark, continuous, gap) (optional, default is mark).\n");
        fprintf(stderr, "  -U <username>:  Username for authentication (optional).\n");
        fprintf(stderr, "  -P <password>:  Password for authentication (optional).\n");
        fprintf(stderr, "  -a:             Enable authentication (use with -U and -P).\n");
        return false;
    }

    if (params->use_auth && (!params->username || !params->password)) {
        fprintf(stderr, "Error: Authentication enabled (-a) but username (-U) and/or password (-P) are missing.\n");
        return false;
    }

    return true;
}

// --- Function to establish IPP connection ---
http_t *establish_ipp_connection(const char *hostname, int port) {
    http_t *http = httpConnect(hostname, port, NULL, AF_UNSPEC, HTTP_ENCRYPTION_ALWAYS, 1, 30000, NULL);
    if (!http) {
        fprintf(stderr, "Error: Unable to connect to printer at %s:%d: %s\n", hostname, port, cupsGetErrorString());
    }
    return http;
}

// --- Function to handle authentication ---
bool handle_authentication(http_t *http, const char *username, const char *password) {
    char credentials[CREDENTIALS_MAX];
    snprintf(credentials, sizeof(credentials), "%s:%s", username, password);
    fprintf(stderr, "%s\n", credentials);
    char *auth_string = base64Encoder(credentials, strlen(credentials));
    if (!auth_string) {
        fprintf(stderr, "Error: base64 encoding failure!\n");
        return false;
    }
    httpSetAuthString(http, "Basic", auth_string);
    free(auth_string);
    return true;
}

// --- Function to create IPP print job request ---
ipp_t *create_print_job_request(const PrintParams *params, const char *printer_uri_str) {
    ipp_t *request = ippNewRequest(IPP_OP_PRINT_JOB);
    if (!request) {
        fprintf(stderr, "Error: Could not create IPP request.\n");
        return NULL;
    }

    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_uri_str);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsGetUser());
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, params->filetype);

    // --- Constructing media-col collection - media-size ---
    ipp_t *media_col = ippNew();
    ippAddString(media_col, IPP_TAG_JOB, IPP_TAG_KEYWORD, "media-type", NULL, "labels-continuous");
    ippAddString(media_col, IPP_TAG_JOB, IPP_TAG_KEYWORD, "media-source", NULL, "main");

    ipp_t *media_size = ippNew();
    ippAddInteger(media_size, IPP_TAG_JOB, IPP_TAG_INTEGER, "x-dimension", params->x_dimension);
    ippAddInteger(media_size, IPP_TAG_JOB, IPP_TAG_INTEGER, "y-dimension", params->y_dimension);
    ippAddCollection(media_col, IPP_TAG_JOB, "media-size", media_size);
    ippDelete(media_size);

    ippAddInteger(media_col, IPP_TAG_JOB, IPP_TAG_INTEGER, "media-bottom-margin", 0);
    ippAddInteger(media_col, IPP_TAG_JOB, IPP_TAG_INTEGER, "media-left-margin", 0);
    ippAddInteger(media_col, IPP_TAG_JOB, IPP_TAG_INTEGER, "media-right-margin", 0);
    ippAddInteger(media_col, IPP_TAG_JOB, IPP_TAG_INTEGER, "media-top-margin", 0);
    ippAddInteger(media_col, IPP_TAG_JOB, IPP_TAG_INTEGER, "media-top-offset", 0);
	ippAddString(media_col, IPP_TAG_JOB, IPP_TAG_KEYWORD, "media-tracking", NULL, params->media_tracking);
    ippAddCollection(request, IPP_TAG_JOB, "media-col", media_col);
    ippDelete(media_col);
    // --- media-col construction complete ---

    ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_INTEGER, "print-darkness", 100);
    ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_INTEGER, "print-speed", 500);
    ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "print-color-mode", NULL, "monochrome");
    ippAddResolution(request, IPP_TAG_JOB, "printer-resolution", IPP_RES_PER_INCH, 203, 203);

    return request;
}

// --- Function to get printer attributes ---
ipp_t *get_printer_attributes(http_t *http, const char *printer_uri_str) {
    ipp_t *request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    if (!request) return NULL;

    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_uri_str);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsGetUser());

    const char *requested_attrs[] = {"printer-state", "printer-state-reasons"};
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_CONST_TAG(IPP_TAG_KEYWORD),
                  "requested-attributes", 2, NULL, requested_attrs);

    ipp_t *response = cupsDoRequest(http, request, "/ipp/print");
    if (!response) {
        fprintf(stderr, "Error sending Get-Printer-Attributes request: %s\n", cupsGetErrorString());
    }
    ippDelete(request); // Delete request regardless of success

    return response;
}

// --- Base64 encoding function (from printLabel.c) ---
char *base64Encoder(const char *data, size_t input_length) {
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t output_length = (size_t)(4.0 * ceil((double)input_length / 3.0));
    char *encoded_data = malloc(output_length + 1);
    if (!encoded_data) return NULL;

    size_t i, j;
    for (i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        encoded_data[j++] = base64_chars[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 0 * 6) & 0x3F];
    }

    for (int i = 0; i < (int)(3 - input_length % 3) % 3; i++) {
        encoded_data[output_length - 1 - i] = '=';
    }

    encoded_data[output_length] = '\0';
    return encoded_data;
}

// --- Function to print keyword attributes ---
void print_keyword_attribute(ipp_attribute_t *attr, const char *name) {
    if (attr != NULL) {
        int count = ippGetCount(attr);
        printf("%s:\n", name);
        for (int i = 0; i < count; i++) {
            const char *value = ippGetString(attr, i, NULL);
            if (value != NULL) {
                printf("  %s\n", value);
            } else {
                printf("  (null)\n");
            }
        }
    } else {
        printf("%s attribute not found in the response.\n", name);
    }
}

// --- Function to print enum attributes (from get-state.c) ---
void print_enum_attribute(ipp_attribute_t *attr, const char *name) {
    if (attr != NULL) {
        int count = ippGetCount(attr);
        printf("%s:\n", name);
        for (int i = 0; i < count; i++) {
            ipp_jstate_t enum_value = ippGetInteger(attr, i);
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
            //job-state
            else if (strcmp(name, "job-state") == 0)
            {
                switch (enum_value) {
                    case IPP_JSTATE_PENDING:  state_string = "Pending"; break;
                    case IPP_JSTATE_PROCESSING: state_string = "Processing"; break;
                    case IPP_JSTATE_STOPPED: state_string = "Stopped"; break;
                    case IPP_JSTATE_CANCELED: state_string = "Canceled"; break;
                    case IPP_JSTATE_ABORTED: state_string = "Aborted"; break;
                    case IPP_JSTATE_COMPLETED: state_string = "Completed"; break;
                    case IPP_JSTATE_HELD: state_string = "Held"; break;
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

// --- Function to get job attributes ---
ipp_t *get_job_attributes(http_t *http, const char *printer_uri, int job_id) {
    ipp_t *request = ippNewRequest(IPP_OP_GET_JOB_ATTRIBUTES);
    if (!request) return NULL;

    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_uri);
    ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", job_id);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsGetUser());

    const char *requested_attrs[] = {"job-state", "job-state-reasons"};
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_CONST_TAG(IPP_TAG_KEYWORD),
                  "requested-attributes", 2, NULL, requested_attrs);

    ipp_t *response = cupsDoRequest(http, request, "/ipp/print");

    if (!response) {
        fprintf(stderr, "Error sending Get-Job-Attributes request: %s\n", cupsGetErrorString());
    }

    ippDelete(request); // Delete request regardless of success
    return response;
}



