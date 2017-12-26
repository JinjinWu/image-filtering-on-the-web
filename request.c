#include "request.h"
#include "response.h"
#include <string.h>


/******************************************************************************
 * ClientState-processing functions
 *****************************************************************************/
ClientState *init_clients(int n) {
    ClientState *clients = malloc(sizeof(ClientState) * n);
    for (int i = 0; i < n; i++) {
        clients[i].sock = -1;  // -1 here indicates available entry
    }
    return clients;
}

/* 
 * Remove the client from the client array, free any memory allocated for
 * fields of the ClientState struct, and close the socket.
 */
void remove_client(ClientState *cs) {
    if (cs->reqData != NULL) {
        free(cs->reqData->method);
        free(cs->reqData->path);
        for (int i = 0; i < MAX_QUERY_PARAMS && cs->reqData->params[i].name != NULL; i++) {
            free(cs->reqData->params[i].name);
            free(cs->reqData->params[i].value);
        }
        free(cs->reqData);
        cs->reqData = NULL;
    }
    close(cs->sock);
    cs->sock = -1;
    cs->num_bytes = 0;
}


/*
 * Search the first inbuf characters of buf for a network newline ("\r\n").
 * Return the index *immediately after* the location of the '\n'
 * if the network newline is found, or -1 otherwise.
 * Definitely do not use strchr or any other string function in here. (Why not?)
 */
int find_network_newline(const char *buf, int inbuf) {

    for (int i = 0; i < inbuf - 1; i++) {
        if (*(buf + i) == '\r' && *(buf + i + 1) == '\n')
            return i + 2;
    }

    return -1;
}

/*
 * Removes one line (terminated by \r\n) from the client's buffer.
 * Update client->num_bytes accordingly.
 *
 * For example, if `client->buf` contains the string "hello\r\ngoodbye\r\nblah",
 * after calling remove_line on it, buf should contain "goodbye\r\nblah"
 * Remember that the client buffer is *not* null-terminated automatically.
 */
void remove_buffered_line(ClientState *client) {

    int where;
    where = find_network_newline(client->buf, client->num_bytes);

    if (where > 0) {
	client->num_bytes -= where;
	memmove(&(client->buf), &(client->buf) + where, client->num_bytes);
    }
}


/*
 * Read some data into the client buffer. Append new data to data already
 * in the buffer.  Update client->num_bytes accordingly.
 * Return the number of bytes read in, or -1 if the read failed.

 * Be very careful with memory here: there might be existing data in the buffer
 * that you don't want to overwrite, and you also don't want to go past
 * the end of the buffer, and you should ensure the string is null-terminated.
 */
int read_from_client(ClientState *client) {

    int numread;
    int room = MAXLINE - client->num_bytes - 1;
    char *after;
    after = client->buf + client->num_bytes;

    numread = read(client->sock, after, room);

    if (numread > 0) {
        client->num_bytes += numread;
	(client->buf)[client->num_bytes] = '\0';
	return numread;
    }

    // read failed
    return -1;
}


/*****************************************************************************
 * Parsing the start line of an HTTP request.
 ****************************************************************************/
// Helper function declarations.
void parse_query(ReqData *req, const char *str);
void update_fdata(Fdata *f, const char *str);
void fdata_free(Fdata *f);
void log_request(const ReqData *req);

/* If there is a full line (terminated by a network newline (CRLF)) 
 * then use this line to initialize client->reqData
 * Return 0 if a full line has not been read, 1 otherwise.
 */
int parse_req_start_line(ClientState *client) {

    int where;
    where = find_network_newline(client->buf, client->num_bytes);

    if (where > 0) {
	char startline_str[MAXLINE];
	memcpy(startline_str, client->buf, where);
	char *token;
	char *path;
	char *params;
	const char s1[2] = " ";
	const char s2[2] = "?";

	// gets method - either GET or POST
	token = strtok(startline_str, s1);

	// allocate memory for reqData struct
	client->reqData = malloc(sizeof(ReqData));

	// allocate memory for method attribute and initialize it
	if (strcmp(GET, token) == 0) {
	    client->reqData->method = malloc(strlen(token) + 1);
	    memcpy(client->reqData->method, token, strlen(token));
	} else if (strcmp(POST, token) == 0) {
	    client->reqData->method = malloc(strlen(token) + 1);
	    memcpy(client->reqData->method, token, strlen(token));
	}

	// get entire target - with path and params
	token = strtok(NULL, s1);

	// get path, allocate memory, and initialize
	path = strtok(token, s2);
	client->reqData->path = malloc(strlen(path) + 1);
	memcpy(client->reqData->path, path, strlen(path));

	// get string of params
	params = strtok(NULL, s2);

	// parse string of params and store them in array of Fdatas
	if (params != NULL) {
	    parse_query(client->reqData, params);
	}

	// finished parsing full line
	remove_buffered_line(client);
	log_request(client->reqData);
	return 1;

    }

    return 0;
}


