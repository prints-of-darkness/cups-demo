#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h> // For sleep() and getopt
#include <libcups3/cups/cups.h>
#include <ctype.h>
#include <stdbool.h>
#include <math.h>

char *base64Encoder(const char *data, size_t input_length);

int main(int argc, char *argv[]) {
    const char *filename = NULL;
    const char *filetype = NULL;
    const char *uri_hostname = NULL;
    const char *username = NULL;
    const char *password = NULL;
    bool use_auth = false;
    int port = 631; // Default port
    http_t *http = NULL;
    ipp_t *request = NULL, *response = NULL;
    int job_id;
    ipp_status_t status;
    char printer_uri_str[256]; // Buffer for constructing the printer URI

    // ADDED: Variables for x and y dimensions, with defaults
    int x_dimension = 10160;  // Default 4x1
    int y_dimension = 2540;   // Default 4x1
	const char *media_tracking = "mark";

    int opt;
    opterr = 0; // Disable getopt's default error printing

    // MODIFIED: Added -x and -y options to getopt
    while ((opt = getopt(argc, argv, "h:p:f:m:U:P:ax:y:t:")) != -1) {
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
            case 'U':
                username = optarg;
                break;
            case 'P':
                password = optarg;
                break;
            case 'a':
                use_auth = true;
                break;
            // ADDED: Cases for -x and -y
            case 'x':
                x_dimension = atoi(optarg);
                break;
            case 'y':
                y_dimension = atoi(optarg);
                break;
			case 't':
                media_tracking = optarg;
                break;

            case '?':
                if (optopt == 'h' || optopt == 'p' || optopt == 'f' || optopt == 'm' || optopt == 'U' || optopt == 'P')
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                // ADDED:  Error handling for -x and -y
                else if (optopt == 'x' || optopt == 'y' || optopt == 't')
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
        // MODIFIED: Updated usage message
        fprintf(stderr, "Usage: %s -h <hostname> [-p <port>] -f <filename> -m <mime_type> [-x <xdim>] [-y <ydim>] [-t <tracking>] [-u <username> -P <password> -a]\n", argv[0]);
        fprintf(stderr, "  -h <hostname>:  Hostname or IP address of the printer (required).\n");
        fprintf(stderr, "  -p <port>:      Port number for the printer (optional, default is 631).\n");
        fprintf(stderr, "  -f <filename>:  Path to the file to print (required).\n");
        fprintf(stderr, "  -m <mime_type>: MIME type of the file (e.g., image/jpeg, application/pdf) (required).\n");
        fprintf(stderr, "  -x <xdim>:      X dimension of the media in 1/1000 inch (optional, default is 10160).\n");
        fprintf(stderr, "  -y <ydim>:      Y dimension of the media in 1/1000 inch (optional, default is 2540).\n");
		fprintf(stderr, "  -t <tracking>:  Media Tracking (mark, continuous, gap) (optional, default is mark).\n");
        fprintf(stderr, "  -U <username>:  Username for authentication (optional).\n");
        fprintf(stderr, "  -P <password>:  Password for authentication (optional).\n");
        fprintf(stderr, "  -a:             Enable authentication (use with -U and -P).\n");
        return 1;
    }

    // Check if authentication is enabled but username/password are missing
    if (use_auth && (username == NULL || password == NULL)) {
        fprintf(stderr, "Error: Authentication enabled (-a) but username (-U) and/or password (-P) are missing.\n");
        return 1;
    }


    // Establish a connection to the printer
     http = httpConnect(uri_hostname, port, NULL, AF_UNSPEC, HTTP_ENCRYPTION_ALWAYS, 1, 30000, NULL);
    if (!http) {
        fprintf(stderr, "Error: Unable to connect to printer at %s:%d: %s\n", uri_hostname, port, cupsGetErrorString());
        return 1;
    }

    // Construct printer URI string
    snprintf(printer_uri_str, sizeof(printer_uri_str), "ipp://%s:%d/ipp/print", uri_hostname, port);

   // Set Basic Authentication header (for PAM AUTH)  -- ONLY IF -a is specified!
    if (use_auth) {
        char credentials[256];
        snprintf(credentials, sizeof(credentials), "%s:%s", username, password);
        char *auth_string = base64Encoder(credentials, strlen(credentials));
		if (auth_string == NULL) {
			fprintf(stderr, "base64 encoding failure!\n");
			return 1;
		}
        //printf("base64: %s\n", auth_string);
        httpSetAuthString(http, "Basic", auth_string); // Set Authorization header
        free(auth_string);
    }

    // Create a new IPP request
    request = ippNewRequest(IPP_OP_PRINT_JOB);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_uri_str);			// Printer URI (already constructed)
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsGetUser());	// Requesting User Name
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, filetype); 			// Document Format (MIME Type)

    // --- Constructing media-col collection - media-size ---
    ipp_t *media_col = ippNew();
    ippAddString(media_col, IPP_TAG_JOB, IPP_TAG_KEYWORD, "media-type", NULL, "labels-continuous");
    ippAddString(media_col, IPP_TAG_JOB, IPP_TAG_KEYWORD, "media-source", NULL, "main");

    //set size of media
    ipp_t *media_size = ippNew();
    // MODIFIED: Use the variables x_dimension and y_dimension
    ippAddInteger(media_size, IPP_TAG_JOB, IPP_TAG_INTEGER, "x-dimension", x_dimension);
    ippAddInteger(media_size, IPP_TAG_JOB, IPP_TAG_INTEGER, "y-dimension", y_dimension);
    ippAddCollection(media_col, IPP_TAG_JOB, "media-size", media_size);
    ippDelete(media_size);

    // set margins
    ippAddInteger(media_col, IPP_TAG_JOB, IPP_TAG_INTEGER, "media-bottom-margin", 0);
    ippAddInteger(media_col, IPP_TAG_JOB, IPP_TAG_INTEGER, "media-left-margin", 0);
    ippAddInteger(media_col, IPP_TAG_JOB, IPP_TAG_INTEGER, "media-right-margin", 0);
    ippAddInteger(media_col, IPP_TAG_JOB, IPP_TAG_INTEGER, "media-top-margin", 0);
    ippAddInteger(media_col, IPP_TAG_JOB, IPP_TAG_INTEGER, "media-top-offset", 0);
    ippAddString(media_col, IPP_TAG_JOB, IPP_TAG_KEYWORD, "media-tracking", NULL, media_tracking);
    ippAddCollection(request, IPP_TAG_JOB, "media-col", media_col); // Add media-col to the request
    ippDelete(media_col);
    // --- media-col construction complete ---

    ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_INTEGER, "print-darkness", 100);						// print darkness
    ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_INTEGER, "print-speed", 500);							// print speed
    ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "print-color-mode", NULL, "monochrome"); 		// Request Monochrome Printing:
    ippAddResolution(request, IPP_TAG_JOB, "printer-resolution", IPP_RES_PER_INCH, 203, 203);

    // Send the request and receive the response
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

    httpClose(http);

    return 0;
}

// Function to encode a string to Base64 (Simplified version for demonstration)
char *base64Encoder(const char *data, size_t input_length) {
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t output_length = (size_t)(4.0 * ceil((double)input_length / 3.0));
    char *encoded_data = malloc(output_length + 1);  // +1 for null terminator

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

    // Add padding if necessary
     for (int i = 0; i < (int)(3 - input_length % 3) % 3; i++) {
        encoded_data[output_length - 1 - i] = '=';
    }

    encoded_data[output_length] = '\0'; // Null-terminate
    return encoded_data;
}