/*
 * Initializes req->params from the key-value pairs contained in the given 
 * string.
 * Assumes that the string is the part after the '?' in the HTTP request target,
 * e.g., name1=value1&name2=value2.
 */
void parse_query(ReqData *req, const char *str) {

    int num = 0;
    int i;
    int len = 0;

    for (i = 0; i < strlen(str); i++) {
	// initialize name param
	if (str[i] == '=') {
	    (req->params)[num].name = malloc(len + 1);
	    memcpy((req->params)[num].name, &(str[i - len]), len);
	    ((req->params)[num].name)[len] = '\0';
	    len = 0;
	// initialize value param
	} else if (str[i] == '&') {
	    (req->params)[num].value = malloc(len + 1);
	    memcpy((req->params)[num].value, &(str[i - len]), len);
	    ((req->params)[num].value)[len] = '\0';
	    len = 0;
	    num++;
	} else {
	    len++;
	}

	if (num == MAX_QUERY_PARAMS) {
	    break;
	}
    }

    (req->params)[num].value = malloc(len + 1);
    memcpy((req->params)[num].value, &(str[i - len]), len);
    ((req->params)[num].value)[len] = '\0';

}




/*
 * Print information stored in the given request data to stderr.
 */
void log_request(const ReqData *req) {
    fprintf(stderr, "Request parsed: [%s] [%s]\n", req->method, req->path);
    for (int i = 0; i < MAX_QUERY_PARAMS && req->params[i].name != NULL; i++) {
        fprintf(stderr, "  %s -> %s\n", 
                req->params[i].name, req->params[i].value);
    }
}


/******************************************************************************
 * Parsing multipart form data (image-upload)
 *****************************************************************************/

char *get_boundary(ClientState *client) {
    int len_header = strlen(POST_BOUNDARY_HEADER);

    while (1) {
        int where = find_network_newline(client->buf, client->num_bytes);
        if (where > 0) {
            if (where < len_header || strncmp(POST_BOUNDARY_HEADER, client->buf, len_header) != 0) {
                remove_buffered_line(client);
            } else {
                // We've found the boundary string!
                // We are going to add "--" to the beginning to make it easier
                // to match the boundary line later
                char *boundary = malloc(where - len_header + 1);
                strncpy(boundary, "--", where - len_header + 1);
                strncat(boundary, client->buf + len_header, where - len_header - 1);
                boundary[where - len_header] = '\0';
                return boundary;
            }
        } else {
            // Need to read more bytes
            if (read_from_client(client) <= 0) {
                // Couldn't read; this is a bad request, so give up.
                return NULL;
            }
        }
    }
    return NULL;
}


char *get_bitmap_filename(ClientState *client, const char *boundary) {
    int len_boundary = strlen(boundary);

    // Read until finding the boundary string.
    while (1) {
        int where = find_network_newline(client->buf, client->num_bytes);
        if (where > 0) {
            if (where < len_boundary + 2 ||
                    strncmp(boundary, client->buf, len_boundary) != 0) {
                remove_buffered_line(client);
            } else {
                // We've found the line with the boundary!
                remove_buffered_line(client);
                break;
            }
        } else {
            // Need to read more bytes
            if (read_from_client(client) <= 0) {
                // Couldn't read; this is a bad request, so give up.
                return NULL;
            }
        }
    }

    int where = find_network_newline(client->buf, client->num_bytes);

    client->buf[where-1] = '\0';  // Used for strrchr to work on just the single line.
    char *raw_filename = strrchr(client->buf, '=') + 2;
    int len_filename = client->buf + where - 3 - raw_filename;
    char *filename = malloc(len_filename + 1);
    strncpy(filename, raw_filename, len_filename);
    filename[len_filename] = '\0';

    // Restore client->buf
    client->buf[where - 1] = '\n';
    remove_buffered_line(client);
    return filename;
}

/*
 * Read the file data from the socket and write it to the file descriptor
 * file_fd.
 * You know when you have reached the end of the file in one of two ways:
 *    - search for the boundary string in each chunk of data read 
 * (Remember the "\r\n" that comes before the boundary string, and the 
 * "--\r\n" that comes after.)
 *    - extract the file size from the bitmap data, and use that to determine
 * how many bytes to read from the socket and write to the file
 */
int save_file_upload(ClientState *client, const char *boundary, int file_fd) {
    // Read in the next two lines: Content-Type line, and empty line
    remove_buffered_line(client);
    remove_buffered_line(client);

    // create ending boundary string
    char end[6 + strlen(boundary)];
    strncpy(end, "\r\n", 2);
    strncat(end, boundary, strlen(boundary));
    strncat(end, "-\r\n\0", 4);

    // loop while we haven't reached end of bmp data
    while (strncmp(client->buf, end, strlen(end)) != 0) {
	if (strlen(client->buf) == MAXLINE) {
	    write(file_fd, client->buf, MAXLINE);
	    memset(client->buf, '\0', MAXLINE);
	}
	read_from_client(client);
    }

    return 0;
}
